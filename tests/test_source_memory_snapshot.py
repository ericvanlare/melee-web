"""Native value-ownership and refusal controls for the source-memory mirror."""
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


class SourceMemorySnapshotTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.directory = cls.new_workspace(ROOT, "source-memory-snapshot-")

    def test_value_capture_restore_replay_and_owner_refusal(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("native C++ compiler unavailable")
        binary = self.directory / "snapshot-control"
        sources = ["tests/source_memory_snapshot_trace.cpp",
                   "src/gameplay_source_memory_runtime.cpp",
                   "src/source_address_context.cpp", "src/source_game_heap_context.cpp"]
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-I", str(ROOT / "src"),
                                *[str(ROOT / path) for path in sources], "-o", str(binary)],
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Source-memory mirror snapshot controls: passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
