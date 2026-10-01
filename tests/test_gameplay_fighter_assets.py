"""Run the fighter-asset ownership trace with pinned tools and production sources."""
import ast
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayFighterAssetsTests(unittest.TestCase):
    def test_scoped_constructor_storage_lifetime(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        cmake = ROOT / ".venv/bin/cmake"
        if not cmake.is_file():
            cmake = Path(shutil.which("cmake") or "")
        if not (compiler / "emcc.py").is_file() or not config.is_file() or not cmake.is_file():
            self.skipTest("Project SDK and CMake dependencies unavailable; run bootstrap/build")

        node_setting = None
        for statement in ast.parse(config.read_text()).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str)
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        if not node.is_file():
            self.skipTest("Pinned SDK Node executable unavailable; run bootstrap/build")
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        self.assertEqual((compiler / "emscripten-version.txt").read_text().strip().strip('"'),
                         lock["emscripten"])

        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        result = subprocess.run([str(cmake), "--build", str(ROOT / "build/browser"),
                                 "--target", "gameplay_fighter_asset_trace", "-j8"],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(node), str(ROOT / "build/browser/gameplay_fighter_asset_trace.js")],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Scoped fighter asset publication, original B10/loaders, independent teardown and restart: passed",
                      result.stdout)


if __name__ == "__main__":
    unittest.main()
