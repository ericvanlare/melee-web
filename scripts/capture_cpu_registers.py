#!/usr/bin/env python3
"""Capture bounded retail CPU registers around declared source instructions.

This is a read-only diagnostic wrapper around the normal retail replay runner.
It copies the complete input plan and a caller-selected collector into an
owned run directory, executes only the requested prefix, and writes a
diagnostic-only JSONL sidecar.  The resulting sidecar is never accepted by the
replay-candidate validators.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))
sys.path.insert(0, str(REPO_ROOT / "tools"))

import capture_retail_replay as capture
from retail_input_plan import load_plan
from retail_cpu_registers import (CpuRegisterError, SCHEMA, VERSION,
                                  collector_sources, instrument_collector,
                                  diagnostic_errors, load_diagnostic, load_probe_document,
                                  sha256_bytes,
                                  validate_stack_limits, validate_window)


DEFAULT_TIMEOUT = 120.0
DEFAULT_STACK_BYTES = 64
DEFAULT_BACKCHAIN_DEPTH = 8
MIN_SETUP_TICKS = 16


class CpuRegisterCaptureError(RuntimeError):
    """A diagnostic prerequisite, child process, or output validation failed."""


def _sha256(path: Path) -> str:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        return digest.hexdigest()
    except OSError as error:
        raise CpuRegisterCaptureError("cannot hash %s: %s" % (path, error)) from error


def _write_json(path: Path, value: dict) -> None:
    try:
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    except OSError as error:
        raise CpuRegisterCaptureError("cannot write %s: %s" % (path, error)) from error


def _prefix_plan(plan: dict, frame_count: int) -> tuple[dict, bytes]:
    value = dict(plan)
    value["frames"] = list(plan["frames"][:frame_count])
    raw = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
    return value, raw


def _copy_prepared_state(paths: dict, snapshot: Path, provenance: Path) -> dict:
    try:
        shutil.copy2(snapshot, paths["snapshot"])
        paths["evidence"].mkdir(parents=True, exist_ok=True)
        (paths["evidence"] / "provenance.json").write_bytes(paths["provenance_bytes"])
    except OSError as error:
        raise CpuRegisterCaptureError("cannot copy owned snapshot/provenance: %s" % error) from error
    if _sha256(paths["snapshot"]) != paths["provenance"].get("setup_snapshot_sha256"):
        raise CpuRegisterCaptureError("owned snapshot does not match pinned provenance")
    gc = paths["user"] / "GC"
    actual_gc = {str(path.relative_to(gc)): _sha256(path)
                 for path in gc.rglob("*") if path.is_file()}
    if actual_gc != paths["provenance"].get("external_save_hashes"):
        raise CpuRegisterCaptureError("owned external GC state does not match pinned provenance")
    return actual_gc


def _make_collector(collector: Path, generated: Path, probes, start: int, end: int,
                    stack_bytes: int, backchain_depth: int, helper_bytes: bytes,
                    extra_memory=()) -> dict:
    sources = collector_sources(collector)
    for source in sources:
        if not source.is_file():
            raise CpuRegisterCaptureError("collector helper is missing: %s" % source)
    if not generated.is_dir() or any(generated.iterdir()):
        raise CpuRegisterCaptureError("generated collector directory is not empty")
    instrumented = instrument_collector(
        collector.read_bytes(), probes=probes, start_tick=start, end_tick=end,
        stack_bytes=stack_bytes, backchain_depth=backchain_depth,
        helper_sha256=sha256_bytes(helper_bytes), extra_memory=extra_memory)
    (generated / "reference_replay_capture.py").write_bytes(instrumented)
    for source in sources[1:]:
        shutil.copy2(source, generated / source.name)
    return {
        "input_collector": str(collector),
        "input_collector_sha256": _sha256(collector),
        "instrumented_collector_sha256": sha256_bytes(instrumented),
        "helpers": [{"name": source.name, "sha256": _sha256(source),
                     "bytes": source.stat().st_size} for source in sources[1:]],
        "embedded_helper": {"name": "retail_cpu_registers.py",
                            "sha256": sha256_bytes(helper_bytes),
                            "bytes": len(helper_bytes)},
        "generated_collector": str(generated / "reference_replay_capture.py"),
    }


def capture_registers(*, dolphin: str | Path, disc: str | Path, dol: str | Path,
                      template_user: str | Path, snapshot: str | Path,
                      checkpoint_gc: str | Path, provenance: str | Path,
                      input_plan: str | Path, probes: str | Path,
                      output: str | Path, collector: str | Path | None = None,
                      start_tick: int, end_tick: int,
                      timeout: float = DEFAULT_TIMEOUT,
                      cpu: str = "Interpreter64",
                      stack_bytes: int = DEFAULT_STACK_BYTES,
                      backchain_depth: int = DEFAULT_BACKCHAIN_DEPTH) -> dict:
    """Run one diagnostic prefix and return diagnostic-only metadata."""
    if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or not math.isfinite(timeout) or timeout <= 0:
        raise CpuRegisterCaptureError("timeout must be positive and finite")
    try:
        full_plan, full_plan_hash = load_plan(Path(input_plan))
    except (OSError, ValueError) as error:
        raise CpuRegisterCaptureError("invalid full input plan: %s" % error) from error
    start, end = validate_window(start_tick, end_tick, len(full_plan["frames"]))
    stack_bytes, backchain_depth = validate_stack_limits(stack_bytes, backchain_depth)
    if end + 1 < MIN_SETUP_TICKS:
        raise CpuRegisterCaptureError(
            "end_tick must include the ordinary 16-tick post-arm setup window")
    probe_values, extra_memory, probe_hash, probe_bytes = load_probe_document(Path(probes))
    output_path = Path(output).expanduser().resolve()
    if output_path.exists():
        raise CpuRegisterCaptureError("diagnostic output already exists: %s" % output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    base_collector = Path(collector).expanduser().resolve() if collector else (
        REPO_ROOT / "tools/reference_replay_capture.py")
    helper_source = REPO_ROOT / "tools/retail_cpu_registers.py"
    helper_bytes = helper_source.read_bytes()
    generated_root = Path(tempfile.mkdtemp(prefix=".cpu-register-collector-",
                                            dir=str(output_path.parent)))
    collector_info = _make_collector(
        base_collector, generated_root, probe_values, start, end,
        stack_bytes, backchain_depth, helper_bytes, extra_memory)
    prefix_plan, prefix_bytes = _prefix_plan(full_plan, end + 1)
    prefix_hash = hashlib.sha256(prefix_bytes).hexdigest()
    started = time.monotonic()
    old_collector_env = os.environ.get("MELEE_REPLAY_COLLECTOR")
    os.environ["MELEE_REPLAY_COLLECTOR"] = collector_info["generated_collector"]
    paths = None
    metadata = None
    dolphin_process = None
    gdb_process = None
    dolphin_log = None
    gdb_log = None
    try:
        paths = capture.prepare_run(
            template_user, checkpoint_gc, provenance, dol, dolphin, output_path,
            cpu=cpu)
        disc_path = capture._regular_file(Path(disc), "disc image")
        paths["identity"]["disc_dol_sha1"] = capture.verify_disc_dol(
            disc_path, paths["source_dol"])
        if full_plan.get("version") == 3:
            paths["identity"]["disc_image_sha256"] = capture._sha256(disc_path)
            paths["identity"]["disc_image_bytes"] = disc_path.stat().st_size
        actual_gc = _copy_prepared_state(paths, Path(snapshot), Path(provenance))
        full_plan_copy = paths["evidence"] / "input-plan-full.json"
        prefix_plan_copy = paths["evidence"] / "input-plan-prefix.json"
        full_plan_copy.write_bytes(Path(input_plan).read_bytes())
        prefix_plan_copy.write_bytes(prefix_bytes)
        if _sha256(full_plan_copy) != full_plan_hash:
            raise CpuRegisterCaptureError("full input plan changed during preparation")
        if _sha256(prefix_plan_copy) != prefix_hash:
            raise CpuRegisterCaptureError("diagnostic input prefix changed during preparation")
        active_ports = (range(1, full_plan["active_player_count"] + 1)
                        if full_plan.get("version") == 3 else (1, 2))
        capture.require_raw_pipe_config(paths["pad_config"], active_ports)
        capture_path = paths["evidence"] / "capture-prefix.jsonl"
        helper_copy = paths["evidence"] / "retail_cpu_registers.py"
        helper_copy.write_bytes(helper_bytes)
        probe_copy = paths["evidence"] / "probe-definitions.json"
        probe_copy.write_bytes(probe_bytes)
        helper_hash = _sha256(helper_copy)
        if helper_hash != collector_info["embedded_helper"]["sha256"]:
            raise CpuRegisterCaptureError("embedded helper changed during preparation")
        helper = paths["run_root"] / "gdb-control.py"
        capture.write_control_helper(helper, paths["user"] / "Pipes", capture_path)
        gdb_commands = paths["run_root"] / "gdb-commands.txt"
        capture.write_gdb_script(gdb_commands, paths["socket"], helper,
                                 paths["collector"], capture_path, end + 1)
        command = capture.dolphin_command(paths["source_dolphin"], paths["user"],
                                          paths["snapshot"], disc_path, cpu=cpu)
        gdb_command = ["gdb", "--quiet", "--nx", "--batch", "-x", str(gdb_commands)]
        metadata = {
            "schema": SCHEMA, "version": VERSION, "status": "prepared",
            "evidence_status": "diagnostic_only", "candidate_admission": "forbidden",
            "scope": "read-only source register diagnostic; no replay equivalence claim",
            "run_id": uuid.uuid4().hex, "run_root": str(paths["run_root"]),
            "output": str(output_path), "frames_requested": end + 1,
            "identity": paths["identity"],
            "diagnostic": {
                "window": {"start_tick": start, "end_tick": end},
                "stack_bytes": stack_bytes, "backchain_depth": backchain_depth,
                "probes": [probe.record() for probe in probe_values],
                "extra_memory": [region.record() for region in extra_memory],
                "probe_definitions_sha256": probe_hash,
                "full_input_plan_sha256": full_plan_hash,
                "executed_input_prefix_sha256": prefix_hash,
                "full_input_plan_path": str(full_plan_copy),
                "executed_input_prefix_path": str(prefix_plan_copy),
                "sidecar": str(output_path),
                "collector": collector_info,
            },
            "source": {"disc": str(disc_path), "dol": str(paths["source_dol"]),
                       "snapshot": str(Path(snapshot).expanduser().resolve()),
                       "provenance": str(Path(provenance).expanduser().resolve())},
            "owned": {"user": str(paths["user"]), "evidence": str(paths["evidence"]),
                      "collector": str(paths["collector"]),
                      "collector_sha256": paths["collector_sha256"],
                      "capture_prefix": str(capture_path),
                      "snapshot_sha256": _sha256(paths["snapshot"]),
                      "provenance_sha256": _sha256(paths["evidence"] / "provenance.json"),
                      "external_save_hashes": actual_gc},
            "launch": {"dolphin": command, "gdb": gdb_command},
            "logs": {"dolphin": str(paths["run_root"] / "dolphin.log"),
                     "gdb": str(paths["run_root"] / "gdb.log")},
        }
        metadata_path = paths["run_root"] / "run-metadata.json"
        _write_json(metadata_path, metadata)
        environment = os.environ.copy()
        environment["MELEE_REPLAY_REFERENCE_WORK"] = str(paths["evidence"])
        environment["MELEE_REPLAY_COLLECTOR"] = str(paths["collector"])
        environment["MELEE_REPLAY_DRAW_AUDIT"] = "0"
        environment["MELEE_REPLAY_UNTIL_MATCH_END"] = "0"
        environment.pop("MELEE_CPU_OBSERVATION", None)
        environment["MELEE_REPLAY_INPUT_PLAN"] = str(prefix_plan_copy)
        environment["MELEE_CPU_REGISTER_OUTPUT"] = str(output_path)
        environment["MELEE_CPU_REGISTER_PLAN_SHA256"] = full_plan_hash
        environment["MELEE_REPLAY_INPUT_BOOTSTRAP_MODE"] = "default"
        dolphin_log = (paths["run_root"] / "dolphin.log").open("w", encoding="utf-8")
        dolphin_process = subprocess.Popen(command, stdout=dolphin_log,
                                           stderr=subprocess.STDOUT, env=environment,
                                           start_new_session=True)
        capture.wait_for_socket(paths["socket"], dolphin_process, min(float(timeout), 30.0))
        remaining = float(timeout) - (time.monotonic() - started)
        if remaining <= 0:
            raise CpuRegisterCaptureError("diagnostic timed out before GDB could start")
        gdb_log = (paths["run_root"] / "gdb.log").open("w", encoding="utf-8")
        gdb_process = subprocess.Popen(gdb_command, stdout=gdb_log,
                                        stderr=subprocess.STDOUT, env=environment,
                                        start_new_session=True)
        try:
            gdb_process.wait(timeout=remaining)
        except subprocess.TimeoutExpired as error:
            raise CpuRegisterCaptureError("timed out waiting for GDB diagnostic") from error
        if gdb_process.returncode != 0:
            raise CpuRegisterCaptureError(
                "GDB diagnostic failed with status %d; see %s" %
                (gdb_process.returncode, paths["run_root"] / "gdb.log"))
        rows, sidecar_hash = load_diagnostic(
            output_path, probes=probe_values, start_tick=start, end_tick=end,
            extra_memory=extra_memory, input_plan_sha256=full_plan_hash)
        errors = diagnostic_errors(rows)
        if errors:
            raise CpuRegisterCaptureError(
                "diagnostic retained %d probe/state errors; see sidecar" % len(errors))
        metadata.update({
            "status": "diagnostic_only", "evidence_status": "diagnostic_only",
            "output_sha256": sidecar_hash, "records": len(rows),
            "capture_wall_seconds": time.monotonic() - started,
        })
        _write_json(metadata_path, metadata)
        return metadata
    except (KeyboardInterrupt, CpuRegisterCaptureError, CpuRegisterError,
            capture.CaptureRunnerError, OSError, subprocess.SubprocessError) as error:
        if metadata is not None:
            metadata.update({"status": "diagnostic_failed",
                             "error": str(error) or type(error).__name__})
            try:
                _write_json(paths["run_root"] / "run-metadata.json", metadata)
            except CpuRegisterCaptureError:
                pass
        raise CpuRegisterCaptureError(str(error) or type(error).__name__) from error
    finally:
        if gdb_log is not None:
            gdb_log.close()
        if dolphin_log is not None:
            dolphin_log.close()
        if paths is not None:
            try:
                capture._terminate(gdb_process, "GDB")
            finally:
                capture._terminate(dolphin_process, "Dolphin")
                paths["socket"].unlink(missing_ok=True)
        if old_collector_env is None:
            os.environ.pop("MELEE_REPLAY_COLLECTOR", None)
        else:
            os.environ["MELEE_REPLAY_COLLECTOR"] = old_collector_env


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "dol", "template-user", "snapshot",
                 "checkpoint-gc", "provenance", "input-plan", "probes", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--collector", type=Path,
                        help="Frozen inline collector matching the prepared donor")
    parser.add_argument("--start-tick", required=True, type=int)
    parser.add_argument("--end-tick", required=True, type=int)
    parser.add_argument("--stack-bytes", type=int, default=DEFAULT_STACK_BYTES)
    parser.add_argument("--backchain-depth", type=int, default=DEFAULT_BACKCHAIN_DEPTH)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--cpu", choices=capture.CPU_PROFILES, default="Interpreter64")
    args = parser.parse_args(argv)
    try:
        metadata = capture_registers(
            dolphin=args.dolphin, disc=args.disc, dol=args.dol,
            template_user=args.template_user, snapshot=args.snapshot,
            checkpoint_gc=args.checkpoint_gc, provenance=args.provenance,
            input_plan=args.input_plan, probes=args.probes, output=args.output,
            collector=args.collector, start_tick=args.start_tick,
            end_tick=args.end_tick, timeout=args.timeout, cpu=args.cpu,
            stack_bytes=args.stack_bytes, backchain_depth=args.backchain_depth)
    except (CpuRegisterCaptureError, CpuRegisterError) as error:
        parser.exit(2, "CPU register diagnostic failed: %s\n" % error)
    print(json.dumps({"status": metadata["status"], "output": metadata["output"],
                      "output_sha256": metadata.get("output_sha256"),
                      "records": metadata.get("records"),
                      "run_root": metadata["run_root"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
