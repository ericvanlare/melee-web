"""Execute original action loaders and the reachable Wait command consumers in Wasm32."""
import ast
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayFighterAssetsTests(unittest.TestCase):
    def test_scoped_constructor_storage_lifetime(self):
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
        with tempfile.TemporaryDirectory(prefix="melee fighter data ") as directory:
            directory = Path(directory)
            output = directory / "fighter_data.js"
            common = ["-O1", "-fexceptions", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                      "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include")]
            # Source per-kind costume storage is otherwise pulled in with every
            # fighter's unrelated callbacks. Supply bounded synthetic storage;
            # the original central table and count declarations remain compiled.
            table_text = (source / "melee/ft/ftdata.c").read_text()
            start = table_text.index("struct UnkCostumeList CostumeListsForeachCharacter")
            table_text = table_text[start:table_text.index("};",start)]
            costumes = directory / "costume_storage.c"
            # Purin's real source object owns its exact five-entry costume
            # table, cache and scoped exchange hook. Keep that definition out
            # of the synthetic storage; all other source tables remain
            # fixture-owned so unrelated kind callbacks stay out of this test.
            names = sorted(set(re.findall(r"ft[A-Za-z]+_CostumeList",table_text)) - {"ftPr_CostumeList"})
            costumes.write_text('#include <melee/ft/types.h>\n' + ''.join(
                f'UnkCostumeStruct {name}[16];\n' for name in names))
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                 "-include", str(ROOT / "src/gameplay_compat.h"), "-c",
                 str(ROOT / "src/gameplay_fighter_assets.c"), str(ROOT / "src/gameplay_action_store.c"),
                 str(ROOT / "src/dat_item_commands.c"),
                 str(source / "melee/ft/ftdata.c"),
                 str(source / "melee/ft/kinds/ftPurin/ftpurin.c"), str(costumes),
                 str(ROOT / "tests/gameplay_fighter_assets_trace.c")],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-c",
                 *[str(ROOT / "src" / (name+".cpp")) for name in ("dat_archive","dat_animation","fighter_binding","dat_fighter_runtime","dat_commands","gameplay_action_store")],
                 str(ROOT / "tests/gameplay_fighter_assets_trace.cpp"), "-o", str(directory / "invalid.o")],
            ]
            # One object per source, including distinct C/C++ trace basenames.
            compile_commands = []
            for command in commands:
                files = [x for x in command if x.endswith((".c", ".cpp"))]
                base = command[:command.index("-c")+1]
                for index, file in enumerate(files):
                    compile_commands.append([*base, file, "-o", str(directory / (Path(file).name + ".o"))])
            commands = compile_commands
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions", *map(str, directory.glob("*.o")),
                                     "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                                     "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)],
                                    cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            asset = ROOT / "assets-local/next-gate/PlMr.dat"
            container = ROOT / "assets-local/next-gate/PlMrAJ.dat"
            args = []
            result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Scoped fighter asset publication, original B10/loaders, independent teardown and restart: passed", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
