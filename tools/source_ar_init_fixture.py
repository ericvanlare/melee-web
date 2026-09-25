"""Owned-input adapter for the bounded original ARInit hardware profile.

Only run_owned_fixture attests input derivation. fixture_arguments accepts
synthetic contexts for component tests; matching JSON labels are not provenance.
Captured allocation rows never configure this target.
"""
from __future__ import annotations

from pathlib import Path
import subprocess
import struct
from typing import Any

try:
    from .original_boot_context import Dol, derive_boot_context
    from .original_startup_fixture import fixture_arguments as startup_arguments
except ImportError:
    from original_boot_context import Dol, derive_boot_context
    from original_startup_fixture import fixture_arguments as startup_arguments

# Pinned GameCube Boot_BS2Emu writes 0x09a7ec80 to the OS bus-clock cell.
# This is a declared hardware profile, not a measured/captured timing value.
GAMECUBE_BUS_CLOCK = 162_000_000


def fixture_arguments(context: dict[str, Any], *, require_identity: bool = False) -> list[str]:
    startup_arguments(context, require_identity=require_identity)
    root, boot = context["root"], context["boot"]
    if root["aram_base"] != 0x4000 or root["aram_size"] != 0x1000000:
        raise ValueError("ARInit oracle requires the declared 16 MiB GameCube profile")
    layout = context.get("static_layout")
    table = layout.get("aram_stack_table") if isinstance(layout, dict) else None
    if not isinstance(table, dict) or table.get("section") != ".bss" or table.get("kind") != "object":
        raise ValueError("ARInit requires the authored BSS stack-table symbol")
    address, size = table.get("address"), table.get("size")
    if (type(address) is not int or type(size) is not int or size != 16 * 4 or
            address & 3 or address < 0x80000000 or
            address + size > 0x80000000 + boot["memory_size"] or
            address + size > context["stages"]["os_arena_lo"]):
        raise ValueError("ARInit stack table must be the 16-entry source object below the arena")
    values = {"aram-size": root["aram_size"], "stack-source": address,
              "stack-entries": size // 4, "bus-clock": GAMECUBE_BUS_CLOCK}
    return [part for key, value in values.items() for part in (f"--{key}", str(value))]


def validate_owned_stack(dol: Dol, table: dict[str, Any]) -> None:
    """Require the actual DOL zero-initialized object, not only a symbol label."""
    start, size = table["address"], table["size"]
    bss, length = struct.unpack_from(">II", dol.raw, 0xD8)
    if not bss <= start < start + size <= bss + length <= 0x100000000:
        raise ValueError("ARInit stack table is outside the owned DOL BSS")
    if any(base < start + size and start < base + extent
           for base, extent, _ in dol.sections):
        raise ValueError("ARInit stack table overlaps initialized DOL data")


def run_owned_fixture(executable: Path, *, dol_path: Path, disc_path: Path,
                      symbols_path: Path, source_root: Path,
                      timeout: int = 30) -> subprocess.CompletedProcess[str]:
    """Derive fresh owned boot roots internally; never accept a saved context."""
    context = derive_boot_context(Path(dol_path), Path(disc_path),
                                  Path(symbols_path), Path(source_root))
    arguments = fixture_arguments(context, require_identity=True)
    validate_owned_stack(Dol(Path(dol_path)), context["static_layout"]["aram_stack_table"])
    return subprocess.run(["node", str(executable), *arguments], check=False,
                          capture_output=True, text=True, timeout=timeout)
