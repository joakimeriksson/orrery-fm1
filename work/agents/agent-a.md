# Agent A handoff

Updated: 2026-11-06 local (UTC session began Nov5). Owner: agent-a (root).
Live status authority: WORK.json via python3 tools/work.py status. Do not infer status from this snapshot.

Completed/reviewed:
- M0-01 fresh baseline and M0-02 audited selective Melodee reuse (cross-reviewed).
- M1-01 contracts, M1-02 native harness, M1-03 registry/router, M1-04 full existing-engine adapters.
- M1-06 inherited ARP/MiniPops signed-shift UB fixed upstream and imported exactly; all6old/new raw renders bitexact.
- M1-07 hybrid HIT/CAPACITY rejection preserves held melodic owners instead of fault-flushing the track.
- M2-01 exact transport, M2-02 effect runtime, M2-03 portable pattern sequencer, M2-04 electronic FX instrument.

Current: no task claimed by either lane. M1-08 accepted; sourceAPI2 parameter readback is complete and all
nine current native suites passed together in build/native-final-review. Output-pointer errors do not mutate
caller output; getters cover loaded/edited native values. Settings/projectwireversions remain1. Query current
DSP only in owning audio/stopped context; retain modelbase separately while locks override DSP. No host decoding
of engine structs or opaque sound payloads. work/decisions/005-parameter-readback.md is the accepted amendment.
Current checkpoint:13tasksDONE of32, no pending review/blocker/claim. WORK.json remains authoritative.

Actual code:
- core/{param,event,instrument,effect,project_format,router,transport}.*, private validation helpers.
- instruments/{arp,eminent,minipops,electronic_fx}.*, all35ARPparameters and sourcepresetparity.
- module/seq.h/.c: fourtracks/fourpatterns/64steps, chords/14hitmask, gates/ties/monoSLIDE, chance,48locks,
  quantized/immediate launches, source-safe stop/mute, bounded timestamp iterator. No UI/module integration yet.
- Tests use python3 tests/run_native.py [stems] --sanitize on; source dependencies in MD_TEST_SOURCES comments.
  tests/seq_check.py runs seq+transport before screen work. tools/work.py owns atomic claims/review/history.

Evidence and checks:
- work/evidence/ has baseline, instrument-runtime, engine-arithmetic, hybrid-hit-capacity, transport,
  sequencer, effect-runtime, adapters and electronic-fx reports; decision001/002 explain provenance/contracts.
- Existing native9suites passed individually strictC11ASan/UBSan; rerun all after M1-08descriptor update.
- Clock: eight tempos x6partitions x10min+137frames equals independentoracle; sampleoffset/eventtrace tests pass.
- Seq: final8scenarios PASS incl oddlengthswing,baseupdate/restorefallback, no-predecessorSLIDE, lengthshrink,
  bounded16silent-stepcatchup and actualrouter samepitchlive-note survival. Native sizeofseq7872B, bank6568B.
- FX: five recipes,8voices, held-owner protection, exactpartitionstate/output, bounded9sdecay; root viewed
  build/electronic-fx/recipes.png. Unnormalized DROP/PING/BUBBLE/CHIRP/METAL.wav references are under build/.
- Historical fresh legacy firmware code23876B/state44788B, focused7.5semu40.6%/0overruns. Separate old-image58s
  reference51.2%/0overruns; provenance caveat in baseline.md. No target budget claimed for new portable core.

Exact next actions:
1. Resume by running tools/work.py status and claiming one ready task. Final verification and handoff are
   recorded in work/evidence/native-foundation-review.md with53source hashes; nine sanitized suites, eight
   tracker checks and three runner checks passed. No implementation claim remains active.
2. Next ready work: M1-05 explicit upstream ARP accent/legato/slide controls (currently honestly unsupported),
   M3-01 audio session/command/render integration, M3-02 fixed mixer, then M3-03 real effect processors.
   Suggested two-agent pair: agent-a claims M1-05, agent-b claims M3-02. Coordinate quiet engine import windows.
3. Session must honor seq event priorities: RESTORE,LOCK,RELEASE,TRIGGER,LEGATO_CLEANUP. Cleanup oldtoken follows
   new slide-on at the same sample. Decode targetnamespace before instrument/effect dispatch; on iterator error
   release only seqsources0..3. STOP preserves live sends/tails; PANIC/projectreplacement resets effect history.
4. New shared contracts need review before dependent code; remain within task resource/file claims.

Remaining limits:
- Legacy module/orrery.c and module.cfg still use the old band/chord wiring; no claim the new UI/sequencer
  runs on device. No songs/recording/projectcodec/localFXrouting or real MIDIclock implementation yet.
- ARP span output is notbitexact because of existing8sample call-local control cadence; measured defaultfixture
  peakdelta811, meanrelative0.10018%, explicittol1024/2%; Eminent/MP exact in representativepartition tests.
- No commits; repo has noHEAD and all source is untracked. Preserve files. No realFM-1 deployment was attempted.
- Upstream edits had explicit sandboxapproval. Both engine copies still match fm1-harbor-proc. New upstream
  tests/engine_arithmetic_check.py is also uncommitted; preserve it with those upstream fixes when later committing.

## 2026-11-06: D0-01 emulator audition

Actual owner agent-a = root; cross-review agent-b = /root/audition_review.
D0-01 implemented in isolated audition/; new portable seq/router actual synths,
not legacy module/orrery.c. Native sanitized both modes PASS, independent
nextbar+ownership checks PASS. Target final34544B code/55056B BSS, pinned image
build/seq-demo/melodock-test_xip.bin. Repeat emu43.8%worst/0overruns/no strikes.
Launcher `python3 tools/run_seq_demo.py`; HOME picker -> PRESETS MELODOCK TEST ->
HOME select -> PLAY. SEL lead/FX restart, ALGOtempo, SEQnextbarpattern, ARPpanic.
See audition/README.md and work/evidence/synth-seq-audition.md for artifacts/hashes.
SDK C11 atomics lower to missingCAS; module uses reviewed aligned32 singlewriter
IRQ/main control words plus compiler fences, explicitly no core1 access. Local
string.h only supplies missing freestanding declarations, hb_lib implementations.
Final source stable and task submitted for review. Next resume: tracker status;
respect claims; M3-01/M3-02 production session/mixer and M1-05 still open. Audition
is dry fixedgain, no fullproductionFX/editor/recording. No commit or hardware flash.

D0-01 final review accepted DONE by agent-b. Final panel.png complete and stable;
source manifest/image hashes and final ELF checked independently. Exact next action:
resume with tracker status and claim one ready production task; audition is runnable now.
