"""Run the compiled source-file bridge against mocked browser stream hooks."""

from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplaySourceFilesTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "source-file-quiescence-")

    def test_native_completion_callback_is_not_a_quiescent_endpoint(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        self.assertIsNotNone(compiler)
        binary = self.work / "source_files_trace"
        build = subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "src"),
            str(ROOT / "src/gameplay_source_files.c"),
            str(ROOT / "tests/gameplay_source_files_trace.c"),
            "-o", str(binary),
        ], capture_output=True, text=True, timeout=60)
        (self.work / "build.log").write_text(build.stdout + build.stderr)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        (self.work / "trace.log").write_text(run.stdout + run.stderr)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_compiled_emscripten_stream_bridge(self):
        candidates = [
            ROOT / "build" / name / "gameplay_source_files_trace.js"
            for name in ("browser", "browser-release", "browser-public-release",
                         "browser-audio-preview-release")
        ]
        targets = [path for path in candidates if path.is_file()]
        if not targets:
            self.skipTest("Build the Emscripten source-file bridge trace first")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "source RuntimeFiles exact DVD reads, ownership, cancellation and lifecycle trace: passed",
            run.stdout,
        )


if __name__ == "__main__":
    unittest.main()
