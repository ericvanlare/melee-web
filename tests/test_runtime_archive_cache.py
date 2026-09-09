import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeArchiveCacheTests(unittest.TestCase):
    def test_immutable_archive_is_parsed_once_per_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "runtime_archive_cache_test"
            subprocess.run(
                [
                    "c++",
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Isrc",
                    "tests/runtime_archive_cache_test.cpp",
                    "src/runtime_archive_cache.cpp",
                    "src/dat_archive.cpp",
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
