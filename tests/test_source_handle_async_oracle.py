"""Granular differential coverage for original asynchronous lbmemory paths."""

from __future__ import annotations

import ast
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def run(command, *, env=None, input_text="", timeout=60):
    result = subprocess.run(command, cwd=ROOT, env=env, input=input_text,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(
            f"{command} returned {result.returncode}:\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return [json.loads(line) for line in result.stdout.splitlines() if line]


class SourceHandleAsyncOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        sdk = ROOT / ".deps/emsdk"
        cls.emcc = sdk / "upstream/emscripten/emcc.py"
        if not compiler or not cls.emcc.is_file():
            raise unittest.SkipTest("native C++ compiler and Emscripten required")
        cls.temp = tempfile.TemporaryDirectory(prefix="source handle async oracle ")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        cls.oracle = directory / "oracle.js"
        cls.model = directory / "model"
        env = dict(os.environ,
                   EM_CONFIG=str(sdk / ".emscripten"),
                   EM_CACHE=str(sdk / "upstream/emscripten/cache"),
                   EMSDK=str(sdk), EMSDK_PYTHON=sys.executable)
        run([
            sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
            "-Itests/source_handle_oracle_include", "-I.deps/melee/src",
            "tests/source_handle_async_oracle.c", "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(cls.oracle),
        ], env=env)
        run([
            compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
            "-Isrc", "src/source_handle_context.cpp",
            "tests/source_handle_async_model.cpp", "-o", str(cls.model),
        ])
        setting = next(
            ast.literal_eval(statement.value)
            for statement in ast.parse((sdk / ".emscripten").read_text()).body
            if isinstance(statement, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                    for target in statement.targets)
        )
        cls.node = Path(setting.replace("$CFGDIR", str(sdk))).resolve()
        if not cls.node.is_relative_to(sdk.resolve()):
            raise RuntimeError("Expected project-local Node runtime")
        cls.env = env

    def compare(self, stream):
        # Keep the arena below 16 MiB because the pinned original's AR arena
        # initialization clamps ARGetSize to 0x01000000.
        args = ["0x00600000", "0x00800000"]
        oracle = run([self.node, str(self.oracle), *args], env=self.env,
                     input_text=stream)
        model = run([str(self.model), *args], input_text=stream)
        self.assertEqual(oracle, model)

    def test_multi_gap_devcom_chain_and_final_callback(self):
        self.compare("""\
b
n heap 0x00600000 0x00800000
a heap a 32
a heap b 32
a heap c 32
f heap a
m heap
c
c
""")

    def test_noop_and_single_move_are_source_equivalent(self):
        self.compare("""\
b
n heap 0x00600000 0x00800000
a heap hole 32
a heap only 64
f heap hole
m heap
c
m heap
""")


if __name__ == "__main__":
    unittest.main()
