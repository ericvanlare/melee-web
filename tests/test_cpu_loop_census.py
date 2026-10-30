"""Expose the private profile's real filesystem evidence controls to discovery."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CpuLoopCensusTests(unittest.TestCase):
    def test_interval_sink_and_route(self):
        subprocess.run(['node', '--test', 'src/slippi_profile/tests/test_cpu_loop_census.mjs'],
                       cwd=ROOT, check=True)
