import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {verifyPositivePeerCompletion} from '../scripts/net_lockstep_observers.mjs';
import {createBrowserNativePeer, BROWSER_CHECKSUM_EXPORT_LIMIT} from '../scripts/net_lockstep_browser_peer.mjs';
import {LockstepPeer} from '../scripts/net_lockstep_core.mjs';

const turn = () => new Promise(resolve => setImmediate(resolve));
const record = tick => { const row = new Uint8Array(64); new DataView(row.buffer).setUint32(0, tick, true); return Array.from(row); };
function fixture({sourceTicks = 8, subscription = 'valid'} = {}) {
  const queue = [], records = [], terminals = [], remoteRecords = [];
  let callbacks, listener, cursor = 0, terminal = {kind: 0, tick: 0, channel: 0};
  let flushOperation = null, drainGate = null, draining = false, accesses = 0, overlap = false;
  const native = {
    subscribeProgress(callback) {
      if (subscription === 'throw') throw Error('fixture subscription failure');
      listener = callback;
      if (subscription === 'missing-owner') return undefined;
      return () => { listener = null; if (subscription === 'unsubscribe-throw') throw Error('fixture unsubscribe failure'); };
    },
    async pushIndexed(first, bytes) {
      if (draining) overlap = true;
      ++accesses;
      for (let offset = 0; offset < bytes.length; offset += 44) {
        assert.equal(first + offset / 44, cursor);
        records.push(record(cursor++));
        listener?.();
      }
      return true;
    }, confirmStart: async () => true,
    terminate(kind, tick, channel) { terminal = {kind, tick, channel}; terminals.push(terminal); },
    status: () => ({active: 1, cursor, ring_pending: records.length,
      blocker: terminal.kind ? 'terminal' : cursor === sourceTicks ? 'complete' : 'network_wait', terminal}),
    async drain(max) {
      ++accesses; assert.equal(draining, false); draining = true;
      try { if (drainGate) await drainGate; return records.splice(0, max); }
      finally { draining = false; }
    },
  };
  const remote = new LockstepPeer({role: 'beta', sourceTicks, inputTicks: sourceTicks - 2,
    pushFrame: async (first, bytes) => { for (let offset = 0; offset < bytes.length; offset += 44) remoteRecords.push(record(first + offset / 44)); }});
  remote.attach(async text => queue.push(['alpha', text]));
  async function flush() {
    if (flushOperation) return flushOperation;
    flushOperation = (async () => { let work = 0; while (queue.length) {
      assert(++work < 10000); const [role, text] = queue.shift();
      if (role === 'alpha') await callbacks.onMessage(text); else await remote.receive(text);
    }})();
    try { await flushOperation; } finally { flushOperation = null; }
  }
  const endpoint = {ready: Promise.resolve(), closed: false, errors: [], transport: {type: 'synthetic-component'},
    send: async text => queue.push(['beta', text]), drainInbound: flush,
    async close() { endpoint.closed = true; }};
  const page = createBrowserNativePeer({role: 'alpha', sourceTicks, inputTicks: sourceTicks - 2,
    agreement: {build: 'same'}, native, autonomousPump: true},
    {createEndpoint: options => { callbacks = options; return endpoint; }});
  return {page, native, endpoint, records, remote, remoteRecords, terminals, flush,
    async start() { await remote.start({build: 'same'}); await page.rpc('start'); await idle(); },
    notify(error = null) { listener?.(error); }, setGate(gate) { drainGate = gate; },
    get cursor() { return cursor; }, get accesses() { return accesses; }, get overlap() { return overlap; },
    get subscribed() { return listener !== null; },
  };
}
async function idle() { for (let i = 0; i < 12; ++i) await turn(); }

test('actual controller/core drains newly generated native records without a polling RPC', async () => {
  const run = fixture(); await run.start();
  await run.page.rpc('addLocalInput', [0, new Uint8Array(11)]);
  await run.remote.addLocalInput(0, new Uint8Array(11)); await run.flush(); await idle();
  assert.equal(run.cursor, 3);
  assert.equal(run.page.snapshot().protocol.local_checksum_ticks, 3);
  assert.equal(run.page.snapshot().exportRecords, 3);
  assert.equal(run.records.length, 0);
  for (let tick = 1; tick < 6; ++tick) {
    await run.page.rpc('addLocalInput', [tick, new Uint8Array(11)]);
    await run.remote.addLocalInput(tick, new Uint8Array(11)); await run.flush(); await idle();
  }
  for (const row of run.remoteRecords) await run.remote.addChecksum(Uint8Array.from(row));
  await run.remote.setNativeProgress(8, {flushFinal: true}); await run.flush(); await idle();
  const beforeExport = run.page.snapshot();
  assert.equal(beforeExport.protocol.local_checksum_ticks, 8);
  assert.equal(beforeExport.protocol.next_checksum_compare, 8);
  assert.equal(beforeExport.protocol.remote_ack_input, 5);
  const exported = await run.page.rpc('drain'); assert.equal(exported.records.length, 8);
  await run.page.close(); assert.equal(run.subscribed, false);
  const accesses = run.accesses; run.notify(); await idle();
  await run.page.rpc('snapshot'); await run.page.rpc('drain');
  assert.equal(run.accesses, accesses);
});

test('wakeups coalesce and native drain/push operations cannot overlap', async () => {
  const run = fixture(); await run.start();
  let release; run.setGate(new Promise(resolve => { release = resolve; }));
  const before = run.page.snapshot().nativePump.completed_wakeups;
  run.notify(); await turn();
  for (let i = 0; i < 100; ++i) run.notify();
  const publication = run.page.rpc('addLocalInput', [0, new Uint8Array(11)]);
  await run.remote.addLocalInput(0, new Uint8Array(11)); const incoming = run.flush();
  release(); run.setGate(null); await Promise.all([publication, incoming]); await idle();
  assert.equal(run.overlap, false);
  assert(run.page.snapshot().nativePump.completed_wakeups - before <= 3);
  await run.page.rpc('disconnect', ['fixture cleanup']); await run.page.close();
});

test('close joins a queued pump and retains trailing terminal evidence without source mutation', async () => {
  const run = fixture(); await run.start(); await run.page.rpc('disconnect', ['fixture terminal']);
  run.records.push(record(2)); run.notify();
  const closing = run.page.close(); assert.equal(run.subscribed, false);
  await closing;
  assert.equal(run.cursor, 2);
  assert.equal(run.page.snapshot().checksumOwnership.post_terminal_native_evidence_records, 1);
  assert.equal(run.page.snapshot().exportRecords, 3);
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.page.close(), closing);
});

test('close at a network wait fails explicitly and closes transport without advancing source', async () => {
  const run = fixture(); await run.start();
  await assert.rejects(run.page.close(), /close failed/);
  assert.match(run.page.snapshot().failure, /requires complete or terminal native quiescence/);
  assert.equal(run.cursor, 2); assert.equal(run.endpoint.closed, true); assert.equal(run.subscribed, false);
});

test('background overflow is sticky, protocol terminal and retains the bounded offending drain', async () => {
  const run = fixture({sourceTicks: 516}); await run.start();
  run.records.push(...Array.from({length: BROWSER_CHECKSUM_EXPORT_LIMIT}, (_, tick) => record(tick + 2)));
  run.notify(); await idle();
  const state = run.page.snapshot();
  assert.match(state.failure, /512-record bound/);
  assert.equal(state.protocol.terminal.kind, 'protocol');
  assert.equal(state.nativePump.drain_failure.returned_records, 511);
  assert.equal(state.nativePump.drain_failure.retained_records.length, 511);
  await assert.rejects(run.page.rpc('drain'), /512-record bound/);
  await assert.rejects(run.page.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});

test('malformed native records surface background failure and terminal without an unhandled rejection', async () => {
  const run = fixture(); await run.start(); run.records.push(Array(64).fill(256)); run.notify(); await idle();
  assert.match(run.page.snapshot().failure, /malformed records/);
  assert.equal(run.page.snapshot().protocol.terminal.kind, 'protocol');
  assert.equal(run.page.snapshot().nativePump.drain_failure.retained_records[0][0], 256);
  await assert.rejects(run.page.close(), /close failed/);
});


test('close waits for in-flight native drain and rejects racing evidence RPC', async () => {
  const run = fixture(); await run.start(); await run.page.rpc('disconnect', ['fixture terminal']);
  let release; run.setGate(new Promise(resolve => { release = resolve; }));
  run.notify(); await turn(); let done = false;
  const closing = run.page.close().then(() => { done = true; });
  await assert.rejects(run.page.rpc('drain'), /is closing/);
  await turn(); assert.equal(done, false);
  release(); run.setGate(null); await closing; assert.equal(done, true);
});

test('close rejects a changed actual native boundary', async () => {
  const run = fixture(); await run.start(); await run.page.rpc('disconnect', ['fixture terminal']);
  const status = run.native.status; let calls = 0;
  run.native.status = () => ({...status(), cursor: ++calls === 1 ? 2 : 3});
  await assert.rejects(run.page.close(), /close failed/);
  assert.match(run.page.snapshot().failure, /boundary changed/);
  assert.equal(run.endpoint.closed, true); assert.equal(run.subscribed, false);
});

test('native drain rejection reaches sticky background failure and native protocol terminal', async () => {
  const run = fixture(); await run.start();
  run.native.drain = async () => { throw Error('native fixture drain rejection'); };
  run.notify(); await idle();
  assert.match(run.page.snapshot().failure, /native fixture drain rejection/);
  assert.equal(run.terminals.at(-1).kind, 3);
  await assert.rejects(run.page.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});


for (const subscription of ['throw', 'missing-owner']) {
  test(`subscription ${subscription} returns failed controller with joined endpoint cleanup`, async () => {
    const run = fixture({subscription});
    assert.match(run.page.snapshot().failure, /subscription failure|requires an unsubscribe owner/);
    await assert.rejects(run.page.rpc('start'), /subscription failure|requires an unsubscribe owner/);
    await assert.rejects(run.page.close(), /close failed/);
    assert.equal(run.endpoint.closed, true);
    const accesses = run.accesses; run.notify(); await idle(); assert.equal(run.accesses, accesses);
  });
}
test('unsubscribe failure remains explicit while endpoint cleanup is joined', async () => {
  const run = fixture({subscription: 'unsubscribe-throw'}); await run.start();
  await run.page.rpc('disconnect', ['fixture terminal']);
  await assert.rejects(run.page.close(), /close failed/);
  assert.match(run.page.snapshot().failure, /fixture unsubscribe failure/);
  assert.equal(run.endpoint.closed, true); assert.equal(run.subscribed, false);
});


test('progress observer failure becomes sticky controller failure and a native protocol terminal', async () => {
  const run = fixture(); await run.start();
  run.notify(Error('original native observation failure')); await idle();
  assert.match(run.page.snapshot().failure, /original native observation failure/);
  assert.equal(run.terminals.at(-1).kind, 3);
  await assert.rejects(run.page.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});


const browserSource = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
const intervalSource = browserSource.slice(browserSource.indexOf('async function observeNativePumpWithoutRpc('),
  browserSource.indexOf('async function pollRun('));
async function runActualInterval({completedBefore = false, injectedRpc = false, stalled = false, failure = null} = {}) {
  let rounds = 0, now = 0;
  const pairResults = {}, instances = {};
  const makeSnapshot = role => {
    const complete = completedBefore || rounds > 0;
    const count = complete && !stalled ? 8 : 2;
    return {failure, endpointErrors: [], exportRecords: count,
      nativePump: {enabled: true, rpc_calls: injectedRpc && rounds > 0 ? 5 : 4},
      protocol: {role, ready: true, terminal: null, checksum_mismatches: [],
        remote_ack_input: complete ? 5 : -1, local_input_ticks: 6, remote_input_ticks: 6,
        local_checksum_ticks: count, remote_checksum_ticks: count, next_checksum_compare: count,
        next_source_frame: 8}};
  };
  for (const role of ['alpha', 'beta']) instances[role] = {
    readPeerSnapshot: async () => makeSnapshot(role),
    peerRpc: () => { throw Error('test must never invoke peer RPC'); },
  };
  const context = vm.createContext({instances, pairResults, sourceTicks: 8, usedInputs: 6, stallMs: 3,
    pollMs: 1, deadline: 100, Date: {now: () => now}, verifyPositivePeerCompletion,
    checkedHealth: async role => ({status: {active: 1, cursor: makeSnapshot(role).exportRecords,
      blocker: makeSnapshot(role).exportRecords === 8 ? 'complete' : 'network_wait', terminal: {kind: 0}}, native: {phase: 1}}),
    sleep: async () => { ++rounds; ++now; }});
  await vm.runInContext(`${intervalSource}; observeNativePumpWithoutRpc()`, context);
  return pairResults;
}
test('actual harness interval observes progress using pure reads and exact unchanged RPC counts', async () => {
  const result = await runActualInterval();
  assert.equal(result.native_pump_interval.no_peer_RPC_during_interval, true);
  assert.equal(result.native_pump_interval.before.alpha.status.cursor, 2);
  assert.equal(result.native_pump_interval.after.alpha.status.cursor, 8);
  assert.equal(result.native_pump_interval.after.beta.snapshot.protocol.next_checksum_compare, 8);
});
test('actual interval rejects already complete, intervening RPC, sticky failure and missing autonomous progress', async () => {
  await assert.rejects(runActualInterval({completedBefore: true}), /completed before/);
  await assert.rejects(runActualInterval({injectedRpc: true}), /peer RPC occurred/);
  await assert.rejects(runActualInterval({failure: 'actual retained failure'}), /diagnostic peer failed/);
  await assert.rejects(runActualInterval({stalled: true}), /bounded no-RPC interval/);
});
