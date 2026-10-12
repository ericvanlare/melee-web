"""Pure negative controls for the separate expected-transport-cut policy."""
from copy import deepcopy
import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from a3_expected_fault_auditor import audit_expected_cut
from run_network_simulation import validate_result

COUNT = 120
TRANSPORT_ERROR = "Error: Room relay WebSocket transport failed"
PROTOCOL_REASON = "transport receive failed: Room relay WebSocket transport failed"
BROWSER_FAILURE = "Error: Expected controlled transport cut observed; route remains FAIL"
RECEIPT = {"kind": "cut", "connectionId": 4, "activatedNs": "123456789",
           "scope": "one opaque owned TCP connection; no inferred player role"}
ENDPOINT = "ws://127.0.0.1:45678"
ROOM = "A" * 22
HEAD = "b" * 40


def make_records():
    records = []
    raw_rows = []
    for tick in range(COUNT):
        raw = tick.to_bytes(4, "little") + bytes(60)
        digest = hashlib.sha256(raw).hexdigest()
        raw_rows.append(list(raw))
        records.append({"source_tick": tick, "bytes": 64,
            "parsed": {"tick": tick}, "alpha_raw_hex": raw.hex(), "beta_raw_hex": raw.hex(),
            "sha256_alpha": digest, "sha256_beta": digest})
    return records, raw_rows


def pad_row(role, tick, buttons):
    raw = [0, 0, buttons, 0, 0, 0, 0, 0, 0, 0, 0]
    return {"source_cursor": tick, "input_tick": tick,
            "local_port": 0 if role == "alpha" else 1,
            "poll_serial": tick + 100, "bytes": raw}


def terminal_row(role, kind, reason):
    is_protocol = kind == 3
    endpoint_errors = ([{"role": role, "message": TRANSPORT_ERROR}] if is_protocol else [])
    peer_failure = TRANSPORT_ERROR if is_protocol else None
    protocol_terminal = {"kind": "protocol" if is_protocol else "disconnect", "reason": reason}
    protocol = {"terminal": protocol_terminal, "checksum_mismatches": [],
                "native_local_divergences": 0}
    peer = {"protocol": protocol, "endpointErrors": endpoint_errors,
            "endpointClosed": True, "failure": peer_failure,
            "cancelledOutboundSends": [], "localInputCapture": {
                "enabled": True, "mode": "live", "captures": [pad_row(role, 0, 1)],
                "cancelled_publications": []}}
    if role == "alpha":
        # A source-supported queue-fence cancellation: the captured row remains,
        # while this packet attempt never existed and no successful send is claimed.
        peer["localInputCapture"]["captures"].append(pad_row(role, 1, 0))
        peer["localInputCapture"]["cancelled_publications"].append({
            **pad_row(role, 1, 0), "disposition": "cancelled_before_publication",
            "reason": "remote_disconnect_before_queue_execution",
            "publication_scope": "no_state_packet_attempt"})
    native = {"blocker": "terminal", "terminal": {"kind": kind}, "active": 1, "cursor": 121}
    return {"native_status": native,
            "health": {"peer": {"protocol": {"checksum_mismatch_count": 0}}},
            "menu_phase": 1, "menu_running": 1,
            "snapshot": {"peer": peer, "checksums": []}}


def clean_connection(connection_id, selected=False):
    directions = {}
    for name in ("clientToWorker", "workerToClient"):
        enqueued = 100
        dropped = 20 if selected else 0
        released = enqueued - dropped
        directions[name] = {"bytesEnqueued": enqueued, "bytesReleased": released,
            "bytesDroppedOnCut": dropped, "bytesDroppedOnStop": 0,
            "bytesDroppedOnFailure": 0,
            "enqueuedSha256": "a" * 64,
            "releasedSha256": ("c" if selected else "a") * 64}
    return {"connectionId": connection_id, "clean": not selected,
            "reason": "cut" if selected else None, "hashesMatch": not selected,
            "directions": directions}


def make_fixture():
    records, raw_rows = make_records()
    checksum = {"compared_source_ticks": COUNT, "requested_minimum": COUNT,
        "freeze_before_leave": True, "exact_record_count_each": COUNT,
        "source_tick_range": [0, COUNT - 1], "records": records}
    browser_path = {"verified": True, "browser_websockets": {
        role: [{"url": f"{ENDPOINT}/v1/rooms/{ROOM}/socket"}]
        for role in ("alpha", "beta")}}
    profile = {name: {"cutAfterBytes": None, "stallAfterBytes": None,
                      "stallDurationMs": 0, "delayMs": 10, "jitterMs": 5}
               for name in ("clientToWorker", "workerToClient")}
    pre_proxy = {"schema": "melee-web-tcp-test-proxy-snapshot-v1",
        "target": {"host": "127.0.0.1", "port": 9000}, "profile": profile,
        "lifecycle": {"activeConnections": 2, "cutConnections": 0,
            "failedConnections": 0, "rejectedConnections": 0},
        "diagnostics": {"integrityFailure": False}, "controlledFaults": [],
        "connections": {"active": [{"connectionId": 4}, {"connectionId": 5}]}}
    capture_rows = {role: [pad_row(role, 0, 1)] for role in ("alpha", "beta")}
    pre_peers = {}
    for role, local_port in (("alpha", 0), ("beta", 1)):
        pre_peer = {"endpointClosed": False, "closed": False,
            "protocol": {"ready": True, "role": role, "local_port": local_port,
                         "checksum_mismatches": [], "native_local_divergences": 0},
            "localInputCapture": {"enabled": True, "mode": "live",
                "captures": capture_rows[role], "cancelled_publications": []},
            "cancelledOutboundSends": []}
        pre_peers[role] = {"menu_phase": 1, "menu_running": 1,
            "native_status": {"active": 1, "terminal": {"kind": 0}, "cursor": COUNT},
            "health": {"peer": {"protocol": {"checksum_mismatch_count": 0}}},
            "snapshot": {"peer": pre_peer, "checksums": raw_rows}}
    final_peers = {
        "alpha": terminal_row("alpha", 3, PROTOCOL_REASON),
        "beta": terminal_row("beta", 2, "4001: peer disconnected"),
    }
    first_by_role = {}
    for role, row in final_peers.items():
        peer = row["snapshot"]["peer"]
        protocol = peer["protocol"]
        first_by_role[role] = {"observation": {
            "native_status": row["native_status"],
            "protocol_terminal": protocol["terminal"],
            "endpoint_errors": peer["endpointErrors"],
            "endpoint_closed": peer["endpointClosed"],
            "peer_failure": peer["failure"]}}
    proxy_final = {
        "schema": "melee-web-tcp-test-proxy-snapshot-v1",
        "lifecycle": {"cleanShutdown": True, "listenerClosed": True,
            "activeConnections": 0, "activeTimers": 0, "acceptedConnections": 5,
            "closedConnections": 5, "rejectedConnections": 0, "cutConnections": 1,
            "failedConnections": 1, "stopClosedConnections": 0, "cleanConnections": 4},
        "dataPathPass": False, "controlledFaults": [RECEIPT],
        "directions": {name: {"bytesEnqueued": 500, "bytesReleased": 480,
            "bytesDroppedOnCut": 20, "bytesDroppedOnStop": 0,
            "bytesDroppedOnFailure": 0, "bytesCutAtThreshold": 0}
            for name in ("clientToWorker", "workerToClient")},
        "connections": {"active": [], "closed": [clean_connection(i) for i in (1, 2, 3)] +
            [clean_connection(4, selected=True), clean_connection(5)], "closedOmitted": 0},
        "diagnostics": {"integrityFailure": True, "socketErrors": 0}}
    cleanup = {"network_proxy_error": "Network proxy: socket or integrity failure",
        "worker": {"ready": True, "disposed": True,
            "exit": {"code": 0, "signal": None}, "processClose": True,
            "groupAlive": False, "fallbackUsed": False,
            "ownedTempPathsClean": True, "runtimeErrors": [], "structuredErrors": [],
            "stderrErrors": False, "cleanupEvents": []}}
    for role in ("alpha", "beta"):
        cleanup[role + "_native_network_cleanup"] = {"runtime_lockstep": {
            "mode": "normal", "after": {"close_mode": "normal",
                "peer": {"closed": True, "endpointClosed": True,
                    "nativePump": {"native_quiescence": "verified", "drain_failure": None},
                    "checksumConsumer": {"pending_batch": None}},
                "transport_cleanup": {"closed": True}}}}
        cleanup[role + "_context_close"] = True
        cleanup[role + "_unload"] = {"attempted": True}
        cleanup[role + "_browser_errors"] = []
    owner = {"schema": "melee-web-capture-process-owner-v3", "result": "fail",
        "capture_exit_code": 1, "cleanup_passed": True, "process_observation_passed": True,
        "process_observation_errors": [], "capture_process_group_absent": True,
        "server_process_group_absent": True, "browser_root_observed": True,
        "cdp_attribution_complete": True, "browser_process_inventory": [{
            "identity": {"pid": 101, "start": "first", "command": "chrome"}}],
        "browser_process_groups": [{"absent": True, "owned_processes_absent": True}],
        "cdp_attribution": [{"owned_parent_lineage_observed": True,
                             "absent_after_cleanup": True}]}
    for role in ("alpha", "beta"):
        cleanup.setdefault("browser_process_info_by_role", {})[role] = [{"id": 101 if role == "alpha" else 102}]
    browser = {"schema": "melee-web-a3-local-room-keyboard-v1",
        "decision": "FAIL", "first_error": BROWSER_FAILURE,
        "relay_mode": "simulated-network", "full_route": None,
        "source": {"head": HEAD},
        "room_worker": {"port": 9000},
        "invitation": {"room_id": ROOM, "invite_sha256": "d" * 64, "input_delay": 3},
        "css_keyboard": {role: {"local_port": port,
            "required_checksum_boundary_after_movement": 4,
            "first_non_neutral_sample": {"local_port": port, "input_tick": 0,
                                     "source_cursor": 0, "bytes": pad_row(role, 0, 1)["bytes"]},
            "geometry_delta_by_peer": {peer: {"x": 1.0, "y": 0.0}
                                        for peer in ("alpha", "beta")}}
            for role, port in (("alpha", 0), ("beta", 1))},
        "checksum_comparison": checksum,
        "network_simulation": {"configuration": {"mode": "simulated-network",
            "profile_name": "moderate"}, "endpoint": ENDPOINT,
            "browser_path": browser_path, "proxy_final": proxy_final},
        "browser_process_info_by_role": {role: [{"id": 101 if role == "alpha" else 102}]
                                          for role in ("alpha", "beta")},
        "leave_and_cleanup": cleanup}
    probe = {"schema": "melee-web-task13-controlled-cut-observation-v1",
        "selection": {"active_connection_ids": [4, 5], "selected_connection_id": 4,
            "scope": "one active opaque room-path TCP connection; no alpha/beta attribution by accept order"},
        "pre_fault": {"room_id": ROOM, "invite_sha256": "d" * 64,
            "browser_path": browser_path, "checksum_comparison": checksum,
            "source_cursors": {"alpha": COUNT, "beta": COUNT},
            "peers": pre_peers, "proxy": pre_proxy},
        "activation": RECEIPT,
        "proxy_immediately_after_activation": {"controlledFaults": [RECEIPT]},
        "post_fault": {"terminal_observed": True, "actions_after_cut": [],
            "resume_calls_after_cut": 0, "observations": [],
            "action_scope": "read-only terminal polling; final UI Stop/Leave and unload occur later as cleanup",
            "first_terminal_by_role": first_by_role, "final_peers": final_peers}}
    browser["network_simulation"]["controlled_cut_probe"] = probe
    return owner, browser


class ExpectedFaultAuditorTests(unittest.TestCase):
    def test_only_the_predeclared_transport_cut_with_retained_cleanup_is_confirmed(self):
        owner, browser = make_fixture()
        receipt = audit_expected_cut(owner, browser, expected_source_head=HEAD)
        self.assertEqual(receipt["decision"], "EXPECTED_TRANSPORT_CUT_CONFIRMED")
        self.assertEqual(receipt["route_decision"], "FAIL")
        self.assertEqual(receipt["pre_fault_raw64_prefix_count"], COUNT)
        # The clean-run policy remains independent and rejects this failed route.
        with self.assertRaises(ValueError):
            validate_result(1, owner, browser, False)

    def test_exact_retained_transport_error_can_be_preserved_without_claiming_unload_success(self):
        owner, browser = make_fixture()
        after = browser["leave_and_cleanup"]["alpha_native_network_cleanup"]["runtime_lockstep"]["after"]["peer"]
        after.update(failure=TRANSPORT_ERROR, endpointErrors=[{"role": "alpha", "message": TRANSPORT_ERROR}])
        runtime = browser["leave_and_cleanup"]["alpha_native_network_cleanup"]["runtime_lockstep"]
        runtime["error"] = TRANSPORT_ERROR
        browser["leave_and_cleanup"]["alpha_unload"]["error"] = TRANSPORT_ERROR
        receipt = audit_expected_cut(owner, browser, expected_source_head=HEAD)
        self.assertEqual(receipt["preserved_native_and_verifier_errors"]["alpha"], TRANSPORT_ERROR)
        self.assertEqual(receipt["preserved_native_and_verifier_errors"]["alpha_unload_error"], TRANSPORT_ERROR)

    def test_unrelated_or_unproved_cleanup_errors_refuse_despite_quiescent_after_snapshot(self):
        mutations = [
            lambda b: b["leave_and_cleanup"]["alpha_native_network_cleanup"]["runtime_lockstep"].update(error="Error: unrelated timeout"),
            lambda b: b["leave_and_cleanup"]["alpha_native_network_cleanup"]["runtime_lockstep"].update(error="AggregateError: Runtime lockstep session cleanup failed"),
            lambda b: b["leave_and_cleanup"]["alpha_native_network_cleanup"].update(errors=["Error: unrelated cleanup"]),
            lambda b: b["leave_and_cleanup"]["alpha_unload"].update(error="Error: unload timed out"),
            lambda b: b["leave_and_cleanup"].update(alpha_context_close_error="Error: context detach failed", alpha_context_closed=True),
            lambda b: b["leave_and_cleanup"]["alpha_native_network_cleanup"]["runtime_lockstep"].update(error=TRANSPORT_ERROR),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                owner, browser = make_fixture()
                mutate(browser)
                with self.assertRaises(ValueError):
                    audit_expected_cut(owner, browser, expected_source_head=HEAD)

    def test_literal_witness_and_both_checksum_boundaries_are_required(self):
        mutations = [
            lambda b: b["css_keyboard"]["alpha"]["first_non_neutral_sample"]["bytes"].__setitem__(2, 2),
            lambda b: b["css_keyboard"]["alpha"]["first_non_neutral_sample"].update(input_tick=500, source_cursor=500),
            lambda b: b["css_keyboard"]["alpha"].update(required_checksum_boundary_after_movement=COUNT+1),
            lambda b: b["css_keyboard"]["beta"].update(required_checksum_boundary_after_movement=3),
            lambda b: b["invitation"].update(input_delay=COUNT),
            lambda b: b["invitation"].update(input_delay=True),
            lambda b: b["network_simulation"]["controlled_cut_probe"]["pre_fault"]["peers"]["alpha"]["snapshot"]["peer"]["localInputCapture"]["captures"].append(pad_row("alpha", 0, 1)),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                owner, browser = make_fixture()
                mutate(browser)
                with self.assertRaises(ValueError):
                    audit_expected_cut(owner, browser, expected_source_head=HEAD)

    def test_first_failure_and_cut_boundary_corruptions_are_rejected(self):
        corruptions = [
            lambda owner, browser: browser["checksum_comparison"]["records"].pop(),
            lambda owner, browser: browser["checksum_comparison"]["records"][2].update(
                beta_raw_hex="f" * 128),
            lambda owner, browser: browser["css_keyboard"].pop("alpha"),
            lambda owner, browser: browser["network_simulation"]["controlled_cut_probe"][
                "selection"].update(selected_connection_id=5),
            lambda owner, browser: browser["network_simulation"]["proxy_final"][
                "controlledFaults"].append({**RECEIPT, "connectionId": 5}),
            lambda owner, browser: browser["network_simulation"]["controlled_cut_probe"][
                "post_fault"]["final_peers"]["alpha"]["snapshot"]["peer"].update(
                    failure="Error: malformed A2 packet"),
            lambda owner, browser: browser["network_simulation"]["controlled_cut_probe"][
                "post_fault"]["final_peers"]["alpha"]["health"]["peer"]["protocol"].update(
                    checksum_mismatch_count=1),
        ]
        for index, corrupt in enumerate(corruptions):
            with self.subTest(corruption=index):
                owner, browser = make_fixture()
                corrupt(owner, browser)
                with self.assertRaises(ValueError):
                    audit_expected_cut(owner, browser, expected_source_head=HEAD)

    def test_quiescence_resource_and_process_gates_are_not_waived(self):
        corruptions = [
            lambda owner, browser: browser["leave_and_cleanup"][
                "alpha_native_network_cleanup"]["runtime_lockstep"]["after"]["peer"][
                "nativePump"].update(native_quiescence="aborted-fatal"),
            lambda owner, browser: browser["leave_and_cleanup"][
                "beta_native_network_cleanup"]["runtime_lockstep"].update(mode="fatal"),
            lambda owner, browser: browser["leave_and_cleanup"][
                "alpha_native_network_cleanup"]["runtime_lockstep"]["after"]["peer"][
                "checksumConsumer"].update(pending_batch={"joined": False, "unjoined": True}),
            lambda owner, browser: browser["leave_and_cleanup"][
                "beta_native_network_cleanup"]["runtime_lockstep"]["after"]["peer"].update(
                endpointClosed=False),
            lambda owner, browser: browser["leave_and_cleanup"][
                "alpha_native_network_cleanup"]["runtime_lockstep"]["after"][
                "transport_cleanup"].update(closed=False),
            lambda owner, browser: browser["network_simulation"]["proxy_final"][
                "lifecycle"].update(activeTimers=1),
            lambda owner, browser: browser["network_simulation"]["proxy_final"][
                "connections"]["active"].append({"connectionId": 6}),
            lambda owner, browser: browser["leave_and_cleanup"]["worker"].update(disposed=False),
            lambda owner, browser: owner["browser_process_groups"][0].update(absent=False),
            lambda owner, browser: owner.update(capture_process_group_absent=False),
        ]
        for index, corrupt in enumerate(corruptions):
            with self.subTest(corruption=index):
                owner, browser = make_fixture()
                corrupt(owner, browser)
                with self.assertRaises(ValueError):
                    audit_expected_cut(owner, browser, expected_source_head=HEAD)


if __name__ == "__main__":
    unittest.main()
