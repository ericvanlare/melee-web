#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Static renderer/parser for the source-backed scheduler-return boundary."""
from __future__ import annotations

import ast
import json
import re
from typing import Any

SCHEDULER_RETURN = 0x80390EB4
TARGET_RUNNING = "Cannot execute this command while the target is running."
SOCKET_RE = re.compile(r"^/private/tmp/m3-v[0-9]+-[0-9a-f]{12}\.sock$")
STAGES = (
    "target_remote_returned",
    "sigterm_stop_print_nopass_configured",
    "scheduler_breakpoint_requested",
    "prearm_breakpoint_captured",
    "precontinue_memory_probe_phase",
    "continue_command_returned",
    "stopped_at_scheduler",
    "prearm_breakpoint_deleted",
    "transport_disconnected",
)
_SEND_RE = re.compile(r"Sending packet: (.*)$")
_RECEIVE_RE = re.compile(r"Packet received: (.*)$")
_OMITTED_SUFFIX_RE = re.compile(r" \[[0-9]+ bytes omitted\]$")
_MEMORY_PROBE_RE = re.compile(r"^(?P<kind>[mx])(?P<address>[0-9a-fA-F]+),(?P<length>[0-9a-fA-F]+)$")


class StageProbeError(RuntimeError):
    pass


def _socket_text(path: str) -> str:
    if len(path.encode("utf-8")) >= 104 or not SOCKET_RE.fullmatch(path):
        raise StageProbeError("GDB socket must be a short canonical nonce-only path")
    if any(ord(char) < 0x20 or char.isspace() for char in path):
        raise StageProbeError("GDB socket contains whitespace/control characters")
    return path


def _absolute_file(path: str, label: str):
    from pathlib import Path
    candidate = Path(path).expanduser()
    if not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file():
        raise StageProbeError(f"{label} must be an absolute regular file: {candidate}")
    return candidate


def render_command(*, socket_path: str, owner_manifest: str) -> str:
    """Render the no-child GDB command after a real native owner exists."""
    socket = _socket_text(socket_path)
    owner_path = _absolute_file(owner_manifest, "owner manifest")
    try:
        with owner_path.open(encoding="utf-8") as owner_stream:
            owner = json.load(owner_stream)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise StageProbeError("owner manifest is not valid JSON") from error
    if not isinstance(owner, dict) or owner.get("gdb_socket") != socket:
        raise StageProbeError("target remote socket differs from owner manifest")
    pid = owner.get("pid")
    if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 0:
        raise StageProbeError("owner manifest lacks a positive native PID")
    lines = [
        "set architecture powerpc:common", "set endian big", "set pagination off",
        "set confirm off", "set non-stop off", "set breakpoint pending on",
        "set debug remote 1", f"target remote {socket}",
        "echo V94_STAGE=target_remote_returned\\n",
        "handle SIGTERM stop print nopass",
        "echo V94_STAGE=sigterm_stop_print_nopass_configured\\n",
        "hbreak *0x80390eb4",
        "echo V94_STAGE=scheduler_breakpoint_requested\\n",
        "python", "import gdb, json",
        "_v94_prearm_bp = int(gdb.parse_and_eval('$bpnum'))",
        "if _v94_prearm_bp <= 0:",
        "    raise gdb.GdbError('V94 temporary breakpoint number is invalid')",
        "_v94_matches = [bp for bp in (gdb.breakpoints() or []) if int(getattr(bp, 'number', 0)) == _v94_prearm_bp]",
        "if len(_v94_matches) != 1:",
        "    raise gdb.GdbError('V94 temporary breakpoint was not uniquely captured')",
        "_v94_location = str(getattr(_v94_matches[0], 'location', ''))",
        "if '80390eb4' not in _v94_location.lower():",
        "    raise gdb.GdbError('V94 captured breakpoint location differs from scheduler boundary')",
        "_v94_other_bp = sorted(int(getattr(bp, 'number', 0)) for bp in (gdb.breakpoints() or []) if int(getattr(bp, 'number', 0)) != _v94_prearm_bp)",
        "print('V94_PREARM_BP_JSON=' + json.dumps({'number': _v94_prearm_bp, 'location': _v94_location}, sort_keys=True), flush=True)",
        "print('V94_STAGE=prearm_breakpoint_captured', flush=True)", "end",
        "echo V94_STAGE=precontinue_memory_probe_phase\\n",
        "continue", "echo V94_STAGE=continue_command_returned\\n",
        "python", "import gdb, json",
        "_v94_inferior = gdb.selected_inferior()",
        "_v94_thread = gdb.selected_thread()",
        "_v94_valid = bool(_v94_inferior.is_valid())",
        "_v94_inferior_num = int(getattr(_v94_inferior, 'num', 0))",
        "_v94_thread_num = int(getattr(_v94_thread, 'num', 0))",
        "_v94_stopped = bool(getattr(_v94_thread, 'is_stopped', lambda: False)())",
        "_v94_pc = int(gdb.parse_and_eval('$pc')) & 0xffffffff",
        "if (not _v94_valid or _v94_inferior_num <= 0 or _v94_thread_num <= 0 or not _v94_stopped):",
        "    raise gdb.GdbError('V94 scheduler resume/stop identity/state is invalid')",
        "if _v94_pc != 0x80390eb4:",
        "    raise gdb.GdbError('V94 stopped PC differs from authored scheduler boundary')",
        "print('V94_STOP_JSON=' + json.dumps({'inferior_valid': _v94_valid, 'inferior_num': _v94_inferior_num, 'thread_num': _v94_thread_num, 'stopped': _v94_stopped, 'pc': _v94_pc, 'boundary': 'scheduler'}, sort_keys=True), flush=True)",
        "print('V94_STAGE=stopped_at_scheduler', flush=True)", "end",
        "python", "import gdb, json",
        "_v94_remaining = [bp for bp in (gdb.breakpoints() or []) if int(getattr(bp, 'number', 0)) == _v94_prearm_bp]",
        "if len(_v94_remaining) != 1:",
        "    raise gdb.GdbError('V94 captured breakpoint disappeared before deletion')",
        "gdb.execute('delete %d' % _v94_prearm_bp)",
        "_v94_after_delete = [bp for bp in (gdb.breakpoints() or []) if int(getattr(bp, 'number', 0)) == _v94_prearm_bp]",
        "if _v94_after_delete:",
        "    raise gdb.GdbError('V94 captured breakpoint was not deleted')",
        "_v94_other_after = sorted(int(getattr(bp, 'number', 0)) for bp in (gdb.breakpoints() or []) if int(getattr(bp, 'number', 0)) != _v94_prearm_bp)",
        "if _v94_other_after != _v94_other_bp:",
        "    raise gdb.GdbError('V94 unrelated breakpoints changed during exact deletion')",
        "print('V94_DELETE_JSON=' + json.dumps({'number': _v94_prearm_bp, 'location': _v94_location, 'deleted': True, 'other_numbers_before': _v94_other_bp, 'other_numbers_after': _v94_other_after}, sort_keys=True), flush=True)",
        "print('V94_STAGE=prearm_breakpoint_deleted', flush=True)", "end",
        "disconnect", "python", "import gdb, json",
        "_v94_transport_inferior = gdb.selected_inferior()",
        "_v94_transport_thread = gdb.selected_thread()",
        "_v94_connection_is_none = (_v94_transport_inferior.connection is None)",
        "_v94_thread_is_none = (_v94_transport_thread is None)",
        "if not _v94_connection_is_none or not _v94_thread_is_none:",
        "    raise gdb.GdbError('V94 transport remained connected after disconnect')",
        "print('V94_TRANSPORT_DISCONNECT_JSON=' + json.dumps({'command': 'disconnect', 'transport_closed': bool(_v94_connection_is_none and _v94_thread_is_none), 'connection_is_none': _v94_connection_is_none, 'selected_thread_is_none': _v94_thread_is_none, 'native_resume_observed': False, 'detach_protocol': False}, sort_keys=True), flush=True)",
        "end", "echo V94_STAGE=transport_disconnected\\n", "quit", "",
    ]
    text = "\n".join(lines)
    validate_command(text)
    return text


def _lines(text: str) -> list[str]:
    if not isinstance(text, str) or not text.endswith("\n"):
        raise StageProbeError("command must be newline-terminated")
    return text.splitlines()


def _python_blocks(lines: list[str]) -> list[tuple[int, int, str]]:
    blocks: list[tuple[int, int, str]] = []
    python_lines: set[int] = set()
    i = 0
    while i < len(lines):
        if lines[i].strip() != "python":
            i += 1
            continue
        start = i; i += 1; body_start = i
        while i < len(lines) and lines[i].strip() != "end":
            python_lines.add(i); i += 1
        if i >= len(lines):
            raise StageProbeError("GDB Python block is missing end")
        body = "\n".join(lines[body_start:i]) + "\n"
        try:
            ast.parse(body, filename="<gdb-python>")
        except SyntaxError as error:
            raise StageProbeError(f"invalid GDB Python block: {error}") from error
        blocks.append((start, i, body)); i += 1
    if len(blocks) != 4:
        raise StageProbeError("V94 requires breakpoint, stop, deletion, and transport Python blocks")
    allowed = ("set ", "handle SIGTERM ", "target remote ", "hbreak ", "echo ", "continue", "disconnect", "quit")
    for index, line in enumerate(lines):
        if index in python_lines or line.strip() in {"python", "end", ""}:
            continue
        if not line.startswith(allowed):
            raise StageProbeError(f"bare or unknown GDB/Python statement outside Python block: {line!r}")
    return blocks


def validate_command(text: str) -> dict[str, Any]:
    lines = _lines(text); _python_blocks(lines)
    targets = [i for i, line in enumerate(lines) if line.startswith("target remote ")]
    breaks = [i for i, line in enumerate(lines) if line == "hbreak *0x80390eb4"]
    continues = [i for i, line in enumerate(lines) if line == "continue"]
    if len(targets) != 1 or len(breaks) != 1 or len(continues) != 1:
        raise StageProbeError("V94 requires exactly one target, scheduler hbreak, and continue")
    if not targets[0] < breaks[0] < continues[0]:
        raise StageProbeError("V94 target/hbreak/continue order is invalid")
    _socket_text(lines[targets[0]].split(" ", 2)[2])
    if any(line.startswith('target remote "') for line in lines):
        raise StageProbeError("target remote must use the unquoted nonce-only path")
    if any(token in text for token in ("stepi", "arm_from_owner_manifest", "m3-v7-finish", "read_memory", "write_memory", "set $")):
        raise StageProbeError("V94 scheduler resume/stop excludes stepi, arm, finish, and direct memory API calls")
    if lines.count("handle SIGTERM stop print nopass") != 1 or lines.count("set debug remote 1") != 1:
        raise StageProbeError("V94 requires one SIGTERM policy and one remote trace control")
    if lines.count("disconnect") != 1 or lines.count("quit") != 1:
        raise StageProbeError("V94 requires one disconnect and one quit")
    if any(line.strip() == "detach" or "gdb.execute('detach')" in line for line in lines):
        raise StageProbeError("V94 forbids unsupported detach")
    for stage in STAGES:
        if text.count(f"V94_STAGE={stage}") != 1:
            raise StageProbeError(f"missing or duplicate V94 stage: {stage}")
    positions = [next(i for i, line in enumerate(lines) if f"V94_STAGE={stage}" in line) for stage in STAGES]
    if positions != sorted(positions):
        raise StageProbeError("V94 stages are out of order")
    return {"target_line": targets[0] + 1, "scheduler_breakpoint_line": breaks[0] + 1,
            "continue_line": continues[0] + 1, "stages": list(STAGES),
            "guest_writes": False, "arm_or_read": False, "direct_memory_api": False}


def _marker(lines: list[str], prefix: str) -> dict[str, Any]:
    values = [line[len(prefix):] for line in lines if line.startswith(prefix)]
    if len(values) != 1:
        raise StageProbeError(f"expected exactly one {prefix} marker")
    try:
        result = json.loads(values[0])
    except json.JSONDecodeError as error:
        raise StageProbeError(f"malformed {prefix} marker") from error
    if not isinstance(result, dict):
        raise StageProbeError(f"{prefix} marker is not an object")
    return result


def _payload(raw: str) -> str:
    value = _OMITTED_SUFFIX_RE.sub("", raw).strip()
    if value.startswith("$") and "#" in value:
        return value[1:value.rfind("#")]
    return value


def parse_remote_packets(text: str) -> dict[str, Any]:
    if not isinstance(text, str):
        raise StageProbeError("GDB trace must be text")
    lines = text.splitlines()
    if TARGET_RUNNING in text:
        raise StageProbeError("target-running failure occurred during scheduler continue")
    stage_line = {stage: next((i for i, line in enumerate(lines) if line == f"V94_STAGE={stage}"), None) for stage in STAGES}
    bp_line = stage_line["prearm_breakpoint_captured"]
    probe_phase_line = stage_line["precontinue_memory_probe_phase"]
    stopped_line = stage_line["stopped_at_scheduler"]
    if bp_line is None or probe_phase_line is None or stopped_line is None:
        raise StageProbeError("scheduler remote trace lacks breakpoint/stop phase markers")
    events: list[dict[str, Any]] = []
    sent: list[dict[str, Any]] = []
    received: list[dict[str, Any]] = []
    for line_no, line in enumerate(lines):
        match = _SEND_RE.search(line)
        if match:
            event = {"direction": "send", "line": line_no, "packet": _payload(match.group(1))}
            events.append(event); sent.append(event); continue
        match = _RECEIVE_RE.search(line)
        if match:
            event = {"direction": "receive", "line": line_no, "packet": _payload(match.group(1))}
            events.append(event); received.append(event)
    if not sent or not received or len(events) > 512 or len(sent) > 256 or len(received) > 256:
        raise StageProbeError("bounded authentic remote trace is missing or oversized")
    z1_events = [event for event in sent if event["packet"] == f"Z1,{SCHEDULER_RETURN:08x},4"]
    if len(z1_events) != 1:
        raise StageProbeError("trace must contain exactly one scheduler Z1 request")
    z1_event = z1_events[0]
    if not probe_phase_line < z1_event["line"] < stopped_line:
        raise StageProbeError("lazy scheduler Z1 insertion must follow probe phase and precede the stop")
    # GDB registers hbreak locally before the resume.  The remote Z1 is sent
    # lazily while GDB prepares the continue, so its acknowledgement is the
    # first received packet after Z1 and before the next outgoing packet.
    next_send = next((event for event in sent if event["line"] > z1_event["line"]), None)
    z1_replies = [event for event in received
                   if z1_event["line"] < event["line"] < (next_send["line"] if next_send else stopped_line)]
    if len(z1_replies) != 1 or z1_replies[0]["packet"] != "OK":
        raise StageProbeError("lazy scheduler Z1 request lacks its first exact OK acknowledgement")
    probes = []
    for event in sent:
        match = _MEMORY_PROBE_RE.fullmatch(event["packet"])
        if match:
            probes.append({**event, "kind": match.group("kind"),
                           "address": int(match.group("address"), 16),
                           "length": int(match.group("length"), 16)})
    scheduler_probes = [event for event in probes if event["address"] == SCHEDULER_RETURN and event["length"] == 4]
    if not scheduler_probes:
        raise StageProbeError("automatic scheduler x/m verification read is missing")
    if not any(event["kind"] == "m" for event in scheduler_probes):
        raise StageProbeError("automatic scheduler verification lacks an m read")
    for probe in scheduler_probes:
        if probe["line"] >= probe_phase_line:
            raise StageProbeError("automatic scheduler read occurred after the precontinue probe phase")
    m_probe = next(event for event in scheduler_probes if event["kind"] == "m")
    m_replies = [event for event in received if event["line"] > m_probe["line"] and event["line"] < probe_phase_line]
    if not any(re.fullmatch(r"[0-9a-fA-F]{8}", event["packet"]) for event in m_replies):
        raise StageProbeError("automatic scheduler m read lacks a four-byte reply")
    c_events = [event for event in sent if event["packet"] == "c"]
    if len(c_events) != 1:
        raise StageProbeError("remote trace must contain exactly one plain c packet")
    c_event = c_events[0]
    if c_event["line"] <= probe_phase_line or c_event["line"] >= stopped_line:
        raise StageProbeError("plain c is outside the scheduler continue phase")
    unexpected = [event for event in sent if
                  ((event["packet"].startswith(("c", "C")) and event["packet"] != "c") or
                   (event["packet"].startswith("vCont") and event["packet"] != "vCont?"))]
    if unexpected:
        raise StageProbeError(f"unexpected resume packet in scheduler trace: {unexpected}")
    after_c = [event for event in received if event["line"] > c_event["line"]]
    if not after_c or after_c[0]["packet"] == "":
        raise StageProbeError("plain c has no nonempty stop reply")
    if not after_c[0]["packet"].startswith("T05"):
        raise StageProbeError("plain c was not followed by T05")
    if after_c[0]["line"] >= stopped_line:
        raise StageProbeError("T05 arrived after stopped_at_scheduler marker")
    if len([event for event in after_c if event["packet"].startswith("T05")]) != 1:
        raise StageProbeError("scheduler continue trace contains multiple T05 stop replies")
    return {"sent_count": len(sent), "received_count": len(received), "event_count": len(events),
            "plain_continue_packet_index": sent.index(c_event), "plain_continue_line": c_event["line"],
            "scheduler_z1_ok": True, "scheduler_z1_line": z1_event["line"],
            "scheduler_z1_ack_line": z1_replies[0]["line"],
            "lazy_breakpoint_insertion": True, "automatic_memory_probes": scheduler_probes,
            "stop_reply_prefix": after_c[0]["packet"][:3], "stop_reply_line": after_c[0]["line"],
            "handshake_empty_reply_count": sum(event["packet"] == "" and event["line"] < c_event["line"] for event in received),
            "permitted_vcont_queries": [event["packet"] for event in sent if event["packet"] == "vCont?"],
            "events": events}


def parse_trace(text: str) -> dict[str, Any]:
    lines = text.splitlines()
    if any(sum(line == f"V94_STAGE={stage}" for line in lines) != 1 for stage in STAGES):
        raise StageProbeError("missing or duplicate V94 stage marker")
    positions = [next(i for i, line in enumerate(lines) if line == f"V94_STAGE={stage}") for stage in STAGES]
    if positions != sorted(positions):
        raise StageProbeError("V94 stage markers are out of order")
    prearm = _marker(lines, "V94_PREARM_BP_JSON=")
    if (isinstance(prearm.get("number"), bool) or not isinstance(prearm.get("number"), int) or prearm["number"] <= 0
            or not isinstance(prearm.get("location"), str) or "80390eb4" not in prearm["location"].lower()):
        raise StageProbeError("breakpoint acknowledgement is not typed/authored")
    stop = _marker(lines, "V94_STOP_JSON=")
    if stop.get("inferior_valid") is not True or stop.get("stopped") is not True or stop.get("boundary") != "scheduler":
        raise StageProbeError("scheduler resume/stop lacks valid stopped state")
    for key in ("inferior_num", "thread_num", "pc"):
        if isinstance(stop.get(key), bool) or not isinstance(stop.get(key), int):
            raise StageProbeError(f"scheduler resume/stop field {key} is not an integer")
    if stop["inferior_num"] <= 0 or stop["thread_num"] <= 0 or stop["pc"] != SCHEDULER_RETURN:
        raise StageProbeError("scheduler resume/stop identity or PC differs from authored boundary")
    deleted = _marker(lines, "V94_DELETE_JSON=")
    if deleted.get("number") != prearm["number"] or deleted.get("deleted") is not True:
        raise StageProbeError("captured breakpoint deletion is not exact")
    for key in ("other_numbers_before", "other_numbers_after"):
        values = deleted.get(key)
        if not isinstance(values, list) or any(isinstance(v, bool) or not isinstance(v, int) or v <= 0 for v in values) or values != sorted(values):
            raise StageProbeError("unrelated breakpoint inventory is not typed")
    if deleted["other_numbers_before"] != deleted["other_numbers_after"]:
        raise StageProbeError("unrelated breakpoint inventory changed")
    close = _marker(lines, "V94_TRANSPORT_DISCONNECT_JSON=")
    if close.get("command") != "disconnect" or close.get("transport_closed") is not True or close.get("connection_is_none") is not True or close.get("selected_thread_is_none") is not True or close.get("native_resume_observed") is not False:
        raise StageProbeError("transport close is not an observed disconnect")
    return {"status": "scheduler_stop_observed", "remote_packets": parse_remote_packets(text),
            "prearm_breakpoint": prearm, "stop": stop, "delete": deleted, "transport_close": close,
            "arm_or_read": False, "direct_memory_api": False,
            "automatic_memory_probe": True, "stage_order": list(STAGES)}


if __name__ == "__main__":
    raise SystemExit("analysis-only module; use focused tests")
