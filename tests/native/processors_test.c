/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/effect.c core/mixer.c effects/filter.c effects/phaser.c effects/echo.c effects/reverb.c effects/registry.c
#include "effects/effects.h"
#include "core/mixer.h"
#include "test.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR MD_SAMPLE_RATE
#define LONG (SR * 20u)
#define PI 3.14159265358979323846
static int32_t in[2u * LONG], out[2u * LONG], ref[2u * LONG];
static md_fx_filter_t filter_state;
static md_fx_phaser_t phaser_state;
static md_fx_echo_t echo_state;
static md_fx_reverb_t reverb_state;
static md_effect_t fx;

static const uint32_t P1[] = {1}, P17[] = {17}, P64[] = {64}, P255[] = {255}, P256[] = {256},
    PIRR[] = {3, 250, 1, 77, 256, 9, 128, 31, 200, 5};
static const struct { const uint32_t *p; uint32_t n; } PARTS[] = {
    {P1, 1}, {P17, 1}, {P64, 1}, {P255, 1}, {P256, 1}, {PIRR, 10}};

static void *state_for(const md_effect_desc_t *d)
{
    if (d == &MD_FX_FILTER) return &filter_state;
    if (d == &MD_FX_PHASER) return &phaser_state;
    if (d == &MD_FX_ECHO) return &echo_state;
    return &reverb_state;
}
static int make(const md_effect_desc_t *d)
{
    return md_effect_init(&fx, d, state_for(d), d->processor.state_bytes, SR) != MD_OK;
}
/* Process frames of `in` into `out` with the given partition (insert in place on a copy). */
static void run(uint32_t frames, const uint32_t *parts, uint32_t count)
{
    int bus = (fx.desc->capabilities & MD_EFFECT_BUS) != 0;
    if (!bus) memcpy(out, in, 2u * frames * sizeof *out);
    for (uint32_t at = 0, p = 0; at < frames; p++) {
        uint32_t n = parts[p % count];
        if (n > frames - at) n = frames - at;
        if (bus) md_effect_process_wet(&fx, &in[2u * at], &out[2u * at], n);
        else md_effect_process_replace(&fx, &out[2u * at], n);
        at += n;
    }
}
static void sine(double hz, double amp, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++)
        in[2 * i] = in[2 * i + 1] = (int32_t)lrint(amp * sin(2.0 * PI * hz * i / SR));
}
static int32_t peak(uint32_t from, uint32_t to)
{
    int32_t m = 0;
    for (uint32_t i = 2u * from; i < 2u * to; i++) m = abs(out[i]) > m ? abs(out[i]) : m;
    return m;
}
static double rms(uint32_t from, uint32_t to)
{
    double s = 0;
    for (uint32_t i = 2u * from; i < 2u * to; i++) s += (double)out[i] * out[i];
    return sqrt(s / (2.0 * (to - from)));
}
/* first frame >= from whose left sample is nonzero, or `to` */
static uint32_t first_nonzero(uint32_t from, uint32_t to, uint32_t channel)
{
    for (uint32_t i = from; i < to; i++) if (out[2 * i + channel]) return i;
    return to;
}
static uint32_t last_nonzero(uint32_t frames)
{
    uint32_t last = 0;
    for (uint32_t i = 0; i < 2u * frames; i++) if (out[i]) last = i / 2u + 1u;
    return last;
}

static int registry_and_settings(void)
{
    CHECK(md_effect_registry_validate(&MD_FX_REGISTRY) == MD_OK);
    for (uint32_t i = 0; i < MD_FX_REGISTRY.count; i++) {
        const md_effect_desc_t *d = MD_FX_REGISTRY.items[i];
        /* decision 002 pools: 12 bytes per insert payload, 8 per bus/master; the filter is also the master */
        CHECK(d->processor.max_settings_bytes <= ((d->capabilities & MD_EFFECT_BUS) || d == &MD_FX_FILTER ? 8u : 12u));
        CHECK(!make(d));
        const md_processor_desc_t *p = &d->processor;
        for (uint32_t k = 0; k < p->param_count; k++) CHECK(md_effect_param(&fx, p->params[k].id, p->params[k].max) == MD_OK);
        uint8_t wire[16], again[16];
        int bytes = md_effect_encode(&fx, wire, sizeof wire);
        CHECK(bytes == 2 * (int)p->param_count);
        CHECK(!make(d));
        CHECK(md_effect_apply(&fx, 1, wire, (uint32_t)bytes) == MD_OK);
        CHECK(md_effect_encode(&fx, again, sizeof again) == bytes && !memcmp(wire, again, (size_t)bytes));
        for (uint32_t k = 0; k < p->param_count; k++) {
            int32_t v = 0;
            CHECK(md_effect_param_get(&fx, p->params[k].id, &v) == MD_OK && v == p->params[k].max);
        }
        CHECK(md_effect_validate(d, 1, wire, (uint32_t)bytes - 1u) == MD_ERR_FORMAT);
        CHECK(md_effect_validate(d, 2, wire, (uint32_t)bytes) == MD_ERR_VERSION);
        wire[0] = 0xff; wire[1] = 0x7f;                 /* 32767: above every first parameter */
        CHECK(md_effect_validate(d, 1, wire, (uint32_t)bytes) == MD_ERR_RANGE);
        CHECK(md_effect_encode(&fx, wire, 1) == MD_ERR_CAPACITY);
        CHECK(md_effect_param(&fx, 99, 0) == MD_ERR_NOT_FOUND);
    }
    return 0;
}

static int filter_response(void)
{
    /* open low-pass passes DC exactly */
    CHECK(!make(&MD_FX_FILTER));
    for (uint32_t i = 0; i < 2u * 4096; i++) in[i] = 10000;
    run(4096, P256, 1);
    CHECK(abs(out[2 * 4095] - 10000) <= 1);
    /* the lowest cutoff, 20 Hz, still converges exactly on DC (no rounding dead band) */
    CHECK(md_effect_param(&fx, MD_FILTER_CUTOFF, 0) == MD_OK);
    uint8_t low[6];
    CHECK(md_effect_encode(&fx, low, sizeof low) == 6 && md_effect_apply(&fx, 1, low, 6) == MD_OK);
    md_effect_reset(&fx);
    for (uint32_t i = 0; i < 2u * SR; i++) in[i] = 10000;
    run(SR, P256, 1);
    CHECK(abs(out[2 * (SR - 1)] - 10000) <= 1);
    for (uint32_t i = 0; i < 2u * SR; i++) in[i] = 3;   /* a tiny signal still moves it */
    md_effect_reset(&fx); run(SR, P256, 1); CHECK(out[2 * (SR - 1)] == 3);
    /* 160 Hz low-pass: 10 kHz is gone, 50 Hz passes */
    CHECK(md_effect_param(&fx, MD_FILTER_CUTOFF, 300) == MD_OK);
    md_effect_reset(&fx);
    uint8_t snap[6];
    CHECK(md_effect_encode(&fx, snap, sizeof snap) == 6 && md_effect_apply(&fx, 1, snap, 6) == MD_OK);
    sine(10000, 20000, 8192); run(8192, P256, 1); CHECK(peak(4096, 8192) < 100);
    md_effect_reset(&fx); sine(50, 20000, 8192); run(8192, P256, 1); CHECK(peak(4096, 8192) > 17000);
    /* high-pass at 1280 Hz: DC decays to zero, 10 kHz passes */
    CHECK(md_effect_param(&fx, MD_FILTER_TYPE, MD_FILTER_HIGHPASS) == MD_OK && md_effect_param(&fx, MD_FILTER_CUTOFF, 600) == MD_OK);
    CHECK(md_effect_encode(&fx, snap, sizeof snap) == 6 && md_effect_apply(&fx, 1, snap, 6) == MD_OK);
    md_effect_reset(&fx);
    for (uint32_t i = 0; i < 2u * 8192; i++) in[i] = 10000;
    run(8192, P256, 1); if (abs(out[2 * 8191]) > 2) printf("  hp dc %d\n", out[2 * 8191]);
    CHECK(abs(out[2 * 8191]) <= 2);              /* DC rejected to within rounding */
    md_effect_reset(&fx); sine(10000, 20000, 8192); run(8192, P256, 1); CHECK(peak(4096, 8192) > 18000);
    /* band-pass is unity at its centre and low away from it, at any resonance */
    for (int32_t res = 0; res <= 100; res += 50) {
        CHECK(md_effect_param(&fx, MD_FILTER_TYPE, MD_FILTER_BANDPASS) == MD_OK);
        CHECK(md_effect_param(&fx, MD_FILTER_CUTOFF, 600) == MD_OK && md_effect_param(&fx, MD_FILTER_RESONANCE, res) == MD_OK);
        CHECK(md_effect_encode(&fx, snap, sizeof snap) == 6 && md_effect_apply(&fx, 1, snap, 6) == MD_OK);
        md_effect_reset(&fx); sine(1280, 20000, 16384); run(16384, P256, 1);
        CHECK(peak(12000, 16384) > 18500 && peak(12000, 16384) < 21500);
        md_effect_reset(&fx); sine(80, 20000, 16384); run(16384, P256, 1);
        CHECK(peak(12000, 16384) < 4000);
    }
    return 0;
}

/* worst input at maximum resonance stays bounded, then decays to exactly zero */
static int bounded_and_decays(const md_effect_desc_t *d, const int32_t *settings, uint32_t count, uint32_t decay_frames)
{
    CHECK(!make(d));
    for (uint32_t k = 0; k < count; k++) CHECK(md_effect_param(&fx, d->processor.params[k].id, settings[k]) == MD_OK);
    uint8_t snap[16];
    int bytes = md_effect_encode(&fx, snap, sizeof snap);
    CHECK(bytes > 0 && md_effect_apply(&fx, 1, snap, (uint32_t)bytes) == MD_OK);
    uint32_t loud = SR / 2u;
    for (uint32_t i = 0; i < loud; i++) {
        int32_t v = (i / 37u) & 1u ? (1 << 24) : -(1 << 24);
        in[2 * i] = v; in[2 * i + 1] = i & 64u ? INT32_MAX : INT32_MIN;
    }
    memset(&in[2u * loud], 0, 2u * decay_frames * sizeof *in);
    run(loud + decay_frames, P256, 1);
    int32_t m = 0;
    for (uint32_t i = 0; i < 2u * (loud + decay_frames); i++) m = abs(out[i]) > m ? abs(out[i]) : m;
    CHECK(m <= (1 << 28));
    uint32_t last = last_nonzero(loud + decay_frames);
    printf("  %s: peak %d, silent %.2f s after the input stopped\n", d->processor.name, m,
           last > loud ? (double)(last - loud) / SR : 0.0);
    CHECK(last < loud + decay_frames);                 /* the tail reached exact zero */
    /* reset: history gone, zero in gives zero out at once */
    for (uint32_t i = 0; i < 2u * 64; i++) in[i] = 30000;
    run(64, P256, 1);
    md_effect_reset(&fx);
    memset(in, 0, 2u * 256 * sizeof *in);
    run(256, P256, 1);
    CHECK(last_nonzero(256) == 0);
    return 0;
}
static int stability_limits_and_decay(void)
{
    const int32_t filter[] = {MD_FILTER_LOWPASS, 500, 100};
    const int32_t lowest[] = {MD_FILTER_LOWPASS, 0, 0};
    const int32_t phaser[] = {500, 100, 90, 100};
    const int32_t phaser_neg[] = {1, 100, -90, 100};
    const int32_t echo[] = {0, 20, 100, 100};
    const int32_t reverb[] = {MD_REVERB_ROOM, 0, 0};
    const int32_t cathedral[] = {MD_REVERB_CATHEDRAL, 100, 0};
    if (bounded_and_decays(&MD_FX_FILTER, filter, 3, SR)) return 1;
    if (bounded_and_decays(&MD_FX_FILTER, lowest, 3, SR)) return 1;
    if (bounded_and_decays(&MD_FX_PHASER, phaser, 4, SR)) return 1;
    if (bounded_and_decays(&MD_FX_PHASER, phaser_neg, 4, SR)) return 1;
    if (bounded_and_decays(&MD_FX_ECHO, echo, 4, 4u * SR)) return 1;
    if (bounded_and_decays(&MD_FX_REVERB, reverb, 3, 4u * SR)) return 1;
    if (bounded_and_decays(&MD_FX_REVERB, cathedral, 3, LONG - SR / 2u)) return 1;
    return 0;
}

static int partition_independence_with_smoothing(void)
{
    for (uint32_t i = 0; i < 2u * 3u * SR; i++) in[i] = (int32_t)((i * 2654435761u) >> 17) - 16384;
    const md_effect_desc_t *const all[] = {&MD_FX_FILTER, &MD_FX_PHASER, &MD_FX_ECHO, &MD_FX_REVERB};
    for (uint32_t e = 0; e < 4; e++)
        for (uint32_t p = 0; p < 6; p++) {
            CHECK(!make(all[e]));
            /* targets move after init, so every smoother and glide is moving during the render */
            const md_processor_desc_t *d = &all[e]->processor;
            for (uint32_t k = 0; k < d->param_count; k++)
                CHECK(md_effect_param(&fx, d->params[k].id, (d->params[k].min + d->params[k].max) / 3) == MD_OK);
            (void)md_effect_tempo(&fx, 97u * 256u);
            run(3u * SR, PARTS[p].p, PARTS[p].n);
            if (!p) memcpy(ref, out, 2u * 3u * SR * sizeof *out);
            else CHECK(!memcmp(ref, out, 2u * 3u * SR * sizeof *out));
        }
    /* a cutoff jump is smoothed: after one control period it has moved 1/32 of the way */
    CHECK(!make(&MD_FX_FILTER));
    CHECK(md_effect_param(&fx, MD_FILTER_CUTOFF, 0) == MD_OK);
    memset(in, 0, 2u * 64 * sizeof *in);
    run(1, P1, 1);
    CHECK(filter_state.cut_q8 == 990 * 256 - (990 * 256 >> 5));
    run(4096, P256, 1);
    CHECK(filter_state.cut_q8 == 0);
    return 0;
}

/* largest |second difference| of the left channel over [from, to): a click shows up here */
static int32_t roughness(uint32_t from, uint32_t to)
{
    int32_t m = 0;
    for (uint32_t i = from + 2; i < to; i++) {
        int32_t d = out[2 * i] - 2 * out[2 * (i - 1)] + out[2 * (i - 2)];
        m = abs(d) > m ? abs(d) : m;
    }
    return m;
}
/* Review finding: depth (phaser), feedback and tone (echo) are declared smoothed and must be. */
static int declared_smoothing_is_real(void)
{
    CHECK(!make(&MD_FX_PHASER));
    const int32_t flat[] = {30, 0, 0, 100};
    uint8_t snap[8];
    for (uint32_t k = 0; k < 4; k++) CHECK(md_effect_param(&fx, MD_FX_PHASER.processor.params[k].id, flat[k]) == MD_OK);
    CHECK(md_effect_encode(&fx, snap, 8) == 8 && md_effect_apply(&fx, 1, snap, 8) == MD_OK);
    sine(200, 16000, 2u * SR);
    run(SR, P256, 1);
    int32_t steady = roughness(SR / 2u, SR);
    CHECK(md_effect_param(&fx, MD_PHASER_DEPTH, 100) == MD_OK);
    memcpy(in, &in[2u * SR], 2u * SR * sizeof *in);
    run(8, P1, 1);
    CHECK(phaser_state.depth_q8 == (100 * 256) >> 5);   /* one control period: 1/32 of the way */
    int32_t head[16];
    memcpy(head, out, sizeof head);
    memmove(in, &in[16], 2u * SR * sizeof *in - 16u * sizeof *in);   /* continue the input after those 8 frames */
    run(SR / 2u, P256, 1);
    memmove(&out[16], out, (2u * SR / 2u) * sizeof *out);
    memcpy(out, head, sizeof head);
    int32_t step = roughness(0, SR / 2u);
    printf("  phaser depth 0 -> 100: second difference %d against %d steady\n", step, steady);
    CHECK(step <= 2 * steady && phaser_state.depth_q8 == 100 * 256);
    CHECK(!make(&MD_FX_ECHO));
    CHECK(md_effect_param(&fx, MD_ECHO_FEEDBACK, 100) == MD_OK && md_effect_param(&fx, MD_ECHO_TONE, 0) == MD_OK);
    int32_t gain0 = echo_state.gain, k0 = echo_state.k;
    CHECK(gain0 == 40 * 278 && echo_state.gain_target == 27800 && echo_state.k_target == 11998);
    memset(in, 0, 2u * 64 * sizeof *in);
    run(4, P1, 1);
    CHECK(echo_state.gain == gain0 + ((27800 - gain0) >> 5) && echo_state.k == k0 - ((k0 - 11998) >> 5));
    run(SR / 4u, P256, 1);
    CHECK(echo_state.gain == 27800 && echo_state.k == 11998);
    return 0;
}

static int echo_timing(void)
{
    CHECK(!make(&MD_FX_ECHO));
    const int32_t s[] = {0, 100, 50, 100};
    for (uint32_t k = 0; k < 4; k++) CHECK(md_effect_param(&fx, MD_FX_ECHO.processor.params[k].id, s[k]) == MD_OK);
    uint8_t snap[8];
    CHECK(md_effect_encode(&fx, snap, 8) == 8 && md_effect_apply(&fx, 1, snap, 8) == MD_OK);
    md_effect_reset(&fx);
    memset(in, 0, 2u * SR * sizeof *in);
    for (uint32_t i = 0; i < 4; i++) in[2 * i] = in[2 * i + 1] = 20000;
    run(SR, P256, 1);
    uint32_t left = first_nonzero(0, SR, 0), right = first_nonzero(0, SR, 1);
    uint32_t second = first_nonzero(left + 400, SR, 0);
    int32_t p1 = peak(left, left + 400), p2 = peak(second, second + 400);
    printf("  echo: first repeat at %u frames (left), %u (right), second at %u; peaks %d, %d\n", left, right, second, p1, p2);
    CHECK(abs((int)left - 4410) <= 8 && abs((int)(right - left) - 882) <= 8 && abs((int)(second - 2 * left)) <= 8);
    CHECK(p1 > 15000 && p2 > p1 / 5 && p2 < p1 / 2);  /* 0.42 feedback, a little tone loss */
    /* tempo sync: 1/4 at 120 BPM is 500 ms; at 240 BPM the delay glides to 250 ms */
    CHECK(md_effect_param(&fx, MD_ECHO_SYNC, 4) == MD_OK);
    md_effect_reset(&fx);
    run(SR, P256, 1);
    CHECK(abs((int)first_nonzero(0, SR, 0) - 22050) <= 8);
    CHECK(md_effect_tempo(&fx, 240u * 256u) == MD_OK);
    CHECK(echo_state.target_q8 == (int32_t)(2756u << 8) && echo_state.delay_q8 == (int32_t)(5512u << 8));
    memset(in, 0, 2u * SR * sizeof *in);
    run(SR / 2u, P256, 1);
    int32_t moved = (5512 << 8) - echo_state.delay_q8;
    CHECK(moved > 0 && moved <= (int32_t)(SR / 2u / 4u + 1u) * 64);   /* glides at a quarter sample per line sample */
    return 0;
}

static int reverb_decay_law(void)
{
    /* HALL at size 50: 1.2 + 0.5 x 3.3 = 2.85 s to -60 dB */
    CHECK(!make(&MD_FX_REVERB));
    const int32_t s[] = {MD_REVERB_HALL, 50, 0};
    for (uint32_t k = 0; k < 3; k++) CHECK(md_effect_param(&fx, MD_FX_REVERB.processor.params[k].id, s[k]) == MD_OK);
    memset(in, 0, 2u * 4u * SR * sizeof *in);
    for (uint32_t i = 0; i < 64; i++) { in[2 * i] = 30000; in[2 * i + 1] = -30000; }
    run(4u * SR, P256, 1);
    double early = rms(SR / 4u, SR / 4u + 4410), late = rms(SR / 4u + 62843, SR / 4u + 62843 + 4410); /* +1.425 s */
    double db = 20.0 * log10(late / early);
    printf("  reverb: %.1f dB over half the 2.85 s decay (law -30 dB)\n", db);
    CHECK(db < -22.0 && db > -38.0);
    int differ = 0;
    for (uint32_t i = SR / 4u; i < SR; i++) differ += out[2 * i] != out[2 * i + 1];
    CHECK(differ > (int)(SR / 2u));                     /* left and right are decorrelated */
    return 0;
}

static int mixer_integration_and_cost(void)
{
    /* the echo on the mixer's echo bus: one repeat of the dry impulse at the set time */
    static md_mixer_t mixer;
    static int32_t track[2u * 256u], mix[2u * SR];
    CHECK(!make(&MD_FX_ECHO));
    CHECK(md_effect_param(&fx, MD_ECHO_SYNC, 0) == MD_OK && md_effect_param(&fx, MD_ECHO_TIME, 50) == MD_OK);
    CHECK(md_effect_param(&fx, MD_ECHO_FEEDBACK, 0) == MD_OK);
    CHECK(md_mixer_init(&mixer) == MD_OK && md_mixer_set_bus(&mixer, MD_MIX_BUS_ECHO, &fx) == MD_OK);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_ECHO_SEND, 32767) == MD_OK && md_mixer_reset(&mixer) == MD_OK);
    memset(mix, 0, sizeof mix);
    for (uint32_t at = 0; at < SR / 2u; at += 256) {
        memset(track, 0, sizeof track);
        if (!at) for (uint32_t i = 0; i < 4; i++) track[2 * i] = track[2 * i + 1] = 10000;
        CHECK(md_mixer_begin(&mixer, 256) == MD_OK && md_mixer_add_track(&mixer, 0, track) == MD_OK);
        CHECK(md_mixer_finish(&mixer, &mix[2u * at]) == MD_OK);
    }
    uint32_t echo_at = 0;
    for (uint32_t i = 8; i < SR / 2u && !echo_at; i++) if (mix[2 * i]) echo_at = i;
    CHECK(mix[0] == 10000 && abs((int)echo_at - 2205) <= 8);
    /* native cost per processor: one second of audio in 256-frame spans */
    const md_effect_desc_t *const all[] = {&MD_FX_FILTER, &MD_FX_PHASER, &MD_FX_ECHO, &MD_FX_REVERB};
    for (uint32_t i = 0; i < 2u * SR; i++) in[i] = (int32_t)((i * 2654435761u) >> 17) - 16384;
    for (uint32_t e = 0; e < 4; e++) {
        CHECK(!make(all[e]));
        clock_t t0 = clock();
        for (uint32_t r = 0; r < 20; r++) run(SR, P256, 1);
        double us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 20.0 / (SR / 256.0);
        printf("  cost: %-6s %6zu bytes state, %.2f us per 256-frame span natively (%.3f%% of a span)\n",
               all[e]->processor.name, (size_t)all[e]->processor.state_bytes, us, us / (256.0 / SR * 1e6) * 100.0);
    }
    return 0;
}

int main(void)
{
    RUN(registry_and_settings);
    RUN(filter_response);
    RUN(stability_limits_and_decay);
    RUN(partition_independence_with_smoothing);
    RUN(declared_smoothing_is_real);
    RUN(echo_timing);
    RUN(reverb_decay_law);
    RUN(mixer_integration_and_cost);
    return 0;
}
