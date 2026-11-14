# M1-05 explicit ARP performance controls

Task M1-05, owner `claude`, 2026-11-06 UTC. Upstream change in fm1-harbor-proc, imported with take.py, then
the adapter. Apple clang 21.0.0, macOS arm64.

Process note: the upstream engine change, the import and the adapter edit were made before M1-05 was claimed in
the tracker; the claim followed once M3-03 was in review. No other agent held the adapters, imports or
upstream-engines resources at the time.

## Upstream (fm1-harbor-proc, HEAD 94f8087ab62e; the ARP files are untracked there)

Originals were copied to the session scratchpad before editing (`arp-backup/`), since git does not hold them.

| File | Before | After |
| --- | --- | --- |
| modules/arp2600/arp_engine.c | 9a426cae12ef7c0541a791ab25ebd3c58994dffec6fdca4afd63b05632501633 | 6a4c2f4c123a0b745c2dc967f89b658b018990bf5819c9d45e7d3abb4c8387ae |
| modules/arp2600/arp_engine.h | 9a65ec4b76eedff6fca4c4e0352252c6d3e03f8086ed7e70b5fd43ca19ef0606 | cceb0640211355c1a1d59a51c4a9a9222a8aaaf3f2712fe870392985c8e264f0 |
| tests/arp_host.c | d215f46c8203b15f61fabeddebeb3090876bdf578eb96c205ee39ae43560decb | ef50ae945ba46c107fd6189dcedf86b2239310fef90d6fc1202edb1d49e1dce8 |
| tests/arp_check.py | c16a0c3580f88601d3c62560ddab6d0bb70b620597e93dc64f56e42ab97041bd | adad47d2ea892e010823f4983e588bcc409f196bb9ad9397add89f6d2567def8 |

**API.** `arp_play(a, note, flags)` and `arp_release(a)`, with `ARP_PLAY_ACCENT`, `ARP_PLAY_LEGATO` and
`ARP_PLAY_SLIDE`. It is a single-voice path for a host that owns note priority: no key stack, and not to be mixed
with `arp_note_on/off` on one engine. Accent uses the patch's existing accent (extra cutoff and level), the same
law the 1601 sequencer's accent steps use; velocity is still ignored. Legato with the gate up changes pitch without
a new attack. Slide is legato plus a glide: the patch's glide time, or a 20 ms time constant (95 % in about 60 ms)
when the patch has none. Without the gate up, legato and slide are a plain attack.

**Unchanged paths.** The internal `voice_on` gained a slide argument; every existing caller passes 0, and the new
`slide_k` is used only while sliding. Old and new engines, each built with its own `arp_host.c`, render
byte-identical output for: note 9/45, note 3/69, note 0/60, seq 1/0, seq 2/4, seq 2/5, seq 9/6, echo 1/0 and the
ladder sweep (sizes 176,400 to 1,411,200 bytes, distinct hashes). A first attempt at this comparison was invalid
(zsh did not split the arguments, so every case ran the sweep); it was redone with explicit arguments.

**Upstream checks.**

    python3 tests/arp_check.py              # in fm1-harbor-proc: all passed, 12 checks
    python3 tests/engine_arithmetic_check.py   # ARP and MiniPops under ASan/UBSan: PASS
    cc -fsyntax-only -I firmware/dock -I modules/arp2600 modules/arp2600/arp_module.c   # only the expected macOS section error

New checks in arp_check.py, on RENDEZ-VOUS (no glide of its own):

| Check | Result |
| --- | --- |
| play attacks | onsets at 0.000, 1.995 and 2.995 s only: none at the legato (0.5 s) or slide (1.0 s) |
| play legato | G3 at 196.0 Hz |
| play slide | 249.2 Hz averaged over the 70 ms from 5 ms in (between G3 and C4), 260.9 Hz at 200 ms |
| play accent | RMS 2734 accented against 2474 plain (+10 %) |

## Import and adapter

`./take.py` copied the engine; `engines/arp_engine.c` and `.h` match upstream exactly (hashes above). The other
taken files were already identical to upstream.

`instruments/arp.c` (b1836dc6aeb211f7523805f67ee2be543b27f71f4411f1bec86f978ad9be9218) now declares
`MD_CAP_ACCENT | MD_CAP_LEGATO | MD_CAP_SLIDE` (still not VELOCITY) and drives `arp_play`/`arp_release`. A held
voice is replaced only by a LEGATO or SLIDE note-on, which is how the router delivers a mono slide (new note-on,
then the old note-off); the old token's off is then a stale no-op. A plain second note-on while one is held is
still refused without mutation. The adapter no longer uses the engine's key stack.

`tests/native/adapters_test.c` (9f70fa00dce268210b9b3b44069f2e8307d06763748ad6d471a7ade5083d3f88) replaces the
old "accent and slide are unsupported" assertions with: capability flags; no key stack; refused plain second
note; SLIDE keeps the envelope stage, glides and replaces the token; stale and flagged offs; velocity 1 and 127
give byte-identical engine state with ACCENT; and a new scenario through the real router (accented note, sequenced
slide, both offs, no owned notes or backend errors left).

    python3 tests/run_native.py --sanitize on --build-dir build/native-claude-all   # 13 suites PASS, ASan/UBSan

The adapter's engine-reference and segmented-render comparisons pass unchanged.

## Listening

`build/arp-performance/arp_play.wav` (2d8cab41170387c6f13a48bce090d2d917b2f804e4e5d3b39dd6efc376ed5283, the
upstream play render) and its spectrogram `arp_play.png`: the legato is a pitch step with no new attack, the slide
a smooth curve up to C4, and the accented C3 has visibly stronger upper harmonics than the plain one.

## Limits

- The upstream ARP files are untracked in fm1-harbor-proc; preserve them (and tests/engine_arithmetic_check.py)
  when that repository is next committed.
- The standalone ARP module does not use the new path; its keys and 1601 sequencer behave as before.
- Not heard on hardware.
