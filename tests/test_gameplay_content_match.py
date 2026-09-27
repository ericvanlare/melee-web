"""Owned source fixtures for new content; not keyboard, rendering or retail acceptance."""
from pathlib import Path
import json
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
        command = [str(node_runtime()), str(target), *map(str, arguments)]
        result = subprocess.run(command,
                                cwd=ROOT, capture_output=True, text=True, timeout=180)
        if result.returncode:
            # A minified Emscripten JS line can hide both the advancing source
            # diagnostics and the stack in unittest's last-N-character tail.
            # Retain the entire failure before making the displayed tail brief.
            failure_root = ROOT / "work" / "content-test-failures"
            failure_root.mkdir(parents=True, exist_ok=True)
            evidence = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=failure_root))
            (evidence / "stdout.log").write_text(result.stdout)
            (evidence / "stderr.log").write_text(result.stderr)
            (evidence / "command.json").write_text(json.dumps(
                {"command": command, "exit_code": result.returncode}, indent=2))
            brief = "\n".join(line[:500] for line in
                              (result.stdout + result.stderr).splitlines())[-12000:]
            self.fail(f"Trace exit {result.returncode}; full logs: {evidence}\n{brief}")
        expectations = (expected,) if isinstance(expected, str) else expected
        for item in expectations:
            self.assertIn(item, result.stdout)

    def test_kirby_donor_effect_banks_release_after_world_particles(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (
            menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
            menu / "smash2.sem", menu / "dsp_coef.bin",
            game / "GrNLa.dat", game / "PlGw.dat", game / "PlKb.dat",
            game / "PlPp.dat", game / "PlFx.dat", game / "EfKbIc.dat",
            game / "kirby.ssm", game / "ice.ssm",
        )
        if not all(path.is_file() for path in required):
            self.skipTest("Owned A-lineup and Kirby/Ice donor fixtures are required")
        self.run_trace(
            "gameplay_content_match_trace",
            [menu, game, 32, 20, 8, "--a-prefix-teardown"],
            "A source prefix teardown after effect runtime passed")

    def test_kirby_copy_use_loss_reacquisition_and_teardown_by_donor(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        common = (
            menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
            menu / "smash2.sem", menu / "dsp_coef.bin",
            game / "GrNLa.dat", game / "ItCo.usd", game / "PlKb.dat", game / "kirby.ssm",
        )
        cases = (
            ("Captain/Falcon Punch", 0,
             ("PlCa.dat", "PlCaAJ.dat", "PlKbCpCa.dat", "EfKbCa.dat")),
            ("Game & Watch", 3, ("PlGw.dat", "PlGwAJ.dat", "PlKbCpGw.dat", "EfKbData.dat")),
            ("Ice Climbers", 14, ("PlPp.dat", "PlPpAJ.dat", "PlKbCpPp.dat", "EfKbIc.dat")),
            ("Mario", 8, ("PlMr.dat", "PlMrAJ.dat", "PlKbCpMr.dat", "EfKbMr.dat")),
            ("Samus", 16, ("PlSs.dat", "PlSsAJ.dat", "PlKbCpSs.dat", "EfKbSs.dat")),
            ("Fox", 2, ("PlFx.dat", "PlFxAJ.dat", "PlKbCpFx.dat", "EfKbFx.dat")),
        )
        if not all(path.is_file() for path in common):
            self.skipTest("Owned menu, Final Destination and Kirby source fixtures are required")
        for donor, ckind, names in cases:
            with self.subTest(donor=donor):
                required = tuple(game / name for name in names)
                if not all(path.is_file() for path in required):
                    self.skipTest(f"Owned {donor} donor source fixtures are required")
                phases = ("Kirby donor acquisition phase=initial acquisition",
                          "Kirby donor acquisition phase=same-donor reacquisition")
                if ckind == 0:
                    expectations = (
                        "Kirby copied Falcon Punch: source motion=ftKb_MS_CaSpecialN",
                        "Kirby action coverage: Captain/Falcon Punch acquire/use/loss/reacquisition and match teardown path passed",
                        *phases,
                    )
                elif ckind == 14:
                    expectations = (
                        "Kirby Ice donor slot=1 uses human PAD control",
                        "Kirby Ice copy verified=FTKIND_POPO from captured entity index=",
                        "Kirby copied neutral special created source Article kind=",
                        f"Kirby action coverage: {donor} acquire/use/loss/reacquisition and match teardown path passed",
                        *phases,
                    )
                else:
                    expectations = (
                        "Kirby copied neutral special created source Article kind=",
                        f"Kirby action coverage: {donor} acquire/use/loss/reacquisition and match teardown path passed",
                        *phases,
                    )
                self.run_trace(
                    "gameplay_content_match_trace",
                    [menu, game, 32, 4, ckind, "--character-actions"],
                    expectations)

    def test_kirby_copy_resets_on_ko_and_resumes_after_rebirth(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not all(path.is_file() for path in
                   (menu / "MnSlChr.usd", game / "GrNLa.dat", game / "PlKb.dat")):
            self.skipTest("Owned Kirby and Final Destination source fixtures required")
        # Joint hat, copied parts, and charge-Article ownership families.
        for ckind, archive in ((2, "PlFx.dat"), (3, "PlGw.dat"), (16, "PlSs.dat")):
            with self.subTest(donor=ckind):
                if not (game / archive).is_file():
                    self.skipTest("Owned copy donor fixture required")
                self.run_trace("gameplay_content_match_trace",
                    [menu, game, 32, 4, ckind, "--kirby-copy-ko"],
                    ("Kirby copied ability KO loss, Rebirth and grounded gameplay passed",
                     "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed"))

    def test_kirby_mario_to_fox_distinct_donor_replacement(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (
            menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
            menu / "smash2.sem", menu / "dsp_coef.bin",
            game / "GrNLa.dat", game / "ItCo.usd", game / "PlKb.dat", game / "kirby.ssm",
            game / "PlMr.dat", game / "PlMrAJ.dat", game / "PlKbCpMr.dat", game / "EfKbMr.dat",
            game / "PlFx.dat", game / "PlFxAJ.dat", game / "PlKbCpFx.dat", game / "EfKbFx.dat",
        )
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, FD, Kirby, Mario and Fox copy fixtures are required")
        self.run_trace(
            "gameplay_content_match_trace",
            [menu, game, 32, 4, 8, "--kirby-mario-fox-replacement"],
            (
                "Kirby acquired donor FighterKind=0 from slot=1",
                "Kirby acquired donor FighterKind=1 from slot=2",
                "Mario acquire/use, ordinary up-appeal copy loss, Fox distinct-donor acquire/use",
            ),
        )

    def test_kirby_joint_hat_and_costume_parts_copy_lifecycle_all_colors(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (menu / "MnSlChr.usd", game / "GrNLa.dat", game / "PlKb.dat",
                    game / "PlKbCpFx.dat", game / "PlKbCpGw.dat")
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Kirby, Fox and Game & Watch fixtures required")
        # Fox's joint-backed hat consumes visibility row zero; Game & Watch's
        # parts-only copy consumes the body costume and its source fallbacks.
        for donor, name in ((2, "Fox"), (3, "Game & Watch")):
            with self.subTest(donor=name):
                self.run_trace(
                    "gameplay_content_match_trace",
                    [menu, game, 32, 4, donor, "--kirby-copy-costumes"],
                    ("Construct mixed content stage=32 costume=5",
                     f"Kirby action coverage: {name} acquire/use/loss/reacquisition",
                     "repeat teardown passed"))

    def test_remaining_fighter_all_costumes_both_orientations_entry_and_teardown(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (menu / "MnSlChr.usd", menu / "main.ssm", menu / "smash2.sem",
                    game / "GrNLa.dat", game / "PlFx.dat", game / "PlFxAJ.dat",
                    *(game / f"Pl{prefix}.dat" for prefix in
                      ("Gw", "Kb", "Pp", "Ss", "Ys", "Zd", "Sk")))
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, FD, Fox and remaining-fighter fixtures required")
        # The trace enumerates the source registry's costume count and creates
        # a new match per color. This gate is construction/advancing entry,
        # source pause/No Contest and repeated teardown, not move coverage.
        # Zelda/Sheik P1 also uses the separate original held-A startup path;
        # in-match transformation coverage belongs to the distinctive test.
        for character in (3, 4, 14, 16, 17, 18, 19):
            for fighter, opponent in ((character, 2), (2, character)):
                with self.subTest(fighter=fighter, opponent=opponent):
                    self.run_trace("gameplay_content_match_trace",
                                   [menu, game, 32, fighter, opponent, "--entry-only"],
                                   "Source content entry, costumes, stage lifecycle, pause and repeat teardown passed")

    def test_remaining_fighter_distinctive_actions_and_lifecycle(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        common = (
            menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
            menu / "smash2.sem", menu / "dsp_coef.bin",
            game / "GrNLa.dat", game / "ItCo.usd", game / "PlMr.dat", game / "PlMrAJ.dat",
        )
        cases = (
            ("Game & Watch", 3, ("PlGw.dat", "PlGwAJ.dat", "PlGwNr.dat", "gw.ssm"),
             ("Game & Watch action coverage: Chef motion/article lifetime passed",)),
            ("Ice Climbers", 14, ("PlPp.dat", "PlPpAJ.dat", "PlPpNr.dat", "PlPpRe.dat", "ice.ssm"),
             ("Ice Climbers action coverage: Belay separated Popo/Nana",)),
            ("Samus", 16, ("PlSs.dat", "PlSsAJ.dat", "EfSsData.dat", "samus.ssm"),
             ("Samus action coverage: down-B Bomb lifetime", "CatchWait -> source throw animation")),
            ("Yoshi", 17, ("PlYs.dat", "PlYsAJ.dat", "EfYsData.dat", "yoshi.ssm"),
             ("Yoshi action coverage: neutral-B captured Mario",)),
            ("Zelda", 18, ("PlZd.dat", "PlZdAJ.dat", "PlSk.dat", "PlSkAJ.dat",
                            "EfZdData.dat", "zs.ssm"),
             ("Zelda/Sheik action coverage: both in-match down-B directions",
              "Active Player_GetEntity/session stats agreed after every Zelda/Sheik form change")),
            ("Sheik", 19, ("PlZd.dat", "PlZdAJ.dat", "PlSk.dat", "PlSkAJ.dat",
                            "EfZdData.dat", "zs.ssm"),
             ("Sheik action coverage: repeated down-B both ways, ground/air side-B",
              "Zelda/Sheik action coverage: both in-match down-B directions",
              "Active Player_GetEntity/session stats agreed after every Zelda/Sheik form change")),
        )
        required = (*common, *(game / name for _, _, names, _ in cases for name in names))
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, FD, Mario and remaining-fighter action fixtures are required")
        for fighter, ckind, _, expected in cases:
            with self.subTest(fighter=fighter):
                self.run_trace("gameplay_content_match_trace",
                               [menu, game, 32, ckind, 8, "--character-actions"], expected)

    def test_remaining_fighter_ground_and_air_up_special_continuation(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
                    menu / "smash2.sem", menu / "dsp_coef.bin", game / "GrNLa.dat",
                    game / "ItCo.usd", game / "PlMr.dat", game / "PlMrAJ.dat")
        cases = (("Mr. Game & Watch", 3, "Gw"), ("Kirby", 4, "Kb"),
                 ("Samus", 16, "Ss"), ("Yoshi", 17, "Ys"),
                 ("Zelda", 18, "Zd"), ("Sheik", 19, "Sk"))
        required += tuple(game / f"Pl{prefix}{suffix}.dat"
                          for _, _, prefix in cases for suffix in ("", "AJ", "Nr"))
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, FD, Mario and remaining-fighter up-special fixtures required")
        for fighter, ckind, _ in cases:
            with self.subTest(fighter=fighter):
                self.run_trace("gameplay_content_match_trace",
                               [menu, game, 32, ckind, 8, "--remaining-up-special"],
                               ("grounded up-special motion, continuation, landing and Article lifecycle passed",
                                "aerial up-special motion, continuation, landing and Article lifecycle passed"))

    def test_ice_nana_rejoins_after_popo_stock_loss_on_source_cpu9_lineup_a(self):
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        required = (
            menu / "MnSlChr.usd", menu / "main.ssm", menu / "mario.ssm",
            menu / "smash2.sem", menu / "dsp_coef.bin",
            game / "GrNLa.dat", game / "ItCo.usd",
            game / "PlGw.dat", game / "PlGwAJ.dat", game / "PlGwNr.dat", game / "gw.ssm",
            game / "PlKb.dat", game / "PlKbAJ.dat", game / "PlKbNr.dat", game / "kirby.ssm",
            game / "PlPp.dat", game / "PlPpAJ.dat", game / "PlPpNr.dat",
            game / "PlPpRe.dat", game / "ice.ssm",
            game / "PlFx.dat", game / "PlFxAJ.dat", game / "PlFxNr.dat", game / "fox.ssm",
        )
        if not all(path.is_file() for path in required):
            self.skipTest("Owned menu, Final Destination and four-player A-lineup fixtures are required")
        self.run_trace(
            "gameplay_content_match_trace",
            [menu, game, 32, 3, 8, "--ice-cpu-lifecycle"],
            (
                "Nana death while Popo alive",
                "Nana Sleep while Popo alive",
                "unchanged since Nana death",
                "subsequent Popo stock loss",
                "both entities Rebirth, Nana damage=0",
                "Nana resumed gameplay motion=",
                "Nana_resumed=1",
                "Ice CPU lifecycle repeated teardown passed",
            ),
        )

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

    def test_mewtwo_source_lifecycles_both_orientations(self):
        common = ROOT / "assets-local/full-game-ganon"
        mewtwo = ROOT / "assets-local/full-game-mewtwo"
        required_common = ("MnSlChr.usd", "PlMr.dat", "PlMrAJ.dat",
                           "EfMrData.dat", "mario.ssm", "GrNLa.dat")
        required_mewtwo = ("PlMt.dat", "PlMtAJ.dat", "PlMtNr.dat", "PlMtRe.dat",
                           "PlMtBu.dat", "PlMtGr.dat", "EfMtData.dat", "mewtwo.ssm")
        if not (all((common / name).is_file() for name in required_common) and
                all((mewtwo / name).is_file() for name in required_mewtwo)):
            self.skipTest("Owned Mewtwo/Mario, costumes, menu and FD fixtures are required")
        temporary, merged = self.merged_asset_roots(common, mewtwo)
        try:
            # CKIND_MEWTWO=10 and CKIND_MARIO=8 in the pinned source. The
            # trace reconstructs all four Mewtwo source costume archives in
            # both player orientations. Only Mewtwo-as-P1 drives specials;
            # the reverse orientation still validates opponent lifetime and
            # teardown without inventing a P2 input recipe.
            for fighter, opponent in ((10, 8), (8, 10)):
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
