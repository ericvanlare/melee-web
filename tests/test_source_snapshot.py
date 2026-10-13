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
            self.assertEqual(report["selection"], {
                "checkpoints": [300, 318, 420, 438, 500, 540, 558],
                "depths": [1, 2, 4, 7],
                "requiresGroundedPlayers": False,
            })

    def test_runner_bounds_checkpoint_depth_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = ROOT / "scripts/check_source_snapshot.mjs"
            node = str(node_runtime())
            common = [node, str(script), "--runtime", str(root / "missing.js"),
                      "--assets", str(root / "missing-assets")]

            for label, selection, message in (
                ("checkpoint-without-depth", ["--checkpoint", "300"], "must be selected together"),
                ("depth-without-checkpoint", ["--depth", "1"], "must be selected together"),
                ("unsupported-checkpoint", ["--checkpoint", "301", "--depth", "1"], "Unsupported checkpoint"),
                ("unsupported-depth", ["--checkpoint", "300", "--depth", "3"], "Unsupported depth"),
            ):
                output = root / label
                result = subprocess.run(common + ["--out", str(output)] + selection,
                                        cwd=ROOT, capture_output=True, text=True, timeout=20)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(message, result.stderr)
                self.assertFalse(output.exists(), f"invalid selection created {output}")

            selected_output = root / "selected"
            selected = subprocess.run(
                common + ["--out", str(selected_output), "--checkpoint", "300", "--depth", "1"],
                cwd=ROOT, capture_output=True, text=True, timeout=20)
            self.assertEqual(selected.returncode, 1, selected.stdout + selected.stderr)
            report = json.loads((selected_output / "evidence.json").read_text())
            self.assertEqual(report["selection"], {
                "checkpoints": [300], "depths": [1], "requiresGroundedPlayers": True,
            })
            self.assertIn("ENOENT", report["failure"])
