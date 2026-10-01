"""Construct Fox's checked native action store from owned revision 2 assets."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FoxGameplayActionTests(unittest.TestCase):
    def test_checked_special_command_graphs(self):
        fighter = ROOT / "assets-local/next-gate/PlFx.dat"
        animation = ROOT / "assets-local/next-gate/PlFxAJ.dat"
        if not fighter.is_file() or not animation.is_file():
            self.skipTest("Owned Fox revision 2 assets are absent")
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
        with tempfile.TemporaryDirectory(prefix="melee Fox actions ") as directory:
            directory = Path(directory)
            output = directory / "fox_actions.js"
            common = ["-O1", "-fexceptions", "-Wall", "-Wextra", "-Werror", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                      "-ffp-contract=off", "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
                      "-I", str(source)]
            c_sources = [ROOT / "src/gameplay_action_store.c", ROOT / "src/gameplay_fighter_data.c",
                         ROOT / "src/gameplay_article_data.c", ROOT / "tests/fox_gameplay_data_trace.c"]
            cpp_sources = [ROOT / "src" / (name + ".cpp") for name in
                           ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime",
                            "dat_commands", "gameplay_result_motion_table", "gameplay_action_store", "native_dat")]
            cpp_sources.append(ROOT / "tests/fox_gameplay_action_trace.cpp")
            objects = []
            for src in c_sources:
                obj = directory / (src.name + ".o")
                result = subprocess.run(
                    [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11",
                     "-include", str(ROOT / "src/gameplay_compat.h"), "-c", str(src), "-o", str(obj)],
                    cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(obj)
            for src in cpp_sources:
                obj = directory / (src.name + ".o")
                result = subprocess.run(
                    [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20",
                     "-c", str(src), "-o", str(obj)],
                    cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(obj)
            result = subprocess.run(
                [sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions", *map(str, objects),
                 "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                 "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)],
                cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(node), str(output), str(fighter), str(animation)], cwd=directory, env=env,
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Article slots, common attacks and all 32 source special command rows: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
