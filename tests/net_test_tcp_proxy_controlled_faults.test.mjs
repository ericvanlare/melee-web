import assert from 'node:assert/strict';
import net from 'node:net';
import test from 'node:test';
import {startTcpProxy} from '../scripts/net_test_tcp_proxy.mjs';

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate, label) {
  const deadline = Date.now() + 2500;
  while (!predicate() && Date.now() < deadline) await pause(2);
  assert(predicate(), label);
}
async function fixture(options = {}) {
  const sockets = new Set();
  const server = net.createServer({allowHalfOpen: true}, socket => {
    sockets.add(socket);
    socket.on('data', bytes => socket.write(bytes));
    socket.on('end', () => socket.end());
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const proxy = await startTcpProxy({targetPort: server.address().port, ...options});
  const clients = [];
  async function client() {
    const before = new Set(proxy.snapshot().connections.active.map(row => row.connectionId));
    const socket = net.connect(proxy.address.port, proxy.address.host);
    const chunks = [], errors = [];
    socket.on('data', bytes => chunks.push(bytes));
    socket.on('error', error => errors.push(String(error)));
    const closed = new Promise(resolve => socket.once('close', resolve));
    await new Promise(resolve => socket.once('connect', resolve));
    await until(() => proxy.snapshot().connections.active.some(row => !before.has(row.connectionId)), 'accepted');
    const id = proxy.snapshot().connections.active.find(row => !before.has(row.connectionId)).connectionId;
    const row = {socket, id, closed, errors, bytes: () => Buffer.concat(chunks)};
    clients.push(row);
    socket.write('hello');
    await until(() => row.bytes().toString() === 'hello', 'upstream connected and first echo returned');
    return row;
  }
  async function close() {
    const final = await proxy.stop();
    for (const client of clients) client.socket.destroy();
    await Promise.all(clients.map(client => client.closed));
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
    assert.equal(final.lifecycle.activeConnections, 0);
    assert.equal(final.lifecycle.activeTimers, 0);
    assert.equal(final.lifecycle.cleanShutdown, true);
    return final;
  }
  return {proxy, client, close};
}

test('post-connect stall holds one stream while another advances, then retains exact bytes', async () => {
  const run = await fixture({eventLimit: 1});
  try {
    const a = await run.client(), b = await run.client();
    const receipt = run.proxy.stallConnection(a.id, 'clientToWorker', 180);
    assert.equal(receipt.queuedBytes, 0);
    assert.equal(receipt.inFlightBytesNotStalled, 0);
    a.socket.write('held-one');
    b.socket.write('free-one');
    await until(() => b.bytes().toString() === 'hellofree-one', 'unselected stream advances');
    assert.equal(a.bytes().toString(), 'hello');
    a.socket.write('held-two');
    a.socket.end(); b.socket.end();
    await Promise.all([a.closed, b.closed]);
    assert.equal(a.bytes().toString(), 'helloheld-oneheld-two');
    const final = await run.close();
    assert.equal(final.dataPathPass, true);
    assert.equal(final.controlledFaults.length, 1);
    assert.equal(final.diagnostics.events.length, 1);
    assert(final.diagnostics.eventsOmitted > 0);
    assert(final.connections.closed.every(row => row.hashesMatch));
    assert(final.directions.clientToWorker.enqueueToWriteCallbackMsMax >= 130);
  } finally { await run.close(); }
});

test('queued and subsequent chunks retain FIFO and bandwidth spacing after a controlled stall', async () => {
  const run = await fixture({profile: {clientToWorker: {seed: 1, bytesPerSecond: 8192}, workerToClient: {seed: 2}}});
  try {
    const a = await run.client();
    const first = Buffer.alloc(1024, 0x38), second = Buffer.alloc(1024, 0xc9);
    a.socket.write(first);
    await until(() => run.proxy.snapshot().connections.active[0].directions.clientToWorker.pendingBytes === 1024, 'first queued');
    const receipt = run.proxy.stallConnection(a.id, 'clientToWorker', 120);
    assert.equal(receipt.queuedBytes, 1024);
    assert.equal(receipt.inFlightBytesNotStalled, 0);
    a.socket.end(second);
    await a.closed;
    assert.deepEqual(a.bytes(), Buffer.concat([Buffer.from('hello'), first, second]));
    const final = await run.close();
    assert.equal(final.dataPathPass, true);
    assert(final.directions.clientToWorker.serializationDelayMsTotal >= 250);
    assert.equal(final.directions.clientToWorker.stallEvents, 1);
    assert.equal(final.directions.clientToWorker.stallDurationMsTotal, 120);
    const writes = final.diagnostics.events.filter(row => row.event === 'release' && row.direction === 'clientToWorker' && row.bytes === 1024);
    assert.equal(writes.length, 2);
    assert(BigInt(writes[1].scheduledReleaseNs) - BigInt(writes[0].scheduledReleaseNs) >= 125_000_000n);
    // Actual write callbacks can run late; scheduled eligibility retains serialization spacing.
    assert(final.connections.closed[0].hashesMatch);
  } finally { await run.close(); }
});

test('post-connect cut is explicit failure of one connection and leaves another usable', async () => {
  const run = await fixture();
  try {
    const a = await run.client(), b = await run.client();
    run.proxy.cutConnection(a.id);
    await a.closed;
    b.socket.end('still-live');
    await b.closed;
    assert.equal(b.bytes().toString(), 'hellostill-live');
    const final = await run.close();
    assert.equal(final.dataPathPass, false);
    assert.equal(final.lifecycle.cutConnections, 1);
    assert.equal(final.lifecycle.failedConnections, 1);
    assert.equal(final.lifecycle.cleanConnections, 1);
    assert.equal(final.controlledFaults[0].connectionId, a.id);
    assert.throws(() => run.proxy.cutConnection(b.id), /active connected/);
  } finally { await run.close(); }
});

test('refuses invalid or repeated controls and joins stop during a queued stall', async () => {
  const run = await fixture();
  try {
    const a = await run.client();
    assert.throws(() => run.proxy.cutConnection(999), /active connected/);
    assert.throws(() => run.proxy.stallConnection(a.id, 'unknown', 10), /direction/);
    assert.throws(() => run.proxy.stallConnection(a.id, 'clientToWorker', 0), /durationMs/);
    run.proxy.stallConnection(a.id, 'clientToWorker', 300);
    assert.throws(() => run.proxy.stallConnection(a.id, 'clientToWorker', 20), /Only one/);
    a.socket.write('will-be-discarded');
    await until(() => run.proxy.snapshot().connections.active[0].directions.clientToWorker.pendingBytes > 0, 'queued before stop');
    const final = await run.close();
    assert.equal(final.dataPathPass, false);
    assert.equal(final.directions.clientToWorker.bytesDroppedOnStop, Buffer.byteLength('will-be-discarded'));
    assert.throws(() => run.proxy.stallConnection(a.id, 'workerToClient', 10), /active connected/);
    assert.equal((await run.proxy.stop()).controlledFaults.length, 1);
  } finally { await run.close(); }
});

test('control receipt capacity stays bounded across sequential retired connections', async () => {
  const run = await fixture({maxConnections: 1});
  try {
    for (let i = 0; i < 3; ++i) {
      const a = await run.client();
      run.proxy.stallConnection(a.id, 'workerToClient', 1);
      a.socket.end(); await a.closed;
      assert.throws(() => run.proxy.cutConnection(a.id), /active connected/);
    }
    const a = await run.client();
    assert.throws(() => run.proxy.cutConnection(a.id), /capacity/);
    a.socket.end(); await a.closed;
    const final = await run.close();
    assert.equal(final.controlledFaults.length, 3);
    assert.equal(final.dataPathPass, true);
  } finally { await run.close(); }
});

test('a controlled stall does not recall a write whose real callback is held by the fixture', async () => {
  const run = await fixture();
  const originalWrite = net.Socket.prototype.write;
  let releaseCallback = null;
  try {
    const a = await run.client();
    const targetPort = run.proxy.snapshot().target.port;
    // Keep the real TCP write, but delay its completion notification at the
    // proxy's upstream socket. This deterministically exercises inFlight.
    net.Socket.prototype.write = function(chunk, ...args) {
      const callback = args.at(-1);
      if (this.remotePort === targetPort && Buffer.isBuffer(chunk) &&
          chunk.toString() === 'already-submitted' && typeof callback === 'function') {
        args[args.length - 1] = error => { releaseCallback = () => callback(error); };
      }
      return originalWrite.call(this, chunk, ...args);
    };
    a.socket.write('already-submitted');
    await until(() => releaseCallback !== null, 'real write callback held');
    await until(() => a.bytes().toString() === 'helloalready-submitted', 'submitted bytes already arrived');
    const receipt = run.proxy.stallConnection(a.id, 'clientToWorker', 120);
    assert.equal(receipt.inFlightBytesNotStalled, Buffer.byteLength('already-submitted'));
    a.socket.end('future');
    releaseCallback(); releaseCallback = null;
    net.Socket.prototype.write = originalWrite;
    await a.closed;
    assert.equal(a.bytes().toString(), 'helloalready-submittedfuture');
    const final = await run.close();
    assert.equal(final.dataPathPass, true);
    assert.equal(final.controlledFaults[0].inFlightBytesNotStalled, Buffer.byteLength('already-submitted'));
    assert(final.connections.closed[0].hashesMatch);
  } finally {
    net.Socket.prototype.write = originalWrite;
    releaseCallback?.();
    await run.close();
  }
});


test('late stall retains one real enqueue/release after the diagnostic prefix saturates', async () => {
  const run = await fixture({eventLimit: 1});
  try {
    const a = await run.client();
    const before = run.proxy.snapshot();
    assert.equal(before.diagnostics.events.length, 1);
    assert(before.diagnostics.eventsOmitted > 0);
    const receipt = run.proxy.stallConnection(a.id, 'clientToWorker', 120);
    assert.equal(run.proxy.snapshot().controlledStallProofs[0].enqueue, null);
    a.socket.write('late-held');
    await until(() => run.proxy.snapshot().controlledStallProofs[0].enqueue !== null, 'retained late enqueue');
    const pending = run.proxy.snapshot().controlledStallProofs[0];
    assert.equal(pending.release, null, 'enqueue does not fabricate release');
    assert.equal(a.bytes().toString(), 'hello');
    // Caller mutation of a snapshot cannot forge future proxy evidence.
    pending.enqueue.sequence = -1;
    await until(() => a.bytes().toString() === 'hellolate-held', 'actual release and echo');
    a.socket.end(); await a.closed;
    const final = await run.close();
    const [proof] = final.controlledStallProofs;
    assert.equal(final.controlledStallProofs.length, 1);
    assert.equal(proof.connectionId, a.id);
    assert.equal(proof.activatedNs, receipt.activatedNs);
    assert.equal(proof.nextReleaseNs, receipt.nextReleaseNs);
    assert.equal(proof.enqueue.sequence, proof.release.sequence);
    assert(proof.enqueue.sequence >= 0);
    assert.equal(proof.enqueue.bytes, Buffer.byteLength('late-held'));
    assert.equal(proof.enqueue.bytes, proof.release.bytes);
    assert(BigInt(proof.enqueue.enqueuedNs) >= BigInt(receipt.activatedNs));
    assert(BigInt(proof.release.writeCallbackNs) >= BigInt(receipt.nextReleaseNs));
    assert.equal(final.diagnostics.events.length, 1);
    assert.equal(final.dataPathPass, true);
  } finally { await run.close(); }
});


test('early timer callbacks cannot release real socket bytes before monotonic eligibility', async () => {
  const run = await fixture({eventLimit: 1});
  const originalSetTimeout = globalThis.setTimeout;
  let earlyWakeups = 0;
  try {
    const a = await run.client();
    globalThis.setTimeout = (callback, delay, ...args) => {
      if (delay > 2) {
        earlyWakeups++;
        return originalSetTimeout(callback, 1, ...args);
      }
      return originalSetTimeout(callback, delay, ...args);
    };
    const receipt = run.proxy.stallConnection(a.id, 'clientToWorker', 80);
    a.socket.write('early-timer-control');
    await until(() => a.bytes().toString() === 'helloearly-timer-control', 'eligible write echoed');
    globalThis.setTimeout = originalSetTimeout;
    a.socket.end(); await a.closed;
    const final = await run.close();
    assert(earlyWakeups > 0, 'fixture delivered actual timer callbacks before requested delay');
    const proof = final.controlledStallProofs[0];
    assert(BigInt(proof.release.writeCallbackNs) >= BigInt(receipt.nextReleaseNs));
    assert(BigInt(proof.release.writeCallbackNs) >= BigInt(proof.release.scheduledReleaseNs));
    assert.equal(final.dataPathPass, true);
    assert.equal(final.directions.clientToWorker.bytesDroppedOnStop, 0);
  } finally {
    globalThis.setTimeout = originalSetTimeout;
    await run.close();
  }
});
