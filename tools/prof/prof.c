/* SPDX-License-Identifier: GPL-3.0-only */
/* A timing build of SEVENTIES (not shipped): every part's render and both buses timed by the host's 24 MHz ticks,
 * a report to the log every five seconds while the band runs, in permille of a block (256 frames). */
#include "core/session.h"
static void prof_hook(int phase, md_session_config_t *c);
#define MD_STRESS_HOOK prof_hook
static int prof_session_render(md_session_t *, int32_t *, uint32_t);
#define md_session_render prof_session_render
#include "../../module/seventies.c"
#undef md_session_render
#define BLOCK 139320u
enum { P_BASS, P_LEAD, P_STRINGS, P_DRUMS, P_BOX, P_ECHO, P_REVERB, P_PARTS };
static uint32_t tot_sum, tot_max, tot_n, p_sum[P_PARTS], p_max[P_PARTS];
static int prof_session_render(md_session_t *s, int32_t *out, uint32_t frames)
{
    uint32_t t0 = H->ticks();
    int status = md_session_render(s, out, frames);
    uint32_t t = H->ticks() - t0;
    tot_sum += t; tot_n++;
    if (t > tot_max) tot_max = t;
    return status;
}
static md_instrument_desc_t w_desc[7];
static const md_instrument_desc_t *r_desc[7];
static md_effect_desc_t w_fx[2];
static const md_effect_desc_t *r_fx[2];
static const uint8_t PART[7] = {P_BASS, P_LEAD, P_STRINGS, P_DRUMS, P_BOX, P_LEAD, P_STRINGS};
static void add(uint32_t part, uint32_t t) { p_sum[part] += t; if (t > p_max[part]) p_max[part] = t; }
#define W(N) static void w_render##N(void *s, int32_t *o, uint32_t n) \
    { uint32_t t0 = H->ticks(); r_desc[N]->render_add(s, o, n); add(PART[N], H->ticks() - t0); }
W(0) W(1) W(2) W(3) W(4) W(5) W(6)
static void w_echo(void *s, const int32_t *i, int32_t *w, uint32_t n)
{ uint32_t t0 = H->ticks(); r_fx[0]->process_wet(s, i, w, n); add(P_ECHO, H->ticks() - t0); }
static void w_reverb(void *s, const int32_t *i, int32_t *w, uint32_t n)
{ uint32_t t0 = H->ticks(); r_fx[1]->process_wet(s, i, w, n); add(P_REVERB, H->ticks() - t0); }
static char *pair(char *p, const char *name, uint32_t sum, uint32_t max)
{
    p = put_s(p, name);
    p = put_u(p, (uint32_t)((uint64_t)sum * 1000u / (tot_n ? tot_n : 1u) / BLOCK)); p = put_s(p, "/");
    { uint32_t m = (uint32_t)((uint64_t)max * 1000u / BLOCK); if (m >= 1000u) { *p++ = (char)('0' + m / 1000u % 10u); if (m % 1000u < 100u) *p++ = '0'; if (m % 1000u < 10u) *p++ = '0'; } p = put_u(p, m % 1000u); }
    return p;
}
static void prof_hook(int phase, md_session_config_t *c)
{
    static void (*const R[7])(void *, int32_t *, uint32_t) = {w_render0, w_render1, w_render2, w_render3, w_render4, w_render5, w_render6};
    static uint32_t next, pb, pi, pf;
    uint32_t k;
    char line[120], *p;
    if (phase == 0) {
        md_instrument_t *h[7] = {&inst[0], &inst[1], &inst[2], &inst[3], &inst[4], &sx, &ens};
        for (k = 0; k < 7u; k++) { r_desc[k] = h[k]->desc; w_desc[k] = *h[k]->desc; w_desc[k].render_add = R[k]; h[k]->desc = &w_desc[k]; }
        r_fx[0] = echo_fx.desc; w_fx[0] = *echo_fx.desc; w_fx[0].process_wet = w_echo; echo_fx.desc = &w_fx[0];
        r_fx[1] = reverb_fx.desc; w_fx[1] = *reverb_fx.desc; w_fx[1].process_wet = w_reverb; reverb_fx.desc = &w_fx[1];
        md_session_ticks = H->ticks;
        (void)c;
        return;
    }
    if (phase != 2 || !running || H->ms() - next < 5000u) { if (!running) next = H->ms(); return; }
    next = H->ms();
    p = put_s(line, "P "); p = put_s(p, SETUP_NAME); p = pair(p, " tot ", tot_sum, tot_max);
    p = pair(p, " ev ", md_session_prof[MD_PROF_EVENTS][0], md_session_prof[MD_PROF_EVENTS][1]);
    p = pair(p, " mix ", md_session_prof[MD_PROF_MIX][0], md_session_prof[MD_PROF_MIX][1]);
    p = pair(p, " drain ", md_session_prof[MD_PROF_DRAIN][0], md_session_prof[MD_PROF_DRAIN][1]);
    p = put_s(p, " par "); p = put_u(p, S.stats.parallel_blocks - pb); p = put_s(p, " ");
    p = put_u(p, S.stats.parallel_inline - pi); p = put_s(p, " "); put_u(p, S.stats.parallel_fallbacks - pf);
    H->log(line);
    p = pair(line, "P bass ", p_sum[P_BASS], p_max[P_BASS]); p = pair(p, " lead ", p_sum[P_LEAD], p_max[P_LEAD]);
    p = pair(p, " str ", p_sum[P_STRINGS], p_max[P_STRINGS]); p = pair(p, " dr ", p_sum[P_DRUMS], p_max[P_DRUMS]);
    p = pair(p, " box ", p_sum[P_BOX], p_max[P_BOX]); p = pair(p, " ec ", p_sum[P_ECHO], p_max[P_ECHO]);
    pair(p, " rv ", p_sum[P_REVERB], p_max[P_REVERB]);
    H->log(line);
    pb = S.stats.parallel_blocks; pi = S.stats.parallel_inline; pf = S.stats.parallel_fallbacks;
    tot_sum = tot_max = tot_n = 0;
    for (k = 0; k < P_PARTS; k++) p_sum[k] = p_max[k] = 0;
    for (k = 0; k < MD_PROF_STAGES; k++) md_session_prof[k][0] = md_session_prof[k][1] = 0;
}
