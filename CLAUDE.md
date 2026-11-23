# CLAUDE.md — orrery-fm1 (the module is ORRERY)

A Jarre band in one fm1-dock module for the FM-1 (ORRERY; it was SEVENTIES until 2026-11-15, and a unit that has the
old module keeps it and what it saved under that name until `dock.py delete SEVENTIES`): Eminent strings, two ARP 2600s (lead, bass), the Korg Mini
Pops, a tape echo, a sequencer. Read AGENTS.md, PLAN.md and EXECUTION.md every session. PLAN.md is the design;
WORK.json is live progress (`python3 tools/work.py status`), and work/agents/ holds per-agent resume notes.

## Layout

- `module/orrery.c`, `module/module.cfg` — the module (HB_F_SCREEN | HB_F_PANEL). The sequencer goes in
  `module/seq.c` / `module/seq.h` as a portable unit.
- Planned `core/` and `instruments/` — portable instrument API, transport/session/routing, and engine adapters.
  Follow PLAN.md's API and Melodee reuse gates before implementing the sequencer or replacing the panel.
- `engines/` — COPIES of the engines from fm1-harbor (fm1-dock; `../fm1-harbor-proc` here, on its
  `orrery-engines` branch: `modules/arp2600`, `modules/eminent`, `modules/minipops`). Fix an engine in
  fm1-harbor, where its checks are (`tests/arp_check.py`, `tests/eminent_check.py`, `tests/minipops_check.py`),
  then `./take.py` to copy it here. Do not fork them silently. `engines/hb_echo.c` is the seqdock host's echo
  that `effects/echo.c` was ported from (the module plays the port); it and the rest of the seqdock work are on
  fm1-harbor's `proc-snapshot` branch, not on main.
- `instruments/harmonic.c` — the HARMONIC voice (two generators of sixteen harmonics: the RMI Harmonic Synthesizer's
  principle), the lead's third instrument after the 2600 and the Synthex. It is written here, not copied from
  fm1-harbor; its waves are `instruments/harmonic_tables.h`, drawn by `tools/gen_harmonic_tables.py`.
- The player's own sequences and song (`seq_t` in `module/orrery.c`, `lead_song()` and `md_band_song_at()` in
  `module/band.c`): four sequences of sixteen steps a setup on the lead track, the STEPS and SONG pages. The setup in
  hand is saved with the settings record (the two are exactly its 240 bytes); every setup's in bank 2 on SAVE.
- `module/setups.h` — the factory setups (nineteen) and the effect patterns, one named-field block each: edit style
  sets there. `tools/listen.py` (`uv run --with numpy --with pillow tools/listen.py [SETUP ...] [--png]`) renders the
  module itself natively (`tools/render_setups.c`, a second a setup) and prints each part's level, pitch classes and
  the mix's bands: balance and check keys with it before the emulator. Changing `set_t`'s layout needs new
  `SET_MAGIC` / `SETUPS_MAGIC`.
- `MANUAL.md` — the player's manual: every page with its picture (`docs/manual/`, taken from fm1-emu by
  `uv run --with pillow tools/manual_shots.py`) and where each sound and effect is changed. A control that changes
  on the panel changes there too, and its picture is taken again.
- `demo.py` — plays a minute in fm1-emu and records `build/demo/demo.wav`. `tools/spectrogram.py` draws a WAV:
  this is how we listen (`uv run --with numpy --with pillow tools/spectrogram.py IN OUT.png "title" [HOP_MS]`,
  then read the PNG). Neither system python here has numpy.

## Build and test

The toolchain lives in fm1-harbor (Docker on this Mac; the compiler is emulated, so a build is about a minute):

    cd ~/work/fm1-harbor-proc && PYTHON=$HOME/work/fm1-harbor/.venv/bin/python3 \
      HB_MOUNT=$HOME/work/orrery-fm1 ./build.sh --no-package \
      --modules cs1,scintilla,$HOME/work/orrery-fm1/module

- The module comes out as `build/modules/module.dock` (named after the directory); the emulator image is
  `build/fm1-dock_xip.bin`. A quick syntax check on the Mac without Docker:
  `cc -fsyntax-only -I ~/work/fm1-harbor-proc/firmware/dock -I engines module/orrery.c` (the one error about
  the descriptor's section is expected on macOS).
- Emulator: `~/work/fm1-emu/target/release/fm1-live --bin build/fm1-dock_xip.bin` from fm1-harbor-proc for the
  window; scripted runs with `tools/dock.py emu --bin ... --ms N --keys SCRIPT --out DIR` (keys `ms:id`, `^`
  release, `eN:clicks`, `mHEX` MIDI; buttons OCT- 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7, HOME 8,
  SAVE 9, ARP 10, SEQ 11, PLAY 12, REC 13; note keys 14 + k). The emulator check of this module:
  `python3 tests/integration/orrery_emu.py` here (fm1-harbor's own `tests/emu_test.py` has a `seventies` case
  only on its `proc-snapshot` branch).
- Hardware differs from the emulator: the device takes about 1.5 times what fm1-emu says for the same code (2.3
  cycles an instruction at 360 MHz against one at 240), and fm1-emu's second core costs next to nothing (2 % where
  the device has 30 .. 57 %): judge load and the split between the cores on the device. A block costs the larger
  of the two cores' parts (each with its tracks' mixing) plus the buses, the sum and the routing (13 %): balance
  the cores' parts, and what is in a row counts fully. It can be driven over USB-MIDI without touching it (channel 10: note 43 starts and
  stops the band, 42 the next setup) and read with `uv run --with python-rtmidi tools/dock.py modules` / `logs` in
  fm1-harbor-proc. Deploying over a module already there has crashed the dock (delete it first), and after a crash
  the dock keeps the second core off until power-off: check `modules` says 2 cores before trusting a number.
  `tools/prof/` is a timing build of the module (each part and bus in the device's log every five seconds while
  the band runs): build it in place of `module`, deploy `build/modules/prof.dock`. Ask before each kind of thing
  sent to the device (a deploy and playing it was allowed; a probe was not).
- Per-part costs without the device: `build/prof1` is the timing build on one core (`-DORRERY_ONE_CORE`, a copy of
  tools/prof with that flag): its P lines in fm1-emu are instruction counts (device time is about 1.5 times that),
  and with two cores fm1-emu's figures for the parts mean nothing. A loop that holds many values compiles to
  mostly stack traffic here (sixteen registers): a pass a stage over a small buffer is two to three times fewer
  instructions for the same samples. Check an engine rewrite sample for sample against the old file (a harness
  that renders every preset from both: the scratch `enscmp`, `emcmp` of 2026-11-15 are the pattern).
- Code a block runs every time is `MD_HOT` (core/hot.h): it runs from the module's 16 KiB of RAM (16324 B used:
  there is room for 60 more; the echo's loop and the reverb's in/out pass are in the flash for it), calls nothing in the flash except through a pointer or `MD_FAR`, and its helpers
  are `MD_INLINE` (JieLi's clang 4 keeps a plain static out of a function in another section; the linker refuses
  the call: "relocation truncated"). The engines have `ARP_HOT` / `EM_HOT` / `MP_HOT` and `..._IN` for the same
  (module.cfg names the sections).
- Measure, do not guess: the emulator log prints the module's worst block in percent; a check on the Mac for
  every unit before a screen; a spectrogram for anything that should sound a certain way. Under a held pad an
  attack detector finds little: read a band's time as the periodicity of the twice-differenced envelope.

## Rules

- Never send anything to a real FM-1 without asking. Commit only when asked; no attribution lines in commits.
- Fixed point only in audio (Q15 blocks, Q16/Q30 inside), no allocation or host calls in `render`, no 64-bit
  division in a per-sample path. Module RAM is 272 KiB and flash plenty; the host's text call paints its own black
  line, so a coloured box is a frame with the text inside it.
- Emulator traps: a MIDI message sent before a module switch finished goes to the old module; HOME is held 1.8 s
  to open the list from a panel module and pressed briefly from a non-panel one; fm1-live loses an encoder detent
  at some loop phases, so tests tolerate one.
- Module settings records are 240 bytes at most; the bank is 8176 bytes and is where a project goes, written on
  SAVE only.
