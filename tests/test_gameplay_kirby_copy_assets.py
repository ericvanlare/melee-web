"""Reduced copy-row adapter checks; no shared runtime or browser rebuild."""
import os
from pathlib import Path
import subprocess
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

    def test_owned_copy_archives_all_costumes(self):
        donors = (("Fx", "Fox", 1), ("Mr", "Mario", 0), ("Pp", "Popo", 10),
                  ("Ss", "Samus", 13), ("Ca", "Captain", 2), ("Gw", "Gamewatch", 24))
        for code, symbol, kind in donors:
            with self.subTest(donor=symbol):
                path = ROOT / f"assets-local/next-gate/PlKbCp{code}.dat"
                if not path.is_file():
                    self.skipTest(f"Owned copy archive unavailable: {path.name}")
                self.run_case(path, f"ftDataKirbyCopy{symbol}", kind)


if __name__ == "__main__":
    unittest.main()
