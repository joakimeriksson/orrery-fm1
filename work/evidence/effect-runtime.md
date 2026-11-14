# M2-02 — Reusable effect runtime

Owner: agent-b. Date: 2026-11-05. Status: submitted for agent-a review.

Implemented core/effect.c against the accepted effect.h contract, reusing the existing read-only common
processor validation helper. No DSP processors, host calls, allocations, clock, mixer graph or legacy wiring
were added. Descriptors/state remain caller-owned; each real mixer slot must supply disjoint state rather
than relying on a global instance allocator. This wrapper introduces no shared runtime pools.

Registry validation checks stable ID uniqueness, common parameter/version/rate/alignment metadata, nonempty
known INSERT/BUS capabilities, required callbacks and the matching processing entry points. Tempo is optional.
Initialization rejects capacity/rate/alignment/handle-state overlap before calling the backend. Parameter
admission rejects unknown/namespaced IDs, read-only fields and out-of-range values before DSP mutation.
Tempo accepts40..240 BPM Q8; missing optional support reports UNSUPPORTED and adds no second clock.

Insert rendering replaces in-place. Bus rendering requires distinct non-overlapping input/output spans and
replaces output with wet-only data. NULL, zero/oversized spans, unsupported routes, overlapping bus buffers and
buffers aliasing instance handle/state are safe no-ops. All valid1..256 frame spans dispatch without allocation
or waits. Settings operations validate descriptor/version/capacity/full payload before apply; trusted callbacks
must honor the accepted apply-after-validation guarantee. Invalid settings are rejected without state mutation.

Bypass ramps, track/send/return gains and routing are mixer responsibilities in M3-02. Parameter smoothing is
performed by each processor for parameters declaring it; the generic wrapper does not invent a curve or turn
integer enum values into interpolated controls. Normal zero input drains history, and only explicit reset
clears it while retaining parameters. Session panic/project replacement must invoke reset; STOP itself does
not gate live notes out of sends. These boundaries match decision002.

Validation:

```sh
python3 tests/run_native.py effect --sanitize on --build-dir build/native-agent-b-effect
```

PASS strict C11, ASan and UBSan without recovery/suppression. Five scenarios cover registry/capability and init
rejection; parameter/tempo/version/settings failure nonmutation; independent state with impulse wet-only versus
in-place replacement and guard samples; zero-input tail plus explicit reset; overlap/unsupported/frame guards;
and identical fake-delay output/state for partitions1/17/64/255/256.

This proves effect-instance dispatch and buffer conventions using the bounded fake processor. It does not prove
real filter/phaser/echo/reverb algorithms, bypass crossfades, alias rejection across a future mixer graph,
actual parameter smoothing, target CPU/RAM budgets, project serialization or hardware behavior. Those remain
named later tasks; callback declarations/fixtures are not a claim that musical effects are implemented.
