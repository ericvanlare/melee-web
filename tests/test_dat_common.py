"""Check typed common scalars, root readiness and strict source-schema drift."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("common_schema", ROOT / "scripts/generate_common_schema.py")
SCHEMA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCHEMA)


class DatCommonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for common-data tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee common tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_common_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_common.cpp"), str(ROOT / "tests/dat_common_test.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"Common test compilation failed:\n{result.stdout}{result.stderr}")

    def test_authored_mixed_types_inventory_and_unsupported_boundaries(self):
        for case in ("typed_fields_and_lifetime", "root_identity_and_readiness",
                     "pointer_and_layout_bounds", "internal_relocations_and_nonfinite"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_schema_matches_pinned_source(self):
        if not (ROOT / ".deps/melee/src").is_dir():
            self.skipTest("Pinned Melee source is unavailable; run dependency bootstrap")
        self.assertEqual(SCHEMA.generate(ROOT / ".deps/melee"), (ROOT / "src/common_schema.h").read_text())

    def test_schema_grammar_rejects_unhandled_layouts(self):
        self.assertEqual(SCHEMA.array_count("0x6F0 - 0x6EC"), 4)
        self.assertEqual(SCHEMA.fields("/* +0 */ float value;\n/* +4 */ GXColor colors[4];"),
                         [(0, "float", "value", 1, False), (4, "GXColor", "colors", 4, True)])
        for text in ("/* +0 */ float* pointer;", "/* +0 */ int bits:3;", "float missing_offset;",
                     "/* +0 */ union { int x; float y; } value;"):
            with self.subTest(text=text), self.assertRaises(SCHEMA.SchemaError):
                SCHEMA.fields(text)
        for text in ("COUNT", "2*4", "010", "0", "65", "-1"):
            with self.subTest(text=text), self.assertRaises(SCHEMA.SchemaError):
                SCHEMA.array_count(text)

    def test_color_alignment_follows_byte_members(self):
        # An authored byte prefix makes GXColor's alignment observable. Float
        # groups retain the required overall scalar size without source data.
        with tempfile.TemporaryDirectory(prefix="common schema alignment ") as temporary:
            melee = Path(temporary)
            ft = melee / "src/melee/ft/types.h"
            lb = melee / "src/melee/lb/forward.h"
            ft.parent.mkdir(parents=True)
            lb.parent.mkdir(parents=True)
            fields = ["/* +0 */ u8 prefix;", "/* +1 */ GXColor color;",
                      "/* +5 */ u8 padding[3];"]
            fields += [f"/* +{8 + i * 256:X} */ float group{i}[64];" for i in range(8)]
            fields += ["/* +808 */ float tail[4];"]
            source = "struct ftCommonData {\n" + "\n".join(fields) + "\n};"
            ft.write_text(source)
            lb.write_text("typedef struct lbColl_80008D30_arg1 { /* +0 */ u32 value; };")
            (ft.parent / "fighter.c").write_text("void Fighter_LoadCommonData(void) {\n" +
                "\n".join(f"root{i} = pData[{i}];" for i in range(23)) + "\n}")
            self.assertIn("X(0x001, BYTE, color.r)", SCHEMA.generate(melee))
            ft.write_text(source.replace("/* +1 */ GXColor", "/* +4 */ GXColor"))
            with self.assertRaisesRegex(SCHEMA.SchemaError, "Source offset differs"):
                SCHEMA.generate(melee)


if __name__ == "__main__":
    unittest.main()
