"""Derive the supported retail cold-boot roots from owned executable/disc data.

This is a bounded GALE01r2/Dolphin BS2 context decoder, not a CPU emulator.
No allocation capture is accepted as an input. The original apploader's FST
placement and the DOL's OSInit/main constants define the arena. Captured boot
words and allocator results are validation targets only.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys

try:
    from .retail_allocation_profile import Dol, read_symbols, SOURCE_REVISION
    from .source_pool_inventory import build_pool_inventory
except ImportError:
    from retail_allocation_profile import Dol, read_symbols, SOURCE_REVISION
    from source_pool_inventory import build_pool_inventory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from extract_disc_file import DiscImage
from bootstrap import verify_repository, require_clean

# Hash of the owned disc's original apploader, including its trailer. The
# decoder below applies only to that implementation; it does not contain its
# executable bytes. Relevant offsets: 0x4e8..0x504, 0x604..0x7b4.
APPLOADER_SHA256 = "05e584735bb81f45e8506ee76fc1a78b143dc2f6dd3f2fc82b015010dc6448dd"
DOLPHIN_REVISION = "c77bbaa0f372c3f72281602a8b087206706542cb"
# SetupGCMemory supplies 24 MiB MEM1 and 16 MiB ARAM, then runs the original
# apploader. Source/Core/Core/Boot/Boot_BS2Emu.cpp at DOLPHIN_REVISION:
DOLPHIN_BOOT_SHA256 = "ed6e86f6bb58b443a0e22e03dddaed22966bf5d7fb09a2d5de4130648e5479b5"
SOURCE_FILES = (
    "extern/dolphin/src/dolphin/os/OS.c",
    "extern/dolphin/src/dolphin/os/OSAlloc.c",
    "extern/dolphin/src/dolphin/ar/ar.c",
    "src/melee/gm/gmmain.c", "src/melee/db/dberror.c",
    "src/sysdolphin/baselib/initialize.c",
    "src/melee/lb/lbheap.c", "src/melee/lb/lbmemory.c",
    "src/sysdolphin/baselib/devcom.c",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def zero_initialized_manager(dol, symbol):
    """Derive manager roots from the owned DOL's zero-initialized BSS range."""
    base, size = symbol['address'], symbol['size']
    bss, length = struct.unpack_from('>II', dol.raw, 0xD8)
    if (symbol.get('section') != '.bss' or size != 0x6F0 or
            not bss <= base < base + size <= bss + length <= 0x100000000):
        raise ValueError('original handle allocator is not wholly in declared BSS')
    if any(start < base + size and base < start + length
           for start, length, _ in dol.sections):
        raise ValueError('original handle allocator overlaps initialized DOL data')
    return dict.fromkeys(('src', 'dst', 'size', 'offset', 'callback_arg',
                          'callback', 'x6E0', 'x6E4', 'x6E8'), 0)


def signed16(word):
    value = word & 0xffff
    return value - 0x10000 if value & 0x8000 else value


def literal(word, register):
    """Decode li/lis only; reject a register-dependent expression."""
    if (word >> 21) & 31 != register or (word >> 16) & 31:
        raise ValueError("original constant is not an independent li/lis")
    if word >> 26 == 14:
        return signed16(word) & 0xffffffff
    if word >> 26 == 15:
        return (signed16(word) << 16) & 0xffffffff
    raise ValueError("unsupported original constant instruction")


def address_pair(first, second, register=3):
    high = literal(first, register)
    if first >> 26 != 15 or second >> 26 != 14 or (second >> 21) & 31 != register or (second >> 16) & 31 != register:
        raise ValueError("original linker address is not lis/addi")
    return (high + signed16(second)) & 0xffffffff


def words(dol, symbol):
    data = dol.read(symbol["address"], symbol["size"])
    return list(struct.unpack(">%dI" % (len(data) // 4), data))


def calls(dol, caller, target):
    body = words(dol, caller)
    result = []
    for index, word in enumerate(body):
        if word >> 26 != 18 or word & 3 != 1:
            continue
        offset = word & 0x03fffffc
        if offset & 0x02000000:
            offset -= 0x04000000
        if (caller["address"] + index * 4 + offset) & 0xffffffff == target:
            result.append(index)
    return body, result


def derive_boot_context(dol_path: Path, disc_path: Path, symbols_path: Path,
                        source_root: Path) -> dict:
    verify_repository(source_root, SOURCE_REVISION)
    require_clean(source_root)
    if symbols_path.resolve() != (source_root / "config/GALE01/symbols.txt").resolve():
        raise ValueError("boot roots require the untouched pinned source symbol map")
    dol = Dol(dol_path)
    symbols = read_symbols(symbols_path)
    with DiscImage(disc_path) as disc:
        header = disc.read(0x420, 0x20)
        bi2 = disc.read(0x440, 0x2000)
        app_header = disc.read(0x2440, 0x20)
        app_size, trailer_size = struct.unpack_from(">II", app_header, 0x14)
        apploader = disc.read(0x2460, app_size + trailer_size)
    if sha256(apploader) != APPLOADER_SHA256:
        raise ValueError("original apploader differs from the independently decoded implementation")
    fst_size, fst_max = struct.unpack_from(">II", header, 8)
    debug_size, simulated_size = struct.unpack_from(">II", bi2, 0)
    debug_flag = struct.unpack_from(">I", bi2, 12)[0]
    # This declared machine is the pinned default GameCube memory configuration.
    # An expanded-memory boot or debugger monitor requires its own derivation.
    mem1_size, aram_size = 24 * 1024 * 1024, 16 * 1024 * 1024
    if debug_size or simulated_size not in (0, mem1_size) or debug_flag >= 2:
        raise ValueError("unsupported original debugger/expanded-memory boot context")
    if not 0 < fst_size <= fst_max < mem1_size:
        raise ValueError("invalid original FST reservation")
    boot_hi = (0x80000000 + mem1_size - fst_max) & ~31
    boot_bi2 = boot_hi + signed16(struct.unpack_from(">I", apploader, 0x790)[0])
    os_init = words(dol, symbols["OSInit"])
    linker_lo = address_pair(os_init[0xcc // 4], os_init[0xd0 // 4])
    stack_top = address_pair(os_init[0x104 // 4], os_init[0x108 // 4])
    arena_lo = (stack_top + 31) & ~31
    crash, sites = calls(dol, symbols["db_SetupCrashHandler"], symbols["OSAllocFromArenaLo"]["address"])
    if len(sites) != 1:
        raise ValueError("unexpected crash-handler arena allocation sites")
    at = sites[0]
    crash_size, crash_align = literal(crash[at - 2], 3), literal(crash[at - 1], 4)
    if not crash_align or crash_align & (crash_align - 1):
        raise ValueError("original arena allocation alignment is not a power of two")
    crash_base = (arena_lo + crash_align - 1) & ~(crash_align - 1)
    after_crash = (crash_base + crash_size + crash_align - 1) & ~(crash_align - 1)
    main, xfb_sites = calls(dol, symbols["main"], symbols["HSD_AllocateXFB"]["address"])
    _, fifo_sites = calls(dol, symbols["main"], symbols["HSD_AllocateFifo"]["address"])
    if len(xfb_sites) != 1 or len(fifo_sites) != 1:
        raise ValueError("unexpected original boot framebuffer/FIFO call sites")
    buffers = literal(main[xfb_sites[0] - 1], 3)
    fifo_size = literal(main[fifo_sites[0] - 1], 3)
    render = dol.read(symbols["GXNtsc480IntDf"]["address"], symbols["GXNtsc480IntDf"]["size"])
    width = struct.unpack_from(">H", render, 4)[0]
    height = struct.unpack_from(">H", render, 8)[0]
    framebuffer_size = ((width + 15) & ~15) * height * 2
    xfb_begin = (after_crash + 31) & ~31
    after_xfb = xfb_begin
    for _ in range(buffers):
        after_xfb = (after_xfb + framebuffer_size + 31) & ~31
    init_lo = ((after_xfb + 31) & ~31) + fifo_size
    heap_count = int.from_bytes(dol.read(symbols["iparam_heap_max_num"]["address"], 4), "big")
    audio_size = int.from_bytes(dol.read(symbols["iparam_audio_heap_size"]["address"], 4), "big")
    # main explicitly sets heap count. Decode that call and reject an audio
    # override, rather than assuming that initial data survives startup.
    _, setters = calls(dol, symbols["main"], symbols["HSD_SetInitParameter"]["address"])
    for at in setters:
        try:
            selector = literal(main[at - 2], 3)
        except ValueError:
            continue  # render-mode pointer uses a different instruction shape
        if selector == 2:
            heap_count = literal(main[at - 1], 4)
        elif selector == 3:
            raise ValueError("unhandled original audio heap parameter override")
    pool_inventory = build_pool_inventory(source_root / 'src', symbols_path)
    source_hashes = {name: sha256((source_root / name).read_bytes()) for name in SOURCE_FILES}
    source_hashes.update({'src/' + item['path']: item['sha256']
                          for item in pool_inventory['source_files']})
    return {
        "schema": "melee-web-original-boot-context", "version": 1,
        "derivation": "owned_disc_apploader_and_dol_no_capture_inputs",
        "root": {"arena_lo": init_lo, "arena_hi": boot_hi,
                 "heap_max_num": heap_count, "audio_heap_size": audio_size,
                 "aram_base": 0x4000, "aram_size": aram_size},
        "boot": {"arena_lo": 0, "arena_hi": boot_hi, "bi2": boot_bi2,
                 "memory_size": mem1_size, "aram_size": aram_size},
        "stages": {"linker_arena_lo": linker_lo, "stack_top": stack_top,
                   "os_arena_lo": arena_lo, "crash_allocation_size": crash_size,
                   "crash_allocation_alignment": crash_align,
                   "crash_allocation_base": crash_base, "after_crash": after_crash,
                   "framebuffer_count": buffers, "framebuffer_size": framebuffer_size,
                   "framebuffer_begin": xfb_begin, "after_xfb": after_xfb,
                   "fifo_size": fifo_size, "os_init_alloc_lo": init_lo},
        "static_layout": {
            "pool_descriptors": {name: item["address"]
                                 for name, item in pool_inventory['pools'].items()},
            "pool_inventory": pool_inventory,
            "aram_stack_table": symbols["ar_stack$1962"],
            "lbmemory_allocator": symbols["lbMemory_804318B0"]["address"],
            "lbmemory_initial_manager": zero_initialized_manager(dol, symbols["lbMemory_804318B0"]),
            "devcom_initial_request_counter": int.from_bytes(dol.read(
                symbols["HSD_DevCom_804D6050"]["address"], 4), "big"),
            "lbheap_descriptors": [list(row) for row in struct.iter_unpack(">4I", dol.read(
                symbols["lbHeap_803BA380"]["address"], symbols["lbHeap_803BA380"]["size"]))],
        },
        "conditions": ["pinned Dolphin default GameCube memory", "no original debugger monitor",
                       "original DOL entry, no savestate", "default NTSC render mode"],
        "identities": {"dol_sha256": sha256(dol.raw), "disc_boot_header_sha256": sha256(header),
                       "bi2_sha256": sha256(bi2), "apploader_sha256": sha256(apploader),
                       "symbols_sha256": sha256(symbols_path.read_bytes()),
                       "source_revision": SOURCE_REVISION, "source_sha256": source_hashes,
                       "dolphin_revision": DOLPHIN_REVISION, "dolphin_boot_source_sha256": DOLPHIN_BOOT_SHA256},
    }
