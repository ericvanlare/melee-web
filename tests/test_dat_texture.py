"""Run the actual checked texture reader on synthetic HSD descriptors and tiles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DatTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for texture reader tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee texture tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_texture_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_texture.cpp"), str(ROOT / "tests/dat_texture_test.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(f"Texture test compilation failed:\n{result.stdout}{result.stderr}")

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cmpr_metadata_original_tiles_and_hsd_blend_endpoint(self):
        self.run_case("real_shape_metadata")

    def test_all_supported_tiled_formats_and_partial_tiles(self):
        self.run_case("tiled_format_sizes")

    def test_complete_mip_chain_sizes_and_truncated_regions(self):
        self.run_case("mip_chains")

    def test_palette_formats_counts_and_required_relationships(self):
        self.run_case("palette_formats_and_counts")

    def test_palette_indices_across_tiles_and_mips_exclude_padding(self):
        self.run_case("palette_indices")

    def test_hsd_sampler_defaults_adjustments_and_explicit_lod(self):
        self.run_case("lod_sampler")

    def test_supported_operations_and_rejection_of_unknown_semantics(self):
        self.run_case("operations_and_modes")

    def test_custom_tev_multitexture_coordinate_and_matrix_rejections(self):
        self.run_case("unsupported_graphs_and_transforms")

    def test_dimension_format_lod_and_nonfinite_rejections(self):
        self.run_case("dimensions_and_finite_values")

    def test_pointer_alignment_missing_content_and_referenced_regions(self):
        self.run_case("pointers_and_region_bounds")


if __name__ == "__main__":
    unittest.main()
