"""Discover the source-only native observation projection contract control."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class StadiumNativeObservationProjectionTests(unittest.TestCase):
    def test_actual_reader_callback_contract_in_vm(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "tests/stadium_c1a_browser_observation_projection.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
