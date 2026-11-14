# Synth/sequencer emulator audition (D0-01)

Owner agent-a; cross-review agent-b (`/root/audition_review`). Local date2026-11-06.

The isolated `audition/` module connects the real new portable sequencer, router,
ARP/Eminent/Minipops adapters and electronic FX instrument. Four parts; FX replaces
lead. It leaves the legacy module intact. Dry fixed gains only; this is not the
planned production session, effects graph, recording or project editor.

Validation:

- `python3 tools/render_seq_demo.py`: strictC11, ASan/UBSan combined test PASS.
  Both modes16s playing plus2s release, all4stems nonzero; no clipping, scheduler
  overflows, base-read errors or router dispatch failures. Native state54176B.
  Unnormalized mixes `build/seq-demo/native/{lead,fx}-0.wav`, stems suffix1–4.
- Reviewer independently captured immutable source snapshot
  `/private/tmp/melodock-review-6wxhtgvc`; strict sanitizer run PASS. Mix peak
  lead28008/FX28901 Q15. Additional snapshot test verifies all4pattern launches
  switch at zero-based sample88200/frame88201 (strictly nextbar at120BPM), stop ownership0, repeated
  FX/lead replacement and stop ownership0. STOP retains decaying strings tails.
- Target command from sibling fm1-harbor-proc:
  `PYTHON=/Users/joakimeriksson/work/fm1-harbor/.venv/bin/python3 HB_MOUNT=/Users/joakimeriksson/work/seventies-fm1 ./build.sh --no-package --seqdock --only --modules /Users/joakimeriksson/work/seventies-fm1/audition`.
  Initial failures: freestanding SDK lacks string.h; local declaration header
  added, implementations remain host hb_lib. SDK C11 atomic loads/stores emitted
  unavailable CAS helper even with acquire/release. Replaced with explicit
  target-only aligned32 single-writer IRQ/main word protocol and compiler fences.
  Main alone writes request; audio alone writes snapshots, no core1 access.
  This protocol is for this target's single-core interrupt ABI, not general C threads.
  Target disassembly confirms plain word loads/stores; final link has no unresolved
  __sync/__atomic helpers. Reviewer independently parsed ELF alignment/symbols.
- First complete target: text34472B, BSS55036B, data/ram_text0, within272KiB RAM.
  `python3 tools/run_seq_demo.py --check`: real new module selected, starts playback,
  queues pattern, changesFX, changes120->125BPM, stops. Recorded9.83s audio;
  1687blocks,0overruns/strikes, worst61004ticks43.8%, last32.0%. Screenshot
  shows STOPPED/FX/125BPM. Native evidence separately proves exact nextbar timing.
  Full redraw could leave partial screenshots, so final build caches unchanged draws.

Pinned image: `build/seq-demo/melodock-test_xip.bin`.
Launcher: `python3 tools/run_seq_demo.py`. Controls and rebuild recipe in
`audition/README.md`. No real hardware deployment; no commit.

Final quiet-source rebuild and repeated identical emulator script PASS:
text34544B, BSS55056B, data/ram_text0, moduleCRC69951706.
1687blocks, worst60988ticks43.8%, last44638ticks32.0%,0overruns, no strikes.
Final output `build/seq-demo/emulator/{audio.wav,log.txt,ram.bin,panel.ppm}`.
SHA256 pinned XIP `f1a48f811f3d0fda9831037891268f85a1282904ea2a880a5b2f4de97bed3094`;
DOCK `cc3ee460e2ad8d60122ed3d2862949b3b58979524394d026159c800b4691a2f9`.
Final source manifest `build/seq-demo/sources.json` records imported/current input hashes.
Remaining compiler warnings are existing missing-braces initializers in core/seq;
no build errors. Emulator timing is evidence for this audition fixture, not a
maximum-density production mixer budget or physical-hardware measurement.
