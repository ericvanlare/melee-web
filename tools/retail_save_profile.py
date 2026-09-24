#!/usr/bin/env python3
"""Bind an MWRC v3 input recipe to the unlock profile in a prepared GCI.

This is a narrow, read-only save-profile reader.  It decodes the original
GALE01r2 HSD card block and copies the two observed masks into a new MWRC v4
transport envelope.  It never edits the GCI, the input recipe, or any other
save field, and it does not accept hand-authored unlock values.

The card permutation and checksum below are a minimal read-only reuse of
``decode_block`` from the capture owner's ``tools/reference_capture_save.py``.
That module follows ``hsd_3B2E.c`` and ``hsd_3B2B.c`` in the pinned source.
Keeping the decoder local makes this preparation tool usable without changing
or importing the capture-owner worktree.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
from typing import Any


MAGIC = b"MWRC"
MWRC_HEADER = struct.Struct(">4sIII")
MWRC_HEADER_BYTES = MWRC_HEADER.size
INPUT_VERSION = 3
OUTPUT_VERSION = 4
OBSERVATION_VERSION = 3
GAME_INFO_BYTES = 0x138
PAD_STATE_BYTES = 30 + 3 * 4 * 66
PAD_BYTES_PER_PORT = 11
PORT_COUNT = 4
FRAME_BYTES = PAD_BYTES_PER_PORT * PORT_COUNT
MAX_FRAMES = 36000
V3_MIN_BYTES = MWRC_HEADER_BYTES + GAME_INFO_BYTES + PAD_STATE_BYTES + FRAME_BYTES
V3_MAX_BYTES = MWRC_HEADER_BYTES + GAME_INFO_BYTES + PAD_STATE_BYTES + MAX_FRAMES * FRAME_BYTES
V4_MAX_BYTES = V3_MAX_BYTES + 4

GCI_HEADER_BYTES = 0x40
CARD_BLOCK_BYTES = 0x2000
GAME_DATA_BLOCK = 1
SAVE_CHARACTER_OFFSET = 0x20
SAVE_STAGE_OFFSET = 0x22
GCI_BLOCK_COUNT = 11

KEYS = (0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E)
PERMUTATIONS = (
    (0, 4, 1, 5, 2, 6, 3, 7), (3, 0, 2, 6, 5, 4, 7, 1),
    (6, 5, 0, 1, 3, 7, 2, 4), (1, 3, 7, 4, 0, 2, 5, 6),
    (7, 2, 5, 0, 6, 1, 4, 3), (5, 6, 4, 3, 7, 0, 1, 2),
    (2, 1, 6, 7, 4, 3, 0, 5),
)


class SaveProfileError(ValueError):
    """The fixture or MWRC input cannot be safely bound."""


@dataclass(frozen=True)
class SaveProfile:
    unlocked_characters: int
    unlocked_stages: int


@dataclass(frozen=True)
class RecipeInfo:
    seed: int
    frames: int


def _fail(message: str) -> None:
    raise SaveProfileError(message)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _expected_sha256(value: Any) -> str:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-fA-F]{64}", value) is None:
        _fail("fixture SHA-256 must be exactly 64 hexadecimal characters")
    return value.lower()


def card_digest(payload: bytes) -> bytes:
    """Return the original HSD card digest for one decoded block payload."""

    state = bytearray.fromhex("0123456789ABCDEFFEDCBA9876543210")
    for index, value in enumerate(payload):
        state[index % 16] = (state[index % 16] + value) & 0xFF
    for index in range(1, 16):
        if state[index - 1] == state[index]:
            state[index] ^= 0xFF
    return bytes(state)


def decode_block(encoded: bytes) -> bytes:
    """Decode and checksum one original HSD card block."""

    if len(encoded) < 16:
        _fail("Truncated HSD card block")
    result = bytearray(encoded)
    previous = encoded[15]
    for index in range(16, len(encoded)):
        current = encoded[index]
        value = sum(((current >> destination) & 1) << source
                     for source, destination in enumerate(PERMUTATIONS[previous % 7]))
        result[index] = value ^ previous ^ KEYS[previous % 13]
        previous = current
    if card_digest(result[16:]) != result[:16]:
        _fail("Invalid HSD card block checksum")
    return bytes(result)


def _read_gci_layout(gci: bytes) -> bytes:
    expected_size = GCI_HEADER_BYTES + GCI_BLOCK_COUNT * CARD_BLOCK_BYTES
    if len(gci) != expected_size:
        _fail("Unsupported GALE01r2 prepared GCI layout")
    if (gci[:6] != b"GALE01" or
            gci[8:40].rstrip(b"\0") != b"SuperSmashBros0110290334" or
            int.from_bytes(gci[56:58], "big") != GCI_BLOCK_COUNT):
        _fail("Unsupported GALE01r2 prepared GCI layout")
    start = GCI_HEADER_BYTES + GAME_DATA_BLOCK * CARD_BLOCK_BYTES
    end = start + CARD_BLOCK_BYTES
    decoded = decode_block(gci[start:end])
    if decoded[16:18] != b"\x00\x01":
        _fail("Prepared GameData block has the wrong logical identity")
    return decoded


def profile_from_gci(gci: bytes, fixture_sha256: str) -> SaveProfile:
    """Verify a prepared GCI hash/checksum and read its two save masks."""

    expected = _expected_sha256(fixture_sha256)
    actual = _sha256(gci)
    if actual != expected:
        _fail(f"fixture SHA-256 mismatch: expected {expected}, got {actual}")
    decoded = _read_gci_layout(gci)
    return SaveProfile(
        unlocked_characters=int.from_bytes(
            decoded[SAVE_CHARACTER_OFFSET:SAVE_CHARACTER_OFFSET + 2], "big"),
        unlocked_stages=int.from_bytes(
            decoded[SAVE_STAGE_OFFSET:SAVE_STAGE_OFFSET + 2], "big"),
    )


def read_save_profile(path: str | Path, fixture_sha256: str) -> tuple[SaveProfile, str]:
    """Read a GCI without modifying it; return its profile and verified hash."""

    source = Path(path).expanduser().resolve()
    try:
        raw = source.read_bytes()
    except OSError as error:
        _fail(f"cannot read fixture {source}: {error}")
    expected = _expected_sha256(fixture_sha256)
    actual = _sha256(raw)
    if actual != expected:
        _fail(f"fixture SHA-256 mismatch: expected {expected}, got {actual}")
    return profile_from_gci(raw, actual), actual


def _read_v3_recipe(raw: bytes) -> RecipeInfo:
    if len(raw) < V3_MIN_BYTES or len(raw) > V3_MAX_BYTES:
        _fail("MWRC v3 recipe size is outside its bounds")
    if len(raw) < MWRC_HEADER_BYTES:
        _fail("Truncated MWRC header")
    magic, version, seed, frames = MWRC_HEADER.unpack_from(raw)
    if magic != MAGIC:
        _fail("Unsupported MWRC input format")
    if version != INPUT_VERSION:
        _fail("save-profile binding requires an MWRC v3 recipe")
    if not 1 <= frames <= MAX_FRAMES:
        _fail(f"MWRC frame count must be between 1 and {MAX_FRAMES}")
    expected_size = MWRC_HEADER_BYTES + GAME_INFO_BYTES + PAD_STATE_BYTES + frames * FRAME_BYTES
    if len(raw) != expected_size:
        _fail("MWRC v3 recipe is truncated or has trailing bytes")
    return RecipeInfo(seed=seed, frames=frames)


def _new_path(path: Path, context: str) -> Path:
    expanded = path.expanduser()
    if expanded.exists() or expanded.is_symlink():
        _fail(f"refusing to overwrite existing {context}: {expanded}")
    resolved = expanded.resolve()
    if resolved.exists() or resolved.is_symlink():
        _fail(f"refusing to overwrite existing {context}: {resolved}")
    return resolved


def _write_new(path: Path, data: bytes, context: str) -> None:
    try:
        with path.open("xb") as stream:
            stream.write(data)
    except OSError as error:
        _fail(f"cannot create {context} {path}: {error}")


def _write_json_new(path: Path, value: dict[str, Any]) -> None:
    payload = json.dumps(value, indent=2, sort_keys=True) + "\n"
    _write_new(path, payload.encode("utf-8"), "provenance sidecar")


def bind_recipe(recipe_path: str | Path, fixture_gci_path: str | Path,
                output_path: str | Path, fixture_sha256: str,
                sidecar_path: str | Path | None = None) -> dict[str, Any]:
    """Create a new v4 recipe and provenance sidecar from a v3 recipe/GCI pair."""

    recipe = Path(recipe_path).expanduser().resolve()
    fixture = Path(fixture_gci_path).expanduser().resolve()
    output = _new_path(Path(output_path), "MWRC output")
    sidecar_input = (Path(sidecar_path) if sidecar_path is not None
                     else Path(str(output) + ".json"))
    sidecar = _new_path(sidecar_input, "provenance sidecar")
    if output == sidecar:
        _fail("MWRC output and provenance sidecar must be different paths")
    if output in (recipe, fixture) or sidecar in (recipe, fixture):
        _fail("output and sidecar must not overwrite the input recipe or fixture")
    try:
        recipe_raw = recipe.read_bytes()
    except OSError as error:
        _fail(f"cannot read recipe {recipe}: {error}")
    recipe_info = _read_v3_recipe(recipe_raw)
    profile, fixture_hash = read_save_profile(fixture, fixture_sha256)

    payload = bytearray(MWRC_HEADER.pack(MAGIC, OUTPUT_VERSION,
                                          recipe_info.seed, recipe_info.frames))
    payload += struct.pack(">HH", profile.unlocked_characters,
                           profile.unlocked_stages)
    payload += recipe_raw[MWRC_HEADER_BYTES:]
    if len(payload) != len(recipe_raw) + 4 or len(payload) > V4_MAX_BYTES:
        _fail("generated MWRC v4 size does not match its v3 recipe")
    output_hash = _sha256(bytes(payload))
    recipe_hash = _sha256(recipe_raw)
    sidecar_data: dict[str, Any] = {
        "schema": "melee-web-retail-save-profile-binding",
        "version": 1,
        "status": "bound",
        "scope": (
            "save-derived unlock masks bound to a v3 MWRC input recipe; "
            "no save editing or port-equivalence claim"
        ),
        "transport": {
            "magic": MAGIC.decode("ascii"),
            "input_version": INPUT_VERSION,
            "output_version": OUTPUT_VERSION,
            "observation_version": OBSERVATION_VERSION,
            "frames": recipe_info.frames,
            "input_bytes": len(recipe_raw),
            "output_bytes": len(payload),
        },
        "fixture": {
            "sha256": fixture_hash,
            "gci_layout": "GALE01r2 prepared GCI",
            "save_block": GAME_DATA_BLOCK,
            "checksum": "validated",
            "character_mask_offset": SAVE_CHARACTER_OFFSET,
            "stage_mask_offset": SAVE_STAGE_OFFSET,
        },
        "profile": {
            "unlocked_characters": profile.unlocked_characters,
            "unlocked_stages": profile.unlocked_stages,
            "unlocked_characters_hex": f"{profile.unlocked_characters:04x}",
            "unlocked_stages_hex": f"{profile.unlocked_stages:04x}",
            "source": "decoded save block only",
        },
        "recipe_sha256": recipe_hash,
        "output_sha256": output_hash,
        "claims": {
            "save_checksum": "pass",
            "masks_from_save": "pass",
            "input_preserved": "pass",
            "fixture_preserved": "pass",
            "port_equivalence": "not_claimed",
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    _write_new(output, bytes(payload), "MWRC output")
    _write_json_new(sidecar, sidecar_data)
    return sidecar_data


# Descriptive alias for callers that use "MWRC" in their own tooling.
bind_mwrc = bind_recipe


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Bind a v3 MWRC recipe to unlock masks read from a prepared GCI")
    parser.add_argument("--fixture-sha256", required=True,
                        help="SHA-256 of the original prepared GCI")
    parser.add_argument("--recipe", required=True, type=Path,
                        help="existing MWRC v3 input recipe")
    parser.add_argument("--fixture-gci", required=True, type=Path,
                        help="original prepared GALE01r2 GCI")
    parser.add_argument("--output", required=True, type=Path,
                        help="new MWRC v4 output path")
    parser.add_argument("--sidecar", type=Path,
                        help="optional new provenance sidecar (default: OUTPUT.json)")
    args = parser.parse_args(argv)
    try:
        data = bind_recipe(args.recipe, args.fixture_gci, args.output,
                           args.fixture_sha256, args.sidecar)
    except SaveProfileError as error:
        parser.exit(2, f"retail save-profile binding failed: {error}\n")
    print(json.dumps({
        "status": data["status"],
        "output_sha256": data["output_sha256"],
        "recipe_sha256": data["recipe_sha256"],
        "fixture_sha256": data["fixture"]["sha256"],
        "unlocked_characters": data["profile"]["unlocked_characters"],
        "unlocked_stages": data["profile"]["unlocked_stages"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
