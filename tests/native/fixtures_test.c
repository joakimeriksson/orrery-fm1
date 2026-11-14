/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: tests/native/fake_processors.c
#include "fake_processors.h"
#include "test.h"
#include <string.h>

static md_event_t note(md_event_kind_t kind, md_note_token_t token, uint8_t key, uint8_t velocity)
{
    md_event_t event={0}; event.kind=kind;event.source=MD_SOURCE_PANEL;event.token=token;
    if (kind==MD_EVENT_NOTE_ON) {event.data.note.key=key;event.data.note.velocity=velocity;}
    return event;
}
static int instrument_callbacks(void)
{
    fake_instrument_state_t a,b,before;
    const md_instrument_desc_t *d=&FAKE_INSTRUMENT;
    CHECK(d->init(&a,MD_SAMPLE_RATE)==MD_OK);
    CHECK(d->init(&b,MD_SAMPLE_RATE)==MD_OK);
    md_event_t event=note(MD_EVENT_NOTE_ON,42,60,100);
    CHECK(d->event(&a,&event)==MD_OK);
    CHECK(a.voice_count==1 && b.voice_count==0 && a.max_voice_count==1);
    int32_t samples[6]={5,7,5,7,99,101};
    d->render_add(&a,samples,2);
    CHECK(samples[0]==5-16*100*64/127 && samples[1]==7+16*100*64/127);
    CHECK(samples[4]==99 && samples[5]==101);
    event=note(MD_EVENT_NOTE_OFF,42,0,0);CHECK(d->event(&a,&event)==MD_OK);
    CHECK(a.voice_count==0 && a.trace_count==2 && a.trace[1].frame==2);
    CHECK(fake_trace_at(&a,0)->event.token==42 && fake_trace_at(&a,2)==NULL);
    before=a;event=note(MD_EVENT_NOTE_ON,43,61,90);event.flags=MD_NOTE_SLIDE;
    CHECK(d->event(&a,&event)==MD_ERR_UNSUPPORTED && !memcmp(&a,&before,sizeof a));
    event=note(MD_EVENT_NOTE_ON,43,128,90);
    CHECK(d->event(&a,&event)==MD_ERR_RANGE && !memcmp(&a,&before,sizeof a));
    event=(md_event_t){.kind=MD_EVENT_PARAM,.source=MD_SOURCE_NONE};
    event.data.param.id=FAKE_PARAM_GAIN;event.data.param.value=127;
    CHECK(d->event(&a,&event)==MD_OK && a.gain==127);
    uint8_t encoded[3]={19,20,21};
    CHECK(d->encode(&a,encoded,0)==MD_ERR_CAPACITY && encoded[0]==19);
    CHECK(d->encode(&a,encoded,1)==1 && encoded[0]==127 && encoded[1]==20);
    CHECK(d->validate(encoded,1)==MD_OK && d->apply(&b,encoded,1)==MD_OK && b.gain==127);
    before=b;encoded[0]=255;
    CHECK(d->validate(encoded,1)==MD_ERR_RANGE && d->apply(&b,encoded,1)==MD_ERR_RANGE);
    CHECK(!memcmp(&b,&before,sizeof b));
    d->reset(&a);CHECK(a.gain==127 && !a.voice_count && !a.frame && a.reset_calls==1);
    CHECK(a.trace_count==0 && b.reset_calls==0);
    CHECK(d->init(&a,48000)==MD_ERR_UNSUPPORTED);
    return 0;
}
static int instrument_limits_and_variants(void)
{
    fake_instrument_state_t s,before;
    CHECK(FAKE_INSTRUMENT.init(&s,MD_SAMPLE_RATE)==MD_OK);
    for (uint32_t i=0;i<FAKE_MAX_VOICES;i++) {
        md_event_t e=note(MD_EVENT_NOTE_ON,i+1u,(uint8_t)(60u+i),100);
        CHECK(FAKE_INSTRUMENT.event(&s,&e)==MD_OK);
    }
    before=s;md_event_t e=note(MD_EVENT_NOTE_ON,5,70,90);
    CHECK(FAKE_INSTRUMENT.event(&s,&e)==MD_ERR_CAPACITY && !memcmp(&s,&before,sizeof s));
    e=note(MD_EVENT_NOTE_ON,1,70,90);
    CHECK(FAKE_INSTRUMENT.event(&s,&e)==MD_ERR_STATE && !memcmp(&s,&before,sizeof s));
    CHECK(FAKE_INSTRUMENT_MONO.init(&s,MD_SAMPLE_RATE)==MD_OK);
    e=note(MD_EVENT_NOTE_ON,1,60,50);CHECK(FAKE_INSTRUMENT_MONO.event(&s,&e)==MD_OK);
    before=s;e=note(MD_EVENT_NOTE_ON,2,61,60);
    CHECK(FAKE_INSTRUMENT_MONO.event(&s,&e)==MD_ERR_CAPACITY && !memcmp(&s,&before,sizeof s));
    CHECK(FAKE_INSTRUMENT_NO_VELOCITY.init(&s,MD_SAMPLE_RATE)==MD_OK);
    e=note(MD_EVENT_NOTE_ON,1,60,1);CHECK(FAKE_INSTRUMENT_NO_VELOCITY.event(&s,&e)==MD_OK);
    CHECK(s.voices[0].velocity==100);
    fake_trace_clear(&s);CHECK(s.trace_count==0 && s.voice_count==1);
    return 0;
}
static int partition_and_trace(void)
{
    const uint32_t partitions[]={1,17,64,255,256};
    int32_t reference[512],split[512];
    fake_instrument_state_t a,b;
    md_event_t on=note(MD_EVENT_NOTE_ON,123,72,99),off=note(MD_EVENT_NOTE_OFF,123,0,0);
    for (uint32_t p=0;p<sizeof partitions/sizeof partitions[0];p++) {
        memset(reference,0,sizeof reference);memset(split,0,sizeof split);
        CHECK(FAKE_INSTRUMENT.init(&a,MD_SAMPLE_RATE)==MD_OK);
        CHECK(FAKE_INSTRUMENT.init(&b,MD_SAMPLE_RATE)==MD_OK);
        CHECK(FAKE_INSTRUMENT.event(&a,&on)==MD_OK && FAKE_INSTRUMENT.event(&b,&on)==MD_OK);
        FAKE_INSTRUMENT.render_add(&a,reference,256);
        for (uint32_t pos=0;pos<256;) {
            uint32_t frames=partitions[p] < 256-pos ? partitions[p] : 256-pos;
            FAKE_INSTRUMENT.render_add(&b,split+2*pos,frames);pos+=frames;
        }
        CHECK(!memcmp(reference,split,sizeof reference));
        CHECK(FAKE_INSTRUMENT.event(&a,&off)==MD_OK && FAKE_INSTRUMENT.event(&b,&off)==MD_OK);
        CHECK(a.trace_count==b.trace_count && a.trace[1].frame==256 && b.trace[1].frame==256);
        CHECK(a.trace[1].event.kind==b.trace[1].event.kind && a.trace[1].event.token==b.trace[1].event.token);
    }
    return 0;
}
static int effect_buffers_and_tails(void)
{
    fake_effect_state_t a,b,before;
    const md_effect_desc_t *d=&FAKE_EFFECT;
    CHECK(d->init(&a,MD_SAMPLE_RATE)==MD_OK && d->init(&b,MD_SAMPLE_RATE)==MD_OK);
    CHECK(d->set_param(&a,FAKE_PARAM_GAIN,127)==MD_OK);
    CHECK(d->set_param(&b,FAKE_PARAM_GAIN,127)==MD_OK);
    int32_t input[6]={100,-80,0,0,42,43},wet[6]={99,99,99,99,44,45};
    d->process_wet(&a,input,wet,1);
    CHECK(wet[0]==0 && wet[1]==0 && wet[2]==99 && wet[4]==44);
    CHECK(input[0]==100 && input[1]==-80 && a.memory[0]==100 && b.memory[0]==0);
    d->process_wet(&a,input+2,wet+2,1);
    CHECK(wet[2]==100 && wet[3]==-80 && wet[4]==44);
    int32_t inplace[6]={100,-80,0,0,46,47};
    d->process_replace(&b,inplace,2);
    CHECK(inplace[0]==0 && inplace[1]==0 && inplace[2]==100 && inplace[3]==-80 && inplace[4]==46);
    before=a;CHECK(d->set_param(&a,FAKE_PARAM_GAIN,-1)==MD_ERR_RANGE && !memcmp(&a,&before,sizeof a));
    d->set_tempo(&a,120u*256u);CHECK(a.tempo==120u*256u);
    uint8_t settings[1]={32};CHECK(d->validate(settings,1)==MD_OK && d->apply(&a,settings,1)==MD_OK);
    CHECK(d->encode(&a,settings,1)==1 && settings[0]==32);
    a.memory[0]=INT32_MAX;a.memory[1]=INT32_MIN;
    CHECK(d->set_param(&a,FAKE_PARAM_GAIN,127)==MD_OK);
    d->process_wet(&a,input,wet,1);CHECK(wet[0]==INT32_MAX && wet[1]==INT32_MIN);
    CHECK(d->set_param(&a,FAKE_PARAM_GAIN,32)==MD_OK);
    a.memory[0]=123;d->reset(&a);CHECK(a.memory[0]==0 && a.gain==32 && a.tempo==120u*256u);
    return 0;
}
int main(void)
{
    RUN(instrument_callbacks);RUN(instrument_limits_and_variants);
    RUN(partition_and_trace);RUN(effect_buffers_and_tails);
    return 0;
}
