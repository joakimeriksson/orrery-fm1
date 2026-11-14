#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Portable clock/pattern checks before any screen work; forwards native-runner options."""
from pathlib import Path
import subprocess
import sys

if __name__ == '__main__':
    raise SystemExit(subprocess.run([sys.executable, str(Path(__file__).with_name('run_native.py')),
                                     'seq', 'transport', *sys.argv[1:]]).returncode)
