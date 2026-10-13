/** Real-socket, same-machine network experiments; never a public-service preflight. */
import http from 'node:http';
import {createHash} from 'node:crypto';
import {performance} from 'node:perf_hooks';

const profiles = {
  control: {clientToWorker: {seed: 101}, workerToClient: {seed: 202}},
  moderate: {
    clientToWorker: {seed: 101, delayMs: 10, jitterMs: 5},
    workerToClient: {seed: 202, delayMs: 10, jitterMs: 5},
  },
  asymmetric: {
    clientToWorker: {seed: 303, delayMs: 5, jitterMs: 15},
    workerToClient: {seed: 404, delayMs: 25, jitterMs: 10},
  },
  constrained: {
    clientToWorker: {seed: 505, delayMs: 10, jitterMs: 5, bytesPerSecond: 65536},
    workerToClient: {seed: 606, delayMs: 10, jitterMs: 5, bytesPerSecond: 65536},
  },
};
export const NETWORK_PROFILE_NAMES = Object.freeze(Object.keys(profiles));

export function resolveNetworkSimulation({profileName, relayUrl, preflight} = {}) {
  if (profileName === undefined) return null;
  if (relayUrl !== undefined || preflight !== undefined)
    throw Error('--network-profile cannot be combined with public --relay-url or --relay-preflight');
  if (typeof profileName !== 'string' || !Object.hasOwn(profiles, profileName))
    throw Error('--network-profile must be one of: ' + NETWORK_PROFILE_NAMES.join(', '));
  const profile = structuredClone(profiles[profileName]);
  return {
    mode: 'simulated-network', profile_name: profileName, profile,
    profile_sha256: createHash('sha256').update(JSON.stringify(profile)).digest('hex'),
    scope: 'same-machine real TCP sockets through an owned proxy and local Worker; simulated network conditions',
    directions: 'client-to-Worker and Worker-to-client per connection; each peer-to-peer message traverses two shaped legs',
    limits: ['no public routing, NAT, TLS or geographic network evidence',
      'configured leg delay is not measured peer RTT or input latency',
      'seeded jitter repeats only for an identical TCP chunk sequence; OS segmentation can vary',
      'bandwidth shaping and stalls do not implement a physical IP packet-loss model'],
  };
}

export async function startNetworkSimulation({worker, configuration}) {
  if (!worker || !Number.isInteger(worker.port) || worker.port <= 0 ||
      configuration?.mode !== 'simulated-network')
    throw Error('Network simulation requires the owned local Worker and a validated profile');
  const {startTcpProxy} = await import('./net_test_tcp_proxy.mjs');
  const proxy = await startTcpProxy({targetHost: '127.0.0.1', targetPort: worker.port,
    listenHost: '127.0.0.1', listenPort: 0, profile: configuration.profile});
  return {proxy, relayUrl: 'ws://127.0.0.1:' + proxy.address.port,
    httpBase: 'http://127.0.0.1:' + proxy.address.port};
}

/** Clean-route policy. Fault scenarios must use a separate explicit policy. */
export function verifyNetworkProxy(snapshot, {final = false} = {}) {
  const require = (condition, message) => {
    if (!condition) throw Error('Network proxy: ' + message);
  };
  require(snapshot?.schema === 'melee-web-tcp-test-proxy-snapshot-v1', 'unknown receipt schema');
  const life = snapshot.lifecycle;
  require(life && snapshot.diagnostics && snapshot.directions && snapshot.connections,
    'incomplete receipt');
  require(snapshot.diagnostics.socketErrors === 0 && snapshot.diagnostics.integrityFailure === false,
    'socket or integrity failure');
  require(life.failedConnections === 0 && life.rejectedConnections === 0 &&
    life.cutConnections === 0 && life.stopClosedConnections === 0, 'unclean connection');
  require(snapshot.connections.closedOmitted === 0 &&
    snapshot.connections.closed.length === life.closedConnections, 'missing connection receipts');
  const closedBytes = {clientToWorker: 0, workerToClient: 0};
  for (const connection of snapshot.connections.closed) {
    require(connection.clean === true && connection.hashesMatch === true && !connection.reason,
      'closed connection did not preserve all bytes');
    for (const name of ['clientToWorker', 'workerToClient']) {
      const row = connection.directions[name];
      require(row && Number.isSafeInteger(row.bytesEnqueued) && row.bytesEnqueued >= 0 &&
        Number.isSafeInteger(row.bytesReleased) && row.bytesReleased >= 0 &&
        row.bytesEnqueued === row.bytesReleased &&
        /^[a-f0-9]{64}$/.test(row.enqueuedSha256) && row.enqueuedSha256 === row.releasedSha256,
        'closed connection byte/hash mismatch');
      closedBytes[name] += row.bytesReleased;
    }
  }
  for (const name of ['clientToWorker', 'workerToClient']) {
    const row = snapshot.directions[name];
    require(row && Number.isSafeInteger(row.bytesEnqueued) && row.bytesEnqueued >= 0 &&
      Number.isSafeInteger(row.bytesReleased) && row.bytesReleased >= 0 &&
      row.bytesReleased <= row.bytesEnqueued, 'invalid byte counters');
    require(row.bytesCutAtThreshold === 0 && row.bytesDroppedOnCut === 0 &&
      row.bytesDroppedOnStop === 0 && row.bytesDroppedOnFailure === 0, 'dropped bytes');
    if (final) require(row.bytesEnqueued > 0 && row.bytesEnqueued === row.bytesReleased,
      'final queue did not drain');
    if (final) require(Number.isSafeInteger(closedBytes[name]) &&
      closedBytes[name] === row.bytesReleased, 'final connection totals differ from aggregate');
  }
  if (final) {
    require(life.cleanShutdown === true && life.listenerClosed === true &&
      life.activeConnections === 0 && life.activeTimers === 0 &&
      snapshot.connections.active.length === 0, 'resources did not retire');
    require(life.acceptedConnections > 0 && life.acceptedConnections === life.closedConnections &&
      life.cleanConnections === life.closedConnections && snapshot.dataPathPass === true,
      'final data path did not pass');
  } else require(snapshot.dataPathPass !== false, 'data path failed');
  return {verified: true, final,
    scope: 'proxy-observed bytes released through completed Node socket writes; not remote TCP acknowledgements'};
}

export async function closeNetworkProxy(proxy) {
  // Browser process exit and TCP FIN callbacks are asynchronous. Give already
  // closing sockets a bounded drain interval inside the caller's cleanup budget.
  // stop() then closes any remaining sockets, which the clean-route policy fails.
  const deadline = performance.now() + 1000;
  while (proxy.snapshot().lifecycle.activeConnections > 0 && performance.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 10));
  return proxy.stop();
}

function measureHttp(base, timeoutMs) {
  return new Promise((resolve, reject) => {
    const started = performance.now();
    const chunks = [];
    let bytes = 0;
    const request = http.get(new URL('/v1/rooms/too-short/socket', base), {agent: false}, response => {
      response.on('data', chunk => {
        bytes += chunk.length;
        if (bytes > 4096) request.destroy(Error('Network calibration response exceeded 4 KiB'));
        else chunks.push(chunk);
      });
      response.once('error', error => { clearTimeout(timer); reject(error); });
      response.once('end', () => {
        clearTimeout(timer);
        const body = Buffer.concat(chunks);
        if (response.statusCode !== 400 || body.toString() !== '{"error":"invalid room id"}') {
          reject(Error('Network calibration did not receive the actual Worker invalid-room response'));
          return;
        }
        resolve({status: response.statusCode, bytes: body.length,
          body_sha256: createHash('sha256').update(body).digest('hex'),
          round_trip_ms: performance.now() - started});
      });
    });
    const timer = setTimeout(() => request.destroy(Error('Network calibration deadline expired')), timeoutMs);
    request.once('error', error => { clearTimeout(timer); reject(error); });
  });
}

export async function calibrateNetworkSimulation({workerBase, proxyBase, remaining}) {
  const samples = [];
  for (let index = 0; index < 3; ++index) {
    const direct = await measureHttp(workerBase, Math.min(5000, remaining()));
    const shaped = await measureHttp(proxyBase, Math.min(5000, remaining()));
    if (direct.body_sha256 !== shaped.body_sha256 || direct.bytes !== shaped.bytes)
      throw Error('Network calibration changed the Worker response bytes');
    samples.push({direct, shaped, difference_ms: shaped.round_trip_ms - direct.round_trip_ms});
  }
  return {scope: 'measured Node HTTP request/response round trips to the same Worker, including host and Worker processing; not browser peer RTT',
    samples, peer_rtt_ms: null, input_latency_ms: null};
}

export function observeBrowserNetworkPaths(instances, expectedBase) {
  const rows = Object.fromEntries(Object.keys(instances).map(role => [role, []]));
  const handlers = {};
  for (const [role, instance] of Object.entries(instances)) {
    handlers[role] = socket => rows[role].push({url: socket.url(), opened_at: new Date().toISOString()});
    instance.page.on('websocket', handlers[role]);
  }
  return {
    snapshot: () => structuredClone(rows),
    verify(roomId) {
      const expected = expectedBase + '/v1/rooms/' + roomId + '/socket';
      for (const [role, sockets] of Object.entries(rows)) {
        if (sockets.length !== 1 || sockets[0].url !== expected)
          throw Error(role + ' did not use exactly one observed WebSocket through the selected network proxy');
      }
      return {verified: true, browser_websockets: structuredClone(rows),
        scope: 'actual browser WebSocket URLs on independent profiles; proxy connection IDs remain opaque and include separate calibration requests'};
    },
    close() {
      for (const [role, instance] of Object.entries(instances))
        instance.page.off('websocket', handlers[role]);
    },
  };
}
