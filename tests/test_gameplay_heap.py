"""Use original Aurora SDK heap code to verify exclusive arena ownership."""
import ast
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayHeapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = ROOT / ".deps/emsdk"
        cls.compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        fmt = ROOT / "build/browser/_deps/fmt-src/include"
        if not (cls.compiler / "em++.py").is_file() or not (fmt / "fmt/format.h").is_file():
            raise unittest.SkipTest("Project SDK/Aurora build dependencies unavailable; run bootstrap/build")
        node_setting = next(ast.literal_eval(statement.value)
            for statement in ast.parse(config.read_text()).body if isinstance(statement, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets))
        cls.node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        if not cls.node.is_relative_to(sdk.resolve()):
            raise RuntimeError("Expected the project-local Node runtime")
        cls.env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                       EM_CACHE=str(cls.compiler / "cache"), EMSDK_PYTHON=sys.executable)
        cls.temp = tempfile.TemporaryDirectory(prefix="melee SDK heap ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.output = cls.directory / "heap.js"
        cls.original = ROOT / ".deps/aurora/lib/dolphin/os/OSAlloc.cpp"
        cls.digest = hashlib.sha256(cls.original.read_bytes()).digest()
        result = subprocess.run([sys.executable, str(cls.compiler / "em++.py"),
            "-std=c++20", "-O1", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
            "-DTARGET_PC", "-DFMT_HEADER_ONLY", "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(fmt), str(ROOT / "src/gameplay_heap.cpp"),
            str(ROOT / ".deps/aurora/lib/logging.cpp"), str(ROOT / "tests/gameplay_heap_trace.cpp"),
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-o", str(cls.output)], env=cls.env, cwd=cls.directory,
            capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.node), str(self.output), case], env=self.env,
                                cwd=self.directory, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original SDK allocator ownership trace: passed", result.stdout)
        self.assertEqual(hashlib.sha256(self.original.read_bytes()).digest(), self.digest)

    def test_owned_heap_destroy_release_free_invalid_queries_and_restart(self):
        self.run_case("owned")

    def test_foreign_initialized_arena_is_not_available(self):
        self.run_case("foreign_initialized")

    def test_foreign_unselected_live_heap_cannot_be_overwritten(self):
        self.run_case("foreign_heap")

    def test_release_refuses_replaced_allocator_identity(self):
        self.run_case("changed_identity")


if __name__ == "__main__":
    unittest.main()
