"""Execute original mpLib initialization, pruning and queries from CMake."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayCollisionTests(unittest.TestCase):
    def test_original_static_collision_load_queries_and_lifetime(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        node = sdk / "node/24.19.0_64bit/bin/node"
        cmake = ROOT / ".venv/bin/cmake"
        if not cmake.is_file():
            cmake = Path(shutil.which("cmake") or "")
        if not (compiler / "emcc.py").is_file() or not config.is_file() or not node.is_file() or not cmake.is_file():
            self.skipTest("Project SDK and Aurora build dependencies unavailable; run bootstrap/build")
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        result = subprocess.run([str(cmake), "--build", str(ROOT / "build/browser"),
                                 "--target", "gameplay_collision_trace", "-j8"],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=300)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(node), str(ROOT / "build/browser/gameplay_collision_trace.js")],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original mpLib collision initialization/query trace: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
