"""Fast rejection controls for the source-only Wasm snapshot experiment."""
from pathlib import Path
import subprocess
import sys
import unittest
import tempfile
import json

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class SourceSnapshotBoundaryTests(unittest.TestCase):
    def test_snapshot_boundary_rejects_active_stack_growth_and_shared_memory(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/quiescent_wasm_snapshot_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("snapshot boundary controls passed", result.stdout)

    def test_runner_retains_preparation_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "failed-run"
            result = subprocess.run(
                [str(node_runtime()), str(ROOT / "scripts/check_source_snapshot.mjs"),
                 "--runtime", str(root / "missing.js"), "--assets", str(root / "missing-assets"),
                 "--out", str(output)],
                cwd=ROOT, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((output / "evidence.json").read_text())
            self.assertEqual(report["result"], "failed")
            self.assertIn("ENOENT", report["failure"])
            self.assertTrue((output / "native-host.log").is_file())
