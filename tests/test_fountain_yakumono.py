"""Check the isolated Fountain of Dreams yakumono source ABI decoder."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FountainYakumonoDecoderTests(unittest.TestCase):
    def test_source_scalar_abi_boundary(self):
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            self.skipTest("C and C++20 compilers are required")
        with tempfile.TemporaryDirectory(prefix="melee Fountain yakumono ") as directory:
            directory = Path(directory)
            includes = ["-I", str(ROOT / "src")]
            decoder_object = directory / "gameplay_stage_fountain.o"
            result = subprocess.run(
                [cc, "-std=c11", *includes, "-c",
                 str(ROOT / "src/gameplay_stage_fountain.c"),
                 "-o", str(decoder_object)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            binary = directory / "fountain_yakumono_decoder_trace"
            result = subprocess.run(
                [cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 *includes, str(ROOT / "tests/fountain_yakumono_decoder_trace.cpp"),
                 str(decoder_object), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("0x54-byte source ABI", result.stdout)

    def test_pinned_source_struct_layout_matches_portable_header(self):
        cc = shutil.which("clang") or shutil.which("cc")
        if not cc:
            self.skipTest("A C compiler is required")
        source_path = ROOT / ".deps/melee/src/melee/gr/grizumi.c"
        if not source_path.is_file():
            self.skipTest("Bootstrap the pinned Melee source for its ABI check")
        source = source_path.read_text()
        match = re.search(
            r"struct grIzumi_YakumonoParam\s*\{.*?\};", source, re.S)
        self.assertIsNotNone(match, "pinned grIzumi yakumono struct was not found")
        source_struct = match.group(0)
        fields = [
            "x0", "x4", "x8", "xC", "x10", "x14", "x18", "x1C",
            "x20", "x24", "x28", "x2C", "x30", "x34", "x38", "x3C",
            "x40", "x44", "x48", "x4C", "x50",
        ]
        assertions = [
            "_Static_assert(sizeof(struct grIzumi_YakumonoParam) == 0x54,"
            ' "pinned source yakumono size changed");',
            "_Static_assert(sizeof(((struct grIzumi_YakumonoParam*)0)->x4) =="
            ' sizeof(int32_t), "pinned source x4 is not 32-bit");',
        ]
        for field in fields:
            scalar = "int32_t" if field == "x4" else "float"
            assertions.append(
                f"_Static_assert(offsetof(struct grIzumi_YakumonoParam, {field}) == "
                f"offsetof(MeleeWebFountainYakumono, {field}), "
                f'"source/header offset mismatch at {field}");'
            )
            for owner in ("struct grIzumi_YakumonoParam", "MeleeWebFountainYakumono"):
                assertions.append(
                    f"_Static_assert(_Generic((({owner}*)0)->{field}, "
                    f"{scalar}: 1, default: 0), "
                    f'"source/header scalar type mismatch at {field}");'
                )
        probe = "\n".join([
            "#include <stddef.h>",
            "#include <stdint.h>",
            '#include "gameplay_stage_fountain.h"',
            source_struct,
            *assertions,
            "int main(void) { return 0; }",
            "",
        ])
        with tempfile.TemporaryDirectory(prefix="melee Fountain source ABI ") as directory:
            directory = Path(directory)
            probe_source = directory / "fountain_source_abi.c"
            probe_source.write_text(probe)
            result = subprocess.run(
                [cc, "-std=c11", "-I", str(ROOT / "src"), "-Wall", "-Wextra",
                 "-Werror", "-c", str(probe_source), "-o",
                 str(directory / "fountain_source_abi.o")],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
