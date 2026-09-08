"""Exercise owned common graphs through unchanged original part consumers in Wasm32."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CommonTablesTests(unittest.TestCase):
    def test_original_part_lookup_and_owned_native_graphs(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Pinned Emscripten SDK unavailable; run bootstrap")
        node_setting = None
        for statement in ast.parse(config.read_text()).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str)
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        self.assertEqual((compiler / "emscripten-version.txt").read_text().strip().strip('"'), lock["emscripten"])
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        sys.path.insert(0, str(ROOT / "scripts"))
        from gameplay_sources import prepare_sources
        source = prepare_sources(ROOT)
        with tempfile.TemporaryDirectory(prefix="melee common native ") as directory:
            directory = Path(directory)
            output = directory / "common.js"
            common = ["-O1", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                      "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include")]
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                 "-include", str(ROOT / "src/gameplay_compat.h"), "-c", str(ROOT / "src/common_tables.c"),
                 str(ROOT / "tests/common_tables_layout_trace.c"),
                 str(source / "melee/ft/ftparts.c"), str(source / "melee/ft/fighter.c")],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-c",
                 str(ROOT / "src/dat_archive.cpp"), str(ROOT / "src/dat_common.cpp"),
                 str(ROOT / "tests/common_tables_trace.cpp")],
            ]
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([
                sys.executable, str(compiler / "em++.py"), "-O1", *map(str, directory.glob("*.o")),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                "-sNODERAWFS=1", "-o", str(output)], cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            asset = ROOT / "assets-local/next-gate/PlCo.dat"
            arguments = [str(asset)] if asset.is_file() else []
            result = subprocess.run([str(node), str(output), *arguments], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Original common table ownership and part lookup trace: passed", result.stdout)
            if arguments:
                self.assertIn("Local PlCo 15 static roots and original Mario/Fox part consumers: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
