#!/usr/bin/env python3
"""Run the bounded source-profile Slippi prefix check.

The replay parser is the only producer of input bytes.  Native state, the
callback trace, and the declared post fields are observations.  This command
requires all source/runtime identities explicitly, writes a fresh private
report directory, and stops at the first mismatch.  It is a diagnostic prefix or
measured recorded-match check; it does not claim postgame, browser gameplay,
or rollback.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import importlib.util
import math
import signal
import time

SCENE_FIRST = 0
SCENE_LAST = 110
SUPPORTED_SCENE_LASTS = (110, 1341)
PROFILE_OFFSET = 0x1234
DEFAULT_SEED = 0x13579BDF
NATIVE_INITIALIZER_PROFILE = "native"
DEFAULT_INITIALIZER_PROFILE = "default"
NATIVE_INITIALIZER_SEED = 4660
NATIVE_STAGE_KIND = 37
INPUT_PORT_BYTES = 11
INPUT_BYTES = 44
NATIVE_SCHEMA = "melee-web-source-slippi-profile-input-v1"
PROFILE_WORKER = Path(__file__).with_name("slippi_profile_prefix_runtime.mjs")


class CheckError(RuntimeError):
    pass


class ParentInterrupted(CheckError):
    pass


def fail(message: str) -> None:
    raise CheckError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_sha256(path: Path) -> str:
    path = path.expanduser()
    if path.is_symlink() or not path.is_dir():
        fail(f"assets directory is missing or symlinked: {path}")
    digest = hashlib.sha256()
    for child in sorted(path.rglob("*")):
        if child.is_symlink():
            fail(f"assets entry is symlinked: {child}")
        if not child.is_file():
            continue
        relative = child.relative_to(path).as_posix().encode()
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(sha256_file(child).encode())
    return digest.hexdigest()


def bits(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08x}"


def parse_hash(value: str, label: str) -> str:
    value = value.lower()
    if len(value) != 64 or any(char not in "0123456789abcdef" for char in value):
        fail(f"{label} must be a SHA-256 hex digest")
    return value


def checked_hash(path: Path, expected: str, label: str) -> str:
    path = path.expanduser()
    if path.is_symlink() or not path.is_file():
        fail(f"{label} is not a regular non-symlink file: {path}")
    actual = sha256_file(path)
    if actual != expected:
        fail(f"{label} SHA-256 differs: expected {expected}, got {actual}")
    return actual


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parser_api():
    """Load the repository parser from both module and direct-script layouts."""
    try:
        from tools.slippi_format import SlippiFormatError, read_timeline
        return SlippiFormatError, read_timeline
    except ModuleNotFoundError:
        repository_root = Path(__file__).resolve().parents[1]
        if str(repository_root) not in sys.path:
            sys.path.insert(0, str(repository_root))
        from tools.slippi_format import SlippiFormatError, read_timeline
        return SlippiFormatError, read_timeline


def pack_port(raw: dict, error: int) -> bytes:
    try:
        return struct.pack(
            ">HbbbbBBBBb",
            int(raw["buttons"]),
            int(raw["stick"][0]), int(raw["stick"][1]),
            int(raw["cstick"][0]), int(raw["cstick"][1]),
            int(raw["triggers"][0]), int(raw["triggers"][1]),
            0, 0, error,
        )
    except (KeyError, IndexError, TypeError, ValueError, struct.error) as exc:
        fail(f"invalid reconstructed raw PAD fields: {exc}")


def expected_state(post) -> dict:
    if post.stocks is None or post.ground_or_air is None:
        fail(f"post frame {post.frame} port {post.port} lacks declared stock/ground state")
    return {
        "fighter_kind": post.character_id,
        "motion_id": post.action_state,
        "position_x_bits": bits(post.position_bits[0]),
        "position_y_bits": bits(post.position_bits[1]),
        "facing_bits": bits(post.facing_bits),
        "damage_bits": bits(post.percent_bits),
        "shield_bits": bits(post.shield_bits),
        "stocks": post.stocks,
        "ground_or_air": post.ground_or_air,
    }


def validate_scene_last(scene_last: int) -> int:
    if scene_last not in SUPPORTED_SCENE_LASTS:
        fail(f"--scene-last must be one of {', '.join(map(str, SUPPORTED_SCENE_LASTS))}")
    return scene_last


def normalized_frames(replay: Path, scene_last: int = SCENE_LAST) -> list[dict]:
    validate_scene_last(scene_last)
    SlippiFormatError, read_timeline = parser_api()
    try:
        with replay.open("rb") as stream:
            timeline = read_timeline(stream)
    except (OSError, SlippiFormatError) as exc:
        fail(f"cannot parse replay: {exc}")

    by_scene: dict[int, object] = {}
    for frame in timeline.frames:
        scene = frame.scene_frame_counter
        if scene is None or not SCENE_FIRST <= scene <= scene_last:
            continue
        if scene in by_scene:
            fail(f"replay has duplicate scene frame {scene}")
        by_scene[scene] = frame
    missing = [scene for scene in range(SCENE_FIRST, scene_last + 1) if scene not in by_scene]
    if missing:
        fail(f"replay does not contain contiguous scene prefix; first missing {missing[0]}")

    result = []
    for scene in range(SCENE_FIRST, scene_last + 1):
        frame = by_scene[scene]
        if frame.start_random_seed is None:
            fail(f"scene {scene} lacks start_random_seed")
        inputs = {value.port: value for value in frame.inputs if not value.is_follower}
        expected = {value.port: value for value in frame.expected if not value.is_follower}
        if set(inputs) != {1, 2} or set(expected) != {1, 2}:
            fail(f"scene {scene} must have exactly active ports 1 and 2")
        pads = []
        input_records = []
        for port in (1, 2):
            value = inputs[port]
            if not value.physical_complete:
                fail(f"scene {scene} port {port} has incomplete physical PAD fields")
            raw = value.reconstructed_pad()
            packed = pack_port(raw, 0)
            pads.append(packed)
            input_records.append({
                "port": port,
                "raw": raw,
                "processed": {
                    "stick_bits": [bits(item) for item in value.processed_stick_bits],
                    "cstick_bits": [bits(item) for item in value.processed_cstick_bits],
                    "trigger_bits": bits(value.processed_trigger_bits),
                    "buttons": value.processed_buttons,
                },
                "expected": expected_state(expected[port]),
            })
        pads.extend(pack_port({"buttons": 0, "stick": (0, 0), "cstick": (0, 0),
                               "triggers": (0, 0)}, -1) for _ in (3, 4))
        combined = b"".join(pads)
        if len(combined) != INPUT_BYTES:
            fail(f"scene {scene} did not encode exactly 44 PAD bytes")
        result.append({
            "scene_frame": scene,
            "recording_frame": frame.number,
            "start_random_seed": frame.start_random_seed,
            "combined_pad_bytes_hex": combined.hex(),
            "inputs": input_records,
            "raw_contract": {
                "bytes": INPUT_BYTES,
                "active_ports": [1, 2],
                "disabled_ports": {"3": -1, "4": -1},
                "unobserved_pad_fields": ["analogA", "analogB"],
            },
        })
    return result


def compare_declared(observed: dict, expected: dict) -> list[str]:
    """Small pure comparator used by the worker and its mutation self-test."""
    return [key for key in expected if observed.get(key) != expected[key]]


def cleanup_succeeded(cleanup: object) -> bool:
    """Return true only when the owned process group was released cleanly."""
    if not isinstance(cleanup, dict):
        return False
    if cleanup.get("process_group_released") is not True:
        return False
    if cleanup.get("stop_error") or cleanup.get("close_error"):
        return False
    if cleanup.get("supervisor_close_succeeded") is False:
        return False
    return True


def worker_outcome(
    result_code: int | None,
    *,
    timed_out: bool,
    worker_error: BaseException | None,
    cleanup: object,
    worker_stderr: str | None = None,
) -> dict:
    """Normalize worker/cleanup status without losing a worker failure report.

    ProcessSupervisor deliberately merges the worker's stderr into its owned log,
    so a parent-side stderr value is unavailable.  Keep a deterministic fallback
    for the terminal error while retaining the worker log identity in the report.
    """
    cleanup_ok = cleanup_succeeded(cleanup)
    error_text = str(worker_error) if worker_error is not None else None
    stderr = (worker_stderr or "").strip()
    if not stderr:
        stderr = error_text or (
            "profile worker failed; see worker.process.log"
            if result_code not in (None, 0) or timed_out
            else "profile worker produced no exit code; see worker.process.log"
            if result_code is None
            else "owned process-group cleanup did not succeed"
            if not cleanup_ok
            else ""
        )
    if timed_out:
        failure = "profile worker timed out"
    elif worker_error is not None:
        failure = f"profile worker raised: {error_text}"
    elif result_code is None:
        failure = "profile worker produced no exit code"
    elif result_code != 0:
        failure = f"profile worker exited with code {result_code}"
    elif not cleanup_ok:
        failure = "owned process-group cleanup did not succeed"
    else:
        failure = None
    return {
        "result": "passed" if failure is None else "failed",
        "failure": failure,
        "worker_returncode": result_code,
        "worker_error": error_text,
        "worker_stderr": stderr,
        "cleanup": cleanup,
        "cleanup_succeeded": cleanup_ok,
    }


def cleanup_owned_process(supervisor: object, child: object) -> dict:
    """Stop the one owned child and close the supervisor exactly once."""
    try:
        cleanup = supervisor.stop(child)
    except BaseException as error:
        cleanup = {
            "process_group_released": False,
            "stop_error": str(error),
        }
    try:
        supervisor.close()
    except BaseException as error:
        if not isinstance(cleanup, dict):
            cleanup = {"process_group_released": False}
        cleanup["supervisor_close_succeeded"] = False
        cleanup["close_error"] = str(error)
    else:
        if isinstance(cleanup, dict):
            cleanup["supervisor_close_succeeded"] = True
    return cleanup


def self_test() -> None:
    error_type, reader = parser_api()
    if not isinstance(error_type, type) or not callable(reader):
        fail("direct-invocation parser API import failed")
    raw = {"buttons": 0x1234, "stick": (-12, 30), "cstick": (7, -8),
           "triggers": (140, 3)}
    encoded = pack_port(raw, 0)
    if len(encoded) != INPUT_PORT_BYTES:
        fail("raw PAD self-test size mismatch")
    changed_raw = dict(raw); changed_raw["buttons"] ^= 1
    if pack_port(changed_raw, 0) == encoded:
        fail("raw PAD sensitivity self-test failed")
    expected = {"motion_id": 0x20, "stocks": 4}
    if compare_declared(dict(expected), expected):
        fail("declared-field self-test rejected equal values")
    mutated = dict(expected); mutated["motion_id"] += 1
    if compare_declared(mutated, expected) != ["motion_id"]:
        fail("declared-field sensitivity self-test failed")
    processed = {"buttons": 0x10, "cstick_bits": ["0x00000000", "0x00000000"]}
    changed_processed = dict(processed); changed_processed["buttons"] ^= 1
    if compare_declared(changed_processed, processed) != ["buttons"]:
        fail("processed-input sensitivity self-test failed")
    print(json.dumps({"result": "passed", "controls": [
        "direct-parser-import", "raw-44-byte-pack", "raw-byte-mutation-detected",
        "declared-motion-mutation-detected", "processed-button-mutation-detected"
    ]}))


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--replay", type=Path)
    result.add_argument("--runtime", type=Path)
    result.add_argument("--assets", type=Path)
    result.add_argument("--source-probe", type=Path)
    result.add_argument("--source-cmake", type=Path)
    result.add_argument("--profile-helper", type=Path)
    result.add_argument("--probe-sha256")
    result.add_argument("--cmake-sha256")
    result.add_argument("--profile-helper-sha256")
    result.add_argument("--runtime-sha256")
    result.add_argument("--wasm-sha256")
    result.add_argument("--profile-offset", type=lambda value: int(value, 0))
    result.add_argument("--seed", type=lambda value: int(value, 0))
    result.add_argument("--initializer-profile", choices=(DEFAULT_INITIALIZER_PROFILE, NATIVE_INITIALIZER_PROFILE),
                        default=DEFAULT_INITIALIZER_PROFILE)
    result.add_argument("--stage-kind-bridge", type=Path)
    result.add_argument("--stage-kind-bridge-sha256")
    result.add_argument("--scene-last", type=int, default=SCENE_LAST,
                        help="inclusive scene endpoint: 110 (default) or measured match endpoint 1341")
    result.add_argument("--node", default="node")
    result.add_argument("--timeout", type=float, default=180.0)
    result.add_argument("--cleanup-timeout", type=float, default=45.0)
    result.add_argument("--out", type=Path)
    result.add_argument("--self-test", action="store_true")
    return result


def require_normal_args(args: argparse.Namespace) -> None:
    required = ("replay", "runtime", "assets", "source_probe", "source_cmake",
                "profile_helper", "probe_sha256", "cmake_sha256",
                "profile_helper_sha256", "runtime_sha256", "wasm_sha256",
                "profile_offset", "seed", "out")
    for name in required:
        if getattr(args, name) is None:
            fail(f"missing --{name.replace('_', '-')}")
    if args.profile_offset != PROFILE_OFFSET:
        fail("--profile-offset must be the measured 0x1234 profile")
    validate_scene_last(args.scene_last)
    if not 0 <= args.seed <= 0xFFFFFFFF:
        fail("--seed must be a uint32")
    if args.initializer_profile == NATIVE_INITIALIZER_PROFILE:
        if args.seed != NATIVE_INITIALIZER_SEED:
            fail("native initializer requires --seed 4660")
        if args.stage_kind_bridge is None or args.stage_kind_bridge_sha256 is None:
            fail("native initializer requires --stage-kind-bridge and its SHA-256")
    elif args.stage_kind_bridge is not None or args.stage_kind_bridge_sha256 is not None:
        fail("stage-kind bridge is only accepted with --initializer-profile native")
    if args.out.exists():
        fail(f"output directory already exists: {args.out}")
    if (
        not math.isfinite(args.timeout)
        or not math.isfinite(args.cleanup_timeout)
        or args.timeout <= 0
        or args.cleanup_timeout <= 0
    ):
        fail("--timeout and --cleanup-timeout must be positive")
    if args.timeout <= args.cleanup_timeout + 6.0:
        fail("--timeout must exceed cleanup reserve by at least six seconds")


def main(argv: list[str]) -> int:
    args = parser().parse_args(argv)
    output_ready = False
    supervisor = None
    child = None
    cleanup = None
    worker_started_at = None
    worker_finished_at = None
    cleanup_started_at = None
    cleanup_finished_at = None
    job_started_at = utc_now()
    job_started_monotonic = time.monotonic()
    old_signal_handlers = {}
    cleanup_attempted = False
    cleanup_in_progress = False
    try:
        if args.self_test:
            self_test()
            return 0
        require_normal_args(args)
        args.probe_sha256 = parse_hash(args.probe_sha256, "--probe-sha256")
        args.cmake_sha256 = parse_hash(args.cmake_sha256, "--cmake-sha256")
        args.profile_helper_sha256 = parse_hash(args.profile_helper_sha256, "--profile-helper-sha256")
        if args.stage_kind_bridge_sha256 is not None:
            args.stage_kind_bridge_sha256 = parse_hash(args.stage_kind_bridge_sha256, "--stage-kind-bridge-sha256")
        args.runtime_sha256 = parse_hash(args.runtime_sha256, "--runtime-sha256")
        args.wasm_sha256 = parse_hash(args.wasm_sha256, "--wasm-sha256")
        args.out.mkdir(parents=True)
        output_ready = True
        def interrupt(signum, _frame):
            if cleanup_in_progress:
                return
            raise ParentInterrupted(f"parent received {signal.Signals(signum).name}")
        for signum in (signal.SIGINT, signal.SIGTERM):
            old_signal_handlers[signum] = signal.getsignal(signum)
            signal.signal(signum, interrupt)
        source_identities = {
            "probe_sha256": checked_hash(args.source_probe, args.probe_sha256, "source probe"),
            "cmake_sha256": checked_hash(args.source_cmake, args.cmake_sha256, "source CMake"),
            "profile_helper_sha256": checked_hash(args.profile_helper, args.profile_helper_sha256, "profile helper"),
        }
        if args.initializer_profile == NATIVE_INITIALIZER_PROFILE:
            source_identities["stage_kind_bridge_sha256"] = checked_hash(
                args.stage_kind_bridge, args.stage_kind_bridge_sha256, "stage-kind bridge")
        runtime = args.runtime.expanduser()
        wasm = runtime.with_suffix(".wasm")
        runtime_identities = {
            "js_sha256": checked_hash(runtime, args.runtime_sha256, "runtime JS"),
            "wasm_sha256": checked_hash(wasm, args.wasm_sha256, "runtime Wasm"),
        }
        runtime = runtime.resolve()
        wasm = wasm.resolve()
        assets_sha256 = directory_sha256(args.assets)
        parser_path = Path(__file__).resolve().parents[1] / "tools/slippi_format.py"
        parser_sha256 = checked_hash(parser_path, sha256_file(parser_path), "Slippi parser")
        frames = normalized_frames(args.replay, args.scene_last)
        inputs_path = args.out / "normalized-inputs.json"
        inputs_path.write_text(json.dumps({
            "schema": NATIVE_SCHEMA,
            "scene_range": [SCENE_FIRST, args.scene_last],
            "frames": frames,
        }, indent=2) + "\n")
        worker_out = args.out / "worker"
        worker_out.mkdir()
        command = [
            args.node, str(PROFILE_WORKER), "--runtime", str(runtime),
            "--wasm", str(wasm), "--assets", str(args.assets.resolve()),
            "--inputs", str(inputs_path), "--probe-sha256", args.probe_sha256,
            "--profile-helper-sha256", args.profile_helper_sha256,
            "--runtime-sha256", args.runtime_sha256, "--wasm-sha256", args.wasm_sha256,
            "--profile-offset", hex(args.profile_offset), "--seed", hex(args.seed),
            "--initializer-profile", args.initializer_profile,
            "--scene-last", str(args.scene_last),
            "--out", str(worker_out),
        ]
        if args.initializer_profile == NATIVE_INITIALIZER_PROFILE:
            command.extend(["--stage-kind-bridge-sha256", args.stage_kind_bridge_sha256])
        # Reuse the repository's process supervisor: the Node worker receives
        # SIGINT first, then only its own process group is escalated. This
        # leaves cleanup time inside the bounded parent deadline.
        supervisor_path = Path(__file__).resolve().parents[1] / "reference-capture/slippi/process.py"
        spec = importlib.util.spec_from_file_location("melee_web_owned_process", supervisor_path)
        if spec is None or spec.loader is None:
            fail("cannot load owned process supervisor")
        supervisor_module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = supervisor_module
        spec.loader.exec_module(supervisor_module)
        supervisor = supervisor_module.ProcessSupervisor(
            graceful_timeout=args.cleanup_timeout, term_timeout=2.0)
        worker_log = args.out / "worker.process.log"
        deadline_monotonic = job_started_monotonic + args.timeout
        cleanup_reserve = args.cleanup_timeout + 6.0
        worker_wait_seconds = deadline_monotonic - time.monotonic() - cleanup_reserve
        if worker_wait_seconds <= 0:
            fail("whole-job timeout was exhausted before worker start")
        worker_started_at = utc_now()
        child = supervisor.start("source-profile-prefix-worker", command,
                                 log_path=worker_log, graceful_signal=signal.SIGINT)
        timed_out = False
        worker_error = None
        try:
            result_code = child.process.wait(timeout=worker_wait_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            result_code = None
        except BaseException as error:
            worker_error = error
            result_code = 130
        finally:
            cleanup_started_at = utc_now()
            cleanup_attempted = True
            cleanup_in_progress = True
            try:
                cleanup = cleanup_owned_process(supervisor, child)
            finally:
                cleanup_in_progress = False
                cleanup_finished_at = utc_now()
                worker_finished_at = utc_now()
        if timed_out:
            result_code = 124
        outcome = worker_outcome(
            result_code,
            timed_out=timed_out,
            worker_error=worker_error,
            cleanup=cleanup,
        )
        (args.out / "worker-timing.json").write_text(json.dumps({
            "worker_started_at_utc": worker_started_at,
            "worker_finished_at_utc": worker_finished_at,
            "timed_out": timed_out,
            "timeout_seconds": args.timeout,
            "cleanup_reserve_seconds": cleanup_reserve,
            "worker_wait_seconds": worker_wait_seconds,
            "cleanup_started_at_utc": cleanup_started_at,
            "cleanup_finished_at_utc": cleanup_finished_at,
            "cleanup": cleanup,
            "cleanup_succeeded": outcome["cleanup_succeeded"],
            "worker_returncode": outcome["worker_returncode"],
            "worker_error": outcome["worker_error"],
            "worker_log_sha256": sha256_file(worker_log),
        }, indent=2) + "\n")
        report = {
            "schema": "melee-web-source-slippi-profile-check-v1",
            "result": outcome["result"],
            "failure": outcome["failure"],
            "claim_boundary": "Source-only bounded prefix. Raw 44-byte PAD records are the sole step inputs; native state, callback telemetry, and declared fields are observations.",
            "source": source_identities,
            "runtime": runtime_identities,
            "assets": {"path": str(args.assets.resolve()), "sha256": assets_sha256},
            "parser_sha256": parser_sha256,
            "replay": {"path": str(args.replay.resolve()), "scene_range": [SCENE_FIRST, args.scene_last], "frame_count": len(frames)},
            "profile": {"offset": args.profile_offset, "seed": args.seed,
                        "initializer_profile": args.initializer_profile},
            "worker_returncode": outcome["worker_returncode"],
            "worker_error": outcome["worker_error"],
            "worker_stderr": outcome["worker_stderr"],
            "worker_command": command,
            "job_started_at_utc": job_started_at,
            "started_at_utc": worker_started_at,
            "finished_at_utc": worker_finished_at,
            "cleanup_started_at_utc": cleanup_started_at,
            "cleanup_finished_at_utc": cleanup_finished_at,
            "timed_out": timed_out,
            "timeout_seconds": args.timeout,
            "worker_wait_seconds": worker_wait_seconds,
            "cleanup_timeout_seconds": args.cleanup_timeout,
            "cleanup_reserve_seconds": cleanup_reserve,
            "cleanup": cleanup,
            "cleanup_succeeded": outcome["cleanup_succeeded"],
            "worker_log": str(worker_log),
            "worker_log_sha256": sha256_file(worker_log),
            "input_sha256": sha256_file(inputs_path),
            "replay_sha256": sha256_file(args.replay),
            "checker_sha256": sha256_file(Path(__file__).resolve()),
            "worker_sha256": sha256_file(PROFILE_WORKER.resolve()),
        }
        (args.out / ("report.json" if outcome["result"] == "passed" else "failure.json")).write_text(
            json.dumps(report, indent=2) + "\n")
        for signum, handler in old_signal_handlers.items():
            signal.signal(signum, handler)
        if outcome["result"] != "passed":
            sys.stderr.write(outcome["worker_stderr"] + "\n")
        else:
            print(json.dumps({"result": "passed", "rows": len(frames), "output": str(args.out)}))
        if outcome["result"] == "passed":
            return 0
        if result_code not in (None, 0):
            return result_code
        return 2
    except BaseException as exc:
        if child is not None and not cleanup_attempted and supervisor is not None:
            cleanup_attempted = True
            cleanup_in_progress = True
            try:
                cleanup = cleanup_owned_process(supervisor, child)
            finally:
                cleanup_in_progress = False
        if output_ready:
            failure = {
                "schema": "melee-web-source-slippi-profile-check-v1",
                "result": "failed", "failure": str(exc),
            }
            if cleanup is not None:
                failure["cleanup"] = cleanup
            (args.out / "failure.json").write_text(json.dumps(failure, indent=2) + "\n")
        for signum, handler in old_signal_handlers.items():
            signal.signal(signum, handler)
        print(json.dumps({"result": "failed", "error": str(exc)}), file=sys.stderr)
        return 130 if isinstance(exc, (KeyboardInterrupt, ParentInterrupted)) else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
