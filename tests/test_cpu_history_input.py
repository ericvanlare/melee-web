"""Discover the bounded retained-history input controls without a browser."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CpuHistoryInputTests(unittest.TestCase):
    def test_host_controls(self):
        subprocess.run(['node', '--test', 'src/slippi_profile/tests/test_cpu_history_input.mjs'],
                       cwd=ROOT, check=True)
