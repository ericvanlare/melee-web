"""Extract the sealed run2 Stadium browser schedule and host expectations offline.

This tool reuses the repository's strict MWRO, consumed-PAD, StartMeleeData,
and idle-state validators. Runtime payloads carry only PAD bytes and action
indices; source identities and expected state are emitted separately for the
host comparator. Fixed artifact identities and count assertions bind this one
retained run2 receipt; this is not an arbitrary-run importer. It does not
launch Dolphin, a browser, or a server.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Callable, Mapping, Sequence


SCHEMA = "melee-web-stadium-idle-browser-schedule"
VERSION = 1
EXPECTED_CONTRACT_SHA256 = "1c416f9d03f5554b77b1e3bf3a11783f8ff2cd8d0cd21e635bfec322255e8ad5"
EXPECTED_SETUP_RECEIPT_SHA256 = "e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda"
EXPECTED_RUN_ARTIFACTS = {
    "observer.bin": (58_562_839, "45de31f20751b87414fe5ca5d3569940ce706bb029e30beb142f33fac546ff3a"),
    "observer-status.json": (521, "0a0f4cb3f1c5cc678f6fffda55396f96413406436686addec537d73576dd1f4e"),
    "report.json": (1_379_922, "be8dba769e0436e91435bc96014594015efe7646ddbf53a99da79c996cd53e22"),
    "idle3500-reference.json": (2_033_197, "562969a7e6a6e7fd43457821f84464852a38da2a83c9dc20672ad713cd6010bc"),
    "inputs.mwri": (753_496, "75e28ea0eba9f31e3e772e981b19eb27f3601ffc26580c5eed107c6ab1460690"),
    "input-status.json": (90, "e907af749995e2ff7b54c84d8e9e4932876b56ee0cb652d137faf4351c1f43e4"),
    "input-intentions.jsonl": (50_530, "6e0f09c0ed54125e36920099aa225a22a71797f6d2c85e598e91881315547cbb"),
    "menu-recipe.json": (6_155, "5f4dd95fb2e00cd35aa9fa42de10758acc4998624c964a1a077d683ebebe95f8"),
    "launch.json": (2_542, "ef6d99b6d62583cf7e3f3542780218e348bb2fa8bd9be956bebaaa3f4f32efb4"),
}
EXPECTED_PACKET_SHA256 = "496a92b47bf43fbe37d8710045707862ef4d2106f539a573ba8c90ff3a91a0b5"
EXPECTED_SOURCE_HEAD = "9e721b130168f863edb0e0839da72f14b49076c0"
EXPECTED_DOLPHIN_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
EXPECTED_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EXPECTED_DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
EXPECTED_TICKS = 3_585
EXPECTED_DRAWS = 3_582
MAX_PACKET_BYTES = 16 * 1024 * 1024


class StadiumIdleScheduleError(ValueError):
    """The retained same-run inputs cannot prove the declared schedule."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise StadiumIdleScheduleError(message)


def _hash_bytes(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _identity(path: str | Path, expected: tuple[int, str] | None = None,
              label: str = "artifact", *, hash_content: bool = True) -> dict[str, Any]:
    source = Path(path)
    _require(not source.is_symlink() and source.is_file(),
             f"{label} is missing, a symlink, or not a regular file")
    size = source.stat().st_size
    _require(expected is None or expected[0] is None or size == expected[0],
             f"{label} byte size differs from its retained identity")
    if hash_content:
        digest = _hash_bytes(source.read_bytes())
        _require(expected is None or digest == expected[1],
                 f"{label} hash differs from its retained identity")
    else:
        _require(expected is None, f"{label} cannot skip hashing a required identity")
        digest = None
    return {"path": str(source), "bytes": size, "sha256": digest,
            "hash_source": "computed" if hash_content else "owner_packet_record"}


def _read_json(path: str | Path, label: str, limit: int = 16 * 1024 * 1024) -> dict[str, Any]:
    source = Path(path)
    _require(not source.is_symlink() and source.is_file() and source.stat().st_size <= limit,
             f"{label} is missing, unsafe, or exceeds its size bound")
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise StadiumIdleScheduleError(f"{label} is malformed JSON") from error
    _require(isinstance(value, dict), f"{label} must be a JSON object")
    return value


def _counter(row: Mapping[str, Any], name: str, label: str) -> int:
    payload = row.get("payload")
    value = row.get(name)
    _require(isinstance(payload, Mapping) and type(value) is int and
             type(payload.get(name)) is int and payload[name] == value and value >= 0,
             f"{label} has missing or inconsistent {name} counters")
    return value


def _event(row: Mapping[str, Any]) -> tuple[str | None, str | None]:
    payload = row.get("payload")
    return row.get("event"), payload.get("boundary") if isinstance(payload, Mapping) else None


def _ports(consumed_ports: Callable[[Mapping[str, Any], int], list[str]],
           row: Mapping[str, Any]) -> list[str]:
    sequence = row.get("seq")
    _require(type(sequence) is int, "PAD consume has no raw sequence identity")
    try:
        result = consumed_ports(row, sequence)
    except Exception as error:
        # Preserve the original typed decoder's explanation within a bounded refusal.
        raise StadiumIdleScheduleError(f"PAD consume {sequence} typed decoder refused: {error}") from error
    _require(isinstance(result, list) and len(result) == 4 and all(
        isinstance(value, str) and re.fullmatch(r"[0-9a-f]{22}", value) is not None
        for value in result), f"PAD consume {sequence} is not four complete 11-byte ports")
    return result


def _build_schedule(rows: Sequence[Mapping[str, Any]],
                    consumed_ports: Callable[[Mapping[str, Any], int], list[str]], *,
                    setup_sequence: int, go_sequence: int, terminal_sequence: int,
                    c_tick: int, f_tick: int,
                    setup_pc: int = 0x8016E9C4, go_pc: int = 0x8016B824,
                    tick_pc: int = 0x80390EB4, draw_enter_pc: int = 0x80390FC0,
                    draw_return_pc: int = 0x80391040,
                    expected_ticks: int = EXPECTED_TICKS,
                    expected_draws: int = EXPECTED_DRAWS,
                    expected_post_go_counts: Mapping[str, int] | None = None) -> dict[str, Any]:
    """Pair actual raw rows and produce a source-order runtime plan.

    Source tick and draw ordinal remain independent. A draw is added only after
    its exact DrawEnter/DrawReturn pair, so adjacent ticks remain adjacent.
    """
    _require(c_tick >= 0 and f_tick == c_tick + 1,
             "strict route did not provide adjacent C/F source ticks")
    by_sequence: dict[int, Mapping[str, Any]] = {}
    for position, row in enumerate(rows):
        seq = row.get("seq")
        _require(type(seq) is int and seq == position and seq not in by_sequence,
                 "raw MWRO rows are missing, reordered, or duplicated")
        by_sequence[seq] = row
    setup = by_sequence.get(setup_sequence)
    _require(setup is not None and _event(setup) == ("boundary", "setup"),
             "strict route Setup sequence does not identify the same-run Setup row")
    _require(setup["payload"].get("pc") == setup_pc,
             "Setup PC differs from the checked source boundary")
    _require(terminal_sequence > setup_sequence and terminal_sequence in by_sequence,
             "terminal DrawReturn sequence is outside the same-run Setup interval")
    _require(go_sequence > setup_sequence and go_sequence < terminal_sequence,
             "GO sequence is outside the same-run Setup/terminal interval")

    bundles: list[dict[str, Any]] = []
    schedule: list[dict[str, Any]] = []
    bundle_provenance: list[dict[str, Any]] = []
    action_provenance: list[dict[str, Any]] = []
    pending_consume: Mapping[str, Any] | None = None
    pending_ports: list[str] | None = None
    open_draw: Mapping[str, Any] | None = None
    go_count = 0
    source_ticks: list[int] = []
    draw_ordinals: list[int] = []
    raw_post_go = {"source_tick": 0, "draw_enter": 0, "draw_return": 0}

    for row in rows[setup_sequence + 1:terminal_sequence + 1]:
        seq = row["seq"]
        event, boundary = _event(row)
        payload = row.get("payload")
        if event == "progress":
            if isinstance(payload, Mapping) and payload.get("phase") == "go_after":
                go_count += 1
                _require(seq == go_sequence and go_count == 1 and
                         pending_consume is not None and
                         _counter(pending_consume, "source_tick", "C PAD consume") == c_tick,
                         "GO marker is missing, duplicated, or differs from strict route")
                _require(_counter(row, "source_tick", "GO") == c_tick and
                         _counter(row, "draw_ordinal", "GO") >= 0 and
                         payload.get("pc") in (go_pc, hex(go_pc)),
                         "GO marker counters or PC differ from the strict source boundary")
                continue
            raise StadiumIdleScheduleError(f"unexpected progress event at raw sequence {seq}")
        _require(event == "boundary" and isinstance(payload, Mapping),
                 f"unexpected record class at raw sequence {seq}")
        source_tick = _counter(row, "source_tick", f"{boundary} row {seq}")
        draw_ordinal = _counter(row, "draw_ordinal", f"{boundary} row {seq}")

        if boundary == "pad_poll":
            continue
        if boundary == "pad_consume":
            _require(pending_consume is None and open_draw is None,
                     f"PAD consume {seq} overlaps a previous consume or open draw")
            _require((source_tick, draw_ordinal) == (len(source_ticks), len(draw_ordinals)),
                     f"PAD consume {seq} counters differ from the actual action prefix")
            pending_consume = row
            pending_ports = _ports(consumed_ports, row)
            continue
        if boundary == "source_tick":
            _require(payload.get("pc") == tick_pc,
                     f"SourceTick {seq} PC differs from the checked source routine")
            _require(pending_consume is not None and pending_ports is not None,
                     f"SourceTick {seq} has no preceding consumed PAD bundle")
            consume_tick = _counter(pending_consume, "source_tick", "PAD consume")
            consume_draw = _counter(pending_consume, "draw_ordinal", "PAD consume")
            _require((consume_tick, consume_draw) == (source_tick, draw_ordinal),
                     f"PAD consume {pending_consume['seq']} and SourceTick {seq} counters differ")
            expected_tick = len(source_ticks)
            _require(source_tick == expected_tick,
                     f"SourceTick coverage first diverges at raw sequence {seq}; expected {expected_tick}")
            if source_tick == c_tick:
                _require(go_count == 1 and pending_consume["seq"] < go_sequence < seq,
                         "GO is not between the exact C PAD consume and C SourceTick")
            else:
                _require(go_count == (1 if source_tick > c_tick else 0),
                         f"GO ordering differs at SourceTick {seq}")
            bundle_index = len(bundles)
            bundles.append({"bundle": bundle_index,
                            "ports": [{"port": port, "status_hex": status}
                                      for port, status in enumerate(pending_ports)]})
            bundle_provenance.append({
                "bundle": bundle_index,
                "source_tick": source_tick,
                "draw_ordinal": draw_ordinal,
                "consume_sequence": pending_consume["seq"],
                "source_tick_sequence": seq,
                "consume_event": "boundary",
                "source_tick_event": "boundary",
            })
            action_index = len(schedule)
            schedule.append({"action": "tick", "action_index": action_index,
                             "bundle": bundle_index})
            intervening = ([go_sequence] if pending_consume["seq"] < go_sequence < seq else [])
            action_provenance.append({
                "action_index": action_index,
                "action": "tick",
                "source_tick": source_tick,
                "draw_ordinal": draw_ordinal,
                "consume_sequence": pending_consume["seq"],
                "source_tick_sequence": seq,
                "intervening_progress_sequences": intervening,
            })
            source_ticks.append(source_tick)
            if source_tick >= c_tick:
                raw_post_go["source_tick"] += 1
            pending_consume = None
            pending_ports = None
            continue
        if boundary == "draw_enter":
            _require(payload.get("pc") == draw_enter_pc and open_draw is None and
                     pending_consume is None,
                     f"DrawEnter {seq} is malformed, nested, or splits a consume/tick pair")
            _require((source_tick, draw_ordinal) == (len(source_ticks), len(draw_ordinals)),
                     f"DrawEnter {seq} counters differ from the actual action prefix")
            open_draw = row
            continue
        if boundary == "draw_return":
            _require(payload.get("pc") == draw_return_pc and open_draw is not None,
                     f"DrawReturn {seq} has no exact open DrawEnter")
            enter_tick = _counter(open_draw, "source_tick", "DrawEnter")
            enter_ordinal = _counter(open_draw, "draw_ordinal", "DrawEnter")
            _require((enter_tick, enter_ordinal) == (source_tick, draw_ordinal),
                     f"DrawEnter {open_draw['seq']} and DrawReturn {seq} identities differ")
            expected_draw = len(draw_ordinals)
            _require(draw_ordinal == expected_draw,
                     f"draw ordinal first diverges at raw sequence {seq}; expected {expected_draw}")
            action_index = len(schedule)
            draw_index = len(draw_ordinals)
            schedule.append({"action": "draw", "action_index": action_index,
                             "draw": draw_index})
            action_provenance.append({
                "action_index": action_index,
                "action": "draw",
                "source_tick": source_tick,
                "draw_ordinal": draw_ordinal,
                "draw_enter_sequence": open_draw["seq"],
                "draw_return_sequence": seq,
            })
            draw_ordinals.append(draw_ordinal)
            if open_draw["seq"] > go_sequence:
                raw_post_go["draw_enter"] += 1
                raw_post_go["draw_return"] += 1
            open_draw = None
            continue
        raise StadiumIdleScheduleError(f"unexpected boundary {boundary!r} at raw sequence {seq}")

    _require(go_count == 1, "same-run schedule does not contain exactly one GO marker")
    _require(pending_consume is None and pending_ports is None and open_draw is None,
             "same-run schedule ends with an incomplete consume/tick or draw pair")
    _require(source_ticks == list(range(expected_ticks)),
             f"same-run SourceTick coverage is not exactly 0..{expected_ticks - 1}")
    _require(draw_ordinals == list(range(expected_draws)),
             f"same-run DrawEnter/Return coverage is not exactly 0..{expected_draws - 1}")
    _require(len(schedule) == expected_ticks + expected_draws and len(bundles) == expected_ticks,
             "source-order schedule count differs from the reviewed same-run contract")
    _require(sum(item["action"] == "tick" for item in schedule) == expected_ticks and
             sum(item["action"] == "draw" for item in schedule) == expected_draws,
             "combined action totals differ from the reviewed same-run contract")
    _require(source_ticks[c_tick] == c_tick and source_ticks[f_tick] == f_tick,
             "dynamic C/F observations do not map to their exact source-tick rows")
    if expected_post_go_counts is None:
        expected_post_go_counts = {"source_tick": 3_501, "draw_enter": 3_498,
                                   "draw_return": 3_498}
    _require(raw_post_go == expected_post_go_counts,
             "post-GO raw rows differ from the reviewed same-run contract")
    return {
        "runtime_payload": {
            "schema": "melee-web-stadium-pad-action-plan",
            "version": 1,
            "bundles": bundles,
            "schedule": schedule,
        },
        "bundle_provenance": bundle_provenance,
        "action_provenance": action_provenance,
        "counts": {
            "ticks": len(source_ticks),
            "combined_draws": len(draw_ordinals),
            "combined_driver_actions": len(schedule),
            "post_go_raw_rows": sum(raw_post_go.values()),
            "post_go_raw_row_counts": raw_post_go,
            "post_go_driver_actions": raw_post_go["source_tick"] + raw_post_go["draw_enter"],
            "full_setup_driver_actions": len(schedule),
            "final_draw_return_sequence": terminal_sequence,
        },
    }


def _load_and_validate_api(repo_root: Path) -> dict[str, Any]:
    tools = repo_root / "tools"
    observer = repo_root / "reference-capture" / "dolphin"
    _require(tools.is_dir() and observer.is_dir(),
             "repo root lacks current source validators")
    for path in (tools, observer):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    from reference_input_stream import validate_status as validate_input_status, validate_stream
    from reference_observer_stream import ObserverStreamError, iter_records
    from stadium_go_prefix import (
        DRAW_RETURN_PC,
        FIRST_CSS_DRAW_ENTER_PC,
        EXPECTED_SETUP_RECEIPT_SHA256,
        GO_AFTER_PC,
        MATCH_SETUP_TAG,
        SOURCE_TICK_PC,
        START_MELEE_RULES_STKIND_OFFSET,
        STADIUM_KIND,
        STADIUM_IDLE_STATE_TAGS,
        VS_SETUP_PC,
        StadiumGoPrefixError,
        _slice,
        _validate_stadium_idle_state_record,
        validate_stadium_go_prefix,
        validate_stadium_idle_3500_reference,
    )
    from transition_trace_format import decode_start_melee_data
    from whole_session_replay import _consumed_ports
    from authored_sd_reference_plan import canonical
    return locals()


def _owner_and_input_identities(run_root: Path, owner: dict[str, Any],
                                launch: dict[str, Any], contract: dict[str, Any],
                                api: Mapping[str, Any]) -> dict[str, Any]:
    capture_dir = run_root / "original-stadium-idle3500-reference-run2"
    owner_dir = run_root / "stadium-fresh-original-run2-owner"
    release_path = owner_dir / "root-one-launch-release.json"
    packet_path = owner_dir / "packet.json"
    packet_identity = _identity(packet_path, label="owner packet")
    release = _read_json(release_path, "root one-launch release")
    release_identity = _identity(release_path, label="root one-launch release")
    _require(release.get("authorized") is True and release.get("launches") == 1 and
             release.get("packet_sha256") == EXPECTED_PACKET_SHA256 and
             packet_identity["sha256"] == EXPECTED_PACKET_SHA256 and
             owner.get("source", {}).get("head") == EXPECTED_SOURCE_HEAD,
             "owner release/source head does not bind the retained one-launch run")

    artifacts: dict[str, Any] = {}
    contract_items = contract.get("reference_capture", {}).get("artifact_ids")
    _require(isinstance(contract_items, list), "contract lacks exact run artifact identities")
    expected_by_name = {item.get("path"): item for item in contract_items
                        if isinstance(item, dict)}
    _require(set(expected_by_name) == set(EXPECTED_RUN_ARTIFACTS),
             "reviewed contract artifact inventory changed")
    for name, identity in EXPECTED_RUN_ARTIFACTS.items():
        expected = expected_by_name[name]
        _require((expected.get("bytes"), expected.get("sha256")) == identity,
                 f"contract identity for {name} differs from the reviewed run")
        artifacts[name] = _identity(capture_dir / name, identity, f"run {name}")

    producer = owner["producer"]
    producer_binary = _identity(producer["binary"]["path"],
                                 (producer["binary"]["bytes"], producer["binary"]["sha256"]),
                                 "native producer binary")
    build_manifest = _identity(producer["manifest"]["path"],
                               (producer["manifest"]["bytes"], producer["manifest"]["sha256"]),
                               "native producer build manifest")
    _require(producer.get("native_source") == "3d4ee3d388877249b994543d93db788ed542e2a0",
             "native producer source commit differs from retained run")

    checked_source_files = _validate_source_manifest(owner.get("source", {}).get("files"),
                                                     packet_identity)

    profile = []
    for item in owner.get("profile", []):
        identity = _identity(item["path"], (item["bytes"], item["sha256"]),
                             "source profile " + Path(item["path"]).name)
        profile.append(identity)
    profile_contract = _profile_provenance(profile, launch.get("profile_sha256"), capture_dir)
    gci_source = owner["inputs"]["gci"]
    gci_original = _identity(gci_source["path"],
                             (gci_source["bytes"], gci_source["sha256"]), "profile GCI source")
    gci_copy = _identity(capture_dir / "gci-folder/USA/personal-after-reload.gci",
                         (gci_source["bytes"], gci_source["sha256"]), "run GCI copy")
    recipe_source = owner["inputs"]["recipe"]
    recipe_original = _identity(recipe_source["path"],
                                (recipe_source["bytes"], recipe_source["sha256"]),
                                "original input recipe")
    recipe_retained = artifacts["menu-recipe.json"]
    _require(launch.get("menu_recipe_sha256") == recipe_original["sha256"],
             "launch does not bind the original input recipe hash")
    original_recipe_value = _read_json(recipe_source["path"], "original input recipe")
    retained_recipe_value = _read_json(capture_dir / "menu-recipe.json", "retained run recipe")
    _require(original_recipe_value == retained_recipe_value,
             "retained canonical recipe JSON differs from original input recipe")
    _require(api["canonical"](original_recipe_value) == (capture_dir / "menu-recipe.json").read_bytes(),
             "retained recipe bytes are not the canonical original input serialization")
    disc = owner["inputs"]["disc"]
    _require(launch.get("command") and disc["path"] in launch["command"],
             "launch command does not use the owner packet's original disc")
    _require(launch.get("build", {}).get("sha256") == build_manifest["sha256"] and
             launch.get("build", {}).get("binary_sha256") == producer_binary["sha256"],
             "capture launch does not bind the retained producer binary/build manifest")

    input_summary = api["validate_stream"](str(capture_dir / "inputs.mwri"))
    input_status = api["validate_input_status"](capture_dir / "input-status.json",
                                                 mode="record", events=input_summary["events"],
                                                 require_complete=True)
    _require(input_status.get("complete") is True and input_status.get("invalid") is False,
             "recorded MWRI status is incomplete or invalid")
    return {
        "owner_packet": packet_identity,
        "one_launch_release": release_identity,
        "producer": {"native_source_commit": producer["native_source"],
                     "binary": producer_binary, "build_manifest": build_manifest},
        "source_and_reader_files": checked_source_files,
        "profile": profile_contract,
        "gci": {"original_profile_input": gci_original, "run_copy": gci_copy},
        "recipe": {"original_input": recipe_original, "retained_run_recipe": recipe_retained,
                   "parsed_json_equal": True,
                   "canonical_original_bytes_equal_retained": True,
                   "launch_recipe_sha256": launch["menu_recipe_sha256"]},
        "disc": {"owner_packet_identity": {"path": disc["path"],
                  "bytes": disc["bytes"], "sha256": disc["sha256"],
                  "hash_source": "owner_packet_record; 1.45GB image not re-read"},
                  "dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
                  "dol_sha1": EXPECTED_DOL_SHA1, "dol_sha256": EXPECTED_DOL_SHA256},
        "mwri_recording": {"artifact": artifacts["inputs.mwri"],
                           "status_artifact": artifacts["input-status.json"],
                           "summary": input_summary, "status": input_status,
                           "used_as_runtime_source_inputs": False},
        "input_intentions": {"artifact": artifacts["input-intentions.jsonl"],
                             "used_as_runtime_source_inputs": False},
        "source_consumed_pad": {
            "artifact": artifacts["observer.bin"],
            "four_port_bundles_derive_only_from": "MWRO PAD consume queue/slot observations",
            "not_substituted_from_mwri_or_intention_log": True,
        },
        "run_artifacts": artifacts,
    }


def _state_has_tags(row: Mapping[str, Any], tags: set[int]) -> bool:
    payload = row.get("payload")
    return isinstance(payload, Mapping) and any(
        isinstance(item, Mapping) and item.get("tag") in tags
        for item in payload.get("slices", []))


def _validate_source_manifest(source_files: Any,
                              packet_identity: Mapping[str, Any]) -> list[dict[str, Any]]:
    """Hash every declared source input from the exact retained owner packet."""
    _require(packet_identity.get("sha256") == EXPECTED_PACKET_SHA256 and
             isinstance(source_files, list) and bool(source_files),
             "owner source inventory is not bound to the reviewed packet")
    checked: list[dict[str, Any]] = []
    paths: set[str] = set()
    for item in source_files:
        _require(isinstance(item, dict) and isinstance(item.get("path"), str) and
                 type(item.get("bytes")) is int and item["bytes"] >= 0 and
                 isinstance(item.get("sha256"), str) and
                 re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is not None and
                 item["path"] not in paths,
                 "owner source file record is malformed")
        paths.add(item["path"])
        checked.append(_identity(item["path"], (item["bytes"], item["sha256"]),
                                 "owner source " + Path(item["path"]).name))
    _require(any(Path(item["path"]).name == "stadium_go_prefix.py" for item in checked),
             "sealed owner source inventory omits its strict host reader")
    return checked


def _profile_provenance(source_profile: Sequence[Mapping[str, Any]],
                        launch_hashes: Any, capture_dir: Path) -> dict[str, Any]:
    source_hashes = {f"Config/{Path(item['path']).name}": item["sha256"]
                     for item in source_profile}
    _require(isinstance(launch_hashes, dict) and launch_hashes == source_hashes,
             "launch profile hash map differs from the packet's prelaunch profile files")
    # Dolphin writes user configuration during the run. Preserve its current
    # bytes as a post-run observation; never mislabel them as launch inputs.
    postrun_user_config = []
    for relative in ("Config/Dolphin.ini", "Config/GCPadNew.ini"):
        path = capture_dir / "user" / relative
        if path.exists():
            postrun_user_config.append(_identity(path, label="post-run user config " + relative))
    return {"owner_prelaunch_profile_files": list(source_profile),
            "launch_profile_sha256": launch_hashes,
            "postrun_user_config_observations": postrun_user_config,
            "postrun_configs_are_not_prelaunch_profile_identity": True}


def _source_expectations(rows: Sequence[Mapping[str, Any]], route: Mapping[str, Any],
                         api: Mapping[str, Any], setup_sequence: int,
                         go_sequence: int, terminal_sequence: int,
                         existing_idle: Mapping[str, Any]) -> dict[str, Any]:
    setup_rows = [row for row in rows if _event(row) == ("boundary", "setup")]
    go_rows = [row for row in rows if _event(row) == ("progress", None) and
               row.get("payload", {}).get("phase") == "go_after"]
    _require(len(setup_rows) == 1 and setup_rows[0]["seq"] == setup_sequence,
             "same-run Setup row is missing or ambiguous")
    _require(len(go_rows) == 1 and go_rows[0]["seq"] == go_sequence,
             "same-run GO row is missing or ambiguous")
    terminal_rows = [row for row in rows if row.get("seq") == terminal_sequence and
                     _event(row) == ("boundary", "draw_return")]
    _require(len(terminal_rows) == 1, "strict route final DrawReturn is missing")

    setup_payload = setup_rows[0]["payload"]
    setup_slice = api["_slice"](setup_payload, api["MATCH_SETUP_TAG"], "same-run Setup", 0x138)
    start_data = setup_slice["raw"]
    _require(len(start_data) == 0x138, "same-run Setup StartMeleeData ABI is not 0x138")
    semantic = api["decode_start_melee_data"](start_data[:0xF0])
    stage_kind = int.from_bytes(start_data[
        api["START_MELEE_RULES_STKIND_OFFSET"]:
        api["START_MELEE_RULES_STKIND_OFFSET"] + 2], "big")
    _require(stage_kind == api["STADIUM_KIND"] and
             semantic["rules"]["stage_kind"] == api["STADIUM_KIND"],
             "same-run Setup semantic stage kind is not original Stadium")
    go_slice = api["_slice"](go_rows[0]["payload"], api["MATCH_SETUP_TAG"],
                             "GO match context", 0x138)
    _require(start_data == go_slice["raw"],
             "same-run Setup and GO StartMeleeData bytes do not match exactly")

    idle_rows: list[dict[str, Any]] = []
    state_by_tick: dict[int, dict[str, Any]] = {}
    state_tags = api["STADIUM_IDLE_STATE_TAGS"]
    digest = hashlib.sha256()
    c_tick = route["go_source_tick_C"]
    f_tick = route["first_full_post_go_source_tick_F"]
    for row in rows[setup_sequence + 1:terminal_sequence + 1]:
        if _event(row) != ("boundary", "source_tick"):
            continue
        tick = _counter(row, "source_tick", "state SourceTick")
        if tick < c_tick:
            _require(not _state_has_tags(row, state_tags),
                     f"typed idle state fields appear before GO at raw sequence {row['seq']}")
            continue
        role = "C" if tick == c_tick else "F" if tick == f_tick else "IDLE"
        snapshot = api["_validate_stadium_idle_state_record"](row, role, tick)
        idle_snapshot = api["_validate_stadium_idle_state_record"](row, "IDLE", tick)
        digest.update(json.dumps(idle_snapshot, sort_keys=True).encode())
        if tick in state_by_tick:
            raise StadiumIdleScheduleError(f"duplicate typed state row for source tick {tick}")
        state_by_tick[tick] = {
            "boundary_role": role,
            "raw_identity": {"sequence": row["seq"], "event": "boundary",
                             "boundary": "source_tick", "source_tick": tick,
                             "draw_ordinal": _counter(row, "draw_ordinal", "state SourceTick")},
            "typed_fields": snapshot,
        }
        idle_rows.append(row)

    expected_state_ticks = list(range(c_tick, route["comparison_end_tick"] + 1))
    _require(list(state_by_tick) == expected_state_ticks,
             "typed post-GO state rows do not cover every declared source tick")
    _require(digest.hexdigest() == existing_idle.get("state_observations_sha256"),
             "new per-tick typed expectations do not reproduce the retained state digest")
    _require(len(idle_rows) == 3_501,
             "typed expected state coverage differs from C through the recorded end tick")

    go_payload = go_rows[0]["payload"]
    go_state = api["_validate_stadium_idle_state_record"](
        go_rows[0], "GO", c_tick)
    setup_identity = {
        "sequence": setup_sequence,
        "event": "boundary",
        "boundary": "setup",
        "source_tick": _counter(setup_rows[0], "source_tick", "Setup"),
        "draw_ordinal": _counter(setup_rows[0], "draw_ordinal", "Setup"),
        "pc": setup_payload.get("pc"),
        "start_melee_data_address": setup_slice["address"],
        "start_melee_data_bytes": len(start_data),
        "start_melee_data_sha256": _hash_bytes(start_data),
    }
    return {
        "comparison_scope": {
            "setup_profile_verified_by_observer": False,
            "setup_phase_mapping": route.get("setup_phase_mapping"),
            "profile_equivalence": "pending; browser Setup receipt must be separately matched",
            "state_pointer_policy": "guest pointer numeric values are not cross-runtime fields",
            "pre_go_state_coverage": "No typed Stadium idle state expectation is declared for source ticks 0..83; only consumed PAD/tick/draw actions are present there.",
            "typed_post_go_state_coverage": {
                "source_tick_first": c_tick,
                "source_tick_last": route["comparison_end_tick"],
                "rows": len(idle_rows),
                "field_schema": "_validate_stadium_idle_state_record v2: map2, display, title, fixed-slot roles and validated pointer relationships",
                "expected_state_digest_matches_existing": digest.hexdigest(),
            },
            "not_observed_or_not_established": [
                "numeric guest pointer equality across runtimes",
                "map-root object types or allocation lifetimes",
                "display timer initialization or display xF0",
                "title table-entry relationships or title text bytes",
                "cross-owner RNG ledger completeness",
                "profile equivalence before a matching browser Setup receipt",
            ],
        },
        "setup": {
            "raw_identity": setup_identity,
            "typed_start_melee_data": semantic,
            "semantic_decoder_scope": "Defined retail StartMeleeData fields from bytes 0x00..0xef; the remaining 0x48 source bytes are bound by hash but not interpreted as semantic fields.",
            "stage_kind": stage_kind,
            "same_bytes_as_go_context": True,
        },
        "GO": {
            "raw_identity": {"sequence": go_sequence, "event": "progress",
                             "phase": "go_after", "source_tick": c_tick,
                             "draw_ordinal": _counter(go_rows[0], "draw_ordinal", "GO"),
                             "pc": go_payload.get("pc")},
            "typed_fields": go_state,
        },
        "C": state_by_tick[c_tick],
        "F": state_by_tick[f_tick],
        "per_source_tick": [state_by_tick[tick] for tick in expected_state_ticks],
        "terminal": {"sequence": terminal_sequence, "event": "boundary",
                     "boundary": "draw_return",
                     "source_tick": _counter(terminal_rows[0], "source_tick", "terminal DrawReturn"),
                     "draw_ordinal": _counter(terminal_rows[0], "draw_ordinal", "terminal DrawReturn"),
                     "pc": terminal_rows[0]["payload"].get("pc"),
                     "synthetic_tail_added": False},
        "constructor_observation": route["constructor_observations"],
    }


def _validate_retained_summaries(report: Mapping[str, Any],
                                 retained_idle: Mapping[str, Any],
                                 route: Mapping[str, Any],
                                 extracted_idle: Mapping[str, Any]) -> None:
    """Match the producer's exact summary projection and complete saved extract."""
    _require(report.get("stadium_go_prefix") == route,
             "capture report route differs from strict raw-source revalidation")
    _require(retained_idle == extracted_idle,
             "retained complete idle extract differs from strict raw-source revalidation")
    # capture_sd_reference_prefix.py writes these two arrays to the separate
    # idle-reference artifact and excludes only them from the report summary.
    summary = {key: value for key, value in retained_idle.items()
               if key not in ("literal_input_bundles", "source_action_schedule")}
    _require(report.get("original_idle_3500") == summary,
             "capture report idle summary differs from the exact producer projection")


def extract_same_run_schedule(repo_root: str | Path, run_root: str | Path) -> dict[str, Any]:
    """Strictly validate retained run2 inputs and build separated outputs."""
    repo = Path(repo_root).resolve()
    run = Path(run_root).resolve()
    contract_path = run / "stadium-idle-browser-contract-review-v2" / "contract.json"
    contract_identity = _identity(contract_path,
                                  (contract_path.stat().st_size, EXPECTED_CONTRACT_SHA256),
                                  "corrected contract")
    contract = _read_json(contract_path, "corrected contract")
    _require(contract.get("decision") == "SOURCE_ONLY_PROPOSED_BOUNDARY_NOT_IMPLEMENTED_OR_EXECUTED" and
             contract.get("offline_extractor_next_contract", {}).get("smallest_experiment") ==
             "Read retained run2 MWRO and retained metadata only. Reuse strict typed parsers and pairing checks; export complete same-run prefix and typed state expectations without launching any runtime.",
             "corrected offline extraction contract scope changed")
    owner_root = run / "stadium-fresh-original-run2-owner"
    owner = _read_json(owner_root / "packet.json", "one-run owner packet")
    capture = run / "original-stadium-idle3500-reference-run2"
    launch = _read_json(capture / "launch.json", "same-run launch record")
    api = _load_and_validate_api(repo)
    identities = _owner_and_input_identities(run, owner, launch, contract, api)
    observer = capture / "observer.bin"
    observer_status = capture / "observer-status.json"
    try:
        route = api["validate_stadium_go_prefix"](
            observer, EXPECTED_SETUP_RECEIPT_SHA256, observer_status,
            constructor_observations=True, original_idle_3500=True)
        idle = api["validate_stadium_idle_3500_reference"](
            observer, status_path=observer_status, extract_inputs=True)
    except (api["StadiumGoPrefixError"], api["ObserverStreamError"], OSError, ValueError) as error:
        raise StadiumIdleScheduleError(f"existing strict same-run MWRO validator refused: {error}") from error

    report = _read_json(capture / "report.json", "same-run capture report")
    retained_idle = _read_json(capture / "idle3500-reference.json", "same-run retained idle summary")
    _validate_retained_summaries(report, retained_idle, route, idle)
    _require(route.get("decision") == "PASS_ORIGINAL_RAW_3500_ROUTE_ONLY" and
             idle.get("decision") == "PASS_ORIGINAL_3500_IDLE_REFERENCE_ONLY" and
             route.get("whole_session_equivalent") is False and
             route.get("setup_profile_verified_by_observer") is False and
             route.get("source", {}).get("dol_sha256") == EXPECTED_DOL_SHA256 and
             route.get("source", {}).get("dol_sha1") == EXPECTED_DOL_SHA1 and
             route.get("source", {}).get("dolphin_commit") == EXPECTED_DOLPHIN_COMMIT,
             "strict same-run route identity/scope differs from the reviewed original-only evidence")

    rows = list(api["iter_records"](observer))
    setup_rows = [row for row in rows if _event(row) == ("boundary", "setup")]
    go_rows = [row for row in rows if _event(row) == ("progress", None) and
               row.get("payload", {}).get("phase") == "go_after"]
    terminal_sequence = route.get("draw_return_sequence")
    _require(len(setup_rows) == len(go_rows) == 1 and type(terminal_sequence) is int,
             "same-run Setup/GO/terminal row mapping is missing or ambiguous")
    setup_sequence, go_sequence = setup_rows[0]["seq"], go_rows[0]["seq"]
    schedule = _build_schedule(
        rows, api["_consumed_ports"], setup_sequence=setup_sequence,
        go_sequence=go_sequence, terminal_sequence=terminal_sequence,
        c_tick=route["go_source_tick_C"],
        f_tick=route["first_full_post_go_source_tick_F"],
        setup_pc=api["VS_SETUP_PC"], go_pc=api["GO_AFTER_PC"],
        tick_pc=api["SOURCE_TICK_PC"],
        draw_enter_pc=api["FIRST_CSS_DRAW_ENTER_PC"], draw_return_pc=api["DRAW_RETURN_PC"])

    # Cross-check the post-GO literal bundles and raw boundary rows against the
    # existing strict idle extractor, without borrowing its earlier-run schedule.
    post_go_bundles = schedule["runtime_payload"]["bundles"][route["go_source_tick_C"]:]
    source_post_go_bundles = idle["literal_input_bundles"]
    _require(len(post_go_bundles) == len(source_post_go_bundles) == 3_501,
             "post-GO input bundle count differs from the strict idle extractor")
    for bundle, source in zip(post_go_bundles, source_post_go_bundles):
        _require(bundle["bundle"] == source["ordinal"] + route["go_source_tick_C"] and
                 [port["status_hex"] for port in bundle["ports"]] ==
                 [port["status_hex"] for port in source["ports"]] and
                 schedule["bundle_provenance"][bundle["bundle"]]["consume_sequence"] ==
                 source["consume_sequence"],
                 "new post-GO bundle does not match the strict idle source extraction")
    our_post_actions = [item for item in schedule["action_provenance"]
                        if (item["action"] == "tick" and item["source_tick"] >= route["go_source_tick_C"]) or
                        (item["action"] == "draw" and item["draw_enter_sequence"] > go_sequence)]
    old_raw_actions = idle["source_action_schedule"]
    old_combined: list[dict[str, Any]] = []
    old_index = 0
    while old_index < len(old_raw_actions):
        previous = old_raw_actions[old_index]
        if previous["action"] == "tick":
            old_combined.append({"action": "tick", "sequence": previous["sequence"],
                                 "draw_ordinal": previous["draw_ordinal"],
                                 "source_tick": route["go_source_tick_C"] + previous["ordinal"]})
            old_index += 1
            continue
        _require(previous["action"] == "draw_enter" and old_index + 1 < len(old_raw_actions),
                 "strict idle extractor emitted an unpaired draw boundary")
        returned = old_raw_actions[old_index + 1]
        _require(returned["action"] == "draw_return" and
                 returned["draw_ordinal"] == previous["draw_ordinal"] and
                 returned["ordinal"] == previous["ordinal"],
                 "strict idle extractor DrawEnter/Return pair differs")
        old_combined.append({"action": "draw", "sequence": previous["sequence"],
                             "return_sequence": returned["sequence"],
                             "draw_ordinal": previous["draw_ordinal"]})
        old_index += 2
    _require(len(our_post_actions) == len(old_combined),
             "post-GO combined source action rows differ from the strict idle extractor")
    for current, previous in zip(our_post_actions, old_combined):
        _require(current["action"] == previous["action"] and
                 current["draw_ordinal"] == previous["draw_ordinal"] and
                 ((current.get("source_tick_sequence") == previous["sequence"] and
                   current.get("source_tick") == previous["source_tick"])
                  if current["action"] == "tick" else
                  (current.get("draw_enter_sequence") == previous["sequence"] and
                   current.get("draw_return_sequence") == previous["return_sequence"])),
                 "post-GO action order/raw sequence differs from strict idle extractor")

    expectations = _source_expectations(
        rows, route, api, setup_sequence, go_sequence, terminal_sequence, idle)
    _require(expectations["terminal"]["draw_ordinal"] == EXPECTED_DRAWS - 1 and
             expectations["terminal"]["source_tick"] == route["comparison_end_tick"] + 1,
             "terminal natural DrawReturn differs from the actual final tick/draw boundary")

    summary = {
        "scope": "Offline extraction of one retained original Stadium Setup→GO→C+3500 run; not browser execution or acceptance.",
        "decision": "PASS_SAME_RUN_OFFLINE_SCHEDULE_AND_TYPED_EXPECTATIONS",
        "route_decision": route["decision"],
        "idle_decision": idle["decision"],
        "setup_profile_verified_by_observer": False,
        "whole_session_equivalent": False,
        "native_parity": False,
        "browser_execution": False,
        "counts": schedule["counts"],
        "observed_dynamic_boundaries": {
            "setup_sequence": setup_sequence,
            "go_sequence": go_sequence,
            "C_source_tick": route["go_source_tick_C"],
            "F_source_tick": route["first_full_post_go_source_tick_F"],
            "comparison_end_source_tick": route["comparison_end_tick"],
            "final_draw_return_sequence": terminal_sequence,
        },
        "known_source_only_limits": [
            "The fixed 171-action native C/F queue does not support this 7167-action schedule.",
            "No browser A, lifetime B, allocation-generation, registration-event, or cross-owner RNG ledger is established.",
            "The source reports setup_profile_verified_by_observer=false; browser Setup equivalence remains a separate gate.",
            "Expected typed state begins at GO/C source tick 84; source ticks 0..83 have consumed inputs/actions but no declared typed Stadium idle-state comparison.",
            "No pixels, PCM, live timing, performance, full-session, C3, or tournament acceptance claim is made.",
        ],
    }
    return {
        "runtime_payload": schedule["runtime_payload"],
        "host_only_expectations": {
            "schema": "melee-web-stadium-source-host-expectations",
            "version": 1,
            "source_identity": {
                "contract": contract_identity,
                "source_run": identities,
                "strict_route": {
                    key: route[key] for key in (
                        "decision", "setup_phase_mapping", "setup_profile_verified_by_observer",
                        "whole_session_equivalent", "source", "constructor_observations",
                        "go_source_tick_C", "first_full_post_go_source_tick_F",
                        "comparison_end_tick", "draw_return_sequence", "stream_bytes",
                        "stream_records", "stream_sha256", "source_setup_phase_raw_sha256")
                },
            },
            "bundle_provenance": schedule["bundle_provenance"],
            "action_provenance": schedule["action_provenance"],
            "expected_state": expectations,
            "recorded_mwri_and_intentions_are_separate_from_source_consumed_pad": True,
        },
        "summary": summary,
    }


def _write_json(path: Path, value: Any) -> dict[str, Any]:
    raw = (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode()
    _require(len(raw) <= MAX_PACKET_BYTES,
             f"generated {path.name} exceeds the bounded output size")
    path.write_bytes(raw)
    return {"path": str(path), "bytes": len(raw), "sha256": _hash_bytes(raw)}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--run-root", type=Path, required=True,
                        help="external RUN directory holding the corrected contract and one-run evidence")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new, empty output directory outside the checkout")
    args = parser.parse_args(argv)
    output = args.output_dir
    if output.exists():
        if output.is_symlink() or not output.is_dir() or any(output.iterdir()):
            print("refused: output directory must be new or empty and not a symlink", file=sys.stderr)
            return 2
    else:
        output.mkdir(parents=True, exist_ok=False)
    try:
        packet = extract_same_run_schedule(args.repo_root, args.run_root)
        runtime_identity = _write_json(output / "runtime-payload.json", packet["runtime_payload"])
        host_identity = _write_json(output / "host-only-expectations.json",
                                    packet["host_only_expectations"])
        summary_identity = _write_json(output / "report.json", packet["summary"])
        manifest = {
            "schema": "melee-web-stadium-idle-browser-schedule-manifest",
            "version": 1,
            "decision": packet["summary"]["decision"],
            "runtime_payload": runtime_identity,
            "host_only_expectations": host_identity,
            "report": summary_identity,
            "separation": "Host-only expected state and source provenance are excluded from runtime-payload.json.",
            "bounded_offline_only": True,
        }
        manifest_identity = _write_json(output / "manifest.json", manifest)
        print(json.dumps({"decision": manifest["decision"],
                          "output": str(output), "manifest": manifest_identity,
                          "counts": packet["summary"]["counts"]}, sort_keys=True))
        return 0
    except Exception as error:
        refusal = {"schema": "melee-web-stadium-idle-browser-schedule-refusal",
                   "version": 1, "decision": "REFUSED_FIRST_FAILURE",
                   "error_type": type(error).__name__,
                   "message": str(error)[:2_000], "no_runtime_launched": True}
        try:
            _write_json(output / "first-failure.json", refusal)
        except Exception as write_error:
            print(f"refused: {error}; failure packet write also failed: {write_error}",
                  file=sys.stderr)
            return 2
        print(json.dumps(refusal, sort_keys=True), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
