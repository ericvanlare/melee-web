import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {readyRenderCallback, readyRenderEvent, verifyAccountedRenderReadiness, verifyPositivePeerCompletion} from '../scripts/net_lockstep_observers.mjs';
import {collapseConsecutiveScenes, validateActiveMatchRoute} from '../scripts/net_determinism_contract.mjs';
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
const intervalSource = browserSource.slice(browserSource.indexOf('function nativePumpChecksumEvidence('),
  browserSource.indexOf('async function pollRun('));
const fullRouteCompletionStart = browserSource.indexOf('function verifyRuntimeFullRoutePeerCompletion(');
const fullRouteCompletionSource = browserSource.slice(fullRouteCompletionStart,
  browserSource.indexOf('\nfunction routeBoundaryPath(', fullRouteCompletionStart));
async function runActualInterval({completedBefore = false, injectedRpc = false, stalled = false, failure = null,
  runtimeCssSss = false, runtimeCssMatch = false, runtimeFullRoute = false, beforeCursor = null,
  matchBoundary = {}, matchFrameSequence = null} = {}) {
  let rounds = 0, now = 0;
  const runtimeCssLiveRoute = runtimeCssSss || runtimeCssMatch;
  const runtimeOwned = runtimeCssLiveRoute || runtimeFullRoute;
  const sourceTicks = runtimeFullRoute ? 5084 : runtimeCssLiveRoute ? 520 : 8;
  const usedInputs = sourceTicks - 2;
  const pairResults = {}, instanceRows = {alpha: {}, beta: {}}, instances = {};
  const matchReads = {alpha: 0, beta: 0};
  const activeMatch = {ready: true, paused: false, ending: false, complete: false, frame: 1,
    rules: {stage: 0x20, player_stocks: [4, 4]},
    players: [{fighter: 0, human: true, stocks: 4}, {fighter: 0, human: true, stocks: 4}]};
  const makeSnapshot = role => {
    const complete = completedBefore || rounds > 0;
    const count = complete && !stalled ? sourceTicks : beforeCursor ??
      (runtimeFullRoute ? 306 : runtimeCssLiveRoute ? 510 : 2);
    const protocol = {role, ready: true, terminal: null, checksum_mismatches: [],
      remote_ack_input: complete ? usedInputs - 1 : count - 2, local_input_ticks: Math.min(count, usedInputs),
      remote_input_ticks: Math.min(count, usedInputs), local_checksum_ticks: count,
      remote_checksum_ticks: count, next_checksum_compare: count, next_source_frame: sourceTicks,
      ...(runtimeFullRoute ? {remote_ack_checksum: complete ? sourceTicks - 1 : count - 1} : {})};
    const snapshot = {failure, endpointErrors: [], exportRecords: runtimeOwned ? 0 : count,
      nativePump: {enabled: true, rpc_calls: injectedRpc && rounds > 0 ? 5 : 4}, protocol,
      ...(runtimeOwned ? {checksumConsumer: {enabled: true, accepted_records: count,
        pending_batch: null, retained_records: 0}, checksumOwnership: {consumer_accepted_records: count}} : {})};
    return snapshot;
  };
  for (const role of ['alpha', 'beta']) instances[role] = {
    readPeerSnapshot: async () => makeSnapshot(role),
    runtimeLockstepSnapshot: async () => ({armed: true, closing: false, failure: null,
      peer: makeSnapshot(role), checksums: Array.from({length: makeSnapshot(role).protocol.local_checksum_ticks}, (_, tick) => record(tick))}),
    matchObservationBoundary: async () => {
      const sequence = matchFrameSequence?.[role];
      const observation = sequence?.length ? {...activeMatch,
        frame: sequence[Math.min(matchReads[role], sequence.length - 1)]} : activeMatch;
      ++matchReads[role];
      const base = {stable_cursor: true, stable_phase: true,
        native_before: {phase: 7, running: 1, error: null}, native_after: {phase: 7, running: 1, error: null},
        status_before: {active: 1, cursor: 306, blocker: 'network_wait', terminal: {kind: 0}},
        status_after: {active: 1, cursor: 306, blocker: 'network_wait', terminal: {kind: 0}}, observation};
      return {...base, ...matchBoundary,
        native_before: {...base.native_before, ...matchBoundary.native_before},
        native_after: {...base.native_after, ...matchBoundary.native_after},
        status_before: {...base.status_before, ...matchBoundary.status_before},
        status_after: {...base.status_after, ...matchBoundary.status_after}};
    },
    peerRpc: () => { throw Error('test must never invoke peer RPC'); },
  };
  const context = vm.createContext({instances, instanceRows, pairResults, runtimeOwned, runtimeCssSss,
    runtimeCssMatch, runtimeCssLiveRoute, runtimeFullRoute,
    NET_RECORD_BYTES: 64, sourceTicks, usedInputs, stallMs: 3,
    pollMs: 1, deadline: 100, Date: {now: () => now}, verifyPositivePeerCompletion,
    verifyRuntimeFullRoutePeerCompletion: vm.runInNewContext(
      `${fullRouteCompletionSource}\nverifyRuntimeFullRoutePeerCompletion`,
      {verifyPositivePeerCompletion, usedInputs, sourceTicks}), validateActiveMatchRoute,
    checkedHealth: async role => ({status: {active: 1,
      cursor: makeSnapshot(role).protocol.local_checksum_ticks,
      blocker: makeSnapshot(role).protocol.local_checksum_ticks === sourceTicks ? 'complete' : 'network_wait',
      terminal: {kind: 0}}, native: {phase: runtimeFullRoute || runtimeCssMatch ? 7 : runtimeCssSss ? 3 : 1}}),
    sleep: async () => { ++rounds; ++now; }});
  await vm.runInContext(`${intervalSource}; observeNativePumpWithoutRpc()`, context);
  return {...pairResults, instanceRows};
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

test('runtime CSS-to-SSS interval observes retained partial evidence and crosses source cursor 512 without peer RPC', async () => {
  const result = await runActualInterval({runtimeCssSss: true});
  assert.equal(result.native_pump_interval.before.alpha.status.cursor, 510);
  assert.equal(result.native_pump_interval.after.alpha.status.cursor, 520);
  assert.equal(result.native_pump_interval.crossed_512_each, true);
  assert.equal(result.native_pump_interval.no_peer_RPC_during_interval, true);
  assert.equal(result.native_pump_interval.complete, true);
  await assert.rejects(runActualInterval({runtimeCssSss: true, beforeCursor: 512}), /passed source cursor 512/);
  await assert.rejects(runActualInterval({runtimeCssSss: true, injectedRpc: true}), /peer RPC occurred/);
});

test('runtime CSS-to-match interval crosses source cursor 512 without peer RPC', async () => {
  const result = await runActualInterval({runtimeCssMatch: true});
  assert.equal(result.native_pump_interval.before.alpha.status.cursor, 510);
  assert.equal(result.native_pump_interval.after.alpha.status.cursor, 520);
  assert.equal(result.native_pump_interval.crossed_512_each, true);
  assert.equal(result.native_pump_interval.no_peer_RPC_during_interval, true);
  assert.equal(result.native_pump_interval.complete, true);
  await assert.rejects(runActualInterval({runtimeCssMatch: true, beforeCursor: 512}), /passed source cursor 512/);
  await assert.rejects(runActualInterval({runtimeCssMatch: true, injectedRpc: true}), /peer RPC occurred/);
});

test('runtime full-route completion verifier requires the final checksum ACK at 5083', () => {
  const verify = vm.runInNewContext(`${fullRouteCompletionSource}\nverifyRuntimeFullRoutePeerCompletion`, {
    verifyPositivePeerCompletion, usedInputs: 5082, sourceTicks: 5084,
  });
  const valid = {terminal: null, checksum_mismatches: [], remote_ack_input: 5081,
    local_checksum_ticks: 5084, remote_checksum_ticks: 5084, next_checksum_compare: 5084,
    remote_ack_checksum: 5083};
  assert.deepEqual(JSON.parse(JSON.stringify(verify(valid))), {remote_ack_input: 5081,
    local_checksum_ticks: 5084, remote_checksum_ticks: 5084, next_checksum_compare: 5084,
    remote_ack_checksum: 5083});
  for (const ack of [5082, 5084, null])
    assert.throws(() => verify({...valid, remote_ack_checksum: ack}), /did not acknowledge checksum 5083/);
});

test('runtime full-route pure-health interval retains the first stable active-match witness without peer RPC', async () => {
  const result = await runActualInterval({runtimeFullRoute: true});
  assert.equal(result.native_pump_interval.before.alpha.status.cursor, 306);
  assert.equal(result.native_pump_interval.after.alpha.status.cursor, 5084);
  assert.equal(result.native_pump_interval.actual_native_cursor_progress_each, true);
  assert.equal(result.native_pump_interval.no_peer_RPC_during_interval, true);
  assert.equal(result.native_pump_interval.complete, true);
  for (const role of ['alpha', 'beta']) {
    const witness = result.instanceRows[role].first_active_match_witness;
    assert.equal(witness.source_phase, 7);
    assert.equal(witness.source_cursor, 306);
    assert.deepEqual(witness.selection, {stage: 0x20, player_stocks: [4, 4],
      fighters: [0, 0], humans: [true, true], current_stocks: [4, 4]});
  }
  await assert.rejects(runActualInterval({runtimeFullRoute: true, beforeCursor: 5084}), /completed before/);
  await assert.rejects(runActualInterval({runtimeFullRoute: true, injectedRpc: true}), /peer RPC occurred/);
  const zeroThenPositive = await runActualInterval({runtimeFullRoute: true,
    matchFrameSequence: {alpha: [0, 1], beta: [0, 1]}});
  for (const role of ['alpha', 'beta'])
    assert.equal(zeroThenPositive.instanceRows[role].first_active_match_witness.observation.frame, 1,
      'a ready frame-zero construction observation is skipped until positive simulation progress');
  await assert.rejects(runActualInterval({runtimeFullRoute: true, matchBoundary: {observation: {
    ...{ready: true, paused: false, ending: false, complete: false, frame: 1,
      rules: {stage: 0x20, player_stocks: [4, 4]},
      players: [{fighter: 0, human: true, stocks: 4}, {fighter: 0, human: true, stocks: 4}]},
    observer_error: true}}}), /Active-match observation/);
  await assert.rejects(runActualInterval({runtimeFullRoute: true, matchBoundary: {observation: {
    ready: true, paused: false, ending: false, complete: false, frame: 1,
    rules: {stage: 0x20, player_stocks: [4, 4]},
    players: [{fighter: 1, human: true, stocks: 4}, {fighter: 0, human: true, stocks: 4}]}}}), /Active-match observation/);
  for (const matchBoundary of [{stable_cursor: false}, {native_before: {running: 0}},
    {native_after: {error: 'native failed'}}, {status_before: {active: 0}},
    {status_after: {terminal: {kind: 2}}}]) {
    const invalid = await runActualInterval({runtimeFullRoute: true, matchBoundary});
    assert.equal(invalid.instanceRows.alpha.first_active_match_witness, undefined);
    assert.equal(invalid.instanceRows.beta.first_active_match_witness, undefined);
  }
});

test('runtime full-route final terminal helper retains only source MatchEnd outcome and ordered participant winners', () => {
  const source = browserSource.slice(browserSource.indexOf('function validateRuntimeFullRouteTerminalResult('),
    browserSource.indexOf('\nfunction routeBoundaryPath('));
  const helpers = vm.runInNewContext(`${source}\n({validateRuntimeFullRouteTerminalResult, compareRuntimeFullRouteTerminalResults})`);
  const base = {complete: true, players: [{human: true}, {human: true}]};
  assert.deepEqual(JSON.parse(JSON.stringify(helpers.validateRuntimeFullRouteTerminalResult({
    ...base, terminal: {outcome: 1, winners: [0, 1]}}))), {outcome: 1, winners: [0, 1]});
  assert.deepEqual(JSON.parse(JSON.stringify(helpers.compareRuntimeFullRouteTerminalResults(
    {...base, terminal: {outcome: 2, winners: [1]}}, {...base, terminal: {outcome: 2, winners: [1]}}))),
  {outcome: 2, winners: [1]});
  for (const observation of [
    {...base}, {...base, terminal: {outcome: 0, winners: [0]}},
    {...base, terminal: {outcome: 3, winners: [0]}}, {...base, terminal: {outcome: 7, winners: [0]}},
    {...base, terminal: {outcome: 1, winners: []}}, {...base, terminal: {outcome: 1, winners: [0, 0]}},
    {...base, terminal: {outcome: 1, winners: [2]}}, {...base, terminal: {outcome: 2, winners: [0, 1]}},
    {...base, players: [{human: true}, {human: false}], terminal: {outcome: 1, winners: [0]}},
  ]) assert.throws(() => helpers.validateRuntimeFullRouteTerminalResult(observation), /natural two-human MatchEnd/);
  assert.throws(() => helpers.compareRuntimeFullRouteTerminalResults(
    {...base, terminal: {outcome: 2, winners: [0]}},
    {...base, terminal: {outcome: 2, winners: [1]}}), /disagree on original MatchEnd/);
  assert.throws(() => helpers.compareRuntimeFullRouteTerminalResults(
    {...base, terminal: {outcome: 1, winners: [0, 1]}},
    {...base, terminal: {outcome: 1, winners: [1, 0]}}), /disagree on original MatchEnd/);
});

test('runtime full-route repeated CSS return reports the consecutive source run start', () => {
  const start = browserSource.indexOf('function verifyRuntimeFullRouteSceneRuns(');
  const end = browserSource.indexOf('\nfunction verifyRuntimeCssMatchPeerCompletion(', start);
  const verify = vm.runInNewContext(`${browserSource.slice(start, end)}\nverifyRuntimeFullRouteSceneRuns`, {
    RUNTIME_FULL_ROUTE_SOURCE_TICKS: 5084, expectedFullSceneOrder: () => [1, 2, 3, 4, 1],
  });
  const scenes = Array.from({length: 5084}, (_, tick) => ({tick,
    scene: tick < 10 ? 1 : tick < 20 ? 2 : tick < 30 ? 3 : tick < 40 ? 4 : 1}));
  const result = verify(scenes);
  assert.deepEqual(JSON.parse(JSON.stringify(result.starts)), [
    {scene: 1, first_tick: 0}, {scene: 2, first_tick: 10}, {scene: 3, first_tick: 20},
    {scene: 4, first_tick: 30}, {scene: 1, first_tick: 40},
  ]);
  assert.throws(() => verify(scenes.slice(0, -1)), /5084 sequential/);
});

test('runtime CSS-to-SSS scene verifier requires 520 ordered checksums with only CSS then SSS', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function verifyRuntimeCssSssSceneRuns(');
  const end = source.indexOf('function routeBoundaryPath(', start);
  const verify = vm.runInNewContext(`${source.slice(start, end)}\nverifyRuntimeCssSssSceneRuns`,
    {RUNTIME_CSS_SSS_SOURCE_TICKS: 520, collapseConsecutiveScenes});
  const sceneRuns = Array.from({length: 520}, (_, tick) => ({tick, scene: tick < 153 ? 1 : 2}));
  assert.deepEqual(JSON.parse(JSON.stringify(verify(sceneRuns))), {scenes: [1, 2], firstSssTick: 153});
  for (const mutate of [rows => rows.pop(), rows => { rows[512].tick = 511; },
    rows => { rows[200].scene = 3; }, rows => { rows[519].scene = 1; }]) {
    const bad = structuredClone(sceneRuns); mutate(bad);
    assert.throws(() => verify(bad));
  }
});

test('runtime CSS-to-match scene verifier requires the exact CSS/SSS/match source order', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function verifyRuntimeCssMatchSceneRuns(');
  const end = source.indexOf('function routeBoundaryPath(', start);
  const verify = vm.runInNewContext(`${source.slice(start, end)}\nverifyRuntimeCssMatchSceneRuns`,
    {RUNTIME_CSS_MATCH_SOURCE_TICKS: 520, collapseConsecutiveScenes});
  const sceneRuns = Array.from({length: 520}, (_, tick) => ({tick, scene: tick < 153 ? 1 : tick < 304 ? 2 : 3}));
  assert.deepEqual(JSON.parse(JSON.stringify(verify(sceneRuns))), {scenes: [1, 2, 3], firstSssTick: 153, firstMatchTick: 304});
  for (const mutate of [rows => rows.pop(), rows => { rows[512].tick = 511; },
    rows => { rows[200].scene = 3; }, rows => { rows[519].scene = 1; }, rows => { rows[510].scene = 4; }]) {
    const bad = structuredClone(sceneRuns); mutate(bad);
    assert.throws(() => verify(bad));
  }
});

test('runtime CSS-to-match completion requires the final checksum acknowledgment', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function verifyRuntimeCssMatchPeerCompletion(');
  const end = source.indexOf('\nfunction routeBoundaryPath(', start);
  const verify = vm.runInNewContext(`${source.slice(start, end)}\nverifyRuntimeCssMatchPeerCompletion`, {
    verifyPositivePeerCompletion, usedInputs: 518, sourceTicks: 520,
  });
  const summary = {terminal: null, checksum_mismatches: [], remote_ack_input: 517,
    local_checksum_ticks: 520, remote_checksum_ticks: 520, next_checksum_compare: 520,
    remote_ack_checksum: 519};
  assert.deepEqual(JSON.parse(JSON.stringify(verify(summary))), {remote_ack_input: 517,
    local_checksum_ticks: 520, remote_checksum_ticks: 520, next_checksum_compare: 520,
    remote_ack_checksum: 519});
  for (const ack of [518, 520, null])
    assert.throws(() => verify({...summary, remote_ack_checksum: ack}), /did not acknowledge checksum 519/);
});

test('runtime CSS-to-SSS final SSS screenshot waits for frozen input and the read-only 512 interval', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function routeBoundaryPath(');
  const end = source.indexOf('\nasync function watchRouteBoundary(', start);
  const helperSource = source.slice(start, end);
  assert(start >= 0 && end > start);
  const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0]);
  async function run({frozen = true, complete = true, noPeerRpc = true, crossed512 = true,
    cursorAfterScreenshot = 520, phaseAfterScreenshot = 3} = {}) {
    const events = [], instanceRows = {alpha: {route_boundary_captures: [], route_boundary_misses: [],
      runtime_input_fixture_frozen: {frozen, captured_count: 518}}};
    let statusCalls = 0, screenshotCount = 0;
    const instances = {alpha: {
      driver: {async diagnostics() { events.push('driver'); return {phase: 3, running: 1, status: 'Original stage select', log: ''}; }},
      async native() { events.push('native'); return {phase: events.includes('screenshot') ? phaseAfterScreenshot : 3}; },
      async status() { events.push('status'); ++statusCalls; return {cursor: statusCalls === 1 ? 520 : cursorAfterScreenshot,
        blocker: 'complete'}; },
      async screenshot(file) { events.push('screenshot'); ++screenshotCount; assert.match(file, /route-sss\.png$/); },
      async graphics() { events.push('graphics'); return {cross_origin_isolated: true, webgpu_api: true, webgpu_adapter: true}; },
    }};
    const pairResults = {native_pump_interval: {complete, no_peer_RPC_during_interval: noPeerRpc,
      actual_native_cursor_progress_each: true, crossed_512_each: crossed512}};
    const context = vm.createContext({
      Buffer, Date, Promise, path: await import('node:path'), output: '/capture',
      childDirectory: role => `/capture/${role}`, instances, instanceRows, pairResults,
      runtimeCssSss: true, sourceTicks: 520, usedInputs: 518,
      POSITIVE_ROUTE_BOUNDARIES: [{name: 'css-start', phase: 1, label: 'CSS'},
        {name: 'sss', phase: 3, label: 'original SSS'}],
      fs: {async readFile(file) { events.push('read'); assert.match(file, /route-sss\.png$/); return png; }},
      readyRenderEvent: (_diagnostics, phase) => phase === 3 ? {phase, draw_calls: 1, source_draws: 1} : null,
      sha256: () => 'fixture-png-sha256',
    });
    const capture = vm.runInContext(`${helperSource}\n;captureRuntimeCssSssFinalBoundary`, context);
    return {events, instanceRows, pairResults, get screenshotCount() { return screenshotCount; },
      capture: () => capture('alpha')};
  }

  const valid = await run();
  await valid.capture();
  const row = valid.instanceRows.alpha.route_boundary_captures[0];
  assert.equal(valid.screenshotCount, 1);
  assert.deepEqual(valid.events, ['driver', 'native', 'status', 'screenshot', 'read', 'native', 'status', 'graphics']);
  assert.equal(row.name, 'sss');
  assert.equal(row.source_cursor_sampled_before_screenshot, 520);
  assert.equal(row.source_cursor_after_screenshot, 520);
  assert.equal(row.source_cursor_stable_during_screenshot, true);
  assert.equal(row.screenshot_phase_stable, true);
  assert.equal(row.gpu_observed, true);
  assert.equal(row.browser_driver.phase, 3);

  const notFrozen = await run({frozen: false});
  await assert.rejects(notFrozen.capture(), /frozen input fixture/);
  assert.equal(notFrozen.screenshotCount, 0);
  const intervalIncomplete = await run({complete: false});
  await assert.rejects(intervalIncomplete.capture(), /read-only 512 interval/);
  assert.equal(intervalIncomplete.screenshotCount, 0);
  const intervalHadRpc = await run({noPeerRpc: false});
  await assert.rejects(intervalHadRpc.capture(), /read-only 512 interval/);
  assert.equal(intervalHadRpc.screenshotCount, 0);
  const intervalMissed512 = await run({crossed512: false});
  await assert.rejects(intervalMissed512.capture(), /read-only 512 interval/);
  assert.equal(intervalMissed512.screenshotCount, 0);
  const movedDuringScreenshot = await run({cursorAfterScreenshot: 521});
  await assert.rejects(movedDuringScreenshot.capture(), /changed the source boundary/);
  assert.equal(movedDuringScreenshot.screenshotCount, 1);
});

test('runtime CSS-to-SSS final SSS capture runs after fixture freeze and before relay close', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const freeze = source.indexOf('instanceRows[role].runtime_input_fixture_frozen = await instances[role].freezeRuntimeInputFixture();');
  const capture = source.indexOf('await captureRuntimeCssSssFinalBoundary(role);', freeze);
  const close = source.indexOf('await relay.close();', capture);
  assert(freeze >= 0 && capture > freeze && close > capture);
});

test('runtime full-route final CSS PNG uses held structured readiness at cursor 5084 and stable native/status/GPU', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function routeBoundaryPath(');
  const end = source.indexOf('\nasync function watchRouteBoundary(', start);
  const helperSource = source.slice(start, end);
  assert(start >= 0 && end > start);
  const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0]);
  async function run({frozen = true, complete = true, noPeerRpc = true, progress = true,
    renderReadiness = {}, nativeBefore = {}, nativeAfter = {}, statusBefore = {}, statusAfter = {},
    gpuObserved = true} = {}) {
    const events = [], instanceRows = {alpha: {route_boundary_captures: [], route_boundary_misses: [],
      runtime_input_fixture_frozen: {frozen, captured_count: 5082}}};
    let screenshotCount = 0;
    const accountingRows = Array.from({length: 5084}, (_, index) => ({frame: index + 1,
      valid: 1, source_steps: 1, source_draws: 1}));
    const draw = {row_index: 5083, frame: 5084, valid: 1, source_steps: 1, source_draws: 1,
      began: 1, drawn: 1, preparation_ms: 0, draw_suppressed: 0, draw_calls: 12,
      source_cursor: 5084, phase: 1, running: 1, error: null,
      status: 'Original character select', source: 'source frame 5084 · ready: 1'};
    const accounting = {frozen: false, initial: {active: 1, cursor: 0, blocker: 'start_identity'},
      final: {active: 1, cursor: 5084, blocker: 'complete', terminal: {kind: 0}},
      overflow: 0, errors: [], rows: accountingRows, render_readiness_enabled: true,
      render_readiness: {...draw, ...renderReadiness}};
    if (renderReadiness === null) accounting.render_readiness = null;
    const staleLogDraw = JSON.stringify({...draw, frame: 320, source_cursor: 320, phase: 7,
      status: 'Original match', source: 'earlier match render'});
    const instances = {alpha: {
      async readSourceAccounting(options) { assert.equal(options.freeze, false); events.push('accounting'); return accounting; },
      driver: {async diagnostics() { events.push('driver'); return {phase: 7, running: 1,
        log: `Native callback ${staleLogDraw}`}; }},
      async native() { events.push('native'); return {phase: 1, running: 1, error: null,
        ...(events.includes('screenshot') ? nativeAfter : nativeBefore)}; },
      async status() { events.push('status'); return {active: 1, cursor: 5084, blocker: 'complete',
        terminal: {kind: 0}, ...(events.includes('screenshot') ? statusAfter : statusBefore)}; },
      async screenshot(file) { events.push('screenshot'); ++screenshotCount; assert.match(file, /final\.png$/); },
      async graphics() { events.push('graphics'); return {cross_origin_isolated: true, webgpu_api: true,
        webgpu_adapter: gpuObserved}; },
    }};
    const pairResults = {native_pump_interval: {complete, no_peer_RPC_during_interval: noPeerRpc,
      actual_native_cursor_progress_each: progress}};
    const context = vm.createContext({Buffer, Date, Promise, path: await import('node:path'), output: '/capture',
      childDirectory: role => `/capture/${role}`, instances, instanceRows, pairResults,
      runtimeFullRoute: true, sourceTicks: 5084, usedInputs: 5082,
      POSITIVE_ROUTE_BOUNDARIES: [{}, {}, {}, {}, {name: 'css-return', phase: 1,
        label: 'original CSS after Results'}],
      fs: {async readFile(file) { events.push('read'); assert.match(file, /final\.png$/); return png; }},
      readyRenderCallback, readyRenderEvent, verifyAccountedRenderReadiness,
      sha256: () => 'fixture-png-sha256'});
    const capture = vm.runInContext(`${helperSource}\n;captureRuntimeFullRouteFinalBoundary`, context);
    return {events, instanceRows, get screenshotCount() { return screenshotCount; },
      capture: () => capture('alpha')};
  }

  const valid = await run();
  const evidence = await valid.capture();
  assert.equal(valid.screenshotCount, 1);
  assert.deepEqual(valid.events, ['accounting', 'driver', 'native', 'status', 'screenshot', 'read', 'native', 'status', 'graphics']);
  assert.equal(evidence.name, 'css-return');
  assert.equal(evidence.screenshot, 'alpha/final.png');
  assert.equal(evidence.source_cursor_sampled_before_screenshot, 5084);
  assert.equal(evidence.source_cursor_after_screenshot, 5084);
  assert.equal(evidence.status_after_screenshot.blocker, 'complete');
  assert.equal(evidence.status_after_screenshot.terminal.kind, 0);
  assert.equal(evidence.native_after_screenshot.running, 1);
  assert.equal(evidence.gpu_observed, true);
  assert.equal(evidence.render_readiness.kind, 'structured native accounting callback');
  assert.equal(evidence.render_readiness.source_cursor, 5084);
  assert.equal(valid.instanceRows.alpha.route_boundary_observations[0].source_accounting_snapshot.render_readiness.frame, 5084);
  assert.equal(valid.instanceRows.alpha.route_boundary_observations[0].browser_driver.phase, 7,
    'the stale driver log is retained as diagnostics but does not supply readiness');

  for (const options of [{frozen: false}, {complete: false}, {noPeerRpc: false}, {progress: false}]) {
    const invalid = await run(options);
    await assert.rejects(invalid.capture());
    assert.equal(invalid.screenshotCount, 0);
  }
  for (const renderReadiness of [{source_cursor: 5083}, {phase: 8}, {row_index: 5082}, null]) {
    const invalid = await run({renderReadiness});
    await assert.rejects(invalid.capture(), /final CSS boundary screenshot was not captured/);
    assert.equal(invalid.screenshotCount, 0);
    assert.equal(invalid.instanceRows.alpha.route_boundary_observations[0].render_readiness, null);
    assert.match(invalid.instanceRows.alpha.route_boundary_observations[0].render_readiness_error,
      /Structured render readiness/);
  }
  for (const options of [{nativeBefore: {phase: 8}}, {nativeBefore: {running: 0}},
    {nativeBefore: {error: 'source failure'}}, {statusBefore: {cursor: 5083}},
    {statusBefore: {blocker: 'terminal'}}, {statusBefore: {terminal: {kind: 2}}}]) {
    const invalid = await run(options);
    await assert.rejects(invalid.capture(), /final CSS boundary screenshot was not captured/);
    assert.equal(invalid.screenshotCount, 0);
  }
  const moved = await run({statusAfter: {cursor: 5085}});
  await assert.rejects(moved.capture(), /changed the source boundary/);
  assert.equal(moved.screenshotCount, 1);
  const terminalAfter = await run({statusAfter: {terminal: {kind: 2}}});
  await assert.rejects(terminalAfter.capture(), /changed the source boundary/);
  assert.equal(terminalAfter.screenshotCount, 1);
  const gpuMissing = await run({gpuObserved: false});
  await assert.rejects(gpuMissing.capture(), /lacks held structured readiness.*GPU diagnostics/);
  assert.equal(gpuMissing.screenshotCount, 1);
});

test('runtime full-route captures final CSS after complete native observation and before relay close', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const freeze = source.indexOf('instanceRows[role].runtime_input_fixture_frozen = await instances[role].freezeRuntimeInputFixture();');
  const finalBranch = source.indexOf('if (runtimeFullRoute) {', freeze);
  const finalObservation = source.indexOf('matchObservationBoundary()', finalBranch);
  const capture = source.indexOf('await captureRuntimeFullRouteFinalBoundary(role);', finalObservation);
  const close = source.indexOf('await relay.close();', capture);
  assert(freeze >= 0 && finalBranch > freeze && finalObservation > finalBranch && capture > finalObservation && close > capture);
});

test('runtime CSS-to-match final screenshot retains the phase-7 active-match boundary after read-only progress', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('function routeBoundaryPath(');
  const end = source.indexOf('\nasync function watchRouteBoundary(', start);
  const helperSource = source.slice(start, end);
  assert(start >= 0 && end > start);
  const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0]);
  async function run({frozen = true, complete = true, noPeerRpc = true, crossed512 = true,
    cursorAfterScreenshot = 520, phaseAfterScreenshot = 7, gpuObserved = true, invalidReadiness = null, pngValid = true,
    nativeBefore = {}, statusBefore = {}} = {}) {
    const events = [], instanceRows = {alpha: {route_boundary_captures: [], route_boundary_misses: [],
      runtime_input_fixture_frozen: {frozen, captured_count: 518}}};
    const accounting = {frozen: false, initial: {active: 1, cursor: 0, blocker: 'start_identity'},
      final: {active: 1, cursor: 520, blocker: 'complete', terminal: {kind: 0}}, overflow: 0, errors: [],
      rows: Array.from({length: 520}, (_, index) => ({frame: index + 1, valid: 1, source_steps: 1, source_draws: 1})),
      render_readiness_enabled: true, render_readiness: {row_index: 519, frame: 520, valid: 1,
        source_steps: 1, source_draws: 1, began: 1, drawn: 1, preparation_ms: 0, draw_suppressed: 0,
        draw_calls: 252, source_cursor: 520, phase: 7, running: 1, error: null,
        status: 'Original match', source: 'source frame: 90 · ready: 1'}};
    if (invalidReadiness) Object.assign(accounting.render_readiness, invalidReadiness);
    let statusCalls = 0, screenshotCount = 0;
    const instances = {alpha: {
      async readSourceAccounting(options) { assert.equal(options.freeze, false); events.push('accounting'); return accounting; },
      driver: {async diagnostics() { events.push('driver'); return {phase: 7, running: 1, status: 'Original match', log: ''}; }},
      async native() { events.push('native'); return {phase: events.includes('screenshot') ? phaseAfterScreenshot : 7, running: 1, error: null, ...(!events.includes('screenshot') ? nativeBefore : {})}; },
      async status() { events.push('status'); ++statusCalls; return {active: 1, cursor: statusCalls === 1 ? 520 : cursorAfterScreenshot,
        blocker: 'complete', terminal: {kind: 0}, ...(!events.includes('screenshot') ? statusBefore : {})}; },
      async screenshot(file) { events.push('screenshot'); ++screenshotCount; assert.match(file, /route-match\.png$/); },
      async graphics() { events.push('graphics'); return {cross_origin_isolated: true, webgpu_api: true,
        webgpu_adapter: gpuObserved}; },
    }};
    const pairResults = {native_pump_interval: {complete, no_peer_RPC_during_interval: noPeerRpc,
      actual_native_cursor_progress_each: true, crossed_512_each: crossed512}};
    const context = vm.createContext({
      Buffer, Date, Promise, path: await import('node:path'), output: '/capture',
      childDirectory: role => `/capture/${role}`, instances, instanceRows, pairResults,
      runtimeCssMatch: true, sourceTicks: 520, usedInputs: 518,
      POSITIVE_ROUTE_BOUNDARIES: [{name: 'css-start', phase: 1, label: 'CSS'},
        {name: 'sss', phase: 3, label: 'SSS'}, {name: 'match', phase: 7, label: 'original match'}],
      fs: {async readFile(file) { events.push('read'); assert.match(file, /route-match\.png$/); return pngValid ? png : Buffer.from([0]); }},
      readyRenderEvent, verifyAccountedRenderReadiness,
      sha256: () => 'fixture-png-sha256',
    });
    const capture = vm.runInContext(`${helperSource}\n;captureRuntimeCssMatchFinalBoundary`, context);
    return {events, instanceRows, pairResults, get screenshotCount() { return screenshotCount; },
      capture: () => capture('alpha')};
  }

  const valid = await run();
  await valid.capture();
  const row = valid.instanceRows.alpha.route_boundary_captures[0];
  assert.equal(valid.screenshotCount, 1);
  assert.deepEqual(valid.events, ['accounting', 'driver', 'native', 'status', 'screenshot', 'read', 'native', 'status', 'graphics']);
  assert.equal(row.name, 'match');
  assert.equal(row.source_cursor_sampled_before_screenshot, 520);
  assert.equal(row.source_cursor_after_screenshot, 520);
  assert.equal(row.source_cursor_stable_during_screenshot, true);
  assert.equal(row.screenshot_phase_stable, true);
  assert.equal(row.gpu_observed, true);
  assert.equal(row.browser_driver.phase, 7);
  assert.equal(row.render_readiness.kind, 'structured native accounting callback');
  assert.equal(valid.instanceRows.alpha.route_boundary_observations[0].source_accounting_snapshot.frozen, false);
  assert.equal(readyRenderEvent(row.browser_driver, 7), null);
  for (const invalidReadiness of [{source_cursor: 519}, {phase: 3}, {preparation_ms: 1}, {draw_suppressed: 1}, {draw_calls: 0}]) {
    const invalid = await run({invalidReadiness});
    await assert.rejects(invalid.capture(), /screenshot was not captured/);
    assert.equal(invalid.screenshotCount, 0);
    assert.match(invalid.instanceRows.alpha.route_boundary_observations[0].render_readiness_error, /Structured render readiness/);
  }

  for (const options of [{frozen: false}, {complete: false}, {noPeerRpc: false}, {crossed512: false}, {gpuObserved: false}]) {
    const invalid = await run(options);
    if (options.gpuObserved === false) await assert.rejects(invalid.capture(), /lacks driver\/GPU diagnostics/);
    else await assert.rejects(invalid.capture());
    assert.equal(invalid.screenshotCount, options.gpuObserved === false ? 1 : 0);
  }
  for (const options of [{nativeBefore: {phase: 3}}, {nativeBefore: {error: 'native failed'}},
    {nativeBefore: {running: 0}}, {statusBefore: {blocker: 'terminal'}}]) {
    const invalid = await run(options);
    await assert.rejects(invalid.capture(), /screenshot was not captured/);
    assert.equal(invalid.screenshotCount, 0);
    const observation = invalid.instanceRows.alpha.route_boundary_observations[0];
    assert(observation.render_readiness_error);
    assert.deepEqual(options.nativeBefore ?? options.statusBefore,
      Object.fromEntries(Object.keys(options.nativeBefore ?? options.statusBefore)
        .map(key => [key, (options.nativeBefore ? observation.native : observation.status)[key]])));
    assert.match(invalid.instanceRows.alpha.route_boundary_misses[0].reason, /verified structured draw/);
  }
  const invalidPng = await run({pngValid: false});
  await assert.rejects(invalidPng.capture(), /not a PNG/);
  assert.equal(invalidPng.screenshotCount, 1);
  const changedPhase = await run({phaseAfterScreenshot: 8});
  await assert.rejects(changedPhase.capture(), /changed the source boundary/);
  const moved = await run({cursorAfterScreenshot: 521});
  await assert.rejects(moved.capture(), /changed the source boundary/);
});

test('runtime CSS-to-match freezes both fixtures and awaits match observations before asynchronous owner close', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('    if (runtimeOwned) {', source.indexOf('  stopRouteCaptureWatchers = true;'));
  const end = source.indexOf('    } else {', start);
  assert(start >= 0 && end > start);
  const events = [], rows = {alpha: {}, beta: {}}, instances = {};
  for (const role of ['alpha', 'beta']) instances[role] = {
    async freezeRuntimeInputFixture() {
      events.push(`freeze:${role}`);
      return rows[role].runtime_input_fixture_frozen = {frozen: true, captured_count: 518};
    },
    async status() { events.push(`status:${role}`); return {cursor: 520, blocker: 'complete', terminal: {kind: 0}}; },
    async native() { events.push(`native:${role}`); return {phase: 7, running: 1, error: null}; },
    async matchObservation() {
      assert.equal(rows.alpha.runtime_input_fixture_frozen?.frozen, true);
      assert.equal(rows.beta.runtime_input_fixture_frozen?.frozen, true);
      events.push(`observe:${role}`);
      return {ready: true};
    },
  };
  const context = {runtimeOwned: true, runtimeInputFixture: true, runtimeCssSss: false, runtimeFullRoute: false,
    runtimeCssMatch: true, sourceTicks: 520, usedInputs: 518,
    POSITIVE_ROUTE_BOUNDARIES: [{}, {}, {phase: 7}],
    instanceRows: rows, pairResults: {}, instances,
    runtimeOwnerWebRtcState: async role => ({role, state: 'connected'}),
    verifyReliableHostWebRtc: value => value,
    async captureRuntimeCssMatchFinalBoundary(role) {
      assert.equal(rows[role].runtime_input_fixture_frozen?.frozen, true);
      events.push(`capture:${role}`);
    },
    relay: {async close() {
      events.push('close:start');
      assert(events.includes('capture:alpha') && events.includes('capture:beta'));
      await Promise.resolve();
      events.push('close:resumed');
    }},
  };
  await vm.runInNewContext(`(async()=>{${source.slice(start, end)}\n}})()`, context);
  const closeStart = events.indexOf('close:start');
  assert(closeStart > events.indexOf('capture:beta'));
  assert(events.indexOf('freeze:beta') < events.indexOf('observe:alpha'));
  assert(events.indexOf('observe:alpha') < events.indexOf('capture:alpha'));
  assert(events.indexOf('capture:alpha') < events.indexOf('observe:beta'));
  assert(events.indexOf('observe:beta') < events.indexOf('capture:beta'));
  assert.equal(context.pairResults.relay_closed, true);
  assert.equal(events.at(-1), 'close:resumed');
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
