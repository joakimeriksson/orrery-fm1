/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: module/band.c module/seq.c core/session.c core/commands.c core/mixer.c core/effect.c core/router.c core/instrument.c core/transport.c instruments/arp.c instruments/eminent.c instruments/minipops.c engines/arp_engine.c engines/em_engine.c engines/mp_engine.c
/* M3-04 integration: the patterns md_band_build makes, played by the session through the real
 * adapters, against each standalone engine's own sequencer as the reference (PLAN.md: "the standalone
 * engine is the reference"). Events are timed to the sample by proxy instruments that count frames. */
#include "module/band.h"
#include "instruments/electronic_fx.h"
#include "core/session.h"
#include "instruments/arp.h"
#include "instruments/eminent.h"
#include "instruments/minipops.h"
#include "engines/arp_presets.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

/* ---- proxy instruments: forward everything, log note/hit events with their frame ---- */
#define LOG_CAP 40000u
typedef struct { uint32_t frame; uint8_t kind, key; uint16_t flags; md_note_token_t token; } entry_t;
typedef union { md_arp_state_t arp; md_eminent_state_t em; md_minipops_state_t mp; } inner_t;
typedef struct { inner_t inner; uint32_t frame, count; entry_t log[LOG_CAP]; } proxy_t;
static const md_instrument_desc_t *INNER[MD_TRACKS];
static md_instrument_desc_t PROXY[MD_TRACKS];
static int p_init(int t, void *s, uint32_t rate) { proxy_t *p = s; p->frame = p->count = 0; return INNER[t]->init(&p->inner, rate); }
static int p_event(int t, void *s, const md_event_t *e)
{
    proxy_t *p = s;
    int status = INNER[t]->event(&p->inner, e);
    if (!status && (e->kind == MD_EVENT_NOTE_ON || e->kind == MD_EVENT_NOTE_OFF || e->kind == MD_EVENT_HIT) && p->count < LOG_CAP)
        p->log[p->count++] = (entry_t){p->frame, (uint8_t)e->kind,
                                      (uint8_t)(e->kind == MD_EVENT_HIT ? e->data.hit.voice : e->data.note.key), e->flags, e->token};
    return status;
}
static void p_render(int t, void *s, int32_t *o, uint32_t n) { proxy_t *p = s; INNER[t]->render_add(&p->inner, o, n); p->frame += n; }
static int p_get(int t, const void *s, md_param_id_t id, int32_t *v) { return INNER[t]->get_param(&((const proxy_t *)s)->inner, id, v); }
static void p_reset(int t, void *s) { INNER[t]->reset(&((proxy_t *)s)->inner); }
static int p_encode(int t, const void *s, uint8_t *d, uint32_t c) { return INNER[t]->encode(&((const proxy_t *)s)->inner, d, c); }
static int p_apply(int t, void *s, const uint8_t *src, uint32_t n) { return INNER[t]->apply(&((proxy_t *)s)->inner, src, n); }
#define PROXY_FNS(T) \
    static int i##T(void *s, uint32_t r) { return p_init(T, s, r); } \
    static int e##T(void *s, const md_event_t *e) { return p_event(T, s, e); } \
    static void r##T(void *s, int32_t *o, uint32_t n) { p_render(T, s, o, n); } \
    static int g##T(const void *s, md_param_id_t id, int32_t *v) { return p_get(T, s, id, v); } \
    static void z##T(void *s) { p_reset(T, s); } \
    static int c##T(const void *s, uint8_t *d, uint32_t n) { return p_encode(T, s, d, n); } \
    static int a##T(void *s, const uint8_t *src, uint32_t n) { return p_apply(T, s, src, n); }
PROXY_FNS(0) PROXY_FNS(1) PROXY_FNS(2) PROXY_FNS(3)
#define PROXY_DESC(T) do { PROXY[T] = *INNER[T]; PROXY[T].processor.state_bytes = sizeof(proxy_t); \
    PROXY[T].processor.state_alignment = _Alignof(proxy_t); PROXY[T].init = i##T; PROXY[T].event = e##T; \
    PROXY[T].render_add = r##T; PROXY[T].get_param = g##T; PROXY[T].reset = z##T; PROXY[T].encode = c##T; \
    PROXY[T].apply = a##T; } while (0)

static proxy_t prox[MD_TRACKS];
static md_instrument_t inst[MD_TRACKS];
static md_session_t S;
static md_seq_bank_t bank;
static md_band_chain_t chain;
static md_band_report_t report;
static int32_t out[2u * 256u];

static int start(const md_band_settings_t *settings, uint32_t bpm)
{
    INNER[0] = &MD_ARP2600; INNER[1] = &MD_ARP2600; INNER[2] = &MD_EMINENT; INNER[3] = &MD_MINIPOPS;
    PROXY_DESC(0); PROXY_DESC(1); PROXY_DESC(2); PROXY_DESC(3);
    md_session_config_t c = {.bank = &bank, .bpm_q8 = (uint16_t)(bpm * 256u)};
    if (md_band_build(settings, &bank, &chain, &report)) return 1;
    for (uint32_t t = 0; t < 4u; t++) {         /* the band's four parts; tracks 4 and 5 unbound here */
        if (md_instrument_init(&inst[t], &PROXY[t], &prox[t], sizeof prox[t], MD_SAMPLE_RATE)) return 1;
        c.instrument[t] = &inst[t];
    }
    if (md_session_init(&S, &c)) return 1;
    md_command_t go = {.kind = MD_CMD_START};
    return md_session_submit(&S, &go) != MD_OK;
}
static uint32_t refuse_chord_launch;   /* test hook: drop this many chord launches as if the ring refused them */
/* the module's main-loop link scheduling, once per block */
static int run_blocks(uint32_t frames)
{
    for (uint32_t at = 0; at < frames; at += 256) {
        memset(out, 0, sizeof out);
        if (md_session_render(&S, out, 256)) return 1;
        md_session_snapshot_t snap;
        if (md_session_snapshot(&S, &snap)) return 1;
        for (uint8_t t = MD_BAND_BASS; t <= MD_BAND_CHORDS; t += MD_BAND_CHORDS - MD_BAND_BASS) {
            int next = md_band_next_link(&chain, snap.position.quarter / 4u, snap.pattern[t], snap.queued[t], snap.requested[t]);
            if (next < 0) continue;
            md_command_t launch = {.kind = MD_CMD_LAUNCH, .track = t, .arg = (uint8_t)next, .value = 0};
            /* a refused chord launch (ring at its reserve) is retried next block on its own */
            if (t == MD_BAND_CHORDS && refuse_chord_launch) { refuse_chord_launch--; continue; }
            if (md_session_submit(&S, &launch)) return 1;
        }
    }
    return 0;
}
static uint32_t bar_frame(uint32_t bar, uint32_t bpm)   /* ideal sample of a bar line, rounded up */
{
    uint64_t num = (uint64_t)bar * 4u * 60u * MD_SAMPLE_RATE;
    return (uint32_t)((num + bpm - 1u) / bpm);
}

/* ---- bass: the ARP 1601 reference against the session ---- */
typedef struct { uint32_t frame; uint8_t gate, key, accent; } edge_t;
static edge_t ref_edges[LOG_CAP], ses_edges[LOG_CAP];
static int bass_matches_1601(uint32_t s)
{
    const arp_seq_t *q = &ARP_SEQS[s];
    md_band_settings_t set = {.bass_seq = (uint8_t)s, .chord_octave = 1};
    for (uint32_t k = 0; k < MD_BAND_LINKS; k++) set.chord[k] = (md_band_chord_t){(uint8_t)(k * 5u % 12u), MD_BAND_CHORD_MIN};
    CHECK(!start(&set, q->tempo));
    uint32_t bars = 2u * (chain.total_bars ? chain.total_bars : 1u) + 1u, frames = bar_frame(bars, q->tempo);
    frames = (frames + 255u) & ~255u;
    CHECK(!run_blocks(frames));
    /* the reference: the same engine and patch, its own sequencer, sampled every frame */
    static arp_t ref;
    arp_init(&ref);
    arp_set_patch(&ref, &prox[0].inner.arp.engine.patch);
    arp_set_seq(&ref, q);
    arp_run(&ref, 1);
    uint32_t nref = 0, nses = 0;
    int32_t frame_buf[2];
    uint8_t gate = 0, key = 0;
    for (uint32_t f = 0; f < frames && nref < LOG_CAP; f++) {
        frame_buf[0] = frame_buf[1] = 0;
        uint32_t before = ref.steps;
        arp_render(&ref, frame_buf, 1);
        uint8_t g = (uint8_t)ref.gate, k = (uint8_t)(ref.target_q16 >> 16);
        int retrig = ref.steps != before && g && ref.env2.stage == 1 && gate;
        if (g != gate || (g && k != key) || retrig) {
            if (retrig && g == gate && k == key) ref_edges[nref++] = (edge_t){f, 0, key, 0};
            ref_edges[nref++] = (edge_t){f, g, k, (uint8_t)(ref.accent_q16 != 0)};
        }
        gate = g; key = k;
    }
    /* the session's ARP: rebuild the same edges from the logged physical events */
    md_note_token_t current = 0;
    for (uint32_t i = 0; i < prox[0].count; i++) {
        const entry_t *e = &prox[0].log[i];
        if (e->kind == MD_EVENT_NOTE_ON) {
            ses_edges[nses++] = (edge_t){e->frame, 1, e->key, (uint8_t)((e->flags & MD_NOTE_ACCENT) != 0)};
            current = e->token;
        } else if (e->token == current) {
            ses_edges[nses++] = (edge_t){e->frame, 0, 0, 0};
            current = 0;
        }
    }
    uint32_t worst = 0, compared = nref < nses ? nref : nses;
    int ok = nref == nses;
    for (uint32_t i = 0; i < compared; i++) {
        uint32_t d = ref_edges[i].frame > ses_edges[i].frame ? ref_edges[i].frame - ses_edges[i].frame : ses_edges[i].frame - ref_edges[i].frame;
        if (d > worst) worst = d;
        if (ref_edges[i].gate != ses_edges[i].gate || (ref_edges[i].gate && ref_edges[i].key != ses_edges[i].key) ||
            (ref_edges[i].gate && ref_edges[i].accent != ses_edges[i].accent)) {
            if (ok) printf("  first mismatch at edge %u: ref %u/%u/%u at %u, session %u/%u/%u at %u\n", i,
                           ref_edges[i].gate, ref_edges[i].key, ref_edges[i].accent, ref_edges[i].frame,
                           ses_edges[i].gate, ses_edges[i].key, ses_edges[i].accent, ses_edges[i].frame);
            ok = 0;
        }
    }
    printf("  bass %-12s %u links, %2u bars: %3u edges (reference %3u), worst %u samples, %u ties as slides\n",
           q->name, chain.links, bars, nses, nref, worst, report.ties_as_slides);
    CHECK(ok && worst <= 2u && nref > 20u);
    /* the engines' own sequencers stayed off throughout: only the session played them */
    CHECK(!prox[0].inner.arp.engine.running && !prox[0].inner.arp.engine.req_running && !prox[0].inner.arp.engine.steps);
    CHECK(!prox[1].inner.arp.engine.running && !prox[3].inner.mp.engine.playing && !prox[3].inner.mp.engine.req_play);
    /* the chords change on exactly the bar lines where the bass changes link, and only there */
    uint8_t attacked[64] = {0}, lowest[64];
    memset(lowest, 0xff, sizeof lowest);
    for (uint32_t i = 0; i < prox[2].count; i++) {
        const entry_t *e = &prox[2].log[i];
        if (e->kind != MD_EVENT_NOTE_ON) continue;
        uint32_t bar = (uint32_t)(((uint64_t)e->frame * q->tempo) / (4u * 60u * MD_SAMPLE_RATE));
        uint32_t line = bar_frame(bar, q->tempo);
        if (e->frame != line) bar++, line = bar_frame(bar, q->tempo);
        CHECK(e->frame == line);                                  /* on a bar line, to the sample */
        CHECK(chain.links == 1u ? bar % 4u == 0u : bar == 0u || md_band_link_at(&chain, bar) != md_band_link_at(&chain, bar - 1u));
        if (bar < 64u) { attacked[bar] = 1; if (e->key < lowest[bar]) lowest[bar] = e->key; }
    }
    /* ... and on every one of those bar lines: none is skipped or late */
    for (uint32_t bar = 1; bar + 1u < bars && bar < 64u; bar++)
        if (chain.links > 1u && md_band_link_at(&chain, bar) != md_band_link_at(&chain, bar - 1u)) CHECK(attacked[bar]);
    /* ... and every chord attacked is its link's chord: root C2 + 12 (octave 1) + the link's root */
    for (uint32_t bar = 0; bar < bars && bar < 64u; bar++)
        if (attacked[bar]) CHECK(lowest[bar] == 48u + md_band_link_at(&chain, bar) * 5u % 12u);
    return 0;
}
static int bass_and_chords(void)
{
    for (uint32_t s = 0; s < ARP_N_SEQS; s++) if (bass_matches_1601(s)) return 1;
    /* review finding: a refused chord launch must not leave the chords a link behind */
    refuse_chord_launch = 1;
    if (bass_matches_1601(1)) return 1;      /* its chord checks require every change on the right bar */
    CHECK(!refuse_chord_launch);
    return 0;
}

/* ---- drums: the Mini Pops rhythm machine against the drum pattern ---- */
typedef struct { uint32_t frame; uint8_t voice, used; } hit_t;
static hit_t ref_hits[LOG_CAP];
static int drums_match_machine(uint16_t keys, uint8_t section, uint8_t extras, uint32_t bpm, uint32_t *worst_grid)
{
    md_band_settings_t set = {.bass_seq = 0, .rhythm_keys = keys, .rhythm_section = section, .extras = extras};
    CHECK(!start(&set, bpm));
    uint32_t frames = (bar_frame(4, bpm) + 255u) & ~255u;
    CHECK(!run_blocks(frames));
    static mp_t ref;
    mp_init(&ref);
    mp_set_keys(&ref, keys, section);
    mp_set_tempo(&ref, bpm);
    mp_set_level(&ref, 80, 50);
    mp_set_extra(&ref, extras & 4u ? 70 : 0, extras & 2u ? 70 : 0, extras & 1u ? 70 : 0);
    mp_play(&ref, 1);
    uint32_t nref = 0;
    int32_t frame_buf[2];
    (void)mp_take_hits(&ref);
    for (uint32_t f = 0; f < frames && nref < LOG_CAP; f++) {
        frame_buf[0] = frame_buf[1] = 0;
        mp_render(&ref, frame_buf, 1);
        uint32_t mask = mp_take_hits(&ref);
        for (uint32_t v = 0; v < MP_VOICES; v++) if (mask >> v & 1u) ref_hits[nref++] = (hit_t){f, (uint8_t)v, 0};
    }
    /* each session hit pairs with the nearest unused reference hit of its voice */
    uint32_t allowed = (uint32_t)(((uint64_t)report.drum_error_permille * 60u * MD_SAMPLE_RATE) / (1000u * bpm)) + 3u;
    uint32_t hits = 0, unmatched = 0;
    for (uint32_t i = 0; i < prox[3].count; i++) {
        const entry_t *e = &prox[3].log[i];
        if (e->kind != MD_EVENT_HIT) continue;
        hits++;
        uint32_t best = 0xffffffffu, at = 0;
        for (uint32_t j = 0; j < nref; j++) {
            if (ref_hits[j].used || ref_hits[j].voice != e->key) continue;
            uint32_t d = ref_hits[j].frame > e->frame ? ref_hits[j].frame - e->frame : e->frame - ref_hits[j].frame;
            if (d < best) { best = d; at = j; }
        }
        if (best > allowed) { unmatched++; continue; }
        ref_hits[at].used = 1;
        if (best <= 3u && best > *worst_grid) *worst_grid = best;
    }
    uint32_t missing = 0;
    for (uint32_t j = 0; j < nref; j++) missing += !ref_hits[j].used;
    if (unmatched || missing > (uint32_t)report.drum_merged * 4u)
        printf("  drums keys %03x/%u: %u hits, reference %u, %u unmatched, %u missing, %u merged a pass\n",
               keys, section, hits, nref, unmatched, missing, report.drum_merged);
    CHECK(hits > 0 && !unmatched && missing <= (uint32_t)report.drum_merged * 4u);
    return 0;
}
static int drums(void)
{
    uint32_t worst = 0, moved_rhythms = 0, max_err = 0;
    for (uint8_t section = 0; section < 2; section++)
        for (uint32_t key = 0; key < MP_KEYS; key++) {
            if (drums_match_machine((uint16_t)(1u << key), section, 7, 120, &worst)) return 1;
            if (report.drum_moved) {
                moved_rhythms++;
                if (report.drum_error_permille > max_err) max_err = report.drum_error_permille;
                printf("  rhythm %-12s: %u of %u hits moved to the grid, at most %u/1000 of a beat\n",
                       mp_rhythm(section, key)->name, report.drum_moved, report.drum_hits, report.drum_error_permille);
            }
        }
    if (drums_match_machine(0x41, 0, 0, 132, &worst)) return 1;   /* two keys combined, no extras */
    printf("  drums: 20 rhythms + a combination match the machine; on-grid hits within %u samples; %u rhythms quantised\n",
           worst, moved_rhythms);
    CHECK(worst <= 2u);
    return 0;
}

static int chain_and_settings(void)
{
    md_band_settings_t set = {0};
    for (uint32_t s = 0; s < ARP_N_SEQS; s++) {
        set.bass_seq = (uint8_t)s;
        CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
        const arp_seq_t *q = &ARP_SEQS[s];
        CHECK(chain.links == (q->n_chain ? q->n_chain : 1u) && !report.links_dropped);
        uint32_t total = 0;
        for (uint32_t k = 0; k < chain.links; k++) total += chain.bars[k];
        CHECK(total == chain.total_bars);
        for (uint32_t bar = 0, k = 0, left = chain.bars[0]; bar < 3u * total; bar++) {
            CHECK(md_band_link_at(&chain, bar) == k);
            if (--left == 0) { k = (k + 1u) % chain.links; left = chain.bars[k]; }
        }
    }
    /* scheduling: nothing to queue mid-link; the next link queued in the bar before it; not twice */
    md_band_chain_t c = {.links = 3, .bars = {2, 1, 1}, .total_bars = 4};
    CHECK(md_band_next_link(&c, 0, 0, 0, 0) == -1);
    CHECK(md_band_next_link(&c, 1, 0, 0, 0) == 1);
    CHECK(md_band_next_link(&c, 1, 0, 1, 1) == -1);
    CHECK(md_band_next_link(&c, 2, 1, 0, 0) == 2);
    CHECK(md_band_next_link(&c, 3, 2, 0, 0) == 0);
    CHECK(md_band_next_link(&c, 3, 2, 1, 1) == -1);      /* a pending launch is never replaced */
    CHECK(md_band_next_link(&c, 2, 1, 1, 2) == -1);      /* (bar line on a block edge: it is about to land) */
    /* chords: kinds map, NONE is a rest, octave bounds */
    set.bass_seq = 1; set.chord_octave = 9;
    set.chord[0] = (md_band_chord_t){11, MD_BAND_CHORD_MAJ7};
    set.chord[1] = (md_band_chord_t){0, MD_BAND_CHORD_NONE};
    CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
    CHECK(bank.pattern[MD_BAND_CHORDS][0].step[0].payload == ((36u + 48u + 11u) | (MD_CHORD_MAJOR7 << 8)));
    CHECK(bank.pattern[MD_BAND_CHORDS][0].step[1].flags == MD_STEP_TIE);   /* a two-bar link holds */
    CHECK(bank.pattern[MD_BAND_CHORDS][1].step[0].gate == 0);
    CHECK(bank.pattern[MD_BAND_LEAD][0].step[0].gate == 0);
    return 0;
}

/* the default AMBIENT progression: Cm, Abmaj7, Fm7, Eb | Cm, Abmaj7, Bbsus2, Gsus4 */
static const md_band_chord_t AMB_DRIFT[8] = {{0, MD_BAND_CHORD_MIN}, {8, MD_BAND_CHORD_MAJ7}, {5, MD_BAND_CHORD_MIN7},
    {3, MD_BAND_CHORD_MAJ}, {0, MD_BAND_CHORD_MIN}, {8, MD_BAND_CHORD_MAJ7}, {10, MD_BAND_CHORD_SUS2}, {7, MD_BAND_CHORD_SUS4}};
/* AMBIENT's chords are the settings': roots F2..E3 at octave 1, the bass an octave under, a NONE a rest, an
 * octave more twelve up; no rhythm keys, no drums */
static int ambient_chords(void)
{
    static const uint8_t ROOT[8] = {48, 44, 41, 51, 48, 44, 46, 43};
    md_band_settings_t set = {.scene = MD_BAND_SCENE_AMBIENT, .amb_arp = 1, .chord_octave = 1};
    for (uint32_t k = 0; k < 8; k++) set.chord[k] = AMB_DRIFT[k];
    set.chord[3].kind = MD_BAND_CHORD_NONE;
    CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
    for (uint32_t i = 0; i < 8; i++) {
        const md_step_t *ch = &bank.pattern[MD_BAND_CHORDS][i / 4u].step[(i % 4u) * 2u];
        const md_step_t *bs = &bank.pattern[MD_BAND_BASS][i / 4u].step[(i % 4u) * 2u];
        if (i == 3) { CHECK(!ch->gate && !bs->gate && !bank.pattern[MD_BAND_FX][0].step[3u * 16u + 8u].gate); continue; }
        CHECK(ch->gate && (ch->payload & 0xffu) == ROOT[i] && bs->payload == ROOT[i] - 12u);
    }
    for (uint32_t k = 0; k < 64; k++) CHECK(!bank.pattern[MD_BAND_DRUMS][0].step[k].gate);   /* no keys: no drums */
    set.chord_octave = 2;
    CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
    CHECK((bank.pattern[MD_BAND_CHORDS][0].step[0].payload & 0xffu) == 60u);
    return 0;
}
/* the effect sequence: twelve droplets and two chirps over sixteen bars, as the module's DROPLETS pattern */
static const md_band_fx_event_t FX_TEST[MD_BAND_FX_EVENTS] = {
    {6, 1, 11, 70, 0}, {12, 1, 13, 70, 0}, {20, 1, 10, 70, 0}, {26, 1, 12, 70, 0}, {42, 1, 10, 70, 0}, {48, 1, 14, 70, 0}, {54, 1, 11, 70, 0},
    {68, 1, 12, 70, 0}, {82, 1, 9, 70, 0}, {88, 1, 11, 70, 0}, {94, 1, 13, 70, 0}, {110, 1, 10, 70, 0}, {60, 4, 10, 100, 0}, {124, 4, 12, 100, 0},
    {6, 5, 0, 100, 0} /* a second event on eighth 6: the first plays */, {0, 0, 0, 0, 0}};
/* AMBIENT: the effects box on one track: the run (base sound) and the effect sequence (a kit sound locked a step) */
static int ambient_combinations(void)
{
    for (uint32_t arp = 0; arp < 2; arp++) {
        md_band_settings_t set = {.scene = MD_BAND_SCENE_AMBIENT, .amb_arp = (uint8_t)arp, .chord_octave = 1,
                                  .rhythm_keys = 1u << 5};
        for (uint32_t k = 0; k < 8; k++) set.chord[k] = AMB_DRIFT[k];
        memcpy(set.fx, FX_TEST, sizeof set.fx);
        CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
        CHECK(chain.links == 2 && chain.bars[0] == 8 && chain.bars[1] == 8 && chain.total_bars == 16);
        CHECK(md_band_link_at(&chain, 7) == 0 && md_band_link_at(&chain, 8) == 1 && md_band_link_at(&chain, 16) == 0);
        CHECK(bank.pattern[MD_BAND_BASS][0].step[0].gate != 0);                  /* the soft bass */
        CHECK(bank.pattern[MD_BAND_DRUMS][0].meta.mode == MD_PATTERN_DRUM);     /* soft drums */
        for (uint32_t k = 0; k < 64; k++) CHECK(!bank.pattern[MD_BAND_LEAD][0].step[k].gate);   /* the player's */
        uint32_t drops = 0, chirps = 0, run = 0;
        for (uint32_t i = 0; i < bank.lock_count; i++) {
            const md_lock_t *l = &bank.locks[i];
            const md_step_t *st = &bank.pattern[MD_BAND_FX][(l->step / 64u) % 4u].step[l->step % 64u];
            CHECK(l->step / 256u == MD_BAND_FX && (l->step / 64u) % 4u < 2u && l->target == MD_FX_KIT);
            CHECK(!i || bank.locks[i - 1].step < l->step);                     /* one a step, in order */
            CHECK(st->gate && st->payload % 12u != 1u && st->payload % 12u != 4u);   /* on a note, in C minor */
            drops += l->value == 1 + MD_FX_DROP; chirps += l->value == 1 + MD_FX_CHIRP;
        }
        for (uint32_t p = 0; p < 2; p++)
            for (uint32_t k = 0; k < 64; k++) {
                const md_step_t *st = &bank.pattern[MD_BAND_FX][p].step[k];
                uint32_t locked = 0;
                for (uint32_t i = 0; i < bank.lock_count; i++) locked |= bank.locks[i].step == (MD_BAND_FX * 4u + p) * 64u + k;
                run += st->gate && !locked;
            }
        printf("  run %u: %u run notes, %u droplets, %u chirps, %u locks\n", arp, run, drops, chirps, bank.lock_count);
        CHECK(chirps == 2 && drops == 12 && bank.lock_count == 14u);          /* every event once; the double not */
        CHECK((run > 50) == (arp != 0) && (arp || run == 0));
        /* events where they were put: eighth 6 of the first half the first droplet (not the METAL after it), eighth
         * 124 the second half's chirp at 60; an event on a run step (12: bar 2 of the first chord) has the step */
        CHECK(bank.pattern[MD_BAND_FX][0].step[6].payload == 87u && bank.pattern[MD_BAND_FX][0].step[6].probability == 70);
        CHECK(bank.pattern[MD_BAND_FX][1].step[60].payload == 89u && bank.pattern[MD_BAND_FX][1].step[60].probability == 100);
        CHECK(bank.pattern[MD_BAND_FX][0].step[12].payload == 91u);
        if (arp)
            for (uint32_t k = 9; k < 12; k++)
                CHECK(bank.pattern[MD_BAND_FX][0].step[k].payload > bank.pattern[MD_BAND_FX][0].step[k - 1].payload);
    }
    /* the keys follow the home chord: A minor's pentatonic from A4, F major's from F4, C minor's without a chord */
    md_band_settings_t band = {0};
    CHECK(md_band_fx_key(&band, 0) == 60u && md_band_fx_key(&band, 1) == 63u && md_band_fx_key(&band, 14) == 94u);
    CHECK(md_band_fx_key(&band, 99) == 94u && md_band_fx_key(0, 5) == 72u);
    band.chord[1] = (md_band_chord_t){5, MD_BAND_CHORD_MAJ7};                /* (chord 0 a rest: the next is home) */
    CHECK(md_band_fx_key(&band, 0) == 65u && md_band_fx_key(&band, 2) == 69u && md_band_fx_key(&band, 5) == 77u);
    /* BAND leaves the effects track silent without events; with them one eight-bar pattern, the second half of the
     * sequence folded onto the first (at % 64), in the home key */
    band = (md_band_settings_t){0};
    CHECK(md_band_build(&band, &bank, &chain, &report) == MD_OK && !bank.lock_count);
    for (uint32_t k = 0; k < 64; k++) CHECK(!bank.pattern[MD_BAND_FX][0].step[k].gate);
    band.chord[0] = (md_band_chord_t){9, MD_BAND_CHORD_MIN};
    memcpy(band.fx, FX_TEST, sizeof band.fx);
    CHECK(md_band_build(&band, &bank, &chain, &report) == MD_OK);
    CHECK(bank.lock_count == 13u && bank.pattern[MD_BAND_FX][0].step[60].gate && bank.pattern[MD_BAND_FX][0].step[6].gate);
    CHECK(bank.pattern[MD_BAND_FX][0].step[4].gate && bank.pattern[MD_BAND_FX][0].step[4].payload == md_band_fx_key(&band, 12));   /* 68 % 64 */
    CHECK((bank.pattern[MD_BAND_FX][0].step[6].payload - 9u) % 12u == 3u);   /* A minor's third */
    for (uint32_t i = 1; i < bank.lock_count; i++) CHECK(bank.locks[i - 1].step < bank.locks[i].step);
    for (uint32_t k = 0; k < 64; k++) CHECK(!bank.pattern[MD_BAND_FX][1].step[k].gate);
    /* every recipe of the box is a sound of the sequence: its lock is the recipe's kit number, at a velocity of its
     * own (the long tones well under the struck ones); a sound past the last is left out */
    memset(band.fx, 0, sizeof band.fx);
    for (uint32_t k = 0; k < MD_BAND_FX_EVENTS; k++) band.fx[k] = (md_band_fx_event_t){(uint8_t)(4u * k), (uint8_t)(k + 1u), 5, 100, MD_BAND_FX_ONCE};
    CHECK(md_band_build(&band, &bank, &chain, &report) == MD_OK && bank.lock_count == MD_FX_RECIPES && MD_FX_RECIPES == 13);
    for (uint32_t k = 0; k < MD_FX_RECIPES; k++) {
        CHECK(bank.locks[k].value == (int16_t)(k + 1u) && bank.locks[k].target == MD_FX_KIT);
        CHECK(bank.pattern[MD_BAND_FX][0].step[4u * k].velocity == md_band_fx_velocity(k + 1u) && md_band_fx_velocity(k + 1u));
    }
    CHECK(md_band_fx_velocity(1 + MD_FX_WHISTLE) < md_band_fx_velocity(1 + MD_FX_DROP) && !md_band_fx_velocity(0) && !md_band_fx_velocity(14));
    CHECK(!bank.pattern[MD_BAND_FX][0].step[4u * 13u].gate);
    /* an event played as a gesture (how: UP ..) is the module's to play: no step, no lock */
    band.fx[0].how = MD_BAND_FX_UP; band.fx[3].how = MD_BAND_FX_SPARKLE;
    CHECK(md_band_build(&band, &bank, &chain, &report) == MD_OK && bank.lock_count == MD_FX_RECIPES - 2u);
    CHECK(!bank.pattern[MD_BAND_FX][0].step[0].gate && !bank.pattern[MD_BAND_FX][0].step[12].gate && bank.pattern[MD_BAND_FX][0].step[4].gate);
    return 0;
}
/* AMBIENT's chords are voiced in one octave over the lowest root (F3 .. E4 at octave 1), so the tones two chords
 * share stay where they are; a bare fifth is left as it is; the lead's phrases play the chord's tones only */
static int voicings_and_phrases(void)
{
    static const uint8_t TONES[MD_BAND_CHORD_KINDS][4] = {{0}, {0, 3, 7, 12}, {0, 4, 7, 12}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 2, 7, 12},
                                                         {0, 5, 7, 12}, {0, 7, 12, 19}};
    md_band_settings_t set = {.scene = MD_BAND_SCENE_AMBIENT, .chord_octave = 1};
    for (uint32_t k = 0; k < 8; k++) set.chord[k] = AMB_DRIFT[k];
    set.chord[6] = (md_band_chord_t){9, MD_BAND_CHORD_5TH};
    CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
    uint32_t held = 0;
    for (uint32_t i = 0; i < 8; i++) {
        uint16_t v = bank.pattern[MD_BAND_CHORDS][i / 4u].step[(i % 4u) * 2u].payload;
        uint32_t root = v & 0xffu, now = 0;
        for (uint32_t t = 1; t < 4u; t++) {
            uint32_t key = root + TONES[set.chord[i].kind][t] + (v & MD_CHORD_RAISE(t) ? 12u : 0u);
            if (set.chord[i].kind != MD_BAND_CHORD_5TH) { CHECK(key >= 53u && key < 65u); now |= 1u << (key - 53u); }
            else CHECK(!(v & MD_CHORD_RAISE(t)));
        }
        if (i == 1) CHECK((now & held) == ((1u << (55 - 53)) | (1u << (60 - 53)) | (1u << (63 - 53))));   /* Cm -> Abmaj7: G3 C4 Eb4 stay */
        held = now;
    }
    CHECK((bank.pattern[MD_BAND_CHORDS][0].step[0].payload & (MD_CHORD_RAISE(1) | MD_CHORD_RAISE(2) | MD_CHORD_RAISE(3))) == MD_CHORD_RAISE(1));
    for (uint32_t style = 0; style <= MD_BAND_PHRASES; style++) {
        uint32_t notes = 0, rests = 0;
        set.phrase = (uint8_t)style;
        CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
        for (uint32_t p = 0; p < 2u; p++)
            for (uint32_t k = 0; k < 64u; k++) {
                const md_step_t *t = &bank.pattern[MD_BAND_LEAD][p].step[k];
                const md_band_chord_t *c = &set.chord[p * 4u + k / 16u];
                uint32_t pc, ok = 0;
                if (!t->gate) { rests += k / 16u == 1u; continue; }
                notes++;
                pc = (t->payload + 120u - c->root) % 12u;
                for (uint32_t i = 0; i < 4u; i++) ok |= TONES[c->kind][i] % 12u == pc;
                CHECK(ok && t->payload >= 53u && t->payload <= 108u);
            }
        CHECK(style ? notes > 12u : !notes);
        if (style == 4u) CHECK(rests == 32u);        /* CALLS: the second chord of each half is quiet */
        printf("  phrase %u: %u steps sound\n", style, notes);
    }
    return 0;
}
/* the player's sequences as the lead's patterns, and the song that chains them */
static int own_sequences(void)
{
    md_band_settings_t set = {.bass_seq = 1, .chord_octave = 1, .song_on = 1, .seq_div = {0, 1, 2, 0}};
    static const uint8_t A[16] = {60, 60 | MD_BAND_SEQ_TIE, 64 | MD_BAND_SEQ_TIE, 0, 67 | MD_BAND_SEQ_TIE, 0, 0, 0, 72, 0, 0, 0, 0, 0, 0, 55};
    for (uint32_t k = 0; k < 16; k++) { set.seq[0][k] = A[k]; set.seq[1][k] = (uint8_t)(48u + k); }
    for (uint32_t scene = 0; scene < 2u; scene++) {   /* BAND, where the lead is empty, and AMBIENT over its phrase */
        set.scene = (uint8_t)scene; set.phrase = 1;
        for (uint32_t k = 0; k < 8; k++) set.chord[k] = AMB_DRIFT[k];
        CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
        const md_seq_pattern_t *a = &bank.pattern[MD_BAND_LEAD][0], *b = &bank.pattern[MD_BAND_LEAD][1], *c = &bank.pattern[MD_BAND_LEAD][2];
        CHECK(a->meta.length == 16 && a->meta.division == MD_DIV_16TH && a->meta.mode == MD_PATTERN_MONO);
        CHECK(b->meta.division == MD_DIV_8TH && c->meta.division == MD_DIV_QUARTER);
        CHECK(a->step[0].payload == 60 && a->step[0].gate && !(a->step[0].flags & (MD_STEP_TIE | MD_STEP_SLIDE)));   /* (55 before it, not tied) */
        CHECK(a->step[1].payload == 60 && (a->step[1].flags & MD_STEP_TIE));          /* the same key: held on */
        CHECK(a->step[2].payload == 64 && (a->step[2].flags & MD_STEP_SLIDE) && !(a->step[2].flags & MD_STEP_TIE));   /* another: slid to */
        CHECK(!a->step[3].gate);                                                     /* a rest */
        CHECK(a->step[4].payload == 67 && a->step[4].gate && !(a->step[4].flags & (MD_STEP_TIE | MD_STEP_SLIDE)));   /* tied to a rest: a note */
        CHECK(a->step[8].payload == 72 && a->step[15].payload == 55 && !a->step[9].gate);
        for (uint32_t k = 0; k < 16; k++) CHECK(b->step[k].payload == 48u + k && b->step[k].velocity == 100 && !c->step[k].gate);
    }
    set.song_on = 0; set.scene = MD_BAND_SCENE_BAND;                                  /* off: the lead is the player's alone */
    CHECK(md_band_build(&set, &bank, &chain, &report) == MD_OK);
    for (uint32_t k = 0; k < 16; k++) CHECK(!bank.pattern[MD_BAND_LEAD][0].step[k].gate);
    /* the song: A (a bar), B (two), A, C (four): eight bars, round */
    static const uint8_t SONG[MD_BAND_SONG_SLOTS] = {1, 2, 1, 3}, NONE[MD_BAND_SONG_SLOTS] = {0, 2, 1}, DIV[4] = {0, 1, 2, 0};
    static const int8_t WANT[8] = {0, 1, 1, 0, 2, 2, 2, 2};
    static const uint8_t SLOT[8] = {0, 1, 1, 2, 3, 3, 3, 3}, INTO[8] = {0, 0, 1, 0, 0, 1, 2, 3};
    CHECK(md_band_seq_bars(0) == 1 && md_band_seq_bars(1) == 2 && md_band_seq_bars(2) == 4);
    CHECK(md_band_song_at(NONE, DIV, 0, 0, 0) == -1 && md_band_song_next(NONE, DIV, 0, 0, 0) == -1);   /* nothing before the first end */
    for (uint32_t bar = 0; bar < 40; bar++) {
        uint32_t slot = 99, into = 99;
        CHECK(md_band_song_at(SONG, DIV, bar, &slot, &into) == WANT[bar % 8u] && slot == SLOT[bar % 8u] && into == INTO[bar % 8u]);
    }
    /* scheduled as the module does, a launch landing on the next bar line: every bar plays what the song says */
    uint8_t playing = (uint8_t)(SONG[0] - 1u);
    uint32_t launches = 0;
    for (uint32_t bar = 0; bar < 40; bar++) {
        CHECK(playing == (uint8_t)WANT[bar % 8u]);
        CHECK(md_band_song_next(SONG, DIV, bar, playing, 1) == -1);                   /* a launch pending: never another */
        int next = md_band_song_next(SONG, DIV, bar, playing, 0);
        if (next >= 0) { playing = (uint8_t)next; launches++; }
    }
    CHECK(launches == 20);                                                           /* four changes a round, five rounds */
    static const uint8_t ONE[MD_BAND_SONG_SLOTS] = {2};                               /* one sequence: it loops by itself */
    for (uint32_t bar = 0; bar < 8; bar++) CHECK(md_band_song_next(ONE, DIV, bar, 1, 0) == -1);
    return 0;
}
int main(void)
{
    RUN(chain_and_settings);
    RUN(ambient_combinations);
    RUN(ambient_chords);
    RUN(voicings_and_phrases);
    RUN(own_sequences);
    RUN(bass_and_chords);
    RUN(drums);
    return 0;
}
