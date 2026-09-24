"""Actual source profile initialization and ownership; no disc assets needed."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class SaveProfileTests(unittest.TestCase):
    def test_original_initialization_and_restoration(self):
        targets = [ROOT / "build" / name / "gameplay_save_profile_trace.js"
                   for name in ("browser", "browser-release")]
        available = [path for path in targets if path.is_file()]
        if not available:
            self.skipTest("Build gameplay_save_profile_trace")
        target = max(available, key=lambda path: path.stat().st_mtime_ns)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Original F600 profile init and Toy aggregate lifetime passed",
                      run.stdout)
