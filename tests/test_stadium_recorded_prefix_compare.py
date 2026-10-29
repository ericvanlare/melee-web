from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import unittest
import zlib


def _source_root() -> Path:
    candidates = (Path.cwd(), *Path(__file__).resolve().parents)
    for candidate in candidates:
        if (candidate / "tools" / "stadium_go_prefix.py").is_file():
            return candidate.resolve()
    raise RuntimeError("run these tests from a checkout containing tools/stadium_go_prefix.py")


ROOT = _source_root()
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"),
                str(ROOT / "tests")]
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import reference_observer_stream as observer_stream  # noqa: E402
import test_stadium_go_prefix as route_fixtures  # noqa: E402
import test_stadium_idle_state_contract as state_fixtures  # noqa: E402
from owned_test_workspace import OwnedWorkspaceTests  # noqa: E402
from stadium_recorded_prefix_compare import (  # noqa: E402
    StadiumCommonWireMismatch,
    compare_stadium_common_wire,
)


EVIDENCE_ROOT_ENV = "MELEE_WEB_STADIUM_COMMON_WIRE_EVIDENCE_ROOT"
_ACTUAL_BASELINE = "original-stadium-go-prefix-7d406f63-feedback-v6"
_ACTUAL_CANDIDATE = "original-stadium-idle-state-59f843fd-ddb40cf-v1"


def _write_case(directory: Path, name: str, data: bytes,
                status: dict) -> tuple[Path, Path]:
    directory.mkdir(parents=True, exist_ok=True)
    stream = directory / f"{name}.mwro"
    status_path = directory / f"{name}.status.json"
    stream.write_bytes(data)
    status_path.write_text(json.dumps(status), encoding="utf-8")
    return stream, status_path


def _retime(data: bytes, offset: int) -> bytes:
    records = []
    cursor = 0
    while cursor < len(data):
        header_values = list(observer_stream.HEADER.unpack_from(data, cursor))
        end = cursor + observer_stream.HEADER.size + header_values[8]
        header_values[4] += offset
        records.append(observer_stream.HEADER.pack(*header_values) +
                       data[cursor + observer_stream.HEADER.size:end])
        cursor = end
    if cursor != len(data):
        raise AssertionError("synthetic stream did not end at a record boundary")
    return b"".join(records)


def _change_header(data: bytes, sequence: int, field_index: int, value: int) -> bytes:
    output = bytearray(data)
    cursor = 0
    while cursor < len(output):
        fields = list(observer_stream.HEADER.unpack_from(output, cursor))
        end = cursor + observer_stream.HEADER.size + fields[8]
        if fields[3] == sequence:
            fields[field_index] = value
            output[cursor:cursor + observer_stream.HEADER.size] = observer_stream.HEADER.pack(*fields)
            return bytes(output)
        cursor = end
    raise AssertionError(f"synthetic sequence {sequence} was not found")


def _rewrite_json_payload(data: bytes, sequence: int, edit) -> bytes:
    records = []
    cursor = 0
    while cursor < len(data):
        fields = list(observer_stream.HEADER.unpack_from(data, cursor))
        payload_start = cursor + observer_stream.HEADER.size
        payload_end = payload_start + fields[8]
        payload = data[payload_start:payload_end]
        if fields[3] == sequence:
            payload_value = json.loads(payload.decode("utf-8"))
            edit(payload_value)
            payload = json.dumps(payload_value, separators=(",", ":")).encode("utf-8")
            fields[8] = len(payload)
            fields[9] = zlib.crc32(payload) & 0xFFFFFFFF
        records.append(observer_stream.HEADER.pack(*fields) + payload)
        cursor = payload_end
    if cursor != len(data):
        raise AssertionError("synthetic stream did not end at a record boundary")
    return b"".join(records)


def _corrupt_first_payload_crc(data: bytes) -> bytes:
    corrupted = bytearray(data)
    offset = observer_stream.HEADER.size
    corrupted[offset] ^= 1
    return bytes(corrupted)


class StadiumRecordedPrefixCompareTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "stadium-recorded-prefix-compare-")
        cls.case_number = 0

    def _next_case(self) -> Path:
        directory = self.workspace / f"case-{type(self).case_number:03d}"
        type(self).case_number += 1
        directory.mkdir()
        return directory

    def _synthetic_pair(self, directory: Path, candidate_mutate=None):
        original_data, original_status = route_fixtures._fixture(draw_between_c_f=True)
        candidate_data, candidate_status = state_fixtures._stateful_route_fixture(
            directory, candidate_mutate)
        original = _write_case(directory / "original", "observer",
                               original_data, original_status)
        candidate = _write_case(directory / "candidate", "observer",
                                candidate_data, candidate_status)
        return original, candidate, candidate_data

    def _compare(self, original, candidate):
        return compare_stadium_common_wire(
            original[0], candidate[0], original_status_path=original[1],
            candidate_status_path=candidate[1])

    def test_exact_old_fields_pass_after_only_timestamp_and_state_additions(self):
        directory = self._next_case()
        original_data, original_status = route_fixtures._fixture(draw_between_c_f=True)
        candidate_data, candidate_status = state_fixtures._stateful_route_fixture(directory)
        candidate_data = _retime(candidate_data, 5_000_000)
        original = _write_case(directory / "original", "observer",
                               original_data, original_status)
        candidate = _write_case(directory / "candidate", "observer",
                                candidate_data, candidate_status)

        receipt = self._compare(original, candidate)

        self.assertEqual(receipt["decision"], "PASS_EXACT_STADIUM_COMMON_WIRE")
        self.assertEqual(receipt["comparison"]["records_compared"],
                         original_status["event_count"])
        self.assertEqual(receipt["candidate_state_companion"]["decision"],
                         "PASS_ORIGINAL_STATE_ONLY_COUNTERPART")
        self.assertEqual(receipt["comparison"]["candidate_state_fields_removed_after_validation"]
                         ["typed_slice_tags"], list(range(67, 90)))

    def test_json_record_raw_header_pc_divergence_is_not_lost_by_decoder(self):
        directory = self._next_case()
        original, candidate, candidate_data = self._synthetic_pair(directory)
        changed = _change_header(candidate_data, sequence=2, field_index=5,
                                 value=0x81234567)
        candidate = _write_case(directory / "candidate-header-pc", "observer",
                                changed, json.loads(candidate[1].read_text()))

        with self.assertRaises(StadiumCommonWireMismatch) as raised:
            self._compare(original, candidate)

        self.assertEqual(raised.exception.receipt["first_difference"]["sequence"], 2)
        self.assertEqual(raised.exception.receipt["first_difference"]["path"],
                         "wire.header.pc")

    def test_common_source_slice_bytes_and_order_are_exact(self):
        directory = self._next_case()

        def change_old_byte(label, payload):
            if label == "C":
                item = next(item for item in payload["slices"] if item["tag"] == 2)
                raw = bytearray.fromhex(item["hex"])
                raw[-1] ^= 1
                item["hex"] = raw.hex()

        original, candidate, _ = self._synthetic_pair(directory, change_old_byte)
        with self.assertRaises(StadiumCommonWireMismatch) as changed_byte:
            self._compare(original, candidate)
        self.assertEqual(changed_byte.exception.receipt["first_difference"]["sequence"], 18)
        self.assertIn("payload.slices", changed_byte.exception.receipt["first_difference"]["path"])

        directory = self._next_case()

        def reorder_old_slices(label, payload):
            if label == "GO":
                first = next(index for index, item in enumerate(payload["slices"])
                             if item["tag"] == 2)
                second = next(index for index, item in enumerate(payload["slices"])
                              if item["tag"] == 17)
                payload["slices"][first], payload["slices"][second] = (
                    payload["slices"][second], payload["slices"][first])

        original, candidate, _ = self._synthetic_pair(directory, reorder_old_slices)
        with self.assertRaises(StadiumCommonWireMismatch) as changed_order:
            self._compare(original, candidate)
        self.assertEqual(changed_order.exception.receipt["first_difference"]["sequence"], 17)
        self.assertIn("payload.slices", changed_order.exception.receipt["first_difference"]["path"])

    def test_unapproved_payload_key_is_not_masked(self):
        directory = self._next_case()
        original, candidate, candidate_data = self._synthetic_pair(directory)
        changed = _rewrite_json_payload(
            candidate_data, sequence=0,
            edit=lambda payload: payload.update({"unapproved_candidate_field": True}))
        candidate = _write_case(directory / "candidate-extra-key", "observer",
                                changed, json.loads(candidate[1].read_text()))

        with self.assertRaises(StadiumCommonWireMismatch) as raised:
            self._compare(original, candidate)

        self.assertEqual(raised.exception.receipt["first_difference"]["sequence"], 0)
        self.assertEqual(raised.exception.receipt["first_difference"]["path"],
                         "payload.unapproved_candidate_field")

    def test_state_metadata_masks_only_go_and_state_tags_only_go_c_f(self):
        directory = self._next_case()
        original, candidate, candidate_data = self._synthetic_pair(directory)
        changed = _rewrite_json_payload(
            candidate_data, sequence=12,
            edit=lambda payload: payload.update({
                "sis_table_entry_relationship_observed": False,
            }))
        candidate = _write_case(directory / "candidate-metadata-outside-go", "observer",
                                changed, json.loads(candidate[1].read_text()))
        with self.assertRaises(StadiumCommonWireMismatch) as metadata_outside_go:
            self._compare(original, candidate)
        self.assertEqual(metadata_outside_go.exception.receipt["first_difference"]["sequence"],
                         12)
        self.assertEqual(metadata_outside_go.exception.receipt["first_difference"]["path"],
                         "payload.sis_table_entry_relationship_observed")

        directory = self._next_case()
        original, candidate, candidate_data = self._synthetic_pair(directory)
        changed = _rewrite_json_payload(
            candidate_data, sequence=17,
            edit=lambda payload: payload.update({"unapproved_go_field": 1}))
        candidate = _write_case(directory / "candidate-extra-go-key", "observer",
                                changed, json.loads(candidate[1].read_text()))
        with self.assertRaises(StadiumCommonWireMismatch) as extra_go_key:
            self._compare(original, candidate)
        self.assertEqual(extra_go_key.exception.receipt["first_difference"]["sequence"], 17)
        self.assertEqual(extra_go_key.exception.receipt["first_difference"]["path"],
                         "payload.unapproved_go_field")

        directory = self._next_case()
        original_data, original_status = route_fixtures._fixture(draw_between_c_f=True)
        candidate_data, candidate_status = state_fixtures._stateful_route_fixture(directory)

        def add_original_state_tag_outside_go_c_f(payload):
            payload["slices"].append({
                "tag": 67, "flags": 0, "address": 0x8049E848,
                "size": 4, "hex": "80410000",
            })

        original_data = _rewrite_json_payload(
            original_data, sequence=12, edit=add_original_state_tag_outside_go_c_f)
        original = _write_case(directory / "original-extra-state-tag", "observer",
                               original_data, original_status)
        candidate = _write_case(directory / "candidate", "observer",
                                candidate_data, candidate_status)
        with self.assertRaises(StadiumCommonWireMismatch) as tag_outside_scope:
            self._compare(original, candidate)
        self.assertEqual(tag_outside_scope.exception.receipt["first_difference"]["sequence"],
                         12)
        self.assertTrue(tag_outside_scope.exception.receipt["first_difference"]["path"]
                        .startswith("payload.slices"))

        directory = self._next_case()
        original, candidate, candidate_data = self._synthetic_pair(directory)

        def add_state_outside_go(payload):
            payload["slices"].append({
                "name": "stadium_map_root", "tag": 67, "flags": 0,
                "address": 0x8049E848, "size": 4, "hex": "80410000",
            })

        changed = _rewrite_json_payload(candidate_data, sequence=12, edit=add_state_outside_go)
        candidate = _write_case(directory / "candidate-state-outside-go-c-f", "observer",
                                changed, json.loads(candidate[1].read_text()))
        with self.assertRaisesRegex(ValueError, "outside GO/C/F"):
            self._compare(original, candidate)

    def test_invalid_state_scope_and_bad_crc_refuse_before_common_comparison(self):
        directory = self._next_case()

        def add_unapproved_state(label, payload):
            if label == "C":
                payload["slices"].append({
                    "tag": 67, "flags": 3, "address": 0x8049E84C,
                    "size": 4, "hex": "80410500",
                })

        original, candidate, _ = self._synthetic_pair(directory, add_unapproved_state)
        with self.assertRaises(ValueError):
            self._compare(original, candidate)

        directory = self._next_case()
        original_data, original_status = route_fixtures._fixture(draw_between_c_f=True)
        candidate_data, candidate_status = state_fixtures._stateful_route_fixture(directory)
        candidate_data = _corrupt_first_payload_crc(candidate_data)
        original = _write_case(directory / "original", "observer",
                               original_data, original_status)
        candidate = _write_case(directory / "candidate", "observer",
                                candidate_data, candidate_status)
        with self.assertRaises(ValueError):
            self._compare(original, candidate)

    @unittest.skipUnless(os.environ.get(EVIDENCE_ROOT_ENV),
                         "set evidence-root env to run retained-v6/live-capture control")
    def test_retained_v6_and_input_differing_capture_refuse_at_first_source_difference(self):
        evidence_root = Path(os.environ[EVIDENCE_ROOT_ENV])
        original_dir = evidence_root / _ACTUAL_BASELINE
        candidate_dir = evidence_root / _ACTUAL_CANDIDATE
        with self.assertRaises(StadiumCommonWireMismatch) as raised:
            compare_stadium_common_wire(
                original_dir / "observer.bin",
                candidate_dir / "observer.bin",
                original_status_path=original_dir / "observer-status.json",
                candidate_status_path=candidate_dir / "observer-status.json",
            )
        receipt = raised.exception.receipt
        self.assertEqual(receipt["decision"], "REFUSE_STADIUM_COMMON_WIRE_MISMATCH")
        difference = receipt["first_difference"]
        self.assertEqual(difference["sequence"], 150)
        self.assertEqual(difference["path"], "payload.slices[0].hex")
        self.assertEqual(
            difference["expected"],
            "01000000000000000000009c00000000000000000000008400000000000000000000ff8400000000000000000000ff30",
        )
        self.assertEqual(
            difference["actual"],
            "00000000000000000000009c00000000000000000000008400000000000000000000ff8400000000000000000000ff30",
        )
        print("retained-v6 versus input-differing capture first difference: " +
              json.dumps(difference, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
