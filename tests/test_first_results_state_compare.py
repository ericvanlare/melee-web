"""Asset-free real comparison-path controls for first-match Results ownership.

The original binary decoder and historical provenance receipts have explicit
fixture adapters. SourceCollector, Comparator, observed joins, fresh reader
prefixes, Results receipt validation, export-tail validation and report all run.
"""
import copy
import contextlib
import shutil
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import test_next_stock_state_compare as next_stock
from test_next_stock_state_compare import fixtures, compare, write_identity, ObserverStreamStats


@contextlib.contextmanager
def owned_fixture():
    path = Path(tempfile.mkdtemp(prefix="first-results-control-"))
    try:
        yield path
    except BaseException:
        print(f"Retained failing synthetic fixture: {path}")
        raise
    else:
        shutil.rmtree(path)


class FirstResultsStateCompareTests(unittest.TestCase):
    def compare_fixture(self, directory, *, mutation=None, deadline=False, reader_cap=False):
        fixture, checkpoints, accepted, boundary, raw_records, ticks = next_stock.NextStockStateCompareTests().next_comparison_fixture(directory)
        packet, selected = fixture["packet"], fixture["selected"]
        fixture["recipe"].scope = compare.V10_FIRST_MATCH_RESULTS_SCOPE
        last_tick = max(ticks)
        last_row = ticks[last_tick]
        chain = []
        for name in ("vs_exit", "vs_exit_return", "vs_mode_exit", "results_enter"):
            sequence = len(fixture["rows"])
            row = fixtures._boundary(name, sequence, last_tick + 1)
            row["payload"]["pc"] = compare.semantics.WHOLE_OBSERVER_PCS[name]
            fixture["rows"].append(row)
            chain.append({"boundary": name, "source_sequence": sequence,
                          "source_tick": last_tick + 1, "draw_ordinal": sequence})
        source = packet["source"]
        # Reuse the existing fixture adapter's five-byte record envelopes;
        # JSON fixture serialization is not the original stream's byte budget.
        raw_records = [b"R" + row["seq"].to_bytes(4, "big") for row in fixture["rows"]]
        def compact_prefix(sequence):
            raw = b"".join(raw_records[:sequence + 1])
            return {"bytes_read": len(raw), "records_read": sequence + 1,
                    "last_source_sequence": sequence, "sha256": hashlib.sha256(raw).hexdigest()}
        for checkpoint in checkpoints:
            checkpoint["prefix"] = compact_prefix(checkpoint["tuple"]["source_sequence"])
        accepted["prefix"] = compact_prefix(accepted["boundary"]["event"]["target"]["source_tick_seq"])
        # The public path loads real synthetic files; only provenance/audit
        # receipts and the original stream adapter are supplied by the fixture.
        reference = b"".join(raw_records) + b"synthetic-unread-tail"
        selected["reference"].write_bytes(reference)
        source["trace"].update(bytes=len(reference), recorded_full_sha256=hashlib.sha256(reference).hexdigest())
        clock_audit = write_identity(directory / "clock2000-audit.json", {})
        source["match_clock_boundary"] = {"target_match_frame_at_least": 2000, **checkpoints[-1]["tuple"]}
        source["match_clock_boundary_audit"] = clock_audit
        for checkpoint in checkpoints:
            label = checkpoint["label"]
            checkpoint["audit"] = (source["positive_boundary_audit"] if label == "clock1" else
                                   source["clock60_boundary_audit"] if label == "clock60" else
                                   clock_audit if label == "target" else
                                   {"path": str(directory / f"{label}-audit.json"), "bytes": 10, "sha256": "a" * 64})
        source["ordered_clock_lineage"] = {
            "schema": compare.ORDERED_CLOCK_LINEAGE_SCHEMA, "checkpoints": checkpoints,
            "runner_packet": {"path": str(directory / "runner.json"), "bytes": 10, "sha256": "b" * 64},
            "supporting_expectations": {label: {"path": str(directory / f"{label}-expectations.json"),
                                               "bytes": 10, "sha256": "c" * 64}
                                       for label in ("clock300", "clock500", "clock1000")}}
        context = {"clock2000_expectations": {"path": str(directory / "clock-expectations.json"), "bytes": 10, "sha256": "d" * 64},
                   "clock2000_prior_audit": clock_audit,
                   "packet": {"path": str(directory / "context.json"), "bytes": 10, "sha256": "e" * 64},
                   "outer_packet": {"path": str(directory / "outer.json"), "bytes": 10, "sha256": "f" * 64}}
        prior = accepted["boundary"]
        prior.update(schema=compare.STOCK_DECREMENT_BOUNDARY_SCHEMA,
                     clock2000_baseline_stocks=[4, 4, 4, 4],
                     audit=write_identity(directory / "accepted-audit.json", {}),
                     audit_context=copy.deepcopy(context),
                     source_scan_limits={"max_bytes": 268435456, "max_records": 48000, "max_seconds": 60})
        accepted["comparison"] = write_identity(directory / "accepted-comparison.json", {})
        boundary.update(audit=write_identity(directory / "next-audit.json", {}),
                        audit_context=context,
                        source_scan_limits={"max_bytes": 268435456, "max_records": 48000, "max_seconds": 60})
        for event in (prior["event"], boundary["event"]):
            event["target"]["state_fields_observed"] = ["fighter_entities", "fighters", "match_frame", "pad_state_hex", "rng", "scene_frame"]
        source["stock_decrement_boundary"] = boundary
        terminal_tuple = compare._stock_decrement_tuple(boundary["event"]["target"])
        terminal_sequence = terminal_tuple["source_sequence"]
        raw = b"".join(raw_records[:terminal_sequence + 1])
        terminal_prefix = {"bytes_read": len(raw), "records_read": terminal_sequence + 1,
                           "last_source_sequence": terminal_sequence, "sha256": hashlib.sha256(raw).hexdigest()}
        last_state = compare._state_from_payload(last_row["payload"], "fixture last state")
        last_state["fighter_entities"] = compare._fighter_entities(last_row["payload"], "fixture last entities", 0, {})
        final_cursor = 3 + last_tick
        last_target = {"match_index": 0, "source_tick": last_tick, "source_tick_seq": last_row["seq"],
                       "pad_consume_source_sequence": last_row["seq"] - 1,
                       "timeline_frame_index": final_cursor - 1, "cursor_after_frame": final_cursor,
                       **{key: last_state[key] for key in ("rng", "scene_frame", "match_frame", "fighter_entities")},
                       "state_fields_observed": sorted(last_state)}
        terminal_sequence = chain[-1]["source_sequence"]
        results_prefix = compact_prefix(terminal_sequence)
        exported_cursor = final_cursor + 1
        browser_rows = [json.loads(line) for line in selected["port_trace"].read_text().splitlines()]
        result_row = {key: copy.deepcopy(browser_rows[2][key]) for key in browser_rows[2]
                      if key in {"record", "index", "scene", "rng", "pad_state_hex", "supplied_inputs"}}
        result_row.update(record="session_frame", index=final_cursor, scene=4,
                          supplied_inputs=copy.deepcopy(fixture["recipe"].frames[final_cursor]["pads"]))
        browser_rows = browser_rows[:final_cursor + 2]
        browser_rows.append(result_row)
        observed = {"last_complete_match": {"target": last_target, "state": last_state},
                    "exit_chain": chain, "source_prefix": results_prefix,
                    "results_scalar_state_observed": False, "winner_or_ko_claim": False,
                    "results_entry": chain[-1], "recipe_cursor": final_cursor}
        observed["rejoins"] = [{"label": "clock2000" if cp["label"] == "target" else cp["label"],
                                "target": {"match_index": cp["tuple"]["match_index"],
                                           "source_tick": cp["tuple"]["source_tick"],
                                           "source_tick_seq": cp["tuple"]["source_sequence"],
                                           "pad_consume_source_sequence": cp["tuple"]["pad_consume_sequence"],
                                           "timeline_frame_index": cp["tuple"]["timeline_frame_index"],
                                           "cursor_after_frame": cp["tuple"]["browser_cursor"],
                                           "match_frame": cp["tuple"]["match_frame"]},
                                "prefix": cp["prefix"]} for cp in checkpoints]
        observed["rejoins"] += [{"label": label, "event": event, "prefix": prefix}
                                for label, event, prefix in (("stock258", prior["event"], accepted["prefix"]),
                                                             ("stock271", boundary["event"], terminal_prefix))]
        stat = compare._file_stat_identity(selected["reference"])
        results_audit = {"schema": "melee-web-b4-first-results-source-audit-v1",
                         "status": "first_match_results_entry_found", "audit_completed": True,
                         "complete": False, "whole_session_equivalent": False, "source_stat_stable": True,
                         "source_stat_before": stat, "source_stat_after": stat, "observed": observed,
                         "source_prefix": results_prefix, "checkpoints_rejoined": observed["rejoins"]}
        audit_id = write_identity(directory / "results-audit.json", results_audit)
        portable = {"schema": "melee-web-b4-first-match-results-source-receipt-v1",
                    "source_identity": {"capture_id": source["capture_id"], "sequence_id": source["sequence_id"],
                                        "trace_bytes": source["trace"]["bytes"],
                                        "recorded_full_trace_sha256": source["trace"]["recorded_full_sha256"],
                                        "full_trace_rehashed": False}, "observed": observed,
                    "provenance": {"source_audit": {"sha256": audit_id["sha256"]}}}
        descriptor = {"schema": compare.RESULTS_BOUNDARY_SCHEMA, "audit": audit_id,
                      "receipt": write_identity(directory / "results-receipt.json", portable),
                      **{key: observed[key] for key in ("last_complete_match", "exit_chain", "source_prefix")},
                      "accepted_stock_receipts": [
                          {"label": "stock258", "audit": prior["audit"], "comparison": accepted["comparison"], "prefix": accepted["prefix"]},
                          {"label": "stock271", "audit": boundary["audit"], "comparison": write_identity(directory / "second-comparison.json", {}), "prefix": terminal_prefix}],
                      "source_scan_limits": {"max_bytes": compare.V10_RESULTS_SCAN_BYTE_CAP,
                                             "max_records": compare.V10_RESULTS_SCAN_RECORD_CAP, "max_seconds": 60},
                      "browser_witness": {"scene": 4, "index": final_cursor, "cursor": exported_cursor}}
        source["first_results_boundary"] = descriptor
        if mutation:
            mutation(fixture, browser_rows, descriptor, raw_records)
        selected["port_trace"].write_text("\n".join(json.dumps(row) for row in browser_rows) + "\n")
        packet["browser"]["trace"] = {
            "path": str(selected["port_trace"]), "bytes": selected["port_trace"].stat().st_size,
            "sha256": hashlib.sha256(selected["port_trace"].read_bytes()).hexdigest()}
        packet.update(schema=compare.RESULTS_EXPECTATION_SCHEMA,
                      scope=compare.V10_FIRST_MATCH_RESULTS_SCOPE, version=1)
        selected["match_clock_boundary_audit"] = directory / "clock2000-audit.json"
        selected["stock_decrement_boundary_audit"] = directory / "next-audit.json"
        selected["first_results_boundary_audit"] = directory / "results-audit.json"
        fixture["packet_path"].write_text(json.dumps(packet))
        stat = compare._file_stat_identity(selected["reference"])
        source_identity = {"trace_bytes": stat["bytes"], "recorded_full_trace_sha256": source["trace"]["recorded_full_sha256"],
                           "full_trace_rehashed": False, "manifest_sha256": source["manifest"]["sha256"],
                           "source_report_sha256": source["report"]["sha256"], "audit_sha256": source["audit"]["sha256"],
                           "audit_records_decoded": 11, "audit_bytes_read": 100}
        browser_identity = {"required_cursor": exported_cursor, "target_cursor": exported_cursor,
                            "observed_cursor": exported_cursor, "requested_cursor": exported_cursor,
                            "exported_cursor": exported_cursor, "capture_report_sha256": "1" * 64,
                            "producer_manifest_sha256": "2" * 64, "browser_report_sha256": "3" * 64, "port_trace_sha256": "4" * 64}
        yielded = []
        def records(path, *, max_bytes, max_records, stats):
            self.assertEqual((max_bytes, max_records), (compare.V10_RESULTS_SCAN_BYTE_CAP, compare.V10_RESULTS_SCAN_RECORD_CAP))
            for row, raw in zip(fixture["rows"], raw_records):
                if reader_cap and row["seq"] == 3:
                    raise compare.ComparisonError("synthetic original-reader cap reached")
                stats.record_bytes(raw)
                stats.records_read += 1
                yielded.append(row["seq"])
                yield row
        def browser_provenance(*args, **kwargs):
            compare._validate_v10_browser_export(selected["port_trace"], fixture["recipe"], exported_cursor, packet)
            return {}, {}, {}, browser_identity
        clock = mock.patch.object(compare.time, "monotonic", side_effect=[0] + [60] * 100) if deadline else contextlib.nullcontext()
        with (clock, mock.patch.object(compare, "_validate_v10_source_provenance", return_value=(fixture["source_manifest"], {}, fixture["source_audit"], source_identity)),
              mock.patch.object(compare, "_validate_v10_browser_provenance", side_effect=browser_provenance),
              mock.patch.object(compare, "_validate_first_positive_audit", return_value=(fixture["positive_audit"], source["positive_boundary_audit"]["sha256"])),
              mock.patch.object(compare, "_validate_clock60_audit", return_value=(fixture["clock_audit"], source["clock60_boundary_audit"]["sha256"])),
              mock.patch.object(compare, "_validate_match_clock_boundary_audit", return_value=({"observed": {}}, clock_audit["sha256"])),
              mock.patch.object(compare, "_validate_stock_decrement_boundary_audit", return_value=({"observed": {"source_prefix": terminal_prefix}}, boundary["audit"]["sha256"])),
              mock.patch.object(compare, "_validate_accepted_stock_receipts"),
              mock.patch.object(compare, "iter_records", side_effect=records)):
            result = compare.compare_paths(
                selected["reference"], selected["recipe"], selected["port_trace"], scope=compare.V10_FIRST_MATCH_RESULTS_SCOPE,
                expectations=fixture["packet_path"], source_manifest=selected["source_manifest"],
                source_report=selected["source_report"], source_audit=selected["source_audit"],
                browser_capture_report=selected["browser_capture_report"], browser_producer_manifest=selected["browser_producer_manifest"],
                browser_report=selected["browser_report"], positive_boundary_audit=selected["positive_boundary_audit"],
                clock60_boundary_audit=selected["clock60_boundary_audit"], match_clock_boundary_audit=selected["match_clock_boundary_audit"],
                stock_decrement_boundary_audit=selected["stock_decrement_boundary_audit"],
                first_results_boundary_audit=selected["first_results_boundary_audit"])
        (directory / "comparison-result.json").write_text(json.dumps(result, indent=2) + "\n")
        return result, yielded, descriptor

    def test_public_path_full_match_eight_gates_and_unpaired_results(self):
        with owned_fixture() as directory:
            result, yielded, boundary = self.compare_fixture(Path(directory))
            self.assertEqual(result["boundary_result"], "equivalent", result.get("error"))
            self.assertEqual(result["result"], "incomplete")
            self.assertIs(result["complete"], False)
            self.assertIs(result["whole_session_equivalent"], False)
            report = result["first_match_results_entry_boundary"]
            self.assertEqual(report["last_complete_match"], boundary["last_complete_match"])
            self.assertEqual(report["exit_chain"], boundary["exit_chain"])
            self.assertEqual([row["label"] for row in report["fresh_prefix_checkpoints"]],
                             ["clock1", "clock60", "clock300", "clock500", "clock1000", "clock2000", "stock258", "stock271"])
            self.assertTrue(all(row["rejoined_before_continuing"] is True for row in report["fresh_prefix_checkpoints"]))
            self.assertEqual(yielded[-1], boundary["source_prefix"]["last_source_sequence"])
            self.assertEqual(len(yielded), boundary["source_prefix"]["records_read"])
            self.assertEqual(result["source_prefix"]["sha256"], boundary["source_prefix"]["sha256"])
            self.assertEqual([row["label"] for row in report["accepted_stock_events"]], ["stock258", "stock271"])
            self.assertTrue(all(row["event"]["target"]["source_tick_seq"] - row["event"]["target"]["pad_consume_source_sequence"] == 2
                                for row in report["accepted_stock_events"]))
            self.assertEqual(result["match_state_frames_compared"], boundary["last_complete_match"]["target"]["source_tick"] + 1)
            self.assertEqual(result["nonmatch_frames_input_ordered"], 2)
            self.assertIs(report["first_stock_decrement_only"], False)
            self.assertIs(report["source_paired_Results_input"], False)
            self.assertIs(report["Results_scalar_equality"], False)
            self.assertEqual(result["timeline_frames_consumed"], boundary["browser_witness"]["index"])
            self.assertEqual(result["browser_results_owner_witness"]["index"], boundary["browser_witness"]["index"])
            self.assertNotIn("stock_decrement_event_boundary", result)
            for check in ("source_ticks_through_first_match_results_entry", "second_accepted_stock_event_rejoin", "ordered_source_results_exit_chain"):
                self.assertEqual(result["checks"][check], "pass")

    def assert_rejected(self, mutation=None, *, contains=None, **kwargs):
        with owned_fixture() as directory:
            result, yielded, _ = self.compare_fixture(directory, mutation=mutation, **kwargs)
            self.assertTrue(result["result"] == "invalid" or result["boundary_result"] == "divergent", result.get("error"))
            self.assertIs(result["whole_session_equivalent"], False)
            self.assertIs(result["complete"], False)
            if contains is not None:
                self.assertIn(contains, result.get("error", str(result.get("first_mismatch"))))
            return result, yielded

    def test_browser_witness_missing_wrong_duplicate_and_malformed_tail(self):
        for mutation in (
                lambda f, b, d, r: b.pop(),
                lambda f, b, d, r: b[-1].update(scene=3),
                lambda f, b, d, r: b.append(copy.deepcopy(b[-1])),
                lambda f, b, d, r: b[-1].update(index=True),
                lambda f, b, d, r: b[-1].update(unexpected=True),
                lambda f, b, d, r: b[-1].update(rng=True),
                lambda f, b, d, r: b[-1]["supplied_inputs"].__setitem__(0, "0100000000000000000000")):
            with self.subTest(mutation=mutation):
                _, yielded = self.assert_rejected(mutation)
                self.assertEqual(yielded, [])  # Complete export-tail preflight precedes original reader.

    def test_results_scalars_are_schema_checked_but_unpaired(self):
        def mutation(f, b, d, r):
            b[-1]["rng"] = 7
            b[-1]["pad_state_hex"] = "ff" * compare.PAD_STATE_BYTES
        with owned_fixture() as directory:
            result, _, _ = self.compare_fixture(directory, mutation=mutation)
            self.assertEqual(result["boundary_result"], "equivalent", result.get("error"))
            self.assertEqual(result["browser_results_owner_witness"]["uncompared_fields"], ["rng", "pad_state_hex"])

    def test_later_stock_selection_stops_but_exact_state_validation_continues(self):
        def both_change(f, b, d, r):
            row = next(row for row in f["rows"] if row.get("payload", {}).get("boundary") == "source_tick" and row["source_tick"] == 2180)
            next(item for item in row["payload"]["slices"] if item.get("name") == "fighter_stocks" and item["flags"] == 0)["hex"] = "02"
            b[4 + 2180]["fighters"][0]["stocks"] = 2
        with owned_fixture() as directory:
            result, _, _ = self.compare_fixture(directory, mutation=both_change)
            self.assertEqual(result["boundary_result"], "equivalent", result.get("error"))
        for field, value in (("stocks", 2), ("position_bits", ["3f800000", "00000000", "00000000"])):
            with self.subTest(field=field):
                self.assert_rejected(lambda f, b, d, r: b[4 + 2180]["fighters"][0].update({field: value}))
        self.assert_rejected(lambda f, b, d, r: b[4 + 2180]["fighter_entities"][0].update(generation=1))
        self.assert_rejected(lambda f, b, d, r: b[4 + 2180].update(rng=7))
        self.assert_rejected(lambda f, b, d, r: b[4 + 2180].update(pad_state_hex="ff" * compare.PAD_STATE_BYTES))

    def test_later_source_entity_and_typed_slice_validation_remains_strict(self):
        def mutate(f, b, d, r):
            row = next(row for row in f["rows"] if row.get("payload", {}).get("boundary") == "source_tick" and row["source_tick"] == 2180)
            item = next(item for item in row["payload"]["slices"] if item.get("name") == "fighter_head" and item["flags"] == 0)
            raw = bytearray.fromhex(item["hex"]); raw[0x0c] = 3; item["hex"] = raw.hex()
        self.assert_rejected(mutate, contains="player_id")
        def bad_stock(f, b, d, r):
            row = next(row for row in f["rows"] if row.get("payload", {}).get("boundary") == "source_tick" and row["source_tick"] == 2180)
            item = next(item for item in row["payload"]["slices"] if item.get("name") == "fighter_stocks" and item["flags"] == 0)
            row["payload"]["slices"].remove(item)
        self.assert_rejected(bad_stock, contains="Missing typed observer coverage")

    def test_typed_exit_chain_rejects_duplicate_missing_wrong_pc_and_pending(self):
        self.assert_rejected(lambda f, b, d, r: f["rows"][-3]["payload"].update(boundary="vs_exit"), contains="duplicated")
        self.assert_rejected(lambda f, b, d, r: f["rows"][-4]["payload"].update(boundary="vs_exit_return"), contains="missing")
        self.assert_rejected(lambda f, b, d, r: f["rows"][-4]["payload"].update(pc=0), contains="authored PC")
        def pending(f, b, d, r):
            consume = copy.deepcopy(next(row for row in reversed(f["rows"]) if row.get("payload", {}).get("boundary") == "pad_consume"))
            f["rows"].insert(len(f["rows"]) - 4, consume)
            for sequence, row in enumerate(f["rows"]): row["seq"] = sequence
            r[:] = [b"R" + row["seq"].to_bytes(4, "big") for row in f["rows"]]
        self.assert_rejected(pending, contains="pending consume")

    def test_fresh_prefix_gate_and_final_terminal_prefix_are_not_receipt_flags(self):
        def first_gate(f, b, d, r):
            r[10] = b"X" + r[10][1:]
        _, yielded = self.assert_rejected(first_gate, contains="clock1 checkpoint")
        self.assertLess(len(yielded), 300)
        def second_gate(f, b, d, r):
            seq = d["accepted_stock_receipts"][1]["prefix"]["last_source_sequence"]
            r[seq] = b"X" + r[seq][1:]
        self.assert_rejected(second_gate, contains="stock-decrement event audit")
        self.assert_rejected(lambda f, b, d, r: r.__setitem__(-1, b"X" + r[-1][1:]), contains="terminal consumed prefix")

    def test_stale_receipt_and_forged_terminal_descriptor_preflight(self):
        def stale(f, b, d, r):
            Path(d["receipt"]["path"]).write_text("{}")
        _, yielded = self.assert_rejected(stale, contains="portable Results receipt")
        self.assertEqual(yielded, [])
        _, yielded = self.assert_rejected(lambda f, b, d, r: d.update(already_rejoined=True), contains="expectations are malformed")
        self.assertEqual(yielded, [])
        self.assert_rejected(lambda f, b, d, r: d["source_prefix"].update(records_read=True))

    def test_original_reader_eof_cap_and_internal_deadline_stop(self):
        self.assert_rejected(lambda f, b, d, r: f["rows"].pop(), contains="Results")
        _, yielded = self.assert_rejected(reader_cap=True, contains="reader cap reached")
        self.assertEqual(yielded, [0, 1, 2])
        _, yielded = self.assert_rejected(deadline=True, contains="60-second bound")
        self.assertEqual(yielded, [0])

    def test_export_cap_is_specific_to_the_results_scope(self):
        # Exercise the real export path with a scaled lower historical cap.
        # The valid fixture's 2,229 records exceed that historical cap.
        with (owned_fixture() as directory,
              mock.patch.object(compare, "V10_BROWSER_EXPORT_RECORD_CAP", 2048),
              mock.patch.object(compare, "V10_RESULTS_BROWSER_EXPORT_RECORD_CAP", 4096)):
            result, _, _ = self.compare_fixture(directory)
            self.assertEqual(result["boundary_result"], "equivalent", result.get("error"))
        from types import SimpleNamespace
        for scope, cap in ((compare.V10_FIRST_MATCH_RESULTS_SCOPE, 16384),
                           (compare.V10_FIRST_STOCK_DECREMENT_SCOPE, 8192)):
            with self.subTest(scope=scope), self.assertRaises(compare.ComparisonError):
                compare._validate_v10_browser_export(Path("absent-synthetic-export.jsonl"),
                    SimpleNamespace(scope=scope, frame_count=20000), cap, {})


if __name__ == "__main__":
    unittest.main()
