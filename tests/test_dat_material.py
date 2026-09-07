"""Exercise original material metadata preservation and unsupported boundaries."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DatMaterialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for material reader tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee material tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_material_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_texture.cpp"), str(ROOT / "src/dat_material.cpp"),
             str(ROOT / "tests/dat_material_test.cpp"), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(f"Material test compilation failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_rgba_colors_floats_and_supported_render_bits_remain_original(self):
        self.run_case("preserves_material_inputs")

    def test_custom_state_and_absent_rendering_services_fail_explicitly(self):
        self.run_case("unsupported_material_services")

    def test_opaque_alpha_and_finite_bounded_shininess(self):
        self.run_case("finite_material_parameters")

    def test_required_material_pointer_alignment_and_referenced_region_bounds(self):
        self.run_case("material_pointer_bounds")


if __name__ == "__main__":
    unittest.main()
