"""Decode the passive observer's bounded source-owned slices.

Only raw PADRead/queue samples are replay inputs. CpuObservation remains the
existing expected-output decoder. No captured address is an implementation
input to the port; addresses here resolve the retail observer's typed owners.
"""
from __future__ import annotations

import struct
from pathlib import Path
from retail_cpu_observation import CpuObservation
from retail_replay_validation import _validate_fighter, _pad_state
from retail_setup_validation import _decode_setup


class SemanticError(ValueError):
    pass


class SliceMemory:
    def __init__(self, slices):
        self.spans = []
        for item in slices:
            address = item["address"]
            if isinstance(address, str):
                address = int(address, 16)
            raw = bytes.fromhex(item["hex"])
            if not (0x80000000 <= address < 0x81800000 and
                    0 < len(raw) <= 0x1000 and address + len(raw) <= 0x81800000):
                raise SemanticError("Invalid typed observer slice")
            for start, data in self.spans:
                left, right = max(start, address), min(start + len(data), address + len(raw))
                if left < right and data[left-start:right-start] != raw[left-address:right-address]:
                    raise SemanticError("Conflicting overlapping observer slices")
            self.spans.append((address, raw))

    def __call__(self, address, size):
        for start, raw in self.spans:
            if start <= address and address + size <= start + len(raw):
                return raw[address-start:address-start+size]
        raise SemanticError(f"Missing typed observer coverage at {address:08x}, {size} bytes")

    def word(self, address):
        return struct.unpack(">I", self(address, 4))[0]


def fighter_state(memory, slot, pointer):
    """Same declared fields and offsets as reference_replay_capture.py."""
    def raw(offset, count): return memory(pointer + offset, count)
    def bits(offset): return raw(offset, 4).hex()
    def word(offset): return struct.unpack(">I", raw(offset, 4))[0]
    def vector(offset): return [bits(offset + i * 4) for i in range(3)]
    value = {"slot": slot, "kind": word(4), "motion": word(0x10), "animation": word(0x14),
             "facing_bits": bits(0x2C), "position_bits": vector(0xB0),
             "velocity_bits": vector(0x80), "knockback_bits": vector(0x8C),
             "ground_air": word(0xE0), "animation_frame_bits": bits(0x894),
             "animation_speed_bits": bits(0x89C), "damage_bits": bits(0x1830),
             "shield_bits": bits(0x1998), "input_hex": raw(0x620, 0x6C).hex(),
             "stocks": struct.unpack("b", memory(0x80453080 + slot * 0xE90 + 0x8E, 1))[0]}
    _validate_fighter(value, slot, "passive fighter")
    return value


def pad_snapshot(memory):
    config = memory(0x804C1F84, 0x20)
    value = config[:10] + config[12:]
    for base in (0x804C1FAC, 0x804C20BC, 0x804C21CC):
        bank = memory(base, 0x110)
        value += b"".join(bank[i:i+66] for i in range(0, 0x110, 68))
    value = value.hex()
    _pad_state(value, "passive PAD state")
    return value


def state_snapshot(memory, fighters):
    return {"rng": memory.word(memory.word(0x804D5F94)),
            "scene_frame": memory.word(0x80479D58),
            "match_frame": memory.word(0x8046B6C4),
            "pad_state_hex": pad_snapshot(memory),
            "fighters": [fighter_state(memory, slot, pointer)
                         for slot, pointer in sorted(fighters.items())]}


BOUNDARIES = {0x8034DD8C: "pad_poll", 0x80377584: "pad_consume",
              0x800693A8: "fighter_create", 0x8016E934: "match_enter",
              0x8016E9C4: "match_initial", 0x80390EB4: "source_tick",
              0x80390FC0: "draw_enter", 0x80391040: "draw_return",
              0x8016E9C8: "result_enter", 0x8016EBBC: "result_return",
              0x8039157C: "scene_reset", 0x801A4B70: "exit_requested"}
RAW_BOUNDARY_NAMES = {**BOUNDARIES, 0x8016E934: "entry", 0x8016E9C4: "setup",
                      0x8039157C: "scene_teardown", 0x801A4B70: "scene_exit"}


class SemanticSession:
    def __init__(self):
        self.fighters = {}
        self.types = []
        self.enter = None
        self.initial = None
        self.frame_count = 0
        self.draw_count = 0
        self.poll_count = 0
        self.pending = []
        self.draw_before = None
        self.last_draw = -1
        self.exit = None
        self.result_pointer = None
        self.result = None
        self.complete = False
        self.scene_routing = None

    def cpu_snapshot(self, memory):
        observer = CpuObservation(Path("unused"), memory)
        observer.types = self.types
        return observer.snapshot(self.fighters)

    def consume(self, row):
        payload = row["payload"]
        pc = payload.get("pc", 0)
        if isinstance(pc, str): pc = int(pc, 16)
        if row["event"] == "boundary" and (
                pc not in BOUNDARIES or payload.get("boundary") != RAW_BOUNDARY_NAMES[pc]):
            raise SemanticError("Observer boundary identity disagrees with its source PC")
        event = BOUNDARIES.get(pc, row["event"])
        if self.complete and event in BOUNDARIES.values():
            raise SemanticError("Original observer boundary after completed teardown")
        if event in ("source_tick", "draw_enter", "draw_return") and self.initial is None:
            raise SemanticError("Gameplay observation before match construction completed")
        memory = SliceMemory(payload.get("slices", []))
        registers = payload.get("gprs", [0] * 32)
        def reg(index):
            value = registers[index] if isinstance(registers, list) else registers[str(index)]
            return int(value, 16) if isinstance(value, str) else value
        result = {key: row[key] for key in ("seq", "source_tick", "draw_ordinal")}
        result.update(event=event, payload={"source_pc": f"{pc:08x}"})
        out = result["payload"]
        # Scene observations remain useful before/after the supported match.
        # Only emit fields actually present in this boundary's typed slices.
        for address, data in memory.spans:
            if address == 0x80479D30 and len(data) >= 6:
                out["scene_routing_hex"] = data[:6].hex()
                if out["scene_routing_hex"] != self.scene_routing:
                    out["scene_transition"] = {"previous_routing_hex": self.scene_routing,
                                               "routing_hex": out["scene_routing_hex"]}
                    self.scene_routing = out["scene_routing_hex"]
            if address <= 0x80479D58 and 0x80479D5C <= address + len(data):
                out["source_scene_frame"] = memory.word(0x80479D58)
        active = self.enter is not None and not self.complete
        if event == "pad_poll":
            raw = memory(reg(31) - 0x30, 0x30)
            if active and raw[10] != 0:
                raise SemanticError("Human controller reported a PAD error during the match")
            out.update(poll_index=self.poll_count,
                       ports=[raw[i:i+11].hex() for i in range(0, 48, 12)],
                       caller=memory.word(reg(1) + 0x54),
                       retrace=memory.word(0x804D7420))
            self.poll_count += 1
        elif event == "match_enter":
            setup = memory(reg(3), 0x138)
            # Title attract demos use this same source constructor. Preserve
            # their entry as scene coverage without admitting demo fighters
            # into the human-versus-CPU comparison lifecycle.
            if not setup[4] & 0x40:
                result["event"] = "non_vs_entry"
                out["versus_match"] = False
                return result
            if self.enter is not None: raise SemanticError("Multiple matches in one capture")
            self.types = [setup[0x61+i*0x24] for i in range(2)]
            if self.types != [0, 1] or any(setup[0x61+i*0x24] != 3 for i in range(2, 6)):
                raise SemanticError("First release requires one human P1 versus one CPU P2")
            self.enter = {"record": "match_enter", "start_melee_hex": setup.hex(),
                          "rng": memory.word(memory.word(0x804D5F94)),
                          "pad_state_hex": pad_snapshot(memory)}
            out.update(retail=self.enter, human_ports=[0], cpu_output_ports=[1],
                       declared_setup=_decode_setup(setup.hex()))
        elif event == "fighter_create" and active:
            pointer = memory.word(reg(3) + 0x2C)
            slot = memory(pointer + 0xC, 1)[0]
            if slot not in (0, 1) or slot in self.fighters:
                raise SemanticError("Unexpected or duplicate fighter creation")
            self.fighters[slot] = pointer
            out.update(slot=slot, allocation_identity=f"{pointer:08x}")
        elif event == "match_initial" and active:
            if self.initial is not None or sorted(self.fighters) != [0, 1]:
                raise SemanticError("Invalid match construction boundary")
            self.initial = {"record": "match_enter_complete", **state_snapshot(memory, self.fighters)}
            out.update(retail=self.initial, cpu=self.cpu_snapshot(memory))
        elif event == "pad_consume" and active and self.initial is not None and self.result is None:
            queue = memory(0x804C1F78, 0xC)
            read = reg(6) & 255
            if not queue[0] or read >= queue[0] or reg(25) != struct.unpack_from(">I", queue, 8)[0] + read*48:
                raise SemanticError("PAD consumption does not identify the source queue slot")
            raw = memory(reg(25), 48)
            ports = [raw[i:i+11].hex() for i in range(0, 48, 12)]
            # CPU decisions live in Fighter.Cpu, never this hardware queue.
            self.pending.append(ports)
            out.update(ports=ports, human_ports=[0], queue_hex=queue.hex(), queue_read=read)
        elif event == "source_tick" and active and self.initial is not None and self.result is None:
            sample = state_snapshot(memory, self.fighters)
            if sample["scene_frame"] != self.frame_count or len(self.pending) != 1:
                raise SemanticError("Lost/reordered source tick or PAD consumption")
            if row["source_tick"] != sample["scene_frame"]:
                raise SemanticError("Source tick envelope disagrees with observed source state")
            out.update(retail={"record": "frame", "index": self.frame_count,
                               "consumed_inputs": self.pending, **sample},
                       cpu=self.cpu_snapshot(memory))
            self.frame_count += 1
            self.pending = []
        elif event == "draw_enter" and active and self.initial is not None and self.result is None:
            if self.draw_before is not None: raise SemanticError("Nested source draw")
            if row["draw_ordinal"] != self.draw_count:
                raise SemanticError("Lost/reordered source draw ordinal")
            self.draw_before = state_snapshot(memory, self.fighters)
        elif event == "draw_return" and active and self.initial is not None and self.result is None:
            if row["draw_ordinal"] != self.draw_count:
                raise SemanticError("Lost/reordered source draw ordinal")
            after = state_snapshot(memory, self.fighters)
            if self.draw_before is None or after["scene_frame"] != self.frame_count:
                raise SemanticError("Unpaired draw or scheduler advanced during draw")
            out.update(draw={"record": "draw", "index": self.draw_count,
                             "source_index": self.frame_count-1,
                             "before": self.draw_before, "after": after},
                       cpu=self.cpu_snapshot(memory))
            self.draw_count += 1
            self.last_draw = self.frame_count - 1
            self.draw_before = None
        elif event == "exit_requested" and active:
            self.exit = {"scene_request": memory.word(0x80479D64), "source_ticks": self.frame_count}
            out.update(self.exit)
        elif event == "result_enter" and active:
            if self.result_pointer is not None:
                raise SemanticError("Duplicate result construction entry")
            self.result_pointer = reg(3)
            if self.result_pointer != 0x80479D98:
                raise SemanticError("Unexpected ordinary VS result destination")
        elif event == "result_return" and active:
            if self.result is not None:
                raise SemanticError("Duplicate result publication")
            if self.result_pointer is None or self.exit is None:
                raise SemanticError("Result publication lacks its entry/exit request")
            raw = memory(self.result_pointer + 0xC, 0x28)
            winners = list(raw[0x10:0x10+raw[0xD]])
            if not winners or len(winners) > 2 or len(set(winners)) != len(winners) or any(x > 1 for x in winners):
                raise SemanticError("Invalid source winners")
            self.result = {"outcome": raw[4], "winners": winners}
            out.update(result=self.result)
        elif event == "scene_reset" and active and self.result is not None:
            count = memory(0x804CE380, 1)[0] + 1
            if not 1 <= count <= 64: raise SemanticError("Invalid entity list count")
            heads = memory(memory.word(0x804D782C), count*4)
            if any(heads): raise SemanticError("Scene ownership reset left live objects")
            if not self.frame_count or self.last_draw != self.frame_count-1 or self.draw_before is not None:
                raise SemanticError("Incomplete final source draw")
            if self.pending: raise SemanticError("Unconsumed input at teardown")
            out.update(remaining_fighter_slots=[], released_fighter_slots=sorted(self.fighters),
                       result=self.result, entity_list_count=count, entity_heads_hex=heads.hex(),
                       source_ticks=self.frame_count, source_draws=self.draw_count)
            self.fighters.clear()
            self.complete = True
        else:
            # Keep every transport sequence, including pre-match events.
            out["outside_active_match"] = True
        return result

    def completion(self):
        return {"complete": self.complete, "source_ticks": self.frame_count,
                "source_draws": self.draw_count, "pad_polls": self.poll_count,
                "result": self.result,
                "missing_coverage": ["audio_pcm", "menu_equivalence", "virtual_memory_card_comparison"],
                "physical_controller_validation": "not_performed_by_automation"}
