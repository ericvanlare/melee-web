"""Exercise explicit simulated-network admission and real HTTP/proxy calibration."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from owned_test_workspace import OwnedWorkspaceTests


CONTRACT = r"""
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {resolveNetworkSimulation, observeBrowserNetworkPaths} from './scripts/a3_network_simulation.mjs';
assert.equal(resolveNetworkSimulation(), null);
for (const bad of ['', null, 'public', 'constructor', '__proto__'])
  assert.throws(() => resolveNetworkSimulation({profileName: bad}), /must be one of/);
for (const extra of [{relayUrl: 'ws://127.0.0.1:1234'}, {preflight: null}, {preflight: 'file.json'}])
  assert.throws(() => resolveNetworkSimulation({profileName: 'moderate', ...extra}), /cannot be combined/);
const first = resolveNetworkSimulation({profileName: 'moderate'});
first.profile.clientToWorker.delayMs = 999;
const second = resolveNetworkSimulation({profileName: 'moderate'});
assert.equal(second.mode, 'simulated-network');
assert.equal(second.profile.clientToWorker.delayMs, 10);
assert.equal(second.profile_sha256, first.profile_sha256);
const instances = {alpha: {page: new EventEmitter()}, beta: {page: new EventEmitter()}};
const observer = observeBrowserNetworkPaths(instances, 'ws://127.0.0.1:4242');
const room = 'abcdefghijklmnopqrstuvwxyz012345';
assert.throws(() => observer.verify(room), /exactly one/);
for (const instance of Object.values(instances))
  instance.page.emit('websocket', {url: () => 'ws://127.0.0.1:4242/v1/rooms/' + room + '/socket'});
assert.equal(observer.verify(room).verified, true);
instances.beta.page.emit('websocket', {url: () => 'ws://127.0.0.1:4343/v1/rooms/' + room + '/socket'});
assert.throws(() => observer.verify(room), /exactly one/);
observer.close();
assert.equal(instances.alpha.page.listenerCount('websocket'), 0);
assert.equal(instances.beta.page.listenerCount('websocket'), 0);
console.log('simulation admission and browser-path refusal controls pass');
"""

REAL_HTTP = r"""
import assert from 'node:assert/strict';
import http from 'node:http';
import {resolveNetworkSimulation, startNetworkSimulation, calibrateNetworkSimulation,
  verifyNetworkProxy, closeNetworkProxy}
  from './scripts/a3_network_simulation.mjs';
let badResponse = false;
const server = http.createServer((request, response) => {
  assert.equal(request.url, '/v1/rooms/too-short/socket');
  response.writeHead(400, {'content-type': 'application/json'});
  response.end(badResponse ? '{"error":"different"}' : '{"error":"invalid room id"}');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const worker = {port: server.address().port};
let simulation;
try {
  simulation = await startNetworkSimulation({worker,
    configuration: resolveNetworkSimulation({profileName: 'moderate'})});
  assert.notEqual(new URL(simulation.relayUrl).port, String(worker.port));
  const result = await calibrateNetworkSimulation({
    workerBase: 'http://127.0.0.1:' + worker.port,
    proxyBase: simulation.httpBase, remaining: () => 5000});
  assert.equal(result.samples.length, 3);
  assert.equal(result.peer_rtt_ms, null);
  for (const sample of result.samples) {
    assert.equal(sample.direct.body_sha256, sample.shaped.body_sha256);
    assert.equal(sample.direct.bytes, sample.shaped.bytes);
    assert(sample.shaped.round_trip_ms > 0);
  }
  badResponse = true;
  await assert.rejects(calibrateNetworkSimulation({
    workerBase: 'http://127.0.0.1:' + worker.port,
    proxyBase: simulation.httpBase, remaining: () => 5000}), /actual Worker invalid-room response/);
  assert.equal(verifyNetworkProxy(simulation.proxy.snapshot()).verified, true);
  const final = await closeNetworkProxy(simulation.proxy);
  assert.equal(verifyNetworkProxy(final, {final: true}).verified, true);
  for (const corrupt of [
    row => { row.diagnostics.socketErrors = 1; },
    row => { row.directions.clientToWorker.bytesDroppedOnStop = 1; },
    row => { row.connections.closed[0].directions.workerToClient.releasedSha256 = '0'.repeat(64); },
    row => { row.connections.closedOmitted = 1; },
    row => { delete row.connections.closed[0].directions.clientToWorker.bytesEnqueued;
      delete row.connections.closed[0].directions.clientToWorker.bytesReleased; },
    row => { row.connections.closed[0].directions.clientToWorker.bytesEnqueued = -1;
      row.connections.closed[0].directions.clientToWorker.bytesReleased = -1; },
    row => { row.connections.closed[0].directions.clientToWorker.bytesEnqueued++;
      row.connections.closed[0].directions.clientToWorker.bytesReleased++; },
    row => { row.lifecycle.activeTimers = 1; },
    row => { row.dataPathPass = null; },
  ]) {
    const damaged = structuredClone(final);
    corrupt(damaged);
    assert.throws(() => verifyNetworkProxy(damaged, {final: true}), /Network proxy:/);
  }
} finally {
  if (simulation) await simulation.proxy.stop();
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
}
console.log('real HTTP/proxy calibration and incorrect-response refusal pass');
"""


class A3NetworkSimulationTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "network-simulation-cli-")

    def run_node(self, source):
        result = subprocess.run([str(node_runtime(ROOT)), "--input-type=module", "-e", source],
                                cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_explicit_mode_and_browser_path_contract(self):
        self.assertIn("refusal controls pass", self.run_node(CONTRACT))

    def test_real_http_proxy_calibration(self):
        self.assertIn("incorrect-response refusal pass", self.run_node(REAL_HTTP))

    def test_actual_cli_rejects_ambiguous_modes_before_startup(self):
        output = self.work / "must-not-be-created"
        base = [str(node_runtime(ROOT)), str(ROOT / "scripts/a3_local_room_keyboard_browser.mjs"),
                "--url", "http://127.0.0.1:8787/runtime.html",
                "--disc", str(self.work / "missing-disc.ciso"), "--out", str(output)]
        for flags, expected in [
            (["--network-profile", "moderate"], "ENOENT"),
            (["--network-profile", "unknown"], "must be one of"),
            (["--network-profile", "moderate", "--relay-url", "wss://example.invalid"], "cannot be combined"),
            (["--network-profile", "moderate", "--relay-preflight", "missing.json"], "cannot be combined"),
        ]:
            with self.subTest(flags=flags):
                result = subprocess.run(base + flags, cwd=ROOT, capture_output=True,
                                        text=True, timeout=15)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
