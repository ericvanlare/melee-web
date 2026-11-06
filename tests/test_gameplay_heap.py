"""Use original Aurora SDK heap code to verify exclusive arena ownership."""
import ast
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


class GameplayHeapTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        sdk = ROOT / ".deps/emsdk"
        cls.compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        cls.fmt = ROOT / "build/browser/_deps/fmt-src/include"
        if not (cls.compiler / "em++.py").is_file() or not (cls.fmt / "fmt/format.h").is_file():
            raise unittest.SkipTest("Project SDK/Aurora build dependencies unavailable; run bootstrap/build")
        node_setting = next(ast.literal_eval(statement.value)
            for statement in ast.parse(config.read_text()).body if isinstance(statement, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets))
        cls.node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        if not cls.node.is_relative_to(sdk.resolve()):
            raise RuntimeError("Expected the project-local Node runtime")
        cls.env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                       EM_CACHE=str(cls.compiler / "cache"), EMSDK_PYTHON=sys.executable)
        cls.directory = cls.new_workspace(ROOT, "gameplay-sdk-heap-")
        cls.output = cls.directory / "heap.js"
        cls.original = ROOT / ".deps/aurora/lib/dolphin/os/OSAlloc.cpp"
        cls.digest = hashlib.sha256(cls.original.read_bytes()).digest()
        cls.compile_trace(cls.output, ROOT / "tests/gameplay_heap_trace.cpp",
                          extra_sources=[ROOT / "src/gameplay_heap.cpp"])

    @classmethod
    def compile_trace(cls, output, fixture, *, extra_sources=(), options=()):
        result = subprocess.run([sys.executable, str(cls.compiler / "em++.py"),
            "-std=c++20", "-O1", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
            "-DTARGET_PC", "-DFMT_HEADER_ONLY", "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(cls.fmt), *options, *map(str, extra_sources),
            str(ROOT / "src/gameplay_source_memory_runtime.cpp"),
            str(ROOT / "src/source_address_context.cpp"),
            str(ROOT / "src/source_game_heap_context.cpp"),
            str(ROOT / ".deps/aurora/lib/logging.cpp"), str(fixture),
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1",
            "-o", str(output)], env=cls.env, cwd=cls.directory,
            capture_output=True, text=True, timeout=120)
        (output.with_suffix(".compile.log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_history(self, case, marker):
        generated = ROOT / "build/gameplay-source-slippi-profile/src"
        tracking = (generated / "sysdolphin/baselib/jobj.h").is_file()
        if case == "joint" and not tracking:
            self.skipTest("Private profile generated source headers unavailable")
        output = self.directory / "heap-history.js"
        if not output.is_file():
            options = ["-fexceptions", "-sDISABLE_EXCEPTION_CATCHING=0"]
            if tracking:
                # The actual tracked source heap allocates a class-piece table;
                # the immutable PRE and prepared restore each retain a copy.
                options += ["-sINITIAL_MEMORY=33554432",
                            "-DMELEE_WEB_CPU_INTERVAL_ENABLED=1", "-I", str(generated)]
            self.compile_trace(output, ROOT / "tests/gameplay_heap_history_trace.cpp",
                               options=options)
        result = subprocess.run([str(self.node), str(output), case], env=self.env,
                                cwd=self.directory, capture_output=True, text=True, timeout=30)
        (self.directory / f"history-{case}.log").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(marker, result.stdout)
        self.assertEqual(hashlib.sha256(self.original.read_bytes()).digest(), self.digest)

    def test_original_sdk_history_primitive(self):
        self.run_history("primitive", "SDK arena history leaf controls: passed")

    def test_original_sdk_and_active_shadow_restore_together(self):
        self.run_history("joint", "Actual SDK and active shadow paired restore: passed two cycles")

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

    def test_source_memory_read_observers_preserve_allocator_owner(self):
        self.run_case("unhealthy_observer")


if __name__ == "__main__":
    unittest.main()
