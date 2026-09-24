"""Derive the seed r5 binding from owned GALE01r2 inputs.

The C sidecar is intentionally a low-level trusted-caller boundary.  This
adapter is the provenance boundary: it validates the owned disc/apploader and
DOL context, then derives the seed global word from the DOL and pinned symbol
map in memory.  It never accepts a saved allocation profile or capture row.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any

try:
    from .original_boot_context import derive_boot_context
    from .retail_allocation_profile import build_profile
except ImportError:
    from original_boot_context import derive_boot_context
    from retail_allocation_profile import build_profile


def _regular_file(path: Path, label: str) -> Path:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{label} must be an owned regular file")
    return path


def derive_owned_seed_binding(*, dol_path: Path, disc_path: Path,
                              symbols_path: Path, source_root: Path) -> dict[str, Any]:
    """Derive a trusted seed binding from owned source inputs.

    ``derive_boot_context`` validates the disc/apploader, pinned source tree,
    symbol map and DOL identity. ``build_profile`` then reads the seed global
    and its initial pointer word directly from that same owned DOL.  A caller
    cannot replace these values with a profile JSON or recorded address.
    """
    dol = _regular_file(dol_path, "DOL")
    disc = _regular_file(disc_path, "disc")
    symbols = _regular_file(symbols_path, "symbols")
    source_root = Path(source_root)
    context = derive_boot_context(dol, disc, symbols, source_root)
    profile = build_profile(dol, symbols)
    seed_symbol = profile["globals"].get("seed_ptr")
    source_word = profile["initial_dol_words"].get("seed_ptr")
    if not isinstance(seed_symbol, dict) or type(source_word) is not int:
        raise ValueError("owned DOL profile lacks seed_ptr source identity")
    if seed_symbol.get("section") != ".sdata" or seed_symbol.get("size") != 4:
        raise ValueError("owned seed_ptr is not the authored four-byte .sdata global")
    if (type(seed_symbol.get("address")) is not int or
            not 0x80000000 <= seed_symbol["address"] < 0x81800000 or
            seed_symbol["address"] & 3):
        raise ValueError("owned seed_ptr global is outside aligned source MEM1")
    if not 0x80000000 <= source_word < 0x81800000 or source_word & 3:
        raise ValueError("owned seed_ptr value is outside aligned source MEM1")
    if context["identities"]["source_revision"] != profile["source_revision"]:
        raise ValueError("boot context and DOL profile source revisions differ")
    if context["identities"]["symbols_sha256"] != profile["symbols_sha256"]:
        raise ValueError("boot context and DOL profile symbol identities differ")
    return {
        "profile_version": profile["version"],
        "global_id": "seed_ptr",
        "source_word": source_word,
        "global_address": seed_symbol["address"],
        "independently_derived": True,
        "provenance": {
            "dol_sha1": profile["dol_sha1"],
            "dol_sha256": context["identities"]["dol_sha256"],
            "source_revision": profile["source_revision"],
            "symbols_sha256": profile["symbols_sha256"],
            "disc_boot_header_sha256": context["identities"]["disc_boot_header_sha256"],
            "apploader_sha256": context["identities"]["apploader_sha256"],
            "derivation": "owned_disc_apploader_and_dol_sda_no_capture_inputs",
        },
    }
