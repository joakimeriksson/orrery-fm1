# CLAUDE.md — seventies-fm1

A Jarre band in one fm1-dock module for the FM-1: Eminent strings, two ARP 2600s (lead, bass), the Korg Mini
Pops, a tape echo, a sequencer. Read AGENTS.md, PLAN.md and EXECUTION.md every session. PLAN.md is the design;
WORK.json is live progress (`python3 tools/work.py status`), and work/agents/ holds per-agent resume notes.

## Layout

- `module/seventies.c`, `module/module.cfg` — the module (HB_F_SCREEN | HB_F_PANEL). The sequencer goes in
  `module/seq.c` / `module/seq.h` as a portable unit.
- Planned `core/` and `instruments/` — portable instrument API, transport/session/routing, and engine adapters.
  Follow PLAN.md's API and Melodee reuse gates before implementing the sequencer or replacing the panel.
- `engines/` — COPIES of the engines from fm1-harbor (fm1-dock; `../fm1-harbor-proc` here, on its
  `seventies-engines` branch: `modules/arp2600`, `modules/eminent`, `modules/minipops`). Fix an engine in
  fm1-harbor, where its checks are (`tests/arp_check.py`, `tests/eminent_check.py`, `tests/minipops_check.py`),
  then `./take.py` to copy it here. Do not fork them silently. `engines/hb_echo.c` is the seqdock host's echo
  that `effects/echo.c` was ported from (the module plays the port); it and the rest of the seqdock work are on
  fm1-harbor's `proc-snapshot` branch, not on main.
- `module/setups.h` — the factory setups (fifteen) and the effect patterns, one named-field block each: edit style
  sets there. `tools/listen.py` (`uv run --with numpy --with pillow tools/listen.py [SETUP ...] [--png]`) renders the
  module itself natively (`tools/render_setups.c`, a second a setup) and prints each part's level, pitch classes and
  the mix's bands: balance and check keys with it before the emulator. Changing `set_t`'s layout needs new
  `SET_MAGIC` / `SETUPS_MAGIC`.
- `demo.py` — plays a minute in fm1-emu and records `build/demo/demo.wav`. `tools/spectrogram.py` draws a WAV:
  this is how we listen (`uv run --with numpy --with pillow tools/spectrogram.py IN OUT.png "title" [HOP_MS]`,
  then read the PNG). Neither system python here has numpy.

## Build and test

The toolchain lives in fm1-harbor (Docker on this Mac; the compiler is emulated, so a build is about a minute):

    cd ~/work/fm1-harbor-proc && PYTHON=$HOME/work/fm1-harbor/.venv/bin/python3 \
      HB_MOUNT=$HOME/work/seventies-fm1 ./build.sh --no-package \
      --modules cs1,scintilla,$HOME/work/seventies-fm1/module

- The module comes out as `build/modules/module.dock` (named after the directory); the emulator image is
  `build/fm1-dock_xip.bin`. A quick syntax check on the Mac without Docker:
  `cc -fsyntax-only -I ~/work/fm1-harbor-proc/firmware/dock -I engines module/seventies.c` (the one error about
  the descriptor's section is expected on macOS).
- Emulator: `~/work/fm1-emu/target/release/fm1-live --bin build/fm1-dock_xip.bin` from fm1-harbor-proc for the
  window; scripted runs with `tools/dock.py emu --bin ... --ms N --keys SCRIPT --out DIR` (keys `ms:id`, `^`
  release, `eN:clicks`, `mHEX` MIDI; buttons OCT- 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7, HOME 8,
  SAVE 9, ARP 10, SEQ 11, PLAY 12, REC 13; note keys 14 + k). The emulator check of this module:
  `python3 tests/integration/seventies_emu.py` here (fm1-harbor's own `tests/emu_test.py` has a `seventies` case
  only on its `proc-snapshot` branch).
- Hardware differs from the emulator: on an SMK-37 Pro the main core's worst block is 75 .. 79 % where fm1-emu says
  57 .. 65 %, and fm1-emu's second-core figure (2 %) is not the device's (17 .. 19 %): judge load and the split
  between the cores on the device. It can be driven over USB-MIDI without touching it (channel 10: note 43 starts and
  stops the band, 42 the next setup) and read with `uv run --with python-rtmidi tools/dock.py modules` / `logs` in
  fm1-harbor-proc. Deploying over a module already there has crashed the dock (delete it first), and after a crash
  the dock keeps the second core off until power-off: check `modules` says 2 cores before trusting a number.
  `tools/prof/` is a timing build of the module (each part and bus in the device's log every five seconds while
  the band runs): build it in place of `module`, deploy `build/modules/prof.dock`.
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
