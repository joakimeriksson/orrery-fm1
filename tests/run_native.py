#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile/run portable C tests; each *_test.c declares MD_TEST_SOURCES dependencies.

    python3 tests/run_native.py [name ...] [--sanitize auto|on|off]

Optional comment in each test, paths relative to repo root:
    // MD_TEST_SOURCES: tests/native/fakes.c core/router.c

CC may contain a compiler and flags; commands use argument vectors, never a shell.
Sanitizers default to auto: a compile/run probe enables address+undefined checks
when supported. --sanitize on requires support instead of silently falling back.
Outputs are isolated under build/native. No SDK, hardware, engines or runtime C
sources are implicitly linked. New tests can add their own dependency comment.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(ROOT)]
SANITIZERS = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']


def invoke(command):
    print('+ ' + shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=ROOT).returncode


def compiler_command():
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    if not compiler or not shutil.which(compiler[0]):
        raise ValueError('C compiler unavailable; set CC to an installed compiler')
    return compiler


def sanitizer_flags(mode, compiler, output):
    if mode == 'off':
        return []
    source = output / 'sanitizer_probe.c'
    binary = output / 'sanitizer_probe'
    source.write_text('int main(void) { return 0; }\n')
    compiled = subprocess.run(compiler + FLAGS + SANITIZERS + [str(source), '-o', str(binary)],
                              capture_output=True, text=True)
    ran = subprocess.run([str(binary)], capture_output=True, text=True) if compiled.returncode == 0 else None
    if ran is not None and ran.returncode == 0:
        print('Sanitizers: address + undefined', flush=True)
        return SANITIZERS
    reason = compiled.stderr if ran is None else ran.stderr
    if mode == 'on':
        raise ValueError('requested sanitizers unavailable: ' + reason.strip())
    print('Sanitizers unavailable; running strict native checks without them. ' + reason.strip(), flush=True)
    return []


def dependencies(test):
    matches = re.findall(r'^\s*//\s*MD_TEST_SOURCES:\s*(.*?)\s*$', test.read_text(), re.M)
    if len(matches) > 1:
        raise ValueError(f'{test.name}: declare MD_TEST_SOURCES only once')
    result = []
    for name in shlex.split(matches[0]) if matches else []:
        path = (ROOT / name).resolve()
        if not path.is_relative_to(ROOT) or path.suffix != '.c' or not path.is_file():
            raise ValueError(f'{test.name}: invalid C source dependency {name}')
        if path == test.resolve() or path in result:
            raise ValueError(f'{test.name}: duplicate dependency {name}')
        result.append(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('names', nargs='*', help='test stem with optional _test suffix')
    parser.add_argument('--sanitize', choices=['auto', 'on', 'off'], default='auto')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/native')
    args = parser.parse_args()
    try:
        found = sorted((ROOT / 'tests/native').glob('*_test.c'))
        wanted = {name if name.endswith('_test') else name + '_test' for name in args.names}
        missing = wanted - {test.stem for test in found}
        if missing:
            raise ValueError('unknown tests: ' + ', '.join(sorted(missing)))
        selected = [test for test in found if not wanted or test.stem in wanted]
        if not selected:
            raise ValueError('no native tests discovered')
        # Check mappings before any test is built/executed.
        mappings = [(test, dependencies(test)) for test in selected]
        output = args.build_dir.resolve()
        output.mkdir(parents=True, exist_ok=True)
        compiler = compiler_command()
        flags = sanitizer_flags(args.sanitize, compiler, output)
        failures = []
        for test, sources in mappings:
            binary = output / test.stem
            command = compiler + FLAGS + flags + [str(test), *map(str, sources), '-o', str(binary)]
            if invoke(command) or invoke([binary]):
                failures.append(test.stem)
        if failures:
            print('FAILED: ' + ', '.join(failures), file=sys.stderr)
            return 1
        print(f'PASS: {len(selected)} native suite(s)')
        return 0
    except (OSError, ValueError) as error:
        print(f'native: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
