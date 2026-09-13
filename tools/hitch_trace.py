"""Offline correlation of bounded hitch reports with a Chrome trace.

This module intentionally reports evidence and unknowns.  It does not classify a
hitch as scheduler preemption, and it never treats a wall interval as CPU or
GPU execution.  Chrome's optional ``tdur`` field is copied as thread CPU time
when it is present; in its absence CPU duration is explicitly unknown.

The public entry point is :func:`analyze_capture`.  ``report`` may be the
browser report object or its ``diagnostic_capture`` member.  ``trace`` may be
a decoded trace mapping, a path, or bytes.  Trace paths ending in ``.gz`` are
stream-decompressed with a 512 MiB default bound.
"""

from __future__ import annotations

from collections import defaultdict
import gzip
import json
import math
from pathlib import Path
from statistics import median
from typing import Any, Iterable, Mapping, Sequence
import zlib


DEFAULT_MAX_DECOMPRESSED_BYTES = 512 * 1024 * 1024
DEFAULT_TOP_N = 12
# Trace timestamps and the injected performance.mark startTime are both
# monotonic clocks.  Keep this deliberately tight: a multi-millisecond window
# could silently associate a repeated marker from a later runtime.  Larger
# drift remains an explicit ambiguous alignment.
_ALIGN_TOLERANCE_US = 100.0
_EPSILON_US = 0.001


class HitchTraceError(ValueError):
    """The report, trace, or trace metadata cannot be interpreted safely."""


def _finite(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return number if math.isfinite(number) else None


def _integer(value: Any) -> int | None:
    number = _finite(value)
    if number is None or number != int(number):
        return None
    return int(number)


def _as_object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise HitchTraceError(f"{label} must be a JSON object")
    return value


def _json_load(data: bytes | str, label: str) -> Any:
    try:
        try:
            import orjson  # type: ignore
        except ImportError:
            orjson = None
        if orjson is not None:
            return orjson.loads(data)
        return json.loads(data)
    except Exception as error:  # JSONDecodeError and orjson.JSONDecodeError differ.
        raise HitchTraceError(f"Malformed {label}: {error}") from error


def _read_bounded(path: Path, limit: int) -> bytes:
    if limit <= 0:
        raise HitchTraceError("max_decompressed_bytes must be positive")
    if limit > DEFAULT_MAX_DECOMPRESSED_BYTES:
        raise HitchTraceError(
            f"max_decompressed_bytes cannot exceed {DEFAULT_MAX_DECOMPRESSED_BYTES}"
        )
    try:
        if path.suffix == ".gz" or path.name.endswith(".json.gz") or path.name.endswith(".orjson.gz"):
            source = gzip.open(path, "rb")
        else:
            source = path.open("rb")
    except OSError as error:
        raise HitchTraceError(f"Cannot read trace {path}: {error}") from error
    chunks: list[bytes] = []
    size = 0
    try:
        while True:
            chunk = source.read(min(1024 * 1024, limit + 1 - size))
            if not chunk:
                break
            size += len(chunk)
            if size > limit:
                raise HitchTraceError(
                    f"Trace decompressed data exceeds {limit} bytes (trace is bounded offline input)"
                )
            chunks.append(chunk)
    except (OSError, EOFError, zlib.error) as error:
        raise HitchTraceError(f"Malformed or unreadable trace {path}: {error}") from error
    finally:
        source.close()
    return b"".join(chunks)


def load_trace(trace: Any, *, max_decompressed_bytes: int = DEFAULT_MAX_DECOMPRESSED_BYTES) -> Mapping[str, Any]:
    """Load a CDP trace JSON object from a mapping, bytes, or JSON/gzip path."""

    if max_decompressed_bytes <= 0:
        raise HitchTraceError("max_decompressed_bytes must be positive")
    if max_decompressed_bytes > DEFAULT_MAX_DECOMPRESSED_BYTES:
        raise HitchTraceError(
            f"max_decompressed_bytes cannot exceed {DEFAULT_MAX_DECOMPRESSED_BYTES}"
        )
    if isinstance(trace, Mapping):
        value = trace
    elif isinstance(trace, (bytes, bytearray, memoryview)):
        raw = bytes(trace)
        if len(raw) > max_decompressed_bytes:
            raise HitchTraceError("Trace bytes exceed max_decompressed_bytes")
        value = _json_load(raw, "trace")
    else:
        path = Path(trace)
        value = _json_load(_read_bounded(path, max_decompressed_bytes), "trace")
    return _as_object(value, "trace")


def load_metadata(metadata: Any, *, max_decompressed_bytes: int = 16 * 1024 * 1024) -> Mapping[str, Any] | None:
    """Load optional ``trace-metadata.json`` sidecar data."""

    if metadata is None:
        return None
    if isinstance(metadata, Mapping):
        return metadata
    if isinstance(metadata, (bytes, bytearray, memoryview)):
        raw = bytes(metadata)
        if len(raw) > max_decompressed_bytes:
            raise HitchTraceError("Trace metadata exceeds max_decompressed_bytes")
        return _as_object(_json_load(raw, "trace metadata"), "trace metadata")
    path = Path(metadata)
    return _as_object(_json_load(_read_bounded(path, max_decompressed_bytes), "trace metadata"),
                      "trace metadata")


def _trace_events(trace: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    raw = trace.get("traceEvents", trace.get("trace_events"))
    if not isinstance(raw, list):
        raise HitchTraceError("trace.traceEvents must be an array")
    result: list[Mapping[str, Any]] = []
    for index, event in enumerate(raw):
        if not isinstance(event, Mapping):
            raise HitchTraceError(f"traceEvents[{index}] must be an object")
        result.append(event)
    return result


def _event_start_us(event: Mapping[str, Any]) -> float | None:
    return _finite(event.get("ts", event.get("timestamp_us")))


def _event_duration_us(event: Mapping[str, Any]) -> float | None:
    duration = _finite(event.get("dur", event.get("duration_us")))
    return duration if duration is not None and duration >= 0 else None


def _event_thread(event: Mapping[str, Any]) -> tuple[int | None, int | None]:
    return _integer(event.get("pid")), _integer(event.get("tid"))


def _event_cpu_us(event: Mapping[str, Any]) -> float | None:
    """Read an actual thread-clock field; never derive it from wall duration."""
    # Chrome's trace-event thread duration has a defined microsecond unit.
    # Arbitrarily named argument fields do not establish a CPU clock or unit.
    value = _finite(event.get("tdur"))
    return value if value is not None and value >= 0 else None


def _event_name(event: Mapping[str, Any]) -> str:
    value = event.get("name")
    return value if isinstance(value, str) else ""


def _mark_name(event: Mapping[str, Any]) -> str:
    name = _event_name(event)
    if name.startswith("melee-hitch-"):
        return name
    args = event.get("args")
    if isinstance(args, Mapping):
        for value in (args.get("name"), _nested(args, "data", "name")):
            if isinstance(value, str) and value.startswith("melee-hitch-"):
                return value
    return name


def _is_mark(event: Mapping[str, Any]) -> bool:
    name = _mark_name(event)
    if not name.startswith("melee-hitch-"):
        return False
    # UserTiming marks have appeared as R, I/i, and X in different Chrome
    # tracing versions.  An exact name is the authoritative signal; category
    # and phase are only used to reject an obvious duration task.
    return event.get("ph") not in {"X", "B", "E"} or _event_duration_us(event) in (None, 0)


def _mark_page_time(event: Mapping[str, Any], report_event: Mapping[str, Any]) -> float | None:
    # ``hitch-capture`` records a User Timing fallback as
    # ``start_time: null`` when the browser rejects the requested mark
    # timestamp.  That mark was emitted at observation time, so native_started
    # (or browser.started) is not a valid page-clock anchor for it.  Once the
    # report has a user_timing member, an unavailable start time is therefore
    # an explicit unknown rather than a reason to guess.
    if "user_timing" in report_event:
        timing = report_event.get("user_timing")
        if not isinstance(timing, Mapping):
            return None
        value = _finite(timing.get("start_time"))
        return value
    for value in (report_event.get("native_started"),
                  _nested(report_event, "browser", "started"),
                  report_event.get("timestamp")):
        value = _finite(value)
        if value is not None:
            return value
    return None


def _nested(value: Any, *keys: str) -> Any:
    for key in keys:
        if not isinstance(value, Mapping):
            return None
        value = value.get(key)
    return value


def _interval(event: Mapping[str, Any]) -> tuple[float | None, float | None, dict[str, Any]]:
    kind = event.get("kind")
    if kind == "browser_gap":
        start = _finite(_nested(event, "browser", "started"))
        end = _finite(_nested(event, "browser", "ended"))
        if start is None or end is None:
            start = _finite(event.get("started"))
            end = _finite(event.get("ended"))
        if start is None or end is None or end < start:
            return None, None, {"clock": "performance.now", "status": "unknown", "reason": "browser start/end missing or invalid"}
        return start, end, {"clock": "performance.now", "status": "known", "duration_ms": end - start}

    # The event duration is active time (preparation excluded). Correlation
    # spans the complete callback using the native record's total_ms.
    start = _finite(event.get("native_started"))
    if start is None:
        start = _finite(_nested(event, "native", "current", "started"))
    if start is None:
        start = _finite(_nested(event, "native", "started"))
    current = _nested(event, "native", "current")
    if not isinstance(current, Mapping):
        current = event.get("native") if isinstance(event.get("native"), Mapping) else None
    total = _finite(current.get("total_ms")) if isinstance(current, Mapping) else None
    if total is None:
        total = _finite(event.get("total_ms"))
    if total is None:
        total = _finite(_nested(event, "native", "total_ms"))
    if total is None:
        total = _finite(event.get("duration_ms"))
    if start is None or total is None or total < 0:
        return None, None, {"clock": "performance.now", "status": "unknown", "reason": "native started/total interval missing or invalid"}
    active = _finite(event.get("duration_ms"))
    return start, start + total, {
        "clock": "performance.now", "status": "known", "duration_ms": total,
        "active_duration_ms": active,
    }


def _phase_name(name: str, category: str) -> str | None:
    lower = f"{category} {name}".lower()
    if "gc" in lower or "garbage" in lower:
        return "gc"
    known = (
        "runtask", "run task", "threadcontroller", "functioncall", "evaluate",
        "animation frame", "requestanimationframe", "beginmainthreadframe",
        "update layertree", "layout", "paint", "composite", "drawframe",
        "commit", "activate", "schedule style recalculation", "recalculate styles",
    )
    if any(token in lower for token in known):
        return "known_phase"
    return None


def _is_renderer_task(event: Mapping[str, Any]) -> bool:
    if _event_duration_us(event) is None or _event_duration_us(event) <= 0:
        return False
    if event.get("ph") not in {"X", "B"} and "dur" not in event:
        return False
    name = _event_name(event).lower()
    category = str(event.get("cat", "")).lower()
    if name.startswith("melee-hitch-"):
        return False
    return (
        "toplevel" in category or "renderer.scheduler" in category or
        "run_task" in name or "runtask" in name or "threadcontroller" in name or
        name in {"task", "main thread task", "mainthreadtask"}
    )


def _slice(event: Mapping[str, Any], start_us: float, end_us: float, *, phase: str | None = None) -> dict[str, Any]:
    event_start = _event_start_us(event)
    duration = _event_duration_us(event)
    assert event_start is not None and duration is not None
    event_end = event_start + duration
    overlap_start = max(start_us, event_start)
    overlap_end = min(end_us, event_end)
    pid, tid = _event_thread(event)
    cpu = _event_cpu_us(event)
    result: dict[str, Any] = {
        "name": _event_name(event),
        "cat": event.get("cat"),
        "ph": event.get("ph"),
        "pid": pid,
        "tid": tid,
        "ts_us": event_start,
        "dur_us": duration,
        "duration_ms": duration / 1000.0,
        "wall_duration_ms": duration / 1000.0,
        "overlap_start_us": overlap_start,
        "overlap_end_us": overlap_end,
        "overlap_wall_ms": max(0.0, overlap_end - overlap_start) / 1000.0,
        "encloses_interval": event_start <= start_us + _EPSILON_US and event_end + _EPSILON_US >= end_us,
        "thread_cpu_ms": cpu / 1000.0 if cpu is not None else None,
        "thread_cpu_duration_ms": cpu / 1000.0 if cpu is not None else None,
        "thread_cpu_status": "known" if cpu is not None else "unknown",
    }
    if phase is not None:
        result["phase"] = phase
    return result


def _complete_events(events: Sequence[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    """Normalize the common B/E trace form into synthetic complete slices."""

    result = list(events)
    starts: dict[tuple[int | None, int | None, str], list[Mapping[str, Any]]] = defaultdict(list)
    for event in events:
        if event.get("ph") == "B":
            pid, tid = _event_thread(event)
            starts[(pid, tid, _event_name(event))].append(event)
        elif event.get("ph") == "E":
            pid, tid = _event_thread(event)
            key = (pid, tid, _event_name(event))
            stack = starts.get(key)
            if stack:
                begin = stack.pop()
                ts = _event_start_us(begin)
                end = _event_start_us(event)
                if ts is not None and end is not None and end >= ts:
                    merged = dict(begin)
                    merged["ph"] = "X"
                    merged["ts"] = ts
                    merged["dur"] = end - ts
                    result.append(merged)
    return result


def _completeness(trace: Mapping[str, Any], metadata: Mapping[str, Any] | None,
                  events: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    values: dict[str, Any] = {}
    sources: list[str] = []
    embedded = trace.get("metadata") if isinstance(trace.get("metadata"), Mapping) else {}
    for source_name, source in (("trace", trace), ("trace.metadata", embedded),
                                ("metadata", metadata or {})):
        if not isinstance(source, Mapping):
            continue
        for key in ("dataLossOccurred", "data_loss_occurred", "truncated", "traceBufferFull", "overflow", "complete"):
            if key in source:
                values[key] = source[key]
                sources.append(f"{source_name}.{key}")
    loss_events = []
    for event in events:
        name = _event_name(event).lower()
        args = event.get("args")
        if "dataloss" in name or "bufferfull" in name:
            loss_events.append({"name": _event_name(event), "ts_us": _event_start_us(event)})
        if isinstance(args, Mapping):
            for key in ("dataLossOccurred", "data_loss_occurred", "truncated", "traceBufferFull", "overflow"):
                if args.get(key) is True:
                    values[key] = True
                    sources.append(f"event.args.{key}")
    data_loss = values.get("dataLossOccurred", values.get("data_loss_occurred"))
    truncated = values.get("truncated")
    if loss_events:
        data_loss = True
    complete = values.get("complete")
    if data_loss is True or truncated is True or values.get("traceBufferFull") is True or values.get("overflow") is True:
        complete = False
    return {
        "complete": complete if isinstance(complete, bool) else None,
        "status": "complete" if complete is True else "incomplete" if complete is False else "unknown",
        "data_loss_occurred": data_loss if isinstance(data_loss, bool) else None,
        "truncated": truncated if isinstance(truncated, bool) else None,
        "trace_buffer_full": values.get("traceBufferFull") if isinstance(values.get("traceBufferFull"), bool) else None,
        "overflow": values.get("overflow") if isinstance(values.get("overflow"), bool) else None,
        "loss_events": loss_events,
        "metadata_sources": sorted(set(sources)),
        "metadata_present": bool(metadata) or bool(embedded) or any(key in trace for key in values),
    }


def _context(items: Iterable[dict[str, Any]], top_n: int) -> dict[str, Any]:
    ordered = sorted(items, key=lambda item: (-float(item.get("overlap_wall_ms", 0)),
                                               float(item.get("ts_us", 0))))
    selected = ordered[:top_n]
    omitted = max(0, len(ordered) - len(selected))
    return {
        "items": selected,
        "returned_count": len(selected),
        "available_count": len(ordered),
        "limit": top_n,
        "truncated": omitted > 0,
        "omitted_count": omitted,
    }


def _event_mark_name(event: Mapping[str, Any]) -> str:
    timing = event.get("user_timing")
    if isinstance(timing, Mapping) and isinstance(timing.get("name"), str):
        return timing["name"]
    identifier = event.get("id")
    return f"melee-hitch-{identifier}" if identifier is not None else ""


def _cache_sync_overlap(capture: Mapping[str, Any], start_ms: float | None,
                        end_ms: float | None) -> dict[str, Any]:
    """Join page-clock intervals offline; overlap is never causal attribution."""
    result: dict[str, Any] = {
        "status": "unavailable", "items": [],
        "interpretation": "Page-clock overlap only; not CPU time or proof that a sync caused the hitch.",
    }
    rows = capture.get("cache_syncs")
    if rows is None:
        return result
    if not isinstance(rows, list) or len(rows) > 1024:
        raise HitchTraceError("cache_syncs must be a bounded array")
    if (start_ms is None or end_ms is None or end_ms < start_ms
            or _nested(capture, "clock", "source") != "performance.now"):
        result["reason"] = "shared page clock or failed interval unavailable"
        return result
    capability = _nested(capture, "capabilities", "cache_sync")
    if (not isinstance(capability, Mapping) or capability.get("installed") is not True
            or capability.get("requested") is not True or capability.get("enabled") is not True):
        result["reason"] = "cache sync hook installation not established"
        return result
    result["status"] = "incomplete" if capture.get("cache_sync_overflow_count", 0) else "available"
    for raw in rows:
        row = _as_object(raw, "cache_syncs row")
        started, ended = _finite(row.get("started")), _finite(row.get("ended"))
        if row.get("clock") != "performance.now" or started is None:
            result["status"] = "incomplete"
            continue
        if row.get("status") == "pending":
            result["status"] = "incomplete"
            if started < end_ms:
                result["items"].append({"sync": dict(row), "overlap_ms": None,
                                         "overlap_status": "possible_pending"})
            continue
        if ended is None or ended < started:
            result["status"] = "incomplete"
            continue
        overlap = min(end_ms, ended) - max(start_ms, started)
        if overlap > 0:
            result["items"].append({"sync": dict(row), "overlap_ms": overlap,
                                     "overlap_status": "measured"})
    return result


def analyze_capture(report: Mapping[str, Any], trace: Any, metadata: Any = None,
                    *, top_n: int = DEFAULT_TOP_N,
                    max_decompressed_bytes: int = DEFAULT_MAX_DECOMPRESSED_BYTES) -> dict[str, Any]:
    """Correlate every captured hitch event with offline Chrome trace evidence."""

    if not isinstance(top_n, int) or isinstance(top_n, bool) or top_n < 0:
        raise HitchTraceError("top_n must be a non-negative integer")
    report_object = _as_object(report, "report")
    capture = report_object.get("diagnostic_capture", report_object)
    capture = _as_object(capture, "report.diagnostic_capture")
    raw_events = capture.get("events")
    if not isinstance(raw_events, list):
        raise HitchTraceError("report.diagnostic_capture.events must be an array")
    events = [_as_object(value, f"diagnostic_capture.events[{index}]") for index, value in enumerate(raw_events)]
    trace_object = load_trace(trace, max_decompressed_bytes=max_decompressed_bytes)
    metadata_object = load_metadata(metadata)
    raw_trace_events = _trace_events(trace_object)
    trace_events = _complete_events(raw_trace_events)
    marks = [event for event in trace_events if _is_mark(event)]
    marks_by_name: dict[str, list[Mapping[str, Any]]] = defaultdict(list)
    for mark in marks:
        marks_by_name[_mark_name(mark)].append(mark)

    # Build offset clusters from all unique report/mark pairs.  An offset is
    # the conversion from performance.now milliseconds to trace microseconds.
    candidates_by_event: list[list[dict[str, Any]]] = []
    for event in events:
        page_time = _mark_page_time(event, event)
        name = _event_mark_name(event)
        candidates: list[dict[str, Any]] = []
        for mark in marks_by_name.get(name, []):
            ts = _event_start_us(mark)
            if ts is None or page_time is None:
                continue
            pid, tid = _event_thread(mark)
            candidates.append({"event": mark, "offset_us": ts - page_time * 1000.0,
                               "pid": pid, "tid": tid})
        candidates_by_event.append(candidates)

    # Clusters retain PID/TID as part of the alignment. The most supported
    # cluster is selected; ties remain ambiguous instead of silently picking a
    # marker from another thread.
    clusters: list[dict[str, Any]] = []
    for event_index, candidates in enumerate(candidates_by_event):
        for candidate in candidates:
            placed = None
            for cluster in clusters:
                if (candidate["pid"], candidate["tid"]) != (cluster["pid"], cluster["tid"]):
                    continue
                if abs(candidate["offset_us"] - cluster["center_us"]) <= _ALIGN_TOLERANCE_US:
                    placed = cluster
                    break
            if placed is None:
                placed = {"pid": candidate["pid"], "tid": candidate["tid"],
                          "center_us": candidate["offset_us"], "event_indices": set(), "count": 0}
                clusters.append(placed)
            placed["event_indices"].add(event_index)
            placed["count"] += 1
            placed["center_us"] = median([candidate["offset_us"]] + [placed["center_us"]])
    clusters.sort(key=lambda cluster: (-len(cluster["event_indices"]), -cluster["count"], cluster["pid"] or -1, cluster["tid"] or -1))

    # Identify all renderer-like tasks, independent of the marker thread. This
    # lets the result say "wrong thread" while retaining unknown CPU evidence.
    renderer_tasks = [event for event in trace_events if _is_renderer_task(event)]
    result_events: list[dict[str, Any]] = []
    for index, event in enumerate(events):
        name = _event_mark_name(event)
        candidates = candidates_by_event[index]
        marker_result: dict[str, Any] = {
            "name": name or None,
            "status": "missing" if not candidates else "ambiguous",
            "candidate_count": len(candidates),
            "candidates": [
                {"pid": item["pid"], "tid": item["tid"], "ts_us": _event_start_us(item["event"]),
                 "offset_us": item["offset_us"]}
                for item in candidates
            ],
        }
        if "user_timing" in event and _mark_page_time(event, event) is None:
            marker_result["reason"] = "user_timing start_time unavailable"
        selected: dict[str, Any] | None = None
        matching_clusters = []
        for cluster in clusters:
            if index in cluster["event_indices"]:
                matching_clusters.append(cluster)
        if candidates and len(matching_clusters) == 1:
            cluster = matching_clusters[0]
            in_cluster = [candidate for candidate in candidates
                          if (candidate["pid"], candidate["tid"]) == (cluster["pid"], cluster["tid"])
                          and abs(candidate["offset_us"] - cluster["center_us"]) <= _ALIGN_TOLERANCE_US]
            # A duplicate same-name marker on the selected thread is not safe
            # to align, even if the timestamps are close.
            if len(in_cluster) == 1:
                selected = in_cluster[0]
                marker_result.update({
                    "status": "matched", "pid": selected["pid"], "tid": selected["tid"],
                    "ts_us": _event_start_us(selected["event"]),
                    "offset_us": selected["offset_us"],
                })
        start_ms, end_ms, interval_info = _interval(event)
        event_result: dict[str, Any] = {
            "id": event.get("id"), "kind": event.get("kind"),
            "report_interval": {"started_ms": start_ms, "ended_ms": end_ms, **interval_info},
            "wall_duration_ms": interval_info.get("duration_ms"),
            "trace_interval": None,
            "marker": marker_result,
            "alignment": {
                "status": "matched" if selected is not None else marker_result["status"],
                "offset_us": selected["offset_us"] if selected is not None else None,
                "pid": selected["pid"] if selected is not None else None,
                "tid": selected["tid"] if selected is not None else None,
            },
            "renderer_tasks": {"items": [], "returned_count": 0, "available_count": 0,
                               "limit": top_n, "truncated": False, "omitted_count": 0},
            "overlapping_renderer_tasks": [],
            "enclosing_renderer_tasks": [],
            "gc_slices": [], "known_phase_slices": [],
            "gc_context": {"items": [], "returned_count": 0, "available_count": 0,
                            "limit": top_n, "truncated": False, "omitted_count": 0},
            "known_phase_context": {"items": [], "returned_count": 0, "available_count": 0,
                                    "limit": top_n, "truncated": False, "omitted_count": 0},
            "native_phases": [],
            "native_begin_partition": None,
            "cache_sync_overlap": _cache_sync_overlap(capture, start_ms, end_ms),
            "thread_cpu": {"status": "unknown", "reason": "trace thread-clock duration (tdur) is absent"},
            "status": "unknown",
        }
        # Expose report phase timings as descriptive slices; they are not
        # inferred CPU and are not included in any trace CPU total.
        current = _nested(event, "native", "current")
        if not isinstance(current, Mapping):
            current = event.get("native") if isinstance(event.get("native"), Mapping) else None
        if isinstance(current, Mapping):
            if isinstance(current.get("begin_phases"), Mapping):
                event_result["native_begin_partition"] = {
                    "values": dict(current["begin_phases"]), "source": "report",
                    "interpretation": "Nested wall timings summed across begin calls; not contiguous slices or CPU time.",
                }
            for key in ("input_ms", "simulation_audio_ms", "preparation_ms", "begin_ms", "draw_ms", "end_ms"):
                duration = _finite(current.get(key))
                if duration is None or duration < 0:
                    continue
                # A catch-up callback interleaves several draws and steps.
                # Aggregates cannot establish contiguous phase start offsets.
                event_result["native_phases"].append({"name": key, "start_ms": None,
                                                        "duration_ms": duration, "source": "report",
                                                        "aggregation": "callback_sum"})
        if selected is None or start_ms is None or end_ms is None:
            if marker_result["status"] == "matched" and (start_ms is None or end_ms is None):
                event_result["status"] = "unknown_interval"
            elif marker_result["status"] == "ambiguous":
                event_result["status"] = "ambiguous_alignment"
            elif marker_result["status"] == "missing":
                event_result["status"] = "missing_marker"
            result_events.append(event_result)
            continue

        offset_us = selected["offset_us"]
        start_us = start_ms * 1000.0 + offset_us
        end_us = end_ms * 1000.0 + offset_us
        event_result["trace_interval"] = {
            "started_us": start_us, "ended_us": end_us,
            "wall_duration_ms": max(0.0, end_us - start_us) / 1000.0,
        }
        if end_us < start_us:
            event_result["status"] = "unknown_interval"
            event_result["report_interval"]["status"] = "unknown"
            event_result["report_interval"]["reason"] = "interval ends before it starts"
            result_events.append(event_result)
            continue
        pid, tid = selected["pid"], selected["tid"]
        same_thread_tasks = [task for task in renderer_tasks
                             if _event_thread(task) == (pid, tid)
                             and (_event_start_us(task) or 0) < end_us
                             and (_event_start_us(task) or 0) + (_event_duration_us(task) or 0) > start_us]
        task_items = [_slice(task, start_us, end_us) for task in same_thread_tasks]
        task_context = _context(task_items, top_n)
        event_result["renderer_tasks"] = task_context
        # Keep the bounded context object and also expose concise list aliases
        # for callers that only need the retained evidence rows.
        event_result["overlapping_renderer_tasks"] = list(task_context["items"])
        event_result["enclosing_renderer_tasks"] = [item for item in task_context["items"]
                                                     if item["encloses_interval"]]
        phase_items = []
        gc_items = []
        for candidate in trace_events:
            ts = _event_start_us(candidate)
            duration = _event_duration_us(candidate)
            if ts is None or duration is None or duration <= 0:
                continue
            if (_event_thread(candidate) != (pid, tid) or ts >= end_us or ts + duration <= start_us):
                continue
            phase = _phase_name(_event_name(candidate), str(candidate.get("cat", "")))
            if phase == "gc":
                gc_items.append(_slice(candidate, start_us, end_us, phase=phase))
            elif phase == "known_phase" and not _is_renderer_task(candidate):
                phase_items.append(_slice(candidate, start_us, end_us, phase=phase))
        gc_context = _context(gc_items, top_n)
        phase_context = _context(phase_items, top_n)
        event_result["gc_slices"] = list(gc_context["items"])
        event_result["known_phase_slices"] = list(phase_context["items"])
        event_result["gc_context"] = gc_context
        event_result["known_phase_context"] = phase_context
        all_relevant_slices = [*task_items, *gc_items, *phase_items]
        if all_relevant_slices and not any(item["thread_cpu_status"] == "known"
                                           for item in all_relevant_slices):
            event_result["thread_cpu"] = {"status": "unknown", "reason": "matching trace slices omit tdur/thread-clock duration"}
        elif all_relevant_slices:
            event_result["thread_cpu"] = {"status": "known", "reason": None}
        # A renderer task on another thread at the same interval is evidence
        # of a possible marker-thread mismatch, never a causal classification.
        other_tasks = [task for task in renderer_tasks if _event_thread(task) != (pid, tid)
                       and (_event_start_us(task) or 0) < end_us
                       and (_event_start_us(task) or 0) + (_event_duration_us(task) or 0) > start_us]
        if not same_thread_tasks and other_tasks:
            event_result["status"] = "wrong_thread"
            event_result["alignment"]["status"] = "wrong_thread"
            event_result["alignment"]["reason"] = "marker thread has no overlapping renderer task; other renderer thread does"
        else:
            event_result["status"] = "correlated"
        result_events.append(event_result)

    completeness = _completeness(trace_object, metadata_object, raw_trace_events)
    capture_cpu = {
        "status": "unknown" if not events else "per_event",
        "reason": ("No hitch events were captured; thread CPU evidence is unavailable."
                    if not events else
                    "Thread CPU evidence is reported only on individual trace slices; no aggregate is inferred."),
    }
    return {
        "schema": "melee-web-hitch-trace-analysis",
        "version": 1,
        "event_count": len(events),
        "events": result_events,
        "capture_evidence": {
            "diagnostic_event_count": len(events),
            "thread_cpu": capture_cpu,
        },
        "trace": {
            "event_count": len(raw_trace_events),
            "expanded_event_count": len(trace_events),
            "completeness": completeness,
            "cpu_aggregation": "Nested trace CPU durations are reported per slice and never added.",
            "wall_time_interpretation": "Wall duration is overlap evidence only; it is not CPU or GPU execution time.",
        },
    }


# Short aliases make the helper convenient for focused tests and callers that
# use the noun from the capture docs.
analyze = analyze_capture
correlate_capture = analyze_capture
analyze_trace = analyze_capture
correlate = analyze_capture
read_trace = load_trace


__all__ = [
    "DEFAULT_MAX_DECOMPRESSED_BYTES", "DEFAULT_TOP_N", "HitchTraceError",
    "analyze_capture", "analyze", "analyze_trace", "correlate", "correlate_capture",
    "load_trace", "read_trace", "load_metadata",
]
