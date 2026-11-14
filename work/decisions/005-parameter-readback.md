# Decision 005: generic current-parameter readback

Task: M1-08. Owner: agent-b. Status: submitted for agent-a review. Date: 2026-11-06 (implementation began 2026-11-05).
Amends decision002's source descriptor contract; its parameter IDs/settings/project wire layout remain unchanged.

## Problem

A host can enumerate parameter metadata and apply opaque preset/project sound payloads, but metadata.initial
only describes the default sound. Without a generic query, the host cannot show the loaded values or initialize
base-value snapshots for editing/locks without decoding engine structs or each opaque payload. This also affects
future effect slots. Descriptor defaults are not current parameter values.

## Source API2

Bump MD_API_VERSION from1 to2. This is an explicit source-contract revision, not a binary-plugin compatibility
promise. All statically linked real/fake descriptors use the new macro. The dock HB_ABI, instrument/effect
settings versions, MD_PROJECT_VERSION1 and the8096-byte project layout do not change.

Both descriptor types add:

```c
int (*get_param)(const void *state, md_param_id_t id, int32_t *value);
```

It is required when param_count>0 and may be NULL when the descriptor exposes no parameters. The callback
returns the current native integer control value for a declared local parameter ID. It must not mutate state,
allocate, wait or call hardware/host services. Read-only parameters are readable; they remain unwriteable.

Public validated operations are:

```c
int md_instrument_param_get(const md_instrument_t *, md_param_id_t id, int32_t *value);
int md_effect_param_get(const md_effect_t *, md_param_id_t id, int32_t *value);
```

Wrappers validate initialized instance, output pointer and local ID, find the descriptor and invoke the callback
with a temporary candidate. Only a successful in-range result is committed to caller output. Unknown ID,
reserved/namespaced ID, NULL output, backend error, impossible backend status or out-of-range backend result
leaves caller output unchanged. A callback cannot bypass a read-only declaration by making a setter call.
Output aliasing the instance handle or private state is rejected rather than letting a nominal read modify DSP.

Error behavior: STATE for uninitialized instance or invalid backend result/status; ARGUMENT for NULL/aliased
output; RANGE for invalid local ID; NOT_FOUND for an undeclared valid local ID. Negative backend errors propagate
without output mutation. Positive nonzero callback status is a contract fault (STATE), because only encoders
return positive lengths. A zero-parameter descriptor with no getter remains valid and unknown reads fail normally.

## Owning context and model base values

Readback runs in the serialized audio/stopped owner context, not directly from UI/draw callbacks. Publish copies
for display. After preset/project application, the owner can enumerate metadata and capture current values
through this API without any engine-specific host decoding. MiniPops reports its accepted requested control
values, including before its next render consumes the request; coefficients/history are not public parameters.
Electronic FX reports its current trigger-template settings, not the snapshots inside already-ringing voices.

The getter reports CURRENT/effective control state. A parameter lock may temporarily override that state, so the
model must retain independent base values and must not rebuild them by querying locked DSP. A panel/base edit
updates the model's retained base; restoring a lock uses that model value. The pure sequencer's base_value callback
must read this model, not blindly call the DSP getter while a lock is active.

Safe initialization sequence: apply validated sound at the owner handoff, capture all exposed values into a new
base snapshot, then permit locks. That orchestration and error recovery belong to session/project work; this
API alone is not an atomic whole-project load transaction. For stopped saves, drain/restore active locks before
encoding sound state or serialize the separately retained model base by the later codec's documented policy.

## Backends and validation

ARP/Eminent query the same declared native sound fields that their setters control; MiniPops queries volume/
balance requests; Electronic FX queries the current settings template. Fake instrument/effect backends expose
pure gain queries for common tests. No DSP algorithm, envelope, seed advancement, parameter namespace, preset
payload or setting schema was changed. The descriptor grows one callback pointer; no instance handle or voice
state is enlarged by this API addition.

Tests query every exposed real parameter after loading every shipped preset, validate edited values, and compare
with independent fixture values. Common wrappers test read-only access, required/optional getter admission,
loaded values differing from defaults, invalid/unknown/NULL/aliased output, backend errors, out-of-range backend
values and invalid positive statuses. The full native suite must pass with API2 before session/mixer/model work
can rely on this seam. UI/model publication and active-lock base restoration remain subsequent integration gates.
