#!/usr/bin/env python3
"""Strict selected-window RNG comparison for the two-lifetime v11 route.

Source coordinates select events; seeds and process addresses never select them.
The complete route and scene endpoints are checked by the browser consumer.
Unobserved actions and partial GO actions remain explicitly unpaired here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import compare_stadium_long_b_rng as semantics
from tools import compare_rng_owner_window as owners
from tools import stadium_rng_compact_live as compact

SCHEMA = "melee-web-stadium-paired-v11-rng-comparison-v1"
ROW_SCHEMA = "melee-web-stadium-paired-v11-row-v1"
require = semantics.require


def read_json(path):
    return semantics.parse_json(Path(path).read_bytes(), str(path))


def identity(path):
    path = Path(path)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def read_rows(path, label):
    require(Path(path).stat().st_size <= semantics.MAX_FILE_BYTES,
            f"{label}: exceeds existing comparison file bound")
    return semantics._read_jsonl(Path(path).read_bytes(), label)


def derive_windows(projection, decoded):
    require(projection.get("schema") == "melee-web-stadium-paired-window-projection-v1" and
            projection.get("source_schedule_verified") is True and
            len(projection.get("matches", [])) == 2,
            "projection is not its verified two-lifetime schedule")
    records = decoded["records"]
    require(decoded["owner_comparison_ready"] is True and decoded["unresolved_owner_events"] == 0,
            "original compact has unresolved owners")
    require(len(decoded["windows"]) == 2, "original compact lacks two windows")
    matches = []
    record_cursor = 0
    for mi, (match, window) in enumerate(zip(projection["matches"], decoded["windows"])):
        require(match.get("match_index") == window["match"] == mi,
                "original match order differs")
        w = match["window"]
        require(w["go_tick"] == window["go_tick"] and w["go_sequence"] == window["go_sequence"] and
                w["state_samples"] == 3501 and w["natural_tail_ticks"] == window["natural_tail_ticks"] and
                w["target_source_tick"]["sequence"] == window["target_source_sequence"] and
                w["target_source_tick"]["source_tick"] == window["target_source_tick"] and
                w["close"]["sequence"] == window["close_sequence"] and
                w["close"]["source_tick"] == window["close_tick"],
                f"match {mi}: projection/compact window differs")
        begin, end = match["rng_event_ordinals"]
        require(begin == record_cursor and end - begin == window["count"] and end <= len(records),
                f"match {mi}: original record ownership differs")
        selected = {}
        selected_count = 0
        tick = draw = 0
        prior_sequence = -1
        for ai, action in enumerate(match["actions"]):
            require(action.get("action_index") == ai and action.get("kind") in ("tick", "draw") and
                    action.get("source_tick") == tick and action.get("draw_ordinal") == draw and
                    type(action.get("after_sequence")) is int and
                    prior_sequence < action["after_sequence"] < action["through_sequence"],
                    f"match {mi} action {ai}: source order differs")
            prior_sequence = action["through_sequence"]
            if action["kind"] == "tick":
                tick += 1
            else:
                draw += 1
            full = (action["after_sequence"] >= window["containing_source_sequence"] and
                    action["through_sequence"] <= window["target_source_sequence"])
            interval = action.get("rng_event_ordinals")
            if interval is None:
                require(not full, f"match {mi} action {ai}: observed action has no RNG range")
                continue
            first, last = interval
            require(first == record_cursor and first <= last <= end,
                    f"match {mi} action {ai}: original RNG range is not contiguous")
            indexes = semantics._select_action_ordinals(
                records, first, last, action_index=ai,
                source_tick=action["source_tick"], draw_ordinal=action["draw_ordinal"],
                after_sequence=action["after_sequence"], through_sequence=action["through_sequence"],
                containing_sequence=window["containing_source_sequence"],
                target_sequence=window["target_source_sequence"])
            selected_first = first
            while selected_first < last and records[selected_first]["observer_next_sequence"] <= window["containing_source_sequence"]:
                selected_first += 1
            selected_last = selected_first
            while selected_last < last and records[selected_last]["observer_next_sequence"] <= window["target_source_sequence"]:
                selected_last += 1
            require(action.get("selected_rng_event_ordinals") == [selected_first, selected_last] and
                    indexes == list(range(selected_first, selected_last)),
                    f"match {mi} action {ai}: selected RNG range differs")
            require(all(records[i]["match"] == mi for i in range(first, last)),
                    f"match {mi} action {ai}: event belongs to another lifetime")
            if full:
                require(indexes == list(range(first, last)),
                        f"match {mi} action {ai}: full action includes a partial guard")
                selected[ai] = records[first:last]
                selected_count += last - first
            else:
                require(not indexes, f"match {mi} action {ai}: unsupported partially selected action")
            record_cursor = last
        require(record_cursor == end and selected_count == match["selected_rng_events"] and
                end - begin - selected_count == match["unpaired_guard_rng_events"],
                f"match {mi}: original selected/guard counts differ")
        matches.append({"match_index": mi, "actions": match["actions"], "selected": selected,
                        "selected_event_count": selected_count,
                        "unpaired_original_guard_events": end - begin - selected_count})
    require(record_cursor == len(records), "original compact has unassigned records")
    return matches


def load_reference(path, expected_sha):
    p = identity(path)
    require(p["sha256"] == expected_sha, "projection SHA-256 differs")
    projection = read_json(path)
    pins = {}
    for name, pin in projection["source_files"].items():
        source, data, sha = semantics._pin(pin, name)
        pins[name] = {"path": str(source), "bytes": len(data), "sha256": sha}
    raw = Path(pins["stadium-whole-paired-compact-rng.mwrl"]["path"]).read_bytes()
    require(hashlib.sha256(raw).hexdigest() == projection["compact_sha256"],
            "projection compact identity differs")
    decoded = compact.decode_live_compact(raw, capture_id="stadium-whole-paired-idle",
        sequence_id="primary2-paired-idle-scene-endpoints", dol_sha256=compact.DOL,
        primary_status=read_json(pins["native-primary-status.json"]["path"]))
    return derive_windows(projection, decoded), {"projection": p, "original_sources": pins}


def completed_actions(rows, matches):
    """Bind each raw probe cursor to one completed, same-owner native action."""
    result = []
    pending = None
    generations = {}
    for order, row in enumerate(rows):
        require(row.get("schema") == ROW_SCHEMA and row.get("record") == "stadium_paired_v11" and
                row.get("order") == order, "native row identity/order differs")
        kind = row.get("kind")
        if kind not in ("action_begin", "action_end"):
            require(pending is None, "native lifecycle observation interrupted an action")
            continue
        mi, ai = row.get("match_index"), row.get("local_action_index")
        require(type(mi) is int and 0 <= mi < 2 and type(ai) is int and
                0 <= ai < len(matches[mi]["actions"]), "native action identity out of range")
        expected = matches[mi]["actions"][ai]
        global_index = ai + sum(len(m["actions"]) for m in matches[:mi])
        require(row.get("diagnostic_action_index") == global_index and
                row.get("source_tick") == expected["source_tick"] and
                row.get("draw_ordinal") == expected["draw_ordinal"] and
                row.get("payload", {}).get("action") == expected["kind"],
                f"native action {global_index}: source coordinate differs")
        generation = row.get("world_generation")
        require(type(generation) is int and generation > 0, "native action generation missing")
        require(generations.setdefault(mi, generation) == generation,
                "native match changed world generation")
        if expected["kind"] == "tick":
            require(row.get("input_cursor") == expected["frame_index"],
                    f"native action {global_index}: literal input cursor differs")
        if kind == "action_begin":
            require(pending is None and global_index == len(result), "native action duplicated/reordered")
            pending = row
        else:
            require(pending is not None and all(row.get(key) == pending.get(key) for key in
                ("match_index", "local_action_index", "diagnostic_action_index", "world_generation",
                 "source_tick", "draw_ordinal", "input_cursor")), "native action lacks exact matching begin")
            result.append((pending, row)); pending = None
    require(pending is None and len(result) == sum(len(m["actions"]) for m in matches),
            "native action stream is incomplete")
    require(len(generations) == 2 and generations[0] != generations[1],
            "native lifetimes did not acquire distinct world generations")
    return result


def compare_browser(matches, rows, probes):
    actions = completed_actions(rows, matches)
    require(len(probes) == len(actions), "browser RNG action row count differs")
    reports = [{"match_index": m["match_index"], "compared_actions": 0,
                "selected_event_count": 0, "unpaired_original_guard_events": m["unpaired_original_guard_events"],
                "unpaired_browser_actions": 0, "unpaired_browser_events": 0} for m in matches]
    digests = [hashlib.sha256(), hashlib.sha256()]
    previous_seed = {}
    for index, ((begin, end), probe) in enumerate(zip(actions, probes)):
        mi, ai = begin["match_index"], begin["local_action_index"]
        require(probe.get("schema") == "melee-web-rng-draw-probe" and probe.get("version") == 3 and
                probe.get("source_cursor") == index and probe.get("overflowed") is False and
                isinstance(probe.get("draws"), list) and len(probe["draws"]) <= 1024,
                f"browser RNG action {index}: raw row identity/bound differs")
        before = begin["payload"].get("seed_before_hex")
        after = end["payload"].get("seed_after_hex")
        require(isinstance(before, str) and semantics.SEED.fullmatch(before) and
                isinstance(after, str) and semantics.SEED.fullmatch(after),
                f"browser action {index}: endpoint seed encoding differs")
        require(mi not in previous_seed or previous_seed[mi] == before,
                f"browser action {index}: seed changed between recorded actions")
        seed = int(before, 16)
        for ei, event in enumerate(probe["draws"]):
            require(event.get("kind") in ("HSD_Rand", "HSD_Randf"), "browser RNG primitive differs")
            seed = semantics._check_seed_transition(seed, event.get("seed_after"),
                                                    action_index=index, event_index=ei)
        require(seed == int(after, 16), f"browser action {index}: recorded RNG does not reach endpoint seed")
        previous_seed[mi] = after
        original = matches[mi]["selected"].get(ai)
        if original is None:
            reports[mi]["unpaired_browser_actions"] += 1
            reports[mi]["unpaired_browser_events"] += len(probe["draws"])
            continue
        normalized_original = [semantics._original_event(r, owners) for r in original]
        normalized_browser = [semantics._native_draw(r, index, ei, owners)
                              for ei, r in enumerate(probe["draws"])]
        compared = semantics._compare_action(normalized_original, normalized_browser, index, original)
        for event in compared:
            digests[mi].update(json.dumps(event, sort_keys=True, separators=(",", ":")).encode())
            digests[mi].update(b"\n")
        reports[mi]["compared_actions"] += 1
        reports[mi]["selected_event_count"] += len(compared)
    for mi, report in enumerate(reports):
        require(report["selected_event_count"] == matches[mi]["selected_event_count"],
                f"match {mi}: selected original records were not all compared")
        report["selected_pair_semantic_sha256"] = digests[mi].hexdigest()
    return reports


def run(args):
    matches, inputs = load_reference(args.projection, args.projection_sha256)
    reports = [{"match_index": m["match_index"], "actual_actions": len(m["actions"]),
                "selected_actions": len(m["selected"]), "selected_event_count": m["selected_event_count"],
                "unpaired_original_guard_events": m["unpaired_original_guard_events"]} for m in matches]
    status = "PASS_ORIGINAL_PREFLIGHT_ONLY"
    if args.browser_rows:
        rows = read_rows(args.browser_rows, "native rows")
        probes = read_rows(args.browser_rng, "browser RNG")
        reports = compare_browser(matches, rows, probes)
        inputs.update(browser_rows=identity(args.browser_rows), browser_rng=identity(args.browser_rng))
        status = "PASS_SELECTED_RNG_COMPARISON"
    return {"schema": SCHEMA, "status": status, "match_reports": reports, "inputs": inputs,
            "selected_event_count": sum(r["selected_event_count"] for r in reports),
            "unpaired_original_guard_events": sum(r["unpaired_original_guard_events"] for r in reports),
            "unpaired_browser_events": sum(r.get("unpaired_browser_events", 0) for r in reports) if args.browser_rows else None,
            "first_mismatch": None, "all_rng_writers_observed": False, "allocation_lifetime_parity": False,
            "whole_session_acceptance": False, "C3": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--projection", required=True, type=Path)
    parser.add_argument("--projection-sha256", required=True)
    parser.add_argument("--browser-rows", type=Path)
    parser.add_argument("--browser-rng", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    if bool(args.browser_rows) != bool(args.browser_rng):
        parser.error("browser rows and RNG must be supplied together")
    require(not args.out.exists(), "comparison output already exists")
    try:
        result = run(args)
    except Exception as exc:
        result = {"schema": SCHEMA, "status": "FAIL", "error": str(exc),
                  "first_mismatch": getattr(exc, "first_mismatch", {"kind": "validation", "detail": str(exc)}), "C3": False}
    with args.out.open("x") as stream:
        json.dump(result, stream, indent=2); stream.write("\n")
    print(json.dumps({"status": result["status"], "output": str(args.out)}))
    return int(result["status"] == "FAIL")


if __name__ == "__main__":
    raise SystemExit(main())
