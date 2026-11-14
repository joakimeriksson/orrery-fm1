/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c tests/native/fake_processors.c
#include "fake_processors.h"
#include "test.h"
#include <string.h>

static md_event_t on(uint32_t token, uint8_t key)
{
    md_event_t e={0};e.kind=MD_EVENT_NOTE_ON;e.track=0;e.source=MD_SOURCE_PANEL;e.token=token;
    e.data.note.key=key;e.data.note.velocity=90;return e;
}
static int registry_and_descriptors(void)
{
    const md_instrument_desc_t *items[]={&FAKE_INSTRUMENT,&FAKE_INSTRUMENT_MONO};
    md_instrument_registry_t registry={items,2};
    CHECK(md_instrument_registry_validate(&registry)==MD_OK);
    CHECK(md_instrument_find(&registry,FAKE_INSTRUMENT_ID)==&FAKE_INSTRUMENT);
    CHECK(md_instrument_find(&registry,0)==NULL && md_instrument_find(&registry,17)==NULL);
    items[1]=items[0];CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    md_instrument_desc_t d=FAKE_INSTRUMENT;items[1]=&d;d.processor.id++;
    d.processor.api_version++;CHECK(md_instrument_registry_validate(&registry)==MD_ERR_VERSION);
    d=FAKE_INSTRUMENT;items[0]=&d;registry.count=1;
    d.capabilities|=MD_CAP_MONO;CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    d=FAKE_INSTRUMENT;d.render_add=NULL;CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    d=FAKE_INSTRUMENT;d.processor.state_alignment=3;CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    md_param_desc_t params[2]={FAKE_INSTRUMENT.processor.params[0],FAKE_INSTRUMENT.processor.params[0]};
    d=FAKE_INSTRUMENT;d.processor.params=params;d.processor.param_count=2;
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    d.processor.param_count=1;params[0].initial=128;
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_RANGE);
    params[0]=FAKE_INSTRUMENT.processor.params[0];params[0].flags=MD_PARAM_READ_ONLY|MD_PARAM_AUTOMATABLE;
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_RANGE);
    const char *labels[]={"one","two"};params[0].flags=MD_PARAM_ENUM;params[0].min=1;params[0].max=2;
    params[0].initial=1;params[0].labels=labels;params[0].label_count=1;
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_RANGE);
    params[0].label_count=2;CHECK(md_instrument_registry_validate(&registry)==MD_OK);
    md_drum_voice_desc_t voices[]={{0,"PING",60},{0,"DROP",61}};
    d=FAKE_INSTRUMENT;d.capabilities|=MD_CAP_PERCUSSION;d.drum_voice_count=2;d.drum_voices=voices;
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    voices[1].id=14;CHECK(md_instrument_registry_validate(&registry)==MD_ERR_RANGE);
    voices[1].id=1;CHECK(md_instrument_registry_validate(&registry)==MD_OK);
    return 0;
}
static int init_admission(void)
{
    _Alignas(16) uint8_t bytes[sizeof(fake_instrument_state_t)+16];
    memset(bytes,0xa5,sizeof bytes);
    md_instrument_t i={0};
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,bytes,sizeof(fake_instrument_state_t)-1,MD_SAMPLE_RATE)==MD_ERR_CAPACITY);
    CHECK(!i.desc && !i.state && bytes[0]==0xa5);
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,bytes+1,sizeof(fake_instrument_state_t),MD_SAMPLE_RATE)==MD_ERR_ARGUMENT);
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,bytes,sizeof bytes,48000)==MD_ERR_UNSUPPORTED);
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,bytes,sizeof bytes,MD_SAMPLE_RATE)==MD_OK);
    CHECK(i.state==bytes && i.desc==&FAKE_INSTRUMENT && i.sample_rate==MD_SAMPLE_RATE);
    CHECK(bytes[sizeof(fake_instrument_state_t)]==0xa5);
    return 0;
}
static int event_admission_and_panic(void)
{
    fake_instrument_state_t s,before;md_instrument_t i;
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,&s,sizeof s,MD_SAMPLE_RATE)==MD_OK);
    md_event_t e=on(1,60);CHECK(md_instrument_event(&i,&e)==MD_OK && s.voice_count==1);
    before=s;e=on(2,61);e.flags=MD_NOTE_SLIDE;
    CHECK(md_instrument_event(&i,&e)==MD_ERR_UNSUPPORTED && !memcmp(&s,&before,sizeof s));
    e=on(2,128);CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE);
    e=on(2,60);e.source=56;CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE);
    e=on(2,60);e.source=MD_SOURCE_NONE;CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE);
    e=on(2,60);e.track=MD_ALL_TRACKS;CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE);
    e=(md_event_t){.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};e.data.param.id=FAKE_PARAM_GAIN;e.data.param.value=128;
    CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    e.data.param.id=100;CHECK(md_instrument_event(&i,&e)==MD_ERR_NOT_FOUND);
    e.data.param.id=0x1001;CHECK(md_instrument_event(&i,&e)==MD_ERR_RANGE);
    e.data.param.id=FAKE_PARAM_GAIN;e.data.param.value=100;
    CHECK(md_instrument_event(&i,&e)==MD_OK && s.gain==100);
    e=(md_event_t){.kind=MD_EVENT_BEND,.source=MD_SOURCE_PANEL};
    CHECK(md_instrument_event(&i,&e)==MD_ERR_UNSUPPORTED);
    e=(md_event_t){.kind=MD_EVENT_SOURCE_RELEASE,.source=MD_SOURCE_PANEL};
    CHECK(md_instrument_event(&i,&e)==MD_ERR_UNSUPPORTED);
    e=(md_event_t){.kind=MD_EVENT_PANIC,.track=MD_ALL_TRACKS,.source=MD_SOURCE_NONE};
    CHECK(md_instrument_event(&i,&e)==MD_OK && s.voice_count==0 && s.gain==100 && s.reset_calls==1);
    return 0;
}
static int settings_and_render(void)
{
    fake_instrument_state_t s,before;md_instrument_t i;
    CHECK(md_instrument_init(&i,&FAKE_INSTRUMENT,&s,sizeof s,MD_SAMPLE_RATE)==MD_OK);
    uint8_t setting[2]={32,99},out[2]={17,18};before=s;
    CHECK(md_sound_apply(&i,2,setting,1)==MD_ERR_VERSION && !memcmp(&s,&before,sizeof s));
    CHECK(md_sound_apply(&i,1,setting,2)==MD_ERR_CAPACITY && !memcmp(&s,&before,sizeof s));
    setting[0]=255;CHECK(md_sound_apply(&i,1,setting,1)==MD_ERR_RANGE && !memcmp(&s,&before,sizeof s));
    setting[0]=32;CHECK(md_sound_apply(&i,1,setting,1)==MD_OK && s.gain==32);
    CHECK(md_sound_encode(&i,out,0)==MD_ERR_CAPACITY && out[0]==17);
    CHECK(md_sound_encode(&i,out,1)==1 && out[0]==32 && out[1]==18);
    md_event_t e=on(1,60);CHECK(md_instrument_event(&i,&e)==MD_OK);
    int32_t audio[4]={10,20,30,40};
    md_instrument_render_add(&i,audio,0);md_instrument_render_add(&i,audio,257);
    CHECK(!s.frame && audio[0]==10 && audio[3]==40);
    md_instrument_render_add(&i,audio,1);CHECK(s.frame==1 && audio[2]==30 && audio[3]==40);
    md_instrument_reset(&i);CHECK(!s.voice_count && s.gain==32);
    return 0;
}
static int getter_error(const void *state,md_param_id_t id,int32_t *value)
{(void)state;(void)id;*value=99;return MD_ERR_STATE;}
static int getter_out_of_range(const void *state,md_param_id_t id,int32_t *value)
{(void)state;(void)id;*value=128;return MD_OK;}
static int getter_positive_status(const void *state,md_param_id_t id,int32_t *value)
{(void)state;(void)id;*value=32;return 1;}
static int parameter_readback(void)
{
    fake_instrument_state_t state,before;md_instrument_t instance;
    CHECK(md_instrument_init(&instance,&FAKE_INSTRUMENT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    uint8_t loaded[]={91};CHECK(md_sound_apply(&instance,1,loaded,1)==MD_OK);
    int32_t value=-12345;before=state;
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_OK && value==91);
    CHECK(!memcmp(&state,&before,sizeof state) && FAKE_INSTRUMENT.processor.params[0].initial==64);
    value=-12345;
    CHECK(md_instrument_param_get(&instance,0,&value)==MD_ERR_RANGE && value==-12345);
    CHECK(md_instrument_param_get(&instance,0x1001u,&value)==MD_ERR_RANGE && value==-12345);
    CHECK(md_instrument_param_get(&instance,99,&value)==MD_ERR_NOT_FOUND && value==-12345);
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,NULL)==MD_ERR_ARGUMENT);
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,(int32_t *)&state)==MD_ERR_ARGUMENT && !memcmp(&state,&before,sizeof state));
    md_instrument_desc_t descriptor=FAKE_INSTRUMENT;descriptor.get_param=NULL;
    const md_instrument_desc_t *items[]={&descriptor};md_instrument_registry_t registry={items,1};
    CHECK(md_instrument_registry_validate(&registry)==MD_ERR_ARGUMENT);
    descriptor.processor.param_count=0;descriptor.processor.params=NULL;
    CHECK(md_instrument_registry_validate(&registry)==MD_OK);
    CHECK(md_instrument_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_NOT_FOUND && value==-12345);
    descriptor=FAKE_INSTRUMENT;md_param_desc_t readonly=FAKE_INSTRUMENT.processor.params[0];readonly.flags=MD_PARAM_READ_ONLY;
    descriptor.processor.params=&readonly;
    CHECK(md_instrument_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_OK && value==64);
    md_event_t change={.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};change.data.param.id=FAKE_PARAM_GAIN;change.data.param.value=100;
    CHECK(md_instrument_event(&instance,&change)==MD_ERR_UNSUPPORTED);
    descriptor=FAKE_INSTRUMENT;descriptor.get_param=getter_error;
    CHECK(md_instrument_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);value=-12345;
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    descriptor.get_param=getter_out_of_range;
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    descriptor.get_param=getter_positive_status;
    CHECK(md_instrument_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    return 0;
}
int main(void)
{
    RUN(parameter_readback);
    RUN(registry_and_descriptors);RUN(init_admission);
    RUN(event_admission_and_panic);RUN(settings_and_render);return 0;
}
