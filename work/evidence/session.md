# M3-01 audio session and bounded command handoff

Task M3-01, owner `claude`, 2026-11-06 UTC. Native implementation only: no module wiring (M3-04), no real
effects (M3-03), no target measurement (M3-05). Apple clang 21.0.0, macOS arm64.

| File | SHA-256 |
| --- | --- |
| core/commands.h | 041f29fa9026d0989ede983c0f25b3008ac9abfb774c9310bc68b1b8e1d77318 |
| core/commands.c | 2467e9ccd0240bbd19d44c7af783f62265dad0a33242cd11bd7c127c1baf4813 |
| core/session.h | fa0ff2b45da0045494514418d7b0c563ddde8b3ac4e0e7313d5234d887bfd8d0 |
| core/session.c | 67a3584e08b9632e8d28e642b7034137da3e36b0296070cbdd44717a6986f635 |
| tests/native/session_test.c | 40e78e49f4d4dc9e88b537605afed0e1bca7ccdb314ee012399c1ff3a938b587 |

## Callback contexts

Read from fm1-harbor-proc `firmware/dock/hb_module.h` and `firmware/src/dock.c`: `render` runs in the audio
interrupt; `init`, `ui`, `note`, `cc`, `midi`, `draw` and `data` all run in the main loop (MIDI and the panel are
dispatched from main-loop code such as `play_note` and `smk_send`). So there is exactly one producer context and
one consumer context on one core, and the interrupt preempts main, never the reverse. That is what makes a
single-producer single-consumer ring valid here. A second core or a producer inside render is not supported.

Shared words are aligned 32-bit (or byte) values with one writer each. `MD_FENCE` orders payload against
publication. The device build must define `MD_SINGLE_CORE`, which makes it a compiler barrier: D0-01 found the
SDK lowers C11 atomics to a missing CAS helper. Native builds use a full fence so the threaded test means
something on a weakly ordered host.

## What exists

- **Command ring** (`core/commands.*`). 128 slots of 32 bytes. Ordinary commands stop at 96; note-off, source
  release, panic, stop and sustain-up may use the last 32. Producer-side counters: rejected, release refused,
  high-water.
- **Emergency flush.** A refused note-off or source release increments that source's flush counter (main is its
  only writer). Render snapshots the counters first, drains the commands queued at its start, then releases every
  flagged source. A flush therefore always follows every command queued before it, so it cannot leave a
  later-admitted note of that source stuck. A note it releases early only ends sooner; its late off is a
  harmless no-op.
- **Session** (`core/session.*`). Owns router, transport, sequencer, mixer, two sequencer banks, per-track
  render buffers and base-value tables. Render order per block: flush snapshot, command drain (all at offset 0,
  FIFO, at most 128), flushes, then the sequencer iterator with each track rendered up to every event offset,
  then mixer and transport. Equal-offset sequencer order is the sequencer's own (restore, lock, release,
  trigger, legato cleanup).
- **Edits are acknowledged.** `edit_bank` hands main the non-live bank (a fresh copy of the live one).
  `publish_bank` validates it and queues the swap; until audio writes the acknowledgement, `edit_bank` returns
  NULL and a second publish is refused, so main can never write a bank audio reads. A pattern mode change on a
  playing track releases that track's sequenced notes. Sounds use one 512-byte staging slot the same way:
  validated by main, applied by audio after a track panic, base table re-read, then acknowledged.
- **Parameter targets** use decision 002's namespaces: instrument, inserts 1/2, mixer track, echo, reverb,
  master. Others are refused. UI edits set base and DSP. While a sequencer lock holds the same target the UI
  edit only moves the base, and the lock's restore applies it (decision 002: "UI base edits remain underneath
  the lock"). Locks and restores never change the base.
- **Panic** stops sequencer and transport, panics every track and resets all effect history. Stop releases
  only sequencer sources, so live notes and tails continue.
- **Live tokens** are allocated by main from 0x80000001 upward; the router's sequencer tokens count up from 1.
- **Snapshot.** Audio publishes transport, per-track pattern/cursor/launch state, mute/solo masks, mixer
  meters and counters (commands, errors, last error, events, per-block maxima, flushes, sequencer faults and
  drops, owned notes, router rejections and backend errors) under a sequence counter. Main retries a read that
  overlapped a write, up to 16 times, then reports MD_ERR_STATE.
- **State size.** `sizeof(md_session_t)` = 55,096 bytes natively, including the 12,816-byte mixer, two
  6,568-byte banks, 8 KiB of track buffers and the 4 KiB ring. Instrument and effect state is separate.

## Validation

    python3 tests/run_native.py session --sanitize on --build-dir build/native-claude-session
    python3 tests/run_native.py --sanitize on --build-dir build/native-claude-all

Both pass with ASan and UBSan, no recovery: the session suite's scenarios (eleven after revision), and all twelve native suites.

| Scenario | Proves |
| --- | --- |
| reserve_and_emergency_flush | 96 note-ons accepted, the 97th and a param refused; 32 offs use the reserve; the 33rd requests a flush; one block later 0 owned notes and 0 voices; a late off is a no-op; a flush releases only its own source |
| bank_edits_are_acknowledged | staging is never the live bank and edits leave it bit-identical; no second edit before the ack; invalid bank refused without queuing; the published edit plays; a mode change releases the track |
| sound_handoff | invalid and wrong-version payloads refused before queuing; one in flight; applied, base re-read, old notes released |
| locks_keep_ui_base_underneath | lock 10 on the DSP, UI 90 stays underneath, the next step restores 90; without a lock UI reaches DSP at once; stop restores base |
| namespaces | mixer level and echo parameter routed; namespace 7, missing insert and out-of-range refused and counted; mute, solo, return level, master bypass reach the mixer |
| panic_cuts_everything | live and sequenced notes cut, transport stopped, echo history cleared, nothing restarts |
| stress | 6,000 blocks of random spans 1..256 with mixed panel/MIDI notes, params, 48 locks, mutes, launches, tempo, start/stop/continue and ring-overflowing bursts: 4,684 refused offs became flushes; at the end 0 owned notes, 0 voices, empty ring, no backend errors or sequencer faults |
| dense_chords | four tracks of four-note chords on every 32nd at 240 BPM with 48 locks and live notes for 11.6 s: 12,046 events, at most 34 per block; rejections counted, none silent; all released |
| threaded_handoff | 200,000 commands across real threads arrive in order; 12.7 million snapshot reads while audio renders on another thread are all consistent (samples = blocks x 64) |
| guards | span limits, direct BANK/SOUND submission refused, bad commands counted, bad config refused |

Mutation check: applying flushes before the drain, letting a UI edit override an active lock, and dropping the
emergency flush request were each caught by the suite.

## Limits and next steps

- Commands are untimestamped and apply at offset 0 of the next block: live input has up to one block of
  latency. Timestamped input needs the host work in M6-01.
- Instrument and effect bindings are fixed at init. Rebinding (electronic FX replacing a part, project load)
  belongs to M4-01 and M5-02 and will need its own staged handoff.
- A refused STOP (ring completely full) returns MD_ERR_CAPACITY; the caller retries. Catch-up after a clock gap is
  bounded by the sequencer (16 visits, 512 events per block); the session adds no catch-up of its own.
- Not built for the target. Its stack use, the pi32v2 cost of the fences and 55 KB of state are M3-04/M3-05
  measurements.

## Revision after review

reviewer-claude-2 returned the task with two blocking findings, both reproduced by its probe:

1. A sustain-up refused at a full ring raised no flush, so a pedal-held note could stay on.
2. The per-source flush counter was a byte that main incremented on every refusal; 256 refusals before audio
   ran wrapped it back to "nothing pending" and the flush was lost.

Fixes: requests are now coalesced. Main raises a source's request only when none is pending
(request = seen + 1); audio completes it after the release (seen = request). A refused sustain-up raises its
source's flush, and the router's source release clears that source's sustain. Following the reviewer's
non-blocking note, a refused stop or global panic now raises a coalesced stop or panic request that audio
applies after the drain; only a refused per-track panic is left for its caller to retry.

New scenario refused_releases_never_strand covers the pedal case, 512 refusals for one source, a refused stop
and a refused global panic. Mutations restoring the wrapping counter or dropping the sustain flush both fail
it. All twelve native suites pass with ASan and UBSan.

| File | SHA-256 after revision |
| --- | --- |
| core/session.h | 70f33d9c5a01fcefe9e7467ef9161d3b958da759e59bb17a8a4469fa0949fded |
| core/session.c | 55427411d357b272d3cc35017b0b73f4381c0ed94363a725c207144c98ea970d |
| tests/native/session_test.c | d5e69c4b4265015fbcc737dc40aa7e1ff7d84cda656a3f08c6f797f249173ff2 |
