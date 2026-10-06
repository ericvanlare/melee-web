"""Run bounded staging experiment and native observer JavaScript contracts."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class StagingRingBrowserBoundaryTests(unittest.TestCase):
    def run_boundary(self, filename):
        result = subprocess.run([str(node_runtime()), '--test', str(ROOT / 'tests' / filename)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_write_sequences(self):
        self.run_boundary('staging_ring_byte_capture_test.mjs')

    def test_native_completion_scope(self):
        self.run_boundary('staging_ring_replay_completion_test.mjs')

    def test_retained_report_comparison(self):
        self.run_boundary('staging_ring_report_comparison_test.mjs')

    def test_exact_stall_boundary(self):
        self.run_boundary('pause_trace_capture_test.mjs')

    def test_actual_development_observer_storage(self):
        self.run_boundary('development_css_observer_storage_test.mjs')
