/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c instruments/harmonic.c
#include "instruments/harmonic.h"
#include "instruments/harmonic_tables.h"
#include "test.h"
#include <math.h>
#include <string.h>

static md_harmonic_state_t state, other;
static md_instrument_t inst, inst2;
static md_event_t note(md_event_kind_t kind, uint32_t token, uint8_t key)
{
    md_event_t e = {.kind = kind, .source = MD_SOURCE_PANEL, .token = token};
    if (kind == MD_EVENT_NOTE_ON) { e.data.note.key = key; e.data.note.velocity = 100; }
    return e;
}
static md_event_t value(uint16_t id, int32_t v)
{
    md_event_t e = {.kind = MD_EVENT_PARAM, .source = MD_SOURCE_PANEL};
    e.data.param.id = id; e.data.param.value = v;
    return e;
}
/* frames of the left channel into mono (NULL: not kept), in spans of the given length; the RMS of both channels */
static double play(md_instrument_t *x, uint32_t frames, uint32_t span, int32_t *mono, int32_t *peak)
{
    double sum = 0;
    for (uint32_t done = 0; done < frames;) {
        int32_t out[512] = {0};
        uint32_t n = frames - done < span ? frames - done : span;
        md_instrument_render_add(x, out, n);
        for (uint32_t k = 0; k < 2u * n; k++) {
            sum += (double)out[k] * out[k];
            if (peak && out[k] > *peak) *peak = out[k];
            if (peak && -out[k] > *peak) *peak = -out[k];
        }
        for (uint32_t k = 0; mono && k < n; k++) mono[done + k] = out[2u * k];
        done += n;
    }
    return sqrt(sum / (2.0 * frames));
}
/* the level of one frequency in n samples */
static double at(const int32_t *x, uint32_t n, double hz)
{
    double re = 0, im = 0;
    for (uint32_t i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / n);
        re += w * x[i] * cos(2.0 * M_PI * hz * i / 44100.0); im += w * x[i] * sin(2.0 * M_PI * hz * i / 44100.0);
    }
    return 4.0 * sqrt(re * re + im * im) / n;
}
static int settings(void)
{
    const md_instrument_desc_t *items[] = {&MD_HARMONIC};
    md_instrument_registry_t r = {items, 1};
    CHECK(md_instrument_registry_validate(&r) == MD_OK && MD_HARMONIC.preset_count == MD_HARMONIC_SOUNDS);
    CHECK(md_instrument_init(&inst, &MD_HARMONIC, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    for (uint32_t p = 0; p < MD_HARMONIC_SOUNDS; p++) {
        CHECK(MD_HARMONIC.presets[p].settings[0] == p && !strcmp(MD_HARMONIC.presets[p].name, HM_SOUND[p].name));
        CHECK(!strcmp(md_harmonic_name(p), HM_SOUND[p].name) && strlen(HM_SOUND[p].name) <= 15u);
    }
    uint8_t wire[2] = {5, 140}, back[2], bad[2] = {MD_HARMONIC_SOUNDS, 100}, loud[2] = {0, 201};
    CHECK(md_sound_apply(&inst, 1, wire, 2) == MD_OK && md_sound_encode(&inst, back, 2) == 2 && !memcmp(wire, back, 2));
    CHECK(md_sound_validate(&MD_HARMONIC, 1, bad, 2) == MD_ERR_RANGE && md_sound_validate(&MD_HARMONIC, 1, loud, 2) == MD_ERR_RANGE);
    CHECK(md_sound_validate(&MD_HARMONIC, 1, wire, 1) == MD_ERR_FORMAT);
    int32_t v = -1;
    CHECK(md_instrument_param_get(&inst, MD_HARMONIC_SOUND, &v) == MD_OK && v == 5);
    CHECK(md_instrument_param_get(&inst, MD_HARMONIC_LEVEL, &v) == MD_OK && v == 140);
    md_event_t e = value(MD_HARMONIC_SOUND, MD_HARMONIC_SOUNDS);
    CHECK(md_instrument_event(&inst, &e) == MD_ERR_RANGE && state.sound == 5);
    return 0;
}
/* every sound: in tune (the first generator's fundamental where the key is), within full scale with six keys down,
 * silent again after the keys are let go, and nothing left over */
static int sounds_play(void)
{
    static int32_t mono[32768];
    for (uint32_t p = 0; p < MD_HARMONIC_SOUNDS; p++) {
        CHECK(md_instrument_init(&inst, &MD_HARMONIC, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
        md_event_t e = value(MD_HARMONIC_SOUND, (int32_t)p), on = note(MD_EVENT_NOTE_ON, 1, 69), off = note(MD_EVENT_NOTE_OFF, 1, 0);
        CHECK(md_instrument_event(&inst, &e) == MD_OK && md_instrument_event(&inst, &on) == MD_OK);
        (void)play(&inst, 2048, 64, NULL, NULL);          /* past the attack's start */
        double held = play(&inst, 8192, 64, mono, NULL);
        double a = at(mono, 8192, 440.0), low = at(mono, 8192, 415.3), high = at(mono, 8192, 466.2);
        printf("  %-15s A4: RMS %7.1f, 440 Hz %7.1f (a semitone under %5.1f, over %5.1f)\n", HM_SOUND[p].name, held, a, low, high);
        CHECK(held > 300.0 && a > 8.0 * low && a > 8.0 * high);
        CHECK(md_instrument_event(&inst, &off) == MD_OK);
        (void)play(&inst, 6u * 44100u, 256, NULL, NULL);
        CHECK(play(&inst, 4410, 256, NULL, NULL) == 0.0);  /* six seconds on: every sound has ended, exactly */
        int32_t peak = 0;
        for (uint32_t k = 0; k < 6u; k++) {
            md_event_t c = note(MD_EVENT_NOTE_ON, 10u + k, (uint8_t)(48u + 5u * k));
            c.data.note.velocity = 127;
            CHECK(md_instrument_event(&inst, &c) == MD_OK);
        }
        (void)play(&inst, 22050, 256, NULL, &peak);
        CHECK(peak < 32767 * 3);                          /* six at once, the level at 100: a track's headroom */
        md_event_t panic = {.kind = MD_EVENT_PANIC, .source = MD_SOURCE_NONE};
        CHECK(md_instrument_event(&inst, &panic) == MD_OK && play(&inst, 4410, 256, NULL, NULL) == 0.0);
    }
    return 0;
}
/* a high key plays the wave with fewer harmonics: nothing of weight lands under the key (an alias would) */
static int high_keys_are_clean(void)
{
    static int32_t mono[16384];
    CHECK(md_instrument_init(&inst, &MD_HARMONIC, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    md_event_t e = value(MD_HARMONIC_SOUND, 4), on = note(MD_EVENT_NOTE_ON, 1, 100);   /* BRIGHT REED, E7: 2637 Hz */
    CHECK(md_instrument_event(&inst, &e) == MD_OK && md_instrument_event(&inst, &on) == MD_OK);
    (void)play(&inst, 4096, 64, NULL, NULL);
    (void)play(&inst, 16384, 64, mono, NULL);
    double f = 440.0 * pow(2.0, (100 - 69) / 12.0), tone = at(mono, 16384, f), worst = 0;
    for (double hz = 200.0; hz < f * 0.9; hz += 37.0) { double x = at(mono, 16384, hz); if (x > worst) worst = x; }
    printf("  E7: its fundamental %.1f, the most under it %.2f\n", tone, worst);
    CHECK(tone > 500.0 && worst < tone / 300.0);
    return 0;
}
/* what is heard does not depend on the spans a block is cut into; a seventh key takes a voice; an off for a taken
 * voice is nothing; a second on for a held note is refused */
static int spans_and_voices(void)
{
    static int32_t one[8192], cut[8192];
    static const uint32_t SPANS[] = {1, 7, 16, 33, 64, 256};
    for (uint32_t k = 0; k < 2u * sizeof SPANS / sizeof SPANS[0]; k++) {   /* the pluck, then the organ and its vibrato */
        md_event_t sound = value(MD_HARMONIC_SOUND, k < sizeof SPANS / sizeof SPANS[0] ? 0 : 8);
        CHECK(md_instrument_init(&inst, &MD_HARMONIC, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
        CHECK(md_instrument_init(&inst2, &MD_HARMONIC, &other, sizeof other, MD_SAMPLE_RATE) == MD_OK);
        CHECK(md_instrument_event(&inst, &sound) == MD_OK && md_instrument_event(&inst2, &sound) == MD_OK);
        md_event_t a = note(MD_EVENT_NOTE_ON, 1, 60), b = note(MD_EVENT_NOTE_ON, 2, 67), off = note(MD_EVENT_NOTE_OFF, 1, 0);
        CHECK(md_instrument_event(&inst, &a) == MD_OK && md_instrument_event(&inst2, &a) == MD_OK);
        (void)play(&inst, 3000, 256, one, NULL); (void)play(&inst2, 3000, SPANS[k % (sizeof SPANS / sizeof SPANS[0])], cut, NULL);
        CHECK(md_instrument_event(&inst, &b) == MD_OK && md_instrument_event(&inst2, &b) == MD_OK);
        CHECK(md_instrument_event(&inst, &off) == MD_OK && md_instrument_event(&inst2, &off) == MD_OK);
        (void)play(&inst, 5192, 256, one + 3000, NULL); (void)play(&inst2, 5192, SPANS[k % (sizeof SPANS / sizeof SPANS[0])], cut + 3000, NULL);
        CHECK(!memcmp(one, cut, sizeof one));
    }
    CHECK(md_instrument_init(&inst, &MD_HARMONIC, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    for (uint32_t k = 0; k < 7u; k++) {
        md_event_t c = note(MD_EVENT_NOTE_ON, 1u + k, (uint8_t)(60u + k));
        CHECK(md_instrument_event(&inst, &c) == MD_OK);
        (void)play(&inst, 256, 256, NULL, NULL);
    }
    uint32_t held = 0;
    for (uint32_t k = 0; k < MD_HARMONIC_VOICES; k++) { held += state.voice[k].token != 0; CHECK(state.voice[k].token != 1u); }
    CHECK(held == MD_HARMONIC_VOICES);                    /* the oldest gave its voice to the seventh */
    md_event_t gone = note(MD_EVENT_NOTE_OFF, 1, 0), again = note(MD_EVENT_NOTE_ON, 7, 72);
    CHECK(md_instrument_event(&inst, &gone) == MD_OK && md_instrument_event(&inst, &again) == MD_ERR_STATE);
    return 0;
}
int main(void)
{
    RUN(settings);
    RUN(sounds_play);
    RUN(high_keys_are_clean);
    RUN(spans_and_voices);
    return 0;
}
