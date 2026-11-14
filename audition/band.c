/* SPDX-License-Identifier: GPL-3.0-only */
#include "band.h"
#include <string.h>

static uint32_t slot(uint32_t mode,uint32_t track){return track==1 && mode?4:track;}
static md_note_token_t token(void *context)
{
    md_audition_context_t *c=context;return md_router_next_token(&c->band->router);
}
static int base_value(void *context,uint8_t track,uint16_t target,int32_t *value)
{
    md_audition_context_t *c=context;if(track>=4 || target>MD_PARAM_ID_MAX)return MD_ERR_NOT_FOUND;
    const md_processor_desc_t *d=&c->band->instrument[slot(c->mode,track)].desc->processor;
    for(uint32_t p=0;p<d->param_count;p++)if(d->params[p].id==target){*value=c->band->base[c->mode][track][p];return MD_OK;}
    return MD_ERR_NOT_FOUND;
}
static void patterns(md_seq_bank_t *b,uint32_t fx)
{
    static const uint8_t bass[]={40,47,52,47,43,47,50,47,40,47,52,55,50,47,43,47};
    static const uint8_t melody[]={76,71,67,69,72,69,76,0,71,67,64,0,74,71,67,0};
    static const uint8_t roots[]={52,57,52,55};
    static const int8_t transpose[]={0,2,-2,5};
    static const uint8_t recipes[]={0,2,1,3,0,4,2,1};
    memset(b,0,sizeof *b);b->seed=0x197604u;
    for(uint32_t i=0;i<3;i++)b->global_owner[i]=MD_AUTOMATION_UNOWNED;
    for(uint32_t t=0;t<MD_TRACKS;t++)for(uint32_t p=0;p<4;p++){
        md_seq_pattern_t *a=&b->pattern[t][p];a->meta=(md_pattern_meta_t){.length=16,.division=MD_DIV_16TH,.swing=50,.mode=MD_PATTERN_MONO};
        for(uint32_t k=0;k<64;k++)a->step[k]=(md_step_t){.velocity=100,.probability=100};
        if(t==0){
            a->meta.transpose=transpose[p];
            for(uint32_t k=0;k<16;k++)a->step[k]=(md_step_t){.payload=bass[k],.velocity=100,.gate=55,.probability=100};
            for(uint32_t k=0;k<4;k++)b->locks[b->lock_count++]=(md_lock_t){.step=(uint16_t)(p*64u+k*4u),.target=MD_ARP_CUTOFF,.value=(int16_t)(32u+k*9u)};
        }else if(t==1 && !fx){
            a->meta.length=32;a->meta.division=MD_DIV_8TH;a->meta.transpose=transpose[p];
            for(uint32_t k=0;k<16;k++)if(melody[k])a->step[k*2]=(md_step_t){.payload=melody[k],.velocity=100,.gate=180,.probability=100};
        }else if(t==1){
            a->meta.mode=MD_PATTERN_DRUM;a->meta.division=MD_DIV_8TH;
            for(uint32_t k=0;k<8;k++){
                a->step[k*2]=(md_step_t){.payload=(uint16_t)(1u<<recipes[k]),.velocity=100,.gate=100,.probability=100};
                b->locks[b->lock_count++]=(md_lock_t){.step=(uint16_t)(256u+p*64u+k*2u),.target=MD_FX_BASE_PITCH,.value=(int16_t)(melody[k]?melody[k]+transpose[p]:72+transpose[p])};
            }
        }else if(t==2){
            a->meta.mode=MD_PATTERN_CHORD;a->meta.division=MD_DIV_BAR;a->meta.length=4;a->meta.transpose=transpose[p];
            for(uint32_t k=0;k<4;k++)a->step[k]=(md_step_t){.payload=(uint16_t)(roots[k]|((k==3?MD_CHORD_MAJOR:MD_CHORD_MINOR)<<8)),.velocity=100,.gate=100,.probability=100};
        }else if(t==3){
            a->meta.mode=MD_PATTERN_DRUM;
            for(uint32_t k=0;k<16;k++){
                uint16_t hits=(k%2u==0)?1u<<MP_HH:0;
                if(k==0 || k==6 || k==8 || k==12)hits|=1u<<MP_BD;
                if(k==4 || k==12)hits|=1u<<MP_SD1;
                if(k==10)hits|=1u<<MP_CL;
                if(p&1u && k==14)hits|=1u<<MP_CO;
                a->step[k]=(md_step_t){.payload=hits,.velocity=95,.gate=hits?100:0,.probability=100};
            }
        }
    }
}
static int drain(md_audition_t *b)
{
    md_seq_t *seq=&b->seq[b->mode];int status=md_seq_begin(seq,&b->clock,1);if(status)return status;
    md_seq_event_t event;
    while((status=md_seq_next(seq,&event))>0){int applied=md_router_event(&b->router,&event.timed.event);if(applied)return applied;}
    return status;
}
static int bind(md_audition_t *b)
{
    md_instrument_t *tracks[MD_TRACKS]={0};for(uint32_t t=0;t<4;t++)tracks[t]=&b->instrument[slot(b->mode,t)];
    return md_router_init(&b->router,tracks);
}
int md_audition_init(md_audition_t *b)
{
    if(!b)return MD_ERR_ARGUMENT;memset(b,0,sizeof *b);
    const md_instrument_desc_t *d[]={&MD_ARP2600,&MD_ARP2600,&MD_EMINENT,&MD_MINIPOPS,&MD_ELECTRONIC_FX};
    void *state[]={&b->bass,&b->lead,&b->strings,&b->drums,&b->fx};
    const uint32_t sizes[]={sizeof b->bass,sizeof b->lead,sizeof b->strings,sizeof b->drums,sizeof b->fx};
    for(uint32_t i=0;i<5;i++){int r=md_instrument_init(&b->instrument[i],d[i],state[i],sizes[i],MD_SAMPLE_RATE);if(r)return r;}
    const md_preset_t *lead=&MD_ARP2600.presets[3];
    int status=md_sound_apply(&b->instrument[1],1,lead->settings,lead->settings_bytes);if(status)return status;
    status=bind(b);if(status)return status;
    status=md_transport_init(&b->clock,120u*256u);if(status)return status;
    for(uint8_t mode=0;mode<2;mode++){
        patterns(&b->bank[mode],mode);b->context[mode]=(md_audition_context_t){b,mode};
        for(uint32_t t=0;t<4;t++){
            md_instrument_t *inst=&b->instrument[slot(mode,t)];const md_processor_desc_t *p=&inst->desc->processor;
            if(p->param_count>40)return MD_ERR_CAPACITY;
            for(uint32_t k=0;k<p->param_count;k++){status=md_instrument_param_get(inst,p->params[k].id,&b->base[mode][t][k]);if(status)return status;}
        }
        status=md_seq_init(&b->seq[mode],&b->bank[mode],(md_seq_callbacks_t){&b->context[mode],token,base_value});if(status)return status;
    }
    return MD_OK;
}
int md_audition_play(md_audition_t *b,uint32_t on)
{
    if(!b || on>1)return MD_ERR_ARGUMENT;if(b->playing==on)return MD_OK;
    md_seq_t *s=&b->seq[b->mode];int status;
    if(!on){status=md_seq_stop(s);if(status)return status;status=md_transport_stop(&b->clock);if(status)return status;status=drain(b);}
    else{status=md_transport_start(&b->clock,(md_position_t){0});if(status)return status;status=md_seq_start(s,(md_position_t){0});}
    if(!status)b->playing=(uint8_t)on;return status;
}
int md_audition_panic(md_audition_t *b)
{
    int status=md_audition_play(b,0);if(status)return status;
    md_event_t e={.kind=MD_EVENT_PANIC,.track=MD_ALL_TRACKS,.source=MD_SOURCE_NONE};return md_router_event(&b->router,&e);
}
int md_audition_mode(md_audition_t *b,uint32_t fx)
{
    if(!b || fx>1)return MD_ERR_ARGUMENT;if(b->mode==fx)return MD_OK;
    uint32_t play=b->playing;int status=md_audition_panic(b);if(status)return status;
    md_instrument_reset(&b->instrument[slot(fx,1)]);b->mode=(uint8_t)fx;
    status=bind(b);if(status)return status;
    status=md_seq_stop(&b->seq[fx]);if(status)return status;status=drain(b);if(status)return status;
    status=md_audition_mute(b,b->muted);if(status)return status;return md_audition_play(b,play);
}
int md_audition_mute(md_audition_t *b,uint32_t mask)
{
    if(!b || mask>15)return MD_ERR_ARGUMENT;
    for(uint8_t t=0;t<4;t++){int status=md_seq_mute(&b->seq[b->mode],t,(mask>>t)&1u);if(status)return status;}
    b->muted=(uint8_t)mask;return MD_OK;
}
int md_audition_tempo(md_audition_t *b,uint32_t bpm)
{
    if(!b || bpm<40 || bpm>180)return MD_ERR_RANGE;return md_transport_set_tempo(&b->clock,(uint16_t)(bpm*256u));
}
int md_audition_pattern(md_audition_t *b,uint32_t pattern)
{
    if(!b || pattern>3)return MD_ERR_RANGE;
    for(uint8_t t=0;t<4;t++){int status=md_seq_launch(&b->seq[b->mode],&b->clock,t,(uint8_t)pattern,0);if(status)return status;}
    return MD_OK;
}
static void span(md_audition_t *b,uint32_t start,uint32_t count)
{
    if(!count)return;for(uint32_t t=0;t<4;t++)md_instrument_render_add(b->router.track[t],b->part[t]+2u*start,count);
}
int md_audition_render(md_audition_t *b,int32_t *out,uint32_t frames)
{
    static const int32_t gain[]={20000,12000,14500,16000};
    if(!b || !out || !frames || frames>256)return MD_ERR_ARGUMENT;
    memset(b->part,0,sizeof b->part);md_seq_t *s=&b->seq[b->mode];
    int status=md_seq_begin(s,&b->clock,frames);if(status)return status;
    uint32_t cursor=0;md_seq_event_t e;
    while((status=md_seq_next(s,&e))>0){
        if(e.timed.offset<cursor || e.timed.offset>=frames)return MD_ERR_STATE;
        span(b,cursor,e.timed.offset-cursor);cursor=e.timed.offset;
        int applied=md_router_event(&b->router,&e.timed.event);if(applied){b->failures++;return applied;}
        if(e.timed.event.kind==MD_EVENT_NOTE_ON || e.timed.event.kind==MD_EVENT_HIT)b->events[e.timed.event.track]++;
    }
    if(status<0){b->failures++;return status;}span(b,cursor,frames-cursor);
    for(uint32_t i=0;i<frames*2u;i++){
        int32_t sum=0;for(uint32_t t=0;t<4;t++){
            b->part[t][i]=(b->muted&(1u<<t))?0:(int32_t)(((int64_t)b->part[t][i]*gain[t])>>15);sum+=b->part[t][i];
        }
        if(sum>32767){sum=32767;b->clipped++;}else if(sum < -32767){sum=-32767;b->clipped++;}
        out[i]+=sum;
    }
    return md_transport_advance(&b->clock,frames);
}
