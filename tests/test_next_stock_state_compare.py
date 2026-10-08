"""Asset-free controls for a typed stock checkpoint followed by one terminal event."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_whole_session_state_compare as fixtures
import whole_session_state_compare as compare
from reference_observer_stream import ObserverStreamStats


def write_identity(path, value):
    raw = json.dumps(value, sort_keys=True).encode()
    path.write_bytes(raw)
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


class NextStockStateCompareTests(unittest.TestCase):
    def fixture(self):
        source, prior = fixtures.WholeSessionStateCompareTests._stock_decrement_expectation_fixture()
        boundary = copy.deepcopy(prior)
        boundary["schema"] = compare.STOCK_DECREMENT_BOUNDARY_V2_SCHEMA
        boundary["accepted_stock_checkpoint"] = {
            "boundary": prior,
            "prefix": {"bytes_read": 151442332, "records_read": 22142,
                       "last_source_sequence": 22141, "sha256": "d" * 64},
            "comparison": {"path": "/comparison.json", "bytes": 10, "sha256": "c" * 64},
        }
        event = boundary["event"]
        event.update(stocks_before=[4, 3, 3, 4], stocks_after=[4, 3, 3, 3],
                     changed_slots=[{"slot": 3, "before": 4, "after": 3}])
        event["target"].update(cursor_after_frame=5167, timeline_frame_index=5166,
                              source_tick=3982, scene_frame=3982, match_frame=3859,
                              source_tick_seq=24022, pad_consume_source_sequence=24020,
                              rng=1688650196)
        source["stock_decrement_boundary"] = boundary
        return source, boundary

    def test_next_expectations_and_nonadjacent_pad_sequence(self):
        source, boundary = self.fixture()
        self.assertIs(compare._validate_stock_decrement_expectations(source, {"frame_count": 6000}), boundary)
        self.assertEqual(boundary["event"]["target"]["source_tick_seq"] -
                         boundary["event"]["target"]["pad_consume_source_sequence"], 2)

    def test_next_expectations_reject_flags_bool_signed_and_bad_order(self):
        mutations = [
            lambda b: b["accepted_stock_checkpoint"].update(already_rejoined=True),
            lambda b: b["accepted_stock_checkpoint"]["prefix"].update(records_read=True),
            lambda b: b["accepted_stock_checkpoint"]["boundary"]["event"]["stocks_after"].__setitem__(0, True),
            lambda b: b["event"]["stocks_before"].__setitem__(0, 128),
            lambda b: b["event"]["target"].update(source_tick=3606),
            lambda b: b["accepted_stock_checkpoint"]["boundary"].update(schema=compare.STOCK_DECREMENT_BOUNDARY_V2_SCHEMA),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                source, boundary = self.fixture()
                mutate(boundary)
                with self.assertRaises(compare.ComparisonError):
                    compare._validate_stock_decrement_expectations(source, {"frame_count": 6000})

    def comparator(self, boundary):
        obj = compare.Comparator.__new__(compare.Comparator)
        obj.stock_decrement_boundary = boundary
        obj.match_clock_boundary = {"source_tick": 2123}
        obj.previous_stock_counts = None
        obj.stock_decrement_observed = False
        obj.accepted_stock_observed = False
        obj.accepted_stock_rejoined = False
        obj.fail = lambda message, **kwargs: (_ for _ in ()).throw(compare.ComparisonError(message))
        return obj

    def frame(self, boundary, tick, stocks):
        target = next((event["target"] for event in
                       (boundary["accepted_stock_checkpoint"]["boundary"]["event"], boundary["event"])
                       if event["target"]["source_tick"] == tick), {})
        return {"source_tick": tick, "state": {"fighters": [{"stocks": value} for value in stocks],
                "rng": target.get("rng", 0), "scene_frame": tick,
                "fighter_entities": copy.deepcopy(target.get("fighter_entities", []))}}

    def test_observed_transitions_do_not_self_accept_prefix_gate(self):
        _, boundary = self.fixture()
        obj = self.comparator(boundary)
        for tick, stocks in ((2123, [4, 4, 3, 4]), (3606, [4, 3, 3, 4]), (3982, [4, 3, 3, 3])):
            obj._validate_stock_decrement_frame(self.frame(boundary, tick, stocks), tick)
        self.assertTrue(obj.accepted_stock_observed)
        self.assertTrue(obj.stock_decrement_observed)
        self.assertFalse(obj.accepted_stock_rejoined)

    def test_transition_rejects_early_late_wrong_slot_rng_entities_and_bool(self):
        cases = [(3605, [4, 3, 3, 4], None), (3606, [4, 4, 3, 4], None),
                 (3606, [4, 4, 2, 4], None), (3606, [4, 3, 3, 4], "rng"),
                 (3606, [4, 3, 3, 4], "entities"), (3606, [True, 3, 3, 4], None)]
        for tick, stocks, mutation in cases:
            with self.subTest(tick=tick, mutation=mutation, stocks=stocks):
                _, boundary = self.fixture()
                obj = self.comparator(boundary)
                obj._validate_stock_decrement_frame(self.frame(boundary, 2123, [4, 4, 3, 4]), 0)
                frame = self.frame(boundary, tick, stocks)
                if mutation == "rng":
                    frame["state"]["rng"] = 1
                if mutation == "entities":
                    frame["state"]["fighter_entities"][0]["generation"] = 1
                with self.assertRaises(compare.ComparisonError):
                    obj._validate_stock_decrement_frame(frame, 1)

    def stream(self, *, mutation=None, observe=True, joined=True):
        raw = [f"row-{index}\n".encode() for index in range(10)]
        def prefix(seq):
            consumed = b"".join(raw[:seq + 1])
            return {"records_read": seq + 1, "bytes_read": len(consumed),
                    "sha256": hashlib.sha256(consumed).hexdigest(), "last_source_sequence": seq}
        def target(seq):
            return {"source_tick": seq, "source_tick_seq": seq, "match_index": 0,
                    "pad_consume_source_sequence": seq - 2, "timeline_frame_index": seq,
                    "cursor_after_frame": seq + 1, "match_frame": seq}
        checkpoints = [{"label": f"clock{index}", "tuple": {"source_sequence": index},
                        "prefix": prefix(index)} for index in range(6)]
        accepted = {"boundary": {"event": {"target": target(6)}}, "prefix": prefix(6)}
        terminal = {"event": {"target": target(8)}}
        if mutation:
            mutation(checkpoints, accepted)
        stats = ObserverStreamStats()
        comparator = SimpleNamespace(stock_decrement_observed=False, accepted_stock_observed=False,
                                     accepted_stock_rejoined=False)
        yielded = []
        def consume(row):
            if row["seq"] == 6:
                comparator.accepted_stock_observed = observe
            if row["seq"] > 6:
                self.assertTrue(comparator.accepted_stock_rejoined)
            comparator.stock_decrement_observed = row["seq"] == 8
        source = SimpleNamespace(consume=consume)
        def records():
            for seq, data in enumerate(raw):
                stats.record_bytes(data)
                stats.records_read += 1
                yielded.append(seq)
                yield {"seq": seq}
        def complete(row, source, comparator, boundary):
            return row["seq"] == boundary["source_sequence"] and (joined or row["seq"] != 6)
        with mock.patch.object(compare, "_match_boundary_join_complete", side_effect=complete):
            result = compare._consume_ordered_clock_lineage(
                records(), source, comparator, stats, checkpoints, terminal_event=terminal,
                terminal_prefix=prefix(8), accepted_stock_checkpoint=accepted, max_seconds=60)
        return result, yielded, comparator

    def test_seventh_gate_and_terminal_stop(self):
        result, yielded, comparator = self.stream()
        self.assertEqual(result, (True, 8))
        self.assertEqual(yielded, list(range(9)))
        self.assertTrue(comparator.accepted_stock_rejoined)

    def test_gate_rejects_missing_observation_join_prefix_and_clock(self):
        for kwargs in ({"observe": False}, {"joined": False},
                       {"mutation": lambda c, a: a["prefix"].update(sha256="0" * 64)},
                       {"mutation": lambda c, a: a["prefix"].update(records_read=8)},
                       {"mutation": lambda c, a: a["prefix"].update(bytes_read=1)},
                       {"mutation": lambda c, a: c.reverse()}):
            with self.subTest(kwargs=kwargs), self.assertRaises(compare.ComparisonError):
                self.stream(**kwargs)

    def test_next_audit_requires_observed_event_and_seventh_status(self):
        with tempfile.TemporaryDirectory() as directory:
            packet, path, recipe, clock_audit, stat = fixtures.WholeSessionStateCompareTests()._stock_decrement_audit_fixture(directory)
            prior = copy.deepcopy(packet["source"]["stock_decrement_boundary"])
            original = json.loads(path.read_text())
            prefix = {key: original["observed"]["source_prefix"][key] for key in compare.ORDERED_CLOCK_PREFIX_FIELDS}
            terminal = {**compare._stock_decrement_tuple(prior["event"]["target"]),
                        **{key: prior["event"][key] for key in ("stocks_before", "stocks_after", "changed_slots")},
                        "source_prefix_sha256": prefix["sha256"], "source_prefix_bytes": prefix["bytes_read"],
                        "source_prefix_records": prefix["records_read"], "audit_sha256": prior["audit"]["sha256"]}
            comparison = {"scope": compare.V10_FIRST_STOCK_DECREMENT_SCOPE, "boundary_result": "equivalent",
                          "complete": False, "whole_session_equivalent": False,
                          "checks": {"stock_decrement_event_state": "pass", "ordered_clock_checkpoint_states": "pass"},
                          "stock_decrement_event_boundary": terminal}
            identity = write_identity(Path(directory) / "accepted-comparison.json", comparison)
            _, boundary = self.fixture()
            boundary["accepted_stock_checkpoint"] = {"boundary": prior, "prefix": prefix, "comparison": identity}
            boundary["audit_context"] = copy.deepcopy(prior["audit_context"])
            outer = {"accepted_baseline": {"event": prior["event"], "prefix": prefix},
                     "accepted_receipts": {"source_audit": prior["audit"], "comparison": identity}}
            outer_identity = write_identity(Path(directory) / "outer.json", outer)
            boundary["audit_context"]["outer_packet"] = outer_identity
            packet["source"]["stock_decrement_boundary"] = boundary
            report = copy.deepcopy(original)
            report.update(schema="melee-web-b4-stock-decrement-source-audit-v3", version=3,
                          scope="source-only-bounded-next-stock-decrement-after-accepted-event",
                          status="next_stock_count_decrement_found", outer_packet=outer_identity,
                          event_definition="first joined tick strictly after the accepted stock event where any exact signed-byte fighter stocks value decreases")
            next_path = Path(directory) / "next-audit.json"
            report["report_path"] = str(next_path)
            report["observed"].update(accepted_stock_observed=copy.deepcopy(prior["event"]),
                                       accepted_stock_rejoined=True, stock_decrement=boundary["event"],
                                       match_ticks_observed=3983, timeline_frames_input_ordered_against_recipe=5167,
                                       source_prefix={"bytes_read": prefix["bytes_read"] + 100,
                                                      "records_read": 24023, "last_source_sequence": 24022,
                                                      "sha256": "0" * 64})
            report["source"]["content_bytes_read"] = prefix["bytes_read"] + 100
            report["checkpoints"]["accepted_stock"] = "pass"
            boundary["audit"] = write_identity(next_path, report)
            compare._validate_stock_decrement_expectations(packet["source"], {"frame_count": 10000})
            compare._validate_stock_decrement_boundary_audit(next_path, packet, recipe, clock_audit,
                                                            source_stat_before=stat)
            for mutate in (
                    lambda r: r["observed"]["accepted_stock_observed"]["target"].update(rng=1),
                    lambda r: r["observed"].update(accepted_stock_rejoined=1),
                    lambda r: r["observed"]["checkpoint_rejoins"].update(clock1=1),
                    lambda r: r["checkpoints"].pop("accepted_stock")):
                bad = copy.deepcopy(report)
                mutate(bad)
                boundary["audit"] = write_identity(next_path, bad)
                with self.subTest(mutation=mutate), self.assertRaises(compare.ComparisonError):
                    compare._validate_stock_decrement_boundary_audit(next_path, packet, recipe, clock_audit)

    def next_comparison_fixture(self, directory):
        fixture = fixtures._clock60_comparison_fixture(directory, terminal_match_frame=2100)
        recipe, rows = fixture["recipe"], fixture["rows"]
        recipe.scope = compare.V10_FIRST_STOCK_DECREMENT_SCOPE
        # A retained PAD poll can interrupt a valid consume/tick pair.
        for tick in (2160, 2150):
            index = next(index for index, row in enumerate(rows)
                         if row.get("payload", {}).get("boundary") == "source_tick"
                         and row["source_tick"] == tick)
            poll = copy.deepcopy(rows[index - 1])
            poll["payload"]["boundary"] = "pad_poll"
            poll["payload"]["slices"] = [{"name": "pad_snapshot", "address": 0x804C1F84,
                                           "size": 0x358, "hex": fixtures._raw_pad_snapshot().hex()}]
            rows.insert(index, poll)
        for sequence, row in enumerate(rows):
            row["seq"] = sequence
        ticks = {row["source_tick"]: row for row in rows
                 if row.get("payload", {}).get("boundary") == "source_tick"}
        browser_path = fixture["selected"]["port_trace"]
        browser_rows = [json.loads(line) for line in browser_path.read_text().splitlines()]
        for tick, row in ticks.items():
            for slot, event_tick in ((1, 2150), (3, 2160)):
                if tick >= event_tick:
                    stock_slice = next(item for item in row["payload"]["slices"]
                                       if item.get("name") == "fighter_stocks" and item.get("flags") == slot)
                    stock_slice["hex"] = "03"
                    browser_rows[4 + tick]["fighters"][slot]["stocks"] = 3
        browser_path.write_text("\n".join(json.dumps(row) for row in browser_rows) + "\n")
        raw_records = [json.dumps(row, sort_keys=True).encode() + b"\n" for row in rows]
        def prefix(sequence):
            raw = b"".join(raw_records[:sequence + 1])
            return {"bytes_read": len(raw), "records_read": sequence + 1,
                    "last_source_sequence": sequence, "sha256": hashlib.sha256(raw).hexdigest()}
        def boundary_tuple(tick):
            sequence = ticks[tick]["seq"]
            return {"match_index": 0, "source_tick": tick, "source_sequence": sequence,
                    "pad_consume_sequence": sequence - (2 if tick in (2150, 2160) else 1),
                    "timeline_frame_index": 2 + tick,
                    "browser_cursor": 3 + tick, "match_frame": tick - 123}
        def event(tick, slot, before, after):
            state = browser_rows[4 + tick]
            target = boundary_tuple(tick)
            return {"event": "first_typed_stock_count_decrement",
                    "stocks_before": before, "stocks_after": after,
                    "changed_slots": [{"slot": slot, "before": 4, "after": 3}],
                    "target": {"match_index": 0, "source_tick": tick,
                               "source_tick_seq": target["source_sequence"],
                               "pad_consume_source_sequence": target["pad_consume_sequence"],
                               "timeline_frame_index": target["timeline_frame_index"],
                               "cursor_after_frame": target["browser_cursor"],
                               "match_frame": target["match_frame"], "scene_frame": tick,
                               "rng": state["rng"], "fighter_entities": state["fighter_entities"]}}
        checkpoints = [{"label": "target" if clock == 2000 else f"clock{clock}",
                        "tuple": boundary_tuple(123 + clock),
                        "prefix": prefix(ticks[123 + clock]["seq"])}
                       for clock in (1, 60, 300, 500, 1000, 2000)]
        accepted_event = event(2150, 1, [4, 4, 4, 4], [4, 3, 4, 4])
        accepted = {"boundary": {"event": accepted_event}, "prefix": prefix(ticks[2150]["seq"])}
        boundary = {"schema": compare.STOCK_DECREMENT_BOUNDARY_V2_SCHEMA,
                    "clock2000_baseline_stocks": [4, 4, 4, 4],
                    "accepted_stock_checkpoint": accepted,
                    "event": event(2160, 3, [4, 3, 4, 4], [4, 3, 4, 3])}
        return fixture, checkpoints, accepted, boundary, raw_records, ticks

    def test_real_source_collector_comparator_rejoins_then_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture, checkpoints, accepted, boundary, raw_records, ticks = self.next_comparison_fixture(Path(directory))
            recipe, rows = fixture["recipe"], fixture["rows"]
            browser_path = fixture["selected"]["port_trace"]
            def prefix(sequence):
                raw = b"".join(raw_records[:sequence + 1])
                return {"bytes_read": len(raw), "records_read": sequence + 1,
                        "last_source_sequence": sequence, "sha256": hashlib.sha256(raw).hexdigest()}
            browser = compare.BrowserReader(browser_path)
            try:
                comparator = compare.Comparator(
                    recipe, browser, positive_boundary=checkpoints[0]["tuple"],
                    clock60_boundary=checkpoints[1]["tuple"],
                    match_clock_boundary={"target_match_frame_at_least": 2000, **checkpoints[-1]["tuple"]},
                    ordered_clock_checkpoints=checkpoints, stock_decrement_boundary=boundary)
                source = compare.SourceCollector(comparator, recipe, fixture["source_manifest"],
                                                 fixture["source_audit"], fixture["packet"]["source"])
                stats = ObserverStreamStats()
                consumed = []
                def records():
                    for row, raw in zip(rows, raw_records):
                        if row["seq"] > ticks[2150]["seq"]:
                            self.assertTrue(comparator.accepted_stock_rejoined)
                        stats.record_bytes(raw)
                        stats.records_read += 1
                        consumed.append(row["seq"])
                        yield row
                complete, last = compare._consume_ordered_clock_lineage(
                    records(), source, comparator, stats, checkpoints,
                    terminal_event=boundary, terminal_prefix=prefix(ticks[2160]["seq"]),
                    accepted_stock_checkpoint=accepted, max_seconds=60)
                self.assertTrue(complete)
                self.assertTrue(comparator.accepted_stock_rejoined)
                self.assertEqual(last, ticks[2160]["seq"])
                self.assertEqual(consumed[-1], last)
                self.assertIsNone(source.pending)
                self.assertEqual(stats.prefix_sha256, prefix(last)["sha256"])
            finally:
                browser.close()

    def compare_fixture(self, directory, *, corrupt_tail=None):
        fixture, checkpoints, accepted, boundary, raw_records, ticks = self.next_comparison_fixture(directory)
        packet, selected = fixture["packet"], fixture["selected"]
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
        exported_cursor = terminal_tuple["browser_cursor"] + 1
        browser_rows = [json.loads(line) for line in selected["port_trace"].read_text().splitlines()]
        browser_rows = browser_rows[:exported_cursor + 2]  # Header and first setup are not timeline frames.
        if corrupt_tail == "bool_index":
            browser_rows[-1]["index"] = True
        elif corrupt_tail == "extra_row":
            browser_rows.append(copy.deepcopy(browser_rows[-1]))
        selected["port_trace"].write_text("\n".join(json.dumps(row) for row in browser_rows) + "\n")
        packet["browser"]["trace"] = {
            "path": str(selected["port_trace"]), "bytes": selected["port_trace"].stat().st_size,
            "sha256": hashlib.sha256(selected["port_trace"].read_bytes()).hexdigest()}
        packet.update(schema=compare.STOCK_DECREMENT_EXPECTATION_SCHEMA,
                      scope=compare.V10_FIRST_STOCK_DECREMENT_SCOPE, version=1)
        selected["match_clock_boundary_audit"] = directory / "clock2000-audit.json"
        selected["stock_decrement_boundary_audit"] = directory / "next-audit.json"
        fixture["packet_path"].write_text(json.dumps(packet))
        stat = compare._file_stat_identity(selected["reference"])
        source_identity = {"trace_bytes": stat["bytes"], "recorded_full_trace_sha256": source["trace"]["recorded_full_sha256"],
                           "full_trace_rehashed": False, "manifest_sha256": source["manifest"]["sha256"],
                           "source_report_sha256": source["report"]["sha256"], "audit_sha256": source["audit"]["sha256"],
                           "audit_records_decoded": 11, "audit_bytes_read": 100}
        browser_identity = {"required_cursor": terminal_tuple["browser_cursor"], "target_cursor": terminal_tuple["browser_cursor"],
                            "observed_cursor": terminal_tuple["browser_cursor"], "requested_cursor": terminal_tuple["browser_cursor"],
                            "exported_cursor": exported_cursor, "capture_report_sha256": "1" * 64,
                            "producer_manifest_sha256": "2" * 64, "browser_report_sha256": "3" * 64, "port_trace_sha256": "4" * 64}
        yielded = []
        def records(path, *, max_bytes, max_records, stats):
            self.assertEqual((max_bytes, max_records), (268435456, 48000))
            for row, raw in zip(fixture["rows"], raw_records):
                stats.record_bytes(raw)
                stats.records_read += 1
                yielded.append(row["seq"])
                yield row
        def browser_provenance(*args, **kwargs):
            compare._validate_v10_browser_export(selected["port_trace"], fixture["recipe"], exported_cursor, packet)
            return {}, {}, {}, browser_identity
        with (mock.patch.object(compare, "_validate_v10_source_provenance", return_value=(fixture["source_manifest"], {}, fixture["source_audit"], source_identity)),
              mock.patch.object(compare, "_validate_v10_browser_provenance", side_effect=browser_provenance),
              mock.patch.object(compare, "_validate_first_positive_audit", return_value=(fixture["positive_audit"], source["positive_boundary_audit"]["sha256"])),
              mock.patch.object(compare, "_validate_clock60_audit", return_value=(fixture["clock_audit"], source["clock60_boundary_audit"]["sha256"])),
              mock.patch.object(compare, "_validate_match_clock_boundary_audit", return_value=({"observed": {}}, clock_audit["sha256"])),
              mock.patch.object(compare, "_validate_stock_decrement_boundary_audit", return_value=({"observed": {"source_prefix": terminal_prefix}}, boundary["audit"]["sha256"])),
              mock.patch.object(compare, "iter_records", side_effect=records)):
            result = compare.compare_paths(
                selected["reference"], selected["recipe"], selected["port_trace"], scope=compare.V10_FIRST_STOCK_DECREMENT_SCOPE,
                expectations=fixture["packet_path"], source_manifest=selected["source_manifest"],
                source_report=selected["source_report"], source_audit=selected["source_audit"],
                browser_capture_report=selected["browser_capture_report"], browser_producer_manifest=selected["browser_producer_manifest"],
                browser_report=selected["browser_report"], positive_boundary_audit=selected["positive_boundary_audit"],
                clock60_boundary_audit=selected["clock60_boundary_audit"], match_clock_boundary_audit=selected["match_clock_boundary_audit"],
                stock_decrement_boundary_audit=selected["stock_decrement_boundary_audit"])
        return result, yielded, accepted, boundary, terminal_prefix

    def test_public_compare_path_reports_both_events_seven_rejoins_and_exact_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            result, yielded, accepted, boundary, prefix = self.compare_fixture(Path(directory))
            self.assertEqual(result["boundary_result"], "equivalent", result)
            self.assertEqual(result["result"], "incomplete")
            self.assertIs(result["complete"], False)
            self.assertIs(result["whole_session_equivalent"], False)
            report = result["stock_decrement_event_boundary"]
            self.assertEqual(report["stocks_after"], boundary["event"]["stocks_after"])
            self.assertEqual(report["source_tick"], boundary["event"]["target"]["source_tick"])
            self.assertEqual(report["accepted_stock_checkpoint"]["event"], accepted["boundary"]["event"])
            self.assertEqual([item["label"] for item in report["fresh_prefix_checkpoints"]],
                             ["clock1", "clock60", "clock300", "clock500", "clock1000", "clock2000", "accepted_stock"])
            self.assertTrue(all(item["rejoined_before_continuing"] is True for item in report["fresh_prefix_checkpoints"]))
            self.assertIs(report["first_stock_decrement_only"], False)
            self.assertIn("after the accepted stock event", report["event_definition"])
            self.assertIn("after the accepted stock event", result["capture_status"])
            self.assertIn("after the accepted stock event", result["source_prefix"]["hash_basis"])
            self.assertNotIn("source_ticks_through_first_stock_decrement", result["checks"])
            self.assertEqual(result["checks"]["source_ticks_through_first_stock_decrement_after_accepted_event"], "pass")
            self.assertEqual(result["checks"]["accepted_stock_event_rejoin"], "pass")
            self.assertEqual(yielded[-1], prefix["last_source_sequence"])
            self.assertEqual(len(yielded), prefix["records_read"])
            self.assertEqual(result["source_prefix"]["sha256"], prefix["sha256"])
            self.assertEqual(result["timeline_frames_consumed"], boundary["event"]["target"]["cursor_after_frame"])

    def test_public_compare_path_rejects_malformed_and_extra_trailing_browser_rows(self):
        for corrupt_tail in ("bool_index", "extra_row"):
            with self.subTest(corrupt_tail=corrupt_tail), tempfile.TemporaryDirectory() as directory:
                result, yielded, _, _, _ = self.compare_fixture(Path(directory), corrupt_tail=corrupt_tail)
                self.assertEqual(result["result"], "invalid", result)
                expected_error = ("browser scene/index is missing, extra, or reordered" if corrupt_tail == "bool_index"
                                  else "browser trace contains records after the exported prefix")
                self.assertEqual(result["error"], expected_error)
                self.assertEqual(result["checks"]["source_provenance"], "pass")
                self.assertIsNone(result["boundary_result"])
                self.assertFalse(result["complete"])
                self.assertFalse(result["whole_session_equivalent"])
                self.assertEqual(yielded, [])

    def test_accepted_receipts_require_actual_audit_prefix_and_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = fixtures.WholeSessionStateCompareTests()._stock_decrement_audit_fixture(directory)
            packet, report_path, recipe, prior_audit, stat = fixture
            prior = packet["source"]["stock_decrement_boundary"]
            audit = json.loads(report_path.read_text())
            prefix = {key: audit["observed"]["source_prefix"][key] for key in compare.ORDERED_CLOCK_PREFIX_FIELDS}
            event = prior["event"]
            comparison = {"scope": compare.V10_FIRST_STOCK_DECREMENT_SCOPE,
                          "boundary_result": "equivalent", "complete": False, "whole_session_equivalent": False,
                          "checks": {"stock_decrement_event_state": "pass", "ordered_clock_checkpoint_states": "pass"},
                          "stock_decrement_event_boundary": {
                              **compare._stock_decrement_tuple(event["target"]),
                              **{key: event[key] for key in ("stocks_before", "stocks_after", "changed_slots")},
                              "source_prefix_sha256": prefix["sha256"], "source_prefix_bytes": prefix["bytes_read"],
                              "source_prefix_records": prefix["records_read"], "audit_sha256": prior["audit"]["sha256"]}}
            path = Path(directory) / "comparison.json"
            accepted = {"boundary": prior, "prefix": prefix, "comparison": write_identity(path, comparison)}
            compare._validate_accepted_stock_receipts(accepted, packet, recipe, prior_audit, source_stat_before=stat)
            for field, value in (("source_prefix_records", True), ("audit_sha256", "0" * 64),
                                 ("source_sequence", 1), ("stocks_after", [4, 4, 4, 4])):
                with self.subTest(field=field):
                    bad = copy.deepcopy(comparison)
                    bad["stock_decrement_event_boundary"][field] = value
                    accepted["comparison"] = write_identity(path, bad)
                    with self.assertRaises(compare.ComparisonError):
                        compare._validate_accepted_stock_receipts(accepted, packet, recipe, prior_audit)


if __name__ == "__main__":
    unittest.main()
