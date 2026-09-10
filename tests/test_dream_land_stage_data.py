"""Validate Dream Land against the owned GALE01 revision-2 archive."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GROP_SHA256 = "44ef32a76216c3f47b79c430167e915953c3da0664f121c270be11fd5af2574d"

class DreamLandStageDataTests(unittest.TestCase):
    def test_exact_archive_services_and_scheduler_data(self):
        asset = ROOT / "assets-local/next-gate/GrOp.dat"
        if not asset.is_file():
            self.skipTest("Owned Dream Land revision-2 asset is absent")
        self.assertEqual(hashlib.sha256(asset.read_bytes()).hexdigest(), GROP_SHA256)
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not cc or not cxx:
            self.skipTest("C and C++20 compilers are required")
        with tempfile.TemporaryDirectory(prefix="melee Dream Land data ") as directory:
            directory = Path(directory)
            dream_object = directory / "gameplay_stage_dream_land.o"
            includes = ["-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
                        "-I", str(ROOT / ".deps/melee/src")]
            result = subprocess.run(
                [cc, "-std=c11", "-DTARGET_PC", *includes, "-c",
                 str(ROOT / "src/gameplay_stage_dream_land.c"), "-o", str(dream_object)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            binary = directory / "dream_land_stage_data_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_stage", "dat_collision", "dat_lights", "native_dat")]
            result = subprocess.run(
                [cxx, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror", "-O1",
                 *includes, *map(str, sources), str(ROOT / "tests/dream_land_stage_data_trace.cpp"),
                 str(dream_object), "-o", str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), str(asset)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("exact eight maps", result.stdout)


if __name__ == "__main__":
    unittest.main()
