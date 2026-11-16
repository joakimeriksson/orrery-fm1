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
module/orrery.c, module/module.cfg, module/string.h, instruments/arp.c, instruments/minipops.*,
tests/native/{mixer,session,processors,band}_test.c, tests/native/adapters_test.c, tests/integration/orrery_emu.py,
tests/stress/ (stress builds and run_stress.py),
tools/gen_effect_tables.py, tools/render_effects.c, legacy/v1/. Upstream in fm1-harbor-proc:
modules/arp2600/arp_engine.{c,h}, tests/arp_host.c, tests/arp_check.py (all untracked there; originals of the
ARP files were backed up to the session scratchpad before editing).

Validation commands, results and evidence: `python3 tests/run_native.py --sanitize on` (14 suites);
`python3 tests/integration/orrery_emu.py` after building both images; fm1-harbor-proc `HB_APP=seqdock
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
untracked ARP files and many modified files from other work; preserve both when committing. module/orrery.c
was replaced; the original is restored byte-exact in legacy/v1/ (hash-checked against the baseline).

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
  tests/integration/orrery_emu.py and fm1-harbor-proc's emu_test (its seventies case now selects Rock 1 first,
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
  60.5 % on both images. 14 native suites pass with ASan/UBSan; orrery_emu.py all passed.
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
- ORRERY maps the SMK-37 over MIDI. The pads are channel 10 notes 36..51 (see pad() in module/orrery.c):
  - 36..40 mute a part, 41 everyone back, 42 the scene, 43 PLAY/STOP.
  - 44..48 the box's hits, 49 a wind gust, 50/51 bells.
  The faders are CC 70..73 (strings, bass, drums, box levels), the mod wheel CC 1 (master low-pass), and the
  pedal CC 64 (the lead's sustain). Pitch bend is not mapped: the ARP adapter has no MD_CAP_BEND.
- Checked in fm1-emu with scripted MIDI: PLAY and STOP from pad 43, each hit pad sounds, the wheel takes 2-8 kHz
  down about 67 dB (at travel x7; now x6). The SMK-37 package with ORRERY builds at 433,920 of 438,204 bytes,
  and boots in fm1-emu with three slots ok. Nothing sent to the real SMK-37.

## 2026-11-08: setups, editable AMBIENT chords

- Eight setups (module/orrery.c: factory(), setup_to(), save_all(); bank 1 holds magic + 8 x set_t, written
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

## 2026-11-08: the Synthex in ORRERY (owner: "study scintilla ... implement a laser harp here")

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
  ORRERY renders lead and bass on core 1 (measured best: 58.5 % here with a harp melody over LASER).
- Size: the module code is now 178 KB (was 87), with bss 177 KB. The SMK-37 package fits only without standalone
  Scintilla (cs1 + ORRERY).
- Not measured: two-layer Synthex patches (twice the voices) over a band; the other setups since the core split.

## 2026-11-08: the Synthex from dock's merged main (compact patches)

- fm1-harbor's origin/main (ca83cb6) has Scintilla with the factory patches as 25-byte records and its engine built
  without SC_SMALL_OSC. The local fm1-harbor-proc checkout is behind, on codex/flash-chip-compat, with uncommitted
  Scintilla and firmware work (the host-reverb change, also uncommitted in ~/work/synthex-fm1). That work was left
  alone: `./take.py --scintilla-ref origin/main` takes the Synthex files with git show and the other engines from
  the working tree (which holds the renamed ARP patches).
- The adapter decodes records with sc_preset_load into its state and switches each patch's own reverb off. That
  engine always carries the reverb (64 KB of RAM): ORRERY bss is 245 KB of 272 KB, code 142 KB (was 178).
  When dock takes scintilla-fm1's host-reverb change, take again and the 64 KB go.
- Same CPU (harp melody over LASER 58.5 % here, core 1 22.5 %). The SMK-37 package fits again with CS-1,
  Scintilla and ORRERY.

## 2026-11-09: Caravel's ENSEMBLE as the strings' second voice

The user found Caravel's ENSEMBLE pads (BonszaiMusic's video "Caravel 0.11.0: Streichfett-inspired ENSEMBLE Engine,
All 14 Presets") Jarre-like. Ported, not copied by take.py (it is not an fm1-harbor engine):
- `engines/ens_engine.{c,h}`: Caravel's ens_render / ens_post line for line, Felucca-Engines' dsp.c helpers, and
  Caravel voice.c's ADSR / filter envelope / voice start; six voices, stereo out at -16 dB (ENS_OUT) so a pad sits at
  the Eminent's level. Two products made 64-bit (UBSan: the dark end's ring mod and the filter's low-pass x gl wrap
  in Caravel's 32-bit code). `tools/gen_ens_tables.py` makes `engines/ens_tables.h` from the same formulas.
- `instruments/ensemble.{c,h}` (MD_INSTRUMENT_ENSEMBLE 0x00010006): PRESET, ATTACK, RELEASE, ENS, TONE, LEVEL.
- ORRERY: `c.alternate[T_STRINGS] = &ens`, bound in send_scene. str_reg 0..3 Eminent, 4..17 the pads
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
  16 native suites pass. ORRERY 158992 B in the slot; seqdock.bin 577244 of 581564 B.
- Follow-up (user: "effects that animate low to high, high to low, in one effect"): PARTS white keys are now gestures
  on the box (runs[1], the same engine as the lead's runs[0]): RISE FALL ARC SPARKLE on PING, DROPS UP / DOWN, METAL
  FALL, BUBBLE RISE (FX_RUN), the chord's pentatonic two octaves from the key; black keys single hits. Each hit sends
  its recipe's own factory macros first (fx_hit) and the box's base sound after: before, every hit took the base
  PING macros (mod depth 0), so BUBBLE / METAL sounded like PING. Measured: all eight shapes in a spectrogram, worst
  block 45.8 %, 0 overruns; integration all passed; 16 native suites. ORRERY 160920 B in the slot.
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
kit_sound). Both images built (ORRERY code 163700 B, bss 252148 of 278528, slot 168348 B);
tests/integration/orrery_emu.py all passed on both (57.4 % worst, PULSE with a lead key). fm1-emu, each setup
selected, PLAY, lead keys, 24 s: worst block DRIFT 49.3, PULSE 60.2, SPIRAL 59.5, OCTAVES 59.6, MAGNET 65.4, LASER
54.0, TIDES 48.6, NIGHT 47.1, DAWN 49.4, RAIN 49.2, CAVERN 46.8, BREATH 62.2 %, 0 overruns (the image before the
chord-kind change). Natively, 45 s of each setup with lead keys: peak <= 25739, none clipped. FX SEQ driven in the
native host (pattern, event, sound, note, when, key, YOURS kept) and seen in fm1-emu (build/listen/emu_fxseq_panel.png).
The box's pitch classes without wind: NIGHT D A G C F, RAIN F G D C, CAVERN F# A D (its METAL and BUBBLE are
inharmonic by nature).

Open:
- The SMK-37 image with CS-1 + SCINTILLA + ORRERY does not fit: the modules end 12 KiB past HX's start (the slot
  may be 159744 B; it was 160920 B before this work too, since the pads and the runs: already over). CS-1 + ORRERY
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
  so the middle is as loud as it was. ORRERY sets 70. The wind stays in the middle.
- `md_electronic_fx_macros()` gives a recipe's factory macros: `fx_hit` in orrery.c uses it (its own copy of the
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
(ORRERY code 168316 B, bss 253572 of 278528; seqdock app 577244 of 581564: 4 KiB left) and
tests/integration/orrery_emu.py passes on both.

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
  as before. The others are gestures: `md_band_build` leaves them out, and orrery.c's `fx_gestures()` (main loop)
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
pass on both images; ORRERY code 169756 B, bss 254128 of 278528. CASCADES and SCALES were rendered on TIDES by a
script (FX three times, ALGORITHM +7 / +6) and read in a spectrogram: drops down and up, a sparkle, an arc, bubbles
down, all at their bars.

Open: the gestures live in the main loop, so no native test plays them (only the renders and the emulator); their
timing is the main loop's (a pass late at most), not sample-exact. Not heard by anyone yet. Nothing committed.

## 2026-11-14: toward a release: fm1-dock's main, the first commit, and the first run on hardware (owner: "please do all these steps!", then "maybe we can optimize something? And what about using all the pad for something?")

Done. fm1-harbor-proc: everything that was uncommitted there is on branch `proc-snapshot` (two commits: the
engines; the seqdock / host reverb / flash IDs / SID / Scintilla work tree as it was). Branch `orrery-engines`
is origin/main plus the three engine modules with their checks (ARP 2600 and EMINENT hand REVERB to the host only
under `HB_HAS_FX_REVERB`: main has no host reverb) and the build's speed-ups (`--only`, one container). Nothing
pushed. Here: the module no longer names `fx_reverb` unless the header defines `HB_HAS_FX_REVERB` (main's hb_host_t
has `board` and `key_base` where the snapshot's had `fx_reverb`: the old module.dock would have called a number);
LICENSE, LICENSING.md, LICENSES/; README (build against main, the pads, State); the first commit. Built on main:
module.dock 174856 B, 16 native suites, the integration test and all fifteen setups in fm1-emu pass (0 overruns,
worst 45.5 .. 65.4 %).

Found on hardware (the owner's SMK-37 Pro, dock built from main, over USB-MIDI: scratch scripts, pads 42 / 43):
- All fifteen setups play. Three minutes through them with a lead line: 42 overruns (PULSE 1, OCTAVES 9, MAGNET 18,
  STORM 1, ORBIT 1, COMET 12), worst block 114 %. Ten-second runs of MAGNET and COMET alone: 75 .. 79 %, no overrun;
  a lead from the band's first beat: 101 %, one overrun. Not the settings save (three saves while playing: none).
- One core (tools/prof, permille of a block, average / worst): TIDES total 640/1117, mixer stage 243/306, bass
  91/103, lead 80/89 (idle), strings 106/217, drums 0/10, box 73/396, echo 17/21, reverb 38/40; CAVERN total
  599..755, strings 93 .. 226, the rest alike. The mixer stage is the same 24 .. 25 % everywhere.
- fm1-emu is not the device: it says mixer stage 16 %, lead 5.6 %, bass 5.7 %, box 1.3 %, and the second core's job
  2 % (device: 17 .. 19 %, worst 28 .. 43 %). The box on the second core as well (`-DORRERY_CORE1=0x13`) made
  fm1-emu's worst block worse (76 .. 82 %): not a judgement the emulator can make.
- Deploying over the module already deployed crashed the dock twice (pc 0x00003598, pc 0x02095f1a in module 5);
  after a crash it runs one core until power-off, where ORRERY is stopped as too slow in a fast setup. Left: the
  regular build deployed, CS-1 selected, the second core off until the owner powers it off and on.

Open, in order: (1) with both cores back, the split (`ORRERY_CORE1`: the box, perhaps the drums, on core 1) and
what a lead note's start costs on core 0; (2) the mixer: 45 instructions a frame a track, five tracks always mixed
(a silent track could be skipped, the sends hoisted); (3) the idle lead's 8 %; (4) no tag before the device plays
three minutes without an overrun. The pads: all sixteen are taken (README), 9 .. 13 only the five old sounds; a
proposal to the owner, not built: 9 .. 16 as the setup's own sounds and gestures.

## 2026-11-15: it holds on the dock (owner: "can we optimize this so that it works well in the dock also?")

On the owner's SMK-37 Pro (fm1-dock 0.9.0 1735f24, two cores, 360 MHz), the regular build, three minutes through
all fifteen setups with a lead line: 0 overruns, worst block 79 % (MAGNET; the others 45 .. 67 %); the timing build
ten minutes in PULSE under the owner's hands: 0, 55 % average, 65 % worst. The same run on the old code and this
dock: 96 overruns, 118 %.

What it was, in the order found (tools/prof, percent of a block):
- The flash cache: not much. The renders and the mixer's loops in RAM (`MD_HOT`, core/hot.h; `.ram_text`, 16040 of
  16384 B) took 3 .. 8 % off each part and the spikes, no more. The device is about 2.3 cycles an instruction
  everywhere: 1.5 times fm1-emu's figure for the same code.
- The Eminent: 48 % in PULSE, OCTAVES, MAGNET, BREATH. A released voice runs 17 s (to -114 dB at release 80) and
  a key played again took another voice, so all sixteen always ran. em_note_on now takes the voice still ringing on
  that note (fm1-harbor's em_engine.c): 9.3 / 10.5 / 11.3 / 6.7 voices, the mix -0.3 .. +0.1 dB. The floor was left.
- The two cores ran one after the other: core 0 rendered its tracks up to each of their events before the job
  started (the router is not for two cores). With the strings on core 1 that gave 104 % worst. Now core/session.c
  routes the block first through a router sink (md_router_t.sink: the instrument's event or reset recorded with its
  offset) and plays it afterwards on both cores from frame 0; an instrument's refusal when played is counted, and
  unless it is a hit over capacity the track is panicked at the next block's start. No second core: the old direct
  path, untouched. session_test: parallel_matches_serial still bit-identical in its six modes;
  played_refusal_panics_the_track is new.
- The mixer was all in a row after both (22.5 %): md_mixer_add_track_lane, a second set of sums, so each core adds
  its own tracks; in a row now the buses and the sum, 10.7 %. mixer_test lanes_give_the_same_mix.
- The split: strings and box on core 1 (`ORRERY_CORE1`), bass, lead and drums on core 0. A block is the larger
  core's parts + 10.7 + the routing (about 2).
- Small: the library's memset is a byte at a time (md_clear32 in the render path).

Audio: eleven setups bit-identical to the commit before (12 s each, native); the four Eminent setups within 0.4 dB.
Checks: 16 native suites, the emulator integration (worst 57.0 %: it cannot see any of this), fm1-harbor's
arp / eminent / minipops / engine_arithmetic checks.

fm1-harbor-proc (branch orrery-engines, NOT committed): the three engines' `*_HOT` / `*_IN` macros, the
Eminent's ringing voice, and tests/arp_host.c building without firmware/src/hb_echo.c (it did not compile on that
branch: the commit before said its check ran, and it had not). take.py copies harbor's Scintilla over the Synthex
files here too, which are from an older origin/main: `git checkout -- engines/sc_*` after it, until that is taken
on purpose.

The device: the owner said to remove the add-ons; FMINI-1 (deployed, 59 KiB v2) was deleted for room, and the owner
put S1VAVE on it meanwhile. Left with the regular ORRERY deployed and selected. The timing probe
(tools/probe.py run probes/timing.c --on fm1) was refused by the permission system: not asked again.

Open: MAGNET is the worst (core 1: 57 %); TIDES' box peaks at 33 % (a wave with gulls: shaped voices compute their
pitch every sample); the band's first block after PLAY once took 87 %. The idle 2600 lead still costs 9 %. No tag
yet: the owner has not said so. The pads' proposal stands (9 .. 16 as the setup's own sounds and gestures).

## 2026-11-15, later: headroom (owner: "optimize the generation of audio so that it is not close to stalling"; and a name)

NOT measured on the device: the unit on USB was another one by then (an older dock: CS-1, SCINTILLA, OBERVAVE,
PROPHET and NIAGARA on it, 76 KiB the largest gap), and nothing was sent to it. Everything below is counted in
fm1-emu with `build/prof1` (the timing build on one core: instructions; the device is about 1.5 times that) and
checked sample for sample. The owner has to connect the SMK-37 again (or say what may go from the other unit)
for the run that decides.

- core/session.c: `parallel_balance` (with `md_parallel_t.ticks`, the host's counter): every block the tracks are
  shared out by what each took lately (up at once, down a quarter a block), the dearest first, each to the core
  with less so far. Which core plays a track changes no sample (session_test modes 6, 7: bit-identical, seven or
  eight different shares). ORRERY turns it on (`ORRERY_FIXED_CORES` for the fixed split).
- Eminent (fm1-harbor's em_engine.c): the voices as passes (`foot`: a footage in runs between the saw's steps,
  a division a run, a 32-bit product for the level; a held key at full level is a plain add), the formant, the
  ensemble (one line read: the three held the same samples) and the phaser as passes. 17.4 / 19.5 % -> 12.9 / 14.3
  (PULSE / MAGNET, fm1-emu). Sample for sample the same (emcmp: 48 patches).
- ENSEMBLE (engines/ens_engine.c, ours): ens_voice in passes (`saws`), ens_post a pass a tap with the block in the
  ring first. 15.7 .. 16.6 -> 12.3 .. 13.0 %. The same samples (enscmp: the fourteen pads, both widths).
- Mini Pops (harbor's mp_engine.c): nothing struck at volume 0 and the fourteen voices looked at only while one
  may sound; send_drums / send_mix give it level 0 when the drums' mix level is 0 (nine setups): 5.5 -> 0 %.
- The echo's loop left the RAM (the 16 KiB overflowed by 28 bytes); 15740 B used.
- Tried and put back: the mixer's settled track loop as passes (no gain: 484 against 484).
- One core, all five parts, fm1-emu: PULSE 48.4, MAGNET 52.6, LASER 49 .. 54, TIDES 46, DRIFT 47.5, RAIN 44.5 %
  (the morning: 52.9, 57.8, 52 .. 56, 50). In a row: 8.3 (the mixer stage 7.7: echo 1.4, reverb 3.7). Estimate for
  the device with the parts balanced: 1.5 x (8.3 + the rest / 2) = 40 .. 46 %, 50 at the peaks. An estimate.
- What is left, dearest first (fm1-emu, % of a block): the two 2600s 5.8 each (an idle lead costs what a played
  one does: a skip would not be the same samples for the next note's filter and LFO phase, and helps no worst
  block); the reverb 3.7 and the echo 1.4 in a row (they could run a block behind on either core, 256 frames
  late, the echo's time shortened by that: about 4 % of the device's block, and the wet signal no longer the same
  samples); the mixing of a track 1.2 each; the effects box at a wave with gulls 12.
- fm1-harbor-proc working tree (orrery-engines, NOT committed): em_engine.c, mp_engine.c, arp_engine.c (the
  macros only), tests/arp_host.c. Its four checks pass.

The name. Proposed to the owner (applied the same day, below): ORRERY (a clockwork model of the planets: machines each on its own
turn; no instrument of that name found). SOLSTICE and PLANETARIUM are taken by software instruments (Minuit
Solstice, DreamWalk Solstice; Tom Avatars' and App Sound's Planetarium, Neonegg's Planetarium 2). A rename touches
the descriptor's name, the README and the repository, and what the dock keeps by a module's name.

2026-11-15, the rename. The owner: "use ORRERY". Applied to what a player and a log reader see: the descriptor's
name, every module.cfg (module, tools/prof, the seventeen stress builds), the log lines ("orrery: ..."), the tests'
lookups, demo.py, the docs. The macros went with it (ORRERY_ONE_CORE, ORRERY_CORE1, ORRERY_FIXED_CORES,
ORRERY_SETUPS_H). The dock finds settings and banks by a module's name: a unit that saved setups as SEVENTIES
does not show them to ORRERY, and the old module has to be deleted by its old name (`dock.py delete SEVENTIES`)
before ORRERY is deployed.

2026-11-15, the rest of the rename. The owner: "rename it all!". The repository's directory is ~/work/orrery-fm1
(no remote yet), the module's file module/orrery.c, the emulator check tests/integration/orrery_emu.py, the v1
reference legacy/v1/v1.c (its bytes as they were: it calls itself SEVENTIES, and its hashes hold), fm1-harbor-proc's
branch orrery-engines (local; nothing pushed). This ledger, WORK.json and the evidence were rewritten to the new
name and paths, so every entry above says ORRERY whenever it was written. Left as they are: what work/evidence/baseline.md
quotes of its logs (they said SEVENTIES) and its artifacts in build/baseline (seventies.dock), the `seventies`
case of fm1-harbor's tests/emu_test.py (proc-snapshot branch), and the SEVENTIES line of fm1-dock's ROADMAP.md as
pushed on origin/roadmap (changed in the working copy here only).

2026-11-15, the scenes. The owner: "did we make something really good here? I want full progress - better sceneries
everything", then "polish all scenarios to sound like different jean michel jarre and vangelis ambient synth
environment" and "improve all sounds, all effects". What was found and done (nothing sent to a device, nothing
committed):
- The reverb, measured (scratch rev/ir.c + an.py: a noise burst into HALL 90/35, the tail's spectrum 0.3 .. 1.3 s
  after): sixty-five resonances in 200 .. 1200 Hz with the median 57 dB under them, std 19 dB about its trend; 134 ms
  of line for a four-second decay. Rebuilt (effects/reverb.c): the loop at half rate (268 ms), lines 1 and 3 read at
  a moving place (0.31 / 0.47 Hz, 6 / 8 samples), two input allpasses a channel, a lowpass in the loop. Now std
  8.7 dB, L/R correlation 0.02, an impulse dense at 0.25 s (0.94). Same level on a held chord (65.7 against 65.4 dB).
  Cost 4.5 % of a block in fm1-emu (3.7 before; 5.0 until the allpasses and the loop's lowpasses were made 32-bit and
  the level moved into the loop). Its in/out pass is in the flash (both in RAM: 320 B over). processors_test passes
  as it was (decay law, exact silence, any span length).
- Chords voiced (band.c voiced(), seq.c MD_CHORD_RAISE bits 11..13 of a chord step's payload): the tones over the
  root in one octave whatever the root. A bare fifth is left alone (raised, its fifth lands on its twelfth: MAGNET
  lost a voice and 2 dB until that was seen).
- Phrases: set.phrase is a style now (band.c MOTIF: HARP as before, LONG TONES, BELLS, CALLS); SEL on PARTS rounds
  them. Halls: set.hall (the old spare byte), eight (HALLS in orrery.c), SELECT on the ECHO page. New magics
  (RR01 / RRB1).
- Setups: seventeen. Seven of the nine ambient ones had the same eight chords transposed; each has its own now
  (README's table). SPIRAL is HELIX (the old name is a record's title of the kind the rule forbids). NIGHT is NEON
  (Synthex 75 "Filtered brass": 440 ms to half level, an octave under its key; LONG TONES). New: ICE (chimes, which
  sound three octaves over their key: BELLS is an octave lower for that), TEMPLE (pan flutes, CALLS). The Synthex's
  quiet patches have a level of their own (sx_level). Levels by tools/listen.py (--count is 17 now), 64 s each.
- Tried and taken out: a tape wow on the echo (the tap at a fractional place dulls a repeat by the line's linear
  interpolation: the echo test's repeat fell from 18834 to 10299).
- A `git checkout -- file` on a file with uncommitted work was refused by the harness, rightly: effects/echo.c
  carried the optimisation's changes. Undo by hand in this tree until it is committed.
- One core in fm1-emu (build/prof1): PULSE 49.4 / 55.7, MAGNET 53.5 / 62.7, NEON 51 .. 55 / 62.6 (its abyss pad
  17 .. 20, 22 at most; the brass 7 .. 8, 10), ICE 50 / 58. Estimate for the device, balanced: 50 .. 55 % worst.
- After that entry's figures: the chimes' phrase an octave lower (band.h phrase_shift, set for Synthex 28), WARM
  BASS in place of SUB BASS under NEON, ICE and TEMPLE (fundamentals at 27 .. 49 Hz), levels re-set. Setups 8, 16,
  17 a minute each in fm1-emu: 0 overruns, worst 45.3 / 49.8 / 42.7 %.
- After the rest of the rename: module.dock crc 9b0a74b0 (code 158312 B, bss 269232, ram_text 16324: the sizes as
  before, the link order is not), 16 native suites, orrery_emu.py all passed (worst block 55.8 %). `./take.py` with
  no --scintilla-ref copies fm1-harbor-proc's working Synthex, which is newer than the one here (six files differ):
  it was run by mistake and the six put back as committed. Taking the newer Synthex is a change of its own.

