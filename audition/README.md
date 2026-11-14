# meloDock synth + sequencer audition

An isolated test module: new portable sequencer/router driving real ARP bass,
ARP lead, Eminent strings and Minipops drums. SEL replaces the lead with the new
DROP/PING/BUBBLE/CHIRP/METAL electronic FX instrument. Four parts throughout.
Dry fixed-gain mix; production session, mixer/effect graph and editor remain planned.

Run from the repository:

```
python3 tools/run_seq_demo.py
```

In the emulator press HOME to open the picker, turn PRESETS to MELODOCK TEST,
then press HOME to select it. Press PLAY to start/stop.

- SEL switches lead/FX and restarts at bar one.
- SEQ queues the next of four patterns at the next bar.
- ALGO changes tempo (40–180 BPM).
- ENV/GLO/EDIT/LFO mute bass/lead-or-FX/strings/drums.
- ARP stops and cuts all voices. STOP lets release tails decay.
- Hold HOME 1.5 seconds to return to the module picker.

`python3 tools/run_seq_demo.py --check` records a scripted emulator session under
`build/seq-demo/emulator`. `python3 tools/render_seq_demo.py` validates and exports
18-second native mixes and stems under `build/seq-demo/native` (16 seconds playing,
2 seconds release). `lead-0.wav` and `fx-0.wav` are mixes; suffixes1–4 are stems.

Rebuild from the sibling fm1-harbor-proc directory:

```
PYTHON=/Users/joakimeriksson/work/fm1-harbor/.venv/bin/python3 HB_MOUNT=/Users/joakimeriksson/work/seventies-fm1 ./build.sh --no-package --seqdock --only --modules /Users/joakimeriksson/work/seventies-fm1/audition
```

Copy its `build/seqdock_xip.bin` to this repository's
`build/seq-demo/melodock-test_xip.bin` before launching. This workflow uses the
emulator only; it does not deploy to an FM-1.
