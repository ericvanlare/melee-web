"""Build an instruction-bound diagnostic profile from the owned original DOL.

Only symbol identities, instruction hashes and allocator metadata are emitted;
no original executable bodies or heap payloads belong in portable evidence.
"""
from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path

DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
SOURCE_REVISION = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
PROFILE_VERSION = 2

# Argument counts follow the pinned original declarations. These are observation
# boundaries, not replacement implementations of the functions.
FUNCTIONS = {
    "ARInit": 2, "ARAlloc": 1, "ARFree": 1, "ARGetSize": 0,
    "OSInitAlloc": 3, "OSCreateHeap": 2, "OSDestroyHeap": 1,
    "OSSetCurrentHeap": 1, "OSAllocFromHeap": 2, "OSFreeToHeap": 2,
    "OSSetArenaLo": 1, "OSSetArenaHi": 1,
    "OSAllocFromArenaLo": 2, "OSAllocFromArenaHi": 2,
    "HSD_OSInit": 0, "HSD_AllocateXFB": 2, "HSD_AllocateFifo": 1,
    "HSD_CreateMainHeap": 2, "HSD_SetHeap": 1,
    "HSD_ObjSetHeap": 2, "HSD_ObjAllocInit": 3,
    "HSD_ObjAllocAddFree": 2, "HSD_ObjAlloc": 1, "HSD_ObjFree": 2,
    "_HSD_ObjAllocForgetMemory": 2,
    "HSD_MemAlloc": 1, "HSD_Free": 1,
    "lbHeap_80015F3C": 0, "lbHeap_800158D0": 2, "lbHeap_80015900": 0,
    "lbHeap_80015BD0": 2, "lbHeap_80015CA8": 2, "lbHeap_80015D6C": 3,
    "lbMemory_8001564C": 0, "lbMemory_80014E24": 2,
    "lbMemory_80014EEC": 1, "lbMemory_80014FC8": 2,
    "lbMemFreeToHeap": 2, "lbMemory_8001529C": 3,
    "lbMemory_800154D4": 2, "lbMemory_800155A4": 0,
    "Fighter_FirstInitialize_80067A84": 0, "Fighter_Create": 1,
    "gm_Scene_Vs_OnEnter": 1, "gm_Scene_Vs_OnExit": 1,
    "fn_80015184": 2, "lbMemory_80015320": 4, "lbDvd_80017A80": 1,
    "HSD_DevComARAMCallback": 1, "HSD_DevComRequest": 8,
}
GLOBALS = ("HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd",
           "__OSArenaLo", "__OSArenaHi", "seed", "seed_ptr",
           "fighter_alloc_data", "obj_heap", "current_heap", "__OSCurrHeap",
           "lbHeap_80431FA0", "lbMemory_804318B0", "iparam_audio_heap_size",
           "iparam_heap_max_num", "hsd_heap_next_arena_lo", "hsd_heap_next_arena_hi",
           "__AR_Size", "__AR_StackPointer", "__AR_FreeBlocks", "__AR_BlockLength",
           "__AR_init_flag")


def read_symbols(path: Path) -> dict:
    symbols = {}
    for line in path.read_text().splitlines():
        match = re.match(r"(\S+) = (\.[^:]+):0x([0-9A-Fa-f]+); // type:(\w+) size:0x([0-9A-Fa-f]+)", line)
        if match:
            name, section, address, kind, size = match.groups()
            # Some local symbols share names. Ambiguity must not silently pick
            # whichever happens to appear last in the map.
            row = {"address": int(address, 16), "size": int(size, 16),
                   "section": section, "kind": kind}
            if name in symbols and name in (*FUNCTIONS, *GLOBALS):
                raise ValueError("ambiguous source symbol: " + name)
            symbols[name] = row
    return symbols


class Dol:
    def __init__(self, path: Path):
        self.raw = path.read_bytes()
        if hashlib.sha1(self.raw).hexdigest() != DOL_SHA1:
            raise ValueError("owned DOL differs from pinned GALE01r2")
        self.sections = []
        for index in range(18):
            offset = struct.unpack_from(">I", self.raw, index * 4)[0]
            address = struct.unpack_from(">I", self.raw, 0x48 + index * 4)[0]
            size = struct.unpack_from(">I", self.raw, 0x90 + index * 4)[0]
            if size:
                self.sections.append((address, size, offset))
        self.entry = struct.unpack_from(">I", self.raw, 0xE0)[0]

    def read(self, address: int, size: int) -> bytes:
        for start, length, offset in self.sections:
            if start <= address and address + size <= start + length:
                return self.raw[offset + address - start:offset + address - start + size]
        raise ValueError("requested identity is outside original DOL sections")


def build_profile(dol_path: Path, symbols_path: Path) -> dict:
    dol = Dol(dol_path)
    symbols = read_symbols(symbols_path)
    functions = []
    for name, argc in FUNCTIONS.items():
        symbol = symbols[name]
        body = dol.read(symbol["address"], symbol["size"])
        words = struct.unpack(">" + "I" * (len(body) // 4), body)
        returns = [symbol["address"] + i * 4 for i, word in enumerate(words)
                   if word == 0x4E800020]
        if not returns:
            raise ValueError("no unconditional original return for " + name)
        functions.append({"name": name, "argc": argc, **symbol,
                          "entry_word": words[0], "returns": returns,
                          "body_sha256": hashlib.sha256(body).hexdigest()})
    globals_ = {name: symbols[name] for name in GLOBALS}
    # Initial global pointer data comes from the executable's linker layout,
    # independently of any allocation result observed during capture.
    initial = {name: int.from_bytes(dol.read(symbols[name]["address"], 4), "big")
               for name in ("seed_ptr", "__OSArenaLo", "current_heap",
                            "iparam_audio_heap_size", "iparam_heap_max_num")}
    entry_word = int.from_bytes(dol.read(dol.entry, 4), "big")
    return {"schema": "melee-web-original-allocation-profile", "version": PROFILE_VERSION,
            "dol_sha1": DOL_SHA1, "source_revision": SOURCE_REVISION,
            "symbols_sha256": hashlib.sha256(symbols_path.read_bytes()).hexdigest(),
            "entry": dol.entry, "entry_word": entry_word,
            "functions": functions, "globals": globals_,
            "initial_dol_words": initial}
