import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {verifyPositivePeerCompletion} from '../scripts/net_lockstep_observers.mjs';
import {createBrowserNativePeer, BROWSER_CHECKSUM_EXPORT_LIMIT} from '../scripts/net_lockstep_browser_peer.mjs';
import {LockstepPeer} from '../scripts/net_lockstep_core.mjs';

const turn = () => new Promise(resolve => setImmediate(resolve));
const record = tick => { const row = new Uint8Array(64); new DataView(row.buffer).setUint32(0, tick, true); return Array.from(row); };
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}
function fixture({sourceTicks = 8, subscription = 'valid', checksumConsumer = null, timeoutMs = 5000,
  notifyOnPush = true, ignoreTerminate = false} = {}) {
  const queue = [], records = [], producedRecords = [], terminals = [], remoteRecords = [], rpcNames = [], disposals = [];
  let callbacks, listener, cursor = 0, terminal = {kind: 0, tick: 0, channel: 0};
  let flushOperation = null, drainGate = null, draining = false, accesses = 0, overlap = false;
  let drainCalls = 0, drainEntered = deferred();
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
        const row = record(cursor++);
        records.push(row); producedRecords.push(row);
        if (notifyOnPush) listener?.();
      }
      return true;
    }, confirmStart: async () => true,
    terminate(kind, tick, channel) {
      terminals.push({kind, tick, channel});
      if (!ignoreTerminate) terminal = {kind, tick, channel};
    },
    status: () => ({active: 1, cursor, ring_pending: records.length,
      blocker: terminal.kind ? 'terminal' : cursor === sourceTicks ? 'complete' : 'network_wait', terminal}),
    async drain(max) {
      ++drainCalls; drainEntered.resolve(drainCalls); drainEntered = deferred();
      ++accesses; assert.equal(draining, false); draining = true;
      try { if (drainGate) await drainGate; return records.splice(0, max); }
      finally { draining = false; }
    },
    dispose() { disposals.push(accesses); },
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
    timeoutMs, agreement: {build: 'same'}, native, autonomousPump: true, checksumConsumer},
    {createEndpoint: options => { callbacks = options; return endpoint; }});
  const pageRpc = page.rpc;
  page.rpc = (name, args = []) => { rpcNames.push(name); return pageRpc(name, args); };
  return {page, native, endpoint, records, producedRecords, remote, remoteRecords, terminals, rpcNames, flush,
    waitForNextDrain() { return drainEntered.promise; }, get drainCalls() { return drainCalls; }, disposals,
    remoteDisconnect(reason = 'remote transport closed') { return callbacks.onDisconnect('alpha', reason); },
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

test('owner close terminalizes an early network wait through native disconnect before quiescence', async () => {
  const run = fixture(); await run.start();
  const closed = await run.page.close();
  assert.equal(run.cursor, 2); assert.equal(run.endpoint.closed, true); assert.equal(run.subscribed, false);
  assert.equal(run.terminals.length, 1);
  assert.equal(run.terminals[0].kind, 2, 'early owner close must call LockstepPeer.disconnect');
  assert.equal(closed.nativePump.native_quiescence, 'verified');
  assert.equal(closed.protocol.terminal.kind, 'disconnect');
});

test('remote disconnect before natural completion reaches native terminal and preserves quiescence checks', async () => {
  const run = fixture(); await run.start();
  await run.remoteDisconnect('remote closed early');
  assert.equal(run.cursor, 2);
  assert.equal(run.terminals.length, 1, 'remote disconnect calls native terminate before close');
  const closed = await run.page.close();
  assert.equal(closed.nativePump.native_quiescence, 'verified');
  assert.equal(closed.protocol.terminal.kind, 'disconnect');
  assert.equal(run.endpoint.closed, true);
});

test('native network-wait status still fails unchanged terminal quiescence requirement', async () => {
  const run = fixture({ignoreTerminate: true}); await run.start();
  await assert.rejects(run.page.close(), /close failed/);
  assert.match(run.page.snapshot().failure, /requires complete or terminal native quiescence/);
  assert.equal(run.page.snapshot().nativePump.native_quiescence, 'failed');
  assert.equal(run.cursor, 2); assert.equal(run.endpoint.closed, true); assert.equal(run.subscribed, false);
});

test('fatal escalation joins an in-flight normal close and prevents later native work', async () => {
  const run = fixture(); await run.start();
  let release;
  run.setGate(new Promise(resolve => { release = resolve; }));
  const drainStarted = run.waitForNextDrain();
  const normalClose = run.page.close();
  await drainStarted;
  const terminalCalls = run.terminals.length;
  assert.equal(terminalCalls, 1, 'normal early close has entered its terminal transition');
  const escalated = run.page.close({mode: 'fatal'});
  assert.equal(escalated, normalClose, 'fatal escalation joins the same close operation');
  const accessCount = run.accesses;
  run.notify();
  await assert.rejects(run.page.rpc('addLocalInput', [0, new Uint8Array(11)]), /closing/);
  release(); run.setGate(null);
  await escalated;
  const state = run.page.snapshot();
  assert.equal(state.nativePump.close_mode, 'fatal');
  assert.equal(state.nativePump.native_quiescence, 'aborted-fatal',
    'fatal cleanup must not be reported as a successful native quiescence check');
  assert.equal(run.terminals.length, terminalCalls, 'no native terminal mutation starts after escalation');
  assert.equal(run.accesses, accessCount, 'the already-entered drain was joined; no after-drain status began');
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.subscribed, false);
});

test('fatal close fences queued callbacks, joins entered work and disposes afterward', async () => {
  const run = fixture(); await run.start();
  let release;
  run.setGate(new Promise(resolve => { release = resolve; }));
  const drainStarted = run.waitForNextDrain();
  run.notify();
  await drainStarted;
  const before = run.accesses;
  const draining = run.page.close({mode: 'fatal'});
  run.notify();
  await assert.rejects(run.page.rpc('addLocalInput', [0, new Uint8Array(11)]), /closing/);
  assert.equal(run.accesses, before, 'progress and rejected RPC do not enter native work');
  release(); run.setGate(null);
  await draining;
  assert.equal(run.accesses, before, 'only the already-entered drain completed');
  assert.equal(run.page.snapshot().nativePump.native_quiescence, 'aborted-fatal');
  assert.equal(run.endpoint.closed, true);
  assert.deepEqual(run.disposals, [before], 'adapter disposal follows the joined native drain');
});

test('endpoint join rejection is retained while endpoint and adapter cleanup still run', async () => {
  const run = fixture(); await run.start();
  run.endpoint.drainInbound = async () => { throw Error('fixture inbound join failure'); };
  await assert.rejects(run.page.close(), error => {
    const errors = [...(error.errors || []), ...(error.errors || []).flatMap(item => item.errors || [])];
    return errors.some(item => /fixture inbound join failure/.test(item.message));
  });
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.page.snapshot().nativePump.native_quiescence, 'failed');
  assert.equal(run.disposals.length, 1, 'adapter cleanup still runs after the failed callback join settles');
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

test('page checksum consumer accepts immutable batches while two actual cores compare 520 records', async () => {
  const consumed = [];
  let mutationChecked = false;
  const run = fixture({sourceTicks: 520, checksumConsumer: async batch => {
    assert(Object.isFrozen(batch));
    for (const row of batch) assert(Object.isFrozen(row));
    if (!mutationChecked) {
      mutationChecked = true;
      assert.throws(() => { batch[0][0] = batch[0][0] ^ 0xff; }, TypeError);
      assert.throws(() => batch.push(batch[0]), TypeError);
    }
    consumed.push(...batch.map(row => Array.from(row)));
    return true;
  }});
  await run.start();
  for (let first = 0; first < 518; first += 32) {
    const entries = Array.from({length: Math.min(32, 518 - first)}, (_, offset) =>
      [first + offset, new Uint8Array(11)]);
    await run.page.rpc('addLocalInputs', [entries]);
    await run.remote.addLocalInputs(entries);
    await run.flush(); await idle();
  }
  assert.equal(run.cursor, 520);
  assert.equal(run.remoteRecords.length, 520);
  for (const row of run.remoteRecords) await run.remote.addChecksum(Uint8Array.from(row));
  await run.remote.setNativeProgress(520, {flushFinal: true});
  await run.flush(); await idle();

  const alpha = run.page.snapshot(), beta = run.remote.summary();
  assert.deepEqual(consumed, run.producedRecords);
  assert.deepEqual(run.producedRecords, run.remoteRecords);
  for (const snapshot of [alpha.protocol, beta]) {
    assert.equal(snapshot.local_input_ticks, 518);
    assert.equal(snapshot.remote_ack_input, 517);
    assert.equal(snapshot.local_checksum_ticks, 520);
    assert.equal(snapshot.remote_checksum_ticks, 520);
    assert.equal(snapshot.remote_ack_checksum, 519);
    assert.equal(snapshot.next_checksum_compare, 520);
    assert.deepEqual(snapshot.checksum_mismatches, []);
    assert.equal(snapshot.terminal, null);
  }
  assert.equal(alpha.checksumOwnership.native_records_drained, 520);
  assert.equal(alpha.checksumOwnership.active_native_records_submitted_before_export, 520);
  assert.equal(alpha.checksumOwnership.consumer_accepted_records, 520);
  assert.equal(alpha.checksumOwnership.retained_records, 0);
  assert.equal(alpha.checksumConsumer.pending_batch, null);
  assert.equal(alpha.nativePump.drain_failure, null);
  assert.equal(run.rpcNames.includes('drain'), false);
  assert.equal(run.overlap, false);
  await run.page.close();
});

test('consumer refusal retains exact read-only evidence and rejects diagnostic drain ownership', async () => {
  const run = fixture({notifyOnPush: false, checksumConsumer: async () => false});
  await assert.rejects(run.start(), /must acknowledge the complete batch/);
  const state = run.page.snapshot();
  assert.match(state.failure, /must acknowledge the complete batch/);
  assert.equal(state.protocol.terminal.kind, 'protocol');
  assert.equal(state.checksumOwnership.consumer_accepted_records, 0);
  assert.equal(state.checksumConsumer.pending_batch.count, 2);
  assert.deepEqual(state.checksumConsumer.pending_batch.records, run.producedRecords);
  assert.deepEqual(state.checksumConsumer.retained, run.producedRecords);
  assert(Object.isFrozen(state.checksumConsumer.retained));
  assert(Object.isFrozen(state.checksumConsumer.retained[0]));
  assert.throws(() => state.checksumConsumer.retained[0].push(1), TypeError);
  const accesses = run.accesses;
  await assert.rejects(run.page.rpc('drain'), /disabled while a checksum consumer owns evidence/);
  assert.equal(run.accesses, accesses);
  await assert.rejects(run.page.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});

test('prompt consumer abort during close retains bytes and joins without late native access', async () => {
  const entered = deferred();
  const run = fixture({timeoutMs: 100, checksumConsumer: (batch, {signal}) => {
    entered.resolve({batch, signal});
    return new Promise(resolve => signal.addEventListener('abort', () => resolve(true), {once: true}));
  }});
  const starting = run.start().then(() => null, error => error);
  const offered = await entered.promise;
  assert.equal(offered.signal.aborted, false);
  const closing = run.page.close();
  await assert.rejects(closing, /close failed/);
  assert.match((await starting).message, /cancelled during close/);
  assert.equal(offered.signal.aborted, true);
  const closed = run.page.snapshot();
  assert.deepEqual(closed.checksumConsumer.pending_batch.records, run.producedRecords);
  assert.equal(closed.checksumConsumer.pending_batch.aborted, true);
  assert.equal(closed.checksumConsumer.pending_batch.joined, true);
  assert.equal(closed.checksumOwnership.consumer_accepted_records, 0);
  const accesses = run.accesses;
  run.notify(); await idle();
  assert.equal(run.accesses, accesses);
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.subscribed, false);
});

test('consumer timeout and ignored abort remain explicit, bounded, and retain exact bytes', async () => {
  const entered = deferred();
  let release;
  const run = fixture({timeoutMs: 20, checksumConsumer: (batch, {signal}) => {
    entered.resolve({batch, signal});
    return new Promise(resolve => { release = resolve; });
  }});
  const starting = run.start().then(() => null, error => error);
  const offered = await entered.promise;
  await new Promise(resolve => setTimeout(resolve, 35));
  assert.match((await starting).message, /did not acknowledge within 20ms/);
  const timedOut = run.page.snapshot();
  assert.match(timedOut.failure, /did not acknowledge within 20ms/);
  assert.equal(offered.signal.aborted, true);
  assert.deepEqual(timedOut.checksumConsumer.pending_batch.records, run.producedRecords);
  assert.equal(timedOut.checksumOwnership.consumer_accepted_records, 0);
  await assert.rejects(run.page.close(), /close failed/);
  const closed = run.page.snapshot();
  assert.equal(closed.checksumConsumer.pending_batch.unjoined, true);
  assert.deepEqual(closed.checksumConsumer.pending_batch.records, run.producedRecords);
  const accesses = run.accesses;
  release(true); await idle();
  const late = run.page.snapshot();
  assert.equal(late.checksumConsumer.pending_batch.accepted, false);
  assert.deepEqual(late.checksumConsumer.pending_batch.records, run.producedRecords);
  assert.equal(late.checksumOwnership.consumer_accepted_records, 0);
  assert.equal(run.accesses, accesses);
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

test('startup consumer failures set protocol and native terminal without a progress notification', async () => {
  for (const consumer of [() => { throw Error('fixture consumer throw'); },
    () => Promise.reject(Error('fixture consumer reject')), () => undefined, () => 1]) {
    const run = fixture({notifyOnPush: false, checksumConsumer: consumer});
    await assert.rejects(run.start(), /consumer throw|consumer reject|complete batch/);
    assert.equal(run.page.snapshot().protocol.terminal.kind, 'protocol');
    assert.equal(run.terminals.length, 1);
    assert.equal(run.terminals[0].kind, 3);
    assert.deepEqual(run.page.snapshot().checksumConsumer.retained, run.producedRecords);
    assert.equal(run.page.snapshot().checksumConsumer.accepted_records, 0);
    await assert.rejects(run.page.close(), /close failed/);
    assert.equal(run.endpoint.closed, true);
  }
});

test('slow consumer holds one batch while wakes coalesce and native work stays serialized', async () => {
  const entered = deferred(), release = deferred(); let calls = 0;
  const run = fixture({checksumConsumer: async () => {
    ++calls;
    if (calls === 1) { entered.resolve(); await release.promise; }
    return true;
  }});
  const starting = run.start(); await entered.promise;
  const before = run.page.snapshot().nativePump.completed_wakeups;
  for (let i = 0; i < 100; ++i) run.notify();
  const publication = run.page.rpc('addLocalInput', [0, new Uint8Array(11)]);
  await run.remote.addLocalInput(0, new Uint8Array(11));
  const incoming = run.flush(); await turn();
  assert.equal(calls, 1);
  assert.equal(run.page.snapshot().checksumConsumer.pending_batch.count, 2);
  assert.equal(run.page.snapshot().checksumConsumer.retained_records, 2);
  release.resolve(); await Promise.all([starting, publication, incoming]); await idle();
  assert.equal(run.overlap, false);
  assert(run.page.snapshot().nativePump.completed_wakeups - before <= 3);
  assert.equal(run.page.snapshot().checksumConsumer.pending_batch, null);
  await run.page.rpc('disconnect', ['fixture cleanup']); await run.page.close();
});

test('remote terminal cancels a pending consumer without accepting its late acknowledgment', async () => {
  const entered = deferred();
  const run = fixture({checksumConsumer: (batch, {signal}) => {
    entered.resolve(signal);
    return new Promise(resolve => signal.addEventListener('abort', () => resolve(true), {once: true}));
  }});
  const starting = run.start().then(() => null, error => error); const signal = await entered.promise;
  await run.remote.fail('desync', {tick: 0, channel: 1}); await run.flush();
  assert.match((await starting).message, /desync terminal/);
  const state = run.page.snapshot();
  assert.equal(signal.aborted, true);
  assert.equal(state.protocol.terminal.kind, 'desync');
  assert.equal(run.terminals[0].kind, 1);
  assert.equal(state.checksumConsumer.accepted_records, 0);
  assert.deepEqual(state.checksumConsumer.retained, run.producedRecords);
  await assert.rejects(run.page.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});

test('close aborts outside the queue and reports an ignored signal without waiting for callback release', async () => {
  const entered = deferred(), release = deferred();
  const run = fixture({timeoutMs: 20, checksumConsumer: (batch, {signal}) => {
    entered.resolve(signal); return release.promise;
  }});
  const starting = run.start().then(() => null, error => error); const signal = await entered.promise;
  await assert.rejects(run.page.close(), /close failed/);
  assert.match((await starting).message, /cancelled during close/);
  const state = run.page.snapshot();
  assert.equal(signal.aborted, true);
  assert.equal(state.checksumConsumer.pending_batch.unjoined, true);
  assert.equal(state.checksumConsumer.pending_batch.callback_settled, false);
  assert.equal(run.endpoint.closed, true);
  const accesses = run.accesses;
  release.resolve(true); await idle();
  assert.equal(run.page.snapshot().checksumConsumer.accepted_records, 0);
  assert.deepEqual(run.page.snapshot().checksumConsumer.retained, run.producedRecords);
  assert.equal(run.accesses, accesses);
});
