"""Differential tests for the address-only AR allocation stack model."""
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
ORIGINAL = ROOT / ".deps/melee/extern/dolphin/src/dolphin/ar/ar.c"


def run(command, *, env=None, input=None, timeout=120):
    result = subprocess.run(command, cwd=ROOT, env=env, input=input,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(
            f"{command[0]} returned {result.returncode}:\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def portable_operations(sizes):
    operations = ["i", "g", "i"]
    operations.extend(f"a block{index} {size}"
                      for index, size in enumerate(sizes))
    operations.extend(("f", "a reused 64", "g"))
    return operations


class SourceAramDifferentialTests(unittest.TestCase):
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
        if destination := os.environ.get("MELEE_SOURCE_ARAM_EVIDENCE_DIR"):
            cls.evidence = Path(destination)
            cls.evidence.mkdir(parents=True, exist_ok=False)
        cls.temp = tempfile.TemporaryDirectory(prefix="source aram oracle ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)

        cls.original_hash = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
        cls.oracle = cls.directory / "oracle.js"
        run([
            sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
            "-ffunction-sections", "-fdata-sections",
            "-Itests/source_aram_oracle_include", "tests/source_aram_oracle.c",
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
            "-sSAFE_HEAP=1", "-sERROR_ON_UNDEFINED_SYMBOLS=1",
            "-Wl,--gc-sections", "-o", str(cls.oracle),
        ], env=cls.env)

        cls.native = cls.directory / "model-native"
        common = [
            "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/source_aram_context.cpp", "tests/source_aram_model.cpp",
        ]
        run([
            compiler, *common, "-fsanitize=address,undefined", "-o",
            str(cls.native),
        ])
        cls.wasm = cls.directory / "model.js"
        run([
            sys.executable, str(cls.emxx), *common, "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-o",
            str(cls.wasm),
        ], env=cls.env)

    @classmethod
    def tearDownClass(cls):
        if ORIGINAL.is_file():
            actual = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
            if actual != cls.original_hash:
                raise AssertionError("original ar.c changed during test")

    def retain_evidence(self, sizes, records, error):
        if self.evidence is None:
            return
        operations = portable_operations(sizes)
        stream = "\n".join(operations) + "\n"
        digest = hashlib.sha256(stream.encode()).hexdigest()[:12]
        directory = self.evidence / f"{self._testMethodName}-{digest}"
        directory.mkdir()
        files = {}
        (directory / "operations.txt").write_text(stream)
        files["operations.txt"] = hashlib.sha256(stream.encode()).hexdigest()
        for name, output in records:
            (directory / name).write_text(output)
            files[name] = hashlib.sha256(output.encode()).hexdigest()
        (directory / "comparison.json").write_text(json.dumps({
            "schema": "melee-source-aram-component-comparison",
            "version": 1,
            "synthetic": True,
            "exact": error is None,
            "events": len(operations),
            "oracle_source_sha256": self.original_hash,
            "files": files,
            "error": error,
        }, indent=2) + "\n")

    def compare(self, sizes):
        records = []
        error = None
        reference = None
        try:
            operations = portable_operations(sizes)
            stream = "\n".join(operations) + "\n"
            # These are declared source-machine layouts, not captured
            # addresses. Every profile keeps a 16 MiB ARAM span while moving
            # its source base and therefore every returned allocation address.
            profiles = (
                (0x00004000, 16),
                (0x00020000, 16),
                (0x00040000, 16),
            )
            for index, (base, capacity) in enumerate(profiles):
                hardware_size = base + 0x01000000
                oracle_text = run([
                    str(self.node), str(self.oracle), f"{base:#x}",
                    str(capacity), f"{hardware_size:#x}",
                ], env=self.env, input=stream)
                oracle = list(map(json.loads, oracle_text.splitlines()))
                self.assertEqual(len(oracle), len(operations))
                records.append((f"original-{index}.jsonl", oracle_text))

                args = [f"{base:#x}", str(capacity), f"{hardware_size:#x}"]
                native_text = run([str(self.native), *args], input=stream)
                native = list(map(json.loads, native_text.splitlines()))
                records.append((f"native-{index:08x}.jsonl", native_text))
                self.assertEqual(oracle, native,
                                 f"native divergence at base {base:#x}")

                wasm_text = run([
                    str(self.node), str(self.wasm), *args
                ], env=self.env, input=stream)
                wasm = list(map(json.loads, wasm_text.splitlines()))
                records.append((f"wasm-{index:08x}.jsonl", wasm_text))
                self.assertEqual(oracle, wasm,
                                 f"wasm divergence at base {base:#x}")
                if reference is None:
                    reference = oracle
                else:
                    self.assertEqual(reference, oracle,
                                     f"source-base relocation divergence at {base:#x}")
            return reference
        except Exception as failure:
            error = str(failure)
            raise
        finally:
            if self.evidence is not None and records:
                self.retain_evidence(sizes, records, error)

    def test_portable_stack_shape_and_lifo_matches_original(self):
        reference = self.compare((1280, 6248864, 196608, 32))
        self.assertEqual(reference[0]["status"], "ok")
        self.assertEqual(reference[1]["hardware_size"], 0x01000000)
        self.assertEqual(reference[2]["status"], "already_initialized")
        top_before_last = 1280 + 6248864 + 196608
        self.assertEqual(reference[6]["address"], top_before_last)
        self.assertEqual(reference[7]["address"], top_before_last)
        self.assertEqual(reference[7]["size"], 32)
        self.assertEqual(reference[8]["address"], top_before_last)
        self.assertEqual(reference[8]["size"], 64)
        self.assertEqual(reference[-1]["status"], "ok")

    def test_layout_and_alignment_guards_in_native_and_checked_wasm(self):
        valid = ["0x4000", "16", "0x1004000"]
        unaligned = "i\na bad 33\n"
        bad_layout = "i\n"
        for name, command, env in (
            ("native", [str(self.native), *valid], None),
            ("wasm", [str(self.node), str(self.wasm), *valid], self.env),
        ):
            with self.subTest(name=name, case="alignment"):
                rows = list(map(json.loads, run(
                    command, env=env, input=unaligned
                ).splitlines()))
                self.assertEqual(rows[1]["status"], "invalid_request")

        for name, command, env in (
            ("native", [str(self.native), "0x4001", "16", "0x1004000"], None),
            ("wasm", [str(self.node), str(self.wasm), "0x4001", "16", "0x1004000"], self.env),
        ):
            with self.subTest(name=name, case="layout"):
                rows = list(map(json.loads, run(
                    command, env=env, input=bad_layout
                ).splitlines()))
                self.assertEqual(rows[0]["status"], "invalid_layout")

    def test_capacity_and_exhaustion_do_not_mutate_stack(self):
        args = ["0x20000", "2", "0x20040"]
        stream = "i\na first 32\na second 32\na full 0\nf\na too_large 64\nf\nf\n"
        for name, command, env in (
            ("native", [str(self.native), *args], None),
            ("wasm", [str(self.node), str(self.wasm), *args], self.env),
        ):
            with self.subTest(name=name):
                rows = list(map(json.loads, run(command, env=env, input=stream).splitlines()))
                self.assertEqual([row["status"] for row in rows],
                                 ["ok", "ok", "ok", "exhausted", "ok", "exhausted", "ok", "empty"])
                self.assertEqual(rows[2]["stack"], rows[3]["stack"])
                self.assertEqual(rows[4]["stack"], rows[5]["stack"])
                self.assertEqual(rows[-1]["stack"], 0)


if __name__ == "__main__":
    unittest.main()
