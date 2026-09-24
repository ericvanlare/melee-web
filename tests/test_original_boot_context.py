"""Test signed linker decoding and rejection of context-dependent instructions."""
import unittest
from tools.original_boot_context import address_pair, literal, calls


class OriginalBootContextTests(unittest.TestCase):
    def test_linker_pair_relocates_with_signed_low_half(self):
        self.assertEqual(address_pair(0x3c601234, 0x3863ffe0), 0x1233ffe0)
        self.assertEqual(address_pair(0x3c605678, 0x3863ffe0), 0x5677ffe0)

    def test_register_dependent_literal_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'independent'):
            literal(0x38640020, 3)
        with self.assertRaisesRegex(ValueError, 'independent'):
            literal(0x38800020, 3)

    def test_mismatched_linker_register_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'lis/addi'):
            address_pair(0x3c601234, 0x3864ffe0)

    def test_only_relative_linked_calls_are_resolved(self):
        class Executable:
            def read(self, address, size):
                # First branches back to 0x0ff0; the other two are an
                # unlinked branch and an absolute call, neither supported.
                return bytes.fromhex('4bfffff14800001048000ff3')
        self.assertEqual(calls(Executable(), {'address': 0x1000, 'size': 12}, 0xff0)[1], [0])
        self.assertEqual(calls(Executable(), {'address': 0x1000, 'size': 12}, 0x1014)[1], [])


if __name__ == '__main__':
    unittest.main()
