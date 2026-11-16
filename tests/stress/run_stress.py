#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""M3-05: build each stress variant of ORRERY (tests/stress/stress_common.h) into fm1-dock's image and play it
in fm1-emu, recording the module's worst block, overruns and audio.

    python3 tests/stress/run_stress.py [1 2 3 4] [--harbor ../fm1-harbor-proc]

Variant 4 runs ten minutes and then checks the band stopped cleanly: after the stop and the release of every live
note, no waveform may remain (no stuck notes); a constant DC offset from the engines is reported, not failed. Results go to build/stress/<variant>/ (audio.wav,
log.txt, spectrogram.png) and are printed; nothing is sent to hardware."""
import argparse
import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# variants measured over budget on purpose: the unsupported combinations the evidence lists
UNSUPPORTED = {1, 2, 3, 5, 9, 11, 12}
SR = 44100
HARBOR_PY = Path.home() / "work" / "fm1-harbor" / ".venv" / "bin" / "python3"


def build(harbor, variant):
    env = dict(os.environ, PYTHON=str(HARBOR_PY), HB_MOUNT=str(ROOT))
    mod = ROOT / "tests" / "stress" / f"stress{variant}"
    only = ["--only"] if "--only" in (harbor / "tools" / "build.py").read_text() else []   # (not on fm1-dock's main yet)
    r = subprocess.run(["./build.sh", "--no-package", *only, "--modules", f"cs1,scintilla,{mod}"],
                       cwd=harbor, env=env, capture_output=True, text=True)
    line = next((l for l in r.stdout.splitlines() if l.startswith("module ORRERY")), "")
    if r.returncode or not line:
        raise SystemExit(f"stress{variant}: build failed\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return line.strip()


def run(harbor, ms, out):
    out.mkdir(parents=True, exist_ok=True)
    image = harbor / "build" / "fm1-dock_xip.bin"
    keys = "700:8,750:8^,900:e6:3,1100:8,1150:8^"      # the picker: ORRERY is slot 3 after CS-1 and SCINTILLA
    r = subprocess.run([sys.executable, str(harbor / "tools" / "dock.py"), "emu", "--bin", str(image), "--ms", str(ms),
                        "--keys", keys, "--out", str(out)], cwd=harbor, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr or r.stdout)
    w = wave.open(str(out / "audio.wav"))
    d = w.readframes(w.getnframes())
    return struct.unpack(f"<{len(d) // 2}h", d), (out / "log.txt").read_text()


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("variants", nargs="*", type=int, default=[6, 8, 10],
                    help="default: the short supported cases; 4 and 15 are the ten-minute runs; 14 and 15 are the "
                         "shipped panel's reachable worst case, over budget in long runs (see audio-budget.md)")
    ap.add_argument("--harbor", type=Path, default=ROOT.parent / "fm1-harbor-proc")
    a = ap.parse_args()
    failures = []
    for v in a.variants:
        line = build(a.harbor, v)
        ms = 625000 if v in (4, 15) else 15000
        out = ROOT / "build" / "stress" / f"stress{v}"
        s, log = run(a.harbor, ms, out)
        tail = log.split("---")[-1]
        worst = float(tail.split("worst ")[1].split("(")[1].split(" %")[0])
        last = float(tail.split("last ")[1].split("(")[1].split(" %")[0])
        overruns = int(tail.split(" overruns")[0].split(",")[-1])
        left = s[0::2]
        band = left[int(4 * SR):int(12 * SR)] if v not in (4, 15) else left[int(60 * SR):int(70 * SR)]
        peak = max(abs(x) for x in left)
        print(f"stress{v}: {line}")
        print(f"  worst block {worst} %, last {last} %, {overruns} overruns, band RMS {rms(band):.0f}, peak {peak}")
        ok = "stress: case running" in log and overruns == 0 and rms(band) > 500
        if v in (4, 15):
            stopped = "stress: stopped after ten minutes" in log
            # the last 3 s, well after the stop and every tail: a stuck note is a waveform, so the check is the AC
            # level with the mean removed; the engines' small DC offsets (passed on by echo and reverb) are reported
            tail3 = left[-int(3 * SR):]
            dc = sum(tail3) / len(tail3)
            ac = math.sqrt(sum((v - dc) ** 2 for v in tail3) / len(tail3))
            print(f"  stopped: {stopped}; last 3 s: AC RMS {ac:.2f} (no stuck notes: below 1), DC offset {dc:.1f}")
            ok = ok and stopped and ac < 1.0
        if worst >= 70.0:
            if v in UNSUPPORTED:
                print("  over budget, as expected: an unsupported combination")
            else:
                print("  OVER BUDGET (70 %)")
                ok = False
        subprocess.run(["uv", "run", "--with", "numpy", "--with", "pillow", str(ROOT / "tools" / "spectrogram.py"),
                        str(out / "audio.wav"), str(out / "spectrogram.png"), f"M3-05 stress {v}"], capture_output=True, cwd=ROOT)
        if not ok:
            failures.append(f"stress{v}")
    print("all within budget" if not failures else f"FAILED: {', '.join(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
