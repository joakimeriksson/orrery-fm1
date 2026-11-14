# M1-08 — Generic current-parameter readback

Owner: agent-b. Updated UTC: 2026-11-05T23:01:30+00:00. Local date: 2026-11-06. Status: submitted for agent-a review.

Implemented source API2 readback in core/instrument.h/.c and core/effect.h/.c. Descriptors expose a pure
get_param(const state,id,out) callback, required when param_count>0. Public md_instrument_param_get and
md_effect_param_get validate local IDs and instance state, allow read-only values, use a temporary callback
candidate and commit caller output only after successful in-range validation. NULL/aliased output, unknown/
namespaced IDs, callback failure, invalid positive status and out-of-range backend results leave output intact.
Zero-parameter descriptors can omit the callback. Common source API is explicitly2; settings/project wire
versions and the8096-byte budget remain1/unchanged. No DSP algorithms or imported engines were changed.

All four real instrument getters read current declared values without engine-specific host decoding: ARP35
fields, Eminent7, MiniPops2 accepted request controls, ElectronicFX10 trigger-template controls. Fake mono/poly/
no-velocity instruments and fake effect also expose pure gain queries. Descriptor defaults remain immutable
defaults, not a substitute for loaded preset values. Generic host code can now build initial parameter snapshots
through metadata plus the readback seam after applying an opaque sound.

Decision005 defines owning audio/stopped-context use and output/error semantics. UI reads published snapshots;
getters are not cross-thread DSP access. Readback reports CURRENT/effective control state, not the base value
under an active lock. The future model must retain that base separately and feed sequencer base_value callbacks
from it. Read-only is readable, never a setter bypass. Whole-project application/publication/error recovery
and stopped-save restoration are still session/model/codec responsibilities.

Validation:

```sh
python3 tests/run_native.py --sanitize on --build-dir build/native-agent-b-api2
python3 tests/run_native.py adapters electronic_fx --sanitize on --build-dir build/native-agent-b-readback
```

Full nine-suite run PASS strict C11 ASan/UBSan without recovery; log build/api2-native.log. Final focused real-
backend extensions also PASS: every exposed parameter read after every shipped preset and after each min/max
edit, with explicit getter-state purity checks. Wrapper tests cover loaded values differing from defaults,
required/missing/zero-parameter getter admission, read-only success/setter rejection, invalid/null/alias output,
backend failure and invalid results/status. Other clock/router/sequencer/effect/buffer/audio regressions pass.
Agent-a independently reran all nine final suites in build/native-final-review and reported PASS; tracker eight
checks and runner three checks also passed in that independent review.

No extra broad testing was needed after that successful review. Work/decisions/005-parameter-readback.md and
PLAN.md document the current contract. This closes parameter introspection; it does not claim project base
snapshots, active-lock model publication, effects DSP, mixer/session integration or device UI are implemented.
