"""Extract the retained original Stadium C/F input schedule for one Setup run.

This is a source-bound diagnostic extractor, not a general MWRO/MWRI importer.
Runtime data contains only typed four-port PAD bundles and tick/draw references.
All source rows, expected state, and original/replay provenance remain
under ``host_only`` and must never be forwarded into the runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Callable, Mapping, Sequence


SCHEMA = "melee-web-stadium-setup-cf-input-schedule"
VERSION = 1
ORIGINAL_V6_SOURCE = {
    "observer": (4_397_889,
                 "361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778"),
    "observer_status": (515,
                        "05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f"),
    "report": (143_112,
               "14711ec4f0905bcad1de44844731b984ec1bdf778cfbbea32cde1c2b0fa2e46b"),
    "mwri": (188_296,
             "57e91222a280f93c861a849c8af404ae4272767ead1debf62312a5be48c473fe"),
    "mwri_status": (89,
                    "583eb80d3a0c2789ecb13a555bae6e8bab3da25dbfb4df4b49ee431a98a49a86"),
}
ORIGINAL_REPLAY_V2 = {
    "observer": (4_402_063,
                 "ba0dc549b8b817dd4c58e8f17afb5760607e0294dc986d8eb29afe0e8d29fece"),
    "observer_status": (515,
                        "05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f"),
    "mwri_status": (89,
                    "4749c6ec9497a8b043555cc1b4dbfb8942543db4d222cda5d6ae5e5e3341544e"),
    "report": (151_591,
               "ad78fea8fa5740be90b43ce8f40f157ecb47ecbf87360a713e5b45787f8d40f9"),
}
DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
DOLPHIN_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
EXPECTED_SETUP_RECEIPT_SHA256 = "e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda"
GO_AFTER_PC = 0x8016B824
SOURCE_TICK_PC = 0x80390EB4
DRAW_ENTER_PC = 0x80390FC0
DRAW_RETURN_PC = 0x80391040
PAD_CONSUME_PC = 0x80377584
FIRST_CONSUME_SEQUENCE = 2504
SAMPLE_COUNT = 86
GO_SOURCE_ORDINAL = 84
FINAL_DRAW_SOURCE_ORDINAL = 85


class StadiumSetupCfScheduleError(ValueError):
    """An exact source, status, route, or schedule check refused."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise StadiumSetupCfScheduleError(message)


def _load_api(repo_root: str | Path) -> dict[str, Any]:
    root = Path(repo_root).resolve()
    tools = root / "tools"
    observer = root / "reference-capture" / "dolphin"
    _require(tools.is_dir() and observer.is_dir(),
             "repository root lacks existing MWRO/MWRI source validators")
    for path in (tools, observer):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    from reference_observer_stream import (  # noqa: PLC0415
        EVENT_NAMES,
        ObserverStreamError,
        iter_records,
        read_status,
    )
    from reference_input_stream import (  # noqa: PLC0415
        InputStreamError,
        validate_status,
        validate_stream,
    )
    from stadium_go_prefix import (  # noqa: PLC0415
        DRAW_RETURN_PC as PINNED_DRAW_RETURN_PC,
        EXPECTED_SETUP_RECEIPT_SHA256 as PINNED_SETUP_RECEIPT_SHA256,
        GO_AFTER_PC as PINNED_GO_AFTER_PC,
        SOURCE_TICK_PC as PINNED_SOURCE_TICK_PC,
        StadiumGoPrefixError,
        validate_stadium_go_prefix,
        validate_stadium_idle_state_snapshot,
        _state_slice,
    )
    from stadium_recorded_prefix_compare import (  # noqa: PLC0415
        compare_stadium_common_wire,
    )
    from whole_session_replay import (  # noqa: PLC0415
        WholeSessionReplayError,
        _consumed_ports,
    )
    _require((PINNED_DRAW_RETURN_PC, PINNED_SETUP_RECEIPT_SHA256,
              PINNED_GO_AFTER_PC, PINNED_SOURCE_TICK_PC) ==
             (DRAW_RETURN_PC, EXPECTED_SETUP_RECEIPT_SHA256,
              GO_AFTER_PC, SOURCE_TICK_PC),
             "existing strict Stadium validator constants changed")
    return locals()


def _identity(path: str | Path, expected: tuple[int, str], label: str) -> dict[str, Any]:
    source = Path(path)
    _require(not source.is_symlink() and source.is_file(),
             f"{label} is missing or is not an ordinary file")
    raw = source.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    _require((len(raw), digest) == expected,
             f"{label} size/hash differs from the retained source identity")
    return {"path": str(source), "bytes": len(raw), "sha256": digest}


def _read_json(path: str | Path, label: str, max_bytes: int = 8_000_000) -> dict[str, Any]:
    source = Path(path)
    _require(not source.is_symlink() and source.is_file() and
             source.stat().st_size <= max_bytes,
             f"{label} is missing, not ordinary, or exceeds its bound")
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise StadiumSetupCfScheduleError(f"{label} is malformed JSON") from error
    _require(isinstance(value, dict), f"{label} must be a JSON object")
    return value




def _counter(row: Mapping[str, Any], field: str, expected: int, label: str) -> None:
    payload = row.get("payload")
    _require(isinstance(payload, dict) and type(row.get(field)) is int and
             type(payload.get(field)) is int and row[field] == expected and
             payload[field] == expected,
             f"{label} {field} counter differs from the pinned source schedule")


def _pc_matches(value: Any, expected: int) -> bool:
    if type(value) is int:
        return value == expected
    if isinstance(value, str):
        try:
            return int(value, 16) == expected
        except ValueError:
            return False
    return False


def _boundary(row: Mapping[str, Any], *, sequence: int, boundary: str, pc: int,
              source_tick: int, draw_ordinal: int, event: str = "boundary") -> None:
    payload = row.get("payload")
    _require(row.get("seq") == sequence and row.get("event") == event and
             isinstance(payload, dict) and payload.get("boundary") == boundary and
             payload.get("pc") == pc,
             f"sequence {sequence} is not the expected {boundary} source row")
    _counter(row, "source_tick", source_tick, boundary)
    _counter(row, "draw_ordinal", draw_ordinal, boundary)


def _record_index(rows: Sequence[Mapping[str, Any]]) -> dict[int, Mapping[str, Any]]:
    indexed: dict[int, Mapping[str, Any]] = {}
    for index, row in enumerate(rows):
        sequence = row.get("seq")
        _require(type(sequence) is int and sequence == index,
                 "decoded MWRO rows are missing, reordered, or duplicated")
        _require(sequence not in indexed, "decoded MWRO row sequence is duplicated")
        indexed[sequence] = row
    return indexed


def _source_timeline(rows: Sequence[Mapping[str, Any]],
                     consumed_ports: Callable[[Mapping[str, Any], int], list[str]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    by_sequence = _record_index(rows)
    bundles: list[dict[str, Any]] = []
    source_events: list[dict[str, Any]] = []

    def get(sequence: int, label: str) -> Mapping[str, Any]:
        row = by_sequence.get(sequence)
        _require(isinstance(row, Mapping), f"source sequence {sequence} ({label}) is missing")
        return row

    for ordinal in range(SAMPLE_COUNT):
        # GO contributes one progress record after C, shifting only the next
        # sample's consume/tick/draw wire sequences by one.
        consume_sequence = FIRST_CONSUME_SEQUENCE + ordinal * 5 + int(ordinal > GO_SOURCE_ORDINAL)
        source_tick_sequence = consume_sequence + 1 + (1 if ordinal == GO_SOURCE_ORDINAL else 0)
        draw_enter_sequence = source_tick_sequence + 1
        draw_return_sequence = source_tick_sequence + 2
        consume = get(consume_sequence, "PAD consume")
        _boundary(consume, sequence=consume_sequence, boundary="pad_consume",
                  pc=PAD_CONSUME_PC, source_tick=ordinal, draw_ordinal=ordinal)
        try:
            ports = consumed_ports(consume, consume_sequence)
        except (KeyError, TypeError, ValueError) as error:
            raise StadiumSetupCfScheduleError(
                f"PAD consume {consume_sequence} has invalid typed source bytes: {error}") from error
        _require(isinstance(ports, list) and len(ports) == 4 and
                 all(isinstance(value, str) and len(value) == 22 and
                     all(char in "0123456789abcdef" for char in value)
                     for value in ports),
                 f"PAD consume {consume_sequence} did not yield four complete 11-byte statuses")
        bundles.append({
            "bundle": ordinal,
            "ports": [{"port": port, "status_hex": status}
                      for port, status in enumerate(ports)],
        })

        if ordinal == GO_SOURCE_ORDINAL:
            go_sequence = consume_sequence + 1
            go = get(go_sequence, "GO")
            go_payload = go.get("payload")
            _require(go.get("seq") == go_sequence and go.get("event") == "progress" and
                     isinstance(go_payload, dict) and go_payload.get("phase") == "go_after" and
                     _pc_matches(go_payload.get("pc"), GO_AFTER_PC),
                     "GO marker is missing or not between C consume and source tick")
            _counter(go, "source_tick", ordinal, "GO")
            _counter(go, "draw_ordinal", ordinal, "GO")
            go_reference = {"sequence": go_sequence, "event": "progress",
                            "phase": "go_after", "source_tick": ordinal,
                            "draw_ordinal": ordinal, "pc": hex(GO_AFTER_PC)}
        else:
            go_reference = None

        tick = get(source_tick_sequence, "SourceTick")
        _boundary(tick, sequence=source_tick_sequence, boundary="source_tick",
                  pc=SOURCE_TICK_PC, source_tick=ordinal, draw_ordinal=ordinal)
        draw_source_tick = ordinal + 1
        draw_enter = get(draw_enter_sequence, "DrawEnter")
        _boundary(draw_enter, sequence=draw_enter_sequence, boundary="draw_enter",
                  pc=DRAW_ENTER_PC, source_tick=draw_source_tick,
                  draw_ordinal=ordinal)
        draw_return = get(draw_return_sequence, "DrawReturn")
        _boundary(draw_return, sequence=draw_return_sequence, boundary="draw_return",
                  pc=DRAW_RETURN_PC, source_tick=draw_source_tick,
                  draw_ordinal=ordinal)

        poll_sequence = consume_sequence - 1 if ordinal else None
        if poll_sequence is not None:
            poll = get(poll_sequence, "preceding PAD poll")
            _boundary(poll, sequence=poll_sequence, boundary="pad_poll",
                      pc=0x8034DD8C, source_tick=ordinal, draw_ordinal=ordinal)
        source_events.append({
            "ordinal": ordinal,
            "consumed_pad": {"sequence": consume_sequence,
                             "event": consume.get("event"),
                             "source_tick": consume["source_tick"],
                             "draw_ordinal": consume["draw_ordinal"]},
            "go_after": go_reference,
            "source_tick": {"sequence": source_tick_sequence,
                            "source_tick": tick["source_tick"],
                            "draw_ordinal": tick["draw_ordinal"]},
            "draw_enter": {"sequence": draw_enter_sequence,
                           "source_tick": draw_enter["source_tick"],
                           "draw_ordinal": draw_enter["draw_ordinal"]},
            "draw_return": {"sequence": draw_return_sequence,
                            "source_tick": draw_return["source_tick"],
                            "draw_ordinal": draw_return["draw_ordinal"]},
            "preceding_pad_poll_sequence": poll_sequence,
        })

    _require(len(bundles) == 86 and len(source_events) == 86,
             "source extractor did not retain exactly 86 consume/tick/draw rows")
    _require(source_events[84]["go_after"] is not None and
             source_events[85]["go_after"] is None and
             source_events[85]["draw_return"]["sequence"] == 2933,
             "GO/C/F/final draw85 source membership differs")
    return bundles, source_events


def _boundary_rng_observations(
        rows: Sequence[Mapping[str, Any]],
        state_slice: Callable[..., dict[str, Any]]) -> list[dict[str, Any]]:
    """Retain direct source seed bytes at exact GO/C/F, for host comparison only."""
    indexed = _record_index(rows)
    observations: list[dict[str, Any]] = []
    for label, sequence, tick, draw, pc in (
            ("GO", 2925, 84, 84, GO_AFTER_PC),
            ("C", 2926, 84, 84, SOURCE_TICK_PC),
            ("F", 2931, 85, 85, SOURCE_TICK_PC)):
        row = indexed.get(sequence)
        _require(isinstance(row, Mapping), f"{label} RNG source row is missing")
        payload = row.get("payload")
        if label == "GO":
            _require(row.get("event") == "progress" and isinstance(payload, dict) and
                     payload.get("phase") == "go_after" and
                     _pc_matches(payload.get("pc"), pc),
                     "GO RNG source row has wrong event membership")
            _counter(row, "source_tick", tick, label)
            _counter(row, "draw_ordinal", draw, label)
        else:
            _boundary(row, sequence=sequence, boundary="source_tick", pc=pc,
                      source_tick=tick, draw_ordinal=draw)
        slices = payload.get("slices")
        _require(isinstance(slices, list) and
                 all(sum(isinstance(item, dict) and item.get("tag") == tag
                         for item in slices) == 1 for tag in (19, 20)),
                 f"{label} RNG requires exactly one pointer and one seed slice")
        try:
            pointer = state_slice(payload, 19, f"{label}.rng_pointer", 4)
            seed = state_slice(payload, 20, f"{label}.rng_seed", 4)
        except ValueError as error:
            raise StadiumSetupCfScheduleError(f"{label} typed RNG slice refused: {error}") from error
        _require(pointer["address"] == 0x804D5F94 and
                 int.from_bytes(pointer["raw"], "big") == seed["address"],
                 f"{label} RNG pointer does not own the observed seed")
        observations.append({
            "label": label, "sequence": sequence, "event": row["event"],
            "source_tick": tick, "draw_ordinal": draw, "pc": hex(pc),
            "pointer_slot_address": pointer["address"],
            "pointer_raw_hex": pointer["raw"].hex(),
            "seed_address": seed["address"], "seed_raw_hex": seed["raw"].hex(),
            "seed_hex": seed["raw"].hex(), "pointer_equals_seed_address": True,
        })
    return observations


def _runtime_schedule() -> list[dict[str, Any]]:
    schedule: list[dict[str, Any]] = []
    for ordinal in range(84):
        schedule.append({"action": "tick", "ordinal": ordinal, "bundle": ordinal})
        schedule.append({"action": "draw", "ordinal": ordinal})
    schedule.extend([
        {"action": "tick", "ordinal": 84, "bundle": 84},
        {"action": "draw", "ordinal": 84},
        {"action": "tick", "ordinal": 85, "bundle": 85},
    ])
    _require(len(schedule) == 171 and
             sum(action["action"] == "tick" for action in schedule) == 86 and
             sum(action["action"] == "draw" for action in schedule) == 85 and
             schedule[-1] == {"action": "tick", "ordinal": 85, "bundle": 85},
             "runtime action plan is not 86 ticks/85 draws ending before draw85")
    return schedule


def _source_report_identity(report: Mapping[str, Any], route: Mapping[str, Any],
                            input_summary: Mapping[str, Any], input_status: Mapping[str, Any]) -> None:
    _require(report.get("schema") == "melee-web-original-stadium-go-prefix-capture" and
             report.get("version") == 1 and report.get("status") == "pass" and
             report.get("scope") == "stadium_go_prefix",
             "original v6 capture report schema/status/scope differs")
    _require(report.get("stadium_go_prefix") == route,
             "original v6 report does not match the independently validated route")
    _require(report.get("input_stream") == input_summary and
             report.get("input_status") == input_status and
             input_status.get("mode") == "record",
             "original v6 report does not bind the exact complete recorded MWRI")
    _require(route.get("source", {}).get("dolphin_commit") == DOLPHIN_COMMIT and
             route.get("source", {}).get("dol_sha256") == DOL_SHA256 and
             route.get("setup_receipt_sha256") == EXPECTED_SETUP_RECEIPT_SHA256 and
             route.get("css_owner_sequence") == 838 and
             route.get("sss_owner_sequence") == 2467 and
             all(route.get("rng_pointer_value_snapshots", {}).get(label, {}).get("value_hex") == seed
                 for label, seed in (("css_entry", "312151c3"),
                                     ("css_return", "312151c3"),
                                     ("sss_entry", "3bb84c53"),
                                     ("sss_return", "3bb84c53"),
                                     ("sss_exit", "3bb84c53"))),
             "original v6 DOL, route owner, Setup, or Entry/SSS seed origin differs")


def _replay_report_identity(report: Mapping[str, Any], replay_route: Mapping[str, Any],
                            replay_input_status: Mapping[str, Any],
                            input_summary: Mapping[str, Any],
                            original_mwri_identity: Mapping[str, Any],
                            comparison: Mapping[str, Any],
                            state_summary: Mapping[str, Any]) -> None:
    _require(report.get("schema") == "melee-web-original-stadium-go-prefix-capture" and
             report.get("version") == 1 and report.get("status") == "pass" and
             report.get("scope") == "stadium_go_prefix" and
             report.get("input_mode") == "replay",
             "original replay report schema/status/scope/mode differs")
    _require(report.get("stadium_go_prefix") == replay_route and
             report.get("input_status") == replay_input_status and
             replay_input_status.get("mode") == "replay" and
             report.get("input_stream") == input_summary,
             "original replay report does not bind the exact replay MWRI and route")
    replay = report.get("input_replay")
    _require(isinstance(replay, dict) and
             replay.get("summary") == input_summary and
             replay.get("source_status_sha256") == original_mwri_identity["original_mwri_status"]["sha256"] and
             replay.get("source") == original_mwri_identity["original_mwri"]["path"],
             "original replay report does not bind the original recorded MWRI source/status")
    _require(comparison.get("decision") == "PASS_EXACT_STADIUM_COMMON_WIRE" and
             report.get("common_wire_comparison") == comparison,
             "independent exact-common-wire proof differs from the replay report")
    _require(report.get("stadium_idle_state") == state_summary and
             state_summary.get("decision") == "PASS_ORIGINAL_STATE_ONLY_COUNTERPART",
             "replay report does not bind the independently validated GO/C/F state companion")
    _require(replay_route.get("source", {}).get("dolphin_commit") == DOLPHIN_COMMIT and
             replay_route.get("source", {}).get("dol_sha256") == DOL_SHA256,
             "original replay DOL/Dolphin source identity differs")


def extract_stadium_setup_cf_input_schedule(
        repo_root: str | Path, *,
        original_observer: str | Path,
        original_observer_status: str | Path,
        original_report: str | Path,
        original_mwri: str | Path,
        original_mwri_status: str | Path,
        replay_observer: str | Path,
        replay_observer_status: str | Path,
        replay_mwri_status: str | Path,
        replay_report: str | Path) -> dict[str, Any]:
    """Return exact runtime PAD bundles/schedule and separate host-only proof."""

    api = _load_api(repo_root)
    identities = {
        "original_observer": _identity(original_observer, ORIGINAL_V6_SOURCE["observer"], "original v6 MWRO"),
        "original_observer_status": _identity(original_observer_status, ORIGINAL_V6_SOURCE["observer_status"], "original v6 status"),
        "original_report": _identity(original_report, ORIGINAL_V6_SOURCE["report"], "original v6 report"),
        "original_mwri": _identity(original_mwri, ORIGINAL_V6_SOURCE["mwri"], "original v6 MWRI"),
        "original_mwri_status": _identity(original_mwri_status, ORIGINAL_V6_SOURCE["mwri_status"], "original v6 MWRI status"),
        "replay_observer": _identity(replay_observer, ORIGINAL_REPLAY_V2["observer"], "original replay MWRO"),
        "replay_observer_status": _identity(replay_observer_status, ORIGINAL_REPLAY_V2["observer_status"], "original replay MWRO status"),
        "replay_mwri_status": _identity(replay_mwri_status, ORIGINAL_REPLAY_V2["mwri_status"], "original replay MWRI status"),
        "replay_report": _identity(replay_report, ORIGINAL_REPLAY_V2["report"], "original replay report"),
    }

    try:
        input_summary = api["validate_stream"](original_mwri)
        input_status = api["validate_status"](
            original_mwri_status, mode="record", events=4706, require_complete=True)
        replay_input_status = api["validate_status"](
            replay_mwri_status, mode="replay", events=4706, require_complete=True)
        original_observer_status_value = api["read_status"](original_observer_status)
        replay_observer_status_value = api["read_status"](replay_observer_status)
        original_route = api["validate_stadium_go_prefix"](
            original_observer, EXPECTED_SETUP_RECEIPT_SHA256, original_observer_status)
        replay_route = api["validate_stadium_go_prefix"](
            replay_observer, EXPECTED_SETUP_RECEIPT_SHA256, replay_observer_status)
        comparison = api["compare_stadium_common_wire"](
            original_observer, replay_observer,
            original_status_path=original_observer_status,
            candidate_status_path=replay_observer_status)
        state_summary = api["validate_stadium_idle_state_snapshot"](
            replay_observer, EXPECTED_SETUP_RECEIPT_SHA256, replay_observer_status)
    except (api["InputStreamError"], api["ObserverStreamError"], api["StadiumGoPrefixError"],
            OSError, ValueError) as error:
        if isinstance(error, StadiumSetupCfScheduleError):
            raise
        raise StadiumSetupCfScheduleError(f"existing strict source validator refused: {error}") from error

    _require(input_summary == {
        "version": 1, "sha256": ORIGINAL_V6_SOURCE["mwri"][1], "bytes": 188_296,
        "events": 4706, "first_emulated_tick": 123552,
        "last_emulated_tick": 9527249170,
        "ports": {"0": 2353, "1": 2353},
        "operations": {"pad_status": 4706, "adapter_connection": 0}, "complete": True,
    }, "original MWRI decoded summary differs from the pinned recording")
    _require(replay_input_status.get("complete") is True and
             replay_input_status.get("invalid") is False and
             replay_input_status.get("error") is None,
             "original replay MWRI status is not complete and valid")

    source_report_value = _read_json(original_report, "original v6 report")
    replay_report_value = _read_json(replay_report, "original replay report")
    _source_report_identity(source_report_value, original_route, input_summary, input_status)
    _replay_report_identity(replay_report_value, replay_route, replay_input_status,
                            input_summary, identities, comparison, state_summary)

    rows = list(api["iter_records"](original_observer))
    _require(len(rows) == original_route.get("stream_records"),
             "original MWRO decoded record count changed after strict route admission")
    try:
        bundles, source_events = _source_timeline(rows, api["_consumed_ports"])
    except api["WholeSessionReplayError"] as error:
        raise StadiumSetupCfScheduleError(f"typed source PAD decoder refused: {error}") from error

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "runtime_payload": {
            "schema": "melee-web-stadium-typed-pad-action-plan",
            "version": 1,
            "bundles": bundles,
            "schedule": _runtime_schedule(),
        },
        "host_only": {
            "boundary_rng_observations": _boundary_rng_observations(
                list(api["iter_records"](replay_observer)), api["_state_slice"]),
            "source_event_provenance": {
                "scope": "original source event counters, not runtime source clock labels",
                "events": source_events,
                "pairing_contract": {
                    "samples": 86,
                    "ticks": "source ordinals 0..85",
                    "draws": "source ordinals 0..85; includes final draw85 only as provenance",
                    "go_after": {"sequence": 2925, "source_tick": 84, "draw_ordinal": 84},
                    "C": {"source_tick": 84, "before_draw_ordinal": 84},
                    "F": {"source_tick": 85, "before_draw_ordinal": 85},
                    "runtime_stop": "after tick action ordinal85; before runtime draw ordinal85",
                },
            },
            "source_identities": {
                "original_v6_recorded_input": {
                    "kind": "original-retail-observer-and-recorded-MWRI",
                    "artifacts": {key: identities[key] for key in (
                        "original_observer", "original_observer_status", "original_report",
                        "original_mwri", "original_mwri_status")},
                    "mwri_summary": input_summary,
                    "mwri_status": input_status,
                    "strict_route_summary": original_route,
                    "route_owner_and_seed_origins": {
                        "css_owner_sequence": original_route["css_owner_sequence"],
                        "sss_owner_sequence": original_route["sss_owner_sequence"],
                        "sss_exit_seed_hex": original_route["rng_pointer_value_snapshots"]["sss_exit"]["value_hex"],
                        "dolphin_commit": DOLPHIN_COMMIT,
                        "dol_sha256": DOL_SHA256,
                    },
                },
                "original_v2_recorded_replay_proof": {
                    "kind": "exact-replay-of-original-MWRI-with-original-state-extension",
                    "artifacts": {key: identities[key] for key in (
                        "replay_observer", "replay_observer_status", "replay_mwri_status",
                        "replay_report")},
                    "replay_input_status": replay_input_status,
                    "strict_route_summary": replay_route,
                    "common_wire_comparison": comparison,
                    "state_companion_decision": state_summary["decision"],
                },
            },
            "expected_original_state": {
                "scope": "host-only validated raw original GO/C/F state; not runtime input",
                "state_observer_identity": identities["replay_observer"],
                "summary": state_summary,
                "initialization_gaps": {
                    "display_timer_initialization_observed": False,
                    "display_xF0_observed": False,
                    "native_pointer_numeric_comparison": False,
                },
            },
        },
    }


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in (
        "repo_root", "original_observer", "original_observer_status", "original_report",
        "original_mwri", "original_mwri_status", "replay_observer",
        "replay_observer_status", "replay_mwri_status", "replay_report", "output",
    ):
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    return parser.parse_args()


def main() -> int:
    args = _arguments()
    result = extract_stadium_setup_cf_input_schedule(
        args.repo_root,
        original_observer=args.original_observer,
        original_observer_status=args.original_observer_status,
        original_report=args.original_report,
        original_mwri=args.original_mwri,
        original_mwri_status=args.original_mwri_status,
        replay_observer=args.replay_observer,
        replay_observer_status=args.replay_observer_status,
        replay_mwri_status=args.replay_mwri_status,
        replay_report=args.replay_report,
    )
    output = Path(args.output)
    _require(not output.exists() and not output.is_symlink(),
             "refusing to overwrite an existing extracted packet")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, sort_keys=True, indent=2, allow_nan=False) + "\n",
                      encoding="utf-8")
    print(json.dumps({"decision": "PASS_EXACT_ORIGINAL_SETUP_CF_EXTRACTION",
                      "output": str(output),
                      "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                      "bundles": len(result["runtime_payload"]["bundles"]),
                      "actions": len(result["runtime_payload"]["schedule"])},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
