"""Discover staging rollback observation and real-correction evidence controls."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class RollbackStagingObserverTests(unittest.TestCase):
    def test_observer_ownership_controls(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(ROOT / "tests/rollback_staging_observer.test.mjs"),
             str(ROOT / "tests/staging_rollback_pair_correction.test.mjs"),
             str(ROOT / "tests/staging_rollback_pair_cdp.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
