"""Owned source fixtures for new content; not keyboard, rendering or retail acceptance."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class ContentMatchTests(unittest.TestCase):
    def merged_asset_roots(self, *roots):
        """Expose one flat source directory without adding generated assets."""
        directory = tempfile.TemporaryDirectory(prefix="melee-web-content-")
        merged = Path(directory.name)
        for root in roots:
            for source in root.iterdir():
                target = merged / source.name
                if target.exists():
                    continue
                os.symlink(source.resolve(), target)
        return directory, merged

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

    def test_donkey_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        donkey = ROOT / "assets-local/full-game-donkey"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat",
                           "EfMrData.dat", "mario.ssm", "GrNLa.dat")
        required_donkey = (
            "PlDk.dat", "PlDkAJ.dat", "PlDkNr.dat", "PlDkBk.dat",
            "PlDkRe.dat", "PlDkBu.dat", "PlDkGr.dat", "EfDkData.dat",
            "dk.ssm",
        )
        if not (all((common / name).is_file() for name in required_common) and
                all((donkey / name).is_file() for name in required_donkey)):
            self.skipTest("Owned Donkey/Mario, English costumes, menu and FD fixtures are required")
        # CKIND_DONKEY=1 and CKIND_MARIO=8 in the pinned source. The trace
        # reconstructs all five Donkey source costume archives in both player
        # orientations. Only the Donkey-as-P1 orientation drives specials and
        # cargo; the reverse orientation still validates opponent lifetime and
        # teardown without inventing a P2 input recipe.
        temporary, merged = self.merged_asset_roots(common, donkey)
        try:
            for fighter, opponent in ((1, 8), (8, 1)):
                with self.subTest(fighter=fighter, opponent=opponent):
                    self.run_trace(
                        "gameplay_content_match_trace",
                        [merged, merged, 32, fighter, opponent],
                        "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")
        finally:
            temporary.cleanup()

    def test_koopa_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        koopa = ROOT / "assets-local/full-game-koopa"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat",
                           "EfMrData.dat", "mario.ssm", "GrNLa.dat")
        required_koopa = ("PlKp.dat", "PlKpAJ.dat", "PlKpNr.dat", "PlKpRe.dat",
                          "PlKpBu.dat", "PlKpBk.dat", "EfKpData.dat", "koopa.ssm")
        if not (all((common / name).is_file() for name in required_common) and
                all((koopa / name).is_file() for name in required_koopa)):
            self.skipTest("Owned Koopa/Mario, costumes, menu and FD fixtures are required")
        temporary, merged = self.merged_asset_roots(common, koopa)
        try:
            for fighter, opponent in ((5, 8), (8, 5)):
                with self.subTest(fighter=fighter, opponent=opponent):
                    self.run_trace("gameplay_content_match_trace",
                                   [merged, merged, 32, fighter, opponent],
                                   "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")
        finally:
            temporary.cleanup()

    def test_battlefield_scaled_geometry_and_background_lifetimes(self):
        game = ROOT / "assets-local/next-gate"
        if not (game / "GrNBa.dat").is_file():
            self.skipTest("Owned Battlefield fixtures are required")
        self.run_trace("gameplay_stage_battlefield_trace", [game],
                       "Original Battlefield")

    def test_captain_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        captain = ROOT / "assets-local/full-game-captain"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat", "GrNLa.dat")
        required_captain = ("PlCa.dat", "PlCaAJ.dat", "EfCaData.dat", "captain.ssm",
                            "PlCaNr.dat", "PlCaGy.dat", "PlCaRe.usd", "PlCaWh.dat",
                            "PlCaGr.dat", "PlCaBu.dat")
        if not (all((common / name).is_file() for name in required_common) and
                all((captain / name).is_file() for name in required_captain)):
            self.skipTest("Owned Captain/Mario, English costumes, menu and FD fixtures are required")
        # The trace cycles the larger of the two authored costume counts,
        # including Captain's sixth costume when Captain is the opponent.
        for fighter, opponent in ((0, 8), (8, 0)):
            with self.subTest(fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [common, captain, 32, fighter, opponent],
                               "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")

    def test_luigi_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        luigi = ROOT / "assets-local/full-game-luigi"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat", "GrNLa.dat")
        required_luigi = (
            "PlLg.dat", "PlLgAJ.dat", "EfLgData.dat", "luigi.ssm",
            "PlLgNr.dat", "PlLgWh.dat", "PlLgAq.dat", "PlLgPi.dat",
        )
        if not (all((common / name).is_file() for name in required_common) and
                all((luigi / name).is_file() for name in required_luigi)):
            self.skipTest("Owned Luigi/Mario, English costumes, menu and FD fixtures are required")
        # CKIND_LUIGI=7 and CKIND_MARIO=8 in the pinned source. The trace's
        # max-costume loop exercises all four Luigi models in both fighter
        # orientations while its Luigi branch checks N/air-N/S/Hi/Lw and the
        # Fire article's creation/destruction lifecycle.
        for fighter, opponent in ((7, 8), (8, 7)):
            with self.subTest(fighter=fighter, opponent=opponent):
                self.run_trace("gameplay_content_match_trace",
                               [common, luigi, 32, fighter, opponent],
                               "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")

    def test_pikachu_pichu_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        pikachu = ROOT / "assets-local/full-game-pikachu"
        pichu = ROOT / "assets-local/full-game-pichu"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat", "GrNLa.dat")
        required_pikachu = (
            "PlPk.dat", "PlPkAJ.dat", "PlPkNr.dat", "PlPkRe.dat",
            "PlPkBu.dat", "PlPkGr.dat", "EfPkData.dat",
            "pikachu-root.ssm", "pikachu.ssm",
        )
        required_pichu = (
            "PlPc.dat", "PlPcAJ.dat", "PlPcNr.dat", "PlPcRe.dat",
            "PlPcBu.dat", "PlPcGr.dat", "pichu-root.ssm", "pichu.ssm",
        )
        if not all((common / name).is_file() for name in required_common):
            self.skipTest("Owned common Mario, menu and FD fixtures are required")
        if not all((pikachu / name).is_file() for name in required_pikachu):
            self.skipTest("Owned Pikachu and shared effect/audio fixtures are required")
        if not all((pichu / name).is_file() for name in required_pichu):
            self.skipTest("Owned Pichu fixtures are required")

        # CKIND_PIKACHU=13 and CKIND_PICHU=24. The source trace's larger-side
        # loop reconstructs all four authored family costumes in either player
        # orientation. Pichu reuses EfPkData.dat from the shared family root.
        for family, ckind in ((pikachu, 13), (pichu, 24)):
            roots = (common, pikachu, pichu) if family == pichu else (common, pikachu)
            temporary, merged = self.merged_asset_roots(*roots)
            try:
                for fighter, opponent in ((ckind, 8), (8, ckind)):
                    with self.subTest(fighter=fighter, opponent=opponent):
                        self.run_trace(
                            "gameplay_content_match_trace",
                            [merged, merged, 32, fighter, opponent],
                            "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")
            finally:
                temporary.cleanup()

    def test_purin_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        purin = ROOT / "assets-local/full-game-jigglypuff"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat", "mario.ssm",
                           "GrNLa.dat")
        required_purin = ("PlPr.dat", "PlPrAJ.dat", "PlPrNr.dat", "PlPrRe.dat",
                          "PlPrBu.dat", "PlPrGr.dat", "PlPrYe.dat", "EfPrData.dat",
                          "purin.ssm")
        if not (all((common / name).is_file() for name in required_common) and
                all((purin / name).is_file() for name in required_purin)):
            self.skipTest("Owned Purin/Mario, English costumes, menu and FD fixtures are required")
        # CKIND_PURIN=15 and CKIND_MARIO=8. The trace's five-costume loop
        # reconstructs every Purin hat/material in either player orientation;
        # only the forward orientation drives Purin's special fixture.
        for fighter, opponent in ((15, 8), (8, 15)):
            with self.subTest(fighter=fighter, opponent=opponent):
                self.run_trace(
                    "gameplay_content_match_trace",
                    [common, purin, 32, fighter, opponent],
                    "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed")

    def test_old_yoshi_source_match_entry_and_repeat_teardown(self):
        common = ROOT / "assets-local/full-game-ganon"
        stage = ROOT / "assets-local/full-game-stage-yoshis-island-64"
        if not all((root / name).is_file() for root, names in (
                (common, ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat", "mario.ssm")),
                (stage, ("GrOy.dat", "old_ys.hps"))) for name in names):
            self.skipTest("Owned Old Yoshi, Mario and menu fixtures are required")
        self.run_trace("gameplay_content_match_trace",
                       [common, stage, 29, 8, 8, "--entry-only"],
                       "Source content entry, costumes, stage lifecycle, pause and repeat teardown passed")

    def test_hyrule_temple_source_lifecycles(self):
        temple = ROOT / "assets-local/full-game-stage-hyrule-temple"
        common = ROOT / "assets-local/full-game-ganon"
        required_temple = ("GrSh.dat", "shrine.hps", "akaneia.hps")
        required_common = ("PlCo.dat", "ItCo.usd", "EfCoData.dat", "PdPm.dat",
                           "LbRb.dat", "PlMr.dat", "PlMrAJ.dat", "EfMrData.dat",
                           "mario.ssm", "sislib_font.bin")
        if not (all((temple / name).is_file() for name in required_temple) and
                all((common / name).is_file() for name in required_common)):
            self.skipTest("Owned Temple and common Mario/runtime fixtures are required")
        self.run_trace("gameplay_stage_temple_trace", [temple, common],
                       "Original Hyrule Temple source maps, scaled collision, lights, BGM1/BGM75 readiness and two-lifetime teardown passed")


if __name__ == "__main__":
    unittest.main()
