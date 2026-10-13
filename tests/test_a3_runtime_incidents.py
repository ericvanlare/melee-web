"""The room capture must retain existing incident evidence without a new clock owner."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class A3RuntimeIncidentTests(unittest.TestCase):
    def test_incident_recording_policy(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(ROOT / "tests/a3_runtime_incidents.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
