/* SPDX-License-Identifier: GPL-3.0-only */
/* SEVENTIES: the seventies' synths of a Jarre record in one fm1-dock module, so they play together: the Eminent 310
 * strings through the Small Stone (em_engine.c) as the bed, an ARP 2600 (arp_engine.c) as the lead on the keys,
 * a second 2600 with its 1601 as the bass with its sequence and pattern, the Korg Mini Pops 7 (mp_engine.c) as
 * the drums, a tape echo (hb_echo.c) with a send from the strings and the lead (the bass and the drums dry, as
 * on the records), and a chord sequencer: a chord a link of the bass pattern, so the strings move with the bass.
 * The dock's reverb takes the whole. The whole panel and screen are the module's (HOME held 1.5 s: the list).
 *
 *   EDIT          STRINGS page:  KNOB1..4 attack, release, chorus, phaser; ALGORITHM the registration; FX the colour
 *   GLO           LEAD page:     ALGORITHM the patch (the 2600's ten); KNOB1..4 cutoff, resonance, glide, level
 *   ENV           BASS page:     ALGORITHM the patch, PRESETS the sequence (eight, with their patterns); KNOB1..4
 *                                cutoff, sweep, tempo, level
 *   LFO           DRUMS page:    the white keys from F3 are the rhythm keys (together they combine), the black ones
 *                                F#3 G#3 A#3 the tambourine, guiro, quijada; OCT- / OCT+ the lever; ALGORITHM one
 *                                rhythm; KNOB1..4 tempo, level, balance, the extras' level
 *   SEQ           CHORDS page:   SELECT the link, KNOB1 its root, KNOB2 its kind (-, MIN, MAJ, MIN7, MAJ7, SUS2,
 *                                SUS4, 5TH), KNOB3 the strings' octave, KNOB4 the strings' level. REC: LATCH, the
 *                                keys set the chord instead of playing the lead (the link under the cursor)
 *   FX            MIX page, FX again ECHO page: KNOB1..4 the four levels; ALGORITHM the reverb; on ECHO: ALGORITHM
 *                                the time (OFF, 1/16 .. 1/2 of the tempo), KNOB1..4 feedback, tone, the strings'
 *                                send, the lead's send
 *   PLAY          the band: the bass and the drums start together, and stop; the chords follow the bass's pattern
 *   ARP           all notes off; SAVE: the settings now (else two seconds after a change)
 *   keys          the lead (OCT- / OCT+ its octave); MIDI notes the lead too, PC its patch, CC 74 its cutoff */
#include "hb_module.h"
#include "arp_engine.h"
#include "arp_presets.h"
#include "em_engine.h"
#include "mp_engine.h"
#include "hb_echo.c"

#define SET_MAGIC 0x31305337u                   /* "7S01" */
#define C_ACC HB_RGB(255, 150, 60)              /* orange, as the decade's panels */
#define C_CREAM HB_RGB(235, 205, 130)
#define C_BLUE HB_RGB(90, 120, 170)
#define C_WHITE 0xFFFFu
#define C_GREY HB_RGB(110, 110, 110)
#define C_DIM HB_RGB(50, 46, 40)
#define C_BLACK 0u
#define MUL(a, b, s) ((int32_t)(((int64_t)(a) * (b)) >> (s)))

enum { PG_STRINGS, PG_LEAD, PG_BASS, PG_DRUMS, PG_CHORDS, PG_MIX, PG_ECHO, N_PAGES };
enum { CH_NONE, CH_MIN, CH_MAJ, CH_MIN7, CH_MAJ7, CH_SUS2, CH_SUS4, CH_5TH, N_KINDS };
static const char *const PAGE_NAME[N_PAGES] = {"STRINGS", "LEAD", "BASS", "DRUMS", "CHORDS", "MIX", "ECHO"};
static const char *const KIND_NAME[N_KINDS] = {"-", "MIN", "MAJ", "MIN7", "MAJ7", "SUS2", "SUS4", "5TH"};
static const uint8_t KIND_IV[N_KINDS][4] = {{0, 0, 0, 0}, {0, 3, 7, 12}, {0, 4, 7, 12}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 2, 7, 12}, {0, 5, 7, 12}, {0, 7, 12, 19}};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const EC_DIV_NAME[7] = {"OFF", "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2"};
static const char *const REG_NAME[EM_N_REG] = {"STRINGS", "CELLO", "VIOLIN", "FULL"};
static const int8_t WHITE[27] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9,
                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

typedef struct { uint8_t root, kind; } chord_t;
typedef struct {                                /* the settings: the module's record (small; the patches are the presets') */
    uint32_t magic;
    uint8_t page, oct, latch, cursor;
    uint8_t lead_patch, bass_patch, bass_seq;
    uint8_t str_reg, str_att, str_rel, str_ens, str_ph, str_col, str_oct;
    int8_t lead_cut, bass_cut;
    uint8_t lead_res, lead_glide, bass_sweep;   /* (255: the patch's / the sequence's own) */
    uint16_t tempo, rh_keys;
    uint8_t rh_section, rh_xmask, rh_extra, rh_balance;
    uint8_t lvl[4];                             /* strings, lead, bass, drums: percent */
    uint8_t reverb, ec_div, ec_fb, ec_tone, ec_str, ec_lead;
    chord_t chord[ARP_CHAIN];
} set_t;

static const hb_host_t *H;
static em_t strings;
static arp_t lead, bass;
static mp_t drums;
static set_t set;
static uint32_t dirty, t_save, drawn = ~0u, drawn_page = ~0u, drawn_live = ~0u, running, want_run;
static int32_t cur_link = -1;                   /* the pattern's link the chord plays for */
static uint8_t chord_on[4], n_chord_on;         /* the strings' notes sounding */
static int32_t tmp[4][2 * 256];                 /* the parts' blocks: strings, lead, bass, drums */
static int32_t ec_src[256];

static char *put_u(char *p, uint32_t v)
{
    if (v >= 100u) *p++ = (char)('0' + v / 100u % 10u);
    if (v >= 10u) *p++ = (char)('0' + v / 10u % 10u);
    *p++ = (char)('0' + v % 10u);
    *p = 0;
    return p;
}
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_i(char *p, int32_t v) { if (v < 0) { *p++ = '-'; v = -v; } else if (v > 0) *p++ = '+'; return put_u(p, (uint32_t)v); }
static uint8_t clamp8(int32_t v, int32_t lo, int32_t hi) { return (uint8_t)(v < lo ? lo : v > hi ? hi : v); }

/* ---- the parts as set */
static void touch(void) { dirty = 1; t_save = H->ms(); }
static void strings_apply(void)
{
    em_patch_t p = strings.p;
    p.reg = set.str_reg; p.attack = set.str_att; p.release = set.str_rel; p.ensemble = set.str_ens; p.phaser = set.str_ph;
    p.color = set.str_col; p.volume = 100;
    em_set_patch(&strings, &p);
}
static void lead_apply(void)
{
    arp_patch_t p = ARP_PATCHES[set.lead_patch % ARP_N_PATCHES];
    p.cutoff = clamp8((int32_t)p.cutoff + set.lead_cut, 0, 100);
    if (set.lead_res != 255u) p.reso = set.lead_res;
    if (set.lead_glide != 255u) p.glide = set.lead_glide;
    p.volume = 100;
    arp_set_patch(&lead, &p);
}
static void bass_apply(void)
{
    arp_patch_t p = ARP_PATCHES[set.bass_patch % ARP_N_PATCHES];
    arp_seq_t q = ARP_SEQS[set.bass_seq % ARP_N_SEQS];
    p.cutoff = clamp8((int32_t)p.cutoff + set.bass_cut, 0, 100);
    p.volume = 100;
    if (set.bass_sweep != 255u) q.sweep = set.bass_sweep;
    arp_set_patch(&bass, &p);
    arp_set_seq(&bass, &q);
    arp_set_tempo(&bass, set.tempo);
}
static void drums_apply(void)
{
    mp_set_keys(&drums, set.rh_keys, set.rh_section);
    mp_set_tempo(&drums, set.tempo);
    mp_set_level(&drums, 100, set.rh_balance);
    mp_set_extra(&drums, set.rh_xmask & 4u ? set.rh_extra : 0, set.rh_xmask & 2u ? set.rh_extra : 0, set.rh_xmask & 1u ? set.rh_extra : 0);
}
static void echo_apply(void)
{
    uint32_t on = set.ec_div && set.ec_div < 7u && (set.ec_str || set.ec_lead);
    if (on) {
        if (!ec_on) ec_clear();
        ec_prep(ec_div_ms(set.ec_div, set.tempo), set.ec_fb, set.ec_tone);
    }
    ec_on = on;
}
static void reverb_out(void)
{
    static uint32_t sent = ~0u;
    if (H->size <= __builtin_offsetof(hb_host_t, fx_reverb) || set.reverb == sent) return;
    sent = set.reverb;
    H->fx_reverb(set.reverb > 0, 2, 45875, 32768, set.reverb * 655);   /* HALL, a large room */
}
static void tempo_apply(void) { bass_apply(); drums_apply(); echo_apply(); }
static void band(uint32_t on)                   /* the bass and the drums together, from the same block: in time by construction */
{
    want_run = on != 0;
    arp_run(&bass, on);
    mp_play(&drums, on);
    running = want_run;
}

/* ---- the chords: the strings' notes for a link */
static void chord_stop(void)
{
    uint32_t i;
    for (i = 0; i < n_chord_on; i++) em_note_off(&strings, chord_on[i]);
    n_chord_on = 0;
}
static void chord_play(uint32_t link)
{
    const chord_t *c = &set.chord[link % ARP_CHAIN];
    uint32_t i;
    chord_stop();
    if (c->kind == CH_NONE || c->kind >= N_KINDS) return;
    for (i = 0; i < 4u; i++) {
        uint32_t n = 36u + 12u * set.str_oct + c->root + KIND_IV[c->kind][i];
        if (n > 108u) n = 108u;
        chord_on[n_chord_on++] = (uint8_t)n;
        em_note_on(&strings, n, 100);
    }
}
static void chords_follow(void)                 /* main loop: the bass's link changed, or the band started / stopped */
{
    int32_t link = running ? arp_link(&bass) : set.latch ? (int32_t)set.cursor : -1;   /* (stopped, in LATCH: the cursor's) */
    if (running && link < 0) link = 0;          /* (a sequence without a pattern: the first chord throughout) */
    if (link == cur_link) return;
    cur_link = link;
    if (link < 0) chord_stop();
    else chord_play((uint32_t)link);
}

/* ---- the screen */
static void draw_knobs(const char *const name[4], const char *const val[4], const uint8_t bar[4], const uint8_t lit[4])
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        int32_t x = 4 + (int32_t)k * 59;
        H->fill(x, 56, 56, 72, C_BLACK);
        H->text(x, 56, 56, 0, name[k], C_GREY, 1);
        H->text(x, 78, 56, 0, val[k], lit[k] ? C_WHITE : C_GREY, 1);
        H->fill(x + 2, 104, 52, 6, C_DIM);
        H->fill(x + 2, 104, (int32_t)(bar[k] > 100u ? 100u : bar[k]) * 52 / 100, 6, lit[k] ? C_ACC : C_DIM);
    }
}
static void draw_head(void)
{
    char t[20], *p;
    H->fill(0, 0, 240, 48, C_BLACK);
    H->text(4, 2, 150, 0, "SEVENTIES", C_ACC, 0);
    p = put_u(t, set.tempo); put_s(p, running ? " RUN" : " BPM");
    H->text(150, 2, 86, 0, t, running ? C_ACC : C_GREY, 2);
    H->text(4, 24, 232, 0, PAGE_NAME[set.page], C_CREAM, 0);
    H->fill(0, 46, 240, 2, C_DIM);
}
static void draw_body(uint32_t full)
{
    static const char *const K_STR[4] = {"ATTACK", "RELEASE", "CHORUS", "PHASER"}, *const K_LEAD[4] = {"CUTOFF", "RESO", "GLIDE", "LEVEL"},
        *const K_BASS[4] = {"CUTOFF", "SWEEP", "TEMPO", "LEVEL"}, *const K_DRUM[4] = {"TEMPO", "LEVEL", "BALANCE", "EXTRAS"},
        *const K_CHORD[4] = {"ROOT", "KIND", "OCTAVE", "STRINGS"}, *const K_MIX[4] = {"STRINGS", "LEAD", "BASS", "DRUMS"},
        *const K_ECHO[4] = {"FEEDBK", "TONE", "STRINGS", "LEAD"};
    char v[4][12], line[40], *p;
    const char *vals[4] = {v[0], v[1], v[2], v[3]};
    uint8_t bar[4] = {0, 0, 0, 0}, lit[4] = {1, 1, 1, 1};
    uint32_t k;
    (void)full;
    H->fill(0, 128, 240, 112, C_BLACK);
    switch (set.page) {
    case PG_STRINGS:
        put_u(v[0], set.str_att); put_u(v[1], set.str_rel);
        if (set.str_ens) put_u(v[2], set.str_ens); else put_s(v[2], "OFF");
        if (set.str_ph) put_u(v[3], set.str_ph); else put_s(v[3], "OFF");
        bar[0] = set.str_att; bar[1] = set.str_rel; bar[2] = set.str_ens; bar[3] = set.str_ph; lit[2] = set.str_ens != 0; lit[3] = set.str_ph != 0;
        draw_knobs(K_STR, vals, bar, lit);
        p = put_s(line, REG_NAME[set.str_reg]); p = put_s(p, "   COLOR "); put_s(p, set.str_col ? "ON" : "OFF");
        H->text(4, 136, 232, 0, line, C_WHITE, 0);
        H->text(4, 200, 232, 0, "ALGO VOICE   FX COLOR", C_GREY, 1);
        break;
    case PG_LEAD: {
        const arp_patch_t *q = &ARP_PATCHES[set.lead_patch % ARP_N_PATCHES];
        put_i(v[0], set.lead_cut); put_u(v[1], set.lead_res == 255u ? q->reso : set.lead_res);
        put_u(v[2], set.lead_glide == 255u ? q->glide : set.lead_glide); put_u(v[3], set.lvl[1]);
        bar[0] = (uint8_t)(50 + set.lead_cut); bar[1] = set.lead_res == 255u ? q->reso : set.lead_res; bar[2] = set.lead_glide == 255u ? q->glide : set.lead_glide; bar[3] = set.lvl[1];
        draw_knobs(K_LEAD, vals, bar, lit);
        p = put_s(line, "P"); p = put_u(p, set.lead_patch + 1u); p = put_s(p, "  "); put_s(p, q->name);
        H->text(4, 136, 232, 0, line, C_WHITE, 0);
        H->text(4, 200, 232, 0, "ALGO PATCH   KEYS PLAY IT", C_GREY, 1);
        break;
    }
    case PG_BASS: {
        const arp_patch_t *q = &ARP_PATCHES[set.bass_patch % ARP_N_PATCHES];
        const arp_seq_t *s = &ARP_SEQS[set.bass_seq % ARP_N_SEQS];
        uint32_t sw = set.bass_sweep == 255u ? s->sweep : set.bass_sweep;
        put_i(v[0], set.bass_cut); put_u(v[1], sw); put_u(v[2], set.tempo); put_u(v[3], set.lvl[2]);
        bar[0] = (uint8_t)(50 + set.bass_cut); bar[1] = (uint8_t)sw; bar[2] = (uint8_t)((set.tempo - 40u) * 100u / 200u); bar[3] = set.lvl[2];
        draw_knobs(K_BASS, vals, bar, lit);
        p = put_s(line, "P"); p = put_u(p, set.bass_patch + 1u); p = put_s(p, " "); put_s(p, q->name);
        H->text(4, 136, 232, 0, line, C_WHITE, 0);
        p = put_s(line, "S"); p = put_u(p, set.bass_seq + 1u); p = put_s(p, " "); p = put_s(p, s->name);
        if (s->n_chain) { p = put_s(p, "  "); p = put_u(p, s->n_chain); put_s(p, " LINKS"); }
        H->text(4, 158, 232, 0, line, C_BLUE, 0);
        H->text(4, 200, 232, 0, "ALGO PATCH  PRESETS SEQ  PLAY", C_GREY, 1);
        break;
    }
    case PG_DRUMS: {
        uint32_t down = 0, one = 10;
        put_u(v[0], set.tempo); put_u(v[1], set.lvl[3]); put_u(v[2], set.rh_balance); put_u(v[3], set.rh_extra);
        bar[0] = (uint8_t)((set.tempo - 40u) * 100u / 200u); bar[1] = set.lvl[3]; bar[2] = set.rh_balance; bar[3] = set.rh_extra;
        draw_knobs(K_DRUM, vals, bar, lit);
        for (k = 0; k < 10u; k++) if (set.rh_keys >> k & 1u) { down++; one = k; }
        if (!down) put_s(line, "NO KEY");
        else if (down > 1u) { p = put_u(line, down); put_s(p, " KEYS"); }
        else { const char *nm = mp_rhythm(set.rh_section, one)->name; p = line; while (*nm) { char c = *nm++; *p++ = c >= 'a' && c <= 'z' ? (char)(c - 32) : c; } *p = 0; }
        p = line; while (*p) p++;
        p = put_s(p, "   LEVER "); put_s(p, set.rh_section ? "B" : "A");
        H->text(4, 136, 232, 0, line, C_WHITE, 0);
        p = put_s(line, "TAMB "); p = put_s(p, set.rh_xmask & 1u ? "ON " : "OFF"); p = put_s(p, "  GUIRO "); p = put_s(p, set.rh_xmask & 2u ? "ON " : "OFF");
        p = put_s(p, "  QUIJ "); put_s(p, set.rh_xmask & 4u ? "ON" : "OFF");
        H->text(4, 158, 232, 0, line, C_GREY, 0);
        H->text(4, 200, 232, 0, "KEYS RHYTHMS  OCT LEVER  PLAY", C_GREY, 1);
        break;
    }
    case PG_CHORDS: {
        const chord_t *c = &set.chord[set.cursor];
        put_s(v[0], NOTE_NAME[c->root % 12u]); put_s(v[1], KIND_NAME[c->kind % N_KINDS]); put_u(v[2], set.str_oct); put_u(v[3], set.lvl[0]);
        bar[0] = (uint8_t)(c->root * 100u / 11u); bar[1] = (uint8_t)(c->kind * 100u / (N_KINDS - 1u)); bar[2] = (uint8_t)(set.str_oct * 25u); bar[3] = set.lvl[0];
        lit[0] = lit[1] = c->kind != CH_NONE;
        draw_knobs(K_CHORD, vals, bar, lit);
        for (k = 0; k < ARP_CHAIN; k++) {       /* the eight links: root and kind, the cursor and the one playing */
            int32_t x = 4 + (int32_t)k * 29;
            const chord_t *d = &set.chord[k];
            uint16_t col = (int32_t)k == cur_link ? C_ACC : k < bass.seq.n_chain ? C_WHITE : C_GREY;
            H->fill(x, 132, 27, 36, k == set.cursor ? C_BLUE : C_DIM);
            H->fill(x + 2, 134, 23, 32, C_BLACK);
            H->text(x, 134, 27, 0, d->kind == CH_NONE ? "-" : NOTE_NAME[d->root % 12u], col, 1);
            H->text(x, 150, 27, 0, d->kind == CH_NONE ? "" : KIND_NAME[d->kind % N_KINDS], col, 1);
        }
        H->text(4, 176, 232, 0, set.latch ? "LATCH: THE KEYS SET THE CHORD" : "FOLLOWING THE BASS PATTERN", set.latch ? C_ACC : C_GREY, 1);
        H->text(4, 200, 232, 0, "SELECT LINK   REC LATCH", C_GREY, 1);
        break;
    }
    case PG_MIX:
        for (k = 0; k < 4u; k++) { put_u(v[k], set.lvl[k]); bar[k] = set.lvl[k]; lit[k] = set.lvl[k] != 0; }
        draw_knobs(K_MIX, vals, bar, lit);
        p = put_s(line, "REVERB "); p = put_u(p, set.reverb); put_s(p, "%   (ALGORITHM)");
        H->text(4, 136, 232, 0, line, C_WHITE, 0);
        H->text(4, 200, 232, 0, "FX AGAIN: THE ECHO", C_GREY, 1);
        break;
    default:
        put_u(v[0], set.ec_fb); put_u(v[1], set.ec_tone); put_u(v[2], set.ec_str); put_u(v[3], set.ec_lead);
        bar[0] = set.ec_fb; bar[1] = set.ec_tone; bar[2] = set.ec_str; bar[3] = set.ec_lead; lit[2] = set.ec_str != 0; lit[3] = set.ec_lead != 0;
        draw_knobs(K_ECHO, vals, bar, lit);
        p = put_s(line, "TIME "); p = put_s(p, EC_DIV_NAME[set.ec_div % 7u]); put_s(p, "   (ALGORITHM)");
        H->text(4, 136, 232, 0, line, ec_on ? C_WHITE : C_GREY, 0);
        H->text(4, 158, 232, 0, "SENDS: BASS AND DRUMS DRY", C_GREY, 0);
        H->text(4, 200, 232, 0, "FX AGAIN: THE MIX", C_GREY, 1);
        break;
    }
}
static uint32_t state_hash(void)
{
    const uint8_t *b = (const uint8_t *)&set;
    uint32_t i, h = 2166136261u;
    for (i = 0; i < sizeof set; i++) h = (h ^ b[i]) * 16777619u;
    return h ^ (uint32_t)(cur_link + 1) << 28 ^ running << 27;
}
static void draw(uint32_t full)
{
    uint32_t h;
    chords_follow();
    h = state_hash();
    if (full || set.page != drawn_page) { H->fill(0, 0, 240, 240, C_BLACK); drawn_page = set.page; drawn = ~0u; }
    if (h != drawn) { draw_head(); draw_body(full); drawn = h; }
    if (set.page == PG_DRUMS) {
        uint32_t k;
        for (k = 0; k < 17u; k++)
            H->led(HB_LED_KEY(k), WHITE[k] >= 0 ? (set.rh_keys >> WHITE[k] & 1u) :
                   k == 1u ? (set.rh_xmask & 1u) : k == 3u ? (set.rh_xmask >> 1 & 1u) : k == 5u ? (set.rh_xmask >> 2 & 1u) : 0u);
        drawn_live = 1;
    } else if (drawn_live) {
        uint32_t k;
        for (k = 0; k < 17u; k++) H->led(HB_LED_KEY(k), 0);
        drawn_live = 0;
    }
    H->led(HB_BTN_PLAY, running);
    H->led(HB_BTN_REC, set.latch);
    H->led(HB_BTN_FX, set.page == PG_MIX || set.page == PG_ECHO);
    if (dirty && H->ms() - t_save > 2000u) {
        set.magic = SET_MAGIC;
        H->save(&set, sizeof set);
        dirty = 0;
    }
}

/* ---- the panel */
static void page_to(uint32_t pg) { set.page = (uint8_t)pg; touch(); }
static void key_down(uint32_t k, int32_t vel)
{
    uint32_t note = 53u + k + 12u * set.oct;
    if (set.page == PG_DRUMS) {                 /* the rhythm keys */
        if (k < 27u && WHITE[k] >= 0) set.rh_keys ^= (uint16_t)(1u << WHITE[k]);
        else if (k == 1u) set.rh_xmask ^= 1u;
        else if (k == 3u) set.rh_xmask ^= 2u;
        else if (k == 5u) set.rh_xmask ^= 4u;
        else return;
        drums_apply(); touch();
        return;
    }
    if (set.latch) {                            /* the chord under the cursor: this key's note, a minor chord if none */
        chord_t *c = &set.chord[set.cursor];
        c->root = (uint8_t)(note % 12u);
        if (c->kind == CH_NONE) c->kind = CH_MIN;
        if (!running || cur_link == (int32_t)set.cursor) { cur_link = -2; }   /* (played now) */
        touch();
        return;
    }
    arp_note_on(&lead, note, vel > 0 ? (uint32_t)vel : 100u);
}
static void key_up(uint32_t k)
{
    if (set.page == PG_DRUMS || set.latch) return;
    arp_note_off(&lead, 53u + k + 12u * set.oct);
}
static void knob(uint32_t k, int32_t d)         /* KNOB1..4 on the page */
{
    switch (set.page) {
    case PG_STRINGS:
        if (k == 0) set.str_att = clamp8(set.str_att + 2 * d, 0, 100);
        else if (k == 1) set.str_rel = clamp8(set.str_rel + 2 * d, 0, 100);
        else if (k == 2) set.str_ens = clamp8(set.str_ens + 5 * d, 0, 100);
        else set.str_ph = clamp8(set.str_ph + 5 * d, 0, 100);
        strings_apply();
        break;
    case PG_LEAD: {
        const arp_patch_t *q = &ARP_PATCHES[set.lead_patch % ARP_N_PATCHES];
        if (k == 0) set.lead_cut = (int8_t)((int32_t)clamp8(set.lead_cut + 50 + 2 * d, 0, 100) - 50);
        else if (k == 1) set.lead_res = clamp8((set.lead_res == 255u ? q->reso : set.lead_res) + 2 * d, 0, 100);
        else if (k == 2) set.lead_glide = clamp8((set.lead_glide == 255u ? q->glide : set.lead_glide) + 2 * d, 0, 100);
        else set.lvl[1] = clamp8(set.lvl[1] + 5 * d, 0, 100);
        lead_apply();
        break;
    }
    case PG_BASS: {
        const arp_seq_t *s = &ARP_SEQS[set.bass_seq % ARP_N_SEQS];
        if (k == 0) set.bass_cut = (int8_t)((int32_t)clamp8(set.bass_cut + 50 + 2 * d, 0, 100) - 50);
        else if (k == 1) set.bass_sweep = clamp8((set.bass_sweep == 255u ? s->sweep : set.bass_sweep) + 5 * d, 0, 100);
        else if (k == 2) { int32_t v = (int32_t)set.tempo + 2 * d; set.tempo = (uint16_t)(v < 40 ? 40 : v > 240 ? 240 : v); tempo_apply(); break; }
        else set.lvl[2] = clamp8(set.lvl[2] + 5 * d, 0, 100);
        bass_apply();
        break;
    }
    case PG_DRUMS:
        if (k == 0) { int32_t v = (int32_t)set.tempo + 2 * d; set.tempo = (uint16_t)(v < 40 ? 40 : v > 240 ? 240 : v); tempo_apply(); break; }
        else if (k == 1) set.lvl[3] = clamp8(set.lvl[3] + 5 * d, 0, 100);
        else if (k == 2) set.rh_balance = clamp8(set.rh_balance + 5 * d, 0, 100);
        else set.rh_extra = clamp8(set.rh_extra + 10 * d, 0, 100);
        drums_apply();
        break;
    case PG_CHORDS: {
        chord_t *c = &set.chord[set.cursor];
        if (k == 0) { c->root = (uint8_t)(((int32_t)c->root + d) % 12 + 12) % 12; if (c->kind == CH_NONE) c->kind = CH_MIN; }
        else if (k == 1) c->kind = clamp8((int32_t)c->kind + d, 0, N_KINDS - 1);
        else if (k == 2) set.str_oct = clamp8(set.str_oct + d, 0, 4);
        else set.lvl[0] = clamp8(set.lvl[0] + 5 * d, 0, 100);
        if (cur_link == (int32_t)set.cursor) cur_link = -2;   /* (the chord playing changed: again) */
        break;
    }
    case PG_MIX:
        set.lvl[k] = clamp8(set.lvl[k] + 5 * d, 0, 100);
        break;
    default:
        if (k == 0) set.ec_fb = clamp8(set.ec_fb + 5 * d, 0, 100);
        else if (k == 1) set.ec_tone = clamp8(set.ec_tone + 5 * d, 0, 100);
        else if (k == 2) set.ec_str = clamp8(set.ec_str + 5 * d, 0, 100);
        else set.ec_lead = clamp8(set.ec_lead + 5 * d, 0, 100);
        echo_apply();
        break;
    }
    touch();
}
static void algo(int32_t d)                     /* ALGORITHM on the page */
{
    switch (set.page) {
    case PG_STRINGS: set.str_reg = clamp8((int32_t)set.str_reg + d, 0, EM_N_REG - 1); strings_apply(); break;
    case PG_LEAD: set.lead_patch = clamp8((int32_t)set.lead_patch + d, 0, ARP_N_PATCHES - 1); set.lead_res = set.lead_glide = 255; lead_apply(); break;
    case PG_BASS: set.bass_patch = clamp8((int32_t)set.bass_patch + d, 0, ARP_N_PATCHES - 1); bass_apply(); break;
    case PG_DRUMS: {                            /* one rhythm of the twenty */
        uint32_t k;
        int32_t v;
        for (k = 0; k < 10u && !(set.rh_keys >> k & 1u); k++)
            ;
        v = (int32_t)(k < 10u ? k + 10u * set.rh_section : 6u) + d;
        v = v < 0 ? 0 : v > 19 ? 19 : v;
        set.rh_keys = (uint16_t)(1u << (v % 10));
        set.rh_section = (uint8_t)(v / 10);
        drums_apply();
        break;
    }
    case PG_CHORDS: set.cursor = (uint8_t)((((int32_t)set.cursor + d) % ARP_CHAIN) + ARP_CHAIN) % ARP_CHAIN; break;
    case PG_MIX: set.reverb = clamp8(set.reverb + 5 * d, 0, 100); reverb_out(); break;
    default: set.ec_div = clamp8((int32_t)set.ec_div + d, 0, 6); echo_apply(); break;
    }
    touch();
}
static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev);
    switch (HB_EV_KIND(ev)) {
    case HB_EV_KEY_DOWN: key_down(id, arg); return;
    case HB_EV_KEY_UP: key_up(id); return;
    case HB_EV_BTN_DOWN:
        switch (id) {
        case HB_BTN_EDIT: page_to(PG_STRINGS); break;
        case HB_BTN_GLO: page_to(PG_LEAD); break;
        case HB_BTN_ENV: page_to(PG_BASS); break;
        case HB_BTN_LFO: page_to(PG_DRUMS); break;
        case HB_BTN_SEQ: page_to(PG_CHORDS); break;
        case HB_BTN_FX: page_to(set.page == PG_MIX ? PG_ECHO : PG_MIX); break;
        case HB_BTN_PLAY: band(!running); touch(); break;
        case HB_BTN_REC: set.latch = !set.latch; if (set.latch) page_to(PG_CHORDS); arp_all_off(&lead); touch(); break;
        case HB_BTN_ARP: arp_all_off(&lead); lead.transpose = 0; chord_stop(); cur_link = -2; break;
        case HB_BTN_SAVE: set.magic = SET_MAGIC; H->save(&set, sizeof set); dirty = 0; H->log("seventies: saved"); break;
        case HB_BTN_OCT_DOWN:
            if (set.page == PG_DRUMS) { set.rh_section = 0; drums_apply(); }
            else if (set.oct > 0) set.oct--;
            touch();
            break;
        case HB_BTN_OCT_UP:
            if (set.page == PG_DRUMS) { set.rh_section = 1; drums_apply(); }
            else if (set.oct < 4u) set.oct++;
            touch();
            break;
        default: break;
        }
        return;
    case HB_EV_ENC:
        switch (id) {
        case HB_ENC_ALGORITHM: algo(arg); return;
        case HB_ENC_PRESETS:
            if (set.page == PG_BASS) { set.bass_seq = clamp8((int32_t)set.bass_seq + arg, 0, ARP_N_SEQS - 1); set.bass_sweep = 255; set.tempo = ARP_SEQS[set.bass_seq].tempo; tempo_apply(); cur_link = -2; touch(); }
            else if (set.page == PG_CHORDS) algo(arg);
            else if (set.page == PG_LEAD) algo(arg);
            return;
        case HB_ENC_SELECT:
            if (set.page == PG_CHORDS) algo(arg);
            return;
        default:
            if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) knob(id - HB_ENC_KNOB1, arg);
            return;
        }
    default: return;
    }
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    switch (st & 0xF0u) {
    case 0x90: if (d2) arp_note_on(&lead, d1, d2); else arp_note_off(&lead, d1); break;
    case 0x80: arp_note_off(&lead, d1); break;
    case 0xC0: if (d1 < ARP_N_PATCHES) { set.lead_patch = (uint8_t)d1; set.lead_res = set.lead_glide = 255; lead_apply(); touch(); } break;
    case 0xB0:
        if (d1 == 74u) { set.lead_cut = (int8_t)((int32_t)d2 * 100 / 127 - 50); lead_apply(); touch(); }
        else if (d1 == 7u) { set.lvl[1] = (uint8_t)(d2 * 100u / 127u); touch(); }
        else if (d1 == 123u || d1 == 120u) { arp_all_off(&lead); chord_stop(); cur_link = -2; }
        break;
    default: break;
    }
}
static void note(uint32_t ch, uint32_t key, uint32_t vel) { (void)ch; if (vel) arp_note_on(&lead, key, vel); else arp_note_off(&lead, key); }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { midi(0xB0u | ch, num, val); }

/* ---- the sound: the four parts apart, the echo from two of them, all into the block */
static void render(int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t g[4];
    if (n > 256u) n = 256u;
    for (k = 0; k < 4u; k++) { for (i = 0; i < 2u * n; i++) tmp[k][i] = 0; g[k] = (int32_t)set.lvl[k] * 256 / 100; }
    em_render(&strings, tmp[0], n);
    arp_render(&lead, tmp[1], n);
    arp_render(&bass, tmp[2], n);
    mp_render(&drums, tmp[3], n);
    for (i = 0; i < 2u * n; i++)
        out[i] += MUL(tmp[0][i], g[0], 8) + MUL(tmp[1][i], g[1], 8) + MUL(tmp[2][i], g[2], 8) + MUL(tmp[3][i], g[3], 8);
    if (ec_on) {
        int32_t gs = (int32_t)set.ec_str * g[0] / 100, gl = (int32_t)set.ec_lead * g[1] / 100;
        for (i = 0; i < n; i++)
            ec_src[i] = (((tmp[0][2 * i] + tmp[0][2 * i + 1]) >> 1) * gs + ((tmp[1][2 * i] + tmp[1][2 * i + 1]) >> 1) * gl) >> 8;
        ec_run(ec_src, out, n);
    }
}

static int init(const hb_host_t *h)
{
    H = h;
    em_init(&strings);
    arp_init(&lead);
    arp_init(&bass);
    mp_init(&drums);
    ec_clear();
    if (H->load(&set, sizeof set) || set.magic != SET_MAGIC) {
        uint32_t k;
        static const chord_t START[ARP_CHAIN] = {{4, CH_MIN}, {9, CH_MIN}, {4, CH_MIN}, {7, CH_MIN}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
        for (k = 0; k < sizeof set; k++) ((uint8_t *)&set)[k] = 0;
        set.magic = SET_MAGIC;
        set.oct = 1; set.page = PG_CHORDS;
        set.lead_patch = 3; set.bass_patch = 7; set.bass_seq = 1;   /* ARP LEAD; OXYGENE SEQ on OXYGENE IV */
        set.str_reg = EM_STRINGS; set.str_att = 45; set.str_rel = 55; set.str_ens = 70; set.str_ph = 25; set.str_col = 1; set.str_oct = 1;
        set.lead_res = set.lead_glide = set.bass_sweep = 255;
        set.tempo = ARP_SEQS[1].tempo; set.rh_keys = 1u << 6; set.rh_balance = 50; set.rh_extra = 70;   /* Rock 1 */
        set.lvl[0] = 70; set.lvl[1] = 80; set.lvl[2] = 85; set.lvl[3] = 70;
        set.reverb = 30; set.ec_div = 3; set.ec_fb = 45; set.ec_tone = 50; set.ec_str = 15; set.ec_lead = 35;
        for (k = 0; k < ARP_CHAIN; k++) set.chord[k] = START[k];
    }
    if (set.page >= N_PAGES) set.page = PG_CHORDS;
    if (set.oct > 4u) set.oct = 1;
    if (set.tempo < 40u || set.tempo > 240u) set.tempo = 120;
    if (set.cursor >= ARP_CHAIN) set.cursor = 0;
    strings_apply();
    lead_apply();
    bass_apply();
    drums_apply();
    echo_apply();
    reverb_out();
    running = want_run = 0;
    cur_link = -1;
    dirty = 0;
    H->log("seventies: strings, lead, bass, drums");
    return 0;
}
static void release(void) { band(0); arp_all_off(&lead); chord_stop(); }

HB_DESC = {HB_ABI, "SEVENTIES", init, release, note, cc, render, ui, draw, 0, 0, HB_F_SCREEN | HB_F_PANEL, midi};
