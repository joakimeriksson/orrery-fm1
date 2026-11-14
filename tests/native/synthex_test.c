/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/instrument.c instruments/synthex.c instruments/synthex_synth.c instruments/synthex_voice.c
#include "instruments/synthex.h"
#include "test.h"
#include <math.h>
#include <string.h>

static md_synthex_state_t state;
static md_instrument_t inst;
static md_event_t note(md_event_kind_t kind, uint32_t token, uint8_t key)
{
    md_event_t e = {.kind = kind, .source = MD_SOURCE_PANEL, .token = token};
    if (kind == MD_EVENT_NOTE_ON) { e.data.note.key = key; e.data.note.velocity = 100; }
    return e;
}
static double rms(uint32_t frames, int32_t *peak)
{
    double sum = 0;
    for (uint32_t done = 0; done < frames; done += 256) {
        int32_t out[512] = {0};
        md_instrument_render_add(&inst, out, 256);
        for (uint32_t k = 0; k < 512; k++) {
            sum += (double)out[k] * out[k];
            if (out[k] > *peak) *peak = out[k];
            if (-out[k] > *peak) *peak = -out[k];
        }
    }
    return sqrt(sum / (2.0 * frames));
}
static int bank_and_settings(void)
{
    const md_instrument_desc_t *items[] = {&MD_SYNTHEX};
    md_instrument_registry_t r = {items, 1};
    CHECK(md_instrument_registry_validate(&r) == MD_OK && MD_SYNTHEX.preset_count == 80);
    int ring = md_synthex_find("Ring mod.");
    CHECK(ring >= 0 && !strcmp(md_synthex_name((uint32_t)ring), "Ring mod.") && md_synthex_find("no such") < 0);
    CHECK(md_instrument_init(&inst, &MD_SYNTHEX, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    uint8_t wire[2] = {(uint8_t)ring, 70}, back[2];
    CHECK(md_sound_apply(&inst, 1, wire, 2) == MD_OK && md_sound_encode(&inst, back, 2) == 2 && !memcmp(wire, back, 2));
    int32_t v = -1;
    CHECK(md_instrument_param_get(&inst, MD_SYNTHEX_PATCH, &v) == MD_OK && v == ring);
    uint8_t bad[2] = {80, 0};
    CHECK(md_sound_apply(&inst, 1, bad, 2) == MD_ERR_RANGE);
    bad[0] = 0; bad[1] = 101;
    CHECK(md_sound_apply(&inst, 1, bad, 2) == MD_ERR_RANGE && md_sound_apply(&inst, 2, wire, 2) == MD_ERR_VERSION);
    return 0;
}
/* the laser harp sounds while held, fades after, and a panic leaves it silent */
static int ring_mod_plays(void)
{
    int ring = md_synthex_find("Ring mod.");
    CHECK(md_instrument_init(&inst, &MD_SYNTHEX, &state, sizeof state, MD_SAMPLE_RATE) == MD_OK);
    md_event_t p = {.kind = MD_EVENT_PARAM, .source = MD_SOURCE_NONE};
    p.data.param.id = MD_SYNTHEX_PATCH; p.data.param.value = ring;
    CHECK(md_instrument_event(&inst, &p) == MD_OK);
    int32_t peak = 0;
    CHECK(rms(4410, &peak) == 0.0);
    md_event_t a = note(MD_EVENT_NOTE_ON, 1, 69), b = note(MD_EVENT_NOTE_ON, 2, 76);
    CHECK(md_instrument_event(&inst, &a) == MD_OK && md_instrument_event(&inst, &b) == MD_OK);
    CHECK(md_instrument_event(&inst, &a) == MD_ERR_STATE);                 /* the same token again */
    md_event_t dup = note(MD_EVENT_NOTE_ON, 3, 69);
    CHECK(md_instrument_event(&inst, &dup) == MD_ERR_STATE);               /* the same key, another owner */
    double held = rms(44100, &peak);
    printf("  Ring mod. A4+E5: RMS %.0f, peak %d\n", held, peak);
    {   /* AC-coupled: the ring modulator's offset does not reach the mix */
        double sum = 0;
        for (uint32_t done = 0; done < 44100u; done += 256u) {
            int32_t out[512] = {0};
            md_instrument_render_add(&inst, out, 256);
            for (uint32_t k = 0; k < 512u; k += 2u) sum += out[k];
        }
        printf("  held a second more: DC %.1f (%.2f %% of the RMS)\n", sum / 44100.0, 100.0 * fabs(sum / 44100.0) / held);
        CHECK(fabs(sum / 44100.0) < held * 0.02);
    }
    CHECK(held > 1000.0 && peak < 32767);
    md_event_t off = note(MD_EVENT_NOTE_OFF, 1, 0), off2 = note(MD_EVENT_NOTE_OFF, 2, 0);
    CHECK(md_instrument_event(&inst, &off) == MD_OK && md_instrument_event(&inst, &off2) == MD_OK);
    (void)rms(88200, &peak);
    double after = rms(4410, &peak);
    printf("  two seconds after release: RMS %.1f\n", after);
    CHECK(after < held * 0.01);
    CHECK(md_instrument_event(&inst, &a) == MD_OK);
    (void)rms(4410, &peak);
    md_event_t panic = {.kind = MD_EVENT_PANIC, .source = MD_SOURCE_NONE};
    CHECK(md_instrument_event(&inst, &panic) == MD_OK);
    CHECK(rms(4410, &peak) == 0.0 && state.voice_count == 0 && state.patch == ring);
    return 0;
}
int main(void)
{
    RUN(bank_and_settings);
    RUN(ring_mod_plays);
    return 0;
}
