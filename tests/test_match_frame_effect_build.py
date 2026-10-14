"""Private observer builds cannot contaminate ordinary runtime artifacts."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import test_match_draw_discard_build as fixture


class MatchFrameEffectBuildTests(unittest.TestCase):
    def test_isolated_release_and_explicit_disable_on_ordinary_configure(self):
        helper = fixture.MatchDrawDiscardBuildTests()
        self.addCleanup(helper.doCleanups)
        root = helper._configured_root()
        build = fixture.BUILD
        target = build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET
        diagnostic = helper._configure_only(root, target)
        ordinary = helper._configure_only(root, "runtime")
        self.assertEqual(diagnostic[diagnostic.index("-B") + 1],
                         str(root / "build/browser-match-frame-effect-release"))
        self.assertNotEqual(diagnostic[diagnostic.index("-B") + 1],
                            ordinary[ordinary.index("-B") + 1])
        self.assertIn("-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=ON", diagnostic)
        self.assertIn("-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=OFF", diagnostic)
        self.assertIn("-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=OFF", ordinary)

    def test_mixed_profiles_and_nonrelease_refuse_before_source_preparation(self):
        build = fixture.BUILD
        for options in ({"configuration": "RelWithDebInfo"},
                        {"pipeline_provenance": True},
                        {"selective_pipelines": True},
                        {"stadium_c1a_diagnostic": True},
                        {"fma_collision_diagnostic": True}):
            with self.subTest(options=options), patch.object(build, "prepare_sources") as prepare:
                arguments = {"configuration": "Release", **options}
                with self.assertRaises(ValueError):
                    build.build(1, target=build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET,
                                **arguments)
                prepare.assert_not_called()


    def _configure_first_pair_only(self, root, enabled):
        build = fixture.BUILD
        generated = root / "build/gameplay-source/src"
        with patch.object(build, "read_lock", return_value={"repositories": {}, "emscripten": "6.0.9"}), \
                patch.object(build, "verify_sources"), \
                patch.object(build, "prepare_sources", return_value=generated), \
                patch.object(build.subprocess, "run") as run:
            build.build(1, root=root, target=build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET,
                        configuration="Release", configure_only=True,
                        first_pair_audio_file_diagnostic=enabled)
        configure = next(call.args[0] for call in run.call_args_list
                         if "-S" in call.args[0])
        self.assertFalse(any("--build" in call.args[0] for call in run.call_args_list))
        return configure

    def test_first_pair_profile_has_independent_release_directory_and_explicit_off(self):
        helper = fixture.MatchDrawDiscardBuildTests()
        self.addCleanup(helper.doCleanups)
        root = helper._configured_root()
        build = fixture.BUILD
        first_pair = self._configure_first_pair_only(root, True)
        frame_effect = helper._configure_only(root, build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET)
        ordinary = helper._configure_only(root, "runtime")
        self.assertEqual(first_pair[first_pair.index("-B") + 1],
                         str(root / "build/browser-first-pair-audio-file-release"))
        self.assertNotEqual(first_pair[first_pair.index("-B") + 1],
                            frame_effect[frame_effect.index("-B") + 1])
        self.assertIn("-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=ON", first_pair)
        self.assertIn("-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=ON", first_pair)
        self.assertIn("-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=OFF", frame_effect)
        self.assertIn("-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=OFF", ordinary)

    def test_first_pair_profile_refuses_escape_before_source_preparation(self):
        build = fixture.BUILD
        cases = (
            ("runtime", "Release", {}),
            (build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET, "RelWithDebInfo", {}),
            (build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET, "Release",
             {"selective_pipelines": True}),
            (build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET, "Release",
             {"fma_collision_diagnostic": True}),
        )
        for target, configuration, extras in cases:
            with self.subTest(target=target, configuration=configuration, extras=extras), \
                    patch.object(build, "prepare_sources") as prepare:
                with self.assertRaises(ValueError):
                    build.build(1, target=target, configuration=configuration,
                                first_pair_audio_file_diagnostic=True, **extras)
                prepare.assert_not_called()

    def test_direct_cmake_public_configuration_refuses_observer(self):
        cmake = fixture.ROOT / ".venv/bin/cmake"
        if not cmake.is_file():
            self.skipTest("Pinned CMake unavailable")
        cases = (
            ({"MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC": "ON"},
             "First-pair audio/file prerequisite requires isolated frame-effect profile"),
            ({"MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC": "ON",
              "MELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC": "ON",
              "MELEE_WEB_PUBLIC_RUNTIME": "ON"},
             "Match frame-effect observation requires its isolated private runtime"),
        )
        for options, expected in cases:
            with self.subTest(options=options), tempfile.TemporaryDirectory(
                    prefix="first-pair-profile-rejected-") as directory:
                result = subprocess.run([
                    str(cmake), "-S", str(fixture.ROOT), "-B", str(Path(directory) / "build"),
                    *[f"-D{name}={value}" for name, value in options.items()],
                ], capture_output=True, text=True, timeout=60)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stdout + result.stderr)
