"""Focused contract checks for the isolated Release audio-preview profile."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("audio_preview_build", ROOT / "scripts" / "build.py")
BUILD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(BUILD)


class AudioPreviewBuildTests(unittest.TestCase):
    def test_profile_selects_distinct_release_directory_and_target(self):
        root = Path("/fixture/repo")
        self.assertEqual(
            BUILD.build_directory(root, target=BUILD.AUDIO_PREVIEW_RUNTIME_TARGET,
                                  configuration="Release"),
            root / "build/browser-audio-preview-release",
        )
        self.assertEqual(
            BUILD.BUILD_TARGETS[BUILD.AUDIO_PREVIEW_RUNTIME_TARGET],
            ("gameplay_audio_preview",),
        )

    def test_profile_rejects_non_release_and_private_instrumentation_before_source_work(self):
        for kwargs, message in (
            ({"configuration": "RelWithDebInfo"}, "Release-only"),
            ({"pipeline_provenance": True}, "private runtime"),
            ({"selective_pipelines": True}, "selective-pipelines"),
        ):
            with self.subTest(kwargs=kwargs), patch.object(BUILD, "read_lock") as read_lock:
                with self.assertRaisesRegex(ValueError, message):
                    BUILD.build(1, root=Path("/missing/repo"),
                                target=BUILD.AUDIO_PREVIEW_RUNTIME_TARGET, **kwargs)
                read_lock.assert_not_called()

    def test_graph_proof_requires_audio_sources_and_no_silent_definition(self):
        with tempfile.TemporaryDirectory(prefix="melee audio preview graph ") as directory:
            root = Path(directory)
            build_dir = root / "build/browser-audio-preview-release"
            build_dir.mkdir(parents=True)
            (build_dir / "build.ninja").write_text(
                "\n".join((
                    "build gameplay_audio_preview.js: link CMakeFiles/gameplay_audio_preview.dir/src/gameplay_menu_browser.cpp.o libfighter_asset_runtime_audio_preview.a",
                    "build libfighter_source_runtime_audio_preview.a: ar CMakeFiles/fighter_source_runtime_audio_preview.dir/src/gameplay_audio_resample.c.o CMakeFiles/fighter_source_runtime_audio_preview.dir/src/gameplay_audio_fx.c.o CMakeFiles/fighter_source_runtime_audio_preview.dir/src/gameplay_audio_stream.c.o",
                    "build libfighter_asset_runtime_audio_preview.a: ar CMakeFiles/fighter_asset_runtime_audio_preview.dir/src/gameplay_world.cpp.o libfighter_source_runtime_audio_preview.a",
                )) + "\n",
                encoding="utf-8",
            )
            commands = [
                {"file": "/src/gameplay_audio.c", "command": "emcc CMakeFiles/fighter_source_runtime_audio_preview.dir -DMELEE_WEB_PUBLIC_RUNTIME -DMELEE_WEB_AUDIO_PREVIEW_RUNTIME"},
                {"file": "/src/gameplay_audio_resample.c", "command": "emcc CMakeFiles/fighter_source_runtime_audio_preview.dir -DMELEE_WEB_PUBLIC_RUNTIME"},
            ]
            (build_dir / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
            proof = BUILD._audio_preview_graph_proof(root, build_dir)
            self.assertEqual(proof["schema"], "melee-web-audio-preview-graph-v1")
            self.assertEqual(proof["checks"]["public_audio_disabled_in_preview_compile_commands"], False)
            self.assertEqual(proof["checks"]["private_fighter_archive_in_preview_ninja_graph"], False)
            commands[0]["command"] += " -DMELEE_WEB_PUBLIC_AUDIO_DISABLED"
            (build_dir / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "silent audio definition"):
                BUILD._audio_preview_graph_proof(root, build_dir)


if __name__ == "__main__":
    unittest.main()
