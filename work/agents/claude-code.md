Updated (UTC): 2026-11-06

Owner / task ID / task status: `claude` (Claude Code session taking over from the Codex pair). Reviews were done
by fresh independent reviewer agents named reviewer-claude, reviewer-claude-2, -3, -4 (one per task), each given
the code and acceptance criteria rather than the implementer's conclusions.

| Task | Status | Evidence |
| --- | --- | --- |
| M3-02 fixed mixer graph | DONE (reviewer-claude) | work/evidence/mixer.md |
| M3-01 audio session and command handoff | DONE after one revision (reviewer-claude-2) | work/evidence/session.md |
| M3-03 filters, phaser, local echo/reverb | DONE after one revision (reviewer-claude-3) | work/evidence/effects.md, work/decisions/006-local-effects.md |
| M1-05 explicit ARP accent/legato/slide | DONE (reviewer-claude-4) | work/evidence/arp-performance.md |
| M3-04 switch the module to the session | DONE after one revision (reviewer-claude-5) | work/evidence/module-session.md |
| M3-05 measure the audio vertical slice | DONE after one revision (reviewer-claude-6) | work/evidence/audio-budget.md |

Current result and changed paths: core/mixer.*, core/session.*, core/commands.*, effects/*, module/band.*,
module/seventies.c, module/module.cfg, module/string.h, instruments/arp.c, instruments/minipops.*,
tests/native/{mixer,session,processors,band}_test.c, tests/native/adapters_test.c, tests/integration/seventies_emu.py,
tests/stress/ (stress builds and run_stress.py),
tools/gen_effect_tables.py, tools/render_effects.c, legacy/seventies-v1/. Upstream in fm1-harbor-proc:
modules/arp2600/arp_engine.{c,h}, tests/arp_host.c, tests/arp_check.py (all untracked there; originals of the
ARP files were backed up to the session scratchpad before editing).

Validation commands, results and evidence: `python3 tests/run_native.py --sanitize on` (14 suites);
`python3 tests/integration/seventies_emu.py` after building both images; fm1-harbor-proc `HB_APP=seqdock
python3 tests/emu_test.py`; fm1-harbor-proc `python3 tests/arp_check.py`. Results are in each evidence file.

Failed checks / blockers: none open. The six emulator `decode` failures in fm1-harbor-proc predate this work.

Exact next action: milestone M3 is complete. Ready next: M4-01 (editable project model and undo). Before M4-02
can pass, the owner must choose how the shipped panel stays in the audio budget (see below).

Open follow-ups not in WORK.json (adding tasks is a coordinator operation):
- Decision 006: move hb_echo.c/hb_fx.c to instance APIs upstream and adapt here instead of the ported copies;
  needs the fm1-harbor owner and a quiet tree.
- The ARP bass sequence "sweep" (cutoff over the pattern) is not ported: v1 locks cannot cover a 64-step sweep;
  the BASS page shows it as unavailable.
- Link changes are queued by the main loop a bar ahead (md_band_next_link); song sections (M4-03) should replace it.
- Owner decision (M3-05): the panel can still reach Beguine (or another dense rhythm) with the extra rhythms at the
  fastest tempo, 75.6 % worst over ten minutes against the 70 % target (0 overruns). Remedies: drop the extras or
  dense rhythms from the panel, optimise the Eminent/Mini Pops engines upstream, or move an instrument to the
  second core (host c1_run). M4-02 acceptance now requires admission to keep the panel in budget.
- The guiro plays one scrape length (v1 drum hits have no duration); the ARP sequence sweep is not ported.
- Engines leave small DC offsets after release (about -36 at the output after a stop, carried by echo and reverb);
  a DC blocker at the bus inputs would remove the returns' share.

Uncommitted files and integration risks: nothing in this repository is committed (no HEAD). fm1-harbor-proc has
untracked ARP files and many modified files from other work; preserve both when committing. module/seventies.c
was replaced; the original is restored byte-exact in legacy/seventies-v1/ (hash-checked against the baseline).

Question or review requested from other agent: none pending beyond the tracker.

## 2026-11-07: playability changes (owner request: soft strings, one part at a time, editing)

- Defaults (fresh settings only): STRINGS page first; soft strings (attack 75, release 80, ensemble 100, phaser rate
  15, colour on, Eminent level 65, mix 85 %); no drums (choose a rhythm on LFO); bass 55 %; reverb 50 %; strings
  echo send 30 %, tone 40.
- Keys play the page's part (STRINGS/CHORDS: strings, BASS: bass, LEAD and others: lead); each key remembers the
  part it started for its key-up. SEL solos the page's part (again: all); header SOLO, SEL lit. FX held +
  KNOB1/KNOB2: master low-pass cutoff/resonance, bypassed when fully open; FX alone changes page on release.
  Solo and sweep are not saved.
- Checked: native render build/listen/strings_soft.wav (peak 31,413, no clip); emulator: a chord on the STRINGS page
  while stopped sounds (RMS ~1,400), FX+KNOB1 takes 2-8 kHz from +5.9 to -20.9 dB against 100-500 Hz;
  tests/integration/seventies_emu.py and fm1-harbor-proc's emu_test (its seventies case now selects Rock 1 first,
  since the default has no drums) pass; module CRC bf632dbc.
- Not yet reviewed by a second agent; no tracker task covers it (owner-directed change ahead of M4-02).

## 2026-11-07: AMBIENT as a backing bed (owner requests: more parts, a playable lead, one effects box, names)

- Scenes: MIX + PRESETS chooses AMBIENT (default) or BAND. AMBIENT: 75 BPM, slow strings, soft bass, soft Bossa
  Nova, the effects box, and a lead left free for the player (the page opens on LEAD).
- Five runtime tracks (core/param.h MD_TRACKS 5; the v1 project wire layout stays at MD_PROJECT_TRACKS 4, a schema
  decision for M5-01): bass, lead, strings, drums, FX. MD_SOURCE_PANEL is 7; sources 5, 6 and 56..63 reserved.
- The effects box (track 4, instruments/electronic_fx.c, settings v2, 18 bytes; 16 still load, wind off): one
  pattern of eighths per link holds the background run (the base sound, a ping), droplets and chirps (their
  recipe locked on their step: one lock a step is all the sequencer allows). The wind is a continuous layer, not
  a voice: MD_FX_WIND level (swells about 0.74 s), MD_FX_WIND_COLOUR centre, its own 11 s sweep. The module sets
  the wind level while AMBIENT runs, 0 otherwise.
- Second core: core/session.c renders one track (config.parallel_track, the strings) with c1_run/c1_wait while
  core 0 renders the rest; events for it are queued in order; a shared event or full queue makes the block
  serial; a timed-out job is not redone and the core is dropped. session_test parallel_matches_serial: bit-identical
  to one core when the core takes jobs, declines, and after a timeout. Not tested: a mid-block serial fallback.
- Lead sounds: PRESETS or ALGORITHM on LEAD steps SOFT LEAD (ARP LEAD made soft) then the ten patches unmodified;
  MIDI PC 0..10 the same. PRESETS on PARTS turns the background run on and off.
- Names: the ARP patches and sequences no longer carry record titles (renamed in fm1-harbor-proc
  modules/arp2600/arp_presets.h, taken; arp_check passes). PLAN.md "Sound library and editor" and task M4-06
  plan twenty sounds a part and an editor.
- Measured (emulator, worst block): AMBIENT with lead notes 59.3 % (core 1 job worst 29.1 %); BAND integration case
  60.5 % on both images. 14 native suites pass with ASan/UBSan; seventies_emu.py all passed.
- Stress rerun in BAND (work/evidence/audio-budget.md, last section): all within budget; variant 15 (ten minutes,
  worst reachable) 68.3 % (was 75.6 %), resolving the M3-05 residual when core 1 is available.
- Independent review (subagent) of the second-core path and the wind: no race in the normal path. Fixed from it:
  a timed-out job now panics the parallel track on core 0 (it may have stopped mid-event); the test now forces
  the serial fallback (config.parallel_queue = 1: 19 mid-block fallbacks, bit-identical) and a core held before
  its job (track panicked, nothing owned, plays on); the wind's gain reaches low targets exactly; load clamps for
  the new settings; comments on fade time and v1 sounds; session.h documents the shared note pool ordering and
  the Eminent engine's static scratch buffer (one Eminent, on the parallel track).
- Still not done: no tracker task covers this owner-directed work (ahead of M4-02); not tested on hardware.

## 2026-11-08: the SMK-37 as the controller

- The owner runs fm1-dock on an SMK-37. fm1-harbor-proc firmware/src/usb.c: SMK-37 builds (BOARD_SMK37) now
  name themselves "SMK-37 Dock" over USB, not "Felucca". FM-1 builds and the update loader are unchanged. The
  installers' patterns already match "smk"; tools/hbdeploy.py also tries "SMK-37 Dock".
- tests/smk_test.py fails "ble on" and "ble boot" with and without this change. Checked with a build from HEAD's
  usb.c, so the failure is not caused by the rename; the cause is unknown.
- SEVENTIES maps the SMK-37 over MIDI. The pads are channel 10 notes 36..51 (see pad() in module/seventies.c):
  - 36..40 mute a part, 41 everyone back, 42 the scene, 43 PLAY/STOP.
  - 44..48 the box's hits, 49 a wind gust, 50/51 bells.
  The faders are CC 70..73 (strings, bass, drums, box levels), the mod wheel CC 1 (master low-pass), and the
  pedal CC 64 (the lead's sustain). Pitch bend is not mapped: the ARP adapter has no MD_CAP_BEND.
- Checked in fm1-emu with scripted MIDI: PLAY and STOP from pad 43, each hit pad sounds, the wheel takes 2-8 kHz
  down about 67 dB (at travel x7; now x6). The SMK-37 package with SEVENTIES builds at 433,920 of 438,204 bytes,
  and boots in fm1-emu with three slots ok. Nothing sent to the real SMK-37.

## 2026-11-08: setups, editable AMBIENT chords

- Eight setups (module/seventies.c: factory(), setup_to(), save_all(); bank 1 holds magic + 8 x set_t, written
  only on SAVE; the settings record keeps the current one and autosaves as before). PRESETS on MIX steps by the
  turn; SMK pad 42 is the next setup; the header shows the setup. Switching stops, sends everything, restarts.
- Factory setups (descriptive names): DRIFT (AMBIENT 75), PULSE (ROOT PULSE 120, Rock 1), SPIRAL (E MIN ARP 128,
  no drums), OCTAVES (OCTAVE ECHO 130, Rock 3), MAGNET (C MIN PULSE 138, Rumba), LASER (UP AND DOWN 134, Rock 2),
  TIDES (AMBIENT 66, no drums), NIGHT (AMBIENT 58, no drums, no run). BAND chords: a minor chord on each link root.
- AMBIENT plays set.chord[0..7], two bars each (band.c amb_root: F2..E3 at strings octave 1); NONE is a rest;
  no rhythm keys means no drums (the Bossa fallback is gone). The CHORDS page edits them.
- Measured in fm1-emu (each setup selected, then PLAY, 26 s): worst block 48.5..63.2 %, 0 overruns; spectrograms in
  build/listen/setups/. Integration test (now PRESETS +1 = PULSE) passes on both images; band_test ambient_chords.
- fm1-harbor-proc tests/emu_test.py seventies case updated for setups (not rerun since this change).

## 2026-11-08: lead level, contrast, LASER HARP, motion (owner: "no lead heard", "too similar", "not yet Jarre")

- The lead was buried (RMS 693 alone vs a 2,000 band, no measurable rise when held). Lead mixer level 100 in both
  scenes, the bed lowered; SOFT LEAD's second oscillator 30 -> 60. LASER setup: harp over band 1,224 -> ~2,200 RMS.
- LASER HARP (lead sound 1; SOFT LEAD 0; the ten patches 2..11; MIDI PC the same): hard sync (VCO 2 a saw an
  octave up, synced), ENVELOPE PITCH 40 (about 19 semitones thrown up at each strike, falling), filter envelope,
  long ring, vibrato; spectrogram build/listen/emu_harp.png shows the falling sync sweep. Used by LASER.
- BAND setups can have the effects box (set.box: droplets and chirps, band.c, chord[0]'s minor pentatonic, FX
  pattern 0 not linked) and wind (set.lvl_wind while running). Setups re-voiced (strings register/octave,
  drums present or not, extras, echo time/feedback, box, wind): see factory().
- Motion (set.motion, MIX + SELECT): while running, the bass/sequence cutoff opens and closes every 8 bars (depth
  from the sequence's sweep x 0.9, AMBIENT 18) and the strings' warming filter breathes every 16 bars; the warm
  filter is now in BAND when motion > 0. PULSE worst block 67.2 % with it (was 62.8 %); integration 64.7 %.

## 2026-11-08: the Synthex in SEVENTIES (owner: "study scintilla ... implement a laser harp here")

- engines/sc_* are take.py copies of fm1-harbor-proc modules/scintilla/engine (the Elka Synthex). They are built
  through instruments/synthex_synth.c and instruments/synthex_voice.c with instruments/synthex_config.h
  (SC_VOICES 2, SC_LANES 1, SC_HOST_REVERB, SC_SMALL_OSC), so every unit agrees on the structs.
- instruments/synthex.{c,h}: MD_SYNTHEX (id 0x00010005): 80 factory patches, params PATCH and LEVEL, settings v1
  {patch, level}; tests/native/synthex_test.c (Ring mod. sounds, fades, panics, round trip).
- The module binds the lead track's alternate to the Synthex (MD_CMD_BIND in send_scene) when the lead sound is a
  Synthex one. Lead sounds: 0 SOFT LEAD, 1 LASER HARP (Synthex "Ring mod."), 2..11 the 2600's, 12..90 the other 79
  Synthex patches (set.lead_soft 2, set.lead_sx). The LEAD page's cutoff, resonance and glide knobs do not act on
  the Synthex. SX_LEVEL 40.
- Session: config.parallel_mask puts several tracks on core 1 (events queued with their track; tests mode 4/5).
  SEVENTIES renders lead and bass on core 1 (measured best: 58.5 % here with a harp melody over LASER).
- Size: the module code is now 178 KB (was 87), with bss 177 KB. The SMK-37 package fits only without standalone
  Scintilla (cs1 + SEVENTIES).
- Not measured: two-layer Synthex patches (twice the voices) over a band; the other setups since the core split.

## 2026-11-08: the Synthex from dock's merged main (compact patches)

- fm1-harbor's origin/main (ca83cb6) has Scintilla with the factory patches as 25-byte records and its engine built
  without SC_SMALL_OSC. The local fm1-harbor-proc checkout is behind, on codex/flash-chip-compat, with uncommitted
  Scintilla and firmware work (the host-reverb change, also uncommitted in ~/work/synthex-fm1). That work was left
  alone: `./take.py --scintilla-ref origin/main` takes the Synthex files with git show and the other engines from
  the working tree (which holds the renamed ARP patches).
- The adapter decodes records with sc_preset_load into its state and switches each patch's own reverb off. That
  engine always carries the reverb (64 KB of RAM): SEVENTIES bss is 245 KB of 272 KB, code 142 KB (was 178).
  When dock takes scintilla-fm1's host-reverb change, take again and the 64 KB go.
- Same CPU (harp melody over LASER 58.5 % here, core 1 22.5 %). The SMK-37 package fits again with CS-1,
  Scintilla and SEVENTIES.

## 2026-11-09: Caravel's ENSEMBLE as the strings' second voice

The user found Caravel's ENSEMBLE pads (BonszaiMusic's video "Caravel 0.11.0: Streichfett-inspired ENSEMBLE Engine,
All 14 Presets") Jarre-like. Ported, not copied by take.py (it is not an fm1-harbor engine):
- `engines/ens_engine.{c,h}`: Caravel's ens_render / ens_post line for line, Felucca-Engines' dsp.c helpers, and
  Caravel voice.c's ADSR / filter envelope / voice start; six voices, stereo out at -16 dB (ENS_OUT) so a pad sits at
  the Eminent's level. Two products made 64-bit (UBSan: the dark end's ring mod and the filter's low-pass x gl wrap
  in Caravel's 32-bit code). `tools/gen_ens_tables.py` makes `engines/ens_tables.h` from the same formulas.
- `instruments/ensemble.{c,h}` (MD_INSTRUMENT_ENSEMBLE 0x00010006): PRESET, ATTACK, RELEASE, ENS, TONE, LEVEL.
- SEVENTIES: `c.alternate[T_STRINGS] = &ens`, bound in send_scene. str_reg 0..3 Eminent, 4..17 the pads
  (STR_VOICES, STR_ENS, STR_PAD); str_voice() loads a pad's knobs (attack, release, depth, tone in percent).
  Factory: DRIFT STRING MACHINE, SPIRAL HUGE ENSEMBLE, TIDES GLASS CHOIR, NIGHT ABYSS PAD. set_t unchanged in layout.
- Measured: tests/native/ensemble_test.c (all 14 pads: level vs Eminent -7.8..+1.8 dB, L/R 0.02..0.39 for the wide
  ones, release, panic, steals); 16 native suites pass; integration 57.4 %; setups worst block DRIFT 46.6, PULSE 57.4,
  SPIRAL 56.4, OCTAVES 59.1, MAGNET 62.9, LASER 56.2, TIDES 48.1, NIGHT 46.7 % (core 1 12-16 %). Module code 153 KB,
  bss 249.8 KB of 272. Caravel's own tools could not be run here (external code: the classifier refused).

## 2026-11-09: the lead's run, and the effects box on the keys

- LEAD + SELECT: set.run (0 OFF, then PENTA / SCALE / CHORD x FAST 70 ms / 1/16 of the tempo; glide 0 and the soft lead's attack 8 while one plays (run_crisp)). A lead key (panel or
  MIDI) plays a rise from itself through the chord's scale (the playing chord, else the latched cursor's, else minor on
  the key; major scales on MAJ / MAJ7), two octaves and the top; the top holds while the key does. Timed in the main
  loop (run_tick on H->ms(), one note a pass, resynced when late): not on the sequencer's grid. Mono lead: each note
  off before the next on (retriggers). set.run sits in set_t's tail padding (size still 72: asserted), so saved
  records and the setups' bank still load.
- PARTS page: the keys play the effects box (fx_key): white keys DROP PING BUBBLE CHIRP METAL round from the lowest F
  at the key's pitch (a HIT with KIT PITCH set to the key, then 72 again, in the one FIFO), the first F# the wind gust,
  the other black keys PING bells two octaves up. The lead no longer plays from PARTS.
- Measured: emulator runs (PENTA FAST 350 -> 1400 Hz in ~0.5 s; 1/16 in DRIFT's tempo; CHORD FAST over the playing band;
  each FX key heard at its pitch), worst block 45.9 % with the band, 0 overruns; integration all passed (57.4 %);
  16 native suites pass. SEVENTIES 158992 B in the slot; seqdock.bin 577244 of 581564 B.
- Follow-up (user: "effects that animate low to high, high to low, in one effect"): PARTS white keys are now gestures
  on the box (runs[1], the same engine as the lead's runs[0]): RISE FALL ARC SPARKLE on PING, DROPS UP / DOWN, METAL
  FALL, BUBBLE RISE (FX_RUN), the chord's pentatonic two octaves from the key; black keys single hits. Each hit sends
  its recipe's own factory macros first (fx_hit) and the box's base sound after: before, every hit took the base
  PING macros (mod depth 0), so BUBBLE / METAL sounded like PING. Measured: all eight shapes in a spectrogram, worst
  block 45.8 %, 0 overruns; integration all passed; 16 native suites. SEVENTIES 160920 B in the slot.
- PARTS + SELECT: set.fx_speed (int8 -4..+4, 0 as made; the record's second padding byte, size still 72) scales the
  gestures' step by FX_PACE (400 .. 30 %), read at each step so a gesture already playing follows the knob. Measured:
  RISE at -4 over ~2.2 s, at +4 ~0.17 s; worst block 45.8 %.

## 2026-11-09: setups on PRESETS everywhere, a banner; LASER the laser harp's setup with a phrase

- Owner: "it must be easy to understand when we change the setup - I do not know how". PRESETS now changes the setup
  on every page (was MIX only) with a two-second banner (draw_banner: number, name, a line about it, the scene, the
  hint); the header's free corner says PRESETS: SETUP. Moved off PRESETS: BASS sequence -> SELECT; PARTS background
  run -> SEL, now rounding none / box run / lead phrase / both (BG_NAME); LEAD and CHORDS had it as a duplicate.
- Owner chose "a laser-harp setup" (asked: the Synthex "Ring mod." already is the factory Laser Harp patch; Jarre's
  melody is not ours to copy). LASER (setup 6) is now AMBIENT: Em Cmaj7 Am7 Bsus4 | Em Cmaj7 Dsus2 B5 at 70 BPM, DUSK
  ORCHESTRA, no drums or run, set.phrase = 1: band.c lead_phrase, an original slow phrase (four two-bar motifs on the
  chord tones two octaves over the bass, tied eighths) on track 1, patterns 0/1 launched and linked like the others
  (band(), schedule_links). set.phrase is set_t's last padding byte (size 72).
- Measured: LASER 30 s, worst block 52.9 %, 0 overruns; the lead adds 4.5-10.9 dB in 300-4000 Hz over the bed.
  Integration all passed; 16 native suites. A saved setups bank keeps the old LASER (factory only without one).

## 2026-11-09: the setups' balance; the Synthex's DC (owner: "many of the other scenes are quite messy")

- Measured each part alone in every setup (the SMK-37 mute pads over MIDI, 14 s each): the bass was the loudest part
  in six setups (TIDES 15 dB over its choir), the wind / box over the strings in three; BAND's strings in a long echo
  (SPIRAL 1/16 at 70 %). factory() ends with one MIX table: the focus part (strings; MAGNET's drums) on top, the bass
  3-5 dB under, wind and box 4-8 under, BAND's strings echo send 0-20 (the lead carries the echo). After: DRIFT bass
  56.0 / strings 60.1 / box 52.1; OCTAVES 55.9 / 59.5; MAGNET drums 57.8 over bass 54.8; TIDES 50.0 / 52.6 / 48.4;
  NIGHT 52.0 / 60.0 / 50.3; energy under 150 Hz e.g. NIGHT 68 -> 46 %, DRIFT 57 -> 31 %.
- The laser harp ("Ring mod.") put out a DC offset (~2000, 6 % of full scale; 93 % of its energy under 40 Hz): the
  adapter (instruments/synthex.c) now AC-couples its output (one-pole high-pass near 10 Hz, y in Q8, a multiply, no
  division); synthex_test checks the held harp's DC (0.20 % of its RMS). The engine copy is untouched: worth telling
  the Scintilla / synthex-fm1 owner that the ring modulator leaves an offset. LASER under 150 Hz 88 -> 15 %, the harp
  alone 57.0 dB over strings 55.8. Worst blocks unchanged (TIDES 68.0 % as before), 0 overruns; integration passed.
- 2026-11-10 (owner: "we are the synthex developers"): the cause is upstream and already fixed in the reference:
  synthex-web 2b0f5c3 AC-couples the oscillators (5 Hz). Ported to ~/work/synthex-fm1 engine/sc_voice.[ch]
  (uncommitted; their CLAUDE.md "Verified" has the numbers): Ring mod. DC 0.10 -> 0.016, 320 pairs in limits, FM-1 =
  host. Reaches this repo only by commits: synthex-fm1 -> fm1-harbor-proc modules/scintilla/take.py (takes HEAD;
  harbor has uncommitted sustain work in sc_synth.[ch] that a take must keep) -> ./take.py. The adapter's DC blocker
  stays: the ring product still carries DC (VoiceCore's too).

## 2026-11-11: the setups cleaned up, twelve of them, FX SEQ (owner: "some of them are quite messy ... add a few more ... easier to edit things like effect sequences")

What made them messy, measured with a new native renderer (below), each part alone:
- The Eminent played every key a semitone sharp and a B the C eleven under it (em_engine.c: the note table read a row
  up), so its strings clashed with the bass in every setup that used it (PULSE's E minor chord sounded F G# C). Its
  phaser did nothing audible either: the all-pass signs put the quarter turn at 19 kHz, so the notches were there and
  the feedback only made it 6 dB louder. Both fixed in ../fm1-harbor-proc/modules/eminent/em_engine.c (two lines) and
  copied here (the other engine files compared equal first; take.py itself not run: it would also take the harbor
  tree's uncommitted Scintilla). tests/eminent_check.py there had measured an A#3 and passed by leakage: it now has a
  tuning check and measures the formant at A3's harmonics and the phaser at 1100 Hz itself (26 dB notch passing; all
  pass). Originals of both harbor files are in this session's scratchpad only: the harbor tree has them uncommitted.
  The standalone EMINENT module changes with it (in tune, a real phaser).
- The effects box's droplets and chirps were written in C minor whatever the setup's key (LASER E minor, TIDES A
  minor, NIGHT D minor). The effect sequence is now data: md_band_settings_t.fx, sixteen events {at, sound, degree,
  chance}, a degree a step of the home chord's pentatonic (md_band_fx_key), both scenes (BAND folds 16 bars onto 8;
  set.box is gone). A new parameter MD_FX_KIT on the electronic FX (not a setting) makes a NOTE_ON a one-shot of a
  recipe with that recipe's own macros, so a sequenced BUBBLE or METAL sounds like itself and rings out (before, a
  locked recipe took the base PING's macros and was cut at its gate).
- Levels: the pads differ by 9.5 dB and the 2600's lead patches by 9 (ARP LEAD clipped PULSE alone: peak 32767).
  PAD_TRIM (send_strings; MD_ENSEMBLE_LEVEL now to 200) and LEAD_TRIM (send_lead) bring each voice to one level; the
  Eminent's own level 65 -> 100 (the 65 was for the broken phaser's +6 dB). Every setup: mix 68.9 .. 70.4 dB RMS,
  strings 68.0, bass 60 .. 63, drums 52 .. 57, the box's loudest 50 ms 65 .. 66, a held lead 70.5; peak with two lead
  keys <= 25265 (was clipping).

New:
- module/setups.h: the factory setups as one table of named fields (SETUPS) and the effect patterns (FX_PATTERNS);
  factory() reads it. Twelve setups: the eight (MAGNET's drums no longer lead; OCTAVES and SPIRAL without the
  strings' echo) and DAWN (F major, bells), RAIN (G minor, breath choir), CAVERN (B minor, metal), BREATH (BAND on
  BREATHING at 104). set_t is 136 bytes (the sequence appended), SET_MAGIC / SETUPS_MAGIC are new: a saved record or
  setups bank from before is replaced by the factory's.
- FX SEQ page (FX: MIX, PARTS, FX SEQ, ECHO): SELECT the event, KNOB1..4 when / sound / note / chance, a key the note,
  ALGORITHM a pattern or YOURS (fx_own, RAM only), SEL solos the box; the line shows events, cursor, playing bar.
- tools/render_setups.c + tools/listen.py: the module itself with a host of its own on the Mac (no Docker), each
  setup and each part alone (the SMK-37's mute pads), levels, bands, pitch classes, PNGs in build/listen/setups/.

- BAND chords where the bass line walks through other chords inside the bar: SUS2 (SPIRAL, OCTAVES) or bare fifths
  (MAGNET) instead of minor triads (OCTAVES held D minor's F over the line's C major half).

Checked: 16 native suites with ASan/UBSan (band_test: the sequence's events, keys, folding; electronic_fx_test:
kit_sound). Both images built (SEVENTIES code 163700 B, bss 252148 of 278528, slot 168348 B);
tests/integration/seventies_emu.py all passed on both (57.4 % worst, PULSE with a lead key). fm1-emu, each setup
selected, PLAY, lead keys, 24 s: worst block DRIFT 49.3, PULSE 60.2, SPIRAL 59.5, OCTAVES 59.6, MAGNET 65.4, LASER
54.0, TIDES 48.6, NIGHT 47.1, DAWN 49.4, RAIN 49.2, CAVERN 46.8, BREATH 62.2 %, 0 overruns (the image before the
chord-kind change). Natively, 45 s of each setup with lead keys: peak <= 25739, none clipped. FX SEQ driven in the
native host (pattern, event, sound, note, when, key, YOURS kept) and seen in fm1-emu (build/listen/emu_fxseq_panel.png).
The box's pitch classes without wind: NIGHT D A G C F, RAIN F G D C, CAVERN F# A D (its METAL and BUBBLE are
inharmonic by nature).

Open:
- The SMK-37 image with CS-1 + SCINTILLA + SEVENTIES does not fit: the modules end 12 KiB past HX's start (the slot
  may be 159744 B; it was 160920 B before this work too, since the pads and the runs: already over). CS-1 + SEVENTIES
  builds (433920 of 438204). build/fm1-dock-smk37*.bin in the harbor tree are still the 9 November images (put back
  after the trial build).
- Not reviewed by a second agent, no tracker task (owner-directed, as the entries above); nothing sent to hardware.
- tests/stress/ not rerun (a stress build passes a syntax check). The harbor tree's whole emu_test was not run to
  its end (its decode cases ran past ten minutes; stopped): its seventies case run by itself with the same keys and
  checks passes (correlation 0.79, RMS 1229, peak 7095, worst 47.7 %).

## 2026-11-13: eight more sounds in the effects box, stereo, fifteen setups (owner: "more effects ... they have many nice effects in background")

What the owner asked for: more of the background effects of the records of 1976-86. Done in the effects box
(`instruments/electronic_fx.*`, this repository's own instrument, not an engine copy):

- Thirteen recipes: the five struck ones as they were, ZAP (struck: a bright tone falling three octaves onto its
  key), and seven *shaped* ones that swell for SWEEP TIME and fall away to the end of DECAY: WHISTLE (a tone gliding
  down 17 semitones in 2.6 s, vibrato growing), GULL (up a fifth in 70 ms, down past the key), TWITTER (rising chirps
  on random fifths and octaves, some left out), BLEEPS (sample and hold through fifths and octaves), and of noise
  WHOOSH (a resonant band travelling up), SURF (a low-pass opening and closing with the envelope), THUNDER (a crack
  falling two octaves into a roll, made up for the low-pass's loss). `electronic_fx.h` says what each macro does to
  each. SWEEP TIME now goes to 4000 ms. The new sound was FALL first: renamed WHISTLE, PARTS already has a FALL
  gesture.
- SPREAD (param 14, wire byte 18: sounds are 19 bytes now; 16 and 18 still load, in the middle): 0 is the mono of
  before, sample for sample (the tests check left == right). Above it each new voice takes a place from its seed,
  and ZAP, WHISTLE, GULL, WHOOSH, SURF and THUNDER travel across. A balance law: the far side falls, the near stays,
  so the middle is as loud as it was. SEVENTIES sets 70. The wind stays in the middle.
- `md_electronic_fx_macros()` gives a recipe's factory macros: `fx_hit` in seventies.c uses it (its own copy of the
  table is gone).
- The sequence: `md_band_fx_velocity` for the thirteen (the long tones at 14..24 where a DROP is 50: at 100 they
  measure 81..84 dB in their loudest 50 ms, as loud as a droplet's peak, and they last seconds; noise at 80..127).
  `setups.h`: six more patterns (SHORE, STORM, BIRDS, COMPUTER, LASERS, SPACE), TIDES now plays SHORE and DAWN BIRDS,
  and three more setups: 13 STORM (AMBIENT, F minor, 62), 14 ORBIT (AMBIENT, F# minor, 68), 15 COMET (BAND, UP AND
  DOWN, 134). The setups' bank is checked by size: a bank saved with twelve is not loaded (the factory's fifteen).
- FX SEQ: the SOUND knob goes through the thirteen, a colour each. PARTS: the black keys go round the thirteen, the
  long ones at twice the sequence's strength.

Found on the way:
- GLASS SHIMMER and ROUGH PULSE (ENSEMBLE pads) carry a third of their energy under 50 Hz (a constant band at
  30..40 Hz in the spectrogram), and RUNNING PAD reads a semitone off its chord in the chroma (A5, G5, F5, E5 gave
  D# and A# instead of G and D). None is used in a setup; setups.h says so. Not investigated in ens_engine.c.
- UP AND DOWN's chain (0, -2, -4, -5 with a minor then a major arpeggio a tone lower in each bar) passes through
  F minor and E flat: no note of A minor's pentatonic is safe in that bar. COMET therefore has bare fifths on the
  strings (no semitone against the line in any link, checked by hand) and LASERS has nothing that holds a pitch
  (shots, sweeps, one chirp).
- `md_instrument_render_add` takes 256 frames at most: a test that asks for more gets silence, not an error.

Measured (tools/listen.py, 64 s each): the fifteen mixes 68.8 .. 70.4 dB, none clipped, peak 26250 at most; the box
(with its wind) 51.5 .. 59.1 dB. Emulator (seqdock image, 45 s each, two lead keys held for two seconds): worst block
46.5 .. 67.1 % (PULSE the highest), 0 overruns in all fifteen. 16 native suites pass with ASan and UBSan (a new
scenario `shaped_and_spread` in electronic_fx_test, the thirteen sounds' locks in band_test). Both images build
(SEVENTIES code 168316 B, bss 253572 of 278528; seqdock app 577244 of 581564: 4 KiB left) and
tests/integration/seventies_emu.py passes on both.

Open:
- Heard by nobody yet: every judgement here is from levels, spectrograms and pitch classes. The long tones' strength
  (WHISTLE, GULL, TWITTER, BLEEPS) is the likeliest thing the owner will want changed: `VELOCITY` in module/band.c.
- The spread is fixed at 70 (no knob: PARTS has its four).
- tests/stress/ and the SMK-37 package not rebuilt; nothing committed, nothing sent to hardware.

## 2026-11-14: scales up and down in the background: an event is a sound and how it is played (owner: "skalor som går uppåt och nedåt i bakgrunden ... should we not be able to trigger these on each effect triggering ... sound + how it will be played?")

First try (13 November, gone again): a global mode on PARTS' SEL (SCALES / ARCS on every chord). The owner asked
whether that was part of the effects and proposed the event as "sound + how it is played": done that way, the mode
removed (SEL on PARTS is the run and the phrase as before).

- `md_band_fx_event_t` has a fifth byte, `how`: ONCE, UP, DOWN, ARC, SPARKLE (band.h). ONCE is a step in the pattern
  as before. The others are gestures: `md_band_build` leaves them out, and seventies.c's `fx_gestures()` (main loop)
  starts one when the sequence comes to the event's eighth (chance rolled there; runs[2], the keys' gesture
  machinery with `sync`: its step is of the tempo). AMBIENT: through the setup's pentatonic from the event's degree;
  BAND: through the pentatonic of the link's own chord from the event's note. `FX_GESTURE` says how many hits and
  how far apart per sound: struck ones eleven over two octaves in sixteenths (an ARC twenty-one at twenty-four a
  bar), ZAP six in eighths, WHISTLE three, GULL four, TWITTER / BLEEPS / WHOOSH three, SURF and THUNDER two.
- FX SEQ: ARP rounds the event's HOW and plays it (elsewhere ARP is all notes off, as it was); the bottom line says
  "EVENT n/16  ARP: UP"; a gesture is a taller mark. setups.h: `RUN(bar, eighth, sound, how, degree, chance)` beside
  `EV`; patterns SCALES and CASCADES; SHORE has two glass arcs, COMPUTER a scale up and one down (so TIDES and ORBIT
  play them).
- set_t is 152 bytes (was 136): SET_MAGIC "7S1E", SETUPS_MAGIC "S7S3": saved records and banks are the factory's again.

Found: the first scales took TIDES and ORBIT to 71.8 and 71.6 % worst block (budget 70): every ping rang to -120 dB,
two seconds past hearing, so all eight voices of the box were busy. Now a voice of the box ends at -72 dB (`FLOOR` in
electronic_fx.c) and a gesture's struck sounds decay in 500 ms at most (`fx_hit_decay`, `GESTURE_DECAY_MS`).

Measured after: the fifteen 46.6 .. 67.1 % worst block (PULSE the highest, as before), 0 overruns; mixes
68.8 .. 70.4 dB; 16 native suites (band_test: a gesture event has no step and no lock) and the integration test
pass on both images; SEVENTIES code 169756 B, bss 254128 of 278528. CASCADES and SCALES were rendered on TIDES by a
script (FX three times, ALGORITHM +7 / +6) and read in a spectrogram: drops down and up, a sparkle, an arc, bubbles
down, all at their bars.

Open: the gestures live in the main loop, so no native test plays them (only the renders and the emulator); their
timing is the main loop's (a pass late at most), not sample-exact. Not heard by anyone yet. Nothing committed.
