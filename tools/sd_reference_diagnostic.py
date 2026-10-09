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
       "input": 0x80377584, "tick": 0x80390eb4, "menu": 0x8034dd8c}
ORDER = ("vs_entry", "vs_setup", "vs_exit", "vs_retired", "sd_entry", "sd_setup")


class SdDiagnosticError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SdDiagnosticError(message)


def slices(payload):
    require(set(payload) == {"diagnostic", "name", "consumed", "pc", "slices"}, "SD event fields differ")
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
                    not payload.get("whole_session"), "SD handshake identity/scope differs")
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
            require(0 < self.order < len(ORDER), "SD tick escaped declared prefix")
            current = row["source_tick"]
            require(type(current) is int and (self.tick is None or current == self.tick + 1),
                    "SD source counter skipped/repeated within a scene")
            self.tick = current
            return
        require(self.order < len(ORDER) and name == ORDER[self.order], "SD phase order differs")
        self.order += 1
        self.records[name] = data
        if name in ("vs_entry", "sd_entry"):
            self.tick = None  # scene counter resets are explicit, never global ticks
        if name == "vs_entry":
            normal, persistent = data.get((4, 0), b""), data.get((4, 1), b"")
            verify_entry(self.plan, normal.hex())
            require(len(persistent) == 0x138, "SD persistent VS payload is missing")
            normalized = bytearray(persistent)
            normalized[2] |= 0x80
            normalized[4] |= 0x40
            require(bytes(normalized) == normal, "Normal VS setup differs from source rule normalization")
        if name == "vs_exit":
            raw = data.get((15, 0), b"")
            require(len(raw) == 0x448 and raw[4:7] == bytes((1, 1, 0)) and raw[0xd] == 2,
                    "SD requires an observed tied stock timeout")
            for base in (0x58, 0x100):
                require(raw[base] == 0 and raw[base + 1] == 8 and raw[base + 5] == 0 and
                        raw[base + 8] == 4 and raw[base + 12:base + 14] == b"\0\0",
                        "SD timeout participant/damage differs")
        if name == "sd_entry":
            persistent = self.records["vs_entry"][(4, 1)]
            require(data.get((4, 1)) == persistent, "SD persistent rules changed after normal VS")
            expected = bytearray(persistent)
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
                "observer_completion": "interrupted-prefix", "whole_session_admission": False}
