"""Execute original action loaders and the reachable Wait command consumers in Wasm32."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayFighterDataTests(unittest.TestCase):
    def test_native_fighter_data(self):
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
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                 "-include", str(ROOT / "src/gameplay_compat.h"), "-c",
                 str(ROOT / "src/gameplay_fighter_data.c"), str(ROOT / "src/gameplay_article_data.c"),
                 str(ROOT / "src/gameplay_action_store.c"), str(ROOT / "tests/gameplay_fighter_data_trace.c")],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-c",
                 *[str(ROOT / "src" / (name+".cpp")) for name in ("dat_archive","native_dat","dat_animation","fighter_binding","dat_fighter_runtime","dat_commands","gameplay_action_store")],
                 str(ROOT / "tests/gameplay_fighter_data_trace.cpp"), "-o", str(directory / "invalid.o")],
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
            args = [str(asset),str(container)] if asset.is_file() and container.is_file() else []
            roy_asset = ROOT / "assets-local/next-gate/PlFe.dat"
            roy_container = ROOT / "assets-local/next-gate/PlFeAJ.dat"
            if args and roy_asset.is_file() and roy_container.is_file():
                args.extend((str(roy_asset),str(roy_container)))
            ganon_asset = ROOT / "assets-local/full-game-ganon/PlGn.dat"
            ganon_container = ROOT / "assets-local/full-game-ganon/PlGnAJ.dat"
            if len(args) == 4 and ganon_asset.is_file() and ganon_container.is_file():
                args.extend((str(ganon_asset),str(ganon_container)))
            result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Owned native ftData synthetic and rejection checks: passed", result.stdout)
            if len(args) >= 4:
                self.assertIn("Native Roy dynamics selector-5 rows and six authored modes: passed", result.stdout)
            if len(args) == 6:
                self.assertIn("Native Ganondorf Captain extension and null Article table: passed", result.stdout)
            captain_asset = ROOT / "assets-local/full-game-captain/PlCa.dat"
            captain_container = ROOT / "assets-local/full-game-captain/PlCaAJ.dat"
            if len(args) == 6 and captain_asset.is_file() and captain_container.is_file():
                args.extend((str(captain_asset),str(captain_container)))
                result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("Native Captain extension, six-costume bounds, zero dynamics and null Article table: passed",
                              result.stdout)
                pikachu_asset = ROOT / "assets-local/full-game-pikachu/PlPk.dat"
                pikachu_container = ROOT / "assets-local/full-game-pikachu/PlPkAJ.dat"
                pichu_asset = ROOT / "assets-local/full-game-pichu/PlPc.dat"
                pichu_container = ROOT / "assets-local/full-game-pichu/PlPcAJ.dat"
                if all(path.is_file() for path in (pikachu_asset, pikachu_container,
                                                   pichu_asset, pichu_container)):
                    args.extend((str(pikachu_asset), str(pikachu_container),
                                 str(pichu_asset), str(pichu_container)))
                    result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("Native Pikachu shared 0xf8 attributes and three Article registrations: passed",
                                  result.stdout)
                    self.assertIn("Native Pichu shared 0xf8 attributes and three Article registrations: passed",
                                  result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
