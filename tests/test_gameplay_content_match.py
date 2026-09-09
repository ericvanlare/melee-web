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

    def test_mixed_falco_mario_costumes_on_both_stages(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not (menu / "MnSlChr.usd").is_file() or not (game / "PlFc.dat").is_file():
            self.skipTest("Owned menu and fighter fixtures are required")
        for stage in (32, 31):
            with self.subTest(stage=stage):
                self.run_trace("gameplay_content_match_trace", [menu, game, stage],
                               "Mixed Falco/Mario source intro")

    def test_battlefield_scaled_geometry_and_background_lifetimes(self):
        game = ROOT / "assets-local/next-gate"
        if not (game / "GrNBa.dat").is_file():
            self.skipTest("Owned Battlefield fixtures are required")
        self.run_trace("gameplay_stage_battlefield_trace", [game],
                       "Original Battlefield")


if __name__ == "__main__":
    unittest.main()
