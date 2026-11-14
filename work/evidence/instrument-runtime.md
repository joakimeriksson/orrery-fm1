# M1-03 — Instrument runtime and source-owned router

Owner: agent-a. Date: 2026-11-05. Status: submitted for cross-review.

Implemented portable registry/descriptor validation, caller-owned init, event admission, additive render/reset
and versioned sound operations in core/instrument.c. Private processor/event checks are shared with the router;
they do not call hardware or a host. Initialization validates capabilities, callbacks, parameter ranges/enums,
preset payloads, kit IDs, registry uniqueness, sample rate, alignment and memory capacity before DSP init.
Per-event checks use the already accepted descriptor, not whole-registry validation in audio.

core/router.h/.c owns up to128 logical notes with independent sources and physical-token/pitch bookkeeping.
Poly duplicate pitches share a physical voice, including after its initial logical owner releases; admission
rejects excess distinct held pitches. Mono retains multiple keys, prefers live/MIDI over sequence and resumes
surviving notes. Source-scoped sustain/release, stale logical offs, token collision/wrap, track isolation and
panic have bounded behavior. A processor fault after validated admission resets that track and clears its
ownership instead of leaving stuck notes. Session/effect panic remains an M3 responsibility.

The API is single-context: token allocation and note admission occur in the serialized/audio owner, not from
unsynchronized UI producers. Frontend raw input commands must be dequeued before allocating/admitting tokens;
key-to-token mapping lives with that owner and retains the original destination until key-up. No successful
queue insertion or sample-accurate input integration is claimed by this router alone.

Validation:

```sh
python3 tests/run_native.py instrument router --sanitize on --build-dir build/native-agent-a
```

PASS with ASan/UBSan and strict C11 warnings. Instrument suite: registry/metadata rejection, init guards,
capability/range/source admission without DSP mutation, settings version/size/range roundtrip, additive render
bounds and reset. Router suite: live/sequence same-pitch release, physical token lifetime, mono override/resume,
source-isolated sustain/release, global logical pool and poly capacity, token wrap, track/input isolation,
mono legato physical-token replacement and controlled backend failure recovery. Seven router scenarios and
four instrument scenarios passed. Existing contracts/fixtures suites are unchanged.

No legacy module wiring, imported engine DSP, effects implementation, clock or project codec changed. The
registry/instrument API is now implemented; real adapter validation and processor timing remain separate tasks.
New renderer calls outside1..256 frames or on uninitialized handles are safe no-ops. Native tests do not establish
firmware CPU/RAM cost; M3 integration must measure it.
