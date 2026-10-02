# SPDX-License-Identifier: MIT
"""Exact finalized Slippi timeline comparison for the local rollback testbed.

This reuses the strict parser's revision/finalization boundary. It compares
declared replay state, not an emulator savestate, pixels or PCM. Rollback
observations and fault schedules must be verified separately by the runner.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.slippi_format import SlippiTimeline, read_timeline


def first_difference(expected, actual, path=""):
    """Return the first structural/bit difference without numeric tolerances."""
    if type(expected) is not type(actual):
        return {"field": path, "expected": expected, "actual": actual}
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return {"field": path, "expected_keys": sorted(expected),
                    "actual_keys": sorted(actual)}
        for key in sorted(expected):
            difference = first_difference(expected[key], actual[key], f"{path}.{key}".lstrip("."))
            if difference:
                return difference
    elif isinstance(expected, (tuple, list)):
        if len(expected) != len(actual):
            return {"field": path, "expected_length": len(expected), "actual_length": len(actual)}
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
    elif expected != actual:
        return {"field": path, "expected": expected, "actual": actual}
    return None


def initial_match(timeline: SlippiTimeline):
    header = timeline.header
    return {"version": header.version, "game_info_hex": header.game_info.hex(),
            "random_seed": header.random_seed,
            "players": sorted(header.players, key=lambda player: player["port"]),
            "pal": header.pal, "frozen_stadium": header.frozen_stadium,
            "minor_scene": header.minor_scene, "major_scene": header.major_scene,
            "event_payload_sizes": header.event_payload_sizes}


def declared_frame(frame):
    record = frame.record()
    # The shared record format emphasizes input. Include the parser's pre-step
    # fighter position/facing too, without changing that shared capture schema.
    for sample, entry in zip(frame.inputs, record["inputs"]):
        entry["pre_position_bits"] = [f"0x{value:08x}" for value in sample.position_bits]
        entry["pre_facing_bits"] = f"0x{sample.facing_bits:08x}"
    return record


def validate_complete(timeline: SlippiTimeline):
    if not timeline.frames or timeline.game_end_method is None:
        raise ValueError("comparison requires a completed, nonempty game")
    if timeline.finalized_through is None or timeline.finalized_through < timeline.frames[-1].number:
        raise ValueError("comparison requires every compared frame to be finalized")
    for previous, current in zip(timeline.frames, timeline.frames[1:]):
        if current.number != previous.number + 1:
            raise ValueError("comparison requires a contiguous finalized timeline")
    ports = tuple(sorted(player["port"] for player in timeline.header.players))
    if ports != (1, 2):
        raise ValueError("first rollback profile requires exactly ports 1 and 2")
    if any(player["character_id"] != 8 or player["stocks"] != 4
           for player in timeline.header.players):
        raise ValueError("first rollback profile requires two four-stock Mario players")
    for frame in timeline.frames:
        if (tuple(sorted(value.port for value in frame.inputs)) != ports or
                tuple(sorted(value.port for value in frame.expected)) != ports or
                any(value.is_follower for value in (*frame.inputs, *frame.expected)) or
                any(not value.physical_complete for value in frame.inputs) or
                frame.start_random_seed is None or frame.scene_frame_counter is None):
            raise ValueError(f"frame {frame.number} lacks the declared two-player input/state/RNG/clock fields")
    stocks = [value.stocks for value in sorted(timeline.frames[-1].expected, key=lambda value: value.port)]
    if (stocks.count(0) != 1 or any(value is None or not 0 <= value <= 4 for value in stocks)
            or timeline.lras_initiator not in (None, -1)):
        raise ValueError("completed comparison requires a four-stock ending without an LRAS initiator")
    return stocks


def compare_timelines(expected: SlippiTimeline, actual: SlippiTimeline):
    expected_stocks = validate_complete(expected)
    actual_stocks = validate_complete(actual)
    result = {"result": "failed", "comparison": "all parsed declared finalized input/state fields, exact bits",
              "first_divergence": None, "compared_frames": 0,
              "expected_final_stocks": expected_stocks, "actual_final_stocks": actual_stocks,
              "expected_revision_updates": expected.duplicate_updates,
              "actual_revision_updates": actual.duplicate_updates,
              "exclusions": ["undeclared emulator memory", "unparsed Slippi payload fields",
                             "prediction/load/resimulation observation",
                             "rendering", "PCM", "physical controller hardware routing",
                             "network latency", "vanilla equivalence"]}
    difference = first_difference(initial_match(expected), initial_match(actual), "initial_match")
    if difference:
        result["first_divergence"] = difference
        return result
    for left, right in zip(expected.frames, actual.frames):
        # The parser retains event-time finalized revisions; never merge a
        # speculative input with a later post-frame update in this comparator.
        difference = first_difference(declared_frame(left), declared_frame(right), "frame")
        if difference:
            result["first_divergence"] = {"expected_frame": left.number, "actual_frame": right.number, **difference}
            return result
        result["compared_frames"] += 1
    if len(expected.frames) != len(actual.frames):
        result["first_divergence"] = {"field": "frame_count", "expected": len(expected.frames), "actual": len(actual.frames)}
        return result
    difference = first_difference(
        {"finalized_through": expected.finalized_through, "game_end_method": expected.game_end_method,
         "lras_initiator": expected.lras_initiator},
        {"finalized_through": actual.finalized_through, "game_end_method": actual.game_end_method,
         "lras_initiator": actual.lras_initiator}, "outcome")
    result["first_divergence"] = difference
    if not difference:
        result["result"] = "passed"
        result["first_frame"] = expected.frames[0].number
        result["last_frame"] = expected.frames[-1].number
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new comparison receipt; never overwritten")
    args = parser.parse_args(argv)
    # Reserve output before parsing so malformed captures retain a failure.
    with args.out.open("x", encoding="utf-8") as output:
        report = {"schema": "melee-web-slippi-finalized-comparison-v1", "result": "failed"}
        try:
            timelines = []
            identities = []
            for capture in (args.expected, args.actual):
                if capture.stat().st_size > 256 * 1024 * 1024:
                    raise ValueError("bounded comparison refuses a replay larger than 256 MiB")
                data = capture.read_bytes()
                identities.append(hashlib.sha256(data).hexdigest())
                import io
                timelines.append(read_timeline(io.BytesIO(data)))
            report.update(compare_timelines(*timelines))
            report["replay_sha256"] = {"expected": identities[0], "actual": identities[1]}
        except Exception as error:
            report["failure"] = f"{type(error).__name__}: {error}"
        output.write(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"result": report["result"], "first_divergence": report.get("first_divergence"),
                      "failure": report.get("failure")}))
    return 0 if report["result"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
