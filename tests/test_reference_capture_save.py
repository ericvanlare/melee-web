import hashlib
import unittest

from tools.reference_capture_save import (
    CARD_BLOCK_BYTES, GCI_HEADER_BYTES, decode_block, encode_block,
    unlock_roster_gci,
)


class ReferenceCaptureSaveTests(unittest.TestCase):
    def test_original_hsd_encoder_known_answers(self):
        # Produced by compiling the pinned original hsd_3B2B/hsd_3B2E routines.
        vectors = {
            32: "87f0709aad0c2dc4f3e04737fbd0df300953c3b98d2e3f4ba95d833bd82708d3",
            400: "c0bfccf9f65ccc332497cafb079966e385d00d591aabdfe0c900af0969d3e7ba",
            8192: "6a667e597e5825371dfcb0949d80d9ca0b1f04c1ed5ca45aab4f359736c74145",
        }
        for size, expected in vectors.items():
            with self.subTest(size=size):
                plain = bytes((i * 59 + 17) % 256 for i in range(size))
                encoded = encode_block(plain)
                self.assertEqual(hashlib.sha256(encoded).hexdigest(), expected)
                self.assertEqual(decode_block(encoded)[16:], plain[16:])
                self.assertEqual(encode_block(decode_block(encoded)), encoded)

    def fixture(self, identity=1):
        gci = bytearray(b"\xa5" * (GCI_HEADER_BYTES + 11 * CARD_BLOCK_BYTES))
        gci[:6] = b"GALE01"
        gci[8:40] = b"SuperSmashBros0110290334".ljust(32, b"\0")
        gci[56:58] = (11).to_bytes(2, "big")
        plain = bytearray((i * 13) % 256 for i in range(CARD_BLOCK_BYTES))
        plain[16:18] = identity.to_bytes(2, "big")
        plain[32:36] = bytes.fromhex("802401c0")
        start = GCI_HEADER_BYTES + CARD_BLOCK_BYTES
        gci[start:start + CARD_BLOCK_BYTES] = encode_block(plain)
        return bytes(gci)

    def test_unlock_preserves_stages_other_data_and_unused_blocks(self):
        original = self.fixture()
        updated = unlock_roster_gci(original)
        start = GCI_HEADER_BYTES + CARD_BLOCK_BYTES
        end = start + CARD_BLOCK_BYTES
        self.assertEqual(updated[:start], original[:start])
        self.assertEqual(updated[end:], original[end:])
        before, after = decode_block(original[start:end]), decode_block(updated[start:end])
        self.assertEqual(after[32:36], bytes.fromhex("87ff01c0"))
        self.assertEqual(after[16:32], before[16:32])
        self.assertEqual(after[34:], before[34:])
        self.assertEqual(unlock_roster_gci(updated), updated)

    def test_rejects_wrong_layout_identity_and_corruption(self):
        original = self.fixture()
        for bad in (original[:-1], b"PAL!!!" + original[6:], self.fixture(identity=2)):
            with self.subTest(prefix=bad[:6], size=len(bad)):
                with self.assertRaises(ValueError):
                    unlock_roster_gci(bad)
        corrupt = bytearray(original)
        corrupt[GCI_HEADER_BYTES + CARD_BLOCK_BYTES + 16] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            unlock_roster_gci(corrupt)
        for codec in (decode_block, encode_block):
            with self.assertRaisesRegex(ValueError, "Truncated"):
                codec(b"\0" * 15)
