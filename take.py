#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The engines come from fm1-dock (fm1-harbor here), where each is also a module of its own (ARP 2600, MINIPOPS,
EMINENT, SCINTILLA's Synthex); this copies them in again from a checkout. (engines/hb_echo.c is the seqdock host's
tape echo, the one effects/echo.c was ported from: fm1-dock's main has no seqdock, so it stays as it is here.)
  ./take.py [PATH_TO_FM1_HARBOR] [--scintilla-ref REF]      (default ../fm1-harbor-proc)
With --scintilla-ref the Synthex's files come from that commit of the checkout (git show, e.g. origin/main: what dock
has merged), the others from its working tree; nothing in the checkout is changed."""
import argparse
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("src", nargs="?", type=Path, default=HERE.parent / "fm1-harbor-proc")
ap.add_argument("--scintilla-ref", help="take the Synthex from this git ref of the checkout")
a = ap.parse_args()
FILES = ["modules/arp2600/arp_engine.h", "modules/arp2600/arp_engine.c", "modules/arp2600/arp_presets.h",
         "modules/eminent/em_engine.h", "modules/eminent/em_engine.c",
         "modules/minipops/mp_engine.h", "modules/minipops/mp_engine.c", "modules/minipops/mp_tables.h"]
# the Elka Synthex (Scintilla's engine, itself taken from joakimeriksson/scintilla-fm1): the laser harp; the factory
# patches as 25-byte records (sc_presets_data.h)
SYNTHEX = ["modules/scintilla/engine/" + n for n in ("sc_synth.c", "sc_synth.h", "sc_voice.c", "sc_voice.h",
           "sc_patch.h", "sc_fixed.h", "sc_tables.h", "sc_presets.h", "sc_presets_data.h")]
for f in FILES + ([] if a.scintilla_ref else SYNTHEX):
    shutil.copy(a.src / f, HERE / "engines" / Path(f).name)
    print("took", f)
if a.scintilla_ref:
    rev = subprocess.run(["git", "-C", str(a.src), "rev-parse", "--short", a.scintilla_ref], capture_output=True,
                         text=True, check=True).stdout.strip()
    for f in SYNTHEX:
        data = subprocess.run(["git", "-C", str(a.src), "show", f"{a.scintilla_ref}:{f}"], capture_output=True,
                              check=True).stdout
        (HERE / "engines" / Path(f).name).write_bytes(data)
        print(f"took {f} at {rev}")
