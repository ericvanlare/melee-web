"""Focused synthetic controls for the diagnostic shared-page snapshot owner."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class SharedPageSnapshotTests(unittest.TestCase):
    def test_shared_page_controls(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/shared_page_snapshot_controls.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("shared-page snapshot controls passed", result.stdout)

    def test_source_runner_retains_preparation_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "failed-run"
            result = subprocess.run(
                [str(node_runtime()), str(ROOT / "scripts/check_shared_page_snapshot.mjs"),
                 "--runtime", str(root / "missing.js"), "--assets", str(root / "missing-assets"),
                 "--out", str(output)],
                cwd=ROOT, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((output / "evidence.json").read_text())
            self.assertEqual(report["result"], "failed")
            self.assertIn("ENOENT", report["failure"])
            self.assertTrue((output / "failure.json").is_file())
            self.assertTrue((output / "native-host.log").is_file())


if __name__ == "__main__":
    unittest.main()
