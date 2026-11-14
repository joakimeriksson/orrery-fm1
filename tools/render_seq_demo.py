#!/usr/bin/env python3
"""Render and validate both real-engine audition mixes plus four stems."""
import os
from pathlib import Path
import subprocess
import wave
ROOT = Path(__file__).resolve().parents[1]
out = ROOT/'build/seq-demo/native'
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3','tests/run_native.py','audition','--sanitize','on','--build-dir','build/native-audition'], cwd=ROOT, env=dict(os.environ, MD_AUDITION_RENDER_DIR=str(out)), check=True)
for raw in out.glob('*.s16le'):
    with wave.open(str(raw.with_suffix('.wav')), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(raw.read_bytes())
print(out)
