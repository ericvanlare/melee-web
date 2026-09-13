"""Optional real stage descriptor boundary; does not claim complete stage execution."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeStageDescriptors(unittest.TestCase):
    def test_complete_visual_graphs_and_platform_animation(self):
        asset=ROOT/"assets-local/next-gate/GrNLa.dat"
        if not asset.is_file():self.skipTest("Optional local GrNLa.dat unavailable")
        if not (ROOT/"build/gameplay-source/src/sysdolphin/baselib/mobj.h").is_file():self.skipTest("Prepared source headers unavailable")
        compiler=shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler,"A C++20 compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/"stage"
            command=[compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror","-DTARGET_PC","-Isrc","-Ibuild/gameplay-source/src","-I.deps/aurora/include"]
            command += [f"src/{name}.cpp" for name in ("dat_archive","dat_stage","dat_texture","dat_material","rigid_model","dat_native_joint","dat_native_animation","dat_material_animation","dat_animation")]
            command += ["tests/dat_native_stage_test.cpp","-o",str(binary)]
            result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary),str(asset)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn("FD complete nine visual graphs",result.stdout)

class NativeStageOriginalRuntime(unittest.TestCase):
    def run_trace(self,name,argument):
        target=ROOT/"build/browser"/(name+".js")
        if not target.is_file():self.skipTest("Original stage trace target unavailable")
        import sys
        sys.path.insert(0,str(ROOT/"scripts"))
        from check_gameplay import node_runtime
        result=subprocess.run([str(node_runtime()),str(target),str(argument)],cwd=ROOT,capture_output=True,text=True,timeout=180)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout

    def test_original_map_objects_scene_animation_and_restart(self):
        asset=ROOT/"assets-local/next-gate/GrNLa.dat"
        if not asset.is_file():self.skipTest("Optional local stage asset unavailable")
        output=self.run_trace("dat_native_stage_map_trace",asset)
        self.assertIn("Complete FD native map",output)

    def test_original_oninit_scheduler_and_teardown(self):
        assets=ROOT/"assets-local/next-gate"
        required=("PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sislib_font.bin")
        if not all((assets/name).is_file() for name in required):self.skipTest("Optional complete local runtime assets unavailable")
        output=self.run_trace("gameplay_stage_last_trace",assets)
        self.assertIn("Original FD OnInit, scheduled background transitions",output)
