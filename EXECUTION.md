# Execution and two-agent workflow

PLAN.md is the architecture. WORK.json is the authoritative live task ledger; this document specifies deliverables.
The ledger records which work has actually passed review; these task descriptions do not imply completion.
The first pair is M0-01 and M0-02 in parallel; later sessions resume from the ledger and evidence, not this
initial assignment. Planning/tracker infrastructure alone is not proof that an implementation gate has passed.

## Startup, ownership and review

1. Read AGENTS.md, PLAN.md and this file; run `python3 tools/work.py status`.
2. Choose a stable unique owner name (for example `agent-a` and `agent-b`). Read your own handoff in
   `work/agents/` and the last evidence/history for any interrupted task. Do not infer completion from chat.
3. Claim exactly one ready task: `python3 tools/work.py claim M0-01 --owner agent-a`. Agent B can independently
   claim M0-02. Claims check prerequisites and resource conflicts under an OS lock; failure means choose another
   ready task or help review. A planned lane is a suggestion, not an exclusive right to a task.
4. Work inside the task's file scope and resource labels. Before touching shared files, add/agree the scope in
   the ledger while no conflicting claim is active; a label is not an automatic file-access sandbox. Never edit
   another active task's implementation or add a conflicting resource after claiming. Do not hand-edit WORK.json
   while a tracker command/other agent is using it. Structural task changes are serialized by the coordinator.
5. Record a concrete summary, exact next action and evidence as work progresses, before switching tasks and
   before ending a session: `python3 tools/work.py note M0-01 --owner agent-a --summary "..." --next "..."
   --validation "command, result, artifact path"`. Include failed checks and limitations, not just passing checks.
6. Run relevant checks, publish evidence, then `python3 tools/work.py submit M0-01 --owner agent-a`. Status is
   REVIEW, not DONE. The other agent reviews actual changes/evidence and runs a focused check, then accepts with
   `python3 tools/work.py accept M0-01 --reviewer agent-b --validation "review evidence"`. Keep review short so
   tasks do not sit waiting while prerequisites block both agents. For corrections use `revise ID --reviewer NAME
   --reason "changes needed"`; this returns the task to ACTIVE with its original owner and evidence preserved.
7. For a real blocker, use `block ID --owner NAME --reason "..."`; retain a precise next action. A blocked task
   is not a completed prerequisite and still reserves its resources. `release ID --owner NAME --reason "handoff..."` returns active/blocked work
   to TODO with history preserved. Submit review work only when it is actually ready. The reviewer should not
   accept tasks they own. Do not forcibly steal claims after a timeout; recover only after checking the prior
   agent is stopped and recording an explicit coordinator handoff.
8. At session end write your own handoff file (template below), then refresh status. On resume compare the log
   with actual files and rerun any invalidated check before continuing. Never commit unless the user asks.

Commands serialize read/check/write with a `.work.lock` file and atomic ledger replacement. This protects claims,
not source files. Use one shared checkout initially: this repository currently has no HEAD commit and its files
are untracked, so isolated worktrees are not assumed available. Preserve all existing user files. Evidence should
reference file hashes and commands until commits exist. If later using separate worktrees, both agents must point
`--file` at the same absolute WORK.json; two copied ledgers cannot coordinate. All resulting changes still need
integration against one tested checkout. Do not remove a lock file while processes may hold its OS lock.

## File ownership and integration

Agent A normally takes transport, sequencer, router, session, song, recorder and codec. Agent B normally takes
adapters, effects, mixer, UI, storage integration and qualification. Review each other's work. Only M3-04 owns
`module/orrery.c` and `module/module.cfg` for the initial migration; later integrations must explicitly claim
`module-integration` in addition to their own resource. Per-feature tests live under distinct native/integration
files. Changes to shared headers use `contracts`; test runner edits use `native-harness`; changes to PLAN.md use
`plan`; engine imports use `imports`. Additional shared edits need a separate task/claim, not informal overlap.

Keep core events, state publication and sound/project versions reviewed before dependents code against them.
No agent can silently break a frozen interface: record the proposed decision in work/decisions, name affected
IDs, update the contract, and only then migrate consumers. Project schema/feature changes must recalculate the
bank/RAM budget. A mixed working tree must not be reported as verified if another agent changed it during tests;
record relevant source hashes and coordinate a quiet integration window for emulator/stress qualification.

## Milestones and checkpoints

| Checkpoint | Required result | Safe next parallel work |
| --- | --- | --- |
| M0 | Reproducible current baseline and source-backed reuse decision | Contract design; no speculative sequencer rewrite |
| M1 | Reviewed API, fake harness, router and existing adapters | Clock/sequencer versus effects/FX voice |
| M2 | Partition-independent clock/events and portable processors | Session integration versus mixer/effects |
| M3 | Four-part vertical slice with local FX and measured cost | Project editing/song versus panel implementation |
| M4 | Device-only composing, recording, arrangement and undo | Codec/transfer versus storage/workflow verification |
| M5 | Recallable project, validated backup and honest fault behavior | Content plus MIDI integration |
| M6 | Tested host timing contract and source-aware external sync | Release qualification |
| M7 | Complete factory songs, documented behavior and evidence | Owner-authorized hardware validation |

M6-01 can proceed earlier when its dependencies are done; internal-clock musical work does not wait for it.
A milestone is complete only when all its required tasks are DONE. Before calling the instrument API reusable,
add a fake/new instrument without modifying sequencer/song/recording code. Before calling the sequencer good,
complete the panel scenarios and timing/ownership tests, not only a demo render. Before calling a project safe,
state exactly which interrupted writes it survives.

## Evidence and handoffs

Store small reports in `work/evidence/` and architectural decisions in `work/decisions/`. Keep large WAVs/PNGs,
logs and build artifacts under ignored `build/`; reports link paths, commands, sizes and content hashes so lost
artifacts can be regenerated. Label historical, freshly measured, estimated, blocked and hardware-unverified
claims distinctly. Record native compiler, host/toolchain versions and context for performance results.

Each agent owns only their handoff file, for example `work/agents/agent-a.md`:

```text
Updated (UTC):
Owner / task ID / task status:
Current result and changed paths:
Validation commands, results and evidence:
Failed checks / blockers:
Exact next action:
Uncommitted files and integration risks:
Question or review requested from other agent:
```

No duplicated status board is maintained: query WORK.json for live status. Handoffs explain how to resume, while
history explains why ownership/status changed. The task details below are planned scope, not claims of progress.

## Task details

### M0-01 — Capture the current baseline

Suggested lane: A. Depends on: none. Resources: baseline.

Paths: `work/evidence/baseline.md`.

Record source hashes, host/engine revisions or hashes, toolchain and exact build/emulator commands; distinguish old reported numbers from new measurements.

Acceptance: Reference trace/render and code/RAM/worst-block measurements are linked, or unavailable checks have a precise blocker; preserve current source without assuming a git commit exists.

### M0-02 — Audit Melodee reuse and record the core decision

Suggested lane: B. Depends on: none. Resources: reuse-decision.

Paths: `work/decisions/001-sequencer-reuse.md`.

Pin upstream commit; inspect transport, patterns, recorder, song, mixer, storage and tests. Try a native extraction seam and compare extraction, engine integration there and a new core.

Acceptance: Decision contains source references, dependency map, experiment results, licence/attribution findings and retained/replaced subsystem comparison; reviewer can reproduce the recommendation.

### M1-01 — Freeze source API and wire contracts

Suggested lane: A. Depends on: M0-01, M0-02. Resources: contracts, plan.

Paths: `core/instrument.h`, `core/event.h`, `core/param.h`, `core/effect.h`, `work/decisions/002-contracts.md`, `core/project_format.h`.

Define structs, IDs/versions, capability bits, statuses, event tokens, context ownership, sample format, state alignment, automation target ranges and maximum resource limits. Include project region layouts before implementation.

Acceptance: Both agents review the headers; every promised step/section/lock/effect field fits the draft budget; instrument/event/effect interfaces compile without hb_module.h; unresolved decisions are explicit blockers.

### M1-02 — Build a portable native harness and fake processors

Suggested lane: B. Depends on: M1-01. Resources: native-harness.

Paths: `tests/native/`, `tests/run_native.py`.

Compile portable C with the system compiler; fake instrument/effect record events and check buffers; provide sanitizers when supported, deterministic traces and a single test entry point.

Acceptance: Fake processors exercise event return codes, independent instance storage, additive/replace buffer rules and unsupported capability handling; compiler/test failures propagate nonzero.

### M1-03 — Implement registry and source-owned note routing

Suggested lane: A. Depends on: M1-02. Resources: router.

Paths: `core/instrument.c`, `core/router.c`, `core/router.h`, `tests/native/router*`, `core/processor_internal.h`, `core/instrument_internal.h`, `tests/native/instrument_test.c`, `work/evidence/instrument-runtime.md`.

Registry, caller-owned initialization, note tokens, source release, mono priority, duplicate pitch ownership, sustain and panic with bounded pools.

Acceptance: Panel/MIDI/sequence at the same pitch cannot release each other; stale offs, source disconnect, sustain and pool exhaustion leave no stuck voices.

### M1-04 — Adapt existing ARP, Eminent and MiniPops engines

Suggested lane: B. Depends on: M1-02, M1-06. Resources: adapters.

Paths: `instruments/arp.*`, `instruments/eminent.*`, `instruments/minipops.*`, `tests/native/adapters*`, `instruments/builtin_ids.h`, `instruments/adapter_common.h`, `work/evidence/adapters.md`.

Expose descriptors, curated/full parameters, preset selection and sound encode/validate/apply. Disable internal sequencing and map stable drum IDs.

Acceptance: Two ARPs are independent, unsupported velocity/slide is honest, sounds round-trip, and segmented rendering is compared against reference output with stated tolerance.

### M1-05 — Add explicit upstream ARP performance controls

Suggested lane: B. Depends on: M1-04. Resources: upstream-engines, adapters, imports.

Paths: `take.py`, `engines/arp_engine.*`, `work/evidence/arp-performance.md`.

Design explicit accent/legato/slide API upstream; verify standalone modules, then import with take.py. Record upstream/local hashes. Work outside this checkout needs normal filesystem approval.

Acceptance: Accent and slide audibly/structurally work through the adapter; upstream regression commands and results are recorded; velocity is not misused as an accent bit.

### M1-06 — Fix inherited engine arithmetic under sanitizers

Suggested lane: A. Depends on: M1-02. Resources: upstream-engines, imports.

Paths: `engines/arp_engine.c`, `engines/mp_engine.c`, `work/evidence/engine-arithmetic.md`.

Correct confirmed undefined signed shifts upstream without changing intended fixed-point arithmetic; import through take.py and preserve upstream/local hashes. Coordinate a quiet engine-test window.

Acceptance: Upstream standalone checks and no-recovery sanitized adapter renders pass; imported copies match upstream; audio changes are compared explicitly and no sanitizer is disabled.

### M1-07 — Preserve pitched ownership when hybrid HIT capacity is full

Suggested lane: A. Depends on: M1-03. Resources: router, contracts, plan.

Paths: `core/router.c`, `core/router.h`, `tests/native/router_test.c`, `work/decisions/002-contracts.md`, `work/evidence/hybrid-hit-capacity.md`.

Hybrid electronicFX can be full of held pitched voices when a HIT arrives. Preserve owners on nonmutating HIT/CAPACITY; retain fail-safe track flush for other unexpected backend failures.

Acceptance: A full hybrid fake backend rejects HIT visibly without DSP/ownership mutation; notes release normally afterward, rejection is counted, and actualbackendfault flush regression still passes underASan/UBSan.

### M1-08 — Add generic parameter readback for preset/base snapshots

Suggested lane: B. Depends on: M1-03, M1-04, M2-02, M2-03, M2-04. Resources: contracts, router, effect-runtime, adapters, electronic-fx, native-harness, plan.

Paths: `core/param.h`, `core/instrument.h`, `core/instrument.c`, `core/effect.h`, `core/effect.c`, `instruments/arp.c`, `instruments/eminent.c`, `instruments/minipops.c`, `instruments/electronic_fx.c`, `tests/native/fake_processors.c`, `tests/native/instrument_test.c`, `tests/native/effect_test.c`, `tests/native/adapters_test.c`, `tests/native/electronic_fx_test.c`, `work/decisions/005-parameter-readback.md`, `work/evidence/parameter-readback.md`, `PLAN.md`.

Opaque sound payloads cannot initialize generic base parameter snapshots from descriptorsinitial after loading presets. Add pure owning-context readback, keep model base separate from active locks, and amend source API version explicitly. Settings/project wire versions remain1.

Acceptance: Every exposed parameter returns loaded/edited current value through generic wrappers; unknown IDs/NULL outputs/backend errors leave caller output unchanged; readonly fields readable. All native suites pass ASan/UBSan with updated descriptors; no host code decodes engine structs or opaque sound payloads.

### M2-01 — Implement one musical transport

Suggested lane: A. Depends on: M1-02. Resources: transport.

Paths: `core/transport.*`, `tests/native/transport*`, `work/evidence/transport.md`.

96 PPQN, remainder accumulation, wrapping sample time, tempo phase preservation, straight/swing scheduling, start/stop/continue and deterministic seek state.

Acceptance: Long runs stay within one-sample mapping error; all supported divisions, odd-length swing, tempo changes and wrap pass partition-independent event tests.

### M2-02 — Implement reusable effect instances

Suggested lane: B. Depends on: M1-02. Resources: effect-runtime.

Paths: `core/effect.c`, `tests/native/effect*`, `work/evidence/effect-runtime.md`.

Registry/instances, in-place insert versus wet-only bus rules, bypass, parameter smoothing, tail/reset behavior and versioned settings validation.

Acceptance: Instance state never aliases, bypass/tails behave as specified, input/output buffer rules and short spans pass native checks; no hardware or host dependency.

### M2-03 — Implement patterns and event generation

Suggested lane: A. Depends on: M2-01, M1-03. Resources: sequencer, contracts, plan.

Paths: `module/seq.*`, `tests/seq_check.py`, `tests/native/seq*`, `work/evidence/sequencer.md`, `PLAN.md`, `work/decisions/002-contracts.md`.

Four tracks/four patterns/64 steps; mono/chord/hit masks, gates/ties/slide flags, chance, 48 locks, launch quantization and bounded release scheduling.

Acceptance: Trace equivalence for 1/17/64/255/256 and irregular blocks; rests/skips/ties/locks restore correctly; simultaneous hits and rejected out-of-range notes are covered.

### M2-04 — Build the electronic FX instrument

Suggested lane: B. Depends on: M1-04, M1-07. Resources: electronic-fx.

Paths: `instruments/electronic_fx.*`, `tests/native/electronic_fx*`, `work/evidence/electronic-fx.md`.

Fixed-point DROP/PING/BUBBLE/CHIRP/METAL recipes, pitch/amplitude envelopes, resonant excitation and bounded FM/ring modulation. Existing track binding, not an extra track.

Acceptance: Presets render, decay to silence, survive max simultaneous triggers/stealing, share no RNG state and preserve timing across block partitions; reference WAV/spectrograms are linked.

### M3-01 — Implement audio session and bounded command handoff

Suggested lane: A. Depends on: M2-03, M1-04, M1-08. Resources: session.

Paths: `core/session.*`, `core/commands.*`, `tests/native/session*`.

Sole audio writer, actual-context-safe queues, edit acknowledgement, snapshots, event merge/order, span rendering, reserved release capacity and emergency source flush.

Acceptance: Stress dense input/locks/releases; no lost note-offs or unbounded catch-up, edits cannot reuse live buffers, and overflow behavior is observable and tested.

### M3-02 — Implement the fixed mixer graph

Suggested lane: B. Depends on: M2-02, M1-04, M1-08. Resources: mixer.

Paths: `core/mixer.*`, `tests/native/mixer*`.

Two optional inserts/track, post-level sends, dry plus wet-only echo/reverb returns, master filter, smoothed gain/pan, wide accumulation and clipping meters.

Acceptance: Impulse/routing tests prove insert/send order, no unintended dry duplication or bus feedback; muting preserves return tails and worst-level signals cannot wrap.

### M3-03 — Implement filters, phaser and local echo/reverb

Suggested lane: B. Depends on: M3-02. Resources: effect-processors, imports.

Paths: `effects/`, `tests/native/processors*`, `work/evidence/effects.md`.

Reusable fixed-point LP/BP filter and phaser, bypassable master sweep, echo feedback tone and local reverb. Extract shared host DSP through a documented upstream path instead of silently forking copies.

Acceptance: Stable resonance/feedback limits, smoothing, bypass/tail/reset, denormal-free integer decay and short-span behavior pass checks; memory and per-processor CPU costs are recorded.

### M3-04 — Switch the module to the portable session

Suggested lane: A. Depends on: M3-01, M3-03, M1-05. Resources: module-integration.

Paths: `module/orrery.c`, `module/module.cfg`, `tests/integration/`.

Keep current UI initially while replacing engine globals/clock wiring with adapters/session; local musical reverb, host room-off request outside render, capability checks and visible overrides.

Acceptance: Build regular dock and seqdock; demo plays four synchronized parts, internal engine sequencers stay off, sample-offset chord/hit transitions pass integration checks.

### M3-05 — Measure the complete audio vertical slice

Suggested lane: B. Depends on: M3-04, M2-04. Resources: performance.

Paths: `work/evidence/audio-budget.md`, `tests/stress/`.

Measure graph variants, maximum strings/lead/bass/dense drums, FX replacement, cold/short spans and local effects. Define supported combinations before claiming capacity.

Acceptance: Code/BSS/RAM-code/stack/queue/delay-line use recorded; supported stress case <=70% worst block and ten minutes no underruns/stuck notes/wrap, or scope is revised and retested before release.

### M4-01 — Implement editable project model and undo

Suggested lane: A. Depends on: M3-04, M1-08. Resources: project-model.

Paths: `core/project.*`, `core/edit.*`, `tests/native/edit*`.

Bounded model, copy/paste/clear, length/division/swing, launches/mute/solo, capacity admission, one-level undo and stable effect automation targets.

Acceptance: Atomic edit publication, max-size admission, lock ownership, pattern-boundary behavior and undo preserve valid state; no direct UI writes to DSP.

### M4-02 — Build instrument, FX, LIVE and STEPS pages

Suggested lane: B. Depends on: M4-01. Resources: panel, ui-tests, module-integration.

Paths: `module/ui.*`, `tests/integration/panel*`, `module/orrery.c`, `module/module.cfg`.

Descriptor-backed pages, named alternate rows, LEDs/header, track FX bindings, two sends, master sweep performance controls, modifiers and cursor preservation.

Acceptance: Every promised control has a documented physical path; page/octave changes retain correct key-up ownership; mute/solo/launch and FX editing work without accidental actions. Panel admission keeps the module inside the measured audio budget (work/evidence/audio-budget.md, M3-05): no insert graph, voice count or drum density outside the supported set is reachable, and a refused choice says why.

### M4-03 — Implement song sections and deterministic seeking

Suggested lane: A. Depends on: M4-01. Resources: song.

Paths: `core/song.*`, `tests/native/song*`.

32 sections, pattern/HOLD/REST, bars/transpose/mutes, section launch, loop/stop and seek reconstruction independent of track lengths.

Acceptance: Polymetric traces, HOLD phase, REST releases, seek from cursor, loop boundaries and changing sections during held notes pass native tests.

### M4-04 — Implement step entry and quantized recording

Suggested lane: A. Depends on: M4-01, M4-03. Resources: recording.

Paths: `core/record.*`, `tests/native/record*`.

One armed track, count-in, step/overdub/replace pass, release-derived gate, chord recognition, drum merge, swung-grid separation and undo.

Acceptance: Quantize midpoint/wrap, end-of-pass releases, unsupported chord feedback, rapid source changes and pass undo are correct without stuck notes or hidden input truncation.

### M4-05 — Complete SONG/REC workflow and musical scenarios

Suggested lane: B. Depends on: M4-02, M4-03, M4-04, M3-05. Resources: panel, ui-tests, module-integration.

Paths: `module/ui.*`, `tests/integration/workflows*`, `work/evidence/workflows.md`, `module/orrery.c`, `module/module.cfg`.

Song/record pages plus complete bass/chord/drum/lead composition, FX performance, copy/undo and arrangement scenarios.

Acceptance: Another agent reproduces all scenarios using only device controls and documented mappings; cursor/launch/record state is clear and interactions do not collide.

### M4-06 — Sound library and editor

Suggested lane: B. Depends on: M4-01, M4-02. Resources: panel, sound-library, module-integration.

Paths: `instruments/arp.*`, `module/sounds.*`, `module/orrery.c`, `tests/native/sound*`, `tests/integration/panel*`.

Twenty sounds a part for the lead and bass (SOFT LEAD, the ten factory patches, nine new ones) with descriptive names only; a descriptor-backed sound editor with named rows, editing while playing, one-level undo; edited sounds saved with the project. The shared user library waits for the storage decision in PLAN.md.

Acceptance: Every sound loads, plays and is measured inside the audio budget; no player-visible name is an artist or record title; every editable parameter has a documented panel path; edits while playing never click or strand notes; undo restores the exact sound; a project round trip preserves an edited sound byte for byte.

### M5-01 — Implement the exact project codec

Suggested lane: A. Depends on: M4-01, M3-03, M2-04, M4-03, M4-04. Resources: project-codec.

Paths: `core/project_codec.*`, `tests/native/codec*`, `work/decisions/003-project-format.md`.

Explicit LE encoding, versions/stable IDs, sounds/effects directory, checksums and validated load staging; prove every fixed region with maximum populated projects.

Acceptance: Max payload <=8176 (target 8096), round-trip settings/notes/effects exact, malformed lengths/references/versions rejected without changing current project; all supported edits remain saveable.

### M5-02 — Implement stopped save/load and settings migration

Suggested lane: B. Depends on: M5-01, M3-04. Resources: module-storage, module-integration.

Paths: `module/storage.*`, `tests/integration/storage*`, `module/orrery.c`, `module/module.cfg`.

SAVE pending while running, coherent snapshot, stopped erase, failure/dirty UI, factory fallback, old settings migration and optional indexed-bank capability checks.

Acceptance: Full bank/error/corrupt load/power-cycle paths preserve documented behavior; no autosave or autoplay; current erase-in-place limitation is visible and no last-good recovery claim is made.

### M5-03 — Implement computer backup and restore

Suggested lane: A. Depends on: M5-02. Resources: transfer, module-integration.

Paths: `module/transfer.*`, `tools/project.py`, `tests/native/transfer*`, `module/orrery.c`, `module/module.cfg`.

HB_F_DATA bounded import staging and project export protocol/tool; validate lengths, ordering, retries, checksum and destination compatibility.

Acceptance: Export/import round-trip is reproducible, duplicate chunks safe, oversized/out-of-order data rejected, interrupted transfer leaves the playing/current project intact.

### M5-04 — Validate persistence and interrupted-write behavior

Suggested lane: B. Depends on: M5-03. Resources: persistence-review.

Paths: `work/evidence/persistence.md`, `tests/integration/storage_faults*`.

Fault injection for save/transfer and boot fallback; document backup procedure. If last-good recovery is added, separately specify/reserve two banks or an atomic host service.

Acceptance: Corrupt-save fallback and backup restore pass; previous-project survival is claimed only if the actual atomic mechanism passes interrupted-write tests.

### M6-01 — Specify and implement host timing extensions

Suggested lane: A. Depends on: M2-01, M3-01. Resources: host-midi.

Paths: `work/decisions/004-host-midi.md`, `work/evidence/host-midi.md`.

Separate fm1-harbor work: source-tagged timestamped real-time/system-common input, sample mapping, deadline output queue and transport priority. Obtain normal filesystem approval for external writes.

Acceptance: Host revision and patch/PR reference recorded; byte-level tests cover F8/FA/FB/FC/F2 and running status, capacity/fallback policy and old-host behavior are documented.

### M6-02 — Implement source-aware MIDI controls and external sync

Suggested lane: B. Depends on: M6-01, M3-04. Resources: module-midi, module-integration.

Paths: `module/midi.*`, `tests/integration/midi*`, `module/orrery.c`, `module/module.cfg`.

Channel routing through events, program/CC/bend/pressure/sustain, clock follower with bounded phase correction, source selection, timeout and no feedback loops.

Acceptance: Per-source cleanup, F2/Continue position, clock loss, jitter/drift and switching tests pass per transport; unavailable transports are explicitly unverified, not certified by emulator results.

### M7-01 — Author factory projects and public documentation

Suggested lane: A. Depends on: M4-05, M5-04. Resources: content, public-docs.

Paths: `presets/`, `README.md`, `docs/`.

Demo plus two complete factory songs including a strings sweep and electronic-FX passage; document exact API, controls, capacities, storage and supported effects combinations.

Acceptance: Factory projects validate/load and demonstrate the complete workflow; documentation matches implemented behavior and makes no Melodee file/feature parity claim.

### M7-02 — Run release qualification and publish remaining limits

Suggested lane: B. Depends on: M7-01, M6-02, M3-05. Resources: release-verification.

Paths: `work/evidence/release.md`.

Run native/integration suites and regular dock/seqdock stress paths, verify measurements and known limitations with artifact hashes.

Acceptance: All mandatory gates pass with reproducible commands/results and source hashes; hardware remains untested until owner authorizes deployment, and that limit is explicit.


### D0-01 — Audition synth and sequencer together in fm1-emu

Suggested lane: A. Depends on: M1-04, M1-08, M2-01, M2-03, M2-04. Resources: audition, module-build.

Paths: `audition/`, `tools/render_seq_demo.py`, `tools/run_seq_demo.py`, `tests/native/audition_test.c`, `work/evidence/synth-seq-audition.md`.

User-requested playable integration test now, ahead of full workstation integration. Preserve legacy module. Four parts plus FX replacing lead, basic fixedgain drymix, sample-offset event splitting, PLAY/STOP and minimalpanel, reproducible native/emulator artifacts.

Acceptance: Native real-synth sequence passes sanitizers, audible mixed/stem outputs and exactevent trace; targetmodule builds and fm1-emu validates audio/transport/controls/nooverruns within measuredbudget; launcher and image are locally runnable. No physical deployment or fullworkstation/effectsgraph claim.
