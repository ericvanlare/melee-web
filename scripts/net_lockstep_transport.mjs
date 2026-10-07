import {openLoopbackPeerPair} from './net_lockstep_relay.mjs';
import {openRoomRelayPeerPair} from './net_lockstep_websocket_relay.mjs';

/** Use the local TCP baseline unless a relay URL was explicitly requested. */
export function openLockstepPeerPair({relayUrl, timeoutMs = 5000,
  onDisconnect = () => {}, onEndpointError = () => {}} = {}) {
  if (relayUrl === undefined)
    return openLoopbackPeerPair({onDisconnect, onEndpointError});
  return openRoomRelayPeerPair({url: relayUrl, timeoutMs, onDisconnect, onEndpointError});
}

export function describeLockstepTransportAttempt(relayUrl) {
  if (relayUrl === undefined) return {type: 'tcp-loopback'};
  let endpoint_origin = null;
  if (typeof relayUrl === 'string') {
    try {
      const url = new URL(relayUrl);
      if (url.protocol === 'ws:' || url.protocol === 'wss:')
        endpoint_origin = `${url.protocol}//${url.host}`;
    } catch {}
  }
  return {type: 'room-websocket', endpoint_origin};
}

export function describeLockstepTransport(pair, {relayUrl} = {}) {
  if (pair?.transport?.type === 'webrtc-datachannel') {
    if (pair.transport.ordered !== true || pair.transport.reliable !== true)
      throw Error('WebRTC lockstep transport must be reliable and ordered');
    return {...pair.transport};
  }
  if (pair?.transport?.type === 'room-websocket') {
    if (typeof relayUrl !== 'string') throw Error('Room WebSocket transport metadata requires its relay URL');
    const url = new URL(relayUrl);
    return {
      ...pair.transport,
      endpoint_origin: `${url.protocol}//${url.host}`,
    };
  }
  if (Number.isInteger(pair?.port) && pair?.traffic) {
    return {
      type: 'tcp-loopback',
      host: '127.0.0.1',
      port: pair.port,
      framing: 'TCP length-prefixed JSON, 4-byte big-endian length',
      relay_interprets_packets: false,
    };
  }
  throw Error('Lockstep transport adapter did not expose a supported transport identity');
}

/** Add byte counts only for transports that actually measured them. */
export function recordAvailableTransportMetrics(summary, pair) {
  if (summary?.type !== 'tcp-loopback' || !pair?.traffic) return;
  summary.alpha_to_beta_bytes = pair.traffic.alpha_to_beta_bytes;
  summary.beta_to_alpha_bytes = pair.traffic.beta_to_alpha_bytes;
}

/** Observe asynchronous endpoint callbacks even when an event emitter ignores
 * their returned promise. The original promise is returned to callers that can
 * await it; drain() joins all callbacks and failures remain inspectable. */
export function createTransportCallbackQueue(onFailure = () => {}) {
  const pending = new Set();
  const failures = [];

  function track(role, kind, callback) {
    if (typeof callback !== 'function') throw Error('Transport callback must be a function');
    const result = Promise.resolve().then(callback);
    const observed = result.then(
      () => ({status: 'fulfilled'}),
      error => {
        const failure = {role, kind, error};
        failures.push(failure);
        try { onFailure(failure); }
        catch (observerError) { failures.push({role, kind: 'failure-observer', error: observerError}); }
        return {status: 'rejected', error};
      },
    );
    pending.add(observed);
    observed.then(() => pending.delete(observed));
    return result;
  }

  async function drain() {
    while (pending.size) await Promise.all([...pending]);
    return [...failures];
  }

  return {track, drain, get pendingCount() { return pending.size; },
    get failures() { return [...failures]; }};
}
