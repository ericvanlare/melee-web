"""Validate and run the source-derived cold-boot allocation fixture.

The checked-Wasm executable takes only source-derived geometry and arena roots.
This adapter rejects capture-shaped inputs so a successful run cannot become a
replay of observed pointers or allocation rows.  Identity strings in a JSON
context are labels, not proof; :func:`run_owned_fixture` derives the context
from the pinned source, symbols, DOL and owned disc before running the fixture.
``fixture_arguments`` remains available for deliberately synthetic unit tests.
"""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import re
from typing import Any

try:
    from .original_boot_context import DOLPHIN_REVISION, SOURCE_REVISION, derive_boot_context
except ImportError:
    from original_boot_context import DOLPHIN_REVISION, SOURCE_REVISION, derive_boot_context


SCHEMA = "melee-web-original-boot-context"
VERSION = 1
SOURCE_BASE = 0x80000000


def _uint(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"{name} must be a uint32")
    return value


def _section(context: dict[str, Any], name: str) -> dict[str, Any]:
    value = context.get(name)
    if not isinstance(value, dict):
        raise ValueError(f"missing {name} section")
    return value


def _aligned(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _rounded_down(value: int, alignment: int) -> int:
    return value & ~(alignment - 1)


def _check_identity(context: dict[str, Any]) -> None:
    identities = context.get("identities")
    if not isinstance(identities, dict):
        raise ValueError("independent boot context is missing source identities")
    for key in ("dol_sha256", "disc_boot_header_sha256", "apploader_sha256", "symbols_sha256",
                "bi2_sha256", "dolphin_boot_source_sha256"):
        value = identities.get(key)
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise ValueError(f"independent boot context is missing identities.{key}")
    if identities.get("source_revision") != SOURCE_REVISION:
        raise ValueError("independent boot context has an unpinned source revision")
    if identities.get("dolphin_revision") != DOLPHIN_REVISION:
        raise ValueError("independent boot context has an unpinned Dolphin revision")
    source_sha256 = identities.get("source_sha256")
    if not isinstance(source_sha256, dict) or not source_sha256 or any(
        not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None
        for value in source_sha256.values()
    ):
        raise ValueError("independent boot context has invalid source hashes")


def fixture_arguments(context: dict[str, Any], *, require_identity: bool = False) -> list[str]:
    """Return numeric fixture arguments after checking source-derived relations."""
    if context.get("schema") != SCHEMA or context.get("version") != VERSION:
        raise ValueError("unsupported original boot context")
    if context.get("derivation") != "owned_disc_apploader_and_dol_no_capture_inputs":
        raise ValueError("fixture requires an independently derived, capture-free context")
    if require_identity:
        _check_identity(context)
    if any(key in context for key in ("allocations", "records", "capture", "pointers")):
        raise ValueError("allocation captures cannot configure startup")

    root = _section(context, "root")
    boot = _section(context, "boot")
    stages = _section(context, "stages")
    mem1 = _uint(boot.get("memory_size"), "boot.memory_size")
    aram = _uint(root.get("aram_size"), "root.aram_size")
    arena_hi = _uint(root.get("arena_hi"), "root.arena_hi")
    root_arena_lo = _uint(root.get("arena_lo"), "root.arena_lo")
    boot_arena_hi = _uint(boot.get("arena_hi"), "boot.arena_hi")
    boot_aram = _uint(boot.get("aram_size"), "boot.aram_size")
    arena_lo = _uint(stages.get("os_arena_lo"), "stages.os_arena_lo")
    crash_size = _uint(stages.get("crash_allocation_size"), "stages.crash_allocation_size")
    crash_align = _uint(stages.get("crash_allocation_alignment"), "stages.crash_allocation_alignment")
    crash_base = _uint(stages.get("crash_allocation_base"), "stages.crash_allocation_base")
    after_crash = _uint(stages.get("after_crash"), "stages.after_crash")
    xfb_count = _uint(stages.get("framebuffer_count"), "stages.framebuffer_count")
    xfb_size = _uint(stages.get("framebuffer_size"), "stages.framebuffer_size")
    fb_width = _uint(stages.get("framebuffer_width"), "stages.framebuffer_width")
    fb_height = _uint(stages.get("framebuffer_height"), "stages.framebuffer_height")
    xfb_begin = _uint(stages.get("framebuffer_begin"), "stages.framebuffer_begin")
    after_xfb = _uint(stages.get("after_xfb"), "stages.after_xfb")
    fifo_size = _uint(stages.get("fifo_size"), "stages.fifo_size")
    init_lo = _uint(stages.get("os_init_alloc_lo"), "stages.os_init_alloc_lo")
    heap_max = _uint(root.get("heap_max_num"), "root.heap_max_num")
    audio_size = _uint(root.get("audio_heap_size"), "root.audio_heap_size")
    aram_base = _uint(root.get("aram_base"), "root.aram_base")

    if root_arena_lo != init_lo or boot_arena_hi != arena_hi or boot_aram != aram:
        raise ValueError("duplicated boot arena or ARAM fields disagree")
    mem1_end = SOURCE_BASE + mem1
    if mem1 == 0 or mem1_end > 0x100000000 or aram == 0:
        raise ValueError("empty or inverted source memory bounds")
    if (arena_lo < SOURCE_BASE or arena_hi > mem1_end or arena_hi <= arena_lo or
            root_arena_lo < SOURCE_BASE or root_arena_lo >= arena_hi):
        raise ValueError("source arena is outside the fresh MEM1 span")
    if xfb_count == 0 or xfb_count > 3 or fb_width == 0 or fb_height == 0 or \
            fb_width > 0xffff or fb_height > 0xffff:
        raise ValueError("invalid authored XFB geometry/count")
    expected_xfb_size = ((fb_width + 15) & ~15) * fb_height * 2
    if expected_xfb_size > 0xffffffff or expected_xfb_size != xfb_size:
        raise ValueError("framebuffer geometry does not produce the source XFB span")
    if crash_size == 0 or crash_align == 0 or crash_align & (crash_align - 1):
        raise ValueError("crash allocation alignment is not a power of two")
    if crash_base != _aligned(arena_lo, crash_align):
        raise ValueError("crash base does not follow source arena alignment")
    if after_crash != _aligned(crash_base + crash_size, crash_align):
        raise ValueError("crash allocation end does not follow source allocation")
    if crash_base < arena_lo or after_crash > arena_hi:
        raise ValueError("crash allocation leaves the source arena")
    if xfb_begin != _aligned(after_crash, 32):
        raise ValueError("XFB start does not follow crash allocation")
    expected_xfb_end = xfb_begin
    for _ in range(xfb_count):
        expected_xfb_end = _aligned(expected_xfb_end + xfb_size, 32)
    if after_xfb != expected_xfb_end:
        raise ValueError("XFB span does not follow source allocation")
    expected_init_lo = _aligned(after_xfb, 32) + fifo_size
    if expected_init_lo > 0xffffffff or init_lo != expected_init_lo:
        raise ValueError("FIFO end does not follow source allocation")
    if (after_xfb > arena_hi or init_lo >= arena_hi or fifo_size == 0 or
            heap_max < 2 or audio_size == 0):
        raise ValueError("source heap setup has no usable arena")
    descriptor_bytes = heap_max * 12
    descriptor_end = _aligned(init_lo + descriptor_bytes, 32)
    audio_end = descriptor_end + audio_size
    arena_end = _rounded_down(arena_hi, 32)
    audio_heap_end = _rounded_down(audio_end, 32)
    main_heap_begin = _aligned(audio_end, 32)
    if (descriptor_end > 0xffffffff or audio_end > 0xffffffff or
            arena_end < descriptor_end or arena_end - descriptor_end < 64 or
            audio_heap_end < descriptor_end or audio_heap_end - descriptor_end < 64 or
            arena_end < main_heap_begin or arena_end - main_heap_begin < 64):
        raise ValueError("source heap descriptors or audio/main heaps exceed the arena")
    if aram_base >= aram or aram_base & 31:
        raise ValueError("source ARAM base is outside aligned ARAM")

    args = {
        "mem1": mem1, "aram": aram, "arena_hi": arena_hi,
        "arena_lo": arena_lo, "crash_size": crash_size,
        "crash_align": crash_align, "crash_base": crash_base,
        "after_crash": after_crash, "xfb_count": xfb_count,
        "xfb_size": xfb_size, "fb_width": fb_width, "fb_height": fb_height,
        "xfb_begin": xfb_begin,
        "after_xfb": after_xfb, "fifo_size": fifo_size,
        "init_lo": init_lo, "heap_max": heap_max,
        "audio_size": audio_size, "aram_base": aram_base,
    }
    return [item for key, value in args.items() for item in (f"--{key}", str(value))]


def load_context(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("boot context must be an object")
    return value


def run_fixture(executable: Path, context: dict[str, Any], *, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    """Run a fresh fixture process from an already derived context.

    Callers that have owned source inputs should use :func:`run_owned_fixture`;
    this lower-level entry point remains useful for synthetic tests.
    """
    return subprocess.run(
        ["node", str(executable), *fixture_arguments(context, require_identity=True)],
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def run_owned_fixture(executable: Path, *, dol_path: Path, disc_path: Path,
                      symbols_path: Path, source_root: Path,
                      timeout: int = 120) -> subprocess.CompletedProcess[str]:
    """Derive a trusted context from owned inputs and run a fresh fixture.

    The identity-bound context is produced internally from the pinned source,
    symbol map, original DOL and owned disc.  Callers cannot supply a JSON
    context with merely matching labels and have it treated as provenance.
    """
    context = derive_boot_context(
        Path(dol_path), Path(disc_path), Path(symbols_path), Path(source_root)
    )
    return run_fixture(Path(executable), context, timeout=timeout)
