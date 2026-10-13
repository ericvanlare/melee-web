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

    def test_direct_cmake_public_configuration_refuses_observer(self):
        cmake = fixture.ROOT / ".venv/bin/cmake"
        if not cmake.is_file():
            self.skipTest("Pinned CMake unavailable")
        with tempfile.TemporaryDirectory(prefix="frame-effect-rejected-") as directory:
            result = subprocess.run([
                str(cmake), "-S", str(fixture.ROOT), "-B", str(Path(directory) / "build"),
                "-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=ON",
                "-DMELEE_WEB_PUBLIC_RUNTIME=ON",
            ], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Match frame-effect observation requires its isolated private runtime",
                          result.stdout + result.stderr)
