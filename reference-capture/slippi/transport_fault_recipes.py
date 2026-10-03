#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate receiver-attributed bounded jitter/reorder journals.

The sender journal alone is insufficient evidence: the native receiver must
report both ENet deliveries, their FNV-1a64 packet identities, and the
``inputs_to_copy`` result from its existing OnData gate.  This helper is the
production receiver-attribution validator used by ``run_rollback.py``;
it performs no network or game work itself.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any


PACKET_HASH = re.compile(r"[0-9a-f]{16}\Z")
FRAME = 98
RELEASE = 99
ROLE = 2  # logical sender role; the native wire player byte is zero-based.
WIRE_PACKET_PORT = 1  # role 2 / m_player_idx=1, from SlippiNetplay.cpp.
RECEIVER_PORT = 1  # diagnostic receiver port is one-based.
WIRE_PLAYER_PORT_MIN = 0
WIRE_PLAYER_PORT_MAX = 3  # SLIPPI_PLAYER_COUNT_MAX - 1.
RECEIVER_PORT_MIN = 1
RECEIVER_PORT_MAX = 4  # one-based diagnostic local port.
MAX_INTERVAL = 8


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        value = json.loads(line)
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{number}: event is not an object")
        rows.append(value)
    return rows


def _hash(row: dict[str, Any], key: str) -> str:
    value = row.get(key)
    if not isinstance(value, str) or not PACKET_HASH.fullmatch(value):
        raise ValueError(f"native {key} is not an FNV-1a64 identity")
    return value


def _ports(row: dict[str, Any]) -> None:
    wire_port = row.get("packet_player_port")
    receiver_port = row.get("receiver_port")
    if (type(wire_port) is not int or not WIRE_PLAYER_PORT_MIN <= wire_port <= WIRE_PLAYER_PORT_MAX
            or type(receiver_port) is not int
            or not RECEIVER_PORT_MIN <= receiver_port <= RECEIVER_PORT_MAX):
        raise ValueError("native receiver port is outside the authored four-player domain")


def _history(row: dict[str, Any]) -> None:
    count = row.get("history_count")
    first = row.get("history_first_frame")
    last = row.get("history_last_frame")
    if (type(count) is not int or type(first) is not int or type(last) is not int
            or count < 1 or count > MAX_INTERVAL or last - first + 1 != count):
        raise ValueError("sender history is missing or not contiguous within the eight-packet bound")


def _int(row: dict[str, Any], key: str) -> int:
    value = row.get(key)
    if type(value) is not int:
        raise ValueError(f"native {key} is not an integer")
    return value


def validate_transport(sender_rows: list[dict[str, Any]], receiver_rows: list[dict[str, Any]],
                       *, scenario: str, game_sequence: int = 0) -> dict[str, Any]:
    if scenario not in {"jitter", "reorder"}:
        raise ValueError("scenario must be jitter or reorder")
    sender = [row for row in sender_rows
              if row.get("event") == "pad_transport"
              and row.get("game_sequence") == game_sequence]
    receive = [row for row in receiver_rows
               if row.get("event") == "pad_transport_receive"
               and row.get("game_sequence") == game_sequence]
    if any(row.get("observer_context") != "transport" for row in sender + receive):
        raise ValueError("transport evidence came from the wrong native context")
    for row in receive:
        _ports(row)
    receive = [row for row in receive
               if row.get("packet_player_port") == WIRE_PACKET_PORT
               and row.get("receiver_port") == RECEIVER_PORT
               and row.get("packet_frame") in {FRAME, RELEASE}]
    if scenario == "jitter":
        if [row.get("action") for row in sender] != ["jitter_hold", "jitter_release"]:
            raise ValueError("jitter requires exactly hold and release sender events")
        hold, release = sender
        if _int(hold, "packet_frame") != FRAME or _int(hold, "held_count") != 1:
            raise ValueError("jitter hold does not identify frame 98 and one held packet")
        _history(hold)
        _history(release)
        held_hash = _hash(hold, "payload_hash")
        if (_int(release, "release_packet_frame") != RELEASE
                or _int(release, "dispatch_frame") != RELEASE
                or _int(release, "release_count") != 1
                or release.get("held_frames") != [FRAME]
                or type(release.get("held_frames")) is not list
                or any(type(value) is not int for value in release["held_frames"])
                or release.get("held_payload_hashes") != [held_hash]
                or type(release.get("held_payload_hashes")) is not list
                or release.get("dispatch_payload_hashes") != []):
            raise ValueError("jitter release is not immediately before packet frame 99")
        release_hash = _hash(release, "release_payload_hash")
        expected_frames = [FRAME, RELEASE]
        expected_payloads = [held_hash, release_hash]
        expected_inputs = [lambda value: value > 0, lambda value: value > 0]
        label = "one-frame delayed delivery in original order"
    else:
        if [row.get("action") for row in sender] != ["reorder_hold", "reorder_dispatch"]:
            raise ValueError("reorder requires exactly hold and dispatch sender events")
        hold, dispatch = sender
        if _int(hold, "packet_frame") != FRAME or _int(hold, "held_count") != 1:
            raise ValueError("reorder hold does not identify frame 98 and one held packet")
        _history(hold)
        _history(dispatch)
        held_hash = _hash(hold, "payload_hash")
        release_hash = _hash(dispatch, "release_payload_hash")
        dispatch_hashes = dispatch.get("dispatch_payload_hashes")
        if (_int(dispatch, "dispatch_frame") != RELEASE
                or type(dispatch_hashes) is not list or len(dispatch_hashes) != 2
                or any(not isinstance(value, str) or not PACKET_HASH.fullmatch(value)
                       for value in dispatch_hashes)
                or dispatch_hashes != [release_hash, held_hash]):
            raise ValueError("reorder dispatch does not send frame 99 before held frame 98")
        expected_frames = [RELEASE, FRAME]
        expected_payloads = dispatch_hashes
        expected_inputs = [lambda value: value > 0, lambda value: value <= 0]
        label = "late reordered delivery with redundant-history bypass observed"
    if [row.get("packet_frame") for row in receive] != expected_frames:
        raise ValueError("receiver delivery order does not match the bounded recipe")
    receiver_payloads = []
    for index, (row, predicate) in enumerate(zip(receive, expected_inputs)):
        receiver_payloads.append(_hash(row, "payload_hash"))
        copied = row.get("inputs_to_copy")
        if type(copied) is not int or not predicate(copied):
            raise ValueError(f"receiver inputs_to_copy does not prove recipe step {index}")
    if receiver_payloads != expected_payloads:
        raise ValueError("receiver packet hashes do not match the exact sender dispatch sequence")
    sender_hashes = {value for row in sender for key in
                     ("payload_hash", "release_payload_hash", "dispatch_payload_hash")
                     if isinstance(value := row.get(key), str)}
    for row in sender:
        for key in ("held_payload_hashes", "dispatch_payload_hashes"):
            values = row.get(key)
            if isinstance(values, list):
                sender_hashes.update(value for value in values
                                     if isinstance(value, str))
    receiver_hashes = {row["payload_hash"] for row in receive}
    if not receiver_hashes.issubset(sender_hashes):
        raise ValueError("receiver packet identity was not joined to a sender identity")
    return {"scenario": scenario, "game_sequence": game_sequence,
            "role": ROLE, "wire_packet_player_port": WIRE_PACKET_PORT,
            "receiver_port": RECEIVER_PORT,
            "delivery_frames": expected_frames, "receiver_rows": len(receive),
            "sender_hashes": sorted(sender_hashes),
            "receiver_hashes": sorted(receiver_hashes),
            "receiver_payload_sequence": receiver_payloads,
            "sender_payload_sequence": expected_payloads,
            "redundant_history_observed": scenario == "reorder",
            "attribution": "native sender FNV hash joined to native OnData receiver row",
            "description": label}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sender", type=Path, required=True)
    parser.add_argument("--receiver", type=Path, required=True)
    parser.add_argument("--scenario", choices=("jitter", "reorder"), required=True)
    parser.add_argument("--game-sequence", type=int, default=0)
    args = parser.parse_args(argv)
    print(json.dumps(validate_transport(read_jsonl(args.sender), read_jsonl(args.receiver),
                                        scenario=args.scenario,
                                        game_sequence=args.game_sequence),
                   indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
