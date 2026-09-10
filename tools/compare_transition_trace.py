#!/usr/bin/env python3
"""Strict comparison for the scoped CSS -> SSS -> match lifecycle trace."""

import argparse
import json
import re
from pathlib import Path

from transition_trace_format import (EXPECTED_EVENTS, PLAYER_KEYS, RULE_KEYS,
                                     SCHEMA, VERSION)

MATCH_SELECTION_EVENTS = {"sss_exit_complete", "match_enter_complete"}
EXPECTED_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EXPECTED_EMULATOR_VERSION = "2606a"
EXPECTED_EMULATOR_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"


class TraceError(ValueError):
    pass


def read_jsonl(path):
    rows = []
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise TraceError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise TraceError(f"{path}:{line_number}: each record must be an object")
        rows.append(row)
    if not rows:
        raise TraceError(f"{path}: trace is empty")
    return rows


def select_run(rows, producer, run):
    errors = [row for row in rows
              if row.get("record") == "collector_error" and row.get("run") == run]
    if errors:
        raise TraceError(f"{producer}: collector error: {errors[0].get('error', 'unknown')}")
    headers = [row for row in rows if row.get("record") == "header"]
    if len(headers) != 1:
        raise TraceError(f"{producer}: trace requires exactly one header")
    header = headers[0]
    if header.get("schema") != SCHEMA or header.get("version") != VERSION:
        raise TraceError(f"{producer}: unsupported transition trace schema")
    if header.get("producer") != producer:
        raise TraceError(f"{producer}: producer label does not match input role")
    if producer == "retail":
        expected = {
            "game_revision": "GALE01r2", "dol_sha1": EXPECTED_DOL_SHA1,
            "emulator_version": EXPECTED_EMULATOR_VERSION,
            "emulator_commit": EXPECTED_EMULATOR_COMMIT,
            "cpu_core": "Interpreter64", "cpu_thread": False,
            "fixed_rtc": 1704067200,
        }
        for key, value in expected.items():
            if header.get(key) != value:
                raise TraceError(f"retail: pinned provenance mismatch for {key}")
    else:
        revision = header.get("source_revision")
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise TraceError("port: source revision must be a full lowercase Git commit")
        if header.get("build_configuration") != "browser-release":
            raise TraceError("port: comparison requires the browser-release build")
    events = [row for row in rows
              if row.get("record") == "event" and row.get("run") == run]
    if not events:
        raise TraceError(f"{producer}: run {run} is absent")
    indices = [event.get("index") for event in events]
    if indices != list(range(len(events))):
        raise TraceError(f"{producer}: run {run} event indices are not consecutive")
    names = tuple(event.get("event") for event in events)
    if names != EXPECTED_EVENTS:
        raise TraceError(
            f"{producer}: run {run} lifecycle sequence differs: {names!r}")
    routes = [event.get("route") for event in events
              if event.get("event") == "sss_exit_complete"]
    if routes != ["css", "match"]:
        raise TraceError(f"{producer}: expected SSS cancel followed by match, got {routes}")
    return header, events


def require_audio(producer, events):
    for index, event in enumerate(events):
        audio = event.get("audio")
        if not isinstance(audio, dict):
            raise TraceError(f"{producer}: event {index} is missing audio state")
        if not isinstance(audio.get("active"), bool):
            raise TraceError(f"{producer}: event {index} has invalid audio active state")
        if not isinstance(audio.get("owner_epoch"), int) or audio["owner_epoch"] < 0:
            raise TraceError(f"{producer}: event {index} has invalid audio owner epoch")
        if not isinstance(audio.get("stream"), str):
            raise TraceError(f"{producer}: event {index} has invalid audio stream")


def validate_continuity(producer, events):
    require_audio(producer, events)
    for index, event in enumerate(events[:-1]):
        audio = event["audio"]
        if not audio["active"] or audio["owner_epoch"] != 0 or audio["stream"] != "menu01.hps":
            raise TraceError(
                f"{producer}: menu audio continuity failed at event {index} ({event['event']})")
    match_audio = events[-1]["audio"]
    if not match_audio["active"] or match_audio["owner_epoch"] != 1:
        raise TraceError(f"{producer}: match did not establish exactly one new audio owner")
    if producer == "retail":
        for index, event in enumerate(events[:-1]):
            diagnostics = event.get("retail_audio_diagnostics")
            if not isinstance(diagnostics, dict):
                raise TraceError(f"retail: event {index} is missing audio call diagnostics")
            if any(diagnostics.get(name) != 0 for name in
                   ("stream_starts", "stream_stops", "driver_reinitializations",
                    "language_bank_initializations")):
                raise TraceError(
                    f"retail: audio restarted or reinitialized before match at event {index}")
        match_diagnostics = events[-1].get("retail_audio_diagnostics")
        expected = {
            "stream_starts": 1, "stream_stops": 1,
            "driver_reinitializations": 0,
            "language_bank_initializations": 0,
        }
        if match_diagnostics != expected:
            raise TraceError("retail: match entry did not perform one isolated stream handoff")


def first_difference(reference, port, path="$"):
    if type(reference) is not type(port):
        return path, reference, port
    if isinstance(reference, dict):
        keys = list(reference)
        keys.extend(key for key in port if key not in reference)
        for key in keys:
            if key not in reference or key not in port:
                return f"{path}.{key}", reference.get(key), port.get(key)
            difference = first_difference(reference[key], port[key], f"{path}.{key}")
            if difference:
                return difference
        return None
    if isinstance(reference, list):
        if len(reference) != len(port):
            return f"{path}.length", len(reference), len(port)
        for index, (left, right) in enumerate(zip(reference, port)):
            difference = first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if reference == port else (path, reference, port)


def validate_selection(selection, event):
    if not isinstance(selection, dict) or set(selection) != {"rules", "players"}:
        raise TraceError(f"{event}: selection shape is invalid")
    rules = selection["rules"]
    players = selection["players"]
    if not isinstance(rules, dict) or set(rules) != RULE_KEYS:
        raise TraceError(f"{event}: rules schema is incomplete or unknown")
    if not isinstance(players, list) or len(players) != 4:
        raise TraceError(f"{event}: exactly four player records are required")
    for index, player in enumerate(players):
        if not isinstance(player, dict) or set(player) != PLAYER_KEYS:
            raise TraceError(f"{event}: player {index} schema is incomplete or unknown")


def comparable_event(event):
    result = {
        "event": event["event"],
        "route": event.get("route"),
        "audio": {
            "active": event["audio"]["active"],
            "owner_epoch": event["audio"]["owner_epoch"],
            "stream": event["audio"]["stream"],
        },
    }
    if event["event"] in MATCH_SELECTION_EVENTS and event.get("route") == "match":
        if "selection" not in event:
            raise TraceError(f"{event['event']}: match route is missing selection")
        validate_selection(event["selection"], event["event"])
        result["selection"] = event["selection"]
        if not isinstance(event.get("rng"), int):
            raise TraceError(f"{event['event']}: source RNG is missing")
        result["rng"] = event["rng"]
    elif event["event"] == "match_enter_complete":
        if "selection" not in event:
            raise TraceError("match_enter_complete: selection is missing")
        validate_selection(event["selection"], event["event"])
        result["selection"] = event["selection"]
        if not isinstance(event.get("rng"), int):
            raise TraceError("match_enter_complete: source RNG is missing")
        result["rng"] = event["rng"]
    return result


def compare(reference_rows, port_rows, run):
    reference_header, reference = select_run(reference_rows, "retail", run)
    port_header, port = select_run(port_rows, "port", run)
    if reference_header.get("game_revision") != port_header.get("game_revision"):
        raise TraceError("retail and port game revisions differ")
    validate_continuity("retail", reference)
    validate_continuity("port", port)
    for index, (retail_event, port_event) in enumerate(zip(reference, port)):
        difference = first_difference(comparable_event(retail_event),
                                      comparable_event(port_event))
        if difference:
            path, expected, actual = difference
            return {
                "equivalent": False,
                "run": run,
                "checks": {
                    "lifecycle_order": "pass",
                    "audio_continuity": "pass",
                    "semantic_state": "fail",
                },
                "first_divergence": {
                    "index": index,
                    "event": retail_event["event"],
                    "field": path,
                    "retail": expected,
                    "port": actual,
                },
            }
    return {
        "equivalent": True,
        "run": run,
        "checks": {
            "lifecycle_order": "pass",
            "audio_continuity": "pass",
            "semantic_state": "pass",
        },
        "events_compared": len(reference),
        "scope": "CSS to SSS, SSS cancel to CSS, CSS to SSS, and supported match entry",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("retail", type=Path)
    parser.add_argument("port", type=Path)
    parser.add_argument("--run", type=int, default=0)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    try:
        result = compare(read_jsonl(arguments.retail), read_jsonl(arguments.port),
                         arguments.run)
    except TraceError as error:
        parser.error(str(error))
    encoded = json.dumps(result, indent=2) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded)
    print(encoded, end="")
    return 0 if result["equivalent"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
