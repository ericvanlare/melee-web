"""Execute original ftCo_800D0FA0 on typed, owned attribute descriptors in Wasm32."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayFighterAttributesTests(unittest.TestCase):
    def test_original_typed_attribute_consumer(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Project SDK unavailable; run bootstrap/build")
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
        with tempfile.TemporaryDirectory(prefix="melee fighter attributes ") as directory:
            directory = Path(directory)
            output = directory / "attributes.js"
            common = ["-O1", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                      "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include")]
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                 "-include", str(ROOT / "src/gameplay_compat.h"), "-c",
                 str(ROOT / "src/gameplay_fighter_attributes.c"), str(source / "melee/ft/ftchangeparam.c")],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-c",
                 *[str(ROOT / "src" / (name + ".cpp")) for name in
                   ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")],
                 str(ROOT / "tests/gameplay_fighter_attributes_trace.cpp")],
            ]
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(compiler / "em++.py"), "-O1", *map(str, directory.glob("*.o")),
                                     "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                                     "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)],
                                    cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            asset = ROOT / "assets-local/next-gate/PlMr.dat"
            container = ROOT / "assets-local/next-gate/PlMrAJ.dat"
            args = [str(asset)] if asset.is_file() else []
            if args and container.is_file():
                args.append(str(container))
            result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Original ftCo_800D0FA0 typed attribute consumer: passed", result.stdout)
            if args:
                self.assertIn("Local Mario attributes consumed by original source: passed", result.stdout)
            if len(args) > 1:
                self.assertIn("Local exact motion2 owned clip: passed", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
