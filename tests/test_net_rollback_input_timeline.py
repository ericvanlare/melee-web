"""Focused Node behavior tests for the browser-portable input timeline."""

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class RollbackInputTimelineTests(unittest.TestCase):
    def test_node_behavioral_contract(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "tests/net_rollback_input_timeline.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
