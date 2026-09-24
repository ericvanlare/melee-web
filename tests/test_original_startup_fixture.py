import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from original_startup_fixture import fixture_arguments


def context():
    return {
        "schema": "melee-web-original-boot-context",
        "version": 1,
        "derivation": "owned_disc_apploader_and_dol_no_capture_inputs",
        "root": {"arena_hi": 0x817F8000, "heap_max_num": 4,
                 "arena_lo": 0x8065CC00, "audio_heap_size": 0x80000,
                 "aram_base": 0x4000,
                 "aram_size": 0x1000000},
        "boot": {"memory_size": 0x01800000, "arena_hi": 0x817F8000,
                 "aram_size": 0x1000000},
        "stages": {
            "os_arena_lo": 0x804EEC00, "crash_allocation_size": 0x2000,
            "crash_allocation_alignment": 4, "crash_allocation_base": 0x804EEC00,
            "after_crash": 0x804F0C00, "framebuffer_count": 2,
            "framebuffer_size": 0x96000, "framebuffer_width": 640,
            "framebuffer_height": 480, "framebuffer_begin": 0x804F0C00,
            "after_xfb": 0x8061CC00, "fifo_size": 0x40000,
            "os_init_alloc_lo": 0x8065CC00,
        },
    }


class OriginalStartupFixtureContextTests(unittest.TestCase):
    def test_arguments_preserve_independent_boot_relations(self):
        args = fixture_arguments(context())
        self.assertIn("--xfb_size", args)
        self.assertEqual(args[args.index("--xfb_count") + 1], "2")

    def test_rejects_capture_shaped_context(self):
        value = context()
        value["allocations"] = []
        with self.assertRaisesRegex(ValueError, "captures"):
            fixture_arguments(value)

    def test_real_runner_requires_source_identity(self):
        with self.assertRaisesRegex(ValueError, "source identities"):
            fixture_arguments(context(), require_identity=True)

    def test_rejects_typed_boolean_and_bad_span(self):
        value = context()
        value["root"]["heap_max_num"] = True
        with self.assertRaisesRegex(ValueError, "uint32"):
            fixture_arguments(value)
        value = context()
        value["stages"]["after_xfb"] += 32
        with self.assertRaisesRegex(ValueError, "XFB span"):
            fixture_arguments(value)

    def test_rejects_duplicate_boot_fields_that_disagree(self):
        value = context()
        value["boot"]["arena_hi"] += 32
        with self.assertRaisesRegex(ValueError, "duplicated boot arena"):
            fixture_arguments(value)

        value = context()
        value["root"]["arena_lo"] += 32
        with self.assertRaisesRegex(ValueError, "duplicated boot arena"):
            fixture_arguments(value)

        value = context()
        value["boot"]["aram_size"] += 32
        with self.assertRaisesRegex(ValueError, "duplicated boot arena"):
            fixture_arguments(value)

    def test_rejects_arena_heap_span_before_runner(self):
        value = context()
        value["root"]["audio_heap_size"] = 0x7F000000
        with self.assertRaisesRegex(ValueError, "heap descriptors"):
            fixture_arguments(value)

        value = context()
        value["stages"]["fifo_size"] = 0
        with self.assertRaisesRegex(ValueError, "FIFO end"):
            fixture_arguments(value)

        value = context()
        value["root"]["audio_heap_size"] = 1
        with self.assertRaisesRegex(ValueError, "heap descriptors"):
            fixture_arguments(value)

    def test_rejects_arena_outside_mem1_and_invalid_geometry(self):
        value = context()
        value["root"]["arena_hi"] = 0x81800020
        value["boot"]["arena_hi"] = 0x81800020
        with self.assertRaisesRegex(ValueError, "outside the fresh MEM1"):
            fixture_arguments(value)

        value = context()
        value["stages"]["framebuffer_width"] = 0x10000
        with self.assertRaisesRegex(ValueError, "XFB geometry"):
            fixture_arguments(value)

    def test_requires_pinned_identity_formats(self):
        value = context()
        value["identities"] = {
            "dol_sha256": "x", "disc_boot_header_sha256": "x",
            "apploader_sha256": "x", "symbols_sha256": "x",
            "bi2_sha256": "x", "dolphin_boot_source_sha256": "x",
            "source_revision": "x", "dolphin_revision": "x",
            "source_sha256": {"src/example.c": "x"},
        }
        with self.assertRaisesRegex(ValueError, "identities.dol_sha256"):
            fixture_arguments(value, require_identity=True)

    def test_rejects_unpinned_source_revision(self):
        value = context()
        digest = "0" * 64
        value["identities"] = {
            "dol_sha256": digest, "disc_boot_header_sha256": digest,
            "apploader_sha256": digest, "symbols_sha256": digest,
            "bi2_sha256": digest, "dolphin_boot_source_sha256": digest,
            "source_revision": "0" * 40, "dolphin_revision": "0" * 40,
            "source_sha256": {"src/example.c": digest},
        }
        with self.assertRaisesRegex(ValueError, "unpinned source revision"):
            fixture_arguments(value, require_identity=True)


if __name__ == "__main__":
    unittest.main()
