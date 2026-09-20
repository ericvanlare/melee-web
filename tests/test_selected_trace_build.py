"""Focused contract tests for the reviewed lifecycle-trace build mode."""

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("selected_trace_build", ROOT / "scripts/build.py")
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class SelectedTraceBuildTests(unittest.TestCase):
    def _configured_root(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee selected trace build ")
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

    def test_build_directory_reuses_private_and_existing_graph_conventions(self):
        root = Path("/fixture/repo")
        self.assertEqual(
            BUILD.build_directory(root, target="all", configuration="RelWithDebInfo"),
            root / "build/browser",
        )
        self.assertEqual(
            BUILD.build_directory(root, target="graphics", configuration="Release"),
            root / "build/browser-release",
        )
        self.assertEqual(
            BUILD.build_directory(root, target=BUILD.PUBLIC_RUNTIME_TARGET,
                                  configuration="Release", selective_pipelines=True),
            root / "build/browser-public-selective-release",
        )
        self.assertEqual(
            BUILD.build_directory(root, target="gameplay_content_match_trace",
                                  configuration="Release"),
            root / "build/browser-release",
        )

    def test_repeated_trace_targets_build_only_reviewed_targets_with_pinned_environment(self):
        root = self._configured_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        traces = ("gameplay_content_match_trace", "gameplay_stage_battlefield_trace",
                  "gameplay_stage_temple_trace", "gameplay_stage_fountain_trace")
        with patch.object(BUILD, "read_lock", return_value=lock), \
                patch.object(BUILD, "verify_sources"), \
                patch.object(BUILD, "prepare_sources", return_value=generated), \
                patch.object(BUILD.subprocess, "run") as run:
            BUILD.build(3, root=root, configuration="Release", trace_targets=traces)

        build_calls = [call for call in run.call_args_list if "--build" in call.args[0]]
        self.assertEqual(len(build_calls), 1)
        command = build_calls[0].args[0]
        self.assertEqual(command[command.index("--target") + 1:command.index("-j")], list(traces))
        self.assertEqual(command[command.index("--build") + 1],
                         str(root / "build/browser-release"))
        environment = build_calls[0].kwargs["env"]
        self.assertEqual(environment["EMSDK"], str(root / ".deps/emsdk"))
        self.assertEqual(environment["EM_CONFIG"], str(root / ".deps/emsdk/.emscripten"))
        self.assertEqual(environment["EM_CACHE"],
                         str(root / ".deps/emsdk/upstream/emscripten/cache"))
        self.assertEqual(environment["EMSDK_PYTHON"], sys.executable)
        self.assertTrue(environment["PATH"].startswith(str(root / ".venv/bin") + os.pathsep))

        configure_calls = [call for call in run.call_args_list if "-S" in call.args[0]]
        self.assertEqual(len(configure_calls), 1)
        configure = configure_calls[0].args[0]
        self.assertIn("-DMELEE_WEB_PUBLIC_RUNTIME=OFF", configure)
        self.assertIn("-DMELEE_WEB_PIPELINE_PROVENANCE=OFF", configure)
        self.assertIn("-DMELEE_WEB_SELECTIVE_PIPELINES=OFF", configure)

    def test_trace_selection_rejects_other_target_graphs_before_source_work(self):
        for kwargs in (
            {"target": "fighter"},
            {"pipeline_provenance": True},
            {"selective_pipelines": True},
        ):
            with self.subTest(kwargs=kwargs), \
                    patch.object(BUILD, "read_lock") as read_lock:
                with self.assertRaisesRegex(ValueError, "mutually exclusive|private development"):
                    BUILD.build(1, root=Path("/missing/repo"),
                                trace_targets=("gameplay_content_match_trace",), **kwargs)
                read_lock.assert_not_called()

    def test_cli_rejects_target_and_trace_target_together(self):
        with patch.object(BUILD.sys, "argv", [
            "build.py", "--target", "gameplay", "--trace-target",
            "gameplay_content_match_trace",
        ]), patch.object(BUILD, "build") as build:
            with self.assertRaises(SystemExit) as raised:
                BUILD.main()
        self.assertEqual(raised.exception.code, 2)
        build.assert_not_called()

    def test_existing_target_group_still_uses_its_reviewed_closure(self):
        root = self._configured_root()
        lock = {"repositories": {}, "emscripten": "6.0.9"}
        generated = root / "build/gameplay-source/src"
        with patch.object(BUILD, "read_lock", return_value=lock), \
                patch.object(BUILD, "verify_sources"), \
                patch.object(BUILD, "prepare_sources", return_value=generated), \
                patch.object(BUILD.subprocess, "run") as run:
            BUILD.build(2, root=root, target="fighter")
        build_calls = [call for call in run.call_args_list if "--build" in call.args[0]]
        self.assertEqual(len(build_calls), 1)
        command = build_calls[0].args[0]
        self.assertEqual(command[command.index("--target") + 1:command.index("-j")],
                         list(BUILD.BUILD_TARGETS["fighter"]))


if __name__ == "__main__":
    unittest.main()
