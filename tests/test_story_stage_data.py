"""Validate Yoshi's Story metadata against the owned revision 2 archive."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GRST_SHA256 = "1ef0ccc51fc69bf2e06f55377111ec1b67e00438df0597bf6195c3032ae29f83"


class StoryStageDataTests(unittest.TestCase):
    def test_exact_archive_services_and_yakumono(self):
        asset = ROOT / "assets-local/next-gate/GrSt.dat"
        if not asset.is_file():
            self.skipTest("Owned Yoshi's Story revision 2 asset is absent")
        self.assertEqual(hashlib.sha256(asset.read_bytes()).hexdigest(), GRST_SHA256)
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            self.skipTest("C and C++20 compilers are required")
        with tempfile.TemporaryDirectory(prefix="melee Yoshi Story data ") as directory:
            directory = Path(directory)
            story_object = directory / "gameplay_stage_story.o"
            includes = ["-I", str(ROOT / "src"),
                        "-I", str(ROOT / ".deps/aurora/include"),
                        "-I", str(ROOT / ".deps/melee/src")]
            result = subprocess.run(
                [cc, "-std=c11", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                 *includes, "-c", str(ROOT / "src/gameplay_stage_story.c"),
                 "-o", str(story_object)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            binary = directory / "story_stage_data_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_stage", "dat_collision", "dat_lights", "native_dat")]
            result = subprocess.run(
                [cxx, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-ffunction-sections", "-fdata-sections", *includes, *map(str, sources),
                 str(ROOT / "tests/story_stage_data_trace.cpp"), str(story_object),
                 "-Wl,-dead_strip", "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), str(asset)], capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("entries=4 vertices=34 lines=29 joints=2 lights=2", result.stdout)


if __name__ == "__main__":
    unittest.main()
