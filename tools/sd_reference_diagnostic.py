"""Separate original SD initialization prefix receiver; never whole-session admission."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference-capture" / "dolphin"))
from reference_observer_stream import read_status
from reference_input_stream import validate_stream, validate_status
from retail_input_plan import AUTHORED_PLAN_VERSION, validate_plan, verify_entry, verify_tick

SCOPE = "sd_initialization_prefix"
PCS = {"vs_entry": 0x8016e934, "vs_setup": 0x8016e9c4, "vs_exit": 0x8016ebbc,
       "vs_retired": 0x8039157c, "sd_entry": 0x8016ebc0, "sd_setup": 0x8016ec24,
       "input": 0x80377584, "menu_input": 0x80377584, "tick": 0x80390eb4,
       "menu": 0x8034dd8c, "rules_ready": 0x8034dd8c, "items_ready": 0x8034dd8c}
ORDER = ("vs_entry", "vs_setup", "vs_exit", "vs_retired", "sd_entry", "sd_setup")


class SdDiagnosticError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SdDiagnosticError(message)


def disabled_rumble_copy(persistent):
    """Exact gm_LoadRumbleEnabled result for the validated unnamed, rumble-off profile."""
    result = bytearray(persistent)
    for slot in range(6):
        result[0x60 + slot * 0x24 + 0xc] &= ~0x80
    return result


def profile_rumble_copy(persistent, preferences):
    """gm_LoadRumbleEnabled/getPort for the declared unnamed two-human setup."""
    require(len(persistent) == 0x138 and len(preferences) == 4 and
            all(value in (0, 1) for value in preferences), "Original rumble context is invalid")
    result = disabled_rumble_copy(persistent)
    for slot in range(4):
        base = 0x60 + slot * 0x24
        if persistent[base + 1] == 0:
            require(slot < 2 and persistent[base + 0xa] == 120,
                    "Original rumble derivation requires declared unnamed humans")
            raw_slot = persistent[base + 4]
            port = slot if raw_slot == 0 else raw_slot - 1
            require(port in range(4), "Original rumble source port is invalid")
            result[base + 0xc] |= preferences[port] << 7
    return result


def slices(payload):
    fields = {"diagnostic", "name", "consumed", "pc", "slices"}
    require(set(payload) in (fields, fields | {"menu_consumed"}), "SD event fields differ")
    values = payload["slices"]
    require(isinstance(values, list) and len(values) <= 64, "SD slice inventory is invalid")
    result = {}
    for item in values:
        require(isinstance(item, dict) and set(item) == {"tag", "flags", "address", "hex"},
                "SD slice fields differ")
        require(all(type(item[k]) is int for k in ("tag", "flags", "address")), "SD slice identity is invalid")
        key = (item["tag"], item["flags"])
        require(key not in result, "SD slice identity repeats")
        try:
            raw = bytes.fromhex(item["hex"])
        except (ValueError, TypeError) as error:
            raise SdDiagnosticError("SD slice is not hex") from error
        require(raw.hex() == item["hex"] and len(raw) <= 0x10000, "SD slice bytes are invalid")
        require(0x80000000 <= item["address"] <= 0x81800000 - len(raw), "SD slice escaped MEM1")
        result[key] = raw
    return result


class Receiver:
    """Consume every event in order; original phase decisions remain observed outputs."""
    def __init__(self, plan, *, competitive_entry=False):
        validate_plan(plan)
        require(plan["version"] == AUTHORED_PLAN_VERSION, "SD receiver requires authored v4")
        require((plan["authored_recipe"]["version"] == 6) == competitive_entry,
                "Competitive entry requires its separate explicit receiver scope")
        self.competitive_entry = competitive_entry
        self.phase_order = ("vs_entry", "vs_setup") if competitive_entry else ORDER
        self.plan = plan
        self.seq = 0
        self.order = 0
        self.consumed = 0
        self.started = False
        self.ended = False
        self.records = {}
        self.tick = None
        self.tick_count = 0
        self.tick_consumed = 0
        self.timeout_clock = None
        self.last_clock_frame = 0
        self.vs_inventory = None
        self.cold_context_observed = None

    def clock(self, data):
        raw = data.get((14, 0), b"")
        require(len(raw) == 0x2e, "SD observed match clock is missing")
        return (int.from_bytes(raw[0x24:0x28], "big"),
                int.from_bytes(raw[0x28:0x2c], "big"),
                int.from_bytes(raw[0x2c:0x2e], "big"))

    def accept(self, row):
        require(not self.ended and row["seq"] == self.seq, "SD sequence gap, repeat or trailing event")
        self.seq += 1
        event, payload = row["event"], row["payload"]
        if event == "handshake":
            require(self.seq == 1 and payload.get("diagnostic") == SCOPE and
                    payload.get("recipe_sha256") == self.plan["authored_recipe_sha256"] and
                    payload.get("dol_sha1") == "08e0bf20134dfcb260699671004527b2d6bb1a45" and
                    payload.get("dol_sha256") == "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646" and
                    payload.get("dolphin_commit") == "c77bbaa0f372c3f72281602a8b087206706542cb" and
                    payload.get("cpu") == "JITARM64" and payload.get("writes_guest_memory") is False and
                    not payload.get("whole_session") and not payload.get("menu_probe"),
                    "SD handshake identity/scope differs")
            return
        if event == "start":
            require(self.seq == 2 and not self.started, "SD start is missing or repeated")
            self.started = True
            return
        require(self.started, "SD event preceded its start")
        if event == "end":
            require(self.order == len(self.phase_order) and payload == {"status": "interrupted", "natural": False},
                    "SD prefix must end interrupted after SD setup, never legacy completion")
            require(self.consumed > 0, "SD prefix consumed no source input")
            self.ended = True
            return
        require(event == "progress" and payload.get("diagnostic") == SCOPE, "Unexpected SD observer event")
        name = payload.get("name")
        require(name in PCS and payload["pc"] == PCS[name], "SD event PC differs")
        require(type(payload["consumed"]) is int, "SD consumed counter is invalid")
        data = slices(payload)
        if name == "menu":
            require(self.order == 0 and payload["consumed"] == 0, "SD menu event escaped preparation")
            return
        if name == "menu_input":
            require(self.order == 0 and payload["consumed"] == 0,
                    "SD menu input escaped preparation")
            return
        if name == "input":
            require(0 < self.order < len(self.phase_order), "SD input escaped declared prefix")
            raw = data.get((3, 0), b"")
            require(len(raw) == 0x30 and payload["consumed"] == self.consumed + 1,
                    "SD consumed sample is missing or repeated")
            verify_tick(self.plan, self.consumed, [raw[p:p + 11].hex() for p in range(0, 48, 12)])
            self.consumed += 1
            return
        require(payload["consumed"] == self.consumed, "SD event skipped consumed input")
        if name == "tick":
            require(not self.competitive_entry, "Competitive profile prefix admitted active gameplay")
            require(self.order == 2, "SD tick escaped declared VS active scene")
            current = row["source_tick"]
            require(type(current) is int and current == self.tick_count,
                    "SD source counter skipped/repeated within a scene")
            require(self.consumed > self.tick_consumed,
                    "SD scheduled iteration lacks observed preceding source consumption")
            frame, seconds, subframe = self.clock(data)
            require(frame in (self.last_clock_frame, self.last_clock_frame + 1) and
                    seconds == max(0, 60 - (frame + 59) // 60) and
                    subframe == (frame + 59) % 60,
                    "SD observed match clock skipped or escaped declared countdown")
            if (frame, seconds, subframe) == (3600, 0, 59):
                self.timeout_clock = {"scene_tick": current, "frame_count": frame,
                                      "timer_seconds": seconds, "timer_frames": subframe}
            self.tick = current
            self.tick_count += 1
            self.tick_consumed = self.consumed
            self.last_clock_frame = frame
            return
        require(self.order < len(self.phase_order) and name == self.phase_order[self.order], "SD phase order differs")
        self.order += 1
        self.records[name] = data
        if name in ("vs_entry", "sd_entry"):
            self.tick = None  # scene counter resets are explicit, never global ticks
        if name == "vs_setup":
            # Setup-time samples cannot satisfy the first active iteration.
            self.tick_consumed = self.consumed
        if name == "vs_entry":
            normal, persistent = data.get((4, 0), b""), data.get((4, 1), b"")
            verify_entry(self.plan, normal.hex())
            require(normal[0x14] == 0, "SD recipe requires original default timer subframe initialization")
            require(len(persistent) == 0x138, "SD persistent VS payload is missing")
            context = self.plan["authored_recipe"].get("cold_original_context")
            preferences = bytes(context["port_rumble_preferences"]) if context else b"\0" * 4
            require(data.get((54, 0)) == preferences,
                    "SD original profile port rumble preferences differ from declared recipe")
            normalized = (profile_rumble_copy(persistent, preferences) if
                          self.plan["authored_recipe"]["version"] >= 5 else disabled_rumble_copy(persistent))
            normalized[2] |= 0x80
            normalized[4] |= 0x40
            for slot in range(6):
                base = 0x60 + slot * 0x24
                if slot < 2:
                    require(persistent[base + 0xa] == 120,
                            "SD profile contract requires original unnamed human ports")
                    if context:
                        source_slot = persistent[base + 4]
                        port = slot if source_slot == 0 else source_slot - 1
                        require((source_slot == context["human_source_slots"][slot])
                                if "human_source_slots" in context else
                                (port == context["human_source_ports_zero_based"][slot]),
                                "SD original human source port mapping differs")
            require(bytes(normalized) == normal, "Normal VS setup differs from source rule normalization")
            raw_slots = [persistent[0x64 + slot * 0x24] for slot in range(2)]
            self.cold_context_observed = {
                "port_rumble_preferences": list(preferences), "human_raw_slots": raw_slots,
                "human_ports_zero_based": [slot if raw == 0 else raw - 1
                                           for slot, raw in enumerate(raw_slots)],
                "human_nametags": [persistent[0x6a + slot * 0x24] for slot in range(2)],
            }
        if self.competitive_entry and name == "vs_setup":
            # Ordinary VS initialization has no SD-only rules.x6=true write.
            require(data.get((4, 0)) == self.records["vs_entry"][(4, 0)],
                    "Competitive setup-return payload differs")
            for slot in range(2):
                head = data.get((5, slot), b"")
                require(len(head) == 0x100 and head[12] == slot and head[4:8] == b"\0"*4 and
                        data.get((8, slot)) == b"\4" and data.get((7, slot)) == struct.pack(">f",0),
                        "Competitive initialized fighter identity/stocks/damage differ")
        if name == "vs_exit":
            raw = data.get((15, 0), b"")
            require(len(raw) == 0x448 and raw[4:7] == bytes((1, 1, 0)) and raw[0xd] == 2,
                    "SD requires an observed tied stock timeout")
            frame, seconds, subframe = self.clock(data)
            require(self.tick_count > 0 and row["source_tick"] == self.tick_count and
                    self.timeout_clock is not None and frame == self.last_clock_frame and
                    int.from_bytes(raw[8:12], "big") == frame and seconds == 0,
                    "SD timeout lacks contiguous source inventory and observed natural clock/frame evidence")
            self.vs_inventory = {"first": 0, "last": self.tick, "count": self.tick_count,
                                 "exit_counter": row["source_tick"], "exit_match_frames": frame}
            for base in (0x58, 0x100):
                require(raw[base] == 0 and raw[base + 1] == 8 and raw[base + 5] == 0 and
                        raw[base + 8] == 4 and raw[base + 12:base + 14] == b"\0\0",
                        "SD timeout participant/damage differs")
        if name == "sd_entry":
            persistent = self.records["vs_entry"][(4, 1)]
            require(data.get((4, 1)) == persistent, "SD persistent rules changed after normal VS")
            expected = (profile_rumble_copy(persistent, self.records["vs_entry"][(54, 0)]) if
                        self.plan["authored_recipe"]["version"] == 5 else disabled_rumble_copy(persistent))
            expected[0] &= ~2  # original gm_SetupSuddenDeath disables timer
            expected[2] &= ~4  # original x2_5
            for slot in range(2):
                base = 0x60 + slot * 0x24
                expected[base + 2] = 1
                expected[base + 0x12:base + 0x14] = (300).to_bytes(2, "big")
            require(data.get((4, 0)) == bytes(expected), "SD setup differs from source participation/rules")
        if name == "sd_setup":
            expected = bytearray(self.records["sd_entry"][(4, 0)])
            expected[6] = 1  # original scene initializer's authored x6 byte
            require(data.get((4, 0)) == bytes(expected), "SD setup-return payload differs")
            for slot in range(2):
                head = data.get((5, slot), b"")
                require(len(head) == 0x100 and head[12] == slot and head[4:8] == b"\0\0\0\0",
                        "SD initialized fighter identity differs")
                require(data.get((8, slot)) == b"\1" and data.get((7, slot)) == struct.pack(">f", 300.0),
                        "SD initialized stocks/damage differ")

    def finish(self, observer_status, input_path, input_status):
        require(self.ended, "SD prefix ended before required events")
        status = read_status(observer_status)
        require(status["state"] == "interrupted" and not status["completed"] and
                not status["invalid"] and not status["error"], "SD observer prefix ending differs")
        native = validate_stream(input_path)
        validate_status(input_status, mode="record", events=native["events"])
        return {"schema": "melee-web-sd-initialization-prefix", "version": 1,
                "scope": SCOPE, "recipe_sha256": self.plan["authored_recipe_sha256"],
                "consumed_samples": self.consumed, "native_input": native,
                "source_inventory": {"vs": self.vs_inventory,
                                     "sd": {"count": 0, "scope": "setup-before-loop"}},
                "natural_timeout_clock": self.timeout_clock,
                "cold_original_context_observed": self.cold_context_observed,
                "observer_completion": "interrupted-prefix", "whole_session_admission": False}


def menu_state(data):
    """Exact original MenuFlow/MenuInputState fields, not host polling guesses."""
    scene = data.get((40, 0), b"")
    require(len(scene) == 1, "Menu scene owner is missing")
    result = {"scene": scene[0]}
    if scene[0] == 1:
        flow, inputs = data.get((45, 0), b""), data.get((46, 0), b"")
        require(len(flow) == 0x18 and len(inputs) == 8, "Menu flow/input owner is missing")
        result.update(kind=flow[0], row=int.from_bytes(flow[2:4], "big"), value=flow[4],
                      entering=flow[0x11], cooldown=int.from_bytes(inputs[:2], "big"))
    return result


def items_lock_state(data, payload, state):
    """Opt-in byte owner; a main-menu cooldown never substitutes for this lock."""
    fields = [s for s in payload["slices"] if s["tag"] == 56]
    owner = state.get("scene") == 1 and state.get("kind") == 16
    require(bool(fields) == owner, "Items lock owner is missing or unexpected")
    if owner:
        require(len(fields) == 1 and fields[0]["flags"] == 0 and
                fields[0]["address"] == 0x804d6bec and
                data.get((56, 0)) in (b"\0", b"\1"), "Items lock address/size/value differs")
        return dict(state, items_locked=data[(56, 0)][0])
    return state


class CompetitiveItemsProgress:
    """Exact authored two-column switch traversal, isolated from historical SD policy.

    A pending source transition belongs to one observed rising PAD bank. Held
    copies can drain, but cannot authorize a second transition. No value or row
    is reconstructed when a callback observation is missing.
    """
    def __init__(self):
        self.rows = list(range(16)) + list(range(30,15,-1))
        self.index = 0
        self.current = None
        self.pending = None
        self.bank = None
        self.inventory = []
        self.off = set()
        self.frequency_rights = 0
        self.commit_seen = False

    def observe(self, state, seq):
        require(state["entering"] == 1 and state["items_locked"] == 0 and
                state["row"] in self.rows + [32], "Competitive Items owner/lock/row differs")
        observed = (state["row"], state["value"])
        require(state["value"] in ((0,1,2,3) if state["row"] == 32 else (0,1)),
                "Competitive Items value differs")
        if self.current is None:
            require(observed[0] == 0, "Competitive Items first row differs")
        elif observed != self.current:
            require(self.pending is not None and observed[0] == self.pending[0] and
                    (self.pending[1] is None or observed[1] == self.pending[1]),
                    "Competitive Items changed without its declared PAD transition")
            if observed[0] != self.current[0]:
                self.index += 1
            self.pending = None
        self.current = observed
        self.inventory.append({"seq":seq,"row":observed[0],"value":observed[1]})
        if observed[0] != 32 and observed[1] == 0:
            self.off.add(observed[0])

    def input(self, state, pad, previous):
        from retail_input_plan import NEUTRAL_PAD
        from reference_versus_sequence_capture import raw_pad
        neutral = [NEUTRAL_PAD]*2
        if pad == neutral:
            self.bank = None
            return
        require(self.current == (state["row"],state["value"]),
                "Competitive Items input lacks an observed current row")
        require(state["cooldown"] == 0 and state["items_locked"] == 0 and not self.commit_seen,
                "Competitive Items input lacks unlocked ready owner")
        if pad == previous:
            require(self.bank == pad, "Competitive Items held bank is undeclared")
            return
        require(previous == neutral and self.pending is None, "Competitive Items pulse lacks neutral/settled predecessor")
        row,value = self.current
        target = None
        if pad == [raw_pad(buttons=["A"]),NEUTRAL_PAD]:
            require(row != 32 and value == 1, "Competitive Items A requires an observed on switch")
            target = (row,0)
        elif row == 32:
            if pad == [raw_pad(buttons=["D_RIGHT"]),NEUTRAL_PAD]:
                require(value > 0 and self.frequency_rights < 3, "Competitive Items frequency cannot wrap")
                self.frequency_rights += 1
                target = (32,value-1)
            else:
                require(pad == [raw_pad(buttons=["B"]),NEUTRAL_PAD] and value == 0 and self.frequency_rights == 3 and
                        self.off == set(range(31)), "Competitive Items commit lacks every observed off switch/None")
                self.commit_seen = True
        else:
            require(value == 0 and row == self.rows[self.index], "Competitive Items navigation preceded off verification")
            button = "D_DOWN" if row < 15 else "D_RIGHT" if row == 15 else "D_UP"
            require(pad == [raw_pad(buttons=[button]),NEUTRAL_PAD], "Competitive Items source two-column direction differs")
            next_row = self.rows[self.index+1] if self.index+1 < len(self.rows) else 32
            target = (next_row,3 if next_row == 32 else None)
        self.pending = target
        self.bank = pad

    def commit(self, state):
        require(self.commit_seen and self.pending is None and self.current == (32,0) and
                self.off == set(range(31)) and state ==
                {"scene":1,"kind":13,"row":5,"value":0,"entering":0,"cooldown":0},
                "Competitive Items owner left before strict committed profile")


class RulesMenuReceiver(Receiver):
    """Reduced Rules probe; unowned routing is observation, never scene admission.

    SceneKind omission does not attest a null pointer: the native producer also
    omits it on a failed pointer read. No steering or readiness precedes an owner.
    """
    def __init__(self, plan, *, profile_campaign=False, full_route=False, items_probe=False,
                 guarded_items=False, competitive_entry=False):
        super().__init__(plan, competitive_entry=competitive_entry)
        require(plan["authored_recipe"]["version"] == (6 if competitive_entry else 5 if full_route else 4 if profile_campaign else 3),
                "Rules probe recipe/profile campaign differs")
        self.full_route = full_route
        self.items_probe = items_probe
        self.guarded_items = guarded_items
        self.items_guard = items_probe or guarded_items
        require(not competitive_entry or (profile_campaign and full_route and guarded_items and not items_probe),
                "Competitive entry scope must own the full guarded original menu")
        self.competitive_items = CompetitiveItemsProgress() if competitive_entry else None
        require(not guarded_items or (full_route and not items_probe), "Guarded full Items scope differs")
        self.items_ready = False
        self.items_up_seen = False
        self.items_entry_drain = False
        self.items_entry_drain_closed = False
        self.items_entry_drain_start = None
        self.items_entry_drain_samples = []
        self.items_entry_neutral = None
        self.items_frequency = None
        self.items_rights = 0
        self.items_right_pending = False
        self.items_commit_seen = False
        self.items_committed = False
        self.sss_confirmation = None
        self.sss_countdown = None
        self.sss_countdown_tick = None
        self.sss_confirmation_neutral = False
        self.sss_countdown_inventory = []
        self.sss_retirement = None
        self.sss_retirement_inventory = []
        require(not items_probe or full_route, "Reduced Items probe requires its recipe-five owner")
        self.menu_consumed = 0
        self.menu_polls = 0
        self.last_pad = None
        self.latest_menu = None
        self.ready = False
        self.rules_entering = 1 if profile_campaign else 0
        self.scene_owner_seen = False
        self.pre_owner_polls = 0
        self.bootstrap_routes = []
        from sd_original_menu_plan import rules_ready_packet
        from retail_input_plan import NEUTRAL_PAD
        packet = rules_ready_packet()
        if full_route:
            from sd_original_menu_plan import gci_sd_prefix_packet, route_pads
            packet = gci_sd_prefix_packet(7 if guarded_items else 5)
            if competitive_entry:
                from sd_original_menu_plan import gci_competitive_entry_packet
                packet = gci_competitive_entry_packet()
            if items_probe:
                from sd_original_menu_plan import gci_items_row_packet
                packet = gci_items_row_packet()
            self.declared_menu_pads = route_pads(packet)
            self.css = None
            self.stage = None
            self.final_css = None
            self.final_stage = None
            return
        self.declared_menu_pads = {(NEUTRAL_PAD, NEUTRAL_PAD)} | {
            (action["p1"], action["p2"]) for action in packet["boot"] + packet["actions"]}

    def accept(self, row):
        event, payload = row["event"], row["payload"]
        if event == "handshake":
            require(payload.get("menu_probe") == ("competitive_entry" if self.competitive_entry else "items_row" if self.items_probe else "sd_prefix" if self.full_route else "rules_ready"),
                    "Rules probe scope differs")
            require(payload.get("profile_gci_sha256", "") == getattr(self, "profile_sha256", ""),
                    "Rules probe loaded-profile identity differs")
            forwarded = dict(row, payload=dict(payload, menu_probe=""))
            return super().accept(forwarded)
        if event == "start":
            return super().accept(row)
        if self.full_route and not self.items_probe and (event == "end" or self.order or
                (event == "progress" and payload.get("name") not in ("menu", "menu_input", "rules_ready"))):
            require(self.ready, "Gameplay preceded verified loaded Rules readiness")
            if payload.get("name") == "vs_entry":
                require(not self.guarded_items or self.items_committed,
                        "VS entry lacks observed committed Items progression")
                require(not self.guarded_items or (self.sss_confirmation is not None and
                        self.sss_countdown == 0 and self.sss_confirmation_neutral),
                        "VS entry lacks observed SSS confirmation countdown/release")
                require(self.final_css is not None and self.final_stage is not None and
                        [p["character"] for p in self.final_css["players"]] == [8, 8] and
                        [p["kind"] for p in self.final_css["players"]] == [0, 0] and
                        [d["costume"] for d in self.final_css["doors"]] == [1, 0] and
                        self.final_stage["kind"] == 32 and self.final_stage["cooldown"] == 0,
                        "VS entry lacks observed final human CSS/FD acceptance prerequisites")
            return super().accept(row)
        require(not self.ended and self.started and row["seq"] == self.seq,
                "Rules probe sequence/start differs")
        self.seq += 1
        if event == "end":
            require(self.ready and (not self.items_probe or self.items_ready) and payload == {"status": "interrupted", "natural": False},
                    "Rules probe lacks bounded interrupted readiness ending")
            self.ended = True
            return
        require(event == "progress" and payload.get("diagnostic") == SCOPE and
                type(payload.get("consumed")) is int and payload["consumed"] == 0,
                "Rules probe escaped menu-only scope")
        name = payload.get("name")
        require(name in (("menu", "menu_input", "rules_ready", "items_ready") if self.items_probe else
                        ("menu", "menu_input", "rules_ready")) and payload.get("pc") == PCS[name],
                "Rules probe event owner differs")
        data = slices(payload)
        count = payload.get("menu_consumed")
        require(type(count) is int, "Rules probe source input inventory missing")
        if not self.scene_owner_seen:
            require(name == "menu", "Rules probe input/readiness preceded its first scene owner")
            if (40, 0) not in data:
                require(set(data) == {(17, 0)} and len(data[(17, 0)]) == 6 and
                        payload["slices"][0]["address"] == 0x80479d30 and
                        count == 0 and self.menu_consumed == 0 and
                        type(row["source_tick"]) is int and row["source_tick"] == 0,
                        "Rules probe unowned bootstrap observation differs")
                self.menu_polls += 1
                self.pre_owner_polls += 1
                require(self.menu_polls <= 7200, "Rules probe menu polling cap exhausted")
                self.bootstrap_routes.append({"seq": row["seq"], "hex": data[(17, 0)].hex()})
                return  # No guessed scene, PAD intention or readiness is produced.
            self.latest_menu = menu_state(data)
            self.scene_owner_seen = True
        if name == "menu_input":
            require(self.sss_retirement is None, "SSS retirement consumed new input")
            require(count == self.menu_consumed + 1 and count <= 7200,
                    "Rules probe source input gap/repeat/cap")
            raw = data.get((3, 0), b"")
            require(len(raw) == 48, "Rules probe source PAD missing")
            previous_pad = self.last_pad
            self.last_pad = [raw[p:p + 11].hex() for p in range(0, 48, 12)]
            require(tuple(self.last_pad[:2]) in self.declared_menu_pads,
                    "Rules probe consumed undeclared menu PAD intent")
            from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD
            require(self.last_pad[2:] == [DISCONNECTED_PAD] * 2,
                    "Rules probe inactive controllers changed")
            if self.guarded_items and self.latest_menu.get("scene") == 9:
                from reference_versus_sequence_capture import raw_pad
                select = [raw_pad(buttons=["A"]), NEUTRAL_PAD]
                if self.sss_confirmation is not None:
                    require(self.last_pad[:2] == [NEUTRAL_PAD]*2 or
                            (not self.sss_confirmation_neutral and self.last_pad[:2] == select and
                             previous_pad == self.last_pad),
                            "SSS consumed a new continuation after confirmation")
                    if self.last_pad[:2] == [NEUTRAL_PAD]*2:
                        self.sss_confirmation_neutral = True
                elif self.last_pad[:2] == select:
                    require(self.stage is not None and self.stage["kind"] == 32 and
                            self.stage["cooldown"] == 0 and previous_pad is not None and
                            previous_pad[:2] == [NEUTRAL_PAD]*2,
                            "SSS confirmation lacks observed FD/zero/neutral predicate")
                    self.sss_confirmation = {"seq":row["seq"], "source_tick":row["source_tick"],
                                             "menu_consumed":count, "stage":self.stage.copy()}
            if self.items_guard and self.latest_menu.get("kind") == 16:
                from reference_versus_sequence_capture import raw_pad
                up = [raw_pad(buttons=["D_UP"]), NEUTRAL_PAD]
                right = [raw_pad(buttons=["D_RIGHT"]), NEUTRAL_PAD]
                back = [raw_pad(buttons=["B"]), NEUTRAL_PAD]
                opening = [raw_pad(buttons=["A"]), NEUTRAL_PAD]
                if self.last_pad[:2] == opening:
                    if self.competitive_entry and self.items_entry_drain_closed:
                        require(previous_pad is not None, "Competitive Items A preceded its observed entry PAD")
                        self.competitive_items.input(self.latest_menu, self.last_pad[:2], previous_pad[:2])
                        self.menu_consumed = count
                        return
                    require(self.items_entry_drain and not self.items_entry_drain_closed and
                            self.latest_menu.get("items_locked") == 1 and
                            previous_pad == self.last_pad and
                            self.menu_polls - self.items_entry_drain_start < 600,
                            "Items opening A escaped its locked entry drain")
                    self.items_entry_drain_samples.append({"seq":row["seq"], "menu_consumed":count})
                    self.menu_consumed = count
                    return
                allowed = ([NEUTRAL_PAD]*2, up, right, back,
                           [raw_pad(buttons=["D_DOWN"]),NEUTRAL_PAD]) if self.competitive_entry else (
                           ([NEUTRAL_PAD]*2, up, right, back) if self.guarded_items else ([NEUTRAL_PAD]*2, up))
                require(self.last_pad[:2] in allowed, "Items consumed undeclared continuation")
                if self.items_entry_drain:
                    require(self.last_pad[:2] == [NEUTRAL_PAD]*2,
                            "Items entry drain lacks a neutral ending")
                    self.items_entry_drain = False
                    self.items_entry_drain_closed = True
                    self.items_entry_neutral = {"seq":row["seq"], "menu_consumed":count}
                require(self.latest_menu.get("items_locked") == 0 or
                        self.last_pad[:2] == [NEUTRAL_PAD] * 2,
                        "Items input consumed while locked")
                require(previous_pad is not None, "Items input preceded its observed entry PAD")
                if self.competitive_entry:
                    if self.competitive_items.current is None and self.latest_menu["items_locked"] == 0:
                        self.competitive_items.observe(self.latest_menu, row["seq"])
                    self.competitive_items.input(self.latest_menu, self.last_pad[:2], previous_pad[:2])
                    self.menu_consumed = count
                    return
                if self.guarded_items and self.last_pad[:2] == right:
                    require(self.items_up_seen and self.latest_menu["row"] == 31 and
                            self.items_frequency is not None and not self.items_commit_seen,
                            "Items Right lacks observed frequency owner")
                    if previous_pad[:2] != right:
                        require(previous_pad[:2] == [NEUTRAL_PAD]*2 and not self.items_right_pending and
                                self.items_frequency > 0 and self.items_rights < 3,
                                "Items frequency Right pulse differs")
                        self.items_rights += 1
                        self.items_right_pending = True
                if self.guarded_items and self.last_pad[:2] == back:
                    require(self.latest_menu["row"] == 31 and self.items_frequency == 0 and
                            self.items_rights == 3 and not self.items_right_pending and
                            (previous_pad[:2] == back or (previous_pad[:2] == [NEUTRAL_PAD]*2 and
                             not self.items_commit_seen)), "Items commit lacks declared None progression")
                    self.items_commit_seen = True
                if self.last_pad[:2] == up and previous_pad[:2] != up:
                    require(not self.items_up_seen and (not self.guarded_items or self.latest_menu["row"] == 0),
                            "Items consumed a second Up pulse")
                    self.items_up_seen = True
            self.menu_consumed = count
            return
        require(count == self.menu_consumed, "Rules probe skipped observed input")
        previous_menu = self.latest_menu
        self.latest_menu = menu_state(data)
        if self.items_guard:
            self.latest_menu = items_lock_state(data, payload, self.latest_menu)
            if self.items_entry_drain:
                require(self.latest_menu.get("scene") == 1 and self.latest_menu.get("kind") == 16 and
                        self.latest_menu.get("row") == 0 and self.latest_menu.get("value") == 1 and
                        self.latest_menu.get("entering") == 1 and self.latest_menu.get("items_locked") == 1 and
                        self.menu_polls - self.items_entry_drain_start < 600,
                        "Items locked entry drain owner/poll cap differs")
            elif not self.items_entry_drain_closed and not self.items_up_seen and previous_menu == {
                    "scene":1,"kind":13,"row":5,"value":0,"entering":0,"cooldown":0}:
                from reference_versus_sequence_capture import raw_pad
                from retail_input_plan import NEUTRAL_PAD
                if (self.latest_menu.get("scene"), self.latest_menu.get("kind"),
                        self.latest_menu.get("row"), self.latest_menu.get("value"),
                        self.latest_menu.get("entering"), self.latest_menu.get("items_locked")) == (1,16,0,1,1,1):
                    require(self.last_pad is not None and
                            self.last_pad[:2] == [raw_pad(buttons=["A"]),NEUTRAL_PAD],
                            "Items locked entry lacks its declared opening A")
                    self.items_entry_drain = True
                    self.items_entry_drain_start = self.menu_polls
            if self.competitive_entry:
                if self.latest_menu.get("kind") == 16 and self.items_entry_drain_closed:
                    if self.latest_menu["items_locked"] == 1 and self.competitive_items.current is None:
                        require(self.latest_menu["row"] == 0 and self.latest_menu["value"] == 1 and
                                self.latest_menu["entering"] == 1,
                                "Competitive Items locked initial owner differs")
                    else:
                        self.competitive_items.observe(self.latest_menu, row["seq"])
                elif previous_menu is not None and previous_menu.get("kind") == 16 and self.latest_menu.get("kind") != 16:
                    self.competitive_items.commit(self.latest_menu)
                    self.items_committed = True
            elif self.guarded_items:
                owner = self.latest_menu.get("scene") == 1 and self.latest_menu.get("kind") == 16
                if owner:
                    require(not self.items_committed and self.latest_menu["row"] in (0,31),
                            "Items progression owner/row differs")
                    if self.latest_menu["row"] == 0:
                        require(self.latest_menu["value"] == 1 and self.items_frequency is None,
                                "Items initial frequency owner differs")
                    else:
                        require(self.items_up_seen and self.latest_menu["items_locked"] == 0,
                                "Items frequency row lacks unlocked Up")
                        value = self.latest_menu["value"]
                        if self.items_frequency is None:
                            require(value == 3, "Items initial frequency differs")
                            self.items_frequency = value
                        elif value != self.items_frequency:
                            require(self.items_right_pending and value == self.items_frequency-1,
                                    "Items frequency changed without declared Right")
                            self.items_frequency = value
                            self.items_right_pending = False
                elif previous_menu is not None and previous_menu.get("kind") == 16:
                    require(self.items_commit_seen and self.latest_menu.get("scene") == 1 and
                            self.latest_menu.get("kind") == 13 and self.latest_menu.get("row") == 5 and
                            self.latest_menu.get("value") == 0 and self.latest_menu.get("entering") == 0,
                            "Items owner left before declared commit")
                    self.items_committed = True
            if name == "items_ready":
                from retail_input_plan import NEUTRAL_PAD
                require(self.ready and self.items_up_seen and not self.items_ready and self.last_pad[:2] == [NEUTRAL_PAD]*2 and
                        self.latest_menu == {"scene":1,"kind":16,"row":31,"value":3,
                        "entering":1,"cooldown":0,"items_locked":0}, "Items ready predicate differs")
                self.items_ready = True
                return
        if self.full_route and not self.items_probe and name == "menu":
            require(self.sss_confirmation is None or self.latest_menu["scene"] == 9,
                    "SSS retirement owner changed before VS entry")
            self.css = css_state(data) if self.latest_menu["scene"] == 8 else None
            self.stage = stage_state(data, payload, confirmation=self.sss_confirmation is not None) if self.latest_menu["scene"] == 9 else None
            require(self.latest_menu["scene"] != 8 or self.final_css is None or self.css is not None,
                    "CSS typed owner disappeared after construction")
            require(self.latest_menu["scene"] != 9 or self.final_stage is None or self.stage is not None,
                    "SSS typed owner disappeared after construction")
            if self.css is not None:
                self.final_css = self.css
            if self.stage is not None:
                if self.sss_confirmation is not None:
                    require(self.stage["index"] == self.sss_confirmation["stage"]["index"] and
                            self.stage["kind"] == 32,
                            "SSS confirmed FD owner changed")
                    # gm_801A4014 advances routing after OnExit, before the next
                    # gm_801A4B88 replaces SceneInfo. PADRead observations in
                    # that interval retain the old owner and frozen scene tick.
                    route = next((s for s in payload["slices"] if s["tag"] == 17 and s["flags"] == 0), None)
                    require(route is not None and route["address"] == 0x80479d30 and
                            len(data[(17, 0)]) == 6, "SSS routing observation differs")
                    retiring = data[(17, 0)] == bytes((2, 2, 1, 2, 1, 0))
                    if retiring or self.sss_retirement is not None:
                        require(retiring and self.sss_countdown == 0 and self.sss_confirmation_neutral and
                                type(row["source_tick"]) is int and
                                row["source_tick"] == self.sss_countdown_tick and
                                self.stage["cooldown"] == 0 and count == self.menu_consumed,
                                "SSS retirement frozen state differs")
                        frozen = {"source_tick": row["source_tick"], "menu_consumed": count,
                                  "stage": self.stage.copy(), "routing": route["hex"]}
                        require(self.sss_retirement is None or frozen == self.sss_retirement,
                                "SSS retirement snapshot changed")
                        self.sss_retirement = frozen
                        self.sss_retirement_inventory.append(dict(frozen, seq=row["seq"]))
                        self.menu_polls += 1
                        require(self.menu_polls <= 7200, "Rules probe menu polling cap exhausted")
                        return  # Retained observation, never source advancement.
                    require(data[(17, 0)] == bytes((2, 2, 1, 1, 0, 0)),
                            "SSS active routing differs")
                    prior_tick = (self.sss_confirmation["source_tick"] if self.sss_countdown is None else
                                  self.sss_countdown_tick)
                    expected = 30 if self.sss_countdown is None else max(0,self.sss_countdown-1)
                    require(row["source_tick"] == prior_tick+1 and self.stage["cooldown"] == expected,
                            "SSS confirmation countdown gap/repeat/value differs")
                    self.sss_countdown = expected
                    self.sss_countdown_tick = row["source_tick"]
                    self.sss_countdown_inventory.append({"seq":row["seq"], "source_tick":row["source_tick"],
                                                         "cooldown":expected})
                else:
                    self.final_stage = self.stage
        if name == "menu":
            self.menu_polls += 1
            require(self.menu_polls <= 7200, "Rules probe menu polling cap exhausted")
        else:
            from retail_input_plan import NEUTRAL_PAD
            require(not self.ready and self.last_pad is not None and
                    self.last_pad[:2] == [NEUTRAL_PAD] * 2 and self.latest_menu ==
                    {"scene": 1, "kind": 13, "row": 0, "value": 0,
                     "entering": self.rules_entering, "cooldown": 0},
                    "Rules probe lacks observed neutral Rules-ready owner")
            require(data.get((54, 0)) == bytes((1, 1, 1, 1)),
                    "Rules probe cold original preferences differ before Options preparation")
            self.ready = True

    def finish(self, observer_status, input_path, input_status):
        require(self.ended, "Rules probe ended before readiness")
        status = read_status(observer_status)
        require(status["state"] == "interrupted" and not status["completed"] and
                not status["invalid"] and not status["error"], "Rules probe observer ending differs")
        native = validate_stream(input_path)
        validate_status(input_status, mode="record", events=native["events"])
        return {"schema": "melee-web-original-rules-ready-probe", "version": 1,
                "scope": "rules_ready", "recipe_sha256": self.plan["authored_recipe_sha256"],
                "menu_source_samples": self.menu_consumed, "menu_polls": self.menu_polls,
                "pre_owner_polls": self.pre_owner_polls,
                "bootstrap_routes": self.bootstrap_routes,
                "native_input": native, "cold_port_preferences": [1, 1, 1, 1],
                "full_sd_prefix_admission": False, "whole_session_admission": False}


class GciRulesMenuReceiver(RulesMenuReceiver):
    """New profile campaign, with observed loaded fields at the reduced ready gate."""
    def __init__(self, plan, profile, *, full_route=False, items_probe=False, guarded_items=False,
                 competitive_entry=False):
        import hashlib
        from sd_gci_profile import GCI_SHA256
        require(profile["sha256"] == GCI_SHA256 and
                hashlib.sha256(profile["raw"]).hexdigest() == GCI_SHA256,
                "Rules profile input identity differs")
        self.profile_sha256 = GCI_SHA256
        self.profile = profile
        self.loaded_context = None
        super().__init__(plan, profile_campaign=True, full_route=full_route, items_probe=items_probe,
                         guarded_items=guarded_items, competitive_entry=competitive_entry)

    def accept(self, row):
        if self.competitive_entry and row["payload"].get("name") == "vs_entry":
            data = slices(row["payload"])
            rules = data.get((38,0), b"")
            fields = {"mode":2,"stock_count":4,"handicap":5,"damage_ratio":6,
                      "stock_time_limit":8,"friendly_fire":9,"pause":10}
            address = next((s["address"] for s in row["payload"]["slices"] if s["tag"] == 38 and s["flags"] == 0),None)
            require(self.loaded_context is not None and len(rules) == 0x18 and
                    address == self.loaded_context["save_address"] - 0x18 and
                    {key:rules[offset] for key,offset in fields.items()} == self.plan["authored_recipe"]["expected_game_rules"],
                    "Competitive committed GameRules differ")
            save = data.get((39,0), b"")
            save_address = next((s["address"] for s in row["payload"]["slices"] if s["tag"] == 39 and s["flags"] == 0),None)
            expected = bytearray(self.profile["save"][0x448:0x468])
            expected[0] = 0xff  # mnItemSw_CommitItems: x21 - 1
            expected[8:16] = bytes.fromhex(self.plan["authored_recipe"]["expected_item_preference_mask_hex"])
            require(len(save) == 0x55e8 and save_address == self.loaded_context["save_address"] and
                    save[0x448:0x468] == bytes(expected), "Competitive committed item preference bytes differ")
            self.committed_game_rules = rules.hex()
        if row["event"] == "progress" and row["payload"].get("name") == "rules_ready":
            import hashlib
            from sd_gci_profile import SAVE_BYTES, BANK_BYTES
            data = slices(row["payload"])
            save, rules = data.get((39, 0), b""), data.get((38, 0), b"")
            require(len(save) == 0x55e8 and len(rules) == 0x18 and
                    data.get((36, 0)) == bytes.fromhex("07ff") and
                    data.get((37, 0)) == bytes.fromhex("07ff"), "Loaded profile extents/unlocks differ")
            addresses = {(s["tag"], s["flags"]): s["address"] for s in row["payload"]["slices"]}
            root = addresses[(39, 0)] - 0x1868
            require(all(addresses[key] == root + offset for key, offset in
                        (((36, 0), 0x1868), ((37, 0), 0x186a), ((38, 0), 0x1850), ((54, 0), 0x1cc0))),
                    "Loaded profile source roots differ")
            require(save[:5] == self.profile["save"][:5] and
                    save[0x448:0x468] == self.profile["save"][0x448:0x468] and
                    save[SAVE_BYTES:] == b"".join(self.profile["banks"][:2]),
                    "Loaded profile declared preferences/name extents differ")
            self.loaded_context = {"seq": row["seq"], "save_address": addresses[(39, 0)],
                "save_hex": save.hex(), "game_rules_hex": rules.hex(),
                "verified_ranges": ["SaveData0..4", "SaveData0x448..0x467", "two complete source name banks"],
                "name_bank_sha256": [hashlib.sha256(save[SAVE_BYTES + i * BANK_BYTES:
                        SAVE_BYTES + (i + 1) * BANK_BYTES]).hexdigest() for i in range(2)],
                "other_source_progress_bytes": "retained observations; no equality or gameplay claim"}
        return super().accept(row)

    def finish(self, observer_status, input_path, input_status):
        require(self.loaded_context is not None, "Loaded profile was not observed")
        report = (Receiver.finish(self, observer_status, input_path, input_status) if self.full_route and not self.items_probe
                  else super().finish(observer_status, input_path, input_status))
        report.update(scope="items_row_gci" if self.items_probe else "sd_prefix_gci" if self.full_route else "rules_ready_gci", profile_gci_sha256=self.profile_sha256,
                      loaded_context=self.loaded_context)
        if self.items_probe:
            report.update(schema="melee-web-original-items-row-probe",
                          items_ready=self.items_ready, stop=self.latest_menu,
                          opening_entry_drain_samples=self.items_entry_drain_samples,
                          opening_entry_neutral=self.items_entry_neutral)
        if self.full_route:
            report.update(menu_source_samples=self.menu_consumed, menu_polls=self.menu_polls,
                          pre_owner_polls=self.pre_owner_polls, bootstrap_routes=self.bootstrap_routes)
        if self.guarded_items:
            report.update(menu_version=7, items_committed=self.items_committed,
                          items_frequency_right_pulses=self.items_rights,
                          opening_entry_drain_samples=self.items_entry_drain_samples,
                          opening_entry_neutral=self.items_entry_neutral)
            report.update(sss_confirmation=self.sss_confirmation,
                          sss_confirmation_countdown=self.sss_countdown_inventory,
                          sss_confirmation_neutral=self.sss_confirmation_neutral,
                          sss_retirement_observations=self.sss_retirement_inventory)
        if self.competitive_entry:
            report.update(schema="melee-web-original-competitive-profile-entry",scope="competitive_entry_gci",
                menu_version=8, committed_game_rules_hex=self.committed_game_rules,
                item_rows_observed=self.competitive_items.inventory,
                items_frequency_right_pulses=self.competitive_items.frequency_rights,
                compared_setup=self.plan["authored_recipe"]["expected_setup"],
                natural_timeout_admission=False, results_css_admission=False,
                source_inventory={"vs":{"count":0,"scope":"setup-before-loop"}},
                full_sd_prefix_admission=False)
        return report


def css_state(data):
    """Existing generic typed CSS inventory; absent during OnEnter is not readiness."""
    if (48, 0) not in data:
        return None
    live, doors = data[(48, 0)], data.get((44, 0), b"")
    require(len(live) == 0x148 and len(doors) == 0x90, "CSS typed owner extent differs")
    result = {"players": [], "doors": [], "cursors": [], "models": []}
    for slot in range(2):
        cursor, model = data.get((43, slot), b""), data.get((47, slot), b"")
        require(len(cursor) == 0x14 and len(model) == 0x18, "Human CSS cursor/model is missing")
        x, y = struct.unpack(">ff", cursor[12:20])
        mx, my = struct.unpack(">ff", model[8:16])
        import math
        require(all(math.isfinite(v) for v in (x, y, mx, my)), "CSS coordinate is invalid")
        result["cursors"].append({"port": cursor[4], "state": cursor[5], "held": cursor[6], "x": x, "y": y})
        result["models"].append({"owner": model[5], "x": mx, "y": my})
        base = 0x70 + slot * 0x24
        result["players"].append({"character": live[base], "kind": live[base+1], "slot": live[base+4]})
        base = slot * 0x24
        result["doors"].append({"kind": doors[base+11], "costume": doors[base+13],
                                "icon": doors[base+14]})
    require(live[0x18] == 0 and all(live[0x70 + slot*0x24 + 1] == 3 for slot in (2, 3)),
            "CSS Teams/inactive-player contract differs")
    return result


def stage_state(data, payload, *, confirmation=False):
    if (55, 0) not in data:
        require((41, 0) not in data and (42, 0) not in data,
                "SSS highlight preceded constructor-owned cooldown")
        return None
    cooldown, index, kind = data[(55, 0)], data.get((41, 0), b""), data.get((42, 0), b"")
    addresses = {(s["tag"], s["flags"]): s["address"] for s in payload["slices"]}
    require(len(cooldown) == 4 and len(index) == 1 and index[0] <= 30 and
            addresses[(55, 0)] == 0x804d6ca4 and addresses[(41, 0)] == 0x804d6cae,
            "SSS cooldown/highlight source binding differs")
    require((index[0] == 30 and (42, 0) not in data) or
            (index[0] < 30 and len(kind) == 1 and
             addresses[(42, 0)] == 0x803f06d0 + index[0]*0x1c + 0xb),
            "SSS authored random/highlight row differs")
    value = int.from_bytes(cooldown, "big")
    require(value <= (30 if confirmation else 20),
            "SSS selection cooldown escaped its authored bound" if confirmation else
            "SSS constructor cooldown escaped its authored bound")
    return {"index": index[0], "kind": kind[0] if kind else None, "cooldown": value}
