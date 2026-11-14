#!/usr/bin/env python3
"""Run the isolated new-sequencer audition in fm1-emu (never hardware)."""
import argparse
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--check', action='store_true', help='headless playback/control capture')
p.add_argument('--image', type=Path, default=ROOT/'build/seq-demo/melodock-test_xip.bin')
a = p.parse_args()
a.image = a.image.resolve()
if not a.image.is_file():
    p.error('audition image missing; see audition/README.md for build command')
live = Path(os.environ.get('FM1_LIVE', ROOT.parent/'fm1-emu/target/release/fm1-live'))
if a.check:
    command = ['python3', str(ROOT.parent/'fm1-harbor-proc/tools/dock.py'), 'emu', '--bin', str(a.image), '--ms', '10000', '--keys', '700:8,750:8^,900:e6:1,1100:8,1150:8^,1500:12,1550:12^,3200:11,3250:11^,4500:3,4550:3^,6000:e1:5,7500:12,7550:12^', '--out', str(ROOT/'build/seq-demo/emulator')]
else:
    print('HOME: picker; PRESETS: MELODOCK TEST; HOME: select; PLAY: start.\nSEL: lead/FX restart; SEQ: next pattern; ALGO: tempo; ARP: panic.', flush=True)
    command = [str(live), '--bin', str(a.image), '--midi', 'none']
raise SystemExit(subprocess.call(command, cwd=ROOT))
