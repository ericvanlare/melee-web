"""Original effect-bank registration plus optional local Mario effect graph."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from check_gameplay import node_runtime

class EffectContextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.target=ROOT/"build/browser/gameplay_effect_banks_trace.js"
        if not cls.target.is_file() or not (ROOT/".deps/emsdk/.emscripten").is_file():
            raise unittest.SkipTest("Effect trace unavailable; build gameplay_effect_banks_trace first")
        cls.node=node_runtime()

    def run_trace(self,*args):
        result=subprocess.run([str(self.node),str(self.target),*map(str,args)],cwd=ROOT,
                              capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout

    def test_authored_bank_bounds_lifetimes_and_restart(self):
        self.assertIn("Original particle bank ownership, bounds, readiness and restoration passed",self.run_trace())

    def test_local_common_descriptors_and_original_spline_path(self):
        asset=ROOT/"assets-local/next-gate/EfCoData.dat"
        if not asset.is_file():self.skipTest("Owned common effect archive unavailable")
        self.assertIn("Common47 descriptors and original PATH reference/evaluation/restart passed",self.run_trace("--common",asset))

    def test_local_mario_original_loader_and_native_animations(self):
        asset=ROOT/"assets-local/next-gate/EfMrData.dat"
        if not asset.is_file():
            self.skipTest("Local EfMrData.dat unavailable; proprietary assets are optional")
        self.assertIn("two native models and animation graphs; original LoadSync/evaluation/restart passed",self.run_trace(asset))

if __name__=="__main__":
    unittest.main()
