#!/usr/bin/env python3
"""Run reviewed simulated-network scenarios serially under the capture owner.

Invoke through agent_workspace.py run. This does not build a runtime, retry a
failure, start public services, or promote simulated traffic to Internet evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from check_gameplay import node_runtime

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = {
    "css-moderate": ("moderate", False, False),
    "full-moderate": ("moderate", True, False),
    "full-asymmetric": ("asymmetric", True, False),
    "css-constrained": ("constrained", False, False),
    "css-constrained-stall": ("constrained", False, True),
}


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def file_identity(path):
    return {"bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def runtime_identity(directory):
    required = ("runtime.html", "gameplay_menu_browser.wasm",
                "net_lockstep_browser_peer.mjs", "net_lockstep_core.mjs",
                "net_lockstep_websocket_relay.mjs")
    for name in required:
        if not (directory / name).is_file():
            raise ValueError(f"served runtime is missing {name}")
    return {str(path.relative_to(directory)): file_identity(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def validate_result(returncode, owner, browser, full_route, controlled_stall=False):
    """Require both independent owners; an expected fault is a different policy."""
    if returncode != 0:
        raise ValueError(f"capture owner exited {returncode}")
    if (owner.get("schema") != "melee-web-capture-process-owner-v3" or
            owner.get("result") != "pass" or owner.get("capture_exit_code") != 0 or
            owner.get("cleanup_passed") is not True or
            owner.get("process_observation_passed") is not True):
        raise ValueError("capture owner did not verify successful cleanup and observation")
    expected_decision = "PASS_POST_CSS_STALL_PROBE" if controlled_stall else "PASS"
    if (browser.get("schema") != "melee-web-a3-local-room-keyboard-v1" or
            browser.get("decision") != expected_decision or browser.get("first_error") is not None or
            browser.get("relay_mode") != "simulated-network"):
        raise ValueError("browser did not pass the requested simulated-network result policy")
    network = browser.get("network_simulation") or {}
    if (network.get("proxy_cleanup", {}).get("verified") is not True or
            network.get("proxy_cleanup", {}).get("final") is not True):
        raise ValueError("browser did not verify final proxy integrity and retirement")
    if full_route and (browser.get("full_route") or {}).get("result") != "pass":
        raise ValueError("full route is missing or incomplete")
    if controlled_stall:
        probe = network.get("post_css_stall_probe") or {}
        if (probe.get("outcome") != "EXERCISED_AND_RESUMED" or
                probe.get("policy_result", {}).get("policy") != "controlled-post-css-stall-exercised" or
                probe.get("policy_result", {}).get("result_is_not_clean_route_pass") is not True):
            raise ValueError("controlled-stall probe did not satisfy its separate result policy")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True,
                        help="new evidence directory on the verified external volume")
    parser.add_argument("--playwright", type=Path, required=True)
    parser.add_argument("--runtime-directory", type=Path,
                        default=ROOT / "build/browser-release")
    parser.add_argument("--browser-executable", type=Path,
                        default=Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, choices=[8787], default=8787,
                        help="the existing keyboard harness requires port 8787")
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS,
                        default=["css-moderate", "full-moderate", "full-asymmetric", "css-constrained"])
    parser.add_argument("--plan-only", action="store_true",
                        help="write commands and identities without starting a server or browser")
    args = parser.parse_args()
    if len(set(args.scenarios)) != len(args.scenarios):
        parser.error("duplicate scenarios would repeat captures without a new hypothesis")
    for path in (args.disc, args.playwright, args.browser_executable):
        if not path.exists():
            parser.error(f"required local input is unavailable: {path}")
    runtime = args.runtime_directory.resolve(strict=True)
    baseline = runtime_identity(runtime)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    temporary = output / "tmp"
    temporary.mkdir()
    environment = dict(os.environ, TMPDIR=str(temporary),
                       MELEE_BROWSER_PATH=str(args.browser_executable.resolve()))
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    report = {"schema": "melee-web-network-simulation-suite-v1",
              "scope": "same-machine simulated network; no public or acceptance claim",
              "source": head, "runtime_directory": str(runtime), "runtime_files": baseline,
              "runner": file_identity(Path(__file__)),
              "execution_environment": {key: environment[key]
                                        for key in ("TMPDIR", "MELEE_BROWSER_PATH")},
              "started_at": datetime.now(timezone.utc).isoformat(),
              "decision": "PLANNED" if args.plan_only else "RUNNING", "scenarios": []}
    report_path = output / "suite.json"
    node = str(node_runtime(ROOT))
    server_spec = output / "server-spec.json"
    write_json(server_spec, {
        "command": [sys.executable, str(ROOT / "scripts/serve.py"),
                    "--directory", str(runtime), "--port", str(args.port)],
        "ready_url": f"http://127.0.0.1:{args.port}/runtime.html", "ready_timeout_ms": 10000})
    try:
        for name in args.scenarios:
            profile, full_route, controlled_stall = SCENARIOS[name]
            folder = output / name
            folder.mkdir()
            command = [sys.executable, str(ROOT / "scripts/capture_owner_deadline.py"),
                       "--overall-timeout-ms", "300000", "--cleanup-reserve-ms", "30000",
                       "--report", str(folder / "owner.json"), "--log", str(folder / "capture.log"),
                       "--process-trace", str(folder / "browser-processes.jsonl"),
                       "--browser-executable", str(args.browser_executable.resolve()),
                       "--browser-report", str(folder / "browser/report.json"),
                       "--require-browser-process", "--require-cdp-attribution",
                       "--server-spec", str(server_spec), "--server-log", str(folder / "server.log"),
                       "--", node, str(ROOT / "scripts/a3_local_room_keyboard_browser.mjs"),
                       "--url", f"http://127.0.0.1:{args.port}/runtime.html",
                       "--disc", str(args.disc.resolve()), "--out", str(folder / "browser"),
                       "--playwright", str(args.playwright.resolve()), "--min-checksums", "120",
                       "--timeout-ms", "260000", "--network-profile", profile]
            if full_route:
                command.append("--full-route")
            if controlled_stall:
                command.append("--post-css-stall-probe")
            row = {"name": name, "profile": profile, "full_route": full_route,
                   "controlled_stall_probe": controlled_stall,
                   "result_policy": "controlled-stall-probe" if controlled_stall else "clean-route",
                   "command": command, "decision": "PLANNED"}
            report["scenarios"].append(row)
            write_json(report_path, report)
            if args.plan_only:
                continue
            if runtime_identity(runtime) != baseline:
                raise ValueError("served runtime changed between scenarios")
            row["decision"] = "RUNNING"
            write_json(report_path, report)
            with (folder / "supervisor.log").open("x") as log:
                result = subprocess.run(command, cwd=ROOT, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT)
            row["exit_code"] = result.returncode
            for key, relative in (("owner", "owner.json"), ("browser", "browser/report.json")):
                path = folder / relative
                if path.is_file():
                    row[key + "_report"] = {"file": str(path.relative_to(output)), **file_identity(path)}
            owner = json.loads((folder / "owner.json").read_text())
            browser = json.loads((folder / "browser/report.json").read_text())
            validate_result(result.returncode, owner, browser, full_route, controlled_stall)
            if runtime_identity(runtime) != baseline:
                raise ValueError("served runtime changed during scenario")
            row["decision"] = "PASS_POST_CSS_STALL_PROBE" if controlled_stall else "PASS"
            write_json(report_path, report)
        report["decision"] = "PLANNED" if args.plan_only else (
            "PASS_WITH_CONTROLLED_STALL_PROBE" if any(
                SCENARIOS[name][2] for name in args.scenarios) else "PASS")
    except Exception as error:
        report["decision"] = "FAIL"
        report["first_error"] = str(error)
        if report["scenarios"]:
            report["scenarios"][-1]["decision"] = "FAIL"
        return 1
    finally:
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        write_json(report_path, report)
        print(json.dumps({"decision": report["decision"], "report": str(report_path)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
