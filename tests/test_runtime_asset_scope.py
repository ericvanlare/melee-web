import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeAssetScopeTests(unittest.TestCase):
    def test_transactional_native_scope_ledger(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee runtime asset scope ") as directory:
            binary = Path(directory) / "runtime_asset_scope_test"
            headers = Path(directory) / "include/dolphin"
            headers.mkdir(parents=True)
            # This ledger-only test needs PADStatus as an opaque declaration;
            # the full Dolphin header also declares C math functions that
            # conflict with the host C++ standard library.
            (headers / "pad.h").write_text("typedef struct PADStatus PADStatus;\n")
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-DMELEE_WEB_RUNTIME_ASSET_SCOPE_TESTING", "-I", str(Path(directory) / "include"), "-Isrc",
                 "tests/runtime_asset_scope_test.cpp", "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
