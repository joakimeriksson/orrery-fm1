/* SPDX-License-Identifier: GPL-3.0-only */
/* M3-05 stress builds of SEVENTIES: the real module (module/seventies.c, included below) with a stress case set up
 * through its MD_STRESS_HOOK. Each tests/stress/<variant>/stress.c defines STRESS_VARIANT and includes this file.
 *
 *   1  the dense band: C MIN PULSE at 138 BPM (sixteenths) with 48 cutoff locks on the bass, four Mini Pops rhythms
 *      combined (Samba, Mambo, Rumba, Rock 1) with all three extra rhythms, the strings' chord plus twelve held
 *      notes (sixteen Eminent voices, its maximum), a held lead note, echo and reverb on
 *   2  1 plus a resonant low-pass insert on the lead, the phaser insert on the strings and the master filter active
 *   3  1 with the electronic FX instrument replacing the lead (the FX-replacement case)
 *   4  6 played for ten minutes, then stopped with every live note released (underruns, stuck notes, wrap)
 *   5  1 with each instrument's render and each bus's processing timed by host ticks (a measurement build: it
 *      calls ticks() inside render, which production code must not), logged every ten seconds
 *   6  the supported stress case: as 1 but four string voices (the chord) and one rhythm (Rock 1) without the
 *      extra rhythms: the band as shipped, at its fastest sequence, with 48 locks, the held lead, echo and reverb
 *   7  6 with the timing proxies of 5
 *   8  6 with the electronic FX replacing the lead (FX replacement on the supported case)
 *   9  6 with the lead filter and strings phaser inserts and the master filter (the full graph on it)
 *   10 6 with only the master filter active (the performance sweep)
 *   11 6 with every rhythm key the DRUMS page allows (all ten, lever A) and all three extra rhythms
 *   12 6 with two rhythm keys (Samba and Rock 1) and all three extra rhythms
 *   13 6 with one rhythm key (Beguine, lever B: the densest single rhythm, 47 hits) and all three extra rhythms
 *   14 the worst the shipped panel can reach (no locks: the panel has none; one rhythm key, its cap): C MIN PULSE
 *      at 138, the held lead, the four-voice chords, Beguine on lever B (the densest rhythm) with all three extras,
 *      echo and reverb (two keys, Beguine and Cha-cha, measured 71.4 %: hence the cap)
 *   15 14 for ten minutes, then stopped
 *   16 14 without the extra rhythms (Beguine alone)
 *   17 14 with Rock 1 instead of Beguine (the shipped default rhythm, with all three extras) */
#if STRESS_VARIANT == 11
#define STRESS_EXTRA_STRINGS 0u
#define STRESS_RHYTHMS 0x3ffu             /* all ten keys */
#define STRESS_EXTRAS 7u
#elif STRESS_VARIANT == 12
#define STRESS_EXTRA_STRINGS 0u
#define STRESS_RHYTHMS ((1u << 1) | (1u << 6))
#define STRESS_EXTRAS 7u
#elif STRESS_VARIANT == 13
#define STRESS_EXTRA_STRINGS 0u
#define STRESS_RHYTHMS (1u << 3)          /* Beguine on lever B */
#define STRESS_SECTION 1u
#define STRESS_EXTRAS 7u
#elif STRESS_VARIANT == 14 || STRESS_VARIANT == 15
#define STRESS_EXTRA_STRINGS 0u
#define STRESS_RHYTHMS (1u << 3)          /* Beguine on lever B */
#define STRESS_SECTION 1u
#define STRESS_EXTRAS 7u
#define STRESS_LOCKS 0
#elif STRESS_VARIANT == 16 || STRESS_VARIANT == 17
#define STRESS_EXTRA_STRINGS 0u
#define STRESS_RHYTHMS (STRESS_VARIANT == 16 ? (1u << 3) : (1u << 6))
#define STRESS_SECTION (STRESS_VARIANT == 16 ? 1u : 0u)
#define STRESS_EXTRAS (STRESS_VARIANT == 16 ? 0u : 7u)
#define STRESS_LOCKS 0
#elif STRESS_VARIANT == 4 || STRESS_VARIANT >= 6
#define STRESS_EXTRA_STRINGS 0u   /* the chord alone: four string voices */
#define STRESS_RHYTHMS (1u << 6)          /* Rock 1 */
#define STRESS_EXTRAS 0u
#else
#define STRESS_EXTRA_STRINGS 12u
#define STRESS_RHYTHMS ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 6))
#define STRESS_EXTRAS 7u
#endif
#ifndef STRESS_LOCKS
#define STRESS_LOCKS 1
#endif
#ifndef STRESS_SECTION
#define STRESS_SECTION 0u
#endif
#define STRESS_PROFILE (STRESS_VARIANT == 5 || STRESS_VARIANT == 7)
#include "core/session.h"
#include "instruments/electronic_fx.h"
#include "effects/effects.h"

static void stress_hook(int phase, md_session_config_t *c);
#define MD_STRESS_HOOK stress_hook
#if STRESS_VARIANT == 5 || STRESS_VARIANT == 7
static int stress_session_render(md_session_t *, int32_t *, uint32_t);
#define md_session_render stress_session_render   /* the module's render() calls the timed one */
#endif
#include "../../module/seventies.c"
#if STRESS_VARIANT == 5 || STRESS_VARIANT == 7
#undef md_session_render
static uint32_t prof_total_sum, prof_total_max, prof_total_n;
static int stress_session_render(md_session_t *s, int32_t *out, uint32_t frames)
{
    uint32_t t0 = H->ticks();
    int status = md_session_render(s, out, frames);
    uint32_t t = H->ticks() - t0;
    prof_total_sum += t; prof_total_n++;
    if (t > prof_total_max) prof_total_max = t;
    return status;
}
#endif

static md_fx_filter_t lead_filter_state;
static md_fx_phaser_t strings_phaser_state;
static md_effect_t lead_filter, strings_phaser;
static md_electronic_fx_state_t efx_state;
static md_instrument_t efx;
static uint32_t stress_t0, stress_stopped;

#if STRESS_PROFILE
/* timing proxies: the same descriptors with render/process wrapped by the 24 MHz counter */
enum { P_BASS, P_LEAD, P_STRINGS, P_DRUMS, P_ECHO, P_REVERB, P_PARTS };
static md_instrument_desc_t prof_inst[MD_TRACKS];
static md_effect_desc_t prof_fx[2];
static const md_instrument_desc_t *real_inst[MD_TRACKS];
static const md_effect_desc_t *real_fx[2];
static uint32_t prof_sum[P_PARTS], prof_max[P_PARTS], prof_block[P_PARTS], prof_last_frame;
static uint32_t prof_blocks, prof_frames;
static void prof_add(uint32_t part, uint32_t t, uint32_t frames)
{
    if (part == P_BASS) { prof_frames += frames; }
    prof_block[part] += t;
    if (part == P_BASS && prof_frames >= 256u) {   /* a block's worth on the bass: close the block */
        uint32_t k;
        for (k = 0; k < P_PARTS; k++) { prof_sum[k] += prof_block[k]; if (prof_block[k] > prof_max[k]) prof_max[k] = prof_block[k]; prof_block[k] = 0; }
        prof_blocks++;
        prof_frames -= 256u;
    }
}
#define PROF_INST(T) static void prof_render##T(void *s, int32_t *o, uint32_t n) \
    { uint32_t t0 = H->ticks(); real_inst[T]->render_add(s, o, n); prof_add(T, H->ticks() - t0, n); }
PROF_INST(0) PROF_INST(1) PROF_INST(2) PROF_INST(3)
static void prof_echo(void *s, const int32_t *i, int32_t *w, uint32_t n)
{ uint32_t t0 = H->ticks(); real_fx[0]->process_wet(s, i, w, n); prof_add(P_ECHO, H->ticks() - t0, n); }
static void prof_reverb(void *s, const int32_t *i, int32_t *w, uint32_t n)
{ uint32_t t0 = H->ticks(); real_fx[1]->process_wet(s, i, w, n); prof_add(P_REVERB, H->ticks() - t0, n); }
static void prof_wrap(void)
{
    static void (*const R[MD_TRACKS])(void *, int32_t *, uint32_t) = {prof_render0, prof_render1, prof_render2, prof_render3};
    uint32_t t;
    for (t = 0; t < MD_TRACKS; t++) {
        real_inst[t] = inst[t].desc; prof_inst[t] = *inst[t].desc; prof_inst[t].render_add = R[t]; inst[t].desc = &prof_inst[t];
    }
    real_fx[0] = echo_fx.desc; prof_fx[0] = *echo_fx.desc; prof_fx[0].process_wet = prof_echo; echo_fx.desc = &prof_fx[0];
    real_fx[1] = reverb_fx.desc; prof_fx[1] = *reverb_fx.desc; prof_fx[1].process_wet = prof_reverb; reverb_fx.desc = &prof_fx[1];
}
static void prof_report(void)
{
    static const char *const NAME[P_PARTS] = {"bass", "lead", "strings", "drums", "echo", "reverb"};
    char line[96], *p;
    uint32_t k;
    if (!prof_blocks || !prof_total_n) return;
    {
        static const char *const STAGE[MD_PROF_STAGES] = {"drain", "clear", "events+render", "mixer+buses", "publish"};
        for (k = 0; k < MD_PROF_STAGES; k++) {
            p = put_s(line, "prof stage "); p = put_s(p, STAGE[k]); p = put_s(p, " avg ");
            p = put_u(p, (uint32_t)((uint64_t)md_session_prof[k][0] * 1000u / prof_total_n / 139160u)); p = put_s(p, " max ");
            p = put_u(p, (uint32_t)((uint64_t)md_session_prof[k][1] * 1000u / 139160u)); put_s(p, " (permille)");
            H->log(line);
        }
    }
    p = put_s(line, "prof session total avg ");
    p = put_u(p, (uint32_t)((uint64_t)prof_total_sum * 1000u / prof_total_n / 139160u)); p = put_s(p, " max ");
    p = put_u(p, (uint32_t)((uint64_t)prof_total_max * 1000u / 139160u)); put_s(p, " (permille)");
    H->log(line);
    for (k = 0; k < P_PARTS; k++) {
        /* average and worst ticks per block in tenths of a percent of 139160 ticks (256 frames at 24 MHz) */
        p = put_s(line, "prof "); p = put_s(p, NAME[k]); p = put_s(p, " avg ");
        p = put_u(p, (uint32_t)((uint64_t)prof_sum[k] * 1000u / prof_blocks / 139160u)); p = put_s(p, " max ");
        p = put_u(p, (uint32_t)((uint64_t)prof_max[k] * 1000u / 139160u)); put_s(p, " (permille)");
        H->log(line);
    }
}
#endif
static void stress_bank(void)
{
    md_band_settings_t b;
    md_seq_bank_t *staging = md_session_edit_bank(&S);
    uint32_t i;
    if (!staging) return;
    band_settings(&b);
    if (md_band_build(&b, staging, &chain, &report)) return;
    for (i = 0; i < (STRESS_LOCKS ? MD_MAX_LOCKS : 0u); i++)   /* every pattern, steps 0..11: a lock each */
        staging->locks[i] = (md_lock_t){.step = (uint16_t)((T_BASS * 4u + i / 12u) * 64u + i % 12u),
                                        .target = MD_ARP_CUTOFF, .value = (int16_t)(20 + (i * 7u) % 60u)};
    staging->lock_count = STRESS_LOCKS ? MD_MAX_LOCKS : 0u;
    (void)md_session_publish_bank(&S);
}
static void stress_hook(int phase, md_session_config_t *c)
{
    uint32_t i;
    md_note_token_t token;
    if (phase == 3) {                            /* the cases measure the BAND scene, whatever the default */
        set.scene = MD_BAND_SCENE_BAND;
        scene_defaults();
        return;
    }
    if (phase == 0) {
#if STRESS_VARIANT == 2 || STRESS_VARIANT == 9
        if (md_effect_init(&lead_filter, &MD_FX_FILTER, &lead_filter_state, sizeof lead_filter_state, MD_SAMPLE_RATE) ||
            md_effect_init(&strings_phaser, &MD_FX_PHASER, &strings_phaser_state, sizeof strings_phaser_state, MD_SAMPLE_RATE)) return;
        (void)md_effect_param(&lead_filter, MD_FILTER_CUTOFF, 600);
        (void)md_effect_param(&lead_filter, MD_FILTER_RESONANCE, 60);
        c->insert[T_LEAD][0] = &lead_filter;
        c->insert[T_STRINGS][0] = &strings_phaser;
#elif STRESS_VARIANT == 3 || STRESS_VARIANT == 8
        if (md_instrument_init(&efx, &MD_ELECTRONIC_FX, &efx_state, sizeof efx_state, MD_SAMPLE_RATE)) return;
        c->instrument[T_LEAD] = &efx;
#else
        (void)c; (void)lead_filter; (void)strings_phaser; (void)efx;
#endif
#if STRESS_PROFILE
        prof_wrap();                             /* before the session: it keeps these handles */
        md_session_ticks = H->ticks;
#endif
        return;
    }
    if (phase == 1) {
        set.bass_seq = 3; set.tempo = ARP_SEQS[3].tempo;   /* C MIN PULSE, 138 BPM */
        set.rh_keys = STRESS_RHYTHMS; set.rh_section = STRESS_SECTION;
        set.rh_xmask = STRESS_EXTRAS; set.rh_extra = 70;
        set.ec_div = 3; set.ec_str = 40; set.ec_lead = 50; set.reverb = 40;
        todo |= G_TEMPO | G_DRUMS | G_MIX | G_ECHO;
        pump();
        stress_bank();
#if STRESS_VARIANT == 2 || STRESS_VARIANT == 9 || STRESS_VARIANT == 10
        (void)command(MD_CMD_BYPASS, MD_BYPASS_MASTER, 0, 0);
        (void)param(T_STRINGS, MD_TARGET_MASTER_FILTER | MD_FILTER_CUTOFF, 700);
        (void)param(T_STRINGS, MD_TARGET_MASTER_FILTER | MD_FILTER_RESONANCE, 30);
#endif
        for (i = 0; i < STRESS_EXTRA_STRINGS; i++)   /* the chord's 4 plus these: up to sixteen string voices */
            (void)md_session_note_on(&S, T_STRINGS, MD_SOURCE_MIDI_BASE + 1u, (uint8_t)(72u + i), 90, 0, &token);
        (void)md_session_note_on(&S, T_LEAD, MD_SOURCE_PANEL, 65, 110, 0, &key_token[12]);
        band(1);
        stress_t0 = H->ms();
        H->log("stress: case running");
        return;
    }
#if STRESS_PROFILE
    {
        static uint32_t next_report = 10000u;
        if (H->ms() - stress_t0 > next_report) { prof_report(); next_report += 10000u; }
    }
#endif
#if STRESS_VARIANT == 4 || STRESS_VARIANT == 15
    if (!stress_stopped && H->ms() - stress_t0 > 600000u) {   /* ten minutes: stop and release everything */
        band(0);
        notes_off();
        stress_stopped = 1;
        H->log("stress: stopped after ten minutes");
    }
#endif
}
