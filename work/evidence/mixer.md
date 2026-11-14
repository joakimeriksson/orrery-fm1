# M3-02 fixed mixer graph

Task M3-02, owner `claude`, 2026-11-06 UTC. Native implementation only; no module integration, effect DSP or
target measurement. Apple clang 21.0.0, macOS arm64.

| File | SHA-256 |
| --- | --- |
| core/mixer.h | 9f0b263857aaadb82ee84174ff4ec243476a42f2e557d145176649610a6105dc |
| core/mixer.c | 14b8abd1e2f84e045bacd438dc923d5b67bfb6fdd642d22916a76da47e4f9a89 |
| tests/native/mixer_test.c | 47f73f4393714b03473c069a8f26a305cda302e6763308e12e10e89cd0fecf20 |

## What exists

`core/mixer.*` implements the PLAN.md graph: per track insert 1 -> insert 2 -> level/pan/mute/solo -> dry mix,
stereo echo and reverb sends taken post-insert and post-fader, wet-only bus returns with their own levels, and
a master insert slot (bypassed by default) over dry plus returns. The mix is saturated to Q15, counted, and
added to the caller's buffer like an instrument's render_add.

- **Span API.** `begin(frames)`, `add_track` per rendered track (processed in place), `finish(out)`. A track
  not added in a span is processed as silence when it has inserts or moving gains, so effect tails and ramps
  advance with time rather than with calls. A track may be added once per span.
- **Smoothing.** Level, pan, sends, returns and insert/master crossfades use a per-sample one-pole on Q23 gains
  (time constant 256 samples, about 5.8 ms; snaps when the step rounds to zero, about 2,700 samples for a full
  step). Being per-sample, output is identical for every render partition.
- **Pan law.** Balance: centre is unity on both sides; moving right attenuates left linearly to zero. This keeps
  stereo instruments (Eminent ensemble) at unity when centred. Q15 32767 is treated as exact unity for levels,
  pan factors, sends and returns, so a default chain is bit-transparent.
- **Headroom.** Every stage boundary clamps to +-2^24 (+48 dB over Q15 full scale) and counts clamps: track
  input, each insert output, bus input, bus output, the dry+returns sum and the master output. Products are
  64-bit multiply then shift, the same pattern as the engines' MUL macro; there is no division in mixer.c.
  Four tracks plus two returns stay below 2^27 before the master clamp.
- **Bypass.** A slot fades between dry and wet. When the fade reaches dry the slot stops calling its processor;
  re-enabling resets the processor first, so dormant history never returns as a stale echo.
- **Mute.** Mute and solo move the post-fader gain to zero smoothly. Sends are post-fader, so the muted track's
  sends fade too while existing returns drain. Mute does not stop the instrument; releasing sequenced notes is
  the router/session's job.
- **Ownership.** Effect instances are caller-owned. One instance or state block may occupy one slot only;
  inserts/master need INSERT capability and buses need BUS. Configuration is refused inside a span. Tempo is
  forwarded to assigned effects and applied to later assignments. Panic resets all effect history; reset (project
  replacement) also settles every gain and crossfade and clears meters.
- **Meters.** Per-track post-fader peak, per-return peak, master peak before saturation, output clip count and
  internal clamp count. Readable and clearable; publication to the UI is the session's job.
- **State size.** `sizeof(md_mixer_t)` = 12,816 bytes, of which 12,288 are six 256-frame stereo work buffers.

## Validation

    python3 tests/run_native.py mixer --sanitize on --build-dir build/native-claude-mixer
    python3 tests/run_native.py --sanitize on --build-dir build/native-claude-all

Both pass with ASan and UBSan, no recovery: the mixer suite's eight scenarios, and all eleven native suites.

| Scenario | Proves |
| --- | --- |
| insert_order_and_exact_unity | x2 then +50 gives 250, swapped slots give 300; empty chain is bit-exact |
| sends_are_post_fader_and_wet_only | half level hard right: dry once at frame 0, echo return once at frame 4, reverb once at 6, nothing else; reverb input equals only the direct send |
| mute_keeps_return_tails | echo sent before mute returns at frame 60; muted dry fades monotonically; bus input stops; output ends silent |
| worst_level_cannot_wrap | INT32_MAX, INT32_MIN and alternating inputs, every insert/bus/master emitting extremes: output exactly +-32767, clip and clamp counters set, no sanitizer report |
| smoothing_and_partition_independence | all smoothers ramping with inserts, buses and master active: identical output for spans 1, 17, 64, 255, 256 and an irregular sequence; level step is monotonic, steps at most 1/256 of the signal and settles exactly |
| bypass_crossfade_and_no_stale_history | a latching insert fades out, goes idle and is not called; re-enabled it is reset and the old value never returns; fade-in rises monotonically |
| panic_unadded_tracks_solo_and_tempo | panic drops a pending echo; a skipped track's insert delay emits on time; solo/mute interplay; tempo reaches existing and later effects |
| guards | parameter ranges, capability, one-slot-per-instance, uninitialised effect, span discipline, aliasing mixer storage |

Mutation check, each compiled from a modified copy in the session scratchpad and run against the same test:
pre-fader sends, echo return feeding the reverb send, a missing input clamp (UBSan signed overflow), no reset
on re-enable, skipping unadded tracks and no smoothing. All six were caught.

## Limits and next steps

- Not measured on the target. The pi32v2 cost of the per-sample 64-bit multiplies, and of six work buffers, is for
  M3-05. A settled track skips its smoothers.
- Replacing an audible insert cuts the old one; callers bypass and wait for `md_mixer_insert_idle` to swap cleanly.
- No real effects yet (M3-03); tests use purpose-built fakes in mixer_test.c. No session wiring (M3-01/M3-04).
- Mixer automation targets (namespace 0x3) use `md_mixer_track_params`; the session decodes namespaces.

## Review and follow-up

Accepted by reviewer-claude, 2026-11-06. Its probe found that two instances whose state blocks partly overlap
were both accepted, because the one-slot check compared state pointers for equality only. Fixed after
acceptance: the check now tests overlap using each descriptor's state_bytes, and the guards scenario covers a
second instance starting 16 bytes into the first. All eleven native suites pass again with ASan and UBSan.
New hashes: core/mixer.c d303fc71a2a99a420d5540cba1ebd5d4c030b2ceaa3d18df244272e986f61083, tests/native/mixer_test.c df0608e6736500bfa0e0b0b07815f5f128cd5b01e0bf76fc74629bc3472b1383.

Open notes from review: inserts and buses can receive up to +48 dB over full scale, so M3-03 effects must
tolerate that range or the mixer must narrow it. md_mixer_insert_idle reports 1 for a newly assigned slot
before its first span; its wet level is zero then, so this is harmless.
