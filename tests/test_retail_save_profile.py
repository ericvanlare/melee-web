"""Focused tests for hash-bound MWRC v4 save-profile preparation."""

import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import retail_save_profile as PROFILE  # noqa: E402


def encode_block(decoded: bytes) -> bytes:
    """Build a synthetic valid HSD block for the read-only decoder tests."""

    result = bytearray(decoded)
    result[:16] = PROFILE.card_digest(result[16:])
    for index in range(16, len(result)):
        previous = result[index - 1]
        value = result[index] ^ previous ^ PROFILE.KEYS[previous % 13]
        result[index] = sum(((value >> source) & 1) << destination
                             for source, destination in enumerate(
                                 PROFILE.PERMUTATIONS[previous % 7]))
    return bytes(result)


def synthetic_gci(characters: int = 0x0024, stages: int = 0x01C0) -> bytes:
    size = PROFILE.GCI_HEADER_BYTES + PROFILE.GCI_BLOCK_COUNT * PROFILE.CARD_BLOCK_BYTES
    gci = bytearray(size)
    gci[:6] = b"GALE01"
    gci[8:8 + len(b"SuperSmashBros0110290334")] = b"SuperSmashBros0110290334"
    struct.pack_into(">H", gci, 56, PROFILE.GCI_BLOCK_COUNT)
    decoded = bytearray(PROFILE.CARD_BLOCK_BYTES)
    decoded[16:18] = b"\x00\x01"
    struct.pack_into(">HH", decoded, PROFILE.SAVE_CHARACTER_OFFSET,
                     characters, stages)
    start = PROFILE.GCI_HEADER_BYTES + PROFILE.GAME_DATA_BLOCK * PROFILE.CARD_BLOCK_BYTES
    gci[start:start + PROFILE.CARD_BLOCK_BYTES] = encode_block(decoded)
    return bytes(gci)


def synthetic_recipe(frames: int = 2, version: int = 3) -> bytes:
    return (PROFILE.MWRC_HEADER.pack(PROFILE.MAGIC, version, 0x12345678, frames) +
            (bytes(range(256)) * 2)[:PROFILE.GAME_INFO_BYTES] +
            bytes([0xA5]) * PROFILE.PAD_STATE_BYTES +
            bytes(range(PROFILE.FRAME_BYTES)) * frames)


class RetailSaveProfileTests(unittest.TestCase):
    def test_v3_binding_inserts_save_masks_and_preserves_inputs(self):
        fixture = synthetic_gci(0x07FF, 0x01C0)
        recipe = synthetic_recipe()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture_path = root / "prepared.gci"
            recipe_path = root / "input-v3.mwrc"
            output_path = root / "bound-v4.mwrc"
            fixture_path.write_bytes(fixture)
            recipe_path.write_bytes(recipe)
            fixture_hash = hashlib.sha256(fixture).hexdigest()
            provenance = PROFILE.bind_recipe(recipe_path, fixture_path, output_path,
                                             fixture_hash)
            bound = output_path.read_bytes()
            magic, version, seed, frames = PROFILE.MWRC_HEADER.unpack_from(bound)
            self.assertEqual((magic, version, seed, frames),
                             (b"MWRC", 4, 0x12345678, 2))
            self.assertEqual(bound[16:20], struct.pack(">HH", 0x07FF, 0x01C0))
            self.assertEqual(bound[20:], recipe[16:])
            self.assertEqual(len(bound), len(recipe) + 4)
            self.assertEqual(recipe_path.read_bytes(), recipe)
            self.assertEqual(fixture_path.read_bytes(), fixture)
            self.assertEqual(provenance["profile"]["unlocked_characters"], 0x07FF)
            self.assertEqual(provenance["profile"]["unlocked_stages"], 0x01C0)
            sidecar = json_load(output_path.with_name(output_path.name + ".json"))
            self.assertEqual(sidecar["transport"]["input_version"], 3)
            self.assertEqual(sidecar["transport"]["output_version"], 4)
            self.assertEqual(sidecar["fixture"]["sha256"], fixture_hash)
            self.assertEqual(sidecar["output_sha256"], hashlib.sha256(bound).hexdigest())

    def test_cli_requires_explicit_inputs_and_reports_masks(self):
        fixture = synthetic_gci(0x1234, 0xABCD)
        recipe = synthetic_recipe(1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture_path = root / "prepared.gci"
            recipe_path = root / "input-v3.mwrc"
            output_path = root / "bound-v4.mwrc"
            fixture_path.write_bytes(fixture)
            recipe_path.write_bytes(recipe)
            command = [sys.executable, str(ROOT / "scripts" / "bind_retail_save_profile.py"),
                       "--fixture-sha256", hashlib.sha256(fixture).hexdigest(),
                       "--recipe", str(recipe_path), "--fixture-gci", str(fixture_path),
                       "--output", str(output_path)]
            result = subprocess.run(command, capture_output=True, text=True,
                                    check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('"unlocked_characters": 4660', result.stdout)
            self.assertIn('"unlocked_stages": 43981', result.stdout)
            missing = subprocess.run(
                [sys.executable, str(ROOT / "scripts" / "bind_retail_save_profile.py"),
                 "--recipe", str(recipe_path), "--fixture-gci", str(fixture_path),
                 "--output", str(root / "missing.mwrc")],
                capture_output=True, text=True, check=False)
            self.assertEqual(missing.returncode, 2)

    def test_bad_hash_checksum_layout_and_recipe_are_rejected(self):
        fixture = synthetic_gci()
        recipe = synthetic_recipe()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture_path, recipe_path = root / "fixture", root / "recipe"
            fixture_path.write_bytes(fixture)
            recipe_path.write_bytes(recipe)
            good_hash = hashlib.sha256(fixture).hexdigest()
            with self.assertRaisesRegex(PROFILE.SaveProfileError, "SHA-256 mismatch"):
                PROFILE.bind_recipe(recipe_path, fixture_path, root / "bad-hash",
                                   "00" * 32)

            corrupt = bytearray(fixture)
            block_start = PROFILE.GCI_HEADER_BYTES + PROFILE.CARD_BLOCK_BYTES
            corrupt[block_start + 100] ^= 1
            corrupt_path = root / "corrupt.gci"
            corrupt_path.write_bytes(corrupt)
            with self.assertRaisesRegex(PROFILE.SaveProfileError, "SHA-256 mismatch"):
                PROFILE.bind_recipe(recipe_path, corrupt_path, root / "bad-checksum",
                                   good_hash)
            corrupt_hash = hashlib.sha256(corrupt).hexdigest()
            with self.assertRaisesRegex(PROFILE.SaveProfileError, "checksum"):
                PROFILE.bind_recipe(recipe_path, corrupt_path, root / "bad-checksum-2",
                                   corrupt_hash)

            for malformed, message in (
                    (recipe[:-1], "truncated"),
                    (PROFILE.MWRC_HEADER.pack(b"MWRC", 2, 0, 1) + recipe[16:], "v3"),
                    (recipe + b"\0", "trailing"),
                    (b"NOPE" + recipe[4:], "format")):
                malformed_path = root / "malformed.mwrc"
                malformed_path.write_bytes(malformed)
                with self.assertRaisesRegex(PROFILE.SaveProfileError, message):
                    PROFILE.bind_recipe(malformed_path, fixture_path,
                                       root / ("out-" + message), good_hash)

    def test_output_and_sidecar_never_overwrite_existing_files(self):
        fixture = synthetic_gci()
        recipe = synthetic_recipe(1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture_path, recipe_path = root / "fixture", root / "recipe"
            fixture_path.write_bytes(fixture)
            recipe_path.write_bytes(recipe)
            digest = hashlib.sha256(fixture).hexdigest()
            output = root / "bound"
            output.write_bytes(b"keep")
            with self.assertRaisesRegex(PROFILE.SaveProfileError, "overwrite"):
                PROFILE.bind_recipe(recipe_path, fixture_path, output, digest)
            output.unlink()
            sidecar = Path(str(output) + ".json")
            sidecar.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(PROFILE.SaveProfileError, "overwrite"):
                PROFILE.bind_recipe(recipe_path, fixture_path, output, digest)
            self.assertEqual(recipe_path.read_bytes(), recipe)
            self.assertEqual(fixture_path.read_bytes(), fixture)


def json_load(path: Path):
    import json
    return json.loads(path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
