"""Exact retained browser re-export for a separate original Rules campaign.

Reads the existing HSD codec; never edits, unlocks or manufactures a save.
"""
import hashlib
from pathlib import Path
from reference_capture_save import decode_block, CARD_BLOCK_BYTES, GCI_HEADER_BYTES

GCI_SHA256 = "5184f7f9bfcbd35ea7cc07904cbed557b8a7fc9e624a05aa02c8d1d308d4d729"
GAME_WRITTEN_SHA256 = "fec85d3faf3df55c1b3476f174f9d6885626ca660ae05321167e409948bfe286"
SAVE_BYTES = 0x1790
BANK_BYTES = 0x1f2c


def decode_declared_profile(raw):
    """Strict layout of the selected re-export, not a general GCI repair reader."""
    if (len(raw) != 90176 or raw[:6] != b"GALE01" or
            raw[8:40].rstrip(b"\0") != b"SuperSmashBros0110290334" or
            int.from_bytes(raw[56:58], "big") != 11):
        raise ValueError("Selected original profile GCI layout differs")
    records = {}
    for slot in range(1, 11):
        start = GCI_HEADER_BYTES + slot * CARD_BLOCK_BYTES
        decoded = decode_block(raw[start:start + CARD_BLOCK_BYTES])
        identity = int.from_bytes(decoded[16:18], "big")
        if identity not in (*range(1, 9), 0xffff):
            raise ValueError("Selected original profile record identity differs")
        records.setdefault(identity, []).append(decoded)
    if (set(records) != {*range(1, 9), 0xffff} or len(records[1]) != 2 or
            any(len(records[key]) != 1 for key in (*range(2, 9), 0xffff))):
        raise ValueError("Selected original profile record inventory differs")
    saves = [row[32:32 + SAVE_BYTES] for row in records[1]]
    if saves[0] != saves[1]:
        raise ValueError("Selected original profile redundant data differs")
    return {"save": saves[0], "banks": [records[key][0][32:32 + BANK_BYTES]
                                           for key in range(2, 9)]}


def load_profile(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size != 90176:
        raise ValueError("Selected original profile is not an exact ordinary GCI")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != GCI_SHA256:
        raise ValueError("Selected original profile GCI identity differs")
    return dict(decode_declared_profile(raw), sha256=GCI_SHA256, raw=raw)


def prepare_gci_folder(source, destination):
    """Interop setup pattern: one unchanged GCI in a fresh owned USA folder."""
    profile = load_profile(source)
    destination = Path(destination)
    destination.mkdir()  # No existing card, path redirect or overwrite.
    usa = destination / "USA"
    usa.mkdir()
    target = usa / "personal-after-reload.gci"
    with target.open("xb") as stream:
        stream.write(profile["raw"])
    target.chmod(0o400)
    if hashlib.sha256(target.read_bytes()).hexdigest() != profile["sha256"]:
        raise ValueError("Owned original profile copy differs")
    return profile, target
