"""Offline GALE01r2 prepared-save update using the original HSD card codec.

No game or save bytes are bundled. This module never accesses Dolphin memory.
The byte permutation and digest follow hsd_3B2E.c and hsd_3B2B.c in the pinned
source; the provisioning caller additionally pins input and output file hashes.
"""
from __future__ import annotations

KEYS = (0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E)
# Destination bit for each source bit in fn_803B2E04, selected by prev % 7.
PERMUTATIONS = (
    (0, 4, 1, 5, 2, 6, 3, 7), (3, 0, 2, 6, 5, 4, 7, 1),
    (6, 5, 0, 1, 3, 7, 2, 4), (1, 3, 7, 4, 0, 2, 5, 6),
    (7, 2, 5, 0, 6, 1, 4, 3), (5, 6, 4, 3, 7, 0, 1, 2),
    (2, 1, 6, 7, 4, 3, 0, 5),
)
GCI_HEADER_BYTES = 0x40
CARD_BLOCK_BYTES = 0x2000
GAME_DATA_BLOCK = 1
SAVE_OFFSET = 0x20
ALL_CHARACTER_BITS = (1 << 11) - 1


def card_digest(payload: bytes) -> bytes:
    state = bytearray.fromhex("0123456789ABCDEFFEDCBA9876543210")
    for i, value in enumerate(payload):
        state[i % 16] = (state[i % 16] + value) & 0xFF
    for i in range(1, 16):
        if state[i - 1] == state[i]:
            state[i] ^= 0xFF
    return bytes(state)


def decode_block(encoded: bytes) -> bytes:
    if len(encoded) < 16:
        raise ValueError("Truncated HSD card block")
    result = bytearray(encoded)
    previous = encoded[15]
    for i in range(16, len(encoded)):
        current = encoded[i]
        value = sum(((current >> destination) & 1) << source
                    for source, destination in enumerate(PERMUTATIONS[previous % 7]))
        result[i] = value ^ previous ^ KEYS[previous % 13]
        previous = current
    if card_digest(result[16:]) != result[:16]:
        raise ValueError("Invalid HSD card block checksum")
    return bytes(result)


def encode_block(decoded: bytes) -> bytes:
    if len(decoded) < 16:
        raise ValueError("Truncated HSD card block")
    result = bytearray(decoded)
    result[:16] = card_digest(result[16:])
    for i in range(16, len(result)):
        previous = result[i - 1]
        value = result[i] ^ previous ^ KEYS[previous % 13]
        result[i] = sum(((value >> source) & 1) << destination
                        for source, destination in enumerate(PERMUTATIONS[previous % 7]))
    return bytes(result)


def unlock_roster_gci(gci: bytes) -> bytes:
    """Update the checked prepared fixture's GameData mask and block encoding.

    lbcardgame.c binds manifest entry 1 to gmMainLib_GetSaveData(). The HSD
    card writer fn_803B1338 places the primary in block 1 after a 0x20-byte
    protected-block header. gmmain_lib.h puts unlocked_characers_bitmask at
    save offset 0; gm_80164F18 sets bits 0..10. Stage availability at +2 and
    every other decoded byte are preserved. In this hash-pinned prepared
    fixture block 10 has no valid HSD checksum; it is preserved as-is, never
    repaired into a newly selectable copy. This is not a general save editor.
    """
    if (len(gci) != GCI_HEADER_BYTES + 11 * CARD_BLOCK_BYTES or
            gci[:6] != b"GALE01" or
            gci[8:40].rstrip(b"\0") != b"SuperSmashBros0110290334" or
            int.from_bytes(gci[56:58], "big") != 11):
        raise ValueError("Unsupported GALE01r2 prepared GCI layout")
    start = GCI_HEADER_BYTES + GAME_DATA_BLOCK * CARD_BLOCK_BYTES
    end = start + CARD_BLOCK_BYTES
    decoded = bytearray(decode_block(gci[start:end]))
    if decoded[16:18] != b"\x00\x01":
        raise ValueError("Prepared GameData block has the wrong logical identity")
    mask = int.from_bytes(decoded[SAVE_OFFSET:SAVE_OFFSET + 2], "big")
    decoded[SAVE_OFFSET:SAVE_OFFSET + 2] = (mask | ALL_CHARACTER_BITS).to_bytes(2, "big")
    encoded = encode_block(decoded)
    if decode_block(encoded)[16:] != decoded[16:]:
        raise ValueError("Updated HSD card block did not round-trip")
    return gci[:start] + encoded + gci[end:]
