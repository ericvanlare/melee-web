"""Synthetic checked HSD material descriptors; proprietary assets are unnecessary."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class MaterialAnimationTests(unittest.TestCase):
    def test_owned_tables_and_rejected_unsafe_indices(self):
        if not (ROOT / "build/gameplay-source/src/sysdolphin/baselib/mobj.h").is_file():
            self.skipTest("Prepared original gameplay headers unavailable; build gameplay first")
        compiler=shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee material animation ") as directory:
            binary=Path(directory)/"material_animation"
            result=subprocess.run([compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror","-DTARGET_PC",
                "-I",str(ROOT/"src"),"-I",str(ROOT/"build/gameplay-source/src"),"-I",str(ROOT/".deps/aurora/include"),
                *[str(ROOT/"src"/(name+".cpp")) for name in ("dat_archive","dat_texture","dat_animation","dat_material_animation")],
                str(ROOT/"tests/dat_material_animation_test.cpp"),"-o",str(binary)],
                capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn("owned material animation topology/index/bounds checks passed",result.stdout)

if __name__=="__main__":
    unittest.main()
