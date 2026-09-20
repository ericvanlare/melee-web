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


class GameplayActionStoreTests(unittest.TestCase):
    def test_original_owned_action_and_wait_commands(self):
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
        with tempfile.TemporaryDirectory(prefix="melee action store ") as directory:
            directory = Path(directory)
            output = directory / "actions.js"
            common = ["-O1", "-fexceptions", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                      "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include")]
            # Extract exact original consumers to avoid linking unreachable effects,
            # audio and match services through the full ftAction dispatch table.
            text = (source / "melee/ft/ftaction.c").read_text()
            def function(name):
                start = text.index("void " + name + "(", text.index("static u8 ftAction_803C0870"))
                opening = text.index("{", start)
                end, depth = opening + 1, 1
                while depth:
                    depth += (text[end] == "{") - (text[end] == "}")
                    end += 1
                return text[start:end]
            subset = directory / "action_consumers.c"
            subset.write_text('#include "gameplay_compat.h"\n#include "gameplay_action_store.h"\n'
                '#include <melee/ft/types.h>\n#include <melee/ft/fighter.h>\n#include <melee/ft/ftdynamics.h>\n#include <melee/ft/inlines.h>\n#include <melee/ft/ft_0DF0.h>\n'
                '#include <melee/lb/inlines.h>\n#include <melee/lb/lbcommand.h>\n'
                'void ftAnim_800704F0(HSD_GObj*, int, float);\nvoid ft_8008A1B8(HSD_GObj*, int);\nvoid ftColl_8007AFC8(HSD_GObj*, int);\n'
                + function("ftAction_80071708") + '\n' + function("ftAction_80071784") + '\n' + function("ftAction_80071908") + '\n' + function("ftAction_80071974") + '\n' + function("ftAction_80072BF4") + '\n' + function("ftAction_80072B94") + '\n' + function("ftAction_80073008") + '\n' + function("ftAction_80071820") + '\n' + function("ftAction_800726F4") + '\n' + function("ftAction_80072C6C") + '\n' + function("ftAction_80073118")
                + '\nstatic void (*ftAction_803C06E8[49])(HSD_GObj*, CommandInfo*) = '
                '{[30]=ftAction_800726F4,[42]=ftAction_80072C6C};\n'
                + function("ftAction_80073240"))
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                 "-include", str(ROOT / "src/gameplay_compat.h"), "-c",
                 str(ROOT / "tests/gameplay_action_store_trace.c"), str(subset),
                 str(source / "melee/ft/ftdata.c"), str(source / "melee/ft/ft_0DF0.c"), str(source / "melee/lb/lbanim.c"), str(source / "melee/lb/lbcommand.c")],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-c",
                 *[str(ROOT / "src" / (name + ".cpp")) for name in
                   ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime", "dat_commands", "gameplay_action_store")],
                 str(ROOT / "tests/gameplay_action_store_trace.cpp")],
            ]
            commands.insert(0, [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                "-include", str(ROOT / "src/gameplay_compat.h"), "-c", str(ROOT / "src/gameplay_action_store.c"),
                "-o", str(directory / "action_store_native.o")])
            # The C and C++ test files also need distinct object names.
            commands[1].remove(str(ROOT / "tests/gameplay_action_store_trace.c"))
            commands.insert(1, [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-I", str(source),
                "-include", str(ROOT / "src/gameplay_compat.h"), "-c", str(ROOT / "tests/gameplay_action_store_trace.c"),
                "-o", str(directory / "action_trace_native.o")])
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions", *map(str, directory.glob("*.o")),
                                     "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                                     "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)],
                                    cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            owned_pairs = [
                (ROOT / "assets-local/next-gate/PlMr.dat", ROOT / "assets-local/next-gate/PlMrAJ.dat"),
                (ROOT / "assets-local/next-gate/PlFx.dat", ROOT / "assets-local/next-gate/PlFxAJ.dat"),
                (ROOT / "assets-local/next-gate/PlFc.dat", ROOT / "assets-local/next-gate/PlFcAJ.dat"),
                (ROOT / "assets-local/next-gate/PlMs.dat", ROOT / "assets-local/next-gate/PlMsAJ.dat"),
                (ROOT / "assets-local/full-game-luigi/PlLg.dat", ROOT / "assets-local/full-game-luigi/PlLgAJ.dat"),
                (ROOT / "assets-local/full-game-pikachu/PlPk.dat", ROOT / "assets-local/full-game-pikachu/PlPkAJ.dat"),
                (ROOT / "assets-local/full-game-pichu/PlPc.dat", ROOT / "assets-local/full-game-pichu/PlPcAJ.dat"),
            ]
            available_pairs = [pair for pair in owned_pairs if all(path.is_file() for path in pair)]
            args = [str(path) for pair in available_pairs for path in pair]
            result = subprocess.run([str(node), str(output), *args], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Original owned action loaders and checked Wait command execution: passed", result.stdout)
            if available_pairs == owned_pairs[:4]:
                self.assertIn("Owned common appeal action rows 239/240 for Mario, Fox, Falco and Marth: passed", result.stdout)
            if owned_pairs[0] in available_pairs:
                self.assertIn("Local Mario Wait2/3/6 source command traces and startup clips: passed", result.stdout)
            if owned_pairs[4] in available_pairs:
                self.assertIn("Luigi kind 17 authored self-motion command rows 295/311: passed", result.stdout)
            for index, kind in ((5, 12), (6, 23)):
                if owned_pairs[index] in available_pairs:
                    self.assertIn(f"Pikachu-family kind {kind} authored self-motion rows 295/319: passed", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
