import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import test from 'node:test';
import {
  createTransportCallbackQueue, describeLockstepTransport, describeLockstepTransportAttempt,
  openLockstepPeerPair, recordAvailableTransportMetrics,
} from '../scripts/net_lockstep_transport.mjs';

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
  await assert.rejects(openLockstepPeerPair({relayUrl: `ws://127.0.0.1:${port}`, timeoutMs: 250}),
    /Room relay WebSocket/);
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
