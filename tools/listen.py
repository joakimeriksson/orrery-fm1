#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Listen to ORRERY' setups on the Mac: builds tools/render_setups.c (the module itself with a host of its own),
renders each setup and each of its parts alone, and prints what a mix engineer would ask.

    uv run --with numpy --with pillow tools/listen.py [SETUP ...] [--seconds 32] [--png] [--script S]

Per setup: each part's level (dB RMS of the int16 scale, the whole render) and its loudest 50 ms (a sparse part is
judged by that), where the mix's energy sits (under 150 Hz,
150-600, 600-2500, over 2500), its peak, and each pitched part's pitch classes (the share of its tonal energy on each
of the twelve notes, strongest first) so a part out of the setup's key shows. --png draws build/listen/setups/N_name.png.
WAVs stay in build/listen/setups/. Needs ../fm1-harbor-proc for hb_module.h."""
import argparse
import platform
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "listen" / "setups"
BIN = ROOT / "build" / "listen" / "render_setups"
SOURCES = """module/band.c module/seq.c instruments/electronic_fx.c instruments/ensemble.c engines/ens_engine.c
instruments/synthex.c instruments/synthex_synth.c instruments/synthex_voice.c core/session.c core/commands.c core/mixer.c
core/effect.c core/router.c core/instrument.c core/transport.c instruments/arp.c instruments/eminent.c
instruments/minipops.c effects/filter.c effects/phaser.c effects/echo.c effects/reverb.c effects/registry.c
engines/arp_engine.c engines/em_engine.c engines/mp_engine.c""".split()
PARTS = ["bass", "lead", "strings", "drums", "box"]
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
SR = 44100


def build(harbor):
    deps = [ROOT / "tools" / "render_setups.c"] + [ROOT / s for s in SOURCES] + list((ROOT / "module").glob("*.[ch]"))
    if BIN.exists() and all(BIN.stat().st_mtime > d.stat().st_mtime for d in deps):
        return
    BIN.parent.mkdir(parents=True, exist_ok=True)
    # (uv's python can be an x86 build under Rosetta, where xcrun cannot load: ask for the Mac's own architecture)
    cc = ["arch", "-arm64", "cc"] if sys.platform == "darwin" and platform.machine() != "arm64" else ["cc"]
    cmd = cc + ["-std=gnu11", "-O2", "-DMD_SINGLE_CORE", "-w", "-I", str(ROOT), "-I", str(ROOT / "engines"), "-I",
           str(harbor / "firmware" / "dock"), str(ROOT / "tools" / "render_setups.c")] + [str(ROOT / s) for s in SOURCES]
    r = subprocess.run(cmd + ["-lm", "-o", str(BIN)], cwd=ROOT)
    if r.returncode:
        raise SystemExit("render_setups did not build")


def render(setup, seconds, part, script):
    path = OUT / f"{setup}_{'mix' if part < 0 else PARTS[part]}.wav"
    cmd = [str(BIN), str(setup), str(seconds), str(path), str(part)] + ([script] if script else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr or r.stdout)
    w = wave.open(str(path))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    return path, x[0::2], x[1::2], r.stdout.strip()


def db(x):
    return 20 * np.log10(np.sqrt(np.mean(x * x)) + 1e-9)


def loudest(x, ms=50):
    """the loudest stretch of that length: what a sparse part (a droplet, a drum) sounds like when it sounds"""
    n = int(SR * ms / 1000)
    e = np.convolve(x * x, np.ones(n) / n, mode="valid")[::n // 2]
    return 10 * np.log10(e.max() + 1e-9)


def bands(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    total = s.sum() + 1e-9
    return [100 * s[(f >= lo) & (f < hi)].sum() / total for lo, hi in ((0, 150), (150, 600), (600, 2500), (2500, 22050))]


def chroma(x):
    """the share of the tonal energy on each pitch class: spectral peaks 60 Hz .. 4 kHz, each to its nearest note"""
    n, hop, acc = 16384, 8192, np.zeros(12)
    win = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / SR)
    keep = (f > 60) & (f < 4000)
    for i in range(0, len(x) - n, hop):
        s = np.abs(np.fft.rfft(x[i:i + n] * win))
        peak = (s[1:-1] > s[:-2]) & (s[1:-1] > s[2:]) & keep[1:-1] & (s[1:-1] > 0.05 * s.max())
        for b in np.nonzero(peak)[0] + 1:
            acc[int(round(69 + 12 * np.log2(f[b] / 440))) % 12] += s[b] ** 2
    return acc / (acc.sum() + 1e-9)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("setups", nargs="*", type=int)
    ap.add_argument("--seconds", type=int, default=32)
    ap.add_argument("--harbor", type=Path, default=ROOT.parent / "fm1-harbor-proc")
    ap.add_argument("--png", action="store_true")
    ap.add_argument("--script", default="")
    ap.add_argument("--count", type=int, default=17, help="how many setups there are, when none is named")
    a = ap.parse_args()
    build(a.harbor)
    OUT.mkdir(parents=True, exist_ok=True)
    for setup in a.setups or range(a.count):
        path, left, right, line = render(setup, a.seconds, -1, a.script)
        name = line.split()[2]
        body = left[SR * 2:]                     # after the start
        print(f"\n== {setup} {name}: mix {db(body):5.1f} dB, peak {int(max(abs(left).max(), abs(right).max()))}"
              f"{'  CLIPPED' if 'clipped 1' in line else ''}")
        print("   energy <150 / 150-600 / 600-2500 / >2500 Hz: " + " / ".join(f"{b:4.1f}" for b in bands(body)) + " %")
        for part in range(5):
            _, l, _, _ = render(setup, a.seconds, part, a.script)
            l = l[SR * 2:]
            if db(l) < 10:
                print(f"   {PARTS[part]:8s}   --")
                continue
            c = chroma(l) if part != 3 else None
            notes = "" if c is None else "  " + " ".join(f"{NOTES[k]}:{100 * c[k]:.0f}" for k in np.argsort(-c)[:7] if c[k] > 0.02)
            print(f"   {PARTS[part]:8s} {db(l):5.1f} dB  loudest 50 ms {loudest(l):5.1f}  <150 Hz {bands(l)[0]:4.1f} %{notes}")
        if a.png:
            subprocess.run([sys.executable, str(ROOT / "tools" / "spectrogram.py"), str(path), str(OUT / f"{setup}_{name}.png"),
                            f"{setup} {name}", "20"], cwd=ROOT, capture_output=True)


if __name__ == "__main__":
    main()
