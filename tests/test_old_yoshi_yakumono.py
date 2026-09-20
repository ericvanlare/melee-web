"""Validate the checked Old Yoshi yakumono ABI against source and GrOy.dat."""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GROY_SHA256 = "31338847be3e4d0ed52d8c404d2b750369a4cb76e31a7217071ef186283d8d8c"


class OldYoshiYakumonoTests(unittest.TestCase):
    def _compilers(self):
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            self.skipTest("C and C++20 compilers are required")
        return cc, cxx

    def test_source_abi_and_real_archive(self):
        asset = ROOT / "assets-local/full-game-stage-yoshis-island-64/GrOy.dat"
        if not asset.is_file():
            self.skipTest("Owned Yoshi's Island 64 GrOy.dat is absent")
        self.assertEqual(hashlib.sha256(asset.read_bytes()).hexdigest(), GROY_SHA256)
        cc, cxx = self._compilers()
        includes = ["-I", str(ROOT / "src"),
                    "-I", str(ROOT / ".deps/aurora/include"),
                    "-I", str(ROOT / ".deps/melee/src")]
        with tempfile.TemporaryDirectory(prefix="melee Old Yoshi yakumono ") as directory:
            directory = Path(directory)
            decoder_object = directory / "gameplay_stage_old_yoshi.o"
            result = subprocess.run(
                [cc, "-std=c11", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror",
                 *includes, "-c", str(ROOT / "src/gameplay_stage_old_yoshi.c"),
                 "-o", str(decoder_object)], capture_output=True, text=True,
                timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            source_path = ROOT / ".deps/melee/src/melee/gr/groldyoshi.c"
            if not source_path.is_file():
                self.skipTest("Bootstrap the pinned Melee source for its ABI check")
            source = source_path.read_text()
            match = re.search(
                r"static\s+struct\s*\{(?P<body>.*?)\}\s*\*\s*yakumono_param\s*;",
                source, re.S)
            self.assertIsNotNone(match,
                                 "pinned grOldYoshi yakumono struct was not found")
            body = match.group("body")
            fields = ["x0", "x2", "x4", "x8", "xC", "x10", "x12",
                      "x14", "x16", "x18"]
            self.assertEqual(
                re.findall(r"\b(?:s16|float)\s+(x[0-9A-F]+)\s*;", body),
                fields, "pinned source yakumono field order changed")
            source_struct = "struct MeleeWebPinnedOldYoshi {" + \
                body.replace("s16", "int16_t") + "};"
            assertions = [
                "_Static_assert(sizeof(struct MeleeWebPinnedOldYoshi) == 0x1c,"
                ' "pinned source yakumono size changed");',
                "_Static_assert(sizeof(MeleeWebOldYoshiYakumono) == 0x1c,"
                ' "portable Old Yoshi yakumono size changed");',
            ]
            for field in fields:
                scalar = "int16_t" if field in {"x0", "x2", "x10", "x12", "x14", "x16", "x18"} else "float"
                assertions.append(
                    f"_Static_assert(offsetof(struct MeleeWebPinnedOldYoshi, {field}) == "
                    f"offsetof(MeleeWebOldYoshiYakumono, {field}), "
                    f'\"source/header offset mismatch at {field}\");')
                for owner in ("struct MeleeWebPinnedOldYoshi",
                              "MeleeWebOldYoshiYakumono"):
                    assertions.append(
                        f"_Static_assert(_Generic((({owner}*)0)->{field}, "
                        f"{scalar}: 1, default: 0), "
                        f'\"source/header scalar type mismatch at {field}\");')
            probe = "\n".join([
                "#include <stddef.h>", "#include <stdint.h>",
                '#include "gameplay_stage_old_yoshi.h"', source_struct,
                *assertions, "int main(void) { return 0; }", "",
            ])
            probe_source = directory / "old_yoshi_source_abi.c"
            probe_source.write_text(probe)
            result = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                 "-c", str(probe_source), "-o",
                 str(directory / "old_yoshi_source_abi.o")],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            binary = directory / "old_yoshi_yakumono_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "native_dat")]
            result = subprocess.run(
                [cxx, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror",
                 "-O1", *includes, *map(str, sources),
                 str(ROOT / "tests/old_yoshi_yakumono_trace.cpp"),
                 str(decoder_object), "-o", str(binary)], capture_output=True,
                text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), str(asset)], capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("exact 28-byte yakumono ABI", result.stdout)


if __name__ == "__main__":
    unittest.main()
