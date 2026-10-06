#!/usr/bin/env python3
"""Create a source-backed, synthetic MWRC v4 staging-ring diagnostic input."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "tools"))

from test_retail_recipe_runtime import PAD_SNAPSHOT, _v9_setup  # noqa: E402
from retail_setup_validation import _decode_setup  # noqa: E402

PROFILE_CHARACTERS = 0x07FF
PROFILE_STAGES = 0x01C0
FIXED_SEED = 0x12345678
MAX_LEGACY_FRAMES = 36000


def create_recipe(frames: int) -> tuple[bytes, dict[str, object]]:
    if not 600 <= frames <= MAX_LEGACY_FRAMES:
        raise ValueError(f"frame count must be 600..{MAX_LEGACY_FRAMES}")
    setup = _v9_setup((8, 8, 8, 8))
    decoded = _decode_setup(setup.hex())
    players = decoded["players"]
    if decoded["stage"] != 0x20 or len(players) != 4 or any(
        player["character_kind"] != 8 or player.get("cpu_kind") != 4 or
        player.get("cpu_level") != 9 or player["stocks"] != 4
        for player in players
    ):
        raise ValueError("tracked setup fixture no longer describes four-stock Mario CPU9 on Final Destination")
    neutral_inputs = bytes(44 * frames)
    payload = (
        struct.pack(">4sIIIHH", b"MWRC", 4, FIXED_SEED, frames,
                    PROFILE_CHARACTERS, PROFILE_STAGES) +
        setup + PAD_SNAPSHOT + neutral_inputs
    )
    if len(payload) != 20 + 0x138 + len(PAD_SNAPSHOT) + 44 * frames:
        raise AssertionError("MWRC v4 payload length disagrees with the declared envelope")
    report: dict[str, object] = {
        "schema": "melee-web-staging-ring-diagnostic-recipe-v1",
        "scope": "synthetic initial-context diagnostic; no original-match identity claim",
        "recipe_format": "MWRC v4",
        "setup_source": "tests/test_retail_recipe_runtime.py::_v9_setup((8, 8, 8, 8))",
        "pad_history_source": "tests/test_retail_recipe_runtime.py::PAD_SNAPSHOT",
        "setup_validation": "tools/retail_setup_validation.py::_decode_setup",
        "declared_setup": decoded,
        "profile_masks": {
            "characters_hex": f"{PROFILE_CHARACTERS:04x}",
            "stages_hex": f"{PROFILE_STAGES:04x}",
        },
        "seed_hex": f"{FIXED_SEED:08x}",
        "pad_snapshot_bytes": len(PAD_SNAPSHOT),
        "pad_rows": "44-byte neutral input rows for four active ports",
        "frames": frames,
        "input_bytes": len(payload),
        "initial_context": "tracked setup and PAD state are explicitly composed for this diagnostic; not copied from a private or original match capture",
    }
    return payload, report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True, type=Path,
                        help="new .mwrc output path outside the checkout")
    parser.add_argument("--frames", type=int, default=600)
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit(f"output already exists: {args.out}")
    payload, report = create_recipe(args.frames)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    report["fixture_sha256"] = hashlib.sha256(payload).hexdigest()
    report["fixture_bytes"] = len(payload)
    report_path = args.out.with_suffix(args.out.suffix + ".json")
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"recipe": str(args.out), "report": str(report_path),
                      "fixture_sha256": report["fixture_sha256"],
                      "bytes": len(payload), "frames": args.frames}, sort_keys=True))


if __name__ == "__main__":
    main()
