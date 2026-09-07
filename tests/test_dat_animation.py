"""Checked FigaTree descriptors and packed stream safety, with synthetic bytes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatAnimationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required for animation parser tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="melee animation parser ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "dat_animation_test"
        result = subprocess.run([
            compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
            "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
            str(ROOT / "src/dat_animation.cpp"), str(ROOT / "tests/dat_animation_test.cpp"),
            "-o", str(cls.binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_channels_stream_endianness_and_owned_storage(self):
        self.run_case("preserves_channels")

    def test_descriptor_pointer_node_and_frame_bounds(self):
        self.run_case("tree_bounds")

    def test_unsupported_channels_and_scalar_encodings(self):
        self.run_case("unsupported_channels_and_formats")

    def test_truncated_nonfinite_and_overflowing_packed_operands(self):
        self.run_case("malformed_operands")

    def test_fixed_point_widths_and_largest_bounded_wait(self):
        self.run_case("encoding_and_integer_edges")


if __name__ == "__main__":
    unittest.main()
