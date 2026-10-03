#!/usr/bin/env python3
"""Authenticated CLI for the private diagnostics query/delete API.

The bearer token is read only from ``MELEE_DIAGNOSTICS_ADMIN_TOKEN``. It is
never accepted as a command-line argument, printed, logged, or included in an
error. The server returns only the normalized v1 report; this client validates
that response before displaying or writing it.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit, urlunsplit
import urllib.request


API_ENV = "MELEE_DIAGNOSTICS_API_URL"
TOKEN_ENV = "MELEE_DIAGNOSTICS_ADMIN_TOKEN"
REPORT_ID_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
RUNTIME_HASH_RE = re.compile(r"^[0-9a-f]{16}$")
CURSOR_RE = re.compile(r"^[0-9]{1,16}\.[0-9a-f]{64}$")
PROFILE_RE = re.compile(r"^(?:player|audio-preview|audio-player)$")
MAX_RESPONSE_BYTES = 8 * 1024 * 1024
MAX_HISTORY_ROWS = 100
MAX_HISTORY_COLUMNS = 20
MAX_PRE_EVENTS = 32
MAX_POST_EVENTS = 24
NUMBER_LIMIT = 1e12
# Server envelope epochs use the safe-integer range, separately from measurements.
MAX_EPOCH_MS = 9007199254740991
FRAME_LIMIT = 0x7FFFFFFF

ENVIRONMENTS = {"staging", "production"}
BROWSER_FAMILIES = {"chrome", "edge", "firefox", "opera", "safari", "unknown"}
PLATFORMS = {"macos", "windows", "linux", "ios", "android", "unknown"}
SCENES = {"css", "preparing", "sss", "unloaded", "match", "results", "prize", "title", "main", "opening", "opening_vs", "unknown"}
CLOCK_OWNERS = {"other", "simulation", "audio"}
REASONS = {
    "simulation_debt", "audio_debt", "nonfinite_clock", "runtime_failure",
    "manual_pause", "manual_resume", "render_preparation", "clock_regression", "scheduled_pause",
}
LIFECYCLE_EVENT_NAMES = {"active", "inactive", "pause", "resume", "visibility_hidden", "visibility_visible", "pagehide", "pageshow", "scene_enter", "scene_exit", "recovered", "recovery", "timeout", "unknown", "scheduled_pause", "render_preparation", "freeze", "preparation", "preparation_done", "asset_preparation"}
AUDIO_CONTEXT_STATES = {"running", "suspended", "closed", "interrupted", "unknown"}
HISTORY_COLUMNS = ("timestamp", "source_frame", "scene", "stage", "fighter0", "fighter1", "fighter2", "fighter3", "debt_ticks", "update_ms", "draw_ms", "total_ms", "preparation_ms", "queued_delta", "created_delta", "texture_upload_bytes", "source_steps", "source_draws", "running", "interval_ms")
CAPABILITY_NAMES = ("native", "audio", "longtask")
CAPABILITY_REASONS = {"not_observed", "unavailable", "supported_no_events"}
REASON_CODES = {name: code for code, name in enumerate(("", "simulation_debt", "audio_debt", "nonfinite_clock", "runtime_failure", "manual_pause", "manual_resume", "render_preparation", "clock_regression", "scheduled_pause")) if code}
SCENE_CODE_NAMES = {0: "unknown", 1: "css", 2: "preparing", 3: "sss", 4: "preparing", 5: "preparing", 6: "unloaded", 7: "match", 8: "results", 9: "prize", 10: "title", 11: "main", 12: "opening", 13: "opening_vs"}


class CliError(ValueError):
    """A bounded user-facing error that contains no token or response dump."""


def _fail(message: str) -> None:
    raise CliError(message)


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    if len(pairs) > 64:
        _fail("response_too_many_keys")
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail("response_duplicate_key")
        result[key] = value
    return result


def _decode_json(raw: bytes) -> Any:
    if len(raw) > MAX_RESPONSE_BYTES:
        _fail("response_too_large")
    try:
        text = raw.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_unique_pairs, parse_constant=lambda _: _fail("response_nonfinite"))
        _bound_json(value)
        return value
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError):
        _fail("invalid_response_json")


def _bound_json(value: Any, depth: int = 0) -> None:
    if depth > 16:
        _fail("response_too_deep")
    if isinstance(value, dict):
        if len(value) > 64:
            _fail("response_too_many_keys")
        for child in value.values():
            _bound_json(child, depth + 1)
    elif isinstance(value, list):
        if len(value) > 128:
            _fail("response_too_many_items")
        for child in value:
            _bound_json(child, depth + 1)


def _read_response(response: Any) -> bytes:
    chunks: list[bytes] = []
    total = 0
    while True:
        chunk = response.read(min(64 * 1024, MAX_RESPONSE_BYTES + 1 - total))
        if not chunk:
            break
        total += len(chunk)
        if total > MAX_RESPONSE_BYTES:
            _fail("response_too_large")
        chunks.append(chunk)
    return b"".join(chunks)


def _known_host(host: str | None) -> bool:
    if host is None:
        return False
    return host in {
        "webmelee.gg",
        "www.webmelee.gg",
        "webmelee.pages.dev",
        "staging.webmelee.gg",
        "webmelee-staging.pages.dev",
    } or bool(re.fullmatch(r"[0-9a-f]{8}\.webmelee\.pages\.dev", host)) \
        or bool(re.fullmatch(r"[0-9a-f]{8}\.webmelee-staging\.pages\.dev", host))


def _base_url(value: str) -> str:
    try:
        parsed = urlsplit(value)
        if parsed.scheme != "https" or parsed.username or parsed.password or parsed.query or parsed.fragment or not _known_host(parsed.hostname):
            _fail("invalid_api_url")
        if parsed.port is not None:
            _fail("invalid_api_url")
        path = parsed.path.rstrip("/")
        if path not in ("", "/api/diagnostics"):
            _fail("invalid_api_url")
        return urlunsplit(("https", parsed.netloc, "/api/diagnostics", "", ""))
    except ValueError:
        _fail("invalid_api_url")


def _token() -> str:
    value = os.environ.get(TOKEN_ENV)
    if not value or len(value) < 16 or len(value) > 512:
        _fail("missing_admin_token")
    return value


def _request_json(base: str, token: str, method: str = "GET", suffix: str = "", params: dict[str, str] | None = None) -> Any:
    query = urlencode([(key, value) for key, value in (params or {}).items() if value is not None])
    url = base + suffix + ("?" + query if query else "")
    request = urllib.request.Request(url, method=method, headers={
        "Accept": "application/json",
        "Authorization": "Bearer " + token,
        "User-Agent": "WebMelee-Diagnostics-Admin/1",
    })
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = _read_response(response)
    except HTTPError as error:
        try:
            body = _decode_json(_read_response(error))
        except CliError:
            body = None
        message = body.get("error") if isinstance(body, dict) and isinstance(body.get("error"), str) else "http_error"
        _fail(f"http_{error.code}_{message}")
    except (URLError, TimeoutError, OSError):
        _fail("network_error")
    return _decode_json(raw)


def _exact(value: Any, keys: tuple[str, ...], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(keys):
        _fail(f"invalid_{label}")
    return value


def _string(value: Any, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or not pattern.fullmatch(value):
        _fail(f"invalid_{label}")
    return value


def _enum(value: Any, allowed: set[str], label: str) -> str:
    if not isinstance(value, str) or value not in allowed:
        _fail(f"invalid_{label}")
    return value


def _number(value: Any, label: str, *, integer: bool = False, minimum: float = -NUMBER_LIMIT, maximum: float = NUMBER_LIMIT) -> float | int:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < minimum or value > maximum:
        _fail(f"invalid_{label}")
    if integer and not isinstance(value, int):
        _fail(f"invalid_{label}")
    return value


def _nullable_number(value: Any, label: str, *, minimum: float = -NUMBER_LIMIT, maximum: float = NUMBER_LIMIT) -> float | int | None:
    return None if value is None else _number(value, label, minimum=minimum, maximum=maximum)


def _sanitize_event(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or not isinstance(value.get("type"), str):
        _fail(f"invalid_{label}")
    event_type = value["type"]
    if event_type == "native":
        event = _exact(value, ("type", "timestamp", "source_frame", "total_ms", "interval_ms"), label)
        return {
            "type": "native",
            "timestamp": _nullable_number(event["timestamp"], f"{label}_timestamp", minimum=0),
            "source_frame": None if event["source_frame"] is None else _number(event["source_frame"], f"{label}_source_frame", integer=True, minimum=0, maximum=FRAME_LIMIT),
            "total_ms": _nullable_number(event["total_ms"], f"{label}_total"),
            "interval_ms": _nullable_number(event["interval_ms"], f"{label}_interval"),
        }
    if event_type == "lifecycle":
        event = _exact(value, ("type", "kind", "timestamp", "source_frame", "scene", "scene_code", "duration_ms", "bytes", "files"), label)
        if not isinstance(event["kind"], str) or event["kind"] not in LIFECYCLE_EVENT_NAMES:
            _fail(f"invalid_{label}_kind")
        scene_code = _number(event["scene_code"], f"{label}_scene_code", integer=True, minimum=0, maximum=255)
        if not isinstance(event["scene"], str) or event["scene"] not in SCENES or SCENE_CODE_NAMES.get(scene_code) != event["scene"]:
            _fail(f"invalid_{label}_scene")
        return {
            "type": "lifecycle", "kind": event["kind"],
            "timestamp": _nullable_number(event["timestamp"], f"{label}_timestamp", minimum=0),
            "source_frame": None if event["source_frame"] is None else _number(event["source_frame"], f"{label}_source_frame", integer=True, minimum=0, maximum=FRAME_LIMIT),
            "scene": event["scene"], "scene_code": scene_code,
            "duration_ms": _nullable_number(event["duration_ms"], f"{label}_duration"),
            "bytes": _nullable_number(event["bytes"], f"{label}_bytes"),
            "files": _nullable_number(event["files"], f"{label}_files"),
        }
    if event_type == "longtask":
        event = _exact(value, ("type", "timestamp", "duration_ms", "source_frame"), label)
        return {
            "type": "longtask",
            "timestamp": _nullable_number(event["timestamp"], f"{label}_timestamp", minimum=0),
            "duration_ms": _nullable_number(event["duration_ms"], f"{label}_duration"),
            "source_frame": None if event["source_frame"] is None else _number(event["source_frame"], f"{label}_source_frame", integer=True, minimum=0, maximum=FRAME_LIMIT),
        }
    if event_type == "audio":
        event = _exact(value, ("type", "timestamp", "callback_ms", "interval_ms", "queue_depth", "underruns", "overflows", "clock_seconds", "context_state", "enabled", "source_frame"), label)
        if not isinstance(event["context_state"], str) or event["context_state"] not in AUDIO_CONTEXT_STATES:
            _fail(f"invalid_{label}_context")
        if event["enabled"] is not None and not isinstance(event["enabled"], bool):
            _fail(f"invalid_{label}_enabled")
        return {
            "type": "audio",
            "timestamp": _nullable_number(event["timestamp"], f"{label}_timestamp", minimum=0),
            "callback_ms": _nullable_number(event["callback_ms"], f"{label}_callback"),
            "interval_ms": _nullable_number(event["interval_ms"], f"{label}_interval"),
            "queue_depth": _nullable_number(event["queue_depth"], f"{label}_queue"),
            "underruns": _nullable_number(event["underruns"], f"{label}_underruns"),
            "overflows": _nullable_number(event["overflows"], f"{label}_overflows"),
            "clock_seconds": _nullable_number(event["clock_seconds"], f"{label}_clock"),
            "context_state": event["context_state"], "enabled": event["enabled"],
            "source_frame": None if event["source_frame"] is None else _number(event["source_frame"], f"{label}_source_frame", integer=True, minimum=0, maximum=FRAME_LIMIT),
        }
    _fail(f"invalid_{label}_type")


def _sanitize_report(value: Any) -> dict[str, Any]:
    report = _exact(value, (
        "schema", "version", "session_id", "incident_id", "identity", "environment", "client",
        "capabilities", "incident", "history", "events", "flags",
    ), "report")
    if report["schema"] != "melee-web-diagnostics" or type(report["version"]) is not int or report["version"] != 1:
        _fail("invalid_report_schema")
    identity = _exact(report["identity"], ("source_commit", "runtime_hash", "build_profile"), "identity")
    environment = _exact(report["environment"], ("env", "origin"), "environment")
    client = _exact(report["client"], ("browser_family", "browser_major", "platform"), "client")
    capabilities = _exact(report["capabilities"], CAPABILITY_NAMES, "capabilities")
    incident = _exact(report["incident"], ("reason", "reason_code", "value", "threshold", "source_frame", "scene", "scene_code", "clock_owner", "clock_owner_code"), "incident")
    history = _exact(report["history"], ("columns", "rows", "evicted", "evicted_count", "truncated"), "history")
    events = _exact(report["events"], ("pre", "post"), "events")
    flags = _exact(report["flags"], ("incomplete", "persistence_failure"), "flags")
    if not isinstance(history["columns"], list) or history["columns"] != list(HISTORY_COLUMNS):
        _fail("invalid_history_columns")
    if not isinstance(history["rows"], list) or len(history["rows"]) > MAX_HISTORY_ROWS:
        _fail("invalid_history_rows")
    sanitized_history = []
    for row_index, row in enumerate(history["rows"]):
        if not isinstance(row, list) or len(row) != MAX_HISTORY_COLUMNS:
            _fail("invalid_history_width")
        sanitized_history.append([None if cell is None else _number(cell, f"history_{row_index}") for cell in row])
    if not isinstance(history["evicted"], bool) or not isinstance(history["truncated"], bool):
        _fail("invalid_history_flags")
    _number(history["evicted_count"], "history_evicted_count", integer=True, minimum=0, maximum=FRAME_LIMIT)
    if not isinstance(events["pre"], list) or len(events["pre"]) > MAX_PRE_EVENTS:
        _fail("invalid_pre_events")
    if not isinstance(events["post"], list) or len(events["post"]) > MAX_POST_EVENTS:
        _fail("invalid_post_events")
    for name in CAPABILITY_NAMES:
        capability = _exact(capabilities[name], ("available", "observed", "reason"), f"capability_{name}")
        if not isinstance(capability["available"], bool) or not isinstance(capability["observed"], bool):
            _fail("invalid_capability")
        if capability["reason"] is not None and (not isinstance(capability["reason"], str) or capability["reason"] not in CAPABILITY_REASONS):
            _fail("invalid_capability_reason")
        if capability["observed"] and capability["reason"] is not None:
            _fail("invalid_capability_observation")
        if not capability["observed"] and capability["reason"] is None:
            _fail("invalid_capability_observation")
    if not isinstance(flags["incomplete"], bool) or not isinstance(flags["persistence_failure"], bool):
        _fail("invalid_flags")
    reason_code = _number(incident["reason_code"], "incident_reason_code", integer=True, minimum=1, maximum=9)
    if not isinstance(incident["reason"], str) or incident["reason"] not in REASONS or REASON_CODES.get(incident["reason"]) != reason_code:
        _fail("invalid_incident_reason")
    scene_code = _number(incident["scene_code"], "incident_scene_code", integer=True, minimum=0, maximum=255)
    if not isinstance(incident["scene"], str) or incident["scene"] not in SCENES or SCENE_CODE_NAMES.get(scene_code) != incident["scene"]:
        _fail("invalid_incident_scene")
    owner_code = _number(incident["clock_owner_code"], "incident_clock_owner_code", integer=True, minimum=0, maximum=2)
    if not isinstance(incident["clock_owner"], str) or incident["clock_owner"] not in CLOCK_OWNERS or owner_code != {"other": 0, "simulation": 1, "audio": 2}[incident["clock_owner"]]:
        _fail("invalid_incident_clock_owner")
    return {
        "schema": "melee-web-diagnostics", "version": 1,
        "session_id": _string(report["session_id"], re.compile(r"session-[a-z0-9]{1,64}"), "session_id"),
        "incident_id": _string(report["incident_id"], re.compile(r"(?:incident-[0-9]+|session-[a-z0-9]{1,64}:incident-[0-9]+)"), "incident_id"),
        "identity": {
            "source_commit": _string(identity["source_commit"], COMMIT_RE, "source_commit"),
            "runtime_hash": _string(identity["runtime_hash"], RUNTIME_HASH_RE, "runtime_hash"),
            "build_profile": _string(identity["build_profile"], PROFILE_RE, "build_profile"),
        },
        "environment": {"env": _enum(environment["env"], ENVIRONMENTS, "environment"), "origin": _origin(environment["origin"])},
        "client": {
            "browser_family": _enum(client["browser_family"], BROWSER_FAMILIES, "browser_family"),
            "browser_major": None if client["browser_major"] is None else _number(client["browser_major"], "browser_major", integer=True, minimum=0, maximum=999),
            "platform": _enum(client["platform"], PLATFORMS, "platform"),
        },
        "capabilities": {name: {"available": capabilities[name]["available"], "observed": capabilities[name]["observed"], "reason": capabilities[name]["reason"]} for name in CAPABILITY_NAMES},
        "incident": {
            "reason": incident["reason"], "reason_code": reason_code,
            "value": _nullable_number(incident["value"], "incident_value"), "threshold": _nullable_number(incident["threshold"], "incident_threshold"),
            "source_frame": None if incident["source_frame"] is None else _number(incident["source_frame"], "source_frame", integer=True, minimum=0, maximum=FRAME_LIMIT),
            "scene": incident["scene"], "scene_code": scene_code,
            "clock_owner": incident["clock_owner"], "clock_owner_code": owner_code,
        },
        "history": {"columns": list(HISTORY_COLUMNS), "rows": sanitized_history, "evicted": history["evicted"], "evicted_count": history["evicted_count"], "truncated": history["truncated"]},
        "events": {"pre": [_sanitize_event(event, "pre_event") for event in events["pre"]], "post": [_sanitize_event(event, "post_event") for event in events["post"]]},
        "flags": {"incomplete": flags["incomplete"], "persistence_failure": flags["persistence_failure"]},
    }


def _origin(value: Any) -> str:
    if not isinstance(value, str) or len(value) > 160:
        _fail("invalid_origin")
    try:
        parsed = urlsplit(value)
        if parsed.scheme != "https" or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/") or not _known_host(parsed.hostname) or parsed.port is not None:
            _fail("invalid_origin")
        canonical = urlunsplit(("https", parsed.netloc, "", "", ""))
        if canonical != value:
            _fail("invalid_origin")
        return value
    except ValueError:
        _fail("invalid_origin")


def _sanitize_envelope(value: Any) -> dict[str, Any]:
    envelope = _exact(value, ("report_id", "received_at", "expires_at", "bytes", "report"), "report_envelope")
    return {
        "report_id": _string(envelope["report_id"], REPORT_ID_RE, "report_id"),
        "received_at": _number(envelope["received_at"], "received_at", integer=True, minimum=0, maximum=MAX_EPOCH_MS),
        "expires_at": _number(envelope["expires_at"], "expires_at", integer=True, minimum=0, maximum=MAX_EPOCH_MS),
        "bytes": _number(envelope["bytes"], "bytes", integer=True, minimum=1, maximum=65536),
        "report": _sanitize_report(envelope["report"]),
    }


def _query_result(value: Any) -> dict[str, Any]:
    result = _exact(value, ("reports", "next_cursor"), "query_result")
    if not isinstance(result["reports"], list) or len(result["reports"]) > 100:
        _fail("invalid_reports")
    cursor = result["next_cursor"]
    if cursor is not None and (not isinstance(cursor, str) or not CURSOR_RE.fullmatch(cursor) or int(cursor.split(".", 1)[0]) > 9007199254740991):
        _fail("invalid_cursor")
    return {"reports": [_sanitize_envelope(report) for report in result["reports"]], "next_cursor": cursor}


def _error_result(value: Any) -> str:
    result = _exact(value, ("error",), "error_result")
    if not isinstance(result["error"], str) or not re.fullmatch(r"[a-z0-9_]+", result["error"]):
        _fail("invalid_error_result")
    return result["error"]


def _report_id(value: str) -> str:
    return _string(value, REPORT_ID_RE, "report_id")


def _positive_int(value: str, label: str, maximum: int) -> str:
    if not re.fullmatch(r"[0-9]+", value) or not 1 <= int(value) <= maximum:
        _fail(f"invalid_{label}")
    return str(int(value))


def _time_filter(value: str, label: str) -> str:
    if not re.fullmatch(r"[0-9]{1,16}", value) or int(value) > 9007199254740991:
        _fail(f"invalid_{label}")
    return str(int(value))


def _query_params(args: argparse.Namespace) -> dict[str, str]:
    params: dict[str, str] = {"limit": _positive_int(str(args.limit), "limit", 100)}
    if args.environment is not None:
        if args.environment not in ENVIRONMENTS:
            _fail("invalid_environment")
        params["environment"] = args.environment
    if args.source_commit is not None:
        params["source_commit"] = _string(args.source_commit, COMMIT_RE, "source_commit")
    if args.runtime_hash is not None:
        params["runtime_hash"] = _string(args.runtime_hash, RUNTIME_HASH_RE, "runtime_hash")
    if args.build is not None:
        if COMMIT_RE.fullmatch(args.build) or RUNTIME_HASH_RE.fullmatch(args.build) or PROFILE_RE.fullmatch(args.build):
            params["build"] = args.build
        else:
            _fail("invalid_build")
    if args.reason is not None:
        if args.reason not in REASONS:
            _fail("invalid_reason")
        params["reason"] = args.reason
    if args.from_time is not None:
        params["from"] = _time_filter(args.from_time, "from")
    if args.to_time is not None:
        params["to"] = _time_filter(args.to_time, "to")
    if args.cursor is not None:
        if not CURSOR_RE.fullmatch(args.cursor) or int(args.cursor.split(".", 1)[0]) > 9007199254740991:
            _fail("invalid_cursor")
        params["cursor"] = args.cursor
    return params


def _base_from_args(args: argparse.Namespace) -> str:
    value = args.url or os.environ.get(API_ENV)
    if not value:
        _fail("missing_api_url")
    return _base_url(value)


def _emit(value: Any) -> None:
    json.dump(value, sys.stdout, ensure_ascii=False, indent=2, sort_keys=True)
    sys.stdout.write("\n")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help=f"API origin or /api/diagnostics URL (default: ${API_ENV})")
    commands = parser.add_subparsers(dest="command", required=True)
    query = commands.add_parser("query", help="query sanitized reports")
    query.add_argument("--environment", choices=sorted(ENVIRONMENTS))
    query.add_argument("--source-commit")
    query.add_argument("--runtime-hash")
    query.add_argument("--build", help="source commit, runtime hash, or build profile")
    query.add_argument("--reason", choices=sorted(REASONS))
    query.add_argument("--from", dest="from_time")
    query.add_argument("--to", dest="to_time")
    query.add_argument("--limit", type=int, default=100)
    query.add_argument("--cursor")

    get = commands.add_parser("get", help="fetch one sanitized report")
    get.add_argument("report_id")

    download = commands.add_parser("download", help="write one sanitized report to a new file")
    download.add_argument("report_id")
    download.add_argument("--output", type=Path, required=True)

    delete = commands.add_parser("delete", help="delete one report")
    delete.add_argument("report_id")

    purge = commands.add_parser("purge", help="delete reports received before a bounded timestamp")
    purge.add_argument("--before", required=True)
    purge.add_argument("--limit", type=int, default=100)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    try:
        args = parser.parse_args(argv)
        base = _base_from_args(args)
        token = _token()
        if args.command == "query":
            result = _query_result(_request_json(base, token, params=_query_params(args)))
            _emit(result)
        elif args.command == "get":
            report_id = _report_id(args.report_id)
            result = _sanitize_envelope(_request_json(base, token, suffix="/" + report_id))
            _emit(result)
        elif args.command == "download":
            report_id = _report_id(args.report_id)
            result = _sanitize_envelope(_request_json(base, token, suffix="/" + report_id))
            if args.output.exists() or args.output.is_symlink():
                _fail("output_exists")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x", encoding="utf-8") as stream:
                json.dump(result["report"], stream, ensure_ascii=False, indent=2, sort_keys=True)
                stream.write("\n")
            args.output.chmod(0o600)
            _emit({"downloaded": True})
        elif args.command == "delete":
            report_id = _report_id(args.report_id)
            result = _exact(_request_json(base, token, method="DELETE", suffix="/" + report_id), ("deleted",), "delete_result")
            if not isinstance(result["deleted"], bool):
                _fail("invalid_delete_result")
            _emit(result)
        elif args.command == "purge":
            before = _time_filter(args.before, "before")
            limit = _positive_int(str(args.limit), "limit", 1000)
            result = _exact(_request_json(base, token, method="DELETE", params={"before": before, "limit": limit}), ("deleted",), "purge_result")
            if not isinstance(result["deleted"], int) or isinstance(result["deleted"], bool) or not 0 <= result["deleted"] <= 1000:
                _fail("invalid_purge_result")
            _emit(result)
        else:  # pragma: no cover - argparse enforces the command
            _fail("missing_command")
        return 0
    except CliError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
