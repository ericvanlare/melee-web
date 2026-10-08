import assert from 'node:assert/strict';
import test from 'node:test';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {LockstepPeer} from '../scripts/net_lockstep_core.mjs';
import {createBrowserNativePeer, BROWSER_CHECKSUM_EXPORT_LIMIT} from '../scripts/net_lockstep_browser_peer.mjs';
import {browserPeerFacade, createPeerModuleResponseObserver} from '../scripts/net_session_instance.mjs';

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}
function harness({terminalFailure = false, confirmFailure = false, sourceTicks = 8,
  readyGate = null, inputCapture = null, dispose = null, endpointCloseGate = null,
  endpointCloseStarted = null, endpointCloseFailure = null, cleanupOrder = []} = {}) {
  const sent = [], nativeRecords = [], frames = [], terminals = [], pending = new Set(), captureConfigs = [];
  let callbacks, confirmations = 0, pushFailure = false;
  const endpoint = {ready: Promise.resolve(), closed: false, errors: [], transport: {type: 'room-websocket'},
    send: async text => { sent.push(JSON.parse(text)); },
    async drainInbound() { while (pending.size) await Promise.all([...pending]); },
    async close() {
      cleanupOrder.push('endpoint-close-start'); endpointCloseStarted?.resolve();
      if (endpointCloseGate) await endpointCloseGate.promise;
      endpoint.closed = true; await callbacks.onDisconnect('alpha', 'closed');
      cleanupOrder.push('endpoint-close-end');
      if (endpointCloseFailure) throw endpointCloseFailure;
    },
  };
  const controller = createBrowserNativePeer({role: 'alpha', sourceTicks, inputTicks: sourceTicks - 2,
    inputCapture,
    relayUrl: 'ws://example.test', roomId: 'a'.repeat(32), agreement: {build: 'same'}, native: {
      async pushIndexed(tick, bytes) { if (pushFailure) return false; frames.push({tick, bytes: Array.from(bytes)}); return true; },
      configureLocalInputCapture(port, ticks) { captureConfigs.push({port, ticks}); return port === 0 && ticks === sourceTicks - 2; },
      async confirmStart() { ++confirmations; if (readyGate) await readyGate.promise; return !confirmFailure; },
      terminate(...args) { terminals.push(args); if (terminalFailure) throw Error('terminal callback failure'); },
      status: () => ({cursor: nativeRecords.length}),
      drain: max => nativeRecords.splice(0, max),
      dispose() { cleanupOrder.push('native-dispose'); return dispose?.(); },
    }}, {createEndpoint: options => { callbacks = options; return endpoint; }});
  async function deliver(packet) {
    const result = callbacks.onMessage(typeof packet === 'string' ? packet : JSON.stringify(packet));
    const observed = result.catch(() => {}).finally(() => pending.delete(observed));
    pending.add(observed);
    return result;
  }
  async function ready() {
    await controller.rpc('start', [{build: 'same'}]);
    await deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0, agreement: {build: 'same'}});
    await controller.rpc('snapshot');
  }
  const record = tick => {
    const bytes = new Uint8Array(64);
    new DataView(bytes.buffer).setUint32(0, tick, true);
    return Array.from(bytes);
  };
  return {controller, endpoint, sent, nativeRecords, frames, terminals, deliver, ready, record,
    captureConfigs, cleanupOrder, setPushFailure(value) { pushFailure = value; },
    get confirmations() { return confirmations; }};
}

test('page controller confirms start and gives native records to peer before exporting evidence', async () => {
  const run = harness();
  await run.ready();
  assert.equal(run.confirmations, 1);
  assert.equal(run.frames[0].tick, 0);
  assert.equal(run.frames[0].bytes.length, 88);
  run.nativeRecords.push(run.record(0), run.record(1));
  const row = await run.controller.rpc('drain');
  assert.equal(row.protocol.local_checksum_ticks, 2);
  assert.equal(row.checksumOwnership.active_native_records_submitted_before_export, 2);
  assert.equal(row.checksumOwnership.post_terminal_native_evidence_records, 0);
  assert.deepEqual(row.records, [run.record(0), run.record(1)]);
  assert.equal((await run.controller.rpc('drain')).records.length, 0);
  await run.controller.close();
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.terminals.length, 0);
});

test('opt-in page capture copies native bytes, records source ticks and releases only its retained sample', async () => {
  const priorState = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadState');
  const priorTransition = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadTransition');
  globalThis.__meleeSyntheticPadState = 'neutral';
  globalThis.__meleeSyntheticPadTransition = state => { globalThis.__meleeSyntheticPadState = state; };
  const run = harness({sourceTicks: 6, inputCapture: {deferSendTicks: [0],
    pattern: ['neutral', 'A', 'release', 'neutral']}});
  try {
    await run.ready();
    assert.deepEqual(run.captureConfigs, [{port: 0, ticks: 4}], 'native sampling is configured before peer confirmation');
    const capture = globalThis.__meleeWebNetLocalInputCapture;
    const original = Uint8Array.of(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert.equal(capture(0, 0, 8, original), true);
    original[1] = 0xff;
    assert.equal(globalThis.__meleeSyntheticPadState, 'A', 'next sample transition is in-page after capture');
    const pressed = Uint8Array.of(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert.equal(capture(1, 0, 9, pressed), true);
    assert.equal(globalThis.__meleeSyntheticPadState, 'release');
    const snapshot = await run.controller.rpc('snapshot');
    assert.equal(snapshot.localInputCapture.mode, 'diagnostic');
    assert.deepEqual(snapshot.localInputCapture.captures.map(row => [row.source_cursor, row.input_tick,
      row.poll_serial, row.bytes[1], row.synthetic_state]), [
      [0, 0, 8, 1, 'neutral'], [1, 1, 9, 0, 'A'],
    ]);
    assert.deepEqual(snapshot.protocol.deferred_input_ticks, [0]);
    await run.controller.rpc('releaseCapturedInput', [0]);
    assert.deepEqual(run.controller.snapshot().protocol.deferred_input_ticks, []);
    await assert.rejects(run.controller.rpc('addLocalInput', [2, new Uint8Array(11)]),
      /cannot be mixed with browser-local sampling/);
    assert.equal(run.terminals.at(-1)[0], 3, 'manual recipe publication becomes a protocol terminal');
  } finally {
    try { await run.controller.close(); } catch {}
    if (priorState) Object.defineProperty(globalThis, '__meleeSyntheticPadState', priorState);
    else delete globalThis.__meleeSyntheticPadState;
    if (priorTransition) Object.defineProperty(globalThis, '__meleeSyntheticPadTransition', priorTransition);
    else delete globalThis.__meleeSyntheticPadTransition;
    delete globalThis.__meleeWebNetLocalInputCapture;
  }
});

test('explicit live input publishes exact native samples without synthetic Gamepad globals', async () => {
  const priorState = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadState');
  const priorTransition = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadTransition');
  delete globalThis.__meleeSyntheticPadState;
  delete globalThis.__meleeSyntheticPadTransition;
  assert.equal(globalThis.__meleeWebNetLocalInputCapture ?? null, null);
  const run = harness({sourceTicks: 6, inputCapture: {mode: 'live'}});
  try {
    await run.ready();
    assert.equal(globalThis.__meleeSyntheticPadState, undefined);
    assert.equal(globalThis.__meleeSyntheticPadTransition, undefined);
    const capture = globalThis.__meleeWebNetLocalInputCapture;
    const first = Uint8Array.of(0x80, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a);
    const second = Uint8Array.of(0x00, 0xff, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90);
    assert.equal(capture(0, 0, 41, first), true);
    first[1] = 0xee;
    assert.equal(capture(1, 0, 42, second), true);

    const snapshot = await run.controller.rpc('snapshot');
    assert.deepEqual(run.captureConfigs, [{port: 0, ticks: 4}]);
    assert.equal(snapshot.localInputCapture.enabled, true);
    assert.equal(snapshot.localInputCapture.mode, 'live');
    assert.equal(snapshot.localInputCapture.input_ticks, 4);
    assert.equal(snapshot.localInputCapture.captures.length, 2);
    assert.deepEqual(snapshot.localInputCapture.captures.map(row => [row.source_cursor, row.input_tick,
      row.local_port, row.poll_serial, row.bytes]), [
      [0, 0, 0, 41, [0x80, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a]],
      [1, 1, 0, 42, [0x00, 0xff, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90]],
    ]);
    assert.equal(Object.hasOwn(snapshot.localInputCapture.captures[0], 'synthetic_state'), false);
    const latest = run.sent.filter(packet => packet.type === 'state').at(-1);
    assert.deepEqual(latest.unacknowledged.map(entry => [entry.tick, [...Buffer.from(entry.pad, 'base64')]]), [
      [0, [0x80, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a]],
      [1, [0x00, 0xff, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90]],
    ]);
    await run.controller.close();
    assert.equal(globalThis.__meleeWebNetLocalInputCapture, null);
  } finally {
    try { await run.controller.close(); } catch {}
    delete globalThis.__meleeWebNetLocalInputCapture;
    if (priorState) Object.defineProperty(globalThis, '__meleeSyntheticPadState', priorState);
    else delete globalThis.__meleeSyntheticPadState;
    if (priorTransition) Object.defineProperty(globalThis, '__meleeSyntheticPadTransition', priorTransition);
    else delete globalThis.__meleeSyntheticPadTransition;
  }
});

test('live and diagnostic input options are unambiguous before native configuration', () => {
  assert.equal(globalThis.__meleeWebNetLocalInputCapture ?? null, null);
  const invalid = [
    {inputCapture: {mode: 'live', pattern: ['neutral']}},
    {inputCapture: {mode: 'live', deferSendTicks: []}},
    {inputCapture: {mode: 'other', pattern: [], deferSendTicks: []}},
    {inputCapture: {mode: 'diagnostic', deferSendTicks: []}},
    {inputCapture: {pattern: [], deferSendTicks: []}},
    {inputCapture: {mode: 'live'}, autonomousPump: true},
    {inputCapture: {mode: 'live'}, sourceTicks: 7},
    {inputCapture: {mode: 'live'}, timeoutMs: 0},
  ];
  for (const options of invalid) {
    let configureCalls = 0, endpointCalls = 0;
    const sourceTicks = options.sourceTicks ?? 6;
    assert.throws(() => createBrowserNativePeer({role: 'alpha', sourceTicks,
      inputTicks: 4, relayUrl: 'ws://example.test', roomId: 'a'.repeat(32),
      agreement: {build: 'same'}, inputCapture: options.inputCapture,
      timeoutMs: options.timeoutMs ?? 5000,
      autonomousPump: options.autonomousPump ?? false,
      native: {configureLocalInputCapture() { ++configureCalls; return true; }}},
    {createEndpoint() { ++endpointCalls; throw Error('endpoint must not be allocated'); }}));
    assert.equal(configureCalls, 0, 'invalid options must fail before native input configuration');
    assert.equal(endpointCalls, 0, 'invalid options must fail before endpoint allocation');
  }
});

test('peer and endpoint construction fail before native capture, and setup refusal joins owned close cleanup', async () => {
  assert.equal(globalThis.__meleeWebNetLocalInputCapture ?? null, null);
  let configureCalls = 0;
  assert.throws(() => createBrowserNativePeer({role: 'alpha', sourceTicks: 6, inputTicks: 4,
    relayUrl: 'bad relay url', roomId: 'a'.repeat(32), timeoutMs: 5000,
    agreement: {build: 'same'}, inputCapture: {mode: 'live'},
    native: {configureLocalInputCapture() { ++configureCalls; return true; }}},
  {createEndpoint() { throw Error('endpoint rejected relay configuration'); }}), /endpoint rejected relay configuration/);
  assert.equal(configureCalls, 0, 'endpoint validation must complete before native capture is enabled');

  const closeStarted = deferred(), allowCloseToFail = deferred();
  const endpointCloseFailure = Error('endpoint close failed after setup refusal');
  let endpointOptions;
  const endpoint = {ready: Promise.resolve(), closed: false, errors: [], transport: {type: 'test'},
    send: async () => {}, drainInbound: async () => {},
    async close() {
      closeStarted.resolve();
      await allowCloseToFail.promise;
      endpoint.closed = true;
      await endpointOptions.onDisconnect('alpha', 'closed');
      throw endpointCloseFailure;
    }};
  const controller = createBrowserNativePeer({role: 'alpha', sourceTicks: 6, inputTicks: 4,
    relayUrl: 'ws://example.test', roomId: 'a'.repeat(32), timeoutMs: 5000,
    agreement: {build: 'same'}, inputCapture: {mode: 'live'},
    native: {configureLocalInputCapture() { ++configureCalls; return false; }}},
  {createEndpoint(options) { endpointOptions = options; return endpoint; }});
  assert.equal(configureCalls, 1);
  assert.equal(controller.snapshot().failure.includes('configuration was rejected'), true,
    'failed setup retains the original configuration failure');
  assert.equal(globalThis.__meleeWebNetLocalInputCapture, null,
    'failed native setup deactivates its callback before returning the cleanup owner');
  let closeSettled = false;
  const closing = controller.close().finally(() => { closeSettled = true; });
  await closeStarted.promise;
  await Promise.resolve();
  assert.equal(closeSettled, false, 'close remains pending while endpoint teardown is pending');
  allowCloseToFail.resolve();
  await assert.rejects(closing, error => {
    assert.equal(error instanceof AggregateError, true);
    assert.equal(error.errors.some(item => String(item).includes('configuration was rejected')), true,
      'joined close reports the original configuration failure');
    assert.equal(error.errors.includes(endpointCloseFailure), true,
      'joined close reports endpoint teardown failure');
    return true;
  });
  assert.equal(endpoint.closed, true, 'failed setup closes its already allocated endpoint');
});

test('live input rejects a second callback owner before configuring native capture', async () => {
  const first = harness({sourceTicks: 6, inputCapture: {mode: 'live'}});
  try {
    let configureCalls = 0, endpointCalls = 0;
    assert.throws(() => createBrowserNativePeer({role: 'beta', sourceTicks: 6,
      inputTicks: 4, relayUrl: 'ws://example.test', roomId: 'b'.repeat(32),
      agreement: {build: 'same'}, inputCapture: {mode: 'live'},
      native: {configureLocalInputCapture() { ++configureCalls; return true; }}},
    {createEndpoint() { ++endpointCalls; throw Error('endpoint must not be allocated'); }}),
    /already has an owner/);
    assert.equal(configureCalls, 0);
    assert.equal(endpointCalls, 0);
  } finally {
    await first.controller.close();
  }
  assert.equal(globalThis.__meleeWebNetLocalInputCapture, null);
});

test('live input enforces source tick, selected port, increasing serial and exact PAD size', async () => {
  const invalidSamples = [
    [1, 0, 1, new Uint8Array(11)],
    [0, 1, 1, new Uint8Array(11)],
    [0, 0, -1, new Uint8Array(11)],
    [0, 0, Number.NaN, new Uint8Array(11)],
    [0, 0, 1, new Uint8Array(10)],
  ];
  for (const sample of invalidSamples) {
    const run = harness({sourceTicks: 6, inputCapture: {mode: 'live'}});
    await run.ready();
    const capture = globalThis.__meleeWebNetLocalInputCapture;
    assert.equal(capture(...sample), false);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(run.controller.snapshot().localInputCapture.captures.length, 0);
    assert.equal(run.terminals.at(-1)[0], 3);
    await assert.rejects(run.controller.close(), /Browser native peer close failed/);
    assert.equal(globalThis.__meleeWebNetLocalInputCapture, null);
  }
});

test('live input callbacks stop after terminal and at close entry', async () => {
  const terminal = harness({sourceTicks: 6, inputCapture: {mode: 'live'}});
  await terminal.ready();
  const retainedAfterTerminal = globalThis.__meleeWebNetLocalInputCapture;
  await terminal.controller.rpc('disconnect', ['test terminal']);
  const sentAfterTerminal = terminal.sent.length;
  assert.equal(retainedAfterTerminal(0, 0, 1, new Uint8Array(11)), false);
  assert.equal(terminal.sent.length, sentAfterTerminal, 'a terminal callback cannot publish another protocol packet');
  assert.match(terminal.controller.snapshot().failure, /no longer active/);
  await assert.rejects(terminal.controller.close(), /Browser native peer close failed/);
  assert.equal(globalThis.__meleeWebNetLocalInputCapture, null);

  const racing = harness({sourceTicks: 6, inputCapture: {mode: 'live'}});
  await racing.ready();
  const retainedAtClose = globalThis.__meleeWebNetLocalInputCapture;
  const sentBeforeCapture = racing.sent.length;
  const nativePublishesBeforeCapture = racing.frames.length;
  assert.equal(retainedAtClose(0, 0, 5, new Uint8Array(11)), true);
  const closing = racing.controller.close();
  assert.equal(globalThis.__meleeWebNetLocalInputCapture, null, 'owned callback is deactivated synchronously at close entry');
  await assert.rejects(closing, /Browser native peer close failed/);
  const sentAfterClose = racing.sent.length;
  assert.equal(retainedAtClose(1, 0, 6, new Uint8Array(11)), false);
  assert.equal(racing.sent.length, sentAfterClose, 'queued or retained callbacks cannot publish after close starts');
  assert.equal(sentAfterClose, sentBeforeCapture, 'queued local publication is stopped by close');
  assert.equal(racing.frames.length, nativePublishesBeforeCapture,
    'stopped input never reaches native indexed publication after the close race');
  assert.match(racing.controller.snapshot().failure, /stopped before queued publication/);
});

test('sampled input rejects cursor/port/serial errors and async native queue failures terminate', async () => {
  const priorState = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadState');
  const priorTransition = Object.getOwnPropertyDescriptor(globalThis, '__meleeSyntheticPadTransition');
  globalThis.__meleeSyntheticPadState = 'neutral';
  globalThis.__meleeSyntheticPadTransition = state => { globalThis.__meleeSyntheticPadState = state; };
  const run = harness({sourceTicks: 6, inputCapture: {deferSendTicks: [],
    pattern: ['neutral', 'A', 'release', 'neutral']}});
  try {
    await run.ready();
    const capture = globalThis.__meleeWebNetLocalInputCapture;
    assert.equal(capture(0, 1, 1, new Uint8Array(11)), false, 'selected local port is fixed by peer role');
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(run.terminals.at(-1)[0], 3);
  } finally {
    try { await run.controller.close(); } catch {}
    if (priorState) Object.defineProperty(globalThis, '__meleeSyntheticPadState', priorState);
    else delete globalThis.__meleeSyntheticPadState;
    if (priorTransition) Object.defineProperty(globalThis, '__meleeSyntheticPadTransition', priorTransition);
    else delete globalThis.__meleeSyntheticPadTransition;
    delete globalThis.__meleeWebNetLocalInputCapture;
  }

  globalThis.__meleeSyntheticPadState = 'neutral';
  globalThis.__meleeSyntheticPadTransition = state => { globalThis.__meleeSyntheticPadState = state; };
  const asyncFailure = harness({sourceTicks: 6, inputCapture: {deferSendTicks: [],
    pattern: ['neutral', 'A', 'release', 'neutral']}});
  try {
    await asyncFailure.ready();
    asyncFailure.setPushFailure(true);
    assert.equal(globalThis.__meleeWebNetLocalInputCapture(0, 0, 5,
      Uint8Array.of(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)), true,
    'the synchronous hook queues work without awaiting native input publication');
    const remoteWire = [];
    const remote = new LockstepPeer({role: 'beta', sourceTicks: 6, inputTicks: 4,
      pushFrame: async () => {}});
    remote.attach(async text => remoteWire.push(text));
    await remote.start({build: 'same'});
    await remote.receive(JSON.stringify(asyncFailure.sent[0]));
    await remote.addLocalInput(0, new Uint8Array(11));
    await assert.rejects(asyncFailure.deliver(remoteWire.at(-1)), /Native indexed input queue rejected/);
    await assert.rejects(asyncFailure.controller.rpc('snapshot'), /Native indexed input queue rejected/);
    assert.equal(asyncFailure.terminals.at(-1)[0], 3, 'async submission failure reaches native terminal');
    const retainedAfterFailure = globalThis.__meleeWebNetLocalInputCapture;
    const terminalCount = asyncFailure.terminals.length;
    assert.equal(retainedAfterFailure(1, 0, 6, new Uint8Array(11)), false);
    assert.equal(asyncFailure.terminals.length, terminalCount, 'failed capture cannot make another native call');
  } finally {
    try { await asyncFailure.controller.close(); } catch {}
    if (priorState) Object.defineProperty(globalThis, '__meleeSyntheticPadState', priorState);
    else delete globalThis.__meleeSyntheticPadState;
    if (priorTransition) Object.defineProperty(globalThis, '__meleeSyntheticPadTransition', priorTransition);
    else delete globalThis.__meleeSyntheticPadTransition;
    delete globalThis.__meleeWebNetLocalInputCapture;
  }
});

test('fixed export capacity overflow remains sticky through snapshot, drain and close', async () => {
  const run = harness({sourceTicks: 516});
  await run.ready();
  run.nativeRecords.push(...Array.from({length: BROWSER_CHECKSUM_EXPORT_LIMIT + 1}, (_, tick) => run.record(tick)));
  await assert.rejects(run.controller.rpc('snapshot'), /512-record bound/);
  await assert.rejects(run.controller.rpc('drain'), /512-record bound/);
  await assert.rejects(run.controller.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.terminals[0][0], 3);
});

test('late receive terminal callback failure is observed by settled RPC and close', async () => {
  const run = harness({terminalFailure: true});
  await run.ready();
  await assert.rejects(run.deliver('invalid JSON'), /terminal callback failure/);
  await assert.rejects(run.controller.rpc('snapshot'), /terminal callback failure/);
  await assert.rejects(run.controller.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});

test('close joins a delayed inbound receive before returning', async () => {
  const run = harness();
  await run.ready();
  const gate = deferred();
  const original = run.endpoint.drainInbound;
  run.endpoint.drainInbound = async () => { await gate.promise; await original(); };
  let complete = false;
  const closing = run.controller.close().then(() => { complete = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(complete, false);
  gate.resolve();
  await closing;
  assert.equal(complete, true);
});

test('native adapter disposal joins endpoint teardown and aggregates both cleanup failures', async () => {
  const gate = deferred(), started = deferred(), order = [];
  const endpointFailure = Error('endpoint teardown failed');
  const adapterFailure = Error('native adapter disposal failed');
  const run = harness({endpointCloseGate: gate, endpointCloseStarted: started,
    endpointCloseFailure: endpointFailure, cleanupOrder: order,
    dispose() { throw adapterFailure; }});
  let settled = false;
  const closing = run.controller.close().finally(() => { settled = true; });
  await started.promise;
  assert.equal(settled, false, 'close must remain pending while endpoint work is open');
  assert.equal(order.includes('native-dispose'), false, 'native scratch stays owned until endpoint teardown joins');
  gate.resolve();
  await assert.rejects(closing, error => {
    assert.equal(error instanceof AggregateError, true);
    assert(error.errors.includes(endpointFailure));
    assert(error.errors.includes(adapterFailure));
    return true;
  });
  assert.deepEqual(order, ['endpoint-close-start', 'endpoint-close-end', 'native-dispose']);
});

test('native start confirmation failure reaches the terminal and remains sticky', async () => {
  const run = harness({confirmFailure: true});
  await run.controller.rpc('start', [{build: 'same'}]);
  await assert.rejects(run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}}), /confirmation/);
  await assert.rejects(run.controller.rpc('snapshot'), /confirmation/);
  await assert.rejects(run.controller.close(), /close failed/);
});

test('facade refresh replaces stale ACK and terminal snapshot and serializes PAD byte views', async () => {
  const calls = [];
  const initial = {protocol: {remote_ack_input: -1, ready: false, terminal: null}, endpointErrors: []};
  const instance = {async peerRpc(name, args) {
    calls.push({name, args});
    return {protocol: {remote_ack_input: 5, ready: true, terminal: {kind: 'disconnect'}}, endpointErrors: []};
  }};
  const peer = browserPeerFacade(instance, initial);
  assert.equal(peer.remoteAckInput, -1);
  await peer.refresh();
  assert.equal(peer.remoteAckInput, 5);
  assert.equal(peer.terminal.kind, 'disconnect');
  await peer.addLocalInput(1, Uint8Array.of(1, 2), {nativeOverrides: [[1, Uint8Array.of(3)]]});
  assert.deepEqual(calls[1].args, [1, [1, 2], {nativeOverrides: [[1, [3]]]}]);
});


test('close fences an inbound callback queued before execution while the snapshot remains readable', async () => {
  const gate = deferred(), run = harness({readyGate: gate});
  await run.controller.rpc('start', [{build: 'same'}]);
  const receive = run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}});
  let snapshotDone = false, closeDone = false;
  const snapshot = run.controller.rpc('snapshot').then(() => { snapshotDone = true; });
  const close = run.controller.close().then(() => { closeDone = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(snapshotDone, true);
  assert.equal(closeDone, true);
  gate.resolve();
  await Promise.all([receive, snapshot, close]);
  assert.equal(run.confirmations, 0, 'the not-yet-entered handshake callback is fenced at execution');
  assert.equal(closeDone, true);
});

const moduleNames = ['net_lockstep_browser_peer.mjs', 'net_lockstep_core.mjs',
  'net_lockstep_native_adapter.mjs', 'net_lockstep_websocket_relay.mjs'];
const webrtcModuleNames = [...moduleNames, 'net_lockstep_webrtc.mjs'];
const roomSignaledWebRtcModuleNames = [...webrtcModuleNames, 'net_lockstep_webrtc_signaling.mjs'];
const runtimeOwnedModuleNames = [...roomSignaledWebRtcModuleNames,
  'net_lockstep_runtime_owner.mjs', 'net_lockstep_development_owner.mjs'];
const moduleBody = Buffer.from('module bytes');
const moduleHash = createHash('sha256').update(moduleBody).digest('hex');
function moduleObserver(names = moduleNames) {
  return createPeerModuleResponseObserver({url: 'http://127.0.0.1:8787/runtime.html',
    peerModuleHashes: Object.fromEntries(names.map(name => [name, moduleHash])),
    runtimeArtifactNames: ['runtime-development.mjs']});
}
function response(name, {body = moduleBody, coop = 'same-origin', status = 200} = {}) {
  return {url: () => new URL(name, 'http://127.0.0.1:8787/').href, status: () => status,
    allHeaders: async () => ({'cross-origin-opener-policy': coop, 'cross-origin-embedder-policy': 'require-corp'}),
    body: async () => body};
}

test('peer module observer records exact loaded bytes while admitting catalog runtime modules', async () => {
  const observer = moduleObserver();
  observer.observe(response('runtime-development.mjs'));
  for (const name of moduleNames) observer.observe(response(name));
  const rows = await observer.freeze();
  assert.equal(rows.length, 4);
  assert.ok(rows.every(row => row.sha256 === moduleHash && row.bytes === moduleBody.length));
});

test('peer module observer binds the optional local WebRTC endpoint module', async () => {
  const observer = moduleObserver(webrtcModuleNames);
  for (const name of webrtcModuleNames) observer.observe(response(name));
  const rows = await observer.freeze();
  assert.deepEqual(rows.map(row => new URL(row.url).pathname.split('/').at(-1)).sort(),
    [...webrtcModuleNames].sort());
  assert.throws(() => createPeerModuleResponseObserver({url: 'http://127.0.0.1:8787/runtime.html',
    peerModuleHashes: {'net_lockstep_webrtc.mjs': moduleHash}, runtimeArtifactNames: []}),
  /exact relay, WebRTC, room-signaling or runtime-owned module SHA-256 inventory/);
});

test('peer module observer binds the page-owned room signaling module as an exact extension', async () => {
  const observer = moduleObserver(roomSignaledWebRtcModuleNames);
  for (const name of roomSignaledWebRtcModuleNames) observer.observe(response(name));
  const rows = await observer.freeze();
  assert.deepEqual(rows.map(row => new URL(row.url).pathname.split('/').at(-1)).sort(),
    [...roomSignaledWebRtcModuleNames].sort());
  assert.throws(() => moduleObserver([...webrtcModuleNames, 'unexpected.mjs']),
    /exact relay, WebRTC, room-signaling or runtime-owned module SHA-256 inventory/);
});

test('runtime-owned module observer binds both lifecycle owners as an exact eight-module set', async () => {
  const observer = moduleObserver(runtimeOwnedModuleNames);
  for (const name of runtimeOwnedModuleNames) observer.observe(response(name));
  const rows = await observer.freeze();
  assert.equal(rows.length, 8);
  assert.deepEqual(rows.map(row => new URL(row.url).pathname.split('/').at(-1)).sort(),
    [...runtimeOwnedModuleNames].sort());
  assert.throws(() => moduleObserver([...runtimeOwnedModuleNames, 'unexpected.mjs']),
    /exact relay, WebRTC, room-signaling or runtime-owned module SHA-256 inventory/);
});

test('peer module observer rejects unexpected paths, changed bytes, headers and duplicate/missing responses', async () => {
  for (const name of ['unexpected.mjs', 'subdir/net_lockstep_core.mjs', 'https://elsewhere.test/net_lockstep_core.mjs',
    'net_lockstep_core.mjs?extra=1']) {
    const observer = moduleObserver();
    for (const expected of moduleNames) observer.observe(response(expected));
    observer.observe(response(name));
    await assert.rejects(observer.freeze(), /Unexpected browser module import/);
  }
  for (const bad of [{body: Buffer.from('changed')}, {coop: 'unsafe-none'}, {status: 404}]) {
    const observer = moduleObserver();
    for (const name of moduleNames) observer.observe(response(name, name === moduleNames[0] ? bad : {}));
    await assert.rejects(observer.freeze(), /response identity differs/);
  }
  const duplicate = moduleObserver();
  for (const name of [...moduleNames, moduleNames[0]]) duplicate.observe(response(name));
  await assert.rejects(duplicate.freeze(), /incomplete or duplicated/);
  const missing = moduleObserver();
  missing.observe(response(moduleNames[0]));
  await assert.rejects(missing.freeze(), /incomplete or duplicated/);
});

test('page controller imports only unchanged portable core and endpoint', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser_peer.mjs', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /\b(?:Buffer|process|require|node:)\b/);
  const imports = [...source.matchAll(/^import .* from '([^']+)';$/gm)].map(match => match[1]);
  assert.deepEqual(imports, ['./net_lockstep_core.mjs', './net_lockstep_websocket_relay.mjs']);
});


test('an immediate remote hello joins startup before any facade start RPC', async () => {
  const run = harness();
  await run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}});
  assert.equal(run.confirmations, 1);
  const row = await run.controller.rpc('start');
  assert.equal(row.protocol.ready, true);
  assert.equal(run.sent.filter(packet => packet.type === 'hello').length, 1);
  await run.controller.close();
});

test('module observer fails sticky on bounded events and rechecks late imports after early freeze', async () => {
  const observer = moduleObserver();
  for (const name of moduleNames) observer.observe(response(name));
  await observer.freeze();
  observer.observe(response('late-unexpected.mjs'));
  await assert.rejects(observer.freeze(), /Unexpected browser module import/);
  const bounded = moduleObserver();
  for (let i = 0; i < 6; ++i) bounded.observe(response('runtime-development.mjs'));
  await assert.rejects(bounded.freeze(), /event bound exceeded/);
});


test('post-terminal records remain separately labeled native evidence without touching closed protocol', async () => {
  const run = harness();
  await run.ready();
  await run.controller.rpc('disconnect', ['controlled disconnect']);
  run.nativeRecords.push(run.record(0));
  const row = await run.controller.rpc('drain');
  assert.equal(row.protocol.local_checksum_ticks, 0);
  assert.equal(row.checksumOwnership.active_native_records_submitted_before_export, 0);
  assert.equal(row.checksumOwnership.post_terminal_native_evidence_records, 1);
  assert.deepEqual(row.records, [run.record(0)]);
  await run.controller.close();
});


test('closed-browser inventory joins pending observations and rejects without a new body read', async () => {
  const observer = moduleObserver();
  for (const name of moduleNames) observer.observe(response(name));
  await observer.freeze();
  const gate = deferred();
  let bodyReads = 0;
  observer.observe({...response(moduleNames[0]), allHeaders: () => gate.promise,
    body: async () => { ++bodyReads; return moduleBody; }});
  const finishing = observer.freeze({closed: true});
  gate.resolve({'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp'});
  await assert.rejects(finishing, /closed before peer module response body/);
  assert.equal(bodyReads, 0);
  await assert.rejects(observer.freeze(), /closed before peer module response body/);
});

test('unarmed endpoint close calls native disconnect without sending on the closed channel', async () => {
  const run = harness();
  await run.ready();
  const before = run.sent.length;
  run.endpoint.send = async () => { throw Error('send after endpoint close'); };
  const row = await run.controller.close({intentional: false});
  assert.equal(row.protocol.terminal.kind, 'disconnect');
  assert.equal(run.terminals.length, 1);
  assert.equal(run.sent.length, before);
  run.nativeRecords.push(run.record(0));
  const drained = await run.controller.rpc('drain');
  assert.deepEqual(drained.records, []);
  assert.equal(run.nativeRecords.length, 1, 'closed peer does not touch native scratch after disposal');
  assert.equal(drained.protocol.local_checksum_ticks, 0);
});

test('unarmed endpoint close retains rejected native terminal callback', async () => {
  const run = harness({terminalFailure: true});
  await run.ready();
  await assert.rejects(run.controller.close({intentional: false}), /Browser native peer close failed/);
  assert.match(run.controller.snapshot().failure, /terminal callback failure/);
  assert.equal(run.terminals.length, 1);
});
