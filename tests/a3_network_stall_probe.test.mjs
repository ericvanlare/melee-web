import assert from 'node:assert/strict';
import test from 'node:test';
import {findPostCssStallWindow, resolvePostCssStallProbe,
  validatePostCssStallEvidence, runPostCssStallProbe} from '../scripts/a3_network_stall_probe.mjs';

function peer(cursor, resumeCount, {role = 'alpha', wait = 1, terminal = 0,
  protocolTerminal = null} = {}) {
  return {role, protocol_role: role, source_cursor: cursor, blocker: 'network_wait',
    network_wait: {active: wait, callbacks: 9, episodes: 4, resume_count: resumeCount},
    terminal: {kind: terminal, tick: 0}, menu_phase: 1, menu_running: 1,
    native_active: 1, runtime_error: null, runtime_owner_failure: null,
    peer_failure: null, protocol_terminal: protocolTerminal};
}

function completePolicyFixture() {
  const observations = [];
  for (let at = 10; at <= 150; at += 20) {
    observations.push({at_monotonic_ms: at,
      peers: {alpha: peer(80, 4, {role: 'alpha'}), beta: peer(79, 4, {role: 'beta'})},
      selected_direction: {queued_bytes: 64, in_flight_bytes: 0, pending_bytes: 64,
        bytes_enqueued: 128, bytes_released: 64},
      proxy_errors: 0, integrity_failure: false});
  }
  const evidence = {
    schema: 'melee-web-post-css-stall-probe-v1',
    outcome: 'EXERCISED_AND_RESUMED', elapsed_ms: 900,
    active_room_connection_count: 2, active_room_connection_ids: [5, 6],
    browser_path_verification: {verified: true, browser_websockets: {alpha: [{}], beta: [{}]}},
    proxy_before: {controlledFaults: [], lifecycle: {activeConnections: 2},
      connections: {active: [{connectionId: 5}, {connectionId: 6}]},
      diagnostics: {socketErrors: 0, integrityFailure: false}},
    proxy_after_stall: {controlledFaults: [{kind: 'stall', connectionId: 5,
      direction: 'clientToWorker'}], lifecycle: {cutConnections: 0, activeConnections: 2},
      connections: {active: [{connectionId: 5}, {connectionId: 6}]},
      diagnostics: {socketErrors: 0, integrityFailure: false}},
    selected_connection_id: 5, selected_connection_role: null,
    before: {peers: {alpha: peer(79, 3, {role: 'alpha', wait: 0}),
      beta: peer(78, 3, {role: 'beta', wait: 0})}},
    stall_receipt: {kind: 'stall', connectionId: 5, direction: 'clientToWorker',
      durationMs: 600, activatedNs: '1000000000', nextReleaseNs: '1600000000',
      queuedChunks: 0, queuedBytes: 0, inFlightBytesNotStalled: 0},
    observations,
    resumed: {peers: {alpha: peer(81, 4, {role: 'alpha', wait: 0}),
      beta: peer(80, 4, {role: 'beta', wait: 0})}},
    release_proof: {
      enqueue: {event: 'enqueue', connectionId: 5, direction: 'clientToWorker', sequence: 2,
        bytes: 64, enqueuedNs: '1100000000', scheduledReleaseNs: '1600000000'},
      release: {event: 'release', connectionId: 5, direction: 'clientToWorker', sequence: 2,
        bytes: 64, scheduledReleaseNs: '1600000000', writeCallbackNs: '1610000000'},
    },
  };
  evidence.held_window = findPostCssStallWindow(observations);
  return evidence;
}

test('stall probe admission is opt-in and constrained CSS only', () => {
  assert.equal(resolvePostCssStallProbe({enabled: false, profileName: 'moderate',
    relayUrl: 'wss://example.invalid', fullRoute: true}), null);
  assert.deepEqual(resolvePostCssStallProbe({enabled: true, profileName: 'constrained',
    simulationMode: 'simulated-network', fullRoute: false}), {
    durationMs: 600, capMs: 3500, minimumStableMs: 120, pollMs: 20,
    maximumPollGapMs: 50, direction: 'clientToWorker', profileName: 'constrained',
    resultPolicy: 'controlled-stall-probe',
  });
  assert.throws(() => resolvePostCssStallProbe({enabled: true, profileName: 'constrained',
    simulationMode: 'simulated-network', relayUrl: 'wss://example.invalid'}), /public relay/);
  assert.throws(() => resolvePostCssStallProbe({enabled: true, profileName: 'constrained',
    simulationMode: 'simulated-network', fullRoute: true}), /CSS-only/);
  assert.throws(() => resolvePostCssStallProbe({enabled: true, profileName: 'moderate',
    simulationMode: 'simulated-network'}), /constrained/);
  assert.throws(() => resolvePostCssStallProbe({enabled: true, profileName: 'constrained',
    simulationMode: 'local'}), /public relay/);
});

test('stall policy accepts only a sampled held queue, timed stall, release and native resume', () => {
  const evidence = completePolicyFixture();
  const result = validatePostCssStallEvidence(evidence);
  assert.equal(result.policy, 'controlled-post-css-stall-exercised');
  assert.equal(result.held_span_ms, 120);
  assert.equal(result.queued_bytes_peak, 64);
  assert.equal(result.result_is_not_clean_route_pass, true);
});

test('stall policy rejects a hold with no actual proxy queue bytes', () => {
  const evidence = completePolicyFixture();
  for (const row of evidence.observations) {
    row.selected_direction.queued_bytes = 0;
    row.selected_direction.pending_bytes = row.selected_direction.in_flight_bytes;
  }
  evidence.held_window = null;
  assert.throws(() => validatePostCssStallEvidence(evidence), /stable two-peer network-wait span/);
});

test('stall policy rejects missing or wrong controlled stall receipts', () => {
  const evidence = completePolicyFixture();
  evidence.stall_receipt = null;
  assert.throws(() => validatePostCssStallEvidence(evidence), /600 ms client-to-Worker/);
  const wrongDirection = completePolicyFixture();
  wrongDirection.stall_receipt.direction = 'workerToClient';
  assert.throws(() => validatePostCssStallEvidence(wrongDirection), /600 ms client-to-Worker/);
});

test('stall policy rejects short wait spans, terminal peers, and missing release proof', () => {
  const short = completePolicyFixture();
  short.observations.splice(6);
  short.held_window = findPostCssStallWindow(short.observations);
  assert.throws(() => validatePostCssStallEvidence(short), /stable two-peer network-wait span/);

  const terminal = completePolicyFixture();
  terminal.observations[3].peers.beta.terminal.kind = 3;
  assert.throws(() => validatePostCssStallEvidence(terminal), /terminal or proxy error/);

  const noRelease = completePolicyFixture();
  noRelease.release_proof = null;
  assert.throws(() => validatePostCssStallEvidence(noRelease), /enqueue and post-stall write-release/);
});

test('stall policy rejects a cursor that does not resume and advance on both peers', () => {
  const evidence = completePolicyFixture();
  evidence.resumed.peers.beta.source_cursor = evidence.held_window.cursors.beta;
  evidence.resumed.peers.beta.network_wait.resume_count = 3;
  assert.throws(() => validatePostCssStallEvidence(evidence), /beta did not advance/);
});


test('policy allows healthy pre-wait and recovered samples around the strict wait window', () => {
  const evidence = completePolicyFixture();
  evidence.observations.unshift({...structuredClone(evidence.observations[0]),
    at_monotonic_ms: 0, peers: structuredClone(evidence.before.peers)});
  evidence.observations.push({...structuredClone(evidence.observations.at(-1)),
    at_monotonic_ms: 200, peers: structuredClone(evidence.resumed.peers)});
  assert.equal(validatePostCssStallEvidence(evidence).held_span_ms, 120);
  const wrongReleaseSchema = completePolicyFixture();
  const release = wrongReleaseSchema.release_proof.release;
  release.releasedNs = release.writeCallbackNs;
  delete release.writeCallbackNs;
  assert.throws(() => validatePostCssStallEvidence(wrongReleaseSchema), /timestamps are malformed/);
});

test('actual probe loop accepts pre-wait and recovery and actual writeCallbackNs release schema', async () => {
  // Component-only controlled service fixture; these rows are not browser evidence.
  const start = performance.now();
  let receipt;
  let activated;
  const snapshot = () => {
    const elapsed = activated === undefined ? 0 : performance.now() - activated;
    const queue = elapsed >= 650 ? 0 : 64;
    return {schema: 'melee-web-tcp-test-proxy-snapshot-v1',
      lifecycle: {activeConnections: 2, cutConnections: 0},
      controlledFaults: receipt ? [receipt] : [],
      connections: {active: [5, 6].map(connectionId => ({connectionId,
        directions: {clientToWorker: {queuedBytes: queue, inFlightBytes: 0,
          pendingBytes: queue, bytesEnqueued: 128, bytesReleased: 128 - queue}}}))},
      diagnostics: {socketErrors: 0, integrityFailure: false,
        events: elapsed >= 650 ? [
          {event: 'enqueue', connectionId: 5, direction: 'clientToWorker', sequence: 2,
            bytes: 64, enqueuedNs: String(BigInt(receipt.activatedNs) + 1_000_000n),
            scheduledReleaseNs: receipt.nextReleaseNs},
          {event: 'release', connectionId: 5, direction: 'clientToWorker', sequence: 2,
            bytes: 64, scheduledReleaseNs: receipt.nextReleaseNs,
            writeCallbackNs: String(BigInt(receipt.nextReleaseNs) + 1_000_000n)},
        ] : []}};
  };
  const proxy = {snapshot, stallConnection(id, direction, durationMs) {
    activated = performance.now();
    const now = process.hrtime.bigint();
    receipt = {kind: 'stall', connectionId: id, direction, durationMs,
      activatedNs: String(now), nextReleaseNs: String(now + 600_000_000n),
      queuedChunks: 1, queuedBytes: 64, inFlightBytesNotStalled: 0};
    return receipt;
  }};
  let healthyPreWaitPolls = 0, recoveryPolls = 0;
  const readRoomState = async role => {
    const elapsed = activated === undefined ? -1 : performance.now() - activated;
    const waiting = elapsed >= 80 && elapsed < 650;
    const recovered = elapsed >= 650;
    if (elapsed >= 0 && elapsed < 80) ++healthyPreWaitPolls;
    if (recovered) ++recoveryPolls;
    const row = peer((role === 'alpha' ? 80 : 79) + (recovered ? 1 : 0),
      recovered ? 5 : waiting ? 4 : 3, {role, wait: waiting ? 1 : 0});
    return {menu_phase: row.menu_phase, menu_running: row.menu_running,
      native_status: {active: 1, cursor: row.source_cursor, terminal: row.terminal,
        blocker: waiting ? 'network_wait' : 'none', network_wait: row.network_wait},
      runtime_error: null, owner_health: {failure: null,
        peer: {failure: null, protocol: {role, terminal: null}}}};
  };
  const evidence = {};
  await runPostCssStallProbe({proxy, readRoomState, assertHealthy() {},
    remaining: () => 10_000 - (performance.now() - start),
    browserPathVerification: {verified: true}, evidence});
  assert(healthyPreWaitPolls > 0);
  assert(recoveryPolls > 0);
  assert.equal(evidence.outcome, 'EXERCISED_AND_RESUMED');
  assert.equal(evidence.release_proof.release.writeCallbackNs !== undefined, true);
  assert.equal(evidence.release_proof.release.releasedNs, undefined);
});
