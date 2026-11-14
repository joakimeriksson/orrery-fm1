/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c instruments/ensemble.c engines/ens_engine.c instruments/eminent.c engines/em_engine.c
#include "instruments/ensemble.h"
#include "instruments/eminent.h"
#include "test.h"
#include <math.h>
#include <string.h>
#include <time.h>

static md_ensemble_state_t state;
static md_eminent_state_t em_state;
static md_instrument_t inst, em;
static const uint8_t CHORD[4] = {52, 55, 59, 64};   /* E3 G3 B3 E4, Caravel's own test chord */
static md_event_t note(md_event_kind_t kind, uint32_t token, uint8_t key)
{
    md_event_t e = {.kind = kind, .source = MD_SOURCE_PANEL, .token = token};
    if (kind == MD_EVENT_NOTE_ON) { e.data.note.key = key; e.data.note.velocity = 100; }
    return e;
}
/* RMS of both channels, the peak, and the correlation of left and right */
static double rms(md_instrument_t *x, uint32_t frames, int32_t *peak, double *corr)
{
    double sum = 0, ll = 0, rr = 0, lr = 0;
    for (uint32_t done = 0; done < frames; done += 256) {
        int32_t out[512] = {0};
        md_instrument_render_add(x, out, 256);
        for (uint32_t k = 0; k < 512; k++) {
            sum += (double)out[k] * out[k];
            if (out[k] > *peak) *peak = out[k];
            if (-out[k] > *peak) *peak = -out[k];
        }
        for (uint32_t k = 0; k < 256; k++) {
            ll += (double)out[2 * k] * out[2 * k]; rr += (double)out[2 * k + 1] * out[2 * k + 1]; lr += (double)out[2 * k] * out[2 * k + 1];
        }
    }
    if (corr) *corr = ll > 0 && rr > 0 ? lr / sqrt(ll * rr) : 1.0;
    return sqrt(sum / (2.0 * frames));
}
static void chord(md_instrument_t *x, int on)
{
    for (uint32_t i = 0; i < 4; i++) {
        md_event_t e = note(on ? MD_EVENT_NOTE_ON : MD_EVENT_NOTE_OFF, i + 1, CHORD[i]);
        (void)md_instrument_event(x, &e);
    }
}
static int settings(void)
{
    const md_instrument_desc_t *items[] = {&MD_ENSEMBLE};
    md_instrument_registry_t r = {items, 1};
    CHECK(md_instrument_registry_validate(&r) == MD_OK && MD_ENSEMBLE.preset_count == ENS_N_PRESETS);
    CHECK(md_instrument_init(&inst, &MD_ENSEMBLE, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    for (uint32_t p = 0; p < ENS_N_PRESETS; p++) {     /* the bank's records are the presets' own controls */
        const uint8_t *b = MD_ENSEMBLE.presets[p].settings;
        CHECK(b[0] == p && b[1] == ENS_PRESETS[p].env[ENS_ATK] && b[2] == ENS_PRESETS[p].env[ENS_REL] &&
              b[3] == ENS_PRESETS[p].e[ENS_DEPTH] && b[4] == ENS_PRESETS[p].e[ENS_TONE]);
        CHECK(!strcmp(MD_ENSEMBLE.presets[p].name, ENS_PRESETS[p].name));
    }
    uint8_t wire[6] = {5, 40, 90, 120, 100, 80}, back[6];
    CHECK(md_sound_apply(&inst, 1, wire, 6) == MD_OK && md_sound_encode(&inst, back, 6) == 6 && !memcmp(wire, back, 6));
    int32_t v = -1;
    CHECK(md_instrument_param_get(&inst, MD_ENSEMBLE_TONE, &v) == MD_OK && v == 100);
    uint8_t bad[6] = {14, 0, 0, 0, 0, 0};
    CHECK(md_sound_apply(&inst, 1, bad, 6) == MD_ERR_RANGE);
    bad[0] = 0; bad[3] = 128;
    CHECK(md_sound_apply(&inst, 1, bad, 6) == MD_ERR_RANGE && md_sound_apply(&inst, 2, wire, 6) == MD_ERR_VERSION);
    return 0;
}
/* every pad sounds held, is stereo where it has a width, stays under full scale, fades after and stops for a panic */
static int pads_play(void)
{
    CHECK(md_instrument_init(&em, &MD_EMINENT, &em_state, sizeof em_state, MD_SAMPLE_RATE) == MD_OK);
    md_event_t lv = {.kind = MD_EVENT_PARAM, .source = MD_SOURCE_NONE};
    lv.data.param.id = MD_EMINENT_LEVEL; lv.data.param.value = 65;            /* as SEVENTIES plays it */
    CHECK(md_instrument_event(&em, &lv) == MD_OK);
    int32_t peak = 0; double corr;
    chord(&em, 1);
    (void)rms(&em, 44100, &peak, 0);
    double ref = rms(&em, 88200, &peak, &corr);
    printf("  Eminent (level 65) E3 G3 B3 E4: RMS %.0f, peak %d, L/R correlation %.2f\n", ref, peak, corr);
    for (uint32_t p = 0; p < ENS_N_PRESETS; p++) {
        CHECK(md_instrument_init(&inst, &MD_ENSEMBLE, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
        CHECK(md_sound_apply(&inst, 1, MD_ENSEMBLE.presets[p].settings, 6) == MD_OK);
        peak = 0;
        CHECK(rms(&inst, 4410, &peak, 0) == 0.0);
        chord(&inst, 1);
        (void)rms(&inst, 44100 * 2, &peak, 0);                                 /* past SLOW SWELL's attack */
        clock_t t0 = clock();
        double held = rms(&inst, 88200, &peak, &corr);
        double us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / (88200.0 / 32.0);
        chord(&inst, 0);
        (void)rms(&inst, 44100 * 5, &peak, 0);
        double after = rms(&inst, 4410, &peak, 0);
        printf("  %-15s RMS %5.0f (%+.1f dB on the Eminent), peak %5d, L/R %.2f, %.1f us a 32-sample block here; 5 s after: %.1f\n",
               ENS_PRESETS[p].name, held, 20 * log10(held / ref), peak, corr, us, after);
        CHECK(held > 500.0 && peak < 32767);
        CHECK(ENS_PRESETS[p].width ? corr < 0.9 : corr > 0.999);
        CHECK(after < held * 0.01);
    }
    chord(&inst, 1);
    (void)rms(&inst, 4410, &peak, 0);
    md_event_t panic = {.kind = MD_EVENT_PANIC, .source = MD_SOURCE_NONE};
    CHECK(md_instrument_event(&inst, &panic) == MD_OK);
    CHECK(rms(&inst, 4410, &peak, 0) == 0.0 && state.voice_count == 0);
    return 0;
}
/* more notes than voices: the oldest goes, nothing sticks */
static int steals(void)
{
    CHECK(md_instrument_init(&inst, &MD_ENSEMBLE, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    int32_t peak = 0;
    for (uint32_t k = 0; k < 10; k++) { md_event_t e = note(MD_EVENT_NOTE_ON, k + 1, (uint8_t)(48 + k)); CHECK(md_instrument_event(&inst, &e) == MD_OK); }
    (void)rms(&inst, 22050, &peak, 0);
    CHECK(peak < 32767);
    for (uint32_t k = 0; k < 10; k++) { md_event_t e = note(MD_EVENT_NOTE_OFF, k + 1, 0); CHECK(md_instrument_event(&inst, &e) == MD_OK); }
    (void)rms(&inst, 44100 * 5, &peak, 0);
    CHECK(rms(&inst, 4410, &peak, 0) < 5.0);
    for (uint32_t i = 0; i < ENS_VOICES; i++) CHECK(state.engine.v[i].stage == 0);
    return 0;
}
int main(void)
{
    RUN(settings);
    RUN(pads_play);
    RUN(steals);
    return 0;
}
