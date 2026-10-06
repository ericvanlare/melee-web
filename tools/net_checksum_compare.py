"""First-divergence comparator for networked-session per-tick checksum streams
(Track A1).

Input is the 64-byte little-endian record stream exported by
`_melee_web_net_checksum_drain` (see src/gameplay_net_checksum.h). A stream
passes only when it has the same length as its peer and every tick agrees in
every channel; there is no tolerance. The comparator never edits or
re-synchronizes either stream, and agreement on the declared channels does not
establish pixels, PCM, timing or retail equivalence. Instance directories must
contain a complete instance report whose record count matches a nonempty stream.
When both reports are present, their pre-first-tick `start_record` values must
also match for the pair to pass. That record covers the declared start-state
fields; it does not identify the build or disc, which the evidence receipt binds
separately.

Channels, in the order a first divergence is named:
  header       tick, scene, RNG seed, match frame count
  input        the four raw 11-byte PAD records the owner consumed
  master_pad   HSD master PAD status after the owner consumed the tick
  scene_state  fighter fields in the match, CSS/SSS selection data in menus
  total        FNV-1a64 over header + input + master_pad + scene_state
  objects      supplementary: GObj link identity and root transforms
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

RECORD = struct.Struct("<6I5Q")
RECORD_BYTES = 64
assert RECORD.size == RECORD_BYTES
FIELDS = ("tick", "scene", "seed", "frame", "flags", "objects",
          "input", "pad", "scene_state", "object_state", "total")
HEADER_FIELDS = ("tick", "scene", "seed", "frame")
CHANNEL_FIELDS = (
    ("header", HEADER_FIELDS),
    ("input", ("input",)),
    ("master_pad", ("pad",)),
    ("scene_state", ("scene_state", "flags")),
    ("total", ("total",)),
    ("objects", ("object_state", "objects")),
)
SCENE_NAMES = {1: "css", 2: "sss", 3: "match", 4: "results", 5: "prize"}


class StreamError(ValueError):
    pass


def read_records(data: bytes) -> list[dict]:
    if not data:
        raise StreamError("stream is empty; zero records cannot establish agreement")
    if len(data) % RECORD_BYTES:
        raise StreamError(f"stream length {len(data)} is not a multiple of {RECORD_BYTES}")
    rows = []
    for index in range(len(data) // RECORD_BYTES):
        row = dict(zip(FIELDS, RECORD.unpack_from(data, index * RECORD_BYTES)))
        if row["tick"] != index:
            raise StreamError(f"record {index} carries tick {row['tick']}; streams must be contiguous from tick 0")
        rows.append(row)
    return rows


def _hex(value: int) -> str:
    return f"{value:016x}"


def _render(row: dict) -> dict:
    out = {name: row[name] for name in ("tick", "scene", "seed", "frame", "flags", "objects")}
    out["scene_name"] = SCENE_NAMES.get(row["scene"], str(row["scene"]))
    for name in ("input", "pad", "scene_state", "object_state", "total"):
        out[name] = _hex(row[name])
    return out


def compare(first: list[dict], second: list[dict]) -> dict:
    """Return per-channel first divergence and mismatch counts."""
    if not first or not second:
        raise StreamError("both streams must contain at least one checksum record")
    shared = min(len(first), len(second))
    mismatched = {name: [] for name, _ in CHANNEL_FIELDS}
    for index in range(shared):
        a, b = first[index], second[index]
        for name, fields in CHANNEL_FIELDS:
            if any(a[field] != b[field] for field in fields):
                mismatched[name].append(index)
    report = {
        "ticks_a": len(first),
        "ticks_b": len(second),
        "ticks_compared": shared,
        "length_equal": len(first) == len(second),
        "channels": {},
    }
    for name, _ in CHANNEL_FIELDS:
        ticks = mismatched[name]
        report["channels"][name] = {
            "mismatched_ticks": len(ticks),
            "first_mismatched_tick": ticks[0] if ticks else None,
        }
    gating = [name for name, _ in CHANNEL_FIELDS if name != "objects"]
    gating_firsts = [(mismatched[name][0], name) for name in gating if mismatched[name]]
    all_firsts = [(ticks[0], name) for name, ticks in mismatched.items() if ticks]
    if not report["length_equal"]:
        all_firsts.append((shared, "stream_length"))
        gating_firsts.append((shared, "stream_length"))
        report["channels"]["stream_length"] = {
            "mismatched_ticks": 1,
            "first_mismatched_tick": shared,
        }

    def divergence_at(first_ticks):
        if not first_ticks:
            return None
        tick = min(first_tick for first_tick, _ in first_ticks)
        differing = [name for first_tick, name in first_ticks if first_tick == tick]
        row_a = first[tick] if tick < len(first) else None
        row_b = second[tick] if tick < len(second) else None
        scene_row = row_a or row_b
        return {
            "tick": tick,
            "scene": SCENE_NAMES.get(scene_row["scene"], str(scene_row["scene"])) if scene_row else "end",
            "channels": differing,
            "objects_also_differs": bool(mismatched["objects"] and mismatched["objects"][0] <= tick),
            "a": _render(row_a) if row_a else None,
            "b": _render(row_b) if row_b else None,
        }

    report["first_divergence"] = divergence_at(all_firsts)
    report["first_gating_divergence"] = divergence_at(gating_firsts)
    report["stream_identical"] = report["first_divergence"] is None
    report["identical"] = report["stream_identical"]
    report["gating_identical"] = report["first_gating_divergence"] is None
    if not report["length_equal"]:
        report["length_note"] = (
            f"stream A has {len(first)} ticks and stream B has {len(second)}; "
            "the longer stream's extra ticks have no peer")
    return report


def scene_runs(rows: list[dict]) -> list[dict]:
    runs = []
    for row in rows:
        if not runs or runs[-1]["scene"] != row["scene"]:
            runs.append({"scene": row["scene"], "name": SCENE_NAMES.get(row["scene"], str(row["scene"])),
                         "first_tick": row["tick"], "ticks": 0})
        runs[-1]["ticks"] += 1
    return runs


def compare_arena(first, second) -> dict:
    """Compare the optional per-scene-entry arena hashes of two instances."""
    if first is None or second is None:
        return {"available": False}
    rows = []
    for index in range(max(len(first), len(second))):
        a = first[index] if index < len(first) else None
        b = second[index] if index < len(second) else None
        rows.append({
            "tick_a": a and a["tick"], "tick_b": b and b["tick"],
            "scene_a": a and a["scene"], "scene_b": b and b["scene"],
            "base_equal": bool(a and b and a["base"] == b["base"]),
            "bytes_equal": bool(a and b and a["bytes"] == b["bytes"]),
            "hash_equal": bool(a and b and a["hash"] == b["hash"]),
        })
    return {"available": True, "entries": rows,
            "all_hashes_equal": all(row["hash_equal"] for row in rows),
            "all_bases_equal": all(row["base_equal"] for row in rows)}


def load(path: Path):
    """Accept a checksum file or a harness instance directory."""
    meta = None
    if path.is_dir():
        meta_path = path / "instance.json"
        if not meta_path.is_file():
            raise StreamError(f"instance directory is missing {meta_path.name}: {path}")
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            raise StreamError(f"invalid instance metadata in {meta_path}: {error}") from error
        if not isinstance(meta, dict) or meta.get("outcome") != "complete":
            outcome = meta.get("outcome") if isinstance(meta, dict) else None
            raise StreamError(f"instance metadata is not complete at {path} (outcome: {outcome})")
        path = path / "checksums.bin"
    rows = read_records(path.read_bytes())
    if meta is not None:
        records = meta.get("records")
        total_ticks = meta.get("total_ticks")
        if type(records) is not int or records != len(rows):
            raise StreamError(f"instance metadata record count disagrees with checksum stream at {path.parent}")
        if type(total_ticks) is not int or total_ticks != len(rows):
            raise StreamError(f"instance metadata is incomplete at {path.parent}: total_ticks does not match the stream")
        if meta.get("first_error") is not None:
            raise StreamError(f"instance metadata records an error at {path.parent}: {meta['first_error']}")
        if meta.get("browser_closed") is not True:
            raise StreamError(f"instance metadata does not confirm browser cleanup at {path.parent}")
        start = meta.get("start_record")
        required_start_fields = {
            "recorded", "scene", "seed", "frame", "total", "pad",
            "scene_state", "object_state", "objects", "flags",
        }
        if (not isinstance(start, dict) or not required_start_fields.issubset(start) or
                type(start.get("recorded")) is not int or start["recorded"] != 1):
            raise StreamError(f"instance metadata is missing its recorded agreed start context at {path.parent}")
    return rows, meta


def compare_paths(a: Path, b: Path) -> dict:
    first, meta_a = load(a)
    second, meta_b = load(b)
    report = compare(first, second)
    report["scene_runs_a"] = scene_runs(first)
    report["scene_runs_b"] = scene_runs(second)
    report["scene_runs_equal"] = report["scene_runs_a"] == report["scene_runs_b"]
    start_a = meta_a and meta_a.get("start_record")
    start_b = meta_b and meta_b.get("start_record")
    report["start_record_equal"] = None if start_a is None or start_b is None else start_a == start_b
    if report["start_record_equal"] is False:
        report["identical"] = False
        report["gating_identical"] = False
        report["start_context_mismatch"] = {"a": start_a, "b": start_b}
    report["arena"] = compare_arena(meta_a and meta_a.get("arena"), meta_b and meta_b.get("arena"))
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("a", type=Path, help="checksums.bin or instance directory")
    parser.add_argument("b", type=Path)
    parser.add_argument("--json", type=Path, help="write the report to a new file")
    args = parser.parse_args(argv)
    try:
        report = compare_paths(args.a, args.b)
    except (StreamError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    text = json.dumps(report, indent=2)
    if args.json:
        with args.json.open("x", encoding="utf-8") as handle:
            handle.write(text + "\n")
    divergence = report.get("first_divergence")
    if report["identical"]:
        print(f"identical: {report['ticks_compared']} ticks, zero mismatched ticks in every channel")
        return 0
    if divergence:
        print(f"DIVERGED at tick {divergence['tick']} ({divergence['scene']}): "
              f"channels {', '.join(divergence['channels'])}")
        gating = report.get("first_gating_divergence")
        if gating and gating["tick"] != divergence["tick"]:
            print(f"first gating divergence at tick {gating['tick']} ({gating['scene']}): "
                  f"channels {', '.join(gating['channels'])}")
    elif report.get("start_record_equal") is False:
        print("streams agree per tick, but the agreed start contexts differ")
    else:
        print("streams differ only in length")
    print(text if not args.json else f"report: {args.json}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
