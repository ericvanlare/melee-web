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
from retail_input_plan import load_plan, pipe_commands, verify_entry, verify_tick

INPUT_PLAN_PATH = os.environ.get("MELEE_REPLAY_INPUT_PLAN")
input_plan, input_plan_sha256 = load_plan(INPUT_PLAN_PATH) if INPUT_PLAN_PATH else (None, None)

observations = BoundaryObservations()
OUTPUT = ROOT / "capture.jsonl"
LIMIT = 240
active = False
ready = False
frame_index = 0
fighters = {}
pending_inputs = []
breakpoints = []
DRAW_AUDIT = os.environ.get("MELEE_REPLAY_DRAW_AUDIT") == "1"
draw_before = None
draw_count = 0
draw_source_index = None
last_drawn_source_index = -1
published_inputs = 0
pad_bootstrapped = False
pad_sample_index = None
exit_observation = None


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


def supply_input(index):
    global published_inputs
    if input_plan is None: return
    if index == published_inputs - 1:
        return
    if index != published_inputs:
        raise RuntimeError('Controller publication skipped or reordered a planned input: '
                           'requested %d, next %d' % (index, published_inputs))
    for port, pad in enumerate(input_plan['frames'][index]):
        path = ROOT.parent / 'user' / 'Pipes' / ('pad%d' % (port + 1))
        fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
        try:
            command = pipe_commands(pad)
            if os.write(fd, command) != len(command):
                raise RuntimeError('Incomplete owned controller pipe write')
        finally:
            os.close(fd)
    published_inputs += 1


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


def pad_state():
    config = memory(0x804C1F84, 0x20)
    # Skip the two alignment bytes before adc_angle and each status tail.
    result = config[:10] + config[12:]
    for base in (0x804C1FAC, 0x804C20BC, 0x804C21CC):
        raw = memory(base, 0x110)
        result += b"".join(raw[i:i + 66] for i in range(0, 0x110, 68))
    return result.hex()


def state():
    return {"pad_state_hex": pad_state(), "rng": rng(), "scene_frame": word(0x80479D58),
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


# The entry of HSD_PadRenewMasterStatus is not a consumption boundary: the
# function disables interrupts first, then shifts qread and decrements qcount.
# At this instruction the queue transition has completed, interrupts are still
# disabled, and r25 retains the pointer to the slot that was consumed while r6
# retains its pre-shift qread index. The header names this boundary explicitly;
# old entry-queue captures remain separately identifiable.
PAD_MASTER_CONSUME = 0x80377584
PAD_MASTER_CONSUME_WORD = 0x3B7E0028


def pad_consume():
    global pad_bootstrapped, pad_sample_index
    if active and ready:
        if frame_index == LIMIT:
            raise RuntimeError('Next source tick reached before the final requested source draw')
        queue = memory(0x804C1F78, 0xC)
        qnum = queue[0]
        read = int(gdb.parse_and_eval('$r6')) & 0xFF
        pointer = int(gdb.parse_and_eval('$r25')) & 0xFFFFFFFF
        queue_base = struct.unpack_from(">I", queue, 8)[0]
        if not qnum or read >= qnum:
            raise RuntimeError('Pinned PAD consume registers have an invalid qread index')
        expected_pointer = queue_base + read * 0x30
        if pointer != expected_pointer:
            raise RuntimeError('Pinned PAD consume register does not retain the consumed slot')
        raw = memory(pointer, 0x30)
        key = (word(0x80479D58), pointer, read)
        observation = (machine_context(), queue.hex(), raw.hex())
        if not observations.accept("pad", key, observation):
            return False
        # Exclude the byte of PADStatus padding at offset 11.
        pending_inputs.append([raw[i:i + 11].hex() for i in range(0, 48, 12)])
        # The first source consumption is the only queue-driven publication.
        # Subsequent lookahead is driven by the complete vector observed in
        # PADRead, independently of qcount, qread, and pending source ticks.
        if input_plan is not None and not pad_bootstrapped:
            pad_bootstrapped = True
            pad_sample_index = 0
            if 1 < LIMIT:
                supply_input(1)


PAD_READ_HSD_CALLER = 0x80376A28


def pad_read_before_interrupt_restore():
    """Observe HSD's PADRead before it restores interrupts.

    PADRead has already copied all four hardware statuses to the caller's
    buffer at ``r31 - 0x30``. The late HSD return hooks were racy: they ran
    after ``OSRestoreInterrupts`` and could publish after another VI sample
    had arrived. This boundary is inside PADRead and is accepted only for
    HSD_PadRenewRawStatus's saved caller.
    """
    global pad_sample_index
    if not active or not ready or input_plan is None or not pad_bootstrapped:
        return False

    stack = int(gdb.parse_and_eval('$r1'))
    if word(stack + 0x54) != PAD_READ_HSD_CALLER:
        return False

    status_end = int(gdb.parse_and_eval('$r31'))
    raw = memory(status_end - 0x30, 0x30)
    queue = memory(0x804C1F78, 0xC)
    scene = word(0x80479D58)
    key = (stack, scene, queue.hex(), raw.hex())
    observation = (machine_context(), queue.hex(), raw.hex())
    if not observations.accept('pad_read', key, observation):
        return False

    if pad_sample_index is None:
        raise RuntimeError('PADRead observed before source PAD bootstrap')
    index = pad_sample_index + 1
    if index >= LIMIT:
        # Hardware may poll ahead of the final consumed source tick. These
        # unconsumed vectors are outside the bounded recipe; the master-pad
        # and scheduler observers still reject any extra source tick.
        return False
    if index >= len(input_plan['frames']):
        raise RuntimeError('PADRead index exceeds the declared input plan')
    verify_tick(input_plan, index,
                [raw[i:i + 11].hex() for i in range(0, 48, 12)])
    pad_sample_index = index
    if index + 1 < LIMIT:
        supply_input(index + 1)
    return False


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
    if input_plan is not None:
        verify_tick(input_plan, frame_index, pending_inputs[0])
    emit({"record": "frame", "index": frame_index,
          "consumed_inputs": list(pending_inputs), **sample})
    pending_inputs.clear()
    frame_index += 1
    if frame_index == LIMIT and not DRAW_AUDIT:
        return finish()
    return False


def finish():
    global active
    emit({"record": "end", "frames": frame_index, "status": "captured"})
    # A bounded input prefix and a complete stock match are different evidence.
    # Observe the original exit request after the final scheduler/draw, before
    # results loading can flush the controller queue or destroy the fighters.
    completion = {
        "schema": "melee-web-retail-match-completion", "version": 1,
        "capture_sha256": hashlib.sha256(OUTPUT.read_bytes()).hexdigest(),
        "frames": frame_index,
        "phase": "after_final_source_draw" if DRAW_AUDIT else "after_final_scheduler",
        "scene_request": word(0x80479D64),
        "match_end_state": memory(0x8046B6A0, 1)[0],
        "match_result": memory(0x8046B6A8, 1)[0],
        "final_draw_source_index": last_drawn_source_index if DRAW_AUDIT else None,
        "exit_observation": exit_observation,
    }
    with (ROOT / "match-completion.json").open("x") as stream:
        json.dump(completion, stream, sort_keys=True)
    active = False
    print("Identical debugger trap repeats:", observations.duplicates)
    print("Reference candidate captured:", OUTPUT)
    return True


def exit_requested():
    global exit_observation
    if not active or not ready:
        return False
    sample = {"phase": "gm_801A4B60_return", "index": frame_index,
              "caller": int(gdb.parse_and_eval("$lr")),
              "scene_request": word(0x80479D64)}
    if not observations.accept("exit", frame_index, (machine_context(), sample)):
        return False
    if exit_observation is not None:
        raise RuntimeError("A second scene exit was requested within one captured match")
    exit_observation = sample
    return False


def draw_enter():
    global draw_before, draw_source_index
    if not active or not ready: return False
    sample = state()
    if not observations.accept("draw_enter", sample["scene_frame"], (machine_context(), sample)):
        return False
    if draw_before is not None: raise RuntimeError("Nested source camera traversal")
    if sample["scene_frame"] != frame_index or frame_index - 1 <= last_drawn_source_index:
        raise RuntimeError("Draw audit requires a new completed scheduler boundary")
    draw_source_index = frame_index - 1
    draw_before = sample
    return False


def draw_return():
    global draw_before, draw_count, last_drawn_source_index
    if not active or not ready: return False
    sample = state()
    if not observations.accept("draw_return", sample["scene_frame"], (machine_context(), sample)):
        return False
    if draw_before is None: raise RuntimeError("Source draw return without entry")
    if sample['scene_frame'] != draw_source_index + 1:
        raise RuntimeError('Source scheduler advanced during camera traversal')
    with (ROOT / "draw-audit.jsonl").open("a") as stream:
        stream.write(json.dumps({"record":"draw", "index":draw_count,
                                "source_index":draw_source_index,
                                "before":draw_before, "after":sample}, sort_keys=True)+"\n")
    last_drawn_source_index = draw_source_index
    draw_before = None
    draw_count += 1
    if frame_index == LIMIT:
        if last_drawn_source_index != LIMIT - 1: raise RuntimeError("Incomplete draw lifecycle")
        return finish()
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
              "pad_state_hex": pad_state()}
    if not observations.accept("entry", 0, (machine_context(), sample)):
        if ready:
            raise RuntimeError("Match entry repeated after initialization")
        return False
    if active:
        raise RuntimeError("A second match entered before reference capture finished")
    active = True
    if input_plan is not None:
        verify_entry(input_plan, sample['start_melee_hex'])
    emit(sample)
    # Hardware polling continues during ordinary VS construction. Queue the
    # first controller intent before that work; by entry return the first
    # source PAD sample may already have been polled into the retail queue.
    supply_input(0)



class Arm(gdb.Command):
    def __init__(self):
        super().__init__("retail-replay-arm", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        global OUTPUT, LIMIT, frame_index, active, ready, published_inputs
        global pad_bootstrapped, pad_sample_index
        values = gdb.string_to_argv(args)
        if len(values) != 2:
            raise gdb.GdbError("retail-replay-arm OUTPUT_PATH FRAME_COUNT")
        if any(bp.is_valid() for bp in breakpoints):
            raise gdb.GdbError("Collector already armed; use a new GDB session")
        OUTPUT = Path(values[0]).resolve()
        LIMIT = int(values[1])
        if not 1 <= LIMIT <= 36000 or OUTPUT.exists():
            raise gdb.GdbError("Require a fresh output path and 1..36000 frames")
        if input_plan is not None and LIMIT != len(input_plan['frames']):
            raise gdb.GdbError('Capture must consume the entire declared input plan')
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        provenance = json.loads((ROOT / "provenance.json").read_text())
        if input_plan is not None:
            provenance['input_plan_sha256'] = input_plan_sha256
        if (provenance.get("dol_sha1") != "08e0bf20134dfcb260699671004527b2d6bb1a45"
                or provenance.get("dolphin_commit") != "c77bbaa0f372c3f72281602a8b087206706542cb"
                or provenance.get("cpu") not in ("Interpreter64", "JITARM64")
                or provenance.get("cpu_thread") is not False
                or provenance.get("cheats") is not False):
            raise gdb.GdbError("Reference provenance does not match the pinned vanilla configuration")
        frame_index = 0
        published_inputs = 0
        pad_bootstrapped = False
        pad_sample_index = None
        active = False
        ready = False
        fighters.clear()
        pending_inputs.clear()
        emit({"record": "header", "schema": "melee-web-retail-replay-candidate",
              "version": 2, "capture_id": uuid.uuid4().hex, "phase": "HSD_GObj_80390CFC_return",
              "input_phase": "HSD_PadRenewMasterStatus_dequeued_slot",
              "initial_phase": "gm_Scene_Vs_OnEnter_entry",
              "game_revision": "GALE01r2", "frames_requested": LIMIT,
              "provenance": provenance,
              "collector_sha256": hashlib.sha256(COLLECTOR.read_bytes() + b"\0" +
                  COLLECTOR.with_name("reference_replay_boundary.py").read_bytes() + b"\0" +
                  COLLECTOR.with_name("retail_input_plan.py").read_bytes()).hexdigest(),
              "writes_game_state": False})
        Observer(0x800693A8, created)
        # Install all observers before execution. Mutating GDB breakpoints
        # inside stop callbacks can retrigger the current remote stop.
        if word(0x8016E9C4) != 0x4E800020:
            raise gdb.GdbError("Pinned VS-entry return instruction does not match")
        Observer(0x8016E934, enter)
        Observer(0x8016E9C4, entered)
        # The entry breakpoint was before OSDisableInterrupts and could race
        # HSD_PadRenewRawStatus's producer. Pin the post-dequeue instruction:
        # r25 is the consumed slot and r6 is its pre-shift qread index here.
        if (word(0x80377534) != 0x4BFCFE31 or
                word(0x80377544) != 0x41820454 or
                word(0x80377578) != 0x7F250214 or
                word(0x80377580) != 0x981E0003 or
                word(PAD_MASTER_CONSUME) != PAD_MASTER_CONSUME_WORD or
                word(0x8037799C) != 0x4BFCF9F1):
            raise gdb.GdbError("Pinned HSD master PAD post-dequeue instructions do not match")
        Observer(PAD_MASTER_CONSUME, pad_consume)
        if (word(0x8034DD8C) != 0x7EC3B378 or
                word(0x8034DD90) != 0x4BFF95FD):
            raise gdb.GdbError("Pinned PADRead pre-restore instructions do not match")
        Observer(0x8034DD8C, pad_read_before_interrupt_restore)
        Observer(0x80390EB4, scheduler_return)
        if word(0x801A4B70) != 0x4E800020:
            raise gdb.GdbError("Pinned scene-exit return instruction does not match")
        Observer(0x801A4B70, exit_requested)
        if DRAW_AUDIT:
            if word(0x80391040) != 0x4E800020:
                raise gdb.GdbError("Pinned camera-traversal return instruction does not match")
            if (ROOT / "draw-audit.jsonl").exists():
                raise gdb.GdbError("Draw audit output already exists")
            Observer(0x80390FC0, draw_enter)
            Observer(0x80391040, draw_return)
        print("Read-only reference collector armed:", OUTPUT)


Arm()
