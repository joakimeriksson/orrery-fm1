/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c core/router.c instruments/arp.c instruments/eminent.c instruments/minipops.c engines/arp_engine.c engines/em_engine.c engines/mp_engine.c
#include "instruments/arp.h"
#include "core/router.h"
#include "engines/arp_presets.h"
#include "instruments/eminent.h"
#include "instruments/minipops.h"
#include "test.h"
#include <string.h>

typedef union { md_arp_state_t arp; md_eminent_state_t eminent; md_minipops_state_t minipops; } state_t;
static const md_instrument_desc_t *const descriptors[]={&MD_ARP2600,&MD_EMINENT,&MD_MINIPOPS};
static md_event_t note(md_event_kind_t kind,md_note_token_t token,uint8_t key,uint8_t velocity)
{
    md_event_t e={.kind=kind,.source=MD_SOURCE_PANEL,.token=token};
    if(kind==MD_EVENT_NOTE_ON){e.data.note.key=key;e.data.note.velocity=velocity;}
    return e;
}
static md_event_t hit(uint16_t voice,uint8_t velocity)
{
    md_event_t e={.kind=MD_EVENT_HIT,.source=3};e.data.hit.voice=voice;e.data.hit.velocity=velocity;return e;
}
static int sounds_and_metadata(void)
{
    md_instrument_registry_t registry={descriptors,3};
    CHECK(md_instrument_registry_validate(&registry)==MD_OK);
    CHECK(md_instrument_find(&registry,MD_INSTRUMENT_ARP2600)==&MD_ARP2600);
    /* M1-05: explicit accent/legato/slide; velocity still has no effect and is not claimed */
    CHECK(!(MD_ARP2600.capabilities&MD_CAP_VELOCITY));
    CHECK((MD_ARP2600.capabilities&(MD_CAP_ACCENT|MD_CAP_SLIDE|MD_CAP_LEGATO))==(MD_CAP_ACCENT|MD_CAP_SLIDE|MD_CAP_LEGATO));
    CHECK(!(MD_EMINENT.capabilities&MD_CAP_VELOCITY));
    CHECK(MD_MINIPOPS.drum_voice_count==14 && MD_MINIPOPS.max_polyphony==14);
    CHECK(MD_MINIPOPS.processor.settings_version==2 && MD_MINIPOPS.processor.max_settings_bytes==5);
    for(uint32_t i=0;i<14;i++) CHECK(MD_MINIPOPS.drum_voices[i].id==i);
    for(uint32_t d=0;d<3;d++) {
        state_t a,b,before;md_instrument_t ia,ib;uint8_t wire[64],restored[64];
        const md_instrument_desc_t *desc=descriptors[d];uint32_t size=desc->processor.state_bytes;
        memset(&a,0,sizeof a);memset(&b,0,sizeof b);
        CHECK(md_instrument_init(&ia,desc,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
        CHECK(md_instrument_init(&ib,desc,&b,sizeof b,MD_SAMPLE_RATE)==MD_OK);
        for(uint32_t p=0;p<desc->preset_count;p++) {
            const md_preset_t *preset=&desc->presets[p];
            CHECK(md_sound_apply(&ia,desc->processor.settings_version,preset->settings,preset->settings_bytes)==MD_OK);
            CHECK(md_sound_encode(&ia,wire,sizeof wire)==preset->settings_bytes);
            CHECK(!memcmp(wire,preset->settings,preset->settings_bytes));
            const uint8_t arp_index[]={12,13,19,32,14,18,20,21,24,27,3,7,0,1,2,4,5,6,8,9,10,11,15,16,17,22,23,25,26,28,29,30,31,34,35};
            const uint8_t eminent_index[]={0,1,2,3,4,5,7};
            for(uint32_t parameter=0;parameter<desc->processor.param_count;parameter++) {
                uint32_t index=d==0 ? arp_index[parameter] : d==1 ? eminent_index[parameter] : parameter;
                int32_t current=-1;CHECK(md_instrument_param_get(&ia,desc->processor.params[parameter].id,&current)==MD_OK);
                CHECK(current==wire[index]);
            }
            CHECK(md_sound_apply(&ib,desc->processor.settings_version,wire,preset->settings_bytes)==MD_OK);
            CHECK(md_sound_encode(&ib,restored,sizeof restored)==preset->settings_bytes);
            CHECK(!memcmp(wire,restored,preset->settings_bytes));
        }
        for(uint32_t parameter=0;parameter<desc->processor.param_count;parameter++)for(uint32_t bound=0;bound<2;bound++) {
            const md_param_desc_t *param=&desc->processor.params[parameter];
            md_event_t edit={.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};
            edit.data.param.id=param->id;edit.data.param.value=bound ? param->max : param->min;
            CHECK(md_instrument_event(&ia,&edit)==MD_OK);before=a;
            int32_t current=-999;CHECK(md_instrument_param_get(&ia,param->id,&current)==MD_OK && current==edit.data.param.value);
            CHECK(!memcmp(&a,&before,size));
        }
        before=a;
        for(uint32_t byte=0;byte<desc->processor.max_settings_bytes;byte++) {
            uint8_t saved=wire[byte];wire[byte]=255;
            CHECK(md_sound_apply(&ia,desc->processor.settings_version,wire,desc->processor.max_settings_bytes)==MD_ERR_RANGE);
            CHECK(!memcmp(&a,&before,size));wire[byte]=saved;
        }
        CHECK(md_sound_apply(&ia,99,wire,1)==MD_ERR_VERSION && !memcmp(&a,&before,size));
        CHECK(md_sound_encode(&ia,wire,0)==MD_ERR_CAPACITY);
        CHECK(md_instrument_init(&ib,desc,&b,sizeof b,48000)==MD_ERR_UNSUPPORTED);
    }
    return 0;
}
static int arp_full_sound_parameters_and_source_presets(void)
{
    CHECK(MD_ARP2600.processor.param_count==35 && MD_ARP2600.preset_count==ARP_N_PATCHES);
    for(uint32_t k=0;k<ARP_N_PATCHES;k++) {
        const arp_patch_t *p=&ARP_PATCHES[k];
        const uint8_t expected[]={p->vco1_wave,p->vco1_oct,p->vco1_pw,p->vco1_lvl,
            p->vco2_wave,p->vco2_oct,p->vco2_detune,p->vco2_lvl,
            p->noise_lvl,p->ring_lvl,p->sync,p->sub_lvl,p->cutoff,p->reso,p->env_amt,p->kbd,
            p->lfo_vcf,p->sh_vcf,p->drive,p->glide,p->a1,p->d1,p->s1,p->r1,
            p->a2,p->d2,p->s2,p->r2,p->lfo_wave,p->lfo_rate,p->lfo_pitch,p->lfo_pw,
            p->volume,0,p->accent,p->env_pitch};
        CHECK(sizeof expected==MD_ARP_SETTINGS_BYTES);
        CHECK(!memcmp(expected,MD_ARP2600.presets[k].settings,sizeof expected));
        CHECK(!strcmp(p->name,MD_ARP2600.presets[k].name));
    }
    /* Independent ID-to-wire expectations preserve the initial12 IDs while
     * exposing every other sound field exactly once, except session reverb. */
    const uint8_t wire_index[]={12,13,19,32,14,18,20,21,24,27,3,7,
        0,1,2,4,5,6,8,9,10,11,15,16,17,22,23,25,26,28,29,30,31,34,35};
    md_arp_state_t state;md_instrument_t i;uint8_t expected[36],wire[36];
    CHECK(md_instrument_init(&i,&MD_ARP2600,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    for(uint32_t p=0;p<35;p++) for(uint32_t bound=0;bound<2;bound++) {
        const md_param_desc_t *param=&MD_ARP2600.processor.params[p];CHECK(param->id==p+1u);
        CHECK(md_sound_apply(&i,1,MD_ARP2600.presets[0].settings,36)==MD_OK);
        memcpy(expected,MD_ARP2600.presets[0].settings,36);
        md_event_t event={.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};
        event.data.param.id=param->id;event.data.param.value=bound ? param->max : param->min;
        expected[wire_index[p]]=(uint8_t)event.data.param.value;
        CHECK(md_instrument_event(&i,&event)==MD_OK);
        CHECK(md_sound_encode(&i,wire,sizeof wire)==36 && !memcmp(expected,wire,36));
        int32_t current=-1;CHECK(md_instrument_param_get(&i,param->id,&current)==MD_OK && current==event.data.param.value);
    }
    return 0;
}
static int independent_arps_and_tokens(void)
{
    md_arp_state_t a,b,before;md_instrument_t ia,ib;
    CHECK(md_instrument_init(&ia,&MD_ARP2600,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_instrument_init(&ib,&MD_ARP2600,&b,sizeof b,MD_SAMPLE_RATE)==MD_OK);
    before=b;md_event_t e=note(MD_EVENT_NOTE_ON,11,60,1);
    CHECK(md_instrument_event(&ia,&e)==MD_OK);
    int32_t samples[514]={0};samples[512]=123;samples[513]=-456;
    md_instrument_render_add(&ia,samples,256);
    CHECK(!memcmp(&b,&before,sizeof b));CHECK(samples[512]==123 && samples[513]==-456);
    /* the explicit path keeps no key stack of its own: the router owns note priority */
    CHECK(a.engine.gate && a.engine.n_held==0 && !a.engine.running && !a.engine.req_running && !a.engine.steps);
    CHECK(!a.engine.accent_q16);
    /* a plain second note while one is held is refused without mutation */
    before=a;e=note(MD_EVENT_NOTE_ON,12,61,100);e.flags=MD_NOTE_ACCENT;
    CHECK(md_instrument_event(&ia,&e)==MD_ERR_CAPACITY && !memcmp(&a,&before,sizeof a));
    e.flags=MD_NOTE_SLIDE<<3;CHECK(md_instrument_event(&ia,&e)!=MD_OK && !memcmp(&a,&before,sizeof a));
    /* SLIDE replaces the held voice without a new attack and glides there */
    md_instrument_render_add(&ia,samples,256);
    uint8_t stage=a.engine.env2.stage;CHECK(stage>=2);
    e.flags=MD_NOTE_SLIDE;CHECK(md_instrument_event(&ia,&e)==MD_OK);
    CHECK(a.voice.token==12 && a.engine.gate && a.engine.sliding && a.engine.env2.stage==stage);
    CHECK(a.engine.target_q16==61*65536 && a.engine.note_q16!=a.engine.target_q16);
    /* the old token's off after the replacement is a stale no-op */
    e=note(MD_EVENT_NOTE_OFF,11,0,0);CHECK(md_instrument_event(&ia,&e)==MD_OK && a.engine.gate && a.voice.token==12);
    e=note(MD_EVENT_NOTE_OFF,999,0,0);CHECK(md_instrument_event(&ia,&e)==MD_OK && a.engine.gate);
    e=note(MD_EVENT_NOTE_OFF,12,0,0);e.flags=MD_NOTE_ACCENT;CHECK(md_instrument_event(&ia,&e)==MD_ERR_ARGUMENT && a.engine.gate);
    e.flags=0;CHECK(md_instrument_event(&ia,&e)==MD_OK && !a.engine.gate && !a.voice.token);
    /* ACCENT comes from the flag, not velocity: velocity 1 and 127 give the same engine state */
    md_arp_state_t soft,loud;md_instrument_t is,il;
    CHECK(md_instrument_init(&is,&MD_ARP2600,&soft,sizeof soft,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_instrument_init(&il,&MD_ARP2600,&loud,sizeof loud,MD_SAMPLE_RATE)==MD_OK);
    e=note(MD_EVENT_NOTE_ON,20,48,1);e.flags=MD_NOTE_ACCENT;CHECK(md_instrument_event(&is,&e)==MD_OK);
    e.data.note.velocity=127;CHECK(md_instrument_event(&il,&e)==MD_OK);
    CHECK(soft.engine.accent_q16>0 && soft.engine.accent_q16==loud.engine.accent_q16 && !memcmp(&soft,&loud,sizeof soft));
    uint8_t sound[MD_ARP_SETTINGS_BYTES],restored[MD_ARP_SETTINGS_BYTES];
    CHECK(md_sound_encode(&ia,sound,sizeof sound)==MD_ARP_SETTINGS_BYTES);
    md_instrument_reset(&ia);CHECK(!a.engine.env1.stage && !a.engine.env2.stage && !a.engine.ph1 && !a.voice.token);
    CHECK(md_sound_encode(&ia,restored,sizeof restored)==MD_ARP_SETTINGS_BYTES && !memcmp(sound,restored,sizeof sound));
    return 0;
}
static int eminent_physical_ownership(void)
{
    md_eminent_state_t state,before;md_instrument_t i;
    CHECK(md_instrument_init(&i,&MD_EMINENT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    for(uint32_t k=0;k<EM_VOICES;k++) {md_event_t e=note(MD_EVENT_NOTE_ON,k+1u,(uint8_t)(48u+k),100);CHECK(md_instrument_event(&i,&e)==MD_OK);}
    CHECK(em_held(&state.engine)==EM_VOICES);
    before=state;md_event_t e=note(MD_EVENT_NOTE_ON,55,90,100);
    CHECK(md_instrument_event(&i,&e)==MD_ERR_CAPACITY && !memcmp(&state,&before,sizeof state));
    e=note(MD_EVENT_NOTE_OFF,1,0,0);CHECK(md_instrument_event(&i,&e)==MD_OK && em_held(&state.engine)==15);
    e=note(MD_EVENT_NOTE_OFF,999,0,0);CHECK(md_instrument_event(&i,&e)==MD_OK && em_held(&state.engine)==15);
    md_instrument_reset(&i);CHECK(!em_held(&state.engine) && !state.voice_count && !state.engine.lfo_ph);
    return 0;
}
static int engine_reference(void)
{
    md_arp_state_t a;md_eminent_state_t e;md_minipops_state_t m;
    md_instrument_t ia,ie,im;int32_t actual[512]={0},expected[512]={0};
    CHECK(md_instrument_init(&ia,&MD_ARP2600,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
    arp_t refa;arp_init(&refa);arp_set_patch(&refa,&a.engine.patch);
    md_event_t on=note(MD_EVENT_NOTE_ON,1,60,100);
    CHECK(md_instrument_event(&ia,&on)==MD_OK);arp_note_on(&refa,60,100);
    md_instrument_render_add(&ia,actual,256);arp_render(&refa,expected,256);CHECK(!memcmp(actual,expected,sizeof actual));
    CHECK(md_instrument_init(&ie,&MD_EMINENT,&e,sizeof e,MD_SAMPLE_RATE)==MD_OK);
    em_t refe;em_init(&refe);em_set_patch(&refe,&e.engine.p);
    CHECK(md_instrument_event(&ie,&on)==MD_OK);em_note_on(&refe,60,100);
    memset(actual,0,sizeof actual);memset(expected,0,sizeof expected);
    md_instrument_render_add(&ie,actual,256);em_render(&refe,expected,256);CHECK(!memcmp(actual,expected,sizeof actual));
    CHECK(md_instrument_init(&im,&MD_MINIPOPS,&m,sizeof m,MD_SAMPLE_RATE)==MD_OK);
    mp_t refm;mp_init(&refm);mp_set_level(&refm,80,50);mp_set_extra(&refm,70,70,70);
    /* the extra rhythms (quijada, guiro, tambourine) at their default 70 % slider level */
    for(uint32_t k=0;k<MP_VOICES;k++) {
        md_event_t h=hit((uint16_t)k,100);CHECK(md_instrument_event(&im,&h)==MD_OK);
        int32_t vel=100*65536/127;if(k>=MP_QU)vel=vel*70/100;
        mp_trigger(&refm,k,vel);
    }
    memset(actual,0,sizeof actual);memset(expected,0,sizeof expected);
    md_instrument_render_add(&im,actual,256);mp_render(&refm,expected,256);CHECK(!memcmp(actual,expected,sizeof actual));
    CHECK(!m.engine.playing && !m.engine.req_play && !m.engine.steps && !m.engine.keys);
    CHECK(m.engine.hit_mask==0x3fffu);
    md_event_t invalid=hit(14,100);md_minipops_state_t before=m;
    CHECK(md_instrument_event(&im,&invalid)==MD_ERR_NOT_FOUND && !memcmp(&m,&before,sizeof m));
    on=note(MD_EVENT_NOTE_ON,1,60,100);CHECK(md_instrument_event(&im,&on)==MD_ERR_UNSUPPORTED);
    /* a muted extra rhythm drops its hits without touching the engine */
    md_event_t mute={.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};mute.data.param.id=MD_MINIPOPS_GUIRO;
    CHECK(md_instrument_event(&im,&mute)==MD_OK && m.engine.req_extra[1]==0);
    m.engine.hit_mask=0;before=m;md_event_t guiro=hit(MP_GU,127);
    CHECK(md_instrument_event(&im,&guiro)==MD_OK && !memcmp(&m,&before,sizeof m));
    md_instrument_reset(&im);CHECK(!m.engine.hit_mask && !m.engine.playing && m.engine.req_extra[1]==0);
    return 0;
}
#define COMPARE_FRAMES 8192u
static int segmented_comparison(void)
{
    const uint32_t partitions[]={1,17,64,255,256};
    static int32_t baseline[COMPARE_FRAMES*2u],segmented[COMPARE_FRAMES*2u];
    for(uint32_t d=0;d<3;d++) for(uint32_t p=0;p<5;p++) {
        state_t a,b;md_instrument_t ia,ib;const md_instrument_desc_t *desc=descriptors[d];
        CHECK(md_instrument_init(&ia,desc,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
        CHECK(md_instrument_init(&ib,desc,&b,sizeof b,MD_SAMPLE_RATE)==MD_OK);
        md_event_t on=d==2 ? hit(MP_BD,100) : note(MD_EVENT_NOTE_ON,1,60,100);
        CHECK(md_instrument_event(&ia,&on)==MD_OK && md_instrument_event(&ib,&on)==MD_OK);
        memset(baseline,0,sizeof baseline);memset(segmented,0,sizeof segmented);
        for(uint32_t pos=0;pos<COMPARE_FRAMES;pos+=256)md_instrument_render_add(&ia,baseline+2u*pos,256);
        for(uint32_t pos=0;pos<COMPARE_FRAMES;) {
            uint32_t count=partitions[p]<COMPARE_FRAMES-pos ? partitions[p] : COMPARE_FRAMES-pos;
            md_instrument_render_add(&ib,segmented+2u*pos,count);pos+=count;
        }
        uint32_t peak=0;uint64_t absolute=0,magnitude=0;
        for(uint32_t k=0;k<COMPARE_FRAMES*2u;k++) {
            int32_t delta=baseline[k]-segmented[k];uint32_t absdelta=(uint32_t)(delta<0 ? -delta : delta);
            if(absdelta>peak)peak=absdelta;
            absolute+=absdelta;magnitude+=(uint32_t)(baseline[k]<0 ? -baseline[k] : baseline[k]);
        }
        printf("SPAN %s partition=%u peak_delta=%u mean_abs_relative=%.9f\n",desc->processor.name,partitions[p],peak,magnitude ? (double)absolute/(double)magnitude : 0.0);
        /* ARP updates pitch/filter every8 samples relative to each render call.
         * Until upstream cadence is made persistent, this default-patch fixture
         * allows <=1024 Q15 units peak and2percent mean absolute relative error.
         * Eminent and direct MiniPops should remain bit-exact in this fixture. */
        CHECK(d==0 ? (peak<=1024u && absolute*100u<=magnitude*2u) : peak==0u);
    }
    return 0;
}
/* M1-05 through the real router: a sequenced SLIDE arrives as new note-on, then the old note-off */
static int arp_slide_and_accent_through_router(void)
{
    static md_arp_state_t a;static md_router_t r;md_instrument_t ia;
    CHECK(md_instrument_init(&ia,&MD_ARP2600,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
    md_instrument_t *tracks[MD_TRACKS]={&ia,NULL,NULL,NULL};
    CHECK(md_router_init(&r,tracks)==MD_OK);
    md_event_t e=note(MD_EVENT_NOTE_ON,1,48,100);e.source=0;e.flags=MD_NOTE_ACCENT;
    CHECK(md_router_event(&r,&e)==MD_OK && a.engine.gate && a.engine.accent_q16>0);
    int32_t buffer[512]={0};md_instrument_render_add(&ia,buffer,256);
    uint8_t stage=a.engine.env2.stage;
    e=note(MD_EVENT_NOTE_ON,2,55,100);e.source=0;e.flags=MD_NOTE_SLIDE;
    CHECK(md_router_event(&r,&e)==MD_OK);
    CHECK(a.voice.token==2 && a.engine.gate && a.engine.sliding && a.engine.env2.stage==stage && !a.engine.accent_q16);
    e=note(MD_EVENT_NOTE_OFF,1,0,0);e.source=0;CHECK(md_router_event(&r,&e)==MD_OK && a.engine.gate);
    e=note(MD_EVENT_NOTE_OFF,2,0,0);e.source=0;CHECK(md_router_event(&r,&e)==MD_OK && !a.engine.gate);
    CHECK(md_router_owned_count(&r)==0 && !r.backend_errors);
    return 0;
}
int main(void)
{
    RUN(sounds_and_metadata);RUN(arp_full_sound_parameters_and_source_presets);RUN(independent_arps_and_tokens);RUN(eminent_physical_ownership);
    RUN(engine_reference);RUN(segmented_comparison);RUN(arp_slide_and_accent_through_router);return 0;
}
