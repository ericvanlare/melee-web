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

def find_stadium_c1_manifest_names(value):
    if isinstance(value, dict):
        if isinstance(value.get("manifest_names"), list):
            return value["manifest_names"]
        for child in value.values():
            found = find_stadium_c1_manifest_names(child)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_stadium_c1_manifest_names(child)
            if found is not None:
                return found
    return None

def stadium_c1_selected_file_names():
    preparation = json.loads(
        (ROOT / "docs/evidence/pokemon-stadium-c1a-css-sss-preparation-v1.json")
        .read_text(encoding="utf-8"))
    names = find_stadium_c1_manifest_names(preparation)
    if not isinstance(names, list):
        raise AssertionError("C1 evidence has no selected-file manifest")
    return names

def missing_stadium_fixture_names(menu, game, required_names):
    return sorted(name for name in set(required_names)
                  if not (menu / name).is_file() and not (game / name).is_file())

def stadium_fixture_campaign_is_explicit():
    return (bool(os.environ.get("MELEE_MENU_FIXTURE_ROOT"))
            or os.environ.get("MELEE_REQUIRE_STADIUM_E8_FIXTURES") == "1")

class NativeMenuSourceTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-c1a-native-menu-")

    def test_vs_sudden_death_source_callbacks_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        command = [str(node_runtime()), str(target),
                   "--vs-sudden-death-source-control"]
        (self.scratch / "sudden-death-source-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "sudden-death-source.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "sudden-death-source.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "sudden-death-source.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "sudden-death-source.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original VS timeout/tie and Sudden Death-to-Results source callbacks passed",
            run.stdout,
        )

    def test_sudden_death_world_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-world-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_ordinary_nontied_timeout_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-ordinary-timeout-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"ordinary-timeout-{stage}.jsonl"),
                    revision, "ordinary-nontied-timeout-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"ordinary-timeout-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"ordinary-timeout-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sparse_source_pad_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sparse-source-pad-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"sparse-source-pad-{stage}.jsonl"),
                    revision, "sparse-source-pad-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"sparse-source-pad-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"sparse-source-pad-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sparse_actual_css_sss_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sparse-actual-css-sss-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"sparse-actual-css-sss-{stage}.jsonl"),
                    revision, "sparse-actual-css-sss-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"sparse-actual-css-sss-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"sparse-actual-css-sss-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_natural_timeout_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-natural-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"natural-sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-natural-timeout-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"natural-sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"natural-sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_natural_resolution_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-resolution-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"resolution-sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-natural-resolution-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"resolution-sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"resolution-sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_returned_menu_reducer_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-returned-menu-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for recipe in ("returned-menu-results-control-v1",
                       "returned-menu-two-human-results-input-control-v1",
                       "returned-menu-two-human-timeout-results-control-v1"):
            for stage, expected in (
                    (32, "Missing owned menu host fixture: MnSlChr.usd"),
                    (31, "Explicit FD recipes require Final Destination")):
                with self.subTest(stage=stage, recipe=recipe):
                    trace = self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.jsonl"
                    command = [str(node_runtime()), str(target), str(absent),
                               str(absent), str(stage), str(trace), revision,
                               recipe]
                    run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                         text=True, timeout=30)
                    (self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.stdout").write_text(
                        run.stdout, encoding="utf-8")
                    (self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.stderr").write_text(
                        run.stderr, encoding="utf-8")
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(expected, run.stderr)
                    self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_resolution_manifest_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        run = subprocess.run([str(node_runtime()), str(target),
                              "--sd-resolution-fixture-manifest"], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        records = [json.loads(line) for line in run.stdout.splitlines()
                   if line.startswith('{')]
        self.assertEqual(len(records), 1)
        names = records[0]["required"]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({"PlMrNr.dat", "PlMrYe.dat", "GrNLa.dat",
                         "GmRst.usd", "GmRstMMr.dat", "ff_mario.hps",
                         "IfPrize.usd", "SdPrize.usd", "s_info1.hps"}.issubset(names))
    def test_sudden_death_returned_menu_manifest_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        run = subprocess.run([str(node_runtime()), str(target),
                              "--returned-menu-fixture-manifest"], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        records = [json.loads(line) for line in run.stdout.splitlines()
                   if line.startswith('{')]
        self.assertEqual(len(records), 1)
        names = records[0]["required"]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({"PlMrNr.dat", "PlMrYe.dat",
                         "GmRst.usd", "GmRstMMr.dat", "ff_mario.hps",
                         "IfPrize.usd", "SdPrize.usd", "s_info1.hps"}.issubset(names))
        self.assertNotIn("GrNLa.dat", names)

    def test_sudden_death_menu_setup_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sd-menu-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage),
                           str(self.scratch / f"sd-menu-dispatch-{stage}.jsonl"),
                           revision, "sudden-death-menu-setup-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"sd-menu-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"sd-menu-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_stadium_yakumono_exchange_round_trip_without_assets(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The C1 diagnostic native host trace has not been built")
        command = [str(node_runtime()), str(target),
                   "--stadium-yakumono-exchange"]
        (self.scratch / "yakumono-exchange-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "yakumono-exchange.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "yakumono-exchange.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "yakumono-exchange.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "yakumono-exchange.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Stadium yakumono exchange asset-free control passed; pointer restored",
            run.stdout,
        )
        self.assertIn("source StageInfo/GObj owner state unchanged", run.stdout)

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
        selected = stadium_c1_selected_file_names()
        self.assertEqual(len(required), 76)
        self.assertEqual(len(set(required)), 76)
        self.assertEqual(len(selected), 36)
        self.assertEqual(len(set(selected)), 36)
        required_union = sorted(set(required) | set(selected))
        self.assertEqual(len(required_union), 98)
        missing = missing_stadium_fixture_names(menu, game, required_union)
        if missing:
            detail = ", ".join(missing[:5])
            message = (
                "C1 context preflight requires the exact 98-file menu/selected union"
                f"; missing {detail}"
            )
            if stadium_fixture_campaign_is_explicit():
                self.fail(message)
            self.skipTest(message)
        if not target.is_file():
            self.skipTest("C1 context preflight requires its built host trace")
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

    def test_stadium_c1_item_state_owner_preflight(self):
        self.run_stadium_owner_preflight(
            "stadium-c1-item-state-preflight-v1", "c1-item-state-preflight",
            "C1 reopened-context lifecycle and item-state-owner preflight passed; no E8 request")

    def test_stadium_screen_roots_owner_preflight(self):
        self.run_stadium_owner_preflight(
            "stadium-screen-roots-preflight-v1", "screen-roots-preflight",
            "C1 screen-root preflight preserved canonical IMAGE, writable SIS and two owner/catalog lifetimes; no stage entry or ticks")

    def run_stadium_owner_preflight(self, recipe, prefix, success_message):
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
        selected = stadium_c1_selected_file_names()
        required_union = sorted(set(required) | set(selected))
        self.assertEqual(len(required), 76)
        self.assertEqual(len(selected), 36)
        self.assertEqual(len(required_union), 98)
        self.assertIn("ItCo.usd", required_union)
        self.assertIn("GrPs.usd", required_union)
        missing = missing_stadium_fixture_names(menu, game, required_union)
        if missing:
            detail = ", ".join(missing[:5])
            message = (
                "Stadium owner preflight requires the retained 98-file union"
                f"; missing {detail}"
            )
            if stadium_fixture_campaign_is_explicit():
                self.fail(message)
            self.skipTest(message)
        if not target.is_file():
            self.skipTest("Stadium owner preflight requires its built host trace")
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / (prefix + ".jsonl")
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision,
                   recipe]
        (self.scratch / (prefix + "-command.txt")).write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / (prefix + ".stdout")).write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / (prefix + ".stderr")).write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / (prefix + ".stdout")).write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / (prefix + ".stderr")).write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            success_message,
            run.stdout,
        )
        self.assertIn(
            "stage publication, or source menu entry",
            run.stdout,
        )

        if recipe == "stadium-screen-roots-preflight-v1":
            self.assertIn(
                "C1 live Stadium IMAGE source hit/miss/remove passed twice; no stage entry or ticks",
                run.stdout,
            )
            self.assertEqual(len(trace.read_text(encoding="utf-8").splitlines()), 1,
                             "Screen root probe must retain a header-only trace")

    def test_stadium_c1_fixture_preflight_detects_missing_selected_file(self):
        target = self.scratch / "synthetic-c1-fixture-preflight"
        menu, game = target / "native-menus", target / "next-gate"
        menu.mkdir(parents=True)
        game.mkdir()
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        self.assertIn("PlCo.dat", selected_names)
        required_union = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(selected_names), 36)
        self.assertEqual(len(required_union), 98)
        for name in required_union:
            if name == "PlCo.dat":
                continue
            root = menu if name in menu_names else game
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.assertEqual(
            missing_stadium_fixture_names(menu, game, required_union),
            ["PlCo.dat"],
        )

    def test_stadium_e8_one_request_and_checked_teardown(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        if not fixture_value:
            if os.environ.get("MELEE_REQUIRE_STADIUM_E8_FIXTURES") == "1":
                self.fail("MELEE_MENU_FIXTURE_ROOT is required for the frozen E8 packet")
            self.skipTest("The reviewed C1 fixture root is required for the E8 trace")
        fixture_root = Path(fixture_value)
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        self.assertTrue(target.is_file(), f"Build the diagnostic target first: {target}")
        self.assertTrue(menu.is_dir(), f"Missing owned menu fixture root: {menu}")
        self.assertTrue(game.is_dir(), f"Missing owned game fixture root: {game}")

        script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(set(menu_names)), len(menu_names))
        preparation = json.loads(
            (ROOT / "docs/evidence/pokemon-stadium-c1a-css-sss-preparation-v1.json")
            .read_text(encoding="utf-8"))
        self.assertEqual(
            preparation.get("schema"),
            "melee-web-pokemon-stadium-c1a-css-sss-preparation-v1",
        )

        selected_names = stadium_c1_selected_file_names()
        self.assertIsNotNone(selected_names, "C1 evidence has no selected-file manifest")
        self.assertEqual(len(selected_names), 36)
        self.assertEqual(len(set(selected_names)), len(selected_names))
        self.assertEqual(
            set(selected_names) & {
                "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat",
                "GrPs4.dat", "pstadium.ssm", "pstadium.hps", "pokesta.hps",
            },
            {
                "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat",
                "GrPs4.dat", "pstadium.ssm", "pstadium.hps", "pokesta.hps",
            },
        )
        required = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(required), 98)
        missing = [name for name in required
                   if not (menu / name).is_file() and not (game / name).is_file()]
        self.assertFalse(
            missing,
            "Frozen C1 selected RuntimeFiles are incomplete before launch: " +
            ", ".join(missing),
        )

        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-e8-request.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-e8-request-v1"]
        (self.scratch / "stadium-e8-request-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        (self.scratch / "stadium-e8-fixture-preflight.json").write_text(
            json.dumps({"menu_names": menu_names,
                        "selected_names": selected_names,
                        "missing": missing}, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "stadium-e8-request.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "stadium-e8-request.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "stadium-e8-request.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "stadium-e8-request.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-6000:])
        self.assertIn(
            "C1 reopened-context lifecycle preflight and one E8 typed request passed",
            run.stdout,
        )
        self.assertIn(
            "ordinary match admission and gameplay entry remain closed",
            run.stdout,
        )
        result = next(json.loads(line) for line in run.stdout.splitlines()
                      if line.startswith('{"probe":"stadium-e8-request"'))
        self.assertEqual(result["source_size_name"], "/GrPs.usd")
        self.assertEqual(result["typed_open_name"], "/GrPs.usd")
        self.assertEqual(result["stage_info_x6E4"][0], -1)
        self.assertIn("stage_info_xA0_observed_only", result)
        self.assertEqual(result["itemdata_public_calls"], 0)
        self.assertEqual(result["map_plit_public_calls"], 0)
        self.assertFalse(result["stage_objects_started"])
        self.assertTrue(result["checked_teardown"])
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        self.assertEqual(rows[0]["record"], "header")
        self.assertEqual(rows[0]["input_recipe"], "stadium-e8-request-v1")
        events = [row for row in rows if row.get("record") == "event"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "stadium_e8_request_returned")
        self.assertEqual(events[0]["selection"]["rules"]["stage_kind"], 3)

    def test_stadium_ground_map1_owner_lifetime(self):
        if os.environ.get("MELEE_RUN_STADIUM_GROUND_MAP1_OWNER") != "1":
            self.skipTest(
                "The single retained map1 constructor experiment requires its explicit run gate"
            )
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        if not fixture_value:
            self.fail("MELEE_MENU_FIXTURE_ROOT is required for the retained map1 packet")
        fixture_root = Path(fixture_value)
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        self.assertTrue(target.is_file(), f"Build the reviewed diagnostic target first: {target}")
        self.assertTrue(menu.is_dir(), f"Missing owned menu fixture root: {menu}")
        self.assertTrue(game.is_dir(), f"Missing owned game fixture root: {game}")

        script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(selected_names), 36)
        required = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(required), 98)
        missing = [name for name in required
                   if not (menu / name).is_file() and not (game / name).is_file()]
        self.assertFalse(
            missing,
            "Frozen C1 map1 RuntimeFiles are incomplete before launch: " +
            ", ".join(missing),
        )

        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-ground-map1-owner.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-ground-map1-owner-v1"]
        (self.scratch / "stadium-ground-map1-owner-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        (self.scratch / "stadium-ground-map1-owner-fixture-preflight.json").write_text(
            json.dumps({"menu_names": menu_names,
                        "selected_names": selected_names,
                        "missing": missing}, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "stadium-ground-map1-owner.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "stadium-ground-map1-owner.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "stadium-ground-map1-owner.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "stadium-ground-map1-owner.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-7000:])
        self.assertIn(
            "C1a raw PAD CSS->SSS selection, one E8 typed request, and one "
            "Ground map1 constructor/removal passed",
            run.stdout,
        )
        e8_result = next(json.loads(line) for line in run.stdout.splitlines()
                         if line.startswith('{"probe":"stadium-e8-request"'))
        self.assertTrue(e8_result["stage_objects_started"])
        self.assertTrue(e8_result["checked_teardown"])
        self.assertEqual(e8_result["source_size_name"], "/GrPs.usd")
        self.assertEqual(e8_result["typed_open_name"], "/GrPs.usd")
        self.assertEqual(e8_result["itemdata_public_calls"], 0)
        self.assertEqual(e8_result["map_plit_public_calls"], 0)
        owner_result = next(json.loads(line) for line in run.stdout.splitlines()
                            if line.startswith('{"probe":"stadium-ground-map1-owner"'))
        self.assertEqual(owner_result["map_id"], 1)
        self.assertEqual(owner_result["requested_bytes"], 64)
        self.assertGreater(owner_result["source_heap"], -1)
        self.assertGreater(owner_result["world_generation"], 0)
        self.assertGreater(owner_result["allocation_generation"], 0)
        self.assertEqual(owner_result["authored_marker_pair_count"], 20)
        self.assertEqual(owner_result["map1_matched_marker_pair_count"], 0)
        self.assertTrue(owner_result["stage_info_marker_baseline_empty"])
        self.assertTrue(owner_result["device_bytes_restored"])
        self.assertTrue(owner_result["buffer_retired"])
        self.assertFalse(owner_result["callback_dispatch"])
        self.assertEqual(owner_result["proc_ticks"], 0)
        self.assertFalse(owner_result["rendered"])
        self.assertTrue(owner_result["single_constructor_removal"])
        owner_ids = owner_result["ft_device_owner_ids"]
        self.assertEqual(len(owner_ids), 6)
        self.assertEqual(len(set(owner_ids)), 6)

        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        self.assertEqual(rows[0]["record"], "header")
        self.assertEqual(rows[0]["input_recipe"], "stadium-ground-map1-owner-v1")
        events = [row for row in rows if row.get("record") == "event"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "stadium_e8_request_returned")
        self.assertEqual(events[0]["selection"]["rules"]["stage_kind"], 3)

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
