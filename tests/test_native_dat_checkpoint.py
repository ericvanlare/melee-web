"""Descriptor allocations only; not rendered-world rollback acceptance."""
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


class NativeDatCheckpoint(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "native-dat-checkpoint-")

    def test_owner_topology_atomic_refusal_and_byte_restoration(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        binary = self.work / "checkpoint"
        build = subprocess.run([
            compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
            "-UNDEBUG", "-Isrc", "src/dat_archive.cpp", "src/native_dat.cpp",
            "tests/native_dat_checkpoint_test.cpp", "-o", str(binary),
        ], cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                             text=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("checkpoint ownership controls passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
