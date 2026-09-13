"""Exercise the patched Aurora SQLite transaction wrapper against host SQLite."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


MINIMAL_INTERNAL = """
#pragma once
#include <utility>
#include <type_traits>
namespace aurora {
struct Module {
  const char* name;
  template <typename... Args>
  void error(const char*, Args&&...) const noexcept {}
};
}
"""


class AuroraSqliteTransactionTests(unittest.TestCase):
    def test_commit_failure_and_rollback_semantics(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
        aurora = ROOT / ".deps" / "aurora"
        source_header = aurora / "lib" / "sqlite_utils.hpp"
        self.assertTrue(source_header.is_file(), "Apply the pinned Aurora patch before running tests")
        with tempfile.TemporaryDirectory(prefix="aurora-sqlite-transaction-") as directory:
            directory = Path(directory)
            shutil.copy2(source_header, directory / "sqlite_utils.hpp")
            (directory / "internal.hpp").write_text(MINIMAL_INTERNAL, encoding="utf-8")
            binary = directory / "transaction-test"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(directory),
                    str(ROOT / "tests" / "aurora_sqlite_transaction_test.cpp"),
                    "-lsqlite3",
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
