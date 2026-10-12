"""Focused real-loopback tests for the opaque TCP network simulator proxy."""

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402


def run_node(source: str, timeout: int = 20) -> None:
    result = subprocess.run(
        [str(node_runtime(ROOT)), "--input-type=module", "-e", source],
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=ROOT,
    )
    if result.returncode:
        raise AssertionError(
            f"Node integration check failed ({result.returncode})\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


class TcpTestProxyTests(unittest.TestCase):
    def test_binary_round_trip_half_close_and_clean_stop(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';

const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const targetPort = server.address().port;
const proxy = await startTcpProxy({targetPort, maxPendingBytesPerDirection: 16 * 1024});
const input = Buffer.alloc(512 * 1024);
for (let i = 0; i < input.length; i++) input[i] = (i * 73 + (i >>> 3)) & 0xff;
const output = [];
const client = net.connect(proxy.address.port, proxy.address.host);
client.on('data', chunk => output.push(chunk));
const closed = new Promise((resolve, reject) => {
  client.once('close', resolve);
  client.once('error', reject);
});
client.end(input);
await closed;
const response = Buffer.concat(output);
assert.deepEqual(response, input, 'proxy must preserve every byte in order');
const beforeStop = proxy.snapshot();
assert.equal(beforeStop.dataPathPass, true);
assert.equal(beforeStop.lifecycle.activeConnections, 0);
assert.equal(beforeStop.lifecycle.cleanConnections, 1);
assert.equal(beforeStop.connections.closed[0].hashesMatch, true);
assert.equal(beforeStop.directions.clientToWorker.bytesEnqueued, input.length);
assert.equal(beforeStop.directions.clientToWorker.bytesReleased, input.length);
assert.equal(beforeStop.directions.workerToClient.bytesEnqueued, input.length);
assert.equal(beforeStop.directions.workerToClient.bytesReleased, input.length);
assert.equal(beforeStop.directions.clientToWorker.maxPendingBytes <= 16 * 1024, true);
assert.equal(beforeStop.directions.workerToClient.maxPendingBytes <= 16 * 1024, true);
const final = await proxy.stop();
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.lifecycle.listenerClosed, true);
assert.equal(final.lifecycle.activeTimers, 0);
await new Promise(resolve => server.close(resolve));
""")

    def test_small_live_writes_return_before_source_fin(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';
const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const proxy = await startTcpProxy({targetPort: server.address().port});
const client = net.connect(proxy.address.port, proxy.address.host);
const output = [];
client.on('data', chunk => output.push(chunk));
const closed = new Promise((resolve, reject) => {
  client.once('close', resolve); client.once('error', reject);
});
await new Promise(resolve => client.once('connect', resolve));
// Neither small read may depend on EOF or filling the proxy's 16 KiB read cap.
for (const text of ['abc', 'defghij']) {
  client.write(text);
  const expected = text === 'abc' ? 'abc' : 'abcdefghij';
  const deadline = Date.now() + 2000;
  while (Buffer.concat(output).length < expected.length && Date.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(Buffer.concat(output).toString(), expected);
  assert.equal(proxy.snapshot().lifecycle.activeConnections, 1);
}
client.end();
await closed;
const final = await proxy.stop();
assert.equal(final.dataPathPass, true);
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.directions.clientToWorker.bytesReleased, 10);
assert.equal(final.directions.workerToClient.bytesReleased, 10);
await new Promise(resolve => server.close(resolve));
""")

    def test_sequential_connections_have_independent_receipts(self):
        run_node(r"""
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';
const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const proxy = await startTcpProxy({targetPort: server.address().port});
const inputs = [Buffer.alloc(37, 0x19), Buffer.alloc(211, 0xa7)];
for (const input of inputs) {
  const output = [];
  const client = net.connect(proxy.address.port, proxy.address.host);
  client.on('data', chunk => output.push(chunk));
  const closed = new Promise((resolve, reject) => {
    client.once('close', resolve); client.once('error', reject);
  });
  client.end(input);
  await closed;
  assert.deepEqual(Buffer.concat(output), input);
}
const final = await proxy.stop();
assert.equal(final.dataPathPass, true);
assert.equal(final.connections.closed.length, 2);
for (const [i, row] of final.connections.closed.entries()) {
  assert.equal(row.clean, true);
  for (const direction of Object.values(row.directions)) {
    assert.equal(direction.bytesEnqueued, inputs[i].length);
    assert.equal(direction.bytesReleased, inputs[i].length);
    assert.equal(direction.enqueuedSha256, createHash('sha256').update(inputs[i]).digest('hex'));
    assert.equal(direction.releasedSha256, direction.enqueuedSha256);
  }
}
for (const name of ['clientToWorker', 'workerToClient']) {
  assert.equal(final.directions[name].bytesReleased, 248);
  assert.equal(final.directions[name].bytesEnqueued, 248);
  assert.equal(final.directions[name].maxPendingBytes, 211, 'max is not summed across connections');
}
await new Promise(resolve => server.close(resolve));
""")

    def test_connection_can_close_while_another_has_pending_bytes(self):
        run_node(r"""
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';
const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cap = 16 * 1024;
const proxy = await startTcpProxy({targetPort: server.address().port,
  maxPendingBytesPerDirection: cap, profile: {
    clientToWorker: {seed: 101, bytesPerSecond: 32 * 1024},
    workerToClient: {seed: 202},
  }});
function transfer(input) {
  const output = [];
  const client = net.connect(proxy.address.port, proxy.address.host);
  client.on('data', chunk => output.push(chunk));
  const closed = new Promise((resolve, reject) => {
    client.once('close', () => resolve(Buffer.concat(output))); client.once('error', reject);
  });
  client.end(input);
  return closed;
}
const large = Buffer.alloc(32 * 1024, 0x91);
const small = Buffer.alloc(29, 0x42);
const largeDone = transfer(large);
const deadline = Date.now() + 2000;
while (!proxy.snapshot().connections.active.some(row => row.directions.clientToWorker.pendingBytes >= cap)
    && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 5));
assert(proxy.snapshot().connections.active.some(row => row.directions.clientToWorker.pendingBytes >= cap));
assert.deepEqual(await transfer(small), small);
const middle = proxy.snapshot();
assert.equal(middle.lifecycle.activeConnections, 1);
assert.equal(middle.lifecycle.cleanConnections, 1);
assert.equal(middle.connections.closed.length, 1);
assert.equal(middle.connections.closed[0].clean, true);
assert.equal(middle.connections.closed[0].directions.clientToWorker.bytesReleased, small.length);
assert.equal(middle.dataPathPass, null, 'pending active connection is not final agreement');
for (const name of ['clientToWorker', 'workerToClient']) {
  const rows = [...middle.connections.closed, ...middle.connections.active];
  assert.equal(middle.directions[name].bytesEnqueued,
    rows.reduce((sum, row) => sum + row.directions[name].bytesEnqueued, 0));
  assert.equal(middle.directions[name].bytesReleased,
    rows.reduce((sum, row) => sum + row.directions[name].bytesReleased, 0));
}
assert.deepEqual(await largeDone, large);
const final = await proxy.stop();
assert.equal(final.dataPathPass, true);
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.lifecycle.cleanConnections, 2);
for (const [index, row] of final.connections.closed.entries()) {
  const input = index === 0 ? small : large;
  for (const direction of Object.values(row.directions)) {
    assert.equal(direction.bytesEnqueued, input.length);
    assert.equal(direction.bytesReleased, input.length);
    assert.equal(direction.releasedSha256, createHash('sha256').update(input).digest('hex'));
  }
}
for (const name of ['clientToWorker', 'workerToClient']) {
  assert.equal(final.directions[name].bytesEnqueued, large.length + small.length);
  assert.equal(final.directions[name].bytesReleased, large.length + small.length);
  assert(final.directions[name].maxPendingBytes <= cap, 'max remains per direction, not sum');
}
await new Promise(resolve => server.close(resolve));
""", timeout=25)

    def test_cut_threshold_is_per_connection(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';
const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
  socket.on('error', () => {});
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const proxy = await startTcpProxy({targetPort: server.address().port, profile: {
  clientToWorker: {seed: 1, cutAfterBytes: 10}, workerToClient: {seed: 2},
}});
for (const byte of [0x15, 0x73]) {
  const client = net.connect(proxy.address.port, proxy.address.host);
  client.resume();
  const closed = new Promise(resolve => client.once('close', resolve));
  client.on('error', () => {});
  client.end(Buffer.alloc(100, byte));
  await closed;
}
const final = await proxy.stop();
assert.equal(final.lifecycle.cutConnections, 2);
assert.equal(final.connections.closed.length, 2);
for (const row of final.connections.closed) {
  assert.equal(row.reason, 'cut');
  assert.equal(row.directions.clientToWorker.bytesEnqueued, 10);
  assert.equal(row.directions.clientToWorker.bytesReleased, 10);
}
assert.equal(final.directions.clientToWorker.bytesEnqueued, 20);
assert.equal(final.directions.clientToWorker.bytesReleased, 20);
assert.equal(final.directions.clientToWorker.bytesCutAtThreshold, 180);
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.dataPathPass, false);
await new Promise(resolve => server.close(resolve));
""")

    def test_seeded_jitter_and_threshold_stall_are_measured_per_leg(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';

const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const profile = {
  clientToWorker: {seed: 71, delayMs: 3, jitterMs: 7,
    stallAfterBytes: 1, stallDurationMs: 90},
  workerToClient: {seed: 97, delayMs: 0, jitterMs: 0},
};
const proxy = await startTcpProxy({targetPort: server.address().port, profile});
const input = Buffer.from('byte-order-is-unchanged');
const chunks = [];
const started = process.hrtime.bigint();
const client = net.connect(proxy.address.port, proxy.address.host);
client.on('data', chunk => chunks.push(chunk));
const closed = new Promise((resolve, reject) => {
  client.once('close', resolve);
  client.once('error', reject);
});
client.end(input);
await closed;
const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
assert.deepEqual(Buffer.concat(chunks), input);
assert.ok(elapsedMs >= 75, `threshold stall should be visible, elapsed=${elapsedMs}ms`);
const snapshot = proxy.snapshot();
const c2w = snapshot.directions.clientToWorker;
assert.equal(c2w.stallEvents, 1);
assert.equal(c2w.stallDurationMsTotal, 90);
assert.ok(c2w.jitterSamples > 0);
assert.ok(c2w.sampledJitterMsMax <= 7);
assert.equal(snapshot.dataPathPass, true);
const final = await proxy.stop();
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.diagnostics.integrityFailure, false);
await new Promise(resolve => server.close(resolve));
""")

    def test_bandwidth_stall_backpressure_and_byte_bound(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';

const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('end', () => socket.end());
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cap = 16 * 1024;
const profile = {
  clientToWorker: {seed: 123, bytesPerSecond: 32 * 1024},
  workerToClient: {seed: 456},
};
const proxy = await startTcpProxy({targetPort: server.address().port,
  profile, maxPendingBytesPerDirection: cap});
const input = Buffer.alloc(32 * 1024, 0x5a);
const chunks = [];
const started = process.hrtime.bigint();
const client = net.connect(proxy.address.port, proxy.address.host);
client.on('data', chunk => chunks.push(chunk));
const closed = new Promise((resolve, reject) => {
  client.once('close', resolve);
  client.once('error', reject);
});
client.end(input);
await closed;
const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
assert.deepEqual(Buffer.concat(chunks), input);
assert.ok(elapsedMs >= 800, `bandwidth limit should shape this transfer, elapsed=${elapsedMs}ms`);
const snapshot = proxy.snapshot();
const c2w = snapshot.directions.clientToWorker;
assert.ok(c2w.serializationDelayMsTotal >= 1000);
assert.ok(c2w.queuePressureEpisodes > 0);
assert.ok(c2w.maxPendingBytes <= cap);
assert.equal(snapshot.dataPathPass, true);
const final = await proxy.stop();
assert.equal(final.lifecycle.cleanShutdown, true);
await new Promise(resolve => server.close(resolve));
""", timeout=25)

    def test_cut_fails_closed_and_reports_trimmed_bytes(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';

const server = net.createServer({allowHalfOpen: true}, socket => {
  socket.on('data', chunk => socket.write(chunk));
  socket.on('error', () => {}); // A deliberate proxy cut can reset the Worker-side TCP socket.
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const proxy = await startTcpProxy({targetPort: server.address().port, profile: {
  clientToWorker: {seed: 1, cutAfterBytes: 10},
  workerToClient: {seed: 2},
}});
const output = [];
const client = net.connect(proxy.address.port, proxy.address.host);
client.on('data', chunk => output.push(chunk));
const closed = new Promise(resolve => client.once('close', resolve));
client.on('error', error => assert.equal(error.code, 'ECONNRESET',
  'only the deliberate proxy reset is expected in the cut fixture'));
client.end(Buffer.alloc(100, 0xa7));
await closed;
assert.ok(Buffer.concat(output).length <= 10,
  'a cut must not release bytes beyond its configured stream boundary');
const final = await proxy.stop();
assert.equal(final.dataPathPass, false);
assert.equal(final.lifecycle.cutConnections, 1);
assert.equal(final.connections.closed[0].clean, false);
assert.equal(final.connections.closed[0].reason, 'cut');
assert.equal(final.directions.clientToWorker.bytesCutAtThreshold, 90);
assert.equal(final.directions.clientToWorker.bytesReleased, 10);
assert.equal(final.lifecycle.cleanShutdown, true);
await new Promise(resolve => server.close(resolve));
""")

    def test_stop_discards_queued_bytes_idempotently_and_refuses_bad_profiles(self):
        run_node(r"""
import assert from 'node:assert/strict';
import net from 'node:net';
import {startTcpProxy} from './scripts/net_test_tcp_proxy.mjs';

for (const profile of [
  {clientToWorker: {seed: 1, surprise: true}, workerToClient: {seed: 2}},
  {clientToWorker: {seed: 1}, workerToClient: {seed: 0}},
  {clientToWorker: {seed: 1, stallAfterBytes: 4}, workerToClient: {seed: 2}},
]) {
  await assert.rejects(startTcpProxy({targetPort: 1, profile}), TypeError);
}
await assert.rejects(startTcpProxy({targetHost: '0.0.0.0', targetPort: 1}), TypeError);
await assert.rejects(startTcpProxy({targetHost: 'localhost', targetPort: 1}), TypeError);

const server = net.createServer();
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const proxy = await startTcpProxy({targetPort: server.address().port, profile: {
  clientToWorker: {seed: 5, delayMs: 5_000},
  workerToClient: {seed: 6},
}});
const client = net.connect(proxy.address.port, proxy.address.host);
const closed = new Promise(resolve => client.once('close', resolve));
await new Promise(resolve => client.once('connect', resolve));
client.write(Buffer.alloc(4096, 0x33));
const deadline = Date.now() + 2000;
while (!proxy.snapshot().directions.clientToWorker.bytesEnqueued && Date.now() < deadline)
  await new Promise(resolve => setTimeout(resolve, 5));
assert.ok(proxy.snapshot().directions.clientToWorker.bytesEnqueued > 0);
const first = proxy.stop();
assert.strictEqual(proxy.stop(), first, 'stop returns one shared completion promise');
const final = await first;
await closed;
assert.equal(final.lifecycle.cleanShutdown, true);
assert.equal(final.lifecycle.listenerClosed, true);
assert.equal(final.lifecycle.activeConnections, 0);
assert.equal(final.lifecycle.activeTimers, 0);
assert.equal(final.dataPathPass, false);
assert.ok(final.directions.clientToWorker.bytesDroppedOnStop > 0);
assert.equal(final.directions.clientToWorker.bytesDroppedOnCut, 0);
await new Promise(resolve => server.close(resolve));
""")


if __name__ == "__main__":
    unittest.main()
