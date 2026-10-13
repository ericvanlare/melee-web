"""Focused build-contract tests for the private match draw/discard diagnostic."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("match_draw_build", ROOT / "scripts/build.py")
BUILD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(BUILD)


class MatchDrawDiscardBuildTests(unittest.TestCase):
    def _configured_root(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee draw diagnostic build ")
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

    def _configure_only(self, root, target, configuration="Release"):
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(BUILD, "read_lock", return_value=lock), \
                patch.object(BUILD, "verify_sources"), \
                patch.object(BUILD, "prepare_sources", return_value=generated), \
                patch.object(BUILD.subprocess, "run") as run:
            BUILD.build(1, root=root, target=target, configuration=configuration,
                        configure_only=True)
        configure = next(call.args[0] for call in run.call_args_list
                         if "-S" in call.args[0])
        self.assertFalse(any("--build" in call.args[0] for call in run.call_args_list))
        return configure

    def test_private_target_configures_only_its_isolated_browser_runtime(self):
        root = self._configured_root()
        target = BUILD.MATCH_DRAW_DISCARD_DIAGNOSTIC_TARGET
        diagnostic = BUILD.build_directory(root, target=target, configuration="Release")
        self.assertEqual(BUILD.BUILD_TARGETS[target], ("gameplay_menu_browser",))
        self.assertEqual(diagnostic, root / "build/browser-match-draw-discard-release")
        self.assertEqual(
            BUILD.build_directory(root, target=target, configuration="RelWithDebInfo"),
            root / "build/browser-match-draw-discard",
        )
        for other in ("runtime", BUILD.PUBLIC_RUNTIME_TARGET,
                      BUILD.AUDIO_PREVIEW_RUNTIME_TARGET):
            self.assertNotEqual(
                diagnostic,
                BUILD.build_directory(root, target=other, configuration="Release"),
            )

        configure = self._configure_only(root, target)
        self.assertEqual(configure[configure.index("-B") + 1], str(diagnostic))
        self.assertIn("-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=ON", configure)
        self.assertIn("-DMELEE_WEB_PUBLIC_RUNTIME=OFF", configure)
        self.assertIn("-DMELEE_WEB_AUDIO_PREVIEW_RUNTIME=OFF", configure)

    def test_ordinary_runtime_explicitly_turns_diagnostic_back_off(self):
        root = self._configured_root()
        diagnostic = self._configure_only(
            root, BUILD.MATCH_DRAW_DISCARD_DIAGNOSTIC_TARGET
        )
        runtime = self._configure_only(root, "runtime")
        self.assertIn("-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=ON", diagnostic)
        self.assertIn("-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=OFF", runtime)
        self.assertEqual(
            runtime[runtime.index("-B") + 1],
            str(root / "build/browser-release"),
        )

    def test_public_and_audio_preview_configs_reject_private_diagnostic(self):
        cmake = ROOT / ".venv/bin/cmake"
        if not cmake.is_file():
            found = shutil.which("cmake")
            if found is None:
                self.skipTest("CMake is unavailable for the configure-only guard check")
            cmake = Path(found)

        cases = (
            ("public-only", "ON", "OFF"),
            ("audio-only", "OFF", "ON"),
            ("audio-preview", "ON", "ON"),
        )
        for name, public_runtime, audio_preview in cases:
            with self.subTest(profile=name), tempfile.TemporaryDirectory(
                    prefix="melee draw diagnostic rejected configure ") as directory:
                result = subprocess.run(
                    [str(cmake), "-S", str(ROOT), "-B", str(Path(directory) / "build"),
                     "-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=ON",
                     f"-DMELEE_WEB_PUBLIC_RUNTIME={public_runtime}",
                     f"-DMELEE_WEB_AUDIO_PREVIEW_RUNTIME={audio_preview}"],
                    text=True, capture_output=True, check=False,
                )
                output = result.stdout + result.stderr
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(
                    "Match draw/discard diagnostic is private and unavailable in public runtimes",
                    output,
                )
