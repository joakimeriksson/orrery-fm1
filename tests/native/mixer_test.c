/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/effect.c core/mixer.c
#include "core/mixer.h"
#include "test.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Purpose-built fakes. One state type; each descriptor gives it a different process. */
#define RING 64u
typedef struct {
    int32_t amount;                /* param 1: meaning depends on the fake */
    int32_t ring[2u * RING];
    uint32_t pos, delay;
    int64_t input_sum;             /* sum of |input| seen by a bus */
    uint32_t calls, resets;
    uint16_t tempo;
} fx_t;

static const uint32_t rates[] = {MD_SAMPLE_RATE};
static const md_param_desc_t fx_params[] = {
    {1, MD_PARAM_AUTOMATABLE, "Amount", -1000000, 1000000, 0, MD_UNIT_INTEGER, NULL, 0},
};
static int fx_init(void *s, uint32_t rate) { (void)rate; memset(s, 0, sizeof(fx_t)); return MD_OK; }
static int fx_set(void *s, md_param_id_t id, int32_t v) { (void)id; ((fx_t *)s)->amount = v; return MD_OK; }
static int fx_get(const void *s, md_param_id_t id, int32_t *v) { (void)id; *v = ((const fx_t *)s)->amount; return MD_OK; }
static void fx_tempo(void *s, uint16_t q8) { ((fx_t *)s)->tempo = q8; }
static void fx_reset(void *s)
{
    fx_t *f = s;
    memset(f->ring, 0, sizeof f->ring);
    f->pos = 0; f->resets++;
}
static int fx_encode(const void *s, uint8_t *d, uint32_t c) { (void)s; (void)d; (void)c; return 0; }
static int fx_validate(const uint8_t *src, uint32_t n) { (void)src; return n ? MD_ERR_FORMAT : MD_OK; }
static int fx_apply(void *s, const uint8_t *src, uint32_t n) { (void)s; (void)src; (void)n; return MD_OK; }

/* amount is a Q8 multiplier */
static void scale(void *s, int32_t *x, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < 2u * frames; i++) x[i] = (int32_t)(((int64_t)x[i] * f->amount) / 256);
}
/* amount is added to every sample */
static void offset(void *s, int32_t *x, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < 2u * frames; i++) x[i] += f->amount;
}
/* holds the last nonzero input forever: makes stale history obvious */
static void latch(void *s, int32_t *x, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < 2u * frames; i++) {
        if (x[i]) f->ring[i & 1u] = x[i];
        x[i] = f->ring[i & 1u];
    }
}
/* delay by f->delay frames (insert form) */
static void delay_insert(void *s, int32_t *x, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < 2; c++) {
            f->ring[2u * f->pos + c] = x[2 * i + c];
            x[2 * i + c] = f->ring[2u * ((f->pos + RING - f->delay) % RING) + c];
        }
        f->pos = (f->pos + 1u) % RING;
    }
}
/* wet-only delay by f->delay frames; records the input it sees */
static void delay_bus(void *s, const int32_t *in, int32_t *wet, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < 2; c++) {
            f->ring[2u * f->pos + c] = in[2 * i + c];
            f->input_sum += in[2 * i + c] < 0 ? -(int64_t)in[2 * i + c] : in[2 * i + c];
            wet[2 * i + c] = f->ring[2u * ((f->pos + RING - f->delay) % RING) + c];
        }
        f->pos = (f->pos + 1u) % RING;
    }
}
static void huge_insert(void *s, int32_t *x, uint32_t frames)
{
    fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < 2u * frames; i++) x[i] = (i & 1u) ? INT32_MIN : INT32_MAX;
}
static void huge_bus(void *s, const int32_t *in, int32_t *wet, uint32_t frames)
{
    (void)in; fx_t *f = s; f->calls++;
    for (uint32_t i = 0; i < 2u * frames; i++) wet[i] = (i & 1u) ? INT32_MIN : INT32_MAX;
}

#define FX_DESC(ident, label, caps, rep, wetfn) { \
    .processor = {.id = ident, .api_version = MD_API_VERSION, .settings_version = 1, .name = label, \
                  .state_bytes = sizeof(fx_t), .state_alignment = _Alignof(fx_t), .sample_rates = rates, \
                  .sample_rate_count = 1, .param_count = 1, .params = fx_params, .max_settings_bytes = 0}, \
    .capabilities = caps, .init = fx_init, .set_param = fx_set, .get_param = fx_get, .set_tempo = fx_tempo, \
    .process_replace = rep, .process_wet = wetfn, .reset = fx_reset, .encode = fx_encode, \
    .validate = fx_validate, .apply = fx_apply }

static const md_effect_desc_t SCALE = FX_DESC(0x7f000001u, "scale", MD_EFFECT_INSERT, scale, NULL);
static const md_effect_desc_t OFFSET = FX_DESC(0x7f000002u, "offset", MD_EFFECT_INSERT, offset, NULL);
static const md_effect_desc_t LATCH = FX_DESC(0x7f000003u, "latch", MD_EFFECT_INSERT, latch, NULL);
static const md_effect_desc_t DELAY_INSERT = FX_DESC(0x7f000004u, "dly-ins", MD_EFFECT_INSERT, delay_insert, NULL);
static const md_effect_desc_t DELAY_BUS = FX_DESC(0x7f000005u, "dly-bus", MD_EFFECT_BUS, NULL, delay_bus);
static const md_effect_desc_t HUGE_INSERT = FX_DESC(0x7f000006u, "huge-ins", MD_EFFECT_INSERT, huge_insert, NULL);
static const md_effect_desc_t HUGE_BUS = FX_DESC(0x7f000007u, "huge-bus", MD_EFFECT_BUS, NULL, huge_bus);

typedef struct { md_effect_t e; fx_t s; } fx_slot_t;
static int fx_make(fx_slot_t *f, const md_effect_desc_t *d, int32_t amount, uint32_t delay)
{
    if (md_effect_init(&f->e, d, &f->s, sizeof f->s, MD_SAMPLE_RATE)) return 1;
    f->s.amount = amount; f->s.delay = delay;
    return 0;
}

static md_mixer_t mixer, other;           /* large: keep off the stack */
#define FRAMES 4096u
static int32_t input[MD_TRACKS][2u * FRAMES], output[2u * FRAMES], reference[2u * FRAMES];
static int32_t scratch[2u * MD_MAX_RENDER_FRAMES];

/* Render [start, start+frames) of input into output; tracks whose bit is clear in
 * `skip` are added, others are left for finish to process as silence. */
static int span(md_mixer_t *m, uint32_t start, uint32_t frames, unsigned skip)
{
    if (md_mixer_begin(m, frames)) return 1;
    for (uint32_t t = 0; t < MD_TRACKS; t++) {
        if (skip & (1u << t)) continue;
        memcpy(scratch, &input[t][2u * start], 2u * frames * sizeof *scratch);
        if (md_mixer_add_track(m, t, scratch)) return 1;
    }
    return md_mixer_finish(m, &output[2u * start]);
}
static int render(md_mixer_t *m, uint32_t frames, const uint32_t *parts, uint32_t part_count)
{
    memset(output, 0, sizeof output);
    for (uint32_t at = 0, p = 0; at < frames; p++) {
        uint32_t n = parts[p % part_count];
        if (n > frames - at) n = frames - at;
        if (span(m, at, n, 0)) return 1;
        at += n;
    }
    return 0;
}
static const uint32_t whole[] = {256};

static int insert_order_and_exact_unity(void)
{
    fx_slot_t a, b;
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&a, &SCALE, 512, 0) && !fx_make(&b, &OFFSET, 50, 0));
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &a.e) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 0, 1, &b.e) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    input[0][0] = 100; input[0][1] = -100;
    CHECK(!render(&mixer, 8, whole, 1));
    /* insert 1 (x2) runs before insert 2 (+50): (100*2)+50, not (100+50)*2 */
    CHECK(output[0] == 250 && output[1] == -150);
    CHECK(output[2] == 50 && output[3] == 50);
    /* swapping slots swaps the order */
    CHECK(md_mixer_set_insert(&mixer, 0, 0, NULL) == MD_OK && md_mixer_set_insert(&mixer, 0, 1, NULL) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &b.e) == MD_OK && md_mixer_set_insert(&mixer, 0, 1, &a.e) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    CHECK(!render(&mixer, 8, whole, 1));
    CHECK(output[0] == 300 && output[1] == -100 && output[2] == 100);
    /* no inserts, unity level and centre pan are bit-transparent */
    CHECK(md_mixer_init(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    for (uint32_t i = 0; i < 2u * 64; i++) input[1][i] = (int32_t)(i * 517u % 65535u) - 32767;
    CHECK(!render(&mixer, 64, whole, 1));
    for (uint32_t i = 0; i < 2u * 64; i++) CHECK(output[i] == input[1][i]);
    return 0;
}

static int sends_are_post_fader_and_wet_only(void)
{
    fx_slot_t echo, reverb;
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&echo, &DELAY_BUS, 0, 4) && !fx_make(&reverb, &DELAY_BUS, 0, 6));
    CHECK(md_mixer_set_bus(&mixer, MD_MIX_BUS_ECHO, &echo.e) == MD_OK);
    CHECK(md_mixer_set_bus(&mixer, MD_MIX_BUS_REVERB, &reverb.e) == MD_OK);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_LEVEL, 16384) == MD_OK);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_PAN, 32767) == MD_OK);   /* hard right */
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_ECHO_SEND, 32767) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    input[0][0] = 1000; input[0][1] = 1000;
    CHECK(!render(&mixer, 32, whole, 1));
    /* dry once (half level, right only), echo return once at frame 4, nothing else */
    for (uint32_t i = 0; i < 32; i++) {
        int32_t want = (i == 0 || i == 4) ? 500 : 0;
        CHECK(output[2 * i] == 0 && output[2 * i + 1] == want);
    }
    CHECK(echo.s.input_sum == 500 && reverb.s.input_sum == 0); /* echo return never feeds reverb */
    /* now both sends: reverb sees only the direct send, never the echo return */
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_REVERB_SEND, 32767) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    echo.s.input_sum = reverb.s.input_sum = 0;
    CHECK(!render(&mixer, 32, whole, 1));
    for (uint32_t i = 0; i < 32; i++) {
        int32_t want = (i == 0 || i == 4 || i == 6) ? 500 : 0;
        CHECK(output[2 * i] == 0 && output[2 * i + 1] == want);
    }
    CHECK(echo.s.input_sum == 500 && reverb.s.input_sum == 500);
    /* return level scales only the wet return */
    CHECK(md_mixer_return_level(&mixer, MD_MIX_BUS_ECHO, 16384) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    CHECK(!render(&mixer, 32, whole, 1));
    CHECK(output[1] == 500 && output[9] == 250 && output[13] == 500);
    int32_t got = 0;
    CHECK(md_mixer_return_level_get(&mixer, MD_MIX_BUS_ECHO, &got) == MD_OK && got == 16384);
    return 0;
}

static int mute_keeps_return_tails(void)
{
    fx_slot_t echo;
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&echo, &DELAY_BUS, 0, 60));
    CHECK(md_mixer_set_bus(&mixer, MD_MIX_BUS_ECHO, &echo.e) == MD_OK);
    CHECK(md_mixer_track_param(&mixer, 2, MD_MIX_ECHO_SEND, 32767) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    input[2][0] = 1000; input[2][1] = -1000;
    memset(output, 0, sizeof output);
    CHECK(!span(&mixer, 0, 8, 0));
    CHECK(output[0] == 1000 && output[1] == -1000);
    CHECK(md_mixer_mute(&mixer, 2, 1) == MD_OK);
    for (uint32_t i = 8; i < FRAMES; i++) input[2][2 * i] = 20000;  /* left only */
    for (uint32_t at = 8; at < FRAMES; at += 128) CHECK(!span(&mixer, at, at + 128 <= FRAMES ? 128 : FRAMES - at, 0));
    /* the echo of the impulse, sent before the mute, still returns at frame 60 */
    CHECK(output[121] == -1000 && output[120] > 1000);
    /* the muted track fades rather than steps, and ends silent with its send */
    CHECK(output[16] > 0 && output[16] < 20000);
    for (uint32_t i = 9; i < 60; i++) CHECK(output[2 * i] <= output[2 * i - 2]);
    int64_t before = echo.s.input_sum;
    CHECK(!span(&mixer, FRAMES - 256, 256, 0));
    CHECK(echo.s.input_sum == before);
    for (uint32_t i = 2u * (FRAMES - 256); i < 2u * FRAMES; i++) CHECK(output[i] == 0);
    return 0;
}

static int worst_level_cannot_wrap(void)
{
    static fx_slot_t inserts[MD_TRACKS][MD_MIX_INSERTS], buses[MD_MIX_BUSES], master;
    for (int pass = 0; pass < 3; pass++) {
        CHECK(md_mixer_init(&mixer) == MD_OK);
        for (uint32_t t = 0; t < MD_TRACKS; t++) {
            for (uint32_t i = 0; i < 2u * 256; i++)
                input[t][i] = pass == 0 ? INT32_MAX : pass == 1 ? INT32_MIN : ((i & 1u) ? INT32_MIN : INT32_MAX);
            CHECK(md_mixer_track_param(&mixer, t, MD_MIX_ECHO_SEND, 32767) == MD_OK);
            CHECK(md_mixer_track_param(&mixer, t, MD_MIX_REVERB_SEND, 32767) == MD_OK);
            if (pass == 2)
                for (uint32_t s = 0; s < MD_MIX_INSERTS; s++) {
                    CHECK(!fx_make(&inserts[t][s], &HUGE_INSERT, 0, 0));
                    CHECK(md_mixer_set_insert(&mixer, t, s, &inserts[t][s].e) == MD_OK);
                }
        }
        for (uint32_t b = 0; b < MD_MIX_BUSES; b++) {
            CHECK(!fx_make(&buses[b], &HUGE_BUS, 0, 0));
            CHECK(md_mixer_set_bus(&mixer, b, &buses[b].e) == MD_OK);
        }
        CHECK(!fx_make(&master, &HUGE_INSERT, 0, 0));
        CHECK(md_mixer_set_master(&mixer, &master.e) == MD_OK);
        CHECK(md_mixer_master_bypass(&mixer, pass != 2) == MD_OK);
        CHECK(md_mixer_reset(&mixer) == MD_OK);
        CHECK(!render(&mixer, 256, whole, 1));
        for (uint32_t i = 0; i < 2u * 256; i++) CHECK(output[i] == 32767 || output[i] == -32767);
        md_mixer_meters_t meters;
        md_mixer_meters(&mixer, &meters);
        CHECK(meters.clipped > 0 && meters.clamped > 0);
        CHECK(meters.master_peak == MD_MIX_LIMIT);
        for (uint32_t t = 0; t < MD_TRACKS; t++) CHECK(meters.track_peak[t] == MD_MIX_LIMIT);
        md_mixer_clear_meters(&mixer);
        md_mixer_meters(&mixer, &meters);
        CHECK(!meters.clipped && !meters.master_peak);
    }
    return 0;
}

static int smoothing_and_partition_independence(void)
{
    static const uint32_t a[] = {1}, b[] = {17}, c[] = {64}, d[] = {255}, e[] = {256},
        irregular[] = {3, 250, 1, 77, 256, 9, 128, 31, 200, 5};
    static const struct { const uint32_t *p; uint32_t n; } parts[] = {
        {a, 1}, {b, 1}, {c, 1}, {d, 1}, {e, 1}, {irregular, 10}};
    for (uint32_t t = 0; t < MD_TRACKS; t++)
        for (uint32_t i = 0; i < 2u * FRAMES; i++)
            input[t][i] = (int32_t)((i * (2654435761u + t * 40503u)) >> 17) - 16384;
    static fx_slot_t ins, echo, rev, master;
    for (uint32_t p = 0; p < 6; p++) {
        CHECK(md_mixer_init(&mixer) == MD_OK);
        CHECK(!fx_make(&ins, &DELAY_INSERT, 0, 5) && !fx_make(&echo, &DELAY_BUS, 0, 33));
        CHECK(!fx_make(&rev, &DELAY_BUS, 0, 47) && !fx_make(&master, &SCALE, 200, 0));
        CHECK(md_mixer_set_insert(&mixer, 1, 1, &ins.e) == MD_OK);
        CHECK(md_mixer_set_bus(&mixer, 0, &echo.e) == MD_OK && md_mixer_set_bus(&mixer, 1, &rev.e) == MD_OK);
        CHECK(md_mixer_set_master(&mixer, &master.e) == MD_OK && md_mixer_master_bypass(&mixer, 0) == MD_OK);
        /* targets move from the init state, so every smoother is ramping during the render */
        CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_LEVEL, 0) == MD_OK);
        CHECK(md_mixer_track_param(&mixer, 1, MD_MIX_PAN, -20000) == MD_OK);
        CHECK(md_mixer_track_param(&mixer, 2, MD_MIX_ECHO_SEND, 30000) == MD_OK);
        CHECK(md_mixer_track_param(&mixer, 3, MD_MIX_REVERB_SEND, 12345) == MD_OK);
        CHECK(md_mixer_solo(&mixer, 3, 1) == MD_OK && md_mixer_solo(&mixer, 3, 0) == MD_OK);
        CHECK(md_mixer_return_level(&mixer, 1, 8000) == MD_OK);
        CHECK(!render(&mixer, FRAMES, parts[p].p, parts[p].n));
        if (!p) memcpy(reference, output, sizeof output);
        else CHECK(!memcmp(reference, output, sizeof output));
    }
    /* a level step ramps: no jump, monotonic, settles exactly */
    CHECK(md_mixer_init(&mixer) == MD_OK);
    for (uint32_t t = 0; t < MD_TRACKS; t++) memset(input[t], 0, sizeof input[t]);
    for (uint32_t i = 0; i < 2u * FRAMES; i++) input[0][i] = 30000;
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_LEVEL, 0) == MD_OK);
    CHECK(!render(&mixer, FRAMES, whole, 1));
    CHECK(output[0] > 29800 && output[0] < 30000);
    for (uint32_t i = 1; i < FRAMES; i++) {
        CHECK(output[2 * i] <= output[2 * i - 2]);
        CHECK(output[2 * i - 2] - output[2 * i] <= 30000 / 256 + 1);
    }
    CHECK(output[2u * (FRAMES - 1)] == 0 && mixer.track[0].gain[0] == 0);
    return 0;
}

static int bypass_crossfade_and_no_stale_history(void)
{
    fx_slot_t hold;
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&hold, &LATCH, 0, 0));
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &hold.e) == MD_OK);
    CHECK(md_mixer_insert_idle(&mixer, 0, 0) == 1);
    CHECK(md_mixer_reset(&mixer) == MD_OK && md_mixer_insert_idle(&mixer, 0, 0) == 0);
    memset(input, 0, sizeof input);
    input[0][0] = 1000; input[0][1] = 1000;
    CHECK(!render(&mixer, 64, whole, 1));
    CHECK(output[0] == 1000 && output[126] == 1000);     /* latched */
    CHECK(md_mixer_insert_bypass(&mixer, 0, 0, 1) == MD_OK);
    memset(input[0], 0, sizeof input[0]);
    CHECK(!render(&mixer, FRAMES, whole, 1));
    CHECK(output[0] < 1000 && output[0] > 990);          /* fades, does not step */
    for (uint32_t i = 1; i < FRAMES; i++) CHECK(output[2 * i] <= output[2 * i - 2]);
    CHECK(output[2u * (FRAMES - 1)] == 0 && md_mixer_insert_idle(&mixer, 0, 0) == 1);
    uint32_t calls = hold.s.calls, resets = hold.s.resets;
    CHECK(!render(&mixer, 512, whole, 1));
    CHECK(hold.s.calls == calls);                         /* idle slot costs nothing */
    /* re-enable: reset first, so the old latched 1000 never comes back */
    CHECK(md_mixer_insert_bypass(&mixer, 0, 0, 0) == MD_OK);
    CHECK(!render(&mixer, FRAMES, whole, 1));
    CHECK(hold.s.resets == resets + 1);
    for (uint32_t i = 0; i < 2u * FRAMES; i++) CHECK(output[i] == 0);
    /* fading in from dry: a new signal through the latch crossfades upward */
    input[0][0] = 2000; input[0][1] = 2000;
    CHECK(md_mixer_insert_bypass(&mixer, 0, 0, 1) == MD_OK && md_mixer_reset(&mixer) == MD_OK);
    CHECK(md_mixer_insert_bypass(&mixer, 0, 0, 0) == MD_OK);
    CHECK(!render(&mixer, FRAMES, whole, 1));
    CHECK(output[0] == 2000 && output[2] > 0 && output[2] < 100);
    for (uint32_t i = 2; i < FRAMES; i++) CHECK(output[2 * i] >= output[2 * i - 2]);
    CHECK(output[2u * (FRAMES - 1)] == 2000);
    /* master is bypassed by default and idle */
    CHECK(md_mixer_master_idle(&mixer) == 1);
    return 0;
}

static int panic_unadded_tracks_solo_and_tempo(void)
{
    fx_slot_t echo, ins, ins_ref;
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&echo, &DELAY_BUS, 0, 40));
    CHECK(md_mixer_set_bus(&mixer, 0, &echo.e) == MD_OK);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_ECHO_SEND, 32767) == MD_OK);
    CHECK(md_mixer_reset(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    input[0][0] = 1000;
    memset(output, 0, sizeof output);
    CHECK(!span(&mixer, 0, 16, 0));
    CHECK(md_mixer_panic(&mixer) == MD_OK);
    CHECK(!span(&mixer, 16, 64, 0));
    for (uint32_t i = 2; i < 2u * 80; i++) CHECK(output[i] == 0);   /* panic dropped the echo */
    /* a track that is not added still runs its inserts as silence */
    CHECK(md_mixer_init(&mixer) == MD_OK && md_mixer_init(&other) == MD_OK);
    CHECK(!fx_make(&ins, &DELAY_INSERT, 0, 10) && !fx_make(&ins_ref, &DELAY_INSERT, 0, 10));
    CHECK(md_mixer_set_insert(&mixer, 2, 0, &ins.e) == MD_OK && md_mixer_reset(&mixer) == MD_OK);
    CHECK(md_mixer_set_insert(&other, 2, 0, &ins_ref.e) == MD_OK && md_mixer_reset(&other) == MD_OK);
    memset(input, 0, sizeof input);
    input[2][0] = 777;
    memset(output, 0, sizeof output);
    CHECK(!span(&mixer, 0, 4, 0) && !span(&mixer, 4, 28, 1u << 2));
    memcpy(reference, output, sizeof output);
    memset(output, 0, sizeof output);
    CHECK(!span(&other, 0, 4, 0) && !span(&other, 4, 28, 0));
    CHECK(!memcmp(reference, output, sizeof output) && output[20] == 777);
    /* solo: only soloed tracks are heard; unsolo restores */
    CHECK(md_mixer_init(&mixer) == MD_OK);
    memset(input, 0, sizeof input);
    for (uint32_t t = 0; t < 4u; t++) for (uint32_t i = 0; i < 16; i++) input[t][i] = (int32_t)(1u << (4 * t));
    CHECK(md_mixer_solo(&mixer, 1, 1) == MD_OK && md_mixer_solo(&mixer, 3, 1) == MD_OK);
    CHECK(md_mixer_mute(&mixer, 3, 1) == MD_OK && md_mixer_reset(&mixer) == MD_OK);
    CHECK(!render(&mixer, 8, whole, 1) && output[0] == 16);           /* 3 is muted even though soloed */
    CHECK(md_mixer_solo(&mixer, 1, 0) == MD_OK && md_mixer_solo(&mixer, 3, 0) == MD_OK && md_mixer_reset(&mixer) == MD_OK);
    CHECK(!render(&mixer, 8, whole, 1) && output[0] == 1 + 16 + 256);
    /* tempo reaches assigned and later-assigned effects */
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(md_mixer_set_bus(&mixer, 0, &echo.e) == MD_OK);
    CHECK(md_mixer_tempo(&mixer, 120u * 256u) == MD_OK && echo.s.tempo == 120u * 256u);
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &ins.e) == MD_OK && ins.s.tempo == 120u * 256u);
    CHECK(md_mixer_tempo(&mixer, 39u * 256u) == MD_ERR_RANGE);
    return 0;
}

/* Lanes: tracks added on the second lane (another core's) give the same output, meters and clamp count as on
 * the first, whichever tracks go where, inserts and sends included. */
static int lane_span(md_mixer_t *m, uint32_t start, uint32_t frames, unsigned lane1)
{
    if (md_mixer_begin(m, frames)) return 1;
    for (uint32_t t = 0; t < MD_TRACKS; t++) {
        static int32_t own[MD_TRACKS][2u * MD_MAX_RENDER_FRAMES];
        memcpy(own[t], &input[t][2u * start], 2u * frames * sizeof own[t][0]);
        if (md_mixer_add_track_lane(m, t, own[t], lane1 >> t & 1u)) return 1;
    }
    return md_mixer_finish(m, &output[2u * start]);
}
static int lanes_give_the_same_mix(void)
{
    for (uint32_t t = 0; t < MD_TRACKS; t++)
        for (uint32_t i = 0; i < 2u * FRAMES; i++)                 /* loud enough to clamp now and then */
            input[t][i] = (int32_t)((i * (2654435761u + t * 40503u)) >> 6) - (1 << 25);
    static fx_slot_t ins[2], echo[2], rev[2];
    md_mixer_meters_t want, got;
    for (unsigned lane1 = 0; lane1 < (1u << MD_TRACKS); lane1 += 5u) {
        md_mixer_t *both[2] = {&mixer, &other};
        for (uint32_t k = 0; k < 2; k++) {
            md_mixer_t *m = both[k];
            CHECK(md_mixer_init(m) == MD_OK);
            CHECK(!fx_make(&ins[k], &DELAY_INSERT, 0, 5) && !fx_make(&echo[k], &DELAY_BUS, 0, 33) && !fx_make(&rev[k], &DELAY_BUS, 0, 47));
            CHECK(md_mixer_set_insert(m, 2, 0, &ins[k].e) == MD_OK);
            CHECK(md_mixer_set_bus(m, 0, &echo[k].e) == MD_OK && md_mixer_set_bus(m, 1, &rev[k].e) == MD_OK);
            for (uint32_t t = 0; t < MD_TRACKS; t++) {
                CHECK(md_mixer_track_param(m, t, MD_MIX_LEVEL, 9000 + 5000 * (int32_t)t) == MD_OK);
                CHECK(md_mixer_track_param(m, t, MD_MIX_PAN, -20000 + 10000 * (int32_t)t) == MD_OK);
                CHECK(md_mixer_track_param(m, t, MD_MIX_ECHO_SEND, t & 1u ? 12000 : 0) == MD_OK);
                CHECK(md_mixer_track_param(m, t, MD_MIX_REVERB_SEND, t >= 2u ? 20000 : 0) == MD_OK);
            }
            CHECK(md_mixer_reset(m) == MD_OK);
            memset(output, 0, sizeof output);
            for (uint32_t at = 0, n = 0; at < 1024u; at += n) {
                n = 1u + (at * 37u) % 256u;
                if (n > 1024u - at) n = 1024u - at;
                if (at == 256u) CHECK(md_mixer_track_param(m, 1, MD_MIX_LEVEL, 30000) == MD_OK);   /* a gain on the move */
                CHECK(!lane_span(m, at, n, k ? lane1 : 0u));
            }
            if (!k) { memcpy(reference, output, sizeof reference); md_mixer_meters(m, &want); }
            else md_mixer_meters(m, &got);
        }
        CHECK(!memcmp(reference, output, 2u * 1024u * sizeof *output));
        CHECK(!memcmp(&want, &got, sizeof want) && want.clamped > 0);
    }
    CHECK(md_mixer_begin(&mixer, 16) == MD_OK);
    CHECK(md_mixer_add_track_lane(&mixer, 0, scratch, MD_MIX_LANES) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_finish(&mixer, output) == MD_OK);
    return 0;
}
static int guards(void)
{
    fx_slot_t ins, bus, spare;
    int32_t v = 0;
    CHECK(md_mixer_init(NULL) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_init(&mixer) == MD_OK);
    CHECK(!fx_make(&ins, &SCALE, 256, 0) && !fx_make(&bus, &DELAY_BUS, 0, 1));
    CHECK(md_mixer_track_param(&mixer, MD_TRACKS, MD_MIX_LEVEL, 0) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_track_param(&mixer, 0, 9, 0) == MD_ERR_NOT_FOUND);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_LEVEL, 32768) == MD_ERR_RANGE);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_PAN, -32768) == MD_ERR_RANGE);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_ECHO_SEND, -1) == MD_ERR_RANGE);
    CHECK(md_mixer_track_param(&mixer, 0, MD_MIX_PAN, -5) == MD_OK);
    CHECK(md_mixer_track_param_get(&mixer, 0, MD_MIX_PAN, &v) == MD_OK && v == -5);
    CHECK(md_mixer_track_param_get(&mixer, 0, MD_MIX_LEVEL, &v) == MD_OK && v == 32767);
    CHECK(md_mixer_track_param_get(&mixer, 0, 99, &v) == MD_ERR_NOT_FOUND && v == 32767);
    CHECK(md_mixer_return_level(&mixer, 2, 0) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_return_level(&mixer, 0, 32768) == MD_ERR_RANGE);
    /* capability, uniqueness and initialisation */
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &bus.e) == MD_ERR_UNSUPPORTED);
    CHECK(md_mixer_set_bus(&mixer, 0, &ins.e) == MD_ERR_UNSUPPORTED);
    CHECK(md_mixer_set_master(&mixer, &bus.e) == MD_ERR_UNSUPPORTED);
    CHECK(md_mixer_set_insert(&mixer, 0, 2, &ins.e) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &ins.e) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 0, 0, &ins.e) == MD_OK);           /* same slot again */
    CHECK(md_mixer_set_insert(&mixer, 1, 0, &ins.e) == MD_ERR_ARGUMENT); /* one slot only */
    CHECK(md_mixer_set_master(&mixer, &ins.e) == MD_ERR_ARGUMENT);
    spare.e = ins.e;                                                     /* same state, other handle */
    CHECK(md_mixer_set_insert(&mixer, 1, 1, &spare.e) == MD_ERR_ARGUMENT);
    /* a second instance whose state starts inside the first one's state */
    static _Alignas(16) uint8_t shared[2 * sizeof(fx_t)];
    md_effect_t first, second;
    CHECK(md_effect_init(&first, &SCALE, shared, sizeof(fx_t), MD_SAMPLE_RATE) == MD_OK);
    CHECK(md_effect_init(&second, &SCALE, shared + 16, sizeof(fx_t), MD_SAMPLE_RATE) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 2, 0, &first) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 2, 1, &second) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_set_insert(&mixer, 2, 0, NULL) == MD_OK);
    md_effect_t blank = {0};
    CHECK(md_mixer_set_insert(&mixer, 1, 1, &blank) == MD_ERR_STATE);
    CHECK(md_mixer_set_bus(&mixer, 0, &bus.e) == MD_OK && md_mixer_set_bus(&mixer, 1, &bus.e) == MD_ERR_ARGUMENT);
    /* span discipline */
    CHECK(md_mixer_add_track(&mixer, 0, scratch) == MD_ERR_STATE);
    CHECK(md_mixer_finish(&mixer, output) == MD_ERR_STATE);
    CHECK(md_mixer_begin(&mixer, 0) == MD_ERR_RANGE && md_mixer_begin(&mixer, 257) == MD_ERR_RANGE);
    CHECK(md_mixer_begin(&mixer, 16) == MD_OK && md_mixer_begin(&mixer, 16) == MD_ERR_STATE);
    CHECK(md_mixer_set_insert(&mixer, 2, 0, NULL) == MD_ERR_STATE);
    CHECK(md_mixer_insert_bypass(&mixer, 0, 0, 1) == MD_ERR_STATE);
    CHECK(md_mixer_tempo(&mixer, 120u * 256u) == MD_ERR_STATE);
    CHECK(md_mixer_panic(&mixer) == MD_ERR_STATE && md_mixer_reset(&mixer) == MD_ERR_STATE);
    CHECK(md_mixer_add_track(&mixer, MD_TRACKS, scratch) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_add_track(&mixer, 0, NULL) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_add_track(&mixer, 0, mixer.dry) == MD_ERR_ARGUMENT); /* aliases mixer storage */
    memset(scratch, 0, sizeof scratch);
    CHECK(md_mixer_add_track(&mixer, 0, scratch) == MD_OK);
    CHECK(md_mixer_add_track(&mixer, 0, scratch) == MD_ERR_STATE);     /* once per span */
    CHECK(md_mixer_finish(&mixer, mixer.wet) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_finish(&mixer, NULL) == MD_ERR_ARGUMENT);
    CHECK(md_mixer_finish(&mixer, output) == MD_OK);
    CHECK(md_mixer_set_insert(&mixer, 0, 0, NULL) == MD_OK && md_mixer_set_insert(&mixer, 1, 0, &ins.e) == MD_OK);
    return 0;
}

int main(void)
{
    printf("mixer state %zu bytes\n", sizeof(md_mixer_t));
    RUN(insert_order_and_exact_unity);
    RUN(sends_are_post_fader_and_wet_only);
    RUN(mute_keeps_return_tails);
    RUN(worst_level_cannot_wrap);
    RUN(smoothing_and_partition_independence);
    RUN(bypass_crossfade_and_no_stale_history);
    RUN(panic_unadded_tracks_solo_and_tempo);
    RUN(lanes_give_the_same_mix);
    RUN(guards);
    return 0;
}
