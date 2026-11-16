# M3-04 switch the module to the portable session

Task M3-04, owner `claude`, 2026-11-06 UTC. Apple clang 21.0.0 for native checks; the JieLi pi32v2 toolchain in
Docker (fm1-harbor-proc build.sh) for the module; fm1-emu for the emulator runs. Nothing was sent to hardware.

## What changed

`module/orrery.c` keeps the ORRERY panel, pages, knobs, screen and settings record (magic 7S01, layout
unchanged), and replaces everything under them:

- **One session.** Engine globals, the ARP 1601 and Mini Pops internal sequencers and the hand-made four-buffer mix
  are gone. Two ARP 2600 adapters (bass, lead), the Eminent and the Mini Pops are caller-owned instances on tracks
  0..3 of `core/session`; `render()` only calls `md_session_render`. Everything else is the main loop, through the
  bounded command ring, staged edits (bank, sounds) and the published snapshot.
- **Patterns from the old presets** (`module/band.*`, portable and natively tested). Each ARP sequence becomes one
  bass pattern per chain link (transposed), each link's chord a held chord pattern, and the Mini Pops keys a drum
  pattern merged as the machine's diode matrix does, extra rhythms included. Link changes are next-bar launches
  of bass and chords, queued by the main loop a bar ahead from the snapshot (`md_band_next_link`); the change
  itself lands on the bar line to the sample.
- **Local effects.** The module's own echo (effects/echo.c) on the echo bus with sends from strings and lead,
  and its own reverb (effects/reverb.c, HALL size 70 damping 50) on the reverb bus with every track's send at the
  MIX page's reverb amount. The master filter instance is assigned and bypassed. `init` asks the dock's room off
  with `fx_reverb(0, ...)` when the host has that call; a host without it shows GLOBAL ROOM in the header.
- **Live input through the router.** Panel keys and MIDI (per channel) start lead notes with tokens kept per key and
  per channel note, so a key-up finds its note after octave or page changes. LATCH while stopped sounds the
  cursor's chord as live notes. ARP releases every panel and MIDI source.
- **Visible limits.** The bass SWEEP knob shows "--" and does nothing (not portable to v1 locks). The DRUMS page
  says how many hits moved to the nearest 32nd. The MIX page shows CLIPPING when the mixer counted output clips.
- **Build.** module.cfg version 2 compiles core, instruments, effects and engines with `-std=c11 -DMD_SINGLE_CORE`
  (compiler-barrier fences, no C11 atomics) and a local string.h declaring hb_lib's functions, as the audition did.

Supporting changes in files reviewed under earlier tasks, to be covered by this review:

- `instruments/minipops.*`: three parameters (QUIJADA, GUIRO, TAMBOURINE levels, default 70) scale those voices'
  hits as the machine's sliders do, 0 mutes them; settings version 2, 5 bytes. A drum step has one velocity, so
  the extra rhythms' levels could not live in the pattern.
- `core/session.c`: each track renders only up to the events that reach it (one track, all, or none for mixer and
  effect targets), so a drum hit no longer splits the other tracks' render calls.
- `core/mixer.c`: return-gain smoothing is skipped once settled, and a send whose gain is exactly zero is skipped.
- `effects/reverb.c`: the inner loop is unrolled with constant line offsets and branch-free rounding toward zero;
  the damping lowpass keeps four extra bits. Same laws; native cost 10.4 to 4.2 us per block.

## Native integration (the standalone engines as reference)

    python3 tests/run_native.py band --sanitize on --build-dir build/native-claude-band

`tests/native/band_test.c` plays the built patterns through the session and the real adapters, wrapped in proxy
instruments that log every note and hit with its frame, and compares them with each engine's own sequencer:

- **Bass against the ARP 1601**, all eight sequences, for two passes of their whole chain plus a bar (9 to 13
  bars), main-loop link scheduling included: every gate on and off, pitch and accent matches, edge for edge
  (145 to 417 edges per sequence), within 2 samples.
- **Chords**: every chord attack falls exactly on a bar line, and only where the bass changes link (every 4 bars
  for a single link).
- **Drums against the Mini Pops**, all 20 rhythms with all three extra rhythms, plus a two-key combination without
  extras, for 4 bars: every hit matched by voice and time; on-grid hits within 0 samples. Hit lengths are not
  compared: the guiro's scrape length is not carried (see Limits). Four rhythms have hits off the v1
  grid and report it: Swing and Latin Swing (late triplet eighths, 4 and 6 hits, at most 40/1000 of a beat), Slow
  Rock and March 6/8 (triplet rhythms, 22 and 16 hits, at most 42/1000 of a beat, about 21 ms at 120 BPM).
- **Engines' sequencers stay off**: the ARP 1601 never runs or steps and the Mini Pops never plays.
- Settings, chain arithmetic and the link-scheduling rule (nothing mid-link, the next link in the bar before it,
  no double queue, a stale queued launch replaced) are checked directly.

All fourteen native suites pass with ASan and UBSan (`python3 tests/run_native.py --sanitize on`).

## Target build and emulator

    cd ~/work/fm1-harbor-proc && PYTHON=$HOME/work/fm1-harbor/.venv/bin/python3 HB_MOUNT=$HOME/work/orrery-fm1 \
      ./build.sh --no-package [--seqdock] --only --modules cs1,scintilla,$HOME/work/orrery-fm1/module
    python3 tests/integration/orrery_emu.py      # in orrery-fm1, after both builds

| Image | Module | Worst block | Overruns |
| --- | --- | ---: | ---: |
| regular dock (fm1-dock_xip.bin) | code 57,228 B, data 16 B, BSS 122,276 B, no RAM code | 66.2 % | 0 |
| seqdock (seqdock_xip.bin) | same module, CRC d8308b67 | 66.2 % | 0 |

`tests/integration/orrery_emu.py` loads ORRERY, presses PLAY, holds a lead note from 6 to 8 s over the band
and records 12 s. Both images pass all seven checks: started on the session, 0 overruns, worst block under 70 %,
no host reverb in the processing section (the baseline log had one: the room-off request works), the hi-hat's
envelope repeats every 250 ms (correlation 0.80), the lead adds level (RMS 2093 to 3274), band RMS 2093 and peak
12628. The spectrogram (build/integration/seqdock/spectrogram.png) shows bass sixteenths, held chords changing at
the link bars, the hats, and the lead's glide at 6 s.

**Cost.** The first build measured 71.8 % on this run, over budget. Measured variants on seqdock: without the
echo and reverb buses 59.1 %; with the per-track render change 71.9 % (no gain); with the reverb loop rewrite and
the mixer fast paths 66.2 %. The fm1-harbor emulator suite's own `seventies` case (a shorter lead) measured
62.2 % on the first build. The baseline module measured 40.6 % with the host reverb outside the module's figure.
The session, mixer and buses are the new cost; measuring and budgeting them is M3-05.

fm1-harbor-proc's own suite on the final seqdock image (`HB_APP=seqdock python3 tests/emu_test.py`): 40 cases pass,
including `seventies` (hat envelope every 250 ms, correlation 0.76, RMS 2283, peak 12628, worst 56.6 %, was 62.2 %
on the first build) and `usb_seventies` (USB audio identical to the speaker's, no ring errors). The only failures
are the six `decode` cases for modules the emulator cannot decode, which fail before and regardless of this work.

| Artifact | SHA-256 |
| --- | --- |
| build/modules/module.dock | b4316351b0da8b1d9e6bc39dae686dd1915fe34b3158fbf8f3c8168677de2ffc |
| build/seqdock_xip.bin | 91c6fe6a0f51abea3901b43cf4d777a6294177bfc2e15dcf7623115b31df27ad |
| build/fm1-dock_xip.bin | 0b257263457348b4bd61aceddb3d83b0c81fe70e1459d17bb3e7f44a5f314aca |
| build/integration/seqdock/audio.wav (orrery-fm1) | 4b6cb15539292f2cfa1484495da4b77d5e22f694b3853c6aeaa5bcbff6638112 |
| build/integration/fm1-dock/audio.wav (orrery-fm1) | e0b679c1c9adfba07c1f31a2b83bbfbd41acada89e72ca57f0633a6ab40917b4 |

| Source | SHA-256 |
| --- | --- |
| module/orrery.c | 822ac57d2a9bad118d5d979992f15b938cbf5ec199b09e1f8d685f9e76ef9a0e |
| module/module.cfg | 47228a31bf749873ca92efd742ccf01bfb925a754b3f0437f78cfdff1b18e18b |
| module/band.c | af3a0cd87ee23c953cb03eb56550ecea7245213c1785e5741c54fde4d6475f9c |
| module/band.h | cf2640f9cfe4a6e4b3fcc9cf12181642db4a339520e5657db185e0d77400df6d |
| module/string.h | 0fd9cb542880d5f3ea0650184c67f8d325ce51806f0eafdbbac805c3092fae71 |
| core/session.c | eb3025730d3da19c3ca30dc86a0f393155a5b0ac31120c5447ccb400e16b5f97 |
| core/mixer.c | 41b56099da7f69cc785300a107dc23b492301aa23450221dbbd2639e03fb9a32 |
| effects/reverb.c | 96dd73530a38b2075f9ec32f44d9878b6953c9721acbda14d762557a3a4124f6 |
| instruments/minipops.c | e768aca70a42e98835bc7e6deab99f554ea61816cc57cbf9ba44a226b3e07b4f |
| instruments/minipops.h | fcf61db2d4798311c28747a303171636f19fe9ddeff4d122f0ce708c449a17f9 |
| tests/native/band_test.c | ecd0d38df02858702ddc549995ccda8beacfacc32ddba867d243fc663c461389 |
| tests/native/adapters_test.c | 359b2e62de6617ea71331944b24c6eff53a6030f2845c9574610065d5f7f1b3f |
| tests/integration/orrery_emu.py | a89ff66c14b3776e8f173bd2aff592a7aabcf46c0212489ebb1359e812347e5f |

## The original module

M3-04 overwrote module/orrery.c, which git did not hold (no commits). It was rebuilt in legacy/v1/
and both files hash exactly as the M0-01 baseline recorded them (v1.c 977fa670..., module.cfg 01837c6f...).
The baseline's built module and image remain at build/baseline/.

## Limits

- Worst block 66.2 % leaves little room under 70 %; the full stress case is M3-05.
- The guiro scrapes for a fixed quarter second: v1 drum hits carry no length, so the machine's long and short
  scrapes (150/60/200 % of a step, following tempo) are lost. The DRUMS page says GUIRO 1/4 S when it is on.
- No ARP sequence sweep; link changes depend on the main loop running at least once a bar; four rhythms are
  quantised as stated. The old host reverb's dry attenuation (1 - mix/2) is not reproduced: the local reverb only
  adds a wet return.
- Live input is untimestamped: it lands at the start of the next block.
- Not heard on hardware.

## Revision after review

reviewer-claude-5 returned the task with two findings:

1. The guiro's per-hit scrape length is lost (the adapter plays a fixed 250 ms). Carrying it needs a length on
   drum hits, which the v1 event contract does not have, so it is now a stated limit: on the DRUMS page and above.
2. If the ring accepted the bass launch but refused the strings launch, the chords stayed a link behind. The
   module and the test now schedule each track from its own snapshot state.

A stronger test then found a third bug the original scheduling already had. When a bar line falls exactly on a
block boundary, the snapshot already reports the new bar while the launch for that bar line is still due in the
next block; the scheduler took that launch for stale and replaced it, skipping a link (OXYGENE II: bar 1 replayed
link 0's chord). The bass hid it because OXYGENE II's first two links are identical. `md_band_next_link` now never
replaces a pending launch.

band_test now also requires every chord attack to be its link's chord (by root) and a chord attack on every
link-change bar, and replays one sequence with a chord launch refused. Run against the old shared scheduling, the
test fails. All fourteen native suites pass with ASan and UBSan; both images rebuilt (module CRC b840d9db, code
57,264 B, BSS 122,276 B) and pass all seven emulator checks, worst block 66.2 %.

| File | SHA-256 after revision |
| --- | --- |
| module/orrery.c | 5a195e9950a008e8b6b638223af22425b45413e60d3b470be46fee5fcd32d54f |
| module/band.c | ebd1bc7646eda57a96f0430b12408dd8cf833542833a3b613ec16b7083aabe27 |
| module/band.h | f7619ec22a9a738ab20a4f17efa5abb8cbbb56a6276247aad6f4d699375dfb21 |
| tests/native/band_test.c | 2c1bb942ffb0b8390f4519d8f3bd70812aeb27d5ed644d0c8fa86361a1254761 |
