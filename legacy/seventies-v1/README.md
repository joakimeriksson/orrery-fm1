# SEVENTIES v1 (pre-session audio reference)

The module as it was before M3-04: engine globals, the ARP 1601 and Mini Pops internal sequencers, a
four-buffer mix and the host's reverb. Kept as the audio reference PLAN.md asks for.

Both files are byte-identical to the M0-01 baseline (work/evidence/baseline.md, build/baseline/source-sha256.json):

| File | SHA-256 |
| --- | --- |
| seventies.c | 977fa670afd4e0e4275b67574d74a96fa895dfab9b3d4483fbfffe51ec6cf0be |
| module.cfg | 01837c6fb649e8006b3710ebb7303bc2462e15fd68026a3ce24a8385fa1fc6ee |

The original was overwritten in module/ by M3-04 and rebuilt here from the session that replaced it; the
hashes above confirm the copy is exact. Its module.cfg paths are relative to module/: to build it, copy both
files into a sibling of module/ (or adjust `../engines` to `../../engines`). The baseline's built image
remains at build/baseline/seventies.dock and build/baseline/seqdock_xip.bin.
