# seventies-fm1

The seventies' synths of a Jarre record in one module for the Hügelton FM-1 under
[fm1-dock](https://github.com/joakimeriksson/fm1-dock), so that they play together: the dock runs one module
at a time, and a band needs its strings, lead, bass and drums at once.

| part | engine | after |
| --- | --- | --- |
| **strings** | `engines/em_engine.c` | the Eminent 310 string ensemble through the Electro-Harmonix Small Stone: the bed under Oxygène and Equinoxe |
| **lead** | `engines/arp_engine.c` | an ARP 2600, mono, on the keys, with ten patches in the manner of the records (LASER SYNC, ARP LEAD, ...) |
| **bass** | the same engine again | a second 2600 with its 1601 sequencer: eight sixteen-step sequences with their patterns (two bars as written, one a step up, ...) and a filter sweep |
| **drums** | `engines/mp_engine.c` | the Korg Mini Pops 7, from its service manual |
| **echo** | `effects/echo.c` | a tape echo at a division of the tempo, fed by the strings and the lead; the bass and the drums stay dry, as on the records |
| **effects box** | `instruments/electronic_fx.c` | thirteen background sounds in stereo (droplets, glass, shots, gulls, sweeps, surf, thunder), a wind, and a sequence of its own (FX SEQ below) |
| **pads** | `engines/ens_engine.c` | the strings' other voice: Caravel's ENSEMBLE, fourteen string / choir / dark pads in a stereo three-tap ensemble (ALGORITHM on the STRINGS page past the Eminent's four registrations) |
| **chords** | `module/seventies.c` | a chord a link of the bass pattern: the strings move with the bass, nothing to keep in time |

The reverb is the module's own (`effects/reverb.c`, Scintilla's at half the lines), on a send from every part.
The ARP 2600, Eminent and Mini Pops engines are copies: they are written as modules of their own for fm1-dock
(ARP 2600, EMINENT, MINIPOPS, on its `seventies-engines` branch until they are in its catalogue), where their checks
against measurements are; `./take.py` copies them in again, and the Synthex with them (Scintilla's engine: the laser
harp). The exception is `engines/ens_engine.c`, ported from [Caravel](https://github.com/bonszaimusic/caravel) 0.11.0's
ENSEMBLE by bonszaimusic (GPL-3.0-only), with the DSP blocks it uses from
[Felucca-Engines](https://github.com/hugelton/Felucca-Engines) by Leo Kuroshita / Hugelton Instruments (MIT); its
tables come from `tools/gen_ens_tables.py`. It lives here, take.py does not copy it.

The next-stage workstation design is in [PLAN.md](PLAN.md): a reusable instrument API, shared multitrack
sequencer, recording, songs and projects, with a Melodee reuse decision before implementation. The behavior
below describes the current module.

Implementation tasks and the two-agent workflow are in [EXECUTION.md](EXECUTION.md). Run
`python3 tools/work.py status` to see current work, owners and the next ready tasks; read [AGENTS.md](AGENTS.md)
before claiming work. Progress and handoffs live in `WORK.json` and `work/agents/`, not only in chat.

## The panel

| | |
| --- | --- |
| **EDIT** | STRINGS: KNOB1..4 attack, release, chorus, phaser; ALGORITHM the registration; FX the phaser's colour |
| **PRESETS** | on every page: the setup, round the fifteen (below); a banner names it for two seconds (the header says PRESETS: SETUP) |
| **GLO** | LEAD: ALGORITHM the patch; KNOB1..4 cutoff, resonance, glide, level; SELECT the run: OFF, PENTA / SCALE / CHORD, each FAST (70 ms a note) or 1/16 of the tempo; glide off and, on the soft lead, a short attack while a run plays. With a run on, a key (or a MIDI note) plays a rise from itself through the chord's scale, two octaves up, and holds the top while held |
| **ENV** | BASS: ALGORITHM the patch, SELECT the sequence (its pattern and tempo with it); KNOB1..4 cutoff, sweep, tempo, level |
| **LFO** | DRUMS: the white keys from F3 are the rhythm keys (together they combine), F#3 G#3 A#3 the tambourine, guiro and quijada, OCT−/OCT+ the lever; ALGORITHM one rhythm; KNOB1..4 tempo, level, balance, the extras' level |
| **SEQ** | CHORDS: SELECT the link, KNOB1 its root, KNOB2 its kind (−, MIN, MAJ, MIN7, MAJ7, SUS2, SUS4, 5TH), KNOB3 the strings' octave, KNOB4 their level. **REC**: LATCH, the keys set the chord under the cursor instead of playing the lead |
| **FX** | MIX: KNOB1..4 strings, lead, bass, drums; ALGORITHM the reverb. FX again: PARTS: KNOB1..4 wind, its colour, the effects box, the lead's echo; the keys play the effects box: the white keys from the lowest F are gestures through the chord's pentatonic, two octaves from the key: RISE, FALL, ARC (up and back), SPARKLE (glass pings), DROPS UP, DROPS DOWN, METAL FALL, BUBBLE RISE, then round again an octave up; SELECT their speed, −4 (four times slower) .. +4 (three times faster), live on a gesture already playing; SEL the background parts in AMBIENT (none, the box's run, the lead's phrase, both); the first F# a wind gust while held, the other black keys single hits (DROP, PING, BUBBLE, CHIRP, METAL, ZAP, WHISTLE, GULL, TWITTER, BLEEPS, WHOOSH, SURF, THUNDER round). FX again: FX SEQ, the effect sequence (below). FX again: ECHO: ALGORITHM the time, KNOB1..4 feedback, tone, the strings' send, the lead's send |
| **PLAY** | the band: the bass and the drums start together and stop; the chords follow the bass's pattern |
| **ARP** | all notes off (on FX SEQ: how the event is played). **SAVE**: the settings now (else two seconds after a change). **HOME** held 1.5 s: fm1-dock's list |
| **keys** | the lead; OCT−/OCT+ its octave. MIDI notes play the lead too, PC picks its patch, CC 74 its cutoff |

**On an SMK-37** (or any controller: these are MIDI). The pads, channel 10 notes 36..51: 1..5 mute the bass, the
lead, the strings, the drums and the effects box; 6 unmutes them all; 7 the next setup; 8 starts and stops the band;
9..13 hits of the effects box (DROP, PING, BUBBLE, CHIRP, METAL); 14 a wind gust while held; 15 and 16 two glass
bells. CC 70..73 are the levels of the strings, the bass, the drums and the effects box, CC 7 the lead's, CC 1 (the
mod wheel) closes a low-pass over everything, CC 64 holds the lead.

## The setups

Fifteen scenes to play a lead over, in the manner of the French electronic records of 1976-86 (none copied, and no
name a title): PRESETS steps through them, SAVE keeps them all as edited.

| | | |
| --- | --- | --- |
| 1 **DRIFT** | ambient, C minor, 75 | the string machine over a far Bossa Nova, droplets, a run up each chord |
| 2 **PULSE** | band, E minor, 120 | the Eminent through its slow phaser, the root-and-octave bass, Rock 1, the 2600 lead |
| 3 **SPIRAL** | band, E minor, 128 | a plucked arpeggio under a huge ensemble, no drums, high wind |
| 4 **OCTAVES** | band, D minor, 130 | violins up high over the octave-answer line, Rock 3 |
| 5 **MAGNET** | band, C minor, 138 | the fast pulse under low cellos, a quiet Rumba, the sample-and-hold lead |
| 6 **LASER** | ambient, E minor, 70 | the laser harp plays a slow phrase of its own on a dusk orchestra |
| 7 **TIDES** | ambient, A minor, 66 | a glass choir, low wind in long swells, waves coming in, gulls, an arc of glass up and back |
| 8 **NIGHT** | ambient, D minor, 58 | an abyss pad, deep wind, far signals in a long dark echo |
| 9 **DAWN** | ambient, F major, 80 | open chords on vintage strings, a slow bell line, birds |
| 10 **RAIN** | ambient, G minor, 72 | a breath choir under steady droplets |
| 11 **CAVERN** | ambient, B minor, 54 | a deep drone, metal and low bubbles far off |
| 12 **BREATH** | band, A minor, 104 | the bass line with rests in it, full Eminent through the phaser, a far Bossa Nova |
| 13 **STORM** | ambient, F minor, 62 | a slow swell, thunder rolling far off, rain, a low wind |
| 14 **ORBIT** | ambient, F# minor, 68 | the glass choir an octave up, sample-and-hold bleeps, sweeps, a scale of glass up and one down |
| 15 **COMET** | band, A minor, 134 | the up-and-down arpeggio walking down a fourth, a pad on bare fifths, Rock 4, shots and sweeps |

In each, one part is in front (the strings; the lead when it is played), the bass 5 to 8 dB under it, the effects
box and the wind under that, and every setup plays at the same loudness, so stepping through them does not jump.
The strings' pads and the 2600's lead patches are trimmed to one level each, so the STRINGS and LEAD faders mean the
same whatever the voice.

**Editing a setup.** On the device, every page edits the setup that plays (chords on CHORDS, voices on STRINGS /
LEAD / BASS, levels on MIX and PARTS, the echo on ECHO). The factory setups are one table in
[`module/setups.h`](module/setups.h): a block of named fields each (key, chords, voices, rhythm, levels, echo, hall,
effect pattern), and the effect patterns above them. A bank saved on the device is used instead of the table, until
the record's layout changes (as it did with the effect sequence: older saved setups are replaced by the factory's).

**FX SEQ, the effect sequence.** What the effects box plays by itself is sixteen events on a line of the setup's
sixteen bars (BAND: eight), each a hit of one of the box's thirteen sounds, ringing out as that sound's own recipe.
Struck: DROP (a falling droplet), PING (a glass bell), BUBBLE, CHIRP (a rising call), METAL (a clang), ZAP (a shot
falling three octaves). Long ones that swell and fall away: WHISTLE (a tone gliding down for two and a half seconds),
GULL (a cry), TWITTER (an electronic bird), BLEEPS (sample-and-hold steps). Of noise, where the note is the colour:
WHOOSH (a resonant sweep upwards), SURF (a wave in and out, four seconds), THUNDER (a crack falling into a long roll).
The box is stereo: each sound takes a place of its own, and the shots, whistles, sweeps and waves travel across. SELECT moves to an event (and plays it); KNOB1 is when (bar.eighth), KNOB2 the sound
(or none: a free slot), KNOB3 the note, KNOB4 its chance; a key sets the note. **ARP is how the event is played**:
ONCE (a single hit), or a gesture of that sound starting from its note: UP the scale, DOWN, ARC (up and back) or
SPARKLE (its notes at random). A struck sound's gesture is a scale of eleven hits over two octaves in sixteenths (an
arc is back within the bar); a long sound's is a few, well apart: three whistles, four gulls, two waves. So scales
running up and down in the background are events like any other: a sound, and how it is played. A gesture shows as
a taller mark on the line. Notes are steps of the setup's
pentatonic (the first chord's: major on a major chord), so a sequence is in key whatever is done to it, and follows
the chords when they are changed. ALGORITHM puts a whole pattern there: EMPTY, DROPLETS, SPARSE, RAIN, BELLS,
BUBBLES, SIGNALS, CAVERN, SHORE (waves and gulls), STORM (thunder and rain), BIRDS (bells and twitter), COMPUTER
(bleeps, sweeps, a whistle), LASERS (shots and sweeps, nothing that holds a pitch), SPACE (sweeps and whistles), SCALES (glass scales up and down
in every second bar), CASCADES (gestures of droplets, glass, shots, gulls and bubbles), and YOURS, the sequence as it was last edited (kept while the others are tried, until the
power goes). SEL solos the box. The line shows each event at its time, higher for a higher note, in its sound's
colour, with the cursor and the bar that plays.

**Listening on the Mac.** `uv run --with numpy --with pillow tools/listen.py [SETUP ...] [--png]` builds the module
itself against a host of its own (`tools/render_setups.c`: no Docker, no emulator; a second a setup), plays each
setup and each of its parts alone, and prints every part's level, its loudest 50 ms, its pitch classes and where
the mix's energy sits. It is how the setups were balanced, and how a wrong note shows.

## State

A preview, not a release yet. It builds against fm1-dock's main, and in fm1-emu every setup plays without an overrun
(worst block 46 .. 65 % of the time it has). On hardware (an SMK-37 Pro, 2026-11-14) it runs and all fifteen setups
play, but the main core is at 75 .. 79 % in the fast setups and a busy moment (a lead note on a chord change) can
take a block over: 42 overruns in three minutes of playing through all fifteen. Measured there on one core, in
percent of a block: the mixer with its buses 25, the strings 9 .. 23, the bass 9, the lead 8 idle, the effects box
4 .. 7 (40 at a gesture's start). The work left is there: the split between the cores, and the mixer.

## Build

In an [fm1-dock](https://github.com/joakimeriksson/fm1-dock) checkout, with its toolchain (Docker on a Mac), and
this repository beside it:

    HB_MOUNT=$HOME/work/seventies-fm1 ./build.sh --no-package \
        --modules cs1,scintilla,$HOME/work/seventies-fm1/module

gives `build/modules/module.dock`, the module to deploy (`tools/dock.py deploy build/modules/module.dock`, or
fm1-dock's page: your own module over USB), and `build/fm1-dock_xip.bin` for fm1-emu with the module in its list.
The module is written for fm1-dock 0.8 or later on an FM-1 or an SMK-37 (the second core, indexed banks).

Checks, none of which needs the toolchain: `python3 tests/run_native.py` (the units, with the sanitizers) and
`tools/listen.py` above. With the image built: `python3 tests/integration/seventies_emu.py` (the module in fm1-emu:
no overruns, its worst block under 70 % of the time it has).

## Demo

`demo.py` plays a minute of it in fm1-emu and records it (`build/demo/demo.wav`): the band of the PULSE setup, the
chords with it, the lead over it.

## Licence

GPL-3.0-only (`LICENSE`); who made what, and the MIT notice for the DSP blocks in the pads' engine, are in
[LICENSING.md](LICENSING.md).
