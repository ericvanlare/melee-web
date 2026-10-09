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
       "menu": 0x8034dd8c, "rules_ready": 0x8034dd8c}
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
    def __init__(self, plan):
        validate_plan(plan)
        require(plan["version"] == AUTHORED_PLAN_VERSION, "SD receiver requires authored v4")
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
            require(self.order == len(ORDER) and payload == {"status": "interrupted", "natural": False},
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
            require(0 < self.order < len(ORDER), "SD input escaped declared prefix")
            raw = data.get((3, 0), b"")
            require(len(raw) == 0x30 and payload["consumed"] == self.consumed + 1,
                    "SD consumed sample is missing or repeated")
            verify_tick(self.plan, self.consumed, [raw[p:p + 11].hex() for p in range(0, 48, 12)])
            self.consumed += 1
            return
        require(payload["consumed"] == self.consumed, "SD event skipped consumed input")
        if name == "tick":
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
        require(self.order < len(ORDER) and name == ORDER[self.order], "SD phase order differs")
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
            normalized = disabled_rumble_copy(persistent)
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
            expected = disabled_rumble_copy(persistent)
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


class RulesMenuReceiver(Receiver):
    """Reduced Rules probe; unowned routing is observation, never scene admission.

    SceneKind omission does not attest a null pointer: the native producer also
    omits it on a failed pointer read. No steering or readiness precedes an owner.
    """
    def __init__(self, plan, *, profile_campaign=False):
        super().__init__(plan)
        require(plan["authored_recipe"]["version"] == (4 if profile_campaign else 3),
                "Rules probe recipe/profile campaign differs")
        self.menu_consumed = 0
        self.menu_polls = 0
        self.last_pad = None
        self.latest_menu = None
        self.ready = False
        self.scene_owner_seen = False
        self.pre_owner_polls = 0
        self.bootstrap_routes = []
        from sd_original_menu_plan import rules_ready_packet
        from retail_input_plan import NEUTRAL_PAD
        packet = rules_ready_packet()
        self.declared_menu_pads = {(NEUTRAL_PAD, NEUTRAL_PAD)} | {
            (action["p1"], action["p2"]) for action in packet["boot"] + packet["actions"]}

    def accept(self, row):
        event, payload = row["event"], row["payload"]
        if event == "handshake":
            require(payload.get("menu_probe") == "rules_ready", "Rules probe scope differs")
            require(payload.get("profile_gci_sha256", "") == getattr(self, "profile_sha256", ""),
                    "Rules probe loaded-profile identity differs")
            forwarded = dict(row, payload=dict(payload, menu_probe=""))
            return super().accept(forwarded)
        if event == "start":
            return super().accept(row)
        require(not self.ended and self.started and row["seq"] == self.seq,
                "Rules probe sequence/start differs")
        self.seq += 1
        if event == "end":
            require(self.ready and payload == {"status": "interrupted", "natural": False},
                    "Rules probe lacks bounded interrupted readiness ending")
            self.ended = True
            return
        require(event == "progress" and payload.get("diagnostic") == SCOPE and
                type(payload.get("consumed")) is int and payload["consumed"] == 0,
                "Rules probe escaped menu-only scope")
        name = payload.get("name")
        require(name in ("menu", "menu_input", "rules_ready") and payload.get("pc") == PCS[name],
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
            require(count == self.menu_consumed + 1 and count <= 7200,
                    "Rules probe source input gap/repeat/cap")
            raw = data.get((3, 0), b"")
            require(len(raw) == 48, "Rules probe source PAD missing")
            self.last_pad = [raw[p:p + 11].hex() for p in range(0, 48, 12)]
            require(tuple(self.last_pad[:2]) in self.declared_menu_pads,
                    "Rules probe consumed undeclared menu PAD intent")
            from retail_input_plan import DISCONNECTED_PAD
            require(self.last_pad[2:] == [DISCONNECTED_PAD] * 2,
                    "Rules probe inactive controllers changed")
            self.menu_consumed = count
            return
        require(count == self.menu_consumed, "Rules probe skipped observed input")
        self.latest_menu = menu_state(data)
        if name == "menu":
            self.menu_polls += 1
            require(self.menu_polls <= 7200, "Rules probe menu polling cap exhausted")
        else:
            from retail_input_plan import NEUTRAL_PAD
            require(not self.ready and self.last_pad is not None and
                    self.last_pad[:2] == [NEUTRAL_PAD] * 2 and self.latest_menu ==
                    {"scene": 1, "kind": 13, "row": 0, "value": 0, "entering": 0, "cooldown": 0},
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
    def __init__(self, plan, profile):
        import hashlib
        from sd_gci_profile import GCI_SHA256
        require(profile["sha256"] == GCI_SHA256 and
                hashlib.sha256(profile["raw"]).hexdigest() == GCI_SHA256,
                "Rules profile input identity differs")
        self.profile_sha256 = GCI_SHA256
        self.profile = profile
        self.loaded_context = None
        super().__init__(plan, profile_campaign=True)

    def accept(self, row):
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
        report = super().finish(observer_status, input_path, input_status)
        report.update(scope="rules_ready_gci", profile_gci_sha256=self.profile_sha256,
                      loaded_context=self.loaded_context)
        return report
