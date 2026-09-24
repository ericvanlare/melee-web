"""Provenance and authored stack ownership at the ARInit argument boundary."""
from pathlib import Path
import sys
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from source_ar_init_fixture import fixture_arguments, run_owned_fixture, validate_owned_stack
from test_original_startup_fixture import context as startup_context


def context():
    result = startup_context()
    # Deliberately synthetic identity, separate from the owned symbol map.
    result["static_layout"] = {"aram_stack_table": {
        "address": 0x80400000, "size": 64, "section": ".bss", "kind": "object"}}
    return result


class SourceArInitFixtureTests(unittest.TestCase):
    def test_preserves_symbol_identity_without_host_pointer_conversion(self):
        args = fixture_arguments(context())
        self.assertEqual(args[args.index("--stack-source") + 1], str(0x80400000))
        self.assertEqual(args[args.index("--stack-entries") + 1], "16")

    def test_rejects_stack_owner_outside_authored_object_boundary(self):
        for field, value in (("size", 68), ("address", True),
                             ("address", 0x804EEC00), ("address", 0x80400001),
                             ("section", ".data"), ("kind", "function")):
            with self.subTest(field=field, value=value):
                source = context()
                source["static_layout"]["aram_stack_table"][field] = value
                with self.assertRaises(ValueError):
                    fixture_arguments(source)

    def test_owned_bss_rejects_initialized_overlap_and_outside_table(self):
        table = context()["static_layout"]["aram_stack_table"]
        raw = bytearray(0xE0)
        struct.pack_into(">II", raw, 0xD8, table["address"], table["size"])
        dol = SimpleNamespace(raw=raw, sections=[])
        validate_owned_stack(dol, table)
        dol.sections = [(table["address"] + 4, 4, 0)]
        with self.assertRaisesRegex(ValueError, "overlaps"):
            validate_owned_stack(dol, table)
        dol.sections = []
        struct.pack_into(">II", raw, 0xD8, table["address"], table["size"] - 4)
        with self.assertRaisesRegex(ValueError, "outside"):
            validate_owned_stack(dol, table)

    def test_capture_context_cannot_launch_process(self):
        captured = context()
        captured["records"] = []
        with self.assertRaisesRegex(ValueError, "captures"):
            fixture_arguments(captured)
        with patch("source_ar_init_fixture.derive_boot_context", return_value=context()) as derive, \
                patch("source_ar_init_fixture.subprocess.run") as run:
            with self.assertRaisesRegex(ValueError, "source identities"):
                run_owned_fixture(Path("fixture.js"), dol_path=Path("owned.dol"),
                                  disc_path=Path("owned.iso"), symbols_path=Path("symbols.txt"),
                                  source_root=Path("source"))
            derive.assert_called_once()
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
