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
from retail_input_bootstrap import (BootstrapCalibrationError, calibration_record,
                                     load_calibration, load_runtime_binding,
                                     write_calibration, MAX_CONSTRUCTION_PAD_READS)

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
UNTIL_MATCH_END = os.environ.get("MELEE_REPLAY_UNTIL_MATCH_END") == "1"
draw_before = None
draw_count = 0
draw_source_index = None
last_drawn_source_index = -1
published_inputs = 0
pad_bootstrapped = False
pad_sample_index = None
exit_observation = None
BOOTSTRAP_MODE = os.environ.get("MELEE_REPLAY_INPUT_BOOTSTRAP_MODE", "default")
BOOTSTRAP_CALIBRATION_PATH = os.environ.get("MELEE_REPLAY_INPUT_BOOTSTRAP_CALIBRATION")
BOOTSTRAP_RUNTIME_PATH = os.environ.get("MELEE_REPLAY_INPUT_BOOTSTRAP_RUNTIME")
BOOTSTRAP_OUTPUT_PATH = os.environ.get("MELEE_REPLAY_INPUT_BOOTSTRAP_OUTPUT")
# Pinned GALE01r2 game SDK symbols: ``retraceCount`` is read by
# VIGetRetraceCount, while ``InputBufferVcount[0]`` is the SI transfer's
# source vertical-count stamp. Both are read-only identity keys for repeated
# debugger stops; neither is used to advance game state.
RETRACE_COUNT = 0x804D7420
INPUT_BUFFER_VCOUNT = 0x804A7F98
VI_GET_RETRACE_COUNT = 0x8035017C
VI_GET_RETRACE_COUNT_WORDS = (0x806DBD80, 0x4E800020)
construction_pad_reads = 0
last_construction_pad_read = None
bootstrap_applied = False
bootstrap_calibration = None
bootstrap_calibration_sha256 = None
collector_sha256 = None
run_provenance = {}


def discovery_end_ready():
    """Return true only after the source exit and final camera draw agree.

    ``exit_requested`` records the scheduler frame count at the callback.  The
    callback can run immediately before the final scheduler return (count is
    the final source index) or after the final draw (count is one past it), so
    the raw count is retained and both orderings are accepted only when the
    final draw supplies the same source index.
    """
    if not UNTIL_MATCH_END or exit_observation is None or last_drawn_source_index < 0:
        return False
    if last_drawn_source_index != frame_index - 1:
        return False
    if exit_observation.get("frame_index") not in (
            last_drawn_source_index, last_drawn_source_index + 1):
        return False
    # The callback can be observed before the final scheduler return.  Its
    # raw callback count then points at the next source tick, but the source
    # scene counter still identifies the eventual final tick.  Do not infer
    # completion from counts alone: the exact source counter must agree with
    # the final draw that was observed later.
    if exit_observation.get("source_scene_frame") != last_drawn_source_index:
        return False
    return word(0x80479D64) == 1


def memory(address, size):
    if not (0x80000000 <= address <= 0x81800000 - size and 0 < size <= 0x1000):
        raise RuntimeError("Reference memory read is outside bounded retail RAM")
    return bytes(gdb.selected_inferior().read_memory(address, size))


def word(address):
    return struct.unpack(">I", memory(address, 4))[0]


def pad_read_identity(stack, caller, queue, raw):
    return {
        "ordinal": construction_pad_reads,
        "scene_frame": word(0x80479D58),
        "retrace_count": word(RETRACE_COUNT),
        "source_vi_count": word(INPUT_BUFFER_VCOUNT),
        "caller": caller,
        "stack": stack,
        "queue_hex": queue.hex(),
        "raw_hex": raw.hex(),
    }


def bootstrap_calibration_runtime():
    return {
        "dol_sha1": run_provenance.get("dol_sha1"),
        "dolphin_binary_sha256": run_provenance.get("dolphin_binary_sha256"),
        "source_revision": run_provenance.get("source_revision"),
        "cpu": run_provenance.get("cpu"),
        "cpu_thread": run_provenance.get("cpu_thread"),
        "cheats": run_provenance.get("cheats"),
        "background_input": run_provenance.get("background_input"),
        "fixed_rtc": run_provenance.get("fixed_rtc"),
        "setup_snapshot_sha256": run_provenance.get("setup_snapshot_sha256"),
        "dolphin_ini_canonical_sha256": run_provenance.get("dolphin_ini_canonical_sha256"),
        "gcpad_ini_sha256": run_provenance.get("gcpad_ini_sha256"),
        "external_save_hashes": run_provenance.get("external_save_hashes"),
    }


def rng():
    return word(word(0x804D5F94))


def timer_audit(row):
    # Read-only sidecar for fixed timed captures. No new breakpoints, inputs,
    # source state writes, or changes to the existing fighter-state schema.
    if UNTIL_MATCH_END:
        return
    kind = row.get("record")
    path = ROOT / "timer-audit.jsonl"
    if kind == "match_enter":
        raw = bytes.fromhex(row["start_melee_hex"])
        if not (raw[0] & 2):
            return
        value = {"record": "header", "schema": "melee-web-match-timer-audit",
                 "version": 1, "frames_requested": LIMIT,
                 "setup_hex": row["start_melee_hex"],
                 "phase": "after_source_tick_before_audio_transport"}
        with path.open("x") as stream:
            stream.write(json.dumps(value, sort_keys=True) + "\n")
        return
    if not path.exists():
        return
    if kind in ("match_enter_complete", "frame"):
        value = {"record": "initial" if kind == "match_enter_complete" else "frame",
                 "match_frame": word(0x8046B6C4),
                 "seconds": word(0x8046B6C8),
                 "subframe": struct.unpack(">H", memory(0x8046B6CC, 2))[0],
                 "outcome": memory(0x8046B6A8, 1)[0],
                 "end_state": memory(0x8046B6A0, 1)[0]}
        if kind == "frame":
            value["index"] = row["index"]
    elif kind == "end" and row.get("status") == "captured":
        value = {"record": "end", "frames": row["frames"], "status": "captured"}
    else:
        return
    with path.open("a") as stream:
        stream.write(json.dumps(value, sort_keys=True) + "\n")


def emit(row):
    with OUTPUT.open("a") as stream:
        stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
    timer_audit(row)


def supply_input(index):
    global published_inputs
    if input_plan is None: return
    if index == published_inputs - 1:
        return
    if index != published_inputs:
        raise RuntimeError('Controller publication skipped or reordered a planned input: '
                           'requested %d, next %d' % (index, published_inputs))
    controlled_ports = set(input_plan.get('controlled_ports', (1, 2)))
    cpu_pad_modes = input_plan.get('source_cpu_pad_modes', ())
    for port, pad in enumerate(input_plan['frames'][index], 1):
        # CPU slots are driven inside the retail engine. A declared
        # disconnected sample remains in the verified four-port vector but
        # must never be sent through a physical controller pipe. A declared
        # neutral sample is an immutable connected neutral status; publish it
        # as transport setup, without treating that port as human-controlled.
        neutral_cpu_pad = (port <= len(cpu_pad_modes) and
                           cpu_pad_modes[port - 1] == 'neutral')
        if port not in controlled_ports and not neutral_cpu_pad:
            continue
        path = ROOT.parent / 'user' / 'Pipes' / ('pad%d' % port)
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
            if BOOTSTRAP_MODE == "apply":
                if not bootstrap_applied:
                    raise RuntimeError('Calibrated input bootstrap was not applied before source consume')
            elif 1 < LIMIT:
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
    global pad_sample_index, construction_pad_reads, last_construction_pad_read
    global bootstrap_applied
    if not active or input_plan is None:
        return False
    # Preserve the legacy collector's pre-entry behavior: its PADRead
    # observer was inactive until VS entry completed. Only the explicit
    # two-pass modes inspect construction polling.
    if not ready and BOOTSTRAP_MODE == "default":
        return False
    # Preserve the old collector's harmless pre-bootstrap window: a PADRead
    # between VS entry and the first source queue consume was ignored.
    if ready and not pad_bootstrapped:
        return False

    stack = int(gdb.parse_and_eval('$r1'))
    if word(stack + 0x54) != PAD_READ_HSD_CALLER:
        return False

    status_end = int(gdb.parse_and_eval('$r31'))
    raw = memory(status_end - 0x30, 0x30)
    queue = memory(0x804C1F78, 0xC)
    scene = word(0x80479D58)
    retrace_count = word(RETRACE_COUNT)
    source_vi_count = word(INPUT_BUFFER_VCOUNT)
    key = (stack, scene, retrace_count, source_vi_count, queue.hex(), raw.hex())
    observation = (machine_context(), retrace_count, source_vi_count,
                   queue.hex(), raw.hex())
    if not observations.accept('pad_read', key, observation):
        return False

    # During VS construction the SI response is already latched before the
    # first source queue consume.  A calibrated run publishes the successor
    # at the final accepted construction PADRead, which gives Dolphin's
    # source SI poll one complete interval to parse the Pipe command.
    if not ready:
        if construction_pad_reads >= MAX_CONSTRUCTION_PAD_READS:
            raise RuntimeError('Construction PADRead count exceeds calibration bound')
        identity = pad_read_identity(stack, PAD_READ_HSD_CALLER, queue, raw)
        if BOOTSTRAP_MODE == "calibrate":
            construction_pad_reads += 1
            identity["ordinal"] = construction_pad_reads - 1
            last_construction_pad_read = identity
            return False
        if BOOTSTRAP_MODE == "apply":
            if bootstrap_calibration is None:
                raise RuntimeError('Input bootstrap calibration was not loaded')
            expected_count = bootstrap_calibration["construction_pad_reads"]
            if construction_pad_reads >= expected_count:
                raise RuntimeError('Construction PADRead count exceeded calibrated count')
            if construction_pad_reads == expected_count - 1:
                if identity != bootstrap_calibration["last_construction_pad_read"]:
                    raise RuntimeError('Final construction PADRead differs from calibration')
                expected_semantic = (bootstrap_calibration["first_input"] +
                                     ["00" * 10 + "ff", "00" * 10 + "ff"])
                actual_semantic = [raw[offset:offset + 11].hex()
                                   for offset in range(0, 48, 12)]
                if actual_semantic != expected_semantic:
                    raise RuntimeError('Calibrated construction PADRead does not contain plan tick 0')
                supply_input(1)
                bootstrap_applied = True
            construction_pad_reads += 1
            return False
        construction_pad_reads += 1
        last_construction_pad_read = identity
        return False

    if not pad_bootstrapped:
        raise RuntimeError('PADRead observed before source PAD bootstrap')

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
    if frame_index == LIMIT and not DRAW_AUDIT and not UNTIL_MATCH_END:
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


def finish_discovery(stop_reason):
    """Finish a source-length discovery without producing a candidate.

    The discovery JSONL has its own schema and is never accepted by the fixed
    replay validator.  A cap hit remains an explicit incomplete observation;
    it is retained for diagnostics but the runner rejects it as success.
    """
    global active
    if stop_reason == "match_end":
        if not discovery_end_ready():
            raise RuntimeError("Match-end discovery lacks the original exit/final-draw boundary")
        status = "complete"
    elif stop_reason == "frame_cap":
        if frame_index != LIMIT or last_drawn_source_index != LIMIT - 1:
            raise RuntimeError("Frame-cap discovery lacks the final source draw")
        status = "cap_exhausted"
    else:
        raise RuntimeError("Unknown match discovery stop reason")
    emit({"record": "end", "frames": frame_index,
          "stop_reason": stop_reason, "status": status,
          "scene_request": word(0x80479D64),
          "match_end_state": memory(0x8046B6A0, 1)[0],
          "match_result": memory(0x8046B6A8, 1)[0],
          "final_draw_source_index": last_drawn_source_index,
          "exit_observation": exit_observation})
    active = False
    print("Retail match-length discovery finished:", stop_reason, OUTPUT)
    return True


def exit_requested():
    global exit_observation
    if not active or not ready:
        return False
    if UNTIL_MATCH_END:
        sample = {"phase": "gm_801A4B60_return", "frame_index": frame_index,
                  "source_scene_frame": word(0x80479D58),
                  "caller": int(gdb.parse_and_eval("$lr")),
                  "scene_request": word(0x80479D64)}
    else:
        sample = {"phase": "gm_801A4B60_return", "index": frame_index,
                  "caller": int(gdb.parse_and_eval("$lr")),
                  "scene_request": word(0x80479D64)}
    if not observations.accept("exit", frame_index, (machine_context(), sample)):
        return False
    if exit_observation is not None:
        raise RuntimeError("A second scene exit was requested within one captured match")
    exit_observation = sample
    if UNTIL_MATCH_END and discovery_end_ready():
        return finish_discovery("match_end")
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
    if UNTIL_MATCH_END:
        if discovery_end_ready():
            return finish_discovery("match_end")
        if frame_index == LIMIT:
            return finish_discovery("frame_cap")
    elif frame_index == LIMIT:
        if last_drawn_source_index != LIMIT - 1: raise RuntimeError("Incomplete draw lifecycle")
        return finish()
    return False


def entered():
    global ready, active
    if not active:
        return False
    sample = state()
    if not observations.accept("initial", 0, (machine_context(), sample)):
        if frame_index:
            raise RuntimeError("Initial boundary repeated after gameplay started")
        return False
    pending_inputs.clear()
    emit({"record": "match_enter_complete", **sample})
    if BOOTSTRAP_MODE == "calibrate":
        if (BOOTSTRAP_OUTPUT_PATH is None or construction_pad_reads <= 0 or
                last_construction_pad_read is None):
            raise RuntimeError('Construction PADRead calibration did not observe a final read')
        try:
            record = calibration_record(
                plan=input_plan, plan_sha256=input_plan_sha256,
                provenance=run_provenance, collector_sha256=collector_sha256,
                construction_pad_reads=construction_pad_reads,
                last_construction_pad_read=last_construction_pad_read)
            write_calibration(BOOTSTRAP_OUTPUT_PATH, record)
        except BootstrapCalibrationError as error:
            raise RuntimeError(str(error)) from error
        active = False
        print('Input bootstrap calibration captured:', BOOTSTRAP_OUTPUT_PATH)
        # The normal GDB script's final ``continue`` stops here. Its next
        # command is ``quit``, so calibration retains the exact normal
        # neutral/press/release setup without running an unbounded session.
        return True
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
        global pad_bootstrapped, pad_sample_index, construction_pad_reads
        global last_construction_pad_read, bootstrap_applied
        global bootstrap_calibration, bootstrap_calibration_sha256
        global collector_sha256, run_provenance
        values = gdb.string_to_argv(args)
        if len(values) != 2:
            raise gdb.GdbError("retail-replay-arm OUTPUT_PATH FRAME_COUNT")
        if any(bp.is_valid() for bp in breakpoints):
            raise gdb.GdbError("Collector already armed; use a new GDB session")
        if BOOTSTRAP_MODE not in ("default", "calibrate", "apply"):
            raise gdb.GdbError("Unsupported input bootstrap mode")
        if BOOTSTRAP_MODE == "calibrate" and (input_plan is None or
                                                BOOTSTRAP_OUTPUT_PATH is None):
            raise gdb.GdbError("Calibration requires an input plan and output path")
        if BOOTSTRAP_MODE == "apply" and (input_plan is None or
                                             BOOTSTRAP_CALIBRATION_PATH is None):
            raise gdb.GdbError("Calibrated input requires an input plan and calibration path")
        OUTPUT = Path(values[0]).resolve()
        LIMIT = int(values[1])
        if not 1 <= LIMIT <= 36000 or OUTPUT.exists():
            raise gdb.GdbError("Require a fresh output path and 1..36000 frames")
        if UNTIL_MATCH_END:
            if input_plan is None:
                raise gdb.GdbError('Match-length discovery requires a complete input plan')
            if LIMIT > len(input_plan['frames']):
                raise gdb.GdbError('Discovery cap exceeds the declared input plan')
            if not DRAW_AUDIT:
                raise gdb.GdbError('Match-length discovery requires the source draw observer')
        elif input_plan is not None and LIMIT != len(input_plan['frames']):
            raise gdb.GdbError('Capture must consume the entire declared input plan')
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        provenance = json.loads((ROOT / "provenance.json").read_text())
        run_provenance = dict(provenance)
        if input_plan is not None:
            provenance['input_plan_sha256'] = input_plan_sha256
            run_provenance['input_plan_sha256'] = input_plan_sha256
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
        construction_pad_reads = 0
        last_construction_pad_read = None
        bootstrap_applied = False
        active = False
        ready = False
        fighters.clear()
        pending_inputs.clear()
        collector_sha256 = hashlib.sha256(
            COLLECTOR.read_bytes() + b"\0" +
            COLLECTOR.with_name("reference_replay_boundary.py").read_bytes() + b"\0" +
            COLLECTOR.with_name("retail_input_plan.py").read_bytes() + b"\0" +
            COLLECTOR.with_name("retail_input_bootstrap.py").read_bytes()).hexdigest()
        bootstrap_calibration = None
        bootstrap_calibration_sha256 = None
        if BOOTSTRAP_MODE in ("calibrate", "apply"):
            if BOOTSTRAP_RUNTIME_PATH is None:
                raise gdb.GdbError("Two-pass input bootstrap requires a runtime binding sidecar")
            try:
                runtime_binding, _ = load_runtime_binding(BOOTSTRAP_RUNTIME_PATH)
            except (OSError, BootstrapCalibrationError) as error:
                raise gdb.GdbError(str(error)) from error
            run_provenance.update(runtime_binding)
        if BOOTSTRAP_MODE == "apply":
            try:
                bootstrap_calibration, bootstrap_calibration_sha256 = load_calibration(
                    BOOTSTRAP_CALIBRATION_PATH, plan=input_plan,
                    plan_sha256=input_plan_sha256,
                    runtime=bootstrap_calibration_runtime(),
                    collector_sha256=collector_sha256)
            except (OSError, BootstrapCalibrationError) as error:
                raise gdb.GdbError(str(error)) from error
        if UNTIL_MATCH_END:
            emit({"record": "header", "schema": "melee-web-retail-match-discovery",
                  "version": 1, "capture_id": uuid.uuid4().hex,
                  "phase": "HSD_GObj_80390CFC_return",
                  "input_phase": "HSD_PadRenewMasterStatus_dequeued_slot",
                  "initial_phase": "gm_Scene_Vs_OnEnter_entry",
                  "game_revision": "GALE01r2", "frames_cap": LIMIT,
                  "input_plan_frames": len(input_plan['frames']),
                  "input_plan_sha256": input_plan_sha256,
                  "provenance": provenance,
                  "collector_sha256": collector_sha256,
                  "writes_game_state": False})
        else:
            emit({"record": "header", "schema": "melee-web-retail-replay-candidate",
                  "version": 2, "capture_id": uuid.uuid4().hex, "phase": "HSD_GObj_80390CFC_return",
                  "input_phase": "HSD_PadRenewMasterStatus_dequeued_slot",
                  "initial_phase": "gm_Scene_Vs_OnEnter_entry",
                  "game_revision": "GALE01r2", "frames_requested": LIMIT,
                  "provenance": provenance,
                  "collector_sha256": collector_sha256,
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
        if (word(VI_GET_RETRACE_COUNT) != VI_GET_RETRACE_COUNT_WORDS[0] or
                word(VI_GET_RETRACE_COUNT + 4) != VI_GET_RETRACE_COUNT_WORDS[1]):
            raise gdb.GdbError("Pinned VIGetRetraceCount instructions do not match")
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
