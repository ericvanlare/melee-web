import assert from 'node:assert/strict';
import net from 'node:net';
import test from 'node:test';
import {startTcpProxy} from '../scripts/net_test_tcp_proxy.mjs';

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate, label) {
  const deadline = Date.now() + 2000;
  while (!predicate() && Date.now() < deadline) await pause(2);
  assert(predicate(), label);
}

test('active proxy snapshot separates queued bytes from an in-flight write', async () => {
  const targetSockets = new Set();
  const target = net.createServer({allowHalfOpen: true}, socket => {
    targetSockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => targetSockets.delete(socket));
    socket.on('data', bytes => socket.write(bytes));
  });
  await new Promise(resolve => target.listen(0, '127.0.0.1', resolve));
  let proxy;
  let client;
  try {
    proxy = await startTcpProxy({targetPort: target.address().port,
      profile: {clientToWorker: {seed: 11, bytesPerSecond: 1024},
        workerToClient: {seed: 12}}});
    client = net.connect(proxy.address.port, proxy.address.host);
    client.on('error', () => {});
    await new Promise(resolve => client.once('connect', resolve));
    await until(() => proxy.snapshot().connections.active.length === 1,
      'owned proxy socket was not accepted');
    const active = proxy.snapshot().connections.active[0];
    const bytes = Buffer.alloc(1024, 0x61);
    client.write(bytes);
    await until(() => {
      const row = proxy.snapshot().connections.active.find(item => item.connectionId === active.connectionId);
      return row?.directions?.clientToWorker?.queuedBytes === bytes.length;
    }, 'exact queued-byte count was not exposed');
    const beforeStall = proxy.snapshot().connections.active[0].directions.clientToWorker;
    assert.equal(beforeStall.pendingBytes,
      beforeStall.queuedBytes + beforeStall.inFlightBytes);
    const receipt = proxy.stallConnection(active.connectionId, 'clientToWorker', 600);
    assert.equal(receipt.queuedBytes, bytes.length);
    const duringStall = proxy.snapshot().connections.active[0].directions.clientToWorker;
    assert.equal(duringStall.queuedBytes, bytes.length);
    assert.equal(duringStall.pendingBytes,
      duringStall.queuedBytes + duringStall.inFlightBytes);
    const final = await proxy.stop();
    assert.equal(final.lifecycle.cleanShutdown, true);
    assert.equal(final.directions.clientToWorker.bytesDroppedOnStop, bytes.length);
    assert.equal(final.dataPathPass, false);
  } finally {
    if (proxy) await proxy.stop().catch(() => {});
    client?.destroy();
    for (const socket of targetSockets) socket.destroy();
    await new Promise(resolve => target.close(() => resolve()));
  }
});
