# M2-03 — Portable pattern sequencer

Owner: agent-a. Date: 2026-11-05. Status: submitted for agent-b review.

module/seq.h/.c implements four tracks/four patterns/up to64steps, independent timing/length/swing/transpose,
mono/chord/drum events, gates/ties/mono slides, seeded probability,48sparse parameter locks, mute, next-bar and
immediate launches, start/stop/continue, and a bounded sample-offset iterator. No engine/hardware/host/storage
function is called. Native test entry point is tests/seq_check.py. Song arrangement, recording, project codec,
UI, real session render splitting and pattern preset conversion remain later tasks.

## Contracts and scheduling

The bank is validated before use; it stays immutable during each iteration. Validated edits may be published
between iterations; a shortened pattern clamps a now-hidden cursor before reading the next step. Binding/mode
replacement needs the session's source release/reconciliation, not concurrent bank mutation. Event targets use
absolute musical positions, so tempo changes do not retain obsolete sample deadlines. Steps at the block end
are deferred. Each track's visit parity continues across odd-length loops; seeded chance is a pure function of
projectseed, track, pattern and64-bitvisit, independent of render partition, token allocations or other RNG.

The iterator uses one bounded timestamp batch, not a full song/block allocation: <=128oldoffs,56newtriggers,
8lockoperations and12sourceflushes, capacity204. A five-bucket stable ordering emits restore,lock,release,trigger,
then legato cleanup. The last phase is an explicit exception to usual off-before-on: a mono slide's new note
must reach the router while its predecessor still exists. After that, the old logical token is released at
the same sample. Only SLIDE is emitted (it implies the required transition); separate LEGATO capability is not
invented. No surviving predecessor means ordinary note-on with no slide flag. Chord slide is rejected in v1.

Ties extend only the most recent note/chord group, not every older overlapping tail. Lookahead holds the
predecessor to the next tie/slide boundary even if its original gate was short. Rest/probability skip releases
that track's sequence source. Mono retriggers release prior sequence owners; chord gates can overlap in the
bounded pending-note pool. Drum masks produce up to14simultaneous hits; drum accent means velocity127.
Out-of-range transposed pitches and failed token admission are counted/dropped, never wrapped into other notes.

Source releases use source IDs0..3, not PANIC, so live/MIDI notes survive stop/mute/restart/launch. The session
pairs seq stop/continue with transport controls. Locks restore current model base at each visited step (even
rest/skipped steps), apply independently of probability, and restore on mute/stop/launch. Base callback failure
uses a cached pre-lock fallback and increments diagnostics; a new lock without a valid base is skipped. The
provider must read retained base values, not the DSP value currently overridden by a lock. Parameter namespaces
are decoded by the future session, not sent raw into an instrument wrapper.

Next-bar launches select the strictly future global4/4bar. Immediate launch rounds forward to the next musical
hundredth-tick boundary (at most7samples at40BPM), rather than waiting for a new render block. Stopped pattern
selection is immediate and survives subsequent start. Callers drain an iteration before editing/restarting.
Catch-up is capped at16stepvisits/block (normal operation needs at most8including launches) and512events;
even a muted/silent clock gap cannot spin unboundedly. On an unexpected iterator failure the caller must release
all four sequence sources; the sequencer also schedules a source-flush batch and stops. Actual session fault
handling/host clock timestamp mapping are not implemented by this unit.

Native64-bit size observations: bank6568B, sequencer7872B, transport20B, event24B. They are not target linker/
stack measurements or wire layout; the separate8096-byte project budget is unchanged. No per-block allocation.

## Validation

```sh
python3 tests/seq_check.py --sanitize on --build-dir build/native-seq-checked
python3 tests/run_native.py seq --sanitize on --build-dir build/native-seq-reviewed
```

Clock plus sequence suites PASS strictC11ASan/UBSan, no recovery. Eight sequence scenarios cover:

1. All six divisions with partitions1/17/64/255/256/irregular; pitches/cursors and sounded-event sample offsets
   agree with an independent rational oracle through looping patterns.
2. Four-note minor-seventh expansion, short-gate tie continuation and rest release, mono slide new-on-before-
   old-off ordering, and56 simultaneous drum hits across four tracks.
3. Probability decisions independent of partitions/unrelated token allocation; swing75 with a three-step loop
   retains alternating phase; negative transposed pitch is rejected rather than wrapped.
4. Lock80 restores a panel-updated base55 before a rest-step lock20; stop restores55. Failed base reads restore
   the cached base/skip a new lock and record diagnostics instead of leaving the old override sticky.
5. Quantized launch lands at the next bar; immediate launch lands within its current block; stopped selection
   is not silently discarded by start.
6. Actual router/fakeinstrument integration: a same-pitch live owner survives sequence stop/source release.
7. Slide without a predecessor becomes normal note-on; shrinking length below cursor cannot play a hidden step.
8. Invalid bank/global-owner/flag inputs reject, token exhaustion counts a dropped note, reentrant calls fail,
   and a huge clock jump while all tracks are muted stops at the step budget and produces a bounded flush.

The implementation emits neutral events. Native fake-router tests do not prove target block load, actual ARP
accent/slide sound, effect parameter routing, song seek or device composing workflow. Those gates remain explicit.
