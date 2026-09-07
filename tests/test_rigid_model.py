"""Validate the real C++ model importer without proprietary game fixtures."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RigidModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler (clang++ or c++) is required for model tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee model tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "rigid_model_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_texture.cpp"),
             str(ROOT / "src/dat_material.cpp"),
             str(ROOT / "src/rigid_model.cpp"), str(ROOT / "tests/rigid_model_test.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(f"Model test compilation failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_valid_fixed_point_model_with_relocated_zero_array(self):
        self.run_case("valid_zero_offset_array")

    def test_position_normal_streams_and_both_index_widths(self):
        self.run_case("normals_and_index_widths")

    def test_valid_ieee_float_positions(self):
        self.run_case("valid_f32_geometry")

    def test_display_commands_vat_formats_and_padding(self):
        self.run_case("display_commands")

    def test_primitive_counts_and_supported_surface_types(self):
        self.run_case("surface_counts")

    def test_truncated_primitive_headers_and_index_payloads(self):
        self.run_case("truncated_packets")

    def test_position_and_normal_index_bounds_and_pointer_alignment(self):
        self.run_case("indexed_array_bounds")

    def test_display_object_and_polygon_cycles(self):
        self.run_case("cyclic_graphs")

    def test_joint_srt_and_mesh_bounds_remain_untransformed(self):
        self.run_case("raw_joint_srt")

    def test_sibling_parentage_and_geometry_shared_between_distinct_joints(self):
        self.run_case("joint_hierarchy")

    def test_joint_cycles_nonfinite_transforms_and_unsupported_modes(self):
        self.run_case("invalid_joint_graphs")

    def test_optional_uv_indices_spans_and_finite_components(self):
        self.run_case("indexed_uv_geometry")

    def test_unsupported_material_and_polygon_features(self):
        self.run_case("materials_and_polygon_modes")

    def test_reflection_normal_and_actual_uv_source_requirements(self):
        self.run_case("material_vertex_dependencies")

    def test_descriptor_order_types_stride_scale_and_termination(self):
        self.run_case("descriptor_formats")

    def test_nonfinite_and_unbounded_position_and_normal_geometry(self):
        self.run_case("finite_geometry")

    def test_absent_root_or_required_model_content(self):
        self.run_case("missing_model_content")


if __name__ == "__main__":
    unittest.main()
