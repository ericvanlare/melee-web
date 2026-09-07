"""Disc boundary tests use authored bytes, never extracted game content."""

import importlib.util
import contextlib
import hashlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "extract_disc_file.py"
SPEC = importlib.util.spec_from_file_location("disc_extract", SCRIPT)
disc_extract = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = disc_extract
SPEC.loader.exec_module(disc_extract)

BLOCK = 0x8000
FST_OFFSET = 0x800
PAYLOAD_OFFSET = 2 * BLOCK + 32
PAYLOAD = b"Authored fixture data; this is not a game asset."


def filesystem_table():
    # The root has no stored name. Real Melee's string table does not start NUL.
    return (
        struct.pack(">III", 0x01000000, 0, 3)
        + struct.pack(">III", 0x01000000, 0, 3)
        + struct.pack(">III", 7, PAYLOAD_OFFSET, len(PAYLOAD))
        + b"models\0target.dat\0"
    )


def raw_image(fst=None):
    fst = filesystem_table() if fst is None else fst
    image = bytearray(BLOCK * 3)
    image[:8] = b"GALE01\x00\x02"
    image[0x1C:0x20] = bytes.fromhex("c2339f3d")
    struct.pack_into(">III", image, 0x424, FST_OFFSET, len(fst), len(fst))
    image[FST_OFFSET:FST_OFFSET + len(fst)] = fst
    image[PAYLOAD_OFFSET:PAYLOAD_OFFSET + len(PAYLOAD)] = PAYLOAD
    return image


def sparse_ciso(image):
    header = bytearray(disc_extract.CISO_HEADER_SIZE)
    header[:4] = b"CISO"
    struct.pack_into("<I", header, 4, BLOCK)
    chunks = []
    for index in range(len(image) // BLOCK):
        block = image[index * BLOCK:(index + 1) * BLOCK]
        if any(block):
            header[8 + index] = 1
            chunks.append(block)
    return header + b"".join(chunks)


class DiscExtractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="melee local extraction ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets-local"
        self.output = self.assets / "target.dat"

    def image(self, data):
        path = self.root / "input image.bin"
        path.write_bytes(data)
        return path

    def extract(self, source, path="models/target.dat", output=None, **range_options):
        return disc_extract.extract_file(
            source, path, self.output if output is None else output, assets_root=self.assets,
            **range_options,
        )

    def test_raw_image_exact_selection_and_source_preserved(self):
        data = raw_image()
        source = self.image(data)
        selected = self.extract(source)
        self.assertEqual(selected, disc_extract.DiscFile("models/target.dat", PAYLOAD_OFFSET, len(PAYLOAD)))
        self.assertEqual(self.output.read_bytes(), PAYLOAD)
        self.assertEqual(source.read_bytes(), data)
        self.assertEqual(list(self.assets.iterdir()), [self.output])

    def test_sparse_ciso_mapping_zero_holes_and_footer(self):
        source = self.image(sparse_ciso(raw_image()) + b"uninterpreted container footer")
        with disc_extract.DiscImage(source) as disc:
            self.assertEqual(disc.read(BLOCK - 2, BLOCK + 4), bytes(BLOCK + 4))
            self.assertEqual(disc.read(PAYLOAD_OFFSET, len(PAYLOAD)), PAYLOAD)
        self.extract(source)
        self.assertEqual(self.output.read_bytes(), PAYLOAD)

    def test_ciso_rejects_truncation_invalid_map_and_block_size(self):
        good = sparse_ciso(raw_image())
        invalid_map = bytearray(good)
        invalid_map[9] = 2
        invalid_size = bytearray(good)
        struct.pack_into("<I", invalid_size, 4, 12345)
        for data, message in [
            (good[:100], "truncated CISO header"),
            (good[:-1], "truncated CISO data blocks"),
            (invalid_map, "block map"),
            (invalid_size, "block size"),
        ]:
            with self.subTest(message=message):
                with self.assertRaisesRegex(disc_extract.DiscFormatError, message):
                    disc_extract.DiscImage(self.image(data))

    def test_disc_identity_magic_and_truncated_header(self):
        for offset, value, message in [
            (3, 80, "GALE01"),
            (7, 1, "revision 2"),
            (0x1C, 0, "disc magic"),
        ]:
            with self.subTest(message=message):
                data = raw_image()
                data[offset] = value
                with self.assertRaisesRegex(disc_extract.DiscFormatError, message):
                    disc_extract.DiscImage(self.image(data))
        with self.assertRaisesRegex(disc_extract.DiscFormatError, "outside"):
            disc_extract.DiscImage(self.image(b"GALE"))

    def test_rejects_out_of_bounds_and_negative_reads(self):
        with disc_extract.DiscImage(self.image(raw_image())) as disc:
            for offset, size in [(-1, 1), (0, -1), (len(raw_image()), 1), (len(raw_image()) + 1, 0)]:
                with self.subTest(offset=offset, size=size):
                    with self.assertRaisesRegex(disc_extract.DiscFormatError, "outside"):
                        disc.read(offset, size)

    def test_fst_range_and_entry_count_limits(self):
        for offset, value in [(0x424, len(raw_image()) - 12), (0x428, 8), (0x428, disc_extract.MAX_FST_SIZE + 1)]:
            with self.subTest(offset=offset, value=value):
                data = raw_image()
                struct.pack_into(">I", data, offset, value)
                with disc_extract.DiscImage(self.image(data)) as disc:
                    with self.assertRaises(disc_extract.DiscFormatError):
                        disc.files()
        for root_offset, value in [(0, 0), (4, 1), (8, 0), (8, 0xFFFFFFFF)]:
            with self.subTest(root_offset=root_offset):
                fst = bytearray(filesystem_table())
                struct.pack_into(">I", fst, root_offset, value)
                with disc_extract.DiscImage(self.image(raw_image(fst))) as disc:
                    with self.assertRaisesRegex(disc_extract.DiscFormatError, "root"):
                        disc.files()

    def test_invalid_directory_hierarchy(self):
        for offset, value in [(16, 1), (20, 1), (20, 4)]:
            with self.subTest(offset=offset, value=value):
                fst = bytearray(filesystem_table())
                struct.pack_into(">I", fst, offset, value)
                with disc_extract.DiscImage(self.image(raw_image(fst))) as disc:
                    with self.assertRaisesRegex(disc_extract.DiscFormatError, "hierarchy"):
                        disc.files()

    def test_filename_bounds_termination_and_path_components(self):
        original = filesystem_table()
        cases = [
            original[:36] + b"models\0target.dat",
            original[:36] + b"models\0../bad\0",
            original[:36] + b"models\0..\0",
            original[:36] + b"models\0ta\xffget\0",
        ]
        bad_offset = bytearray(original)
        struct.pack_into(">I", bad_offset, 24, 0xFFFFFF)
        cases.append(bad_offset)
        for fst in cases:
            with self.subTest(fst=fst[36:]):
                with disc_extract.DiscImage(self.image(raw_image(fst))) as disc:
                    with self.assertRaises(disc_extract.DiscFormatError):
                        disc.files()

    def test_duplicate_path_unknown_type_and_file_outside_image(self):
        duplicate = (
            struct.pack(">III", 0x01000000, 0, 3)
            + struct.pack(">III", 0, PAYLOAD_OFFSET, 1) * 2
            + b"duplicate.dat\0"
        )
        unknown_type = bytearray(filesystem_table())
        unknown_type[24] = 2
        outside = bytearray(filesystem_table())
        struct.pack_into(">I", outside, 28, len(raw_image()))
        for fst, message in [(duplicate, "duplicate"), (unknown_type, "entry type"), (outside, "outside")]:
            with self.subTest(message=message):
                with disc_extract.DiscImage(self.image(raw_image(fst))) as disc:
                    with self.assertRaisesRegex(disc_extract.DiscFormatError, message):
                        disc.files()

    def test_selection_is_exact_and_rejects_ambiguous_paths(self):
        source = self.image(raw_image())
        for path in ["", "/models/target.dat", "models//target.dat", "models/../target.dat", "models/./target.dat", "models\\target.dat", "C:target.dat"]:
            with self.subTest(path=path):
                with self.assertRaises(disc_extract.DiscFormatError):
                    self.extract(source, path)
        for path in ["target.dat", "models/Target.dat"]:
            with self.subTest(path=path):
                with self.assertRaisesRegex(disc_extract.DiscFormatError, "not found"):
                    self.extract(source, path)
        self.assertFalse(self.assets.exists())

    def test_output_must_be_local_ignored_path_and_cannot_overwrite(self):
        source = self.image(raw_image())
        with self.assertRaisesRegex(disc_extract.DiscFormatError, "assets-local"):
            self.extract(source, output=self.root / "tracked.dat")
        self.extract(source)
        self.output.write_bytes(b"preserve my existing output")
        with self.assertRaises(FileExistsError):
            self.extract(source)
        self.assertEqual(self.output.read_bytes(), b"preserve my existing output")

    def test_symlink_cannot_escape_assets_directory(self):
        source = self.image(raw_image())
        self.assets.mkdir()
        outside = self.root / "outside"
        outside.mkdir()
        (self.assets / "escape").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(disc_extract.DiscFormatError, "assets-local"):
            self.extract(source, output=self.assets / "escape" / "file.dat")
        self.assertEqual(list(outside.iterdir()), [])

    def test_size_cap_prevents_output(self):
        source = self.image(raw_image())
        with patch.object(disc_extract, "MAX_EXTRACT_SIZE", len(PAYLOAD) - 1):
            with self.assertRaisesRegex(disc_extract.DiscFormatError, "extraction limit"):
                self.extract(source)
        self.assertFalse(self.assets.exists())

    def test_selected_slice_and_default_remainder(self):
        data = sparse_ciso(raw_image())
        source = self.image(data)
        selected = self.extract(source, offset=4, length=9)
        self.assertEqual(selected, disc_extract.DiscFile("models/target.dat", PAYLOAD_OFFSET + 4, 9))
        self.assertEqual(self.output.read_bytes(), PAYLOAD[4:13])
        tail = self.assets / "tail.dat"
        selected = self.extract(source, output=tail, offset=13)
        self.assertEqual(selected.size, len(PAYLOAD) - 13)
        self.assertEqual(tail.read_bytes(), PAYLOAD[13:])
        self.assertEqual(source.read_bytes(), data)
        with self.assertRaises(FileExistsError):
            self.extract(source, offset=1, length=2)
        self.assertEqual(self.output.read_bytes(), PAYLOAD[4:13])

    def test_rejects_negative_empty_and_out_of_file_segments(self):
        source = self.image(raw_image())
        for options in [
            {"offset": -1}, {"length": -1}, {"length": 0},
            {"offset": len(PAYLOAD)}, {"offset": len(PAYLOAD) + 1},
            {"offset": len(PAYLOAD), "length": 1},
            {"offset": 1, "length": len(PAYLOAD)},
        ]:
            with self.subTest(options=options):
                with self.assertRaises(disc_extract.DiscFormatError):
                    self.extract(source, **options)
        self.assertFalse(self.assets.exists())

    def test_segment_cap_applies_to_selected_bytes_not_whole_file(self):
        source = self.image(raw_image())
        with patch.object(disc_extract, "MAX_EXTRACT_SIZE", 8):
            with self.assertRaisesRegex(disc_extract.DiscFormatError, "extraction limit"):
                self.extract(source, offset=1, length=9)
            self.assertFalse(self.assets.exists())
            self.extract(source, offset=1, length=8)
        self.assertEqual(self.output.read_bytes(), PAYLOAD[1:9])

    def test_cli_reports_file_relative_selected_range_and_digest(self):
        source = self.image(raw_image())
        extract_file = disc_extract.extract_file
        def extract_local(image, disc_path, output, **options):
            return extract_file(image, disc_path, output, assets_root=self.assets, **options)
        report = io.StringIO()
        with patch.object(disc_extract, "extract_file", side_effect=extract_local):
            with contextlib.redirect_stdout(report):
                self.assertEqual(disc_extract.main([
                    str(source), "models/target.dat", "--output", str(self.output),
                    "--offset", "0x4", "--length", "9",
                ]), 0)
        self.assertIn("file bytes [4, 13): 9 bytes", report.getvalue())
        self.assertIn(hashlib.sha256(PAYLOAD[4:13]).hexdigest(), report.getvalue())


if __name__ == "__main__":
    unittest.main()
