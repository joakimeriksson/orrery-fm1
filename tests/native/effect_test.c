/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/effect.c tests/native/fake_processors.c
#include "core/effect.h"
#include "fake_processors.h"
#include "test.h"
#include <string.h>

static int registry_and_init_guards(void)
{
    md_effect_desc_t insert=FAKE_EFFECT,bus=FAKE_EFFECT;
    insert.processor.id++;insert.capabilities=MD_EFFECT_INSERT;insert.process_wet=NULL;
    bus.processor.id+=2u;bus.capabilities=MD_EFFECT_BUS;bus.process_replace=NULL;bus.set_tempo=NULL;
    const md_effect_desc_t *items[]={&FAKE_EFFECT,&insert,&bus};
    md_effect_registry_t registry={items,3};
    CHECK(md_effect_registry_validate(&registry)==MD_OK);
    CHECK(md_effect_find(&registry,FAKE_EFFECT_ID)==&FAKE_EFFECT);
    CHECK(!md_effect_find(&registry,0) && !md_effect_find(&registry,123));
    items[1]=items[0];CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);items[1]=&insert;
    insert.process_replace=NULL;CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    insert=FAKE_EFFECT;insert.processor.id++;insert.capabilities=0;
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    insert=FAKE_EFFECT;insert.processor.id++;insert.capabilities|=4u;
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    insert=FAKE_EFFECT;insert.processor.id++;insert.processor.api_version++;
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_VERSION);
    insert=FAKE_EFFECT;insert.processor.id++;insert.processor.state_alignment=3;
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    insert=FAKE_EFFECT;insert.processor.id++;insert.set_param=NULL;
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    _Alignas(16) uint8_t raw[sizeof(fake_effect_state_t)+16];memset(raw,0xa5,sizeof raw);
    md_effect_t effect={0};
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,raw,sizeof(fake_effect_state_t)-1u,MD_SAMPLE_RATE)==MD_ERR_CAPACITY);
    CHECK(!effect.desc && raw[0]==0xa5);
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,raw+1,sizeof(fake_effect_state_t),MD_SAMPLE_RATE)==MD_ERR_ARGUMENT);
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,raw,sizeof raw,48000)==MD_ERR_UNSUPPORTED);
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,raw,sizeof raw,MD_SAMPLE_RATE)==MD_OK);
    CHECK(effect.state==raw && raw[sizeof(fake_effect_state_t)]==0xa5);
    return 0;
}
static int parameters_tempo_settings(void)
{
    fake_effect_state_t state,before;md_effect_t effect;
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    before=state;
    CHECK(md_effect_param(&effect,0x1001u,5)==MD_ERR_RANGE);
    CHECK(md_effect_param(&effect,99,5)==MD_ERR_NOT_FOUND);
    CHECK(md_effect_param(&effect,FAKE_PARAM_GAIN,128)==MD_ERR_RANGE && !memcmp(&state,&before,sizeof state));
    CHECK(md_effect_param(&effect,FAKE_PARAM_GAIN,127)==MD_OK && state.gain==127);
    CHECK(md_effect_tempo(&effect,40u*256u)==MD_OK && state.tempo==40u*256u);
    CHECK(md_effect_tempo(&effect,240u*256u)==MD_OK && state.tempo==240u*256u);
    before=state;CHECK(md_effect_tempo(&effect,39u*256u)==MD_ERR_RANGE && !memcmp(&state,&before,sizeof state));
    CHECK(md_effect_tempo(&effect,241u*256u)==MD_ERR_RANGE && !memcmp(&state,&before,sizeof state));
    uint8_t wire[]={32,99},output[]={11,22};
    CHECK(md_effect_apply(&effect,2,wire,1)==MD_ERR_VERSION && !memcmp(&state,&before,sizeof state));
    CHECK(md_effect_apply(&effect,1,wire,2)==MD_ERR_CAPACITY && !memcmp(&state,&before,sizeof state));
    wire[0]=255;CHECK(md_effect_apply(&effect,1,wire,1)==MD_ERR_RANGE && !memcmp(&state,&before,sizeof state));
    wire[0]=32;CHECK(md_effect_apply(&effect,1,wire,1)==MD_OK && state.gain==32);
    CHECK(md_effect_encode(&effect,output,0)==MD_ERR_CAPACITY && output[0]==11);
    CHECK(md_effect_encode(&effect,output,1)==1 && output[0]==32 && output[1]==22);
    md_effect_desc_t no_tempo=FAKE_EFFECT;no_tempo.set_tempo=NULL;
    CHECK(md_effect_init(&effect,&no_tempo,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    before=state;CHECK(md_effect_tempo(&effect,120u*256u)==MD_ERR_UNSUPPORTED && !memcmp(&state,&before,sizeof state));
    return 0;
}
static int independent_instances_wet_replace_and_tail(void)
{
    fake_effect_state_t a,b;md_effect_t first,second;
    CHECK(md_effect_init(&first,&FAKE_EFFECT,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_effect_init(&second,&FAKE_EFFECT,&b,sizeof b,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_effect_param(&first,FAKE_PARAM_GAIN,127)==MD_OK);
    CHECK(md_effect_param(&second,FAKE_PARAM_GAIN,127)==MD_OK);
    int32_t input[]={100,-80,0,0,123,456},wet[]={9,9,9,9,234,567};
    md_effect_process_wet(&first,input,wet,1);
    CHECK(wet[0]==0 && wet[1]==0 && wet[2]==9 && wet[4]==234);
    CHECK(input[0]==100 && input[1]==-80 && a.memory[0]==100 && !b.memory[0]);
    /* No transport/bypass transition is implicit in these wrappers. Zero input
     * drains the existing history; resetting is an explicit panic/load action. */
    md_effect_process_wet(&first,input+2,wet+2,1);
    CHECK(wet[2]==100 && wet[3]==-80 && wet[4]==234);
    int32_t inplace[]={100,-80,0,0,99,77};
    md_effect_process_replace(&second,inplace,2);
    CHECK(inplace[0]==0 && inplace[1]==0 && inplace[2]==100 && inplace[3]==-80 && inplace[4]==99);
    md_effect_process_wet(&first,input,wet,1);CHECK(a.memory[0]==100);
    md_effect_reset(&first);CHECK(a.gain==127 && !a.memory[0] && !a.memory[1] && !a.frames);
    md_effect_process_wet(&first,input+2,wet,1);CHECK(wet[0]==0 && wet[1]==0);
    return 0;
}
static int alias_and_frame_guards(void)
{
    fake_effect_state_t state,before;md_effect_t effect;
    CHECK(md_effect_init(&effect,&FAKE_EFFECT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    int32_t buffer[]={11,12,13,14,15,16,17,18},saved[8];memcpy(saved,buffer,sizeof saved);before=state;
    md_effect_process_wet(&effect,buffer,buffer,2);
    md_effect_process_wet(&effect,buffer,buffer+1,2);
    md_effect_process_wet(&effect,buffer+1,buffer,2);
    md_effect_process_wet(&effect,buffer,NULL,2);
    md_effect_process_wet(&effect,NULL,buffer,2);
    md_effect_process_replace(&effect,buffer,0);md_effect_process_replace(&effect,buffer,257);
    md_effect_process_wet(&effect,buffer,buffer+4,0);md_effect_process_wet(&effect,buffer,buffer+4,257);
    md_effect_process_replace(&effect,(int32_t *)&state,1);
    CHECK(!memcmp(buffer,saved,sizeof saved) && !memcmp(&state,&before,sizeof state));
    md_effect_desc_t bus=FAKE_EFFECT;bus.capabilities=MD_EFFECT_BUS;bus.process_replace=NULL;
    CHECK(md_effect_init(&effect,&bus,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    before=state;md_effect_process_replace(&effect,buffer,1);
    CHECK(!memcmp(&state,&before,sizeof state) && !memcmp(buffer,saved,sizeof saved));
    md_effect_desc_t insert=FAKE_EFFECT;insert.capabilities=MD_EFFECT_INSERT;insert.process_wet=NULL;
    CHECK(md_effect_init(&effect,&insert,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    before=state;md_effect_process_wet(&effect,buffer,buffer+4,2);
    CHECK(!memcmp(&state,&before,sizeof state) && !memcmp(buffer,saved,sizeof saved));
    return 0;
}
static int short_span_equivalence(void)
{
    const uint32_t partitions[]={1,17,64,255,256};
    int32_t input[512],reference[512],split[512];
    for(uint32_t k=0;k<512;k++)input[k]=(int32_t)(k%31u)-15;
    for(uint32_t p=0;p<5;p++) {
        fake_effect_state_t a,b;md_effect_t first,second;
        CHECK(md_effect_init(&first,&FAKE_EFFECT,&a,sizeof a,MD_SAMPLE_RATE)==MD_OK);
        CHECK(md_effect_init(&second,&FAKE_EFFECT,&b,sizeof b,MD_SAMPLE_RATE)==MD_OK);
        md_effect_process_wet(&first,input,reference,256);
        for(uint32_t pos=0;pos<256;) {
            uint32_t frames=partitions[p]<256-pos ? partitions[p] : 256-pos;
            md_effect_process_wet(&second,input+2*pos,split+2*pos,frames);pos+=frames;
        }
        CHECK(!memcmp(reference,split,sizeof reference) && !memcmp(&a,&b,sizeof a));
    }
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
    fake_effect_state_t state,before;md_effect_t instance;
    CHECK(md_effect_init(&instance,&FAKE_EFFECT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    uint8_t loaded[]={91};CHECK(md_effect_apply(&instance,1,loaded,1)==MD_OK);
    int32_t value=-12345;before=state;
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_OK && value==91);
    CHECK(!memcmp(&state,&before,sizeof state) && FAKE_EFFECT.processor.params[0].initial==64);
    value=-12345;
    CHECK(md_effect_param_get(&instance,0,&value)==MD_ERR_RANGE && value==-12345);
    CHECK(md_effect_param_get(&instance,0x1001u,&value)==MD_ERR_RANGE && value==-12345);
    CHECK(md_effect_param_get(&instance,99,&value)==MD_ERR_NOT_FOUND && value==-12345);
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,NULL)==MD_ERR_ARGUMENT);
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,(int32_t *)&state)==MD_ERR_ARGUMENT && !memcmp(&state,&before,sizeof state));
    md_effect_desc_t descriptor=FAKE_EFFECT;descriptor.get_param=NULL;
    const md_effect_desc_t *items[]={&descriptor};md_effect_registry_t registry={items,1};
    CHECK(md_effect_registry_validate(&registry)==MD_ERR_ARGUMENT);
    descriptor.processor.param_count=0;descriptor.processor.params=NULL;
    CHECK(md_effect_registry_validate(&registry)==MD_OK);
    CHECK(md_effect_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_NOT_FOUND && value==-12345);
    descriptor=FAKE_EFFECT;md_param_desc_t readonly=FAKE_EFFECT.processor.params[0];readonly.flags=MD_PARAM_READ_ONLY;
    descriptor.processor.params=&readonly;
    CHECK(md_effect_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_OK && value==64);
    CHECK(md_effect_param(&instance,FAKE_PARAM_GAIN,100)==MD_ERR_UNSUPPORTED);
    descriptor=FAKE_EFFECT;descriptor.get_param=getter_error;
    CHECK(md_effect_init(&instance,&descriptor,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);value=-12345;
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    descriptor.get_param=getter_out_of_range;
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    descriptor.get_param=getter_positive_status;
    CHECK(md_effect_param_get(&instance,FAKE_PARAM_GAIN,&value)==MD_ERR_STATE && value==-12345);
    return 0;
}
int main(void)
{
    RUN(parameter_readback);
    RUN(registry_and_init_guards);RUN(parameters_tempo_settings);RUN(independent_instances_wet_replace_and_tail);
    RUN(alias_and_frame_guards);RUN(short_span_equivalence);return 0;
}
