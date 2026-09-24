"""Decode the passive observer's bounded source-owned slices.

Only raw PADRead/queue samples are replay inputs. CpuObservation remains the
existing expected-output decoder. No captured address is an implementation
input to the port; addresses here resolve the retail observer's typed owners.
"""
from __future__ import annotations

import struct
from copy import deepcopy
from pathlib import Path
from retail_cpu_observation import CpuObservation
from retail_replay_validation import _validate_fighter, _pad_state
from retail_setup_validation import _decode_setup
from transition_trace_format import (PLAYER_KEYS as TRANSITION_PLAYER_KEYS,
                                     RULE_KEYS as TRANSITION_RULE_KEYS)


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


# This is deliberately a separate, experimental schema from the version-one
# single-match observer stream above. A v1 stream has no sequence identity and
# cannot be promoted by concatenating rows from several captures. The opt-in
# Dolphin observer now emits the passive CSS/SSS, VS/Results, Results GObj,
# and return-CSS boundaries below. The adapter joins those menu rows to a
# same-capture transition trace and owner epoch, while retaining separate
# missing coverage for PCM and the save block's persistent records. Consequently
# this reducer remains an experimental contract and is not connected to accepted
# replay-bundle completion.
WHOLE_SESSION_SCHEMA = "melee-web-reference-whole-session"
WHOLE_SESSION_VERSION = 2
WHOLE_SESSION_HEADER_KEYS = {
    "record", "schema", "version", "sequence_id", "capture_id", "match_count",
    "source_revision", "observer_schema", "observer_version",
}
WHOLE_SESSION_BOUNDARY_KEYS = {
    "record", "schema", "version", "sequence_id", "capture_id",
    "event", "match_index", "snapshot", "result", "draw", "exit", "teardown",
}
WHOLE_SESSION_EVENTS = (
    "css_enter", "css_exit", "sss_enter", "sss_exit", "vs_enter", "final_draw",
    "vs_exit", "results_enter", "results_gobj", "results_exit", "scene_reset",
    "return_css",
)
WHOLE_SESSION_SNAPSHOT_KEYS = {
    "pad_state_hex", "rng", "setup", "lifecycle", "menu_context",
}
WHOLE_SESSION_MENU_KEYS = {
    "trace_event", "run", "index", "route", "audio", "selection",
}
WHOLE_SESSION_AUDIO_KEYS = {"active", "owner_epoch", "stream"}
WHOLE_SESSION_SOURCE_OUTCOMES = {1, 2, 3, 7}  # timeout, elimination, team elimination, NC
WHOLE_SESSION_TRANSITION_EVENTS = {
    "css_enter", "css_exit", "sss_enter", "sss_exit", "return_css",
}
WHOLE_SESSION_TRACE_EVENTS = {
    "capture_begin", "css_enter_complete", "css_exit_complete",
    "sss_enter_complete", "sss_exit_complete", "match_enter_complete",
}
WHOLE_SESSION_SOURCE_HOOK_PCS = {
    "gm_Scene_Vs_OnExit": 0x8016E9C8,
    "gmVsMelee_ExitVs": 0x801A5AF0,
    "gm_Scene_Results_OnEnter": 0x80177368,
    "gm_Scene_Results_OnExit": 0x80177704,
    "gmVsMelee_ExitResults": 0x801A5F64,
    "gm_ModeState_Prize_OnEnter": 0x801BFCFC,
    "ifPrize_Scene_OnEnter": 0x802FEBE0,
    "ifPrize_Scene_OnExit": 0x802FED10,
    "gm_ModeState_Prize_OnExit": 0x801A6308,
    "gm_ModeState_ChallengerPrize_OnExit": 0x801BFF7C,
}


class WholeSessionSemanticError(SemanticError):
    """A versioned whole-session sequence is missing or contradicts a boundary."""


class WholeSessionSemanticSession:
    """Validate an experimental, source-hooked CSS/SSS/match loop.

    The input is a normalized join of two existing source-only formats:
    ``melee-web-transition-trace`` menu events and the passive Dolphin
    observer's typed boundary records. ``lifecycle`` is a closed union of
    those two record shapes or an explicit ``source_hooks`` record for hooks
    that the current observer does not yet emit. It is never an arbitrary
    dictionary accepted as proof of a transition.

    A future normalizer must still provide one header and all rows from one
    capture, retaining the typed menu context and the per-match draw, exit,
    result, Results, and scene-reset boundaries. The reducer intentionally
    reports ``experimental`` and ``accepted_for_reference_bundle=False`` even
    for a syntactically complete stream because PCM and final profile evidence
    remain outside this observer join.
    """

    def __init__(self):
        self.header = None
        self.phase = "header"
        self.next_match = None
        self.current_setup = None
        self.current_result = None
        self.matches = {}
        self.transitions = []
        self.event_count = 0
        self.last_draw = None

    @staticmethod
    def _text(value, field):
        if not isinstance(value, str) or not value or len(value) > 256:
            raise WholeSessionSemanticError(f"{field} must be a non-empty string")
        return value

    @staticmethod
    def _integer(value, field, minimum=0, maximum=0xffffffff):
        if type(value) is not int or not minimum <= value <= maximum:
            raise WholeSessionSemanticError(
                f"{field} must be an integer in [{minimum}, {maximum}]")
        return value

    @staticmethod
    def _exact(value, fields, context):
        if not isinstance(value, dict) or set(value) != set(fields):
            raise WholeSessionSemanticError(
                f"{context} has missing or unrecognized fields")

    def _consume_header(self, row):
        if self.header is not None or self.phase != "header":
            raise WholeSessionSemanticError("Duplicate whole-session header")
        if not isinstance(row, dict) or set(row) != WHOLE_SESSION_HEADER_KEYS:
            raise WholeSessionSemanticError(
                "Whole-session header has missing or unrecognized fields")
        if row["record"] != "header" or row["schema"] != WHOLE_SESSION_SCHEMA:
            raise WholeSessionSemanticError("Unsupported whole-session header")
        if row["version"] != WHOLE_SESSION_VERSION:
            raise WholeSessionSemanticError("Unsupported whole-session version")
        self._text(row["sequence_id"], "sequence_id")
        self._text(row["capture_id"], "capture_id")
        if row["source_revision"] != "GALE01r2":
            raise WholeSessionSemanticError("Whole-session source revision is not GALE01r2")
        if row["observer_schema"] != "melee-web-passive-dolphin-observer":
            raise WholeSessionSemanticError("Unsupported passive observer schema")
        if row["observer_version"] != 1:
            raise WholeSessionSemanticError("Unsupported passive observer version")
        match_count = self._integer(row["match_count"], "match_count", 3, 64)
        self.header = {"sequence_id": row["sequence_id"],
                       "capture_id": row["capture_id"],
                       "match_count": match_count}
        self.next_match = 0
        self.phase = "css_enter"
        return deepcopy(row)

    def _expected(self):
        if self.phase == "header":
            return ("header", None)
        if self.phase == "css_enter":
            return ("css_enter", 0)
        if self.phase == "css_exit":
            return ("css_exit", self.next_match)
        if self.phase == "sss_enter":
            return ("sss_enter", self.next_match)
        if self.phase == "sss_exit":
            return ("sss_exit", self.next_match)
        if self.phase == "vs_enter":
            return ("vs_enter", self.next_match)
        if self.phase == "final_draw":
            return ("final_draw", self.next_match)
        if self.phase == "vs_exit":
            return ("vs_exit", self.next_match)
        if self.phase == "results_enter":
            return ("results_enter", self.next_match)
        if self.phase == "results_gobj":
            return ("results_gobj", self.next_match)
        if self.phase == "results_exit":
            return ("results_exit", self.next_match)
        if self.phase == "scene_reset":
            return ("scene_reset", self.next_match)
        if self.phase == "return_css":
            return ("return_css", self.next_match)
        return (None, None)

    def _validate_transition_lifecycle(self, lifecycle, event):
        expected_trace = {
            "css_enter": "capture_begin",
            "css_exit": "css_exit_complete",
            "sss_enter": "sss_enter_complete",
            "sss_exit": "sss_exit_complete",
            "return_css": "css_enter_complete",
        }[event]
        self._exact(lifecycle, {"kind", "event", "run", "index", "route"},
                    f"{event} transition lifecycle")
        if lifecycle["kind"] != "transition_trace" or lifecycle["event"] != expected_trace:
            raise WholeSessionSemanticError(
                f"{event} does not identify its existing transition-trace event")
        self._integer(lifecycle["run"], f"{event} transition run")
        self._integer(lifecycle["index"], f"{event} transition index")
        route = lifecycle["route"]
        if route not in (None, "css", "match"):
            raise WholeSessionSemanticError(f"{event} transition route is invalid")
        if event == "sss_exit" and route != "match":
            raise WholeSessionSemanticError("SSS exit did not take the match route")
        if event != "sss_exit" and route is not None:
            raise WholeSessionSemanticError(f"{event} transition unexpectedly has a route")

    def _validate_observer_lifecycle(self, lifecycle, event):
        expected = {
            "vs_enter": ("setup", 0x8016E9C4),
            "final_draw": ("draw_return", 0x80391040),
            "scene_reset": ("scene_teardown", 0x8039157C),
        }[event]
        self._exact(lifecycle,
                    {"kind", "boundary", "seq", "source_tick", "draw_ordinal", "pc"},
                    f"{event} observer lifecycle")
        if lifecycle["kind"] != "passive_observer" or lifecycle["boundary"] != expected[0]:
            raise WholeSessionSemanticError(
                f"{event} does not identify its existing passive observer boundary")
        self._integer(lifecycle["seq"], f"{event} observer seq")
        self._integer(lifecycle["source_tick"], f"{event} observer source_tick")
        self._integer(lifecycle["draw_ordinal"], f"{event} observer draw_ordinal")
        if lifecycle["pc"] != expected[1]:
            raise WholeSessionSemanticError(f"{event} observer PC does not match its boundary")

    def _validate_source_hooks(self, lifecycle, event):
        expected = {
            "vs_exit": [("gm_Scene_Vs_OnExit", 0x8016E9C8),
                        ("gmVsMelee_ExitVs", 0x801A5AF0)],
            "results_enter": [("gm_Scene_Results_OnEnter", 0x80177368)],
            # No current observer record identifies these result GObj process
            # callbacks. The named placeholder keeps the missing emitter work
            # explicit and prevents an arbitrary lifecycle dictionary passing.
            "results_gobj": [("results_gobj_procs", None)],
            "results_exit": [("gm_Scene_Results_OnExit", 0x80177704),
                             ("gmVsMelee_ExitResults", 0x801A5F64)],
        }[event]
        self._exact(lifecycle, {"kind", "hooks", "source_tick", "draw_ordinal"},
                    f"{event} source-hook lifecycle")
        if lifecycle["kind"] != "source_hooks":
            raise WholeSessionSemanticError(f"{event} lacks source-hook lifecycle evidence")
        self._integer(lifecycle["source_tick"], f"{event} source_tick")
        self._integer(lifecycle["draw_ordinal"], f"{event} draw_ordinal")
        hooks = lifecycle["hooks"]
        if not isinstance(hooks, list) or len(hooks) != len(expected):
            raise WholeSessionSemanticError(f"{event} source-hook order is incomplete")
        for index, (actual, wanted) in enumerate(zip(hooks, expected)):
            self._exact(actual, {"name", "pc"}, f"{event} hook[{index}]")
            if actual["name"] != wanted[0] or actual["pc"] != wanted[1]:
                raise WholeSessionSemanticError(
                    f"{event} source-hook order or address disagrees with source")

    def _validate_menu_selection(self, selection, context):
        if selection is None:
            return
        self._exact(selection, {"rules", "players"}, context + ".selection")
        self._exact(selection["rules"], TRANSITION_RULE_KEYS, context + ".rules")
        players = selection["players"]
        if not isinstance(players, list) or len(players) != 4:
            raise WholeSessionSemanticError(context + ".players must contain four records")
        for index, player in enumerate(players):
            self._exact(player, TRANSITION_PLAYER_KEYS, f"{context}.players[{index}]")

    def _validate_menu_context(self, context, event, lifecycle):
        if event not in WHOLE_SESSION_TRANSITION_EVENTS:
            if context is not None:
                raise WholeSessionSemanticError(f"{event} must not carry menu context")
            return
        self._exact(context, WHOLE_SESSION_MENU_KEYS, f"{event} menu context")
        if context["trace_event"] != lifecycle["event"]:
            raise WholeSessionSemanticError(f"{event} menu context disagrees with lifecycle")
        self._integer(context["run"], f"{event} menu run")
        self._integer(context["index"], f"{event} menu index")
        if context["route"] != lifecycle["route"]:
            raise WholeSessionSemanticError(f"{event} menu route disagrees with lifecycle")
        self._exact(context["audio"], WHOLE_SESSION_AUDIO_KEYS, f"{event} menu audio")
        if not isinstance(context["audio"]["active"], bool):
            raise WholeSessionSemanticError(f"{event} menu audio active is invalid")
        self._integer(context["audio"]["owner_epoch"], f"{event} menu audio owner_epoch")
        self._text(context["audio"]["stream"], f"{event} menu audio stream")
        self._validate_menu_selection(context["selection"], f"{event} menu")

    def _validate_result(self, result, event):
        needs_result = {"vs_exit", "results_enter", "results_gobj",
                        "results_exit", "scene_reset"}
        if event not in needs_result:
            if result is not None:
                raise WholeSessionSemanticError(f"{event} must not publish a result")
            return
        if not isinstance(result, dict) or set(result) != {"outcome", "winners"}:
            raise WholeSessionSemanticError(f"{event} lacks its source result snapshot")
        outcome = self._integer(result["outcome"], f"{event} result.outcome", 0, 0xff)
        if outcome not in WHOLE_SESSION_SOURCE_OUTCOMES:
            raise WholeSessionSemanticError(
                f"{event} result.outcome is not a supported terminal source outcome")
        if outcome == 3 and not self.current_setup["declared_setup"]["is_teams"]:
            raise WholeSessionSemanticError(
                "Team elimination requires the source setup to declare teams")
        winners = result["winners"]
        if not isinstance(winners, list):
            raise WholeSessionSemanticError(f"{event} result.winners is not a list")
        active_count = len(self.current_setup["declared_setup"]["players"])
        if not 0 <= len(winners) <= active_count or (outcome != 7 and not winners):
            raise WholeSessionSemanticError(f"{event} result winner count is invalid")
        if (any(type(slot) is not int or not 0 <= slot < active_count for slot in winners)
                or len(set(winners)) != len(winners)):
            raise WholeSessionSemanticError(f"{event} result winners are invalid")
        if self.current_result is None:
            self.current_result = deepcopy(result)
        elif result != self.current_result:
            raise WholeSessionSemanticError(f"{event} result changed after source publication")

    def _validate_snapshot(self, row, event, match_index):
        snapshot = row["snapshot"]
        if not isinstance(snapshot, dict) or set(snapshot) != WHOLE_SESSION_SNAPSHOT_KEYS:
            raise WholeSessionSemanticError(
                f"{event} snapshot has missing or unrecognized fields")
        pad_state = snapshot["pad_state_hex"]
        if not isinstance(pad_state, str) or pad_state.lower() != pad_state:
            raise WholeSessionSemanticError(
                f"{event} snapshot PAD state must be lowercase hexadecimal")
        try:
            _pad_state(pad_state, f"{event} PAD state")
        except (ValueError, TypeError) as error:
            raise WholeSessionSemanticError(str(error)) from error
        self._integer(snapshot["rng"], f"{event} snapshot RNG")

        lifecycle = snapshot["lifecycle"]
        if event in WHOLE_SESSION_TRANSITION_EVENTS:
            self._validate_transition_lifecycle(lifecycle, event)
        elif event in {"vs_enter", "final_draw", "scene_reset"}:
            self._validate_observer_lifecycle(lifecycle, event)
        else:
            self._validate_source_hooks(lifecycle, event)
        self._validate_menu_context(snapshot["menu_context"], event, lifecycle)

        setup = snapshot["setup"]
        setup_events = {"vs_enter", "final_draw", "vs_exit", "results_enter",
                        "results_gobj", "results_exit", "scene_reset"}
        if event in setup_events:
            if not isinstance(setup, dict) or set(setup) != {"start_melee_hex", "declared_setup"}:
                raise WholeSessionSemanticError(f"{event} requires a complete setup snapshot")
            try:
                decoded = _decode_setup(setup["start_melee_hex"])
            except ValueError as error:
                raise WholeSessionSemanticError(str(error)) from error
            if setup["declared_setup"] != decoded:
                raise WholeSessionSemanticError(
                    f"{event} setup snapshot disagrees with StartMeleeData")
            if event == "vs_enter":
                self.current_setup = deepcopy(setup)
            elif setup != self.current_setup:
                raise WholeSessionSemanticError(
                    f"{event} setup snapshot changed within match {match_index}")
        elif setup is not None:
            raise WholeSessionSemanticError(f"{event} must not carry match setup")

    def _validate_terminal_fields(self, row, event, lifecycle):
        draw = row["draw"]
        if event == "final_draw":
            self._exact(draw, {"source_index", "draw_ordinal"}, "final_draw")
            self._integer(draw["source_index"], "final_draw.source_index")
            self._integer(draw["draw_ordinal"], "final_draw.draw_ordinal")
            if draw["draw_ordinal"] != lifecycle["draw_ordinal"]:
                raise WholeSessionSemanticError("final draw ordinal disagrees with observer")
            self.last_draw = deepcopy(draw)
        elif draw is not None:
            raise WholeSessionSemanticError(f"{event} must not carry a draw boundary")

        exit_value = row["exit"]
        if event == "vs_exit":
            self._exact(exit_value, {"scene_request", "source_ticks"}, "vs_exit exit")
            self._integer(exit_value["scene_request"], "vs_exit.scene_request")
            self._integer(exit_value["source_ticks"], "vs_exit.source_ticks")
        elif exit_value is not None:
            raise WholeSessionSemanticError(f"{event} must not carry an exit boundary")

        teardown = row["teardown"]
        if event == "scene_reset":
            self._exact(teardown, {"released_fighter_slots", "remaining_fighter_slots",
                                   "entity_list_count", "entity_heads_hex"},
                        "scene_reset teardown")
            released = teardown["released_fighter_slots"]
            remaining = teardown["remaining_fighter_slots"]
            for name, slots in (("released_fighter_slots", released),
                                ("remaining_fighter_slots", remaining)):
                if (not isinstance(slots, list)
                        or slots != sorted(set(slots))
                        or any(type(slot) is not int or not 0 <= slot <= 3 for slot in slots)):
                    raise WholeSessionSemanticError(f"scene_reset {name} is invalid")
            if remaining:
                raise WholeSessionSemanticError("scene_reset left live fighter slots")
            count = self._integer(teardown["entity_list_count"],
                                  "scene_reset entity_list_count", 1, 64)
            heads = teardown["entity_heads_hex"]
            if (not isinstance(heads, str) or heads.lower() != heads
                    or len(heads) != count * 8
                    or any(byte != "0" for byte in heads)):
                raise WholeSessionSemanticError("scene_reset entity heads are not empty")
        elif teardown is not None:
            raise WholeSessionSemanticError(f"{event} must not carry teardown")

    def consume(self, row):
        if not isinstance(row, dict):
            raise WholeSessionSemanticError("Whole-session row must be an object")
        if row.get("record") == "header":
            return self._consume_header(row)
        if self.header is None:
            raise WholeSessionSemanticError("Whole-session boundary appeared before header")
        if set(row) != WHOLE_SESSION_BOUNDARY_KEYS:
            raise WholeSessionSemanticError(
                "Whole-session boundary has missing or unrecognized fields")
        if row["record"] != "boundary" or row["schema"] != WHOLE_SESSION_SCHEMA:
            raise WholeSessionSemanticError("Unsupported whole-session boundary")
        if row["version"] != WHOLE_SESSION_VERSION:
            raise WholeSessionSemanticError("Unsupported whole-session version")
        if row["sequence_id"] != self.header["sequence_id"]:
            raise WholeSessionSemanticError("Boundary sequence_id disagrees with header")
        if row["capture_id"] != self.header["capture_id"]:
            raise WholeSessionSemanticError(
                "Boundary capture_id disagrees; spliced captures are not a session")
        event = row["event"]
        if event not in WHOLE_SESSION_EVENTS:
            raise WholeSessionSemanticError(f"Unknown whole-session boundary {event!r}")
        match_index = self._integer(row["match_index"], "match_index",
                                     0, self.header["match_count"] - 1)
        expected_event, expected_index = self._expected()
        if (event, match_index) != (expected_event, expected_index):
            raise WholeSessionSemanticError(
                f"Expected {expected_event} for match {expected_index}, got "
                f"{event} for match {match_index}")
        self._validate_snapshot(row, event, match_index)
        lifecycle = row["snapshot"]["lifecycle"]
        self._validate_terminal_fields(row, event, lifecycle)
        self._validate_result(row["result"], event)

        self.transitions.append(event)
        self.event_count += 1
        if match_index not in self.matches:
            self.matches[match_index] = {"match_index": match_index,
                                         "boundaries": {}, "result": None,
                                         "draw": None, "exit": None, "teardown": None}
        self.matches[match_index]["boundaries"][event] = deepcopy(row["snapshot"])
        if row["result"] is not None:
            self.matches[match_index]["result"] = deepcopy(row["result"])
        if row["draw"] is not None:
            self.matches[match_index]["draw"] = deepcopy(row["draw"])
        if row["exit"] is not None:
            self.matches[match_index]["exit"] = deepcopy(row["exit"])
        if row["teardown"] is not None:
            self.matches[match_index]["teardown"] = deepcopy(row["teardown"])

        if event == "css_enter":
            self.phase = "css_exit"
        elif event == "css_exit":
            self.phase = "sss_enter"
        elif event == "sss_enter":
            self.phase = "sss_exit"
        elif event == "sss_exit":
            self.phase = "vs_enter"
        elif event == "vs_enter":
            self.phase = "final_draw"
        elif event == "final_draw":
            self.phase = "vs_exit"
        elif event == "vs_exit":
            self.phase = "results_enter"
        elif event == "results_enter":
            self.phase = "results_gobj"
        elif event == "results_gobj":
            self.phase = "results_exit"
        elif event == "results_exit":
            self.phase = "scene_reset"
        elif event == "scene_reset":
            self.phase = "return_css"
        else:
            if match_index + 1 < self.header["match_count"]:
                self.next_match = match_index + 1
                self.current_setup = None
                self.current_result = None
                self.last_draw = None
                self.phase = "css_exit"
            else:
                self.phase = "complete"
        return deepcopy(row)

    def completion(self):
        if self.header is None:
            raise WholeSessionSemanticError("Whole-session header is missing")
        if self.phase != "complete" or len(self.matches) != self.header["match_count"]:
            expected_event, expected_index = self._expected()
            raise WholeSessionSemanticError(
                f"Whole-session sequence incomplete; expected {expected_event} "
                f"for match {expected_index}")
        return {
            "schema": WHOLE_SESSION_SCHEMA,
            "version": WHOLE_SESSION_VERSION,
            "complete": True,
            "experimental": True,
            "accepted_for_reference_bundle": False,
            "observer_integration": "pending",
            "sequence_id": self.header["sequence_id"],
            "capture_id": self.header["capture_id"],
            "match_count": self.header["match_count"],
            "event_count": self.event_count,
            "transition_order": list(self.transitions),
            "matches": [deepcopy(self.matches[index])
                        for index in range(self.header["match_count"])],
            "claims": {
                "original_repeatability": "not_evaluated",
                "port_equivalence": "not_evaluated",
                "physical_controller_validation": "not_performed_by_automation",
            },
        }


def validate_whole_session(rows):
    """Validate a complete experimental v2 sequence; v1 rows are rejected."""
    session = WholeSessionSemanticSession()
    for row in rows:
        session.consume(row)
    return session.completion()


WHOLE_OBSERVER_REQUIRED = (
    "css_enter", "css_exit", "sss_enter", "sss_exit", "entry", "setup",
    "draw_return", "vs_exit", "vs_exit_return", "vs_mode_exit",
    "results_enter", "results_gobj", "results_exit", "results_mode_exit",
    "scene_teardown", "return_css",
)
WHOLE_OBSERVER_ORDER = (
    "css_enter", "css_exit", "sss_enter", "sss_exit", "entry", "setup",
    "vs_exit", "vs_exit_return", "vs_mode_exit", "results_enter",
    "results_gobj", "results_exit", "results_mode_exit", "scene_teardown",
    "return_css",
)
WHOLE_OBSERVER_PCS = {
    "css_enter": 0x8026688C,
    "css_cancel_enter": 0x8026688C,
    "css_exit": 0x80266D70,
    "sss_enter": 0x8025A998,
    "sss_exit": 0x8025BBD0,
    "entry": 0x8016E934,
    "setup": 0x8016E9C4,
    "draw_return": 0x80391040,
    "vs_exit": 0x8016E9C8,
    "vs_exit_return": 0x8016EBBC,
    "vs_mode_exit": 0x801A5AF0,
    "results_enter": 0x80177368,
    "results_gobj": 0x80179350,
    "results_exit": 0x80177704,
    "results_mode_exit": 0x801A5F64,
    "scene_teardown": 0x8039157C,
    "return_css": 0x8026688C,
    "prize_mode_enter": 0x801BFCFC,
    "prize_scene_enter": 0x802FEBE0,
    "prize_scene_exit": 0x802FED10,
    "prize_mode_exit": 0x801A6308,
    "startup_prize_mode_exit": 0x801BFF7C,
}


def _whole_slice(row, name):
    return next((item for item in row["payload"].get("slices", [])
                 if item.get("name") == name), None)


def _whole_profile_masks(row):
    """Decode the two source-owned masks published at session boundaries."""
    boundary = row["payload"].get("boundary", "boundary")
    values = {}
    for name in ("profile_characters", "profile_stages"):
        item = _whole_slice(row, name)
        if item is None:
            raise WholeSessionSemanticError(
                f"{boundary} lacks its loaded {name} profile mask")
        if item.get("size") != 2:
            raise WholeSessionSemanticError(
                f"{boundary} {name} profile mask must be exactly two bytes")
        try:
            raw = bytes.fromhex(item.get("hex", ""))
        except (TypeError, ValueError) as error:
            raise WholeSessionSemanticError(
                f"{boundary} {name} profile mask is not hexadecimal") from error
        if len(raw) != 2:
            raise WholeSessionSemanticError(
                f"{boundary} {name} profile mask must be exactly two bytes")
        # Guest RAM is PowerPC big-endian. Keep this as a typed u16 in the
        # normalized report rather than treating the bytes as an opaque claim.
        values[name.removeprefix("profile_")] = int.from_bytes(raw, "big")
    return values


# Authored gmm_x0 layout behind the typed first-CSS context. GameRules is the
# asserted 0x18-byte rules block at +0x1850 (src/melee/gm/types.h:203-226).
# gmMainLib_GetSaveData() returns &gmm_x0.thing, the block the matched retail
# accessors read at +0x1868; its 0x55E8 bytes end at the +0x6E50 trailing pad
# (src/melee/gm/gmmain_lib.c:99-102, src/melee/gm/types.h:258-317,410-411).
# Field offsets below are the ones matched accessors read, so a decoder binds
# the profile the first CSS ran with instead of only its two unlock masks.
GAME_RULES_SIZE = 0x18
SAVE_DATA_SIZE = 0x55E8
SAVE_UNLOCKED_CHARACTERS = 0x0000
SAVE_UNLOCKED_STAGES = 0x0002
SAVE_UNLOCKED_FEATURES = 0x0004
SAVE_MATCH_COUNTERS = (
    ("time_matches", 0x01B0), ("stock_matches", 0x01B4), ("coin_matches", 0x01B8),
    ("bonus_matches", 0x01BC), ("stamina_matches", 0x01C0), ("match_resets", 0x01C4),
)
SAVE_TROPHY_COUNT = 0x0468
SAVE_TROPHY_CATEGORY_FLAGS = 0x046A
SAVE_TROPHY_FLAGS = 0x046C
# src/melee/ty/forward.h: #define TY_TROPHY_COUNT 293
TROPHY_FLAG_COUNT = 293
GAME_RULES_BYTES = (
    ("force_main_menu", 0x00), ("bgm", 0x01), ("mode", 0x02), ("time_limit", 0x03),
    ("stock_count", 0x04), ("handicap", 0x05), ("damage_ratio", 0x06), ("unk_x7", 0x07),
    ("stock_time_limit", 0x08), ("friendly_fire", 0x09), ("pause", 0x0A),
    ("score_display", 0x0B), ("unk_xc", 0x0C), ("xD", 0x0D), ("xE", 0x0E), ("xF", 0x0F),
    ("unk_x10", 0x10), ("x11", 0x11), ("x12", 0x12), ("x13", 0x13),
)
GAME_RULES_LAST_FIELD = ("unk_14", 0x14)


def _typed_slice_bytes(row, name, size):
    boundary = row["payload"].get("boundary", "boundary")
    item = _whole_slice(row, name)
    if item is None:
        raise WholeSessionSemanticError(f"{boundary} lacks its typed {name} slice")
    if item.get("size") != size:
        raise WholeSessionSemanticError(f"{boundary} {name} must be exactly {size:#x} bytes")
    try:
        raw = bytes.fromhex(item.get("hex", ""))
    except (TypeError, ValueError) as error:
        raise WholeSessionSemanticError(f"{boundary} {name} is not hexadecimal") from error
    if len(raw) != size:
        raise WholeSessionSemanticError(f"{boundary} {name} must be exactly {size:#x} bytes")
    return raw


def _packed_bits(raw):
    """Expand one guest big-endian packed mask into its authored bit indices."""
    value = int.from_bytes(raw, "big")
    return {"value": value,
            "set": [index for index in range(len(raw) * 8) if value >> index & 1]}


def _whole_profile_context(row):
    """Decode the typed first-CSS context the observer publishes at CSS entry.

    The observer copies the authored ranges; every field here is decoded with
    its explicit width and signedness from guest big-endian bytes. The
    persistent fighter records and name banks inside the save block are
    captured as bytes but not typed: the pinned source's own offsets for that
    region disagree with each other, so this decoder does not guess them.

    Returns ``None`` when the stream predates the typed context, so an older
    capture stays readable as explicit, incomplete evidence.
    """
    boundary = row["payload"].get("boundary", "boundary")
    if (_whole_slice(row, "profile_game_rules") is None and
            _whole_slice(row, "profile_save_data") is None):
        return None
    rules = _typed_slice_bytes(row, "profile_game_rules", GAME_RULES_SIZE)
    save = _typed_slice_bytes(row, "profile_save_data", SAVE_DATA_SIZE)
    game_rules = {name: rules[offset] for name, offset in GAME_RULES_BYTES}
    last_name, last_offset = GAME_RULES_LAST_FIELD
    game_rules[last_name] = int.from_bytes(rules[last_offset:last_offset + 4], "big",
                                           signed=True)
    counters = {name: int.from_bytes(save[offset:offset + 4], "big")
                for name, offset in SAVE_MATCH_COUNTERS}
    trophy_flags = [int.from_bytes(save[SAVE_TROPHY_FLAGS + 2 * index:
                                        SAVE_TROPHY_FLAGS + 2 * index + 2], "big")
                    for index in range(TROPHY_FLAG_COUNT)]
    return {
        "boundary": boundary,
        "game_rules": game_rules,
        "unlocked_characters": _packed_bits(
            save[SAVE_UNLOCKED_CHARACTERS:SAVE_UNLOCKED_CHARACTERS + 2]),
        "unlocked_stages": _packed_bits(save[SAVE_UNLOCKED_STAGES:SAVE_UNLOCKED_STAGES + 2]),
        "unlocked_features": _packed_bits(
            save[SAVE_UNLOCKED_FEATURES:SAVE_UNLOCKED_FEATURES + 1]),
        "match_counters": counters,
        "trophy_count": int.from_bytes(save[SAVE_TROPHY_COUNT:SAVE_TROPHY_COUNT + 2],
                                       "big", signed=True),
        "trophy_category_flags": int.from_bytes(
            save[SAVE_TROPHY_CATEGORY_FLAGS:SAVE_TROPHY_CATEGORY_FLAGS + 2], "big"),
        "trophy_flags": trophy_flags,
    }


def _whole_menu_audio(row):
    stream_slice = _whole_slice(row, "menu_audio")
    voice_slice = _whole_slice(row, "menu_audio_voice")
    if stream_slice is None or voice_slice is None:
        return None
    try:
        stream = bytes.fromhex(stream_slice["hex"]).split(b"\0", 1)[0].decode("ascii")
        voice_bytes = bytes.fromhex(voice_slice["hex"])
    except (ValueError, UnicodeDecodeError):
        raise WholeSessionSemanticError("menu audio slices are malformed")
    if len(voice_bytes) != 4:
        raise WholeSessionSemanticError("menu audio voice slice has the wrong size")
    return {"active": int.from_bytes(voice_bytes, "big") != 0xffffffff,
            "stream": stream}


def _join_whole_transition_trace(boundaries, transition_trace, capture_id, sequence_id):
    """Join a continuous transition trace to passive menu boundaries.

    The GDB collector and passive observer must carry the same IDs.  Events are
    matched in source order, with route bytes from the observer's SSS state
    slice checked against the collector's completed callback route.  No event
    is synthesized for a missing return or a separate capture.
    """
    rows = list(transition_trace)
    headers = [row for row in rows if row.get("record") == "header"]
    if len(headers) != 1:
        raise WholeSessionSemanticError("transition trace requires exactly one header")
    header = headers[0]
    if header.get("schema") != "melee-web-transition-trace" or header.get("version") != 1:
        raise WholeSessionSemanticError("unsupported transition trace schema")
    if header.get("capture_id") != capture_id or header.get("sequence_id") != sequence_id:
        raise WholeSessionSemanticError("transition trace identity disagrees with observer capture")
    events = [row for row in rows if row.get("record") == "event"]
    if not events:
        raise WholeSessionSemanticError("transition trace has no lifecycle events")
    for event in events:
        if event.get("capture_id") != capture_id or event.get("sequence_id") != sequence_id:
            raise WholeSessionSemanticError("transition event identity disagrees with observer capture")
    by_run = {}
    for event in events:
        run = event.get("run")
        index = event.get("index")
        if type(run) is not int or run < 0 or type(index) is not int or index < 0:
            raise WholeSessionSemanticError("transition event has invalid run/index")
        by_run.setdefault(run, []).append(event)
    ordered = []
    for run, run_events in sorted(by_run.items()):
        run_events.sort(key=lambda event: event["index"])
        if [event["index"] for event in run_events] != list(range(len(run_events))):
            raise WholeSessionSemanticError("transition event indices are not consecutive")
        ordered.extend(run_events)

    menu = [row for row in boundaries if row["payload"]["boundary"] in {
        "css_enter", "css_cancel_enter", "css_exit", "sss_enter", "sss_exit", "entry",
    }]
    cursor = 0
    joined = []
    expected = {
        "capture_begin": "css_enter",
        "css_exit_complete": "css_exit",
        "sss_enter_complete": "sss_enter",
        "sss_exit_complete": "sss_exit",
        "css_enter_complete": "css_cancel_enter",
        "match_enter_complete": "entry",
    }
    for event in ordered:
        name = event.get("event")
        if name not in expected:
            raise WholeSessionSemanticError(f"unsupported transition event {name!r}")
        target_name = expected[name]
        if name == "css_enter_complete":
            # A collector started from a captured CSS can see a CSS entry
            # after the SSS cancel. A direct CSS entry is only accepted when
            # the producer labels it explicitly with the same event.
            candidates = {"css_cancel_enter", "css_enter"}
        else:
            candidates = {target_name}
        if cursor >= len(menu) or menu[cursor]["payload"]["boundary"] not in candidates:
            raise WholeSessionSemanticError(
                f"transition event {name} does not match the next passive boundary")
        # A later matching event cannot cover an omitted transition. Every
        # boundary in this same-capture join must be consumed exactly once.
        row = menu[cursor]
        cursor += 1
        if name == "sss_exit_complete":
            route_slice = _whole_slice(row, "menu_sss_route")
            if route_slice is None or bytes.fromhex(route_slice["hex"]) not in (b"\0", b"\1"):
                raise WholeSessionSemanticError("SSS exit lacks its source route byte")
            route = "match" if bytes.fromhex(route_slice["hex"]) == b"\1" else "css"
            if event.get("route") != route:
                raise WholeSessionSemanticError("transition SSS route disagrees with source state")
        audio = event.get("audio")
        if not isinstance(audio, dict) or type(audio.get("owner_epoch")) is not int:
            raise WholeSessionSemanticError("transition event lacks typed audio owner epoch")
        if (name != "match_enter_complete" and
                row["payload"].get("audio_owner_epoch") != audio["owner_epoch"]):
            raise WholeSessionSemanticError("menu audio owner epoch disagrees with transition trace")
        menu_audio = _whole_menu_audio(row) if name != "match_enter_complete" else None
        if menu_audio is not None:
            if menu_audio["active"] != audio.get("active") or menu_audio["stream"] != audio.get("stream"):
                raise WholeSessionSemanticError("menu audio slices disagree with transition trace")
        joined.append({"event": name, "observer_seq": row["seq"],
                       "transition_run": event["run"], "transition_index": event["index"]})
    if cursor != len(menu):
        raise WholeSessionSemanticError("observer menu boundaries extend beyond transition trace")
    return joined


def validate_whole_session_observer_records(records, *, transition_trace=None):
    """Validate one decoded opt-in observer stream and its continuous joins.

    This adapter consumes records decoded by reference_observer_stream.  It
    enforces source boundary order, identity, typed audio owner epochs, a
    same-capture transition trace, and the typed first-CSS profile context when
    the stream publishes it.  It still reports audio PCM and the save block's
    persistent-record semantics as separate missing evidence.
    """
    records = list(records)
    starts = [row for row in records if row.get("event") == "start"]
    if len(starts) != 1:
        raise WholeSessionSemanticError(
            "whole-session observer stream must contain exactly one start record")
    start = starts[0]["payload"]
    if start.get("whole_session") is not True:
        raise WholeSessionSemanticError("observer stream was not declared whole-session")
    match_count = start.get("match_count")
    if type(match_count) is not int or not 3 <= match_count <= 64:
        raise WholeSessionSemanticError("observer stream has an invalid declared match_count")
    handshakes = [row for row in records if row.get("event") == "handshake"]
    if len(handshakes) != 1:
        raise WholeSessionSemanticError("whole-session observer stream must contain one handshake")
    handshake = handshakes[0]["payload"]
    capture_id = start.get("capture_id")
    sequence_id = start.get("sequence_id")
    if not isinstance(capture_id, str) or not capture_id or not isinstance(sequence_id, str) or not sequence_id:
        raise WholeSessionSemanticError("whole-session observer start lacks capture/sequence identity")
    if handshake.get("capture_id") != capture_id or handshake.get("sequence_id") != sequence_id:
        raise WholeSessionSemanticError("observer handshake identity disagrees with start")

    boundaries = []
    for row in records:
        if row.get("event") != "boundary":
            continue
        payload = row.get("payload")
        if not isinstance(payload, dict) or payload.get("whole_session") is not True:
            continue
        match_index = payload.get("match_index")
        if type(match_index) is not int or not 0 <= match_index < match_count:
            raise WholeSessionSemanticError("observer boundary has an invalid match_index")
        if payload.get("whole_boundary_kind") != payload.get("boundary_kind"):
            raise WholeSessionSemanticError("observer boundary kind metadata disagrees")
        boundaries.append(row)
    if not boundaries:
        raise WholeSessionSemanticError("whole-session observer stream has no flagged boundaries")

    grouped = {index: [] for index in range(match_count)}
    for row in boundaries:
        grouped[row["payload"]["match_index"]].append(row)
    if any(not grouped[index] for index in grouped):
        missing = [index for index, rows_for_match in grouped.items() if not rows_for_match]
        raise WholeSessionSemanticError(
            f"whole-session observer stream is missing match indices {missing}")

    reports = []
    profile_context = None
    for match_index, rows_for_match in grouped.items():
        names = [row["payload"]["boundary"] for row in rows_for_match]
        for row in rows_for_match:
            name = row["payload"]["boundary"]
            if name in WHOLE_OBSERVER_PCS and row["payload"].get("pc") != WHOLE_OBSERVER_PCS[name]:
                raise WholeSessionSemanticError(
                    f"match {match_index} {name} boundary has an unpinned source PC")
        menu_names = {"css_enter", "css_cancel_enter", "css_exit", "sss_enter", "sss_exit"}
        profile_names = menu_names | {
            "entry", "vs_exit", "vs_exit_return", "vs_mode_exit", "results_enter",
            "results_gobj", "results_exit", "results_mode_exit", "scene_teardown",
            "prize_mode_enter", "prize_scene_enter", "prize_scene_exit", "prize_mode_exit",
            "startup_prize_mode_exit",
        }
        profile_snapshots = {}
        for row in rows_for_match:
            name = row["payload"]["boundary"]
            if name in profile_names:
                profile_snapshots.setdefault(name, _whole_profile_masks(row))
            # The typed first-CSS context is the profile the first CSS entry
            # ran with; later entries are already compared through the masks.
            if name in {"css_enter", "css_cancel_enter"} and profile_context is None:
                profile_context = _whole_profile_context(row)
        prize_names = {"prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                       "prize_mode_exit"}
        startup_prize_name = "startup_prize_mode_exit"
        all_prize_names = prize_names | {startup_prize_name}
        startup_prize_order = ["prize_mode_enter", "prize_scene_enter",
                               "prize_scene_exit", startup_prize_name]
        startup_prize = []
        route_rows = rows_for_match
        route_names = names
        if match_index == 0 and names[:1] and names[0] in all_prize_names:
            if names[:4] != startup_prize_order:
                raise WholeSessionSemanticError(
                    "match 0 has an incomplete or out-of-order startup Prize prelude")
            startup_prize = names[:4]
            route_rows = rows_for_match[4:]
            route_names = names[4:]
        elif any(name in all_prize_names for name in names[:4]):
            raise WholeSessionSemanticError(
                "startup Prize prelude is only valid before match 0 CSS enter")
        for name in WHOLE_OBSERVER_REQUIRED:
            if name == "draw_return":
                if route_names.count(name) == 0:
                    raise WholeSessionSemanticError(
                        f"match {match_index} is missing a source draw return")
            elif name == "results_gobj":
                if route_names.count(name) == 0:
                    raise WholeSessionSemanticError(
                        f"match {match_index} is missing a Results GObj process boundary")
            elif name == "css_enter" and match_index != 0:
                if route_names.count(name) != 0:
                    raise WholeSessionSemanticError(
                        f"match {match_index} has an unexpected CSS enter boundary")
            elif name in menu_names:
                if route_names.count(name) == 0:
                    raise WholeSessionSemanticError(
                        f"match {match_index} is missing {name} boundary")
            elif route_names.count(name) != 1:
                raise WholeSessionSemanticError(
                    f"match {match_index} has missing or duplicate {name} boundary")
        # The source permits an SSS cancel route before the match route.  It
        # returns to CSS and repeats CSS->SSS; preserve every such cycle rather
        # than collapsing it into a synthetic single transition.
        entry_position = route_names.index("entry")
        menu_prefix = route_names[:entry_position]
        menu_cursor = 0
        if match_index == 0:
            if menu_prefix[:1] != ["css_enter"]:
                raise WholeSessionSemanticError(
                    f"match {match_index} is missing its initial CSS enter boundary")
            menu_cursor = 1
        while True:
            expected_menu = ("css_exit", "sss_enter", "sss_exit")
            if menu_prefix[menu_cursor:menu_cursor + 3] != list(expected_menu):
                raise WholeSessionSemanticError(
                    f"match {match_index} has an out-of-order CSS/SSS lifecycle")
            menu_cursor += 3
            if menu_cursor < len(menu_prefix) and menu_prefix[menu_cursor] == "css_cancel_enter":
                menu_cursor += 1
                continue
            break
        if menu_cursor != len(menu_prefix):
            raise WholeSessionSemanticError(
                f"match {match_index} has an out-of-order CSS/SSS lifecycle")
        suffix = route_names[entry_position:]
        has_prize = bool(prize_names & set(suffix))
        if has_prize and not all(suffix.count(name) == 1 for name in prize_names):
            raise WholeSessionSemanticError(
                f"match {match_index} has an incomplete Prize lifecycle")
        if not has_prize and any(name in prize_names for name in suffix):
            raise WholeSessionSemanticError(
                f"match {match_index} has an incomplete Prize lifecycle")
        expected_suffix = list(WHOLE_OBSERVER_ORDER[4:])
        if has_prize:
            expected_suffix = [
                "entry", "setup", "vs_exit", "vs_exit_return", "vs_mode_exit", "results_enter",
                "results_gobj", "results_exit", "results_mode_exit", "prize_mode_enter",
                "scene_teardown", "prize_scene_enter", "prize_scene_exit",
                "prize_mode_exit", "return_css",
            ]
        if [name for name in suffix if name != "draw_return"] != expected_suffix:
            raise WholeSessionSemanticError(
                f"match {match_index} has an out-of-order source lifecycle")
        vs_exit_position = route_names.index("vs_exit")
        if any(index > vs_exit_position for index, actual in enumerate(route_names)
               if actual == "draw_return"):
            raise WholeSessionSemanticError(
                f"match {match_index} has a draw return after VS exit")
        draws_before_exit = [
            row for row in route_rows[:vs_exit_position]
            if row["payload"]["boundary"] == "draw_return"
        ]
        if not draws_before_exit:
            raise WholeSessionSemanticError(
                f"match {match_index} has no final draw before VS exit")
        result_row = route_rows[vs_exit_position]
        if not any(item["name"] == "result"
                   for item in result_row["payload"].get("slices", [])):
            raise WholeSessionSemanticError(
                f"match {match_index} VS exit lacks its source Result slice")
        reports.append({
            "match_index": match_index,
            "boundary_order": names,
            "startup_prize_prelude": startup_prize,
            "final_draw_seq": draws_before_exit[-1]["seq"],
            "vs_exit_seq": result_row["seq"],
            "scene_reset_seq": next(
                row["seq"] for row in rows_for_match
                if row["payload"]["boundary"] == "scene_teardown"),
            "return_css_seq": next(
                row["seq"] for row in rows_for_match
                if row["payload"]["boundary"] == "return_css"),
            "loaded_profile_masks": profile_snapshots,
            "loaded_profile_context": profile_context,
        })

    menu_boundaries = [row for row in boundaries if row["payload"]["boundary"] in {
        "css_enter", "css_cancel_enter", "css_exit", "sss_enter", "sss_exit", "return_css",
    }]
    audio_missing = [row["seq"] for row in menu_boundaries
                     if row["payload"].get("audio_owner_epoch") is None]
    transition_join = None
    missing = []
    if transition_trace is None:
        missing.append("transition_trace_css_sss_join")
    elif audio_missing:
        # Keep the cause visible before attempting a join against an untyped
        # stream; callers must retain the old raw capture for diagnosis.
        missing.append("menu_audio_owner_epoch")
    else:
        transition_join = _join_whole_transition_trace(
            boundaries, transition_trace, capture_id, sequence_id)
    if audio_missing and "menu_audio_owner_epoch" not in missing:
        missing.append("menu_audio_owner_epoch")
    if not transition_join and "transition_trace_css_sss_join" not in missing:
        missing.append("transition_trace_css_sss_join")

    return {
        "schema": WHOLE_SESSION_SCHEMA,
        "version": WHOLE_SESSION_VERSION,
        "complete": not missing,
        "experimental": True,
        "accepted_for_reference_bundle": False,
        "observer_integration": "complete" if not missing else "incomplete",
        "capture_identity": {"capture_id": capture_id, "sequence_id": sequence_id},
        "match_count": match_count,
        "matches": reports,
        "transition_join": transition_join,
        # The typed first-CSS context replaces the coarse final-profile gap.
        # What stays open is the save block's persistent fighter records and
        # name banks: they are captured as bytes, but the pinned source's own
        # offsets for that region disagree, so no decoder may claim them yet.
        "missing_coverage": missing + [
            "audio_pcm",
            "final_profile_semantics" if profile_context is None
            else "persistent_record_semantics"],
    }
