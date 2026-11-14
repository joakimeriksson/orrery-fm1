# M1-06 — Defined signed fixed-point arithmetic

Owner: agent-a. Date: 2026-11-05. Status: submitted for cross-review.

Adapter sanitizers found inherited signed-left-shift undefined behavior at ARP's BLEP edge output, negative
BLEP phase doubling and tanh quotient conversion, plus MiniPops' tanh quotient conversion. The errors existed
in upstream and imported copies before the instrument API work; ordinary audio tests did not detect them.

With explicit sandbox approval, changed upstream modules/arp2600/arp_engine.c and modules/minipops/mp_engine.c:
replace the diagnosed signed shifts by bounded multiplication by32 or2. BLEP x is clamped to0..32767 or
-32767..0; x*x and doubled x fit int32, and its Q15 residual times32 fits Q20. Tanh clamps outside +/-3 before
conversion, so its Q15 quotient times32 remains bounded. No 64-bit division, DSP topology, envelope, clock,
parameter mapping or output gain was added/changed. Then ran take.py; local engine copies match upstream.

Also added upstream tests/engine_arithmetic_check.py: native strict C11 ASan/UBSan, no recovery/suppression,
every ARP factory patch at24/60/96, note release, alternating17/64 spans; all14 MiniPops direct hits, alternating
17/256 spans, combined rhythms and stop. The reviewer caught a first-driver parity error that never selected17;
explicit block counters now alternate spans, and the corrected driver was rerun on original and fixed sources.
The original-source fixture fails at ARP BLEP's first negative shift; the fixed-source test passes fully.
No real device commands run in these checks.

Validation:

- Upstream `python3 tests/arp_check.py`: all passed (pitch/envelopes/filter/sequence/rest/tie/chain behavior).
- Upstream `python3 tests/minipops_check.py`: all passed (14voice comparisons and four rhythm comparisons
  against the JavaScript reference). Logs: build/engine-arithmetic/arp-upstream.log and minipops-upstream.log.
- `python3 ../fm1-harbor-proc/tests/engine_arithmetic_check.py --out build/engine-arithmetic/after-sanitized`:
  PASS with strictASan/UBSan, no recovery; originalfixture FAIL expected at negative shift. See before-sanitized.log.
- Six old/new native raw renders compare byte-for-byte: two ARP pitches, sequence, noise/filter sweep,
  all14 MiniPops voices and Rock1 rhythm. Actual compiler observation, not a proof for every possible patch.
- `python3 tests/run_native.py --sanitize on --build-dir build/native-verified-upstream`: all five current
  suites PASS, including real adapters, without sanitizer suppression. Another agent independently reran adapters.

## Source hashes

- `arp_engine.c` before: `378a8ed71c281fb8c4b516fe9b82cd68af2eecb7c21809ae2ea0c6b29f967eb6`; after: `9a426cae12ef7c0541a791ab25ebd3c58994dffec6fdca4afd63b05632501633`.
- `mp_engine.c` before: `aae58f24e567906191044f7e99a4172d413b955e1b41acf652d5b4c4ffb87f46`; after: `65d65c865f639b3184d5697e56e86465779c48cf3e13d91e8d41b26f5292ce4b`.
- Upstream `tests/engine_arithmetic_check.py`: `f4e5daa8d10190113c5dcf6b2cae44ebcfdce3662d81cb93583dd0303a49e560`.

## Raw render comparisons

| Case | Bytes | Identical before/after SHA-256 |
| --- | ---: | --- |
| arp-note45 | 176400 | `5d2da74eeb74730fafa9b8895c051a2f2f6efb0f02fa39e7566728308c0180c6` |
| arp-note69 | 176400 | `84d4e08bf828cd6082370f69f8284c093e31ff7a6ba222e218d518adad2401a6` |
| arp-seq | 705600 | `43f76f638430d622c76e1262615f2dbbdd63e4c1bcc9766d780e14890856cd53` |
| arp-sweep | 220500 | `b6bb9e71ccd2e214f247840cb09797d96741842f640a7d1bb7f94d0a7906cc7c` |
| mp-voices | 1234800 | `324f8f18088a1a22c1c910a9e97ff559d65089f71ba4faeb64ee8d60fe11b9d6` |
| mp-rhythm | 352800 | `1b13fe83ddc7490b8b01e999b07e9cc81e36127056b28c52ccd4e3332e88c6bd` |

## Reproduction and limits

The standalone engines/tests live in the same fm1-harbor-proc checkout named in baseline.md. Baseline HEAD
is unchanged; upstream engine/test files may be untracked, so use source hashes rather than claiming a commit.
No commits were made. Run take.py from this project only after upstream checks pass. Retained original and
fixed sources, standalone binaries, raw files and comparison.json are under ignored build/engine-arithmetic.
Compile standalone render helpers with `cc -O2 -I engines ../fm1-harbor-proc/tests/arp_host.c
engines/arp_engine.c -o build/engine-arithmetic/arp-after` (and analogous minipops_host/mp_engine), then use the
same note/seq/sweep/voices/rhythm arguments documented by those helpers. Negative regression can be reproduced
by compiling the original-source fixture in before-source with --source-root and a workspace-local --out.

No broad assertion that every engine path is UB-free or sample-partition invariant is made. ARP has a separate
known8-sample call-local control cadence, explicitly measured in adapter tests; this arithmetic fix does not
change it. Firmware/emulator performance after the fix is not remeasured here; the prior baseline remains
historical. These fixes unblock sanitized adapter completion; live accent/slide capability remains M1-05.
