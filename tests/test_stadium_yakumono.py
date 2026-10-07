"""Check the exact Stadium yakumono scalar ABI with synthetic DAT bytes only."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StadiumYakumonoDecoderTests(unittest.TestCase):
    def _compilers(self):
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            self.skipTest("C and C++20 compilers are required")
        return cc, cxx

    def test_synthetic_reader_bounds_and_arena_ownership(self):
        cc, cxx = self._compilers()
        includes = ["-I", str(ROOT / "src")]
        with tempfile.TemporaryDirectory(prefix="melee Stadium yakumono ") as directory:
            directory = Path(directory)
            decoder_object = directory / "gameplay_stage_stadium.o"
            result = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                 "-c", str(ROOT / "src/gameplay_stage_stadium.c"),
                 "-o", str(decoder_object)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            binary = directory / "stadium_yakumono_decoder_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "native_dat")]
            result = subprocess.run(
                [cxx, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror",
                 "-O1", *includes, *map(str, sources),
                 str(ROOT / "tests/gameplay_stage_stadium_yakumono_trace.cpp"),
                 str(decoder_object), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("exact 0x54-byte source ABI", result.stdout)

    def test_pinned_source_layout_matches_portable_header(self):
        cc = shutil.which("clang") or shutil.which("cc")
        if not cc:
            self.skipTest("A C compiler is required")
        source_path = ROOT / ".deps/melee/src/melee/gr/grpstadium.c"
        if not source_path.is_file():
            self.skipTest("Bootstrap the pinned Melee source for its ABI check")
        source = source_path.read_text()
        match = re.search(
            r"static\s+struct\s+grPStadium_YakumonoParam\s*\{(?P<body>.*?)\}"
            r"\s*\*\s*yakumono_param\s*;", source, re.S)
        self.assertIsNotNone(match,
                             "pinned grPStadium yakumono struct was not found")
        body = match.group("body")
        actual_fields = []
        for match in re.finditer(r"\b(int|u8|u32|s16)\s+([^;]+);", body):
            source_type, names = match.groups()
            actual_fields.extend((source_type, name.strip())
                                 for name in names.split(","))
        expected_fields = (
            [("int", field) for field in
             ("x0", "x4", "x8", "xC", "x10", "x14", "x18")] +
            [("u8", field) for field in ("r", "g", "b")] +
            [("u32", field) for field in
             ("x20", "x24", "x28", "x2C", "x30", "x34", "x38", "x3C",
              "x40", "x44")] +
            [("s16", field) for field in ("x48", "x4A", "x4C", "x4E", "x50")]
        )
        self.assertEqual(actual_fields, expected_fields,
                         "pinned Stadium field order, names or types changed")

        type_names = {
            "int": "int32_t", "u8": "uint8_t", "u32": "uint32_t",
            "s16": "int16_t",
        }
        portable_body = body
        for source_type, portable_type in type_names.items():
            portable_body = re.sub(rf"\b{source_type}\b", portable_type,
                                   portable_body)
        pinned = ("struct MeleeWebPinnedStadiumYakumono {" + portable_body +
                  "};")
        assertions = [
            "_Static_assert(sizeof(struct MeleeWebPinnedStadiumYakumono) == 0x54,"
            ' "pinned Stadium yakumono ABI size changed");',
            "_Static_assert(sizeof(MeleeWebStadiumYakumono) == 0x54,"
            ' "portable Stadium yakumono ABI size changed");',
            "_Static_assert(offsetof(MeleeWebStadiumYakumono, _rgb_padding) == 0x1f,"
            ' "portable RGB pad moved");',
            "_Static_assert(offsetof(MeleeWebStadiumYakumono, _final_padding) == 0x52,"
            ' "portable final pad moved");',
        ]
        for source_type, field in expected_fields:
            portable_type = type_names[source_type]
            assertions.append(
                f"_Static_assert(offsetof(struct MeleeWebPinnedStadiumYakumono, {field}) == "
                f"offsetof(MeleeWebStadiumYakumono, {field}), "
                f'"source/header offset mismatch at {field}");')
            for owner in ("struct MeleeWebPinnedStadiumYakumono",
                          "MeleeWebStadiumYakumono"):
                assertions.append(
                    f"_Static_assert(_Generic((({owner}*)0)->{field}, "
                    f"{portable_type}: 1, default: 0), "
                    f'"source/header scalar type mismatch at {field}");')
        probe = "\n".join([
            "#include <stddef.h>", "#include <stdint.h>",
            '#include "gameplay_stage_stadium.h"', pinned, *assertions,
            "int main(void) { return 0; }", "",
        ])
        with tempfile.TemporaryDirectory(prefix="melee Stadium source ABI ") as directory:
            directory = Path(directory)
            probe_source = directory / "stadium_source_abi.c"
            probe_source.write_text(probe)
            result = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 *["-I", str(ROOT / "src")], "-c", str(probe_source),
                 "-o", str(directory / "stadium_source_abi.o")],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
