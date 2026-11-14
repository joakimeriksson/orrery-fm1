# M2-04 — Electronic FX instrument

Owner: agent-b. Date: 2026-11-05. Status: submitted for agent-a review.

Implemented instruments/electronic_fx.h/.c as a new instrument using the accepted whole-instance API and stable
builtin ID. Eight fixed voices, POLY+PERCUSSION+VELOCITY, five preset/kit recipes: DROP, PING, BUBBLE, CHIRP and
METAL. It can replace an existing part; it does not add a track or a bus effect. No imported engine or shared
runtime code was changed for the generator.

## Sound and event behavior

- DROP: sine-like voice with a fast falling pitch envelope and decaying amplitude.
- PING: short noise/impulse excitation of a bounded resonant state-variable filter. Frequency is capped near
  5.5kHz for coefficient stability; resonance maps to bounded damping and state is explicitly bounded.
- BUBBLE: rounded tone with pitch-envelope motion and a12Hz pitch wobble under modulation depth.
- CHIRP: fast rising pitch envelope in its factory preset and a small second harmonic.
- METAL: FM phase modulation plus ring-modulated partials, with bounded depth and decaying amplitude.

All recipes have trigger-time tone filtering, pitch/amplitude envelopes and velocity/level gain. These are new
sound recipes, not claimed reproductions of a specific recording or historic circuit. Tone/resonance/modulation
parameters affect the relevant recipe topology; there is no imported effect-processor stack or embedded echo/
reverb. Shared spatial effects remain future mixer sends.

NOTE_ON uses the selected recipe and the incoming MIDI pitch. HIT IDs0..4 select recipe topology and use the
current sound's base pitch and other macros; HIT does not automatically load a different preset's settings.
The five factory presets provide distinct complete macro settings. All settings are snapshotted into the new
voice at trigger time: a step lock changes the event it creates without rewriting previously ringing tails.
Parameters expose recipe, kit pitch, decay20..4000ms, sweep-48..48semitones, sweep time5..1000ms, tone,
resonance0..90, modulation, level and randomization. The explicit version1 settings payload is16 bytes including
a stored32-bit seed; evolving RNG/history/voice state is never serialized.

Each instance owns its RNG; each ping voice owns its noise RNG. Settings/reset restore the stored seed. Every
sample advances only voice-local state, with no allocation, host calls, floating point or64-bit sample division.
Sine and pitch tables are mathematical values, generated from sine/equal temperament rather than copied DSP.
The envelope decrements by at least one fixed-point unit to avoid a stuck residual, and voices end by8seconds.
Normal note-off initiates a short release; reset clears voices/history while retaining the sound.

The generator retains physical pitched token/held state, while the common router owns logical sources. It can
steal a transient or released tail, never a held pitched owner. If all eight physical slots are held, HIT returns
CAPACITY without mutating voice/RNG/settings state. M1-07 updated the router to report that benign transient
admission failure without flushing the existing melody. Invalid/capability errors are similarly nonmutating.

## Native validation

```sh
python3 tests/run_native.py electronic_fx --sanitize on --build-dir build/native-agent-b-electronic-fx
```

PASS strict C11, ASan/UBSan without recovery/suppression. Five scenarios cover:

1. Descriptor/preset/settings round trips, invalid payload/version/parameter and unsupported slide rejection.
2. Eight held pitched owners survive a refused hit; after one release,100 successive transient steals leave
   the other seven owners intact; stale off and panic behave correctly through the actual router.
3. Independent instance RNG/state and bit-identical output plus state for a mixed timed note/hit/off stream
   across partitions1/17/64/255/256 and irregular1..256. No RNG or clock depends on block boundaries.
4. All presets produce finite audible energy, stay below32768 peak singly and decay to exact digital silence;
   render guard words stay intact. Each preset is run for9seconds, then a further silent block is checked.
5. Eight simultaneous voices at maximum decay/resonance/modulation/level remain bounded and end; changing base
   parameters does not mutate an already-triggered voice's snapshot.

| Factory recipe | Single-voice raw Q15 peak | Active voices after9s |
| --- | ---: | ---: |
| DROP | 19449 | 0 |
| PING | 5168 | 0 |
| BUBBLE | 20145 | 0 |
| CHIRP | 14303 | 0 |
| METAL | 17040 | 0 |

Native peak bounds are checked before any export normalization; no normalization was used. Extra observed
positive zero-crossing frequency estimates: DROP falls from about1051Hz over0..10ms to523Hz over40..80ms;
CHIRP rises from about983Hz to2093Hz over those windows. These are short-window observations, not pitch-tuning
certification for every setting. Spectrogram inspection shows the rounded/drop fundamentals, isolated ping,
bubble wobble and multiple metallic partials; screenshots do not establish subjective quality or hardware sound.

## References and reproduction

Three-second, unnormalized stereo44100Hz16-bit references:
[DROP](../../build/electronic-fx/DROP.wav), [PING](../../build/electronic-fx/PING.wav),
[BUBBLE](../../build/electronic-fx/BUBBLE.wav), [CHIRP](../../build/electronic-fx/CHIRP.wav),
[METAL](../../build/electronic-fx/METAL.wav). Spectrograms have matching `.png` names; the inspected montage is
[recipes.png](../../build/electronic-fx/recipes.png). Fingerprints are in `build/electronic-fx/artifacts.json`;
render output and pitch observations are in `render.log` and `pitch-observations.json`. Build artifacts are ignored
and regenerable, not assumed durable.

```sh
mkdir -p build/electronic-fx
MD_FX_RENDER_DIR="$PWD/build/electronic-fx" python3 tests/run_native.py electronic_fx --sanitize on
```

The test additionally writes exactly3seconds of each recipe as NAME.s16le. Convert to WAV using stdlib Python:

```python
from pathlib import Path
import wave
for p in Path('build/electronic-fx').glob('*.s16le'):
    with wave.open(str(p.with_suffix('.wav')), 'wb') as out:
        out.setnchannels(2); out.setsampwidth(2); out.setframerate(44100)
        out.writeframes(p.read_bytes())
```

Run the existing `tools/spectrogram.py NAME.wav NAME.png NAME 5` with a Python containing NumPy/Pillow. This
review used `/Users/joakimeriksson/opt/anaconda3/bin/python3`; no package installation was needed. The montage
simply arranges those five images for inspection.

No firmware/module integration, target cycle/RAM/stack qualification, physical listening test, effects routing,
factory song or actual instrument selector UI is claimed here. Target budgets and complete composition workflow
remain later milestones. The voice filter is part of this instrument's sound, not the shared M3-03 effect DSP.
