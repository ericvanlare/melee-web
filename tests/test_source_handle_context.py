"""Differential tests for the source-address lbMemory handle model.

The reference executable includes the pinned original ``lbmemory.c`` directly
under wasm32.  The C++ model receives every allocator root explicitly, so the
same event stream can be replayed with relocated source bases without using a
captured pointer as an input.
"""
from __future__ import annotations

import ast
import hashlib
import json
import os
import random
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / ".deps/melee/src/melee/lb/lbmemory.c"


def run(command, *, env=None, input=None, timeout=120):
    result = subprocess.run(command, cwd=ROOT, env=env, input=input,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(
            f"{command[0]} returned {result.returncode}:\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def portable_operations(arena_lo):
    """Return a source-shaped stream using offsets from the supplied arena."""
    heap_lo = arena_lo + 0x20000
    heap_hi = arena_lo + 0x20100
    return [
        "b", "k", f"n heap {heap_lo:#x} {heap_hi:#x}",
        "a heap first 33", "a heap middle 33", "a heap last 33",
        "f heap middle", "a heap tie 33", "m heap",
        "f heap first", "f heap last", "f heap tie", "d heap", "s",
    ]


class SourceHandleDifferentialTests(unittest.TestCase):
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
        if destination := os.environ.get("MELEE_SOURCE_HANDLE_EVIDENCE_DIR"):
            cls.evidence = Path(destination)
            cls.evidence.mkdir(parents=True, exist_ok=False)
        cls.temp = tempfile.TemporaryDirectory(prefix="source handle oracle ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)

        cls.original_hash = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
        cls.oracle = cls.directory / "oracle.js"
        run([
            sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
            "-Itests/source_handle_oracle_include", "-I.deps/melee/src",
            "tests/source_handle_oracle.c", "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(cls.oracle),
        ], env=cls.env)

        cls.native = cls.directory / "model-native"
        common = [
            "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/source_handle_context.cpp", "tests/source_handle_model.cpp",
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
                raise AssertionError("original lbmemory.c changed during test")

    def retain_evidence(self, operations, records, error):
        if self.evidence is None:
            return
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
            "schema": "melee-source-handle-component-comparison",
            "version": 1,
            "synthetic": True,
            "exact": error is None,
            "events": len(operations),
            "oracle_source_sha256": self.original_hash,
            "files": files,
            "error": error,
        }, indent=2) + "\n")

    def compare(self, operation_factory):
        records = []
        error = None
        reference = None
        try:
            bases = (0x10000000, 0x23450000, 0x45670000)
            arena_los = (0x0062a000, 0x0072a000, 0x0082a000)
            for index, (base, arena_lo) in enumerate(zip(bases, arena_los)):
                arena_hi = arena_lo + 0x100000
                operations = operation_factory(arena_lo)
                stream = "\n".join(operations) + "\n"
                records.append((f"operations-{index}.txt", stream))
                oracle_text = run([
                    str(self.node), str(self.oracle), f"{arena_lo:#x}",
                    f"{arena_hi:#x}"
                ], env=self.env, input=stream)
                oracle = list(map(json.loads, oracle_text.splitlines()))
                self.assertEqual(len(oracle), len(operations))
                records.append((f"original-{index}.jsonl", oracle_text))

                args = [
                    f"{base:#x}", f"{base + 8:#x}", f"{base + 0x638:#x}",
                    f"{base + 0x69c:#x}", f"{arena_lo:#x}", f"{arena_hi:#x}",
                ]
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
                                     f"arena relocation divergence at {arena_lo:#x}")
            return reference
        except Exception as failure:
            error = str(failure)
            raise
        finally:
            if self.evidence is not None and records:
                self.retain_evidence(operation_factory(0x0062a000), records, error)

    def test_portable_allocation_shape_matches_original(self):
        reference = self.compare(portable_operations)
        self.assertTrue(all(row["status"] == "ok" for row in reference[:8]))
        self.assertEqual(reference[8]["status"], "async_move_required")
        self.assertEqual(reference[8]["allocations"], 3)
        self.assertEqual(reference[-1]["status"], "ok")
        self.assertEqual(reference[7]["payload"], 0x20000 + 192)

    def test_fragmentation_and_descriptor_reuse_in_mem1(self):
        def operations(arena_lo):
            # MEM1 heap bounds are source-owned ranges outside the ARAM
            # domain. Relocate them together with the synthetic arena.
            start = 0x80000000 + arena_lo
            rows = ["b", f"n heap {start:#x} {start + 0x10000:#x}"]
            rng = random.Random(7341)
            live = set()
            for _ in range(250):
                vacant = [slot for slot in range(32) if slot not in live]
                if live and (not vacant or rng.randrange(3) == 0):
                    slot = rng.choice(sorted(live))
                    rows.append(f"f heap p{slot}")
                    live.remove(slot)
                else:
                    slot = rng.choice(vacant)
                    rows.append(f"a heap p{slot} {rng.randint(1, 1024)}")
                    live.add(slot)
            rows.extend(f"f heap p{slot}" for slot in sorted(live))
            rows += ["m heap", "d heap", f"n recycled {start:#x} {start + 0x10000:#x}",
                     "a recycled final 64", "d recycled", "s"]
            return rows
        result = self.compare(operations)
        self.assertTrue(all(row["status"] == "ok" for row in result))
        self.assertEqual(result[-1]["allocations"], 0)

    def test_layout_and_source_domain_guards(self):
        base = 0x10000000
        args = [
            f"{base:#x}", f"{base + 8:#x}", f"{base + 0x638:#x}",
            f"{base + 0x69c:#x}", "0x00629ea0", "0x01000000",
        ]
        overlap = [
            f"{base:#x}", f"{base + 8:#x}", f"{base + 0x20:#x}",
            f"{base + 0x69c:#x}", "0x00629ea0", "0x01000000",
        ]
        for name, command, env in (
            ("native", [str(self.native), *overlap], None),
            ("wasm", [str(self.node), str(self.wasm), *overlap], self.env),
        ):
            with self.subTest(name=name, case="descriptor overlap"):
                rows = list(map(json.loads, run(
                    command, env=env, input="b\n"
                ).splitlines()))
                self.assertEqual(rows[0]["status"], "invalid_layout")

        slot_overlap = [
            f"{base:#x}", f"{base + 8:#x}", f"{base + 0x638:#x}",
            f"{base + 0x10:#x}", "0x00629ea0", "0x01000000",
        ]
        for name, command, env in (
            ("native", [str(self.native), *slot_overlap], None),
            ("wasm", [str(self.node), str(self.wasm), *slot_overlap], self.env),
        ):
            with self.subTest(name=name, case="current slot overlap"):
                rows = list(map(json.loads, run(
                    command, env=env, input="b\n"
                ).splitlines()))
                self.assertEqual(rows[0]["status"], "invalid_layout")

        invalid_stream = "\n".join([
            "b", "n below_arena 0x00600000 0x00601000",
            "a current zero 0",
        ]) + "\n"
        for name, command, env in (
            ("native", [str(self.native), *args], None),
            ("wasm", [str(self.node), str(self.wasm), *args], self.env),
        ):
            with self.subTest(name=name, case="source domain"):
                rows = list(map(json.loads, run(
                    command, env=env, input=invalid_stream
                ).splitlines()))
                self.assertEqual(rows[1]["status"], "invalid_request")
                self.assertEqual(rows[2]["status"], "invalid_request")


if __name__ == "__main__":
    unittest.main()
