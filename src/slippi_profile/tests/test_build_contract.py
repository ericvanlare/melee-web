# SPDX-License-Identifier: GPL-3.0-or-later
"""Build-isolation controls for the private Slippi synctest entry point."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import build  # noqa: E402


class SlippiSynctestBuildContractTests(unittest.TestCase):
    def test_trace_uses_a_distinct_build_directory(self):
        ordinary = build.build_directory(ROOT, target="gameplay_snapshot_probe")
        profile = build.build_directory(ROOT, target="gameplay_slippi_synctest")
        self.assertEqual(profile, ROOT / "build/slippi-synctest")
        self.assertNotEqual(profile, ordinary)

    def test_trace_is_registered_and_private_options_are_rejected(self):
        self.assertIn("gameplay_slippi_synctest", build.TRACE_TARGETS)
        with self.assertRaisesRegex(ValueError, "isolated private build profile"):
            build.build_directory(ROOT, target="gameplay_slippi_synctest",
                                  pipeline_provenance=True)
        with self.assertRaisesRegex(ValueError, "isolated private build profile"):
            build.build_directory(ROOT, target="gameplay_slippi_synctest",
                                  selective_pipelines=True)

    def test_profile_wiring_is_explicitly_opt_in_and_separate(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        fighter_cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text()
        self.assertIn('option(MELEE_WEB_SLIPPI_SYNCTEST', cmake)
        self.assertIn('if(MELEE_WEB_SLIPPI_SYNCTEST)', cmake)
        self.assertIn('add_custom_target(gameplay_slippi_synctest DEPENDS', fighter_cmake)
        self.assertIn('MELEE_WEB_SLIPPI_PROFILE=1', cmake)
        self.assertIn('gameplay-source-slippi-profile',
                      (ROOT / "scripts/gameplay_sources.py").read_text())

    def test_mixed_trace_rejects_before_source_preparation(self):
        with mock.patch.object(build, "prepare_sources") as prepare:
            with self.assertRaisesRegex(ValueError, "must be the only --trace-target"):
                build.build(1, root=ROOT, trace_targets=(
                    "gameplay_slippi_synctest", "gameplay_snapshot_probe"))
        prepare.assert_not_called()

    def test_isolated_configure_explicitly_selects_profile_sources_and_assets(self):
        with tempfile.TemporaryDirectory(prefix="slippi-configure-control-") as temporary:
            root = Path(temporary)
            bins = root / ".venv/bin"
            emscripten = root / ".deps/emsdk/upstream/emscripten"
            assets = root / "assets"
            for directory in (bins, emscripten, assets):
                directory.mkdir(parents=True)
            for tool in (bins / "cmake", bins / "ninja", emscripten / "emcmake"):
                tool.write_text("fixture\n")
            (root / ".deps/emsdk/.emscripten").write_text("fixture\n")
            (emscripten / "emscripten-version.txt").write_text("6.0.9\n")
            lock = {"repositories": {}, "emscripten": "6.0.9"}
            generated = root / "build/gameplay-source-slippi-profile/src"
            with mock.patch.object(build, "read_lock", return_value=lock), \
                    mock.patch.object(build, "verify_sources"), \
                    mock.patch.object(build, "prepare_sources", return_value=generated) as prepare, \
                    mock.patch.object(build.subprocess, "run") as run:
                build.build(1, root=root, configure_only=True,
                            trace_targets=("gameplay_slippi_synctest",),
                            slippi_synctest_assets=assets)
            prepare.assert_called_once_with(root, lock, slippi_profile=True)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertFalse(any("--build" in command for command in commands))
            configure = next(command for command in commands if "-DMELEE_WEB_SLIPPI_SYNCTEST=ON" in command)
            self.assertIn(f"-DMELEE_WEB_SLIPPI_SYNCTEST_ASSET_ROOT={assets.resolve()}", configure)
            self.assertIn(f"-DMELEE_WEB_GAMEPLAY_SOURCE_DIR={generated}", configure)
            self.assertEqual(configure[configure.index("-B") + 1], str(root / "build/slippi-synctest"))


if __name__ == "__main__":
    unittest.main()
