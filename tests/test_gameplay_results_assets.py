"""Focused Results action-store integration against the source Nana root."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplayResultsAssetsTests(unittest.TestCase):
    def test_nana_demo_actions_use_nonzero_ice_results_root(self):
        target = ROOT / "build/browser-release/gameplay_results_assets_test.js"
        assets = ROOT / "assets-local/next-gate"
        required = [
            "PlNn.dat", "PlNnAJ.dat", "PlPp.dat", "PlPpAJ.dat",
            "GmRstMPn.dat",
        ]
        if not target.is_file() or not all((assets / name).is_file() for name in required):
            self.skipTest("Build the Results asset trace and provide owned Ice Climber archives")
        result = subprocess.run(
            [str(node_runtime(ROOT)), str(target), str(assets), "Nana"],
            cwd=ROOT, capture_output=True, text=True, timeout=90,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Nana demo action store used the authored nonzero Results root",
                      result.stdout)
        self.assertIn("root offset 88800", result.stdout)


if __name__ == "__main__":
    unittest.main()
