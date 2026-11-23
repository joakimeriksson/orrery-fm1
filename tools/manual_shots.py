#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The manual's pictures (docs/manual/*.png): every page of the module, as fm1-emu shows it. Run it after a page
changes, with the module built into fm1-harbor's image (CLAUDE.md, Build):

    uv run --with pillow tools/manual_shots.py [--harbor ../fm1-harbor-proc] [NAME ...]

Each picture is one run of the emulator: ORRERY picked from the dock's list, a setup, the page's button, then the
panel cut down to the screen. The whole panel (docs/manual/panel.png) comes from the LEAD page's run."""
import argparse
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--harbor", type=Path, default=ROOT.parent / "fm1-harbor-proc")
ap.add_argument("names", nargs="*", help="only these pictures")
a = ap.parse_args()
harbor = a.harbor.resolve(); out = ROOT / "docs" / "manual"; tmp = ROOT / "build" / "manual"; only = a.names
out.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(harbor / "tools"))
import hbmod
image = harbor / "build" / "fm1-dock_xip.bin"
img = image.read_bytes(); names = []
for i in range(32):
    magic, at, size, _ = struct.unpack_from("<IIII", img, 0x30000 + 16 * i)
    if magic != 0x544F4C53: break
    names.append(hbmod.parse(img[at - 0x02000000:at - 0x02000000 + size])[0]["name"])
idx = 1 + names.index("ORRERY")
PICK = f"700:8,750:8^,900:e6:{idx},1100:8,1150:8^"
def setup(n): return f",1300:e6:{n}" if n else ""
def press(b, t): return f",{t}:{b},{t + 50}:{b}^"
FX, SEL, ENV, LFO, EDIT, GLO, SEQ, PLAY = 2, 3, 4, 5, 6, 7, 11, 12
ARP = 10
def write(keys, t):                             # note keys (14 + k) one after the other; None: ARP, a rest
    out = ""
    for k in keys:
        out += press(ARP, t) if k is None else f",{t}:{14 + k},{t + 50}:{14 + k}^"
        t += 100
    return out
WRITE_A = write([0, 4, 7, 12, 7, 4, 0, None, 2, 5, 9, 14, 9, 5, 2, None], 3900)
WRITE_B = write([12, None, 12, 11, 9, None, 7, None], 5500)
SHOTS = {
    "strings": setup(0) + press(EDIT, 3600),
    "strings-pad": setup(6) + press(EDIT, 3600),
    "lead": setup(0) + press(GLO, 3600),
    "lead-organ": setup(18) + press(GLO, 3600),
    "bass": setup(1) + press(ENV, 3600),
    "drums": setup(1) + press(LFO, 3600) + ",3900:25,4000:23,4100:23^,4200:25^",   # Rock 1 held, Bossanova added
    "chords": setup(0) + press(SEQ, 3600) + press(PLAY, 3800),
    "mix": setup(0) + press(FX, 3600),
    "parts": setup(18) + press(FX, 3600) + press(FX, 3800),
    "fxseq": setup(0) + press(FX, 3600) + press(FX, 3800) + press(FX, 4000) + press(PLAY, 4200),
    "echo": setup(0) + press(FX, 3600) + press(FX, 3800) + press(FX, 4000) + press(FX, 4200),
    "banner": setup(18),
    # the STEPS page: a sequence written with the keys (a rest by ARP), switched on, playing
    "steps": setup(1) + press(SEQ, 3600) + press(SEQ, 3800) + WRITE_A + ",5400:e5:1" + press(PLAY, 5600),
    # the SONG page: a second sequence written, then the song A B A B
    "song": setup(1) + press(SEQ, 3600) + press(SEQ, 3800) + WRITE_A + ",5400:e5:1,5450:e1:1" + WRITE_B + press(SEQ, 6600)
            + ",6700:e0:1,6750:e2:2,6800:e0:1,6850:e2:1,6900:e0:1,6950:e2:2" + press(PLAY, 7100),
}
for name, keys in SHOTS.items():
    if only and name not in only: continue
    d = tmp / name; d.mkdir(parents=True, exist_ok=True)
    ms = 2300 if name == "banner" else 9000 if name in ("steps", "song") else 6500
    r = subprocess.run(["/usr/bin/env", "python3", str(harbor / "tools" / "dock.py"), "emu", "--bin", str(image), "--ms", str(ms),
                        "--keys", PICK + keys, "--out", str(d)], capture_output=True, text=True, cwd=harbor)
    for f in ("panel.ppm", "panel.png"):
        if (d / f).exists(): (d / f).unlink()
    r = subprocess.run(["/usr/bin/env", "python3", str(harbor / "tools" / "dock.py"), "emu", "--bin", str(image), "--ms", str(ms),
                        "--keys", PICK + keys, "--out", str(d)], capture_output=True, text=True, cwd=harbor)
    src = d / "panel.png" if (d / "panel.png").exists() else d / "panel.ppm"
    if not src.exists(): print(name, r.stderr[-300:]); continue
    im = Image.open(src).convert("RGB")
    if im.size != (1280, 820): print(name, "size", im.size)
    im.crop((340, 20, 880, 540)).save(out / f"{name}.png", optimize=True)
    if name == "lead": im.resize((960, 615)).save(out / "panel.png", optimize=True)
    print(name, (out / f"{name}.png").stat().st_size)
