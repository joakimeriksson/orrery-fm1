/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: audition/band.c core/instrument.c core/router.c core/transport.c module/seq.c instruments/arp.c instruments/eminent.c instruments/minipops.c instruments/electronic_fx.c engines/arp_engine.c engines/em_engine.c engines/mp_engine.c
#include "audition/band.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>
static md_audition_t b;
static int combined(void)
{
    for(unsigned mode=0;mode<2;mode++){
        CHECK(md_audition_init(&b)==0);CHECK(md_audition_mode(&b,mode)==0);CHECK(md_audition_play(&b,1)==0);
        FILE *wav[5]={0};const char *dir=getenv("MD_AUDITION_RENDER_DIR");char path[1024];
        if(dir)for(unsigned t=0;t<5;t++){snprintf(path,sizeof path,"%s/%s-%u.s16le",dir,mode?"fx":"lead",t);wav[t]=fopen(path,"wb");CHECK(wav[t]);}
        uint64_t energy[4]={0};
        for(unsigned at=0;at<44100u*18u;){
            if(at==44100u*16u)CHECK(md_audition_play(&b,0)==0);
            unsigned end=at<44100u*16u?44100u*16u:44100u*18u,n=end-at;if(n>256)n=256;
            int32_t out[512]={0};CHECK(md_audition_render(&b,out,n)==0);
            for(unsigned t=0;t<5;t++){
                int16_t pcm[512];for(unsigned i=0;i<n*2;i++){int32_t x=t?b.part[t-1][i]:out[i];CHECK(x>=-32768 && x<=32767);pcm[i]=(int16_t)x;if(t)energy[t-1]+=(uint32_t)(x<0?-x:x);}
                if(wav[t])CHECK(fwrite(pcm,sizeof *pcm,n*2,wav[t])==n*2);
            }at+=n;
        }
        for(unsigned t=0;t<5;t++)if(wav[t])CHECK(!fclose(wav[t]));
        for(unsigned t=0;t<4;t++){CHECK(b.events[t]>0);CHECK(energy[t]>100000u);printf("mode%u track%u triggers=%u energy=%llu\n",mode,t,b.events[t],(unsigned long long)energy[t]);}
        CHECK(!b.failures);CHECK(!b.clipped);CHECK(!b.seq[mode].base_errors);CHECK(!b.seq[mode].overflows);
        CHECK(md_audition_play(&b,1)==0);CHECK(md_audition_pattern(&b,2)==0);CHECK(md_audition_mute(&b,15)==0);
        int32_t out[512]={0};CHECK(md_audition_render(&b,out,256)==0);for(unsigned i=0;i<512;i++)CHECK(!out[i]);
        CHECK(md_audition_mode(&b,!mode)==0);CHECK(md_audition_tempo(&b,137)==0);CHECK(md_audition_mute(&b,0)==0);
        CHECK(md_audition_render(&b,out,17)==0);CHECK(md_audition_panic(&b)==0);CHECK(!b.playing);
    }return 0;
}
int main(void){RUN(combined);printf("band state: %zu bytes\n",sizeof b);return 0;}
