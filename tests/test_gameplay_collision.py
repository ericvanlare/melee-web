"""Execute original mpLib initialization, pruning and queries against the real HSD heap."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayCollisionTests(unittest.TestCase):
    def test_original_static_collision_load_queries_and_lifetime(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        fmt = ROOT / "build/browser/_deps/fmt-src/include"
        if not (compiler / "emcc.py").is_file() or not config.is_file() or not (fmt / "fmt/format.h").is_file():
            self.skipTest("Project SDK and Aurora build dependencies unavailable; run bootstrap/build")
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
        source = ROOT / "build/gameplay-source/src"
        if not (source / "melee/mp/mplib.c").is_file():
            self.skipTest("Prepare checked gameplay sources first")
        if "mpLib_80058820_owned" not in (source / "melee/mp/mplib.c").read_text():
            self.skipTest("Prepare checked collision update ownership patch first")
        original = source / "sysdolphin/baselib"
        c_sources = [ROOT / "src/gameplay_bootstrap.c", ROOT / "src/hsd_host_support.c",
                     ROOT / "src/gameplay_collision.c", ROOT / "tests/gameplay_collision_trace.c"]
        c_sources += [original / (name + ".c") for name in (
            "gobj", "gobjproc", "gobjplink", "gobjgxlink", "gobjobject", "gobjuserdata",
            "objalloc", "memory", "initialize")]
        c_sources += [source / "melee" / path for path in (
            "mp/mpisland.c", "gr/grdynamicattr.c", "gr/ground.c", "lb/lb_00B0.c",
            "lb/lb_00F9.c")]
        c_sources += [ROOT / ".deps/aurora/lib/dolphin/mtx/vec.c"]
        with tempfile.TemporaryDirectory(prefix="melee gameplay collision ") as directory:
            directory = Path(directory)
            output = directory / "collision.js"
            # The real Aurora logger reads this process-local configuration.
            # No renderer or successful platform-function doubles are linked.
            fixture_config = directory / "aurora_config.cpp"
            fixture_config.write_text('#include <aurora/aurora.h>\nnamespace aurora { AuroraConfig g_config{}; }\n')
            common = ["-O1", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                      "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include")]
            commands = [
                [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11", "-DTARGET_PC",
                 "-I", str(source), "-include", str(ROOT / "src/gameplay_compat.h"),
                 "-c", *map(str, c_sources)],
                [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20", "-DFMT_HEADER_ONLY",
                 "-I", str(fmt), "-c", str(ROOT / "src/gameplay_heap.cpp"),
                 str(ROOT / ".deps/aurora/lib/logging.cpp"), str(fixture_config)],
            ]
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([
                sys.executable, str(compiler / "em++.py"), "-O1", *map(str, directory.glob("*.o")),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                "-o", str(output)], cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node), str(output)], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Original mpLib collision initialization/query trace: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
