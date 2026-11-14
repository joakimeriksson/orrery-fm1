# M3-03 filters, phaser and local echo/reverb

Task M3-03, owner `claude`, 2026-11-06 UTC. Native implementation and reference renders only; no module
wiring (M3-04) and no target measurement (M3-05). Apple clang 21.0.0, macOS arm64. Provenance and the upstream
question are in [decision 006](../decisions/006-local-effects.md).

| File | SHA-256 |
| --- | --- |
| effects/effects.h | 6aa6f59d7f31d3cbb0b731a56a19460993143980a636f0ad5e5ec40295b805ee |
| effects/fx_common.h | b733fe1c4f5f099c8045af79b3d8a951c3bf5627fedcc11f70492afc744ff5c0 |
| effects/fx_tables.h (generated) | b736c78ccad9ece4aa3acb574aa613c1139ad9f2b4b834f29f0ee5eac68c7ad1 |
| effects/filter.c | f13bcd806e566775d254848940c558ef81c0eb4854e2b435854a0bb9df5b4779 |
| effects/phaser.c | 4ba605c6e549e81f68cfd4249cc3bce4208175e950c49852babf0818cb01ab4e |
| effects/echo.c | e80216e0325cc2c0d368899d7f4b8010d0eb725b769fcac5bce40e9b55a4bd35 |
| effects/reverb.c | 0a4d16464b340bca0a9983fa00e7238446059e1d62815a9eaa1d5ad92451a43f |
| effects/registry.c | 9905e9645dcd203d84ddaaf3aacf5c91581bdd95a2730a0f4f27c05ab2e80888 |
| tests/native/processors_test.c | 92beba12ae35620d6817dd0bd15e3fe30437d63c87abb7413e259ec691448e00 |
| tools/gen_effect_tables.py | c26b7f60381589f1cdd4b17e9bcae64d693da8e0d26591d0503049b62b8b52ca |
| tools/render_effects.c | 8b722e0206da493f7b23d224c066c470717f94aba042c6ed6a55d701c6bc4aad |

## What exists

All four use decision 002's reserved IDs and `core/effect.h`. Settings are every parameter as little-endian
int16 in descriptor order, settings version 1, and fit decision 002's pools (filter 6 bytes, so it also fits the
8-byte master slot; phaser 8 of 12; echo 8 of 8; reverb 6 of 8). Control-rate work runs every 8 samples at the
instance's own sample phase, so output is identical for any span partition.

- **Filter** (INSERT, also the master sweep). Trapezoidal state-variable filter, low/band/high-pass, cutoff
  20 Hz x 2^(v/100) for v 0..990 (20 Hz to 19.6 kHz), resonance 0..100 as Q 0.5..10, band-pass normalised to
  unity peak. Cutoff and resonance are smoothed (one-pole, 256-sample time constant). The state runs 2^8 above Q15
  and the integrators carry their rounding remainder, so even a 20 Hz low-pass converges exactly on DC; without
  that carry the test fails. Input limited to +6 dB over full scale.
- **Phaser** (INSERT). Four first-order allpasses per channel, triangle LFO 0.01..5 Hz, right channel a quarter
  cycle ahead, sweep about 200 Hz to 4 kHz scaled by depth, feedback -90..90 %, mix where 100 % is half dry plus
  half allpassed. Depth, feedback and mix are smoothed at control rate.
- **Echo** (BUS, tempo-synced). Port of the host's tape echo laws: 11.025 kHz line of 1.09 s, feedback ceiling
  0.85, the tone table in the loop, right tap 20 ms behind. Sync off (20..1090 ms) or 1/16, 1/8, dotted 1/8,
  1/4, dotted 1/4, 1/2 of the session tempo. Delay changes glide at a quarter line sample per line sample;
  feedback and tone move by a one-pole once per four-frame group (about 3 ms).
- **Reverb** (BUS). Port of the host's four-line Hadamard reverb: ROOM, PLATE, HALL, CATHEDRAL, size and
  damping, with its decay, damping and level laws, run per sample. Wet only; the mixer's return level is the mix.
- **Integer decay.** Feedback paths round toward zero and one-poles land exactly on target, so every tail ends in
  exact silence: no denormals and no small limit cycles.

## Validation

    python3 tests/run_native.py processors --sanitize on --build-dir build/native-claude-fx
    python3 tests/run_native.py --sanitize on --build-dir build/native-claude-all

Both pass with ASan and UBSan, no recovery: eight processor scenarios, and all thirteen native suites. UBSan
found one bug during development (a left shift of a negative value in the phaser coefficient), now fixed.

| Scenario | Proves |
| --- | --- |
| registry_and_settings | registry valid; every effect round-trips its maximum settings; short, wrong-version and out-of-range payloads refused; payloads fit decision 002's pools |
| filter_response | open low-pass passes DC exactly; 20 Hz low-pass converges on DC and on a value of 3; 160 Hz low-pass removes 10 kHz and passes 50 Hz; 1.28 kHz high-pass rejects DC to within 2 LSB and passes 10 kHz; band-pass is unity (within 8 %) at its centre for resonance 0, 50 and 100 |
| stability_limits_and_decay | worst input (+-2^24 squares, INT32 extremes) at maximum resonance, feedback 90 %, -90 %, 100 % echo feedback and the longest cathedral: all bounded, all tails reach exact zero, reset clears history |
| partition_independence_with_smoothing | all four effects with every smoother and the echo glide moving: identical output for spans 1, 17, 64, 255, 256 and an irregular sequence; one control period moves cutoff 1/32 of the way |
| declared_smoothing_is_real | phaser depth 0 to 100 on a 200 Hz sine: moves 1/32 per control period and the largest second difference is 16 against 11 steady (no click); echo feedback and tone move 1/32 per group and settle |
| echo_timing | 100 ms echo returns at 4,408 frames, right tap 880 later, second repeat at twice; feedback ratio 0.40; 1/4 sync at 120 BPM is 500 ms; at 240 BPM the delay glides toward 250 ms at the stated rate |
| reverb_decay_law | HALL size 50 (T60 2.85 s): -32.3 dB over half its decay against the law's -30 dB; left and right decorrelated |
| mixer_integration_and_cost | the echo on the mixer's echo bus returns the dry impulse once at 50 ms; native cost |

Decay times after the worst input stopped, from the suite:

| Effect, setting | Silent after |
| --- | --- |
| Filter, low-pass 500, resonance 100 | 0.06 s |
| Filter, low-pass 20 Hz | 0.08 s |
| Phaser, feedback +90 / -90 | 0.13 s / 0.37 s |
| Echo, 20 ms, feedback 100 % | 1.12 s |
| Reverb, ROOM size 0 / CATHEDRAL size 100 | 0.83 s / 9.02 s |

Mutation: removing the filter's rounding carry makes the 20 Hz DC check fail.

## Memory and cost

| Effect | State bytes | Native time per 256-frame span |
| --- | ---: | ---: |
| Filter | 72 | 5.1 us |
| Phaser | 116 | 12.6 us |
| Echo | 24,640 | 2.6 us |
| Reverb | 16,432 | 10.4 us |

Native times are on an Apple laptop core and only rank the effects; a 256-frame span lasts 5,805 us. The target
cost on the FM-1 is M3-05's job. One echo and one reverb add about 40 KiB of RAM.

## Reference renders

Built and drawn with:

    cc -std=c11 -O2 -I . tools/render_effects.c core/effect.c effects/filter.c effects/phaser.c effects/echo.c \
       effects/reverb.c effects/registry.c -o build/effects/render && build/effects/render build/effects
    uv run --with numpy --with pillow tools/spectrogram.py build/effects/NAME.wav build/effects/NAME.png "M3-03 NAME"

| Render | SHA-256 | What the spectrogram shows |
| --- | --- | --- |
| sweep.wav | 2b18e3864f245e0083e944310b8faae646998b90114ff213596ecb96bbc141fa | a 55 Hz saw's resonant peak rising smoothly to about 8 kHz and back, no steps |
| phaser.wav | 2079b7cddd91706e9bb749f93b0986edaa532209af49e6de63e4d6c128dfa893 | notches sweeping a held chord with a 2.5 s period |
| echo.wav | 0f9877fea56200215aa8a431fcdf93767704688d990092486c100913ee5cd061 | repeats every 375 ms, each darker, gone by about 3.8 s |
| reverb.wav | 8c79da6b8f370f3b764508cf77085454ee90e209c1b395c303f9f50fe23401d0 | a dense tail ending about 2 s after the last click, with the four-line modal banding of the host's reverb |

## Limits and next steps

- Not built or timed on the target. Wiring into the module and the host room-off request are M3-04.
- The host and these files hold two copies of the echo and reverb laws; decision 006 records the upstream path.
- The filter clips its input at +6 dB over full scale; louder material is clipped first by the filter, then by
  the mix's own output saturation.

## Revision after review

reviewer-claude-3 returned the task: phaser depth and echo feedback and tone were declared smoothed
(MD_PARAM_SMOOTH) but changed at once; a probe measured a click on a depth step (second difference 13 to 104).
Fixed: depth is smoothed at control rate like feedback and mix; echo feedback and tone have targets and move
once per group; init, reset and apply snap them. New scenario declared_smoothing_is_real covers both. The reviewer
also asked where decision 006's upstream follow-up is tracked; the decision now says it is not in WORK.json, why,
and where it is recorded. The reviewer's fuzz probe (all four effects, extreme inputs, random parameter and tempo
changes, spans 1..256 against 256) found no undefined behaviour and bit-identical output across partitions.

| File | SHA-256 after revision |
| --- | --- |
| effects/effects.h | e7ff942cb682b9fc21bf6bc543d1d5f017b70cd896d7639ae5cdf7bcf40f6624 |
| effects/phaser.c | 77bbf1292a330538034d72c1730e720a40bf2e08b34c6756cdfa3c67fe46f859 |
| effects/echo.c | ef7795a698bc8514fee0c4aca3a59cc1622bd0af8b3e48d1710e40942096c1cc |
| tests/native/processors_test.c | 71e7dc0447cab253eb616dbc89e06c8939cb28ba97b6720f8e52917eae947a07 |
