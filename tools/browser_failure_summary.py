"""Summarize recorded browser replay failures without comparing gameplay state."""

from __future__ import annotations

import json
import math
import os
from collections import OrderedDict
from pathlib import Path
import re
from typing import Any


SCHEMA = "melee-web-browser-failure-summary"
VERSION = 1
ARTIFACTS = (
    "report.json",
    "retail-browser-report.json",
    "progress.json",
    "retail-port.jsonl",
    "page.txt",
    "failure.txt",
    "final.png",
    "prefix.png",
)
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
LOCAL_PATH = re.compile(r"(?P<path>/(?:Users|home)/[^\s\"'<>]+)")
STACK_LINE = re.compile(r"^\s+at\s+.+$")
ITEM_PREFIX = re.compile(r"^ITEMDRAW(?:-META|-OWNER|-HIDE)?\s+(.*)$")
KEY_VALUE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")


class SummaryError(ValueError):
    """Input evidence is unreadable, malformed, or unsafe to summarize."""


def _safe_text(value: Any, limit: int = 2400) -> str:
    text = ANSI_ESCAPE.sub("", str(value))
    text = re.sub(r"file://(?=/(?:Users|home)/)", "", text)

    def shorten_path(match: re.Match[str]) -> str:
        raw = match.group("path").rstrip(":")
        # Keep a useful source basename and any trailing line/column numbers,
        # while omitting the user's home and worktree path.
        file_part, sep, suffix = raw.rpartition(":")
        if sep and suffix.isdigit():
            base, sep2, line = file_part.rpartition(":")
            if sep2 and line.isdigit():
                raw = f"{Path(base).name}:{line}:{suffix}"
            else:
                raw = f"{Path(file_part).name}:{suffix}"
        else:
            raw = Path(raw).name
        return raw

    text = LOCAL_PATH.sub(shorten_path, text)
    if len(text) > limit:
        return text[: limit - 1] + "…"
    return text


def _safe_artifact(root: Path, name: str) -> Path | None:
    path = root / name
    if not path.exists():
        return None
    resolved = path.resolve()
    if root not in resolved.parents or not resolved.is_file():
        raise SummaryError(f"artifact {name!r} is not a regular file inside the run directory")
    return resolved


def _read_json(root: Path, name: str, *, required: bool = False) -> dict[str, Any] | None:
    path = _safe_artifact(root, name)
    if path is None:
        if required:
            raise SummaryError(f"required artifact {name!r} is missing")
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SummaryError(f"cannot read {name}: {error}") from error
    if not isinstance(value, dict):
        raise SummaryError(f"{name} must contain a JSON object")
    return value


def _number(value: Any) -> int | float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value


def _integer(value: Any) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _scan_jsonl(root: Path) -> dict[str, Any] | None:
    path = _safe_artifact(root, "retail-port.jsonl")
    if path is None:
        return None
    header: dict[str, Any] | None = None
    last_frame: dict[str, Any] | None = None
    frame_count = 0
    record_count = 0
    truncated_line: int | None = None
    try:
        with path.open("rb") as stream:
            line_number = 0
            while True:
                raw = stream.readline()
                if not raw:
                    break
                line_number += 1
                terminated = raw.endswith(b"\n")
                payload = raw.rstrip(b"\r\n")
                try:
                    decoded = payload.decode("utf-8")
                    row = json.loads(decoded)
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    if not terminated:
                        truncated_line = line_number
                        break
                    raise SummaryError(
                        f"retail-port.jsonl:{line_number}: malformed interior record: {error}"
                    ) from error
                if not isinstance(row, dict):
                    raise SummaryError(
                        f"retail-port.jsonl:{line_number}: each record must be a JSON object"
                    )
                record_count += 1
                if row.get("record") == "header" and header is None:
                    header = {key: row.get(key) for key in ("schema", "version", "frames_requested")
                              if isinstance(row.get(key), (str, int))}
                elif row.get("record") == "session_frame":
                    frame_count += 1
                    fighter_rows = row.get("fighters")
                    fighter_states = []
                    if isinstance(fighter_rows, list):
                        for fighter in fighter_rows[:8]:
                            if isinstance(fighter, dict):
                                fighter_states.append({
                                    key: _integer(fighter.get(key))
                                    for key in ("slot", "kind", "motion", "animation",
                                                "ground_air", "stocks")
                                })
                    last_frame = {
                        "index": _integer(row.get("index")),
                        "match_frame": _integer(row.get("match_frame")),
                        "scene": _integer(row.get("scene")),
                        "fighters": fighter_states if isinstance(fighter_rows, list) else None,
                        "line": line_number,
                    }
    except OSError as error:
        raise SummaryError(f"cannot read retail-port.jsonl: {error}") from error
    return {
        "header": header,
        "last_frame": last_frame,
        "frame_count": frame_count,
        "record_count": record_count,
        "truncated_final_line": truncated_line,
    }


def _latest_cursor(report: dict[str, Any] | None,
                   progress: dict[str, Any] | None) -> dict[str, Any]:
    observations: list[dict[str, Any]] = []
    if report:
        snapshots = report.get("snapshots")
        if isinstance(snapshots, list):
            candidates = [item for item in snapshots
                          if isinstance(item, dict)
                          and isinstance(item.get("source_cursor"), int)
                          and not isinstance(item.get("source_cursor"), bool)]
            if candidates:
                latest = max(candidates, key=lambda item: (
                    -math.inf if _number(item.get("at_ms")) is None
                    else _number(item.get("at_ms"))))
                observations.append({
                    "source": "report.json:snapshots[]",
                    "value": latest["source_cursor"],
                    "at_ms": _number(latest.get("at_ms")),
                })
        final_snapshot = report.get("final_snapshot")
        if (isinstance(final_snapshot, dict)
                and isinstance(final_snapshot.get("source_cursor"), int)
                and not isinstance(final_snapshot.get("source_cursor"), bool)):
            observations.append({
                "source": "report.json:final_snapshot.source_cursor",
                "value": final_snapshot["source_cursor"],
                "at_ms": _number(final_snapshot.get("at_ms")),
            })
    if progress and isinstance(progress.get("cursor"), int) and not isinstance(progress.get("cursor"), bool):
        observations.append({
            "source": "progress.json:cursor",
            "value": progress["cursor"],
            "at_ms": _number(progress.get("at_ms")),
        })
    if not observations:
        return {"value": None, "source": None, "observations": [], "ordering": "unavailable"}

    timed = [item for item in observations if item["at_ms"] is not None]
    if timed:
        latest_time = max(item["at_ms"] for item in timed)
        latest = [item for item in timed if item["at_ms"] == latest_time]
        values = {item["value"] for item in latest}
        if len(values) > 1:
            return {"value": None, "source": None, "observations": observations,
                    "ordering": "conflicting_at_latest_timestamp"}
        chosen = latest[-1]
        return {"value": chosen["value"], "source": chosen["source"],
                "observations": observations, "ordering": "timestamped"}

    values = {item["value"] for item in observations}
    if len(values) == 1:
        chosen = observations[-1]
        return {"value": chosen["value"], "source": chosen["source"],
                "observations": observations, "ordering": "same_value_no_timestamps"}
    return {"value": None, "source": None, "observations": observations,
            "ordering": "conflicting_without_timestamps"}


def _message(error: Any) -> str | None:
    if isinstance(error, str):
        return _safe_text(error)
    if isinstance(error, dict):
        value = error.get("message")
        return _safe_text(value) if isinstance(value, str) else None
    return None


def _error_record(error: Any, *, source: str, field: str) -> dict[str, Any] | None:
    message = _message(error)
    if message is None:
        return None
    frames = [line.strip() for line in message.splitlines()[1:] if STACK_LINE.match(line)]
    return {
        "kind": error.get("kind") if isinstance(error, dict) and isinstance(error.get("kind"), str) else None,
        "phase": error.get("phase") if isinstance(error, dict) and isinstance(error.get("phase"), str) else None,
        "message": message,
        "evidence": {"artifact": source, "field": field},
        "stack_frames": frames[:10],
    }


def _runtime_errors(report: dict[str, Any] | None) -> tuple[dict[str, Any] | None, list[dict[str, Any]], str]:
    if not report:
        return None, [], "unavailable"
    explicit = _error_record(report.get("first_error"), source="report.json",
                             field="first_error.message")
    listed: list[dict[str, Any]] = []
    browser_errors = report.get("browser_errors")
    if isinstance(browser_errors, list):
        for index, item in enumerate(browser_errors):
            record = _error_record(item, source="report.json",
                                   field=f"browser_errors[{index}].message")
            if record is not None:
                listed.append(record)
    if explicit is not None:
        if listed and listed[0]["message"] != explicit["message"]:
            candidates = [explicit]
            candidates.extend(item for item in listed
                              if item["message"] != explicit["message"])
            return None, candidates, "conflicting_order"
        following = listed[1:] if listed and listed[0]["message"] == explicit["message"] else []
        return explicit, following, "explicit_first_error"
    if len(listed) == 1:
        return listed[0], [], "single_runtime_error"
    if len(listed) > 1:
        return None, listed, "ordering_unknown"
    return None, [], "none_recorded"


def _page_evidence(root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    path = _safe_artifact(root, "page.txt")
    if path is None:
        return [], []
    item_groups: OrderedDict[tuple[int, int], dict[str, Any]] = OrderedDict()
    source_locations: list[dict[str, Any]] = []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            for line_number, raw_line in enumerate(stream, 1):
                line = _safe_text(raw_line.rstrip("\n"), limit=1600)
                if "HSD assertion at " in line:
                    match = re.search(r"([A-Za-z0-9_.-]+\.h:\d+)", line)
                    if match and len(source_locations) < 4:
                        source_locations.append({
                            "location": match.group(1),
                            "artifact": "page.txt",
                            "line": line_number,
                        })
                tag = ITEM_PREFIX.match(line)
                if not tag:
                    continue
                values = dict(KEY_VALUE.findall(tag.group(1)))
                try:
                    cursor = int(values["cursor"])
                    serial = int(values["serial"])
                except (KeyError, ValueError):
                    continue
                key = (cursor, serial)
                record = item_groups.pop(key, None)
                if record is None:
                    record = {"cursor": cursor, "serial": serial, "evidence": []}
                if line.startswith("ITEMDRAW-META"):
                    for field in ("kind", "spawn_kind", "owner", "dynamic_bones", "flags"):
                        if field in values:
                            record[field] = values[field]
                elif line.startswith("ITEMDRAW-OWNER"):
                    for field in ("gobj", "class", "p_link", "user_data"):
                        if field in values:
                            record[f"owner_{field}" if field == "gobj" else field] = values[field]
                elif line.startswith("ITEMDRAW-HIDE"):
                    record["hide"] = {key: values[key] for key in
                                       ("count", "index_list", "table", "item_var_slot")
                                       if key in values}
                if len(record["evidence"]) < 4:
                    record["evidence"].append({"artifact": "page.txt", "line": line_number,
                                               "text": line})
                item_groups[key] = record
                while len(item_groups) > 16:
                    item_groups.popitem(last=False)
    except OSError as error:
        raise SummaryError(f"cannot read page.txt: {error}") from error
    return list(item_groups.values())[-2:], source_locations


def _failure_evidence(report: dict[str, Any] | None, failure_text: str | None,
                      primary: dict[str, Any] | None) -> list[dict[str, Any]]:
    candidates: list[tuple[str, str]] = []
    if failure_text:
        candidates.append(("failure.txt", failure_text))
    if report and isinstance(report.get("failure"), str):
        candidates.append(("report.json:failure", report["failure"]))
    results: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    for source, value in candidates:
        text = _safe_text(value, limit=1400)
        if not text or (primary and text == primary["message"]):
            continue
        is_timeout = bool(re.search(r"timeout|timed out", text, re.I))
        is_cleanup = bool(re.search(r"#unload|unload|teardown|cleanup|dispose", text, re.I))
        if is_timeout and is_cleanup:
            category = "cleanup_timeout"
        elif "browser replay report failed" in text.lower():
            category = "replay_report_failure"
        else:
            category = "harness_failure"
        dedupe_key = (category, " ".join(text.splitlines()[0].split()).lower())
        if dedupe_key in seen:
            continue
        seen.add(dedupe_key)
        results.append({
            "category": category,
            "message": text,
            "evidence": {"artifact": source},
            "ordering": "follow_up_to_replay" if primary else "unknown",
        })
    return results


def _fighter_states(trace: dict[str, Any] | None) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not trace or not isinstance(trace.get("last_frame"), dict):
        return [], {"index": None, "match_frame": None, "source_line": None,
                    "scene": None, "frame_count": 0}
    frame = trace["last_frame"]
    fighters = frame.get("fighters")
    result: list[dict[str, Any]] = []
    if isinstance(fighters, list):
        for fighter in fighters[:8]:
            if not isinstance(fighter, dict):
                continue
            result.append({
                "slot": _integer(fighter.get("slot")),
                "kind": _integer(fighter.get("kind")),
                "identity_name": None,
                "motion": _integer(fighter.get("motion")),
                "animation": _integer(fighter.get("animation")),
                "ground_air": _integer(fighter.get("ground_air")),
                "stocks": _integer(fighter.get("stocks")),
            })
    return result, {
        "index": frame.get("index"),
        "match_frame": frame.get("match_frame"),
        "source_line": frame.get("line"),
        "scene": frame.get("scene"),
        "frame_count": trace.get("frame_count", 0),
    }


def _artifact_links(root: Path, output: Path, present: set[str]) -> list[dict[str, Any]]:
    links: list[dict[str, Any]] = []
    for name in ARTIFACTS:
        path = root / name
        if name not in present:
            continue
        relative = os.path.relpath(path, start=output)
        links.append({"name": name, "path": Path(relative).as_posix()})
    return links


def build_summary(run_directory: str | Path, output_directory: str | Path) -> dict[str, Any]:
    root = Path(run_directory).expanduser().resolve()
    output = Path(output_directory).expanduser().resolve()
    if not root.exists() or not root.is_dir():
        raise SummaryError("run directory is missing or is not a directory")
    if root == output or root in output.parents or output in root.parents:
        raise SummaryError("output directory must not overlap the input run directory")
    if output.exists():
        raise SummaryError("output directory already exists; choose a new output directory")

    present: set[str] = set()
    for name in ARTIFACTS:
        if _safe_artifact(root, name) is not None:
            present.add(name)
    if not present:
        raise SummaryError("run directory contains no recognized browser replay artifacts")

    report = _read_json(root, "report.json")
    retail_report = _read_json(root, "retail-browser-report.json")
    progress = _read_json(root, "progress.json")
    trace = _scan_jsonl(root)
    if report is not None:
        present.add("report.json")
    if retail_report is not None:
        present.add("retail-browser-report.json")
    if progress is not None:
        present.add("progress.json")

    failure_path = _safe_artifact(root, "failure.txt")
    failure_text = None
    if failure_path:
        try:
            failure_text = failure_path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            raise SummaryError(f"cannot read failure.txt: {error}") from error
    page_items, source_locations = _page_evidence(root)
    primary, runtime_candidates, ordering = _runtime_errors(report)
    follow_ups = _failure_evidence(report, failure_text, primary)
    deliberate = report.get("deliberate_prefix_stop") if report else None
    if not isinstance(deliberate, dict):
        deliberate = None
    final_runtime_status = None
    if report:
        final_snapshot = report.get("final_snapshot")
        final_runtime = (final_snapshot.get("runtime_error")
                         if isinstance(final_snapshot, dict) else None)
        if isinstance(final_runtime, str) and final_runtime:
            normalized = _safe_text(final_runtime)
            final_runtime_status = normalized
            error_like_status = bool(re.search(
                r"memory access|runtimeerror|assert|abort|exception|uncaught|failed|error",
                normalized, re.I))
            if (not deliberate or error_like_status) and (
                    not primary or normalized != primary["message"].splitlines()[0].removeprefix("RuntimeError: ")):
                if not any(item["message"] == normalized for item in runtime_candidates):
                    runtime_candidates.append({
                        "kind": "runtime_status",
                        "phase": None,
                        "message": normalized,
                        "evidence": {"artifact": "report.json", "field": "final_snapshot.runtime_error"},
                        "stack_frames": [],
                    })
    fighters, last_frame = _fighter_states(trace)
    cursor = _latest_cursor(report, progress)

    replay_report: dict[str, Any] = {}
    if report and isinstance(report.get("final_snapshot"), dict):
        value = report["final_snapshot"].get("replay_report")
        if isinstance(value, dict):
            replay_report = value
    if not replay_report and report and isinstance(report.get("browser_report"), dict):
        replay_report = report["browser_report"]
    if not replay_report and retail_report:
        replay_report = retail_report

    inputs = report.get("inputs") if report and isinstance(report.get("inputs"), dict) else {}
    identities: dict[str, Any] = {}
    for name in ("disc", "recipe"):
        item = inputs.get(name)
        if isinstance(item, dict):
            identities[name] = {
                "bytes": item.get("bytes") if isinstance(item.get("bytes"), int) else None,
                "sha256": item.get("sha256") if isinstance(item.get("sha256"), str) else None,
            }
    build_identity = None
    for source in (report or {}, replay_report):
        for key in ("build_id", "build_sha256", "build_commit", "source_revision"):
            if isinstance(source.get(key), str):
                build_identity = {key: source[key]}
                break
        if build_identity is not None:
            break

    result = report.get("result") if report and isinstance(report.get("result"), str) else None
    complete = replay_report.get("complete") if isinstance(replay_report.get("complete"), bool) else None
    if primary or runtime_candidates:
        classification = "runtime_error_recorded" if primary else "runtime_error_order_unknown"
    elif deliberate:
        classification = "deliberate_bounded_stop"
    elif result == "fail" or failure_text or (progress and progress.get("error")):
        classification = "failure_recorded"
    elif result == "pass" and complete is True:
        classification = "reported_complete"
    else:
        classification = "outcome_unknown"

    missing: list[str] = []
    if report is None:
        missing.append("report.json: no browser-level event summary")
    if cursor["value"] is None:
        missing.append("source cursor: unavailable or conflicting observations")
    if last_frame["index"] is None:
        missing.append("retail-port.jsonl: session frame index unavailable")
    if last_frame["match_frame"] is None:
        missing.append("retail-port.jsonl: match frame unavailable")
    if last_frame["index"] is not None:
        # The diagnostics format does not define a match-index field.
        missing.append("match index: not recorded; session frame index is separate")
    if not fighters:
        missing.append("fighter state: unavailable in retained artifacts")
    elif any(fighter["identity_name"] is None for fighter in fighters):
        missing.append("fighter display names: numeric kind identifiers are recorded, names are not")
    if not page_items:
        missing.append("item/owner diagnostics: not recorded in page.txt; this does not mean no items existed")
    if not identities.get("recipe", {}).get("sha256"):
        missing.append("recipe identity: hash not recorded")
    if not identities.get("disc", {}).get("sha256"):
        missing.append("disc identity: hash not recorded")
    if build_identity is None:
        missing.append("build identity: not recorded")
    if not isinstance(report.get("scope") if report else None, str):
        missing.append("declared validation scope: not recorded")
    if not any(name in present for name in ("final.png", "prefix.png")):
        missing.append("screenshots: none retained")

    conflicts: list[str] = []
    if cursor["ordering"].startswith("conflicting"):
        conflicts.append("source cursor observations disagree and their order cannot be established")
    if ordering == "conflicting_order":
        conflicts.append("report.json:first_error conflicts with browser_errors[0]; runtime error order is unknown")
    if ordering == "ordering_unknown":
        missing.append("runtime error chronology: no explicit first_error or timestamps establish which recorded error came first")
    truncation = trace.get("truncated_final_line") if trace else None
    if truncation is not None:
        missing.append(f"retail-port.jsonl: incomplete final line {truncation} was reported and excluded")

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "run_name": root.name,
        "run_outcome": {
            "result": result,
            "classification": classification,
            "declared_scope": report.get("scope") if report else None,
            "complete": complete,
            "diagnostic_only": True,
        },
        "runtime_error_order": ordering,
        "primary_runtime_error": primary,
        "runtime_error_candidates": runtime_candidates,
        "final_runtime_status": final_runtime_status,
        "follow_up_failures": follow_ups,
        "last_progress": {
            "source_cursor": cursor,
            "session_frame_index": last_frame["index"],
            "match_index": {"value": None, "source": None},
            "match_frame": {"value": last_frame["match_frame"],
                             "source": "retail-port.jsonl:session_frame.match_frame"
                             if last_frame["match_frame"] is not None else None},
            "session_frame_evidence": {
                "index": last_frame["index"],
                "scene": last_frame["scene"],
                "artifact": "retail-port.jsonl",
                "line": last_frame["source_line"],
            },
        },
        "fighter_states": fighters,
        "item_context": {
            "status": "recorded" if page_items else "not_recorded",
            "records": page_items,
        },
        "runtime_source_locations": source_locations,
        "input_identity": {
            "disc": identities.get("disc"),
            "recipe": identities.get("recipe"),
            "build": build_identity,
            "scheduling_mode": replay_report.get("input_scheduling"),
            "browser": {
                "name": ((report or {}).get("browser") or {}).get("executable")
                if isinstance((report or {}).get("browser"), dict) else None,
                "version": ((report or {}).get("browser") or {}).get("version")
                if isinstance((report or {}).get("browser"), dict) else None,
                "trace_sha256": replay_report.get("trace_sha256"),
            },
        },
        "deliberate_prefix_stop": deliberate,
        "replay_failures": replay_report.get("failures")
        if isinstance(replay_report.get("failures"), list) else [],
        "artifacts": _artifact_links(root, output, present),
        "missing_evidence": missing,
        "conflicting_observations": conflicts,
        "jsonl": {
            "records": trace.get("record_count") if trace else None,
            "session_frames": trace.get("frame_count") if trace else None,
            "truncated_final_line": truncation,
        },
    }


def render_markdown(summary: dict[str, Any]) -> str:
    outcome = summary["run_outcome"]
    error = summary.get("primary_runtime_error")
    progress = summary["last_progress"]
    cursor = progress["source_cursor"]
    lines = [
        f"# Browser failure summary: {summary['run_name']}",
        "",
        f"- Outcome: **{outcome.get('classification')}** (recorded result: {outcome.get('result') or 'unknown'}; complete: {outcome.get('complete') if outcome.get('complete') is not None else 'unknown'})",
        f"- Scope: {outcome.get('declared_scope') or 'not recorded'}",
        "- This is a diagnostic summary, not an acceptance or equivalence result.",
        "",
        "## Earliest runtime error",
    ]
    if error:
        lines.extend([
            f"- **{error.get('kind') or 'runtime error'}** in `{error.get('phase') or 'phase unknown'}`: {_one_line(error['message'].splitlines()[0])}",
            f"- Evidence: `{error['evidence']['artifact']}:{error['evidence']['field']}`",
        ])
        for location in summary.get("runtime_source_locations", [])[:2]:
            lines.append(f"- Source log location: `{location['location']}` at `page.txt:{location['line']}`")
        frames = error.get("stack_frames", [])
        if frames:
            lines.append("- Stack:")
            lines.extend(f"  - `{frame}`" for frame in frames[:5])
    elif summary.get("runtime_error_order") in ("ordering_unknown", "conflicting_order"):
        lines.append("- Multiple runtime errors are recorded, but available evidence does not establish which was first.")
        for candidate in summary.get("runtime_error_candidates", [])[:4]:
            lines.append(f"  - `{candidate['evidence']['field']}`: {_one_line(candidate['message'])}")
    else:
        lines.append("- No runtime error is recorded in the available browser error fields.")
        if summary.get("final_runtime_status"):
            lines.append(f"- Final page status: {_one_line(summary['final_runtime_status'])}.")

    lines.extend(["", "## Progress and context"])
    lines.append(_cursor_line(cursor))
    lines.append(f"- Last session frame index: {progress.get('session_frame_index') if progress.get('session_frame_index') is not None else 'not recorded'}; match index: not recorded; match frame: {progress['match_frame']['value'] if progress['match_frame']['value'] is not None else 'not recorded'}.")
    if summary.get("deliberate_prefix_stop"):
        stop = summary["deliberate_prefix_stop"]
        lines.append(f"- Bounded stop: requested cursor {stop.get('requested_cursor', 'unknown')}, observed cursor {stop.get('observed_cursor', 'unknown')} ({_one_line(stop.get('reason') or 'reason not recorded')}); this stop is not itself classified as a crash.")
    if summary.get("fighter_states"):
        lines.append("- Last fighter states (numeric source identifiers; no lineup-based crash attribution):")
        for fighter in summary["fighter_states"]:
            fields = [f"slot {fighter['slot'] if fighter['slot'] is not None else '?'}",
                      f"kind {fighter['kind'] if fighter['kind'] is not None else '?'}",
                      f"motion {fighter['motion'] if fighter['motion'] is not None else '?'}",
                      f"animation {fighter['animation'] if fighter['animation'] is not None else '?'}"]
            lines.append("  - " + "; ".join(fields))
    else:
        lines.append("- Fighter states: unavailable.")
    items = summary.get("item_context", {}).get("records", [])
    if items:
        lines.append("- Item context (diagnostics only; owner fighter identity is not established):")
        for item in items:
            detail = [f"cursor {item['cursor']}", f"serial {item['serial']}"]
            if "kind" in item: detail.append(f"item kind {item['kind']}")
            if "owner_gobj" in item: detail.append(f"owner object {item['owner_gobj']}")
            if "class" in item: detail.append(f"owner class {item['class']}")
            if "p_link" in item: detail.append(f"p_link {item['p_link']}")
            lines.append("  - " + "; ".join(detail))
    else:
        lines.append("- Item/owner diagnostics: not recorded; absence does not establish that there were no items.")

    followups = summary.get("follow_up_failures", [])
    lines.extend(["", "## Follow-up failures"])
    if followups:
        for item in followups[:5]:
            lines.append(f"- **{item['category']}** (`{item['evidence']['artifact']}`; {item['ordering']}): {_one_line(item['message'])}")
    else:
        lines.append("- None recorded separately from the runtime error.")
    replay_failures = summary.get("replay_failures", [])
    if replay_failures:
        lines.append("- Replay report findings: " + "; ".join(_one_line(value) for value in replay_failures[:5]))

    identity = summary.get("input_identity", {})
    recipe = identity.get("recipe") or {}
    disc = identity.get("disc") or {}
    lines.extend(["", "## Run identity and files"])
    lines.append(f"- Input scheduling: {identity.get('scheduling_mode') or 'not recorded'}; build identity: {_format_build(identity.get('build'))}; browser: {_browser(identity.get('browser'))}.")
    lines.append(f"- Recipe SHA-256: {recipe.get('sha256') or 'not recorded'}; disc SHA-256: {disc.get('sha256') or 'not recorded'}.")
    if summary.get("conflicting_observations"):
        lines.append("- Conflicts: " + "; ".join(summary["conflicting_observations"]))
    if summary.get("missing_evidence"):
        lines.append("- Missing evidence: " + "; ".join(summary["missing_evidence"][:6]))
    for artifact in summary.get("artifacts", []):
        lines.append(f"- [{artifact['name']}]({artifact['path']})")

    return "\n".join(lines) + "\n"


def _one_line(value: Any) -> str:
    if not isinstance(value, str):
        return "unknown"
    first = next((line for line in value.splitlines() if line.strip()), "")
    text = " ".join(first.split())
    if "#unload" in value:
        unload = next((" ".join(line.split()) for line in value.splitlines()
                       if "#unload" in line), "")
        if unload and unload not in text:
            text += f" ({unload})"
    return text[:240]


def _cursor_line(cursor: dict[str, Any]) -> str:
    if cursor.get("value") is None:
        observations = ", ".join(f"{row['source']}={row['value']}" for row in cursor.get("observations", []))
        return f"- Last observed source cursor: **unknown** ({observations or 'no cursor evidence'}; order: {cursor.get('ordering')})."
    return f"- Last observed source cursor: **{cursor['value']}** from `{cursor.get('source')}` (ordering: {cursor.get('ordering')})."


def _format_build(build: Any) -> str:
    if not isinstance(build, dict):
        return "not recorded"
    return ", ".join(f"{key}={value}" for key, value in build.items())


def _browser(browser: Any) -> str:
    if not isinstance(browser, dict):
        return "not recorded"
    return " ".join(str(browser.get(key)) for key in ("name", "version") if browser.get(key)) or "not recorded"


def write_summary(run_directory: str | Path, output_directory: str | Path) -> dict[str, Any]:
    summary = build_summary(run_directory, output_directory)
    output = Path(output_directory).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    try:
        (output / "summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        (output / "summary.md").write_text(render_markdown(summary), encoding="utf-8")
    except OSError as error:
        raise SummaryError(f"cannot write summary output: {error}") from error
    return summary
