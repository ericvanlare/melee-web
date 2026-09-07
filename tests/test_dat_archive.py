"""Exercise the actual portable C++ DAT reader against synthetic archive bytes."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DatArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler (clang++ or c++) is required for DAT reader tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee dat tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_archive_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "tests/dat_archive_test.cpp"), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(f"DAT reader test compilation failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_endian_scalar_and_public_symbol_reads(self):
        self.run_case("endian_reads")

    def test_relocated_zero_null_and_required_pointer_ranges(self):
        self.run_case("pointer_semantics")

    def test_copies_input_and_remains_independent_when_copied(self):
        self.run_case("ownership")

    def test_data_ranges_reject_overflow_and_table_access(self):
        self.run_case("bounds")

    def test_header_sizes_and_every_truncated_input_prefix(self):
        self.run_case("file_and_header_sizes")

    def test_table_counts_reject_truncation_and_integer_overflow(self):
        self.run_case("table_counts")

    def test_relocation_slots_are_unique_aligned_and_contained(self):
        self.run_case("relocation_slots")

    def test_relocation_targets_stay_inside_data(self):
        self.run_case("relocation_targets")

    def test_public_targets_stay_inside_data(self):
        self.run_case("public_targets")

    def test_public_names_are_bounded_terminated_nonempty_and_unique(self):
        self.run_case("public_names")

    def test_unresolved_external_links_fail_explicitly(self):
        self.run_case("external_links")

    def test_archive_entry_and_name_resource_limits(self):
        self.run_case("resource_limits")

    def test_referenced_targets_bound_regions_without_claiming_allocation_sizes(self):
        self.run_case("referenced_region_boundaries")


if __name__ == "__main__":
    unittest.main()
