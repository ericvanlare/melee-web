"""Check typed original collision archive layouts with authored game-free data."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatCollisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for collision reader tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee collision tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_collision_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_stage.cpp"), str(ROOT / "src/dat_collision.cpp"),
             str(ROOT / "tests/dat_collision_test.cpp"), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"Collision test compilation failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_coordinates_adjacency_flags_and_owned_lifetime(self):
        self.run_case("source_metadata")

    def test_original_capacity_and_relocated_array_bounds(self):
        self.run_case("counts_and_arrays")

    def test_index_sentinels_category_partitions_and_joint_ranges(self):
        self.run_case("indices_and_ranges")

    def test_nonfinite_coordinates_and_invalid_joint_bounds(self):
        self.run_case("finite_coordinates_and_bounds")

    def test_dynamic_ranges_and_archive_bindings_preserve_unapplied_semantics(self):
        self.run_case("dynamic_metadata_and_bindings")

    def test_named_source_stage_scale_requires_valid_scalar(self):
        self.run_case("source_stage_scale")


if __name__ == "__main__":
    unittest.main()
