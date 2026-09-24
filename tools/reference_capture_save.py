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
# gm_1601.c's lbl_803B790C has exactly eleven authored stage unlock rows,
# indexed by bits 0..10.  Bits above this range are not unlock data.
AUTHORED_STAGE_UNLOCK_MASK = (1 << 11) - 1
# These are the authored unlock rows used by gm_80164430/gm_80164504 in
# gm_1601.c.  Yoshi's Story (Gr_Kind_Story, 0x0A) is a default stage and has
# no row in lbl_803B790C, so it contributes no save bit.
SUPPORTED_STAGE_UNLOCK_BITS = {
    "battlefield": 1 << 6,
    "final_destination": 1 << 7,
    "dream_land": 1 << 8,
    "yoshis_story": 0,
}
SUPPORTED_STAGE_MASK = (SUPPORTED_STAGE_UNLOCK_BITS["battlefield"] |
                        SUPPORTED_STAGE_UNLOCK_BITS["final_destination"] |
                        SUPPORTED_STAGE_UNLOCK_BITS["dream_land"])


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


def unlock_profile_gci(gci: bytes, *, character_bits: int = 0,
                       stage_bits: int = 0) -> bytes:
    """OR explicit authored unlock bits into every valid GameData copy.

    This helper intentionally cannot write an arbitrary save image: it only
    changes the two unlock masks, then validates the original HSD checksum and
    encoding by decoding the newly encoded block again.  Callers should pass
    bits identified in the pinned source tables, rather than a guessed
    all-stages value.

    lbcardgame.c binds manifest entry 1 to gmMainLib_GetSaveData(). The HSD
    card writer fn_803B1338 places the primary in block 1 after a 0x20-byte
    protected-block header. gmmain_lib.h puts unlocked_characers_bitmask at
    save offset 0; gm_80164F18 sets bits 0..10. Stage availability is at +2.
    The original reader can select a later physical copy with the same logical
    identity (fn_803ADF90), including block 10 in a freshly created save. Update
    all checksum-valid copies so the prepared availability does not depend on
    which copy is selected. Invalid secondary blocks are preserved, never
    repaired into newly selectable copies. This is not a general save editor.
    """
    for name, value in (("character_bits", character_bits),
                        ("stage_bits", stage_bits)):
        if (isinstance(value, bool) or not isinstance(value, int) or
                not 0 <= value <= 0xFFFF):
            raise ValueError(f"{name} must be an integer in the range 0..0xffff")
        if name == "stage_bits" and value & ~AUTHORED_STAGE_UNLOCK_MASK:
            raise ValueError("stage_bits contains bits outside authored unlock rows")
        if name == "character_bits" and value & ~ALL_CHARACTER_BITS:
            raise ValueError("character_bits contains bits outside authored unlock rows")

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
    result = bytearray(gci)
    for block in range(GAME_DATA_BLOCK, 11):
        start = GCI_HEADER_BYTES + block * CARD_BLOCK_BYTES
        end = start + CARD_BLOCK_BYTES
        try:
            decoded = bytearray(decode_block(gci[start:end]))
        except ValueError:
            # The primary was checked above; an invalid spare remains invalid.
            continue
        if decoded[16:18] != b"\x00\x01":
            continue
        character_mask = int.from_bytes(decoded[SAVE_OFFSET:SAVE_OFFSET + 2], "big")
        stage_mask = int.from_bytes(decoded[SAVE_OFFSET + 2:SAVE_OFFSET + 4], "big")
        decoded[SAVE_OFFSET:SAVE_OFFSET + 2] = (
            character_mask | character_bits).to_bytes(2, "big")
        decoded[SAVE_OFFSET + 2:SAVE_OFFSET + 4] = (
            stage_mask | stage_bits).to_bytes(2, "big")
        encoded = encode_block(decoded)
        if decode_block(encoded)[16:] != decoded[16:]:
            raise ValueError("Updated HSD card block did not round-trip")
        result[start:end] = encoded
    return bytes(result)


def unlock_roster_gci(gci: bytes) -> bytes:
    """Backward-compatible all-character-only prepared-save update."""
    return unlock_profile_gci(gci, character_bits=ALL_CHARACTER_BITS)
