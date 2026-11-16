#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A minute of the ORRERY module in fm1-emu, recorded: the PULSE setup's band (bass on ROOT PULSE with its pattern,
the Mini Pops), the strings following the pattern's chords, the lead over it with the echo, and out.
  ./demo.py [FM1_DOCK] [OUT_DIR]     (defaults ../fm1-harbor-proc, ./build/demo) -> OUT_DIR/demo.wav
FM1_DOCK is an fm1-dock checkout built with the module in its image (README, Build)."""
import struct
import subprocess
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
HARBOR = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "fm1-harbor-proc"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "build" / "demo"
BIN = HARBOR / "build" / "fm1-dock_xip.bin"
sys.path.insert(0, str(HARBOR / "tools"))
import hbmod  # noqa: E402

HOME, PLAY, FX = 8, 12, 2
ev = []


def at(ms, what): ev.append((int(ms), what))
def press(ms, btn, hold=50): at(ms, f"{btn}"); at(ms + hold, f"{btn}^")
def enc(ms, e, clicks): at(ms, f"e{e}:{clicks}")
def midi(ms, *b): at(ms, "m" + "".join(f"{x:02x}" for x in b))


def names():
    img = BIN.read_bytes()
    out = ["SINE"]
    for i in range(32):
        magic, a, size, crc = struct.unpack_from("<IIII", img, 0x30000 + 16 * i)
        if magic != 0x544F4C53:
            break
        h, _, _ = hbmod.parse(img[a - 0x02000000:a - 0x02000000 + size])
        out.append(h["name"])
    return out


def main():
    mods = names()
    if "ORRERY" not in mods:
        raise SystemExit(f"demo: ORRERY is not in {BIN}: {mods}")
    press(700, HOME); enc(900, 6, mods.index("ORRERY")); press(1100, HOME)   # the module from the list
    press(1180, FX, 20); enc(1220, 6, 1)                      # PRESETS one on: PULSE, the second setup
    t0 = 2000
    press(t0, PLAY)                                           # the band: bass, drums and the first chord, together
    bar = 2000                                                # 120 bpm; ROOT PULSE's pattern: 2 1 1 1 bars, 5 bars = 10 s
    # the lead (ARP LEAD, the module's start patch) from the third pattern: phrases in E minor, A minor, G minor
    phrases = [[(76, 1800), (71, 1800), (67, 1500), (69, 2500), (0, 400)],    # bars 1-2, E minor
               [(72, 900), (69, 900), (76, 1800), (0, 400)],                  # bar 3, A minor
               [(71, 900), (67, 900), (64, 1800), (0, 400)],                  # bar 4, E minor
               [(74, 900), (70, 900), (67, 1800), (0, 400)]]                  # bar 5, G minor
    for cyc in (2, 3):
        t = t0 + cyc * 5 * bar
        for k, ph in enumerate(phrases):
            tm = t + (0 if k == 0 else (k + 1) * bar)
            for note, length in ph:
                if note:
                    midi(tm, 0x90, note, 110)
                    midi(tm + length - 80, 0x80, note, 0)
                tm += length
    t_end = t0 + 5 * 5 * bar                                  # five patterns: 52 s
    press(t_end, PLAY)                                        # the band out; the strings and the echo ring
    end = t_end + 6000
    ev.sort(key=lambda e: e[0])
    keys = ",".join(f"{ms}:{w}" for ms, w in ev)
    OUT.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([sys.executable, str(HARBOR / "tools" / "dock.py"), "emu", "--bin", str(BIN), "--ms", str(end),
                        "--keys", keys, "--out", str(OUT)], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"demo: {r.stderr or r.stdout}")
    w = wave.open(str(OUT / "audio.wav"))
    d = w.readframes(w.getnframes())
    s = struct.unpack(f"<{len(d) // 2}h", d)
    g = 32767 * 0.89 / max(1, max(abs(v) for v in s))
    o = wave.open(str(OUT / "demo.wav"), "wb")
    o.setnchannels(2); o.setsampwidth(2); o.setframerate(44100)
    o.writeframes(struct.pack(f"<{len(s)}h", *(int(v * g) for v in s)))
    o.close()
    print(f"{OUT / 'demo.wav'}: {end / 1000:.0f} s, at -1 dB; the log's last lines:")
    print("\n".join((OUT / "log.txt").read_text().splitlines()[-3:]))


if __name__ == "__main__":
    main()
