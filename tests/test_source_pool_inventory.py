import tempfile
from pathlib import Path
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.source_pool_inventory import (
    build_pool_inventory,
    validate_observed_pool_addresses,
)


class SourcePoolInventoryTests(unittest.TestCase):
    def _fixture(self):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name) / "src"
        (root / "sysdolphin/baselib").mkdir(parents=True)
        (root / "melee").mkdir()
        (root / "sysdolphin/baselib/objalloc.h").write_text(
            "typedef struct _HSD_ObjAllocData HSD_ObjAllocData;\n"
            "ASSERT_SIZE(struct _HSD_ObjAllocData, 0x2C);\n"
        )
        (root / "sysdolphin/baselib/generator.h").write_text(
            "struct hsd_804D0F60_t {\n"
            "    HSD_ObjAllocData alloc_data;\n"
            "    void* pad;\n"
            "};\n"
        )
        (root / "sysdolphin/baselib/pools.c").write_text(
            "#include \"generator.h\"\n"
            "HSD_ObjAllocData direct_pool;\n"
            "static HSD_ObjAllocData static_pool;\n"
            "struct hsd_804D0F60_t wrapped_pool;\n"
            "extern HSD_ObjAllocData declaration_only;\n"
            "void local(void) { HSD_ObjAllocData local_pool; }\n"
        )
        (root / "melee/unrelated.c").write_text(
            "struct unrelated { unsigned char bytes[44]; };\n"
            "struct unrelated same_sized_object;\n"
        )
        symbols = Path(temp.name) / "symbols.txt"
        symbols.write_text(
            "direct_pool = .bss:0x80401000; // type:object size:0x2C scope:global\n"
            "static_pool = .bss:0x8040102C; // type:object size:0x2C scope:local\n"
            "wrapped_pool = .bss:0x80401058; // type:object size:0x30 scope:global data:byte\n"
            "declaration_only = .bss:0x80401088; // type:object size:0x2C scope:global\n"
            "same_sized_object = .bss:0x804010B4; // type:object size:0x2C scope:global\n"
        )
        return temp, root, symbols

    def test_source_declarations_accept_direct_and_padded_wrapper(self):
        temp, root, symbols = self._fixture()
        self.addCleanup(temp.cleanup)
        inventory = build_pool_inventory(root, symbols)
        pools = inventory["pools"]
        self.assertEqual(pools["direct_pool"]["data_size"], 0x2C)
        self.assertEqual(pools["static_pool"]["storage_size"], 0x2C)
        self.assertEqual(pools["wrapped_pool"]["storage_size"], 0x30)
        self.assertEqual(pools["wrapped_pool"]["data_offset"], 0)
        self.assertNotIn("declaration_only", pools)
        self.assertNotIn("same_sized_object", pools)
        self.assertEqual(inventory["object_size"], 0x2C)
        source_paths = {item["path"] for item in inventory["source_files"]}
        self.assertIn("sysdolphin/baselib/objalloc.h", source_paths)
        self.assertIn("sysdolphin/baselib/generator.h", source_paths)
        self.assertIn("sysdolphin/baselib/pools.c", source_paths)

    def test_unknown_observation_cannot_extend_inventory(self):
        temp, root, symbols = self._fixture()
        self.addCleanup(temp.cleanup)
        inventory = build_pool_inventory(root, symbols)
        addresses = {row["address"] for row in inventory["pools"].values()}
        validate_observed_pool_addresses(addresses, inventory)
        with self.assertRaisesRegex(ValueError, "absent from source inventory"):
            validate_observed_pool_addresses((*addresses, 0x80409999), inventory)
        self.assertEqual(len(inventory["pools"]), 3)

    def test_wrapper_without_room_for_allocator_is_rejected(self):
        temp, root, symbols = self._fixture()
        self.addCleanup(temp.cleanup)
        text = symbols.read_text().replace(
            "wrapped_pool = .bss:0x80401058; // type:object size:0x30",
            "wrapped_pool = .bss:0x80401058; // type:object size:0x28",
        )
        symbols.write_text(text)
        with self.assertRaisesRegex(ValueError, "invalid allocator storage extent"):
            build_pool_inventory(root, symbols)

    def test_changed_authoritative_size_assertion_is_rejected(self):
        temp, root, symbols = self._fixture()
        self.addCleanup(temp.cleanup)
        header = root / "sysdolphin/baselib/objalloc.h"
        header.write_text(header.read_text().replace("0x2C", "0x30"))
        with self.assertRaisesRegex(ValueError, "size assertion"):
            build_pool_inventory(root, symbols)

    def test_storage_span_cannot_wrap_source_address_space(self):
        temp, root, symbols = self._fixture()
        self.addCleanup(temp.cleanup)
        symbols.write_text(symbols.read_text().replace('0x80401058', '0xFFFFFFF0'))
        with self.assertRaisesRegex(ValueError, 'storage extent'):
            build_pool_inventory(root, symbols)


if __name__ == "__main__":
    unittest.main()
