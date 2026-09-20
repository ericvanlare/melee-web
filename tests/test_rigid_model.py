"""Validate the real C++ model importer without proprietary game fixtures."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RigidModelTests(unittest.TestCase):
    def test_signed_byte_position_normal_scales_and_bounds(self):
        self.run_case("signed_byte_geometry")

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

    def run_case(self, name, *arguments):
        result = subprocess.run([str(self.binary), name, *map(str, arguments)],
                                capture_output=True, text=True, timeout=20)
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

    def test_shape_geometry_and_logical_display_bounds(self):
        self.run_case("shape_geometry_and_logical_bounds")

    def test_reflection_normal_and_actual_uv_source_requirements(self):
        self.run_case("material_vertex_dependencies")

    def test_descriptor_order_types_stride_scale_and_termination(self):
        self.run_case("descriptor_formats")

    def test_nonfinite_and_unbounded_position_and_normal_geometry(self):
        self.run_case("finite_geometry")

    def test_direct_rgba8_packet_layout_and_original_vertex_material_requirements(self):
        self.run_case("direct_rgba8_geometry")

    def test_native_indexed_rgba8_array_bounds_and_viewer_boundary(self):
        self.run_case("indexed_rgba8_geometry")

    def test_native_indexed_rgb565_index16_width_and_source_count(self):
        self.run_case("indexed_rgb565_index16_geometry")

    def test_real_captain_six_costume_native_packed_color_construction(self):
        directory = ROOT / "assets-local/full-game-captain"
        required = [
            directory / name for name in
            ("PlCaNr.dat", "PlCaGy.dat", "PlCaRe.usd", "PlCaWh.dat",
             "PlCaGr.dat", "PlCaBu.dat")
        ]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Captain model fixtures are required")
        self.run_case("real_captain", directory)

    def test_native_interleaved_nbt_and_cull_flags_preserve_source_metadata(self):
        self.run_case("native_nbt_geometry_and_cull")

    def test_explicit_opaque_pass_counts_source_identity_and_unused_billboard_pruning(self):
        self.run_case("explicit_opaque_pass")

    def test_opaque_pass_retains_envelope_bones_and_unsupported_dependency_rejections(self):
        self.run_case("opaque_envelope_dependency_closure")

    def test_absent_root_or_required_model_content(self):
        self.run_case("missing_model_content")

    def test_skin_joint_mapping_raw_weights_inverse_binds_and_per_palette_bounds(self):
        self.run_case("skin_metadata_and_bounds")

    def test_inverse_bind_semantic_requirements_and_matrix_bounds(self):
        self.run_case("inverse_bind_requirements")

    def test_envelope_pointer_termination_weights_and_joint_membership(self):
        self.run_case("envelope_reference_validation")

    def test_original_hsd_palette_and_influence_resource_limits(self):
        self.run_case("envelope_resource_limits")

    def test_direct_position_texture_matrix_indices_and_packet_layout(self):
        self.run_case("matrix_index_validation")

    def test_lit_envelope_owner_has_original_normal_palette_upload_flag(self):
        self.run_case("envelope_lighting_contract")

    def test_dobj_preorder_identity_with_multiple_and_shared_polygon_lists(self):
        self.run_case("dobj_preorder_mapping")

    def test_active_uv_matrix_indices_cannot_consume_unloaded_palette_state(self):
        self.run_case("active_texture_matrix_contract")


if __name__ == "__main__":
    unittest.main()
