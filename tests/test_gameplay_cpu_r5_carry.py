"""Focused typed CPU r5 carry checks; no browser or retail input admission."""
from __future__ import annotations

import ast
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from cpu_r5_source_context import derive_owned_seed_binding


def run(command: list[str], *, cwd: Path = ROOT, timeout: int = 60,
        env: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(f"{command!r} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


class CpuR5CarryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which(os.environ.get("CC", "cc"))
        if not cls.cc:
            raise unittest.SkipTest("C compiler unavailable")
        configured_source = os.environ.get("MELEE_PINNED_SOURCE")
        cls.melee = Path(configured_source) if configured_source else ROOT / ".deps/melee"
        cls.original_random = cls.melee / "src/sysdolphin/baselib/random.c"
        if not cls.original_random.is_file():
            raise unittest.SkipTest("set MELEE_PINNED_SOURCE to the pinned source checkout")
        cls.temp = tempfile.TemporaryDirectory(prefix="cpu-r5-carry ")
        cls.directory = Path(cls.temp.name)
        cls.native = cls.directory / "carry"
        run([cls.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-Isrc",
             "src/gameplay_cpu_r5_carry.c", "tests/gameplay_cpu_r5_carry_trace.c",
             "-o", str(cls.native)])
        cls.source = None
        cls.source_compile_error = None
        sdk = Path(os.environ.get("MELEE_EMSDK", str(ROOT / ".deps/emsdk")))
        emcc = sdk / "upstream/emscripten/emcc.py"
        if emcc.is_file():
            cls.source = cls.directory / "source-carry.js"
            # The pinned SDK's Runtime/platform.h hard-codes the Metrowerks
            # host typedef for ssize_t.  Emscripten already owns that libc
            # typedef; skip only that platform wrapper and provide the
            # source SDK's fixed-width aliases required by random.c.  This is
            # a Wasm32 header boundary, not a host ABI typedef override.
            cls.source_sdk_header = cls.directory / "source_sdk_types.h"
            cls.source_sdk_header.write_text(
                "#ifndef RUNTIME_PLATFORM_H\n"
                "#define RUNTIME_PLATFORM_H\n"
                "typedef signed char s8;\n"
                "typedef unsigned char u8;\n"
                "typedef signed short s16;\n"
                "typedef unsigned short u16;\n"
                "typedef signed long s32;\n"
                "typedef unsigned long u32;\n"
                "typedef float f32;\n"
                "typedef double f64;\n"
                "#endif\n"
            )
            setting = next(
                ast.literal_eval(statement.value)
                for statement in ast.parse((sdk / ".emscripten").read_text()).body
                if isinstance(statement, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                        for target in statement.targets)
            )
            node = Path(setting.replace("$CFGDIR", str(sdk))).resolve()
            if not node.is_relative_to(sdk.resolve()):
                raise AssertionError("pinned Emscripten Node escapes the SDK")
            env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"),
                       EM_CACHE=str(sdk / "upstream/emscripten/cache"),
                       EMSDK=str(sdk), EMSDK_PYTHON=sys.executable)
            try:
                run([sys.executable, str(emcc), "-std=gnu11", "-O1", "-Wall", "-Wextra",
                     "-include", str(cls.source_sdk_header), "-Isrc",
                     "-I", str(cls.melee / "src"),
                     "-I", str(cls.melee / "extern/dolphin/include"),
                     "src/gameplay_cpu_r5_carry.c", "tests/gameplay_cpu_r5_source_trace.c",
                     str(cls.original_random), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                     "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-o", str(cls.source)], env=env)
                cls.source_node = node
                cls.source_env = env
            except (AssertionError, OSError) as error:
                cls.source_compile_error = str(error)
                cls.source = None
        else:
            cls.source_compile_error = "pinned Emscripten emcc.py unavailable"

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_typed_lifetime_and_fail_closed_consumer(self):
        self.assertEqual(run([str(self.native)]).strip(),
                         "cpu r5 carry trace: passed")

    def test_original_random_source_publishes_seed_route(self):
        if self.source is None:
            self.skipTest(f"pinned Wasm32 source compile unavailable: {self.source_compile_error}")
        configured = {
            name: os.environ.get(name)
            for name in ("MELEE_CPU_DOL", "MELEE_CPU_DISC",
                         "MELEE_CPU_SYMBOLS", "MELEE_CPU_SOURCE_ROOT")
        }
        if all(configured.values()):
            binding = derive_owned_seed_binding(
                dol_path=Path(configured["MELEE_CPU_DOL"]),
                disc_path=Path(configured["MELEE_CPU_DISC"]),
                symbols_path=Path(configured["MELEE_CPU_SYMBOLS"]),
                source_root=Path(configured["MELEE_CPU_SOURCE_ROOT"]),
            )
            self.assertTrue(binding["independently_derived"])
        else:
            # This still executes the untouched source random.c in Wasm32,
            # but the relocated words are synthetic and cannot establish
            # retail provenance. The owned adapter test is the provenance
            # gate when DOL/disc inputs are configured.
            binding = {"source_word": 0x811230A4,
                       "global_address": 0x81234010}
        source_word = binding["source_word"]
        output = run([str(self.source_node), str(self.source), hex(source_word),
                      hex(binding["global_address"]), "0x81234540"], env=self.source_env)
        self.assertEqual(output.strip(), "original random source -> cpu carry: passed")

    def test_no_tracked_numeric_retail_input(self):
        for path in (ROOT / "src/gameplay_cpu_r5_carry.h",
                     ROOT / "src/gameplay_cpu_r5_carry.c",
                     ROOT / "tests/gameplay_cpu_r5_source_trace.c"):
            text = path.read_text()
            self.assertNotIn("804D5F90", text)
            self.assertNotIn("804d5f90", text)
            self.assertNotIn("90,40", text)


if __name__ == "__main__":
    unittest.main()
