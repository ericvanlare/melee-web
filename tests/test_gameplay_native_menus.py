"""Original SIS bytecode behavior; runs without proprietary menu assets."""
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
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
sys.path.insert(0, str(ROOT / "tools"))
import compare_transition_trace as transition_compare

def has_menu_trophy_assets(*roots):
    return all(any((root / name).is_file() for root in roots)
               for name in ("TyDatai.usd", "TyDatai.dat"))

class NativeMenuSourceTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-c1a-native-menu-")

    def test_stadium_c1a_raw_pad_selection_stops_before_match_admission(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        required = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        missing = [str(menu / name) for name in required if not (menu / name).is_file()]
        if not target.is_file() or missing:
            detail = ", ".join(missing[:5])
            self.skipTest("C1a requires its built host trace and exact owned menu closure" +
                          (f"; missing {detail}" if detail else ""))
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "c1a-selection-port.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-c1a-v1"]
        (self.scratch / "c1a-selection-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "c1a-selection.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "c1a-selection.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "c1a-selection.stdout").write_text(run.stdout, encoding="utf-8")
        (self.scratch / "c1a-selection.stderr").write_text(run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "C1a raw PAD CSS->SSS Stadium selection and exact preparation manifest passed",
            run.stdout,
        )
        self.assertIn(
            "ordinary admission and source/stage construction remain closed",
            run.stdout,
        )

    def test_stadium_c1_reopened_context_lifecycle_preflight(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        required = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        missing = [str(menu / name) for name in required if not (menu / name).is_file()]
        if not target.is_file() or missing:
            detail = ", ".join(missing[:5])
            self.skipTest("C1 context preflight requires its built host trace and exact owned menu closure" +
                          (f"; missing {detail}" if detail else ""))
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "c1-context-preflight.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-c1-context-preflight-v1"]
        (self.scratch / "c1-context-preflight-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "c1-context-preflight.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "c1-context-preflight.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "c1-context-preflight.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "c1-context-preflight.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "C1 reopened-context lifecycle preflight passed; no E8 request",
            run.stdout,
        )
        self.assertIn(
            "stage publication, or source menu entry",
            run.stdout,
        )

    def test_owned_css_scene_lifecycle(self):
        targets = [ROOT / "build" / name / "native_css_callbacks.js"
                   for name in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        menu = ROOT / "assets-local/native-menus"
        audio = ROOT / "assets-local/next-gate"
        required = [menu / name for name in (
            "MnSlChr.usd", "SdSlChr.usd", "MnExtAll.usd", "LbMcGame.usd",
            "NtMemAc.usd", "menu01.hps", "menu3.hps", "nr_select.ssm", "nr_title.ssm",
            "nr_name.ssm", "pokemon.ssm", "end.ssm")] + [audio / name for name in (
            "smash2.sem", "main.ssm", "mario.ssm", "dsp_coef.bin", "sislib_font.bin")]
        if not targets or not all(path.is_file() for path in required):
            self.skipTest("Original CSS target and owned local fixtures are required")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target), str(menu), str(audio)],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original CSS enter, 120 neutral input/scheduler/audio ticks and exit passed in two worlds", run.stdout)

    def test_owned_original_menu_match_loop(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not (game / "PlMr.dat").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu/game fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage_kind in (32, 31):
            with self.subTest(stage_kind=stage_kind):
                with tempfile.TemporaryDirectory(prefix="menu transition trace ") as directory:
                    trace = Path(directory) / "port.jsonl"
                    run = subprocess.run(
                        [str(node_runtime()), str(target), str(menu), str(game),
                         str(stage_kind), str(trace), source_revision], cwd=ROOT,
                        capture_output=True, text=True, timeout=120)
                    self.assertEqual(run.returncode, 0,
                                     (run.stdout + run.stderr)[-4000:])
                    rows = [json.loads(line) for line in trace.read_text().splitlines()]
                    for trace_run in (0, 1):
                        _, events = transition_compare.select_run(rows, "port", trace_run)
                        transition_compare.validate_continuity("port", events)
                self.assertIn(
                    "Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice",
                    run.stdout)

    def test_link_audio_registry_css_unload(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu = game = ROOT / "assets-local/issue34"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")) or not all(
                (game / name).is_file() for name in ("link.ssm", "clink.ssm")):
            self.skipTest("Build the native menu host and supply owned Link audio fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="link css unload trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "link-css-unload-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original CSS Link audio registry entered, aborted and unloaded", run.stdout)
        self.assertIn("Original CSS Young Link audio registry entered, aborted and unloaded", run.stdout)

    def test_title_and_main_checked_abort_teardown(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="title main abort trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "title-main-abort-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original Title Eject released source ownership and allowed CSS re-entry", run.stdout)
        self.assertIn("Original Main Eject released source ownership and allowed CSS re-entry", run.stdout)
        self.assertIn(
            "Original Opening VS handoff selected four CPUs, suspended with retained PAD input, and cleaned up",
            run.stdout,
        )
        self.assertIn("Original all-unlocked CSS roster, P1/P2 Title Start edges, unsupported Challenger, Title timeout to Opening state 1 and recovery passed", run.stdout)
        self.assertIn("Normal CSS->SSS leave cleared its consumed transition before host teardown", run.stdout)
        self.assertIn("Native Title/Main checked abort and CSS re-entry smoke passed", run.stdout)

    def test_original_main_settings_sound_mix_route(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not targets or not (menu / "MnMaAll.usd").is_file():
            self.skipTest("Build the native menu host and supply owned Main-menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="main settings sound trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "main-settings-sound-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original Main Settings Sound changed SaveData mix to -5, retained through Title/Main, returned, re-entered, and cleaned up",
            run.stdout,
        )

    def test_opening_movie_entry_requires_source_heap_owner(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-public-release",
                                "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="opening movie preload trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "opening-movie-preload-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original Opening movie route selected state 0 and rejected missing source heap/cache ownership explicitly",
            run.stdout,
        )
        self.assertIn("no movie decode or retail-route claim", run.stdout)

    def test_trophy_baseline_waits_for_original_tydati_owner(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not targets or not (menu / "MnSlChr.usd").is_file() or not has_menu_trophy_assets(menu, game):
            self.skipTest("Build the native menu host and supply owned TyDatai fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="TyDatai profile baseline ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "trophy-baseline-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original TyDatai-backed save baseline initialized after source-file ownership",
            run.stdout)

    def test_original_sis_layout_and_style_stack(self):
        candidates = [ROOT / "build" / directory / "native_menu_scene_trace.js"
                      for directory in ("browser", "browser-release")]
        targets = [p for p in candidates if p.is_file()]
        if not targets:
            self.skipTest("Build fighter targets before the original SIS consumer check")
        target = max(targets, key=lambda p: p.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[:4000])
        self.assertIn("Original SIS big-endian layout and style-stack trace passed", run.stdout)
        for argument in ("--bad-image-index", "--bad-palette-index"):
            with self.subTest(argument=argument):
                rejected = subprocess.run([str(node_runtime()), str(target), argument],
                                          cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("Native texture animation index exceeds its owned table",
                              rejected.stdout + rejected.stderr)

if __name__ == "__main__": unittest.main()
