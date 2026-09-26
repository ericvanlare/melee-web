"""Strict, bounded allocation-history comparison tests."""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import tempfile
from contextlib import redirect_stdout
from types import SimpleNamespace
import unittest

from tools import allocation_trace_compare as compare


OWNER_EVIDENCE = "source maps both selected heaps to the same owner"
WORLD_EVIDENCE = "independent source world map"
BOUNDARY_EVIDENCE = "same authored boundary"
ADDRESS_EVIDENCE = "same source address domain"


def proof(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def event(index: int, operation: str = "allocate", *, size: int = 20,
          alignment: int = 32, owner: str = "main_heap", owner_proof: str | None = None,
          world: str = "match", world_proof: str | None = None,
          boundary: str = "vs_entry", boundary_proof: str | None = None,
          local_id: str | None = None, address: str | None = None,
          domain: str = "mem1") -> dict:
    value = {
        "record": "event", "sequence": index,
        "world": {"id": world, "proof_id": world_proof or proof(WORLD_EVIDENCE)},
        "boundary": {"id": boundary, "proof_id": boundary_proof or proof(BOUNDARY_EVIDENCE)},
        "operation": operation,
        "owner": {"id": owner, "proof_id": owner_proof or proof(OWNER_EVIDENCE)},
        "allocation": {"local_id": local_id or f"local-{index}"},
        "source_address": address or f"0x{0x80010000 + index * 64:08x}",
    }
    if operation == "allocate":
        value["request"] = {"size": size, "alignment": alignment}
    return value


def stream(events: list[dict], *, complete: bool = True,
           initial: bool = True, address_domain: dict | None = None) -> compare.ReadResult:
    return compare.ReadResult(
        format="fixture",
        metadata={"header_address_domain": address_domain},
        events=events,
        complete=complete,
        initial_events_present=initial,
    )


def run(left: list[dict], right: list[dict], *, left_complete: bool = True,
        right_complete: bool = True, initial: bool = True,
        left_domain: dict | None = None, right_domain: dict | None = None):
    return compare.compare_streams(
        stream(left, complete=left_complete, initial=initial, address_domain=left_domain),
        stream(right, complete=right_complete, initial=initial, address_domain=right_domain),
        window=2,
        owner_identity="main_heap", owner_evidence=OWNER_EVIDENCE,
        world_identity="match", world_evidence=WORLD_EVIDENCE,
        boundary_identity="vs_entry", boundary_evidence=BOUNDARY_EVIDENCE,
    )


class AllocationTraceCompareTests(unittest.TestCase):
    def test_matching_sequences(self):
        left = [event(0, local_id="a", address="0x80011000"),
                event(1, local_id="b", address="0x80011040")]
        right = [event(0, local_id="x", address="0x80011000"),
                 event(1, local_id="y", address="0x80011040")]
        domain = {"id": "mem1", "proof_id": proof(ADDRESS_EVIDENCE)}
        result, _ = run(left, right, left_domain=domain, right_domain=domain)
        self.assertEqual(result["status"], "matching_comparable_prefix")
        self.assertEqual(result["matched_prefix_length"], 2)

    def test_different_request_size_and_alignment(self):
        for field, value in (("size", 21), ("alignment", 16)):
            left_event = event(0)
            right_event = event(0)
            right_event["request"][field] = value
            result, _ = run([left_event], [right_event])
            self.assertEqual(result["status"], "first_differing_comparable_event")
            self.assertEqual(result["first_difference"]["differences"][0]["field"], f"request.{field}")
            self.assertEqual(result["matched_prefix_length"], 0)

    def test_supported_pool_operations_require_request_and_lifetime_fields(self):
        left_allocate = event(0, "pool_allocate", local_id="left-pool-object")
        right_allocate = event(0, "pool_allocate", local_id="right-pool-object")
        for value in (left_allocate, right_allocate):
            value["request"] = {"size": 24, "alignment": 8}
        left_free = event(1, "pool_free", local_id="left-pool-object")
        right_free = event(1, "pool_free", local_id="right-pool-object")
        matched, _ = run([left_allocate, left_free], [right_allocate, right_free])
        self.assertEqual(matched["status"], "matching_comparable_prefix")
        self.assertEqual(matched["matched_prefix_length"], 2)

        missing_alignment = event(0)
        missing_alignment["request"].pop("alignment")
        invalid_alignment = event(0)
        invalid_alignment["request"]["alignment"] = 3
        for browser_event in (missing_alignment, invalid_alignment):
            result, _ = run([event(0)], [browser_event])
            self.assertEqual(result["status"], "incompatible_or_insufficient_evidence_for_alignment")
            self.assertEqual(result["first_unsupported"]["field"], "event_semantics")

    def test_unknown_or_incompletely_defined_operations_never_match(self):
        for operation, expected in (("mystery", "no defined comparison semantics"),
                                    ("pool_init", "no comparable pool descriptor semantics")):
            result, _ = run([event(0, operation)], [event(0, operation)])
            self.assertEqual(result["status"], "incompatible_or_insufficient_evidence_for_alignment")
            self.assertEqual(result["matched_prefix_length"], 0)
            self.assertEqual(result["first_unsupported"]["field"], "operation")
            self.assertIn(expected, result["first_unsupported"]["reason"])

    def test_unlike_operations_are_reported_before_lifecycle_checks(self):
        left = event(0, "allocate")
        right = event(0, "pool_init")
        right.pop("allocation")
        reverse_left = event(0, "pool_init")
        reverse_left.pop("allocation")
        reverse_right = event(0, "allocate")
        for original, browser in ((left, right), (reverse_left, reverse_right)):
            result, _ = run([original], [browser])
            self.assertEqual(result["status"], "first_differing_comparable_event")
            self.assertEqual(result["first_difference"]["differences"], [
                {"field": "operation", "original": original["operation"],
                 "browser": browser["operation"]}
            ])

        allocation_then_free = [event(0, local_id="old"),
                                event(1, "free", local_id="old")]
        allocation_then_allocation = [event(0, local_id="first"),
                                      event(1, "allocate", local_id="second")]
        free_vs_allocate, _ = run(allocation_then_free, allocation_then_allocation)
        allocate_vs_free, _ = run(allocation_then_allocation, allocation_then_free)
        for result in (free_vs_allocate, allocate_vs_free):
            self.assertEqual(result["status"], "first_differing_comparable_event")
            self.assertEqual(result["matched_prefix_length"], 1)
            self.assertEqual(result["first_difference"]["differences"][0]["field"], "operation")

    def test_extra_operation_does_not_shift_the_strict_difference(self):
        left = [event(0, local_id="a"), event(1, "pool_init"), event(2, "free", local_id="a")]
        right = [event(0, local_id="x"), event(1, "free", local_id="x")]
        result, _ = run(left, right)
        self.assertEqual(result["status"], "first_differing_comparable_event")
        self.assertEqual(result["matched_prefix_length"], 1)
        self.assertEqual(result["first_difference"]["event_index"], 1)
        self.assertEqual(result["first_difference"]["differences"][0]["field"], "operation")

    def test_owner_difference_is_reported_when_both_mappings_are_known(self):
        right = event(0, owner="object_pool")
        result, _ = run([event(0)], [right])
        self.assertEqual(result["status"], "first_differing_comparable_event")
        self.assertIn("owner.identity", [row["field"] for row in result["first_difference"]["differences"]])

    def test_unknown_owner_mapping_stops_comparison(self):
        right = event(0, owner_proof="unmapped")
        result, _ = run([event(0)], [right])
        self.assertEqual(result["status"], "incompatible_or_insufficient_evidence_for_alignment")
        self.assertEqual(result["matched_prefix_length"], 0)

    def test_free_reallocation_can_reuse_same_address(self):
        left = [event(0, local_id="old", address="0x80012000"),
                event(1, "free", local_id="old", address="0x80012000"),
                event(2, local_id="new", address="0x80012000"),
                event(3, "free", local_id="new", address="0x80012000")]
        right = [event(0, local_id="first", address="0x80012000"),
                 event(1, "free", local_id="first", address="0x80012000"),
                 event(2, local_id="second", address="0x80012000"),
                 event(3, "free", local_id="second", address="0x80012000")]
        domain = {"id": "mem1", "proof_id": proof(ADDRESS_EVIDENCE)}
        result, _ = run(left, right, left_domain=domain, right_domain=domain)
        self.assertEqual(result["status"], "matching_comparable_prefix")
        self.assertEqual(result["matched_prefix_length"], 4)

    def test_separate_world_generations_require_selection_or_mapping(self):
        right = event(0, world="match-generation-2")
        result, _ = run([event(0)], [right])
        self.assertEqual(result["status"], "incompatible_or_insufficient_evidence_for_alignment")
        self.assertEqual(result["first_unsupported"]["field"], "world")

    def test_missing_initial_events_is_truncation_not_a_match(self):
        result, _ = run([event(0)], [event(0)], initial=False)
        self.assertEqual(result["status"], "trace_exhaustion_or_truncation")
        self.assertEqual(result["matched_prefix_length"], 0)

    def test_wrapped_browser_ring_reports_truncation_and_raw_array_location(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "browser.json"
            raw_events = [
                {"sequence": 2, "operation": 1, "heap": 0, "host": 123,
                 "source": 0x80010040, "requested": 20, "generation": 3},
                {"sequence": 1, "operation": 1, "heap": 0, "host": 456,
                 "source": 0x80010000, "requested": 16, "generation": 3},
            ]
            path.write_text(json.dumps({"final_snapshot": {
                "source_allocation_trace": raw_events,
                "source_allocation_trace_total": 3,
            }}))
            args = SimpleNamespace(browser_trace=path, browser_generation=3,
                                   browser_heap=0, owner_identity="main_heap",
                                   world_identity="match", boundary_identity="vs_entry")
            parsed = compare._browser_report_events(
                args, owner_proof=proof(OWNER_EVIDENCE),
                world_proof=proof(WORLD_EVIDENCE),
                boundary_proof=proof(BOUNDARY_EVIDENCE))
            self.assertFalse(parsed.complete)
            self.assertFalse(parsed.initial_events_present)
            self.assertEqual(parsed.truncation["kind"], "ring_buffer_or_truncated")
            rows = list(parsed.events)
            self.assertEqual(rows[0]["source_location"]["sequence"], 1)
            self.assertTrue(rows[0]["source_location"]["path"].endswith("/1"))

    def test_output_must_be_outside_retained_input_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            capture = Path(tmp) / "capture"
            capture.mkdir()
            original = capture / "original.jsonl"
            browser = capture / "browser.json"
            original.touch()
            browser.touch()
            with self.assertRaisesRegex(compare.CompareProblem, "outside the retained input directory"):
                compare._validate_output_location(capture / "reports" / "run", (original, browser))
            compare._validate_output_location(Path(tmp) / "reports" / "run", (original, browser))

    def test_incompatible_address_domains_do_not_compare_pointer_values(self):
        left_domain = {"id": "mem1", "proof_id": "original-proof"}
        right_domain = {"id": "wasm-offset", "proof_id": "browser-proof"}
        result, _ = run([event(0, address="0x80010000")],
                        [event(0, address="0x00010000")],
                        left_domain=left_domain, right_domain=right_domain)
        self.assertEqual(result["status"], "matching_comparable_prefix")
        self.assertTrue(any("address domains" in note for note in result["comparison_notes"]))

    def test_one_sided_exhaustion_is_reported_separately(self):
        left = [event(0, local_id="a")]
        right = [event(0, local_id="b"), event(1, "free", local_id="b")]
        result, window = run(left, right)
        self.assertEqual(result["status"], "trace_exhaustion_or_truncation")
        self.assertEqual(result["matched_prefix_length"], 1)
        self.assertEqual(window[0]["side"], "browser")

    def _write_normalized(self, path: Path, events: list[dict], *, tail: str = "\n"):
        header = {"record": "header", "schema": compare.NORMALIZED_SCHEMA,
                  "version": compare.NORMALIZED_VERSION,
                  "coverage": {"kind": "complete", "initial_events_present": True},
                  "address_domain": None}
        rows = [header, *events]
        path.write_text("".join(json.dumps(row) + "\n" for row in rows) +
                        json.dumps({"record": "end", "complete": True,
                                    "status": "complete", "event_count": len(events)}) + tail)

    def _run_normalized_cli(self, root: Path, original_events: list[dict],
                            browser_events: list[dict]):
        original_dir = root / "original-input"
        browser_dir = root / "browser-input"
        original_dir.mkdir()
        browser_dir.mkdir()
        original = original_dir / "trace.jsonl"
        browser = browser_dir / "trace.jsonl"
        output = root / "report-output"
        self._write_normalized(original, original_events)
        self._write_normalized(browser, browser_events)
        args = [
            "--original-trace", str(original), "--browser-trace", str(browser),
            "--out", str(output), "--owner-identity", "main_heap",
            "--owner-evidence", OWNER_EVIDENCE, "--world-identity", "match",
            "--world-evidence", WORLD_EVIDENCE, "--boundary-identity", "vs_entry",
            "--boundary-evidence", BOUNDARY_EVIDENCE,
        ]
        with redirect_stdout(io.StringIO()):
            code = compare.main(args)
        return code, json.loads((output / "report.json").read_text())

    def test_normalized_cli_rejects_identical_unknown_operations(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, report = self._run_normalized_cli(
                Path(tmp), [event(0, "mystery")], [event(0, "mystery")])
            self.assertEqual(code, 1)
            self.assertEqual(report["status"], "incompatible_or_insufficient_evidence_for_alignment")
            self.assertEqual(report["first_unsupported"]["field"], "operation")

    def test_normalized_cli_preserves_unlike_operation_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            original = event(0, "allocate")
            browser = event(0, "pool_init")
            browser.pop("allocation")
            code, report = self._run_normalized_cli(Path(tmp), [original], [browser])
            self.assertEqual(code, 1)
            self.assertEqual(report["status"], "first_differing_comparable_event")
            self.assertEqual(report["first_difference"]["differences"][0]["field"], "operation")

    def test_incomplete_final_record_is_safe_truncation(self):
        with tempfile.TemporaryDirectory() as tmp:
            original = Path(tmp) / "original.jsonl"
            browser = Path(tmp) / "browser.jsonl"
            self._write_normalized(original, [event(0)])
            self._write_normalized(browser, [event(0)], tail="\n{\"record\":\"event\"")
            l = compare._parse_normalized_trace(original, "original", None, None)
            r = compare._parse_normalized_trace(browser, "browser", None, None)
            result, _ = run(list(l.events), list(r.events),
                            left_complete=l.complete, right_complete=r.complete)
            self.assertEqual(result["status"], "trace_exhaustion_or_truncation")
            self.assertEqual(result["matched_prefix_length"], 1)

    def test_malformed_interior_record_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.jsonl"
            header = {"record": "header", "schema": compare.NORMALIZED_SCHEMA,
                      "version": 1, "coverage": {"initial_events_present": True}}
            path.write_text(json.dumps(header) + "\n{bad json}\n" + json.dumps(event(0)) + "\n")
            with self.assertRaises(compare.CompareProblem):
                compare._parse_normalized_trace(path, "original", None, None)

    def test_normalized_event_requires_nonempty_operation_label(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "missing-operation.jsonl"
            value = event(0)
            value.pop("operation")
            self._write_normalized(path, [value])
            with self.assertRaisesRegex(compare.CompareProblem, "operation must be a nonempty string"):
                compare._parse_normalized_trace(path, "original", None, None)

    def test_multiple_worlds_need_explicit_selector(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "worlds.jsonl"
            self._write_normalized(path, [event(0, world="gen-1"), event(1, world="gen-2")])
            with self.assertRaises(compare.CompareProblem):
                compare._parse_normalized_trace(path, "original", None, None)
            selected = compare._parse_normalized_trace(path, "original", "gen-2", None)
            self.assertEqual(selected.metadata["selected_event_count"], 1)


if __name__ == "__main__":
    unittest.main()
