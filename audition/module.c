/* SPDX-License-Identifier: GPL-3.0-only */
#include "hb_module.h"
#include "band.h"
#include <string.h>
static const hb_host_t *H;
static md_audition_t band;
/* AC79/core0 only: main and the audio interrupt share naturally aligned words.
 * Each channel has one writer. A word access cannot be interrupted mid-instruction;
 * compiler barriers prevent accesses moving across publication. No core1 access,
 * read/modify/write sharing, pointers or payload outside the word. This avoids
 * the SDK atomic-load lowering to an unavailable CAS helper. */
typedef struct {volatile uint32_t value;} __attribute__((aligned(4))) control_word_t;
static control_word_t request,status,positions,errors;
static uint32_t load_word(const control_word_t *p)
{uint32_t v=p->value;__asm__ volatile("" ::: "memory");return v;}
static void store_word(control_word_t *p,uint32_t v)
{__asm__ volatile("" ::: "memory");p->value=v;}

static unsigned applied;
static int failed;
static unsigned drawn[4],draw_valid;
/* One UI writer publishes one complete word. Audio owns every engine/seq field.
 * Main reads only word snapshots. Controls are latest-value, never a queue. */
static int init(const hb_host_t *h)
{
    H=h;int r=md_audition_init(&band);if(r)return r;
    store_word(&request,120u<<8);store_word(&status,0);store_word(&positions,0);store_word(&errors,0);
    applied=120u<<8;failed=0;draw_valid=0;
    if(HB_HOST_HAS(H,fx_reverb))H->fx_reverb(0,0,0,0,0);
    H->log("melodock-test: ready");return 0;
}
static void release(void){md_audition_panic(&band);}
static void render(int32_t *out,uint32_t n)
{
    if(failed)return;
    unsigned r=load_word(&request);int e=0;
    if((r^applied)&2u)e=md_audition_mode(&band,(r>>1)&1u);
    if(!e && ((r^applied)&0xff00u))e=md_audition_tempo(&band,(r>>8)&255u);
    if(!e && ((r^applied)&60u))e=md_audition_mute(&band,(r>>2)&15u);
    if(!e && ((r^applied)&(192u|2u)))e=md_audition_pattern(&band,(r>>6)&3u);
    if(!e && ((r^applied)&1u))e=md_audition_play(&band,r&1u);
    if(!e && ((r^applied)&65536u))e=md_audition_panic(&band);
    applied=r;
    if(!e)e=md_audition_render(&band,out,n);
    if(e){md_audition_panic(&band);failed=1;store_word(&errors,(unsigned)-e);return;}
    md_seq_t *s=&band.seq[band.mode];unsigned p=0;
    for(unsigned t=0;t<4;t++)p|=((unsigned)s->track[t].cursor&63u)<<(t*6u);
    store_word(&positions,p);
    store_word(&status,(band.playing?1u:0u)|((unsigned)band.mode<<1)|((unsigned)band.muted<<2)|((unsigned)s->track[0].pattern<<6)|((band.clock.position.quarter/4u+1u)<<16));
}
static void ui(uint32_t ev,int32_t arg)
{
    unsigned r=load_word(&request),id=HB_EV_ID(ev);
    if(HB_EV_KIND(ev)==HB_EV_BTN_DOWN){
        switch(id){
        case HB_BTN_PLAY:r^=1u;break;
        case HB_BTN_SEL:r^=2u;break;
        case HB_BTN_SEQ:r=(r&~192u)|((((r>>6)+1u)&3u)<<6);break;
        case HB_BTN_ARP:r=(r&~1u)^65536u;break;
        case HB_BTN_ENV:r^=4u;break;
        case HB_BTN_GLO:r^=8u;break;
        case HB_BTN_EDIT:r^=16u;break;
        case HB_BTN_LFO:r^=32u;break;
        default:return;
        }
    }else if(HB_EV_KIND(ev)==HB_EV_ENC && id==HB_ENC_ALGORITHM){
        int bpm=(int)((r>>8)&255u)+arg;if(bpm<40)bpm=40;if(bpm>180)bpm=180;r=(r&~0xff00u)|((unsigned)bpm<<8);
    }else return;
    store_word(&request,r);
}
static void number(char *s,unsigned x){char temp[12];unsigned n=0;do{temp[n++]=(char)('0'+x%10u);x/=10u;}while(x);for(unsigned i=0;i<n;i++)s[i]=temp[n-i-1];s[n]=0;}
static void draw(uint32_t full)
{
    unsigned r=load_word(&request),s=load_word(&status),p=load_word(&positions),err=load_word(&errors);char v[12];
    if(!full && draw_valid && drawn[0]==r && drawn[1]==s && drawn[2]==p && drawn[3]==err)return;
    drawn[0]=r;drawn[1]=s;drawn[2]=p;drawn[3]=err;draw_valid=1;
    H->fill(0,HB_TOP,240,240-HB_TOP,0x0841);H->text(8,26,224,0,"MELODOCK TEST",0xffff,0);
    H->text(8,47,225,0,err?"AUDIO ERROR":(s&1u?"PLAYING":"STOPPED"),0x07e0,0);
    number(v,(r>>8)&255u);H->text(130,47,100,0,v,0xffff,0);H->text(180,47,60,0,"BPM",0xffff,0);
    static const char *const names[]={"BASS / ENV","LEAD / GLO","STRINGS / EDIT","DRUMS / LFO"};
    for(unsigned t=0;t<4;t++){
        int y=70+(int)t*27;const char *name=t==1&&(s&2u)?"FX / GLO":names[t];
        H->text(8,y,224,0,name,s&(4u<<t)?0x7bef:0xffff,0);
        H->fill(8,y+19,224,3,0x2104);H->fill(8+(int)((p>>(t*6u))&15u)*14,y+19,12,3,0x07ff);
    }
    H->text(8,180,225,0,"PLAY start/stop  ARP panic",0xffff,0);
    H->text(8,197,225,0,"SEL lead/FX (restart)",0xffff,0);
    H->text(8,214,225,0,"SEQ next pattern / next bar",0xffff,0);
    number(v,((s>>6)&3u)+1u);H->text(210,26,30,0,v,0x07ff,0);
    H->led(HB_BTN_PLAY,s&1u);H->led(HB_BTN_SEL,(s>>1)&1u);
}
HB_DESC={HB_ABI,"MELODOCK TEST",init,release,0,0,render,ui,draw,0,0,HB_F_SCREEN|HB_F_PANEL,0};
