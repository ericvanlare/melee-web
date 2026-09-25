"""Differential ordinary HSD pool reset/refill checks at relocated roots."""
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
SOURCE_FILES = (
    ROOT / ".deps/melee/extern/dolphin/src/dolphin/os/OSAlloc.c",
    ROOT / ".deps/melee/src/sysdolphin/baselib/memory.c",
    ROOT / ".deps/melee/src/sysdolphin/baselib/objalloc.c",
)
COMPONENT_FILES = SOURCE_FILES + (
    ROOT / "src/source_address_context.hpp",
    ROOT / "src/source_address_context.cpp",
    ROOT / "tests/source_pool_lifetime_model.cpp",
    ROOT / "tests/source_pool_lifetime_oracle.c",
    ROOT / "tests/source_pool_lifetime_test.cpp",
    ROOT / "tests/test_source_pool_lifetime.py",
)


class SourcePoolLifetimeDifferentialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        sdk = ROOT / ".deps/emsdk"
        cls.emcc = sdk / "upstream/emscripten/emcc.py"
        cls.emxx = sdk / "upstream/emscripten/em++.py"
        if not compiler or not cls.emcc.is_file() or not cls.emxx.is_file() or not all(path.is_file() for path in SOURCE_FILES):
            raise unittest.SkipTest("pinned source, Emscripten, and native compiler are required")
        setting = next(ast.literal_eval(statement.value)
                       for statement in ast.parse((sdk / ".emscripten").read_text()).body
                       if isinstance(statement, ast.Assign)
                       and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                               for target in statement.targets))
        cls.node = Path(setting.replace("$CFGDIR", str(sdk))).resolve()
        cls.env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"),
                       EM_CACHE=str(sdk / "upstream/emscripten/cache"), EMSDK=str(sdk),
                       EMSDK_PYTHON=sys.executable)
        cls.evidence = None
        if destination := os.environ.get("MELEE_SOURCE_POOL_LIFETIME_EVIDENCE_DIR"):
            destination = Path(destination)
            if not destination.is_absolute():
                destination = ROOT / destination
            destination = destination.resolve()
            work_root = (ROOT / "work").resolve()
            if destination == work_root or work_root not in destination.parents:
                raise RuntimeError("pool-lifetime evidence must be private under the repository work/ directory")
            cls.evidence = destination
            cls.evidence.mkdir(parents=True, exist_ok=False)
        cls.temp = tempfile.TemporaryDirectory(prefix="source pool lifetime ")
        cls.directory = Path(cls.temp.name)
        cls.hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in SOURCE_FILES}
        cls.component_hashes = {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in COMPONENT_FILES
        }
        cls.oracle = cls.directory / "oracle.js"
        subprocess.run(
            [sys.executable, str(cls.emcc), "-std=gnu11", "-O1",
             "-Itests/source_address_oracle_include", "-I.deps/melee/src",
             "tests/source_pool_lifetime_oracle.c", "-sENVIRONMENT=node",
             "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
             "-o", str(cls.oracle)], cwd=ROOT, env=cls.env,
            check=True, capture_output=True, text=True)
        cls.native = cls.directory / "model-native"
        subprocess.run(
            [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-Isrc", "src/source_address_context.cpp",
             "tests/source_pool_lifetime_model.cpp", "-o", str(cls.native)],
            cwd=ROOT, check=True, capture_output=True, text=True)
        cls.wasm = cls.directory / "model.js"
        subprocess.run(
            [sys.executable, str(cls.emxx), "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
             "-Isrc", "src/source_address_context.cpp", "tests/source_pool_lifetime_model.cpp",
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-o", str(cls.wasm)], cwd=ROOT, env=cls.env,
            check=True, capture_output=True, text=True)
        cls.lifetime_wasm = cls.directory / "lifetime.js"
        subprocess.run(
            [sys.executable, str(cls.emxx), "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
             "-Isrc", "src/source_address_context.cpp", "tests/source_pool_lifetime_test.cpp",
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
             "-o", str(cls.lifetime_wasm)], cwd=ROOT, env=cls.env,
            check=True, capture_output=True, text=True)
        cls.lifetime_native = cls.directory / "lifetime-native"
        subprocess.run(
            [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-Isrc", "src/source_address_context.cpp",
             "tests/source_pool_lifetime_test.cpp", "-o", str(cls.lifetime_native)],
            cwd=ROOT, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        for name, digest in cls.hashes.items():
            if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
                raise AssertionError(f"pinned source changed: {name}")
        cls.temp.cleanup()

    @staticmethod
    def stream():
        return "\n".join((
            "p 24 4",       # ordinary pool, no dedicated/limit modes
            "s 1",           # HSD_MemAlloc refill uses selected heap 1
            "q 2 0",         # split replay: child OS allocation, then adopt/link
            "e 0 0",         # pop-only allocation, no implicit refill
            "r 2",
            "o 0 0",
            "R 24 4",       # genuine in-place HSD_ObjAllocInit reset
            "c 1 0",         # replace the selected OS heap after one reset
            "o 1 0",         # refill succeeds without a second descriptor reset
            "s 0",
            "o 2 0",         # refill from newly selected heap 0
            "R 24 4",
            "s 1",
            "r 1",
            "o 3 0",
        )) + "\n"

    def run_command(self, command, base, stream):
        result = subprocess.run([*command, hex(base)], cwd=ROOT, input=stream,
                                env=self.env, capture_output=True, text=True, check=True)
        return result.stdout

    def retain_fixture(self, base, stream, outputs, guards):
        if self.evidence is None:
            return
        directory = self.evidence / f"{self._testMethodName}-{base:08x}"
        directory.mkdir()
        files = {"operations.txt": stream}
        files.update(outputs)
        files.update(guards)
        hashes = {}
        for name, content in files.items():
            (directory / name).write_text(content)
            hashes[name] = hashlib.sha256(content.encode()).hexdigest()
        (directory / "comparison.json").write_text(json.dumps({
            "schema": "melee-source-pool-lifetime-component-comparison",
            "version": 1,
            "synthetic": True,
            "retail_admission": False,
            "exact": True,
            "events": len(stream.splitlines()),
            "base": f"{base:#x}",
            "source_hashes": self.component_hashes,
            "files": hashes,
            "commands": {
                "original_checked_wasm": ["node", "oracle.js", f"{base:#x}"],
                "native": ["model-native", f"{base:#x}"],
                "model_checked_wasm": ["node", "model.js", f"{base:#x}"],
                "guards_native": ["lifetime-native", f"{base:#x}"],
                "guards_checked_wasm": ["node", "lifetime.js", f"{base:#x}"],
            },
        }, indent=2) + "\n")

    def test_original_ordinary_pool_reset_and_selected_refill_match(self):
        stream = self.stream()
        for base in (0x00102000, 0x34561220, 0x456701C0):
            with self.subTest(base=hex(base)):
                original_text = self.run_command([str(self.node), str(self.oracle)], base, stream)
                native_text = self.run_command([str(self.native)], base, stream)
                wasm_text = self.run_command([str(self.node), str(self.wasm)], base, stream)
                original = [json.loads(line) for line in original_text.splitlines()]
                native = [json.loads(line) for line in native_text.splitlines()]
                wasm = [json.loads(line) for line in wasm_text.splitlines()]
                self.assertEqual(original, native)
                self.assertEqual(original, wasm)
                native_guard = subprocess.run([str(self.lifetime_native), hex(base)], cwd=ROOT,
                                              capture_output=True, text=True, check=True)
                wasm_guard = subprocess.run([str(self.node), str(self.lifetime_wasm), hex(base)], cwd=ROOT,
                                            env=self.env, capture_output=True, text=True, check=True)
                self.assertIn("pool lifetime and selected-refill guards: passed", native_guard.stdout)
                self.assertIn("pool lifetime and selected-refill guards: passed", wasm_guard.stdout)
                self.retain_fixture(
                    base, stream,
                    {"original-checked-wasm.jsonl": original_text,
                     "native.jsonl": native_text,
                     "model-checked-wasm.jsonl": wasm_text},
                    {"guards-native.txt": native_guard.stdout,
                     "guards-checked-wasm.txt": wasm_guard.stdout},
                )


if __name__ == "__main__":
    unittest.main()
