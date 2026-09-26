"""Synthetic coverage for offline browser failure summaries."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))

from browser_failure_summary import (  # noqa: E402
    SummaryError,
    build_summary,
    write_summary,
)
from summarize_browser_failure import main  # noqa: E402


def _write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def _report(*, first_error=None, browser_errors=None, cursor=12, at_ms=20,
            result="fail", deliberate=None, runtime_error=None):
    return {
        "result": result,
        "scope": "synthetic bounded browser replay; no acceptance claim",
        "mode": "state",
        "first_error": first_error,
        "browser_errors": browser_errors or [],
        "snapshots": [{"source_cursor": cursor, "at_ms": at_ms - 1}],
        "final_snapshot": {
            "source_cursor": cursor,
            "at_ms": at_ms,
            "runtime_error": runtime_error,
            "replay_report": {
                "complete": False,
                "input_scheduling": "per_tick",
                "failures": ["whole-session final CSS was not entered"],
            },
        },
        "deliberate_prefix_stop": deliberate,
        "inputs": {
            "disc": {"bytes": 10, "sha256": "a" * 64, "path": "private-inputs/disc.iso"},
            "recipe": {"bytes": 20, "sha256": "b" * 64, "path": "private-inputs/recipe.mwrc"},
        },
        "browser": {"executable": "Google Chrome", "version": "123.0"},
    }


def _trace_frame(index=11, match_frame=37, fighters=None):
    return {
        "record": "session_frame",
        "scene": 3,
        "index": index,
        "match_frame": match_frame,
        "fighters": fighters if fighters is not None else [
            {"slot": 0, "kind": 24, "motion": 341, "animation": 46,
             "ground_air": 0, "stocks": 4},
        ],
    }


def _write_trace(path: Path, rows) -> None:
    path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


class BrowserFailureSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="browser-failure-summary-")
        self.root = Path(self.temporary.name)
        self.run = self.root / "run"
        self.run.mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def output(self, name="summary"):
        return self.root / name

    def test_runtime_error_remains_primary_when_cleanup_times_out(self):
        runtime = {
            "kind": "pageerror",
            "phase": "whole-session-replay",
            "message": "RuntimeError: memory access out of bounds\n    at wasm.draw_path (game.wasm:1)",
        }
        cleanup = "locator.click: Timeout 120000ms exceeded\n  waiting for locator('#unload')"
        _write_json(self.run / "report.json", _report(first_error=runtime,
                                                       browser_errors=[runtime]))
        (self.run / "failure.txt").write_text(cleanup, encoding="utf-8")
        summary = write_summary(self.run, self.output())
        self.assertEqual(summary["primary_runtime_error"]["message"].splitlines()[0],
                         "RuntimeError: memory access out of bounds")
        self.assertEqual(summary["follow_up_failures"][0]["category"], "cleanup_timeout")
        self.assertEqual(summary["follow_up_failures"][0]["ordering"], "unknown")
        self.assertEqual(summary["run_outcome"]["classification"], "runtime_error_recorded")
        self.assertTrue((self.output() / "summary.json").exists())
        self.assertNotIn("private-inputs", (self.output() / "summary.json").read_text())

    def test_missing_optional_files_still_produces_summary(self):
        _write_json(self.run / "progress.json", {"cursor": 9, "at_ms": 10})
        summary = write_summary(self.run, self.output())
        self.assertIsNone(summary["primary_runtime_error"])
        self.assertEqual(summary["last_progress"]["source_cursor"]["value"], 9)
        self.assertIn("report.json: no browser-level event summary", summary["missing_evidence"])
        self.assertEqual(summary["run_outcome"]["classification"], "outcome_unknown")

    def test_deliberate_bounded_stop_is_not_inferred_to_be_a_crash(self):
        report = _report(
            first_error=None,
            deliberate={"requested_cursor": 80, "observed_cursor": 82,
                        "reason": "bounded diagnostic prefix"},
            runtime_error="bounded prefix stop reached",
        )
        report["final_snapshot"]["replay_report"]["failures"] = [
            "incomplete input timeline", "source tick/draw count mismatch",
        ]
        _write_json(self.run / "report.json", report)
        _write_json(self.run / "retail-browser-report.json", {
            "complete": False,
            "pass": False,
            "failures": ["incomplete input timeline", "source tick/draw count mismatch"],
        })
        summary = build_summary(self.run, self.output())
        self.assertIsNone(summary["primary_runtime_error"])
        self.assertEqual(summary["runtime_error_candidates"], [])
        self.assertEqual(summary["run_outcome"]["classification"], "deliberate_bounded_stop")

    def test_timed_cursor_disagrees_with_untimestamped_final_snapshot(self):
        report = _report(cursor=20, at_ms=30)
        report["snapshots"] = [{"source_cursor": 10, "at_ms": 1}]
        report["final_snapshot"].pop("at_ms")
        _write_json(self.run / "report.json", report)
        cursor = build_summary(self.run, self.output())["last_progress"]["source_cursor"]
        self.assertIsNone(cursor["value"])
        self.assertEqual(cursor["ordering"], "conflicting_with_untimestamped_observation")
        self.assertEqual({item["value"] for item in cursor["observations"]}, {10, 20})

    def test_multiple_untimestamped_snapshots_preserve_conflict(self):
        report = _report()
        report["snapshots"] = [
            {"source_cursor": 10}, {"source_cursor": 20}, {"source_cursor": 10},
        ]
        report.pop("final_snapshot")
        _write_json(self.run / "report.json", report)
        cursor = build_summary(self.run, self.output())["last_progress"]["source_cursor"]
        self.assertIsNone(cursor["value"])
        self.assertEqual(cursor["ordering"], "conflicting_without_timestamps")
        self.assertEqual({item["value"] for item in cursor["observations"]}, {10, 20})

    def test_source_cursor_and_match_frame_remain_distinct_from_session_index(self):
        _write_json(self.run / "report.json", _report(cursor=20, at_ms=30))
        _write_trace(self.run / "retail-port.jsonl", [
            {"record": "header", "schema": "synthetic"},
            _trace_frame(index=18, match_frame=703),
        ])
        summary = build_summary(self.run, self.output())
        progress = summary["last_progress"]
        self.assertEqual(progress["source_cursor"]["value"], 20)
        self.assertEqual(progress["session_frame_index"], 18)
        self.assertIsNone(progress["match_index"]["value"])
        self.assertEqual(progress["match_frame"]["value"], 703)

    def test_incomplete_final_jsonl_line_is_reported_and_prior_context_retained(self):
        path = self.run / "retail-port.jsonl"
        path.write_text(json.dumps({"record": "header"}) + "\n" +
                        json.dumps(_trace_frame(index=3, match_frame=9)) + "\n" +
                        '{"record":"session_frame","index":4', encoding="utf-8")
        summary = build_summary(self.run, self.output())
        self.assertEqual(summary["jsonl"]["truncated_final_line"], 3)
        self.assertEqual(summary["last_progress"]["session_frame_index"], 3)
        self.assertIn("incomplete final line 3", " ".join(summary["missing_evidence"]))

    def test_malformed_interior_jsonl_record_is_rejected(self):
        (self.run / "retail-port.jsonl").write_text(
            '{"record":"header"}\nnot-json\n{"record":"session_frame"}\n',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(SummaryError, "retail-port.jsonl:2: malformed interior record"):
            build_summary(self.run, self.output())

    def test_conflicting_runtime_error_order_is_left_unknown(self):
        first = {"kind": "pageerror", "message": "Error B"}
        listed = [{"kind": "pageerror", "message": "Error A"}, first]
        _write_json(self.run / "report.json", _report(first_error=first,
                                                       browser_errors=listed))
        summary = build_summary(self.run, self.output())
        self.assertIsNone(summary["primary_runtime_error"])
        self.assertEqual(summary["runtime_error_order"], "conflicting_order")
        self.assertTrue(summary["conflicting_observations"])

    def test_multiple_runtime_errors_without_ordering_metadata_stay_unordered(self):
        _write_json(self.run / "report.json", _report(
            browser_errors=[{"kind": "pageerror", "message": "Error A"},
                            {"kind": "pageerror", "message": "Error B"}],
        ))
        summary = build_summary(self.run, self.output())
        self.assertIsNone(summary["primary_runtime_error"])
        self.assertEqual(summary["runtime_error_order"], "ordering_unknown")
        self.assertTrue(any("runtime error chronology" in item
                            for item in summary["missing_evidence"]))

    def test_retail_report_only_exposes_runtime_failure_candidate(self):
        _write_json(self.run / "retail-browser-report.json", {
            "complete": False,
            "pass": False,
            "failures": ["memory access out of bounds"],
        })
        summary = write_summary(self.run, self.output())
        self.assertEqual(summary["run_outcome"]["result"], "fail")
        self.assertIsNone(summary["primary_runtime_error"])
        self.assertEqual(summary["runtime_error_order"], "ordering_unknown")
        candidate = summary["runtime_error_candidates"][0]
        self.assertEqual(candidate["evidence"]["artifact"], "retail-browser-report.json")
        self.assertEqual(candidate["evidence"]["field"], "failures[0]")
        markdown = (self.output() / "summary.md").read_text()
        self.assertIn("retail-browser-report.json:failures[0]", markdown)
        self.assertIn("memory access out of bounds", markdown)
        self.assertNotIn("No runtime error is recorded", markdown)

    def test_cursor_conflict_at_same_timestamp_is_not_resolved_by_field_priority(self):
        report = _report(cursor=14, at_ms=20)
        report["snapshots"] = [{"source_cursor": 13, "at_ms": 20}]
        _write_json(self.run / "report.json", report)
        _write_json(self.run / "progress.json", {"cursor": 12, "at_ms": 20})
        summary = build_summary(self.run, self.output())
        self.assertIsNone(summary["last_progress"]["source_cursor"]["value"])
        self.assertEqual(summary["last_progress"]["source_cursor"]["ordering"],
                         "conflicting_at_latest_timestamp")

    def test_item_context_is_included_only_when_diagnostics_are_present(self):
        _write_json(self.run / "report.json", _report())
        page = self.run / "page.txt"
        page.write_text(
            "ITEMDRAW-META cursor=12 serial=7 kind=114 owner=0xa47d6a0\n"
            "ITEMDRAW-OWNER cursor=12 serial=7 gobj=0xa47d6a0 class=0004 p_link=8 user_data=0xa47d700\n",
            encoding="utf-8",
        )
        present = build_summary(self.run, self.output())
        item = present["item_context"]["records"][-1]
        self.assertEqual(item["kind"], "114")
        self.assertEqual(item["class"], "0004")
        self.assertEqual(item["p_link"], "8")

        page.unlink()
        absent = build_summary(self.run, self.output("summary-absent"))
        self.assertEqual(absent["item_context"]["status"], "not_recorded")
        self.assertIn("this does not mean no items existed", " ".join(absent["missing_evidence"]))

    def test_output_cannot_overlap_input_or_overwrite_existing_directory(self):
        with self.assertRaisesRegex(SummaryError, "overlap"):
            build_summary(self.run, self.run / "summary")
        existing = self.output()
        existing.mkdir()
        with self.assertRaisesRegex(SummaryError, "already exists"):
            build_summary(self.run, existing)

    def test_artifact_symlink_cannot_read_outside_the_selected_run(self):
        outside = self.root / "outside-report.json"
        _write_json(outside, _report())
        (self.run / "report.json").symlink_to(outside)
        with self.assertRaisesRegex(SummaryError, "inside the run directory"):
            build_summary(self.run, self.output())

    def test_cli_returns_success_for_a_summarized_gameplay_failure(self):
        _write_json(self.run / "report.json", _report(result="fail"))
        with redirect_stdout(io.StringIO()):
            status = main([str(self.run), "--out", str(self.output())])
        self.assertEqual(status, 0)
        self.assertTrue((self.output() / "summary.md").exists())


if __name__ == "__main__":
    unittest.main()
