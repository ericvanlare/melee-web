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

    def test_admitted_fighter_and_stage_source_lifecycles(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (menu / "MnSlChr.usd", game / "PlFc.dat", game / "PlFx.dat",
                    game / "PlFxAJ.dat", game / "GrNLa.dat", game / "GrNBa.dat",
                    game / "GrSt.dat", game / "PlMs.dat", game / "PlMsAJ.dat",
                    game / "GrOp.dat", game / "LbRb.dat")
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, Fox/Falco and stage fixtures are required")
        cases = ((32, 20, 8), (31, 20, 8), (32, 2, 8), (31, 2, 8),
                 (8, 20, 8), (8, 2, 8), (32, 2, 20),
                 (28, 20, 8), (28, 9, 8))
        for stage, fighter, opponent in cases:
            with self.subTest(stage=stage, fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [menu, game, stage, fighter, opponent],
                               "Mixed source content intro")

    def test_dr_mario_roy_source_lifecycles_both_orientations(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (
            menu / "MnSlChr.usd", game / "PlDr.dat", game / "PlDrAJ.dat",
            game / "PlFe.dat", game / "PlFeAJ.dat", game / "EfMrData.dat",
            game / "EfFeData.dat",
            game / "drmario.ssm", game / "emblem.ssm", game / "GrNLa.dat",
        )
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Dr. Mario/Roy and stage fixtures are required")
        # CKIND_DRMARIO=22 and CKIND_EMBLEM=23 in the pinned source. Run both
        # directions so each clone is exercised as the selected fighter and as
        # the opponent while every selected costume reconstructs the world.
        for fighter, opponent in ((22, 23), (23, 22)):
            with self.subTest(fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [menu, game, 8, fighter, opponent],
                               "Mixed source content intro")

    def test_ganondorf_source_lifecycles_both_orientations(self):
        game = ROOT / "assets-local/full-game-ganon"
        required = ("MnSlChr.usd", "PlGn.dat", "PlGnAJ.dat", "PlMr.dat",
                    "PlMrAJ.dat", "EfGnData.dat", "ganon.ssm", "GrNLa.dat")
        if not all((game / name).is_file() for name in required):
            self.skipTest("Owned Ganondorf/Mario, menu and FD fixtures are required")
        for fighter, opponent in ((25, 8), (8, 25)):
            with self.subTest(fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [game, game, 32, fighter, opponent],
                               "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")

    def test_battlefield_scaled_geometry_and_background_lifetimes(self):
        game = ROOT / "assets-local/next-gate"
        if not (game / "GrNBa.dat").is_file():
            self.skipTest("Owned Battlefield fixtures are required")
        self.run_trace("gameplay_stage_battlefield_trace", [game],
                       "Original Battlefield")


if __name__ == "__main__":
    unittest.main()
