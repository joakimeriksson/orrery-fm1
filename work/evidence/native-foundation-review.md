# Native foundation review

Reviewer: agent-a. UTC snapshot: 2026-11-05T23:01:20+00:00. Local project date: 2026-11-06.

Implemented and cross-reviewed: portable instrument/effect registries and caller-owned instances, source/token
routing with sustain/mono-priority/duplicate-pitch ownership, actual ARP/Eminent/MiniPops adapters, exact musical
transport, bounded four-track pattern playback and the new five-recipe ElectronicFX instrument. Current source
API2 adds generic parameter readback; settings/project wire schemas remain1. M1-08 was accepted by agent-a after agent-b submitted;13tasks are reviewed DONE with no pending claims.
This report records independent review evidence, not self-acceptance. Live progress remains in WORK.json.

Independent final validation:

```sh
python3 tests/run_native.py --sanitize on --build-dir build/native-final-review
python3 tests/work_tracker_check.py
python3 tests/native/runner_check.py
```

All **nine native suites pass** under strict C11, address and undefined-behavior sanitizers without recovery or
suppression: adapters, contracts, effect, electronic_fx, fixtures, instrument, router, seq and transport.
Tracker checks:8passed, including competing claims. Runner checks:3passed, including failure propagation.
All32 task headings/dependencies/resources/file scopes match between EXECUTION.md and WORK.json.

Native source fingerprints for this tested checkpoint are in native-foundation-sources.json (53files).
Compiler commands are emitted by the native runner, with test dependencies declared in each C suite; build
outputs are under ignored build/native-final-review. The snapshot is not a git revision: this repository still
has noHEAD and no commits were made. Imported arithmetic fixes match upstream; see engine-arithmetic.md for
standalone checks, exact hashes and6bit-identical before/after raw renders.

ElectronicFX references and inspected plots are linked from electronic-fx.md. These are unnormalized synthetic
sound recipes, not reproductions of a specific recording. Clock/seq tests prove event timing/ownership in native
harnesses, not physical hardware jitter or target block load. Legacy module/orrery.c/module.cfg have not been
switched to these new portable components. Audio-session/mixer/DSP/panel/project/MIDI integration and actual
composing workflows remain their named gates, along with explicit ARP accent/legato/slide support in M1-05.
No real FM-1 deployment or hardware test was attempted.

The next useful parallel pair after parameter-readback acceptance is M1-05 (upstream ARP performance controls)
and M3-02 (fixed mixer). Coordinate imports so tests do not inspect a moving engine tree. M3-01 session work
then uses source-safe sequence priorities and retained base snapshots; no host-side opaque-payload decoding.
