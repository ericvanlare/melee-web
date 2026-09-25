"""Optional read-only allocation stream, loaded by GDB before original boot.

All hardware execute breakpoints are installed before continuing. Function
entries/returns retain nesting, thread, source arguments and observed results.
Only allocator descriptors and identity fields are read; payloads are omitted.
"""
import hashlib
import json
import os
from pathlib import Path
import struct

import gdb

_allocation_profile_path = Path(os.environ["MELEE_ALLOCATION_PROFILE"])
_allocation_profile = json.loads(_allocation_profile_path.read_text())
_allocation_output = Path(os.environ["MELEE_ALLOCATION_OUTPUT"])
_allocation_stream = _allocation_output.open("x", buffering=1)
_allocation_stacks = {}
_allocation_sequence = 0
_allocation_calls = 0
_allocation_failed = None
_allocation_breakpoints = []
_allocation_finished = False
_allocation_last_stop = None
_allocation_repeated_stops = 0
_allocation_limit = int(os.environ.get("MELEE_ALLOCATION_EVENT_LIMIT", "2000000"))


def _allocation_read(address, size):
    if not (0 < size <= 256 and 0x80000000 <= address <= 0x81800000 - size):
        raise RuntimeError("allocation metadata read outside bounded MEM1")
    return bytes(gdb.selected_inferior().read_memory(address, size))


def _allocation_word(address):
    return struct.unpack(">I", _allocation_read(address, 4))[0]


def _allocation_reg(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xFFFFFFFF


def _allocation_machine():
    result = {name: _allocation_reg(name) for name in
              [*("r%d" % i for i in range(32)), "pc", "lr", "ctr", "cr"]}
    result["scene_frame"] = _allocation_word(0x80479D58)
    result["retrace_count"] = _allocation_word(0x804D7420)
    return result


def _allocation_emit(row):
    global _allocation_sequence
    if _allocation_sequence >= _allocation_limit:
        raise RuntimeError("allocation event budget exhausted")
    row["sequence"] = _allocation_sequence
    _allocation_sequence += 1
    _allocation_stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")


def _allocation_metadata(name, args, return_value=None):
    result = {}
    if name in ("ARInit", "ARAlloc", "ARFree", "ARGetSize"):
        result["aram"] = {key: _allocation_word(_allocation_profile["globals"][key]["address"])
                          for key in ("__AR_Size", "__AR_StackPointer", "__AR_FreeBlocks",
                                      "__AR_BlockLength", "__AR_init_flag")}
    if name in ("HSD_ObjAllocInit", "HSD_ObjAllocAddFree", "HSD_ObjAlloc", "HSD_ObjFree"):
        # HSD_ObjAllocData is eleven words. No object payload/free-chain bytes.
        result["pool_words"] = list(struct.unpack(">11I", _allocation_read(args[0], 44)))
    if name in ("HSD_ObjSetHeap", "HSD_ObjAllocAddFree", "HSD_ObjAlloc"):
        result["object_heap_words"] = list(struct.unpack(">4I", _allocation_read(
            _allocation_profile["globals"]["obj_heap"]["address"], 16)))
    if return_value is not None:
        if name in ("OSInitAlloc", "OSCreateHeap", "OSDestroyHeap", "OSAllocFromHeap", "OSFreeToHeap"):
            result["heaps"] = _allocation_heap_metadata()
        if name.startswith("lbHeap_"):
            result["game_heap_words"] = list(struct.unpack(">46I", _allocation_read(
                _allocation_profile["globals"]["lbHeap_80431FA0"]["address"], 184)))
        if name in ("lbMemory_80014E24", "lbMemory_80014FC8", "lbMemory_800154D4") and return_value:
            words = 3 if name == "lbMemory_80014FC8" else 4
            result["handle_words"] = list(struct.unpack(">%dI" % words, _allocation_read(return_value, words * 4)))
        if name == "Fighter_Create" and return_value:
            fighter = _allocation_word(return_value + 0x2C)
            result["fighter"] = {"gobj": return_value, "address": fighter,
                "slot": _allocation_read(fighter + 12, 1)[0], "kind": _allocation_word(fighter + 4)}
        if name == "gm_Scene_Vs_OnEnter":
            result["globals"] = {key: _allocation_word(info["address"])
                for key, info in _allocation_profile["globals"].items()
                if key in ("seed_ptr", "ArenaStart", "ArenaEnd", "HeapArray", "NumHeaps")}
            result["r2"] = _allocation_reg("r2")
            result["r13"] = _allocation_reg("r13")
    return result


def _allocation_heap_metadata():
    symbols = _allocation_profile["globals"]
    values = {name: _allocation_word(symbols[name]["address"])
              for name in ("HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd",
                           "__OSCurrHeap", "current_heap", "__OSArenaLo", "__OSArenaHi")}
    count, array = values["NumHeaps"], values["HeapArray"]
    if count > 32:
        raise RuntimeError("original heap descriptor count exceeds diagnostic bound")
    values["descriptors"] = [list(struct.unpack(">3I", _allocation_read(array + i * 12, 12)))
                             for i in range(count)]
    return values


class _AllocationProbe(gdb.Breakpoint):
    def __init__(self, address, spec, phase):
        super().__init__("*0x%x" % address, gdb.BP_HARDWARE_BREAKPOINT, internal=True)
        self.silent = True
        self.spec = spec
        self.phase = phase

    def stop(self):
        global _allocation_calls, _allocation_failed, _allocation_last_stop, _allocation_repeated_stops
        if _allocation_failed is not None:
            return True
        try:
            name = self.spec["name"]
            # SDK __OSCurrentThread lives in the original low-memory context.
            thread = _allocation_word(0x800000E4)
            stack = _allocation_stacks.setdefault(thread, [])
            sp, lr = _allocation_reg("r1"), _allocation_reg("lr")
            machine = _allocation_machine()
            key = [self.phase, name, thread, sp, lr]
            args = ([_allocation_reg("r%d" % (3 + i)) for i in range(self.spec["argc"])]
                    if self.phase == "enter" else
                    (_allocation_last_stop["args"] if _allocation_last_stop and _allocation_last_stop["key"] == key
                     else stack[-1]["args"] if stack else []))
            snapshot = (_allocation_metadata(name, args, machine["r3"] if self.phase == "return" else None)
                        if args or not self.spec["argc"] else {})
            if _allocation_last_stop and _allocation_last_stop["key"] == key:
                if machine != _allocation_last_stop["machine"] or snapshot != _allocation_last_stop["snapshot"]:
                    raise RuntimeError("changed allocator state at a repeated debugger boundary")
                _allocation_last_stop["repeats"] += 1
                if _allocation_last_stop["repeats"] > 32:
                    raise RuntimeError("repeated debugger boundary did not make progress")
                _allocation_repeated_stops += 1
                _allocation_emit({"record": "repeated_stop", "original_sequence": _allocation_last_stop["sequence"],
                                  "function": name, "phase": self.phase,
                                  "machine": machine, "observed": snapshot})
                return False
            stop_sequence = _allocation_sequence
            if self.phase == "enter":
                call = {"call": _allocation_calls, "function": name,
                        "parent": stack[-1]["call"] if stack else None,
                        "thread": thread, "sp": sp, "lr": lr, "args": args}
                _allocation_calls += 1
                if stack and (stack[-1]["function"], stack[-1]["sp"], stack[-1]["lr"]) == (name, sp, lr):
                    raise RuntimeError("repeated allocator entry without a source return")
                stack.append(call)
                _allocation_emit({"record": "enter", **call,
                                  "machine": machine,
                                  "observed": snapshot})
            else:
                if not stack or stack[-1]["function"] != name or stack[-1]["sp"] != sp:
                    raise RuntimeError("unmatched allocation return: %s sp=%x" % (name, sp))
                call = stack.pop()
                value = _allocation_reg("r3")
                _allocation_emit({"record": "return", "call": call["call"],
                                  "function": name, "thread": thread,
                                  "machine": machine,
                                  "result": value, "observed": snapshot})
            _allocation_last_stop = {"key": key, "args": args, "machine": machine,
                                     "snapshot": snapshot, "sequence": stop_sequence, "repeats": 0}
        except Exception as error:
            _allocation_failed = str(error)
            # An exhausted budget still retains its terminal failure record.
            _allocation_stream.write(json.dumps({"record": "error", "sequence": _allocation_sequence,
                "error": _allocation_failed}) + "\n")
            return True
        return False


def allocation_finish(status, detail=None):
    global _allocation_finished
    if _allocation_finished:
        raise RuntimeError("allocation stream already finished")
    pending = [call for stack in _allocation_stacks.values() for call in stack]
    if _allocation_failed or pending:
        status = "incomplete"
    _allocation_emit({"record": "end", "status": status, "detail": detail,
                      "error": _allocation_failed, "pending_calls": pending,
                      "calls": _allocation_calls, "repeated_stops": _allocation_repeated_stops})
    _allocation_finished = True
    _allocation_stream.close()
    if status != "captured":
        raise RuntimeError("original allocation capture incomplete")


if _allocation_reg("pc") != _allocation_profile["entry"]:
    raise RuntimeError("allocation collection must begin at the original DOL entry, without a savestate")
_allocation_emit({"record": "header", "schema": "melee-web-original-allocation-history",
                  "version": 1, "start": "original_dol_entry", "writes_game_state": False,
                  "profile_sha256": hashlib.sha256(_allocation_profile_path.read_bytes()).hexdigest(),
                  "initial_pc": _allocation_reg("pc"), "initial_sp": _allocation_reg("r1"),
                  "observed_boot_context": {"arena_lo": _allocation_word(0x80000030),
                    "arena_hi": _allocation_word(0x80000034), "memory_size": _allocation_word(0x80000028),
                    "bi2": _allocation_word(0x800000F4)}})
for _allocation_spec in _allocation_profile["functions"]:
    if _allocation_word(_allocation_spec["address"]) != _allocation_spec["entry_word"]:
        raise RuntimeError("allocator instruction mismatch: " + _allocation_spec["name"])
    _allocation_breakpoints.append(_AllocationProbe(_allocation_spec["address"], _allocation_spec, "enter"))
    for _allocation_return in _allocation_spec["returns"]:
        if _allocation_word(_allocation_return) != 0x4E800020:
            raise RuntimeError("allocator return instruction mismatch")
        _allocation_breakpoints.append(_AllocationProbe(_allocation_return, _allocation_spec, "return"))
print("Read-only original allocation collector installed before DOL entry", flush=True)
