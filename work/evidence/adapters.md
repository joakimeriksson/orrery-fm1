# M1-04 — Existing-engine instrument adapters

Owner: agent-b. Date: 2026-11-05. Status: submitted for agent-a review.

Implemented descriptor/callback adapters in instruments/arp.*, eminent.* and minipops.*, stable catalog IDs in
builtin_ids.h, and small adapter-local physical-token helpers in adapter_common.h. These use the real common
instrument wrappers, not a second registry or logical-source router. No legacy module integration occurred.

ARP exposes all35 non-reverb sound fields through metadata; IDs1..12 retain the initial curated controls and
IDs13..35 append the other fields. Wave/LFO/octave selectors carry enum labels, pulse width and sync carry
correct narrower bounds, and normalized engine controls stay0..100 (detune50 is centre). Full sound recall is
36 explicit bytes in documented field order, with byte33 reserved zero for session reverb. All10 embedded
preset payloads/names are checked independently against imported ARP_PATCHES, except deliberately zero reverb.
Accent amount is retained as a sound parameter; explicit accented/slide/legato events remain unsupported until
upstream performance controls are added in M1-05. The global glide knob retains existing engine behavior.

Eminent exposes registration/attack/release/ensemble/phaser/color/level, preserves integrated ensemble/phaser,
and has four registration presets. Its eight-byte explicit sound payload reserves reverb byte6 as zero.
MiniPops exposes level/balance, one kit preset and all14 stable kit voice IDs/names/default MIDI mappings;
its sound payload is two bytes. Direct HIT dispatch converts velocity to engine Q16 and never starts its rhythm
player. Rhythm combinations/preset sequences remain separate future pattern-import work.

ARP and Eminent do not claim velocity response (their upstream note paths ignore velocity). ARP claims MONO,
Eminent POLY16, MiniPops PERCUSSION+VELOCITY14. Unsupported accent/slide/bend/pressure behavior is not invented.
Adapters track only resolved physical token-to-pitch transitions; the common router owns sources, sustain,
duplicate logical pitches and mono priority. Stale physical offs are harmless. Reset clears DSP history and
physical bookkeeping while preserving sound; ordinary off preserves the underlying engine release.

## Validation

```sh
python3 tests/run_native.py adapters --sanitize on --build-dir build/native-agent-b-adapters
```

PASS with strict C11, ASan and UBSan, no recovery or suppression. Tests cover all15 presets' round trips,
independent ARPs, all35 ARP parameter min/max-to-wire mappings, every settings byte invalid-range rejection,
version/capacity errors without state mutation, explicit capability rejection, token/stale-off handling,
16 Eminent held pitches, all14 simultaneous MiniPops hits, disabled internal clocks and reset sound retention.
Same-span adapter output compares bit-for-bit with direct raw-engine calls for all three engines.

Representative default-sound comparison:8192 frames, baseline256-frame spans versus partitions1/17/64/255/256:

| Engine / partition | Peak raw Q15 delta | Sum absolute error / sum baseline magnitude |
| --- | ---: | ---: |
| ARP /1 | 811 | 0.001001768 |
| ARP /17 | 66 | 0.000108972 |
| ARP /64 | 0 | 0 |
| ARP /255 | 17 | 0.000072542 |
| ARP /256 | 0 | 0 |
| Eminent /all five | 0 | 0 |
| MiniPops /all five | 0 | 0 |

ARP's existing control coefficients refresh every8 samples relative to each render call, so arbitrary partition
changes are not bit-exact. The default-patch regression explicitly allows <=1024 raw Q15 peak error and <=2%
mean absolute relative error. Initial guessed peak bound256 failed at811 and was replaced by this stated,
measured tolerance; that is not proof for every patch or a sample-partition-invariant engine. The differences
remain a future upstream cadence/performance concern. No cadence change is hidden in these adapters.

Sanitizers initially exposed pre-existing signed-negative-shift UB in imported ARP/MiniPops. M1-04 was blocked
until M1-06 fixed those paths upstream, passed standalone checks and imported exact copies with take.py. The
final clean sanitizer run above uses those accepted imports. No engine copies were forked or modified by this
task. See engine-arithmetic.md for source hashes and old/new raw-render parity evidence.

No target CPU/RAM/cold-start/audio-budget certification, firmware deployment, effects graph, sequencer/recorder
or complete musical workstation is claimed by these native adapter checks. Those remain later integration gates.
