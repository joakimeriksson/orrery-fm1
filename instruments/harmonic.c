/* SPDX-License-Identifier: GPL-3.0-only */
/* Version1 payload: sound (0..7), level (0..200 percent). See harmonic.h. A voice is two wave generators into a
 * span's buffer, then the buffer into the two channels; the envelopes move every HM_CTL samples, counted in the
 * voice, so what is heard does not depend on how a block is cut into spans. */
#include "harmonic.h"
#include "harmonic_tables.h"
#include "core/hot.h"
_Static_assert(HM_SOUNDS == MD_HARMONIC_SOUNDS, "the table is the sounds");
#define ONE (1 << 30)
#define SILENT (ONE >> 13)                      /* 78 dB down: a released generator stops here */
static const uint32_t rates[]={MD_SAMPLE_RATE};
static const char *const NAMES[MD_HARMONIC_SOUNDS]={"HARMONIC PLUCK","HOLLOW PLUCK","GLASS BELLS","PIPES","BRIGHT REED",
    "SOFT FLUTE","OCTAVE CHIME","NASAL LEAD"};
static const uint8_t preset_bytes[MD_HARMONIC_SOUNDS][MD_HARMONIC_SETTINGS_BYTES]={
    {0,100},{1,100},{2,100},{3,100},{4,100},{5,100},{6,100},{7,100}};
static const md_preset_t presets[]={
    {1,"HARMONIC PLUCK",preset_bytes[0],2},{2,"HOLLOW PLUCK",preset_bytes[1],2},{3,"GLASS BELLS",preset_bytes[2],2},
    {4,"PIPES",preset_bytes[3],2},{5,"BRIGHT REED",preset_bytes[4],2},{6,"SOFT FLUTE",preset_bytes[5],2},
    {7,"OCTAVE CHIME",preset_bytes[6],2},{8,"NASAL LEAD",preset_bytes[7],2}
};
static const md_param_desc_t params[]={
    {.id=MD_HARMONIC_SOUND,.flags=MD_PARAM_ENUM,.name="SOUND",.min=0,.max=MD_HARMONIC_SOUNDS-1,.initial=0,
     .unit=MD_UNIT_INTEGER,.labels=NAMES,.label_count=MD_HARMONIC_SOUNDS},
    {.id=MD_HARMONIC_LEVEL,.flags=MD_PARAM_AUTOMATABLE,.name="LEVEL",.min=0,.max=200,.initial=100,.unit=MD_UNIT_PERCENT}
};
const char *md_harmonic_name(uint32_t sound){return sound<MD_HARMONIC_SOUNDS ? NAMES[sound] : "";}
static int validate(const uint8_t *src,uint32_t bytes)
{
    if(!src)return MD_ERR_ARGUMENT;
    if(bytes!=MD_HARMONIC_SETTINGS_BYTES)return MD_ERR_FORMAT;
    return src[0]>=MD_HARMONIC_SOUNDS || src[1]>200u ? MD_ERR_RANGE : MD_OK;
}
static int apply(void *state,const uint8_t *src,uint32_t bytes)
{
    if(!state)return MD_ERR_ARGUMENT;
    int result=validate(src,bytes);if(result)return result;
    md_harmonic_state_t *s=state;
    s->sound=src[0];s->level=src[1];
    return MD_OK;
}
static int encode(const void *state,uint8_t *dst,uint32_t capacity)
{
    int result=md_adapter_encode_check(state,dst,capacity,MD_HARMONIC_SETTINGS_BYTES);if(result)return result;
    const md_harmonic_state_t *s=state;
    dst[0]=s->sound;dst[1]=s->level;
    return MD_HARMONIC_SETTINGS_BYTES;
}
static void reset(void *state)
{
    md_harmonic_state_t *s=state;
    for(uint32_t i=0;i<MD_HARMONIC_VOICES;i++)s->voice[i]=(md_harmonic_voice_t){0};
}
static int init(void *state,uint32_t rate)
{
    if(!state)return MD_ERR_ARGUMENT;
    if(rate!=MD_SAMPLE_RATE)return MD_ERR_UNSUPPORTED;
    md_harmonic_state_t *s=state;
    reset(s);s->sound=0;s->level=100;s->count=0;
    return MD_OK;
}
static int get_param(const void *state,md_param_id_t id,int32_t *value)
{
    if(!state || !value)return MD_ERR_ARGUMENT;
    const md_harmonic_state_t *s=state;
    if(id==MD_HARMONIC_SOUND)*value=s->sound;
    else if(id==MD_HARMONIC_LEVEL)*value=s->level;
    else return MD_ERR_NOT_FOUND;
    return MD_OK;
}
/* a generator's wave for a step: sixteen harmonics while the sixteenth stays under 20 kHz, then eight, then four */
static void fit(md_harmonic_voice_t *v,uint32_t g,const hm_sound_t *q)
{
    uint32_t step=v->step[g];                    /* 2^32 a cycle: a step of 2^32 / 35.3 is 1250 Hz */
    if(step<121700000u){v->wave[g]=q->wave[g];v->bits[g]=8;}
    else if(step<243400000u){v->wave[g]=q->wave[g]+257;v->bits[g]=7;}
    else{v->wave[g]=q->wave[g]+257+129;v->bits[g]=6;}
}
static void start(md_harmonic_state_t *s,md_note_token_t token,uint32_t key,uint32_t velocity)
{
    const hm_sound_t *q=&HM_SOUND[s->sound];
    md_harmonic_voice_t *v=0;
    uint32_t i;
    int32_t second=(int32_t)key+q->semitones;
    /* a silent voice; or the quietest of those let go; or the oldest */
    for(i=0;i<MD_HARMONIC_VOICES && !v;i++)if(!s->voice[i].stage[0] && !s->voice[i].stage[1])v=&s->voice[i];
    for(i=0;i<MD_HARMONIC_VOICES && !(v && (!v->stage[0] && !v->stage[1]));i++){
        md_harmonic_voice_t *x=&s->voice[i];
        if(!x->token && (!v || x->env[0]/2+x->env[1]/2<v->env[0]/2+v->env[1]/2))v=x;
    }
    if(!v)for(v=&s->voice[0],i=1;i<MD_HARMONIC_VOICES;i++)if(s->voice[i].born<v->born)v=&s->voice[i];
    uint32_t place=(uint32_t)(v-s->voice);
    /* a taken voice starts from where it was: no step in the level */
    v->token=token;v->key=(uint8_t)key;v->sound=s->sound;v->born=++s->count;v->left=0;
    if(second>127)second=127;
    if(second<0)second=0;
    v->step[0]=HM_STEP[key];
    v->step[1]=HM_STEP[second]+(uint32_t)(int32_t)(((int64_t)HM_STEP[second]*q->detune)>>20);
    for(uint32_t g=0;g<2u;g++){
        if(!v->stage[g]){v->phase[g]=0;v->env[g]=0;}
        v->goal[g]=v->env[g];v->slope[g]=0;v->stage[g]=1;
        fit(v,g,q);
    }
    /* velocity: 40 % at the softest to all of it; the voices a little apart, left and right in turn */
    int32_t g12=(int32_t)(1638u+velocity*2458u/127u),side=(int32_t)(place&1u ? 30 : -30)*(int32_t)(1u+place/2u)/3;
    v->gain[0]=(int16_t)(g12*(100-side)/100);
    v->gain[1]=(int16_t)(g12*(100+side)/100);
}
static int event(void *state,const md_event_t *ev)
{
    if(!state || !ev)return MD_ERR_ARGUMENT;
    md_harmonic_state_t *s=state;
    if(ev->kind==MD_EVENT_PARAM){
        if(ev->flags)return MD_ERR_UNSUPPORTED;
        uint32_t id=ev->data.param.id;int32_t v=ev->data.param.value;
        if(id<1u || id>sizeof params/sizeof params[0])return MD_ERR_NOT_FOUND;
        if(v<params[id-1u].min || v>params[id-1u].max)return MD_ERR_RANGE;
        if(id==MD_HARMONIC_SOUND)s->sound=(uint8_t)v;   /* the next note's: what sounds goes on as it is */
        else s->level=(uint8_t)v;
        return MD_OK;
    }
    if(ev->kind==MD_EVENT_PANIC){reset(s);return MD_OK;}
    if(ev->kind!=MD_EVENT_NOTE_ON && ev->kind!=MD_EVENT_NOTE_OFF)return MD_ERR_UNSUPPORTED;
    int result=md_adapter_note_check(ev);if(result)return result;
    for(uint32_t i=0;i<MD_HARMONIC_VOICES;i++)
        if(s->voice[i].token==ev->token){
            if(ev->kind==MD_EVENT_NOTE_ON)return MD_ERR_STATE;
            s->voice[i].token=0;
            for(uint32_t g=0;g<2u;g++)if(s->voice[i].stage[g])s->voice[i].stage[g]=3;
            return MD_OK;
        }
    if(ev->kind==MD_EVENT_NOTE_ON)start(s,ev->token,ev->data.note.key,ev->data.note.velocity);
    return MD_OK;                                /* (an off for a voice already taken: nothing to do) */
}
/* a control tick: where each generator's level goes over the next HM_CTL samples */
static void tick(md_harmonic_voice_t *v)
{
    const hm_sound_t *q=&HM_SOUND[v->sound];
    for(uint32_t g=0;g<2u;g++){
        int32_t e=v->goal[g],next=e;
        v->env[g]=e;
        switch(v->stage[g]){
        case 1:
            next=q->attack[g]>=(uint32_t)(ONE-e) ? ONE : e+(int32_t)q->attack[g];
            if(next==ONE)v->stage[g]=2;
            break;
        case 2:
            next=q->sustain[g]+(int32_t)(((int64_t)(e-q->sustain[g])*q->decay[g])>>32);
            if(!q->sustain[g] && next<SILENT){next=0;v->stage[g]=0;}
            break;
        case 3:
            next=(int32_t)(((int64_t)e*q->release[g])>>32);
            if(next<SILENT){next=0;v->stage[g]=0;}
            break;
        default:next=0;break;
        }
        v->goal[g]=next;
        v->slope[g]=(next-e)/(int32_t)HM_CTL;
    }
    v->left=HM_CTL;
}
/* n samples of a generator into buf (set, or added to what the other left there) */
MD_INLINE void generate(md_harmonic_voice_t *v,uint32_t g,int32_t *buf,uint32_t n,uint32_t add)
{
    const int16_t *w=v->wave[g];
    uint32_t phase=v->phase[g],step=v->step[g],shift=32u-v->bits[g];
    int32_t env=v->env[g],slope=v->slope[g];
    for(uint32_t i=0;i<n;i++){
        uint32_t at=phase>>shift;
        int32_t part=(int32_t)((phase<<v->bits[g])>>17);            /* between two points, 15 bits */
        int32_t a=w[at],x=a+(((w[at+1u]-a)*part)>>15);
        env+=slope;
        x=(x*(env>>15))>>15;
        buf[i]=add ? buf[i]+x : x;
        phase+=step;
    }
    v->phase[g]=phase;v->env[g]=env;
}
static void render(void *state,int32_t *out,uint32_t frames)
{
    md_harmonic_state_t *s=state;
    int32_t level=(int32_t)s->level*41;          /* percent as Q12 */
    for(uint32_t k=0;k<MD_HARMONIC_VOICES;k++){
        md_harmonic_voice_t *v=&s->voice[k];
        int32_t *o=out;
        uint32_t todo=frames;
        if(!v->stage[0] && !v->stage[1] && !v->left)continue;
        int32_t gl=(v->gain[0]*level)>>12,gr=(v->gain[1]*level)>>12;
        while(todo){
            int32_t buf[HM_CTL];
            if(!v->left){
                if(!v->stage[0] && !v->stage[1]){v->env[0]=v->env[1]=0;break;}   /* its last tick is played */
                tick(v);
            }
            uint32_t n=todo<v->left ? todo : v->left;
            /* (a generator that has died away, a pluck's bright half, is not drawn: nothing moves in it) */
            if(v->env[0] | v->slope[0])generate(v,0,buf,n,0);
            else for(uint32_t i=0;i<n;i++)buf[i]=0;
            if(v->env[1] | v->slope[1])generate(v,1,buf,n,1);
            for(uint32_t i=0;i<n;i++){o[2u*i]+=(buf[i]*gl)>>13;o[2u*i+1u]+=(buf[i]*gr)>>13;}
            o+=2u*n;todo-=n;v->left=(uint8_t)(v->left-n);
        }
    }
}
const md_instrument_desc_t MD_HARMONIC={
    .processor={.id=MD_INSTRUMENT_HARMONIC,.api_version=MD_API_VERSION,.settings_version=MD_HARMONIC_SETTINGS_VERSION,
        .name="HARMONIC",.state_bytes=sizeof(md_harmonic_state_t),.state_alignment=_Alignof(md_harmonic_state_t),
        .sample_rates=rates,.sample_rate_count=1,.params=params,.param_count=sizeof params/sizeof params[0],
        .max_settings_bytes=MD_HARMONIC_SETTINGS_BYTES},
    .capabilities=MD_CAP_POLY|MD_CAP_VELOCITY,.max_polyphony=MD_HARMONIC_VOICES,.presets=presets,
    .preset_count=MD_HARMONIC_SOUNDS,
    .init=init,.event=event,.get_param=get_param,.render_add=render,.reset=reset,.encode=encode,.validate=validate,.apply=apply
};
