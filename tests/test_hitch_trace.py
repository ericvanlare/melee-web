"""Focused offline tests for Chrome hitch trace correlation."""

from __future__ import annotations

import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from hitch_trace import (DEFAULT_MAX_DECOMPRESSED_BYTES, HitchTraceError,
                         analyze_capture)  # noqa: E402


OFFSET_US = 500_000.0


def mark(name: str, ts: float, pid: int = 1, tid: int = 10) -> dict:
    return {"name": name, "cat": "blink.user_timing", "ph": "R", "ts": ts,
            "pid": pid, "tid": tid}


def task(name: str, ts: float, dur: float, *, pid: int = 1, tid: int = 10,
         tdur: float | None = None, cat: str = "toplevel") -> dict:
    value = {"name": name, "cat": cat, "ph": "X", "ts": ts, "dur": dur,
             "pid": pid, "tid": tid}
    if tdur is not None:
        value["tdur"] = tdur
    return value


def native_event(identifier: str = "native-1", start: float = 100.0) -> dict:
    return {
        "id": identifier, "kind": "native_deadline", "native_started": start,
        "duration_ms": 20.0, "user_timing": {"name": f"melee-hitch-{identifier}",
                                                "start_time": start},
        "native": {"current": {"started": start, "total_ms": 22.0,
                                  "input_ms": 2.0, "draw_ms": 4.0}},
    }


def browser_event(identifier: str = "browser-1", start: float = 200.0,
                  end: float = 240.0) -> dict:
    return {
        "id": identifier, "kind": "browser_gap",
        "browser": {"started": start, "ended": end},
        "user_timing": {"name": f"melee-hitch-{identifier}", "start_time": start},
    }


def report(*events: dict) -> dict:
    return {"diagnostic_capture": {"events": list(events)}}


class HitchTraceTests(unittest.TestCase):
    def base_trace(self, *events: dict) -> dict:
        return {"traceEvents": list(events)}

    def test_native_and_browser_overlap_and_nested_tasks(self):
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000),
            mark("melee-hitch-browser-1", OFFSET_US + 200_000),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 99_000, 25_000,
                 tdur=17_000),
            task("FunctionCall", OFFSET_US + 105_000, 5_000, tdur=3_000,
                 cat="devtools.timeline"),
            task("V8.GCScavenger", OFFSET_US + 110_000, 2_000, tdur=1_000,
                 cat="v8"),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 199_000, 42_000,
                 tdur=31_000),
        )
        result = analyze_capture(report(native_event(), browser_event()), trace, top_n=8)
        native, browser = result["events"]
        self.assertEqual(result["event_count"], 2)
        self.assertEqual(native["status"], "correlated")
        self.assertEqual(native["marker"]["pid"], 1)
        self.assertEqual(native["report_interval"]["duration_ms"], 22.0)
        self.assertEqual(len(native["enclosing_renderer_tasks"]), 1)
        self.assertAlmostEqual(native["renderer_tasks"]["items"][0]["thread_cpu_ms"], 17.0)
        self.assertEqual(len(native["gc_slices"]), 1)
        # Parent/child CPU durations remain individual evidence; no aggregate
        # is emitted that could accidentally add nested CPU intervals.
        self.assertNotIn("thread_cpu_total_ms", native)
        self.assertEqual(browser["status"], "correlated")
        self.assertEqual(browser["report_interval"]["duration_ms"], 40.0)

    def test_marker_wrong_thread_is_explicit_and_does_not_borrow_task(self):
        event = native_event()
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000, pid=2, tid=99),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 99_000, 25_000,
                 pid=1, tid=10),
        )
        item = analyze_capture(report(event), trace)["events"][0]
        self.assertEqual(item["status"], "wrong_thread")
        self.assertEqual(item["renderer_tasks"]["available_count"], 0)
        self.assertEqual(item["alignment"]["status"], "wrong_thread")

    def test_cache_intervals_join_offline_without_claiming_causality_or_phase_offsets(self):
        event = native_event()
        event['native']['current']['begin_phases'] = {'frame_slot_ms': 17, 'frame_slot_wait_count': 2}
        value = report(event)
        capture = value['diagnostic_capture']
        capture.update(clock={'source': 'performance.now'},
                       capabilities={'cache_sync': {'installed': True, 'requested': True, 'enabled': True}},
                       cache_sync_overflow_count=0,
                       cache_syncs=[
                           {'id': 'one', 'clock': 'performance.now', 'started': 105, 'ended': 118,
                            'status': 'completed', 'stack': 'fd_sync'},
                           {'id': 'outside', 'clock': 'performance.now', 'started': 122, 'ended': 140,
                            'status': 'completed'},
                       ])
        item = analyze_capture(value, self.base_trace())['events'][0]
        self.assertEqual(item['status'], 'missing_marker')
        # Page-clock overlap survives missing trace alignment but is not a trace attribution.
        overlap = item['cache_sync_overlap']
        self.assertEqual(overlap['status'], 'available')
        self.assertEqual(len(overlap['items']), 1)
        self.assertEqual(overlap['items'][0]['overlap_ms'], 13)
        self.assertEqual(overlap['items'][0]['sync']['stack'], 'fd_sync')
        self.assertEqual(item['native_begin_partition']['values']['frame_slot_wait_count'], 2)
        self.assertTrue(all(phase['start_ms'] is None for phase in item['native_phases']))

        capture['cache_syncs'].append({'id': 'pending', 'clock': 'performance.now',
                                      'started': 90, 'ended': None, 'status': 'pending'})
        overlap = analyze_capture(value, self.base_trace())['events'][0]['cache_sync_overlap']
        self.assertEqual(overlap['status'], 'incomplete')
        self.assertIsNone(overlap['items'][1]['overlap_ms'])
        self.assertEqual(overlap['items'][1]['overlap_status'], 'possible_pending')
        capture['clock']['source'] = 'unknown'
        self.assertEqual(analyze_capture(value, self.base_trace())['events'][0]
                         ['cache_sync_overlap']['status'], 'unavailable')

    def test_duplicate_marker_alignment_is_ambiguous(self):
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000),
            mark("melee-hitch-native-1", OFFSET_US + 130_000),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 99_000, 25_000),
        )
        item = analyze_capture(report(native_event()), trace)["events"][0]
        self.assertEqual(item["status"], "ambiguous_alignment")
        self.assertEqual(item["marker"]["status"], "ambiguous")
        self.assertIsNone(item["alignment"]["offset_us"])

    def test_missing_thread_clock_is_unknown(self):
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 99_000, 25_000),
        )
        item = analyze_capture(report(native_event()), trace)["events"][0]
        self.assertEqual(item["thread_cpu"]["status"], "unknown")
        self.assertIsNone(item["renderer_tasks"]["items"][0]["thread_cpu_ms"])

    def test_user_timing_fallback_without_start_time_is_not_aligned_by_native_clock(self):
        event = native_event()
        event["user_timing"] = {"name": "melee-hitch-native-1", "start_time": None,
                                "requested_start_time": 100.0}
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000),
            task("ThreadControllerImpl::RunTask", OFFSET_US + 99_000, 25_000,
                 tdur=17_000),
        )
        item = analyze_capture(report(event), trace)["events"][0]
        self.assertEqual(item["status"], "missing_marker")
        self.assertEqual(item["marker"]["status"], "missing")
        self.assertEqual(item["marker"].get("reason"), "user_timing start_time unavailable")
        self.assertIsNone(item["trace_interval"])

    def test_arbitrary_argument_names_do_not_establish_thread_cpu_time(self):
        fake = task('ThreadControllerImpl::RunTask', OFFSET_US + 99_000, 25_000)
        fake['args'] = {'thread_cpu_us': 100, 'data': {'tdur': 100}}
        trace = self.base_trace(mark('melee-hitch-native-1', OFFSET_US + 100_000), fake)
        result = analyze_capture(report(native_event()), trace)
        self.assertEqual(result['events'][0]['thread_cpu']['status'], 'unknown')

    def test_loss_metadata_and_explicit_context_truncation(self):
        trace = self.base_trace(
            mark("melee-hitch-native-1", OFFSET_US + 100_000),
            *[task(f"ThreadControllerImpl::RunTask-{i}", OFFSET_US + 99_000 + i * 100,
                   25_000, tdur=1_000) for i in range(5)],
        )
        result = analyze_capture(report(native_event()), trace,
                                 {"complete": True, "dataLossOccurred": True, "truncated": True}, top_n=2)
        self.assertEqual(result["trace"]["completeness"]["status"], "incomplete")
        self.assertTrue(result["trace"]["completeness"]["data_loss_occurred"])
        context = result["events"][0]["renderer_tasks"]
        self.assertTrue(context["truncated"])
        self.assertEqual(context["available_count"], 5)
        self.assertEqual(context["omitted_count"], 3)
        self.assertEqual(result["event_count"], 1)

    def test_malformed_trace_rejected_and_gzip_bound_is_enforced(self):
        with self.assertRaisesRegex(HitchTraceError, "Malformed trace"):
            analyze_capture(report(native_event()), b"{not json")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.json.gz"
            with gzip.open(path, "wb") as stream:
                stream.write(json.dumps(self.base_trace()).encode())
            with self.assertRaisesRegex(HitchTraceError, "exceeds"):
                analyze_capture(report(native_event()), path, max_decompressed_bytes=4)

    def test_trace_bound_cannot_be_raised_above_512_mib(self):
        with self.assertRaisesRegex(HitchTraceError, "cannot exceed"):
            analyze_capture(report(native_event()), {"traceEvents": []},
                            max_decompressed_bytes=DEFAULT_MAX_DECOMPRESSED_BYTES + 1)

    def test_empty_capture_exposes_missing_thread_cpu_evidence(self):
        result = analyze_capture(report(), {"traceEvents": []})
        self.assertEqual(result["event_count"], 0)
        self.assertEqual(result["capture_evidence"]["thread_cpu"]["status"], "unknown")


if __name__ == "__main__":
    unittest.main()
