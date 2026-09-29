"""Reduced copy-row adapter checks; no shared runtime or browser rebuild."""
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayKirbyCopyAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/em++.py"
        if not compiler.is_file() or not (sdk / ".emscripten").is_file():
            raise unittest.SkipTest("Pinned Emscripten SDK unavailable")
        sys.path.insert(0, str(ROOT / "scripts"))
        from check_gameplay import node_runtime
        cls.node = node_runtime(ROOT)
        cls.temp = tempfile.TemporaryDirectory(prefix="melee-kirby-copy-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "kirby-copy.js"
        env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK=str(sdk), EMSDK_PYTHON=sys.executable)
        source_tables = Path(cls.temp.name) / "kirby-source-tables.o"
        result = subprocess.run([
            sys.executable, str(sdk / "upstream/emscripten/emcc.py"),
            "-O1", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
            "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/melee/src"),
            "-I", str(ROOT / ".deps/aurora/include"),
            "-include", str(ROOT / "src/gameplay_compat.h"),
            "-c", str(ROOT / ".deps/melee/src/melee/ft/kinds/ftKirby/ftkirbydata.c"),
            "-o", str(source_tables),
        ], cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        command = [
            sys.executable, str(compiler), "-std=c++20", "-O1", "-fexceptions",
            "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
            "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/melee/src"),
            "-I", str(ROOT / ".deps/aurora/include"),
            str(ROOT / "src/dat_archive.cpp"),
            str(ROOT / "tests/gameplay_kirby_copy_assets_test.cpp"),
            str(source_tables),
            "-sENVIRONMENT=node", "-sNODERAWFS=1", "-sEXIT_RUNTIME=1",
            "-Wl,--gc-sections", "-o", str(cls.binary),
        ]
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, timeout=120,
            env=env,
        )
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, *arguments):
        result = subprocess.run([str(self.node), str(self.binary), *map(str, arguments)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_joint_hat_uses_source_row_zero(self):
        self.run_case("joint_row_zero")

    def test_costume_parts_and_textures_use_source_fallback(self):
        self.run_case("costume_fallback")

    def test_malformed_consumed_rows_still_reject(self):
        self.run_case("malformed_consumed_rows")

    def test_source_costume_cache_rows_and_archive_manifest(self):
        self.run_case("source_costume_cache_rows")

    def test_borrowed_rows_require_empty_original_signed_loops(self):
        self.run_case("borrowed_signed_visibility_tail")

    def test_native_pobj_scalars_follow_checked_source_graph(self):
        self.run_case("native_pobj_fields")

    def test_all_source_copy_archives_all_costumes(self):
        assets = ROOT / "assets-local/next-gate"
        body = assets / "PlKb.dat"
        if not body.is_file():
            self.skipTest("Owned Kirby body archive unavailable")
        output = self.run_case("real_all_copy_archives", assets, body)
        self.assertIn("All 25 non-null source Kirby copy rows passed", output)


class KirbyVisibilityOriginalProfileTests(unittest.TestCase):
    def test_owned_original_visibility_loops_compare_signed_counts(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        for address, size, digest, compare, instruction in (
                (0x80074B6C, 0x134, "ada0a905cc355c65bc8ee58080c772cf006c948ac7f73b61ac3a899ad3792b0e",
                 0x80074C64, 0x7c180000),
                (0x80074CA0, 0xDC, "fdeaab716b218690230cb0a45cd8584b03177221c951b51d75d7833b2465bce7",
                 0x80074D44, 0x7c170000),
                (0x80074D7C, 0xDC, "7fb36b9e80f88ee59f72072c285f16b4f97ddf548c5ffe040708413cab93bc3a",
                 0x80074E20, 0x7c170000)):
            symbol = symbols[f"ftParts_{address:08X}"]
            self.assertEqual((symbol["address"], symbol["size"]), (address, size))
            self.assertEqual(hashlib.sha256(dol.read(address, size)).hexdigest(), digest)
            # lwz count; signed cmpw j,count (not cmplw); blt loop body.
            words = struct.unpack(">III", dol.read(compare - 4, 12))
            self.assertEqual(words, (0x801c0000 if address == 0x80074B6C else 0x801b0000,
                                     instruction,
                                     0x4180ff68 if address == 0x80074B6C else 0x4180ffb0))


if __name__ == "__main__":
    unittest.main()
