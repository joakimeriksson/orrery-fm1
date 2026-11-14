/* SPDX-License-Identifier: GPL-3.0-only */
/* Reference renders of the local effects (M3-03), to listen to and to draw:
 *   cc -std=c11 -O2 -I . tools/render_effects.c core/effect.c effects/filter.c effects/phaser.c \
 *      effects/echo.c effects/reverb.c effects/registry.c -o build/effects/render && build/effects/render build/effects
 * Writes sweep.wav (sawtooth through a resonant low-pass swept 20 Hz .. 10 kHz and back), phaser.wav (a held
 * sawtooth chord), echo.wav (clicks into the tape echo, wet only) and reverb.wav (clicks into the hall, wet only). */
#include "effects/effects.h"
#include <stdio.h>
#include <string.h>

#define SR 44100u
#define SECONDS 6u
#define N (SR * SECONDS)
static int32_t buf[2u * N], wet[2u * N];
static md_fx_filter_t fs; static md_fx_phaser_t ps; static md_fx_echo_t es; static md_fx_reverb_t rs;

static void wav(const char *dir, const char *name, const int32_t *x)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    uint32_t bytes = N * 4u, v;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    uint16_t h[] = {1, 2}; fwrite(h, 2, 2, f);
    v = SR; fwrite(&v, 4, 1, f); v = SR * 4u; fwrite(&v, 4, 1, f);
    uint16_t h2[] = {4, 16}; fwrite(h2, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < 2u * N; i++) {
        int32_t s = x[i] > 32767 ? 32767 : x[i] < -32767 ? -32767 : x[i];
        int16_t o = (int16_t)s; fwrite(&o, 2, 1, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}
static void saw(uint32_t hz_x100, int32_t amp, int add)
{
    uint32_t phase = 0, inc = (uint32_t)((uint64_t)hz_x100 * 4294967296ull / (SR * 100u));
    for (uint32_t i = 0; i < N; i++, phase += inc) {
        int32_t v = (int32_t)(((int64_t)(int32_t)phase * amp) >> 31);
        if (add) { buf[2 * i] += v; buf[2 * i + 1] += v; } else buf[2 * i] = buf[2 * i + 1] = v;
    }
}
static void clicks(void)
{
    memset(buf, 0, sizeof buf);
    for (uint32_t at = 0; at < SR * 2u; at += SR / 2u)
        for (uint32_t i = 0; i < 64; i++) buf[2 * (at + i)] = buf[2 * (at + i) + 1] = (i & 8u) ? 20000 : -20000;
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    md_effect_t e;
    /* sweep: 55 Hz saw, cutoff 0 -> 860 -> 0 over 6 s, resonance 85, in 64-frame spans with a lock per span */
    saw(5500, 12000, 0);
    md_effect_init(&e, &MD_FX_FILTER, &fs, sizeof fs, SR);
    md_effect_param(&e, MD_FILTER_RESONANCE, 85);
    for (uint32_t at = 0; at < N; at += 64) {
        uint32_t tri = at < N / 2u ? at : N - at;
        md_effect_param(&e, MD_FILTER_CUTOFF, (int32_t)((uint64_t)tri * 860u / (N / 2u)));
        md_effect_process_replace(&e, &buf[2u * at], 64);
    }
    wav(dir, "sweep.wav", buf);
    /* phaser: a minor chord of saws, rate 0.4 Hz, depth 90, feedback 60 */
    saw(22000, 5000, 0); saw(26163, 5000, 1); saw(32963, 5000, 1);
    md_effect_init(&e, &MD_FX_PHASER, &ps, sizeof ps, SR);
    md_effect_param(&e, MD_PHASER_RATE, 40); md_effect_param(&e, MD_PHASER_DEPTH, 90); md_effect_param(&e, MD_PHASER_FEEDBACK, 60);
    for (uint32_t at = 0; at < N; at += 100) md_effect_process_replace(&e, &buf[2u * at], at + 100 <= N ? 100 : N - at);
    wav(dir, "phaser.wav", buf);
    /* echo: dotted eighth at 120 BPM (375 ms), feedback 60, tone 40 */
    clicks();
    md_effect_init(&e, &MD_FX_ECHO, &es, sizeof es, SR);
    md_effect_param(&e, MD_ECHO_SYNC, 3); md_effect_param(&e, MD_ECHO_FEEDBACK, 60); md_effect_param(&e, MD_ECHO_TONE, 40);
    for (uint32_t at = 0; at < N; at += 256) md_effect_process_wet(&e, &buf[2u * at], &wet[2u * at], 256);
    wav(dir, "echo.wav", wet);
    /* reverb: hall, size 60, damping 30 */
    clicks();
    md_effect_init(&e, &MD_FX_REVERB, &rs, sizeof rs, SR);
    md_effect_param(&e, MD_REVERB_SIZE, 60); md_effect_param(&e, MD_REVERB_DAMPING, 30);
    for (uint32_t at = 0; at < N; at += 256) md_effect_process_wet(&e, &buf[2u * at], &wet[2u * at], 256);
    wav(dir, "reverb.wav", wet);
    return 0;
}
