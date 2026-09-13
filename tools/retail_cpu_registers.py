"""Validation and code generation for a bounded retail CPU register probe.

The generated code is embedded in a private copy of the ordinary GDB
collector. This module is host-only and does not import GDB.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any, Iterable


SCHEMA = "melee-web-retail-cpu-register-diagnostic"
VERSION = 1
MAX_PROBES = 32
MAX_EXTRA_MEMORY = 32
MAX_LABEL_BYTES = 96
MAX_STACK_BYTES = 256
MAX_BACKCHAIN_DEPTH = 16
RAM_START = 0x80000000
RAM_END = 0x81800000
_LABEL = re.compile(r"[A-Za-z0-9_.:/-]{1,%d}" % MAX_LABEL_BYTES)


class CpuRegisterError(ValueError):
    """Invalid probe or diagnostic configuration."""


@dataclass(frozen=True)
class Probe:
    label: str
    address: int
    expected_word: int
    phase: str = "point"
    call: str | None = None

    def record(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "label": self.label,
            "address": "0x%08x" % self.address,
            "expected_word": "0x%08x" % self.expected_word,
            "phase": self.phase,
        }
        if self.call is not None:
            result["call"] = self.call
        return result


@dataclass(frozen=True)
class ExtraMemory:
    label: str
    address: int
    size: int

    def record(self) -> dict[str, Any]:
        return {"label": self.label, "address": "0x%08x" % self.address,
                "size": self.size}


def _strict_object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CpuRegisterError("duplicate JSON key: %s" % key)
        result[key] = value
    return result


def _integer(value: Any, name: str, low: int, high: int) -> int:
    if isinstance(value, bool):
        raise CpuRegisterError("%s must be an integer" % name)
    if isinstance(value, str):
        if not re.fullmatch(r"0[xX][0-9a-fA-F]+|[0-9]+", value):
            raise CpuRegisterError("%s is not an integer" % name)
        value = int(value, 0)
    if type(value) is not int or not low <= value <= high:
        raise CpuRegisterError("%s is outside its bounded range" % name)
    return value


def _label(value: Any, name: str) -> str:
    if not isinstance(value, str) or not _LABEL.fullmatch(value):
        raise CpuRegisterError("%s is not a bounded diagnostic label" % name)
    return value


def validate_probe(value: Any, index: int = 0) -> Probe:
    if not isinstance(value, dict):
        raise CpuRegisterError("probe %d is not an object" % index)
    if not set(value) <= {"label", "address", "expected_word", "phase", "call"}:
        raise CpuRegisterError("probe %d has unexpected fields" % index)
    if not {"label", "address", "expected_word"} <= set(value):
        raise CpuRegisterError("probe %d is missing required fields" % index)
    label = _label(value["label"], "probe %d label" % index)
    address = _integer(value["address"], "probe %d address" % index,
                       RAM_START, RAM_END - 4)
    if address & 3:
        raise CpuRegisterError("probe %d address is not word aligned" % index)
    expected = _integer(value["expected_word"], "probe %d expected_word" % index,
                        0, 0xffffffff)
    phase = value.get("phase", "point")
    if phase not in ("point", "before", "after"):
        raise CpuRegisterError("probe %d has an invalid phase" % index)
    call = value.get("call")
    if call is not None:
        call = _label(call, "probe %d call" % index)
    if phase != "point" and call is None:
        raise CpuRegisterError("probe %d %s probe requires call" % (index, phase))
    return Probe(label, address, expected, phase, call)


def validate_probes(value: Any) -> tuple[Probe, ...]:
    if isinstance(value, dict):
        if not set(value) <= {"schema", "version", "probes", "extra_memory"}:
            raise CpuRegisterError("probe document has missing or unexpected fields")
        if not {"schema", "version", "probes"} <= set(value):
            raise CpuRegisterError("probe document has missing or unexpected fields")
        if value["schema"] != SCHEMA or value["version"] != VERSION:
            raise CpuRegisterError("unsupported CPU register probe schema")
        value = value["probes"]
    if not isinstance(value, list) or not 1 <= len(value) <= MAX_PROBES:
        raise CpuRegisterError("probe document requires 1..%d probes" % MAX_PROBES)
    probes = tuple(validate_probe(item, index) for index, item in enumerate(value))
    labels = [probe.label for probe in probes]
    if len(set(labels)) != len(labels):
        raise CpuRegisterError("probe labels must be unique")
    return probes


def validate_extra_memory(value: Any) -> tuple[ExtraMemory, ...]:
    if value is None:
        return ()
    if not isinstance(value, list) or len(value) > MAX_EXTRA_MEMORY:
        raise CpuRegisterError("extra_memory requires 0..%d entries" % MAX_EXTRA_MEMORY)
    result = []
    labels = set()
    for index, item in enumerate(value):
        if not isinstance(item, dict) or set(item) != {"label", "address", "size"}:
            raise CpuRegisterError("extra_memory entry %d has invalid fields" % index)
        label = _label(item["label"], "extra_memory %d label" % index)
        if label in labels:
            raise CpuRegisterError("extra_memory labels must be unique")
        labels.add(label)
        address = _integer(item["address"], "extra_memory %d address" % index,
                           RAM_START, RAM_END - 1)
        size = _integer(item["size"], "extra_memory %d size" % index, 1, 0x1000)
        if address > RAM_END - size:
            raise CpuRegisterError("extra_memory %d escapes bounded retail RAM" % index)
        result.append(ExtraMemory(label, address, size))
    return tuple(result)


def load_probe_document(path: str | Path) -> tuple[tuple[Probe, ...], tuple[ExtraMemory, ...], str, bytes]:
    source = Path(path).expanduser().resolve()
    try:
        raw = source.read_bytes()
    except OSError as error:
        raise CpuRegisterError("cannot read probe document: %s" % error) from error
    if not 2 <= len(raw) <= 1024 * 1024:
        raise CpuRegisterError("probe document has an invalid bounded size")
    try:
        value = json.loads(raw.decode("utf-8"),
                           object_pairs_hook=_strict_object_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CpuRegisterError("invalid probe JSON: %s" % error) from error
    if isinstance(value, dict):
        extras = validate_extra_memory(value.get("extra_memory"))
    else:
        extras = ()
    return (validate_probes(value), extras, hashlib.sha256(raw).hexdigest(), raw)


def load_probes(path: str | Path) -> tuple[tuple[Probe, ...], str, bytes]:
    probes, _, digest, raw = load_probe_document(path)
    return probes, digest, raw


def validate_window(start_tick: Any, end_tick: Any, full_frames: int) -> tuple[int, int]:
    start = _integer(start_tick, "start_tick", 0, full_frames - 1)
    end = _integer(end_tick, "end_tick", start, full_frames - 1)
    return start, end


def validate_stack_limits(stack_bytes: Any, backchain_depth: Any) -> tuple[int, int]:
    stack = _integer(stack_bytes, "stack_bytes", 1, MAX_STACK_BYTES)
    depth = _integer(backchain_depth, "backchain_depth", 0, MAX_BACKCHAIN_DEPTH)
    return stack, depth


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def collector_sources(base: Path) -> tuple[Path, ...]:
    """Return the ordinary collector and the files copied by the runner."""
    return tuple(base.with_name(name) for name in (
        "reference_replay_capture.py", "reference_replay_boundary.py",
        "retail_input_plan.py", "retail_input_bootstrap.py"))


def _json_literal(value: Any) -> str:
    return repr(json.dumps(value, sort_keys=True, separators=(",", ":")))


def render_instrumentation(*, probes: Iterable[Probe], start_tick: int,
                           end_tick: int, stack_bytes: int,
                           backchain_depth: int, helper_sha256: str,
                           extra_memory: Iterable[ExtraMemory] = ()) -> str:
    """Render code inserted immediately before the base collector's Arm()."""
    probe_json = [probe.record() for probe in probes]
    extra_json = [region.record() for region in extra_memory]
    return f'''

# ---- bounded CPU register diagnostic (generated; read-only) ----
_CPU_REGISTER_PROBES = json.loads({_json_literal(probe_json)})
_CPU_REGISTER_START = {start_tick}
_CPU_REGISTER_END = {end_tick}
_CPU_REGISTER_STACK_BYTES = {stack_bytes}
_CPU_REGISTER_BACKCHAIN_DEPTH = {backchain_depth}
_CPU_REGISTER_HELPER_SHA256 = {helper_sha256!r}
_CPU_REGISTER_EXTRA_MEMORY = json.loads({_json_literal(extra_json)})
_CPU_REGISTER_OUTPUT = (Path(os.environ["MELEE_CPU_REGISTER_OUTPUT"])
                       if os.environ.get("MELEE_CPU_REGISTER_OUTPUT") else None)
_CPU_REGISTER_STARTED = False
_CPU_REGISTER_FINISHED = False
_CPU_REGISTER_ACTORS = {{}}
_CPU_REGISTER_SEQUENCE = 0
_CPU_REGISTER_PROBE_OBJECTS = []
_CPU_REGISTER_PROBES_ENABLED = False

def _cpu_register_emit(value):
    if not _CPU_REGISTER_OUTPUT:
        return
    _CPU_REGISTER_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with _CPU_REGISTER_OUTPUT.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\\n")

def _cpu_register_u32(raw, offset=0):
    return struct.unpack_from(">I", raw, offset)[0]

def _cpu_register_i32(raw, offset=0):
    return struct.unpack_from(">i", raw, offset)[0]

def _cpu_register_read(address, size):
    if not (0x80000000 <= address <= 0x81800000 - size and 0 < size <= 0x1000):
        raise RuntimeError("diagnostic memory read escaped bounded retail RAM")
    return bytes(gdb.selected_inferior().read_memory(address, size))

def _cpu_register_machine():
    names = ["r%d" % index for index in range(32)]
    registers = {{name: int(gdb.parse_and_eval("$" + name)) & 0xffffffff
                  for name in names}}
    fprs = {{}}
    for index in range(7):
        name = "f%d" % index
        try:
            value = gdb.parse_and_eval("$" + name)
            raw = bytes(value.bytes)
            fprs[name] = {{"bytes_hex": raw.hex(), "length": len(raw),
                          "type": str(value.type)}}
        except Exception as error:
            fprs[name] = {{"error": str(error)}}
    return {{"gpr": registers, "fpr": fprs,
             "pc": int(gdb.parse_and_eval("$pc")) & 0xffffffff,
             "lr": int(gdb.parse_and_eval("$lr")) & 0xffffffff,
             "ctr": int(gdb.parse_and_eval("$ctr")) & 0xffffffff,
             "cr": int(gdb.parse_and_eval("$cr")) & 0xffffffff}}

def _cpu_register_stack(registers):
    result = {{"pointer": registers["r1"], "frames": []}}
    pointer = registers["r1"]
    seen = set()
    for depth in range(_CPU_REGISTER_BACKCHAIN_DEPTH):
        # OSContext.c OSDumpContext uses both 0 and -1 as backchain ends.
        if pointer in (0, 0xffffffff):
            break
        if pointer in seen:
            result["frames"].append({{"depth": depth, "pointer": pointer,
                                      "error": "cyclic source stack backchain"}})
            break
        seen.add(pointer)
        try:
            raw = _cpu_register_read(pointer, max(4, _CPU_REGISTER_STACK_BYTES))
        except Exception as error:
            result["frames"].append({{"depth": depth, "pointer": pointer,
                                      "error": str(error)}})
            break
        next_pointer = _cpu_register_u32(raw)
        result["frames"].append({{"depth": depth, "pointer": pointer,
                                  "backchain": next_pointer,
                                  "bytes_hex": raw.hex()}})
        pointer = next_pointer
    return result

def _cpu_register_fighter(slot, pointer, registers):
    head = _cpu_register_read(pointer, 0x100)
    cpu_pointer = pointer + 0x1A88
    cpu = _cpu_register_read(cpu_pointer, 0x57C)
    damage = _cpu_register_read(pointer + 0x182C, 0x88)
    flags = _cpu_register_read(pointer + 0x2210, 0x20)
    actor = _CPU_REGISTER_ACTORS.get(slot) or _cpu_register_u32(head)
    actor_user_data = _cpu_register_u32(_cpu_register_read(actor + 0x2C, 4))
    if actor_user_data != pointer:
        raise RuntimeError("fighter actor does not own observed fighter pointer")
    return {{
        "slot": slot, "fighter_pointer": pointer,
        "actor_pointer": actor, "actor_user_data": actor_user_data,
        "register_matches": {{name: value for name, value in registers.items()
                              if value in (pointer, actor)}},
        "fighter": {{"kind": _cpu_register_u32(head, 4),
                     "player_id": head[0xC], "motion": _cpu_register_u32(head, 0x10),
                     "animation": _cpu_register_u32(head, 0x14),
                     "facing_bits": head[0x2C:0x30].hex(),
                     "position_bits": [head[offset:offset + 4].hex()
                                       for offset in (0xB0, 0xB4, 0xB8)],
                     "velocity_bits": [head[offset:offset + 4].hex()
                                       for offset in (0x80, 0x84, 0x88)],
                     "knockback_velocity_bits": [head[offset:offset + 4].hex()
                                                  for offset in (0x8C, 0x90, 0x94)],
                     "ground_air": _cpu_register_u32(head, 0xE0)}},
        "cpu": {{"pointer": cpu_pointer, "kind": _cpu_register_i32(cpu, 0xC),
                 "level": _cpu_register_i32(cpu, 0x10),
                 "state": _cpu_register_i32(cpu, 0x18),
                 "default_state": _cpu_register_i32(cpu, 0x1C),
                 "secondary_state": _cpu_register_i32(cpu, 0x20),
                 "buttons": _cpu_register_u32(cpu),
                 "sticks": list(struct.unpack_from("bbbb", cpu, 4)),
                 "triggers": [cpu[8], cpu[9]], "raw_hex": cpu.hex()}},
        "knockback": {{"percent_bits": damage[4:8].hex(),
                       "kb_applied_bits": damage[0x24:0x28].hex(),
                       "angle": _cpu_register_i32(damage, 0x1C),
                       "magnitude_bits": damage[0x78:0x7C].hex(),
                       "time_since_hit": _cpu_register_i32(damage, 0x80),
                       "raw_hex": damage.hex()}},
        "flags": {{"x2210_2230_hex": flags.hex(),
                  # PowerPC big-endian u8 bitfields number b0 from the MSB.
                  "x221a": flags[0xA], "x221a_b3": (flags[0xA] >> 4) & 1,
                  "allow_sdi": (flags[0xA] >> 5) & 1,
                  "fall_fast": (flags[0xA] >> 3) & 1}},
    }}

def _cpu_register_fighters(registers):
    result = []
    for slot, pointer in sorted(fighters.items()):
        try:
            result.append(_cpu_register_fighter(slot, pointer, registers))
        except Exception as error:
            result.append({{"slot": slot, "fighter_pointer": pointer,
                           "error": str(error)}})
    return result

def _cpu_register_sample(spec, tick):
    global _CPU_REGISTER_SEQUENCE
    try:
        registers = _cpu_register_machine()
        value = {{"record": "probe", "sequence": _CPU_REGISTER_SEQUENCE,
                  "tick": tick, "probe": spec, "registers": registers,
                  "stack": _cpu_register_stack(registers["gpr"]),
                  "fighters": _cpu_register_fighters(registers["gpr"]),
                  "extra_memory": [
                      dict(region, bytes_hex=_cpu_register_read(
                          int(region["address"], 16), region["size"]).hex())
                      for region in _CPU_REGISTER_EXTRA_MEMORY]}}
    except Exception as error:
        value = {{"record": "probe_error", "sequence": _CPU_REGISTER_SEQUENCE,
                  "tick": tick, "probe": spec, "error": str(error)}}
    _CPU_REGISTER_SEQUENCE += 1
    _cpu_register_emit(value)
    return False

class _CpuRegisterProbe(gdb.Breakpoint):
    def __init__(self, spec):
        self.spec = spec
        address = int(spec["address"], 16)
        expected = int(spec["expected_word"], 16)
        if word(address) != expected:
            raise gdb.GdbError("CPU register probe instruction mismatch at 0x%08x" % address)
        super().__init__("*0x%08x" % address, gdb.BP_HARDWARE_BREAKPOINT, internal=True)
        self.silent = True
        self.enabled = False
        breakpoints.append(self)
    def stop(self):
        if (not _CPU_REGISTER_STARTED or _CPU_REGISTER_FINISHED or
                not active or not ready or source_finished):
            return False
        if _CPU_REGISTER_START <= frame_index <= _CPU_REGISTER_END:
            return _cpu_register_sample(self.spec, frame_index)
        return False

_cpu_register_original_created = created
def created():
    if active and not source_finished:
        actor = int(gdb.parse_and_eval("$r3")) & 0xffffffff
        _cpu_register_original_created()
        pointer = word(actor + 0x2C)
        slot = memory(pointer + 0xC, 1)[0]
        if slot in fighters:
            _CPU_REGISTER_ACTORS[slot] = actor
        return
    return _cpu_register_original_created()

_cpu_register_original_entered = entered
def entered():
    global _CPU_REGISTER_STARTED, _CPU_REGISTER_PROBES_ENABLED
    result = _cpu_register_original_entered()
    if ready and not _CPU_REGISTER_STARTED:
        _CPU_REGISTER_STARTED = True
        if _CPU_REGISTER_START == 0:
            for probe in _CPU_REGISTER_PROBE_OBJECTS:
                probe.enabled = True
            _CPU_REGISTER_PROBES_ENABLED = True
        _cpu_register_emit({{"record": "header", "schema": {SCHEMA!r},
                            "version": {VERSION}, "status": "diagnostic_only",
                            "window": {{"start_tick": _CPU_REGISTER_START,
                                       "end_tick": _CPU_REGISTER_END}},
                            "probes": _CPU_REGISTER_PROBES,
                            "extra_memory": _CPU_REGISTER_EXTRA_MEMORY,
                            "helper_sha256": _CPU_REGISTER_HELPER_SHA256,
                            "input_plan_sha256": os.environ.get(
                                "MELEE_CPU_REGISTER_PLAN_SHA256")}})
    return result

_cpu_register_original_scheduler = scheduler_return
def scheduler_return():
    global _CPU_REGISTER_FINISHED, _CPU_REGISTER_PROBES_ENABLED
    tick = frame_index
    result = _cpu_register_original_scheduler()
    if (_CPU_REGISTER_STARTED and not _CPU_REGISTER_PROBES_ENABLED and
            frame_index >= _CPU_REGISTER_START and not _CPU_REGISTER_FINISHED):
        for probe in _CPU_REGISTER_PROBE_OBJECTS:
            probe.enabled = True
        _CPU_REGISTER_PROBES_ENABLED = True
    if (_CPU_REGISTER_STARTED and not _CPU_REGISTER_FINISHED and
            frame_index > _CPU_REGISTER_END):
        for probe in _CPU_REGISTER_PROBE_OBJECTS:
            probe.enabled = False
        _CPU_REGISTER_PROBES_ENABLED = False
        _CPU_REGISTER_FINISHED = True
        _cpu_register_emit({{"record": "end", "status": "diagnostic_window_complete",
                            "frames": frame_index,
                            "window_end_tick": _CPU_REGISTER_END}})
    return result

_cpu_register_original_arm_invoke = Arm.invoke
def _cpu_register_arm_invoke(self, args, from_tty):
    result = _cpu_register_original_arm_invoke(self, args, from_tty)
    global _CPU_REGISTER_PROBE_OBJECTS, _CPU_REGISTER_PROBES_ENABLED
    _CPU_REGISTER_PROBE_OBJECTS = []
    _CPU_REGISTER_PROBES_ENABLED = False
    for spec in _CPU_REGISTER_PROBES:
        _CPU_REGISTER_PROBE_OBJECTS.append(_CpuRegisterProbe(spec))
    return result
Arm.invoke = _cpu_register_arm_invoke
# ---- end bounded CPU register diagnostic ----
'''


def instrument_collector(base: bytes, *, probes: Iterable[Probe], start_tick: int,
                         end_tick: int, stack_bytes: int, backchain_depth: int,
                         helper_sha256: str,
                         extra_memory: Iterable[ExtraMemory] = ()) -> bytes:
    """Return an instrumented collector while preserving the base verbatim."""
    if base.count(b"\nArm()\n") != 1 or not base.endswith(b"Arm()\n"):
        raise CpuRegisterError("collector does not have expected terminal Arm()")
    prefix = base[:-len(b"Arm()\n")]
    injection = render_instrumentation(
        probes=probes, start_tick=start_tick, end_tick=end_tick,
        stack_bytes=stack_bytes, backchain_depth=backchain_depth,
        helper_sha256=helper_sha256, extra_memory=extra_memory).encode("utf-8")
    return prefix + injection + b"\nArm()\n"


def _bounded_hex(value: Any, name: str, *, length: int | None = None) -> None:
    if not isinstance(value, str) or len(value) % 2 or not re.fullmatch(r"[0-9a-fA-F]*", value):
        raise CpuRegisterError("%s is not canonical hexadecimal bytes" % name)
    if length is not None and len(value) != length * 2:
        raise CpuRegisterError("%s has an unexpected byte length" % name)


def _bounded_u32(value: Any, name: str) -> None:
    if isinstance(value, bool) or type(value) is not int or not 0 <= value <= 0xffffffff:
        raise CpuRegisterError("%s is not a raw unsigned 32-bit value" % name)


def _bounded_s32(value: Any, name: str) -> None:
    if isinstance(value, bool) or type(value) is not int or not -0x80000000 <= value <= 0x7fffffff:
        raise CpuRegisterError("%s is not a raw signed 32-bit value" % name)


def _bounded_vector(value: Any, name: str) -> None:
    if not isinstance(value, list) or len(value) != 3:
        raise CpuRegisterError("%s is not a three-component raw vector" % name)
    for index, item in enumerate(value):
        _bounded_hex(item, "%s[%d]" % (name, index), length=4)


def _validate_register_state(row: dict[str, Any]) -> None:
    registers = row.get("registers")
    if not isinstance(registers, dict):
        raise CpuRegisterError("probe row is missing raw registers")
    if set(registers) != {"gpr", "fpr", "pc", "lr", "ctr", "cr"}:
        raise CpuRegisterError("probe row has incomplete raw register groups")
    gpr = registers["gpr"]
    if not isinstance(gpr, dict) or set(gpr) != {"r%d" % i for i in range(32)}:
        raise CpuRegisterError("probe row is missing one or more raw GPRs")
    for name, value in gpr.items():
        _bounded_u32(value, name)
    for name in ("pc", "lr", "ctr", "cr"):
        _bounded_u32(registers[name], name)
    fpr = registers["fpr"]
    if not isinstance(fpr, dict) or set(fpr) != {"f%d" % i for i in range(7)}:
        raise CpuRegisterError("probe row is missing one or more raw FPRs")
    for name, value in fpr.items():
        if not isinstance(value, dict):
            raise CpuRegisterError("%s is not a raw FPR record" % name)
        if "error" in value:
            if set(value) != {"error"} or not isinstance(value["error"], str):
                raise CpuRegisterError("%s has an invalid FPR error record" % name)
            continue
        if set(value) != {"bytes_hex", "length", "type"}:
            raise CpuRegisterError("%s is missing raw FPR bytes" % name)
        _bounded_hex(value["bytes_hex"], name + ".bytes_hex", length=8)
        if type(value["length"]) is not int or value["length"] != len(value["bytes_hex"]) // 2:
            raise CpuRegisterError("%s has an invalid raw FPR length" % name)
        if not isinstance(value["type"], str) or not value["type"]:
            raise CpuRegisterError("%s has an invalid raw FPR type" % name)
    stack = row.get("stack")
    if not isinstance(stack, dict) or set(stack) != {"pointer", "frames"}:
        raise CpuRegisterError("probe row is missing bounded stack data")
    _bounded_u32(stack["pointer"], "stack.pointer")
    if not isinstance(stack["frames"], list) or len(stack["frames"]) > MAX_BACKCHAIN_DEPTH:
        raise CpuRegisterError("probe row has an invalid stack backchain")
    for frame in stack["frames"]:
        if not isinstance(frame, dict) or "depth" not in frame or "pointer" not in frame:
            raise CpuRegisterError("stack frame is incomplete")
        if type(frame["depth"]) is not int or not 0 <= frame["depth"] < MAX_BACKCHAIN_DEPTH:
            raise CpuRegisterError("stack frame depth is outside its bound")
        _bounded_u32(frame["pointer"], "stack.frame.pointer")
        if "error" in frame:
            if set(frame) != {"depth", "pointer", "error"} or not isinstance(frame["error"], str):
                raise CpuRegisterError("stack frame has an invalid error record")
        elif set(frame) != {"depth", "pointer", "backchain", "bytes_hex"}:
            raise CpuRegisterError("stack frame is missing raw bytes")
        else:
            _bounded_u32(frame["backchain"], "stack.frame.backchain")
            _bounded_hex(frame["bytes_hex"], "stack.frame.bytes_hex")


def _validate_fighter_state(row: dict[str, Any]) -> None:
    fighters = row.get("fighters")
    if not isinstance(fighters, list) or not 1 <= len(fighters) <= 4:
        raise CpuRegisterError("probe row has no bounded fighter state")
    slots = []
    for fighter in fighters:
        required = {"slot", "fighter_pointer", "actor_pointer", "actor_user_data",
                    "register_matches", "fighter", "cpu", "knockback", "flags"}
        if not isinstance(fighter, dict) or not required <= set(fighter):
            raise CpuRegisterError("fighter state is incomplete")
        if type(fighter["slot"]) is not int or not 0 <= fighter["slot"] < 4:
            raise CpuRegisterError("fighter slot is outside its bound")
        slots.append(fighter["slot"])
        for name in ("fighter_pointer", "actor_pointer"):
            _bounded_u32(fighter[name], "fighter." + name)
        _bounded_u32(fighter["actor_user_data"], "fighter.actor_user_data")
        if fighter["actor_user_data"] != fighter["fighter_pointer"]:
            raise CpuRegisterError("actor does not own observed fighter pointer")
        if not isinstance(fighter["register_matches"], dict):
            raise CpuRegisterError("fighter register matches are missing")
        for name, pointer in fighter["register_matches"].items():
            if not re.fullmatch(r"r(?:[0-9]|[12][0-9]|3[01])", name):
                raise CpuRegisterError("fighter register match has an invalid register")
            _bounded_u32(pointer, "fighter.register_matches." + name)
        state = fighter["fighter"]
        if not isinstance(state, dict):
            raise CpuRegisterError("fighter state is not an object")
        for name in ("kind", "player_id", "motion", "animation", "ground_air"):
            _bounded_u32(state.get(name), "fighter." + name)
        for name in ("facing_bits",):
            _bounded_hex(state.get(name), "fighter." + name, length=4)
        for name in ("position_bits", "velocity_bits", "knockback_velocity_bits"):
            _bounded_vector(state.get(name), "fighter." + name)
        cpu = fighter["cpu"]
        if not isinstance(cpu, dict):
            raise CpuRegisterError("fighter CPU state is missing")
        _bounded_u32(cpu.get("pointer"), "fighter.cpu.pointer")
        for name in ("kind", "level", "state", "default_state", "secondary_state"):
            _bounded_s32(cpu.get(name), "fighter.cpu." + name)
        _bounded_u32(cpu.get("buttons"), "fighter.cpu.buttons")
        if not isinstance(cpu.get("sticks"), list) or len(cpu["sticks"]) != 4:
            raise CpuRegisterError("fighter CPU sticks are missing")
        if any(type(value) is not int or not -128 <= value <= 127 for value in cpu["sticks"]):
            raise CpuRegisterError("fighter CPU sticks are invalid")
        if (not isinstance(cpu.get("triggers"), list) or len(cpu["triggers"]) != 2 or
                any(type(value) is not int or not 0 <= value <= 255 for value in cpu["triggers"])):
            raise CpuRegisterError("fighter CPU triggers are invalid")
        _bounded_hex(cpu.get("raw_hex"), "fighter.cpu.raw_hex", length=0x57c)
        knockback = fighter["knockback"]
        if not isinstance(knockback, dict):
            raise CpuRegisterError("fighter knockback state is missing")
        for name in ("percent_bits", "kb_applied_bits", "magnitude_bits"):
            _bounded_hex(knockback.get(name), "fighter.knockback." + name, length=4)
        _bounded_s32(knockback.get("angle"), "fighter.knockback.angle")
        _bounded_s32(knockback.get("time_since_hit"), "fighter.knockback.time_since_hit")
        _bounded_hex(knockback.get("raw_hex"), "fighter.knockback.raw_hex", length=0x88)
        flags = fighter["flags"]
        if not isinstance(flags, dict) or not {"x2210_2230_hex", "x221a", "x221a_b3"} <= set(flags):
            raise CpuRegisterError("fighter flags are incomplete")
        _bounded_hex(flags["x2210_2230_hex"], "fighter.flags", length=0x20)
        if type(flags["x221a"]) is not int or not 0 <= flags["x221a"] <= 255:
            raise CpuRegisterError("fighter flags are invalid")
        if type(flags["x221a_b3"]) is not int or flags["x221a_b3"] not in (0, 1):
            raise CpuRegisterError("fighter flags are invalid")
    if slots != sorted(set(slots)):
        raise CpuRegisterError("fighter slots are duplicated or unordered")


def diagnostic_errors(rows: Iterable[dict[str, Any]]) -> list[str]:
    """Return every explicit read error so callers cannot accept partial state."""
    result: list[str] = []
    def walk(value: Any, path: str) -> None:
        if isinstance(value, dict):
            if "error" in value and isinstance(value["error"], str):
                result.append(path + ": " + value["error"])
            for key, child in value.items():
                walk(child, path + "." + str(key))
        elif isinstance(value, list):
            for index, child in enumerate(value):
                walk(child, "%s[%d]" % (path, index))
    for index, row in enumerate(rows):
        walk(row, "row[%d]" % index)
    return result


def load_diagnostic(path: str | Path, *, probes: Iterable[Probe],
                    start_tick: int, end_tick: int,
                    extra_memory: Iterable[ExtraMemory] = (),
                    input_plan_sha256: str | None = None) -> tuple[list[dict[str, Any]], str]:
    """Load the sidecar and enforce complete diagnostic record shape."""
    source = Path(path)
    try:
        raw = source.read_bytes()
    except OSError as error:
        raise CpuRegisterError("cannot read diagnostic output: %s" % error) from error
    if not 2 <= len(raw) <= 256 * 1024 * 1024:
        raise CpuRegisterError("diagnostic output has an invalid bounded size")
    rows: list[dict[str, Any]] = []
    try:
        for line_number, line in enumerate(raw.splitlines(), 1):
            row = json.loads(line, object_pairs_hook=_strict_object_pairs)
            if not isinstance(row, dict):
                raise CpuRegisterError("diagnostic row %d is not an object" % line_number)
            rows.append(row)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CpuRegisterError("invalid diagnostic JSON: %s" % error) from error
    if not rows:
        raise CpuRegisterError("diagnostic output is empty")
    header = rows[0]
    if (header.get("record") != "header" or header.get("schema") != SCHEMA or
            header.get("version") != VERSION or header.get("status") != "diagnostic_only"):
        raise CpuRegisterError("diagnostic header does not identify diagnostic-only output")
    probes = tuple(probes)
    extra_memory = tuple(extra_memory)
    if header.get("window") != {"start_tick": start_tick, "end_tick": end_tick}:
        raise CpuRegisterError("diagnostic window does not match requested bound")
    expected = [probe.record() for probe in probes]
    if header.get("probes") != expected:
        raise CpuRegisterError("diagnostic header probe identity does not match definitions")
    expected_extra = [region.record() for region in extra_memory]
    if header.get("extra_memory") != expected_extra:
        raise CpuRegisterError("diagnostic header extra-memory identity does not match definitions")
    if not isinstance(header.get("helper_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", header["helper_sha256"]):
        raise CpuRegisterError("diagnostic header lacks helper identity")
    plan_hash = header.get("input_plan_sha256")
    if not isinstance(plan_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", plan_hash):
        raise CpuRegisterError("diagnostic header lacks full input-plan identity")
    if input_plan_sha256 is not None and plan_hash != input_plan_sha256:
        raise CpuRegisterError("diagnostic input-plan identity does not match full plan")
    by_label = {probe.label: probe.record() for probe in probes}
    sequence = 0
    for row in rows[1:]:
        if row.get("record") in ("probe", "probe_error"):
            probe = row.get("probe")
            if not isinstance(probe, dict) or probe.get("label") not in by_label:
                raise CpuRegisterError("diagnostic row names an unknown probe")
            if probe != by_label[probe["label"]]:
                raise CpuRegisterError("diagnostic row probe identity differs from header")
            if type(row.get("tick")) is not int or not start_tick <= row["tick"] <= end_tick:
                raise CpuRegisterError("diagnostic probe tick is outside requested window")
            if row.get("sequence") != sequence:
                raise CpuRegisterError("diagnostic probe sequence is not contiguous")
            sequence += 1
            if row.get("record") == "probe":
                _validate_register_state(row)
                _validate_fighter_state(row)
                extras = row.get("extra_memory")
                expected_by_label = {item["label"]: item for item in expected_extra}
                if not isinstance(extras, list) or len(extras) != len(expected_extra):
                    raise CpuRegisterError("diagnostic extra-memory rows are incomplete")
                for item, expected_item in zip(extras, expected_extra):
                    if item.get("label") not in expected_by_label:
                        raise CpuRegisterError("diagnostic extra-memory rows are incomplete")
                    if item.get("address") != expected_item["address"] or item.get("size") != expected_item["size"]:
                        raise CpuRegisterError("diagnostic extra-memory identity differs from header")
                    if "error" in item:
                        if set(item) != {"label", "address", "size", "error"} or not isinstance(item["error"], str):
                            raise CpuRegisterError("diagnostic extra-memory error is malformed")
                    else:
                        if set(item) != {"label", "address", "size", "bytes_hex"}:
                            raise CpuRegisterError("diagnostic extra-memory bytes are missing")
                        _bounded_hex(item["bytes_hex"], "diagnostic extra-memory bytes",
                                     length=item["size"])
        elif row.get("record") == "end":
            continue
        else:
            raise CpuRegisterError("diagnostic output contains an unknown record")
    ends = [row for row in rows[1:] if row.get("record") == "end"]
    if (len(ends) != 1 or rows[-1] is not ends[0] or
            ends[0].get("status") != "diagnostic_window_complete" or
            ends[0].get("frames") != end_tick + 1 or
            ends[0].get("window_end_tick") != end_tick):
        raise CpuRegisterError("diagnostic output lacks its bounded end record")
    return rows, hashlib.sha256(raw).hexdigest()
