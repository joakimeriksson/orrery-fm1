# M0-01 — Current baseline

Status: submitted for cross-review. Date: 2026-11-05. Owner: agent-a.

## Provenance

This project has no HEAD commit; project source is untracked and was preserved. Host checkout
`../fm1-harbor-proc` reports HEAD `94f8087ab62ea6c762cbb34747c8a01f02141a80` and 25 dirty-status entries;
that commit alone does not identify its working tree. `build/baseline/host-git-status.txt` records the status,
and `host-source-sha256.json` hashes 121 relevant firmware/tool/linked-module source inputs (manifest SHA-256
`67d4ac18b53c2a54ffa45eb21c7c26060ee760cc7198d88f928e4332214f711e`). The 19 project/core-host inputs below
were hashed before the build; all nine imported engine files compare byte-for-byte with their upstream copies.
No engine import or runtime source was changed by this task.

Emulator checkout HEAD: `7e7ed981f05b162cebbba22ad75270e92a07aa36`.
Native compiler: Apple clang 21.0.0, clang-2100.3.34.2, arm64-apple-darwin25.6.0.
Cross compiler: default `~/.jieli/toolchain/pi32v2/bin/clang`, executable SHA-256
`c45189ee624de0430deb0db02e6dce171fb5538e7fe323e3964dc8ac6236240f`; run by the host's Docker build wrapper.
Docker CLI: 29.9.2, desktop-linux context, build image default `debian:bookworm-slim`.
Python for build: `../fm1-harbor/.venv/bin/python3`. Main-line probes used system `python3`.

## Fresh checks

Native module syntax passes with the descriptor section annotation replaced for Mach-O parsing:

```sh
cc -fsyntax-only -Wall '-DHB_DESC=const hb_module_t hb_module' \
  -I ../fm1-harbor-proc/firmware/dock -I engines module/orrery.c
```

Fresh build, from fm1-harbor-proc (Docker access and external build writes received sandbox approval):

```sh
PYTHON=/Users/joakimeriksson/work/fm1-harbor/.venv/bin/python3 \
HB_MOUNT=/Users/joakimeriksson/work/orrery-fm1 \
./build.sh --no-package --seqdock --only \
  --modules cs1,scintilla,/Users/joakimeriksson/work/orrery-fm1/module
```

Build exited 0. Module: code **23876 B**, initialized data **20 B**, BSS **44768 B**, RAM code **0 B**,
198 relocations. Total static state **44788 B** out of **278528 B** allowed; this excludes stack and future state.
Host: 66652 B text, 88628 B data+BSS; the image has CS-1, SCINTILLA and SEVENTIES. Build static checks passed
RAM-code call restrictions and hardware-register isolation. These are today's baseline costs, not future capacity.

Copied fresh image/package/symbols into build/baseline before emulator validation. Focused replay:

```sh
python3 ../fm1-harbor-proc/tools/dock.py emu \
  --bin build/baseline/seqdock_xip.bin --ms 7500 \
  --keys '700:8,750:8^,900:e6:3,1100:8,1150:8^,1500:12,1550:12^,4000:21,4400:21^,6000:12,6050:12^' \
  --out build/baseline/emulator/seventies
```

The focused check reproduces the upstream `tests/emu_test.py` SEVENTIES acceptance calculation: left channel
2.5..5.5 s, twice-differenced signal, 10 ms RMS windows at 5 ms spacing, normalized autocorrelation at lags
49/50/51, signal RMS and peak, and parsed render load/overrun counters. Runnable analysis is retained at
`build/baseline/focused_check.py` and results at `focused-results.json`.

| Fresh rebuilt-image metric | Result | Existing acceptance |
| --- | ---: | ---: |
| Worst module render block | 40.6% | <70% |
| Overruns | 0 / 1257 blocks | 0 |
| 250 ms hat envelope correlation | 0.7659203381 | >0.4 |
| Audio RMS (sample units) | 1945.8442 | >800 |
| Peak (sample units) | 12628 | <32000 |
| Host reverb+echo worst | 3.4% | recorded separately |

All focused checks passed. The log confirms SEVENTIES loaded and PLAY/lead/STOP behavior, no strike reports,
and no core-1 jobs used by this module. Not a ten-minute release qualification, broad host suite or hardware test.

## Reference render, distinct from the fresh-image test

`python3 demo.py ../fm1-harbor-proc build/baseline/reference-demo` completed a 58 s scripted session with
9959 rendered blocks, 51.2% worst module block, 0 overruns and host FX worst 3.4%. Output includes raw `audio.wav`,
normalized `demo.wav`, framebuffer/RAM artifacts and `log.txt`.

This demo started against the pre-existing six-slot image while the new three-slot image was being built.
Its log identifies those six slots and SEVENTIES at slot six. The old image hash was not captured before it was
replaced, so this is a freshly generated reference render of the pre-build image, **not validation of the rebuilt
image**. The fresh 7.5 s replay above has an independently pinned image hash. Future comparisons should use the
fresh pinned image and reproduce either script deliberately, never silently mix the two provenance claims.
Raw audio duration is slightly shorter than simulated wall time because audio starts after boot. Demo normalization
is for listening only; raw peak/load measurements come from unnormalized audio/logs.

## Limits and next action

No new instrument API, sequencer, effect, storage format or MIDI-clock behavior was introduced. The known full
host-suite decode failures were not rerun here. Regular-dock qualification and the full ten-minute graph stress
case remain later integration gates. No real FM-1 was accessed or deployment attempted. There is no baseline
claim that an interrupted save preserves the prior project.

Reviewer: inspect source/artifact hashes, parse package dimensions and check focused-results.json/log against the
existing upstream SEVENTIES acceptance. On acceptance, M0-01 can be DONE; M1-01 still waits for the audited reuse
decision. Keep the old implementation as reference until session integration.

## Project and core-host source hashes

- `/Users/joakimeriksson/work/fm1-harbor-proc/firmware/dock/hb_module.h`: `3b09411ac290cd6dea1e77d2eacc7452845f92096d891c97d47ed413baca110b`
- `/Users/joakimeriksson/work/fm1-harbor-proc/firmware/src/dock.c`: `c0963f7eb05d5ba48f60de6ef241b13bb0a0473a791478a05e85f8e08fc3359a`
- `/Users/joakimeriksson/work/fm1-harbor-proc/tests/emu_test.py`: `aafad8685e4b0f4706f5b6c2944289a16160ef59b0ad12cd5af5f7ba32f12877`
- `/Users/joakimeriksson/work/fm1-harbor-proc/tools/build.py`: `181f2b2c535340995b819b1e0c15776b3d1b1f1a6f1e280de273285e65aca4f0`
- `/Users/joakimeriksson/work/fm1-harbor-proc/tools/dock.py`: `42ff86cd01aa0aa08d5e491ac78afd3d7e9b930881f8088b8d109a1a2509bfd0`
- `/Users/joakimeriksson/work/fm1-harbor-proc/tools/hbmod.py`: `b2e51157cd0a1853c949fec0d46e3ea68159705a3270fe4bd269b056d0365024`
- `demo.py`: `43c229b579a5207a0bb14f4e98936158573b2ad92af954c1ef2c8e9655b40086`
- `engines/arp_engine.c`: `378a8ed71c281fb8c4b516fe9b82cd68af2eecb7c21809ae2ea0c6b29f967eb6`
- `engines/arp_engine.h`: `9a65ec4b76eedff6fca4c4e0352252c6d3e03f8086ed7e70b5fd43ca19ef0606`
- `engines/arp_presets.h`: `49c2481206054bc7d6194a13292528b36e206f1591a2a7e22293a5cc82cc0b5b`
- `engines/em_engine.c`: `f96377bc317e04b925c4cb4cd928e706fa26461c52e6de18120bcd47adbce20c`
- `engines/em_engine.h`: `ee73922edf234b39b539a2624d55ea96ad9e1385ab71373e336343d17b5c2748`
- `engines/hb_echo.c`: `6eaee129c32f068f53ee0d7cc373cdb0e6be82b38e1f2c586ec673b93e446807`
- `engines/mp_engine.c`: `aae58f24e567906191044f7e99a4172d413b955e1b41acf652d5b4c4ffb87f46`
- `engines/mp_engine.h`: `668e64b71f37e451ec0fb430a5e306b84cc00a18e6988717a3c9a5144ddd2bcd`
- `engines/mp_tables.h`: `9e024e4f4e2b88b55ee4a95e1613f9cd2c68e4673f681e169cf7abc52a4e6453`
- `module/module.cfg`: `01837c6fb649e8006b3710ebb7303bc2462e15fd68026a3ce24a8385fa1fc6ee`
- `module/orrery.c`: `977fa670afd4e0e4275b67574d74a96fa895dfab9b3d4483fbfffe51ec6cf0be`
- `take.py`: `4fb5f77552fd9d41ba4777f19def644bd99b3ce22d1c9588a0e476b005aa6dc0`

## Retained artifact hashes

- `build/baseline/seventies.dock` (24920 B): `74593db04b2b04f7355c5bd77657cf384cf17fce327d8e766b91c674ef52dcb3`
- `build/baseline/seqdock_xip.bin` (577532 B): `c2c27e666968b9e41087bd01faf3430ac84f882711cf4087895cc1d9db5d1be3`
- `build/baseline/seqdock.syms` (31932 B): `e36982dec6349f2587304dbdb132ef14a3866d947ea15cf993b17352ce29376f`
- `build/baseline/reference-demo/audio.wav` (10204204 B): `888833ec61971ab905a501fe2077408f57e5a8befc04b27fcb6600817b6ab059`
- `build/baseline/reference-demo/demo.wav` (10204204 B): `93d43686218899c297ceb7831a5b929e7ab3c553ddef9a537506ea7caedbe6a0`
- `build/baseline/emulator/seventies/audio.wav` (1292332 B): `0d62e8bc456c86aa6d9ff179415ad5316382fe75117fd76647573af4954727b0`
- `build/baseline/source-sha256.json` (2524 B): `47a12d435a50f92652972f3ae287e1ac9f899eb7c90caa9e54ef9ea98e06216e`
