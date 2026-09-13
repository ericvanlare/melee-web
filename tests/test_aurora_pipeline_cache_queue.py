"""Run the bounded Aurora pipeline-cache queue checks without a full build."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AuroraPipelineCacheQueueTests(unittest.TestCase):
    def test_queue_coalescing_bounds_and_overflow_latch(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
        aurora = ROOT / ".deps" / "aurora"
        header = aurora / "lib" / "gfx" / "pipeline_cache_queue.hpp"
        self.assertTrue(header.is_file(), "Apply the pinned Aurora patch before running tests")
        with tempfile.TemporaryDirectory(prefix="aurora-cache-queue-") as directory:
            binary = Path(directory) / "queue-test"
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(header.parent),
                    str(ROOT / "tests" / "aurora_pipeline_cache_queue_test.cpp"),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
            run_result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(run_result.returncode, 0, run_result.stdout + run_result.stderr)


if __name__ == "__main__":
    unittest.main()
