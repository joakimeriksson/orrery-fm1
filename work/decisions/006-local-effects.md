# Decision 006: local effect DSP and its relation to the host's echo and reverb

Task: M3-03. Owner: claude. Status: proposed, for review with M3-03. Date: 2026-11-06.
Affects M3-03, M3-04, M3-05 and any later upstream work in fm1-harbor.

## Context

EXECUTION.md asks M3-03 to "extract shared host DSP through a documented upstream path instead of silently
forking copies". The host's processing section has a tape echo (`firmware/src/hb_echo.c`, seqdock only, already
copied here by take.py as `engines/hb_echo.c`) and a reverb (`firmware/src/hb_fx.c`). Both are written for the
host's role and cannot be called through `core/effect.h` as they are:

- One global instance each (file statics), so two echoes or an echo plus a host echo would share state.
- The echo processes only whole groups of four frames and drops a remainder; the effect API requires spans of
  1..256 with partition-independent output.
- The echo takes a mono sum and adds into the output; the reverb mixes dry and wet in place. Bus processors here
  must take stereo input and produce wet-only output, with gains owned by the mixer.
- Coefficients are handed over by a main-loop/ISR flag protocol inside the host.

At decision time fm1-harbor-proc (HEAD 94f8087ab62e) has uncommitted work by others in `firmware/src/dock.c`,
the only caller of both files, and in several other host and module files. Refactoring the two files into
instance APIs means editing dock.c's processing section inside that uncommitted tree, then rebuilding and
re-qualifying every host path. That is a change to another project's in-progress work, which needs its owner.

## Decision

Implement the four local effects in `effects/` as new, instance-owned code, and port the host's echo and reverb
laws into them with explicit attribution, rather than editing fm1-harbor now:

| Effect | Source of its law | What changed in the port |
| --- | --- | --- |
| `effects/echo.c` | hb_echo.c: quarter-rate line, 0.85 feedback ceiling, tone table, 20 ms spread, tap interpolation | per-instance state; frame-streamed group phase; stereo in, wet out; delay glides; decays to exact zero |
| `effects/reverb.c` | hb_fx.c (Scintilla's reverb at four lines): line lengths, taps, signs, decay, damping and level laws, restoring division | per-instance state; per-sample order; wet-only at full mix; decays to exact zero |
| `effects/filter.c` | new: trapezoidal SVF, low/band/high-pass | n/a |
| `effects/phaser.c` | new: four-stage allpass phaser | n/a |

This is a documented port, not a silent fork: each file names its upstream file and license, the laws and tables
are the same so a setting means the same sound, and `engines/hb_echo.c` stays an unmodified take.py copy that this
module does not compile. Upstream hashes at decision time: hb_echo.c
6eaee129c32f068f53ee0d7cc373cdb0e6be82b38e1f2c586ec673b93e446807, hb_fx.c
4e8824b1b660b7ad0e62d09893b758dc017da8923fbe2f608a7ae6b41a3405d8.

## Consequences

- Two implementations of the same echo and reverb laws exist, one in the host and one here. A future change to
  either law must be made in both, or the upstream path below must be taken first. The tests here pin the laws
  that matter: echo time and spread to within 8 samples, the feedback ratio, the reverb's -30 dB at half its T60.
- Upstream path, proposed and not done: in fm1-harbor, give hb_echo.c and hb_fx.c state structs and span-safe run
  functions, keep dock.c's processing section as their caller, rerun `tests/echo_check.py` and the emulator suite,
  then take.py both files and replace the ported cores here with thin adapters. This needs the fm1-harbor owner's
  agreement and a quiet tree. It is not part of M3-03 and is not in WORK.json: the tracker CLI cannot add tasks,
  and the protocol reserves task additions for a coordinator. It is listed as an open follow-up in the claude
  handoff (work/agents/claude-code.md) and reported to the owner; whoever coordinates next should add it as a task.
- The host reverb must still be requested off by M3-04 (decision 002) so a project's local reverb does not double.
