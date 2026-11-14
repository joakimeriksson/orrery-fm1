/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MD_AUDITION_BAND_H
#define MD_AUDITION_BAND_H
#include "core/router.h"
#include "module/seq.h"
#include "instruments/arp.h"
#include "instruments/eminent.h"
#include "instruments/minipops.h"
#include "instruments/electronic_fx.h"
/* Isolated audition integration, not the production session/mixer. Every call
 * after init belongs to the audio owner. Frontends publish controls separately. */
typedef struct md_audition md_audition_t;
typedef struct {md_audition_t *band;uint8_t mode;} md_audition_context_t;
struct md_audition {
    md_arp_state_t bass,lead;
    md_eminent_state_t strings;
    md_minipops_state_t drums;
    md_electronic_fx_state_t fx;
    md_instrument_t instrument[5];
    md_router_t router;
    md_transport_t clock;
    md_seq_bank_t bank[2];
    md_seq_t seq[2];
    md_audition_context_t context[2];
    int32_t base[2][4][40];
    int32_t part[4][512]; /* full-block post-gain stems for inspection */
    uint32_t events[4],clipped,failures;
    uint8_t mode,playing,muted;
};
int md_audition_init(md_audition_t *);
int md_audition_play(md_audition_t *,uint32_t on);
int md_audition_mode(md_audition_t *,uint32_t fx);
int md_audition_mute(md_audition_t *,uint32_t mask);
int md_audition_tempo(md_audition_t *,uint32_t bpm);
int md_audition_pattern(md_audition_t *,uint32_t pattern);
int md_audition_render(md_audition_t *,int32_t *out,uint32_t frames);
int md_audition_panic(md_audition_t *);
#endif
