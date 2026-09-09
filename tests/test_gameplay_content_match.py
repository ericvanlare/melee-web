"""Owned source fixtures for new content; not keyboard, rendering or retail acceptance."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class ContentMatchTests(unittest.TestCase):
    def run_trace(self, name, arguments, expected):
        targets = [ROOT / "build" / directory / (name + ".js")
                   for directory in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the source content trace targets")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run([str(node_runtime()), str(target), *map(str, arguments)],
                                cwd=ROOT, capture_output=True, text=True, timeout=180)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        self.assertIn(expected, result.stdout)

    def test_falco_fox_and_yoshis_story_source_lifecycles(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (menu / "MnSlChr.usd", game / "PlFc.dat", game / "PlFx.dat",
                    game / "PlFxAJ.dat", game / "GrNLa.dat", game / "GrNBa.dat",
                    game / "GrSt.dat")
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, Fox/Falco and stage fixtures are required")
        cases = ((32, 20, 8), (31, 20, 8), (32, 2, 8), (31, 2, 8),
                 (8, 20, 8), (8, 2, 8), (32, 2, 20))
        for stage, fighter, opponent in cases:
            with self.subTest(stage=stage, fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [menu, game, stage, fighter, opponent],
                               "Mixed source content intro")

    def test_battlefield_scaled_geometry_and_background_lifetimes(self):
        game = ROOT / "assets-local/next-gate"
        if not (game / "GrNBa.dat").is_file():
            self.skipTest("Owned Battlefield fixtures are required")
        self.run_trace("gameplay_stage_battlefield_trace", [game],
                       "Original Battlefield")


if __name__ == "__main__":
    unittest.main()
