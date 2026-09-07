#!/usr/bin/env python3
"""Extract one user-selected file from a local Melee 1.02 ISO/GCM/CISO.

The image is opened read-only. Output must be explicitly placed in the ignored
assets-local directory. This is an asset preparation tool, not an emulator.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
CISO_HEADER_SIZE = 0x8000
MAX_FST_SIZE = 16 * 1024 * 1024
MAX_EXTRACT_SIZE = 64 * 1024 * 1024


class DiscFormatError(ValueError):
    """The image cannot be safely interpreted as a supported Melee disc."""


def relative_disc_path(value: str) -> str:
    """Reject ambiguous or unsafe paths instead of normalizing them."""
    parts = value.split("/")
    if not value or any(part in ("", ".", "..") for part in parts):
        raise DiscFormatError("disc path must be a canonical relative path")
    if "\\" in value or ":" in value or any(ord(c) < 32 for c in value):
        raise DiscFormatError("disc path contains an unsupported character")
    return value


@dataclass(frozen=True)
class DiscFile:
    path: str
    offset: int
    size: int


class DiscImage:
    """Bounded random access over a raw image or Wii-style sparse CISO.

    CISO's header/map format is documented by Dolphin's CISOBlob reader:
    https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DiscIO/CISOBlob.cpp
    This does not support the unrelated compressed PSP CISO format.
    """

    def __init__(self, path: Path):
        self._file = path.open("rb")
        try:
            self._file.seek(0, 2)
            self.physical_size = self._file.tell()
            self._file.seek(0)
            magic = self._file.read(4)
            self._mapping: list[int | None] | None = None
            self.block_size = 0
            self.logical_size = self.physical_size
            if magic == b"CISO":
                self._read_ciso_header()
            header = self.read(0, 0x440)
            if header[:8] != b"GALE01\x00\x02":
                raise DiscFormatError("expected Super Smash Bros. Melee USA revision 2 (GALE01 1.02)")
            if header[0x1C:0x20] != bytes.fromhex("c2339f3d"):
                raise DiscFormatError("invalid GameCube disc magic")
            self._fst_offset, self._fst_size = struct.unpack_from(">II", header, 0x424)
        except BaseException:
            self._file.close()
            raise

    def _read_ciso_header(self) -> None:
        self._file.seek(0)
        header = self._file.read(CISO_HEADER_SIZE)
        if len(header) != CISO_HEADER_SIZE:
            raise DiscFormatError("truncated CISO header")
        block_size = struct.unpack_from("<I", header, 4)[0]
        if not 0x8000 <= block_size <= 32 * 1024 * 1024 or block_size & (block_size - 1):
            raise DiscFormatError("unsupported CISO block size")
        mapping = []
        count = 0
        for present in header[8:]:
            if present not in (0, 1):
                raise DiscFormatError("invalid CISO block map")
            mapping.append(count if present else None)
            count += present
        if CISO_HEADER_SIZE + count * block_size > self.physical_size:
            raise DiscFormatError("truncated CISO data blocks")
        # A container footer may follow the mapped blocks; it is not disc data.
        self._mapping = mapping
        self.block_size = block_size
        self.logical_size = len(mapping) * block_size

    def __enter__(self) -> DiscImage:
        return self

    def __exit__(self, *_exc: object) -> None:
        self._file.close()

    def _check_range(self, offset: int, size: int) -> None:
        if offset < 0 or size < 0 or offset > self.logical_size or size > self.logical_size - offset:
            raise DiscFormatError("disc byte range is outside the image")

    def read(self, offset: int, size: int) -> bytes:
        self._check_range(offset, size)
        output = bytearray()
        while size:
            if self._mapping is None:
                physical_offset = offset
                part_size = size
            else:
                block, within = divmod(offset, self.block_size)
                part_size = min(size, self.block_size - within)
                mapped = self._mapping[block]
                physical_offset = (
                    None if mapped is None else CISO_HEADER_SIZE + mapped * self.block_size + within
                )
            if physical_offset is None:
                output.extend(bytes(part_size))
            else:
                self._file.seek(physical_offset)
                part = self._file.read(part_size)
                if len(part) != part_size:
                    raise DiscFormatError("truncated disc data")
                output.extend(part)
            size -= part_size
            offset += part_size
        return bytes(output)

    def files(self) -> dict[str, DiscFile]:
        if self._fst_offset < 0x440 or not 12 <= self._fst_size <= MAX_FST_SIZE:
            raise DiscFormatError("invalid or oversized filesystem table")
        fst = self.read(self._fst_offset, self._fst_size)
        root_tag, root_parent, count = struct.unpack_from(">III", fst)
        if root_tag != 0x01000000 or root_parent != 0 or count < 1 or count > len(fst) // 12:
            raise DiscFormatError("invalid filesystem root or entry count")
        names = fst[count * 12:]
        if not names:
            raise DiscFormatError("invalid filesystem string table")

        def name_at(offset: int) -> str:
            if offset >= len(names):
                raise DiscFormatError("filesystem name offset is out of bounds")
            end = names.find(b"\0", offset)
            if end < 0:
                raise DiscFormatError("unterminated filesystem name")
            try:
                name = names[offset:end].decode("ascii")
            except UnicodeDecodeError as exc:
                raise DiscFormatError("non-ASCII filesystem name") from exc
            relative_disc_path(name)
            if "/" in name:
                raise DiscFormatError("filesystem name contains a path separator")
            return name

        # Each active directory supplies its exclusive end index and parent id.
        stack: list[tuple[int, int, str]] = [(count, 0, "")]
        result: dict[str, DiscFile] = {}
        seen: set[str] = set()
        for index in range(1, count):
            while index >= stack[-1][0]:
                stack.pop()
            tag, first, second = struct.unpack_from(">III", fst, index * 12)
            kind, name_offset = tag >> 24, tag & 0xFFFFFF
            if kind not in (0, 1):
                raise DiscFormatError("unknown filesystem entry type")
            name = name_at(name_offset)
            path = f"{stack[-1][2]}/{name}" if stack[-1][2] else name
            if path in seen:
                raise DiscFormatError("duplicate filesystem path")
            seen.add(path)
            if kind:
                if first != stack[-1][1] or not index < second <= stack[-1][0]:
                    raise DiscFormatError("invalid filesystem directory hierarchy")
                stack.append((second, index, path))
            else:
                self._check_range(first, second)
                result[path] = DiscFile(path, first, second)
        return result


def extract_file(image: Path, disc_path: str, output: Path, *, assets_root: Path = ROOT / "assets-local") -> DiscFile:
    """Extract exactly one file without overwriting existing output."""
    relative_disc_path(disc_path)
    destination = output.resolve()
    allowed = assets_root.resolve()
    if destination == allowed or not destination.is_relative_to(allowed):
        raise DiscFormatError("output must be inside the ignored assets-local directory")
    with DiscImage(image) as disc:
        selected = disc.files().get(disc_path)
        if selected is None:
            raise DiscFormatError(f"disc file was not found: {disc_path}")
        if selected.size > MAX_EXTRACT_SIZE:
            raise DiscFormatError("selected file exceeds the 64 MiB extraction limit")
        data = disc.read(selected.offset, selected.size)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects an existing extracted file from replacement.
    file = destination.open("xb")
    try:
        with file:
            file.write(data)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise
    return selected


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="local GALE01 1.02 ISO, GCM, or Wii-style CISO")
    parser.add_argument("disc_path", help="exact case-sensitive relative disc path, for example TyTarget.dat")
    parser.add_argument("--output", required=True, type=Path, help="new file inside this repository's assets-local directory")
    args = parser.parse_args(argv)
    try:
        selected = extract_file(args.image, args.disc_path, args.output)
        digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Extraction failed: {exc}\n")
    print(f"Extracted {selected.path}: {selected.size} bytes; SHA-256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
