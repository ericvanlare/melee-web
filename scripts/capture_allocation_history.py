#!/usr/bin/env python3
"""Capture original allocation history from a fresh DOL boot, without a state load.

Reuses the retail runner's owned Dolphin/config/controller transport and the
original menu/input collectors. This diagnostic stream cannot be admitted as a
replacement gold capture. Every attempted process retains its logs and hashes.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))
import capture_retail_replay as retail
from retail_allocation_profile import build_profile
from original_boot_context import derive_boot_context
from retail_input_plan import load_plan
from retail_replay_validation import load_capture
from retail_input_plan import verify_capture
from cpu_reference_scenarios import scenario_target, validate_scenario, validate_input_plan


def write_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def tree_hashes(root):
    return {str(path.relative_to(root)): retail._sha256(path)
            for path in sorted(root.rglob("*")) if path.is_file()}


def gdb_script(paths, boot_only=False):
    q = retail._gdb_quote
    lines = ["set architecture powerpc:common", "set endian big", "set pagination off",
             "set confirm off", "set breakpoint pending on",
             f"target remote {paths['socket']}",
             "hbreak *0x80390eb4", "commands", "silent", "end",
             f"source {paths['allocation_collector']}", "continue",
             "python",
             "if _allocation_failed: raise RuntimeError(_allocation_failed)",
             "if _allocation_reg('pc') != 0x80390eb4: raise RuntimeError('boot did not reach first scheduler return')",
             "end"]
    if not boot_only:
        lines += [f"source {paths['menu_driver']}",
                  f"source {paths['helper']}",
                  f"source {paths['collector']}",
                  "enable 1",
                  f"retail-replay-arm {q(paths['trace'])} {paths['frames']}",
                  "retail-step 8 1 PRESS A", "retail-step 8 1 RELEASE A",
                  "disable 1", "continue",
                  "python",
                  "if _allocation_failed: raise RuntimeError(_allocation_failed)",
                  "end"]
    lines += ["python", "allocation_finish('captured', '" +
              ("first_scheduler_return" if boot_only else "requested_match_capture_boundary") + "')",
              "end", "quit", ""]
    return "\n".join(lines)


def capture(args):
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    metadata = {"schema": "melee-web-allocation-capture-run", "version": 1,
                "run_id": uuid.uuid4().hex, "status": "preparing",
                "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "start": "original_dol_entry", "savestate_loaded": False,
                "boot_only": args.boot_only, "candidate_admission": "forbidden",
                "scope": "original allocation diagnostic; no gameplay or performance admission"}
    paths = None
    processes = []
    streams = []
    inputs = {}
    try:
        plan, plan_hash = load_plan(args.input_plan)
        scenario = json.loads(args.scenario.read_text())
        validate_scenario(scenario)
        validate_input_plan(plan)
        if plan.get("version") != 3:
            raise ValueError("allocation experiment requires an existing v3 CPU input plan")
        for path in (args.disc, args.dol, args.provenance, args.input_plan, args.scenario,
                     ROOT / ".deps/melee/config/GALE01/symbols.txt",
                     Path(__file__), ROOT / "tools/original_boot_context.py",
                     ROOT / "tools/reference_allocation_capture.py",
                     ROOT / "tools/retail_allocation_profile.py",
                     ROOT / "tools/retail_allocation_menu.py"):
            inputs[str(path.resolve())] = retail._sha256(path)
        for parent in (args.template_user, args.checkpoint_gc):
            for path in parent.rglob("*"):
                if path.is_file(): inputs[str(path.resolve())] = retail._sha256(path)
        # Use the repository collector, never an inherited custom capture hook.
        previous_collector = os.environ.pop("MELEE_REPLAY_COLLECTOR", None)
        try:
            paths = retail.prepare_run(args.template_user, args.checkpoint_gc, args.provenance,
                                       args.dol, args.dolphin, output / "trace.jsonl", cpu=args.cpu)
        finally:
            if previous_collector is not None:
                os.environ["MELEE_REPLAY_COLLECTOR"] = previous_collector
        evidence = paths["evidence"]
        evidence.mkdir()
        paths["trace"] = output / "trace.jsonl"
        paths["frames"] = len(plan["frames"])
        inputs[str(paths["source_dolphin"])] = paths["identity"]["dolphin_binary_sha256"]
        paths["allocation_collector"] = paths["collector"].with_name("reference_allocation_capture.py")
        shutil.copy2(ROOT / "tools/reference_allocation_capture.py", paths["allocation_collector"])
        profile_path = evidence / "allocation-profile.json"
        write_json(profile_path, build_profile(args.dol, ROOT / ".deps/melee/config/GALE01/symbols.txt"))
        boot_context = derive_boot_context(args.dol, args.disc,
            ROOT / ".deps/melee/config/GALE01/symbols.txt", ROOT / ".deps/melee")
        write_json(evidence / "independent-boot-context.json", boot_context)
        shutil.copy2(args.input_plan, evidence / "input-plan.json")
        if retail._sha256(evidence / "input-plan.json") != plan_hash:
            raise ValueError("owned input-plan copy differs from its frozen source")
        shutil.copy2(args.scenario, evidence / "scenario.json")
        # Expected setup is the already frozen human/controller scenario declaration.
        write_json(evidence / "menu-target.json", scenario_target(scenario))
        paths["menu_driver"] = evidence / "menu-driver.py"
        if not args.boot_only:
            from retail_allocation_menu import render_driver
            paths["menu_driver"].write_text(render_driver())
        paths["helper"] = evidence / "gdb-control.py"
        retail.write_control_helper(paths["helper"], paths["user"] / "Pipes", paths["trace"])
        # Any allocation failure must stop every controller-driven continue loop.
        helper = paths["helper"].read_text().replace(
            'gdb.execute("continue", to_string=True)',
            'gdb.execute("continue", to_string=True)\n'
            '                if globals().get("_allocation_failed"): raise RuntimeError(_allocation_failed)')
        paths["helper"].write_text(helper)
        original_provenance = dict(paths["provenance"])
        original_provenance.pop("setup_snapshot_sha256", None)
        original_provenance["application_context"] = "fresh_original_dol_boot_no_savestate"
        original_provenance["allocation_profile_sha256"] = retail._sha256(profile_path)
        original_provenance["independent_boot_context_sha256"] = retail._sha256(evidence / "independent-boot-context.json")
        write_json(evidence / "provenance.json", original_provenance)
        external = tree_hashes(paths["user"] / "GC")
        if external != paths["provenance"].get("external_save_hashes"):
            raise ValueError("external card context differs from pinned provenance")
        retail.require_raw_pipe_config(paths["pad_config"], range(1, plan["active_player_count"] + 1))
        identity = {**paths["identity"], "disc_dol_sha1": retail.verify_disc_dol(args.disc, args.dol),
                    "disc_image_sha256": inputs[str(args.disc.resolve())],
                    "dolphin_ini_canonical_sha256": retail._canonical_dolphin_ini_sha256(paths["config"], paths["socket"]),
                    "gcpad_ini_sha256": retail._sha256(paths["pad_config"]),
                    "external_save_hashes": external}
        command = retail.dolphin_command(paths["source_dolphin"], paths["user"], paths["snapshot"], args.disc, cpu=args.cpu)
        at = command.index("-s")
        del command[at:at + 2]
        commands = evidence / "gdb-commands.txt"
        commands.write_text(gdb_script(paths, args.boot_only))
        # These owned files are execution inputs even though they live beside
        # the diagnostic outputs. Freeze them before any original process runs.
        for path in evidence.iterdir():
            if path.is_file():
                inputs[str(path.resolve())] = retail._sha256(path)
        debugger_path = Path(shutil.which("gdb") or "").resolve()
        if not debugger_path.is_file():
            raise ValueError("GDB executable is unavailable")
        inputs[str(debugger_path)] = retail._sha256(debugger_path)
        environment = {k: v for k, v in os.environ.items()
                       if not k.startswith(("MELEE_REPLAY_", "MELEE_CPU_", "MELEE_ALLOCATION_"))}
        environment.update({"MELEE_ALLOCATION_PROFILE": str(profile_path),
            "MELEE_ALLOCATION_OUTPUT": str(output / "allocations.jsonl"),
            "MELEE_REPLAY_REFERENCE_WORK": str(evidence),
            "MELEE_REPLAY_COLLECTOR": str(paths["collector"]),
            "MELEE_REPLAY_INPUT_PLAN": str(evidence / "input-plan.json"),
            "MELEE_REPLAY_DRAW_AUDIT": "1",
            "MELEE_CHECKPOINT_TARGET_JSON": str(evidence / "menu-target.json")})
        metadata.update({"status": "running", "identity": identity, "owned_run": str(paths["run_root"]),
                         "input_plan_sha256": plan_hash, "frames_requested": len(plan["frames"]),
                         "launch": command, "inputs": inputs,
                         "collector_files": tree_hashes(paths["collector"].parent)})
        write_json(output / "launch.json", metadata)
        dolphin_log = (output / "dolphin.log").open("x"); streams.append(dolphin_log)
        dolphin = subprocess.Popen(command, stdout=dolphin_log, stderr=subprocess.STDOUT,
                                   env=environment, start_new_session=True)
        processes.append((dolphin, "Dolphin"))
        metadata["dolphin_pid"] = dolphin.pid
        retail.wait_for_socket(paths["socket"], dolphin, 30)
        gdb_log = (output / "gdb.log").open("x"); streams.append(gdb_log)
        debugger = subprocess.Popen([str(debugger_path), "--quiet", "--nx", "--batch", "-x", str(commands)],
                                    stdout=gdb_log, stderr=subprocess.STDOUT, env=environment,
                                    start_new_session=True)
        processes.append((debugger, "GDB"))
        metadata["gdb_pid"] = debugger.pid
        write_json(output / "processes.json", {"run_id": metadata["run_id"],
                   "started_at": metadata["started_at"], "dolphin_pid": dolphin.pid,
                   "gdb_pid": debugger.pid, "owned_run": str(paths["run_root"])})
        debugger.wait(timeout=args.timeout)
        if debugger.returncode:
            raise RuntimeError("GDB capture failed; preserved gdb.log")
        allocation_path = output / "allocations.jsonl"
        with allocation_path.open() as stream:
            last = None
            first = json.loads(next(stream))
            for key in ("arena_lo", "arena_hi", "bi2", "memory_size"):
                observed = first["observed_boot_context"][key]
                if boot_context["boot"].get(key) != observed:
                    raise RuntimeError("observed boot context differs from independently derived " + key)
            for line in stream: last = json.loads(line)
        if last is None or last.get("record") != "end" or last.get("status") != "captured":
            raise RuntimeError("allocation capture lacks a successful terminal record")
        if not args.boot_only:
            trace = load_capture(paths["trace"], cpu=args.cpu)
            verify_capture(plan, trace)
            if len(trace.frames) != len(plan["frames"]):
                raise RuntimeError("original trace does not contain the complete frozen input timeline")
            metadata["frames_captured"] = len(trace.frames)
        metadata["status"] = "captured_diagnostic"
    except Exception as error:
        metadata.update({"status": "failed", "error": str(error)})
    finally:
        for process, label in reversed(processes):
            retail._terminate(process, label)
        for stream in streams: stream.close()
        if paths: paths["socket"].unlink(missing_ok=True)
        metadata["wall_seconds"] = time.monotonic() - started
        metadata["inputs"] = inputs
        metadata["inputs_unchanged"] = all(Path(path).is_file() and retail._sha256(Path(path)) == digest
                                           for path, digest in inputs.items())
        if not metadata["inputs_unchanged"]:
            metadata.update({"status": "failed", "error": "frozen capture input changed"})
        metadata["outputs"] = {path.name: retail._sha256(path) for path in output.iterdir() if path.is_file()}
        if paths:
            metadata["owned_evidence_outputs"] = tree_hashes(paths["evidence"])
            metadata["owned_final_gc_hashes"] = tree_hashes(paths["user"] / "GC")
            metadata["collector_files_after"] = tree_hashes(paths["collector"].parent)
            if metadata.get("collector_files") is not None and metadata["collector_files_after"] != metadata["collector_files"]:
                metadata.update({"status": "failed", "error": "owned collector changed during capture"})
        write_json(output / "result.json", metadata)
    print(json.dumps({"status": metadata["status"], "output": str(output), "error": metadata.get("error")}))
    return 0 if metadata["status"] == "captured_diagnostic" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "dol", "template-user", "checkpoint-gc", "provenance",
                 "input-plan", "scenario", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"), default="Interpreter64")
    parser.add_argument("--timeout", type=float, default=7200)
    parser.add_argument("--boot-only", action="store_true", help="Stop at first scheduler return; diagnostic smoke only")
    return capture(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
