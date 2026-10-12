import {performance} from 'node:perf_hooks';

export const POST_CSS_STALL_PROBE = Object.freeze({
  durationMs: 600,
  capMs: 3500,
  minimumStableMs: 120,
  pollMs: 20,
  maximumPollGapMs: 50,
  direction: 'clientToWorker',
});

function requireValue(condition, message) {
  if (!condition) throw Error('Post-CSS stall probe: ' + message);
}

/** Admission is opt-in and happens before any Worker, proxy, browser, or output startup. */
export function resolvePostCssStallProbe({enabled = false, profileName, simulationMode,
  relayUrl, relayPreflight, fullRoute = false} = {}) {
  if (!enabled) return null;
  requireValue(relayUrl === undefined && relayPreflight === undefined &&
    simulationMode === 'simulated-network',
  'requires the owned simulated-network mode and cannot use a public relay');
  requireValue(profileName === 'constrained',
    'requires the fixed constrained 64 KiB/s profile');
  requireValue(fullRoute === false,
    'is a CSS-only probe and cannot be combined with --full-route');
  return {...POST_CSS_STALL_PROBE, profileName, resultPolicy: 'controlled-stall-probe'};
}

function peerEvidence(state, role) {
  const native = state?.native_status;
  const wait = native?.network_wait;
  return {
    role,
    source_cursor: Number.isSafeInteger(native?.cursor) ? native.cursor : null,
    blocker: native?.blocker ?? null,
    network_wait: wait && typeof wait === 'object' ? {...wait} : null,
    terminal: native?.terminal && typeof native.terminal === 'object' ? {...native.terminal} : null,
    menu_phase: state?.menu_phase ?? null,
    menu_running: state?.menu_running ?? null,
    native_active: native?.active ?? null,
    runtime_error: state?.runtime_error ?? null,
    runtime_owner_failure: state?.owner_health?.failure ?? null,
    peer_failure: state?.owner_health?.peer?.failure ?? null,
    protocol_role: state?.owner_health?.peer?.protocol?.role ?? null,
    protocol_terminal: state?.owner_health?.peer?.protocol?.terminal ?? null,
  };
}

function isNonterminalPeerSample(peer, expectedRole) {
  return peer && peer.role === expectedRole && peer.protocol_role === expectedRole &&
    peer.menu_phase === 1 && peer.menu_running === 1 &&
    peer.native_active === 1 && peer.runtime_error == null &&
    peer.runtime_owner_failure == null && peer.peer_failure == null &&
    peer.terminal?.kind === 0 && peer.protocol_terminal == null &&
    Number.isSafeInteger(peer.source_cursor) &&
    Number.isSafeInteger(peer.network_wait?.active) &&
    Number.isSafeInteger(peer.network_wait?.callbacks) &&
    Number.isSafeInteger(peer.network_wait?.episodes) &&
    Number.isSafeInteger(peer.network_wait.resume_count);
}

function isCleanPeerSample(peer, expectedRole) {
  return isNonterminalPeerSample(peer, expectedRole) && peer.network_wait.active === 1;
}

/**
 * Reconstruct one adequately sampled stable wait window from read-only observations.
 * Synthetic policy fixtures exercise this predicate only; they are not browser evidence.
 */
export function findPostCssStallWindow(observations) {
  let window = [];
  for (const row of observations || []) {
    const peers = row?.peers;
    const valid = Number.isFinite(row?.at_monotonic_ms) && isCleanPeerSample(peers?.alpha, 'alpha') &&
      isCleanPeerSample(peers?.beta, 'beta');
    const direction = row?.selected_direction;
    const queuePresent = Number.isSafeInteger(direction?.queued_bytes) && direction.queued_bytes > 0 &&
      Number.isSafeInteger(direction?.in_flight_bytes) &&
      Number.isSafeInteger(direction?.pending_bytes) &&
      direction.pending_bytes === direction.queued_bytes + direction.in_flight_bytes;
    if (!valid || !queuePresent) {
      window = [];
      continue;
    }
    const sameCursor = window.length === 0 ||
      (peers.alpha.source_cursor === window[0].peers.alpha.source_cursor &&
       peers.beta.source_cursor === window[0].peers.beta.source_cursor);
    const gap = window.length === 0 ? 0 : row.at_monotonic_ms - window.at(-1).at_monotonic_ms;
    if (!sameCursor || gap <= 0 || gap > POST_CSS_STALL_PROBE.maximumPollGapMs) {
      window = [row];
      continue;
    }
    window.push(row);
    const spanMs = window.at(-1).at_monotonic_ms - window[0].at_monotonic_ms;
    if (spanMs >= POST_CSS_STALL_PROBE.minimumStableMs) {
      return {
        start_monotonic_ms: window[0].at_monotonic_ms,
        end_monotonic_ms: window.at(-1).at_monotonic_ms,
        span_ms: spanMs,
        cursors: {alpha: window[0].peers.alpha.source_cursor,
          beta: window[0].peers.beta.source_cursor},
        queued_bytes_peak: Math.max(...window.map(sample =>
          sample.selected_direction.queued_bytes)),
        in_flight_bytes_peak: Math.max(...window.map(sample =>
          sample.selected_direction.in_flight_bytes)),
        observation_count: window.length,
      };
    }
  }
  return null;
}

function findReleaseProof(snapshot, connectionId, receipt) {
  let activationNs, nextReleaseNs;
  try {
    activationNs = BigInt(receipt.activatedNs);
    nextReleaseNs = BigInt(receipt.nextReleaseNs);
  } catch {
    return null;
  }
  const events = snapshot?.diagnostics?.events;
  if (!Array.isArray(events)) return null;
  const enqueues = events.filter(row => row?.event === 'enqueue' &&
    row.connectionId === connectionId && row.direction === POST_CSS_STALL_PROBE.direction &&
    Number.isSafeInteger(row.sequence));
  for (const enqueued of enqueues) {
    let enqueuedNs, scheduledReleaseNs;
    try {
      enqueuedNs = BigInt(enqueued.enqueuedNs);
      scheduledReleaseNs = BigInt(enqueued.scheduledReleaseNs);
    } catch { continue; }
    if (enqueuedNs < activationNs || scheduledReleaseNs < nextReleaseNs) continue;
    const released = events.find(row => row?.event === 'release' &&
      row.connectionId === connectionId && row.direction === POST_CSS_STALL_PROBE.direction &&
      row.sequence === enqueued.sequence);
    if (!released) continue;
    try {
      const releasedNs = BigInt(released.writeCallbackNs);
      if (releasedNs < nextReleaseNs) continue;
    } catch { continue; }
    return {enqueue: enqueued, release: released};
  }
  return null;
}

/** Separate fault policy. This does not replace or weaken verifyNetworkProxy. */
export function validatePostCssStallEvidence(evidence) {
  requireValue(evidence?.schema === 'melee-web-post-css-stall-probe-v1', 'unknown evidence schema');
  requireValue(evidence.outcome === 'EXERCISED_AND_RESUMED', 'probe did not complete its separate stall policy');
  requireValue(Number.isFinite(evidence.elapsed_ms) && evidence.elapsed_ms <= POST_CSS_STALL_PROBE.capMs,
    'probe exceeded its 3500 ms cap');
  requireValue(evidence.active_room_connection_count === 2 &&
    Array.isArray(evidence.active_room_connection_ids) && evidence.active_room_connection_ids.length === 2,
  'expected exactly two active room connections after calibration');
  requireValue(evidence.active_room_connection_ids.every(Number.isSafeInteger) &&
    new Set(evidence.active_room_connection_ids).size === 2 &&
    evidence.proxy_before?.controlledFaults?.length === 0 &&
    evidence.proxy_before?.lifecycle?.activeConnections === 2 &&
    evidence.proxy_before?.connections?.active?.length === 2 &&
    evidence.proxy_before?.connections.active.map(row => row.connectionId).sort((a, b) => a - b)
      .every((id, index) => id === [...evidence.active_room_connection_ids].sort((a, b) => a - b)[index]) &&
    evidence.proxy_before?.diagnostics?.socketErrors === 0 &&
    evidence.proxy_before?.diagnostics?.integrityFailure === false,
  'room connection IDs or pre-stall fault receipt are malformed');
  requireValue(evidence.browser_path_verification?.verified === true,
    'actual alpha and beta browser WebSocket paths were not verified');
  requireValue(Number.isSafeInteger(evidence.selected_connection_id) &&
    evidence.active_room_connection_ids.includes(evidence.selected_connection_id) &&
    evidence.selected_connection_role === null,
  'selected connection is not one opaque active ID or was attributed to a player');
  const receipt = evidence.stall_receipt;
  requireValue(receipt?.kind === 'stall' && receipt.connectionId === evidence.selected_connection_id &&
    receipt.direction === POST_CSS_STALL_PROBE.direction &&
    receipt.durationMs === POST_CSS_STALL_PROBE.durationMs &&
    Number.isSafeInteger(receipt.queuedChunks) && receipt.queuedChunks >= 0 &&
    Number.isSafeInteger(receipt.queuedBytes) && receipt.queuedBytes >= 0 &&
    Number.isSafeInteger(receipt.inFlightBytesNotStalled) && receipt.inFlightBytesNotStalled >= 0,
  'missing or malformed 600 ms client-to-Worker stall receipt');
  requireValue(evidence.proxy_after_stall?.controlledFaults?.length === 1 &&
    evidence.proxy_after_stall.controlledFaults[0]?.kind === 'stall' &&
    evidence.proxy_after_stall.controlledFaults[0]?.connectionId === evidence.selected_connection_id &&
    evidence.proxy_after_stall.controlledFaults[0]?.direction === POST_CSS_STALL_PROBE.direction &&
    evidence.proxy_after_stall.lifecycle?.cutConnections === 0 &&
    evidence.proxy_after_stall.lifecycle?.activeConnections === 2 &&
    evidence.proxy_after_stall.connections?.active?.length === 2 &&
    evidence.proxy_after_stall.diagnostics?.socketErrors === 0 &&
    evidence.proxy_after_stall.diagnostics?.integrityFailure === false,
  'proxy did not retain exactly the requested one-direction stall without a cut');
  requireValue(evidence.before?.peers?.alpha && evidence.before?.peers?.beta,
    'missing read-only pre-stall peer observations');
  for (const role of ['alpha', 'beta'])
    requireValue(isNonterminalPeerSample(evidence.before.peers[role], role),
      role + ' was terminal or malformed before stall');
  requireValue(Array.isArray(evidence.observations) &&
    evidence.observations.every(row => isNonterminalPeerSample(row?.peers?.alpha, 'alpha') &&
      isNonterminalPeerSample(row?.peers?.beta, 'beta') && row.proxy_errors === 0 &&
      row.integrity_failure === false &&
      Number.isSafeInteger(row.selected_direction?.queued_bytes) &&
      Number.isSafeInteger(row.selected_direction?.in_flight_bytes) &&
      Number.isSafeInteger(row.selected_direction?.pending_bytes) &&
      Number.isSafeInteger(row.selected_direction?.bytes_enqueued) &&
      Number.isSafeInteger(row.selected_direction?.bytes_released) &&
      row.selected_direction.pending_bytes ===
        row.selected_direction.queued_bytes + row.selected_direction.in_flight_bytes &&
      row.selected_direction.bytes_released <= row.selected_direction.bytes_enqueued),
  'a runtime/protocol/transport terminal or proxy error occurred during observation');
  const window = findPostCssStallWindow(evidence.observations);
  requireValue(window !== null, 'no 120 ms stable two-peer network-wait span with actual queued bytes');
  requireValue(evidence.held_window && evidence.held_window.start_monotonic_ms === window.start_monotonic_ms &&
    evidence.held_window.end_monotonic_ms === window.end_monotonic_ms &&
    evidence.held_window.span_ms === window.span_ms &&
    evidence.held_window.observation_count === window.observation_count &&
    evidence.held_window.cursors?.alpha === window.cursors.alpha &&
    evidence.held_window.cursors?.beta === window.cursors.beta,
  'reported held window does not match its sampled observations');
  const resumed = evidence.resumed?.peers;
  for (const role of ['alpha', 'beta']) {
    const before = evidence.before.peers[role];
    const heldCursor = window.cursors[role];
    const after = resumed?.[role];
    requireValue(isNonterminalPeerSample(after, role) && after.source_cursor > heldCursor &&
      after.network_wait?.resume_count > before.network_wait.resume_count &&
      after.terminal?.kind === 0 && after.protocol_terminal == null,
    role + ' did not advance and increment native network_wait resume after release');
  }
  requireValue(evidence.release_proof?.enqueue?.event === 'enqueue' &&
    evidence.release_proof?.release?.event === 'release' &&
    evidence.release_proof.enqueue.connectionId === evidence.selected_connection_id &&
    evidence.release_proof.release.connectionId === evidence.selected_connection_id &&
    evidence.release_proof.enqueue.direction === POST_CSS_STALL_PROBE.direction &&
    evidence.release_proof.release.direction === POST_CSS_STALL_PROBE.direction,
  'missing selected-direction enqueue and post-stall write-release timestamps');
  try {
    const activation = BigInt(receipt.activatedNs);
    const nextRelease = BigInt(receipt.nextReleaseNs);
    const enqueue = evidence.release_proof.enqueue;
    const release = evidence.release_proof.release;
    requireValue(Number.isSafeInteger(enqueue.sequence) && enqueue.sequence === release.sequence &&
      BigInt(enqueue.enqueuedNs) >= activation &&
      nextRelease >= activation + BigInt(POST_CSS_STALL_PROBE.durationMs) * 1_000_000n &&
      BigInt(enqueue.scheduledReleaseNs) >= nextRelease &&
      BigInt(release.scheduledReleaseNs) >= nextRelease &&
      BigInt(release.writeCallbackNs) >= nextRelease &&
      Number.isSafeInteger(enqueue.bytes) && enqueue.bytes > 0 &&
      release.bytes === enqueue.bytes && BigInt(release.writeCallbackNs) >= BigInt(enqueue.enqueuedNs),
    'recorded queue/write timestamps do not prove a scheduled release after the stall');
  } catch (error) {
    if (String(error?.message || error).startsWith('Post-CSS stall probe:')) throw error;
    requireValue(false, 'stall release timestamps are malformed');
  }
  return {policy: 'controlled-post-css-stall-exercised', held_span_ms: window.span_ms,
    queued_bytes_peak: window.queued_bytes_peak,
    observed_source_cursors: window.cursors, result_is_not_clean_route_pass: true};
}

function liveDirectionSnapshot(snapshot, connectionId) {
  const rows = snapshot?.connections?.active;
  if (!Array.isArray(rows)) return null;
  const connection = rows.find(row => row.connectionId === connectionId);
  const direction = connection?.directions?.[POST_CSS_STALL_PROBE.direction];
  if (!direction || !Number.isSafeInteger(direction.queuedBytes) ||
      !Number.isSafeInteger(direction.inFlightBytes) || !Number.isSafeInteger(direction.pendingBytes) ||
      !Number.isSafeInteger(direction.bytesEnqueued) || !Number.isSafeInteger(direction.bytesReleased) ||
      direction.pendingBytes !== direction.queuedBytes + direction.inFlightBytes ||
      direction.bytesReleased > direction.bytesEnqueued)
    return null;
  return {queued_bytes: direction.queuedBytes, in_flight_bytes: direction.inFlightBytes,
    pending_bytes: direction.pendingBytes, bytes_enqueued: direction.bytesEnqueued,
    bytes_released: direction.bytesReleased};
}

function validateLiveRoomSnapshot(snapshot, selectedId) {
  requireValue(snapshot?.schema === 'melee-web-tcp-test-proxy-snapshot-v1' &&
    snapshot.lifecycle?.activeConnections === 2 && snapshot.connections?.active?.length === 2,
  'proxy did not retain exactly two active room connections');
  requireValue(snapshot.diagnostics?.socketErrors === 0 && snapshot.diagnostics.integrityFailure === false,
    'proxy reported a socket or integrity failure');
  requireValue(Array.isArray(snapshot.controlledFaults) && snapshot.controlledFaults.length === 1 &&
    snapshot.controlledFaults[0]?.kind === 'stall' &&
    snapshot.controlledFaults[0]?.connectionId === selectedId &&
    snapshot.controlledFaults[0]?.direction === POST_CSS_STALL_PROBE.direction &&
    snapshot.lifecycle?.cutConnections === 0,
  'proxy did not retain exactly one controlled stall and no cut');
  requireValue(snapshot.connections.active.some(row => row.connectionId === selectedId),
    'selected owned connection is no longer active');
}

/**
 * Runs a passive, one-direction 600 ms stall after the two actual CSS keyboard
 * witnesses. It reads native state only; it never pumps, resumes, or injects.
 */
export async function runPostCssStallProbe({proxy, readRoomState, assertHealthy, remaining,
  browserPathVerification, evidence}) {
  const startedAt = performance.now();
  const deadline = startedAt + POST_CSS_STALL_PROBE.capMs;
  const roles = ['alpha', 'beta'];
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  const readStatesWithinCap = async () => {
    remaining();
    const timeLeft = Math.floor(deadline - performance.now());
    requireValue(timeLeft > 0, '3500 ms probe cap expired before a fresh native observation');
    let timer;
    try {
      return await Promise.race([
        Promise.all(roles.map(role => readRoomState(role))),
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error(
            'Post-CSS stall probe: native observation exceeded the 3500 ms cap')),
          timeLeft);
        }),
      ]);
    } finally {
      clearTimeout(timer);
    }
  };
  evidence.schema = 'melee-web-post-css-stall-probe-v1';
  evidence.profile_name = 'constrained';
  evidence.duration_ms = POST_CSS_STALL_PROBE.durationMs;
  evidence.cap_ms = POST_CSS_STALL_PROBE.capMs;
  evidence.minimum_stable_ms = POST_CSS_STALL_PROBE.minimumStableMs;
  evidence.direction = POST_CSS_STALL_PROBE.direction;
  evidence.selected_connection_role = null;
  evidence.connection_scope = 'one opaque active owned TCP connection; no player-role attribution';
  evidence.observations = [];
  evidence.outcome = 'RUNNING';
  try {
    requireValue(typeof proxy?.stallConnection === 'function' && typeof proxy?.snapshot === 'function',
      'owned proxy does not expose the controlled stall API');
    requireValue(remaining() >= POST_CSS_STALL_PROBE.capMs,
      'less than the predeclared 3500 ms probe cap remains in the existing operation budget');
    const beforeStates = await readStatesWithinCap();
    const beforePeers = Object.fromEntries(roles.map((role, index) =>
      [role, peerEvidence(beforeStates[index], role)]));
    evidence.before = {observed_at_monotonic_ms: performance.now() - startedAt,
      peers: beforePeers};
    for (const [index, role] of roles.entries()) {
      assertHealthy(beforeStates[index], role);
      requireValue(isNonterminalPeerSample(beforePeers[role], role), role + ' was not healthy in active CSS before stall');
    }

    requireValue(browserPathVerification?.verified === true,
      'actual alpha and beta WebSocket paths were not verified before stall');
    evidence.browser_path_verification = browserPathVerification;
    const initialProxy = proxy.snapshot();
    requireValue(initialProxy?.schema === 'melee-web-tcp-test-proxy-snapshot-v1' &&
      initialProxy.lifecycle?.activeConnections === 2 && initialProxy.connections?.active?.length === 2,
    'expected exactly two active room sockets after HTTP calibration retired');
    requireValue(initialProxy.diagnostics?.socketErrors === 0 &&
      initialProxy.diagnostics.integrityFailure === false,
    'proxy was unhealthy before controlled stall');
    const activeIds = initialProxy.connections.active.map(row => row.connectionId).sort((a, b) => a - b);
    requireValue(activeIds.every(Number.isSafeInteger), 'active proxy connection IDs are malformed');
    const selectedId = activeIds[0];
    evidence.active_room_connection_count = activeIds.length;
    evidence.active_room_connection_ids = activeIds;
    evidence.selected_connection_id = selectedId;
    evidence.selection = 'lowest opaque ID among exactly two active room connections; not player-attributed';
    evidence.proxy_before = initialProxy;
    evidence.activation_started_at_monotonic_ms = performance.now() - startedAt;
    const receipt = proxy.stallConnection(selectedId, POST_CSS_STALL_PROBE.direction,
      POST_CSS_STALL_PROBE.durationMs);
    evidence.stall_receipt = receipt;
    evidence.activated_at_monotonic_ms = performance.now() - startedAt;

    let hold = null;
    let releaseProof = null;
    let latestProxy = proxy.snapshot();
    while (performance.now() < deadline) {
      remaining();
      const states = await readStatesWithinCap();
      const peers = Object.fromEntries(roles.map((role, index) =>
        [role, peerEvidence(states[index], role)]));
      latestProxy = proxy.snapshot();
      const selectedDirection = liveDirectionSnapshot(latestProxy, selectedId);
      const sample = {at_monotonic_ms: performance.now() - startedAt, peers,
        selected_direction: selectedDirection,
        proxy_errors: latestProxy?.diagnostics?.socketErrors ?? null,
        integrity_failure: latestProxy?.diagnostics?.integrityFailure ?? null};
      evidence.observations.push(sample);
      validateLiveRoomSnapshot(latestProxy, selectedId);
      requireValue(selectedDirection !== null,
        'proxy did not expose exact queued and in-flight bytes for the selected direction');
      for (const [index, role] of roles.entries()) {
        assertHealthy(states[index], role);
        requireValue(isNonterminalPeerSample(peers[role], role), role + ' native or protocol state became terminal during stall');
      }
      hold = findPostCssStallWindow(evidence.observations);
      releaseProof = findReleaseProof(latestProxy, selectedId, receipt);
      if (hold && releaseProof) {
        const held = evidence.observations.filter(row =>
          row.at_monotonic_ms >= hold.start_monotonic_ms &&
          row.at_monotonic_ms <= hold.end_monotonic_ms).at(-1);
        const heldCursors = hold.cursors;
        const baselineResume = Object.fromEntries(roles.map(role =>
          [role, beforePeers[role].network_wait.resume_count]));
        const resumed = roles.every(role => peers[role].source_cursor > heldCursors[role] &&
          peers[role].network_wait.resume_count > baselineResume[role]);
        if (resumed) {
          evidence.held_window = hold;
          evidence.resumed = {observed_at_monotonic_ms: sample.at_monotonic_ms,
            after_held_observation: held?.at_monotonic_ms ?? null, peers};
          break;
        }
      }
      await sleep(Math.min(POST_CSS_STALL_PROBE.pollMs, Math.max(1, deadline - performance.now())));
    }
    evidence.elapsed_ms = performance.now() - startedAt;
    evidence.proxy_after_stall = latestProxy;
    if (!evidence.held_window && hold) evidence.held_window = hold;
    if (releaseProof) evidence.release_proof = releaseProof;
    requireValue(evidence.resumed !== undefined,
      'both peers did not advance and increment network_wait resume counters within 3500 ms');
    requireValue(evidence.elapsed_ms <= POST_CSS_STALL_PROBE.capMs,
      'probe exceeded its predeclared 3500 ms cap');
    evidence.outcome = 'EXERCISED_AND_RESUMED';
    evidence.policy_result = validatePostCssStallEvidence(evidence);
    return evidence;
  } catch (error) {
    evidence.elapsed_ms = performance.now() - startedAt;
    evidence.outcome = 'FAIL';
    evidence.failure = String(error?.stack || error);
    throw error;
  }
}
