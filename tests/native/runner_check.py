#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify native-runner failure propagation using temporary compiler stubs."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'tests/run_native.py'


class RunnerCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name).resolve()

    def run_runner(self, *args, compiler=None):
        env = os.environ.copy()
        if compiler:
            stub = self.output / 'compiler.py'
            stub.write_text(compiler)
            env['CC'] = shlex.join([sys.executable, str(stub)])
        return subprocess.run([sys.executable, str(RUNNER), '--build-dir', str(self.output / 'out'),
                               '--sanitize', 'off', *args], env=env, capture_output=True, text=True)

    def test_compile_failure_returns_nonzero(self):
        result = self.run_runner('contracts', compiler='raise SystemExit(37)\n')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('FAILED: contracts_test', result.stderr)
        self.assertNotIn('+ ' + str(self.output / 'out/contracts_test') + '\n', result.stdout)

    def test_execution_failure_returns_nonzero(self):
        compiler = '''import pathlib,sys
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1])
output.write_text('#!/usr/bin/env python3\\nraise SystemExit(9)\\n')
output.chmod(0o755)
'''
        result = self.run_runner('contracts', compiler=compiler)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('FAILED: contracts_test', result.stderr)
        self.assertIn('+ ' + str(self.output / 'out/contracts_test'), result.stdout)

    def test_unknown_test_returns_nonzero(self):
        result = self.run_runner('not_a_real_suite')
        self.assertEqual(result.returncode, 1)
        self.assertIn('unknown tests', result.stderr)
        self.assertNotIn('Traceback', result.stderr)


if __name__ == '__main__':
    unittest.main()
