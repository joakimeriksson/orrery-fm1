/* SPDX-License-Identifier: GPL-3.0-only */
/* An ambient piece in the manner of Oxygène, rendered offline through the real meloDock session, adapters and
 * effects (the same code the module runs). Original material, not a copy: C minor, slow, no drums.
 *
 *   track 0 WIND     an ARP 2600 playing only noise through its resonant ladder, the cutoff rolled in slow waves
 *   track 1 LEAD     the second ARP 2600, portamento and a little vibrato, a few long notes into the tape echo
 *   track 2 STRINGS  the Eminent: slow swell, long release, full ensemble, a very slow phaser, wide minor chords
 *   track 3 DROPS    the electronic FX instrument: sparse high pings, mostly heard through the echo
 *
 *   cc -std=c11 -O2 -I . tools/render_ambient.c module/band.c module/seq.c core/session.c core/commands.c \
 *      core/mixer.c core/effect.c core/router.c core/instrument.c core/transport.c instruments/arp.c \
 *      instruments/eminent.c instruments/minipops.c instruments/electronic_fx.c effects/filter.c effects/phaser.c \
 *      effects/echo.c effects/reverb.c effects/registry.c engines/arp_engine.c engines/em_engine.c \
 *      engines/mp_engine.c -lm -o build/listen/render_ambient && build/listen/render_ambient build/listen/ambient.wav */
#include "core/session.h"
#include "module/band.h"
#include "instruments/arp.h"
#include "instruments/eminent.h"
#include "instruments/electronic_fx.h"
#include "effects/effects.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR 44100u
#define SECONDS 100u
enum { WIND, LEAD, STRINGS, DROPS };

static md_session_t S;
static md_arp_state_t wind_state, lead_state;
static md_eminent_state_t strings_state;
static md_electronic_fx_state_t drops_state;
static md_instrument_t inst[4];
static md_fx_echo_t echo_state;
static md_fx_reverb_t reverb_state;
static md_fx_filter_t warm_state;
static md_effect_t echo, reverb, warm;
static md_seq_bank_t bank;
static int16_t pcm[2u * SR * SECONDS];

static void param(uint8_t track, uint16_t target, int32_t value)
{
    if (md_session_param(&S, track, target, value)) fprintf(stderr, "param %u/%04x refused\n", track, target);
}
static int32_t q15(uint32_t pct) { return pct >= 100u ? 32767 : (int32_t)(pct * 32767u / 100u); }
static void mix(uint8_t track, uint32_t level, uint32_t echo_send, uint32_t reverb_send)
{
    param(track, MD_TARGET_MIXER | MD_MIX_LEVEL, q15(level));
    param(track, MD_TARGET_MIXER | MD_MIX_ECHO_SEND, q15(echo_send));
    param(track, MD_TARGET_MIXER | MD_MIX_REVERB_SEND, q15(reverb_send));
}

/* ---- the score: times in seconds ---- */
typedef struct { float at, until; uint8_t keys[4]; } chord_t;
static const chord_t CHORDS[] = {               /* wide, open voicings; each overlaps the next by its release */
    {6, 14, {48, 55, 62, 63}},                   /* Cm(add9) */
    {14, 22, {44, 51, 55, 60}},                  /* Abmaj7 */
    {22, 30, {41, 48, 56, 67}},                  /* Fm9 */
    {30, 38, {43, 51, 55, 58}},                  /* Eb/G */
    {38, 46, {48, 55, 62, 63}},                  /* Cm(add9) */
    {46, 54, {44, 51, 55, 60}},                  /* Abmaj7 */
    {54, 62, {46, 53, 58, 60}},                  /* Bbsus2 */
    {62, 70, {43, 50, 55, 60}},                  /* Gsus4 */
    {70, 88, {36, 48, 55, 63}},                  /* Cm, low, held into the fade */
};
typedef struct { float at, until; uint8_t key; } note_t;
static const note_t MELODY[] = {                 /* slow, sparse, gliding between notes */
    {30, 34, 67}, {34.5f, 37, 75}, {37.5f, 41.5f, 74}, {42, 47, 72},
    {50, 52, 70}, {52.5f, 57, 72}, {60, 64, 67}, {64.5f, 66.5f, 68}, {67, 74, 67},
};
static const note_t DROPLETS[] = {               /* high C minor pentatonic pings */
    {19, 19.2f, 87}, {23.5f, 23.7f, 91}, {27, 27.2f, 84}, {33, 33.2f, 94}, {40.5f, 40.7f, 87},
    {47, 47.2f, 89}, {55, 55.2f, 82}, {61.5f, 61.7f, 91}, {69, 69.2f, 84}, {76, 76.2f, 87},
};

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "ambient.wav";
    md_session_config_t c = {0};
    if (md_instrument_init(&inst[WIND], &MD_ARP2600, &wind_state, sizeof wind_state, SR) ||
        md_instrument_init(&inst[LEAD], &MD_ARP2600, &lead_state, sizeof lead_state, SR) ||
        md_instrument_init(&inst[STRINGS], &MD_EMINENT, &strings_state, sizeof strings_state, SR) ||
        md_instrument_init(&inst[DROPS], &MD_ELECTRONIC_FX, &drops_state, sizeof drops_state, SR) ||
        md_effect_init(&echo, &MD_FX_ECHO, &echo_state, sizeof echo_state, SR) ||
        md_effect_init(&reverb, &MD_FX_REVERB, &reverb_state, sizeof reverb_state, SR) ||
        md_effect_init(&warm, &MD_FX_FILTER, &warm_state, sizeof warm_state, SR)) return 1;
    /* the strings warmed: a gentle low-pass near 3.5 kHz takes the saw edge off the ensemble */
    md_effect_param(&warm, MD_FILTER_CUTOFF, 745);
    md_effect_param(&warm, MD_FILTER_RESONANCE, 10);
    md_sound_apply(&inst[LEAD], MD_ARP_SETTINGS_VERSION, MD_ARP2600.presets[3].settings, MD_ARP_SETTINGS_BYTES);
    md_band_settings_t none = {0};
    md_band_chain_t chain;
    md_band_report_t report;
    md_band_build(&none, &bank, &chain, &report);   /* empty patterns: everything below is played live */
    for (int t = 0; t < 4; t++) c.instrument[t] = &inst[t];
    c.insert[STRINGS][0] = &warm;
    c.bus[MD_MIX_BUS_ECHO] = &echo;
    c.bus[MD_MIX_BUS_REVERB] = &reverb;
    c.bank = &bank;
    c.bpm_q8 = 75u * 256u;
    if (md_session_init(&S, &c)) return 1;

    /* WIND: noise only, a resonant ladder, a slow swell; the cutoff is rolled below */
    static const int32_t wind[][2] = {
        {MD_ARP_OSC1_LEVEL, 0}, {MD_ARP_OSC2_LEVEL, 0}, {MD_ARP_SUB_LEVEL, 0}, {MD_ARP_RING_LEVEL, 0},
        {MD_ARP_NOISE_LEVEL, 100}, {MD_ARP_CUTOFF, 50}, {MD_ARP_RESONANCE, 72}, {MD_ARP_FILTER_ENV, 0},
        {MD_ARP_KEYBOARD_TRACK, 0}, {MD_ARP_LFO_FILTER, 0}, {MD_ARP_SAMPLE_HOLD_FILTER, 0}, {MD_ARP_DRIVE, 30},
        {MD_ARP_AMP_ATTACK, 92}, {MD_ARP_AMP_DECAY, 50}, {MD_ARP_AMP_SUSTAIN, 100}, {MD_ARP_AMP_RELEASE, 95},
        {MD_ARP_LEVEL, 100}, {MD_ARP_GLIDE, 0},
    };
    for (unsigned i = 0; i < sizeof wind / sizeof wind[0]; i++) param(WIND, (uint16_t)wind[i][0], wind[i][1]);
    /* LEAD: the ARP LEAD patch, softened: portamento, slow vibrato, a gentle attack and long release */
    static const int32_t lead[][2] = {
        {MD_ARP_GLIDE, 48}, {MD_ARP_LFO_WAVE, 0}, {MD_ARP_LFO_RATE, 32}, {MD_ARP_LFO_PITCH, 2},
        {MD_ARP_AMP_ATTACK, 45}, {MD_ARP_AMP_SUSTAIN, 90}, {MD_ARP_AMP_RELEASE, 72}, {MD_ARP_CUTOFF, 52},
        {MD_ARP_RESONANCE, 22}, {MD_ARP_FILTER_ENV, 18}, {MD_ARP_LEVEL, 100},
    };
    for (unsigned i = 0; i < sizeof lead / sizeof lead[0]; i++) param(LEAD, (uint16_t)lead[i][0], lead[i][1]);
    /* STRINGS: the soft Eminent */
    static const int32_t strings[][2] = {
        {MD_EMINENT_REGISTRATION, EM_STRINGS}, {MD_EMINENT_ATTACK, 85}, {MD_EMINENT_RELEASE, 90},
        {MD_EMINENT_ENSEMBLE, 100}, {MD_EMINENT_PHASER, 8}, {MD_EMINENT_COLOR, 1}, {MD_EMINENT_LEVEL, 60},
    };
    for (unsigned i = 0; i < sizeof strings / sizeof strings[0]; i++) param(STRINGS, (uint16_t)strings[i][0], strings[i][1]);
    /* DROPS: a ringing ping with a short upward sweep */
    static const int32_t drops[][2] = {
        {MD_FX_RECIPE, MD_FX_PING}, {MD_FX_DECAY_MS, 900}, {MD_FX_SWEEP_SEMITONES, 7}, {MD_FX_SWEEP_MS, 60},
        {MD_FX_TONE, 45}, {MD_FX_RESONANCE, 60}, {MD_FX_LEVEL, 70},
    };
    for (unsigned i = 0; i < sizeof drops / sizeof drops[0]; i++) param(DROPS, (uint16_t)drops[i][0], drops[i][1]);
    /* the space: a long tape echo, a large hall */
    param(STRINGS, MD_TARGET_ECHO | MD_ECHO_SYNC, 0);
    param(STRINGS, MD_TARGET_ECHO | MD_ECHO_TIME, 660);
    param(STRINGS, MD_TARGET_ECHO | MD_ECHO_FEEDBACK, 58);
    param(STRINGS, MD_TARGET_ECHO | MD_ECHO_TONE, 32);
    param(STRINGS, MD_TARGET_REVERB | MD_REVERB_ALGORITHM, MD_REVERB_HALL);
    param(STRINGS, MD_TARGET_REVERB | MD_REVERB_SIZE, 90);
    param(STRINGS, MD_TARGET_REVERB | MD_REVERB_DAMPING, 35);
    mix(WIND, 80, 0, 70);
    mix(LEAD, 52, 60, 50);
    mix(STRINGS, 62, 22, 60);
    mix(DROPS, 40, 75, 60);

    md_note_token_t wind_tok = 0, chord_tok[4] = {0}, lead_tok = 0, drop_tok = 0;
    int chord = -1, melody = -1, drop = -1;
    int32_t last_cut = -1;
    for (uint32_t frame = 0; frame < SR * SECONDS; frame += 256u) {
        float t = (float)frame / SR;
        /* the wind: on from the start, off at 88 s; the cutoff in slow waves (9 and 23 s periods) */
        if (!wind_tok && t < 1.0f) md_session_note_on(&S, WIND, MD_SOURCE_PANEL, 60, 100, 0, &wind_tok);
        if (wind_tok && t >= 88.0f) { md_session_note_off(&S, WIND, MD_SOURCE_PANEL, wind_tok); wind_tok = 0; }
        int32_t cut = (int32_t)lrintf(52.0f + 13.0f * sinf(6.2832f * t / 9.3f) + 8.0f * sinf(6.2832f * t / 23.0f)
                                      + 5.0f * sinf(6.2832f * t / 4.1f + 1.0f));   /* three waves: never quite regular */
        if (cut != last_cut) { param(WIND, MD_ARP_CUTOFF, cut); last_cut = cut; }
        /* the strings */
        int want = -1;
        for (unsigned i = 0; i < sizeof CHORDS / sizeof CHORDS[0]; i++) if (t >= CHORDS[i].at && t < CHORDS[i].until) want = (int)i;
        if (want != chord) {
            for (int k = 0; k < 4; k++) if (chord_tok[k]) md_session_note_off(&S, STRINGS, MD_SOURCE_MIDI_BASE, chord_tok[k]);
            memset(chord_tok, 0, sizeof chord_tok);
            if (want >= 0) for (int k = 0; k < 4; k++) md_session_note_on(&S, STRINGS, MD_SOURCE_MIDI_BASE, CHORDS[want].keys[k], 80, 0, &chord_tok[k]);
            chord = want;
        }
        /* the lead */
        int wantm = -1;
        for (unsigned i = 0; i < sizeof MELODY / sizeof MELODY[0]; i++) if (t >= MELODY[i].at && t < MELODY[i].until) wantm = (int)i;
        if (wantm != melody) {
            if (lead_tok) { md_session_note_off(&S, LEAD, MD_SOURCE_MIDI_BASE + 1u, lead_tok); lead_tok = 0; }
            if (wantm >= 0) md_session_note_on(&S, LEAD, MD_SOURCE_MIDI_BASE + 1u, MELODY[wantm].key, 90, 0, &lead_tok);
            melody = wantm;
        }
        /* the droplets */
        int wantd = -1;
        for (unsigned i = 0; i < sizeof DROPLETS / sizeof DROPLETS[0]; i++) if (t >= DROPLETS[i].at && t < DROPLETS[i].until) wantd = (int)i;
        if (wantd != drop) {
            if (drop_tok) { md_session_note_off(&S, DROPS, MD_SOURCE_MIDI_BASE + 2u, drop_tok); drop_tok = 0; }
            if (wantd >= 0) md_session_note_on(&S, DROPS, MD_SOURCE_MIDI_BASE + 2u, DROPLETS[wantd].key, 80, 0, &drop_tok);
            drop = wantd;
        }
        int32_t block[512] = {0};
        md_session_render(&S, block, 256);
        for (unsigned i = 0; i < 512u; i++) pcm[2u * frame + i] = (int16_t)(block[i] > 32767 ? 32767 : block[i] < -32767 ? -32767 : block[i]);
    }
    md_session_snapshot_t snap;
    md_session_snapshot(&S, &snap);
    printf("%u s: master peak %d, clipped %u, track peaks wind %d lead %d strings %d drops %d, commands %u, refused %u, rejected notes %u\n",
           SECONDS, snap.meters.master_peak, snap.meters.clipped, snap.meters.track_peak[WIND], snap.meters.track_peak[LEAD],
           snap.meters.track_peak[STRINGS], snap.meters.track_peak[DROPS], snap.commands, snap.command_errors, snap.rejected_notes);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    uint32_t bytes = SR * SECONDS * 4u, v;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    uint16_t h[] = {1, 2}; fwrite(h, 2, 2, f);
    v = SR; fwrite(&v, 4, 1, f); v = SR * 4u; fwrite(&v, 4, 1, f);
    uint16_t h2[] = {4, 16}; fwrite(h2, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(pcm, 2, 2u * SR * SECONDS, f);
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}
