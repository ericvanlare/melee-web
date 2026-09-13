"""Differential source-address prerequisite checks; no retail match admission.

The oracle includes the pinned original SDK/HSD C bodies unchanged under wasm32
release layout. Only addresses are normalized to test-arena offsets. The model
runs the same events as both host-native C++ and Wasm at several declared source
bases. Invalid-input checks belong to the adapter/model, not retail assertions.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "extern/dolphin/src/dolphin/os/OSAlloc.c",
    "src/sysdolphin/baselib/objalloc.c",
    "src/sysdolphin/baselib/memory.c",
)


def run(command, *, env=None, input=None, timeout=120):
    result = subprocess.run(command, cwd=ROOT, env=env, input=input,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{command[0]} returned {result.returncode}:\n{result.stdout}\n{result.stderr}")
    return result.stdout


class NativeSourceAddressTests(unittest.TestCase):
    def test_context_lifetime_invalid_inputs_and_all_register_bytes(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler:
            self.skipTest("Native C++ compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="source address native ") as directory:
            output = Path(directory) / "guards"
            run([compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-Isrc", "src/source_address_context.cpp",
                 "tests/source_address_context_test.cpp", "-o", str(output)])
            self.assertIn("register guards: passed", run([str(output)]))


class SourceAddressDifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = ROOT / ".deps/emsdk"
        cls.emcc = sdk / "upstream/emscripten/emcc.py"
        cls.emxx = sdk / "upstream/emscripten/em++.py"
        cls.original = [ROOT / ".deps/melee" / path for path in SOURCES]
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler or not cls.emcc.is_file() or not all(p.is_file() for p in cls.original):
            raise unittest.SkipTest("Pinned source, local Emscripten and native C++ compiler required")
        cls.hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in cls.original}
        cls.evidence = None
        if destination := os.environ.get("MELEE_SOURCE_ADDRESS_EVIDENCE_DIR"):
            cls.evidence = Path(destination)
            cls.evidence.mkdir(parents=True, exist_ok=False)
        setting = next(ast.literal_eval(statement.value)
                       for statement in ast.parse((sdk / ".emscripten").read_text()).body
                       if isinstance(statement, ast.Assign)
                       and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                               for target in statement.targets))
        cls.node = Path(setting.replace("$CFGDIR", str(sdk))).resolve()
        if not cls.node.is_relative_to(sdk.resolve()):
            raise RuntimeError("Expected project-local Node runtime")
        cls.env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"),
                       EM_CACHE=str(sdk / "upstream/emscripten/cache"), EMSDK=str(sdk),
                       EMSDK_PYTHON=sys.executable)
        cls.temp = tempfile.TemporaryDirectory(prefix="source address oracle ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.oracle = cls.directory / "oracle.js"
        digest = cls.hashes[str(cls.original[0].relative_to(ROOT))]
        run([sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
             "-Itests/source_address_oracle_include", "-I.deps/melee/src",
             f'-DSOURCE_OSALLOC_SHA256="{digest}"', "tests/source_address_oracle.c",
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
             "-o", str(cls.oracle)], env=cls.env)
        cls.native = cls.directory / "model-native"
        cls.wasm = cls.directory / "model.js"
        common = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
                  "src/source_address_context.cpp", "tests/source_address_model_trace.cpp"]
        run([compiler, *common, "-fsanitize=address,undefined", "-o", str(cls.native)])
        run([sys.executable, str(cls.emxx), *common, "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
             "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-o", str(cls.wasm)], env=cls.env)
        cls.guards = cls.directory / "guards.js"
        run([sys.executable, str(cls.emxx), "-std=c++17", "-O1", "-Isrc",
             "src/source_address_context.cpp", "tests/source_address_context_test.cpp",
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
             "-o", str(cls.guards)], env=cls.env)

    @classmethod
    def tearDownClass(cls):
        for name, before in cls.hashes.items():
            after = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            if after != before:
                raise AssertionError(f"Original source changed: {name}")

    def compare_stream(self, operations):
        stream = "\n".join(operations) + "\n"
        retained = {"operations.txt": stream}
        accepted = False
        error = None
        try:
            reference = self.compare_retained_stream(operations, stream, retained)
            accepted = True
            return reference
        except Exception as failure:
            error = str(failure)
            raise
        finally:
            destination = self.evidence
            if not accepted and destination is None:
                failures = ROOT / "work/source-address-context-failures"
                failures.mkdir(parents=True, exist_ok=True)
                destination = Path(tempfile.mkdtemp(prefix="failure-", dir=failures))
            if destination:
                identity = hashlib.sha256(stream.encode()).hexdigest()[:12]
                directory = destination / f"{self._testMethodName}-{identity}"
                directory.mkdir(exist_ok=False)
                files = {}
                for name, text in retained.items():
                    (directory / name).write_text(text)
                    files[name] = hashlib.sha256(text.encode()).hexdigest()
                (directory / "comparison.json").write_text(json.dumps({
                    "schema": "melee-source-address-component-comparison", "version": 1,
                    "synthetic": True, "retail_admission": False, "exact": accepted,
                    "events": len(operations), "source_hashes": self.hashes,
                    "files": files, "error": error,
                }, indent=2) + "\n")
                if not accepted:
                    print(f"Complete source-address diagnostic retained: {directory}", file=sys.stderr)

    def compare_retained_stream(self, operations, stream, retained):
        raw = run([str(self.node), str(self.oracle)], env=self.env, input=stream)
        retained["original.jsonl"] = raw
        header, *reference = map(json.loads, raw.splitlines())
        self.assertEqual(header["object_allocator"], "original_ordinary_os_backed")
        self.assertEqual(header["source_osalloc_sha256"], next(iter(self.hashes.values())))
        self.assertEqual(len(reference), len(operations))
        # Bases are synthetic test inputs, not observed retail address constants.
        for base in (0x102000, 0x34561220, 0x456701C0):
            args = [str(base), str(header["heap_start_offset"]), str(header["heap_end_offset"])]
            for target, command in (("native", [str(self.native)]),
                                    ("wasm", [str(self.node), str(self.wasm)])):
                output = run([*command, *args], env=self.env, input=stream)
                retained[f"{target}-{base:08x}.jsonl"] = output
                actual = list(map(json.loads, output.splitlines()))
                self.assertEqual(len(actual), len(reference))
                for index, (expected, observed) in enumerate(zip(reference, actual)):
                    self.assertEqual(expected, observed,
                                     f"first {target} divergence at event {index}: {operations[index]}, base {base:#x}")
        return reference

    def test_source_first_fit_split_threshold_exhaustion_and_coalescing(self):
        operations = ["a 0 1", "a 1 31", "a 2 32", "a 3 33", "a 4 95",
                      "f 1", "f 3", "a 5 33", "a 6 31", "f 0", "f 2", "f 5",
                      "f 6", "f 4", "a 7 65441", "a 8 1", "f 7", "a 9 65408",
                      "a 10 1", "f 9", "f 10", "a 11 65536", "a 12 0", "f 127",
                      "a 128 16", "a 13 4294967295"]
        reference = self.compare_stream(operations)
        self.assertTrue(any(row["status"] == "oom" for row in reference))
        self.assertTrue(any(row["status"] == "invalid" for row in reference))

    def test_fragmented_source_histories_at_multiple_source_bases(self):
        for seed in (17, 809, 65537):
            rng = random.Random(seed)
            operations = []
            for _ in range(1000):
                slot = rng.randrange(64)
                if rng.randrange(3) == 0:
                    operations.append(f"f {slot}")
                else:
                    operations.append(f"a {slot} {rng.choice((1, 7, 31, 32, 33, 95, 128, 1000, 8192, 65536))}")
            operations.extend(f"f {slot}" for slot in range(64))
            with self.subTest(seed=seed):
                reference = self.compare_stream(operations)
                self.assertEqual(reference[-1]["allocated_cells"], [])
                self.assertEqual(len(reference[-1]["free_cells"]), 1)

    def test_original_hsd_refill_lifo_and_interleaved_os_ownership(self):
        operations = ["p 0 21 4", "p 1 37 64", "a 100 99", "r 0 5", "r 1 3"]
        operations += [f"o {slot} {slot % 2}" for slot in range(12)]
        operations += [f"q {slot} {slot % 2}" for slot in (0, 4, 2, 1, 7, 3)]
        operations += ["r 0 2", "r 1 2", "f 100"]
        operations += [f"o {slot} {slot % 2}" for slot in (0, 4, 2, 1, 7, 3)]
        operations += [f"q {slot} {slot % 2}" for slot in range(12)]
        operations += ["q 0 0", "p 0 21 4", "o 0 7", "o 0 0", "f 0", "q 0 1"]
        reference = self.compare_stream(operations)
        self.assertTrue(reference[-1]["allocated_cells"], "HSD frees retain backing OS cells")
        self.assertEqual(reference[-1]["pools"][0]["peak"], 6)

    def test_context_and_register_guards_in_wasm(self):
        self.assertIn("register guards: passed", run([str(self.node), str(self.guards)], env=self.env))


if __name__ == "__main__":
    unittest.main()
