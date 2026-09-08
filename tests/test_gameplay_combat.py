"""Run the raw PAD close-range combat trace against the owned runtime assets."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplayCombatTests(unittest.TestCase):
    def test_two_world_jab_shield_grab_and_directional_throws(self):
        assets = ROOT / "assets-local/next-gate"
        names = (
            "PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat", "GrNLa.dat",
            "ItCo.usd", "EfMrData.dat", "EfCoData.dat", "PdPm.dat", "sislib_font.bin",
        )
        binary = ROOT / "build/browser/gameplay_combat_trace.js"
        if not binary.is_file() or not all((assets / name).is_file() for name in names):
            self.skipTest("Optional built combat trace and owned runtime assets required")
        result = subprocess.run(
            [str(node_runtime()), str(binary), str(assets)],
            cwd=ROOT, capture_output=True, text=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "Original Mario close-range jab, shield, grab and directional throw and move paths passed in two complete passes",
            result.stdout,
        )
        output = result.stdout + result.stderr
        for label in ("forward", "back", "up", "down"):
            self.assertIn(f"{label} entered source throw", output)


if __name__ == "__main__":
    unittest.main()
