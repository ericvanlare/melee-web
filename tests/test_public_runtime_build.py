"""Focused checks for the bounded Release public runtime packaging target."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("public_build", ROOT / "scripts/build.py")
public_build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(public_build)


def _uleb(value):
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        encoded.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(encoded)


def _wasm_with_exports(names):
    payload = bytearray(_uleb(len(names)))
    for index, name in enumerate(names):
        encoded = name.encode("utf-8")
        payload.extend(_uleb(len(encoded)))
        payload.extend(encoded)
        payload.extend((0,))  # function export
        payload.extend(_uleb(index))
    return b"\0asm\1\0\0\0" + b"\7" + _uleb(len(payload)) + payload


class PublicRuntimeBuildTests(unittest.TestCase):
    def _configured_build_root(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee build configure ")
        root = Path(temporary.name)
        bins = root / ".venv/bin"
        emscripten = root / ".deps/emsdk/upstream/emscripten"
        bins.mkdir(parents=True)
        emscripten.mkdir(parents=True)
        for path in (bins / "cmake", bins / "ninja", emscripten / "emcmake"):
            path.write_text("fixture\n", encoding="utf-8")
        (root / ".deps/emsdk/.emscripten").write_text("fixture\n", encoding="utf-8")
        (emscripten / "emscripten-version.txt").write_text("6.0.9\n", encoding="utf-8")
        self.addCleanup(temporary.cleanup)
        return root

    def test_build_target_map_preserves_reviewed_target_closures(self):
        self.assertEqual(public_build.BUILD_TARGETS["graphics"], ("gx_probe",))
        self.assertEqual(public_build.BUILD_TARGETS["gameplay"], ("gameplay_checks",))
        self.assertEqual(public_build.BUILD_TARGETS["runtime"], ("gameplay_menu_browser",))
        self.assertEqual(
            public_build.BUILD_TARGETS["fighter"],
            (
                "fighter_runtime_probe",
                "gameplay_effect_banks_trace",
                "gameplay_bonus_data_trace",
                "gameplay_stage_numeric_trace",
                "native_menu_scene_trace",
                "dat_menu_support_trace",
            ),
        )
        self.assertEqual(
            public_build.BUILD_TARGETS["all"],
            ("gx_probe", "gameplay_checks", "gameplay_menu_browser"),
        )

    def test_configure_only_runs_validation_and_configure_without_build(self):
        root = self._configured_build_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(public_build, "read_lock", return_value=lock) as read_lock, \
                patch.object(public_build, "verify_sources") as verify_sources, \
                patch.object(public_build, "prepare_sources", return_value=generated) as prepare_sources, \
                patch.object(public_build.subprocess, "run") as run:
            public_build.build(2, root=root, target="fighter", configure_only=True)

        read_lock.assert_called_once_with(root)
        verify_sources.assert_called_once_with(root, lock)
        prepare_sources.assert_called_once_with(root, lock)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(sum("--build" in command for command in commands), 0)
        self.assertTrue(any("-DMELEE_WEB_GAMEPLAY_SOURCE_DIR=" in arg
                            for command in commands for arg in command))

    def test_normal_build_uses_the_shared_target_map(self):
        root = self._configured_build_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(public_build, "read_lock", return_value=lock), \
                patch.object(public_build, "verify_sources"), \
                patch.object(public_build, "prepare_sources", return_value=generated), \
                patch.object(public_build.subprocess, "run") as run:
            public_build.build(2, root=root, target="fighter")

        build_commands = [call.args[0] for call in run.call_args_list if "--build" in call.args[0]]
        self.assertEqual(len(build_commands), 1)
        self.assertEqual(
            build_commands[0][build_commands[0].index("--target") + 1:build_commands[0].index("-j")],
            list(public_build.BUILD_TARGETS["fighter"]),
        )

    def test_link_jobs_forwards_a_cmake_link_pool(self):
        root = self._configured_build_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(public_build, "read_lock", return_value=lock), \
                patch.object(public_build, "verify_sources"), \
                patch.object(public_build, "prepare_sources", return_value=generated), \
                patch.object(public_build.subprocess, "run") as run:
            public_build.build(2, root=root, target="fighter", link_jobs=1)

        configure_commands = [call.args[0] for call in run.call_args_list if "-G" in call.args[0]]
        self.assertEqual(len(configure_commands), 1)
        self.assertIn("-DCMAKE_JOB_POOLS=melee_link=1", configure_commands[0])
        self.assertIn("-DCMAKE_JOB_POOL_LINK=melee_link", configure_commands[0])

    def test_default_configure_has_no_link_pool_flags(self):
        root = self._configured_build_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(public_build, "read_lock", return_value=lock), \
                patch.object(public_build, "verify_sources"), \
                patch.object(public_build, "prepare_sources", return_value=generated), \
                patch.object(public_build.subprocess, "run") as run:
            public_build.build(2, root=root, target="fighter", configure_only=True)

        configure_commands = [call.args[0] for call in run.call_args_list if "-G" in call.args[0]]
        self.assertEqual(len(configure_commands), 1)
        self.assertFalse(any("CMAKE_JOB_POOL" in arg for arg in configure_commands[0]))

    def test_link_jobs_rejects_nonpositive_before_source_work(self):
        with patch.object(public_build, "read_lock") as read_lock:
            with self.assertRaisesRegex(ValueError, "link-jobs must be positive"):
                public_build.build(1, root=Path("missing-source"), link_jobs=0)
        read_lock.assert_not_called()

    def test_configure_only_rejects_public_runtime_before_source_work(self):
        with patch.object(public_build, "read_lock") as read_lock:
            with self.assertRaisesRegex(ValueError, "configure-only.*runtime-public"):
                public_build.build(1, root=Path("missing-source"), target="runtime-public",
                                   configuration="Release", configure_only=True)
        read_lock.assert_not_called()

    def test_selective_profile_rejects_unrelated_targets_and_recorder(self):
        for target, recorder in (("graphics", False), ("all", False), ("runtime", True)):
            with self.subTest(target=target, recorder=recorder), self.assertRaisesRegex(ValueError, "selective-pipelines"):
                public_build.build(1, root=Path("missing-source"), target=target,
                                   pipeline_provenance=recorder, selective_pipelines=True)

    def test_release_target_has_a_small_explicit_api(self):
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        public = cmake.split("# The public player", 1)[1].split(
            "# Shared typed scene/model tables", 1
        )[0]
        self.assertIn("if(CMAKE_BUILD_TYPE STREQUAL \"Release\" AND MELEE_WEB_PUBLIC_RUNTIME)", public)
        self.assertIn("add_custom_target(runtime-public DEPENDS gameplay_public)", public)
        self.assertIn("MELEE_WEB_PUBLIC_AUDIO_DISABLED", public)
        self.assertIn("fighter_asset_runtime_public", public)
        self.assertNotIn("gameplay_audio_resample.c", public)
        self.assertEqual(public_build.PUBLIC_RUNTIME_BUILD_DIR, "build/browser-public-release")
        self.assertNotIn("--profiling-funcs", public)
        self.assertIn("_melee_web_native_menu_message", public)
        self.assertIn("_melee_web_input_set_keyboard_layout", public)
        for forbidden in public_build.PUBLIC_RUNTIME_FORBIDDEN_EXPORTS:
            self.assertNotIn(forbidden, public)

    def test_development_target_keeps_instrumentation(self):
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        development = cmake.split("add_executable(gameplay_menu_browser", 1)[1].split(
            "# The public player", 1
        )[0]
        self.assertIn("--profiling-funcs", development)
        self.assertIn("_melee_web_native_menu_replay", development)
        self.assertIn("_melee_web_native_menu_diagnostics", development)

    def test_build_cli_rejects_non_release_public_runtime(self):
        with self.assertRaisesRegex(ValueError, "Release-only"):
            public_build.build(1, root=ROOT, target="runtime-public", configuration="RelWithDebInfo")

    def test_private_provenance_is_rejected_before_public_build_work(self):
        with self.assertRaisesRegex(ValueError, "requires the private runtime target"):
            public_build.build(1, root=Path("missing-source"), target="runtime-public",
                               configuration="Release", pipeline_provenance=True)

    def test_provenance_covers_reviewed_patch_and_build_pipeline(self):
        required = {
            "patches/melee-gameplay.patch",
            "patches/aurora-browser.patch",
            "scripts/bootstrap.py",
            "scripts/build.py",
            "scripts/gameplay_bool.py",
            "scripts/gameplay_sources.py",
            "scripts/generate_common_schema.py",
            "scripts/generate_fighter_registry.py",
            "scripts/materialize_pipeline_cache.py",
        }
        self.assertTrue(required.issubset(public_build.PUBLIC_RUNTIME_SOURCE_FILES))

    def test_wasm_export_reader_reports_actual_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            wasm = Path(directory) / "runtime.wasm"
            wasm.write_bytes(_wasm_with_exports(["_main", "_malloc", "memory_probe"]))
            exports = public_build._wasm_exports(wasm)
        self.assertEqual(
            [item["name"] for item in exports],
            ["_main", "_malloc", "memory_probe"],
        )
        self.assertTrue(all(item["kind"] == 0 for item in exports))


if __name__ == "__main__":
    unittest.main()
