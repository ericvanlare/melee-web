#!/usr/bin/env python3
"""Create a privacy-minimized, exact-bit Slippi replay timeline."""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from slippi_format import SlippiFormatError, read_timeline
from replay_transport import encode_transport


def load(path):
    source = path.expanduser().resolve(strict=True)
    if not source.is_file() or source.suffix.lower() != ".slp":
        raise ValueError("input must be a completed .slp file")
    digest = hashlib.sha256()
    with source.open("rb") as stream:
        class HashedReader:
            def read(self, count=-1):
                block = stream.read(count)
                digest.update(block)
                return block
        reader = HashedReader()
        timeline = read_timeline(reader)
        # Hash the unused metadata as well, without rereading potentially
        # changed source bytes after parsing the input timeline.
        while reader.read(1024 * 1024):
            pass
    return timeline, digest.hexdigest()


def _workload_record(timeline, *, characters=None, stage=None, digest=None):
    source = timeline.header.record()
    source_players = source["players"]
    if tuple(player["port"] for player in source_players) != (1, 2):
        raise ValueError(
            "replay workload only supports source ports 1 and 2 mapped to runtime slots 0 and 1")
    source_characters = [player["character_id"] for player in source_players]
    applied_characters = source_characters if characters is None \
        else list(characters)
    if len(applied_characters) != 2 or any(
            not isinstance(value, int) or not 0 <= value <= 255
            for value in applied_characters):
        raise ValueError("exactly two byte-sized character IDs are required")
    source_stage = source["stage_id"]
    applied_stage = source_stage if stage is None else stage
    if not isinstance(applied_stage, int) or not 0 <= applied_stage <= 0xffff:
        raise ValueError("workload stage ID must fit uint16")
    if digest is None:
        raise ValueError("source SHA-256 is required for workload provenance")
    return {
        "source": {
            "kind": "slippi",
            "sha256": digest,
            "slippi_version": source["slippi_version"],
            "game_info_hex": source["game_info_hex"],
            "settings": {
                "stage_id": source_stage,
                "is_teams": source["is_teams"],
                "item_spawn_frequency": source["item_spawn_frequency"],
                "pal": source["pal"],
                "frozen_stadium": source["frozen_stadium"],
                "minor_scene": source["minor_scene"],
                "major_scene": source["major_scene"],
            },
            "players": source_players,
        },
        "workload": {
            "execution": "source_input_workload_only",
            "comparison": "never",
            "performance_measurement": "not_reported",
            "input_provenance": {
                "source_sha256": digest,
                "source_format": "slippi_pre_frame_physical_fields",
                "first_frame": timeline.frames[0].number,
                "last_frame": timeline.frames[-1].number,
                "frame_count": len(timeline.frames),
            },
            "setup": {
                "rules_origin": "derived",
                "stock_count": 4,
                "timer_enabled": False,
                "items_enabled": False,
                "is_teams": False,
                "rumble_enabled": True,
            },
            "port_mapping": [
                {"source_port": player["port"], "runtime_slot": player["port"] - 1}
                for player in source_players
            ],
            "overrides": {
                "characters": {
                    "source": source_characters,
                    "applied": applied_characters,
                    "overridden": characters is not None,
                },
                "stage": {
                    "source": source_stage,
                    "applied": applied_stage,
                    "overridden": stage is not None,
                },
            },
        },
    }


def normalized_record(timeline, digest, *, characters=None, stage=None):
    record = timeline.record()
    record["source_sha256"] = digest
    record.update(_workload_record(
        timeline, characters=characters, stage=stage, digest=digest))
    return record


def normalize(path, *, characters=None, stage=None):
    timeline, digest = load(path)
    return normalized_record(timeline, digest, characters=characters, stage=stage)


def publish(path, record):
    destination = path.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=destination.parent,
            prefix=f".{destination.name}.", suffix=".tmp", delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(record, stream, sort_keys=True, separators=(",", ":"))
        stream.write("\n")
        stream.flush()
    temporary.replace(destination)


def publish_bytes(path, payload):
    destination = path.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            mode="wb", dir=destination.parent, prefix=f".{destination.name}.",
            suffix=".tmp", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(payload)
        stream.flush()
    temporary.replace(destination)


def main():
    parser = argparse.ArgumentParser(
        description="Normalize a completed Slippi replay without copying metadata")
    parser.add_argument("replay", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="destination for the normalized JSON timeline")
    parser.add_argument("--transport-output", type=Path,
                        help="optional fixed-width Wasm runner transport")
    parser.add_argument("--characters", type=lambda value: tuple(
                            int(part, 0) for part in value.split(",")),
                        help="two external character IDs for a derived workload")
    parser.add_argument("--stage", type=lambda value: int(value, 0),
                        help="stage ID override for a derived workload")
    arguments = parser.parse_args()
    try:
        if any(destination and destination.resolve()==arguments.replay.resolve()
               for destination in (arguments.output,arguments.transport_output)):
            raise ValueError("output cannot overwrite the source replay")
        timeline, digest = load(arguments.replay)
        record = normalized_record(
            timeline, digest, characters=arguments.characters, stage=arguments.stage)
        payload = None
        if arguments.transport_output:
            if arguments.output.resolve() == arguments.transport_output.resolve():
                raise ValueError("JSON and transport destinations must be distinct")
            payload = encode_transport(
                timeline, digest, characters=arguments.characters,
                stage=arguments.stage)
        # Validate all requested products from the same immutable parse before
        # publishing either. Each individual destination is replaced atomically.
        if payload is not None:
            publish_bytes(arguments.transport_output, payload)
        publish(arguments.output, record)
    except (OSError, SlippiFormatError, ValueError) as error:
        parser.exit(2, f"Slippi normalization failed: {error}\n")
    classification = record["classification"]
    print(json.dumps({
        "source_sha256": record["source_sha256"],
        "frame_count": record["frame_count"],
        "first_frame": record["first_frame"],
        "last_frame": record["last_frame"],
        "execution": "source_input_workload_only",
        "comparison": "not_run",
        "eligibility_profile": classification["profile"],
        "exact_input_candidate": classification["exact_input_candidate"],
        "rejection_reasons": classification["rejection_reasons"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
