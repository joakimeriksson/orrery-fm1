#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""M3-04 integration: ORRERY on the portable session, in fm1-emu, on fm1-dock's image.

    python3 tests/integration/orrery_emu.py [--harbor ../fm1-harbor-proc] [--ms 12000] [--images A_xip.bin,B_xip.bin]

Builds nothing: build the image first (CLAUDE.md). For each image (fm1-dock_xip.bin; others by --images) it loads
ORRERY from the picker, presses PLAY, plays a lead key over the band and records the audio. Checks:
the module started on the session; no overruns; worst block under 70 %; the dock's own room is off (no host
reverb line in the log); the band's hi-hat repeats every eighth at 120 BPM (Rock 1 under ROOT PULSE: the PULSE setup); the
lead key adds level while held. Writes audio.wav, log.txt and a spectrogram per image under
build/integration/. Sample-exact timing of every part is proven natively by tests/native/band_test.c."""
import argparse
import math
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SR = 44100


def run(harbor, image, keys, ms, out):
    out.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([sys.executable, str(harbor / "tools" / "dock.py"), "emu", "--bin", str(image), "--ms", str(ms),
                        "--keys", keys, "--out", str(out)], capture_output=True, text=True, cwd=harbor)
    if r.returncode:
        raise SystemExit(f"{image.name}: {r.stderr or r.stdout}")
    w = wave.open(str(out / "audio.wav"))
    d = w.readframes(w.getnframes())
    return struct.unpack(f"<{len(d) // 2}h", d), (out / "log.txt").read_text()


def slot_index(harbor, image, name):
    sys.path.insert(0, str(harbor / "tools"))
    import hbmod  # noqa: E402
    img = image.read_bytes()
    names = []
    for i in range(32):
        magic, at, size, _ = struct.unpack_from("<IIII", img, 0x30000 + 16 * i)
        if magic != 0x544F4C53:
            break
        h, _, _ = hbmod.parse(img[at - 0x02000000:at - 0x02000000 + size])
        names.append(h["name"])
    return 1 + names.index(name)


def periodicity(left, lag_frames):
    """correlation of the twice-differenced signal's envelope at the lag (the hat's highs under a held chord)"""
    w = int(0.01 * SR)
    dif = [left[i] - left[i - 1] for i in range(1, len(left))]
    dif = [dif[i] - dif[i - 1] for i in range(1, len(dif))]
    env = [math.sqrt(sum(v * v for v in dif[i:i + w]) / w) for i in range(0, len(dif) - w, w // 2)]
    m = sum(env) / len(env)
    e0 = [v - m for v in env]
    lag = round(lag_frames / (w // 2))

    def r(k):
        n = len(e0) - k
        den = math.sqrt(sum(v * v for v in e0[:n]) * sum(v * v for v in e0[k:k + n]))
        return sum(e0[i] * e0[i + k] for i in range(n)) / max(1e-9, den)
    return max(r(k) for k in (lag - 1, lag, lag + 1))


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--harbor", type=Path, default=ROOT.parent / "fm1-harbor-proc")
    ap.add_argument("--ms", type=int, default=12000)
    ap.add_argument("--images", default="fm1-dock_xip.bin", help="the images in the checkout's build/, comma separated")
    a = ap.parse_args()
    failures = []
    for image_name in a.images.split(","):
        image = a.harbor / "build" / image_name
        if not image.exists():
            print(f"  FAIL {image_name}: not built")
            failures.append(image_name)
            continue
        idx = slot_index(a.harbor, image, "ORRERY")
        # picker -> ORRERY (its DRIFT setup); FX opens MIX and PRESETS +1 selects PULSE (the band on ROOT PULSE at
        # 120 with Rock 1); GLO (LEAD page) so the keys play the lead; PLAY at 1.5 s; a lead key held 6.0 .. 8.0 s
        keys = (f"700:8,750:8^,900:e6:{idx},1100:8,1150:8^,1180:2,1200:2^,1220:e6:1,1400:7,1420:7^,"
                "1500:12,1550:12^,6000:21,8000:21^")
        out = ROOT / "build" / "integration" / image_name.replace("_xip.bin", "")
        s, log = run(a.harbor, image, keys, a.ms, out)
        left = s[0::2]
        tail = log.split("---")[-1]
        worst = float(tail.split("worst ")[1].split("(")[1].split(" %")[0])
        overruns = int(tail.split(" overruns")[0].split(",")[-1])
        band = left[int(3.0 * SR):int(5.8 * SR)]
        r8 = periodicity(band, 0.25 * SR)
        before, held = rms(left[int(4.0 * SR):int(5.8 * SR)]), rms(left[int(6.2 * SR):int(7.8 * SR)])
        peak = max(abs(v) for v in left)
        checks = [
            ("session", "strings, lead, bass, drums on one session" in log, "module started on the session"),
            ("overruns", overruns == 0, f"{overruns} overruns"),
            ("budget", worst < 70.0, f"worst block {worst} % (budget 70 %)"),
            ("room", "host reverb" not in log, "the dock's room is off (no host reverb in the processing section)"),
            ("band", r8 > 0.4, f"hat envelope repeats every 250 ms: correlation {r8:.2f}"),
            ("lead", held > before * 1.05, f"lead key adds level: RMS {before:.0f} -> {held:.0f}"),
            ("level", 800 < before and peak < 32767, f"band RMS {before:.0f}, peak {peak}"),
        ]
        for case, ok, why in checks:
            print(f"  {'ok  ' if ok else 'FAIL'} {image_name} {case}: {why}")
            if not ok:
                failures.append(f"{image_name} {case}")
        subprocess.run(["uv", "run", "--with", "numpy", "--with", "pillow", str(ROOT / "tools" / "spectrogram.py"),
                        str(out / "audio.wav"), str(out / "spectrogram.png"), f"ORRERY on the session ({image_name})"],
                       capture_output=True, cwd=ROOT)
    print("all passed" if not failures else f"FAILED: {', '.join(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
