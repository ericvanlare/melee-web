"""Exercise the bounded native preparation registry without a GPU device."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
AURORA = ROOT / ".deps/aurora"


class AuroraPipelinePreparationTests(unittest.TestCase):
    def setUp(self):
        self.compiler = shutil.which("clang++") or shutil.which("c++")
        if not self.compiler:
            self.skipTest("C++ compiler unavailable")
        self.assertTrue((AURORA / "include/aurora/pipeline_prepare.h").is_file(),
                        "Apply the pinned Aurora patch before running tests")

    def compile(self, source, binary, *flags):
        result = subprocess.run([
            self.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
            "-DMELEE_WEB_SELECTIVE_PIPELINES", "-I", str(AURORA / "include"),
            "-I", str(AURORA / "lib/gfx"), *flags, str(source), "-o", str(binary),
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_registry_selection_atomicity_bounds_and_reinitialization(self):
        with tempfile.TemporaryDirectory(prefix="pipeline-prepare-") as directory:
            for flags in ([], ["-DMELEE_WEB_PUBLIC_RUNTIME"]):
                with self.subTest(flags=flags):
                    binary = Path(directory) / "registry-test"
                    self.compile(AURORA / "tests/pipeline_prepare_state_test.cpp", binary, *flags)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_digest_matches_independent_hashlib_padding_and_config_vectors(self):
        with tempfile.TemporaryDirectory(prefix="pipeline-prepare-sha-") as directory:
            source = Path(directory) / "hash.cpp"
            source.write_text('''
#include "pipeline_prepare_state.hpp"
#include <cstdio>
#include <vector>
int main() {
  for (size_t size : {0u, 55u, 56u, 64u, 4176u}) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i * 17 + 3);
    for (auto byte : aurora::gfx::selective::sha256(bytes.data(), bytes.size()))
      std::printf("%02x", byte);
    std::printf("\\n");
  }
}
''')
            binary = Path(directory) / "hash-test"
            self.compile(source, binary)
            output = subprocess.check_output([str(binary)], text=True).splitlines()
            expected = [hashlib.sha256(bytes((i * 17 + 3) & 255 for i in range(size))).hexdigest()
                        for size in (0, 55, 56, 64, 4176)]
            self.assertEqual(output, expected)


if __name__ == "__main__":
    unittest.main()
