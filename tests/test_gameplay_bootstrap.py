"""Execute original HSD allocation/lifecycle/scheduling with Aurora's real heap."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayBootstrapTests(unittest.TestCase):
    def test_original_object_world_and_process_order(self):
        self.run_trace("bootstrap")

    def test_original_fighter_input_consumer_and_lifetime(self):
        self.run_trace("fighter")

    def run_trace(self, kind):
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
        source = ROOT / ".deps/melee/src"
        if kind == "fighter":
            sys.path.insert(0, str(ROOT / "scripts"))
            from gameplay_sources import prepare_sources
            source = prepare_sources(ROOT)
        original = source / "sysdolphin/baselib"
        c_sources = [ROOT / "src/gameplay_bootstrap.c", ROOT / "src/hsd_host_support.c",
                     ROOT / ("tests/gameplay_bootstrap_trace.c" if kind == "bootstrap"
                             else "src/gameplay_fighter_probe.c")]
        c_sources.append(source / "melee/lb/lb_00F9.c")
        cpp_sources = []
        if kind == "fighter":
            c_sources += [source / "melee/ft/fighter.c", source / "melee/ft/ftwalkcommon.c"]
            cpp_sources += [ROOT / "tests/gameplay_fighter_trace.cpp",
                            ROOT / "src/dat_archive.cpp", ROOT / "src/dat_common.cpp"]
        c_sources += [original / (name + ".c") for name in (
            "gobj", "gobjproc", "gobjplink", "gobjgxlink", "gobjobject", "gobjuserdata",
            "objalloc", "memory", "initialize")]
        with tempfile.TemporaryDirectory(prefix="melee gameplay bootstrap ") as directory:
            directory = Path(directory)
            output = directory / "bootstrap.js"
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
                 str(ROOT / ".deps/aurora/lib/logging.cpp"), str(fixture_config), *map(str, cpp_sources)],
            ]
            for command in commands:
                result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([
                sys.executable, str(compiler / "em++.py"), "-O1", *map(str, directory.glob("*.o")),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
                *( ["-sNODERAWFS=1"] if kind == "fighter" else []),
                "-o", str(output)], cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            asset = ROOT / "assets-local/next-gate/PlCo.dat"
            arguments = [str(asset)] if kind == "fighter" and asset.is_file() else []
            result = subprocess.run([str(node), str(output), *arguments], cwd=directory, env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            expected = "Original HSD gameplay-bootstrap scheduler trace: passed" if kind == "bootstrap" else \
                       "Original Fighter input consumer and lifetime trace: passed"
            self.assertIn(expected, result.stdout)
            if arguments:
                self.assertIn("Local PlCo typed root0 consumed by original walk predicate: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
