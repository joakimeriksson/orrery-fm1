/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c core/router.c instruments/electronic_fx.c
#include "instruments/electronic_fx.h"
#include "core/router.h"
#include "test.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static md_event_t note(md_event_kind_t kind,uint32_t token,uint8_t key,uint8_t velocity)
{
    md_event_t e={.kind=kind,.source=MD_SOURCE_PANEL,.token=token};
    if(kind==MD_EVENT_NOTE_ON){e.data.note.key=key;e.data.note.velocity=velocity;}return e;
}
static md_event_t hit(uint16_t recipe)
{
    md_event_t e={.kind=MD_EVENT_HIT,.source=0};e.data.hit.voice=recipe;e.data.hit.velocity=100;return e;
}
static int setting(md_instrument_t *i,uint16_t id,int32_t value)
{
    md_event_t e={.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};e.data.param.id=id;e.data.param.value=value;
    return md_instrument_event(i,&e);
}
static int init(md_instrument_t *i,md_electronic_fx_state_t *s)
{return md_instrument_init(i,&MD_ELECTRONIC_FX,s,sizeof *s,MD_SAMPLE_RATE);}
static int metadata_and_settings(void)
{
    const md_instrument_desc_t *items[]={&MD_ELECTRONIC_FX};md_instrument_registry_t r={items,1};
    CHECK(md_instrument_registry_validate(&r)==MD_OK && MD_ELECTRONIC_FX.preset_count==MD_FX_RECIPES && MD_ELECTRONIC_FX.drum_voice_count==MD_FX_RECIPES && MD_FX_RECIPES==13);
    md_electronic_fx_state_t s,before;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
    uint8_t wire[19];
    for(uint32_t p=0;p<MD_FX_RECIPES;p++) {
        const md_preset_t *preset=&MD_ELECTRONIC_FX.presets[p];
        CHECK(md_sound_apply(&i,2,preset->settings,preset->settings_bytes)==MD_OK);
        CHECK(preset->settings_bytes==19 && md_sound_encode(&i,wire,sizeof wire)==19 && !memcmp(wire,preset->settings,19));
        const int32_t expected[]={wire[0],wire[1],wire[2]+256*wire[3],wire[4]<=127 ? wire[4]:(int32_t)wire[4]-256,
            wire[5]+256*wire[6],wire[7],wire[8],wire[9],wire[10],wire[11],wire[16],wire[17],0 /* KIT: not a setting */,wire[18]};
        for(uint32_t parameter=0;parameter<MD_ELECTRONIC_FX.processor.param_count;parameter++) {
            int32_t current=-999;CHECK(md_instrument_param_get(&i,MD_ELECTRONIC_FX.processor.params[parameter].id,&current)==MD_OK);
            CHECK(current==expected[parameter]);
        }
    }
    for(uint32_t parameter=0;parameter<MD_ELECTRONIC_FX.processor.param_count;parameter++)for(uint32_t bound=0;bound<2;bound++) {
        const md_param_desc_t *param=&MD_ELECTRONIC_FX.processor.params[parameter];
        int32_t expected=bound ? param->max : param->min;
        CHECK(setting(&i,param->id,expected)==MD_OK);before=s;
        int32_t current=-999;CHECK(md_instrument_param_get(&i,param->id,&current)==MD_OK && current==expected);
        CHECK(!memcmp(&s,&before,sizeof s));
    }
    before=s;wire[0]=255;CHECK(md_sound_apply(&i,2,wire,18)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    wire[0]=0;wire[16]=101;CHECK(md_sound_apply(&i,2,wire,18)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    wire[16]=0;CHECK(md_sound_apply(&i,2,wire,17)==MD_ERR_FORMAT && !memcmp(&s,&before,sizeof s));
    wire[18]=101;CHECK(md_sound_apply(&i,2,wire,19)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    wire[18]=0;
    {   /* the 18-byte form loads in the middle */
        md_electronic_fx_state_t t;md_instrument_t j;CHECK(init(&j,&t)==MD_OK);
        CHECK(setting(&j,MD_FX_SPREAD,70)==MD_OK && md_sound_apply(&j,2,MD_ELECTRONIC_FX.presets[1].settings,18)==MD_OK);
        int32_t w=-1;CHECK(md_instrument_param_get(&j,MD_FX_SPREAD,&w)==MD_OK && w==0);
    }
    CHECK(md_sound_apply(&i,1,wire,16)==MD_ERR_VERSION && !memcmp(&s,&before,sizeof s));   /* v1 is not loaded */
    CHECK(md_sound_apply(&i,99,wire,16)==MD_ERR_VERSION && !memcmp(&s,&before,sizeof s));
    {   /* the 16-byte form loads with the wind off */
        md_electronic_fx_state_t t;md_instrument_t j;CHECK(init(&j,&t)==MD_OK);
        CHECK(setting(&j,MD_FX_WIND,70)==MD_OK && md_sound_apply(&j,2,MD_ELECTRONIC_FX.presets[1].settings,16)==MD_OK);
        int32_t w=-1;CHECK(md_instrument_param_get(&j,MD_FX_WIND,&w)==MD_OK && w==0);
    }
    CHECK(setting(&i,MD_FX_RESONANCE,91)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    md_event_t e=note(MD_EVENT_NOTE_ON,1,60,100);e.flags=MD_NOTE_SLIDE;
    CHECK(md_instrument_event(&i,&e)==MD_ERR_UNSUPPORTED && !memcmp(&s,&before,sizeof s));
    return 0;
}
static int held_owners_and_transient_stealing(void)
{
    md_electronic_fx_state_t s,before;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
    md_instrument_t *tracks[MD_TRACKS]={&i};md_router_t router;
    CHECK(md_router_init(&router,tracks)==MD_OK);
    for(uint32_t n=0;n<MD_ELECTRONIC_FX_VOICES;n++) {
        md_event_t e=note(MD_EVENT_NOTE_ON,n+1u,(uint8_t)(48+n),100);CHECK(md_router_event(&router,&e)==MD_OK);
    }
    before=s;md_event_t h=hit(MD_FX_PING);
    CHECK(md_router_event(&router,&h)==MD_ERR_CAPACITY && !memcmp(&s,&before,sizeof s));
    CHECK(md_router_owned_count(&router)==8 && !router.backend_errors && router.rejected_notes==1);
    md_event_t off=note(MD_EVENT_NOTE_OFF,1,0,0);CHECK(md_router_event(&router,&off)==MD_OK);
    for(uint32_t n=0;n<100;n++){h=hit((uint16_t)(n%MD_FX_RECIPES));CHECK(md_router_event(&router,&h)==MD_OK);}
    CHECK(md_router_owned_count(&router)==7 && !router.backend_errors);
    for(uint32_t v=1;v<8;v++)CHECK(s.voices[v].held && s.voices[v].token==v+1u);
    off=note(MD_EVENT_NOTE_OFF,1,0,0);CHECK(md_router_event(&router,&off)==MD_OK && md_router_owned_count(&router)==7);
    md_event_t panic={.kind=MD_EVENT_PANIC,.track=MD_ALL_TRACKS,.source=MD_SOURCE_NONE};
    CHECK(md_router_event(&router,&panic)==MD_OK && !md_router_owned_count(&router));
    for(uint32_t v=0;v<8;v++)CHECK(!s.voices[v].active && !s.voices[v].held);
    return 0;
}
static int render_sequence(md_instrument_t *i,uint32_t partition,int32_t *output,uint32_t frames)
{
    static const uint32_t at[]={0,17,50,90,111,256,512,1700,2048};uint32_t event=0,pos=0;
    while(pos<frames) {
        while(event<9 && at[event]==pos) {
            md_event_t e;
            if(event==1 || event==7)e=note(MD_EVENT_NOTE_ON,event+1u,(uint8_t)(60+event),100);
            else if(event==3 || event==8)e=note(MD_EVENT_NOTE_OFF,event==3 ? 2u:8u,0,0);
            else e=hit((uint16_t)(event*3u%MD_FX_RECIPES));   /* DROP CHIRP WHISTLE THUNDER BUBBLE ZAP */
            CHECK(md_instrument_event(i,&e)==MD_OK);event++;
        }
        uint32_t span=partition ? partition : 1u+((pos*1103515245u+12345u)>>24);
        if(event<9 && span>at[event]-pos)span=at[event]-pos;
        if(span>frames-pos)span=frames-pos;
        CHECK(span>0);md_instrument_render_add(i,output+2u*pos,span);pos+=span;
    }
    return 0;
}
static int independent_rng_and_partitions(void)
{
    const uint32_t partitions[]={1,17,64,255,256,0};
    static int32_t reference[8192],candidate[8192];
    for(uint32_t p=0;p<6;p++) {
        md_electronic_fx_state_t a,b,before;md_instrument_t first,second;
        CHECK(init(&first,&a)==MD_OK && init(&second,&b)==MD_OK);
        CHECK(setting(&first,MD_FX_RANDOM,100)==MD_OK && setting(&second,MD_FX_RANDOM,100)==MD_OK);
        before=b;md_event_t h=hit(MD_FX_PING);CHECK(md_instrument_event(&first,&h)==MD_OK);
        int32_t scratch[512]={0};md_instrument_render_add(&first,scratch,256);CHECK(!memcmp(&b,&before,sizeof b));
        md_instrument_reset(&first);memset(reference,0,sizeof reference);memset(candidate,0,sizeof candidate);
        CHECK(!render_sequence(&first,256,reference,4096));
        CHECK(!render_sequence(&second,partitions[p],candidate,4096));
        CHECK(!memcmp(reference,candidate,sizeof reference) && !memcmp(&a,&b,sizeof a));
    }
    return 0;
}
static uint32_t active(const md_electronic_fx_state_t *s)
{
    uint32_t n=0;for(uint32_t v=0;v<8;v++)n+=s->voices[v].active;return n;
}
static int presets_decay_and_artifacts(void)
{
    const char *directory=getenv("MD_FX_RENDER_DIR");
    for(uint32_t p=0;p<MD_FX_RECIPES;p++) {
        md_electronic_fx_state_t state;md_instrument_t i;CHECK(init(&i,&state)==MD_OK);
        CHECK(md_sound_apply(&i,2,MD_ELECTRONIC_FX.presets[p].settings,19)==MD_OK);
        md_event_t h=hit((uint16_t)p);CHECK(md_instrument_event(&i,&h)==MD_OK);
        uint64_t energy=0;uint32_t peak=0;FILE *raw=NULL;
        if(directory){char name[512];int n=snprintf(name,sizeof name,"%s/%s.s16le",directory,MD_ELECTRONIC_FX.presets[p].name);CHECK(n>0 && (size_t)n<sizeof name);raw=fopen(name,"wb");CHECK(raw);}
        for(uint32_t frame=0;frame<MD_SAMPLE_RATE*9u;) {
            int32_t audio[514]={0};audio[512]=123;audio[513]=-456;
            uint32_t n=MD_SAMPLE_RATE*9u-frame;if(n>256)n=256;
            md_instrument_render_add(&i,audio,n);CHECK(audio[512]==123 && audio[513]==-456);
            for(uint32_t k=0;k<2u*n;k++) {
                uint32_t value=(uint32_t)(audio[k]<0 ? -audio[k]:audio[k]);if(value>peak)peak=value;
                energy+=(uint64_t)value*value;
                if(raw && frame+k/2u<MD_SAMPLE_RATE*3u){int16_t sample=(int16_t)audio[k];uint8_t bytes[]={(uint8_t)sample,(uint8_t)((uint16_t)sample>>8)};CHECK(fwrite(bytes,1,2,raw)==2);}
            }
            frame+=n;
        }
        if(raw)CHECK(!fclose(raw));
        CHECK(energy>0 && peak<32768 && !active(&state));
        int32_t silent[512]={0};md_instrument_render_add(&i,silent,256);
        for(uint32_t k=0;k<512;k++)CHECK(!silent[k]);
        printf("VOICE %s peak=%u energy=%llu final_active=%u\n",MD_ELECTRONIC_FX.presets[p].name,peak,(unsigned long long)energy,active(&state));
    }
    return 0;
}
static int corner_parameters_and_snapshot(void)
{
    md_electronic_fx_state_t s,before;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
    CHECK(setting(&i,MD_FX_DECAY_MS,4000)==MD_OK && setting(&i,MD_FX_RESONANCE,90)==MD_OK);
    CHECK(setting(&i,MD_FX_MOD_DEPTH,100)==MD_OK && setting(&i,MD_FX_LEVEL,100)==MD_OK);
    CHECK(setting(&i,MD_FX_SWEEP_MS,5)==MD_OK);
    for(uint32_t round=0;round<2;round++) {
    if(round)CHECK(setting(&i,MD_FX_SPREAD,100)==MD_OK && setting(&i,MD_FX_SWEEP_MS,4000)==MD_OK);
    for(uint32_t k=0;k<8;k++){md_event_t h=hit((uint16_t)(round ? 5u+k : k%5u));CHECK(md_instrument_event(&i,&h)==MD_OK);}
    before=s;CHECK(setting(&i,MD_FX_TONE,0)==MD_OK);
    CHECK(!memcmp(s.voices,before.voices,sizeof s.voices)); /* notes retain trigger-time sound */
    for(uint32_t frame=0;frame<MD_SAMPLE_RATE*9u;frame+=256) {
        int32_t out[512]={0};md_instrument_render_add(&i,out,256);
        for(uint32_t k=0;k<512;k++)CHECK(out[k]>=-8*32767 && out[k]<=8*32767);
    }
    CHECK(!active(&s));
    }
    return 0;
}
/* the wind: it swells to its level over about a second, takes no voice, sits in its band, and fades to exact
 * silence; renders are identical however the block is cut */
static double rms_of(md_instrument_t *i,uint32_t frames,uint32_t span)
{
    double sum=0;
    for(uint32_t done=0;done<frames;) {
        uint32_t n=frames-done<span ? frames-done : span;int32_t out[512]={0};
        md_instrument_render_add(i,out,n);
        for(uint32_t k=0;k<n;k++){CHECK(out[2u*k]==out[2u*k+1u] && out[2u*k]>=-32767 && out[2u*k]<=32767);sum+=(double)out[2u*k]*out[2u*k];}
        done+=n;
    }
    return sqrt(sum/frames);
}
static int wind_layer(void)
{
    md_electronic_fx_state_t s;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
    CHECK(rms_of(&i,4410,256)==0.0);
    CHECK(setting(&i,MD_FX_WIND,80)==MD_OK && setting(&i,MD_FX_WIND_COLOUR,40)==MD_OK);
    double first=rms_of(&i,4410,256),later=0;
    for(uint32_t k=0;k<20;k++)later=rms_of(&i,4410,256);
    printf("  wind: RMS %.0f in the first 0.1 s, %.0f after 2 s\n",first,later);
    CHECK(first<later*0.3 && later>2000.0 && later<20000.0);
    CHECK(!active(&s));                                   /* no voice taken */
    CHECK(setting(&i,MD_FX_WIND,0)==MD_OK);
    for(uint32_t k=0;k<120;k++)(void)rms_of(&i,4410,256);   /* 12 s */
    CHECK(rms_of(&i,4410,256)==0.0 && s.wind_gain==0);
    /* the lowest level is reached exactly, rising and falling */
    CHECK(setting(&i,MD_FX_WIND,1)==MD_OK);
    for(uint32_t k=0;k<120;k++)(void)rms_of(&i,4410,256);
    CHECK(s.wind_gain==327*256);
    CHECK(setting(&i,MD_FX_WIND,0)==MD_OK);
    for(uint32_t k=0;k<120;k++)(void)rms_of(&i,4410,256);
    CHECK(s.wind_gain==0);
    /* partitions: two instances, same settings, one rendered in odd spans */
    md_electronic_fx_state_t a,b;md_instrument_t ia,ib;CHECK(init(&ia,&a)==MD_OK && init(&ib,&b)==MD_OK);
    CHECK(setting(&ia,MD_FX_WIND,100)==MD_OK && setting(&ib,MD_FX_WIND,100)==MD_OK);
    for(uint32_t k=0;k<200;k++){
        int32_t x[512]={0},y[512]={0};
        md_instrument_render_add(&ia,x,256);
        md_instrument_render_add(&ib,y,37);md_instrument_render_add(&ib,y+74,219);
        CHECK(!memcmp(x,y,sizeof x));
    }
    return 0;
}
/* KIT: a NOTE_ON is a one-shot of the kit's recipe with that recipe's own macros at the note's key: it takes no
 * token, rings past its note-off, sounds unlike the base sound, and off again the notes are the base sound's */
static int kit_sound(void)
{
    md_electronic_fx_state_t s;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
    static int32_t base[2u*22050u],kit[2u*22050u];
    CHECK(md_sound_apply(&i,2,MD_ELECTRONIC_FX.presets[MD_FX_PING].settings,18)==MD_OK);
    md_event_t on=note(MD_EVENT_NOTE_ON,1,72,100),off=note(MD_EVENT_NOTE_OFF,1,0,0);
    CHECK(md_instrument_event(&i,&on)==MD_OK && s.voices[0].held && s.voices[0].recipe==MD_FX_PING);
    memset(base,0,sizeof base);for(uint32_t at=0;at<22050u;at+=210u)md_instrument_render_add(&i,base+2u*at,210);
    md_event_t panic={.kind=MD_EVENT_PANIC,.track=MD_ALL_TRACKS,.source=MD_SOURCE_NONE};
    CHECK(md_instrument_event(&i,&panic)==MD_OK);
    CHECK(setting(&i,MD_FX_KIT,MD_FX_RECIPES+1)==MD_ERR_RANGE && setting(&i,MD_FX_KIT,1+MD_FX_METAL)==MD_OK);
    int32_t value=-1;CHECK(md_instrument_param_get(&i,MD_FX_KIT,&value)==MD_OK && value==1+MD_FX_METAL);
    CHECK(md_instrument_event(&i,&on)==MD_OK);
    CHECK(s.voices[0].active && !s.voices[0].held && !s.voices[0].token && s.voices[0].recipe==MD_FX_METAL);
    CHECK(s.voices[0].mod_depth==85 && s.settings.mod_depth==0 && s.settings.recipe==MD_FX_PING);   /* METAL's own; the sound's untouched */
    memset(kit,0,sizeof kit);
    for(uint32_t at=0;at<22050u;at+=210u) {
        if(at==2100u)CHECK(md_instrument_event(&i,&off)==MD_OK);   /* 48 ms on: nothing to release */
        md_instrument_render_add(&i,kit+2u*at,210);
    }
    double late=0,differ=0;
    for(uint32_t n=2u*13230u;n<2u*22050u;n++)late+=(double)kit[n]*kit[n];
    for(uint32_t n=0;n<2u*22050u;n++)differ+=fabs((double)kit[n]-base[n]);
    printf("  kit METAL: RMS %.0f at 0.3 .. 0.5 s, after its note-off at 48 ms\n",sqrt(late/(2.0*8820.0)));
    CHECK(sqrt(late/(2.0*8820.0))>200.0 && differ>1e6);
    CHECK(setting(&i,MD_FX_KIT,0)==MD_OK);
    on=note(MD_EVENT_NOTE_ON,2,72,100);CHECK(md_instrument_event(&i,&on)==MD_OK);
    uint32_t held=0;for(uint32_t v=0;v<MD_ELECTRONIC_FX_VOICES;v++)held+=s.voices[v].held && s.voices[v].token==2u && s.voices[v].recipe==MD_FX_PING;
    CHECK(held==1);
    CHECK(md_instrument_event(&i,&panic)==MD_OK && s.kit==0);   /* a panic: the kit sound off with the voices */
    return 0;
}
/* the shaped recipes and the spread: in the middle with no spread (left and right the same sample), each kit sound as
 * long as its recipe says and silent after; with the spread a WHOOSH crosses from one side to the other; a held shaped
 * note is cut by its note-off */
static double side(const int32_t *audio,uint32_t from,uint32_t to,uint32_t channel)
{
    double sum=0;for(uint32_t n=from;n<to;n++)sum+=(double)audio[2u*n+channel]*audio[2u*n+channel];return sqrt(sum/(to-from));
}
static int shaped_and_spread(void)
{
    static int32_t audio[2u*5u*MD_SAMPLE_RATE];
    static const uint16_t length_ms[MD_FX_RECIPES]={0,0,0,0,0,0,2600,750,1500,1300,2600,4000,3800};
    for(uint32_t recipe=0;recipe<MD_FX_RECIPES;recipe++) {
        md_electronic_fx_state_t s;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
        CHECK(setting(&i,MD_FX_KIT,(int32_t)recipe+1)==MD_OK);
        md_event_t on=note(MD_EVENT_NOTE_ON,1,72,100);CHECK(md_instrument_event(&i,&on)==MD_OK);
        CHECK(s.voices[0].shaped==(recipe>=MD_FX_WHISTLE) && s.voices[0].gain_l==32768 && s.voices[0].gain_r==32768);
        memset(audio,0,sizeof audio);
        for(uint32_t at=0;at<5u*MD_SAMPLE_RATE;at+=147u)md_instrument_render_add(&i,audio+2u*at,147);
        uint32_t last=0;
        for(uint32_t n=0;n<5u*MD_SAMPLE_RATE;n++){CHECK(audio[2u*n]==audio[2u*n+1u]);if(audio[2u*n]>8 || audio[2u*n]<-8)last=n;}
        CHECK(!active(&s) && last>0);
        if(length_ms[recipe]) {   /* it ends with its length: the fall is squared, the last tenth is under the floor */
            uint32_t ms=last*1000u/MD_SAMPLE_RATE;
            CHECK(ms<=length_ms[recipe] && ms>=length_ms[recipe]*8u/10u);
            CHECK(side(audio,0,220,0)<side(audio,length_ms[recipe]*441u/100u,length_ms[recipe]*441u/100u+2205u,0)+50.0 || recipe==MD_FX_THUNDER);
        }
    }
    {   /* a WHOOSH with the spread at full: one side at the start, the other at the end */
        md_electronic_fx_state_t s;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
        CHECK(setting(&i,MD_FX_KIT,1+MD_FX_WHOOSH)==MD_OK && setting(&i,MD_FX_SPREAD,100)==MD_OK);
        md_event_t on=note(MD_EVENT_NOTE_ON,1,72,100);CHECK(md_instrument_event(&i,&on)==MD_OK);
        memset(audio,0,sizeof audio);
        for(uint32_t at=0;at<3u*MD_SAMPLE_RATE;at+=64u)md_instrument_render_add(&i,audio+2u*at,64);
        double l0=side(audio,8820,22050,0),r0=side(audio,8820,22050,1),l1=side(audio,88200,101430,0),r1=side(audio,88200,101430,1);
        printf("  whoosh: left/right %.0f/%.0f at 0.2 .. 0.5 s, %.0f/%.0f at 2 .. 2.3 s\n",l0,r0,l1,r1);
        CHECK((l0>2.0*r0 && r1>2.0*l1) || (r0>2.0*l0 && l1>2.0*r1));
        /* a struck sound stays where it landed, and the places differ */
        CHECK(setting(&i,MD_FX_KIT,1+MD_FX_DROP)==MD_OK);
        uint32_t left=0,right=0;
        for(uint32_t k=0;k<32;k++) {
            md_event_t e=note(MD_EVENT_NOTE_ON,10u+k,72,100);CHECK(md_instrument_event(&i,&e)==MD_OK);
            const md_electronic_fx_voice_t *v=0;
            for(uint32_t n=0;n<MD_ELECTRONIC_FX_VOICES;n++)if(s.voices[n].active && (!v || s.voices[n].order>v->order))v=&s.voices[n];
            CHECK(v && !v->pan_move && (v->gain_l==32768 || v->gain_r==32768));
            left+=v->gain_r<32768;right+=v->gain_l<32768;
        }
        CHECK(left>=8 && right>=8);
    }
    {   /* WHISTLE as the base sound, held: its note-off ends it in a few tens of milliseconds */
        md_electronic_fx_state_t s;md_instrument_t i;CHECK(init(&i,&s)==MD_OK);
        CHECK(md_sound_apply(&i,2,MD_ELECTRONIC_FX.presets[MD_FX_WHISTLE].settings,19)==MD_OK);
        md_event_t on=note(MD_EVENT_NOTE_ON,1,72,100),off=note(MD_EVENT_NOTE_OFF,1,0,0);
        CHECK(md_instrument_event(&i,&on)==MD_OK && s.voices[0].held);
        memset(audio,0,sizeof audio);
        for(uint32_t at=0;at<22050u;at+=147u)md_instrument_render_add(&i,audio+2u*at,147);
        CHECK(md_instrument_event(&i,&off)==MD_OK);
        for(uint32_t at=22050u;at<44100u;at+=147u)md_instrument_render_add(&i,audio+2u*at,147);
        CHECK(side(audio,17640,22050,0)>1000.0 && side(audio,22050+4410,44100,0)<2.0 && !active(&s));
    }
    return 0;
}
int main(void)
{
    RUN(metadata_and_settings);RUN(held_owners_and_transient_stealing);RUN(independent_rng_and_partitions);
    RUN(presets_decay_and_artifacts);RUN(corner_parameters_and_snapshot);RUN(wind_layer);RUN(kit_sound);RUN(shaped_and_spread);return 0;
}
