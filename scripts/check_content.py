#!/usr/bin/env python3
"""Run declared asset checks and freshly built source lifecycle traces.

This is an early porting check, not browser, retail, timing or content admission.
The local manifest names source identities and existing trace contracts; it does
not enable content or infer a new fighter/stage's dependencies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from check_assets import parse_checks, unique_json_object, validate_check_paths
from check_gameplay import node_runtime

ROOT = Path(__file__).resolve().parents[1]
MATCH = "gameplay_content_match_trace"
BATTLEFIELD = "gameplay_stage_battlefield_trace"
SCOPE = ("Declared asset parser checks and source lifecycle traces only. "
         "No browser, retail comparison, pixels, PCM fidelity, live timing or admission claim.")


def file_identity(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def local_path(value, base: Path) -> Path:
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError("Expected a nonempty local path")
    return (base / value).resolve()


def load_manifest(path: Path) -> tuple[list, list[dict]]:
    value = json.loads(path.read_text(), object_pairs_hook=unique_json_object)
    if not isinstance(value, dict) or set(value) != {"checks", "lifecycles"}:
        raise ValueError("Content manifest must contain only checks and lifecycles")
    checks = parse_checks(value["checks"], path.parent)
    rows = value["lifecycles"]
    if not isinstance(rows, list) or not rows:
        raise ValueError("lifecycles must be a nonempty array")
    traces = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"lifecycles[{index}] must be an object")
        target = row.get("target")
        fields = {"target", "assets"}
        if target == MATCH:
            fields |= {"menus", "stkind", "p1_ckind", "p2_ckind"}
        elif target != BATTLEFIELD:
            raise ValueError(f"Unsupported lifecycle target: {target!r}; add a reviewed trace contract first")
        if set(row) != fields:
            raise ValueError(f"{target} requires exactly {sorted(fields)}")
        trace = dict(row)
        for key in ("assets", "menus"):
            if key in trace:
                trace[key] = local_path(trace[key], path.parent)
                if not trace[key].is_dir():
                    raise ValueError(f"Missing {key} directory: {trace[key]}")
        if target == MATCH:
            for key in ("stkind", "p1_ckind", "p2_ckind"):
                if type(trace[key]) is not int or not 0 <= trace[key] <= 0x7fffffff:
                    raise ValueError(f"{key} must be a nonnegative source integer identity")
        traces.append(trace)
    return checks, traces


def input_inventory(checks: list, traces: list[dict]) -> list[dict]:
    validate_check_paths(checks)
    paths = {Path(check["path"]) for check in checks}
    for trace in traces:
        for key in ("assets", "menus"):
            if key in trace:
                files = [path for path in trace[key].iterdir() if path.is_file()]
                if not files:
                    raise ValueError(f"Empty {key} directory: {trace[key]}")
                paths.update(files)
    return [file_identity(path) for path in sorted(paths)]


def trace_runs(traces: list[dict], build_dir: Path, node: Path) -> list[dict]:
    runs = []
    for index, trace in enumerate(traces):
        target = trace["target"]
        command = [str(node), str(build_dir / (target + ".js"))]
        if target == MATCH:
            p1, p2 = trace["p1_ckind"], trace["p2_ckind"]
            pairs = [(p1, p2)] if p1 == p2 else [(p1, p2), (p2, p1)]
            for player, opponent in pairs:
                runs.append({
                    "id": f"lifecycle-{index:02d}-p{player}-p{opponent}",
                    "command": command + [str(trace["menus"]), str(trace["assets"]),
                                          str(trace["stkind"]), str(player), str(opponent)],
                    "expected": "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed",
                    "scope": {"target": target, "stkind": trace["stkind"],
                              "p1_ckind": player, "p2_ckind": opponent,
                              "coverage": "All selected-P1 costumes; only the action branches in tests/gameplay_content_match_trace.cpp"},
                })
        else:
            runs.append({
                "id": f"lifecycle-{index:02d}-battlefield",
                "command": command + [str(trace["assets"])],
                "expected": "Original Battlefield",
                "scope": {"target": target, "coverage": "Two Battlefield owner lifetimes and the scheduler/geometry assertions in tests/gameplay_stage_battlefield_trace.cpp"},
            })
    return runs


class CheckFailed(Exception):
    pass


def run_step(report: dict, out: Path, step: dict, *, timeout: int, root: Path) -> None:
    report["steps"].append(step)
    step["log"] = str(out / (step["id"] + ".log"))
    step["status"] = "running"
    print(f"{step['id']}: running; log: {step['log']}", flush=True)
    started = time.monotonic()
    try:
        with open(step["log"], "xb") as log:
            result = subprocess.run(step["command"], cwd=root, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout,
                                    env=os.environ | step.get("environment", {}))
        step["returncode"] = result.returncode
        if result.returncode:
            raise CheckFailed(f"Command exited {result.returncode}")
        if step.get("expected") and step["expected"] not in Path(step["log"]).read_text(errors="replace"):
            raise CheckFailed("Trace exited without its completion marker")
        step["status"] = "passed"
    except (OSError, subprocess.TimeoutExpired, CheckFailed) as error:
        step["status"] = "failed"
        step["error"] = str(error)
        if Path(step["log"]).is_file():
            with open(step["log"], "rb") as log:
                log.seek(max(0, Path(step["log"]).stat().st_size - 4000))
                step["diagnostic_tail"] = log.read().decode(errors="replace")
            if step["id"] == "assets":
                with open(step["log"], errors="replace") as log:
                    for line in log:
                        try:
                            record = json.loads(line)
                        except ValueError:
                            continue
                        if isinstance(record, dict) and record.get("status") == "rejected":
                            step["first_rejection"] = record
                            break
        raise CheckFailed(str(error)) from error
    finally:
        step["seconds"] = round(time.monotonic() - started, 3)
        print(f"{step['id']}: {step['status']} ({step['seconds']}s)", flush=True)


def validate(manifest: Path, out: Path, *, configuration: str, jobs: int,
             root: Path = ROOT) -> int:
    # An existing directory may hold another attempt; never merge or overwrite it.
    out.mkdir(parents=True, exist_ok=False)
    report = {"schema": "melee-web-content-check-v1", "scope": SCOPE,
              "result": "failed", "steps": [], "unverified": [
                  "original CSS/SSS route", "retail state/draw comparison",
                  "complete action/stage inventory", "pixels", "PCM fidelity", "cold/warm live timing"],
              "configuration": configuration}
    boundary = "inputs"
    next_check = "Check manifest paths and source identities; supply the owned inputs before rebuilding."
    try:
        report["manifest"] = file_identity(manifest)
        checks, traces = load_manifest(manifest)
        build_dir = root / ("build/browser-release" if configuration == "Release" else "build/browser")
        report["planned_lifecycles"] = [
            {"id": run["id"], "scope": run["scope"]}
            for run in trace_runs(traces, build_dir, Path("node"))]
        for trace in traces:
            for key in ("assets", "menus"):
                if key in trace and out.is_relative_to(trace[key]):
                    raise ValueError("Output must be outside input asset directories")
        report["inputs"] = input_inventory(checks, traces)
        report["source"] = {
            "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=root)).hexdigest(),
            "dependencies": file_identity(root / "dependencies.lock.json"),
            "runner": file_identity(Path(__file__)),
        }
        boundary = "runtime"
        next_check = "Resolve the configured project-local Node/SDK error before running checks."
        node = node_runtime(root)
        report["node"] = file_identity(node)
        # Materialize exactly the resolved check rows for the existing checker.
        asset_manifest = out / "assets.json"
        asset_manifest.write_text(json.dumps({"checks": checks}, default=str, indent=2) + "\n")
        boundary = "assets"
        next_check = "Inspect the rejected file/root and reason in assets.log against its original source consumer."
        run_step(report, out, {"id": boundary, "command": [sys.executable,
                 str(root / "scripts/check_assets.py"), "--manifest", str(asset_manifest)]},
                 timeout=600, root=root)
        boundary = "configure"
        next_check = "Inspect configure.log; resolve the first pinned-source or toolchain error before retrying."
        run_step(report, out, {"id": boundary, "command": [sys.executable,
                 str(root / "scripts/build.py"), "--target", "gameplay", "--configuration",
                 configuration, "--configure-only"]}, timeout=600, root=root)
        cmake = root / ".venv" / ("Scripts/cmake.exe" if os.name == "nt" else "bin/cmake")
        targets = sorted({trace["target"] for trace in traces})
        sdk = root / ".deps/emsdk"
        environment = {"EMSDK": str(sdk), "EM_CONFIG": str(sdk / ".emscripten"),
                       "EM_CACHE": str(sdk / "upstream/emscripten/cache"),
                       "EMSDK_PYTHON": sys.executable,
                       "PATH": str(cmake.parent) + os.pathsep + os.environ.get("PATH", "")}
        boundary = "build"
        next_check = "Inspect build.log and repair the first compiler/linker failure in the selected source target."
        run_step(report, out, {"id": boundary, "command": [str(cmake), "--build", str(build_dir),
                 "--target", *targets, "--parallel", str(jobs)], "environment": environment},
                 timeout=1800, root=root)
        boundary = "build-artifacts"
        next_check = "Inspect build.log and the selected target's missing output; no other build directory will be substituted."
        report["build"] = [file_identity(build_dir / (target + suffix))
                           for target in targets for suffix in (".js", ".wasm")]
        runs = trace_runs(traces, build_dir, node)
        for run in runs:
            boundary = run["id"]
            next_check = "Inspect this lifecycle log's first error and last source checkpoint; reduce that boundary before a full browser replay."
            run_step(report, out, run, timeout=180, root=root)
        boundary = "input-integrity"
        next_check = "Inputs changed during the run; preserve this report and rerun with stable owned inputs."
        before = {row["path"]: row for row in report["inputs"]}
        after = {row["path"]: row for row in input_inventory(checks, traces)}
        report["input_changes"] = [
            {"path": path, "before": before.get(path), "after": after.get(path)}
            for path in sorted(before.keys() | after.keys()) if before.get(path) != after.get(path)]
        if report["input_changes"]:
            raise ValueError("Input inventory changed during validation")
        report["result"] = "passed"
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, CheckFailed) as error:
        failed = next((step for step in report["steps"] if step["status"] == "failed"), None)
        report["first_failure"] = {"boundary": boundary, "message": str(error), "next_check": next_check}
        if failed:
            report["first_failure"].update(command=failed["command"], log=failed["log"],
                                           diagnostic_tail=failed.get("diagnostic_tail", ""))
            if "first_rejection" in failed:
                report["first_failure"]["asset"] = failed["first_rejection"]
        print(f"Content check failed at {boundary}: {error}", file=sys.stderr)
        return 1
    finally:
        completed = {step["id"] for step in report["steps"] if step["status"] == "passed"}
        report["not_run"] = [run["id"] for run in report.get("planned_lifecycles", [])
                             if run["id"] not in completed and run["id"] != boundary]
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{report['result']}: {out / 'report.json'}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path,
                        help="Local JSON checks/lifecycles inventory; paths are relative to this file")
    parser.add_argument("--out", required=True, type=Path, help="New evidence directory under work/")
    parser.add_argument("--configuration", choices=("Release", "RelWithDebInfo"), default="Release")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "work"):
        parser.error("--out must be under ignored work/")
    try:
        return validate(args.manifest.resolve(), out, configuration=args.configuration, jobs=args.jobs)
    except OSError as error:
        parser.exit(1, f"content checks: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
