/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/transport.c
#include "core/transport.h"
#include "test.h"
#include <stdint.h>
#include <string.h>

static int advance_frames(md_transport_t *t, uint32_t frames, uint32_t partition)
{
    while (frames) {
        uint32_t size=partition?partition:1u+((frames*1664525u+1013904223u)>>24);
        uint32_t n=frames<size?frames:size;
        if (md_transport_advance(t,n)) return 1;frames-=n;
    }
    return 0;
}
static int oracle(const md_transport_t *t, uint64_t numerator)
{
    uint64_t units=numerator/MD_TRANSPORT_DENOMINATOR;
    CHECK(t->position.quarter==(uint32_t)(units/MD_UNITS_PER_QUARTER));
    CHECK(t->position.unit==units%MD_UNITS_PER_QUARTER);
    CHECK(t->remainder==numerator%MD_TRANSPORT_DENOMINATOR);return 0;
}
static int long_run_partitions(void)
{
    const uint16_t tempos[]={40u*256u,73u*256u,97u*256u,120u*256u,137u*256u,
        173u*256u,240u*256u,73u*256u+128u};
    const uint32_t parts[]={1,17,64,255,256,0};uint32_t frames=MD_SAMPLE_RATE*600u+137u;
    for (uint32_t b=0;b<sizeof tempos/sizeof tempos[0];b++) for (uint32_t p=0;p<6;p++) {
        md_transport_t t;CHECK(md_transport_init(&t,tempos[b])==MD_OK);
        CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK);
        CHECK(!advance_frames(&t,frames,parts[p]));
        CHECK(!oracle(&t,(uint64_t)frames*tempos[b]*MD_UNITS_PER_QUARTER));
        CHECK(t.samples==frames);
    }
    return 0;
}
static int tempo_stop_continue_and_seek(void)
{
    md_transport_t t;CHECK(md_transport_init(&t,120u*256u)==MD_OK);
    CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK);
    CHECK(md_transport_advance(&t,123)==MD_OK);
    uint32_t rem=t.remainder;md_position_t before=t.position;
    CHECK(md_transport_set_tempo(&t,73u*256u+128u)==MD_OK);
    CHECK(t.remainder==rem && t.position.quarter==before.quarter && t.position.unit==before.unit);
    CHECK(md_transport_advance(&t,217)==MD_OK);
    CHECK(md_transport_stop(&t)==MD_OK);before=t.position;rem=t.remainder;
    CHECK(md_transport_advance(&t,256)==MD_OK);
    CHECK(t.position.quarter==before.quarter && t.position.unit==before.unit && t.remainder==rem);
    uint32_t offset=99;
    CHECK(md_transport_offset(&t,t.position,256,&offset)==MD_ERR_STATE && offset==99);
    CHECK(md_transport_continue(&t)==MD_OK && md_transport_advance(&t,17)==MD_OK);
    uint64_t numerator=((uint64_t)123*120u*256u+(uint64_t)234*(73u*256u+128u))*MD_UNITS_PER_QUARTER;
    CHECK(!oracle(&t,numerator) && t.samples==613);
    CHECK(md_transport_seek(&t,(md_position_t){.quarter=17,.unit=153})==MD_ERR_STATE);
    CHECK(md_transport_stop(&t)==MD_OK && md_transport_seek(&t,(md_position_t){.quarter=17,.unit=153})==MD_OK);
    CHECK(t.position.quarter==17 && t.position.unit==153 && !t.remainder && t.samples==613);
    CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK && t.running && !t.position.quarter && !t.position.unit);
    CHECK(t.samples==613);return 0;
}
static int offset_queries_and_wrap(void)
{
    md_transport_t t;CHECK(md_transport_init(&t,137u*256u+64u)==MD_OK);
    CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK);
    for (uint32_t k=0;k<1000;k++) {
        CHECK(md_transport_advance(&t,(k*17u)%257u)==MD_OK);
        for (uint32_t delta=0;delta<=223;delta+=7) {
            md_position_t target;CHECK(md_position_add(t.position,delta,&target)==MD_OK);
            uint32_t offset=999;
            uint64_t goal=(uint64_t)delta*MD_TRANSPORT_DENOMINATOR;
            uint32_t rate=t.bpm_q8*MD_UNITS_PER_QUARTER;
            uint32_t expected=goal<=t.remainder?0u:(uint32_t)((goal-t.remainder+rate-1u)/rate);
            int status=md_transport_offset(&t,target,256,&offset);
            if (expected>256) CHECK(status==MD_ERR_NOT_FOUND && offset==999);
            else CHECK(status==MD_OK && offset==expected);
        }
    }
    CHECK(md_transport_set_tempo(&t,120u*256u)==MD_OK);
    CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK);
    md_position_t target={.unit=100};uint32_t offset=999;
    CHECK(md_transport_offset(&t,target,230,&offset)==MD_OK && offset==230);
    CHECK(md_transport_offset(&t,target,229,&offset)==MD_ERR_NOT_FOUND); /* boundary deferred */
    t.samples=UINT32_MAX-10u;
    CHECK(md_transport_start(&t,(md_position_t){.quarter=UINT32_MAX,.unit=9599})==MD_OK);
    target=(md_position_t){0};CHECK(md_transport_offset(&t,target,17,&offset)==MD_OK && offset>0 && offset<17);
    CHECK(md_transport_advance(&t,17)==MD_OK && t.samples==6 && t.position.quarter==0);
    target=(md_position_t){.quarter=UINT32_MAX,.unit=9599};
    CHECK(md_transport_offset(&t,target,256,&offset)==MD_OK && offset==0);
    md_position_t added;CHECK(md_position_add((md_position_t){.quarter=UINT32_MAX,.unit=9599},1,&added)==MD_OK);
    CHECK(!added.quarter && !added.unit);
    CHECK(md_position_add((md_position_t){.unit=123},UINT32_MAX,&added)==MD_OK);
    CHECK(added.quarter==((uint64_t)123+UINT32_MAX)/9600 && added.unit==((uint64_t)123+UINT32_MAX)%9600);
    return 0;
}
static int capture(uint16_t tempo, uint32_t partition, uint32_t straight, uint8_t swing, uint32_t *times, uint32_t count)
{
    md_transport_t t;CHECK(md_transport_init(&t,tempo)==MD_OK);
    CHECK(md_transport_start(&t,(md_position_t){0})==MD_OK);
    md_position_t target={0};uint32_t got=0,frames=0;
    while (got<count) {
        uint32_t n=partition?partition:1u+((frames*1103515245u+12345u)>>24);
        uint32_t offset;
        while (md_transport_offset(&t,target,n,&offset)==MD_OK && offset<n && got<count) {
            times[got]=frames+offset;
            /* Independent caller scheduling, not md_step_span_units under test. */
            uint32_t interval=straight*2u*((got&1u)?100u-swing:swing)/100u;
            CHECK(md_position_add(target,interval,&target)==MD_OK);got++;
        }
        CHECK(md_transport_advance(&t,n)==MD_OK);frames+=n;
    }
    return 0;
}
static int sample_events_divisions_and_swing(void)
{
    const uint32_t parts[]={1,17,64,255,256,0};
    const uint32_t expected_base[]={1200,2400,4800,9600,19200,38400};
    const uint8_t swings[]={50,51,67,75};
    uint16_t tempo=137u*256u+64u;uint32_t times[12];
    for (uint8_t d=0;d<6;d++) {
        CHECK(md_step_straight_units(d)==expected_base[d]);
        for (uint32_t s=0;s<4;s++) {
            uint32_t even=md_step_span_units(d,swings[s],0),odd=md_step_span_units(d,swings[s],1);
            CHECK(even+odd==2u*expected_base[d]);
            CHECK(even==2u*expected_base[d]*swings[s]/100u);
            /* Across a length-three loop parity DOES NOT reset: visit3 is odd. */
            CHECK(md_step_span_units(d,swings[s],3u&1u)==odd);
            for (uint32_t p=0;p<6;p++) {
                CHECK(!capture(tempo,parts[p],expected_base[d],swings[s],times,12));
                for (uint32_t i=0;i<12;i++) {
                    uint64_t units=(i/2u)*2u*expected_base[d]+(i&1u?even:0);
                    uint64_t goal=units*MD_TRANSPORT_DENOMINATOR,rate=tempo*MD_UNITS_PER_QUARTER;
                    CHECK(times[i]==(goal+rate-1u)/rate);
                }
            }
        }
    }
    /* The internal96PPQN tick also uses the same offset contract. */
    CHECK(!capture(tempo,255,MD_UNITS_PER_TICK,50,times,12));
    for (uint32_t i=0;i<12;i++) {
        uint64_t goal=(uint64_t)i*100u*MD_TRANSPORT_DENOMINATOR,rate=tempo*MD_UNITS_PER_QUARTER;
        CHECK(times[i]==(goal+rate-1u)/rate);
    }
    return 0;
}
static int invalid_input_is_nonmutating(void)
{
    md_transport_t t,before;CHECK(md_transport_init(&t,120u*256u)==MD_OK);before=t;
    CHECK(md_transport_set_tempo(&t,39u*256u)==MD_ERR_RANGE && !memcmp(&t,&before,sizeof t));
    CHECK(md_transport_start(&t,(md_position_t){.unit=9600})==MD_ERR_RANGE && !memcmp(&t,&before,sizeof t));
    CHECK(md_transport_advance(&t,257)==MD_ERR_RANGE && !memcmp(&t,&before,sizeof t));
    CHECK(md_step_straight_units(6)==0 && !md_step_span_units(0,49,0) && !md_step_span_units(0,76,0));
    CHECK(!md_step_span_units(0,50,2));return 0;
}
int main(void)
{
    RUN(long_run_partitions);RUN(tempo_stop_continue_and_seek);RUN(offset_queries_and_wrap);
    RUN(sample_events_divisions_and_swing);RUN(invalid_input_is_nonmutating);return 0;
}
