"""Run original persistent common material consumers in the built Wasm world."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from check_gameplay import node_runtime

class CommonContextTests(unittest.TestCase):
    def test_original_material_owners_restore_all_common_globals(self):
        target=ROOT/"build/browser/gameplay_common_context_trace.js"
        if not target.is_file() or not (ROOT/".deps/emsdk/.emscripten").is_file():
            self.skipTest("Common context trace unavailable; build gameplay first")
        result=subprocess.run([str(node_runtime()),str(target)],cwd=ROOT,
                              capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("Original common8064/8F6C persistent ownership/publication/restoration/restart passed",result.stdout)

if __name__=="__main__":
    unittest.main()
