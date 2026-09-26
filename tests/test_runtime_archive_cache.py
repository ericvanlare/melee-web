import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeArchiveCacheTests(unittest.TestCase):
    def test_immutable_archive_is_parsed_once_per_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "runtime_archive_cache_test"
            headers = Path(directory) / "include/dolphin"
            headers.mkdir(parents=True)
            # RuntimeArchiveCache never consumes controller records; the
            # production header only needs their opaque function signature.
            (headers / "pad.h").write_text("typedef struct PADStatus PADStatus;\n")
            subprocess.run(
                [
                    "c++",
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(Path(directory) / "include"),
                    "-Isrc",
                    "tests/runtime_archive_cache_test.cpp",
                    "src/runtime_archive_cache.cpp",
                    "src/dat_archive.cpp",
                    "src/dat_audio.cpp",
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
