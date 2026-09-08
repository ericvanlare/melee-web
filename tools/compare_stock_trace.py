#!/usr/bin/env python3
"""Compare the bounded Final Destination four-stock movement trace.

The comparison aligns the 541 captured samples by order.  It intentionally
compares source gameplay state only; startup animation age and RNG are outside
this trace's established equivalence boundary.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct


FRAME_COUNT = 541
STOCK_JAB_FRAME_COUNT = 754
RECIPES = {
    "stock": {
        "frame_count": FRAME_COUNT,
        "scope": "Final Destination four-stock movement, one stock loss and first respawn; not a full-match equivalence claim",
    },
    "stock-jab": {
        "frame_count": STOCK_JAB_FRAME_COUNT,
        "scope": "Final Destination four-stock movement, one stock loss, first respawn and a grounded jab input; not a full-match equivalence claim",
    },
}
VECTOR_FIELDS = ("position", "velocity")
SCALAR_FIELDS = ("motion", "ground_air", "stocks")
EXCLUDED_FIELDS = (
    "animation frame / idle phase (the captures start at different settle ages)",
    "RNG (initial port and original startup states are not synchronized)",
)


def read(path):
    return [json.loads(line) for line in path.read_text().splitlines()
            if line.startswith("{")]


def _validate_rows(rows, kind, frame_count):
    if len(rows) != frame_count:
        raise ValueError(
            f"{kind} capture must contain exactly {frame_count} samples; got {len(rows)}")
    if kind.lower() == "reference":
        frames = [row.get("game_frame") for row in rows]
        if any(frame is None for frame in frames):
            raise ValueError("Reference capture is missing a game_frame")
        expected = list(range(frames[0], frames[0] + frame_count))
        if frames != expected:
            raise ValueError("Reference capture has missing or non-consecutive game frames")
    else:
        ticks = [row.get("tick") for row in rows]
        if any(tick is None for tick in ticks):
            raise ValueError("Port capture is missing a tick")
        if ticks != list(range(frame_count)):
            raise ValueError(
                f"Port capture must contain exactly one tick for each sample 0..{frame_count - 1}")


def _fighter(row, container_name, slot, index):
    fighters = row.get(container_name)
    if not isinstance(fighters, list):
        raise ValueError(f"Sample {index} is missing {container_name}")
    if any(not isinstance(fighter, dict) for fighter in fighters):
        raise ValueError(f"Sample {index} has a null or invalid {container_name} entry")
    matches = [fighter for fighter in fighters if fighter.get("slot") == slot]
    if len(matches) != 1:
        raise ValueError(
            f"Sample {index} must contain exactly one {container_name} entry for slot {slot}")
    return matches[0]


def _scalar(fighter, field, index, slot):
    value = fighter.get(field)
    if field not in fighter or value is None or isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"Sample {index} slot {slot} is missing a valid {field}")
    return value


def _finite_bits(bits, field, index, slot):
    if not isinstance(bits, str) or re.fullmatch(r"[0-9a-fA-F]{8}", bits) is None:
        raise ValueError(f"Sample {index} slot {slot} has invalid {field} bits")
    if not math.isfinite(struct.unpack(">f", bytes.fromhex(bits))[0]):
        raise ValueError(f"Sample {index} slot {slot} has non-finite {field} bits")
    return bits.lower()


def _bits(vector, field, index, slot):
    if not isinstance(vector, list) or len(vector) != 3:
        raise ValueError(f"Sample {index} slot {slot} has invalid {field}")
    bits = []
    for component in vector:
        if not isinstance(component, dict) or "bits" not in component:
            raise ValueError(f"Sample {index} slot {slot} has invalid {field} bits")
        bits.append(_finite_bits(component["bits"], field, index, slot))
    return bits


def compare(reference, port, recipe="stock"):
    """Return a bounded state comparison, or reject malformed captures."""
    if recipe not in RECIPES:
        raise ValueError(f"Unknown stock trace recipe: {recipe}")
    frame_count = RECIPES[recipe]["frame_count"]
    _validate_rows(reference, "Reference", frame_count)
    _validate_rows(port, "Port", frame_count)
    differences = []
    for index, (reference_row, port_row) in enumerate(zip(reference, port)):
        reference_fighters = [_fighter(reference_row, "fighters", slot, index)
                              for slot in (0, 1)]
        port_fighters = [_fighter(port_row, "players", slot, index)
                         for slot in (0, 1)]
        row_differences = []
        for slot, (original, current) in enumerate(zip(reference_fighters, port_fighters)):
            fields = []
            values = {}
            for field in SCALAR_FIELDS:
                original_value = _scalar(original, field, index, slot)
                current_value = _scalar(current, field, index, slot)
                if original_value != current_value:
                    fields.append(field)
                    values[field] = {"reference": original_value, "port": current_value}
            for field in VECTOR_FIELDS:
                original_bits = _bits(original.get(field), field, index, slot)
                current_bits = _bits(current.get(field), field, index, slot)
                if original_bits != current_bits:
                    fields.append(f"{field} bits")
                    values[f"{field} bits"] = {"reference": original_bits,
                                                "port": current_bits}
            original_damage_data = original.get("damage")
            current_damage_data = current.get("damage")
            if not isinstance(original_damage_data, dict) or "bits" not in original_damage_data:
                raise ValueError(f"Sample {index} slot {slot} is missing damage bits")
            if not isinstance(current_damage_data, dict) or "bits" not in current_damage_data:
                raise ValueError(f"Sample {index} slot {slot} is missing damage bits")
            original_damage = _finite_bits(original_damage_data["bits"], "damage", index, slot)
            current_damage = _finite_bits(current_damage_data["bits"], "damage", index, slot)
            if original_damage != current_damage:
                fields.append("damage bits")
                values["damage bits"] = {"reference": original_damage,
                                          "port": current_damage}
            if fields:
                row_differences.append({"slot": slot, "fields": fields, "values": values})
        if row_differences:
            differences.append({
                "sample": index,
                "reference_game_frame": reference_row["game_frame"],
                "port_tick": port_row["tick"],
                "fighters": row_differences,
            })
    return {
        "recipe": recipe,
        "frames": frame_count,
        "matched": frame_count - len(differences),
        "differences": differences,
        "fields": ["motion", "ground_air", "stocks", "position float bits",
                    "velocity float bits", "damage float bits"],
        "excluded": list(EXCLUDED_FIELDS),
        "scope": RECIPES[recipe]["scope"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--port", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--recipe", choices=tuple(RECIPES), default="stock")
    args = parser.parse_args()
    try:
        result = compare(read(args.reference), read(args.port), recipe=args.recipe)
    except ValueError as error:
        parser.error(str(error))
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return bool(result["differences"])


if __name__ == "__main__":
    raise SystemExit(main())
