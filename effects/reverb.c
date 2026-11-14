/* SPDX-License-Identifier: GPL-3.0-only */
/* Reverb bus, ported from fm1-harbor firmware/src/hb_fx.c (GPL-3.0-only; itself
 * Scintilla's eight-line reverb, joakimeriksson/scintilla-fm1 engine/sc_synth.c, at half
 * the lines) to a caller-owned instance:
 *  - the same four 16-bit lines (1051, 1327, 1613, 1907 samples) in a Hadamard loop,
 *    input taps, signs, decay law per algorithm and size, damping lowpass and level;
 *  - per-sample order instead of the host's block passes (the host notes they are
 *    equivalent for blocks up to 256), so any span length gives the same output;
 *  - wet-only output at full mix (the mixer's return level is the mix);
 *  - the loop rounds toward zero and the damping lowpass keeps four extra bits, so a tail
 *    ends in exact silence instead of a small limit cycle;
 *  - the inner loop is unrolled with constant line offsets and branch-free rounding. */
#include "fx_common.h"
#include <string.h>

static const uint32_t rates[] = {MD_SAMPLE_RATE};
static const char *const algorithms[] = {"ROOM", "PLATE", "HALL", "CATHEDRAL"};
static const md_param_desc_t params[] = {
    {MD_REVERB_ALGORITHM, MD_PARAM_AUTOMATABLE | MD_PARAM_ENUM, "Type", 0, 3, 2, MD_UNIT_INTEGER, algorithms, 4},
    {MD_REVERB_SIZE, MD_PARAM_AUTOMATABLE, "Size", 0, 100, 50, MD_UNIT_PERCENT, NULL, 0},
    {MD_REVERB_DAMPING, MD_PARAM_AUTOMATABLE, "Damping", 0, 100, 40, MD_UNIT_PERCENT, NULL, 0},
};
#define COUNT 3u
#define MASK (MD_REVERB_LEN * MD_REVERB_LINES - 1u)
#define SH 7 /* lines hold Q13 (Q20 >> 7) */
#define Q16(x) ((int32_t)((x) * 65536.0 + 0.5))
#define ONE16 65536
static const uint16_t D[MD_REVERB_LINES] = {1051, 1327, 1613, 1907};
/* input taps P = 67, 307, 547, 887 and signs +L +R -L -R are written out in process() */
static const int32_t BASE[4] = {Q16(0.6), Q16(0.3), Q16(1.2), Q16(3.0)};
static const int32_t MAXS[4] = {Q16(2.5), Q16(1.4), Q16(4.5), Q16(9.0)};
static const int32_t BRIGHT[4] = {Q16(0.85), Q16(0.65), Q16(0.55), Q16(0.35)};
static const uint32_t EXP2[65] = { /* 2^(i / 64), Q16 */
    65536u, 66250u, 66971u, 67700u, 68438u, 69183u, 69936u, 70698u, 71468u, 72246u,
    73032u, 73828u, 74632u, 75444u, 76266u, 77096u, 77936u, 78785u, 79642u, 80510u,
    81386u, 82273u, 83169u, 84074u, 84990u, 85915u, 86851u, 87796u, 88752u, 89719u,
    90696u, 91684u, 92682u, 93691u, 94711u, 95743u, 96785u, 97839u, 98905u, 99982u,
    101070u, 102171u, 103283u, 104408u, 105545u, 106694u, 107856u, 109031u, 110218u, 111418u,
    112631u, 113858u, 115098u, 116351u, 117618u, 118899u, 120194u, 121502u, 122825u, 124163u,
    125515u, 126882u, 128263u, 129660u, 131072u,
};
static int32_t exp2_q16(int32_t y) /* 2^(y / 65536), Q16; y <= 0 */
{
    int32_t ip = y >> 16;
    uint32_t f = (uint32_t)y & 0xffffu, i = f >> 10, fr = f & 1023u;
    uint32_t v = EXP2[i] + (uint32_t)(((uint64_t)(EXP2[i + 1] - EXP2[i]) * fr) >> 10);
    return ip >= 0 ? (int32_t)(v << ip) : ip < -30 ? 0 : (int32_t)(v >> -ip);
}
static uint32_t isqrt64(uint64_t x)
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return (uint32_t)r;
}
/* hb_fx.c fx_prep at mix 100 % */
static void prepare(md_fx_reverb_t *r)
{
    int32_t sz = r->size * 65536 / 100, dp = r->damping * 65536 / 100;
    int al = (int)r->algorithm;
    int32_t sec = BASE[al] + fx_mul(sz, MAXS[al] - BASE[al], 16);
    uint32_t len = (uint32_t)(((uint64_t)(uint32_t)sec * 44100u) >> 16);
    for (uint32_t i = 0; i < MD_REVERB_LINES; i++) {
        uint32_t y = fx_udiv64((uint64_t)653118u * D[i], len); /* log2(1000) d / N, Q16 */
        r->g[i] = (int32_t)(((int64_t)exp2_q16(-(int32_t)y) * 536870912) >> 16); /* x 1/2, Q30 */
    }
    int32_t fc = fx_mul(fx_mul(20000, ONE16 - dp, 16), BRIGHT[al], 16) + 200;
    r->a = ONE16 - exp2_q16(-(int32_t)(((int64_t)fc * 882827) >> 16));
    uint32_t ratio = fx_udiv64((uint64_t)(uint32_t)(2 * ONE16 - r->a) << 16, (uint32_t)r->a);
    uint32_t early = fx_udiv64((uint64_t)len << 32, len + 12286u);
    r->wet = fx_mul(fx_mul(5787, (int32_t)isqrt64((uint64_t)ratio << 16), 16), (int32_t)isqrt64(early), 16);
}
static int init(void *state, uint32_t rate)
{
    (void)rate;
    md_fx_reverb_t *r = state;
    memset(r, 0, sizeof *r);
    r->algorithm = MD_REVERB_HALL; r->size = 50; r->damping = 40;
    prepare(r);
    return MD_OK;
}
static int set_param(void *state, md_param_id_t id, int32_t v)
{
    md_fx_reverb_t *r = state;
    if (id == MD_REVERB_ALGORITHM) r->algorithm = v;
    else if (id == MD_REVERB_SIZE) r->size = v;
    else if (id == MD_REVERB_DAMPING) r->damping = v;
    else return MD_ERR_NOT_FOUND;
    prepare(r);
    return MD_OK;
}
static int get_param(const void *state, md_param_id_t id, int32_t *v)
{
    const md_fx_reverb_t *r = state;
    if (id == MD_REVERB_ALGORITHM) *v = r->algorithm;
    else if (id == MD_REVERB_SIZE) *v = r->size;
    else if (id == MD_REVERB_DAMPING) *v = r->damping;
    else return MD_ERR_NOT_FOUND;
    return MD_OK;
}
/* Branch-free rounding toward zero (so a tail decays to exact silence instead of a -1 limit cycle). */
static inline int32_t shr0_32(int32_t v, int sh) { return (v + ((v >> 31) & ((1 << sh) - 1))) >> sh; }
static inline int32_t shr0_64(int64_t p, int sh) { return (int32_t)((p + ((p >> 63) & (((int64_t)1 << sh) - 1))) >> sh); }
static inline int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
/* line k's slot that an input entering now reaches (D - P samples behind the write) and its tap (D behind) */
#define IN_AT(k) ((t4 + (k) + 4u * (MD_REVERB_LEN + P_##k - D_##k)) & MASK)
#define TAP(k) ((int32_t)b[(t4 + (k) + 4u * (MD_REVERB_LEN - D_##k)) & MASK] * (1 << SH))
#define D_0 1051u
#define D_1 1327u
#define D_2 1613u
#define D_3 1907u
#define P_0 67u
#define P_1 307u
#define P_2 547u
#define P_3 887u
#define LP_X 4 /* the damping lowpass keeps 4 more bits than the lines, so plain shifts settle */
static void process(void *state, const int32_t *in, int32_t *wet, uint32_t frames)
{
    md_fx_reverb_t *r = state;
    int16_t *b = r->line;
    const int32_t g0 = r->g[0], g1 = r->g[1], g2 = r->g[2], g3 = r->g[3], a = r->a, level = r->wet;
    int32_t lpl = r->lpl, lpr = r->lpr;
    uint32_t w = r->w;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t t4 = 4u * w;
        int32_t xl = in[2 * i], xr = in[2 * i + 1];
        xl = (xl > (1 << 18) ? (1 << 18) : xl < -(1 << 18) ? -(1 << 18) : xl) * 32;   /* Q15 -> Q20 */
        xr = (xr > (1 << 18) ? (1 << 18) : xr < -(1 << 18) ? -(1 << 18) : xr) * 32;
        int32_t ql = shr0_32(xl, SH), qr = shr0_32(xr, SH);   /* the signs: +L +R -L -R (hb_fx.c FX_S) */
        b[IN_AT(0)] = sat16(b[IN_AT(0)] + ql);
        b[IN_AT(1)] = sat16(b[IN_AT(1)] + qr);
        b[IN_AT(2)] = sat16(b[IN_AT(2)] - ql);
        b[IN_AT(3)] = sat16(b[IN_AT(3)] - qr);
        int32_t y0 = TAP(0), y1 = TAP(1), y2 = TAP(2), y3 = TAP(3);
        int32_t a0 = y0 + y1, a1 = y0 - y1, a2 = y2 + y3, a3 = y2 - y3;
        int32_t h0 = a0 + a2, h1 = a1 + a3;
        b[(t4 + 0u) & MASK] = sat16(shr0_64((int64_t)h0 * g0, 30 + SH));
        b[(t4 + 1u) & MASK] = sat16(shr0_64((int64_t)h1 * g1, 30 + SH));
        b[(t4 + 2u) & MASK] = sat16(shr0_64((int64_t)(a0 - a2) * g2, 30 + SH));
        b[(t4 + 3u) & MASK] = sat16(shr0_64((int64_t)(a1 - a3) * g3, 30 + SH));
        w = (w + 1u) & (MD_REVERB_LEN - 1u);
        lpl += (int32_t)(((int64_t)(h0 * (1 << LP_X) - lpl) * a) >> 16);
        lpr += (int32_t)(((int64_t)(h1 * (1 << LP_X) - lpr) * a) >> 16);
        wet[2 * i] = shr0_64((int64_t)lpl * level, 21 + LP_X);   /* level Q16, then Q20 -> Q15 */
        wet[2 * i + 1] = shr0_64((int64_t)lpr * level, 21 + LP_X);
    }
    r->lpl = lpl;
    r->lpr = lpr;
    r->w = w;
}
static void reset(void *state)
{
    md_fx_reverb_t *r = state;
    memset(r->line, 0, sizeof r->line);
    r->lpl = r->lpr = 0;
    r->w = 0;
}
static int encode(const void *state, uint8_t *dst, uint32_t capacity)
{
    return fx_settings_encode(params, COUNT, state, get_param, dst, capacity);
}
static int validate(const uint8_t *src, uint32_t bytes) { return fx_settings_validate(params, COUNT, src, bytes); }
static int apply(void *state, const uint8_t *src, uint32_t bytes)
{
    int status = validate(src, bytes);
    if (status) return status;
    for (uint32_t i = 0; i < COUNT; i++) (void)set_param(state, params[i].id, fx_settings_value(src, i));
    return MD_OK;
}
const md_effect_desc_t MD_FX_REVERB = {
    .processor = {.id = MD_FX_REVERB_ID, .api_version = MD_API_VERSION, .settings_version = 1, .name = "REVERB",
                  .state_bytes = sizeof(md_fx_reverb_t), .state_alignment = _Alignof(md_fx_reverb_t),
                  .sample_rates = rates, .sample_rate_count = 1, .param_count = COUNT, .params = params,
                  .max_settings_bytes = 2u * COUNT},
    .capabilities = MD_EFFECT_BUS, .init = init, .set_param = set_param, .get_param = get_param,
    .process_wet = process, .reset = reset, .encode = encode, .validate = validate, .apply = apply,
};
