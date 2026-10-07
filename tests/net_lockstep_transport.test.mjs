import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {
  createTransportCallbackQueue, describeLockstepTransport, describeLockstepTransportAttempt,
  openLockstepPeerPair, recordAvailableTransportMetrics,
} from '../scripts/net_lockstep_transport.mjs';
import {
  createRoomId, createRoomRelayPeerEndpoint, openRoomRelayPeerPair,
  ROOM_RELAY_MAX_PENDING_SEND_MESSAGES,
} from '../scripts/net_lockstep_websocket_relay.mjs';
import {LockstepPeer, LOCKSTEP_VERSION} from '../scripts/net_lockstep_protocol.mjs';

async function freePort() {
  const server = createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const {port} = server.address();
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  return port;
}

async function startReadylessWebSocketServer() {
  const sockets = new Set();
  const server = createServer((_request, response) => {
    response.writeHead(404).end();
  });
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key'];
    if (typeof key !== 'string') { socket.destroy(); return; }
    const accept = createHash('sha1')
      .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\nConnection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sockets.add(socket);
    socket.on('data', chunk => {
      if ((chunk[0] & 0x0f) !== 0x08) return;
      // Complete the client Close handshake; this test server deliberately
      // never sends the relay's ready control message.
      socket.write(Buffer.from([0x88, 0x02, 0x03, 0xe8]));
      socket.end();
    });
    socket.on('close', () => sockets.delete(socket));
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return {
    url: `ws://127.0.0.1:${server.address().port}`,
    async close() {
      for (const socket of sockets) socket.destroy();
      await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    },
  };
}

const READY_PACKET = JSON.stringify({relay: 1, event: 'ready'});
const FIRST_PACKET = JSON.stringify({type: 'hello', opaque: 'first packet 雪'});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

async function waitFor(predicate, label, timeoutMs = 1000) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate() && Date.now() < deadline) await wait(2);
  assert(predicate(), `timed out waiting for ${label}`);
}

function rawServerFrame(opcode, payload) {
  const body = Buffer.from(payload);
  if (body.length <= 125) return Buffer.concat([Buffer.from([0x80 | opcode, body.length]), body]);
  if (body.length <= 0xffff) {
    const header = Buffer.alloc(4);
    header[0] = 0x80 | opcode; header[1] = 126; header.writeUInt16BE(body.length, 2);
    return Buffer.concat([header, body]);
  }
  const header = Buffer.alloc(10);
  header[0] = 0x80 | opcode; header[1] = 127; header.writeBigUInt64BE(BigInt(body.length), 2);
  return Buffer.concat([header, body]);
}

async function startImmediateReadyServer({peerCount = 1, packets = [FIRST_PACKET],
  delayReadyMs = 0, closeAfterReady = false, closeBeforeReady = false} = {}) {
  const peers = new Set();
  const received = [];
  const fixtureErrors = [];
  let upgradeCount = 0;
  let initialSent = false;
  const server = createServer((_request, response) => response.writeHead(404).end());
  const sendInitial = () => {
    initialSent = true;
    const data = closeBeforeReady
      ? rawServerFrame(8, Buffer.from([0x03, 0xe9]))
      : Buffer.concat([
        rawServerFrame(1, READY_PACKET), ...packets.map(packet => rawServerFrame(1, packet)),
        ...(closeAfterReady ? [rawServerFrame(8, Buffer.from([0x03, 0xe9]))] : []),
      ]);
    for (const peer of peers) peer.socket.write(data);
  };
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key'];
    if (typeof key !== 'string') { socket.destroy(); return; }
    const accept = createHash('sha1')
      .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
    const peer = {socket, buffered: Buffer.alloc(0)};
    peers.add(peer);
    ++upgradeCount;
    socket.once('close', () => peers.delete(peer));
    socket.on('data', chunk => {
      peer.buffered = Buffer.concat([peer.buffered, chunk]);
      while (peer.buffered.length >= 2) {
        const first = peer.buffered[0], second = peer.buffered[1];
        const opcode = first & 0x0f;
        let length = second & 0x7f, headerBytes = 2;
        if (length === 126) {
          if (peer.buffered.length < 4) return;
          length = peer.buffered.readUInt16BE(2); headerBytes = 4;
        } else if (length === 127) {
          if (peer.buffered.length < 10) return;
          const largeLength = peer.buffered.readBigUInt64BE(2);
          if (largeLength > BigInt(Number.MAX_SAFE_INTEGER)) {
            fixtureErrors.push('oversized test client frame'); socket.destroy(); return;
          }
          length = Number(largeLength); headerBytes = 10;
        }
        const masked = Boolean(second & 0x80);
        const maskBytes = masked ? 4 : 0;
        const frameBytes = headerBytes + maskBytes + length;
        if (peer.buffered.length < frameBytes) return;
        let payload = Buffer.from(peer.buffered.subarray(headerBytes + maskBytes, frameBytes));
        if (masked) {
          const mask = peer.buffered.subarray(headerBytes, headerBytes + 4);
          for (let index = 0; index < payload.length; ++index) payload[index] ^= mask[index % 4];
        } else {
          fixtureErrors.push('test client WebSocket frame was not masked'); socket.destroy(); return;
        }
        peer.buffered = peer.buffered.subarray(frameBytes);
        if (opcode === 1) received.push(payload.toString('utf8'));
        if (opcode === 8) {
          socket.write(rawServerFrame(8, payload));
          socket.end();
        }
      }
    });
    if (peers.size === peerCount) setTimeout(sendInitial, delayReadyMs);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return {
    url: `ws://127.0.0.1:${server.address().port}`,
    received, fixtureErrors,
    get upgrades() { return upgradeCount; },
    get initialSent() { return initialSent; },
    async close() {
      for (const peer of peers) peer.socket.destroy();
      await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    },
  };
}

test('omitting relay URL preserves real TCP loopback and only reports measured counters', async () => {
  const pair = await openLockstepPeerPair();
  try {
    const summary = describeLockstepTransport(pair);
    assert.equal(summary.type, 'tcp-loopback');
    assert.equal(summary.host, '127.0.0.1');
    assert.equal(summary.port, pair.port);
    const received = new Promise(resolve => pair.beta.onMessage(resolve));
    await pair.alpha.send('{"type":"transport-probe"}');
    await pair.beta.flush();
    assert.equal(await received, '{"type":"transport-probe"}');
    recordAvailableTransportMetrics(summary, pair);
    assert(pair.traffic.alpha_to_beta_bytes > 0);
    assert.equal(summary.alpha_to_beta_bytes, pair.traffic.alpha_to_beta_bytes);
  } finally {
    await pair.close();
  }
});

test('malformed explicit relay URL fails instead of falling back to loopback', async () => {
  await assert.rejects(openLockstepPeerPair({relayUrl: 'not-a-websocket-url', timeoutMs: 100}), /Invalid URL/);
});

test('unreachable explicit relay URL fails instead of falling back to loopback', async () => {
  const port = await freePort();
  await assert.rejects(openLockstepPeerPair({relayUrl: `ws://127.0.0.1:${port}`, timeoutMs: 250}), error =>
    /Room relay WebSocket/.test(String(error?.message || error)));
});

test('explicit relay readiness timeout fails without fallback and closes accepted sockets', async () => {
  const server = await startReadylessWebSocketServer();
  try {
    await assert.rejects(openLockstepPeerPair({relayUrl: server.url, timeoutMs: 250}),
      /Room relay is still waiting for its second peer/);
  } finally {
    await server.close();
  }
});

test('single-peer endpoint installs receive before READY and gates A2 send on room readiness', async () => {
  const server = await startImmediateReadyServer({delayReadyMs: 80,
    packets: [JSON.stringify({type: 'hello', version: LOCKSTEP_VERSION, role: 'beta',
      local_port: 1, remote_port: 0, agreement: {build: 'same', marker: '雪'}})],
  });
  const agreement = {build: 'same', marker: '雪'};
  const readySnapshots = [];
  const peer = new LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    pushFrame: async () => {},
    onReady: async current => readySnapshots.push({hash: current.agreementHash, localHello: current.localHello}),
  });
  let endpoint;
  try {
    endpoint = createRoomRelayPeerEndpoint({url: server.url, roomId: 'A'.repeat(24), role: 'alpha',
      timeoutMs: 1000, onMessage: text => peer.receive(text),
    });
    peer.attach(text => endpoint.send(text));
    const start = peer.start(agreement);
    await waitFor(() => server.upgrades === 1, 'single endpoint HTTP upgrade');
    assert.equal(server.initialSent, false, 'the fixture withholds READY during the local agreement preparation');
    await Promise.all([endpoint.ready, start]);
    await waitFor(() => peer.ready, 'the first remote hello delivered immediately after READY');
    await endpoint.drainInbound();
    await waitFor(() => server.received.length === 1, 'local hello received by the real HTTP/WebSocket server');
    assert.equal(peer.terminal, null);
    assert.equal(readySnapshots.length, 1);
    assert.equal(readySnapshots[0].hash, peer.agreementHash);
    assert.equal(readySnapshots[0].localHello, peer.localHello);
    assert.equal(server.received.length, 1, 'the local hello is sent after READY and received exactly once');
    assert.equal(JSON.parse(server.received[0]).type, 'hello');
    assert.deepEqual(server.fixtureErrors, []);
    await endpoint.close();
    assert.equal(endpoint.closed, true);
  } finally {
    if (endpoint && !endpoint.closed) {
      try { await endpoint.close(); } catch {}
    }
    await server.close();
  }
});

test('pair compatibility retains READY-adjacent first packets until listeners attach', async () => {
  const server = await startImmediateReadyServer({peerCount: 2});
  const delivered = {alpha: [], beta: []};
  let pair;
  try {
    pair = await openRoomRelayPeerPair({url: server.url, roomId: 'B'.repeat(24), timeoutMs: 1000});
    pair.alpha.onMessage(text => delivered.alpha.push(text));
    pair.beta.onMessage(text => delivered.beta.push(text));
    await Promise.all([pair.alpha.drainInbound(), pair.beta.drainInbound()]);
    assert.deepEqual(delivered, {alpha: [FIRST_PACKET], beta: [FIRST_PACKET]});
    assert.deepEqual(server.fixtureErrors, []);
    await pair.close();
    assert(pair.alpha.closed && pair.beta.closed);
  } finally {
    if (pair) {
      try { await pair.close(); } catch {}
    }
    await server.close();
  }
});

test('pair close releases and rejects queued pre-listener messages', async () => {
  const server = await startImmediateReadyServer({peerCount: 2, closeAfterReady: true});
  const disconnects = [];
  const endpointErrors = [];
  let pair;
  try {
    pair = await openRoomRelayPeerPair({url: server.url, roomId: 'C'.repeat(24), timeoutMs: 1000,
      onDisconnect: async (role, reason) => disconnects.push({role, reason}),
      onEndpointError: (role, error) => endpointErrors.push({role, message: String(error?.message || error)}),
    });
    await Promise.all(['alpha', 'beta'].map(role => assert.rejects(pair[role].drainInbound(500), error =>
      error instanceof AggregateError && error.errors.some(row =>
        /closed before a message listener was registered/.test(String(row?.message || row))))));
    await waitFor(() => disconnects.length === 2, 'reserved disconnect callbacks after an early close');
    assert.deepEqual(disconnects.map(row => row.role).sort(), ['alpha', 'beta']);
    assert.equal(endpointErrors.length, 2);
    assert(endpointErrors.every(row => /closed before a message listener was registered/.test(row.message)));
    await pair.close();
    assert(pair.alpha.closed && pair.beta.closed);
  } finally {
    if (pair) {
      try { await pair.close(); } catch {}
    }
    await server.close();
  }
});

test('pair close shares one rejected cleanup promise when an async listener fails', async () => {
  const server = await startImmediateReadyServer({peerCount: 2});
  let pair;
  let releaseListener;
  const listenerGate = new Promise(resolve => { releaseListener = resolve; });
  let listenerEnteredResolve;
  const listenerEntered = new Promise(resolve => { listenerEnteredResolve = resolve; });
  try {
    pair = await openRoomRelayPeerPair({url: server.url, roomId: 'G'.repeat(24), timeoutMs: 1000});
    pair.alpha.onMessage(async () => {
      listenerEnteredResolve();
      await listenerGate;
      throw new Error('delayed pair close callback failure');
    });
    pair.beta.onMessage(() => {});
    await listenerEntered;

    const firstClose = pair.close();
    const concurrentClose = pair.close();
    assert.strictEqual(concurrentClose, firstClose, 'concurrent callers share the pair cleanup operation');
    releaseListener();
    const results = await Promise.allSettled([firstClose, concurrentClose]);
    assert(results.every(result => result.status === 'rejected'));
    assert.strictEqual(results[0].reason, results[1].reason, 'all callers observe the same retained failure');
    assert(results[0].reason instanceof AggregateError);
    const nestedErrors = [];
    const collectErrors = error => {
      if (error instanceof AggregateError) error.errors.forEach(collectErrors);
      else nestedErrors.push(error);
    };
    collectErrors(results[0].reason);
    assert(nestedErrors.some(error => /delayed pair close callback failure/.test(error.message)));
    assert.strictEqual(pair.close(), firstClose, 'later callers cannot consume or erase the failed close result');
  } finally {
    releaseListener();
    if (pair) {
      try { await pair.close(); } catch {}
    }
    await server.close();
  }
});

test('single endpoint closed before READY rejects readiness and every queued send cleanly', async () => {
  const server = await startImmediateReadyServer({closeBeforeReady: true});
  const disconnects = [];
  let endpoint;
  try {
    endpoint = createRoomRelayPeerEndpoint({url: server.url, roomId: 'H'.repeat(24), role: 'alpha',
      timeoutMs: 1000, onMessage: () => {},
      onDisconnect: async (role, reason) => disconnects.push({role, reason}),
    });
    const pending = [endpoint.send('first'), endpoint.send('second')];
    await assert.rejects(endpoint.ready, /closed before both peers joined/);
    const sendResults = await Promise.allSettled(pending);
    assert.equal(sendResults.length, 2);
    assert(sendResults.every(result => result.status === 'rejected'));
    assert(sendResults.every(result => /closed before both peers joined/.test(result.reason.message)));
    await endpoint.close();
    await endpoint.drainInbound();
    assert.equal(endpoint.closed, true);
    assert.deepEqual(disconnects.map(row => row.role), ['alpha']);
    assert.match(disconnects[0].reason, /^1001:/);
    assert.deepEqual(server.fixtureErrors, []);
  } finally {
    if (endpoint && !endpoint.closed) {
      try { await endpoint.close(); } catch {}
    }
    await server.close();
  }
});

test('pre-listener inbound backlog stops at the existing message and byte bounds', async () => {
  const server = await startImmediateReadyServer({peerCount: 2, packets: Array(257).fill('x')});
  const endpointErrors = [];
  let pair;
  try {
    pair = await openRoomRelayPeerPair({url: server.url, roomId: 'D'.repeat(24), timeoutMs: 1000,
      onEndpointError: (role, error) => endpointErrors.push({role, message: String(error?.message || error)}),
    });
    await waitFor(() => pair.alpha.closed && pair.beta.closed, 'bounded pre-listener queue shutdown');
    assert.equal(endpointErrors.length, 2);
    assert(endpointErrors.every(row => /inbound callback queue exceeded its bound/.test(row.message)));
    await Promise.all(['alpha', 'beta'].map(role => assert.rejects(pair[role].drainInbound(1000), error =>
      error instanceof AggregateError && error.errors.some(row =>
        /inbound callback queue exceeded its bound/.test(String(row?.message || row))))));
    assert.deepEqual(server.fixtureErrors, []);
    await pair.close();
  } finally {
    if (pair) {
      try { await pair.close(); } catch {}
    }
    await server.close();
  }
});

test('pre-listener inbound bytes stop at 1 MiB while each opaque packet remains valid', async () => {
  const server = await startImmediateReadyServer({peerCount: 2,
    packets: ['a'.repeat(600 * 1024), 'b'.repeat(500 * 1024)],
  });
  const endpointErrors = [];
  let pair;
  try {
    pair = await openRoomRelayPeerPair({url: server.url, roomId: 'F'.repeat(24), timeoutMs: 1000,
      onEndpointError: (role, error) => endpointErrors.push({role, message: String(error?.message || error)}),
    });
    await waitFor(() => pair.alpha.closed && pair.beta.closed, 'bounded pre-listener byte queue shutdown');
    assert.equal(endpointErrors.length, 2);
    assert(endpointErrors.every(row => /inbound callback queue exceeded its bound/.test(row.message)));
    await Promise.all(['alpha', 'beta'].map(role => assert.rejects(pair[role].drainInbound(1000), error =>
      error instanceof AggregateError && error.errors.some(row =>
        /inbound callback queue exceeded its bound/.test(String(row?.message || row))))));
    assert.deepEqual(server.fixtureErrors, []);
    await pair.close();
  } finally {
    if (pair) {
      try { await pair.close(); } catch {}
    }
    await server.close();
  }
});

test('room endpoint has no Node module or global dependency and bounds pending sends', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_websocket_relay.mjs', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /^\s*import\b/m, 'the endpoint module has no imports');
  assert.doesNotMatch(source, /\b(?:Buffer|process)\b|\brequire\s*\(|node:/,
    'the endpoint module uses no Node-only globals');
  assert.throws(() => createRoomRelayPeerEndpoint({url: 'ws://127.0.0.1/', role: 'gamma', onMessage: () => {}}),
    /role must be alpha or beta/);
  assert.throws(() => createRoomRelayPeerEndpoint({url: 'https://relay.invalid', role: 'alpha', onMessage: () => {}}),
    /must use ws or wss/);
  const previousBuffer = Object.getOwnPropertyDescriptor(globalThis, 'Buffer');
  const previousProcess = Object.getOwnPropertyDescriptor(globalThis, 'process');
  const previousRequire = Object.getOwnPropertyDescriptor(globalThis, 'require');
  try {
    Object.defineProperty(globalThis, 'Buffer', {configurable: true, writable: true, value: undefined});
    Object.defineProperty(globalThis, 'process', {configurable: true, writable: true, value: undefined});
    Object.defineProperty(globalThis, 'require', {configurable: true, writable: true, value: undefined});
    const portableModule = await import('../scripts/net_lockstep_websocket_relay.mjs?without-node-globals');
    assert.match(portableModule.createRoomId(), /^[A-Za-z0-9_-]{32}$/);
    assert.match(createRoomId(), /^[A-Za-z0-9_-]{32}$/);
  } finally {
    if (previousBuffer) Object.defineProperty(globalThis, 'Buffer', previousBuffer); else delete globalThis.Buffer;
    if (previousProcess) Object.defineProperty(globalThis, 'process', previousProcess); else delete globalThis.process;
    if (previousRequire) Object.defineProperty(globalThis, 'require', previousRequire); else delete globalThis.require;
  }

  const server = await startReadylessWebSocketServer();
  let endpoint;
  let pending = [];
  try {
    endpoint = createRoomRelayPeerEndpoint({url: server.url, roomId: 'E'.repeat(24), role: 'alpha',
      timeoutMs: 100, onMessage: () => {},
    });
    pending.push(endpoint.send('x'.repeat(600 * 1024)));
    await assert.rejects(endpoint.send('y'.repeat(500 * 1024)), /queued send bytes would exceed 1 MiB/);
    for (let index = 0; index < ROOM_RELAY_MAX_PENDING_SEND_MESSAGES - 1; ++index)
      pending.push(endpoint.send('x'));
    await assert.rejects(endpoint.send('last'), /queued send bytes would exceed 1 MiB/);
    const allSends = Promise.allSettled(pending);
    await assert.rejects(endpoint.ready, /still waiting for its second peer/);
    const results = await allSends;
    assert.equal(results.length, ROOM_RELAY_MAX_PENDING_SEND_MESSAGES);
    assert(results.every(result => result.status === 'rejected'));
    await endpoint.close();
    assert(endpoint.closed);
  } finally {
    if (endpoint && !endpoint.closed) {
      try { await endpoint.close(); } catch {}
    }
    await server.close();
  }
});

test('requested transport metadata is available before setup and excludes secrets and room paths', () => {
  assert.deepEqual(describeLockstepTransportAttempt(undefined), {type: 'tcp-loopback'});
  assert.deepEqual(describeLockstepTransportAttempt(
    'wss://user:password@relay.example.test:9443/v1/rooms/secret-room/socket?token=secret#fragment'), {
    type: 'room-websocket', endpoint_origin: 'wss://relay.example.test:9443',
  });
  assert.deepEqual(describeLockstepTransportAttempt('not-a-websocket-url'), {
    type: 'room-websocket', endpoint_origin: null,
  });
});

test('WebSocket transport metadata leaves unavailable TCP byte counters absent', () => {
  const summary = describeLockstepTransport({transport: {
    type: 'room-websocket', peer_limit: 2, packet_limit_bytes: 1024,
  }}, {relayUrl: 'wss://relay.example.test/'});
  assert.deepEqual(summary, {
    type: 'room-websocket', peer_limit: 2, packet_limit_bytes: 1024,
    endpoint_origin: 'wss://relay.example.test',
  });
  recordAvailableTransportMetrics(summary, {transport: {type: 'room-websocket'}});
  assert.equal(Object.hasOwn(summary, 'alpha_to_beta_bytes'), false);
  assert.equal(Object.hasOwn(summary, 'beta_to_alpha_bytes'), false);
});

test('tracked terminal callbacks return their rejection and drain waits for settlement', async () => {
  const callbackFailures = [];
  const queue = createTransportCallbackQueue(failure => callbackFailures.push(failure));
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  let completed = false;
  const callback = queue.track('alpha', 'disconnect-terminal', async () => {
    await gate;
    completed = true;
    throw Error('delayed terminal rejection sentinel');
  });
  let drained = false;
  const drain = queue.drain().then(result => { drained = true; return result; });
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(queue.pendingCount, 1);
  assert.equal(drained, false);
  assert.equal(completed, false);
  release();
  await assert.rejects(callback, /delayed terminal rejection sentinel/);
  const failures = await drain;
  assert.equal(drained, true);
  assert.equal(queue.pendingCount, 0);
  assert.equal(failures.length, 1);
  assert.equal(failures[0].role, 'alpha');
  assert.match(String(failures[0].error), /delayed terminal rejection sentinel/);
  assert.deepEqual(callbackFailures, failures);
});
