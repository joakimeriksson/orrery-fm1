/* SPDX-License-Identifier: GPL-3.0-only */
/* The processing section's echo (seqdock): a tape echo in the host, as Jarre's Revox behind the ARP 2600's
 * sequences. One line at a quarter of the sample rate (11.025 kHz: 1.1 s in 24 KiB of the host's RAM, where a
 * full-rate line would not fit beside the reverb; a tape echo's repeats are dark, and a one-pole tone in the
 * loop makes them darker each time round, as the tape does), the time a division of the band's tempo, the right
 * channel's tap 20 ms behind the left's for width. In: the sends added up, mono Q15 a frame; out: the repeats,
 * interpolated back to the full rate, added to the interleaved block. Compiled into dock.c (seqdock) and by
 * tests/echo_check.py on a computer. */
#define EC_DIV 4u
#define EC_RATE (44100u / EC_DIV)
#define EC_LEN 12288u                           /* line samples: 1.11 s */
#define EC_SPREAD 220u                          /* the right tap behind the left: 20 ms */
#define EC_MAX_MS ((EC_LEN - EC_SPREAD - 8u) * 1000u / EC_RATE)   /* 1092 ms */
static int16_t ec_buf[EC_LEN];
static volatile uint32_t ec_on;                 /* the ISR runs it */
static uint32_t ec_w, ec_t;                     /* the write index; the time in line samples */
static int32_t ec_gain, ec_k, ec_lp, ec_yl, ec_yr;   /* feedback Q15; the tone's one-pole Q15 and its state; the taps' last */
static const uint16_t EC_TONE[11] = {11998, 13815, 15788, 17888, 20069, 22270, 24415, 26420, 28202, 29695, 30857};   /* 1 - e^(-2 pi fc / rate), fc 800 Hz .. 5 kHz */

static void ec_clear(void)
{
    uint32_t i;
    for (i = 0; i < EC_LEN; i++) ec_buf[i] = 0;
    ec_lp = ec_yl = ec_yr = 0;
}
static uint32_t ec_div_ms(uint32_t div, uint32_t bpm)   /* div 1..6: 1/16, 1/8, 1/8., 1/4, 1/4., 1/2 at bpm, in ms */
{
    static const uint8_t NUM[7] = {0, 1, 2, 3, 4, 6, 8};   /* sixteenths */
    uint32_t ms = 15000u * NUM[div < 7u ? div : 0] / (bpm ? bpm : 120u);
    return ms > EC_MAX_MS ? EC_MAX_MS : ms;
}
static void ec_prep(uint32_t ms, uint32_t fb_pct, uint32_t tone_pct)   /* fb up to 85 %, the tone 800 Hz .. 5 kHz */
{
    uint32_t t = ms * EC_RATE / 1000u, k = tone_pct > 100u ? 10u : tone_pct / 10u, r = tone_pct > 100u ? 0 : tone_pct % 10u;
    if (t + EC_SPREAD + 8u > EC_LEN) t = EC_LEN - EC_SPREAD - 8u;
    if (t < 8u) t = 8u;
    ec_t = t;
    ec_gain = (int32_t)((fb_pct > 100u ? 100u : fb_pct) * 278u);
    ec_k = (int32_t)(EC_TONE[k] + (k < 10u ? (EC_TONE[k + 1] - EC_TONE[k]) * r / 10u : 0));
}
static void ec_run(const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i + EC_DIV <= n; i += EC_DIV) {
        int32_t x = (in[i] + in[i + 1] + in[i + 2] + in[i + 3]) >> 2;
        uint32_t rl = (ec_w + EC_LEN - ec_t) % EC_LEN, rr = (ec_w + 2u * EC_LEN - ec_t - EC_SPREAD) % EC_LEN;
        int32_t yl = ec_buf[rl], yr = ec_buf[rr], v;
        ec_lp += ((x + ((yl * ec_gain) >> 15) - ec_lp) * ec_k) >> 15;   /* the tape: one pass, one pole */
        v = ec_lp > 32767 ? 32767 : ec_lp < -32767 ? -32767 : ec_lp;
        ec_buf[ec_w] = (int16_t)v;
        ec_w = (ec_w + 1u) % EC_LEN;
        for (j = 0; j < EC_DIV; j++) {          /* the four frames: from the last tap values to these */
            int32_t k = (int32_t)(j + 1u);
            out[2u * (i + j)] += ec_yl + (((yl - ec_yl) * k) >> 2);
            out[2u * (i + j) + 1u] += ec_yr + (((yr - ec_yr) * k) >> 2);
        }
        ec_yl = yl;
        ec_yr = yr;
    }
}
