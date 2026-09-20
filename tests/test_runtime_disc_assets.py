from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
class RuntimeDiscAssetsTests(unittest.TestCase):
    def test_bounded_disc_reader(self):
        self.run_node("disc_image_test.mjs")

    def test_scene_disc_session(self):
        self.run_node("disc_session_test.mjs")

    def test_language_and_executable_validation(self):
        self.run_node("runtime_disc_assets_test.mjs")

    def run_node(self, script):
        if not (ROOT/'.deps/emsdk/.emscripten').is_file():
            self.skipTest('Bootstrap the pinned Node runtime')
        result=subprocess.run([str(node_runtime()),str(ROOT/'tests'/script)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
