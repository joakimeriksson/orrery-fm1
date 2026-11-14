# M1-07 — Hybrid HIT admission preserves held notes

Owner: agent-a. Date: 2026-11-05. Status: submitted for agent-b review.

ElectronicFX can be both a pitched instrument and a one-shot percussion kit. When every physical slot is
occupied by a held pitched note, a HIT cannot steal those owners. A nonmutating MD_ERR_CAPACITY response is
legitimate admission backpressure, not an engine fault. The previous generic router fault path would reset
that whole track, cutting the held melody.

Router dispatch now counts and returns HIT/CAPACITY without resetting DSP or logical/physical ownership.
Existing rejected_notes diagnostics count transient hit refusals as well. Other unexpected backend failures
still reset/flush the track and increment backend_errors; note capacity is preflighted by the router as before.
Decision002 and router.h state the exception explicitly; no broad error suppression or capability downgrade.

Validation: `python3 tests/run_native.py router --sanitize on --build-dir build/native-hybrid-router` passes
strictC11ASan/UBSan. A fake hybrid holds four pitches, refuses a hit, and is byte-identical afterward; all logical
owners and physical voices survive, rejection increments, backend_errors stays zero, and subsequent note-offs
release normally. The prior injected-backend-fault test still confirms fail-safe flush. This is a regression of
observable ownership behavior; ElectronicFX's real allocator is independently tested in M2-04.
