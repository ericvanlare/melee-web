# SPDX-License-Identifier: MIT
"""Run the pinned desktop frame-input baseline and bounded rollback fixture.

Reuse the ordinary testbed's menu, pairing, rematch, socket and cleanup gates.
The opt-in native diagnostic generates gameplay PAD bytes; controller Pipes
remain neutral during the match. Raw replays and logs remain private evidence.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import re
import struct
import sys

from compare_rollback import compare_timelines, first_difference, validate_complete
from run_local import LOCK_PATH, PairRun, ROOT, _sha256
from slippi_format import (FRAME_START, PRE_FRAME, POST_FRAME, _raw_stream, _decode_pre_frame,
                           _decode_post_frame, decode_timeline, read_timeline)
from slippi_rollback_diagnostic import PROFILE_NAME, SCHEMA
from transport_fault_recipes import validate_transport as validate_transport_delivery

PINNED_INPUT_DELAY = 2  # Config/MainSettings.cpp SLIPPI_ONLINE_DELAY default.
RECORDING_FRAME_OFFSET = 123  # Recording index starts at -123, scene at 0.
ARTIFACT_IDENTITY_FIELDS = (
    "client_binary_sha256", "matchmaker_sha256", "dependency_lock_sha256",
    "downstream_patch_sha256", "game_modification_sha256", "disc_sha256",
    "bundle_sys_sha256", "service_framing_lineage_sha256")
EXPECTED_BASELINE_CYCLES = 2
EXPECTED_BASELINE_GAMES = (1, 2)
EXPECTED_FINALIZED_FRAMES = 1342


def artifact_identities(evidence):
    try:
        values = {key: evidence[key] for key in ARTIFACT_IDENTITY_FIELDS}
    except KeyError as error:
        raise ValueError(f"evidence lacks artifact identity {error.args[0]}") from error
    for key, value in values.items():
        if key == "downstream_patch_sha256":
            _validate_downstream_patch_identity(value)
        else:
            _strict_sha(value, f"artifact identity {key}")
    return values


def _strict_sha(value, label):
    if type(value) is not str or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError(f"{label} is not a lowercase SHA-256 identity")
    return value


def _expected_downstream_patch_identities():
    """Return the exact patch map emitted by ``PairRun``.

    The downstream identity is a map because the client is built from every
    locked patch.  Keeping the path keys in the identity makes an omitted,
    extra, or substituted patch visible at baseline admission.
    """
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    patches = lock.get("patches")
    if (type(patches) is not list or not patches
            or any(type(path) is not str or not path for path in patches)
            or len(set(patches)) != len(patches)):
        raise ValueError("client.lock.json patches are not a unique non-empty list")
    return {path: _sha256(LOCK_PATH.parent / path) for path in patches}


def _validate_downstream_patch_identity(value):
    if type(value) is not dict:
        raise ValueError("artifact identity downstream_patch_sha256 is not a patch identity map")
    expected = _expected_downstream_patch_identities()
    if set(value) != set(expected):
        raise ValueError("artifact identity downstream_patch_sha256 path set differs from client.lock.json")
    for path, digest in value.items():
        _strict_sha(digest, f"artifact identity downstream_patch_sha256[{path}]")
        if digest != expected[path]:
            raise ValueError(f"artifact identity downstream_patch_sha256[{path}] differs from patch bytes")


def validate_service_lineage(path):
    """Admit a fresh service build from the public PR #133 composition.

    The public source identity is fixed by ``client.lock.json``.  Build output,
    cache and source-inventory identities are produced by the current build and
    are bound by their receipt, baseline and fault runs as one set; old
    retained binaries and receipts are evidence only.
    """
    path = path.expanduser().resolve(strict=True)
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("service lineage receipt is not an object")
    if value.get("schema") != "melee-web-service-lineage-v2":
        raise ValueError("service lineage schema is unsupported")
    lock_path = ROOT / "reference-capture/slippi/client.lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    expected = lock.get("service_prerequisite")
    if not isinstance(expected, dict):
        raise ValueError("client lock lacks the PR #133 service prerequisite")
    public_fields = {
        "service_public_ref": "public_ref",
        "service_public_commit": "public_commit",
        "service_public_parent_commit": "public_parent_commit",
        "service_public_path_diff_sha256": "public_path_diff_sha256",
        "service_base_server_cpp_sha256": "base_server_cpp_sha256",
        "service_integration_test_sha256": "integration_test_sha256",
        "service_source_sha256": "source_sha256",
    }
    for actual, locked in public_fields.items():
        actual_value = value.get(actual)
        if actual == "service_public_ref":
            if type(actual_value) is not str or actual_value != expected.get(locked):
                raise ValueError(f"service lineage {actual} differs from client.lock.json")
        elif actual in {"service_public_commit", "service_public_parent_commit"}:
            if type(actual_value) is not str or not re.fullmatch(r"[0-9a-f]{40}", actual_value):
                raise ValueError(f"service lineage {actual} is not a commit identity")
        else:
            _strict_sha(actual_value, f"service lineage {actual}")
        if actual_value != expected.get(locked):
            raise ValueError(f"service lineage {actual} differs from client.lock.json")
    worktree_head = value.get("service_worktree_head")
    if type(worktree_head) is not str or not re.fullmatch(r"[0-9a-f]{40}", worktree_head):
        raise ValueError("service lineage service_worktree_head is not a commit identity")
    for actual in (
            "service_binary_sha256", "service_build_cache_sha256",
            "service_source_inventory_sha256"):
        _strict_sha(value.get(actual), f"service lineage {actual}")
    if value.get("service_source_patch_sha256") != value["service_public_path_diff_sha256"]:
        raise ValueError("service lineage source patch is not the public path-limited diff")
    _strict_sha(value["service_source_patch_sha256"], "service lineage service_source_patch_sha256")
    profile = value.get("build_profile")
    if type(profile) is not dict or profile.get("matchmaker_release_build_verified") is not True:
        raise ValueError("service lineage lacks a verified fresh matchmaker build profile")
    lineage_sha = _sha256(path)
    return {
        "service_framing_lineage_sha256": lineage_sha,
        "service_public_ref": value["service_public_ref"],
        "service_public_commit": value["service_public_commit"],
        "service_public_parent_commit": value["service_public_parent_commit"],
        "service_public_path_diff_sha256": value["service_public_path_diff_sha256"],
        "service_base_server_cpp_sha256": value["service_base_server_cpp_sha256"],
        "service_integration_test_sha256": value["service_integration_test_sha256"],
        "service_binary_sha256": value["service_binary_sha256"],
        "service_build_cache_sha256": value["service_build_cache_sha256"],
        "service_source_inventory_sha256": value["service_source_inventory_sha256"],
        "service_source_sha256": value["service_source_sha256"],
        "service_source_patch_sha256": value["service_source_patch_sha256"],
        "service_worktree_head": value["service_worktree_head"],
    }


def validate_baseline_receipts(root, service_identity):
    """Bind both fresh baseline cycles, all four games and their inputs.

    Every file identity is recomputed at admission. This prevents a passed
    cycle-01 report, stale cycle-02/config files, or a changed service receipt
    from being paired with a new impaired run.
    """
    root = root.expanduser().resolve(strict=True)
    rollback_path = root / "rollback-run.json"
    if not rollback_path.is_file():
        raise ValueError("baseline rollback-run.json is missing")
    rollback = json.loads(rollback_path.read_text(encoding="utf-8"))
    if type(rollback) is not dict:
        raise ValueError("baseline rollback-run.json is not an object")
    if (rollback.get("result") != "passed" or rollback.get("scenario") != "none"
            or rollback.get("fresh_profile_repeatability_verified") is not True
            or rollback.get("rollback_correctness_claimed") is not False):
        raise ValueError("baseline is not a passed diagnostic none run")
    cycles = rollback.get("cycles")
    if (type(cycles) is not list or len(cycles) != EXPECTED_BASELINE_CYCLES
            or any(type(row) is not dict for row in cycles)
            or [row.get("cycle") for row in cycles] != [1, 2]
            or any(type(row.get("cycle")) is not int for row in cycles)
            or any(row.get("result") != "passed" for row in cycles)):
        raise ValueError("baseline must contain two passed fresh cycles")

    identities = None
    evidence_hashes = {}
    comparison_hashes = {}
    config_hashes = {}
    baseline_replays = {}
    for cycle in cycles:
        cycle_number = cycle["cycle"]
        cycle_root = (root / f"cycle-{cycle_number:02d}").resolve(strict=True)
        evidence_path = cycle_root / "evidence.json"
        if cycle.get("evidence") != f"cycle-{cycle_number:02d}/evidence.json":
            raise ValueError("baseline cycle evidence path is not canonical")
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        if type(evidence) is not dict:
            raise ValueError(f"baseline cycle {cycle_number} evidence is not an object")
        if evidence.get("result") != "passed":
            raise ValueError(f"baseline cycle {cycle_number} evidence is not passed")
        current = artifact_identities(evidence)
        if evidence.get("service_framing_lineage_sha256") != service_identity["service_framing_lineage_sha256"]:
            raise ValueError("baseline service framing receipt differs from the requested prerequisite")
        if evidence.get("matchmaker_sha256") != service_identity["service_binary_sha256"]:
            raise ValueError("baseline service binary differs from the requested prerequisite")
        if evidence.get("service_framing") != service_identity:
            raise ValueError("baseline service source/build identity differs from the requested prerequisite")
        if identities is None:
            identities = current
        elif current != identities:
            raise ValueError("baseline artifact identities differ between fresh cycles")
        evidence_hashes[f"cycle-{cycle_number:02d}"] = _sha256(evidence_path)
        for game in EXPECTED_BASELINE_GAMES:
            comparison_path = cycle_root / f"game-{game:02d}-comparison.json"
            comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
            if type(comparison) is not dict:
                raise ValueError(f"baseline cycle {cycle_number} game {game} comparison is not an object")
            validate_baseline_game(comparison, game)
            peer = comparison.get("peer_comparison", {})
            if (type(peer) is not dict
                    or type(peer.get("compared_frames")) is not int
                    or peer.get("compared_frames") != EXPECTED_FINALIZED_FRAMES
                    or peer.get("first_divergence") is not None):
                raise ValueError(f"baseline cycle {cycle_number} game {game} comparison is incomplete")
            comparison_hashes[f"cycle-{cycle_number:02d}/game-{game:02d}"] = _sha256(comparison_path)
            baseline_replays.setdefault(game, {})
            replays = comparison.get("replays")
            for name in ("p1", "p2"):
                replay = replays.get(name) if type(replays) is dict else None
                if (type(replays) is not dict
                        or not isinstance(replay, dict)
                        or type(replay.get("file")) is not str):
                    raise ValueError(f"baseline cycle {cycle_number} game {game} lacks {name} replay")
                replay_path = (cycle_root / replay["file"]).resolve(strict=True)
                _strict_sha(replay.get("sha256"), f"baseline {cycle_number} {game} {name} replay")
                try:
                    replay_path.relative_to(cycle_root)
                except ValueError as error:
                    raise ValueError("baseline replay escaped its cycle or changed identity") from error
                if _sha256(replay_path) != replay.get("sha256"):
                    raise ValueError("baseline replay escaped its cycle or changed identity")
                if cycle_number == 1:
                    baseline_replays[game][name] = replay_path
            for name, role in (("p1", 1), ("p2", 2)):
                config_path = cycle_root / f"{name}-rollback-config.json"
                config = json.loads(config_path.read_text(encoding="utf-8"))
                if type(config) is not dict:
                    raise ValueError(f"baseline cycle {cycle_number} {name} config is not an object")
                transport = config.get("transport")
                input_profile = config.get("input_profile")
                if (config.get("schema") != SCHEMA or config.get("enabled") is not True
                        or config.get("stage_id") != 32
                        or config.get("rng_offset") != 0x1234
                        or type(transport) is not dict or transport.get("action") != "none"
                        or transport.get("frame") is not None
                        or transport.get("release_frame") is not None
                        or config.get("overlay") is not None
                        or type(input_profile) is not dict
                        or input_profile.get("name") != PROFILE_NAME
                        or input_profile.get("role") != role
                        or type(config.get("stage_id")) is not int
                        or type(config.get("rng_offset")) is not int
                        or type(input_profile.get("role")) is not int):
                    raise ValueError("baseline diagnostic config differs from the pinned none profile")
                config_hashes[f"cycle-{cycle_number:02d}/{name}"] = _sha256(config_path)
    return {
        "rollback_run_sha256": _sha256(rollback_path),
        "cycle_evidence_sha256": evidence_hashes,
        "comparison_sha256": comparison_hashes,
        "config_sha256": config_hashes,
        "artifact_identity": identities,
        "service_framing": service_identity,
        "baseline_replays": baseline_replays,
    }


def validate_baseline_game(receipt, game):
    peer = receipt.get("peer_comparison") if type(receipt) is dict else None
    if (type(receipt) is not dict or receipt.get("result") != "passed"
            or receipt.get("game") != game
            or receipt.get("scenario") != "none"
            or type(peer) is not dict or peer.get("result") != "passed"):
        raise ValueError("baseline game lacks passed no-impairment peer comparison")


def read_diagnostic_log(path):
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("diagnostic log exceeds the bounded 64 MiB reader")
    data = path.read_text(encoding="utf-8")
    if not data or not data.endswith("\n"):
        raise ValueError("diagnostic log is empty or has an incomplete final row")
    rows = []
    for number, line in enumerate(data.splitlines(), 1):
        row = json.loads(line)
        if not isinstance(row, dict):
            raise ValueError(f"diagnostic row {number} is not an object")
        rows.append(row)
    return rows


def verify_log_order(rows):
    next_sequence = {"exi": 0, "transport": 0}
    games = {"exi": -1, "transport": -1}
    for row in rows:
        context = row.get("observer_context")
        if (row.get("diagnostic_schema") != SCHEMA or context not in next_sequence
                or row.get("event_sequence") != next_sequence.get(context)):
            raise ValueError("diagnostic schema/context/event order is incomplete")
        game = row.get("game_sequence")
        if not isinstance(game, int) or isinstance(game, bool) or not games[context] <= game <= games[context] + 1:
            raise ValueError("diagnostic game sequence is not ordered")
        games[context] = game
        next_sequence[context] += 1
    return next_sequence


def recording_episode(rows, game_number):
    """Use GameInfo boundaries: the first recording precedes online frame 1."""
    exi = [row for row in rows if row.get("observer_context") == "exi"]
    starts = [index for index, row in enumerate(exi)
              if row.get("event") == "recording_game_start"]
    if not 1 <= game_number <= len(starts):
        raise ValueError("native recording lacks the requested GameInfo boundary")
    first = starts[game_number - 1]
    last = starts[game_number] if game_number < len(starts) else len(exi)
    return exi[first:last]


def verify_recording_clock(timeline, rows, *, game_number):
    episode = recording_episode(rows, game_number)
    starts = [row for row in episode if row.get("event") == "recording_frame_start"]
    if not starts or (starts[0].get("frame"), starts[0].get("scene_frame")) != (-123, 0):
        raise ValueError("native recording does not start at the pinned recording/scene clocks")
    known_preservation = None
    for row in episode:
        event = row.get("event")
        if event == "input_profile_assignment":
            # handleOnlineInputs resets the diagnostic caches at frame 1.
            known_preservation = None
        elif event in ("savestate_capture", "savestate_load"):
            known_preservation = row["preservation"]
        elif event == "recording_frame_start" and any(
                key in row for key in ("odb", "rxb", "preservation")):
            if known_preservation is None or not all(
                    key in row for key in ("odb", "rxb", "preservation")):
                raise ValueError("recording inherited stale rollback preservation metadata")
            for key in ("odb_address", "odb_size", "rxb_address", "rxb_size"):
                if row["preservation"][key] != known_preservation[key]:
                    raise ValueError("recording preservation identity differs from this game's native observation")
    final = {}
    for row in starts:
        frame = row["frame"]
        if row["scene_frame"] != frame + RECORDING_FRAME_OFFSET:
            raise ValueError(f"native recording clock mapping differs at frame {frame}")
        final[frame] = {"rng": row["rng"], "scene_frame": row["scene_frame"]}
    if sorted(final) != [frame.number for frame in timeline.frames]:
        raise ValueError("native recording and finalized replay frame coverage differ")
    for frame in timeline.frames:
        difference = first_difference(
            {"rng": frame.start_random_seed, "scene_frame": frame.scene_frame_counter},
            final[frame.number], "recording_frame_start")
        if difference:
            raise ValueError(json.dumps({"recorded_frame": frame.number, **difference}, sort_keys=True))
    return {"game_info_boundary": game_number, "observed_frame_starts": len(starts),
            "finalized_frames_compared": len(final), "repeated_frame_starts": len(starts) - len(final),
            "recorded_frame_mapping": "scene_frame - 123", "exact_final_rng_and_scene_clock": True,
            "attached_rollback_metadata_timing": "DMA observation; recording payloads may be batched"}


def verify_transport(rows, *, game_sequence, scenario, role):
    faults = [row for row in rows if row.get("event") == "pad_transport"
              and row.get("game_sequence") == game_sequence]
    receives = [row for row in rows if row.get("event") == "pad_transport_receive"
                and row.get("game_sequence") == game_sequence]
    if any(row.get("observer_context") != "transport" for row in receives):
        raise ValueError("receiver observations came from the wrong native context")
    receive_report = None
    if scenario == "duplicate" and role == 1:
        # OnData reads the wire player byte from m_player_idx (zero-based);
        # receiver_port is the diagnostic's one-based local port.
        for row in receives:
            wire_port = row.get("packet_player_port")
            receiver_port = row.get("receiver_port")
            if (type(wire_port) is not int or not 0 <= wire_port <= 3
                    or type(receiver_port) is not int or not 1 <= receiver_port <= 4):
                raise ValueError("native receiver port is outside the authored four-player domain")
        duplicate_candidates = [row for row in receives
                                if row.get("packet_frame") == 98
                                and row.get("packet_player_port") == 1]
        if (any(row.get("receiver_port") != 1 for row in duplicate_candidates)
                or len(duplicate_candidates) != 2):
            raise ValueError("role-2 duplicate receiver rows have the wrong local port or count")
        duplicate_receives = [row for row in duplicate_candidates
                              if row.get("receiver_port") == 1]
        hashes = [row.get("payload_hash") for row in duplicate_receives]
        if (len(duplicate_receives) != 2
                or any(not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{16}", value)
                       for value in hashes)
                or len(set(hashes)) != 1
                or [row.get("inputs_to_copy") for row in duplicate_receives] != [1, 0]
                or any(type(row.get("inputs_to_copy")) is not int for row in duplicate_receives)):
            raise ValueError("role-2 duplicate did not produce two identical receiver arrivals")
        receive_report = {"receiver_event_count": 2,
                          "packet_frame": 98,
                          "wire_packet_player_port": 1,
                          "receiver_port": 1,
                          "payload_hashes": hashes,
                          "identical_wire_payloads_received": True}
    action = scenario if role == 2 else "none"
    if any(row.get("observer_context") != "transport" for row in faults):
        raise ValueError("fault observations came from the wrong native context")
    if action == "none":
        if faults:
            raise ValueError("no-impairment peer unexpectedly applied a transport fault")
        report = {"action": action, "fault_events": 0}
        if receive_report is not None:
            report["receiver"] = receive_report
        return report
    for row in faults:
        field = ("release_payload_hash" if row.get("action") in {"hold_release", "jitter_release", "reorder_dispatch"}
                 else "payload_hash")
        if not isinstance(row.get(field), str) or not re.fullmatch(r"[0-9a-f]{16}", row[field]):
            raise ValueError("fault packet lacks its native FNV-1a64 identity")
        count = row.get("history_count")
        first = row.get("history_first_frame")
        last = row.get("history_last_frame")
        if (type(count) is not int or type(first) is not int or type(last) is not int
                or count <= 0 or last - first + 1 != count):
            raise ValueError("fault packet history is not contiguous")
    if action == "drop":
        drop = faults[0] if len(faults) == 1 else {}
        if (len(faults) != 1 or drop.get("action") != "drop"
                or type(drop.get("packet_frame")) is not int
                or drop.get("packet_frame") != 98
                or type(drop.get("drop_count")) is not int
                or drop.get("drop_count") != 1
                or type(drop.get("history_last_frame")) is not int
                or drop.get("history_last_frame") != 98):
            raise ValueError("single-packet loss schedule was not observed exactly")
        return {"action": action, "fault_events": 1, "packet_frame": 98,
                "redundant_history_preserved": True}
    if action == "duplicate":
        duplicate = faults[0] if len(faults) == 1 else {}
        if (len(faults) != 1 or duplicate.get("action") != "duplicate"
                or type(duplicate.get("role")) is not int or duplicate.get("role") != 2
                or type(duplicate.get("packet_frame")) is not int
                or duplicate.get("packet_frame") != 98
                or type(duplicate.get("duplicate_count")) is not int
                or duplicate.get("duplicate_count") != 1
                or type(duplicate.get("history_last_frame")) is not int
                or duplicate.get("history_last_frame") != 98
                or duplicate.get("duplicate_payload_hash") != duplicate.get("payload_hash")
                or not isinstance(duplicate.get("duplicate_payload_hash"), str)
                or not re.fullmatch(r"[0-9a-f]{16}", duplicate["duplicate_payload_hash"])):
            raise ValueError("single role-2 duplicate schedule or exact payload was not observed")
        return {"action": action, "fault_events": 1, "packet_frame": 98,
                "duplicate_count": 1, "role": 2,
                "payload_hash": duplicate["payload_hash"],
                "duplicate_payload_hash": duplicate["duplicate_payload_hash"],
                "exact_payload_and_history_preserved": True}
    if action in {"jitter", "reorder"}:
        # The receiver-attributed validator owns the authored hold/dispatch
        # schedule.  Keeping this per-peer pass limited to native identities
        # and contiguous history prevents a second, drifting recipe parser.
        expected_actions = (["jitter_hold", "jitter_release"] if action == "jitter"
                            else ["reorder_hold", "reorder_dispatch"])
        if [row.get("action") for row in faults] != expected_actions:
            raise ValueError(f"{action} requires exactly its authored hold/release event sequence")
        for row in faults:
            field = "release_payload_hash" if row["action"] in {"jitter_release", "reorder_dispatch"} else "payload_hash"
            if not isinstance(row.get(field), str) or not re.fullmatch(r"[0-9a-f]{16}", row[field]):
                raise ValueError(f"{row['action']} lacks its native FNV-1a64 identity in {field}")
        return {"action": action, "fault_events": len(faults),
                "native_history_and_identities_observed": True}
    if action != "hold":
        raise ValueError("unsupported transport observation scenario")
    if ([row["action"] for row in faults] != ["hold_begin", *(["hold"] * 5), "hold_release"]
            or any(type(row.get("packet_frame")) is not int for row in faults[:-1])
            or [row["packet_frame"] for row in faults[:-1]] != list(range(98, 104))):
        raise ValueError("six-packet hold interval was not observed exactly")
    for count, row in enumerate(faults[:-1], 1):
        if (type(row.get("held_count")) is not int
                or type(row.get("begin_count")) is not int
                or type(row.get("history_last_frame")) is not int
                or row["held_count"] != count
                or row["begin_count"] != (1 if count == 1 else 0)
                or row["history_last_frame"] != row["packet_frame"]):
            raise ValueError("held packet queue or history differs from the declared schedule")
    release = faults[-1]
    if (type(release.get("release_packet_frame")) is not int
            or type(release.get("release_count")) is not int
            or type(release.get("history_last_frame")) is not int
            or type(release.get("held_frames")) is not list
            or any(type(value) is not int for value in release["held_frames"])
            or release["release_packet_frame"] != 104
            or release["release_count"] != 6
            or release["held_frames"] != list(range(98, 104)) or
            release["held_payload_hashes"] != [row["payload_hash"] for row in faults[:-1]] or
            release["history_last_frame"] != 104):
        raise ValueError("held packet release did not preserve the original packet identities/order")
    return {"action": action, "fault_events": 7, "held_frames": release["held_frames"],
            "release_packet_frame": 104, "released_original_packets_in_order": True,
            "redundant_history_preserved": True}


def verify_prediction_rollbacks(rows, *, game_number):
    """Observe the pinned B1/B2 paths separately from finalized agreement.

    Recording payloads may be batched, so repeated callback coverage is joined
    by source scene interval, not by their DMA-time rollback flags.
    """
    episode = recording_episode(rows, game_number)
    recording_starts = []
    recording_start_indices = {}
    for index, row in enumerate(episode):
        if row.get("event") != "recording_frame_start":
            continue
        frame = row.get("frame")
        scene = row.get("scene_frame")
        rng = row.get("rng")
        sequence = row.get("event_sequence")
        if (not isinstance(frame, int) or isinstance(frame, bool) or
                not isinstance(scene, int) or isinstance(scene, bool) or
                not isinstance(rng, int) or isinstance(rng, bool) or not 0 <= rng <= 0xFFFFFFFF or
                not isinstance(sequence, int) or isinstance(sequence, bool)):
            raise ValueError("recording frame-start occurrence lacks typed frame/scene/rng/event order")
        if recording_starts and sequence <= recording_starts[-1]["event_sequence"]:
            raise ValueError("recording frame-start event order is not monotonic")
        occurrence = len(recording_starts)
        value = {"frame": frame, "scene_frame": scene, "rng": rng,
                 "event_sequence": sequence, "occurrence": occurrence}
        recording_starts.append(value)
        recording_start_indices[index] = value
    active = {}
    loads = []
    used_recording_occurrences = set()
    last_load_complete_event_sequence = None
    for index, row in enumerate(episode):
        event = row.get("event")
        if event == "savestate_capture_complete":
            if (index == 0 or episode[index - 1].get("event") != "savestate_capture"
                    or episode[index - 1]["frame"] != row["frame"]):
                raise ValueError("native capture completion lacks its preceding capture")
            active[row["frame"]] = row
        elif event == "savestate_load":
            target = row["frame"]
            odb = row["odb"]
            load_event_sequence = row.get("event_sequence")
            load_complete_event_sequence = (
                episode[index + 1].get("event_sequence") if index + 1 < len(episode) else None)
            if (index + 1 == len(episode) or
                    episode[index + 1].get("event") != "savestate_load_complete" or
                    episode[index + 1]["frame"] != target):
                raise ValueError("native state load did not complete")
            if (not isinstance(load_event_sequence, int) or isinstance(load_event_sequence, bool) or
                    not isinstance(load_complete_event_sequence, int) or
                    isinstance(load_complete_event_sequence, bool) or
                    load_complete_event_sequence <= load_event_sequence or
                    (last_load_complete_event_sequence is not None and
                     load_event_sequence <= last_load_complete_event_sequence)):
                raise ValueError("native prediction-error load event order is malformed")
            if target not in active or active[target]["odb"]["savestate_is_predicting"] != 1:
                raise ValueError("native state load lacks a completed predicted-input capture")
            if first_difference(active[target]["preservation"], row["preservation"]):
                raise ValueError("predicted capture and native load preservation identities differ")
            if (any(odb[key] != 1 for key in
                    ("rollback_active", "rollback_should_load_state", "stable_rollback_active",
                     "stable_rollback_should_load_state")) or
                    odb["savestate_frame"] != target or odb["stable_savestate_frame"] != target or
                    odb["frame"] <= target):
                raise ValueError("state load does not establish the pinned prediction-error path")
            end = odb["stable_rollback_end_frame"]
            depth = end - target
            if odb["rollback_end_frame"] != end or not 1 <= depth <= 7:
                raise ValueError("prediction-error load exceeds the pinned rollback interval")
            next_load_index = next((later_index for later_index in range(index + 2, len(episode))
                                    if episode[later_index].get("event") == "savestate_load"),
                                   len(episode))
            selected_starts = []
            search_index = index + 2
            for scene in range(target, end):
                selected = next((start_index for start_index in range(search_index, next_load_index)
                                 if start_index in recording_start_indices and
                                 recording_start_indices[start_index]["frame"] ==
                                 scene - RECORDING_FRAME_OFFSET and
                                 recording_start_indices[start_index]["scene_frame"] == scene),
                                None)
                if selected is None:
                    raise ValueError("prediction-error load lacks its own ordered recording callback interval")
                selected_value = dict(recording_start_indices[selected])
                previous_candidates = [start for start_index, start in
                                      ((candidate_index, recording_start_indices[candidate_index])
                                       for candidate_index in recording_start_indices)
                                      if start_index < index and
                                      start["frame"] == scene - RECORDING_FRAME_OFFSET and
                                      start["scene_frame"] == scene]
                if not previous_candidates:
                    raise ValueError("prediction-error load lacks its pre-rewind recording occurrence")
                selected_value["pre_rewind_occurrence"] = previous_candidates[-1]["occurrence"]
                selected_starts.append(selected_value)
                search_index = selected + 1
            if not selected_starts or selected_starts[0]["occurrence"] == 0:
                raise ValueError("prediction-error load lacks a preceding recording rewind occurrence")
            previous_start = recording_starts[selected_starts[0]["occurrence"] - 1]
            if not target <= previous_start["scene_frame"] < end:
                raise ValueError("prediction-error load lacks an ordered recording rewind")
            post_occurrences = [value["occurrence"] for value in selected_starts]
            pre_occurrences = [value["pre_rewind_occurrence"] for value in selected_starts]
            if (post_occurrences != list(range(post_occurrences[0], post_occurrences[0] + depth)) or
                    pre_occurrences != list(range(pre_occurrences[0], pre_occurrences[0] + depth))):
                raise ValueError("prediction-error load lacks a contiguous recording occurrence interval")
            selected_occurrences = {value["occurrence"] for value in selected_starts}
            if used_recording_occurrences & selected_occurrences:
                raise ValueError("prediction-error load reuses a recording occurrence")
            used_recording_occurrences.update(selected_occurrences)
            clear = next((later for later in episode[index + 2:next_load_index]
                          if later.get("event") == "online_inputs" and later["source_frame"] > end
                          and all(later["odb"][key] == 0 for key in
                                  ("rollback_active", "rollback_should_load_state",
                                   "stable_rollback_active", "stable_rollback_should_load_state"))), None)
            if clear is None:
                raise ValueError("prediction-error rollback never returned to a normal online-input boundary")
            if (not isinstance(clear.get("event_sequence"), int) or
                    isinstance(clear.get("event_sequence"), bool) or
                    clear["event_sequence"] <= load_complete_event_sequence):
                raise ValueError("native prediction-error clear event order is malformed")
            loads.append({"state_scene_frame": target, "end_scene_frame": end, "depth": depth,
                          "load_event_sequence": load_event_sequence,
                          "load_complete_event_sequence": load_complete_event_sequence,
                          "normal_input_event_sequence": clear["event_sequence"],
                          "repeated_scene_frames": list(range(target, end)),
                          # This is an ordinal join between the native DMA
                          # recording callbacks and the raw replay's Frame
                          # Start vector. It is deliberately carried per load
                          # so a later same-scene load cannot satisfy this one.
                          "recording_frame_start_occurrences": selected_starts})
            last_load_complete_event_sequence = load_complete_event_sequence
            active.clear()  # Original native B2 releases every active state.
        elif event == "savestate_load_complete":
            if index == 0 or episode[index - 1].get("event") != "savestate_load":
                raise ValueError("native load completion lacks its preceding load")
    return {"prediction_error_loads": loads, "observed_load_count": len(loads),
            "recording_frame_starts": recording_starts,
            "depths": [load["depth"] for load in loads],
            "source_resimulation_observation": "repeated recording payloads across each loaded scene interval",
            "resimulation_gate": "full interval coverage; may reject native iterations absent from the recording stream",
            "recording_metadata_timing": "DMA; no callback-time ODB attribution",
            "finalized_state_agreement_verified_separately": True}


def verify_profile(timeline, rows, *, game_sequence, role):
    """Check generated PAD against the source's recorded consumed PAD fields.

    Pinned ASM starts recording at -123 and the scene at 0. Retained producer
    edges at source 76/111 occur at scene 78/113 with the pinned delay of 2.
    Native recording events and exact raw PAD checks revalidate this mapping.
    Input delay is already included in consumed_frame. Analog A/B are unrecorded.
    """
    assignments = [row for row in rows if row.get("event") == "input_profile_assignment"
                   and row.get("game_sequence") == game_sequence]
    if len(assignments) != 1 or any(row.get("role") != role or row.get("local_player_port") != role
                              for row in assignments):
        raise ValueError("native profile assignment does not match the agreed player role")
    generated = {}
    for row in rows:
        if row.get("event") != "input_profile" or row.get("game_sequence") != game_sequence:
            continue
        if (row["role"] != role or row["delay"] != PINNED_INPUT_DELAY
                or row["observer_context"] != "exi"):
            raise ValueError("profile role/input delay differs from the pinned fixture")
        frame = row["source_frame"]
        expected = bytearray(8)
        if role == 1 and 90 <= frame <= 139:
            expected[2] = 127
        elif role == 1 and frame >= 150:
            expected[2] = 129
        elif role == 2 and 90 <= frame <= 126:
            expected[2] = 129
        if role == 2 and 96 <= frame <= 100:
            expected[0] = 1
        if (row["generated_pad_hex"] != expected.hex()
                or row["pad_after_hex"][:16] != expected.hex()
                or row["pad_after_hex"][16:] != row["preserved_tail_hex"]
                or row["consumed_frame"] != frame + row["delay"]):
            raise ValueError(f"native input generation differs at source frame {frame}")
        consumed = row["consumed_frame"]
        if consumed in generated and generated[consumed] != expected.hex():
            raise ValueError(f"conflicting generated input at consumed frame {consumed}")
        generated[consumed] = expected.hex()
    source_frames = sorted(consumed - PINNED_INPUT_DELAY for consumed in generated)
    if not source_frames or source_frames != list(range(1, source_frames[-1] + 1)):
        raise ValueError("generated forward source input lacks its contiguous startup prefix")
    if timeline.frames[0].number != -123:
        raise ValueError("diagnostic replay lacks the pinned first recorded frame")
    compared = 0
    startup_compared = 0
    for frame in timeline.frames:
        consumed = frame.number + RECORDING_FRAME_OFFSET
        source = consumed - PINNED_INPUT_DELAY
        if consumed <= PINNED_INPUT_DELAY:
            # Pinned ASM initializes its local delay ring and dummy peer PADs
            # to zero. This also detects leaked pre-match controller history.
            pad = bytes(8)
        elif consumed not in generated:
            raise ValueError(f"no generated input for recorded frame {frame.number}")
        else:
            pad = bytes.fromhex(generated[consumed])
        expected = {"buttons": int.from_bytes(pad[:2], "big"),
                    "stick": [value if value < 128 else value - 256 for value in pad[2:4]],
                    "cstick": [value if value < 128 else value - 256 for value in pad[4:6]],
                    "triggers": list(pad[6:8])}
        sample = next(value for value in frame.inputs if value.port == role)
        physical = sample.record()["physical"]
        observed = {"buttons": physical["buttons"], "stick": physical["stick"],
                    "cstick": physical["cstick"], "triggers": physical["trigger_bytes"]}
        difference = first_difference(expected, observed, "consumed_pad")
        if difference:
            raise ValueError(json.dumps({"recorded_frame": frame.number,
                                         "consumed_frame": consumed, "source_frame": source,
                                         **difference}, sort_keys=True))
        if source < 90:
            startup_compared += 1
        else:
            compared += 1
    if compared < 100:
        raise ValueError("profile consumption check lacks a bounded active gameplay interval")
    return {"role": role, "generated_frames": len(generated), "compared_active_frames": compared,
            "compared_startup_frames": startup_compared,
            "recorded_frame_mapping": "consumed_frame - 123",
            "uncovered_pad_fields": ["analogA", "analogB"]}


def verify_speculative_corrections(data, timeline, loads, *, remote_port,
                                   recording_frame_starts=None):
    """Find wrong input and changed post-state, followed by the finalized pair.

    Reuse the pinned decoder for individual revisions after its strict timeline
    decoder validates these same bytes. This retains event pairing locally;
    the shared finalized capture schema remains unchanged.
    """
    if len(data) > 256 * 1024 * 1024:
        raise ValueError("revision comparison exceeds the bounded 256 MiB reader")
    if loads and recording_frame_starts is None:
        raise ValueError("prediction-error load lacks its native recording occurrence mapping")
    if decode_timeline(data) != timeline:
        raise ValueError("revision bytes differ from the validated finalized timeline")
    raw = _raw_stream(data)
    cursor = 1 + raw[1]
    revisions = {}
    raw_recording_starts = []
    pending = {}
    current_recording_start = None
    raw_event_index = 0
    while cursor < len(raw):
        command = raw[cursor]
        size = timeline.header.event_payload_sizes[command]
        event = raw[cursor:cursor + size + 1]
        if command == FRAME_START:
            if len(event) < 13:
                raise ValueError("raw Frame Start event is truncated")
            current_recording_start = {
                "frame": struct.unpack_from(">i", event, 1)[0],
                "rng": struct.unpack_from(">I", event, 5)[0],
                "scene_frame": struct.unpack_from(">I", event, 9)[0],
                "occurrence": len(raw_recording_starts),
                "raw_event_index": raw_event_index,
            }
            raw_recording_starts.append(current_recording_start)
        elif command in (PRE_FRAME, POST_FRAME):
            value = (_decode_pre_frame if command == PRE_FRAME else _decode_post_frame)(event)
            if value.port == remote_port and not value.is_follower:
                if (current_recording_start is None or
                        current_recording_start["frame"] != value.frame):
                    raise ValueError("remote PRE/POST pair is outside its recording Frame Start occurrence")
                if command == PRE_FRAME:
                    if value.frame in pending:
                        raise ValueError("remote PRE frame has no completed POST pair")
                    pending[value.frame] = (value, current_recording_start)
                else:
                    if value.frame not in pending:
                        raise ValueError("remote POST frame has no preceding PRE pair")
                    pre, pre_start = pending.pop(value.frame)
                    if pre_start["occurrence"] != current_recording_start["occurrence"]:
                        raise ValueError("remote PRE/POST pair crosses recording Frame Start occurrences")
                    revisions.setdefault(value.frame, []).append({
                        "pre": pre,
                        "post": value,
                        "recording_occurrence": current_recording_start["occurrence"],
                        "raw_event_index": raw_event_index,
                    })
        cursor += size + 1
        raw_event_index += 1
    if pending:
        raise ValueError("remote PRE frame has no completed POST pair")
    pairs_per_occurrence = {occurrence: 0 for occurrence in range(len(raw_recording_starts))}
    for pairs in revisions.values():
        for pair in pairs:
            occurrence = pair["recording_occurrence"]
            pairs_per_occurrence[occurrence] = pairs_per_occurrence.get(occurrence, 0) + 1
    if any(count != 1 for count in pairs_per_occurrence.values()):
        raise ValueError("recording Frame Start occurrence lacks exactly one remote PRE/POST pair")
    if recording_frame_starts is not None:
        if not isinstance(recording_frame_starts, list):
            raise ValueError("native recording Frame Start occurrence vector is malformed")
        for index, value in enumerate(recording_frame_starts):
            if (not isinstance(value, dict) or
                    any(not isinstance(value.get(key), int) or isinstance(value.get(key), bool)
                        for key in ("frame", "rng", "scene_frame", "event_sequence", "occurrence")) or
                    not 0 <= value.get("rng", -1) <= 0xFFFFFFFF or
                    value["occurrence"] != index or
                    (index and value["event_sequence"] <= recording_frame_starts[index - 1]["event_sequence"])):
                raise ValueError("native recording Frame Start occurrence vector is malformed")
        expected_starts = [
            {key: value[key] for key in ("frame", "rng", "scene_frame", "occurrence")}
            for value in recording_frame_starts
        ]
        actual_starts = [
            {key: value[key] for key in ("frame", "rng", "scene_frame", "occurrence")}
            for value in raw_recording_starts
        ]
        if expected_starts != actual_starts:
            raise ValueError("native and replay recording Frame Start occurrence ordering differs")
    finalized = {frame.number: frame for frame in timeline.frames}
    observations = []
    used_recording_occurrences = set()
    previous_load_complete_event_sequence = None
    for load_index, load in enumerate(loads):
        load_event_sequence = load.get("load_event_sequence")
        load_complete_event_sequence = load.get("load_complete_event_sequence")
        if (not isinstance(load_event_sequence, int) or isinstance(load_event_sequence, bool) or
                not isinstance(load_complete_event_sequence, int) or
                isinstance(load_complete_event_sequence, bool) or
                load_complete_event_sequence <= load_event_sequence or
                (previous_load_complete_event_sequence is not None and
                 load_event_sequence <= previous_load_complete_event_sequence)):
            raise ValueError("prediction-error load event order is malformed")
        next_load_event_sequence = None
        if load_index + 1 < len(loads):
            next_load_event_sequence = loads[load_index + 1].get("load_event_sequence")
            if (not isinstance(next_load_event_sequence, int) or
                    isinstance(next_load_event_sequence, bool) or
                    next_load_event_sequence <= load_complete_event_sequence):
                raise ValueError("prediction-error load event order is malformed")
        selected_starts = load.get("recording_frame_start_occurrences")
        if not isinstance(selected_starts, list) or not selected_starts:
            raise ValueError("prediction-error load lacks its native recording occurrence mapping")
        selected_by_scene = {}
        for selected in selected_starts:
            if not isinstance(selected, dict):
                raise ValueError("prediction-error recording occurrence is malformed")
            required = ("frame", "rng", "scene_frame", "event_sequence", "occurrence",
                        "pre_rewind_occurrence")
            if any(key not in selected for key in required):
                raise ValueError("prediction-error recording occurrence lacks ordered identity")
            if (not isinstance(selected["event_sequence"], int) or
                    isinstance(selected["event_sequence"], bool) or
                    not isinstance(selected["frame"], int) or
                    isinstance(selected["frame"], bool) or
                    not isinstance(selected["rng"], int) or
                    isinstance(selected["rng"], bool) or
                    not 0 <= selected["rng"] <= 0xFFFFFFFF or
                    not isinstance(selected["scene_frame"], int) or
                    isinstance(selected["scene_frame"], bool)):
                raise ValueError("prediction-error recording occurrence lacks typed identity")
            occurrence = selected["occurrence"]
            if (not isinstance(occurrence, int) or isinstance(occurrence, bool) or
                    occurrence < 0 or occurrence >= len(raw_recording_starts) or
                    raw_recording_starts[occurrence]["frame"] != selected["frame"] or
                    raw_recording_starts[occurrence]["rng"] != selected["rng"] or
                    raw_recording_starts[occurrence]["scene_frame"] != selected["scene_frame"] or
                    selected["event_sequence"] != recording_frame_starts[occurrence]["event_sequence"]):
                raise ValueError("prediction-error recording occurrence does not match raw Frame Start")
            if occurrence in used_recording_occurrences:
                raise ValueError("prediction-error load reuses a recording occurrence")
            pre_occurrence = selected["pre_rewind_occurrence"]
            if (not isinstance(pre_occurrence, int) or isinstance(pre_occurrence, bool) or
                    pre_occurrence < 0 or pre_occurrence >= len(raw_recording_starts) or
                    raw_recording_starts[pre_occurrence]["frame"] != selected["frame"] or
                    raw_recording_starts[pre_occurrence]["scene_frame"] != selected["scene_frame"] or
                    pre_occurrence > occurrence):
                raise ValueError("prediction-error pre-rewind occurrence does not match raw Frame Start")
            pre_event_sequence = recording_frame_starts[pre_occurrence]["event_sequence"]
            post_event_sequence = selected["event_sequence"]
            if (pre_event_sequence >= load_event_sequence or
                    post_event_sequence <= load_complete_event_sequence or
                    (next_load_event_sequence is not None and
                     post_event_sequence >= next_load_event_sequence)):
                raise ValueError("prediction-error recording occurrence is outside its load interval")
            if selected["scene_frame"] in selected_by_scene:
                raise ValueError("prediction-error load reuses a recording occurrence")
            selected_by_scene[selected["scene_frame"]] = {
                "pre": pre_occurrence, "post": occurrence}
            used_recording_occurrences.add(occurrence)
        expected_scenes = list(range(load["state_scene_frame"], load["end_scene_frame"]))
        if sorted(selected_by_scene) != expected_scenes:
            raise ValueError("prediction-error recording occurrence interval is incomplete")
        correction = None
        for scene in range(load["state_scene_frame"], load["end_scene_frame"]):
            frame = scene - RECORDING_FRAME_OFFSET
            final = finalized[frame]
            final_pre = next(value for value in final.inputs if value.port == remote_port)
            final_post = next(value for value in final.expected if value.port == remote_port)
            occurrences = selected_by_scene[scene]
            pre_pairs = [pair for pair in revisions.get(frame, [])
                         if pair["recording_occurrence"] == occurrences["pre"]]
            post_pairs = [pair for pair in revisions.get(frame, [])
                          if pair["recording_occurrence"] == occurrences["post"]]
            if occurrences["pre"] >= occurrences["post"]:
                raise ValueError("prediction-error load lacks a later recording occurrence after rewind")
            if pre_pairs and post_pairs:
                # The last complete pair before this load is the prediction
                # witness. The finalized pair must be in this load's own
                # post-rewind occurrence; older/later occurrences are out of
                # scope even if their bytes happen to match.
                wrong = pre_pairs[-1]
                input_difference = first_difference(
                    wrong["pre"].reconstructed_pad(), final_pre.reconstructed_pad(), "physical_input")
                state_difference = first_difference(
                    wrong["post"].record(), final_post.record(), "post_state")
                # If a malformed or future producer supplies multiple pairs,
                # use the last complete post pair rather than allowing an
                # earlier matching revision to satisfy this load. The normal
                # pinned producer shape is one remote pair per occurrence;
                # duplicate occurrences are rejected above.
                finalized_pair = post_pairs[-1]
                if input_difference and state_difference and finalized_pair is not None:
                    if (finalized_pair["pre"] != final_pre or
                            finalized_pair["post"] != final_post):
                        finalized_pair = None
                if input_difference and state_difference and finalized_pair is not None:
                    correction = {"scene_frame": scene, "recorded_frame": frame,
                                  "remote_port": remote_port,
                                  "input_difference": input_difference,
                                  "post_state_difference": state_difference,
                                  "corrected_revision_matches_finalized_pair": True,
                                  "recording_frame_start_occurrence": occurrences["post"],
                                  "pre_rewind_recording_frame_start_occurrence": occurrences["pre"],
                                  "revision_pair_index": len(pre_pairs) - 1,
                                  "load_event_sequence": load["load_event_sequence"]}
            if correction:
                break
        if correction is None:
            raise ValueError("prediction-error load lacks a wrong-input/state revision corrected to the finalized pair")
        observations.append(correction)
        previous_load_complete_event_sequence = load_complete_event_sequence
    return {"observed_corrections": observations, "correction_count": len(observations),
            "scope": "paired remote pre/post revisions within each observed loaded scene interval"}


class RollbackRun(PairRun):
    baseline_replays: dict[int, dict[str, Path]] | None = None
    baseline_identities: dict | None = None
    service_identity: dict | None = None

    def _make_profiles(self, *args):
        if self.baseline_identities is not None:
            current = {key: self.evidence.get(key) for key in ARTIFACT_IDENTITY_FIELDS
                       if key in self.evidence and key != "service_framing_lineage_sha256"}
            expected = {key: value for key, value in self.baseline_identities.items()
                        if key != "service_framing_lineage_sha256"}
            difference = first_difference(expected, current, "baseline_artifact_identity")
            if difference:
                raise ValueError(json.dumps(difference, sort_keys=True))
        super()._make_profiles(*args)
        if self.service_identity is not None:
            if self.evidence.get("matchmaker_sha256") != self.service_identity["service_binary_sha256"]:
                raise ValueError("configured matchmaker binary differs from the service prerequisite")
            self.evidence["service_framing_lineage_sha256"] = (
                self.service_identity["service_framing_lineage_sha256"])
            self.evidence["service_framing"] = dict(self.service_identity)
        if self.baseline_identities is not None:
            difference = first_difference(self.baseline_identities, artifact_identities(self.evidence),
                                          "baseline_artifact_identity")
            if difference:
                raise ValueError(json.dumps(difference, sort_keys=True))

    def run(self, **kwargs):
        try:
            return super().run(**kwargs)
        except BaseException as error:
            output = self.work / "evidence.json"
            if not output.exists():
                # Parent preflight precedes process ownership. Retain the
                # preparation failure without claiming a launched lifecycle.
                self.evidence.update(result="failed-preparation",
                                     failure=f"{type(error).__name__}: {error}")
                with output.open("x", encoding="utf-8") as stream:
                    json.dump(self.evidence, stream, indent=2, sort_keys=True)
                    stream.write("\n")
            raise

    def _run_game(self, game_number):
        receipt = {"schema": "melee-web-desktop-rollback-game-v1", "result": "failed",
                   "game": game_number, "scenario": self.rollback_diagnostic}
        output = self.work / f"game-{game_number:02d}-comparison.json"
        try:
            super()._run_game(game_number)
            paths = {}
            timelines = {}
            replay_data = {}
            for name in ("p1", "p2"):
                expected_hash = self.evidence["games"][-1]["replays"][name]["sha256"]
                matches = [path for path in self.profiles[name].replay_root.glob("*.slp")
                           if _sha256(path) == expected_hash]
                if len(matches) != 1:
                    raise ValueError(f"{name} completed replay identity is ambiguous")
                paths[name] = matches[0]
                if matches[0].stat().st_size > 256 * 1024 * 1024:
                    raise ValueError("diagnostic replay exceeds the bounded 256 MiB reader")
                replay_data[name] = matches[0].read_bytes()
                timelines[name] = decode_timeline(replay_data[name])
                validate_complete(timelines[name])
                if timelines[name].header.record()["stage_id"] != 32:
                    raise ValueError("diagnostic profile did not use Final Destination")
            receipt["peer_comparison"] = compare_timelines(timelines["p1"], timelines["p2"])
            if receipt["peer_comparison"]["result"] != "passed":
                raise ValueError("finalized peer timelines differ")
            receipt["profile_consumption"] = {}
            transport_reports = {}
            for name, role in (("p1", 1), ("p2", 2)):
                rows = read_diagnostic_log(self.work / f"{name}-rollback.jsonl")
                receipt.setdefault("diagnostic_events", {})[name] = verify_log_order(rows)
                receipt.setdefault("recording_clock", {})[name] = verify_recording_clock(
                    timelines[name], rows, game_number=game_number)
                transport_reports[name] = verify_transport(
                    rows, game_sequence=game_number - 1, scenario=self.rollback_diagnostic, role=role)
                receipt.setdefault("transport", {})[name] = transport_reports[name]
                receipt.setdefault("rollback_observations", {})[name] = verify_prediction_rollbacks(
                    rows, game_number=game_number)
                receipt.setdefault("speculative_corrections", {})[name] = verify_speculative_corrections(
                    replay_data[name], timelines[name],
                    receipt["rollback_observations"][name]["prediction_error_loads"],
                    remote_port=3 - role,
                    recording_frame_starts=receipt["rollback_observations"][name]["recording_frame_starts"])
                receipt["profile_consumption"][name] = verify_profile(
                    timelines[name], rows, game_sequence=game_number - 1, role=role)
            if self.rollback_diagnostic in {"jitter", "reorder"}:
                sender_rows = read_diagnostic_log(self.work / "p2-rollback.jsonl")
                receiver_rows = read_diagnostic_log(self.work / "p1-rollback.jsonl")
                receipt["transport_delivery"] = validate_transport_delivery(
                    sender_rows, receiver_rows, scenario=self.rollback_diagnostic,
                    game_sequence=game_number - 1)
            if self.rollback_diagnostic == "duplicate":
                sender = transport_reports["p2"]
                receiver = transport_reports["p1"].get("receiver", {})
                if sender.get("payload_hash") not in receiver.get("payload_hashes", []):
                    raise ValueError("duplicate sender and receiver packet identities did not join")
            if self.rollback_diagnostic == "hold" and not any(
                    load["state_scene_frame"] <= 98 < load["end_scene_frame"]
                    for load in receipt["rollback_observations"]["p1"]["prediction_error_loads"]):
                raise ValueError("held A transition did not produce an observed prediction-error rollback")
            if self.baseline_replays is not None:
                receipt["baseline_comparison"] = {}
                for name in ("p1", "p2"):
                    with self.baseline_replays[game_number][name].open("rb") as stream:
                        baseline = read_timeline(stream)
                    receipt["baseline_comparison"][name] = compare_timelines(baseline, timelines[name])
                    if receipt["baseline_comparison"][name]["result"] != "passed":
                        raise ValueError(f"{name} finalized timeline differs from no-impairment baseline")
            receipt["replays"] = {name: {"file": str(path.relative_to(self.work)),
                                                "sha256": _sha256(path)}
                                   for name, path in paths.items()}
            receipt["result"] = "passed"
        except BaseException as error:
            receipt["failure"] = f"{type(error).__name__}: {error}"
            raise
        finally:
            with output.open("x", encoding="utf-8") as stream:
                json.dump(receipt, stream, indent=2, sort_keys=True)
                stream.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True, help="absent private output directory")
    parser.add_argument("--scenario", choices=("none", "hold", "drop", "duplicate", "jitter", "reorder"), default="none")
    parser.add_argument("--baseline-root", type=Path, help="a passed prior no-impairment run")
    parser.add_argument("--repeat", type=int, choices=(1, 2), default=2)
    parser.add_argument("--profile-temp-root", type=Path, required=True)
    parser.add_argument("--service-lineage", type=Path, required=True,
                        help="PR #133 service framing receipt for the configured matchmaker build")
    for option in ("client-binary", "matchmaker-binary", "dolphin-build", "matchmaker-build",
                   "dolphin-source", "enet-source"):
        parser.add_argument(f"--{option}", type=Path)
    parser.add_argument("--boot-timeout", type=float, default=60)
    parser.add_argument("--pair-timeout", type=float, default=90)
    parser.add_argument("--game-timeout", type=float, default=420)
    parser.add_argument("--rematch-timeout", type=float, default=90)
    args = parser.parse_args(argv)
    if args.scenario != "none" and args.baseline_root is None:
        parser.error("impaired run requires --baseline-root")
    root = args.run_root.expanduser().resolve()
    root.mkdir(parents=True, exist_ok=False, mode=0o700)
    report = {"schema": "melee-web-desktop-rollback-run-v1", "result": "failed",
              "scenario": args.scenario, "rollback_correctness_claimed": False,
              "browser_cross_play_claimed": False, "fresh_profile_repeatability_verified": False,
              "machine": {"platform": platform.platform(), "arch": platform.machine(),
                          "python": sys.version},
              "source_sha256": {},
              "cycles": []}
    try:
        report["source_sha256"] = {name: _sha256(ROOT / name) for name in
                                  ("reference-capture/slippi/run_rollback.py",
                                   "reference-capture/slippi/transport_fault_recipes.py",
                                   "reference-capture/slippi/compare_rollback.py",
                                   "reference-capture/slippi/run_local.py",
                                   "reference-capture/slippi/process.py", "reference-capture/slippi/runtime.py",
                                   "tools/slippi_format.py", "tools/slippi_rollback_diagnostic.py")}
        service_identity = validate_service_lineage(args.service_lineage)
        report["service_framing"] = dict(service_identity)
        baseline_replays = None
        baseline_identities = None
        baseline_binding = None
        if args.baseline_root is not None:
            baseline_root = args.baseline_root.expanduser().resolve(strict=True)
            baseline_binding = validate_baseline_receipts(baseline_root, service_identity)
            baseline_identities = baseline_binding["artifact_identity"]
            baseline_replays = baseline_binding["baseline_replays"]
            report["baseline_receipt_binding"] = {
                key: value for key, value in baseline_binding.items() if key != "baseline_replays"
            }
        for cycle in range(1, args.repeat + 1):
            print(f"{args.scenario} cycle {cycle}/{args.repeat}: fresh profile pair", flush=True)
            run = RollbackRun(
                root=root, disc=args.disc.expanduser().resolve(strict=True), cycle=cycle,
                timeouts={"boot": args.boot_timeout, "pair": args.pair_timeout,
                          "game": args.game_timeout, "rematch": args.rematch_timeout},
                profile_temp_root=args.profile_temp_root,
                rollback_diagnostic=args.scenario,
                **{name: getattr(args, name) for name in
                   ("client_binary", "matchmaker_binary", "dolphin_build", "matchmaker_build",
                    "dolphin_source", "enet_source")})
            run.baseline_replays = baseline_replays
            run.baseline_identities = baseline_identities
            run.service_identity = service_identity
            cycle_receipt = {"cycle": cycle, "result": "incomplete",
                             "evidence": f"cycle-{cycle:02d}/evidence.json"}
            report["cycles"].append(cycle_receipt)
            try:
                evidence = run.run(disconnect_after_rematch=(cycle == args.repeat))
            finally:
                cycle_receipt["result"] = run.evidence["result"]
            if evidence["result"] != "passed":
                raise ValueError("desktop fixture did not complete")
            if baseline_replays is None:
                baseline_identities = artifact_identities(evidence)
                baseline_replays = {}
                for game in (1, 2):
                    receipt = json.loads((run.work / f"game-{game:02d}-comparison.json").read_text())
                    validate_baseline_game(receipt, game)
                    baseline_replays[game] = {name: run.work / receipt["replays"][name]["file"]
                                              for name in ("p1", "p2")}
        report["result"] = "passed"
        report["fresh_profile_repeatability_verified"] = args.repeat == 2
        report["rollback_correctness_claimed"] = args.scenario == "hold"
        if args.scenario == "hold":
            report["rollback_correctness_scope"] = (
                "six-packet hold fixture only: completed prediction-error native loads and repeated source "
                "recording callbacks, wrong input/state revisions corrected to finalized pairs, "
                "with exact parsed declared finalized input/state/RNG/outcomes "
                "between both desktop peers and their corresponding no-impairment games")
    except BaseException as error:
        report["failure"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        with (root / "rollback-run.json").open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, sort_keys=True)
            stream.write("\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
