"""Trace original HSD PObj calls through the bounded Aurora array adapter."""

import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HsdPObjBridgeTests(unittest.TestCase):
    def test_original_draw_calls_cache_reuse_and_metadata_rejection(self):
        sdk = ROOT / ".deps/emsdk"
        compiler_root = sdk / "upstream/emscripten"
        compiler = compiler_root / "emcc.py"
        config = sdk / ".emscripten"
        if not compiler.is_file() or not config.is_file():
            self.skipTest("Project-local Emscripten SDK unavailable; run scripts/bootstrap.py")

        # Read the SDK's activated Node path as data, without executing its
        # Python configuration or accidentally choosing a global Node binary.
        node_setting = None
        for statement in ast.parse(config.read_text(encoding="utf-8")).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                        for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str, "Expected an activated SDK Node path")
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()), "Node must belong to this SDK")
        if not node.is_file():
            self.skipTest("Project-local SDK Node unavailable; run scripts/bootstrap.py")

        lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
        version = (compiler_root / "emscripten-version.txt").read_text().strip().strip('"')
        self.assertEqual(version, lock["emscripten"], "SDK differs from the dependency pin")
        env = dict(os.environ)
        env.update({
            "EMSDK": str(sdk),
            "EM_CONFIG": str(config),
            "EM_CACHE": str(compiler_root / "cache"),
            "EMSDK_PYTHON": sys.executable,
        })
        with tempfile.TemporaryDirectory(prefix="melee hsd trace ") as directory:
            output = Path(directory) / "hsd_pobj_trace.js"
            command = [
                sys.executable, str(compiler), "-O1", "-DTARGET_PC",
                "-I", str(ROOT / "src"),
                "-I", str(ROOT / ".deps/melee/src"),
                "-I", str(ROOT / ".deps/aurora/include"),
                "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                "-include", str(ROOT / "src/hsd_probe_compat.h"),
                str(ROOT / "src/hsd_pobj_bridge.c"),
                str(ROOT / ".deps/melee/src/sysdolphin/baselib/state.c"),
                str(ROOT / "tests/hsd_pobj_trace.c"),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(output),
            ]
            compiled = subprocess.run(
                command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run(
                [str(node), str(output)], cwd=ROOT, env=env,
                capture_output=True, text=True, timeout=20,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("HSD original PObj bridge trace: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
