"""Read-only GALE01r2 match-entry and input/state collector, sourced by GDB.

This produces reference *candidates*. A separate repeatability/comparison step
must validate them. Breakpoints observe ordinary retail execution; no registers,
game memory, executable code, RNG or fighter state are written.
"""

import hashlib
import json
import os
from pathlib import Path
import struct
import uuid
import sys

import gdb


ROOT = Path(os.environ.get("MELEE_REPLAY_REFERENCE_WORK",
                           "work/reference-replay-proof")).resolve()
COLLECTOR = Path(os.environ.get("MELEE_REPLAY_COLLECTOR", "tools/reference_replay_capture.py")).resolve()
sys.path.insert(0, str(COLLECTOR.parent))
from reference_replay_boundary import BoundaryObservations

observations = BoundaryObservations()
OUTPUT = ROOT / "capture.jsonl"
LIMIT = 240
active = False
ready = False
frame_index = 0
fighters = {}
pending_inputs = []
breakpoints = []


def memory(address, size):
    if not (0x80000000 <= address <= 0x81800000 - size and 0 < size <= 0x1000):
        raise RuntimeError("Reference memory read is outside bounded retail RAM")
    return bytes(gdb.selected_inferior().read_memory(address, size))


def word(address):
    return struct.unpack(">I", memory(address, 4))[0]


def rng():
    return word(word(0x804D5F94))


def emit(row):
    with OUTPUT.open("a") as stream:
        stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")


def machine_context():
    names = [f"r{i}" for i in range(32)] + ["pc", "lr", "ctr", "cr"]
    return [int(gdb.parse_and_eval("$" + name)) for name in names] + [word(0x80479D58)]


def float_bits(address):
    return memory(address, 4).hex()


def fighter_state(slot, address):
    head = memory(address, 0x100)
    def u(offset):
        return struct.unpack_from(">I", head, offset)[0]
    def vector(offset):
        return [head[index:index + 4].hex() for index in
                range(offset, offset + 12, 4)]
    return {
        "slot": slot, "kind": u(4), "motion": u(0x10),
        "animation": u(0x14), "facing_bits": head[0x2C:0x30].hex(),
        "position_bits": vector(0xB0), "velocity_bits": vector(0x80),
        "knockback_bits": vector(0x8C), "ground_air": u(0xE0),
        "animation_frame_bits": float_bits(address + 0x894),
        "animation_speed_bits": float_bits(address + 0x89C),
        "damage_bits": float_bits(address + 0x1830),
        "shield_bits": float_bits(address + 0x1998),
        "stocks": struct.unpack("b", memory(0x80453080 + slot * 0xE90 + 0x8E, 1))[0],
        "input_hex": memory(address + 0x620, 0x6C).hex(),
    }


def state():
    return {"rng": rng(), "scene_frame": word(0x80479D58),
            "match_frame": word(0x8046B6C4),
            "fighters": [fighter_state(slot, pointer)
                         for slot, pointer in sorted(fighters.items())]}


class Observer(gdb.Breakpoint):
    def __init__(self, address, action):
        super().__init__(f"*0x{address:08x}", gdb.BP_HARDWARE_BREAKPOINT,
                         internal=True)
        self.silent = True
        self.action = action
        breakpoints.append(self)

    def stop(self):
        global active
        try:
            return bool(self.action())
        except Exception as error:
            active = False
            emit({"record": "error", "error": str(error)})
            print("Reference capture failed:", error)
            return True


def created():
    if active:
        pointer = word(int(gdb.parse_and_eval("$r3")) + 0x2C)
        slot = memory(pointer + 0xC, 1)[0]
        if slot not in (0, 1):
            raise RuntimeError("Initial reference slice requires human slots 0 and 1")
        fighters[slot] = pointer


def pad_consume():
    if active and ready:
        queue = memory(0x804C1F78, 0xC)
        count, read = queue[3], queue[1]
        if count:
            pointer = struct.unpack_from(">I", queue, 8)[0]
            raw = memory(pointer + read * 0x30, 0x30)
            key = (word(0x80479D58), pointer, read)
            observation = (machine_context(), queue.hex(), raw.hex())
            if not observations.accept("pad", key, observation):
                return False
            # Exclude the byte of PADStatus padding at offset 11.
            pending_inputs.append([raw[i:i + 11].hex() for i in range(0, 48, 12)])


def scheduler_return():
    global active, frame_index
    if not active or not ready:
        return False
    if set(fighters) != {0, 1}:
        raise RuntimeError("Scheduler reached without both retail fighters")
    sample = state()
    if not observations.accept("scheduler", sample["scene_frame"],
                               (machine_context(), sample)):
        if pending_inputs:
            raise RuntimeError("Repeated scheduler trap has new consumed input")
        return False
    if sample["scene_frame"] != frame_index:
        raise RuntimeError("Source scene counter is not contiguous from match entry")
    if len(pending_inputs) != 1:
        raise RuntimeError("Source tick did not consume exactly one PAD vector")
    emit({"record": "frame", "index": frame_index,
          "consumed_inputs": list(pending_inputs), **sample})
    pending_inputs.clear()
    frame_index += 1
    if frame_index == LIMIT:
        emit({"record": "end", "frames": frame_index, "status": "captured"})
        active = False
        print("Identical debugger trap repeats:", observations.duplicates)
        print("Reference candidate captured:", OUTPUT)
        return True
    return False


def entered():
    global ready
    if not active:
        return False
    sample = state()
    if not observations.accept("initial", 0, (machine_context(), sample)):
        if frame_index:
            raise RuntimeError("Initial boundary repeated after gameplay started")
        return False
    pending_inputs.clear()
    emit({"record": "match_enter_complete", **sample})
    ready = True


def enter():
    global active
    pointer = int(gdb.parse_and_eval("$r3"))
    sample = {"record": "match_enter", "rng": rng(),
              "start_melee_hex": memory(pointer, 0x138).hex(),
              "pad_lib_hex": memory(0x804C1F78 + 0xC, 0x20).hex(),
              "pad_master_hex": memory(0x804C1FAC, 0x110).hex(),
              "pad_game_hex": memory(0x804C21CC, 0x110).hex()}
    if not observations.accept("entry", 0, (machine_context(), sample)):
        if ready:
            raise RuntimeError("Match entry repeated after initialization")
        return False
    if active:
        raise RuntimeError("A second match entered before reference capture finished")
    active = True
    emit(sample)



class Arm(gdb.Command):
    def __init__(self):
        super().__init__("retail-replay-arm", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        global OUTPUT, LIMIT, frame_index, active, ready
        values = gdb.string_to_argv(args)
        if len(values) != 2:
            raise gdb.GdbError("retail-replay-arm OUTPUT_PATH FRAME_COUNT")
        if any(bp.is_valid() for bp in breakpoints):
            raise gdb.GdbError("Collector already armed; use a new GDB session")
        OUTPUT = Path(values[0]).resolve()
        LIMIT = int(values[1])
        if not 1 <= LIMIT <= 36000 or OUTPUT.exists():
            raise gdb.GdbError("Require a fresh output path and 1..36000 frames")
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        provenance = json.loads((ROOT / "provenance.json").read_text())
        if (provenance.get("dol_sha1") != "08e0bf20134dfcb260699671004527b2d6bb1a45"
                or provenance.get("dolphin_commit") != "c77bbaa0f372c3f72281602a8b087206706542cb"
                or provenance.get("cpu") != "Interpreter64"
                or provenance.get("cpu_thread") is not False
                or provenance.get("cheats") is not False):
            raise gdb.GdbError("Reference provenance does not match the pinned vanilla configuration")
        frame_index = 0
        active = False
        ready = False
        fighters.clear()
        pending_inputs.clear()
        emit({"record": "header", "schema": "melee-web-retail-replay-candidate",
              "version": 1, "capture_id": uuid.uuid4().hex, "phase": "HSD_GObj_80390CFC_return",
              "input_phase": "HSD_PadRenewMasterStatus_entry_queue",
              "initial_phase": "gm_Scene_Vs_OnEnter_entry",
              "game_revision": "GALE01r2", "frames_requested": LIMIT,
              "provenance": provenance,
              "collector_sha256": hashlib.sha256(COLLECTOR.read_bytes() + b"\0" + COLLECTOR.with_name("reference_replay_boundary.py").read_bytes()).hexdigest(),
              "writes_game_state": False})
        Observer(0x800693A8, created)
        # Install all observers before execution. Mutating GDB breakpoints
        # inside stop callbacks can retrigger the current remote stop.
        if word(0x8016E9C4) != 0x4E800020:
            raise gdb.GdbError("Pinned VS-entry return instruction does not match")
        Observer(0x8016E934, enter)
        Observer(0x8016E9C4, entered)
        Observer(0x8037750C, pad_consume)
        Observer(0x80390EB4, scheduler_return)
        print("Read-only reference collector armed:", OUTPUT)


Arm()
