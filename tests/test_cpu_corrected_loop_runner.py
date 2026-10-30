"""Discover the private reusable host loop controls without running a browser."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CpuCorrectedLoopRunnerTests(unittest.TestCase):
    def test_host_controls(self):
        subprocess.run(['node', '--test', 'src/slippi_profile/tests/test_cpu_corrected_loop_runner.mjs'],
                       cwd=ROOT, check=True)
