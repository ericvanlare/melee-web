import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {createBrowserNativePeer} from '../scripts/net_lockstep_browser_peer.mjs';
import {browserPeerFacade} from '../scripts/net_session_instance.mjs';
const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
const first = source.indexOf('async function publishAllInputs(');
const last = source.indexOf('\nasync function publishDisconnectPrefix(', first);
assert(first >= 0 && last > first);
const publisher = source.slice(first, last);
const roles = ['alpha', 'beta'];

// Actual publisher, facade RPC, and peer/core APIs; native and endpoint queues
// are synthetic. No native, browser, or WebRTC desync evidence is claimed.
async function scenario({injection = 'positive', mode = 'flip', usedInputs = 18, terminalKind = 'desync', mixed = false} = {}) {
  const controllers = {}, callbacks = {}, endpoints = {}, facades = {}, frames = {}, nativeTerminals = {};
  const queue = [], held = [], calls = [], drains = [];
  let flushing = null, holdStates = false, holdTerminals = false, injected = false, now = 0, refreshes = 0;
  async function flush() {
    if (flushing) return flushing;
    flushing = (async () => {
      let work = 0;
      while (queue.length) {
        assert(++work < 1000, 'pure endpoint queue must quiesce');
        const [role, text] = queue.shift();
        await callbacks[role].onMessage(text);
      }
    })();
    try { await flushing; } finally { flushing = null; }
  }
  for (const role of roles) {
    frames[role] = []; nativeTerminals[role] = [];
    controllers[role] = createBrowserNativePeer({role, sourceTicks: usedInputs + 2, inputTicks: usedInputs,
      relayUrl: 'ws://pure-fixture.invalid', roomId: 'a'.repeat(32), agreement: {build: 'pure-rpc-fixture'},
      native: {
        async pushIndexed(tick, bytes) { frames[role].push({tick, bytes: Array.from(bytes)}); return true; },
        async confirmStart() { return true; },
        terminate(...args) { nativeTerminals[role].push(args); },
        status: () => ({cursor: frames[role].length}),
        drain: () => [],
      }}, {createEndpoint(options) {
        callbacks[role] = options;
        return endpoints[role] = {ready: Promise.resolve(), closed: false, errors: [],
          transport: {type: 'synthetic-in-memory-RPC-fixture'},
          async send(text) {
            const packet = JSON.parse(text), remote = role === 'alpha' ? 'beta' : 'alpha';
            if ((holdStates && packet.type === 'state') || (holdTerminals && packet.type === 'terminal')) held.push({role, packet});
            else queue.push([remote, text]);
          },
          drainInbound: flush,
          async close() { this.closed = true; },
        };
      }});
  }
  await Promise.all(roles.map(role => controllers[role].rpc('start', [])));
  await flush();
  async function inject() {
    assert(!injected); injected = true;
    // Exercise the actual RPC/core terminal path. This is an explicit scheduling
    // fixture, not a claim that synthetic native records caused a real desync.
    if (mixed) holdTerminals = true;
    await controllers.alpha.rpc('fail', [terminalKind, {tick: 12, channel: 1}]);
    if (mixed) await controllers.beta.rpc('fail', ['protocol', {reason: 'independent bad terminal'}]);
    await flush();
  }
  for (const role of roles) {
    facades[role] = browserPeerFacade({
      async peerRpc(name, args) {
        calls.push({role, name});
        let row = await controllers[role].rpc(name, args);
        if (!injected && name === 'addLocalInputs' && injection === `after-${role}-add`) {
          await inject(); row = await controllers[role].rpc('snapshot');
        }
        return row;
      },
      closePeer: () => controllers[role].close(),
    }, await controllers[role].rpc('snapshot'));
    assert(facades[role].ready);
  }
  holdStates = !['positive', 'between-batches'].includes(injection);
  const context = vm.createContext({
    usedInputs, scenario: mode, flip: mode === 'flip' ? {tick: 10, byte: 2, bit: 0, port: 0} : null, browserOwned: true,
    localSample: () => new Uint8Array(11),
    nativeSample: () => { const bytes = new Uint8Array(11); bytes[2] = 1; return bytes; },
    Date: {now: () => now}, deadline: 1000, stallMs: 10,
    sleep: async ms => { now += ms; },
    async refreshBrowserPeers() {
      ++refreshes;
      if (!injected && injection === 'first-ack-refresh' && refreshes === 4) await inject();
      await Promise.all(roles.map(role => facades[role].refresh()));
    },
    async drainChecksums(role, peer) {
      const row = await peer.drain(); drains.push({role, count: row.count});
      if (!injected && injection === 'between-batches' && role === 'beta') await inject();
    },
  });
  const publish = vm.runInContext(`${publisher}\npublishAllInputs`, context);
  let error = null;
  try { await publish(facades.alpha, facades.beta); }
  catch (caught) { error = String(caught?.message || caught); }
  const snapshots = Object.fromEntries(roles.map(role => [role, controllers[role].snapshot()]));
  const closeErrors = Object.fromEntries(await Promise.all(roles.map(async role => {
    try { await controllers[role].close(true); return [role, null]; }
    catch (caught) { return [role, String(caught?.message || caught)]; }
  })));
  return {injection, error, injected, elapsed_fixture_ms: now, refreshes, calls, drains,
    held_state_packets: held.length, snapshots, closeErrors, native_terminals: nativeTerminals,
    synthetic_native_queued_frames: Object.fromEntries(roles.map(role => [role, frames[role].length])),
    endpoint_closed: Object.fromEntries(roles.map(role => [role, endpoints[role].closed]))};
}

function addCalls(row) { return row.calls.filter(call => call.name === 'addLocalInputs'); }
function assertDesync(row) {
  for (const role of roles) {
    assert.equal(row.snapshots[role].protocol.terminal.kind, 'desync');
    assert.equal(row.snapshots[role].protocol.terminal.tick, 12);
    assert.equal(row.snapshots[role].protocol.terminal.channel, 1);
    assert.deepEqual(row.native_terminals[role], [[1, 12, 1]]);
    assert.equal(row.endpoint_closed[role], true);
  }
}
test('positive publication requires actual exact ACK through every batch', async () => {
  const row = await scenario({usedInputs: 40, mode: 'positive'});
  assert.equal(row.error, null);
  assert.equal(addCalls(row).length, 4);
  for (const role of roles) {
    assert.equal(row.snapshots[role].protocol.remote_ack_input, 39);
    assert.equal(row.snapshots[role].protocol.local_input_ticks, 40);
    assert.equal(row.snapshots[role].protocol.terminal, null);
  }
});
for (const injection of ['after-alpha-add', 'after-beta-add', 'first-ack-refresh']) {
  test(`actual RPC desync ${injection} exits publication into downstream validation`, async () => {
    const row = await scenario({injection});
    assert.equal(row.error, null);
    assert.equal(row.elapsed_fixture_ms, 0);
    assert.equal(addCalls(row).length, injection === 'after-alpha-add' ? 1 : 2);
    assertDesync(row);
    for (const role of roles) assert.equal(row.snapshots[role].protocol.remote_ack_input, -1);
  });
}
test('terminal delivered while draining prevents publication of the next batch', async () => {
  const row = await scenario({injection: 'between-batches', usedInputs: 40});
  assert.equal(row.error, null);
  assert.equal(addCalls(row).length, 2);
  assert.equal(row.drains.length, 2);
  assertDesync(row);
  for (const role of roles) assert.equal(row.snapshots[role].protocol.local_input_ticks, 32);
});
for (const terminalKind of ['disconnect', 'protocol']) {
  test(`unexpected actual ${terminalKind} fails during flip publication`, async () => {
    const row = await scenario({injection: 'after-alpha-add', terminalKind});
    assert.match(row.error, /Unexpected terminal during input publication/);
    assert.equal(addCalls(row).length, 1);
    assert.equal(row.snapshots.alpha.protocol.terminal.kind, terminalKind);
  });
}
test('actual desync is rejected during positive publication', async () => {
  const row = await scenario({injection: 'after-alpha-add', mode: 'positive'});
  assert.match(row.error, /Unexpected terminal during input publication/);
  assert.equal(addCalls(row).length, 1);
});
test('expected desync cannot mask another actual bad terminal', async () => {
  const row = await scenario({injection: 'after-alpha-add', mixed: true});
  assert.match(row.error, /Unexpected terminal during input publication/);
  assert.equal(row.snapshots.alpha.protocol.terminal.kind, 'desync');
  assert.equal(row.snapshots.beta.protocol.terminal.kind, 'protocol');
  assert.equal(addCalls(row).length, 1);
});
test('missing actual ACK without terminal still fails at the bounded wait', async () => {
  const row = await scenario({injection: 'missing-ack'});
  assert.match(row.error, /Loopback input acknowledgement stalled before tick 17/);
  assert.equal(row.elapsed_fixture_ms, 12);
  for (const role of roles) {
    assert.equal(row.snapshots[role].protocol.terminal, null);
    assert.equal(row.snapshots[role].protocol.remote_ack_input, -1);
  }
});
