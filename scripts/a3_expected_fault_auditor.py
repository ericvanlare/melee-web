#!/usr/bin/env python3
"""Separate policy for one explicitly controlled TCP-cut CSS failure.

The ordinary clean-route validator remains authoritative and unchanged. This
module can confirm only that the captured route failed for the predeclared cut
and that normal-mode native quiescence and every owned resource retired.
"""
from __future__ import annotations

import hashlib
import math
import re

BROWSER_SCHEMA = "melee-web-a3-local-room-keyboard-v1"
OWNER_SCHEMA = "melee-web-capture-process-owner-v3"
PROBE_SCHEMA = "melee-web-task13-controlled-cut-observation-v1"
PROXY_SCHEMA = "melee-web-tcp-test-proxy-snapshot-v1"
EXPECTED_BROWSER_ERROR = "Error: Expected controlled transport cut observed; route remains FAIL"
EXPECTED_TRANSPORT_ERROR = "Error: Room relay WebSocket transport failed"
EXPECTED_PROTOCOL_REASON = "transport receive failed: Room relay WebSocket transport failed"
ALLOWED_DISCONNECT_REASONS = {
    "1006: room relay closed",
    "4001: peer disconnected",
    "4001: peer transport failed",
}


def _need(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _object(value, label):
    _need(isinstance(value, dict), f"{label} must be an object")
    return value


def _array(value, label):
    _need(isinstance(value, list), f"{label} must be an array")
    return value


def _uint(value, label, maximum=2**53 - 1):
    _need(type(value) is int and 0 <= value <= maximum, f"{label} must be a bounded nonnegative integer")
    return value


def _first_line(value):
    if not isinstance(value, str) or not value:
        return None
    return value.splitlines()[0]


def _checksum_prefix(browser, probe):
    comparison = _object(browser.get("checksum_comparison"), "checksum_comparison")
    pre = _object(probe.get("pre_fault"), "pre_fault")
    _need(pre.get("checksum_comparison") == comparison,
          "pre-cut checksum receipt differs from the browser report")
    count = _uint(comparison.get("compared_source_ticks"), "compared_source_ticks")
    requested = _uint(comparison.get("requested_minimum"), "requested_minimum")
    _need(count >= 120 and requested >= 120 and
          comparison.get("freeze_before_leave") is True,
          "pre-cut raw checksum boundary is absent, short, or not frozen")
    records = _array(comparison.get("records"), "checksum records")
    _need(len(records) == count and comparison.get("exact_record_count_each") == count and
          comparison.get("source_tick_range") == [0, count - 1],
          "pre-cut checksum record count or source range is incomplete")
    for tick, row in enumerate(records):
        _object(row, f"checksum record {tick}")
        _need(row.get("source_tick") == tick and row.get("bytes") == 64 and
              _object(row.get("parsed"), f"checksum record {tick} parsed").get("tick") == tick,
              f"checksum prefix is not contiguous at source tick {tick}")
        left, right = row.get("alpha_raw_hex"), row.get("beta_raw_hex")
        _need(isinstance(left, str) and re.fullmatch(r"[0-9a-f]{128}", left) and left == right,
              f"raw 64-byte checksum differs or is malformed at source tick {tick}")
        raw = bytes.fromhex(left)
        _need(row.get("sha256_alpha") == hashlib.sha256(raw).hexdigest() and
              row.get("sha256_beta") == hashlib.sha256(raw).hexdigest(),
              f"raw checksum digest differs at source tick {tick}")
    return count, records


def _keyboard_witnesses(browser, probe, compared_count):
    delay = _uint(browser.get("invitation", {}).get("input_delay"), "invite input delay")
    rows = _object(browser.get("css_keyboard"), "CSS keyboard witnesses")
    for role, port in (("alpha", 0), ("beta", 1)):
        row = _object(rows.get(role), f"{role} CSS keyboard witness")
        sample = _object(row.get("first_non_neutral_sample"), f"{role} PAD witness")
        raw = _array(sample.get("bytes"), f"{role} PAD bytes")
        _need(row.get("local_port") == port and sample.get("local_port") == port and
              sample.get("input_tick") == sample.get("source_cursor") and
              type(sample.get("input_tick")) is int and len(raw) == 11 and
              all(type(byte) is int and 0 <= byte <= 255 for byte in raw) and
              (raw[2] != 0 or raw[3] != 0),
              f"{role} did not retain a literal non-neutral 11-byte keyboard PAD sample")
        tick = _uint(sample["input_tick"], f"{role} witness input tick")
        captured = _array(probe.get("pre_fault", {}).get("peers", {}).get(role, {}).get(
            "snapshot", {}).get("peer", {}).get("localInputCapture", {}).get("captures"),
            f"{role} pre-cut captured PAD rows")
        matches = [item for item in captured if isinstance(item, dict) and item.get("input_tick") == tick]
        _need(len(matches) == 1 and all(matches[0].get(key) == sample.get(key)
              for key in ("input_tick", "source_cursor", "local_port", "bytes")),
              f"{role} CSS witness differs from its literal pre-cut captured PAD row")
        derived = tick + delay + 1
        declared = _uint(row.get("required_checksum_boundary_after_movement"),
                         f"{role} declared movement checksum boundary")
        _need(declared >= derived and compared_count >= max(derived, declared),
              f"{role} pre-cut raw prefix does not cover the keyboard movement boundary")
        deltas = _object(row.get("geometry_delta_by_peer"), f"{role} cursor deltas")
        for observer in ("alpha", "beta"):
            delta = _object(deltas.get(observer), f"{role} {observer} cursor delta")
            dx, dy = delta.get("x"), delta.get("y")
            _need(isinstance(dx, (int, float)) and isinstance(dy, (int, float)) and
                  math.isfinite(dx) and math.isfinite(dy) and math.hypot(dx, dy) > 0.01,
                  f"{role} keyboard witness did not move the observed CSS cursor on {observer}")


def _pre_fault_peers(probe, records):
    peers = _object(probe.get("pre_fault", {}).get("peers"), "pre-fault peers")
    for role in ("alpha", "beta"):
        row = _object(peers.get(role), f"pre-fault {role} snapshot")
        native = _object(row.get("native_status"), f"pre-fault {role} native status")
        native_terminal = _object(native.get("terminal"), f"pre-fault {role} terminal status")
        peer = _object(row.get("snapshot", {}).get("peer"), f"pre-fault {role} peer")
        _need(row.get("menu_phase") == 1 and row.get("menu_running") == 1 and
              native.get("active") == 1 and native_terminal.get("kind") == 0 and
              peer.get("endpointClosed") is False,
              f"pre-fault {role} was not active, terminal-free original CSS")
        _need(peer.get("protocol", {}).get("ready") is True and
              row.get("health", {}).get("peer", {}).get("protocol", {}).get("checksum_mismatch_count") == 0 and
              peer.get("protocol", {}).get("checksum_mismatches") == [] and
              peer.get("protocol", {}).get("native_local_divergences") == 0,
              f"pre-fault {role} protocol/checksum state was not healthy")
        cursor = _uint(native.get("cursor"), f"pre-fault {role} source cursor")
        source_cursors = _object(probe.get("pre_fault", {}).get("source_cursors"), "pre-fault source cursors")
        _need(source_cursors.get(role) == cursor and cursor >= len(records),
              f"pre-fault {role} cursor does not cover the frozen checksum prefix")
        retained = _array(row.get("snapshot", {}).get("checksums"), f"pre-fault {role} checksum rows")
        _need(len(retained) >= len(records), f"pre-fault {role} did not retain the matched raw prefix")
        for tick, record in enumerate(records):
            actual = retained[tick]
            _need(isinstance(actual, list) and len(actual) == 64 and
                  bytes(actual).hex() == record[role + "_raw_hex"],
                  f"pre-fault {role} retained checksum differs at source tick {tick}")


def _browser_path(browser, probe):
    invitation = _object(browser.get("invitation"), "Host/Join invitation")
    room_id = invitation.get("room_id")
    _need(isinstance(room_id, str) and room_id and
          probe.get("pre_fault", {}).get("room_id") == room_id and
          isinstance(invitation.get("invite_sha256"), str) and
          re.fullmatch(r"[0-9a-f]{64}", invitation["invite_sha256"]) and
          probe.get("pre_fault", {}).get("invite_sha256") == invitation["invite_sha256"],
          "pre-fault Host invite and room identity are missing or changed")
    network = _object(browser.get("network_simulation"), "network simulation evidence")
    _need(network.get("configuration", {}).get("mode") == "simulated-network" and
          network.get("configuration", {}).get("profile_name") == "moderate",
          "cut case is not the fixed moderate simulated-network profile")
    path = _object(network.get("browser_path"), "browser WebSocket path evidence")
    _need(probe.get("pre_fault", {}).get("browser_path") == path,
          "pre-fault WebSocket path evidence differs from the browser report")
    endpoint = network.get("endpoint")
    expected = f"{endpoint}/v1/rooms/{room_id}/socket"
    _need(path.get("verified") is True, "actual browser room path was not verified")
    sockets = path.get("browser_websockets")
    for role in ("alpha", "beta"):
        rows = _array(sockets.get(role) if isinstance(sockets, dict) else None,
                      f"{role} observed room WebSockets")
        _need(len(rows) == 1 and _object(rows[0], f"{role} observed room WebSocket").get("url") == expected,
              f"{role} did not use the observed simulated room URL")


def _validate_pre_and_cut(browser, probe):
    pre_proxy = _object(probe.get("pre_fault", {}).get("proxy"), "pre-cut proxy snapshot")
    worker = _object(browser.get("room_worker"), "owned local Worker")
    target = _object(pre_proxy.get("target"), "proxy upstream target")
    profile = _object(pre_proxy.get("profile"), "pre-cut proxy profile")
    _need(target.get("host") == "127.0.0.1" and target.get("port") == worker.get("port") and
          all(isinstance(profile.get(name), dict) and
            profile[name].get("cutAfterBytes") is None and
            profile[name].get("stallAfterBytes") is None and
            profile[name].get("stallDurationMs") == 0
            for name in ("clientToWorker", "workerToClient")),
          "pre-cut proxy is not the loopback owned Worker path with no threshold fault")
    _need(pre_proxy.get("schema") == PROXY_SCHEMA and
          pre_proxy.get("lifecycle", {}).get("activeConnections") == 2 and
          pre_proxy.get("lifecycle", {}).get("cutConnections") == 0 and
          pre_proxy.get("lifecycle", {}).get("failedConnections") == 0 and
          pre_proxy.get("lifecycle", {}).get("rejectedConnections") == 0 and
          pre_proxy.get("diagnostics", {}).get("integrityFailure") is False and
          pre_proxy.get("controlledFaults") == [],
          "pre-cut proxy was not fault-free with exactly two active room sockets")
    active = _array(pre_proxy.get("connections", {}).get("active"), "active proxy connections")
    ids = [_uint(row.get("connectionId"), "active connection id") for row in active]
    _need(len(ids) == 2 and len(set(ids)) == 2, "pre-cut proxy active IDs are not exactly two unique opaque IDs")
    selection = _object(probe.get("selection"), "cut selection")
    selected = _uint(selection.get("selected_connection_id"), "selected connection id")
    _need(selection.get("scope") == "one active opaque room-path TCP connection; no alpha/beta attribution by accept order" and
          set(selection.get("active_connection_ids", [])) == set(ids) and selected in ids,
          "selected cut ID does not match the active role-agnostic proxy pair")
    receipt = _object(probe.get("activation"), "controlled cut receipt")
    _need(receipt.get("kind") == "cut" and _uint(receipt.get("connectionId"), "cut receipt connection id") == selected and
          receipt.get("scope") == "one opaque owned TCP connection; no inferred player role" and
          isinstance(receipt.get("activatedNs"), str) and receipt["activatedNs"].isdigit(),
          "controlled cut receipt does not match its selected active connection")
    immediate = _object(probe.get("proxy_immediately_after_activation"), "post-activation proxy snapshot")
    _need(immediate.get("controlledFaults") == [receipt],
          "proxy did not retain exactly the selected explicit cut receipt")
    _browser_path(browser, probe)
    return selected, receipt


def _validate_cancellation_receipts(pre_row, post_row, role):
    pre_peer = _object(pre_row.get("snapshot", {}).get("peer"), f"pre-cut {role} peer")
    post_peer = _object(post_row.get("snapshot", {}).get("peer"), f"post-cut {role} peer")
    pre_inputs = _object(pre_peer.get("localInputCapture"), f"pre-cut {role} local input capture")
    post_inputs = _object(post_peer.get("localInputCapture"), f"post-cut {role} local input capture")
    _need(pre_inputs.get("enabled") is True and pre_inputs.get("mode") == "live" and
          post_inputs.get("enabled") is True and post_inputs.get("mode") == "live",
          f"{role} did not retain live local-input capture around the cut")
    pre_cancelled = _array(pre_inputs.get("cancelled_publications"), f"pre-cut {role} cancellations")
    post_cancelled = _array(post_inputs.get("cancelled_publications"), f"post-cut {role} cancellations")
    pre_sends = _array(pre_peer.get("cancelledOutboundSends"), f"pre-cut {role} send cancellations")
    post_sends = _array(post_peer.get("cancelledOutboundSends"), f"post-cut {role} send cancellations")
    pre_captured = _array(pre_inputs.get("captures"), f"pre-cut {role} captured PAD rows")
    captured = _array(post_inputs.get("captures"), f"post-cut {role} captured PAD rows")
    _need(post_cancelled[:len(pre_cancelled)] == pre_cancelled and
          post_sends[:len(pre_sends)] == pre_sends and
          captured[:len(pre_captured)] == pre_captured,
          f"{role} cancellation/input history changed or lost pre-cut receipts")
    captured_by_tick = {}
    for index, row in enumerate(captured):
        _object(row, f"{role} captured PAD row {index}")
        tick = _uint(row.get("input_tick"), f"{role} input tick")
        _need(tick == index and row.get("source_cursor") == tick and
              row.get("local_port") == (0 if role == "alpha" else 1) and
              isinstance(row.get("bytes"), list) and len(row["bytes"]) == 11 and
              all(type(byte) is int and 0 <= byte <= 255 for byte in row["bytes"]),
              f"{role} captured input rows are malformed or non-contiguous")
        captured_by_tick[tick] = row
    local_input_ticks = set()
    for index, receipt in enumerate(post_sends):
        _object(receipt, f"{role} outbound cancellation {index}")
        _need(receipt.get("status") == "cancelled_unpublished" and
              receipt.get("packet_type") == "state" and
              receipt.get("publication_scope") in {"no_state_packet_attempt", "this_state_packet_attempt_only"} and
              receipt.get("reason") in {"peer_disconnected_before_state_send", "closed_transport_send"},
              f"{role} send cancellation is not a scoped state-packet disposition")
        context = receipt.get("context")
        _need(context in {"local_input", "native_progress", "checksum", "inbound_state_ack"},
              f"{role} cancellation has an unsupported publication context")
        for field in ("input_ticks", "packet_input_ticks", "checksum_ticks"):
            values = _array(receipt.get(field), f"{role} cancellation {field}")
            for tick in values:
                _uint(tick, f"{role} cancellation tick")
                if field == "packet_input_ticks":
                    _need(tick in captured_by_tick,
                          f"{role} state-packet attempt references an uncaptured local PAD tick")
        if context == "local_input":
            local_input_ticks.update(receipt["input_ticks"])
        if receipt.get("publication_scope") == "no_state_packet_attempt":
            _need(receipt.get("sequence") is None and receipt.get("packet_input_ticks") == [] and
                  receipt.get("checksum_ticks") == [],
                  f"{role} no-attempt cancellation claims packet content")
        else:
            _uint(receipt.get("sequence"), f"{role} attempted packet sequence")
    seen_cancelled_ticks = set()
    for index, row in enumerate(post_cancelled):
        _object(row, f"{role} local cancellation {index}")
        tick = _uint(row.get("input_tick"), f"{role} cancelled input tick")
        reason = row.get("reason")
        queue_fence = (reason == "remote_disconnect_before_queue_execution" and
                       row.get("disposition") == "cancelled_before_publication" and
                       row.get("publication_scope") == "no_state_packet_attempt")
        send_cancellation = (reason in {"peer_disconnected_before_state_send", "closed_transport_send"} and
                             row.get("disposition") in {"cancelled_before_publication", "cancelled_unpublished"} and
                             row.get("publication_scope") in {"no_state_packet_attempt", "this_state_packet_attempt_only"} and
                             tick in local_input_ticks)
        _need((queue_fence or send_cancellation) and tick not in seen_cancelled_ticks and
              tick in captured_by_tick and row.get("bytes") == captured_by_tick[tick].get("bytes"),
              f"{role} local cancellation lacks a matching retained PAD and scoped queue/send receipt")
        seen_cancelled_ticks.add(tick)
    return {"outbound_send_receipts": len(post_sends),
            "cancelled_local_input_rows": len(post_cancelled),
            "publication_scope": "one state-packet attempt only; no global input non-delivery claim"}


def _validate_terminals(probe):
    post = _object(probe.get("post_fault"), "post-cut observation")
    _need(post.get("terminal_observed") is True and post.get("actions_after_cut") == [] and
          post.get("resume_calls_after_cut") == 0 and
          post.get("action_scope") == "read-only terminal polling; final UI Stop/Leave and unload occur later as cleanup",
          "post-cut observation is incomplete or issued input/Resume during terminal polling")
    final = _object(post.get("final_peers"), "post-cut final peer observations")
    first = _object(post.get("first_terminal_by_role"), "first terminal observations")
    evidence = {}
    for role in ("alpha", "beta"):
        row = _object(final.get(role), f"post-cut {role} peer")
        peer = _object(row.get("snapshot", {}).get("peer"), f"post-cut {role} peer snapshot")
        native = _object(row.get("native_status"), f"post-cut {role} native status")
        terminal = _object(native.get("terminal"), f"post-cut {role} native terminal")
        protocol = _object(peer.get("protocol"), f"post-cut {role} protocol")
        kind = terminal.get("kind")
        _need(native.get("blocker") == "terminal" and kind in (2, 3) and
              peer.get("endpointClosed") is True,
              f"post-cut {role} lacks a source-supported terminal or closed endpoint")
        protocol_terminal = _object(protocol.get("terminal"), f"post-cut {role} protocol terminal")
        _need(protocol_terminal.get("kind") == ("disconnect" if kind == 2 else "protocol"),
              f"post-cut {role} protocol and native terminal kinds disagree")
        errors = _array(peer.get("endpointErrors"), f"post-cut {role} endpoint errors")
        for error in errors:
            _object(error, f"post-cut {role} endpoint error")
            _need(error.get("role") == role and _first_line(error.get("message")) == EXPECTED_TRANSPORT_ERROR,
                  f"post-cut {role} contains an unrelated endpoint error")
        reason = protocol_terminal.get("reason")
        if kind == 3:
            _need(reason == EXPECTED_PROTOCOL_REASON and
                  any(error.get("role") == role and
                      _first_line(error.get("message")) == EXPECTED_TRANSPORT_ERROR for error in errors) and
                  _first_line(peer.get("failure")) == EXPECTED_TRANSPORT_ERROR,
                  f"post-cut {role} protocol terminal is not rooted in the exact endpoint transport failure")
        else:
            _need(reason in ALLOWED_DISCONNECT_REASONS,
                  f"post-cut {role} disconnect reason is not a source-observed close from this cut")
            _need(peer.get("failure") is None or
                  _first_line(peer.get("failure")) == EXPECTED_TRANSPORT_ERROR,
                  f"post-cut {role} disconnect carries an unrelated first failure")
        health_protocol = _object(row.get("health", {}).get("peer", {}).get("protocol"),
                                  f"post-cut {role} protocol health")
        _need(health_protocol.get("checksum_mismatch_count") == 0 and
              protocol.get("checksum_mismatches") == [] and
              protocol.get("native_local_divergences") == 0,
              f"post-cut {role} reported checksum or native divergence")
        first_row = _object(first.get(role), f"first post-cut {role} terminal")
        first_observation = _object(first_row.get("observation"), f"first post-cut {role} state")
        first_native = _object(first_observation.get("native_status"), f"first post-cut {role} native status")
        first_protocol = _object(first_observation.get("protocol_terminal"),
                                 f"first post-cut {role} protocol terminal")
        first_errors = _array(first_observation.get("endpoint_errors"),
                              f"first post-cut {role} endpoint errors")
        _need(first_native.get("blocker") == "terminal" and
              first_native.get("terminal", {}).get("kind") == kind and
              first_protocol.get("kind") == protocol_terminal.get("kind") and
              first_protocol.get("reason") == protocol_terminal.get("reason"),
              f"first post-cut {role} terminal receipt is missing or changed")
        if kind == 3:
            _need(first_protocol.get("reason") == EXPECTED_PROTOCOL_REASON and
                  any(error.get("role") == role and
                      _first_line(error.get("message")) == EXPECTED_TRANSPORT_ERROR for error in first_errors) and
                  _first_line(first_observation.get("peer_failure")) == EXPECTED_TRANSPORT_ERROR,
                  f"first {role} protocol terminal did not retain its exact endpoint transport cause")
        cancellation = _validate_cancellation_receipts(
            _object(probe.get("pre_fault", {}).get("peers", {}).get(role), f"pre-cut {role}"), row, role)
        evidence[role] = {"native_terminal": terminal, "protocol_terminal": protocol_terminal,
                          "endpoint_errors": errors, "cancellation_receipts": cancellation,
                          "first_observation": first_observation,
                          "first_failure": peer.get("failure")}
    direct_transport_roles = [role for role, row in evidence.items()
        if (row["native_terminal"].get("kind") == 3 and
            row["first_observation"].get("protocol_terminal", {}).get("reason") == EXPECTED_PROTOCOL_REASON) or
        row["first_observation"].get("protocol_terminal", {}).get("reason") == "1006: room relay closed"]
    _need(direct_transport_roles,
          "Worker 4001 close reasons alone do not prove the browser-side controlled transport cut")
    for role, row in evidence.items():
        if row["protocol_terminal"].get("reason", "").startswith("4001:"):
            _need(any(other != role for other in direct_transport_roles),
                  f"{role} Worker 4001 terminal has no paired direct transport-failure observation")
    return evidence


def _cut_cleanup_error(value, peer, label):
    # A serialized AggregateError loses its children; its title is not causal proof.
    # Refuse that unknown rather than treating every cleanup failure as intentional.
    _need(isinstance(value, str) and _first_line(value) == EXPECTED_TRANSPORT_ERROR and
          _first_line(peer.get("failure")) == EXPECTED_TRANSPORT_ERROR and
          any(isinstance(row, dict) and _first_line(row.get("message")) == EXPECTED_TRANSPORT_ERROR
              for row in peer.get("endpointErrors", [])),
          f"{label} is not an independently retained exact cut transport cause")


# This is the generated loader inspected for the one-file package contract below.
# A different loader needs a new source review; a matching URL is not authority.
REVIEWED_PACKAGE_LOADER_SHA256 = "170c03c7ba2a92f6e961e233b0f783099073177de85c44ded16c2a687fefefa5"
PACKAGE_DIAGNOSTIC_CLASSIFICATION = "PREEXISTING_PACKAGE_REQUEST_ABORT_WITH_VERIFIED_READY_FS_ARTIFACT"


def _preexisting_package_diagnostic(browser, role, errors, runtime_files, runtime_url):
    """Classify immutable pre-cut evidence, without calling the aborted request successful."""
    _need(runtime_url == "http://127.0.0.1:8787/runtime.html" and
          browser.get("browser", {}).get("url") == runtime_url,
          "package diagnostic lacks the runner's admitted runtime URL")
    expected_url = runtime_url.removesuffix("runtime.html") + "gameplay_menu_browser.data"
    runtime_files = _object(runtime_files, "trusted served runtime identities")
    package = _object(runtime_files.get("gameplay_menu_browser.data"), "trusted data package identity")
    loader = _object(runtime_files.get("gameplay_menu_browser.js"), "trusted loader identity")
    size = _uint(package.get("bytes"), "trusted package bytes", maximum=8 * 1024 * 1024)
    _need(size > 0 and isinstance(package.get("sha256"), str) and
          re.fullmatch(r"[0-9a-f]{64}", package["sha256"]) is not None and
          loader.get("sha256") == REVIEWED_PACKAGE_LOADER_SHA256 and
          _uint(loader.get("bytes"), "trusted loader bytes") > 0,
          "package diagnostic lacks the source-reviewed loader/package identities")
    before = _object(browser.get("profiles", {}).get(role, {}).get(
        "data_package_diagnostics_before_cut"), f"{role} pre-cut package diagnostic")
    _need(set(before) == {"lifecycle", "seed"}, "unknown pre-cut package diagnostic fields")
    lifecycle = _object(before.get("lifecycle"), "pre-cut package lifecycle")
    final = _object(browser.get("leave_and_cleanup", {}).get(
        role + "_data_package_diagnostics_after_context_close"), "retired package lifecycle")
    lifecycle_keys = {"schema", "expected_url", "requests", "errors", "overflow", "finished",
                      "pending_response_bodies", "scope"}
    for row, finished in ((lifecycle, False), (final, True)):
        _need(set(row) == lifecycle_keys and
              row.get("schema") == "melee-web-task13-data-package-lifecycle-v1" and
              row.get("expected_url") == expected_url and row.get("errors") == [] and
              row.get("overflow") is False and row.get("finished") is finished and
              type(row.get("pending_response_bodies")) is int and row["pending_response_bodies"] == 0 and
              row.get("scope") == "passive request identity/timing and response body hash; no abort/fetch behavior change",
              "package lifecycle is incomplete, malformed or contains unknown diagnostics")
    requests = _array(lifecycle.get("requests"), "pre-cut package requests")
    _need(len(requests) == 1 and final.get("requests") == requests,
          "package request inventory changed after the pre-cut snapshot")
    request = _object(requests[0], "pre-cut package request")
    _need(set(request) == {"request_id", "identity_scope", "url", "method", "resource_type",
                          "requested_at_monotonic_ms", "response", "finished_at_monotonic_ms",
                          "failure", "failed_at_monotonic_ms", "browser_timing"} and
          request.get("request_id") == "page-request-1" and
          request.get("identity_scope") == "actual Playwright Request object within this profile" and
          request.get("url") == expected_url and request.get("method") == "GET" and
          request.get("resource_type") == "fetch" and request.get("failure") == "net::ERR_ABORTED" and
          request.get("finished_at_monotonic_ms") is None,
          "package request is not the narrowly observed pre-cut abort")
    response = _object(request.get("response"), "package response diagnostic")
    _need(set(response) == {"status", "observed_at_monotonic_ms", "content_length", "body", "body_error"} and
          type(response.get("status")) is int and response["status"] == 200 and
          response.get("content_length") == str(size), "package response identity differs")
    stamps = [request.get("requested_at_monotonic_ms"), response.get("observed_at_monotonic_ms"),
              request.get("failed_at_monotonic_ms")]
    _need(all(type(value) in (int, float) and math.isfinite(value) and value >= 0 for value in stamps) and
          stamps[0] <= stamps[1] <= stamps[2], "package request timestamps are invalid or unordered")
    _object(request.get("browser_timing"), "raw browser request timing")
    if response.get("body") is None:
        _need(isinstance(response.get("body_error"), str) and
              _first_line(response["body_error"]) ==
              "response.body: Protocol error (Network.getResponseBody): No data found for resource with given identifier",
              "unavailable package body has an unknown diagnostic")
        body_observation = "unavailable_with_retained_CDP_NoData_diagnostic"
    else:
        _need(response.get("body") == package and response.get("body_error") is None,
              "observed package response body differs from trusted artifact")
        body_observation = "observed_exact_trusted_artifact"
    seed = _object(before.get("seed"), "pre-cut ready FS package observation")
    _need(set(seed) == {"schema", "path", "bytes", "sha256", "observed_at_page_monotonic_ms",
                       "called_run", "preload_results", "data_file_downloads",
                       "expected_data_file_downloads", "native_phase", "runtime_error"} and
          seed.get("schema") == "melee-web-task13-preloaded-seed-observation-v1" and
          seed.get("path") == "/initial_pipeline_cache.db" and
          type(seed.get("bytes")) is int and seed["bytes"] == size and
          seed.get("sha256") == package["sha256"] and seed.get("runtime_error") is None and
          type(seed.get("expected_data_file_downloads")) is int and seed["expected_data_file_downloads"] == 1 and
          seed.get("preload_results") == {"gameplay_menu_browser.data": {"fromCache": False}} and
          seed.get("data_file_downloads") == {expected_url: {"loaded": size, "total": size}} and
          type(seed.get("called_run")) is bool and type(seed.get("native_phase")) is int and seed["native_phase"] >= 0 and
          type(seed.get("observed_at_page_monotonic_ms")) in (int, float) and
          math.isfinite(seed["observed_at_page_monotonic_ms"]) and seed["observed_at_page_monotonic_ms"] >= 0,
          "ready FS package or source loader counters differ from trusted artifact")
    expected_error = {"kind": "requestfailed", "method": "GET", "url": expected_url,
                      "failure": "net::ERR_ABORTED"}
    _need(errors == [expected_error], "package browser diagnostic multiplicity or fields differ")
    return {"classification": PACKAGE_DIAGNOSTIC_CLASSIFICATION, "request_id": request["request_id"],
            "request": request, "ready_fs_observation": seed,
            "trusted_runtime_files": {"gameplay_menu_browser.data": package, "gameplay_menu_browser.js": loader},
            "response_body_observation": body_observation,
            "scope": "immutable diagnostic already present before cut; exact ready FS artifact independently verified; not requestfinished, benign transport, fallback fetch or solved Chrome cause"}


def _validate_cleanup(browser, owner, selected_id, receipt, runtime_files=None, runtime_url=None):
    leave = _object(browser.get("leave_and_cleanup"), "browser cleanup report")
    preserved_errors = {}
    preexisting_package_diagnostics = {}
    for role in ("alpha", "beta"):
        cleanup = _object(leave.get(role + "_native_network_cleanup"), f"{role} native cleanup")
        runtime = _object(cleanup.get("runtime_lockstep"), f"{role} runtime cleanup")
        after = _object(runtime.get("after"), f"{role} runtime after snapshot")
        peer = _object(after.get("peer"), f"{role} native peer after snapshot")
        pump = _object(peer.get("nativePump"), f"{role} native pump after snapshot")
        transport = _object(after.get("transport_cleanup"), f"{role} transport cleanup")
        consumer = _object(peer.get("checksumConsumer"), f"{role} checksum consumer cleanup")
        pending = consumer.get("pending_batch")
        consumer_joined = pending is None or (
            isinstance(pending, dict) and pending.get("joined") is True and
            pending.get("unjoined") is False and
            (pending.get("callback_started") is not True or pending.get("callback_settled") is True))
        _need(runtime.get("mode") == "normal" and after.get("close_mode") == "normal" and
              pump.get("native_quiescence") == "verified" and
              peer.get("closed") is True and peer.get("endpointClosed") is True and
              transport.get("closed") is True and pump.get("drain_failure") is None and
              consumer_joined,
              f"{role} lacks normal-mode verified native quiescence, closed endpoint/transport, or joined consumer")
        if runtime.get("error"):
            _cut_cleanup_error(runtime["error"], peer, f"{role} runtime cleanup error")
            preserved_errors[role] = runtime["error"]
        if cleanup.get("errors"):
            for error in _array(cleanup["errors"], f"{role} cleanup errors"):
                _cut_cleanup_error(error, peer, f"{role} cleanup error")
            preserved_errors[role + "_cleanup_errors"] = cleanup["errors"]
        unload = leave.get(role + "_unload")
        if isinstance(unload, dict) and unload.get("error"):
            _cut_cleanup_error(unload["error"], peer, f"{role} unload error")
            preserved_errors[role + "_unload_error"] = unload["error"]
        _need(leave.get(role + "_context_close") is True or
              leave.get(role + "_context_closed") is True,
              f"{role} persistent Chrome context did not close")
        _need(not leave.get(role + "_context_close_error"),
              f"{role} Chrome context close has an unexplained cleanup failure")
        browser_processes = _array(browser.get("browser_process_info_by_role", {}).get(role),
                                   f"{role} browser process inventory")
        _need(browser_processes and all(isinstance(row, dict) and
              type(row.get("id")) is int and row["id"] > 0 for row in browser_processes),
              f"{role} browser process inventory is missing")
        browser_errors = _array(leave.get(role + "_browser_errors"), f"{role} browser errors")
        expected_ws = f"{browser.get('network_simulation', {}).get('endpoint')}/v1/rooms/{browser.get('invitation', {}).get('room_id')}/socket"
        package_errors = [error for error in browser_errors if isinstance(error, dict) and
                          isinstance(error.get("url"), str) and
                          error["url"].endswith("/gameplay_menu_browser.data")]
        if package_errors:
            preexisting_package_diagnostics[role] = _preexisting_package_diagnostic(
                browser, role, package_errors, runtime_files, runtime_url)
        for error in browser_errors:
            if error in package_errors:
                continue
            _object(error, f"{role} browser error")
            allowed_request_failure = (error.get("kind") == "requestfailed" and
                error.get("method") == "GET" and error.get("url") == expected_ws and
                error.get("failure") in {"net::ERR_CONNECTION_RESET", "net::ERR_CONNECTION_CLOSED",
                                          "net::ERR_CONNECTION_ABORTED"})
            _need(allowed_request_failure,
                  f"{role} recorded an unrelated fatal browser error during the cut route")
        if browser_errors:
            preserved_errors[role + "_browser_errors"] = browser_errors
    worker = _object(leave.get("worker"), "owned Worker cleanup")
    _need(worker.get("ready") is True and worker.get("disposed") is True and
          worker.get("exit", {}).get("code") == 0 and
          worker.get("exit", {}).get("signal") is None and worker.get("processClose") and
          worker.get("groupAlive") is False and worker.get("fallbackUsed") is False and
          worker.get("ownedTempPathsClean") is True and worker.get("runtimeErrors") == [] and
          worker.get("structuredErrors") == [] and worker.get("stderrErrors") is False and
          not any(isinstance(row, dict) and row.get("error") for row in worker.get("cleanupEvents", [])),
          "owned Worker disposal or process-group cleanup was not verified")
    proxy = _object(browser.get("network_simulation", {}).get("proxy_final"), "final proxy snapshot")
    life = _object(proxy.get("lifecycle"), "final proxy lifecycle")
    _need(proxy.get("schema") == PROXY_SCHEMA and life.get("cleanShutdown") is True and
          life.get("listenerClosed") is True and life.get("activeConnections") == 0 and
          life.get("activeTimers") == 0 and proxy.get("connections", {}).get("active") == [] and
          life.get("acceptedConnections") == life.get("closedConnections") and
          life.get("rejectedConnections") == 0 and life.get("cutConnections") == 1 and
          life.get("failedConnections") == 1 and life.get("stopClosedConnections") == 0 and
          life.get("cleanConnections") == life.get("closedConnections") - 1 and
          proxy.get("connections", {}).get("closedOmitted") == 0 and
          proxy.get("dataPathPass") is False and
          proxy.get("diagnostics", {}).get("integrityFailure") is True and
          proxy.get("diagnostics", {}).get("socketErrors") == 0,
          "final proxy did not retain one expected cut and fully retire its owned sockets/listener/timers")
    controlled = _array(proxy.get("controlledFaults"), "final controlled faults")
    _need(controlled == [receipt], "final proxy contains a missing, mismatched, or extra controlled fault")
    closed = _array(proxy.get("connections", {}).get("closed"), "closed proxy connections")
    _need(len(closed) == life.get("closedConnections") == life.get("acceptedConnections"),
          "final proxy connection receipts are incomplete")
    connection_ids = set()
    cut_rows = []
    cut_sums = {name: 0 for name in ("clientToWorker", "workerToClient")}
    aggregate_sums = {name: {"bytesEnqueued": 0, "bytesReleased": 0,
                             "bytesDroppedOnCut": 0, "bytesDroppedOnStop": 0,
                             "bytesDroppedOnFailure": 0} for name in cut_sums}
    for connection in closed:
        _object(connection, "closed proxy connection")
        connection_id = _uint(connection.get("connectionId"), "closed connection id")
        _need(connection_id not in connection_ids, "proxy duplicated a closed connection ID")
        connection_ids.add(connection_id)
        is_cut = connection_id == selected_id
        _need((connection.get("reason") == "cut" and connection.get("clean") is False)
              if is_cut else
              (not connection.get("reason") and connection.get("clean") is True and
               connection.get("hashesMatch") is True),
              "proxy contains an unexplained failed or cut connection")
        if is_cut:
            cut_rows.append(connection)
        for name in cut_sums:
            direction = _object(connection.get("directions", {}).get(name), f"{name} connection counters")
            for field in aggregate_sums[name]:
                value = _uint(direction.get(field), f"{name} {field}")
                aggregate_sums[name][field] += value
            if is_cut:
                enqueued = _uint(direction.get("bytesEnqueued"), f"{name} cut enqueued bytes")
                released = _uint(direction.get("bytesReleased"), f"{name} cut released bytes")
                dropped = _uint(direction.get("bytesDroppedOnCut"), f"{name} cut dropped bytes")
                _need(enqueued == released + dropped and
                      direction.get("bytesDroppedOnStop") == 0 and
                      direction.get("bytesDroppedOnFailure") == 0,
                      "selected cut socket has unreconciled or misattributed dropped bytes")
                cut_sums[name] += dropped
            else:
                _need(direction.get("bytesDroppedOnCut") == 0 and
                      direction.get("bytesDroppedOnStop") == 0 and
                      direction.get("bytesDroppedOnFailure") == 0 and
                      direction.get("bytesEnqueued") == direction.get("bytesReleased") and
                      direction.get("enqueuedSha256") == direction.get("releasedSha256"),
                      "nonselected proxy connection lost or changed bytes")
    _need(len(cut_rows) == 1 and cut_rows[0].get("connectionId") == selected_id,
          "final proxy did not close exactly the selected connection as a cut")
    for name, sums in aggregate_sums.items():
        aggregate = _object(proxy.get("directions", {}).get(name), f"aggregate {name} counters")
        _need(all(aggregate.get(field) == value for field, value in sums.items()) and
              aggregate.get("bytesCutAtThreshold") == 0 and
              aggregate.get("bytesDroppedOnStop") == 0 and
              aggregate.get("bytesDroppedOnFailure") == 0 and
              aggregate.get("bytesDroppedOnCut") == cut_sums[name],
              f"final {name} drop counters do not reconcile to per-connection receipts")
    clean_error = leave.get("network_proxy_error")
    _need(isinstance(clean_error, str) and "Network proxy:" in clean_error,
          "unchanged clean proxy verifier did not retain its intentional-cut rejection")
    preserved_errors["clean_proxy_verifier"] = clean_error

    _need(owner.get("schema") == OWNER_SCHEMA and owner.get("result") == "fail" and
          type(owner.get("capture_exit_code")) is int and owner["capture_exit_code"] != 0 and
          owner.get("cleanup_passed") is True and owner.get("process_observation_passed") is True and
          owner.get("process_observation_errors") == [] and owner.get("capture_process_group_absent") is True and
          owner.get("server_process_group_absent") is True and owner.get("browser_root_observed") is True and
          owner.get("cdp_attribution_complete") is True and not owner.get("owner_deadline_expired") and
          not owner.get("owner_error"),
          "capture owner did not preserve a failed route while proving cleanup and process observation")
    inventory = _array(owner.get("browser_process_inventory"), "owned browser process inventory")
    _need(inventory, "capture owner observed no owned browser process")
    groups = _array(owner.get("browser_process_groups"), "browser process groups")
    _need(groups and all(row.get("absent") is True and row.get("owned_processes_absent") is True for row in groups),
          "an owned browser process group remains or lacks an absence observation")
    cdp = _array(owner.get("cdp_attribution"), "CDP process attribution")
    _need(cdp and all(row.get("owned_parent_lineage_observed") is True and
                       row.get("absent_after_cleanup") is True for row in cdp),
          "owned Chrome process attribution or cleanup is incomplete")
    return {"normal_native_quiescence_and_endpoints_verified": True,
            "proxy_listener_sockets_timers_retired": True,
            "worker_and_owned_processes_absent": True,
            "preserved_errors": preserved_errors,
            "preexisting_package_diagnostics": preexisting_package_diagnostics}


def audit_expected_cut(owner, browser, expected_source_head=None, *,
                       expected_runtime_files=None, expected_runtime_url=None):
    """Raise ValueError for any missing/unexpected evidence; otherwise return a receipt."""
    _object(owner, "capture owner report")
    _object(browser, "browser report")
    _need(browser.get("schema") == BROWSER_SCHEMA, "unknown browser report schema")
    _need(browser.get("relay_mode") == "simulated-network" and browser.get("full_route") is None,
          "expected cut auditor accepts only the simulated CSS route")
    _need(browser.get("decision") == "FAIL" and browser.get("first_error") is not None and
          _first_line(browser.get("first_error")) == EXPECTED_BROWSER_ERROR,
          "browser route did not retain the explicit expected cut failure")
    probe = _object(browser.get("network_simulation", {}).get("controlled_cut_probe"), "controlled cut probe")
    _need(probe.get("schema") == PROBE_SCHEMA, "unknown controlled cut probe schema")
    count, records = _checksum_prefix(browser, probe)
    _keyboard_witnesses(browser, probe, count)
    _pre_fault_peers(probe, records)
    selected_id, receipt = _validate_pre_and_cut(browser, probe)
    terminal_evidence = _validate_terminals(probe)
    if expected_source_head is not None:
        _need(browser.get("source", {}).get("head") == expected_source_head,
              "browser report source head differs from the runner's frozen source")
    cleanup = _validate_cleanup(browser, owner, selected_id, receipt,
                                expected_runtime_files, expected_runtime_url)
    return {"schema": "melee-web-task13-expected-transport-cut-audit-v1",
            "decision": "EXPECTED_TRANSPORT_CUT_CONFIRMED",
            "route_decision": "FAIL",
            "source_head": browser.get("source", {}).get("head"),
            "room_id": browser.get("invitation", {}).get("room_id"),
            "pre_fault_raw64_prefix_count": count,
            "controlled_cut": {"receipt": receipt, "selected_connection_id": selected_id,
                               "scope": probe["selection"]["scope"]},
            "terminal_evidence": terminal_evidence,
            "cleanup": cleanup,
            "preserved_native_and_verifier_errors": cleanup["preserved_errors"],
            "limitations": ["the cut route failed and is not a clean route pass",
                            "raw64 prefix comparison is pre-fault only",
                            "no post-cut input, Resume, gameplay continuation, or same-page recovery is claimed",
                            "cancellation receipts describe state-packet attempts, not global input non-delivery"]}
