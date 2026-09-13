"""Differential tests for the address-only lbHeap lifecycle model.

The reference executable includes the pinned original ``lbheap.c`` unchanged
under wasm32.  Its OS/HSD/ARAM boundaries are explicit synthetic services;
unrelated allocator entry points abort if a fixture reaches them.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / ".deps/melee/src/melee/lb/lbheap.c"


def run(command, *, env=None, input=None, timeout=120):
    result = subprocess.run(command, cwd=ROOT, env=env, input=input,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(
            f"{command[0]} returned {result.returncode}:\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


class SourceGameHeapDifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        sdk = ROOT / ".deps/emsdk"
        cls.emcc = sdk / "upstream/emscripten/emcc.py"
        cls.emxx = sdk / "upstream/emscripten/em++.py"
        if (not compiler or not cls.emcc.is_file() or not cls.emxx.is_file()
                or not ORIGINAL.is_file()):
            raise unittest.SkipTest("native C++ compiler and pinned Emscripten required")

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
        cls.env = dict(
            os.environ,
            EM_CONFIG=str(sdk / ".emscripten"),
            EM_CACHE=str(sdk / "upstream/emscripten/cache"),
            EMSDK=str(sdk),
            EMSDK_PYTHON=sys.executable,
            ASAN_OPTIONS="detect_leaks=0",
        )
        cls.evidence = None
        if destination := os.environ.get("MELEE_SOURCE_GAME_HEAP_EVIDENCE_DIR"):
            cls.evidence = Path(destination)
            cls.evidence.mkdir(parents=True, exist_ok=False)
        cls.temp = tempfile.TemporaryDirectory(prefix="source game heap oracle ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.original_hash = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()

        cls.oracle = cls.directory / "oracle.js"
        run([
            sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
            "-Itests/source_game_heap_oracle_include", "-I.deps/melee/src",
            "tests/source_game_heap_oracle.c", "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(cls.oracle),
        ], env=cls.env)

        common = [
            "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/source_game_heap_context.cpp", "tests/source_game_heap_model.cpp",
        ]
        cls.native = cls.directory / "model-native"
        run([
            compiler, *common, "-fsanitize=address,undefined", "-o",
            str(cls.native),
        ])
        cls.wasm = cls.directory / "model.js"
        run([
            sys.executable, str(cls.emxx), *common, "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(cls.wasm),
        ], env=cls.env)

        guard_common = [
            "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/source_game_heap_context.cpp", "tests/source_game_heap_guard_test.cpp",
        ]
        cls.guard_native = cls.directory / "guard-native"
        run([
            compiler, *guard_common, "-fsanitize=address,undefined", "-o",
            str(cls.guard_native),
        ])
        cls.guard_wasm = cls.directory / "guard.js"
        run([
            sys.executable, str(cls.emxx), *guard_common, "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(cls.guard_wasm),
        ], env=cls.env)
        run([str(cls.guard_native)])
        run([str(cls.node), str(cls.guard_wasm)], env=cls.env)

    @classmethod
    def tearDownClass(cls):
        if ORIGINAL.is_file():
            actual = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
            if actual != cls.original_hash:
                raise AssertionError("original lbheap.c changed during test")

    @staticmethod
    def operations():
        # Includes both the convenience rebuild and the stepwise request API.
        # The latter is the boundary needed when HSD_CreateMainHeap has nested
        # ObjAlloc lifecycle work that must occur before request completion.
        return [
            "b", "r", "t 2 0", "q", "t 2 1", "t 3 0", "r",
            "t 3 1", "t 4 0", "t 5 0", "q", "t 4 1", "r",
        ]

    def compare(self, operations, bounds):
        stream = "\n".join(operations) + "\n"
        arena_lo, arena_hi, aram_lo, aram_hi = bounds
        args = [f"{value:#x}" for value in bounds]
        oracle_text = run([
            str(self.node), str(self.oracle), *args,
        ], env=self.env, input=stream)
        oracle = [json.loads(line) for line in oracle_text.splitlines()]
        native_text = run([str(self.native), *args], input=stream)
        native = [json.loads(line) for line in native_text.splitlines()]
        wasm_text = run([
            str(self.node), str(self.wasm), *args,
        ], env=self.env, input=stream)
        wasm = [json.loads(line) for line in wasm_text.splitlines()]
        self.assertEqual(oracle, native)
        self.assertEqual(oracle, wasm)
        if self.evidence is not None:
            digest = hashlib.sha256(json.dumps({"stream": stream, "bounds": bounds}, sort_keys=True).encode()).hexdigest()[:12]
            directory = self.evidence / f"{self._testMethodName}-{digest}"
            directory.mkdir()
            files = {
                "operations.txt": stream,
                "oracle.jsonl": oracle_text,
                "native.jsonl": native_text,
                "wasm.jsonl": wasm_text,
            }
            hashes = {}
            for name, content in files.items():
                (directory / name).write_text(content)
                hashes[name] = hashlib.sha256(content.encode()).hexdigest()
            (directory / "comparison.json").write_text(json.dumps({
                "schema": "melee-source-game-heap-component-comparison",
                "version": 1,
                "synthetic": True,
                "exact": True,
                "events": len(operations),
                "oracle_source_sha256": self.original_hash,
                "bounds": [f"{value:#x}" for value in bounds],
                "files": hashes,
            }, indent=2) + "\n")
        return oracle

    def test_lifecycle_order_and_preserved_recreation(self):
        result = self.compare(self.operations(),
                              (0x10000000, 0x12000000,
                               0x0062A000, 0x0162A000))
        relocated = self.compare(self.operations(),
                                 (0x31000000, 0x33000000,
                                  0x0072A000, 0x0172A000))
        # Both adapters report offsets from their independently supplied MEM1
        # and ARAM roots, so identical operations must preserve the full shape.
        self.assertEqual(result, relocated)
        rebuilds = [row for row in result if row["op"] == "rebuild"]
        self.assertGreaterEqual(len(rebuilds), 4)
        self.assertEqual(
            [call["kind"] for call in rebuilds[0]["calls"]],
            ["replace_hsd_main", "destroy_current_handle", "new_current_handle"],
        )
        self.assertEqual(rebuilds[-1]["status"], "ok")
        self.assertEqual(rebuilds[-1]["heaps"][4]["status"], 1)

    def test_relocated_roots_and_invalid_index(self):
        operations = ["b", "t 9 0", "t 5 0", "r", "t 5 1", "r"]
        first = self.compare(operations,
                             (0x21000000, 0x23000000,
                              0x0072A000, 0x0172A000))
        self.assertEqual(first[1]["status"], "invalid_request")
        self.assertEqual(first[-1]["status"], "ok")
        self.assertEqual(first[3]["heaps"][5]["start"], 0)


if __name__ == "__main__":
    unittest.main()
