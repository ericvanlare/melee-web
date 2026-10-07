import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import test from 'node:test';
import fs from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import {EventEmitter} from 'node:events';
import {readyRenderEvent, renderEventSignatures, verifyFirstChecksumMismatch,
  verifyTerminalHold, verifyPositivePeerCompletion, verifyReliableHostWebRtc, WasmResponseIdentityObserver, attachWasmResponseIdentityObserver} from '../scripts/net_lockstep_observers.mjs';
import {createTransportCallbackQueue} from '../scripts/net_lockstep_transport.mjs';

function callback(data, kind = 'Native callback') {
  return `${kind} ${JSON.stringify(data)}`;
}

const readyDraw = extra => ({began: 1, drawn: 1, frame: 17, preparation_ms: 0,
  draw_calls: 90, source_draws: 1, draw_suppressed: 0, source: 'Original stage select', ...extra});

test('route readiness requires a new same-phase positive source draw with no preparation', () => {
  const oldDraw = callback(readyDraw({frame: 16}));
  const newDraw = callback(readyDraw({frame: 17}));
  const prior = renderEventSignatures(oldDraw);
  const diagnostics = {phase: 3, running: 1, status: 'Original stage select',
    log: `${oldDraw}\n${newDraw}`};
  assert.deepEqual(readyRenderEvent(diagnostics, 3, prior), {
    phase: 3, kind: 'Native callback', frame: 17, draw_calls: 90,
    source_draws: 1, draw_suppressed: 0, source: 'Original stage select', signature: newDraw,
  });
  assert.equal(readyRenderEvent({...diagnostics, log: oldDraw}, 3, prior), null,
    'a positive draw already present when the boundary watcher starts is stale evidence');
  assert.equal(readyRenderEvent({...diagnostics, running: 0}, 3, new Set()), null);
  assert.equal(readyRenderEvent({...diagnostics, phase: 1}, 3, new Set()), null);
  assert.equal(readyRenderEvent({...diagnostics, status: 'Preparing original next scene...'}, 3, new Set()), null);
  for (const data of [
    readyDraw({draw_suppressed: 1}), readyDraw({draw_calls: 0}),
    readyDraw({source_draws: 0}), readyDraw({source: 'match preparing · ready: 0'}),
    readyDraw({preparation_ms: 0.005}), readyDraw({preparation_ms: undefined}),
    readyDraw({began: 0}),
  ]) assert.equal(readyRenderEvent({...diagnostics, log: callback(data)}, 3, new Set()), null);
});

function nativeStatus({cursor = 14, pushed = 20, kind = 1, tick = 12, channel = 1,
  active = 1, blocker = 'terminal'} = {}) {
  return {active, cursor, pushed, blocker, terminal: {kind, tick, channel}};
}

test('native terminal hold requires both peers terminal and source cursor/pushed to stay fixed', () => {
  const before = {alpha: nativeStatus(), beta: nativeStatus({cursor: 15, pushed: 20})};
  const after = {alpha: nativeStatus(), beta: nativeStatus({cursor: 15, pushed: 20})};
  assert.deepEqual(verifyTerminalHold(before, after, 1, {expectedTick: 12, expectedChannel: 1}), {
    minimum_hold_ms: 120, no_fallback: true,
    peers: {
      alpha: {kind: 1, tick: 12, channel: 1, active: 1, blocker: 'terminal', cursor_before: 14,
        cursor_after: 14, pushed_before: 20, pushed_after: 20},
      beta: {kind: 1, tick: 12, channel: 1, active: 1, blocker: 'terminal', cursor_before: 15,
        cursor_after: 15, pushed_before: 20, pushed_after: 20},
    },
  });
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({cursor: 16, pushed: 20})}, 1), /source advanced/);
  assert.throws(() => verifyTerminalHold(before, {...after, alpha: nativeStatus({active: 0})}, 1), /hold is incomplete/);
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({blocker: 'remote_input'})}, 1), /hold is incomplete/);
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({kind: 2})}, 1), /hold is incomplete/);
});

function checksum(tick, {input = 1, scene = 2} = {}) {
  const bytes = Buffer.alloc(64);
  bytes.writeUInt32LE(tick, 0);
  bytes.writeUInt32LE(scene, 4);
  bytes.writeBigUInt64LE(BigInt(input), 24);
  return bytes;
}

test('raw native checksums retain an equal prefix and first changed PAD channel', () => {
  const alpha = Buffer.concat([checksum(0), checksum(1), checksum(2, {input: 5})]);
  const beta = Buffer.concat([checksum(0), checksum(1), checksum(2, {input: 6})]);
  assert.deepEqual(verifyFirstChecksumMismatch(alpha, beta, 2, 1), {
    equal_prefix_through_tick: 1, first_mismatch_tick: 2, first_mismatch_channel: 1,
  });
  assert.throws(() => verifyFirstChecksumMismatch(
    Buffer.concat([checksum(0), checksum(1, {input: 3}), checksum(2, {input: 5})]), beta, 2, 1),
  /diverged before expected tick/);
  assert.throws(() => verifyFirstChecksumMismatch(
    Buffer.concat([checksum(0), checksum(1), checksum(2, {scene: 3})]), beta, 2, 1),
  /expected channel 1/);
  assert.throws(() => verifyFirstChecksumMismatch(alpha.subarray(0, 128), beta.subarray(0, 128), 2, 1),
    /missing expected mismatch tick/);
});

function fakeResponse(body, {url = 'http://127.0.0.1:18943/gameplay_menu_browser.wasm', status = 200} = {}) {
  return {url: () => url, status: () => status, request: () => ({resourceType: () => 'fetch'}),
    body: () => typeof body === 'function' ? body() : Promise.resolve(body)};
}

test('loaded Wasm identity rejects missing, failed, unreadable, and conflicting response sets', async () => {
  await assert.rejects(new WasmResponseIdentityObserver().freeze(), /No runtime Wasm load response/);
  const failedStatus = new WasmResponseIdentityObserver();
  failedStatus.observe(fakeResponse(Buffer.from('bad'), {status: 503}));
  await assert.rejects(failedStatus.freeze(), /not usable/);
  const failedBody = new WasmResponseIdentityObserver();
  failedBody.observe(fakeResponse(() => Promise.reject(Error('body unavailable'))));
  await assert.rejects(failedBody.freeze(), /body unavailable/);
  const conflicts = new WasmResponseIdentityObserver();
  conflicts.observe(fakeResponse(Buffer.from('wasm A')));
  conflicts.observe(fakeResponse(Buffer.from('wasm B')));
  await assert.rejects(conflicts.freeze(), /conflicting body identities/);
});

test('loaded Wasm response set freezes before the later handshake fresh fetch', async () => {
  let resolveInitial, bodyReadStarted = false;
  const observer = new WasmResponseIdentityObserver();
  observer.observe(fakeResponse(() => {
    bodyReadStarted = true;
    return new Promise(resolve => { resolveInitial = resolve; });
  }));
  assert.equal(bodyReadStarted, true, 'response body capture starts synchronously in the response event');
  const frozen = observer.freeze();
  observer.observe(fakeResponse(Buffer.from('handshake fresh fetch')));
  resolveInitial(Buffer.from('runtime load body'));
  const identity = await frozen;
  assert.equal(identity.sha256, createHash('sha256').update('runtime load body').digest('hex'));
  assert.equal(identity.byte_length, Buffer.byteLength('runtime load body'));
  assert.equal(identity.response_count, 1);
  assert.equal(identity.response_set_frozen_before_handshake_fetch, true);
  assert.equal(identity.response_hashes.length, 1);
});


const wasmUrl = 'http://127.0.0.1:18943/gameplay_menu_browser.wasm';
class Cdp extends EventEmitter {
  calls = [];
  bodies = new Map();
  detached = false;
  async send(method, params) {
    this.calls.push({method, params});
    if (method === 'Network.getResponseBody') {
      const body = this.bodies.get(params.requestId);
      if (body instanceof Error) throw body;
      return {body: body.toString('base64'), base64Encoded: true};
    }
    return {};
  }
  async detach() { this.detached = true; }
}
function load(cdp, requestId, body, {url = wasmUrl, method = 'GET', status = 200,
  type = 'Fetch', headers = {}, finish = true, request = true} = {}) {
  cdp.bodies.set(requestId, body);
  if (request) cdp.emit('Network.requestWillBeSent', {requestId, request: {url, method}});
  cdp.emit('Network.responseReceived', {requestId, type, response: {url, status, headers}});
  if (finish) cdp.emit('Network.loadingFinished', {requestId});
}

test('dedicated observer reads its exact load request and freezes before a changed fresh request', async () => {
  const cdp = new Cdp();
  const observer = await attachWasmResponseIdentityObserver(cdp, {expectedUrl: wasmUrl});
  assert.deepEqual(cdp.calls, [{method: 'Network.enable', params: {
    maxResourceBufferSize: 64 * 1024 * 1024, maxTotalBufferSize: 128 * 1024 * 1024}}]);
  load(cdp, 'unrelated', Buffer.from('other host'), {url: wasmUrl.replace('127.0.0.1', 'localhost')});
  load(cdp, 'load-1', Buffer.from('actual load'));
  const frozen = observer.freeze();
  load(cdp, 'fresh-2', Buffer.from('fresh changed'));
  const receipt = await frozen;
  assert.equal(receipt.sha256, createHash('sha256').update('actual load').digest('hex'));
  assert.equal(receipt.response_count, 1);
  assert.equal(receipt.response_hashes[0].request_id, 'load-1');
  assert.equal(receipt.response_hashes[0].method, 'GET');
  assert.deepEqual(cdp.calls.map(row => row.method), ['Network.enable', 'Network.getResponseBody']);
  assert.equal(cdp.calls[1].params.requestId, 'load-1');
  await observer.detach();
  assert.equal(cdp.detached, true);
  assert.equal(cdp.eventNames().length, 0);
});

test('dedicated observer retains request, status, read, and size failures without a new fetch', async () => {
  for (const [options, body, match] of [
    [{request: false}, Buffer.from('load'), /no matching observed request/],
    [{method: 'POST'}, Buffer.from('load'), /not usable/],
    [{status: 503}, Buffer.from('load'), /not usable/],
    [{type: 'Document'}, Buffer.from('load'), /not usable/],
    [{}, Error('evicted sentinel'), /evicted sentinel/],
    [{}, Buffer.alloc(9), /byte bound/],
    [{}, Buffer.alloc(12), /encoded byte bound/],
    [{headers: {'Content-Length': '9'}}, Buffer.from('load'), /Content-Length exceeded/],
    [{headers: {'Content-Length': 'invalid'}}, Buffer.from('load'), /Content-Length exceeded/],
  ]) {
    const cdp = new Cdp();
    const observer = await attachWasmResponseIdentityObserver(cdp, {expectedUrl: wasmUrl, maxBodyBytes: 8});
    load(cdp, 'bad', body, options);
    await assert.rejects(observer.freeze(), match);
    await observer.detach();
    assert.ok(cdp.calls.every(row => ['Network.enable', 'Network.getResponseBody'].includes(row.method)));
  }
});

test('dedicated observer rejects load failure, incomplete teardown, conflicts and absence', async () => {
  for (const mode of ['failed', 'detach', 'conflict', 'missing']) {
    const cdp = new Cdp();
    const observer = await attachWasmResponseIdentityObserver(cdp, {expectedUrl: wasmUrl});
    if (mode === 'conflict') {
      load(cdp, 'one', Buffer.from('one')); load(cdp, 'two', Buffer.from('two'));
    } else if (mode !== 'missing') {
      load(cdp, 'one', Buffer.from('one'), {finish: false});
      if (mode === 'failed') cdp.emit('Network.loadingFailed', {requestId: 'one', errorText: 'load failed sentinel'});
      else await observer.detach();
    }
    await assert.rejects(observer.freeze(), /failed sentinel|detached before|conflicting|No runtime/);
    await observer.detach();
  }
});

// Exercise the actual runner's acquisition and finalizer with one rejected
// launch and one live owned sibling, without launching a desktop browser.
test('asymmetric lockstep startup failures close the successfully opened sibling', async () => {
  const source = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('  const opened = await Promise.allSettled(');
  const end = source.indexOf('  pairResults.browser_version', start);
  const cleanupStart = source.indexOf('  intentionalRelayClose = true;', end);
  const cleanupEnd = source.indexOf('  pairResults.cleanup_notes =', cleanupStart);
  assert(start >= 0 && end > start && cleanupStart > end && cleanupEnd > cleanupStart);
  for (const failedRole of ['alpha', 'beta']) {
    const healthyRole = failedRole === 'alpha' ? 'beta' : 'alpha';
    const failure = new Error(`injected ${failedRole} startup failure`);
    const healthy = {closed: false, errors: [], unloaded: false,
      async timingPauseDiagnostics() { return null; },
      async unload() { this.unloaded = true; },
      async close() { this.closed = true; return true; },
      async status() { return null; }, async native() { return null; }};
    const context = vm.createContext({browserOwned: false, inputSampling: false, peerModuleHashes: null,
      instances: {}, instanceRows: {alpha: {}, beta: {}},
      chromium: {}, launchOptions: {}, values: {url: 'http://127.0.0.1/', disc: '/unused'},
      path, childDirectory: role => role, openTimeout: 100, deadline: 100,
      checksumFiles: {}, pairResults: {transport: {}}, closeNotes: [],
      fs: {async writeFile() {}}, intentionalRelayClose: false, roomRuntime: null, runPassed: false,
      transportCallbackQueue: {async drain() { return []; }},
      recordAvailableTransportMetrics() {}, transportErrors: [], callbackErrors: [],
      relay: {traffic: {alpha_to_beta_bytes: 0, beta_to_alpha_bytes: 0}, async close() {}},
      openNetInstance: async ({label}) => {
        if (label === failedRole) throw failure;
        return healthy;
      }});
    await assert.rejects(vm.runInContext(`(async()=>{${source.slice(start, end)}})()`, context), failure);
    await vm.runInContext(`(async()=>{${source.slice(cleanupStart, cleanupEnd)}})()`, context);
    assert.equal(healthy.unloaded, true, `${healthyRole} source owner was not unloaded`);
    assert.equal(healthy.closed, true, `${healthyRole} browser escaped final cleanup`);
    assert.equal(context.instanceRows[healthyRole].browser_closed, true);
    assert.equal(context.pairResults.relay_closed, true);
    assert.equal(context.closeNotes.length, 0);
  }
});

test('relay cleanup outcome is recorded only after awaited close succeeds', async () => {
  const source = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const cleanupStart = source.indexOf('  intentionalRelayClose = true;');
  const cleanupEnd = source.indexOf('  pairResults.finished_at =', cleanupStart);
  assert(cleanupStart >= 0 && cleanupEnd > cleanupStart);
  const cleanup = source.slice(cleanupStart, cleanupEnd);
  const runCleanup = async relay => {
    const context = vm.createContext({
      browserOwned: false, intentionalRelayClose: false, roomRuntime: null, runPassed: false,
      transportCallbackQueue: {async drain() { return []; }},
      instances: {}, instanceRows: {}, checksumFiles: {}, peers: null, relay,
      path, output: 'out', childDirectory: role => role,
      pairResults: {outcome: 'complete', first_error: null, relay_closed: false, transport: {type: 'room-websocket'}},
      closeNotes: [], transportErrors: [], callbackErrors: [],
      recordAvailableTransportMetrics() {},
      fs: {async writeFile() {}},
    });
    await vm.runInContext(`(async()=>{${cleanup}})()`, context);
    return context;
  };
  const success = await runCleanup({transport: {type: 'room-websocket'}, async close() { return true; }});
  assert.equal(success.pairResults.relay_closed, true);
  assert.equal(success.pairResults.outcome, 'complete');
  assert.deepEqual(success.closeNotes, []);

  const failure = await runCleanup({transport: {type: 'room-websocket'}, async close() {
    throw Error('relay disposal rejection sentinel');
  }});
  assert.equal(failure.pairResults.relay_closed, false);
  assert.equal(failure.pairResults.outcome, 'fail');
  assert.match(failure.pairResults.first_error, /relay disposal rejection sentinel/);
  assert.match(failure.closeNotes[0], /relay disposal rejection sentinel/);
});

test('finalizer joins a delayed callback admitted during relay close', async () => {
  const source = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const cleanupStart = source.indexOf('  intentionalRelayClose = true;');
  const cleanupEnd = source.indexOf('  pairResults.finished_at =', cleanupStart);
  assert(cleanupStart >= 0 && cleanupEnd > cleanupStart);
  const cleanup = source.slice(cleanupStart, cleanupEnd);
  const callbackErrors = [];
  const transportErrors = [];
  const queue = createTransportCallbackQueue(({role, kind, error}) => {
    const row = {role, kind, message: String(error?.stack || error?.message || error)};
    callbackErrors.push(row);
    transportErrors.push({role, message: `${kind} callback failed: ${row.message}`});
  });
  let drainCalls = 0;
  const transportCallbackQueue = {
    track: queue.track,
    async drain() { ++drainCalls; return queue.drain(); },
    get pendingCount() { return queue.pendingCount; },
  };
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  let callbackStarted = false;
  let callbackFinished = false;
  const context = vm.createContext({
    browserOwned: false, intentionalRelayClose: false, roomRuntime: null, runPassed: false, transportCallbackQueue,
    instances: {}, instanceRows: {}, checksumFiles: {}, peers: null,
    relay: {transport: {type: 'room-websocket'}, async close() {
      // Model an event emitter dispatching terminal work without awaiting it.
      transportCallbackQueue.track('alpha', 'disconnect-terminal', async () => {
        callbackStarted = true;
        await gate;
        callbackFinished = true;
        throw Error('delayed during close rejection sentinel');
      });
      return true;
    }},
    path, output: 'out', childDirectory: role => role,
    pairResults: {outcome: 'complete', first_error: null, relay_closed: false, transport: {type: 'room-websocket'}},
    closeNotes: [], transportErrors, callbackErrors,
    recordAvailableTransportMetrics() {}, fs: {async writeFile() {}},
  });
  let settled = false;
  const finalizer = vm.runInContext(`(async()=>{${cleanup}})()`, context).finally(() => { settled = true; });
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(drainCalls, 2, 'the finalizer must drain before and after transport close');
  assert.equal(queue.pendingCount, 1);
  assert.equal(callbackStarted, true);
  assert.equal(callbackFinished, false);
  assert.equal(settled, false, 'the run result must wait for the close-admitted callback');
  release();
  await finalizer;
  assert.equal(callbackFinished, true);
  assert.equal(context.pairResults.relay_closed, true, 'relay close itself succeeded');
  assert.equal(context.pairResults.outcome, 'fail', 'late callback rejection fails the run');
  assert.match(context.pairResults.first_error, /delayed during close rejection sentinel/);
  assert.equal(context.pairResults.callback_errors.length, 1);
  assert.equal(transportCallbackQueue.pendingCount, 0);
});

test('browser finalizer observes late module failure after context close before reporting pass', async () => {
  const source = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('  intentionalRelayClose = true;');
  const end = source.indexOf('  pairResults.finished_at =', start);
  const instance = {closed: false, errors: [],
    async freezePeerModuleIdentity() { return []; },
    async timingPauseDiagnostics() { return null; }, async unload() {},
    async close() { this.closed = true; return true; },
    async finishPeerModuleIdentity() {
      assert.equal(this.closed, true, 'final module join must follow owned context close');
      throw Error('late module response failure sentinel');
    },
    async status() { return null; }, async native() { return null; },
  };
  const context = vm.createContext({browserOwned: true, intentionalRelayClose: false, roomRuntime: null, runPassed: false,
    transportCallbackQueue: {async drain() { return []; }},
    instances: {alpha: instance}, instanceRows: {alpha: {}}, checksumFiles: {}, peers: null,
    relay: {async close() {}}, path, output: 'out', childDirectory: role => role,
    pairResults: {outcome: 'complete', first_error: null, relay_closed: false},
    closeNotes: [], transportErrors: [], callbackErrors: [], fs: {async writeFile() {}},
  });
  await vm.runInContext(`(async()=>{${source.slice(start, end)}})()`, context);
  assert.equal(context.pairResults.outcome, 'fail');
  assert.match(context.pairResults.first_error, /closed-browser module identity.*late module response failure sentinel/);
  assert.equal(context.instanceRows.alpha.browser_closed, true);
});

const completedPeer = overrides => ({remote_ack_input: 5081, local_checksum_ticks: 5084,
  remote_checksum_ticks: 5084, next_checksum_compare: 5084, terminal: null,
  checksum_mismatches: [], ...overrides});
test('positive completion requires remote receipt and comparison, not just exported native records', () => {
  assert.deepEqual(verifyPositivePeerCompletion(completedPeer(), 5082, 5084), {
    remote_ack_input: 5081, local_checksum_ticks: 5084,
    remote_checksum_ticks: 5084, next_checksum_compare: 5084});
  for (const field of ['remote_ack_input', 'local_checksum_ticks', 'remote_checksum_ticks', 'next_checksum_compare']) {
    for (const delta of [-1, 1])
      assert.throws(() => verifyPositivePeerCompletion(completedPeer({[field]: completedPeer()[field] + delta}),
        5082, 5084), /exact ACK\/checksum totals/);
  }
  for (const change of [{terminal: {kind: 'disconnect'}}, {checksum_mismatches: [{tick: 5083}]},
    {checksum_mismatches: undefined}, {remote_checksum_ticks: '5084'}])
    assert.throws(() => verifyPositivePeerCompletion(completedPeer(change), 5082, 5084), /exact ACK\/checksum totals/);
});

const connectedRtc = overrides => ({attach_error: null, ready_state: 'open', ordered: true,
  max_retransmits: null, max_packet_lifetime: null, connection_state: 'connected',
  ice_connection_state: 'connected', local_candidate_types: ['host'], remote_candidate_types: ['host'], ...overrides});
test('final WebRTC observation rejects closed, unreliable or non-host endpoints', () => {
  assert.deepEqual(verifyReliableHostWebRtc(connectedRtc()), connectedRtc());
  assert.equal(verifyReliableHostWebRtc(connectedRtc({ice_connection_state: 'completed'})).ice_connection_state, 'completed');
  for (const change of [{ready_state: 'closed'}, {ordered: false}, {max_retransmits: 1},
    {max_packet_lifetime: 1}, {connection_state: 'disconnected'}, {ice_connection_state: 'failed'},
    {attach_error: 'error'}, {local_candidate_types: []}, {remote_candidate_types: ['srflx']},
    {remote_candidate_types: undefined}])
    assert.throws(() => verifyReliableHostWebRtc(connectedRtc(change)), /connected, reliable, ordered and host-only/);
});
