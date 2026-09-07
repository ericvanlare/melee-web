"""Exercise the checked map_head/entry boundary without proprietary fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class DatStageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="melee stage metadata ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "dat_stage_test"
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                        "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
                        str(ROOT / "src/dat_stage.cpp"), str(ROOT / "tests/dat_stage_test.cpp"),
                        "-o", str(cls.binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    def test_explicit_root_entry_identity_zero_offsets_and_empty_graph(self):
        self.run_case("entry_identity")
    def test_unapplied_services_are_preserved_per_selected_entry(self):
        self.run_case("optional_service_references")
    def test_known_table_widths_alignment_and_referenced_region_limits(self):
        self.run_case("known_counted_regions")
    def test_opaque_service_units_are_not_falsely_inferred(self):
        self.run_case("uninterpreted_service_count")
    def test_negative_excessive_missing_misaligned_and_truncated_metadata(self):
        self.run_case("count_and_pointer_failures")
if __name__ == "__main__":
    unittest.main()
