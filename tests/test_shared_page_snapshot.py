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


    def test_runner_bounds_checkpoint_depth_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = ROOT / "scripts/check_shared_page_snapshot.mjs"
            node = str(node_runtime())
            common = [node, str(script), "--runtime", str(root / "missing.js"),
                      "--assets", str(root / "missing-assets")]

            for label, selection, message in (
                ("checkpoint-without-depth", ["--checkpoint", "300"], "must be selected together"),
                ("depth-without-checkpoint", ["--depth", "7"], "must be selected together"),
                ("unsupported-checkpoint", ["--checkpoint", "301", "--depth", "7"], "Unsupported checkpoint"),
                ("unsupported-depth", ["--checkpoint", "300", "--depth", "3"], "Unsupported depth"),
                ("duplicate-depth", ["--checkpoint", "300", "--depth", "7", "--depth", "1"], "Duplicate option"),
            ):
                output = root / label
                result = subprocess.run(common + ["--out", str(output)] + selection,
                                        cwd=ROOT, capture_output=True, text=True, timeout=20)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(message, result.stderr)
                self.assertFalse(output.exists(), f"invalid selection created {output}")

            selected_output = root / "selected-300-7"
            selected = subprocess.run(
                common + ["--out", str(selected_output), "--checkpoint", "300", "--depth", "7"],
                cwd=ROOT, capture_output=True, text=True, timeout=20)
            self.assertEqual(selected.returncode, 1, selected.stdout + selected.stderr)
            report = json.loads((selected_output / "evidence.json").read_text())
            self.assertEqual(report["selection"]["checkpoints"], [300])
            self.assertEqual(report["selection"]["depths"], [7])
            self.assertTrue(report["selection"]["requiresGroundedPlayers"])
            self.assertFalse(report["selection"]["requiresFullScenarioCoverage"])
            self.assertEqual(report["selection"]["ringStates"], 8)
            self.assertTrue(report["selection"]["ringRunsAfterSelectedCycles"])
            self.assertIn("ENOENT", report["failure"])

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
            self.assertEqual(report["selection"]["checkpoints"], [300, 318, 420, 438, 500, 540, 558])
            self.assertEqual(report["selection"]["depths"], [1, 2, 4, 7])
            self.assertFalse(report["selection"]["requiresGroundedPlayers"])
            self.assertTrue(report["selection"]["requiresFullScenarioCoverage"])
            self.assertEqual(report["selection"]["ringStates"], 8)
            self.assertTrue(report["selection"]["ringRunsAfterSelectedCycles"])
            self.assertTrue((output / "failure.json").is_file())
            self.assertTrue((output / "native-host.log").is_file())


if __name__ == "__main__":
    unittest.main()
