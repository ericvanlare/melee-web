"""Focused tests for source-shaped asynchronous handle compaction."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SourceHandleAsyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler:
            raise unittest.SkipTest("native C++ compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory(prefix="source handle async ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "source-handle-async"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-Isrc",
            "src/source_handle_context.cpp",
            "tests/source_handle_async_test.cpp",
            "-o",
            str(cls.binary),
        ]
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, timeout=30)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_async_source_ordering_and_rejections(self):
        result = subprocess.run([str(self.binary)], cwd=ROOT,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
