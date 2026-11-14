# M3-05 audio budget of the vertical slice

Task M3-05, owner `claude`, 2026-11-06 UTC. Measured in fm1-emu on the seqdock image (the host's render timer:
256-frame blocks, 139,160 ticks of the 24 MHz counter each). Nothing was run on hardware: these are emulator
figures, and the emulator's cycle model is the only target timing available here.

## How it is measured

    python3 tests/stress/run_stress.py [variants]      # builds each variant, plays it, prints worst/last/overruns

`tests/stress/stress_common.h` builds the real module (module/seventies.c is compiled into the variant) with a
stress case set up through its `MD_STRESS_HOOK` (compiled only there). Variants 5 and 7 add timing proxies on every
instrument render and bus, plus `MD_SESSION_PROFILE` stage timers in core/session.c; they call the host's tick
counter inside render, which production code does not, and exist only to attribute cost.

## Supported combinations (the claim)

Every figure is the worst block over the run; the budget is 70 %. All builds use `-O2` (see below).

| Case | Worst | Within budget |
| --- | ---: | --- |
| **Supported stress case** (variant 6): MAGNETIC II at 138 BPM in sixteenths with 48 cutoff locks on the bass, a held lead note, the strings' four-note chord patterns, Rock 1 on the drums, echo and reverb on with sends from every track | 63.2 % | yes |
| Supported case for ten minutes, then stop (variant 4) | 68.2 % | yes |
| Supported case with the electronic FX replacing the lead (variant 8) | 58.7 % | yes |
| Supported case with the master sweep filter active (variant 10) | 66.5 % | yes |
| The shipped demo with a lead note (tests/integration/seventies_emu.py, both images) | 63.6 % | yes |

The supported set is: the four instruments as SEVENTIES binds them (or the electronic FX in place of one part),
four string voices (the chord patterns), one rhythm key without the extra rhythms, the echo and reverb buses, and
the master filter. Inserts and denser drums are not in it:

| Case (15 s unless stated) | Worst |
| --- | ---: |
| Supported case plus a filter insert on the lead and the phaser on the strings, master filter on (variant 9) | 76.5 % |
| Dense band: sixteen string voices, four rhythm keys with all three extra rhythms (variant 1) | 79.4 % |
| Dense band with the electronic FX replacing the lead (variant 3) | 76.8 % |
| Dense band with both inserts and the master filter (variant 2) | 94.7 % |
| Supported case with every rhythm key and the extras (variant 11) | 78.1 % |
| Supported case with two keys and the extras (variant 12) | 69.9 % |
| Supported case with Beguine (the densest single rhythm) and the extras (variant 13) | 69.9 % |

## What the shipped panel can reach (review finding)

The panel has no locks and no insert pages, and its chord patterns hold four voices; but the DRUMS page could
combine all ten rhythm keys with the three extra rhythms while the band plays. The reachable worst cases, with the
fastest sequence (MAGNETIC II, 138 BPM), the held lead and echo and reverb on, no locks:

| Case | Worst |
| --- | ---: |
| Two keys (Beguine and Cha-cha) with the extras (15 s) | 71.4 % |
| One key, Beguine, with the extras (variant 14, 15 s) | 69.8 % |
| The same for ten minutes (variant 15) | **75.6 %**, 0 overruns, no stuck notes |
| Beguine without the extras (variant 16, 15 s) | 65.6 % |
| Rock 1, the default rhythm, with the extras (variant 17, 15 s) | 67.3 % |

Ten-minute runs add about five to six points to a fifteen-second figure (rare coincidences of the drums' and the
strings' peaks): only the supported case without the extra rhythms (variant 4, 68.2 % over ten minutes) is shown
to stay under 70 % for long.

Changed: **the DRUMS page now takes one rhythm key at a time** (a key selects its rhythm, the lit key clears it;
the page says "ONE RHYTHM AT A TIME (CPU)"; a saved combination keeps its lowest key). Combinations measured
71 to 78 % in fifteen seconds and would approach overruns over long runs.

**Not resolved:** the panel can still reach dense single rhythms with the extra rhythms, which measured 75.6 % over
ten minutes: above the 70 % target, with no overrun. Removing the extra rhythms from the panel, or the denser
rhythms, would fit the budget at the cost of the machine's character. The capacity remedy is not in the module's
own code (session, mixer and buses are about 12 % of a block): it is the engines (the Eminent's fixed cost, the
Mini Pops' peaks) or running an instrument on the second core, as CS-1 and Scintilla do through the host's
c1_run. Both are decisions for the owner. EXECUTION.md's M4-02 acceptance now requires panel admission to keep the
module inside the measured budget.

## Where the time goes

Variant 7 (the supported case, profiled), per block, thousandths of the block:

| Part | Average | Worst |
| --- | ---: | ---: |
| Strings (Eminent, four voices) | 201 | 318 |
| Bass (ARP 2600) | 60 | 62 |
| Lead (ARP 2600, one held note) | 57 | 114 |
| Drums (Mini Pops, Rock 1) | 52 | 119 |
| Reverb bus | 37 | 37 |
| Echo bus | 14 | 14 |
| Mixer (tracks, sends, returns, output), from the stage timers less the buses | about 68 | |
| Sequencer, router and event dispatch beyond the instruments | about 5 | |
| Clearing four track buffers | 12 | 12 |
| Session total | 507 | 624 |

The engines dominate. The Eminent costs about a fifth of a block even at four voices (its ensemble and phaser),
reaching half a block at sixteen. The dense drums peak at 27 %. These are engine costs in fm1-harbor; reducing them
is upstream work.

## Changes made while measuring

- **`-O2` for the module.** module.cfg and every stress build compile with `-O2` instead of `-Os`: the supported case
  went from 65.6 % to 63.2 %, the demo from 66.2 % to 63.6 %, for code 57,228 to 70,300 bytes (flash is not scarce).
  The engines profit most.
- **Mixer: two passes folded.** For a track whose inserts will not run, the clamp moves after the gain (a gain of
  at most unity cannot enlarge a sample), and with an idle master slot the final clamp joins the output loop. Same
  output within the limit; all mixer tests still pass bit-exactly. Gain was small (0.2 points): the mixer's cost is
  its per-sample multiplies, which compile to native widening multiplies (checked in the disassembly).
- **Phaser clamp moved to 2^23.** `-2^22` is 0xffc00000, which fm1-harbor's hbmod rejects as a chip-ROM address
  reference; the phaser had never been linked into a module before variant 2.
- **Session stage timers** (`MD_SESSION_PROFILE`, off by default) and the module's `MD_STRESS_HOOK` points.
- **A fused all-tracks mixer pass was tried and reverted**: bit-exact, but slower on the target (64.5 % against
  63.2 %), because its per-frame loop over tracks kept the gains in memory.
- **The DRUMS page's one-key cap** (above).

## Ten minutes

Result of variant 4 (supported case, 600 s, then stop and release every live note, then 24 s more):

- 107,673 blocks (about 625 s): worst block 68.2 %, last 55.9 %, **0 overruns**, no strikes.
- The log shows "stress: stopped after ten minutes"; after the stop and release, the last 3 s carry an AC level of
  0.13 LSB RMS (below 1: **no stuck notes**) on a constant offset of -36.
- Peak output 12,628: no saturation, so no clipping and no accumulator wrap. Sample and position counters do not
  wrap within ten minutes (32-bit sample time wraps after 27 hours; the transport keeps musical position apart).
- Module CRC 836e08c3, code 70,964 bytes (the stress hooks add 664 bytes to the module's 70,300).

A first ten-minute run built with `-Os` measured worst 70.1 %, 0 overruns, and after the stop an AC level of
0.13 LSB RMS with a constant offset of -36: no stuck note. The offset is the engines' DC after release (bass 8, lead
10, strings 28, drums 1 at the track meters, natively) carried through the echo and reverb returns, which pass DC.
The check therefore measures the AC level after the stop and reports the DC. A DC-blocking stage at the bus inputs
would remove the returns' share; it is not added here.

## Memory, short spans, stack

| Item | Bytes |
| --- | ---: |
| Module code (`-O2`) | 70,300 |
| Module data + BSS | 16 + 122,276 (of 278,528) |
| RAM code | 0 |
| Session state (natively; includes mixer, two banks, ring, track buffers) | 55,096 |
| Command ring (128 commands of 32 bytes) | 4,096 |
| Echo delay line (12,288 x 16-bit) | 24,576 |
| Reverb lines (4 x 2,048 x 16-bit) | 16,384 |
| Track render buffers (4 x 256 stereo frames) | 8,192 |

Short spans, natively (the module's config, 20 s): per 256 frames, 42.3 us at 256-frame spans, 43.1 at 64, 46.0
at 16, 63.9 at 4 and 117.6 at 1. Spans of 16 frames or more cost within 10 % of whole blocks; single-frame spans
cost 2.8 times as much (per-call engine coefficient work). The host renders whole blocks; short spans occur only
where events split a track, and each track now splits only at its own events.

Stack: native (arm64) `-fstack-usage` gives the deepest frames md_seq_next 432, md_session_render 352,
effect_in_use 224, md_session_snapshot 144, reconcile/apply_param/md_mixer_finish 128 bytes; the render path
(render, session render, sequencer iterator, dispatch, router, adapter) is about 1.3 KB natively. The target's
stack was not measured; pointers are half the size there.

## Revision after review

reviewer-claude-6 returned the task: the claim that the shipped module stays in the supported set was false,
because the DRUMS page could combine every rhythm key with the extras. The variants 11 to 17 above measure
what the panel can reach; the panel now takes one key at a time; what it can still reach above 70 % is stated
as unresolved, with the remedies; M4-02's acceptance now carries the admission requirement. run_stress.py's
default is the short supported set, and known-unsupported variants report "over budget, as expected"; the
mixer's clamp comment states the over-limit change.

## Limits

- Emulator timing only; hardware cost unverified.
- Only one ten-minute run per build; rare worst-case coincidences can exceed a fifteen-second run's worst, as the
  `-Os` run did (70.1 % over ten minutes against 65.6 % in fifteen seconds).
- The unsupported cases above need engine work upstream or a lighter insert graph before they can be offered.

| File | SHA-256 (after revision) |
| --- | --- |
| tests/stress/stress_common.h | c77648aa5d942064901811737a686003f82ce9e372ef999c0b2e68719827611f |
| tests/stress/run_stress.py | 69a8ea1a639f157fbbfe60b7bcf97348f85bf60a9cd043019b984c3b46ea55e3 |
| module/seventies.c | 89f0fb98c9550679dfc90f773152e4ffb4de840d30e6597611da0bd4dba0e521 |
| module/module.cfg | 02a002dc57a23f93a876a30e25530bc4f8e0a7a425fbf5b11156d2294f7642c4 |
| core/mixer.c | ece51942b8c807a18659c29aaddbb4eb915582987e77e54f4ec809ee5cb35fd0 |
| core/session.c | 46b6f5a67c00e800973fb43c6132313ce6392b112ef81d2cb7f5f929d1e49f68 |
| core/session.h | bc8448a74936afd037cfebd8e9cde9b2e20ba13abf83cd5a5fdc2292557f7576 |
| effects/phaser.c | 3267dfa8273fb9f27417bbf02b074409f65b9064842a95055dffa9b00cab1dfd |

## Remeasured 2026-11-07: five tracks, the strings on the second core

What changed since the figures above:
- The strings render on core 1 through c1_run/c1_wait (core/session.c, config.parallel_track). Core 0 renders
  the other tracks at the same time.
- The effects box is a single electronic-FX instance on track 4, so MD_TRACKS is 5.
- Stress builds start in the BAND scene through a new stress-only hook phase. A first rerun without it measured
  AMBIENT instead and is discarded.

`python3 tests/stress/run_stress.py 6 8 10 14 4 15` (build/stress_rerun2.log). All runs had 0 overruns:

| Variant | Case | Worst block | Before |
| --- | --- | ---: | ---: |
| 6 | supported stress case | 61.6 % | 63.2 % |
| 8 | 6 with the electronic FX as the lead | 57.2 % | |
| 10 | 6 with the master filter | 64.9 % | |
| 14 | worst the panel reaches: Beguine on lever B with all extras, C MIN PULSE at 138 | 66.7 % | |
| 4 | 6 for ten minutes, then stopped | 64.3 % | 68.2 % |
| 15 | 14 for ten minutes, then stopped | 68.3 % | 75.6 % |

Both ten-minute runs ended with AC RMS 0.45 in the last 3 s (no stuck notes) and the known DC offset of -36.7.

The emulator logs core 1's job: worst 29.1 % of a block in AMBIENT with lead notes. The second-core path is
emulator evidence only, not tested on hardware.

Production images (CRC 334fb18d) measured in tests/integration/seventies_emu.py: BAND 60.4 % on both images.
AMBIENT with lead notes measured 59.3 %.

The residual over-budget reachable case reported above (variant 15) is now within budget, provided core 1 is
available. Without it, the session renders the strings on core 0 and the earlier figures apply. A host without
c1_run still has the old exposure.
