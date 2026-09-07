"""Check the actual command boundary with authored HSD and map_head bytes."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def stage_fixture(translucent=False):
    # Two public roots share a joint: direct joint inspection and explicit map
    # entry inspection must remain separate operations. Geometry is one indexed
    # triangle. The optional second DObj shares its PObj but has an XLU MObj.
    data = bytearray(640)
    relocs = []

    def u32(offset, value):
        struct.pack_into(">I", data, offset, value)

    def link(offset, value):
        u32(offset, value)
        relocs.append(offset)

    struct.pack_into(">9h", data, 0, -2, -1, 0, 2, -1, 0, 0, 3, 1)
    struct.pack_into(">3f", data, 64 + 32, 1, 1, 1)
    link(64 + 16, 128)
    link(128 + 8, 144)
    link(128 + 12, 188)
    u32(144 + 4, 1)  # RENDER_CONSTANT, no lighting or normal requirement.
    link(144 + 12, 168)
    data[172:176] = bytes([20, 40, 60, 255])
    struct.pack_into(">f", data, 168 + 12, 1)
    link(188 + 8, 212)
    struct.pack_into(">H", data, 188 + 14, 1)
    link(188 + 16, 288)
    struct.pack_into(">4I", data, 212, 9, 2, 1, 3)  # POS, INDEX8, XYZ, S16.
    struct.pack_into(">H", data, 212 + 18, 6)
    link(212 + 20, 0)  # A relocated zero is the vertex array, not NULL.
    u32(236, 255)
    data[288:294] = bytes([0x90, 0, 3, 0, 1, 2])
    if translucent:
        link(128 + 4, 320)
        link(320 + 8, 336)
        link(320 + 12, 188)
        u32(336 + 4, 0x60000001)
        link(336 + 12, 168)
    link(512 + 8, 560)
    u32(512 + 12, 1)
    link(560, 64)
    link(560 + 40, 624)  # Byte flags are present, but not applied by inspection.
    data[624] = 0x80
    names = b"fixture_joint\0map_head\0"
    header = struct.pack(">5I", 32 + len(data) + 4 * len(relocs) + 16 + len(names),
                         len(data), len(relocs), 2, 0) + bytes(12)
    return (header + data + b"".join(struct.pack(">I", slot) for slot in relocs)
            + struct.pack(">4I", 64, 0, 512, len(b"fixture_joint\0")) + names)


class AssetCheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="melee checker ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.binary = cls.directory / "asset_check"
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        sources = ["src/dat_archive.cpp", "src/dat_texture.cpp", "src/dat_material.cpp",
                   "src/dat_stage.cpp", "src/rigid_model.cpp", "tools/asset_check.cpp"]
        result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                                 "-I", str(ROOT / "src"), *(str(ROOT / source) for source in sources),
                                 "-o", str(cls.binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.ordinary = cls.directory / "ordinary.dat"
        cls.ordinary.write_bytes(stage_fixture())
        cls.mixed = cls.directory / "mixed.dat"
        cls.mixed.write_bytes(stage_fixture(translucent=True))

    def check_file(self, path, *args, expected=0):
        result = subprocess.run([str(self.binary), str(path), *args], capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def test_default_keeps_strict_joint_root_behavior(self):
        records = self.check_file(self.ordinary, expected=1)
        self.assertEqual([(r["root"], r["status"]) for r in records],
                         [("fixture_joint", "accepted"), ("map_head", "rejected")])
        self.assertTrue(all(r["target_type"] == "joint_root" and r["render_pass"] == "all"
                            for r in records))
        direct = self.check_file(self.ordinary, "fixture_joint")[0]
        self.assertEqual((direct["meshes"], direct["submitted_vertices"]), (1, 3))

    def test_stage_report_retains_source_and_unapplied_service(self):
        record = self.check_file(self.ordinary, "--stage-entry", "0")[0]
        self.assertEqual((record["scope"], record["target_type"], record["render_pass"]),
                         ("stage_model", "stage_entry", "all"))
        self.assertEqual((record["source_entry"], record["offset"], record["entry_offset"],
                          record["joint_offset"]), (0, 512, 560, 64))
        self.assertEqual(record["services_unapplied"], ["animation flags"])
        self.assertEqual(record["omitted_meshes"], 0)

    def test_opaque_is_explicit_and_reports_omitted_translucency(self):
        all_pass = self.check_file(self.mixed, "--stage-entry", "0", expected=1)[0]
        self.assertEqual(all_pass["render_pass"], "all")
        opaque = self.check_file(self.mixed, "--stage-entry", "0", "--opaque")[0]
        self.assertEqual((opaque["render_pass"], opaque["meshes"], opaque["omitted_dobjs"],
                          opaque["omitted_meshes"], opaque["omitted_translucent_meshes"],
                          opaque["omitted_texture_edge_meshes"]), ("opaque", 1, 1, 1, 1, 0))

    def test_bad_selection_rejects_without_falling_back_to_a_model(self):
        record = self.check_file(self.ordinary, "--stage-entry", "1", expected=1)[0]
        self.assertEqual((record["scope"], record["source_entry"], record["status"]),
                         ("stage_model", 1, "rejected"))
        missing = self.check_file(self.ordinary, "--stage-entry", "0", "--symbol", "missing", expected=1)[0]
        self.assertEqual(missing["root"], "missing")
        self.assertIn("missing", missing["reason"])

    def test_both_command_frontends_reject_ambiguous_or_invalid_modes(self):
        for args in [("--opaque",), ("--stage-entry", "-1"), ("--stage-entry", "4294967296")]:
            for command in [[str(self.binary)], [sys.executable, str(ROOT / "scripts/check_assets.py")]]:
                with self.subTest(command=command, args=args):
                    result = subprocess.run([*command, str(self.ordinary), *args], capture_output=True,
                                            text=True, timeout=20)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
