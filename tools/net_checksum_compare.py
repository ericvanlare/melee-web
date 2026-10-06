"""First-divergence comparator for networked-session per-tick checksum streams
(Track A1).

Input is the 64-byte little-endian record stream exported by
`_melee_web_net_checksum_drain` (see src/gameplay_net_checksum.h). A stream
passes only when it has the same length as its peer and every tick agrees in
every channel; there is no tolerance. The comparator never edits or
re-synchronizes either stream, and agreement on the declared channels does not
establish pixels, PCM, timing or retail equivalence.

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
    # The first divergence is the earliest mismatching tick in the gating
    # channels; name every gating channel that differs there.
    gating = [name for name, _ in CHANNEL_FIELDS if name != "objects"]
    firsts = [(mismatched[name][0], name) for name in gating if mismatched[name]]
    report["identical"] = not firsts and report["length_equal"] and not mismatched["objects"]
    report["gating_identical"] = not firsts and report["length_equal"]
    if firsts:
        tick = min(first_tick for first_tick, _ in firsts)
        differing = [name for name in gating if mismatched[name] and mismatched[name][0] == tick]
        report["first_divergence"] = {
            "tick": tick,
            "scene": SCENE_NAMES.get(first[tick]["scene"], str(first[tick]["scene"])),
            "channels": differing,
            "objects_also_differs": bool(mismatched["objects"] and mismatched["objects"][0] <= tick),
            "a": _render(first[tick]),
            "b": _render(second[tick]),
        }
    elif mismatched["objects"]:
        tick = mismatched["objects"][0]
        report["first_divergence"] = {
            "tick": tick, "scene": SCENE_NAMES.get(first[tick]["scene"], str(first[tick]["scene"])),
            "channels": ["objects"], "objects_also_differs": True,
            "a": _render(first[tick]), "b": _render(second[tick]),
        }
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
        if meta_path.exists():
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        path = path / "checksums.bin"
    return read_records(path.read_bytes()), meta


def compare_paths(a: Path, b: Path) -> dict:
    first, meta_a = load(a)
    second, meta_b = load(b)
    report = compare(first, second)
    report["scene_runs_a"] = scene_runs(first)
    report["scene_runs_b"] = scene_runs(second)
    report["scene_runs_equal"] = report["scene_runs_a"] == report["scene_runs_b"]
    start_a = meta_a and meta_a.get("start_record")
    start_b = meta_b and meta_b.get("start_record")
    report["start_record_equal"] = None if not (start_a and start_b) else start_a == start_b
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
    else:
        print("streams differ only in length")
    print(text if not args.json else f"report: {args.json}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
