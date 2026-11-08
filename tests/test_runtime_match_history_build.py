"""Pure build-graph controls for the opt-in ordinary-runtime history profile."""

import importlib.util
import re
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import call, patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("history_build", ROOT / "scripts/build.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class RuntimeMatchHistoryBuildTests(unittest.TestCase):
    def _configured_root(self):
        temporary = tempfile.TemporaryDirectory(prefix="match-history-build-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        bins = root / ".venv/bin"
        emscripten = root / ".deps/emsdk/upstream/emscripten"
        bins.mkdir(parents=True)
        emscripten.mkdir(parents=True)
        for path in (bins / "cmake", bins / "ninja", emscripten / "emcmake"):
            path.write_text("fixture\n", encoding="utf-8")
        (root / ".deps/emsdk/.emscripten").write_text("fixture\n", encoding="utf-8")
        (emscripten / "emscripten-version.txt").write_text("6.0.9\n", encoding="utf-8")
        return root

    def _run_mocked_build(self, root, target):
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        normal = root / "build/gameplay-source/src"
        profile = root / "build/gameplay-source-slippi-profile/src"
        history = target in {BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET, BUILD.ROLLBACK_STAGING_TARGET}
        with patch.object(BUILD, "read_lock", return_value=lock), \
                patch.object(BUILD, "verify_sources") as verify_sources, \
                patch.object(BUILD, "prepare_sources", return_value=profile if history else normal) as prepare_sources, \
                patch.object(BUILD, "record_build") as record_build, \
                patch.object(BUILD.subprocess, "run") as run:
            BUILD.build(2, root=root, target=target,
                        configuration="Release" if history else "RelWithDebInfo")
        return verify_sources, prepare_sources, record_build, run

    def test_target_is_private_release_only_and_uses_separate_directory(self):
        target = BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET
        self.assertEqual(BUILD.BUILD_TARGETS[target], (
            "gameplay_menu_browser", "gameplay_menu_browser_match_history_source_manifest"))
        self.assertEqual(BUILD.build_directory(root=Path("/repo"), target=target,
                                               configuration="Release"),
                         Path("/repo/build/browser-match-history-release"))
        with self.assertRaisesRegex(ValueError, "Release-only"):
            BUILD.build_directory(root=Path("/repo"), target=target,
                                  configuration="RelWithDebInfo")
        with self.assertRaisesRegex(ValueError, "fixed isolated private profile"):
            BUILD.build_directory(root=Path("/repo"), target=target,
                                  configuration="Release", stage_arena_owner_diagnostic=True)

    def test_history_config_selects_profile_overlay_v20_owners_and_target_manifest(self):
        root = self._configured_root()
        verify_sources, prepare_sources, record_build, run = self._run_mocked_build(
            root, BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET)
        lock = verify_sources.call_args.args[1]
        prepare_sources.assert_called_once_with(root, lock, slippi_profile=True)
        self.assertEqual(record_build.call_args_list, [
            call(root, root / "build/browser-match-history-release", False),
            call(root, root / "build/browser-match-history-release", True),
        ])
        configure_calls = [call.args[0] for call in run.call_args_list if "-S" in call.args[0]]
        self.assertEqual(len(configure_calls), 1)
        configure = configure_calls[0]
        for option in (
            "-DMELEE_WEB_MATCH_HISTORY_DIAGNOSTIC=ON",
            "-DMELEE_WEB_SLIPPI_SYNCTEST=OFF",
            "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=ON",
            "-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=ON",
            "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=ON",
            "-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=ON",
            "-DMELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC=ON",
            "-DMELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY=ON",
            "-DMELEE_WEB_STAGE_ARENA_OWNER_INVENTORY=OFF",
        ):
            self.assertIn(option, configure)
        build_calls = [call.args[0] for call in run.call_args_list if "--build" in call.args[0]]
        self.assertEqual(len(build_calls), 1)
        command = build_calls[0]
        self.assertEqual(
            command[command.index("--target") + 1:command.index("-j")],
            list(BUILD.BUILD_TARGETS[BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET]),
        )
        self.assertEqual(command[command.index("--build") + 1],
                         str(root / "build/browser-match-history-release"))

    def test_staging_uses_an_isolated_release_profile_and_typed_exports(self):
        root = self._configured_root()
        target = BUILD.ROLLBACK_STAGING_TARGET
        verify_sources, prepare_sources, record_build, run = self._run_mocked_build(root, target)
        prepare_sources.assert_called_once_with(root, verify_sources.call_args.args[1],
                                                slippi_profile=True)
        staging = root / "build/browser-rollback-staging-release"
        self.assertEqual(record_build.call_args_list, [call(root, staging, False),
                                                      call(root, staging, True)])
        configure = next(c.args[0] for c in run.call_args_list if "-S" in c.args[0])
        for key, value in (
            ("ROLLBACK_STAGING", "ON"), ("MATCH_HISTORY_DIAGNOSTIC", "OFF"),
            ("PUBLIC_RUNTIME", "OFF"), ("AUDIO_PREVIEW_RUNTIME", "OFF"),
            ("AURORA_QUIESCENCE_DIAGNOSTIC", "ON"), ("AURORA_STAGING_SHADOW_INVENTORY", "ON"),
            ("SLIPPI_LOGICAL_FRAME_CLOCK", "ON"), ("SLIPPI_LOBJ_PRESENTATION_CACHE", "ON"),
            ("SLIPPI_TOBJ_PRESENTATION_CACHE", "ON"), ("SLIPPI_PARTICLE_PRESENTATION_CACHE", "ON"),
        ):
            self.assertIn(f"-DMELEE_WEB_{key}={value}", configure)
        command = next(c.args[0] for c in run.call_args_list if "--build" in c.args[0])
        self.assertEqual(command[command.index("--target") + 1:command.index("-j")],
                         list(BUILD.BUILD_TARGETS[target]))
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        names = re.search(r"set\(_rollback_staging_exports\n(.*?)\n  \)", cmake, re.S)[1].split()
        self.assertEqual(len(names), len(set(names)))
        for suffix in ("cpu_endpoint_admit", "ticket_data", "terminal_tail_final_json",
                       "terminal_tail_checksum_data", "start_commit", "pacing_wait_begin"):
            self.assertIn(f"_melee_web_native_menu_match_history_{suffix}", names)
        self.assertIn("_melee_web_net_validate_pad", names)
        self.assertIn('set(gameplay_menu_browser_runtime_methods "ccall,FS,IDBFS,', cmake)
        for name in set(names) - {"_melee_web_net_validate_pad"}:
            self.assertNotRegex(name, r"cpu_(?:arena|particle)_endpoint|cpu_endpoint_(?:begin|record|data|bytes|end)|_motor_|_pcm_|menu_(?:memory|replay|stock_check|diagnostics)|_net_")
        self.assertNotIn("_melee_web_native_menu_snapshot_unlocked_baseline", names)

    def test_staging_rejects_diagnostic_options_before_source_work(self):
        target = BUILD.ROLLBACK_STAGING_TARGET
        for kwargs in (
            {"configuration": "RelWithDebInfo"},
            {"configuration": "Release", "pipeline_provenance": True},
            {"configuration": "Release", "trace_targets": ("gameplay_slippi_synctest",)},
        ):
            with self.subTest(kwargs=kwargs), patch.object(BUILD, "read_lock") as read_lock:
                with self.assertRaises(ValueError):
                    BUILD.build(1, root=Path("/missing"), target=target, **kwargs)
                read_lock.assert_not_called()

    def test_default_runtime_keeps_source_tree_and_profile_options_unchanged(self):
        root = self._configured_root()
        verify_sources, prepare_sources, _record_build, run = self._run_mocked_build(root, "runtime")
        lock = verify_sources.call_args.args[1]
        prepare_sources.assert_called_once_with(root, lock)
        configure = next(call.args[0] for call in run.call_args_list if "-S" in call.args[0])
        for option in (
            "-DMELEE_WEB_ROLLBACK_STAGING=OFF",
            "-DMELEE_WEB_MATCH_HISTORY_DIAGNOSTIC=OFF",
            "-DMELEE_WEB_SLIPPI_SYNCTEST=OFF",
            "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=OFF",
            "-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=OFF",
            "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=OFF",
            "-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=OFF",
            "-DMELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY=OFF",
        ):
            self.assertIn(option, configure)
        build_call = next(call.args[0] for call in run.call_args_list if "--build" in call.args[0])
        self.assertEqual(build_call[build_call.index("--target") + 1:build_call.index("-j")],
                         ["gameplay_menu_browser"])

    def test_invalid_history_profile_fails_before_dependency_or_source_work(self):
        target = BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET
        for kwargs, expected in (
            ({"configuration": "RelWithDebInfo"}, "Release-only"),
            ({"configuration": "Release", "stage_arena_owner_diagnostic": True},
             "fixed isolated private profile"),
            ({"configuration": "Release", "pipeline_provenance": True},
             "fixed isolated private profile"),
        ):
            with self.subTest(kwargs=kwargs), \
                    patch.object(BUILD, "read_lock") as read_lock, \
                    patch.object(BUILD, "prepare_sources") as prepare_sources:
                with self.assertRaisesRegex(ValueError, expected):
                    BUILD.build(1, root=Path("/missing"), target=target, **kwargs)
                read_lock.assert_not_called()
                prepare_sources.assert_not_called()

    def test_cli_exposes_only_the_named_opt_in_target(self):
        with patch.object(BUILD.sys, "argv", [
            "build.py", "--target", BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET,
            "--configuration", "Release", "--configure-only",
        ]), patch.object(BUILD, "build") as build:
            BUILD.main()
        self.assertEqual(build.call_args.kwargs["target"], BUILD.MATCH_HISTORY_DIAGNOSTIC_TARGET)
        self.assertEqual(build.call_args.kwargs["configuration"], "Release")
        self.assertTrue(build.call_args.kwargs["configure_only"])

    def test_cmake_splits_profile_providers_from_fixture_driver(self):
        top = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        runtime = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        self.assertIn(
            'option(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC\n'
            '  "Build the private Release ordinary-runtime source-history profile" OFF)', top)
        self.assertIn(
            "if(MELEE_WEB_SLIPPI_SYNCTEST OR MELEE_WEB_MATCH_HISTORY_CORE)", top)
        for provider in ("profile.c", "offscreen_damage.c", "dynamics_fix.c"):
            self.assertIn(provider, top)
        self.assertIn("if(MELEE_WEB_SLIPPI_PROFILE_SOURCE_ENABLED)", runtime)
        fixture = runtime.split("if(MELEE_WEB_SLIPPI_SYNCTEST)\n", 1)[1].split(
            "\nendif()\n\nadd_executable(gameplay_music_profile_trace", 1)[0]
        self.assertIn("src/slippi_profile/tests/synctest_browser.cpp", fixture)
        self.assertIn("gameplay_menu_browser_match_history.map", runtime)
        self.assertIn("gameplay_menu_browser_match_history.source-state.json", runtime)
        self.assertIn("gameplay_menu_browser_match_history_source_manifest", runtime)


if __name__ == "__main__":
    unittest.main()
