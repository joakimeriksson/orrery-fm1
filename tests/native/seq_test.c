/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: module/seq.c core/transport.c core/instrument.c core/router.c tests/native/fake_processors.c
#include "module/seq.h"
#include "core/router.h"
#include "fake_processors.h"
#include "test.h"
#include <string.h>

#define LOG_MAX 4096u
typedef struct {uint32_t token;int32_t base[MD_TRACKS];uint8_t fail_base,bad_token;md_router_t *router;} context_t;
typedef struct {uint32_t sample;md_seq_event_t event;} record_t;
typedef struct {record_t row[LOG_MAX];uint32_t count;} log_t;
typedef struct {md_seq_bank_t bank;md_seq_t seq;md_transport_t clock;context_t context;} fixture_t;
static md_note_token_t allocate(void *context)
{
    context_t *c=context;if(c->bad_token)return 0;
    if(c->router)return md_router_next_token(c->router);
    c->token++;if(!c->token)c->token++;return c->token;
}
static int base(void *context,uint8_t track,uint16_t target,int32_t *value)
{
    context_t *c=context;(void)target;if(c->fail_base)return MD_ERR_NOT_FOUND;*value=c->base[track];return MD_OK;
}
static void defaults(fixture_t *f)
{
    memset(f,0,sizeof *f);f->bank.seed=0x12345678u;
    for(uint32_t i=0;i<3;i++)f->bank.global_owner[i]=MD_AUTOMATION_UNOWNED;
    for(uint32_t t=0;t<MD_TRACKS;t++){
        f->context.base[t]=40;
        for(uint32_t p=0;p<4;p++){
            f->bank.pattern[t][p].meta=(md_pattern_meta_t){.length=3,.division=MD_DIV_16TH,.swing=50,.mode=MD_PATTERN_MONO};
            for(uint32_t k=0;k<64;k++)f->bank.pattern[t][p].step[k]=(md_step_t){.payload=(uint16_t)(60u+k%3u),.velocity=100,.probability=100};
        }
    }
}
static int start(fixture_t *f)
{
    if(md_seq_init(&f->seq,&f->bank,(md_seq_callbacks_t){&f->context,allocate,base}))return 1;
    if(md_transport_init(&f->clock,120u*256u) || md_transport_start(&f->clock,(md_position_t){0}))return 1;
    if(md_seq_start(&f->seq,(md_position_t){0}))return 1;
    for(uint8_t t=1;t<MD_TRACKS;t++)if(md_seq_mute(&f->seq,t,1))return 1;return 0;
}
static int run(fixture_t *f,uint32_t frames,uint32_t partition,log_t *log,int perturb)
{
    while(frames){
        uint32_t n=partition?partition:1u+((f->clock.samples*1664525u+1013904223u)>>24);
        if(n>frames)n=frames;
        if(perturb)f->context.token+=13;
        int status=md_seq_begin(&f->seq,&f->clock,n);if(status)return status;
        md_seq_event_t event;uint32_t offset=0;uint8_t priority=0;
        while((status=md_seq_next(&f->seq,&event))>0){
            if(event.timed.offset<offset || (event.timed.offset==offset && event.priority<priority))return MD_ERR_STATE;
            offset=event.timed.offset;priority=event.priority;
            if(log){if(log->count>=LOG_MAX)return MD_ERR_CAPACITY;log->row[log->count++]=(record_t){f->clock.samples+offset,event};}
            if(f->context.router){int applied=md_router_event(f->context.router,&event.timed.event);if(applied)return applied;}
        }
        if(status<0)return status;
        status=md_transport_advance(&f->clock,n);if(status)return status;frames-=n;
    }
    return MD_OK;
}
static uint32_t count_kind(const log_t *log,md_event_kind_t kind)
{
    uint32_t count=0;for(uint32_t i=0;i<log->count;i++)count+=log->row[i].event.timed.event.kind==kind;return count;
}
static int compare_notes(const log_t *a,const log_t *b)
{
    uint32_t ia=0,ib=0;
    while(ia<a->count || ib<b->count){
        while(ia<a->count && a->row[ia].event.timed.event.kind!=MD_EVENT_NOTE_ON)ia++;
        while(ib<b->count && b->row[ib].event.timed.event.kind!=MD_EVENT_NOTE_ON)ib++;
        if(ia==a->count || ib==b->count)return ia==a->count && ib==b->count;
        const md_event_t *x=&a->row[ia].event.timed.event,*y=&b->row[ib].event.timed.event;
        if(a->row[ia].sample!=b->row[ib].sample || x->track!=y->track || x->data.note.key!=y->data.note.key || x->flags!=y->flags)return 0;
        ia++;ib++;
    }
    return 1;
}
static int division_and_partition_timing(void)
{
    const uint32_t parts[]={1,17,64,255,256,0};static log_t reference,other;
    for(uint8_t d=0;d<6;d++)for(uint32_t p=0;p<6;p++){
        fixture_t f;defaults(&f);md_seq_pattern_t *pat=&f.bank.pattern[0][0];pat->meta.division=d;
        for(uint32_t k=0;k<3;k++)pat->step[k].gate=25;
        CHECK(!start(&f));log_t *log=p?&other:&reference;memset(log,0,sizeof *log);
        uint32_t base_units=md_step_straight_units(d);
        uint32_t frames=(uint32_t)(((uint64_t)(base_units*3u+base_units/2u)*22050u+9599u)/9600u)+1u;
        CHECK(run(&f,frames,parts[p],log,0)==MD_OK);
        uint32_t visit=0;
        for(uint32_t i=0;i<log->count;i++)if(log->row[i].event.timed.event.kind==MD_EVENT_NOTE_ON){
            CHECK(log->row[i].sample==((uint64_t)visit*base_units*22050u+9599u)/9600u);
            CHECK(log->row[i].event.timed.event.data.note.key==60u+visit%3u);visit++;
        }
        CHECK(visit==4 && !f.seq.overflows);
        if(p)CHECK(compare_notes(&reference,&other));
    }
    return 0;
}
static int chords_drums_ties_and_slide(void)
{
    fixture_t f;static log_t log;defaults(&f);
    md_seq_pattern_t *p=&f.bank.pattern[0][0];p->meta.mode=MD_PATTERN_CHORD;
    p->step[0]=(md_step_t){.payload=60u|(MD_CHORD_MINOR7<<8),.velocity=90,.gate=1,.probability=100};
    p->step[1]=(md_step_t){.payload=0,.velocity=90,.gate=100,.flags=MD_STEP_TIE,.probability=100};
    CHECK(!start(&f));memset(&log,0,sizeof log);CHECK(run(&f,11026,17,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_NOTE_ON)==4 && !count_kind(&log,MD_EVENT_NOTE_OFF));
    const uint8_t keys[]={60,63,67,70};uint32_t k=0;int release=0;
    for(uint32_t i=0;i<log.count;i++){
        const md_event_t *e=&log.row[i].event.timed.event;
        if(e->kind==MD_EVENT_NOTE_ON){CHECK(log.row[i].sample==0 && e->data.note.key==keys[k++]);}
        if(e->kind==MD_EVENT_SOURCE_RELEASE && e->track==0 && log.row[i].sample==11025)release=1;
    }
    CHECK(release);
    defaults(&f);p=&f.bank.pattern[0][0];p->step[0].payload=48;p->step[0].gate=1;
    p->step[1].payload=55;p->step[1].gate=50;p->step[1].flags=MD_STEP_SLIDE;
    CHECK(!start(&f));memset(&log,0,sizeof log);CHECK(run(&f,6000,255,&log,0)==MD_OK);
    int on_index=-1,off_index=-1;
    for(uint32_t i=0;i<log.count;i++)if(log.row[i].sample==5513){
        const md_event_t *e=&log.row[i].event.timed.event;
        if(e->kind==MD_EVENT_NOTE_ON){on_index=(int)i;CHECK(e->flags==MD_NOTE_SLIDE && e->data.note.key==55);}
        if(e->kind==MD_EVENT_NOTE_OFF)off_index=(int)i;
    }
    CHECK(on_index>=0 && off_index>on_index);
    defaults(&f);for(uint32_t t=0;t<4;t++){
        f.bank.pattern[t][0].meta.mode=MD_PATTERN_DRUM;
        f.bank.pattern[t][0].step[0]=(md_step_t){.payload=0x3fff,.velocity=80,.gate=100,.flags=MD_STEP_ACCENT,.probability=100};
    }
    CHECK(!start(&f));for(uint8_t t=1;t<4;t++)CHECK(md_seq_mute(&f.seq,t,0)==MD_OK);
    memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_HIT)==56);
    for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_HIT)
        CHECK(!log.row[i].sample && log.row[i].event.timed.event.data.hit.velocity==127);
    return 0;
}
static int chance_swing_odd_loops_and_invalid_pitch(void)
{
    const uint32_t parts[]={1,17,64,255,256,0};static log_t expected,actual;
    for(uint32_t part=0;part<6;part++){
        fixture_t f;defaults(&f);md_seq_pattern_t *p=&f.bank.pattern[0][0];p->meta.swing=67;
        for(uint32_t i=0;i<3;i++){p->step[i].gate=50;p->step[i].probability=47;}
        CHECK(!start(&f));log_t *log=part?&actual:&expected;memset(log,0,sizeof *log);
        CHECK(run(&f,44100*2,parts[part],log,part==5)==MD_OK);
        CHECK(count_kind(log,MD_EVENT_NOTE_ON)>0 && count_kind(log,MD_EVENT_NOTE_ON)<16);
        if(part)CHECK(compare_notes(&expected,&actual));
    }
    fixture_t f;defaults(&f);md_seq_pattern_t *p=&f.bank.pattern[0][0];p->meta.swing=75;
    for(uint32_t i=0;i<3;i++)p->step[i].gate=100;
    CHECK(!start(&f));memset(&actual,0,sizeof actual);CHECK(run(&f,44100,0,&actual,0)==MD_OK);
    uint32_t visit=0;
    for(uint32_t i=0;i<actual.count;i++)if(actual.row[i].event.timed.event.kind==MD_EVENT_NOTE_ON){
        uint32_t units=(visit/2u)*4800u+(visit&1u?3600u:0u);
        CHECK(actual.row[i].sample==((uint64_t)units*22050u+9599u)/9600u);visit++;
    }
    CHECK(visit>=8); /* parity continues after the three-step pattern wraps */
    defaults(&f);f.bank.pattern[0][0].meta.transpose=-48;f.bank.pattern[0][0].step[0].payload=1;f.bank.pattern[0][0].step[0].gate=100;
    CHECK(!start(&f));memset(&actual,0,sizeof actual);CHECK(run(&f,256,256,&actual,0)==MD_OK);
    CHECK(!count_kind(&actual,MD_EVENT_NOTE_ON) && f.seq.dropped_notes==1);return 0;
}
static int locks_restore_live_base_and_stop(void)
{
    fixture_t f;defaults(&f);f.bank.pattern[0][0].step[0].gate=100;
    f.bank.lock_count=2;f.bank.locks[0]=(md_lock_t){.step=0,.target=1,.value=80};
    f.bank.locks[1]=(md_lock_t){.step=1,.target=1,.value=20};
    CHECK(!start(&f));static log_t log;memset(&log,0,sizeof log);
    CHECK(run(&f,256,256,&log,0)==MD_OK && f.seq.track[0].lock.active);
    f.context.base[0]=55;CHECK(run(&f,5514-256,17,&log,0)==MD_OK);
    uint32_t params=0;
    for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_PARAM){
        int32_t value=log.row[i].event.timed.event.data.param.value;
        if(params==0)CHECK(log.row[i].sample==0 && value==80);
        else if(params==1)CHECK(log.row[i].sample==5513 && value==55);
        else if(params==2)CHECK(log.row[i].sample==5513 && value==20);
        params++;
    }
    CHECK(params==3 && f.seq.track[0].lock.active);
    CHECK(md_seq_stop(&f.seq)==MD_OK && md_transport_stop(&f.clock)==MD_OK);
    memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_PARAM)==1 && log.row[0].event.timed.event.data.param.value==55);
    CHECK(!f.seq.track[0].lock.active && count_kind(&log,MD_EVENT_SOURCE_RELEASE)==MD_TRACKS);
    CHECK(!start(&f));memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    f.context.fail_base=1;memset(&log,0,sizeof log);
    CHECK(run(&f,5514-256,17,&log,0)==MD_OK && f.seq.base_errors==2);
    CHECK(count_kind(&log,MD_EVENT_PARAM)==1 && log.row[0].event.timed.event.data.param.value==55);
    CHECK(!f.seq.track[0].lock.active); /* cached base fallback; bad new lock is skipped */
    return 0;
}
static int queued_immediate_and_stopped_launch(void)
{
    fixture_t f;static log_t log;defaults(&f);
    f.bank.pattern[0][0].meta.length=1;f.bank.pattern[0][0].step[0].gate=50;
    f.bank.pattern[0][1].meta.length=1;f.bank.pattern[0][1].step[0].payload=72;f.bank.pattern[0][1].step[0].gate=50;
    CHECK(!start(&f));CHECK(run(&f,1000,256,NULL,0)==MD_OK);
    CHECK(md_seq_launch(&f.seq,&f.clock,0,1,0)==MD_OK && f.seq.track[0].queued);
    memset(&log,0,sizeof log);CHECK(run(&f,88201-1000,255,&log,0)==MD_OK);
    int found=0;for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_NOTE_ON &&
        log.row[i].event.timed.event.data.note.key==72){CHECK(log.row[i].sample==88200);found++;}
    CHECK(found==1 && f.seq.track[0].pattern==1 && !f.seq.track[0].queued);
    CHECK(md_seq_stop(&f.seq)==MD_OK && md_transport_stop(&f.clock)==MD_OK);
    CHECK(run(&f,1,1,NULL,0)==MD_OK);
    CHECK(md_transport_start(&f.clock,(md_position_t){0})==MD_OK && md_seq_start(&f.seq,(md_position_t){0})==MD_OK);
    CHECK(md_seq_launch(&f.seq,&f.clock,0,0,1)==MD_OK);CHECK(run(&f,1000,256,NULL,0)==MD_OK);
    CHECK(md_seq_launch(&f.seq,&f.clock,0,1,1)==MD_OK);uint32_t expected;
    CHECK(md_transport_offset(&f.clock,f.seq.track[0].launch_at,256,&expected)==MD_OK);
    uint32_t begin=f.clock.samples;memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    found=0;for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_NOTE_ON){
        CHECK(log.row[i].event.timed.event.data.note.key==72 && log.row[i].sample==begin+expected);found++;
    }
    CHECK(found==1 && expected<8);
    CHECK(md_seq_stop(&f.seq)==MD_OK && md_transport_stop(&f.clock)==MD_OK);
    CHECK(md_seq_launch(&f.seq,&f.clock,0,0,0)==MD_OK && !f.seq.track[0].queued && f.seq.track[0].pattern==0);
    CHECK(md_seq_start(&f.seq,(md_position_t){0})==MD_OK && md_transport_start(&f.clock,(md_position_t){0})==MD_OK);
    memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_NOTE_ON)==1);return 0;
}
static int source_stop_preserves_live_note(void)
{
    fixture_t f;defaults(&f);f.bank.pattern[0][0].step[0].gate=100;CHECK(!start(&f));
    fake_instrument_state_t state;md_instrument_t inst;md_router_t router;
    CHECK(md_instrument_init(&inst,&FAKE_INSTRUMENT,&state,sizeof state,MD_SAMPLE_RATE)==MD_OK);
    md_instrument_t *tracks[MD_TRACKS]={&inst};CHECK(md_router_init(&router,tracks)==MD_OK);
    f.context.router=&router;md_event_t live={.kind=MD_EVENT_NOTE_ON,.source=MD_SOURCE_PANEL,.token=md_router_next_token(&router)};
    live.data.note.key=60;live.data.note.velocity=100;CHECK(md_router_event(&router,&live)==MD_OK);
    CHECK(run(&f,256,256,NULL,0)==MD_OK && md_router_owned_count(&router)==2 && state.voice_count==1);
    CHECK(md_seq_stop(&f.seq)==MD_OK && md_transport_stop(&f.clock)==MD_OK);
    CHECK(run(&f,256,256,NULL,0)==MD_OK && md_router_owned_count(&router)==1 && state.voice_count==1);
    live.kind=MD_EVENT_NOTE_OFF;live.data.note.key=0;live.data.note.velocity=0;
    CHECK(md_router_event(&router,&live)==MD_OK && !state.voice_count);return 0;
}
static int slide_without_predecessor_and_length_edit(void)
{
    fixture_t f;static log_t log;defaults(&f);
    f.bank.pattern[0][0].step[0].gate=100;f.bank.pattern[0][0].step[0].flags=MD_STEP_SLIDE;
    CHECK(!start(&f));memset(&log,0,sizeof log);CHECK(run(&f,256,256,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_NOTE_ON)==1);
    for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_NOTE_ON)
        CHECK(!log.row[i].event.timed.event.flags);
    defaults(&f);f.bank.pattern[0][0].step[0].gate=100;
    f.bank.pattern[0][0].step[1].gate=100;f.bank.pattern[0][0].step[1].payload=99;
    CHECK(!start(&f));CHECK(run(&f,256,256,NULL,0)==MD_OK && f.seq.track[0].cursor==1);
    f.bank.pattern[0][0].meta.length=1;CHECK(md_seq_bank_validate(&f.bank)==MD_OK);
    memset(&log,0,sizeof log);CHECK(run(&f,5514-256,256,&log,0)==MD_OK);
    CHECK(count_kind(&log,MD_EVENT_NOTE_ON)==1);
    for(uint32_t i=0;i<log.count;i++)if(log.row[i].event.timed.event.kind==MD_EVENT_NOTE_ON)
        CHECK(log.row[i].sample==5513 && log.row[i].event.timed.event.data.note.key==60);
    CHECK(!f.seq.track[0].cursor);return 0;
}
static int validation_failures_and_silent_catchup_bound(void)
{
    fixture_t f;defaults(&f);f.bank.pattern[0][0].meta.division=6;CHECK(md_seq_bank_validate(&f.bank)==MD_ERR_RANGE);
    defaults(&f);f.bank.lock_count=1;f.bank.locks[0]=(md_lock_t){.step=0,.target=MD_TARGET_ECHO|1,.value=30};
    CHECK(md_seq_bank_validate(&f.bank)==MD_ERR_RANGE);f.bank.global_owner[0]=0;CHECK(md_seq_bank_validate(&f.bank)==MD_OK);
    defaults(&f);f.bank.pattern[0][0].step[0].flags=MD_STEP_TIE;CHECK(md_seq_bank_validate(&f.bank)==MD_ERR_RANGE);
    defaults(&f);f.bank.pattern[0][0].step[0].gate=100;CHECK(!start(&f));f.context.bad_token=1;
    CHECK(run(&f,256,256,NULL,0)==MD_OK && f.seq.dropped_notes==1);
    defaults(&f);CHECK(!start(&f));CHECK(md_seq_mute(&f.seq,0,1)==MD_OK);
    CHECK(md_transport_start(&f.clock,(md_position_t){.quarter=1000000})==MD_OK);
    CHECK(md_seq_begin(&f.seq,&f.clock,256)==MD_OK);
    CHECK(md_seq_begin(&f.seq,&f.clock,256)==MD_ERR_STATE && md_seq_mute(&f.seq,0,0)==MD_ERR_STATE);
    md_seq_event_t e;int status;uint32_t events=0;
    while((status=md_seq_next(&f.seq,&e))>0)events++;
    CHECK(status==MD_ERR_CAPACITY && f.seq.overflows==1 && !f.seq.running && events<MD_BLOCK_EVENT_LIMIT);
    CHECK(f.seq.visited==MD_SEQ_STEP_LIMIT && !f.seq.iterating);
    CHECK(md_seq_begin(&f.seq,&f.clock,256)==MD_OK);events=0;
    while((status=md_seq_next(&f.seq,&e))>0){CHECK(e.timed.event.kind==MD_EVENT_SOURCE_RELEASE);events++;}
    CHECK(!status && events==MD_TRACKS);return 0;
}
int main(void)
{
    RUN(division_and_partition_timing);RUN(chords_drums_ties_and_slide);RUN(chance_swing_odd_loops_and_invalid_pitch);
    RUN(locks_restore_live_base_and_stop);RUN(queued_immediate_and_stopped_launch);RUN(source_stop_preserves_live_note);
    RUN(slide_without_predecessor_and_length_edit);RUN(validation_failures_and_silent_catchup_bound);return 0;
}
