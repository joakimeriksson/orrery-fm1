/* SPDX-License-Identifier: GPL-3.0-only */
// MD_TEST_SOURCES: core/commands.c core/session.c core/mixer.c core/effect.c core/router.c core/instrument.c core/transport.c module/seq.c tests/native/fake_processors.c
#include "core/session.h"
#include "fake_processors.h"
#include "test.h"
#include <pthread.h>
#include <string.h>

static md_session_t S;
static fake_instrument_state_t fake[MD_TRACKS];
static md_instrument_t inst[MD_TRACKS];
static fake_effect_state_t echo_state;
static md_effect_t echo;
static md_seq_bank_t bank;
static int32_t out[2u * MD_MAX_RENDER_FRAMES];

static void empty_bank(md_seq_bank_t *b)
{
    memset(b, 0, sizeof *b);
    b->seed = 0x5e55u;
    for (uint32_t i = 0; i < 3; i++) b->global_owner[i] = MD_AUTOMATION_UNOWNED;
    for (uint32_t t = 0; t < MD_TRACKS; t++)
        for (uint32_t p = 0; p < MD_PATTERNS_PER_TRACK; p++) {
            md_seq_pattern_t *a = &b->pattern[t][p];
            a->meta = (md_pattern_meta_t){.length = 16, .division = MD_DIV_16TH, .swing = 50, .mode = MD_PATTERN_MONO};
            for (uint32_t k = 0; k < MD_STEPS_PER_PATTERN; k++) a->step[k] = (md_step_t){.probability = 100};
        }
}
static void note(md_seq_bank_t *b, uint32_t t, uint32_t p, uint32_t k, uint16_t payload, uint8_t gate)
{
    b->pattern[t][p].step[k] = (md_step_t){.payload = payload, .velocity = 100, .gate = gate, .probability = 100};
}
static int setup(const md_seq_bank_t *b)
{
    md_session_config_t c = {.bank = b, .bpm_q8 = 120u * 256u};
    for (uint32_t t = 0; t < MD_TRACKS; t++) {
        if (md_instrument_init(&inst[t], &FAKE_INSTRUMENT, &fake[t], sizeof fake[t], MD_SAMPLE_RATE)) return 1;
        c.instrument[t] = &inst[t];
    }
    if (md_effect_init(&echo, &FAKE_EFFECT, &echo_state, sizeof echo_state, MD_SAMPLE_RATE)) return 1;
    c.bus[MD_MIX_BUS_ECHO] = &echo;
    return md_session_init(&S, &c) != MD_OK;
}
static int run(uint32_t frames)
{
    memset(out, 0, sizeof out);
    for (uint32_t t = 0; t < MD_TRACKS; t++) fake_trace_clear(&fake[t]);
    return md_session_render(&S, out, frames) != MD_OK;
}
static int command(uint8_t kind, uint8_t track, uint8_t arg, uint32_t value)
{
    md_command_t c = {.kind = kind, .track = track, .arg = arg, .value = value};
    return md_session_submit(&S, &c);
}
static uint32_t voices(void)
{
    uint32_t n = 0;
    for (uint32_t t = 0; t < MD_TRACKS; t++) n += fake[t].voice_count;
    return n;
}

static int reserve_and_emergency_flush(void)
{
    empty_bank(&bank);
    CHECK(!setup(&bank));
    md_note_token_t token[MD_COMMAND_CAPACITY];
    uint32_t on = 0;
    /* ordinary commands stop at the reserve */
    while (md_session_note_on(&S, 0, MD_SOURCE_PANEL, 60, 100, 0, &token[on]) == MD_OK) on++;
    CHECK(on == MD_COMMAND_CAPACITY - MD_RELEASE_RESERVE && S.ring.rejected == 1);
    CHECK(token[0] > MD_LIVE_TOKEN_BASE && token[1] == token[0] + 1u);
    CHECK(md_session_param(&S, 0, FAKE_PARAM_GAIN, 5) == MD_ERR_CAPACITY);
    /* releases use the reserve */
    uint32_t off = 0;
    while (md_session_note_off(&S, 0, MD_SOURCE_PANEL, token[off]) == MD_OK) off++;
    CHECK(off == MD_RELEASE_RESERVE && S.ring.release_refused == 1);
    CHECK(S.flush_request[MD_SOURCE_PANEL] == 1);   /* the refused off asked for a flush */
    CHECK(md_ring_used(&S.ring) == MD_COMMAND_CAPACITY);
    CHECK(!run(64));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK);
    CHECK(snap.max_block_commands == MD_COMMAND_CAPACITY && snap.commands == MD_COMMAND_CAPACITY);
    CHECK(snap.flushes == 1 && snap.owned_notes == 0 && voices() == 0);
    CHECK(snap.max_owned_notes == 0);                 /* sampled after the flush in the same block */
    CHECK(S.ring.high_water == MD_COMMAND_CAPACITY);
    /* a late off for a flushed note is a harmless no-op */
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, token[off + 5]) == MD_OK && !run(64));
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.command_errors == 0);
    /* a flush releases only its source: MIDI notes on the same pitch survive */
    md_note_token_t midi, panel;
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_MIDI_BASE, 60, 90, 0, &midi) == MD_OK);
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 60, 90, 0, &panel) == MD_OK && !run(16));
    S.flush_request[MD_SOURCE_PANEL]++;
    CHECK(!run(16) && md_router_owned_count(&S.router) == 1 && fake[0].voice_count == 1);
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_MIDI_BASE, midi) == MD_OK && !run(16) && voices() == 0);
    return 0;
}

static int fill_ring(void)
{
    md_note_token_t t;
    while (md_session_note_on(&S, 2, MD_SOURCE_MIDI_BASE + 1, 30, 100, 0, &t) == MD_OK) {}
    while (md_session_note_off(&S, 2, MD_SOURCE_MIDI_BASE + 2, 1) == MD_OK) {}
    return md_ring_used(&S.ring) != MD_COMMAND_CAPACITY;
}
/* Review findings: refused sustain-up, counter wrap, refused stop and panic. */
static int refused_releases_never_strand(void)
{
    empty_bank(&bank);
    for (uint32_t k = 0; k < 16; k++) note(&bank, 1, 0, k, (uint16_t)(50 + k), 190);
    CHECK(!setup(&bank));
    md_command_t pedal = {.kind = MD_CMD_SUSTAIN, .track = 0, .value = 1};
    pedal.event.source = MD_SOURCE_PANEL;
    md_note_token_t a;
    CHECK(md_session_submit(&S, &pedal) == MD_OK);
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 64, 100, 0, &a) == MD_OK);
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, a) == MD_OK && !run(32));
    CHECK(fake[0].voice_count == 1);                      /* held by the pedal */
    CHECK(!fill_ring());
    pedal.value = 0;
    CHECK(md_session_submit(&S, &pedal) == MD_ERR_CAPACITY);
    CHECK(!run(32) && fake[0].voice_count == 0 && !S.router.sustain[0][MD_SOURCE_PANEL]);
    /* more than 256 refusals before audio runs still flush */
    md_note_token_t b;
    CHECK(!run(32) && md_session_note_on(&S, 0, MD_SOURCE_PANEL, 65, 100, 0, &b) == MD_OK && !run(32));
    CHECK(fake[0].voice_count == 1 && !fill_ring());
    for (uint32_t i = 0; i < 512; i++) CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, b) == MD_ERR_CAPACITY);
    CHECK(S.flush_request[MD_SOURCE_PANEL] != S.flush_seen[MD_SOURCE_PANEL]);
    CHECK(!run(32) && fake[0].voice_count == 0);
    CHECK(S.flush_request[MD_SOURCE_PANEL] == S.flush_seen[MD_SOURCE_PANEL]);
    /* refused stop: the sequencer still stops */
    CHECK(!run(32) && command(MD_CMD_START, 0, 0, 0) == MD_OK && !run(256) && fake[1].voice_count == 1);
    CHECK(!fill_ring() && command(MD_CMD_STOP, 0, 0, 0) == MD_ERR_CAPACITY);
    CHECK(!run(64));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && !snap.running && fake[1].voice_count == 0);
    /* refused global panic: everything is cut */
    md_note_token_t c;
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK);
    CHECK(md_session_note_on(&S, 3, MD_SOURCE_PANEL, 70, 100, 0, &c) == MD_OK && !run(256));
    CHECK(voices() == 2 && !fill_ring());
    md_command_t panic = {.kind = MD_CMD_EVENT};
    panic.event = (md_event_t){.kind = MD_EVENT_PANIC, .track = MD_ALL_TRACKS, .source = MD_SOURCE_NONE};
    CHECK(md_session_submit(&S, &panic) == MD_ERR_CAPACITY);
    CHECK(!run(64) && voices() == 0 && md_router_owned_count(&S.router) == 0);
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && !snap.running);
    return 0;
}

static int bank_edits_are_acknowledged(void)
{
    empty_bank(&bank);
    CHECK(!setup(&bank));
    const md_seq_bank_t *live = S.seq.bank;
    md_seq_bank_t before = *live;
    md_seq_bank_t *edit = md_session_edit_bank(&S);
    CHECK(edit && edit != live && md_session_edit_bank(&S) == edit);
    note(edit, 0, 0, 0, 64, 50);
    CHECK(!memcmp(live, &before, sizeof before));      /* editing never touches the live bank */
    CHECK(md_session_publish_bank(&S) == MD_OK);
    CHECK(md_session_pending(&S) == 1u && !md_session_edit_bank(&S));
    CHECK(md_session_publish_bank(&S) == MD_ERR_STATE);
    CHECK(!run(32));
    CHECK(md_session_pending(&S) == 0 && S.seq.bank == edit);
    /* the next staging buffer is the old live one, refreshed from the new live bank */
    md_seq_bank_t *next = md_session_edit_bank(&S);
    CHECK(next == live && next != S.seq.bank && !memcmp(next, edit, sizeof *next));
    /* an invalid bank is refused, nothing is queued, the staging copy stays open */
    next->pattern[0][0].meta.length = 0;
    CHECK(md_session_publish_bank(&S) == MD_ERR_RANGE && md_session_pending(&S) == 0);
    CHECK(md_ring_used(&S.ring) == 0 && md_session_edit_bank(&S) == next);
    next->pattern[0][0].meta.length = 8;
    CHECK(md_session_publish_bank(&S) == MD_OK && !run(32) && S.seq.bank == next);
    /* the published edit plays: start and hear key 64 on track 0 */
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK && !run(32));
    CHECK(fake[0].voice_count == 1 && fake[0].voices[0].key == 64);
    /* changing a track's pattern mode while it plays releases its notes */
    md_seq_bank_t *mode = md_session_edit_bank(&S);
    CHECK(mode);
    for (uint32_t p = 0; p < MD_PATTERNS_PER_TRACK; p++) {
        mode->pattern[0][p].meta.mode = MD_PATTERN_CHORD;
        for (uint32_t k = 0; k < MD_STEPS_PER_PATTERN; k++) mode->pattern[0][p].step[k].payload = 0;
    }
    CHECK(md_session_publish_bank(&S) == MD_OK && !run(32));
    CHECK(fake[0].voice_count == 0);
    return 0;
}

static int sound_handoff(void)
{
    empty_bank(&bank);
    CHECK(!setup(&bank));
    uint8_t good = 100, bad = 200;
    CHECK(md_session_stage_sound(&S, 0, 1, &bad, 1) == MD_ERR_RANGE && md_session_pending(&S) == 0);
    CHECK(md_session_stage_sound(&S, 0, 2, &good, 1) == MD_ERR_VERSION);
    md_note_token_t held;
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 50, 100, 0, &held) == MD_OK && !run(16));
    CHECK(md_session_stage_sound(&S, 0, 1, &good, 1) == MD_OK && md_session_pending(&S) == 2u);
    CHECK(md_session_stage_sound(&S, 0, 1, &good, 1) == MD_ERR_STATE);  /* one in flight */
    CHECK(fake[0].gain == 64);
    CHECK(!run(16));
    CHECK(md_session_pending(&S) == 0 && fake[0].gain == 100 && S.base[0][0] == 100);
    CHECK(fake[0].voice_count == 0 && md_router_owned_count(&S.router) == 0); /* old sound's notes released */
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, held) == MD_OK && !run(16));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.command_errors == 0);
    return 0;
}

static int locks_keep_ui_base_underneath(void)
{
    empty_bank(&bank);
    note(&bank, 0, 0, 0, 60, 50);
    note(&bank, 0, 0, 1, 62, 50);
    bank.locks[0] = (md_lock_t){.step = 0, .target = FAKE_PARAM_GAIN, .value = 10};
    bank.lock_count = 1;
    CHECK(!setup(&bank));
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK && !run(256));
    CHECK(fake[0].gain == 10 && S.base[0][0] == 64);    /* lock on DSP, base untouched */
    CHECK(md_session_param(&S, 0, FAKE_PARAM_GAIN, 90) == MD_OK && !run(256));
    CHECK(fake[0].gain == 10 && S.base[0][0] == 90);    /* UI edit stays under the lock */
    for (uint32_t i = 0; i < 22; i++) CHECK(!run(256));  /* past step 1 at 5512.5 samples */
    CHECK(fake[0].gain == 90);                           /* restore brings the new base */
    /* without a lock, a UI edit reaches the DSP at once */
    CHECK(md_session_param(&S, 0, FAKE_PARAM_GAIN, 33) == MD_OK && !run(16) && fake[0].gain == 33);
    /* stop restores base after a lock */
    for (uint32_t i = 0; i < 400 && fake[0].gain != 10; i++) CHECK(!run(256));
    CHECK(fake[0].gain == 10);
    CHECK(command(MD_CMD_STOP, 0, 0, 0) == MD_OK && !run(64) && fake[0].gain == 33);
    CHECK(voices() == 0);
    return 0;
}

static int namespaces(void)
{
    empty_bank(&bank);
    CHECK(!setup(&bank));
    int32_t v = 0;
    CHECK(md_session_param(&S, 2, MD_TARGET_MIXER | MD_MIX_LEVEL, 1000) == MD_OK);
    CHECK(md_session_param(&S, 2, MD_TARGET_ECHO | FAKE_PARAM_GAIN, 7) == MD_OK);
    CHECK(md_session_param(&S, 2, 0x7001, 1) == MD_OK);                     /* queued, refused by audio */
    CHECK(md_session_param(&S, 2, MD_TARGET_INSERT_1 | 1, 1) == MD_OK);     /* no insert bound */
    CHECK(md_session_param(&S, 2, MD_TARGET_MIXER | MD_MIX_LEVEL, 40000) == MD_OK);
    CHECK(!run(16));
    CHECK(md_mixer_track_param_get(&S.mixer, 2, MD_MIX_LEVEL, &v) == MD_OK && v == 1000);
    CHECK(S.mix_base[2][0] == 1000 && echo_state.gain == 7);
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.command_errors == 3 && snap.commands == 2);
    CHECK(snap.last_error == MD_ERR_RANGE);
    /* mute, solo, return level and bypass reach the mixer */
    CHECK(command(MD_CMD_MUTE, 1, 0, 1) == MD_OK && command(MD_CMD_SOLO, 3, 0, 1) == MD_OK);
    CHECK(command(MD_CMD_RETURN_LEVEL, 0, MD_MIX_BUS_ECHO, 123) == MD_OK);
    CHECK(command(MD_CMD_BYPASS, MD_BYPASS_MASTER, 0, 0) == MD_OK && !run(16));
    CHECK(S.mixer.track[1].mute && S.mixer.track[3].solo && S.mixer.return_level[0] == 123);
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.mute_mask == 2 && snap.solo_mask == 8);
    CHECK(snap.command_errors == 3);
    return 0;
}

static int panic_cuts_everything(void)
{
    empty_bank(&bank);
    for (uint32_t k = 0; k < 16; k++) note(&bank, 1, 0, k, (uint16_t)(48 + k), 190);
    CHECK(!setup(&bank));
    md_note_token_t a;
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 70, 100, 0, &a) == MD_OK);
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK && !run(256));
    echo_state.memory[0] = 999;                          /* pretend the echo holds a tail */
    CHECK(voices() == 2);
    md_command_t panic = {.kind = MD_CMD_EVENT};
    panic.event = (md_event_t){.kind = MD_EVENT_PANIC, .track = MD_ALL_TRACKS, .source = MD_SOURCE_NONE};
    CHECK(md_session_submit(&S, &panic) == MD_OK && !run(256));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && !snap.running && snap.owned_notes == 0);
    CHECK(voices() == 0 && echo_state.memory[0] == 0);
    for (uint32_t i = 0; i < 40; i++) CHECK(!run(256));
    CHECK(voices() == 0);                                /* stopped: nothing restarts */
    return 0;
}

/* Deterministic stress: dense live input from several sources, locks, launches, mutes,
 * tempo and transport changes, bursts that hit the reserve, random span sizes. */
static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }
typedef struct { md_note_token_t token; uint8_t track; md_source_id_t source; } held_t;
static held_t held[4096];
static uint32_t held_count;
static void forget_source(md_source_id_t source)
{
    for (uint32_t i = 0; i < held_count;)
        if (held[i].source == source) held[i] = held[--held_count];
        else i++;
}
static int release_one(uint32_t index)
{
    held_t h = held[index];
    held[index] = held[--held_count];
    int status = md_session_note_off(&S, h.track, h.source, h.token);
    if (status == MD_ERR_CAPACITY) forget_source(h.source);   /* the session will flush it */
    else if (status) return 1;
    return 0;
}
static int stress(void)
{
    empty_bank(&bank);
    for (uint32_t p = 0; p < MD_PATTERNS_PER_TRACK; p++) {
        bank.pattern[1][p].meta.mode = MD_PATTERN_CHORD;
        bank.pattern[2][p].meta.division = MD_DIV_32ND;
        bank.pattern[2][p].meta.swing = 75;
        for (uint32_t k = 0; k < 16; k++) {
            note(&bank, 0, p, k, (uint16_t)(36 + (k * 7 + p) % 24), (uint8_t)(20 + k * 11));
            note(&bank, 1, p, k, (uint16_t)((48 + k) | (uint16_t)((k % MD_CHORD_COUNT) << 8)), 120);
            note(&bank, 2, p, k, (uint16_t)(60 + k), (uint8_t)(k & 1u ? 200 : 30));
            bank.pattern[2][p].step[k].probability = 60;
            if (k % 3 == 0) bank.pattern[2][p].step[k].flags = MD_STEP_TIE;
        }
    }
    for (uint32_t i = 0; i < MD_MAX_LOCKS; i++)
        bank.locks[i] = (md_lock_t){.step = (uint16_t)((i / 12) * 64u + i % 12), .target = FAKE_PARAM_GAIN,
                                    .value = (int16_t)(i * 2)};
    bank.lock_count = MD_MAX_LOCKS;
    CHECK(!setup(&bank));
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK);
    held_count = 0;
    uint32_t refused_ons = 0, refused_offs = 0;
    for (uint32_t round = 0; round < 6000; round++) {
        int burst = rnd(16) == 0;                              /* sometimes overflow the ring */
        uint32_t ops = burst ? 200 : rnd(6);
        for (uint32_t k = 0; k < ops; k++) {
            /* a burst queues note-ons until refused, then note-offs into the reserve and past it */
            uint32_t op = burst ? (k < 120 ? 0 : 50) : rnd(100);
            if (op < 45) {
                md_source_id_t source = rnd(3) ? MD_SOURCE_PANEL : (md_source_id_t)(MD_SOURCE_MIDI_BASE + rnd(16));
                held_t h = {0, (uint8_t)rnd(MD_TRACKS), source};
                int status = md_session_note_on(&S, h.track, source, (uint8_t)(40 + rnd(40)), (uint8_t)(1 + rnd(127)), 0, &h.token);
                if (status == MD_OK && held_count < 4096) held[held_count++] = h;
                else if (status == MD_ERR_CAPACITY) refused_ons++;
                else CHECK(status == MD_OK);
            } else if (op < 85) {
                if (held_count) {
                    uint32_t before = S.ring.release_refused;
                    CHECK(!release_one(rnd(held_count)));
                    refused_offs += S.ring.release_refused - before;
                }
            } else if (op < 90) {
                (void)md_session_param(&S, (uint8_t)rnd(MD_TRACKS), FAKE_PARAM_GAIN, (int32_t)rnd(128));
            } else if (op < 93) {
                (void)command(MD_CMD_MUTE, (uint8_t)rnd(MD_TRACKS), 0, rnd(2));
            } else if (op < 96) {
                (void)command(MD_CMD_LAUNCH, (uint8_t)rnd(MD_TRACKS), (uint8_t)rnd(4), rnd(2));
            } else if (op < 98) {
                (void)command(MD_CMD_TEMPO, 0, 0, 40u * 256u + rnd(200u * 256u));
            } else {
                uint32_t which = rnd(3);
                (void)command(which == 0 ? MD_CMD_STOP : which == 1 ? MD_CMD_CONTINUE : MD_CMD_START, 0, 0, rnd(64));
            }
        }
        CHECK(!run(1 + rnd(MD_MAX_RENDER_FRAMES)));
    }
    /* release everything still held, stop, and drain */
    while (held_count) {
        CHECK(!release_one(held_count - 1));
        if (md_ring_used(&S.ring) > MD_COMMAND_CAPACITY / 2) CHECK(!run(64));
    }
    CHECK(command(MD_CMD_STOP, 0, 0, 0) == MD_OK);
    for (uint32_t i = 0; i < 8; i++) CHECK(!run(256));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK);
    printf("  stress: %u blocks, %u events (max %u/block), %u commands (max %u/block), %u flushes, "
           "%u refused ons, %u refused offs, max owned %u, %u rejected notes, %u event errors\n",
           snap.blocks, snap.events, snap.max_block_events, snap.commands, snap.max_block_commands,
           snap.flushes, refused_ons, refused_offs, snap.max_owned_notes, snap.rejected_notes, snap.event_errors);
    CHECK(snap.owned_notes == 0 && voices() == 0 && md_ring_used(&S.ring) == 0);
    CHECK(snap.backend_errors == 0 && snap.seq_faults == 0);
    CHECK(snap.max_block_events <= MD_BLOCK_EVENT_LIMIT && snap.max_block_commands <= MD_COMMAND_CAPACITY);
    CHECK(refused_ons > 0 && refused_offs > 0 && snap.flushes > 0 && snap.events > 1000);
    return 0;
}

/* Densest supported sequence: four tracks of four-note chords on every 32nd at 240 BPM,
 * full-length gates, 48 locks, with live notes on top. */
static int dense_chords(void)
{
    empty_bank(&bank);
    for (uint32_t t = 0; t < MD_TRACKS; t++)
        for (uint32_t p = 0; p < MD_PATTERNS_PER_TRACK; p++) {
            bank.pattern[t][p].meta = (md_pattern_meta_t){.length = 64, .division = MD_DIV_32ND, .swing = 50,
                                                          .mode = MD_PATTERN_CHORD};
            for (uint32_t k = 0; k < 64; k++)
                note(&bank, t, p, k, (uint16_t)((40 + (k * 5 + t * 3) % 40) | (uint16_t)(MD_CHORD_MAJOR7 << 8)), 100);
        }
    for (uint32_t i = 0; i < MD_MAX_LOCKS; i++)
        bank.locks[i] = (md_lock_t){.step = (uint16_t)(i * 21u), .target = FAKE_PARAM_GAIN, .value = (int16_t)(i + 1)};
    bank.lock_count = MD_MAX_LOCKS;
    CHECK(!setup(&bank));
    CHECK(command(MD_CMD_TEMPO, 0, 0, 240u * 256u) == MD_OK && command(MD_CMD_START, 0, 0, 0) == MD_OK);
    md_note_token_t live[8];
    for (uint32_t i = 0; i < 8; i++) CHECK(md_session_note_on(&S, (uint8_t)(i % 4), MD_SOURCE_PANEL, 100, 100, 0, &live[i]) == MD_OK);
    for (uint32_t i = 0; i < 2000; i++) CHECK(!run(256));       /* about 11.6 s */
    for (uint32_t i = 0; i < 8; i++) CHECK(md_session_note_off(&S, (uint8_t)(i % 4), MD_SOURCE_PANEL, live[i]) == MD_OK);
    CHECK(command(MD_CMD_STOP, 0, 0, 0) == MD_OK && !run(256) && !run(256));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK);
    printf("  dense: %u events (max %u/block), max owned %u, %u rejected notes, %u event errors\n",
           snap.events, snap.max_block_events, snap.max_owned_notes, snap.rejected_notes, snap.event_errors);
    CHECK(snap.seq_faults == 0 && snap.backend_errors == 0 && snap.events > 10000);
    CHECK(snap.rejected_notes > 0 && snap.event_errors == snap.rejected_notes); /* visible, not silent */
    CHECK(snap.owned_notes == 0 && voices() == 0 && snap.max_block_events <= MD_BLOCK_EVENT_LIMIT);
    return 0;
}

/* Real concurrency on the host: one producer thread, one audio thread. */
#define THREAD_COMMANDS 200000u
static md_command_ring_t ring;
static volatile int ring_error;
static void *consumer(void *arg)
{
    (void)arg;
    uint32_t expect = 0;
    md_command_t c;
    while (expect < THREAD_COMMANDS)
        if (md_ring_pop(&ring, &c)) {
            if (c.value != expect || c.kind != MD_CMD_TEMPO || c.event.token != expect * 3u) ring_error = 1;
            expect++;
        }
    return NULL;
}
static volatile int audio_done;
static void *audio(void *arg)
{
    (void)arg;
    int32_t buffer[2u * 64u];
    for (uint32_t i = 0; i < 20000; i++) {
        memset(buffer, 0, sizeof buffer);
        if (md_session_render(&S, buffer, 64)) ring_error = 1;
    }
    audio_done = 1;
    return NULL;
}
static int threaded_handoff(void)
{
    md_ring_init(&ring);
    ring_error = 0;
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, consumer, NULL));
    for (uint32_t i = 0; i < THREAD_COMMANDS;) {
        md_command_t c = {.kind = MD_CMD_TEMPO, .value = i};
        c.event.token = i * 3u;
        if (md_ring_push(&ring, &c, i & 1u) == MD_OK) i++;
    }
    CHECK(!pthread_join(thread, NULL) && !ring_error && md_ring_used(&ring) == 0);
    /* snapshots stay consistent while audio rewrites them: samples == blocks * 64 */
    empty_bank(&bank);
    for (uint32_t k = 0; k < 16; k++) note(&bank, 0, 0, k, (uint16_t)(50 + k), 60);
    CHECK(!setup(&bank));
    CHECK(command(MD_CMD_START, 0, 0, 0) == MD_OK);
    audio_done = 0;
    CHECK(!pthread_create(&thread, NULL, audio, NULL));
    uint32_t reads = 0, busy = 0;
    md_note_token_t token = 0;
    while (!audio_done) {
        md_session_snapshot_t snap;
        int status = md_session_snapshot(&S, &snap);
        if (status == MD_OK) { reads++; CHECK(snap.samples == snap.blocks * 64u); }
        else busy++;
        if (token) { (void)md_session_note_off(&S, 3, MD_SOURCE_PANEL, token); token = 0; }
        else (void)md_session_note_on(&S, 3, MD_SOURCE_PANEL, 72, 100, 0, &token);
    }
    CHECK(!pthread_join(thread, NULL) && !ring_error);
    if (token) CHECK(md_session_note_off(&S, 3, MD_SOURCE_PANEL, token) == MD_OK);
    CHECK(command(MD_CMD_STOP, 0, 0, 0) == MD_OK && !run(64) && !run(64));
    CHECK(md_router_owned_count(&S.router) == 0 && voices() == 0);
    printf("  threaded: %u consistent snapshot reads, %u retried out\n", reads, busy);
    CHECK(reads > 0);
    return 0;
}

/* MD_CMD_BIND: a track switches to its alternate instrument and back at a block start */
static int alternate_binding(void)
{
    static fake_instrument_state_t alt_state;
    static md_instrument_t alt;
    empty_bank(&bank);
    CHECK(!setup(&bank));
    CHECK(md_instrument_init(&alt, &FAKE_INSTRUMENT_MONO, &alt_state, sizeof alt_state, MD_SAMPLE_RATE) == MD_OK);
    alt_state.gain = 17;
    md_session_config_t c = {.bank = &bank, .bpm_q8 = 120u * 256u};
    for (uint32_t t = 0; t < MD_TRACKS; t++) c.instrument[t] = &inst[t];
    c.alternate[0] = &inst[1];
    CHECK(md_session_init(&S, &c) == MD_ERR_ARGUMENT);          /* shares a primary's state */
    c.alternate[0] = &alt;
    CHECK(md_session_init(&S, &c) == MD_OK);
    md_note_token_t held, next;
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 60, 100, 0, &held) == MD_OK && !run(16) && fake[0].voice_count == 1);
    CHECK(command(MD_CMD_BIND, 0, 0, 1) == MD_OK && !run(16));
    CHECK(fake[0].voice_count == 0 && md_router_owned_count(&S.router) == 0);   /* released on the way out */
    CHECK(S.base[0][0] == 17);                                                   /* the alternate's bases */
    CHECK(md_session_note_on(&S, 0, MD_SOURCE_PANEL, 62, 100, 0, &next) == MD_OK && !run(16));
    CHECK(alt_state.voice_count == 1 && fake[0].voice_count == 0);
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, held) == MD_OK && !run(16) && alt_state.voice_count == 1);  /* stale */
    CHECK(command(MD_CMD_BIND, 0, 0, 0) == MD_OK && !run(16) && alt_state.voice_count == 0);
    CHECK(md_session_note_off(&S, 0, MD_SOURCE_PANEL, next) == MD_OK && !run(16));
    CHECK(command(MD_CMD_BIND, 1, 0, 1) == MD_OK && command(MD_CMD_BIND, 0, 0, 2) == MD_OK && !run(16));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.command_errors == 2);   /* no alternate; bad value */
    CHECK(md_router_owned_count(&S.router) == 0 && voices() == 0);
    return 0;
}

/* Parallel: a track rendered by a (fake) second core gives exactly the one-core result: the core taking each job
 * (mode 0), declining (1), a one-event queue so most blocks fall back to serial mid-way (2); and (3) a core that
 * is held before its 40th job: identical until then, and after it the track is panicked (no notes left owned)
 * and plays on, on one core. Modes 6 and 7: the tracks shared out anew every block by a wandering clock
 * (parallel_balance), 7 with a one-operation record. The fake runs the job inside run(), so this proves ordering,
 * not the absence of races. */
static int fake_mode, fake_runs, fake_fail_at;
static int fake_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (fake_mode == 1) return 0;                     /* no core: the session does the job */
    fake_runs++;
    if (fake_fail_at && fake_runs >= fake_fail_at) return 1;   /* a core held before it started: nothing done */
    fn(arg);                                          /* the job, now (a core that is instantly done) */
    return 1;
}
static int fake_wait(void) { return fake_fail_at && fake_runs >= fake_fail_at ? -1 : 0; }
static uint32_t fake_clock, fake_seed = 1;
static uint32_t fake_ticks(void)                      /* a counter whose steps wander: the tracks' costs with them */
{
    fake_seed = fake_seed * 1664525u + 1013904223u;
    return fake_clock += 1u + (fake_seed >> 24);
}
static md_session_t P;
static fake_instrument_state_t pfake[MD_TRACKS];
static md_instrument_t pinst[MD_TRACKS];
static int32_t pout[2u * 256u];
static int parallel_matches_serial(void)
{
    empty_bank(&bank);
    for (uint32_t k = 0; k < 16; k++) {               /* chords on track 2 (the parallel one), notes on 0 and 1 */
        bank.pattern[2][0].meta.mode = MD_PATTERN_CHORD;
        bank.pattern[2][0].step[k] = (md_step_t){.payload = (uint16_t)((48 + k) | (MD_CHORD_MINOR7 << 8)),
                                                  .velocity = 90, .gate = 70, .probability = 100};
        note(&bank, 0, 0, k, (uint16_t)(40 + k), 40);
        note(&bank, 1, 0, k, (uint16_t)(60 + k), 160);
    }
    bank.locks[0] = (md_lock_t){.step = (2u * 4u) * 64u + 3u, .target = FAKE_PARAM_GAIN, .value = 99};
    bank.lock_count = 1;
    for (fake_mode = 0; fake_mode < 8; fake_mode++) {   /* 4, 5: tracks 1 and 2 on core 1 (5 with a one-event queue) */
        uint32_t masks_seen = 0;
        fake_runs = 0;
        fake_fail_at = fake_mode == 3 ? 40 : 0;
        uint32_t failed_at = 0xffffffffu;
        uint32_t after_ons = 0;
        CHECK(!setup(&bank));
        md_session_config_t c = {.bank = &bank, .bpm_q8 = 120u * 256u, .parallel = {fake_run, fake_wait, 0}, .parallel_track = 2,
                                 .parallel_queue = fake_mode == 2 || fake_mode == 5 || fake_mode == 7 ? 1u : 0u,
                                 .parallel_mask = (uint8_t)(fake_mode >= 4 ? (1u << 1) | (1u << 2) : 0u)};
        if (fake_mode >= 6) { c.parallel.ticks = fake_ticks; c.parallel_balance = 1; }
        for (uint32_t t = 0; t < 4; t++) {
            CHECK(md_instrument_init(&pinst[t], &FAKE_INSTRUMENT, &pfake[t], sizeof pfake[t], MD_SAMPLE_RATE) == MD_OK);
            c.instrument[t] = &pinst[t];
        }
        CHECK(md_session_init(&P, &c) == MD_OK);
        md_command_t go = {.kind = MD_CMD_START};
        CHECK(md_session_submit(&S, &go) == MD_OK && md_session_submit(&P, &go) == MD_OK);
        md_note_token_t a, b;
        for (uint32_t block = 0; block < 400; block++) {
            if (block == 50) {                        /* live notes on the parallel track and another */
                CHECK(md_session_note_on(&S, 2, MD_SOURCE_PANEL, 90, 100, 0, &a) == MD_OK);
                CHECK(md_session_note_on(&P, 2, MD_SOURCE_PANEL, 90, 100, 0, &b) == MD_OK);
            }
            if (block == 90) {
                CHECK(md_session_note_off(&S, 2, MD_SOURCE_PANEL, a) == MD_OK && md_session_note_off(&P, 2, MD_SOURCE_PANEL, b) == MD_OK);
                md_command_t panic = {.kind = MD_CMD_EVENT};   /* a command: applied before the sequencer pass */
                panic.event = (md_event_t){.kind = MD_EVENT_SOURCE_RELEASE, .track = MD_ALL_TRACKS, .source = 0};
                CHECK(md_session_submit(&S, &panic) == MD_OK && md_session_submit(&P, &panic) == MD_OK);
            }
            uint32_t frames = 1u + (block * 97u) % 256u;
            for (uint32_t t = 0; t < MD_TRACKS; t++) { fake_trace_clear(&fake[t]); fake_trace_clear(&pfake[t]); }
            memset(out, 0, sizeof out); memset(pout, 0, sizeof pout);
            CHECK(md_session_render(&S, out, frames) == MD_OK && md_session_render(&P, pout, frames) == MD_OK);
            masks_seen |= 1u << P.par_now;
            if (failed_at == 0xffffffffu) {
                md_session_snapshot_t now;
                CHECK(md_session_snapshot(&P, &now) == MD_OK);
                if (now.parallel_failed) {               /* the failed block: the track panicked, nothing owned */
                    failed_at = block;
                    for (uint32_t n = 0; n < MD_MAX_OWNED_NOTES; n++) CHECK(!(P.router.notes[n].used && P.router.notes[n].track == 2));
                } else CHECK(!memcmp(out, pout, 2u * frames * sizeof *out));
            } else                                       /* and it plays on, serially */
                for (uint32_t k = 0; k < pfake[2].trace_count; k++) after_ons += pfake[2].trace[k].event.kind == MD_EVENT_NOTE_ON;
        }
        CHECK((failed_at != 0xffffffffu) == (fake_mode == 3));
        if (fake_mode == 3) CHECK(after_ons > 10u);
        md_session_snapshot_t snap;
        CHECK(md_session_snapshot(&P, &snap) == MD_OK);
        printf("  mode %d: %u blocks on the second core, %u done here, %u serial, failed %u\n", fake_mode,
               snap.parallel_blocks, snap.parallel_inline, snap.parallel_fallbacks, snap.parallel_failed);
        if (fake_mode == 0) CHECK(snap.parallel_blocks > 300 && !snap.parallel_failed);
        if (fake_mode == 1) CHECK(snap.parallel_inline > 300 && !snap.parallel_blocks);
        if (fake_mode == 2) CHECK(snap.parallel_fallbacks >= 10u && !snap.parallel_failed);   /* serial mid-way */
        if (fake_mode == 3) CHECK(snap.parallel_failed && snap.parallel_blocks == 39u);
        if (fake_mode == 4) CHECK(snap.parallel_blocks > 300 && !snap.parallel_failed);
        if (fake_mode == 5) CHECK(snap.parallel_fallbacks >= 10u && !snap.parallel_failed);
        if (fake_mode >= 6) {                           /* the tracks moved between the cores, and nothing was heard of it */
            uint32_t kinds = 0;
            for (uint32_t m = 0; m < 32u; m++) kinds += masks_seen >> m & 1u;
            printf("  mode %d: %u different shares of the tracks\n", fake_mode, kinds);
            CHECK(kinds >= 3u && snap.parallel_blocks > 300 && !snap.parallel_failed);
        }
        CHECK(snap.event_errors == 0 && snap.events > 100);
    }
    return 0;
}

/* Routed first, played afterwards: an instrument that refuses a played event can no longer tell the router, which
 * took it as done. The event is counted, and at the next block's start the track is panicked as the router does
 * with a backend fault (its notes gone from the books, the instrument reset); then it plays on. */
static int played_refusal_panics_the_track(void)
{
    empty_bank(&bank);
    for (uint32_t k = 0; k < 16; k++) {
        note(&bank, 0, 0, k, (uint16_t)(40 + k), 40);
        note(&bank, 2, 0, k, (uint16_t)(60 + k), 160);
    }
    fake_mode = 0; fake_runs = 0; fake_fail_at = 0;
    md_session_config_t c = {.bank = &bank, .bpm_q8 = 120u * 256u, .parallel = {fake_run, fake_wait, 0}, .parallel_track = 2};
    for (uint32_t t = 0; t < 4; t++) {
        CHECK(md_instrument_init(&pinst[t], &FAKE_INSTRUMENT, &pfake[t], sizeof pfake[t], MD_SAMPLE_RATE) == MD_OK);
        c.instrument[t] = &pinst[t];
    }
    CHECK(md_session_init(&P, &c) == MD_OK);
    md_command_t go = {.kind = MD_CMD_START};
    CHECK(md_session_submit(&P, &go) == MD_OK);
    md_session_snapshot_t snap;
    uint32_t block = 0, resets = pfake[2].reset_calls, ons = 0;
    for (; block < 200 && !pfake[2].voice_count; block++) {          /* until the parallel track holds a note */
        fake_trace_clear(&pfake[2]);
        CHECK(md_session_render(&P, pout, 128) == MD_OK);
    }
    CHECK(pfake[2].voice_count == 1);
    CHECK(md_session_snapshot(&P, &snap) == MD_OK && !snap.event_errors && !snap.backend_errors);
    pfake[2].trace_count = FAKE_TRACE_CAPACITY;                      /* the fake refuses its next event */
    for (; block < 400; block++) {
        CHECK(md_session_render(&P, pout, 128) == MD_OK);
        CHECK(md_session_snapshot(&P, &snap) == MD_OK);
        if (snap.event_errors) break;
    }
    uint32_t refused = snap.event_errors;                            /* (an off and the next on can share a block) */
    CHECK(refused >= 1u && !snap.backend_errors && pfake[2].reset_calls == resets);   /* counted; not yet panicked */
    fake_trace_clear(&pfake[2]);
    CHECK(md_session_render(&P, pout, 1) == MD_OK);                  /* the next block's start: the track panicked */
    CHECK(md_session_snapshot(&P, &snap) == MD_OK && snap.backend_errors == 1 && pfake[2].reset_calls == resets + 1u);
    for (uint32_t n = 0; n < MD_MAX_OWNED_NOTES; n++) CHECK(!(P.router.notes[n].used && P.router.notes[n].track == 2));
    CHECK(pfake[0].reset_calls == 0 || pfake[0].voice_count <= 1);   /* (the other track untouched) */
    for (uint32_t k = 0; k < 400; k++) {                             /* and it plays on */
        fake_trace_clear(&pfake[2]);
        CHECK(md_session_render(&P, pout, 128) == MD_OK);
        for (uint32_t i = 0; i < pfake[2].trace_count; i++) ons += pfake[2].trace[i].event.kind == MD_EVENT_NOTE_ON;
    }
    CHECK(md_session_snapshot(&P, &snap) == MD_OK);
    printf("  refused %u when played, then %u more; %u backend faults; %u note-ons after\n", refused, snap.event_errors - refused,
           snap.backend_errors, ons);
    CHECK(ons > 5u && snap.event_errors == refused && snap.backend_errors == 1);
    return 0;
}

static int guards(void)
{
    empty_bank(&bank);
    CHECK(!setup(&bank));
    CHECK(md_session_render(&S, out, 0) == MD_ERR_RANGE && md_session_render(&S, out, 257) == MD_ERR_RANGE);
    CHECK(md_session_render(&S, NULL, 16) == MD_ERR_ARGUMENT);
    CHECK(command(MD_CMD_BANK, 0, 1, 1) == MD_ERR_ARGUMENT && command(MD_CMD_SOUND, 0, 0, 1) == MD_ERR_ARGUMENT);
    CHECK(md_session_publish_bank(&S) == MD_ERR_STATE);  /* nothing staged */
    CHECK(md_session_stage_sound(&S, 4, 1, NULL, 0) == MD_ERR_ARGUMENT);
    CHECK(command(MD_CMD_TEMPO, 0, 0, 20u * 256u) == MD_OK && command(MD_CMD_LAUNCH, 0, 9, 0) == MD_OK);
    CHECK(command(MD_CMD_MUTE, 7, 0, 1) == MD_OK && command(99, 0, 0, 0) == MD_OK);
    CHECK(!run(16));
    md_session_snapshot_t snap;
    CHECK(md_session_snapshot(&S, &snap) == MD_OK && snap.command_errors == 4 && snap.bpm_q8 == 120u * 256u);
    md_seq_bank_t broken = bank;
    broken.pattern[0][0].meta.division = 99;
    md_session_config_t c = {.bank = &broken, .bpm_q8 = 120u * 256u};
    CHECK(md_session_init(&S, &c) != MD_OK);
    c.bank = &bank; c.bpm_q8 = 10;
    CHECK(md_session_init(&S, &c) == MD_ERR_RANGE);
    return 0;
}

int main(void)
{
    printf("session state %zu bytes, command %zu bytes\n", sizeof(md_session_t), sizeof(md_command_t));
    RUN(reserve_and_emergency_flush);
    RUN(refused_releases_never_strand);
    RUN(bank_edits_are_acknowledged);
    RUN(sound_handoff);
    RUN(locks_keep_ui_base_underneath);
    RUN(namespaces);
    RUN(panic_cuts_everything);
    RUN(stress);
    RUN(dense_chords);
    RUN(threaded_handoff);
    RUN(alternate_binding);
    RUN(parallel_matches_serial);
    RUN(played_refusal_panics_the_track);
    RUN(guards);
    return 0;
}
