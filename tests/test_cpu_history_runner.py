"""Discover retained-history host admission and evidence controls."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CpuHistoryRunnerTests(unittest.TestCase):
    def test_host_controls(self):
        subprocess.run(['node', '--test', 'src/slippi_profile/tests/test_cpu_history_runner.mjs'],
                       cwd=ROOT, check=True)
