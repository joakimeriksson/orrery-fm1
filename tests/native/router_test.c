/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c core/router.c tests/native/fake_processors.c
#include "core/router.h"
#include "fake_processors.h"
#include "test.h"
#include <string.h>

typedef struct {md_router_t router;md_instrument_t inst[2];fake_instrument_state_t state[2];} fixture_t;
static int init(fixture_t *f, int mono)
{
    if (md_instrument_init(&f->inst[0],mono?&FAKE_INSTRUMENT_MONO:&FAKE_INSTRUMENT,&f->state[0],sizeof f->state[0],MD_SAMPLE_RATE)) return 1;
    if (md_instrument_init(&f->inst[1],&FAKE_INSTRUMENT,&f->state[1],sizeof f->state[1],MD_SAMPLE_RATE)) return 1;
    md_instrument_t *tracks[MD_TRACKS]={&f->inst[0],&f->inst[1],NULL,NULL};
    return md_router_init(&f->router,tracks)!=MD_OK;
}
static md_event_t note(md_event_kind_t kind, uint32_t token, uint16_t source, uint8_t key)
{
    md_event_t e={0};e.kind=kind;e.token=token;e.source=source;
    if (kind==MD_EVENT_NOTE_ON) {e.data.note.key=key;e.data.note.velocity=90;}return e;
}
static int send(fixture_t *f, md_event_kind_t kind, uint32_t token, uint16_t source, uint8_t key)
{md_event_t e=note(kind,token,source,key);return md_router_event(&f->router,&e);}
static int duplicate_pitch_and_token_lifetime(void)
{
    fixture_t f;CHECK(!init(&f,0));
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,0,60)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_ON,2,MD_SOURCE_PANEL,60)==MD_OK);
    CHECK(f.state[0].voice_count==1 && md_router_owned_count(&f.router)==2 && f.state[0].event_calls==1);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,1,0,0)==MD_OK && f.state[0].voice_count==1);
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,0,62)==MD_ERR_STATE); /* still physical token */
    CHECK(send(&f,MD_EVENT_NOTE_OFF,1,0,0)==MD_OK && f.state[0].voice_count==1);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,2,8,0)==MD_ERR_ARGUMENT);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,2,MD_SOURCE_PANEL,0)==MD_OK && f.state[0].voice_count==0);
    CHECK(f.state[0].trace_count==2 && f.state[0].trace[1].event.token==1);
    CHECK(!md_router_owned_count(&f.router));return 0;
}
static int mono_priority_and_resume(void)
{
    fixture_t f;CHECK(!init(&f,1));
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,0,48)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_ON,2,MD_SOURCE_PANEL,72)==MD_OK);
    CHECK(f.state[0].voice_count==1 && f.state[0].voices[0].key==72 && f.state[0].max_voice_count==1);
    CHECK(send(&f,MD_EVENT_NOTE_ON,3,0,50)==MD_OK); /* seq can change under live */
    CHECK(f.state[0].voices[0].key==72 && md_router_owned_count(&f.router)==3);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,2,MD_SOURCE_PANEL,0)==MD_OK && f.state[0].voices[0].key==50);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,3,0,0)==MD_OK && f.state[0].voices[0].key==48);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,1,0,0)==MD_OK && !f.state[0].voice_count);
    CHECK(f.state[0].max_voice_count==1);return 0;
}
static int sustain_and_source_release(void)
{
    fixture_t f;CHECK(!init(&f,0));
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,8,60)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_ON,2,9,60)==MD_OK);
    CHECK(md_router_sustain(&f.router,0,8,1)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,1,8,0)==MD_OK && md_router_owned_count(&f.router)==2);
    CHECK(md_router_sustain(&f.router,0,9,0)==MD_OK && md_router_owned_count(&f.router)==2);
    md_event_t release={.kind=MD_EVENT_SOURCE_RELEASE,.track=MD_ALL_TRACKS,.source=8};
    CHECK(md_router_event(&f.router,&release)==MD_OK && md_router_owned_count(&f.router)==1 && f.state[0].voice_count==1);
    CHECK(!f.router.sustain[0][8]);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,2,9,0)==MD_OK && !f.state[0].voice_count);
    CHECK(send(&f,MD_EVENT_NOTE_ON,3,8,62)==MD_OK);
    CHECK(md_router_sustain(&f.router,0,8,1)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,3,8,0)==MD_OK);
    CHECK(md_router_sustain(&f.router,0,8,0)==MD_OK && !md_router_owned_count(&f.router) && !f.state[0].voice_count);
    return 0;
}
static int admission_capacity_and_wrap(void)
{
    fixture_t f;CHECK(!init(&f,0));
    for (uint32_t i=1;i<=FAKE_MAX_VOICES;i++) CHECK(send(&f,MD_EVENT_NOTE_ON,i,0,(uint8_t)(59+i))==MD_OK);
    uint32_t events=f.state[0].event_calls;
    CHECK(send(&f,MD_EVENT_NOTE_ON,10,0,70)==MD_ERR_CAPACITY && f.state[0].event_calls==events);
    CHECK(send(&f,MD_EVENT_NOTE_ON,10,0,60)==MD_OK); /* duplicate pitch uses no slot */
    CHECK(send(&f,MD_EVENT_NOTE_ON,10,0,61)==MD_ERR_STATE);
    f.router.next_token=UINT32_MAX;CHECK(md_router_next_token(&f.router)==5);
    md_event_t panic={.kind=MD_EVENT_PANIC,.track=MD_ALL_TRACKS,.source=MD_SOURCE_NONE};
    CHECK(md_router_event(&f.router,&panic)==MD_OK);
    for (uint32_t i=1;i<=MD_MAX_OWNED_NOTES;i++) CHECK(send(&f,MD_EVENT_NOTE_ON,i,0,60)==MD_OK);
    CHECK(md_router_owned_count(&f.router)==MD_MAX_OWNED_NOTES);
    CHECK(send(&f,MD_EVENT_NOTE_ON,1000,0,60)==MD_ERR_CAPACITY);
    CHECK(f.state[0].voice_count==1 && f.router.rejected_notes==2);
    CHECK(md_router_event(&f.router,&panic)==MD_OK && !md_router_owned_count(&f.router) && !f.state[0].voice_count);
    CHECK(f.state[0].gain==64);return 0;
}
static int track_isolation_and_input_errors(void)
{
    fixture_t f;CHECK(!init(&f,0));
    md_event_t e=note(MD_EVENT_NOTE_ON,1,MD_SOURCE_PANEL,60);e.track=1;
    CHECK(md_router_event(&f.router,&e)==MD_OK && f.state[1].voice_count==1 && !f.state[0].voice_count);
    e=note(MD_EVENT_NOTE_OFF,1,MD_SOURCE_PANEL,0);CHECK(md_router_event(&f.router,&e)==MD_ERR_ARGUMENT);
    e=note(MD_EVENT_NOTE_ON,2,56,62);CHECK(md_router_event(&f.router,&e)==MD_ERR_RANGE);
    e=note(MD_EVENT_NOTE_ON,2,MD_SOURCE_PANEL,62);e.flags=MD_NOTE_ACCENT;
    CHECK(md_router_event(&f.router,&e)==MD_ERR_UNSUPPORTED && md_router_owned_count(&f.router)==1);
    e=(md_event_t){.kind=MD_EVENT_PANIC,.track=0,.source=MD_SOURCE_NONE};
    CHECK(md_router_event(&f.router,&e)==MD_OK && f.state[1].voice_count==1);
    e.track=MD_ALL_TRACKS;CHECK(md_router_event(&f.router,&e)==MD_OK && !f.state[1].voice_count);
    e=note(MD_EVENT_NOTE_OFF,999,MD_SOURCE_PANEL,0);e.track=2;
    CHECK(md_router_event(&f.router,&e)==MD_OK);
    e.source=56;CHECK(md_router_event(&f.router,&e)==MD_ERR_RANGE);
    e=note(MD_EVENT_NOTE_ON,999,MD_SOURCE_PANEL,60);e.track=2;
    CHECK(md_router_event(&f.router,&e)==MD_ERR_NOT_FOUND);
    md_instrument_t *aliased[MD_TRACKS]={&f.inst[0],&f.inst[0],NULL,NULL};
    CHECK(md_router_init(&f.router,aliased)==MD_ERR_ARGUMENT);
    CHECK(md_router_init(&f.router,f.router.track)==MD_ERR_ARGUMENT);return 0;
}
static int legato_backend(void *state, const md_event_t *e)
{
    fake_instrument_state_t *s=state;
    if (e->kind==MD_EVENT_NOTE_ON && (e->flags & MD_NOTE_LEGATO) && s->voice_count==1) {
        if (s->trace_count==FAKE_TRACE_CAPACITY) return MD_ERR_CAPACITY;
        s->trace[s->trace_count++]=(fake_trace_t){.frame=s->frame,.event=*e};s->event_calls++;
        s->voices[0]=(fake_voice_t){e->token,e->data.note.key,e->data.note.velocity};return MD_OK;
    }
    return FAKE_INSTRUMENT_MONO.event(state,e);
}
static int mono_legato_replaces_physical_token(void)
{
    fixture_t f;CHECK(!init(&f,1));
    md_instrument_desc_t desc=FAKE_INSTRUMENT_MONO;
    desc.capabilities|=MD_CAP_LEGATO;desc.event=legato_backend;
    CHECK(md_instrument_init(&f.inst[0],&desc,&f.state[0],sizeof f.state[0],MD_SAMPLE_RATE)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,0,48)==MD_OK);
    md_event_t e=note(MD_EVENT_NOTE_ON,2,MD_SOURCE_PANEL,72);e.flags=MD_NOTE_LEGATO;
    CHECK(md_router_event(&f.router,&e)==MD_OK);
    CHECK(f.state[0].voice_count==1 && f.state[0].voices[0].token==2 && f.state[0].voices[0].key==72);
    CHECK(f.state[0].trace[1].event.kind==MD_EVENT_NOTE_ON && f.state[0].trace[2].event.token==1);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,2,MD_SOURCE_PANEL,0)==MD_OK && f.state[0].voices[0].key==48);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,1,0,0)==MD_OK && !f.state[0].voice_count);return 0;
}
static int full_hybrid_backend(void *state, const md_event_t *e)
{
    if (e->kind==MD_EVENT_HIT) return MD_ERR_CAPACITY;
    return FAKE_INSTRUMENT.event(state,e);
}
static int transient_capacity_preserves_pitched_owners(void)
{
    fixture_t f;CHECK(!init(&f,0));
    const md_drum_voice_desc_t voices[]={{0,"DROP",60}};
    md_instrument_desc_t d=FAKE_INSTRUMENT;d.capabilities|=MD_CAP_PERCUSSION;
    d.drum_voice_count=1;d.drum_voices=voices;d.event=full_hybrid_backend;
    CHECK(md_instrument_init(&f.inst[0],&d,&f.state[0],sizeof f.state[0],MD_SAMPLE_RATE)==MD_OK);
    for (uint32_t i=0;i<FAKE_MAX_VOICES;i++) CHECK(send(&f,MD_EVENT_NOTE_ON,i+1u,MD_SOURCE_PANEL,(uint8_t)(60u+i))==MD_OK);
    fake_instrument_state_t before=f.state[0];
    md_event_t hit={.kind=MD_EVENT_HIT,.source=0};hit.data.hit.voice=0;hit.data.hit.velocity=100;
    CHECK(md_router_event(&f.router,&hit)==MD_ERR_CAPACITY);
    CHECK(!memcmp(&f.state[0],&before,sizeof before));
    CHECK(md_router_owned_count(&f.router)==FAKE_MAX_VOICES && !f.router.backend_errors && f.router.rejected_notes==1);
    for (uint32_t i=0;i<FAKE_MAX_VOICES;i++) CHECK(send(&f,MD_EVENT_NOTE_OFF,i+1u,MD_SOURCE_PANEL,0)==MD_OK);
    CHECK(!md_router_owned_count(&f.router) && !f.state[0].voice_count);return 0;
}
static int backend_failure_flushes_ownership(void)
{
    fixture_t f;CHECK(!init(&f,0));
    CHECK(send(&f,MD_EVENT_NOTE_ON,1,0,60)==MD_OK);
    f.state[0].trace_count=FAKE_TRACE_CAPACITY; /* fake refuses next event */
    CHECK(send(&f,MD_EVENT_NOTE_ON,2,MD_SOURCE_PANEL,62)==MD_ERR_STATE);
    CHECK(!md_router_owned_count(&f.router) && !f.state[0].voice_count && f.router.backend_errors==1);
    CHECK(send(&f,MD_EVENT_NOTE_ON,3,0,64)==MD_OK);
    CHECK(send(&f,MD_EVENT_NOTE_OFF,3,0,0)==MD_OK && !f.state[0].voice_count);return 0;
}
int main(void)
{
    RUN(duplicate_pitch_and_token_lifetime);RUN(mono_priority_and_resume);RUN(sustain_and_source_release);
    RUN(admission_capacity_and_wrap);RUN(track_isolation_and_input_errors);RUN(mono_legato_replaces_physical_token);RUN(transient_capacity_preserves_pitched_owners);RUN(backend_failure_flushes_ownership);
    return 0;
}
