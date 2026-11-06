import os
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "posix", "focused control uses POSIX fork for abort guards")
class HsdTextureBoundsSnapshotTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "hsd-texture-bounds-snapshot-")

    def test_actual_c_snapshot_lifecycle(self):
        compiler = shutil.which(os.environ.get("CC", "cc"))
        if compiler is None:
            self.skipTest("A C11 compiler is required for the native leaf control")
        source = ROOT / "src" / "hsd_texture_bounds.c"
        control = ROOT / "tests" / "texture_bounds_snapshot_control.c"
        include = ROOT / "src"
        obj = self.workspace / "hsd_texture_bounds.o"
        binary = self.workspace / "texture_bounds_snapshot_control"
        leaf_command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            f"-I{include}",
            "-Dmalloc=texture_bounds_test_malloc",
            "-Dcalloc=texture_bounds_test_calloc",
            "-Dfree=texture_bounds_test_free",
            "-c", str(source), "-o", str(obj),
        ]
        compiled_leaf = subprocess.run(
            leaf_command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled_leaf.returncode, 0,
                         compiled_leaf.stdout + compiled_leaf.stderr)
        control_command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            f"-I{include}", str(control), str(obj), "-lm", "-o", str(binary),
        ]
        compiled_control = subprocess.run(
            control_command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled_control.returncode, 0,
                         compiled_control.stdout + compiled_control.stderr)
        result = subprocess.run(
            [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-5000:])
        self.assertIn("texture bounds snapshot C controls passed", result.stdout)
