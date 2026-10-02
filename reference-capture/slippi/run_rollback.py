# SPDX-License-Identifier: MIT
"""Run the pinned desktop frame-input baseline and bounded rollback fixture.

Reuse the ordinary testbed's menu, pairing, rematch, socket and cleanup gates.
The opt-in native diagnostic generates gameplay PAD bytes; controller Pipes
remain neutral during the match. Raw replays and logs remain private evidence.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import re
import sys

from compare_rollback import compare_timelines, first_difference, validate_complete
from run_local import PairRun, ROOT, _sha256
from slippi_format import (PRE_FRAME, POST_FRAME, _raw_stream, _decode_pre_frame,
                           _decode_post_frame, decode_timeline, read_timeline)
from slippi_rollback_diagnostic import SCHEMA

PINNED_INPUT_DELAY = 2  # Config/MainSettings.cpp SLIPPI_ONLINE_DELAY default.
RECORDING_FRAME_OFFSET = 123  # Recording index starts at -123, scene at 0.
ARTIFACT_IDENTITY_FIELDS = (
    "client_binary_sha256", "matchmaker_sha256", "dependency_lock_sha256",
    "downstream_patch_sha256", "game_modification_sha256", "disc_sha256", "bundle_sys_sha256")


def artifact_identities(evidence):
    return {key: evidence[key] for key in ARTIFACT_IDENTITY_FIELDS}


def validate_baseline_game(receipt, game):
    if (receipt.get("result") != "passed" or receipt.get("game") != game
            or receipt.get("scenario") != "none"
            or receipt.get("peer_comparison", {}).get("result") != "passed"):
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
    action = scenario if role == 2 else "none"
    if any(row.get("observer_context") != "transport" for row in faults):
        raise ValueError("fault observations came from the wrong native context")
    if action == "none":
        if faults:
            raise ValueError("no-impairment peer unexpectedly applied a transport fault")
        return {"action": action, "fault_events": 0}
    for row in faults:
        field = "release_payload_hash" if row.get("action") == "hold_release" else "payload_hash"
        if not isinstance(row.get(field), str) or not re.fullmatch(r"[0-9a-f]{16}", row[field]):
            raise ValueError("fault packet lacks its native FNV-1a64 identity")
        if (row["history_count"] <= 0 or
                row["history_last_frame"] - row["history_first_frame"] + 1 != row["history_count"]):
            raise ValueError("fault packet history is not contiguous")
    if action == "drop":
        if (len(faults) != 1 or faults[0]["action"] != "drop" or
                faults[0]["packet_frame"] != 98 or faults[0]["drop_count"] != 1 or
                faults[0]["history_last_frame"] != 98):
            raise ValueError("single-packet loss schedule was not observed exactly")
        return {"action": action, "fault_events": 1, "packet_frame": 98,
                "redundant_history_preserved": True}
    if action != "hold":
        raise ValueError("unsupported transport observation scenario")
    if ([row["action"] for row in faults] != ["hold_begin", *(["hold"] * 5), "hold_release"]
            or [row["packet_frame"] for row in faults[:-1]] != list(range(98, 104))):
        raise ValueError("six-packet hold interval was not observed exactly")
    for count, row in enumerate(faults[:-1], 1):
        if (row["held_count"] != count or row["begin_count"] != (1 if count == 1 else 0)
                or row["history_last_frame"] != row["packet_frame"]):
            raise ValueError("held packet queue or history differs from the declared schedule")
    release = faults[-1]
    if (release["release_packet_frame"] != 104 or release["release_count"] != 6 or
            release["held_frames"] != list(range(98, 104)) or
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
    scenes = [row["scene_frame"] for row in episode if row.get("event") == "recording_frame_start"]
    counts = Counter(scenes)
    rewinds = [(previous, current) for previous, current in zip(scenes, scenes[1:])
               if current <= previous]
    active = {}
    loads = []
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
            if (index + 1 == len(episode) or
                    episode[index + 1].get("event") != "savestate_load_complete" or
                    episode[index + 1]["frame"] != target):
                raise ValueError("native state load did not complete")
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
            if (any(counts[scene] < 2 for scene in range(target, end)) or
                    not any(current == target and target <= previous < end
                            for previous, current in rewinds)):
                raise ValueError("prediction-error load lacks repeated source recording callback coverage")
            clear = next((later for later in episode[index + 2:]
                          if later.get("event") == "online_inputs" and later["source_frame"] > end
                          and all(later["odb"][key] == 0 for key in
                                  ("rollback_active", "rollback_should_load_state",
                                   "stable_rollback_active", "stable_rollback_should_load_state"))), None)
            if clear is None:
                raise ValueError("prediction-error rollback never returned to a normal online-input boundary")
            loads.append({"state_scene_frame": target, "end_scene_frame": end, "depth": depth,
                          "load_event_sequence": row["event_sequence"],
                          "load_complete_event_sequence": episode[index + 1]["event_sequence"],
                          "normal_input_event_sequence": clear["event_sequence"],
                          "repeated_scene_frames": list(range(target, end))})
            active.clear()  # Original native B2 releases every active state.
        elif event == "savestate_load_complete":
            if index == 0 or episode[index - 1].get("event") != "savestate_load":
                raise ValueError("native load completion lacks its preceding load")
    return {"prediction_error_loads": loads, "observed_load_count": len(loads),
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


def verify_speculative_corrections(data, timeline, loads, *, remote_port):
    """Find wrong input and changed post-state, followed by the finalized pair.

    Reuse the pinned decoder for individual revisions after its strict timeline
    decoder validates these same bytes. This retains event pairing locally;
    the shared finalized capture schema remains unchanged.
    """
    if len(data) > 256 * 1024 * 1024:
        raise ValueError("revision comparison exceeds the bounded 256 MiB reader")
    if decode_timeline(data) != timeline:
        raise ValueError("revision bytes differ from the validated finalized timeline")
    raw = _raw_stream(data)
    cursor = 1 + raw[1]
    revisions = {}
    pending = {}
    while cursor < len(raw):
        command = raw[cursor]
        size = timeline.header.event_payload_sizes[command]
        event = raw[cursor:cursor + size + 1]
        if command in (PRE_FRAME, POST_FRAME):
            value = (_decode_pre_frame if command == PRE_FRAME else _decode_post_frame)(event)
            if value.port == remote_port and not value.is_follower:
                if command == PRE_FRAME:
                    pending[value.frame] = value
                else:
                    revisions.setdefault(value.frame, []).append((pending.pop(value.frame), value))
        cursor += size + 1
    finalized = {frame.number: frame for frame in timeline.frames}
    observations = []
    for load in loads:
        correction = None
        for scene in range(load["state_scene_frame"], load["end_scene_frame"]):
            frame = scene - RECORDING_FRAME_OFFSET
            final = finalized[frame]
            final_pre = next(value for value in final.inputs if value.port == remote_port)
            final_post = next(value for value in final.expected if value.port == remote_port)
            pairs = revisions.get(frame, [])
            for index, (pre, post) in enumerate(pairs):
                input_difference = first_difference(pre.reconstructed_pad(), final_pre.reconstructed_pad(),
                                                    "physical_input")
                state_difference = first_difference(post.record(), final_post.record(), "post_state")
                if (input_difference and state_difference and
                        any(later_pre == final_pre and later_post == final_post
                            for later_pre, later_post in pairs[index + 1:])):
                    correction = {"scene_frame": scene, "recorded_frame": frame,
                                  "remote_port": remote_port,
                                  "input_difference": input_difference,
                                  "post_state_difference": state_difference,
                                  "corrected_revision_matches_finalized_pair": True,
                                  "load_event_sequence": load["load_event_sequence"]}
                    break
            if correction:
                break
        if correction is None:
            raise ValueError("prediction-error load lacks a wrong-input/state revision corrected to the finalized pair")
        observations.append(correction)
    return {"observed_corrections": observations, "correction_count": len(observations),
            "scope": "paired remote pre/post revisions within each observed loaded scene interval"}


class RollbackRun(PairRun):
    baseline_replays: dict[int, dict[str, Path]] | None = None
    baseline_identities: dict | None = None

    def _make_profiles(self, *args):
        if self.baseline_identities is not None:
            difference = first_difference(self.baseline_identities, artifact_identities(self.evidence),
                                          "baseline_artifact_identity")
            if difference:
                raise ValueError(json.dumps(difference, sort_keys=True))
        super()._make_profiles(*args)

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
            for name, role in (("p1", 1), ("p2", 2)):
                rows = read_diagnostic_log(self.work / f"{name}-rollback.jsonl")
                receipt.setdefault("diagnostic_events", {})[name] = verify_log_order(rows)
                receipt.setdefault("recording_clock", {})[name] = verify_recording_clock(
                    timelines[name], rows, game_number=game_number)
                receipt.setdefault("transport", {})[name] = verify_transport(
                    rows, game_sequence=game_number - 1, scenario=self.rollback_diagnostic, role=role)
                receipt.setdefault("rollback_observations", {})[name] = verify_prediction_rollbacks(
                    rows, game_number=game_number)
                receipt.setdefault("speculative_corrections", {})[name] = verify_speculative_corrections(
                    replay_data[name], timelines[name],
                    receipt["rollback_observations"][name]["prediction_error_loads"], remote_port=3 - role)
                receipt["profile_consumption"][name] = verify_profile(
                    timelines[name], rows, game_sequence=game_number - 1, role=role)
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
    parser.add_argument("--scenario", choices=("none", "hold", "drop"), default="none")
    parser.add_argument("--baseline-root", type=Path, help="a passed prior no-impairment run")
    parser.add_argument("--repeat", type=int, choices=(1, 2), default=2)
    parser.add_argument("--profile-temp-root", type=Path, required=True)
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
                                   "reference-capture/slippi/compare_rollback.py",
                                   "reference-capture/slippi/run_local.py",
                                   "reference-capture/slippi/process.py", "reference-capture/slippi/runtime.py",
                                   "tools/slippi_format.py", "tools/slippi_rollback_diagnostic.py")}
        baseline_replays = None
        baseline_identities = None
        if args.baseline_root is not None:
            baseline_root = args.baseline_root.expanduser().resolve(strict=True)
            prior = json.loads((baseline_root / "rollback-run.json").read_text())
            if prior.get("result") != "passed" or prior.get("scenario") != "none":
                raise ValueError("baseline must be a passed no-impairment fixture")
            baseline_evidence = json.loads((baseline_root / "cycle-01/evidence.json").read_text())
            if baseline_evidence.get("result") != "passed":
                raise ValueError("baseline cycle lacks passed native lifecycle evidence")
            baseline_identities = artifact_identities(baseline_evidence)
            baseline_replays = {}
            for game in (1, 2):
                receipt = json.loads((baseline_root / "cycle-01" /
                                      f"game-{game:02d}-comparison.json").read_text())
                validate_baseline_game(receipt, game)
                baseline_replays[game] = {}
                for name in ("p1", "p2"):
                    replay = receipt["replays"][name]
                    path = baseline_root / "cycle-01" / replay["file"]
                    if _sha256(path) != replay["sha256"]:
                        raise ValueError("baseline replay identity changed")
                    baseline_replays[game][name] = path
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
