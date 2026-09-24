"""Checked-Wasm source ARInit with a three-cast C++ compatibility boundary."""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DEPS = Path(os.environ.get("MELEE_DEPS_ROOT", ROOT / ".deps")).resolve()
ORIGINAL = DEPS / "melee/extern/dolphin/src/dolphin/ar/ar.c"
SDK = DEPS / "emsdk"


class SourceArInitProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not ORIGINAL.is_file() or not (SDK / "upstream/emscripten/em++.py").is_file():
            raise unittest.SkipTest("pinned source and Emscripten SDK are required")
        setting = next(
            ast.literal_eval(statement.value)
            for statement in ast.parse((SDK / ".emscripten").read_text()).body
            if isinstance(statement, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                    for target in statement.targets)
        )
        cls.node = Path(setting.replace("$CFGDIR", str(SDK))).resolve()
        if not cls.node.is_relative_to(SDK.resolve()):
            raise RuntimeError("expected project-local Node runtime")
        cls.env = dict(os.environ, EM_CONFIG=str(SDK / ".emscripten"),
                       EM_CACHE=str(SDK / "upstream/emscripten/cache"),
                       EMSDK=str(SDK), EMSDK_PYTHON=sys.executable)
        if destination := os.environ.get("MELEE_SOURCE_AR_INIT_EVIDENCE_DIR"):
            cls.directory = Path(destination).resolve()
            cls.directory.mkdir(parents=True, exist_ok=False)
        else:
            cls.temp = tempfile.TemporaryDirectory(prefix="source ar init profile ")
            cls.addClassCleanup(cls.temp.cleanup)
            cls.directory = Path(cls.temp.name)
        cls.binary = cls.directory / "source_ar_init_profile.js"
        cls.original_hash = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
        if cls.original_hash != "dc2ad84463413ef6b91a8f8fe8acefb26ed35743d63dd85bf6590cec28d95cc8":
            raise RuntimeError("ar.c differs from the pinned source")
        source = ORIGINAL.read_text()
        old = "= (void*) (((u32) &"
        replacement = "= (u32*) (uintptr_t) (((u32) &"
        if source.count(old) != 3:
            raise RuntimeError("expected exactly three legacy C pointer casts in ar.c")
        # Keep the explained downstream source adaptation under patches/.
        patched = cls.directory / "ar.c"
        patched.write_text(source)
        patch_path = ROOT / "patches/melee-source-ar-init-cxx.patch"
        applied = subprocess.run(["git", "apply", "--no-index", str(patch_path)],
                                 cwd=cls.directory, capture_output=True, text=True,
                                 timeout=30)
        if applied.returncode:
            raise RuntimeError(f"AR C++ compatibility patch failed: {applied.stderr}")
        compat = patched.read_text()
        if compat != source.replace(old, replacement):
            raise RuntimeError("AR compatibility patch changed more than the three casts")
        compat += """
extern \"C\" uint32_t melee_web_source_ar_init_source_stack_pointer(void) { return __AR_StackPointer; }
extern \"C\" uint32_t melee_web_source_ar_init_source_free_blocks(void) { return __AR_FreeBlocks; }
extern \"C\" uintptr_t melee_web_source_ar_init_source_block_length(void) { return reinterpret_cast<uintptr_t>(__AR_BlockLength); }
extern \"C\" int melee_web_source_ar_init_source_init_flag(void) { return __AR_init_flag; }
"""
        cls.compat = cls.directory / "ar_cxx_compat.cpp"
        cls.compat.write_text(compat)
        cls.compat_hash = hashlib.sha256(compat.encode()).hexdigest()
        command = [sys.executable, str(SDK / "upstream/emscripten/em++.py"),
                   "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off",
                   "-Itests/source_ar_init_profile_include",
                   "-I" + str(ORIGINAL.parent),
                   str(cls.compat),
                   "tests/source_ar_init_profile_oracle.cpp",
                   "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                   "-sSAFE_HEAP=1", "-sINITIAL_MEMORY=67108864",
                   "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-Wno-bitwise-op-parentheses", "-Wno-unused-parameter",
                   "-o", str(cls.binary)]
        result = subprocess.run(command, cwd=ROOT, env=cls.env,
                                capture_output=True, text=True, timeout=180)
        (cls.directory / "compile.stdout").write_text(result.stdout)
        (cls.directory / "compile.stderr").write_text(result.stderr)
        (cls.directory / "source-hashes.json").write_text(json.dumps({
            "original": cls.original_hash, "compat": cls.compat_hash,
            "adaptation": "exactly three pointer result casts; four observation accessors",
        }, indent=2) + "\n")
        if result.returncode:
            failures = ROOT / "work/source-ar-init-failures"
            failures.mkdir(parents=True, exist_ok=True)
            retained = Path(tempfile.mkdtemp(prefix="compile-", dir=failures))
            shutil.copytree(cls.directory, retained, dirs_exist_ok=True)
            raise RuntimeError(f"oracle compile failed:\n{result.stdout}\n{result.stderr}")
        if hashlib.sha256(ORIGINAL.read_bytes()).hexdigest() != cls.original_hash:
            raise RuntimeError("original ar.c changed during compatibility generation")

    def tearDown(self):
        result = self._outcome.result
        if any(test is self or getattr(test, "test_case", None) is self
               for test, _ in result.failures + result.errors):
            failures = ROOT / "work/source-ar-init-failures"
            failures.mkdir(parents=True, exist_ok=True)
            retained = Path(tempfile.mkdtemp(prefix="runtime-", dir=failures))
            shutil.copytree(self.directory, retained, dirs_exist_ok=True)

    def test_original_arinit_profile_and_lifo_reservations(self):
        result = subprocess.run([
            str(self.node), str(self.binary),
            "--aram-size", str(0x01000000),
            "--stack-source", str(0x80400000),
            "--stack-entries", "16", "--bus-clock", "162000000",
        ], cwd=ROOT,
                                env=self.env, capture_output=True, text=True,
                                timeout=30)
        (self.directory / "positive.stdout").write_text(result.stdout)
        (self.directory / "positive.stderr").write_text(result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["schema"], "melee-web-source-ar-init-profile")
        self.assertEqual(row["version"], 1)
        self.assertEqual(row["profile"], {
            "name": "declared-retail-aram-16m",
            "initial_base": 0x4000,
            "hardware_size": 0x01000000,
            "stack_entries": 16,
            "stack_source": 0x80400000,
            "bus_clock": 162000000,
        })
        self.assertEqual(row["init"], {
            "return": 0x4000,
            "second_return": 0x4000,
            "check_init": 1,
            "size": 0x01000000,
            "size_cell": 0x01000000,
            "source_stack_pointer": 0x4000,
            "source_free_blocks": 16,
            "source_block_length_offset": 0,
            "source_init_flag": 1,
        })
        self.assertEqual(row["stack"]["words_after_init"], [0] * 16)
        self.assertFalse(row["stack"]["source_symbol_validated"])
        self.assertTrue(row["dsp"]["handler_installed"])
        self.assertEqual(row["dsp"]["unmask_mask"], 0x02000000)
        self.assertEqual(row["dsp"]["mode_after_probe"], 3)
        self.assertEqual(row["dsp"]["refresh"], 156)
        self.assertEqual(row["dsp"]["busy"], 0)
        self.assertGreaterEqual(row["dma"]["count"], 18)
        self.assertGreater(row["dma"]["alias_writes"], 0)
        self.assertGreater(row["dma"]["out_of_range_reads"], 0)
        self.assertGreater(row["dma"]["out_of_range_writes"], 0)
        self.assertEqual(row["allocations"], [0x4000, 0x4500, 0x6500])
        self.assertEqual(row["frees"], [0x6500, 0x4500, 0x4000])
        self.assertEqual(row["stack"]["source_symbol_validated"], False)
        self.assertEqual(hashlib.sha256(ORIGINAL.read_bytes()).hexdigest(), self.original_hash)
        self.assertEqual(hashlib.sha256(self.compat.read_bytes()).hexdigest(), self.compat_hash)

    def test_absent_expansion_preserves_nonzero_published_bytes(self):
        result = subprocess.run([
            str(self.node), str(self.binary), "--aram-size", str(0x1000000),
            "--stack-source", str(0x80400000), "--stack-entries", "16",
            "--bus-clock", "162000000", "--control", "absent-expansion"],
            cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30)
        (self.directory / "absent-expansion.stdout").write_text(result.stdout)
        (self.directory / "absent-expansion.stderr").write_text(result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout),
                         {"synthetic_absent_expansion_preserves_bytes": True})

    def test_rejects_unowned_spans_and_stalled_hardware(self):
        base = ["--aram-size", str(0x1000000), "--stack-source", str(0x80400000),
                "--stack-entries", "16", "--bus-clock", "162000000"]
        cases = [(base + ["--control", "unowned-dma"], "owned cache spans"),
                 (base + ["--control", "unowned-cache"], "live source probe stack"),
                 (base + ["--control", "stalled-ready"], "operation budget"),
                 ([*base[:4], "--stack-source", str(0x80400000), *base[6:]], "duplicate"),
                 ([*base[:3], str(0xFFFFFFE0), *base[4:]], "outside declared MEM1"),
                 ([*base[:7], "202500000"], "GameCube bus clock")]
        for index, (args, message) in enumerate(cases):
            with self.subTest(message=message):
                result = subprocess.run([str(self.node), str(self.binary), *args],
                                        cwd=ROOT, env=self.env, capture_output=True,
                                        text=True, timeout=30)
                (self.directory / f"negative-{index}.stdout").write_text(result.stdout)
                (self.directory / f"negative-{index}.stderr").write_text(result.stderr)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
