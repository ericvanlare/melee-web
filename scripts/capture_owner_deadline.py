#!/usr/bin/env python3
"""Own one Chrome capture and its loopback server under a bounded wall clock."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from urllib.parse import urlparse


def process_table():
    output = subprocess.check_output(
        ["ps", "-Aww", "-o", "pid=,ppid=,pgid=,lstart=,command="],
        text=True, stderr=subprocess.STDOUT)
    rows = {}
    for line in output.splitlines():
        fields = line.strip().split(None, 8)
        if len(fields) != 9:
            continue
        try:
            pid, ppid, pgid = map(int, fields[:3])
        except ValueError:
            continue
        rows[pid] = {
            "pid": pid, "ppid": ppid, "pgid": pgid,
            "start_identity": " ".join(fields[3:8]),
            "command": fields[8],
        }
    return rows


def process_identity(row):
    return (row["pid"], row["start_identity"], row["command"])


def is_browser_command(command, aliases):
    command = command.lstrip()
    for executable in aliases:
        if command.startswith(executable) and (
                len(command) == len(executable) or command[len(executable)] in " \t"):
            return True
        for quote in ('"', "'"):
            quoted = quote + executable + quote
            if command.startswith(quoted) and (
                    len(command) == len(quoted) or command[len(quoted)] in " \t"):
                return True
    return False


def lineage_for(pid, table, root_pid):
    chain = []
    seen = set()
    current = pid
    while current in table and current not in seen:
        seen.add(current)
        row = table[current]
        chain.append(dict(row))
        if current == root_pid:
            return list(reversed(chain))
        current = row["ppid"]
    return None


def capture_browser_inventory(root_pid, aliases, inventory, trace_stream):
    table = process_table()
    descendants = {}
    for pid in table:
        chain = lineage_for(pid, table, root_pid)
        if chain and len(chain) > 1:
            descendants[pid] = chain
    browser_roots = [pid for pid, chain in descendants.items()
                     if is_browser_command(table[pid]["command"], aliases)]
    added = []
    for pid, chain in descendants.items():
        root_matches = [root for root in browser_roots
                        if any(row["pid"] == root for row in chain)]
        if not root_matches:
            continue
        row = table[pid]
        identity = process_identity(row)
        if identity in inventory:
            continue
        record = {
            **row,
            "identity": {"pid": row["pid"], "start": row["start_identity"],
                         "command": row["command"]},
            "browser_root_pid": min(root_matches),
            "parent_lineage": chain,
            "observed_while_capture_root_alive": True,
        }
        inventory[identity] = record
        trace_stream.write(json.dumps(record, separators=(",", ":")) + "\n")
        trace_stream.flush()
        added.append(record)
        if len(inventory) > 512:
            raise RuntimeError("owned browser process inventory exceeded 512 identities")
    return table, added


def signal_group(pgid, sig):
    try:
        os.killpg(pgid, sig)
        return None
    except ProcessLookupError:
        return None
    except OSError as error:
        return f"{type(error).__name__}: {error}"


def group_exists(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def signal_pid_if_same(record, table, sig):
    current = table.get(record["pid"])
    if current is None:
        return "absent"
    if (current["start_identity"] != record["start_identity"] or
            current["command"] != record["command"]):
        return "identity_changed_preserved"
    try:
        os.kill(record["pid"], sig)
        return "signaled"
    except ProcessLookupError:
        return "absent"
    except OSError as error:
        return f"{type(error).__name__}: {error}"


def wait_for_server(server, ready_url, timeout_ms, capture_deadline):
    deadline = min(capture_deadline, time.monotonic() + timeout_ms / 1000)
    last_error = None
    while time.monotonic() < deadline:
        if server.poll() is not None:
            raise RuntimeError(f"server exited before HTTP readiness: {server.returncode}")
        try:
            timeout = min(1.0, max(0.1, deadline - time.monotonic()))
            with urllib.request.urlopen(ready_url, timeout=timeout) as response:
                if response.status == 200:
                    if server.poll() is not None:
                        raise RuntimeError(
                            f"owned server exited during HTTP readiness: {server.returncode}")
                    return
                last_error = f"HTTP {response.status}"
        except Exception as error:
            last_error = f"{type(error).__name__}: {error}"
        time.sleep(min(0.1, max(0.001, deadline - time.monotonic())))
    raise TimeoutError(f"loopback server readiness exceeded {timeout_ms} ms: {last_error}")


def require_loopback_port_free(ready_url):
    parsed = urlparse(ready_url)
    if parsed.hostname != "127.0.0.1" or parsed.port is None:
        raise ValueError("server readiness URL must use an explicit 127.0.0.1 port")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.25)
        result = probe.connect_ex((parsed.hostname, parsed.port))
    if result == 0:
        raise RuntimeError(f"loopback port {parsed.port} is already in use; preserving its owner")


def read_capture_report(path, report):
    if path is None or not path.is_file():
        report["capture_report_available"] = False
        return []
    try:
        data = json.loads(path.read_text())
    except Exception as error:
        report["capture_report_error"] = f"{type(error).__name__}: {error}"
        return []
    if not isinstance(data, dict):
        report["capture_report_error"] = "capture report root must be a JSON object"
        return []
    report["capture_report_available"] = True
    report["capture_report_path"] = str(path)
    info = data.get("browser_process_info") or []
    report["cdp_browser_process_info"] = info
    report["cdp_browser_process_groups"] = data.get("browser_process_groups", [])
    report["capture_browser_process_ps"] = data.get("browser_process_ps", [])
    report["capture_browser_process_start"] = data.get("browser_process_start", [])
    report["capture_browser_cleanup"] = {
        "pids": data.get("browser_process_cleanup", []),
        "groups": data.get("browser_process_group_cleanup", []),
    }
    report["cdp_inventory_count"] = len(info) if isinstance(info, list) else 0
    malformed = [row for row in info if not isinstance(row, dict) or
                 not isinstance(row.get("id"), int) or row.get("id", 0) <= 0 or
                 not isinstance(row.get("type"), str)] if isinstance(info, list) else [info]
    if malformed:
        report["cdp_inventory_error"] = f"malformed CDP process rows: {malformed[:8]}"
    return [int(row["id"]) for row in info if isinstance(row, dict) and
            isinstance(row.get("id"), int) and row["id"] > 0] if isinstance(info, list) else []


def prepare_output_parents(output_paths, browser_report=None):
    """Prepare owner outputs without precreating the capture runner's directory."""
    browser_report = Path(browser_report) if browser_report else None
    if browser_report is not None:
        browser_report.parent.parent.mkdir(parents=True, exist_ok=True)
    for path in output_paths:
        path = Path(path)
        if browser_report is not None and path == browser_report:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overall-timeout-ms", type=int, required=True)
    parser.add_argument("--cleanup-reserve-ms", type=int, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--process-trace", type=Path, required=True)
    parser.add_argument("--browser-executable", required=True)
    parser.add_argument("--browser-report", type=Path)
    parser.add_argument("--require-browser-process", action="store_true")
    parser.add_argument("--require-cdp-attribution", action="store_true")
    parser.add_argument("--browser-scan-interval-ms", type=int, default=250)
    parser.add_argument("--server-spec", type=Path)
    parser.add_argument("--server-log", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a capture command is required after --")
    if args.overall_timeout_ms < 10000 or args.cleanup_reserve_ms < 1000:
        parser.error("overall timeout must be at least 10000 ms and cleanup reserve at least 1000 ms")
    if args.cleanup_reserve_ms >= args.overall_timeout_ms:
        parser.error("cleanup reserve must be shorter than the overall timeout")
    if args.browser_scan_interval_ms < 50 or args.browser_scan_interval_ms > 5000:
        parser.error("browser scan interval must be between 50 and 5000 ms")
    if bool(args.server_spec) != bool(args.server_log):
        parser.error("--server-spec and --server-log must be supplied together")
    output_paths = [args.report, args.log, args.process_trace]
    if args.browser_report:
        output_paths.append(args.browser_report)
    if args.server_log:
        output_paths.append(args.server_log)
    if any(path.exists() for path in output_paths):
        parser.error("report, trace and log paths must be new so evidence is never overwritten")
    browser_path = str(Path(args.browser_executable).expanduser().resolve())
    if not Path(browser_path).is_file() or not os.access(browser_path, os.X_OK):
        parser.error("--browser-executable must name the installed executable selected by browser_tools.mjs")

    prepare_output_parents(output_paths, args.browser_report)
    started_wall = time.time()
    started_mono = time.monotonic()
    hard_deadline = started_mono + args.overall_timeout_ms / 1000
    capture_deadline = hard_deadline - args.cleanup_reserve_ms / 1000
    aliases = {browser_path, os.path.realpath(browser_path)}
    report = {
        "schema": "melee-web-capture-process-owner-v3",
        "owner_script": {
            "path": str(Path(__file__).resolve()),
            "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        },
        "result": "fail",
        "capture_command": command,
        "browser_executable": browser_path,
        "browser_executable_aliases": sorted(aliases),
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started_wall)),
        "overall_timeout_ms": args.overall_timeout_ms,
        "cleanup_reserve_ms": args.cleanup_reserve_ms,
        "capture_deadline_monotonic_seconds": capture_deadline,
        "hard_deadline_monotonic_seconds": hard_deadline,
        "browser_scan_interval_ms": args.browser_scan_interval_ms,
        "browser_process_inventory": [],
    }
    child = None
    server = None
    capture_log = None
    server_log = None
    trace_stream = None
    inventory = {}
    process_rows = {}
    server_spec = None
    try:
        trace_stream = open(args.process_trace, "w")
        if args.server_spec:
            server_spec = json.loads(args.server_spec.read_text())
            server_command = server_spec.get("command")
            ready_url = server_spec.get("ready_url")
            ready_timeout_ms = server_spec.get("ready_timeout_ms", 15000)
            if (not isinstance(server_command, list) or not server_command or
                    not all(isinstance(part, str) and part for part in server_command)):
                raise ValueError("server spec command must be a nonempty string array")
            if not isinstance(ready_url, str) or not ready_url.startswith("http://127.0.0.1:"):
                raise ValueError("server spec must use loopback HTTP")
            if not isinstance(ready_timeout_ms, int) or ready_timeout_ms < 1000:
                raise ValueError("server readiness timeout must be at least 1000 ms")
            require_loopback_port_free(ready_url)
            report["server_spec"] = {"path": str(args.server_spec), "ready_url": ready_url,
                                     "ready_timeout_ms": ready_timeout_ms,
                                     "command": server_command}
            server_log = open(args.server_log, "wb")
            server = subprocess.Popen(server_command, stdin=subprocess.DEVNULL,
                                      stdout=server_log, stderr=subprocess.STDOUT,
                                      start_new_session=True)
            report["server_pid"] = server.pid
            report["server_process_group"] = server.pid
            wait_for_server(server, ready_url, ready_timeout_ms, capture_deadline)
            report["server_ready"] = True

        capture_log = open(args.log, "wb")
        child_env = os.environ.copy()
        child_env["MELEE_BROWSER_PATH"] = browser_path
        child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=capture_log,
                                 stderr=subprocess.STDOUT, start_new_session=True, env=child_env)
        report["capture_pid"] = child.pid
        report["capture_process_group"] = child.pid
        scan_interval = args.browser_scan_interval_ms / 1000
        next_scan = 0.0
        while child.poll() is None:
            now = time.monotonic()
            if now >= capture_deadline:
                report["owner_deadline_expired"] = True
                report["termination_signal"] = "SIGTERM"
                break
            if now >= next_scan:
                process_rows, added = capture_browser_inventory(
                    child.pid, aliases, inventory, trace_stream)
                report["browser_inventory_observation_count"] = report.get(
                    "browser_inventory_observation_count", 0) + 1
                next_scan = time.monotonic() + scan_interval
            time.sleep(min(0.05, max(0.001, capture_deadline - time.monotonic())))
        if child.poll() is not None:
            report["capture_exit_code"] = child.returncode
    except Exception as error:
        report["owner_error"] = f"{type(error).__name__}: {error}"
    finally:
        cleanup_started = time.monotonic()
        cleanup_deadline = min(hard_deadline - 0.25,
                               cleanup_started + args.cleanup_reserve_ms / 1000)
        groups = []
        for label, process in (("capture", child), ("server", server)):
            if process is not None:
                groups.append((label, process, process.pid))
        # First stop only the isolated runner and server groups. Chrome can live
        # in a different group and is handled below using recorded identities.
        for label, process, pgid in groups:
            if process.poll() is None and group_exists(pgid):
                report[f"{label}_term_error"] = signal_group(pgid, signal.SIGTERM)
        term_deadline = min(cleanup_deadline - 1.0,
                            time.monotonic() + max(0.25, args.cleanup_reserve_ms / 1000 * 0.70))
        while time.monotonic() < term_deadline:
            if child is not None and child.poll() is None:
                try:
                    process_rows, _ = capture_browser_inventory(
                        child.pid, aliases, inventory, trace_stream)
                except Exception as error:
                    report["browser_inventory_cleanup_error"] = f"{type(error).__name__}: {error}"
            leaders_done = all(process.poll() is not None for _, process, _ in groups)
            if leaders_done:
                break
            time.sleep(0.05)
        for label, process, pgid in groups:
            if process.poll() is None and group_exists(pgid):
                report[f"{label}_kill_error"] = signal_group(pgid, signal.SIGKILL)
            if process.poll() is None:
                try:
                    process.wait(timeout=max(0.0, cleanup_deadline - time.monotonic() - 0.25))
                except subprocess.TimeoutExpired:
                    report[f"{label}_reaped"] = False

        if args.browser_report:
            cdp_ids = read_capture_report(args.browser_report, report)
        else:
            cdp_ids = []
        # Refresh exact parent lineage before cleanup if the runner is still alive.
        if child is not None and child.poll() is None:
            try:
                process_rows, _ = capture_browser_inventory(
                    child.pid, aliases, inventory, trace_stream)
            except Exception as error:
                report["browser_inventory_final_error"] = f"{type(error).__name__}: {error}"
        try:
            process_rows = process_table()
        except Exception as error:
            process_rows = {}
            report["process_table_cleanup_error"] = f"{type(error).__name__}: {error}"
        report["browser_process_inventory"] = list(inventory.values())
        report["cdp_attribution"] = []
        for cdp_pid in cdp_ids:
            matching = [record for record in inventory.values() if record["pid"] == cdp_pid]
            current = process_rows.get(cdp_pid)
            same_identity = bool(matching and current is not None and
                                 current["start_identity"] == matching[0]["start_identity"] and
                                 current["command"] == matching[0]["command"])
            report["cdp_attribution"].append({
                "pid": cdp_pid,
                "owned_parent_lineage_observed": bool(matching),
                "owned_process_identity": (matching[0]["identity"] if matching else None),
                "currently_live_with_observed_identity": same_identity,
                "status": ("owned_observed" if matching else
                           ("already_absent" if current is None else "unattributed_live_process")),
            })
        unattributed = [row for row in report["cdp_attribution"]
                        if row["status"] == "unattributed_live_process"]
        report["cdp_attribution_complete"] = not unattributed
        if args.require_cdp_attribution and not report.get("capture_report_available"):
            report["cdp_attribution_complete"] = False
            report["cdp_attribution_error"] = "capture report with CDP inventory is unavailable"
        if args.require_cdp_attribution and not report.get("cdp_inventory_count"):
            report["cdp_attribution_complete"] = False
            report["cdp_attribution_error"] = "capture report contains no CDP process inventory"
        if args.require_cdp_attribution and report.get("cdp_inventory_error"):
            report["cdp_attribution_complete"] = False
            report["cdp_attribution_error"] = report["cdp_inventory_error"]
        report["browser_root_observed"] = any(
            record["pid"] == record["browser_root_pid"] and
            is_browser_command(record["command"], aliases)
            for record in inventory.values())
        if args.require_browser_process and not report["browser_root_observed"]:
            report["browser_attribution_error"] = "no exact Chrome root was observed while the capture runner was alive"

        # Chrome may outlive Node and may have its own process group. Signal only
        # processes whose PID, start identity and command still match the exact
        # descendant inventory captured while Node was alive.
        owned_cleanup = []
        for identity, record in inventory.items():
            current = process_rows.get(record["pid"])
            if current is None:
                state = "absent"
            elif (current["start_identity"] != record["start_identity"] or
                  current["command"] != record["command"]):
                state = "identity_changed_preserved"
            else:
                state = signal_pid_if_same(record, process_rows, signal.SIGTERM)
            owned_cleanup.append({"pid":record["pid"], "start_identity":record["start_identity"],
                                  "pgid":record["pgid"], "term":state, "kill":"not_needed"})
        while time.monotonic() < term_deadline:
            try:
                process_rows = process_table()
            except Exception as error:
                report["process_table_wait_error"] = f"{type(error).__name__}: {error}"
                break
            live_owned = []
            for row in owned_cleanup:
                current = process_rows.get(row["pid"])
                if current is not None and current["start_identity"] == row["start_identity"]:
                    record = inventory[(row["pid"], row["start_identity"], current["command"])] if (row["pid"], row["start_identity"], current["command"]) in inventory else None
                    if record is not None and current["command"] == record["command"]:
                        live_owned.append(row)
            if not live_owned:
                break
            time.sleep(0.05)
        try:
            process_rows = process_table()
        except Exception as error:
            process_rows = {}
            report["process_table_kill_error"] = f"{type(error).__name__}: {error}"
        for row in owned_cleanup:
            current = process_rows.get(row["pid"])
            record = next((r for r in inventory.values()
                           if r["pid"] == row["pid"] and r["start_identity"] == row["start_identity"]), None)
            if (record is not None and current is not None and
                    current["start_identity"] == record["start_identity"] and
                    current["command"] == record["command"]):
                row["kill"] = signal_pid_if_same(record, process_rows, signal.SIGKILL)
        # Direct process signals above preserve unrelated members of a shared
        # Chrome group. Verify each owned instance and summarize each observed PGID.
        verify_deadline = cleanup_deadline - 0.1
        while time.monotonic() < verify_deadline:
            try:
                process_rows = process_table()
            except Exception as error:
                report["process_table_verify_error"] = f"{type(error).__name__}: {error}"
                break
            owned_live = []
            for row in owned_cleanup:
                current = process_rows.get(row["pid"])
                record = next((r for r in inventory.values()
                               if r["pid"] == row["pid"] and r["start_identity"] == row["start_identity"]), None)
                if (record is not None and current is not None and
                        current["start_identity"] == record["start_identity"] and
                        current["command"] == record["command"]):
                    owned_live.append(row)
            if not owned_live:
                break
            time.sleep(0.05)
        for row in owned_cleanup:
            current = process_rows.get(row["pid"])
            record = next((r for r in inventory.values()
                           if r["pid"] == row["pid"] and r["start_identity"] == row["start_identity"]), None)
            row["absent"] = (current is None or record is None or
                             current["start_identity"] != row["start_identity"] or
                             current["command"] != record["command"])
            if current is not None and row["absent"]:
                row["replacement_pid_preserved"] = True
        report["browser_process_cleanup"] = owned_cleanup
        cleanup_by_identity = {
            (row["pid"], row["start_identity"]): row for row in owned_cleanup
        }
        for cdp_row in report["cdp_attribution"]:
            identity = cdp_row.get("owned_process_identity")
            cleanup = (cleanup_by_identity.get((identity["pid"], identity["start"]))
                       if identity else None)
            cdp_row["absent_after_cleanup"] = (
                cdp_row["status"] == "already_absent" or
                (cleanup is not None and cleanup.get("absent") is True))
            if not cdp_row["absent_after_cleanup"]:
                cdp_row["cleanup_error"] = "CDP process identity remains or was not safely attributed"
        report["cdp_attribution_complete"] = (
            report.get("cdp_attribution_complete", False) and
            all(row["absent_after_cleanup"] for row in report["cdp_attribution"]))
        observed_groups = sorted({record["pgid"] for record in inventory.values()})
        group_rows = []
        for pgid in observed_groups:
            members = [row for row in process_rows.values() if row["pgid"] == pgid]
            owned_member_pids = {record["pid"] for record in inventory.values()
                                 if record["pgid"] == pgid and
                                 any(cleanup["pid"] == record["pid"] and cleanup["absent"]
                                     for cleanup in owned_cleanup)}
            foreign = [row["pid"] for row in members if row["pid"] not in owned_member_pids]
            group_rows.append({"pgid":pgid,"absent":not members,
                               "owned_processes_absent":not any(row["pgid"] == pgid and not row["absent"] for row in owned_cleanup),
                               "foreign_or_unattributed_members_preserved":foreign})
        report["browser_process_groups"] = group_rows
        report["capture_process_group_absent"] = (child is None or not group_exists(child.pid))
        report["server_process_group_absent"] = (server is None or not group_exists(server.pid))
        if child is not None and child.poll() is not None:
            report["capture_exit_code"] = child.returncode
        if server is not None and server.poll() is not None:
            report["server_exit_code"] = server.returncode

        if trace_stream is not None:
            trace_stream.close()
        if capture_log is not None:
            capture_log.close()
        if server_log is not None:
            server_log.close()
        for key, log_path in (("capture_log", args.log), ("server_log", args.server_log),
                              ("browser_process_trace", args.process_trace)):
            if log_path and log_path.exists():
                report[key] = {"path": str(log_path), "bytes": log_path.stat().st_size,
                               "sha256": hashlib.sha256(log_path.read_bytes()).hexdigest()}
        report["cleanup_elapsed_ms"] = round((time.monotonic() - cleanup_started) * 1000)
        report["elapsed_ms"] = round((time.monotonic() - started_mono) * 1000)
        report["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        owned_browser_absent = all(row.get("absent") is True for row in owned_cleanup)
        owner_groups_absent = report["capture_process_group_absent"] and report["server_process_group_absent"]
        browser_inventory_ok = (not args.require_browser_process or
                                 report.get("browser_root_observed") is True)
        cdp_ok = (not args.require_cdp_attribution or report.get("cdp_attribution_complete") is True)
        observation_errors = sorted(
            key for key in report
            if key.startswith(("process_table_", "browser_inventory_")) and key.endswith("_error")
        )
        report["process_observation_passed"] = not observation_errors
        report["process_observation_errors"] = observation_errors
        report["cleanup_passed"] = (
            owner_groups_absent and owned_browser_absent and cdp_ok and
            report["process_observation_passed"]
        )
        report["result"] = "pass" if (
            child is not None and child.returncode == 0 and report["cleanup_passed"] and
            browser_inventory_ok and cdp_ok and not report.get("owner_deadline_expired") and
            not report.get("owner_error") and report["elapsed_ms"] <= args.overall_timeout_ms
        ) else "fail"
        temporary = args.report.with_suffix(args.report.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2) + "\n")
        os.replace(temporary, args.report)
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
