# Decision 001: sequencer reuse audit

Task: M0-02. Owner: agent-b. Status: accepted by agent-a after source comparison and rerunning the native probe and both NOR tests; this decision does not mark the core implemented.
Date: 2026-11-05. Source revision: `165d177a425944f3d30b0b669255a6ed0050c3a5` (upstream commit date 2026-11-02,
“Merge release/1.6.1 into next”). Checkout: ignored `build/melodee-audit`.

## Decision and its limits

Proceed with the portable meloDock instrument/event/effect contracts and a small four-track core. Reuse the
proven musical-unit/remainder arithmetic and selected step/ownership behavior as reviewed source or test
references, preserving attribution. Do **not** port the complete Melodee firmware or silently fork its entire
sequencer/recorder/project stack. Freeze the API before deciding exactly which functions to carry across.

This recommendation follows a source audit and a successful **limited extraction**, not a full reimplementation
comparison. It does not prove a full extraction impossible, estimate total project effort, or establish sound,
CPU/RAM or hardware parity. The evidence supports choosing a boundary and bounded scope; implementation gates
must still prove a useful workstation. If priorities shift to full Melodee functionality or its larger recorder,
integrating these instruments into Melodee remains the preferable path to evaluate rather than recreating it.

## What was inspected

All source links below are pinned to this exact commit. The local checkout was not edited.

| Area | Source and observed coupling | Reuse consequence |
| --- | --- | --- |
| Compilation/ownership | [melodee.c, lines 78–134](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/melodee.c#L78) includes engines, params, voices, mixer, USB, song, sequencer, audio, UI and project in one translation unit. [core.h, lines 569–572](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/core.h#L569) provides static `song`/`trk` globals. | A native harness already exists, but compiling it is not equivalent to extracting a reentrant session library. |
| Instrument boundary | [core.h, lines 330–374](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/core.h#L330) has a genuine engine descriptor, with per-voice callbacks receiving `track_t`, `voice_t` and modulation data. [engines.c](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/engines.c) manages state by engine IDs/resources. | Existing abstraction is useful precedent, not the proposed whole-instance caller-owned instrument contract. Porting our engines there requires adapting their own polyphony, filters and buffers to its voice/runtime model. |
| Internal clock | [mix.c, lines 66–106](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/mix.c#L66) maps samples to 24576 musical units per quarter, carrying remainder through bounded 32-frame chunks. | Small portable arithmetic seam demonstrated unchanged; retaining its finer integer resolution is an option, not a reason to discard it in favor of a novel clock. |
| Pattern dispatch | [seq.c, lines 985–994](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/seq.c#L985) releases with live/ARP/record ownership guards; [seq_step, lines 1099–1164](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/seq.c#L1099) emits to `trk_note_on/off`, handles notes, ties, rests, accent, slide, lane hits and chance. | Good extraction seam into an injected sink, demonstrated below. The sink currently identifies notes by pitch, so meloDock's source/token protocol still needs adaptation. |
| Sequencer/transport | [seq.c](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/seq.c) also contains keyboard scales, arpeggiator, recording, count-in, source routing, audition, erase, motion and MIDI control/clock includes. [mix_block, lines 344–370](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/mix.c#L344) calls `events_block(n)` before rendering a span. | A full portable sequencer import needs global/context separation and explicit render-offset delivery; exact musical phase does not establish sample-offset synthesis. Upstream swing fixtures themselves measure block-exact timing. |
| Patterns | [pattern.c, lines 1–65](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/pattern.c#L1) uses bounded compressed inactive banks, per-track staging, RAM/noinit/pool placement and serialized mutations. [core.h, lines 379–438](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/core.h#L379) defines four-note steps and eight drum lanes. | Useful bounded staging/admission ideas; capacity/layout differs from our 14-hit drum payload and four-pattern bank budget. Wholesale import adds a second project representation. |
| Recording | [recording.c, lines 1–94](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/recording.c#L1) owns 1024 shared timed-note records, onset/capture indexes, reference counts, shared heaps and cached schedule state. [recorded_note.h](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/recorded_note.h) defines a nine-byte runtime entry, explicitly separate from wire layout. | Far richer independent-event recording than v1's one note/recognized chord per step. Keeping it would require new storage/runtime budgeting and UI, not just replacing a note callback. |
| Song | [song_chain.c, lines 102–109 and 218–249](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/song_chain.c#L102) derives ordinary section duration from track one's pattern; rests/HOLD use a four-beat fallback. Includes pattern storage and song sound snapshots. | Our independent global-bar sections need an explicit semantic change. HOLD/REST/seek tests are valuable regression references. |
| Mixer/effects | [mix.c](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/mix.c) includes shared effects/punch, per-track and global caches/buffers, resource/audio worker hooks, master gain and output protection. [fx.c](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/fx.c) also includes host-platform EQ and SLOOP-derived processors. | Importing the mixer duplicates responsibilities now assigned to meloDock and dock; DSP extraction can be considered independently, with measured memory/timing. |
| Projects | [project.c, lines 17–100](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/project.c#L17) and [pattern_store.c](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/pattern_store.c) combine engine-native patches, timed recording, legacy migrations, songs and a larger bank container. | Does not fit our 8176-byte project schema unchanged. Engine and workflow migration cost remains even if sequencer code is reused. |
| Flash storage | [storage.c, lines 1–68](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/storage.c#L1) has injected read/erase/program hooks, shared pool allocation, addresses/object IDs and commit-last safe saves; includes streaming. | A separable and tested subsystem exists. Its flash map/allocator cannot be installed inside a dock module owning only bank services; retain algorithms/tests as references or propose atomic host storage separately. |
| Tests | [hostsim.c, lines 14–86](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/tests/hostsim.c#L14) composes engines/mixer/USB/song/seq with stubs; recording and song fixtures include broader MIDI/UI fixtures. [run_tests.sh](https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/tests/run_tests.sh) expects generated build tables/artifacts for the full suite. | Existing tests materially reduce risk for Melodee, but they are not a standalone portable sequencer test package. Only the exact checks listed below were executed. |

For scale only, `seq.c` 1569 lines + `recording.c` 983 + `song_chain.c` 267 + `pattern.c` 362 +
`pattern_store.c` 606 + `mix.c` 371 + `project.c` 1646 + `storage.c` 561 + `storage_stream.c` 146 +
`core.h` 614 + `midi_clock.c` 243 = **7368 lines**, excluding many included dependencies. This is a coupling
map, not a claim that every line must be rewritten or a time estimate.

## Extraction experiment and test results

Native toolchain: Apple clang 21.0.0, arm64-apple-darwin25.6.0; `cc -std=c11 -O1 -Wall -Wextra -Werror` for
our extraction probe. The generator extracts these actual function bodies without editing them:
`int_clock_units` from mix.c, `step_chance`/`step_gate` from core.h, `seq_release`/`seq_step` from seq.c.
It supplies narrowed track/song structs, a note sink, a fake no-op recorder/observer and a controlled RNG.
This replaces dependencies for an experiment; it is not a whole-source compile with all real dependencies.

Passed checks:

- Seven tempos (40, 73, 97, 120, 137, 173, 240), five render partitions (1, 17, 64, 255, 256), ten minutes plus
  137 frames each: exact integer total phase equals the independent 64-bit arithmetic oracle.
- Real extracted step dispatch produces four-note chords, ties without retriggering, rest releases and eight
  simultaneous drum lanes. Live-held ownership suppresses a sequencer release; probability-silent steps emit no new note.
- Unmodified upstream `storage_test.c` passes against its simulated NOR backend.
- Unmodified upstream `storage_stream_test.c` passes, reporting 245 program cutoffs, 453 page/header-byte
  cutoffs, unpublished snapshots, allocated erase boundaries, write protection and stale publication.

The first probe-generator attempt accidentally matched a forward declaration and produced a `motion.c` missing
include compile failure. The generator was corrected to match definitions followed by `{`; the final successful
probe above is the relevant result. No upstream code was changed to make it pass.

Not executed/proven: the complete upstream test suite, full seq_tick/song/recorder extraction, full note token
semantics, sample-offset audio scheduling, actual jitter, target DSP CPU/RAM, audio renders, and hardware tests.
The injected sink sees note dispatch, not a reconstructed whole-song sample timeline. Native NOR fault tests
prove that upstream backend's behavior, **not** the dock bank API's power-loss safety.

## Retain versus replace

| Path | Retain | Adapt or replace | Decision |
| --- | --- | --- | --- |
| Whole Melodee port under dock | Existing music features and their tests | Hardware/USB/audio/MIDI ownership, flash allocator/map, memory sections, globals, output processing, engine adapters, platform-dependent UI/tests | Reject for this dock-native scope; it carries two platform ownership models through the boundary. |
| Extract its sequencer stack | Clock arithmetic, note-dispatch/tie behavior, recorder algorithms, many test expectations | Context/global ownership, pitch-only sinks, eight-lane steps, chance RNG, bar semantics, render offsets, patch/project format, UI and recorder capacities | Technically plausible, but the narrow probe is not evidence that the complete coupled stack is inexpensive. Do not claim it as already extracted. |
| Engines into Melodee | Complete sequencer/recorder/song/UI/storage/platform, most existing validation | Four engine integrations and adapting their voice/instance model, state size, patch pages/persistence and performance budgets | Strong alternative if full Melodee workflow/capacity is the actual objective. It changes the dock-composition objective. |
| Small meloDock core with selective reuse | Existing engines, dock platform, proven clock/step algorithms and source-backed regression cases | Portable contracts/session, small project model, renderer, recording subset, panel, local effects integration | Recommended for the agreed bounded v1; must earn success through native and musical workflow gates. |

Concrete differences requiring behavior changes: chance uses the shared `rng()` also consumed by arpeggiator
logic (`seq.c:339,347,1115`; generator in `libc.c:122`), so unrelated random activity affects step outcomes;
upstream eight-lane hits cannot represent our fourteen MiniPops voices; ordinary song section length follows
track one; the core exports no source/token event sink; full recording capacity conflicts with our small
wire schema. None is a defect in Melodee's own design. They explain why a direct stack port is not our contract.

At M1, first keep a production-sized fake instrument/session harness independently compilable. If importing
additional Melodee functions, document their pinned provenance, exact adaptations and tests alongside the
portable boundary. If this expands into carrying its recording/pattern/project stack, stop and reopen the reuse
choice with a prototype and measured budgets rather than pretending the original decision still covers it.

## Licence and attribution check

Read `LICENSE`, `LICENSING.md`, upstream `AGENTS.md` and the headers of inspected/extracted sources.
The inspected sequencer/storage functions are marked GPL-3.0-only; seq.c/mix.c retain both Kerem Kilic (Ellic
Studio) and the applicable Leo Kuroshita/Hügelton Instruments portions. core.h and recording.c identify Kerem
Kilic. The generated probe preserves the mixed-file notices for the extracted functions. Our existing engine
headers also declare GPL-3.0-only; licence difference is not the architectural reason for this decision.

This is a source-notice inventory, not a blanket assertion about every asset. Upstream documents distinct
third-party engine/asset licences, including native factory sound data with reserved vendor rights. No such
factory assets, installer, platform or DSP engine sources were imported into this runtime. Any future copy
must inventory its own dependencies/notices and provide corresponding source/licence material as applicable.

## Reproduction and artifact fingerprints

Fetch the pinned source into ignored build storage (no real-device commands):

```sh
gh repo clone keremimo/melodee build/melodee-audit -- --depth 1
git -C build/melodee-audit fetch origin 165d177a425944f3d30b0b669255a6ed0050c3a5
git -C build/melodee-audit checkout --detach 165d177a425944f3d30b0b669255a6ed0050c3a5
mkdir -p build/melodee-audit-results
```

If the clone already exists, use its fetch/checkout steps only. Restore `probe.py` from the final Python fence
in this report, then run it. The appendix makes the ignored artifacts regenerable; they are not assumed durable.
For example, from the repository root:

```sh
python3 -c 'from pathlib import Path; p=Path("work/decisions/001-sequencer-reuse.md"); Path("build/melodee-audit-results/probe.py").write_text(p.read_text().split("```python\n")[-1].split("```",1)[0])'
python3 build/melodee-audit-results/probe.py
cc -O1 -Wall -Wno-unused-function build/melodee-audit/tests/storage_test.c -o build/melodee-audit-results/storage_test
build/melodee-audit-results/storage_test > build/melodee-audit-results/storage_test.log
cc -O1 -Wall -Wno-unused-function build/melodee-audit/tests/storage_stream_test.c -o build/melodee-audit-results/storage_stream_test
build/melodee-audit-results/storage_stream_test > build/melodee-audit-results/storage_stream_test.log
```

The source manifest is SHA-256 of the listed original source files; regenerate by hashing those paths at the
pinned revision. Artifact hashes at audit completion:

| Artifact in `build/melodee-audit-results/` | SHA-256 |
| --- | --- |
| `probe.py` | `2af2960cb4b60b7a67ff00d0a6d4e548f05f69d37b3733e3491abd5760a9d2bc` |
| `probe.c` | `b496d9733ae86472fe027884f5de1efee078df5eb059169db02e21c316bbdfc9` |
| `probe.log` | `f150fbe84cb5a2d5884d882776f1c53e15c3e0a5e88b4bd7c38b4d332a7a2459` |
| `storage_test.log` | `04b1c4a60a393b26df2dd871da28e01b49a4e1e1dc331124bbdfb64652ef4b75` |
| `storage_stream_test.log` | `a102ab9a54bbee0b779cf59f9ea56214c5bd9e15eb3f4cb4fc12b1238b8cb6d9` |
| `sources.sha256.json` | `2b7b5e5af7fcd0b8207ccef2a13df08799259b3b31c396e44c775a130f720ce1` |

## Reproducible limited-probe generator

```python
#!/usr/bin/env python3
"""Regenerate and run the limited, source-extracted Melodee audit probe."""
from pathlib import Path
import hashlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'build/melodee-audit/firmware/src'
OUT = ROOT / 'build/melodee-audit-results'
PIN = '165d177a425944f3d30b0b669255a6ed0050c3a5'
assert subprocess.check_output(['git', '-C', str(SRC.parents[1]), 'rev-parse', 'HEAD'], text=True).strip() == PIN

def function(file, name):
    source = (SRC / file).read_text()
    match = re.search(r'^static[^\n]*\b' + name + r'\([^\n]*\)\s*\{', source, re.M)
    assert match, name
    brace = source.index('{', match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

prelude = r'''/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Audit-only harness supplies narrowed structs, fake recorder/observer and event sink.
 * It is not an extraction of the whole sequencer or a production adapter. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FS 44100u
#define CTL 32u
#define MIDI_BEAT_UNITS 24576u
#define NSTEP 64u
#define NLANE 8u
#define G_BPM 0
#define P_SGATE 0
#define P_SLEN 1
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RECORDED 4u
enum { ST_NOTE, ST_TIE, ST_REST };
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc, probability; } step_t;
typedef struct {
    int16_t p[2]; step_t step[NSTEP];
    uint32_t seq_n, seq_hold, slide_glide, seq_off, seq_idx, arp_note;
    uint8_t seq_notes[4 + NLANE];
} track_t;
static struct { int16_t g[1]; } song;
static uint32_t int_clock_rem;
static uint32_t live_held[1][4];
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 42, 46, 41, 45, 49, 51};
static int events[256]; static uint32_t nevents, observed, seed = 1;
static uint32_t rng(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static uint32_t trk_index(const track_t *t) { (void)t; return 0; }
static int recording_owns(const track_t *t, uint32_t n) { (void)t; (void)n; return 0; }
static uint32_t recording_step_skip(track_t *t, const step_t *s, uint32_t skip)
{ (void)t; (void)s; return skip; }
static const step_t *seq_steps(track_t *t) { return t->step; }
static void trk_note_on(track_t *t, uint32_t n, uint32_t vel)
{ (void)t; assert(vel <= 127 && n <= 127 && nevents < 256); events[nevents++] = (int)n + 1; }
static void trk_note_off(track_t *t, uint32_t n)
{ (void)t; assert(n <= 127 && nevents < 256); events[nevents++] = -(int)n - 1; }
static void chord_auto_observe(track_t *t, uint32_t n) { (void)t; (void)n; observed++; }
'''
prelude = prelude.replace('static uint32_t int_clock_rem;\n', '')
code = prelude + 'static uint32_t int_clock_rem;\n'
for file, name in [('mix.c', 'int_clock_units'), ('core.h', 'step_chance'),
                   ('core.h', 'step_gate'), ('seq.c', 'seq_release'), ('seq.c', 'seq_step')]:
    code += function(file, name)
code += r'''
static uint64_t clock_run(uint32_t frames, uint32_t partition)
{
    uint64_t total = 0; int_clock_rem = 0;
    while (frames) { uint32_t n = frames < partition ? frames : partition;
        total += int_clock_units(n); frames -= n; }
    return total;
}
int main(void)
{
    const uint32_t bpms[] = {40, 73, 97, 120, 137, 173, 240};
    const uint32_t parts[] = {1, 17, 64, 255, 256};
    uint32_t frames = FS * 600u + 137u;
    for (uint32_t b = 0; b < 7; b++) {
        song.g[G_BPM] = (int16_t)bpms[b];
        uint64_t exact = (uint64_t)frames * MIDI_BEAT_UNITS * bpms[b] / (FS * 60u);
        for (uint32_t p = 0; p < 5; p++) assert(clock_run(frames, parts[p]) == exact);
    }
    puts("clock: seven tempos x five partitions x ten minutes, exact integer phase PASS");
    track_t t = {0}; t.p[P_SLEN] = 4; t.p[P_SGATE] = 64; t.arp_note = 255;
    step_t s = {.note={60,64,67,72}, .n=4, .time=ST_NOTE, .vel=100};
    seq_step(&t, &s, 6000, 0);
    assert(nevents == 4 && t.seq_n == 4 && t.seq_off == 3000 && observed == 4);
    assert(events[0] == 61 && events[3] == 73);
    s.time = ST_TIE; seq_step(&t, &s, 6000, 0);
    assert(nevents == 4 && t.seq_off == 6000);
    s.time = ST_REST; seq_step(&t, &s, 6000, 0);
    assert(nevents == 8 && !t.seq_n && events[4] == -61);
    puts("step sink: four-note chord, tie without retrigger, rest releases PASS");
    nevents = 0; s = (step_t){.time=ST_NOTE, .hit=255, .vel=90};
    seq_step(&t, &s, 6000, 0); assert(nevents == 8 && t.seq_n == 8);
    seq_release(&t); assert(nevents == 16 && !t.seq_n);
    puts("step sink: eight simultaneous drum lanes and releases PASS");
    nevents = 0; s = (step_t){.note={60}, .n=1, .time=ST_NOTE, .vel=90};
    live_held[0][60 >> 5] = 1u << (60 & 31);
    seq_step(&t, &s, 6000, 0); seq_release(&t); assert(nevents == 1);
    live_held[0][60 >> 5] = 0;
    s.probability = 101; seq_step(&t, &s, 6000, 0); assert(nevents == 1);
    puts("step sink: live ownership guard and probability-silent step PASS");
    puts("LIMIT: no seq_tick/song/recording/MIDI/render-split extraction tested");
    return 0;
}
'''
path = OUT / 'probe.c'
path.write_text(code)
subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(OUT / 'probe')], check=True)
result = subprocess.check_output([str(OUT / 'probe')], text=True)
(OUT / 'probe.log').write_text(result)
print(result, end='')
print('probe.c sha256:', hashlib.sha256(path.read_bytes()).hexdigest())
```
