"""Synthetic refusal controls for the Stadium GO/C/F state companion."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"),
                str(ROOT / "reference-capture" / "dolphin"),
                str(ROOT / "tests")]

import reference_observer_stream as observer_stream  # noqa: E402

from stadium_go_prefix import (  # noqa: E402
    GO_AFTER_PC,
    SOURCE_TICK_PC,
    STADIUM_IDLE_STATE_SCHEMA,
    StadiumGoPrefixError,
    _validate_stadium_idle_state_record,
    validate_stadium_go_prefix,
    validate_stadium_idle_state_snapshot,
)


from owned_test_workspace import OwnedWorkspaceTests  # noqa: E402
import test_stadium_go_prefix as route_fixtures  # noqa: E402


def _new_scratch_dir(root: Path, prefix: str) -> Path:
    return Path(tempfile.mkdtemp(prefix=prefix, dir=root))


def _slice(tag: int, flags: int, address: int, data: bytes) -> dict:
    return {"tag": tag, "flags": flags, "address": address,
            "size": len(data), "hex": data.hex()}


def _u16(value: int) -> bytes:
    return value.to_bytes(2, "big", signed=False)


def _u32(value: int) -> bytes:
    return value.to_bytes(4, "big", signed=False)


def _record(label: str = "GO") -> dict:
    roots = {0: 0x80410000, 1: 0x80410100,
             2: 0x80410200, 5: 0x80410500}
    grounds = {0: 0x80411000, 1: 0x80411100,
               2: 0x80411200, 5: 0x80411500}
    slices = []
    for map_id in (0, 1, 2, 5):
        root_slot = 0x8049E6C8 + 0x180 + map_id * 4
        slices.append(_slice(67, map_id, root_slot, _u32(roots[map_id])))
        slices.append(_slice(68, map_id, roots[map_id] + 0x2C,
                             _u32(grounds[map_id])))

    map2 = grounds[2]
    for tag, offset, size, value in (
            (69, 0xC4, 1, b"\x80"),
            (70, 0xD8, 4, (60).to_bytes(4, "big", signed=True)),
            (71, 0xDC, 2, _u16(1)),
            (72, 0xDE, 2, _u16(5)),
            (73, 0xE0, 2, _u16(0)),
            (74, 0xE2, 2, _u16(0)),
            (75, 0xE4, 4, _u32(roots[5]))):
        assert len(value) == size
        slices.append(_slice(tag, 0, map2 + offset, value))

    display = grounds[1]
    for tag, offset, size, value in (
            (76, 0xE0, 4, (12).to_bytes(4, "big", signed=True)),
            (77, 0xE4, 2, _u16(2)),
            (78, 0xE6, 2, _u16(1)),
            (79, 0xE8, 2, _u16(0xFFFF)),
            (80, 0xEA, 2, _u16(2)),
            (81, 0xEE, 2, _u16(0)),
            (82, 0xF2, 2, _u16(0)),
            (83, 0xF8, 1, b"\xA0")):
        assert len(value) == size
        slices.append(_slice(tag, 0, display + offset, value))

    title_child = 0x80413000
    wrapper = 0x80414000
    slices.extend((
        _slice(84, 0, 0x8049F040, bytes(range(256))),
        _slice(85, 0, 0x804D1128, _u32(0x80412000)),
        _slice(86, 0, display + 0xD4, _u32(title_child)),
        _slice(87, 0, title_child + 0x2C, _u32(wrapper)),
        _slice(88, 0, wrapper + 0x18, _u32(0x80415000)),
        _slice(89, 0, wrapper + 0x1C, _u32(0x80416000)),
    ))
    tick = 84 if label in ("GO", "C") else 85
    payload = {"slices": slices, "source_tick": tick, "draw_ordinal": 0}
    if label == "GO":
        payload.update({"phase": "go_after", "pc": f"0x{GO_AFTER_PC:08x}",
                        "word": "0x881f24c9",
                        "state_snapshot_schema": STADIUM_IDLE_STATE_SCHEMA,
                        "sis_table_entry_relationship_observed": False,
                        "dynamic_title_text_bytes_observed": False})
        return {"event": "progress", "seq": 1, "source_tick": tick,
                "draw_ordinal": 0, "payload": payload}
    payload.update({"boundary": "source_tick", "pc": SOURCE_TICK_PC})
    return {"event": "boundary", "seq": 2 if label == "C" else 3,
            "source_tick": tick, "draw_ordinal": 0, "payload": payload}


def _stateful_route_fixture(scratch: Path, mutate=None) -> tuple[bytes, dict]:
    data, status = route_fixtures._fixture(draw_between_c_f=True)
    source = _new_scratch_dir(scratch, "fixture-source-") / "route.mwro"
    source.write_bytes(data)
    rows = list(observer_stream.iter_records(source))

    records = []
    labels = set()
    go_seen = False
    offset = 0
    for row in rows:
        header = observer_stream.HEADER.unpack_from(data, offset)
        record_end = offset + observer_stream.HEADER.size + header[8]
        raw_record = data[offset:record_end]
        offset = record_end
        payload = row.get("payload", {})
        label = None
        if row.get("event") == "progress" and payload.get("phase") == "go_after":
            label = "GO"
            go_seen = True
        elif go_seen and row.get("event") == "boundary" and \
                payload.get("boundary") == "source_tick":
            if payload.get("source_tick") == 125 and "C" not in labels:
                label = "C"
            elif payload.get("source_tick") == 126 and "F" not in labels:
                label = "F"
        if label is None:
            records.append(raw_record)
            continue

        payload = copy.deepcopy(payload)
        payload["slices"].extend(copy.deepcopy(_record(label)["payload"]["slices"]))
        if label == "GO":
            payload.update({
                "state_snapshot_schema": STADIUM_IDLE_STATE_SCHEMA,
                "sis_table_entry_relationship_observed": False,
                "dynamic_title_text_bytes_observed": False,
            })
        if mutate is not None:
            mutate(label, payload)
        labels.add(label)
        if row["event"] == "progress":
            records.append(route_fixtures._frame(
                4, row["seq"], json.dumps(payload, separators=(",", ":")).encode(),
                pc=int(payload["pc"], 16), source_tick=payload["source_tick"]))
        else:
            pc = payload["pc"]
            pc_value = int(pc, 16) if isinstance(pc, str) else pc
            records.append(route_fixtures._boundary(
                payload["boundary_kind"], row["seq"], pc=pc_value,
                source_tick=payload["source_tick"], slices=payload["slices"]))
    if offset != len(data) or labels != {"GO", "C", "F"}:
        raise AssertionError("route fixture did not retain exact GO/C/F records")
    return b"".join(records), status


def _validate_temp(scratch: Path, data: bytes, status: dict, validator):
    stream = _new_scratch_dir(scratch, "validation-") / "candidate.mwro"
    status_path = Path(str(stream) + ".status.json")
    stream.write_bytes(data)
    status_path.write_text(json.dumps(status), encoding="utf-8")
    return validator(stream, status_path=status_path)


class StadiumIdleStateContractTests(unittest.TestCase):
    def test_exact_go_record_is_complete_and_scoped(self):
        decoded = _validate_stadium_idle_state_record(_record("GO"), "GO", 84)
        self.assertEqual(decoded["map2"]["form_s16"], 5)
        self.assertTrue(decoded["map2"]["map5_root_pointer_equals_fixed_map5_slot_value"])
        self.assertFalse(decoded["map_root_object_type_observed"])
        self.assertFalse(decoded["map_root_allocation_lifetime_proved"])
        self.assertFalse(decoded["display"]["xF0_observed"])
        self.assertFalse(decoded["title"]["sis_table_entry_relationship_observed"])
        self.assertFalse(decoded["title"]["dynamic_title_text_bytes_observed"])

    def test_exact_c_and_f_contexts_are_accepted(self):
        self.assertEqual(
            _validate_stadium_idle_state_record(_record("C"), "C", 84)["source_tick"], 84)
        self.assertEqual(
            _validate_stadium_idle_state_record(_record("F"), "F", 85)["source_tick"], 85)

    def test_missing_typed_field_refuses_without_partial_result(self):
        row = _record("GO")
        row["payload"]["slices"] = [item for item in row["payload"]["slices"]
                                      if item["tag"] != 70]
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(row, "GO", 84)

    def test_truncated_fixed_title_scratch_refuses(self):
        row = _record("C")
        item = next(item for item in row["payload"]["slices"] if item["tag"] == 84)
        item["size"] = 255
        item["hex"] = item["hex"][:-2]
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(row, "C", 84)

    def test_guest_pointer_outside_ram_refuses(self):
        row = _record("F")
        item = next(item for item in row["payload"]["slices"]
                    if item["tag"] == 67 and item["flags"] == 2)
        for address in (0x00001000, 0x90001000):
            with self.subTest(address=hex(address)):
                item["hex"] = _u32(address).hex()
                with self.assertRaises(StadiumGoPrefixError):
                    _validate_stadium_idle_state_record(row, "F", 85)

    def test_state_member_slice_in_mem2_refuses(self):
        row = _record("C")
        item = next(item for item in row["payload"]["slices"] if item["tag"] == 70)
        item["address"] = 0x90001000
        with self.assertRaisesRegex(StadiumGoPrefixError, "escaped checked source MEM1"):
            _validate_stadium_idle_state_record(row, "C", 84)

    def test_map5_ownership_mismatch_refuses(self):
        row = _record("GO")
        item = next(item for item in row["payload"]["slices"] if item["tag"] == 75)
        item["hex"] = _u32(0x80410700).hex()
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(row, "GO", 84)

    def test_duplicate_role_or_wrong_tick_context_refuses(self):
        row = _record("C")
        row["payload"]["slices"].append(copy.deepcopy(
            next(item for item in row["payload"]["slices"]
                 if item["tag"] == 67 and item["flags"] == 0)))
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(row, "C", 84)
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(_record("F"), "F", 84)

    def test_non_integer_role_flags_refuse(self):
        row = _record("GO")
        root = next(item for item in row["payload"]["slices"]
                    if item["tag"] == 67 and item["flags"] == 1)
        root["flags"] = True
        with self.assertRaises(StadiumGoPrefixError):
            _validate_stadium_idle_state_record(row, "GO", 84)

    def test_null_title_root_is_explicitly_unobserved(self):
        row = _record("GO")
        slices = row["payload"]["slices"]
        child = next(item for item in slices if item["tag"] == 86)
        child["hex"] = _u32(0).hex()
        row["payload"]["slices"] = [item for item in slices if item["tag"] not in (87, 88, 89)]
        decoded = _validate_stadium_idle_state_record(row, "GO", 84)
        self.assertFalse(decoded["title"]["title_child_present"])
        self.assertFalse(decoded["title"]["static_title_text_bytes_observed"])


class StadiumIdleStateCompanionTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "stadium-idle-state-contract-")

    def test_full_route_fixture_passes_with_new_go_c_f_state(self):
        data, status = _stateful_route_fixture(self.workspace)
        result = _validate_temp(self.workspace, data, status, validate_stadium_idle_state_snapshot)
        self.assertEqual(result["decision"], "PASS_ORIGINAL_STATE_ONLY_COUNTERPART")
        self.assertEqual([item["label"] for item in result["snapshots"]], ["GO", "C", "F"])
        self.assertEqual([item["source_tick"] for item in result["snapshots"]], [125, 125, 126])

    def test_old_state_less_route_passes_baseline_but_refuses_companion(self):
        data, status = route_fixtures._fixture()
        baseline = _validate_temp(self.workspace, data, status, validate_stadium_go_prefix)
        self.assertEqual(baseline["decision"], "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY")
        with self.assertRaisesRegex(StadiumGoPrefixError,
                                    "typed Stadium state inventory is incomplete"):
            _validate_temp(self.workspace, data, status, validate_stadium_idle_state_snapshot)

    def test_wrong_go_phase_or_source_tick_pc_refuses_full_companion(self):
        def wrong_go_phase(label, payload):
            if label == "GO":
                payload["phase"] = "go_before"

        def wrong_c_pc(label, payload):
            if label == "C":
                payload["pc"] = "0x80390eb8"

        for mutate in (wrong_go_phase, wrong_c_pc):
            data, status = _stateful_route_fixture(self.workspace, mutate)
            with self.subTest(mutate=mutate.__name__), self.assertRaises(
                    StadiumGoPrefixError):
                _validate_temp(self.workspace, data, status, validate_stadium_idle_state_snapshot)

    def test_malformed_typed_inventory_fails_late_without_partial_result(self):
        def omit_c_countdown(label, payload):
            if label == "C":
                payload["slices"] = [item for item in payload["slices"]
                                     if item["tag"] != 70]

        data, status = _stateful_route_fixture(self.workspace, omit_c_countdown)
        result = None
        with self.assertRaisesRegex(StadiumGoPrefixError, "inventory is incomplete"):
            result = _validate_temp(self.workspace, data, status, validate_stadium_idle_state_snapshot)
        self.assertIsNone(result)


if __name__ == "__main__":
    unittest.main()
