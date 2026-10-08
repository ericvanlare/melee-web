import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {createBrowserNativePeer} from '../scripts/net_lockstep_browser_peer.mjs';
import {LockstepPeer} from '../scripts/net_lockstep_core.mjs';
import {createRoomTransport, createRuntimeLockstepSession} from '../scripts/net_lockstep_runtime_owner.mjs';

const turn = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  promise.catch(() => {});
  return {promise, resolve, reject};
}

function adapterFixture({startRecorded = 0, statusFailure = null} = {}) {
  const events = [];
  const state = {active: 1, cursor: 0, ring_pending: 0, blocker: 'network_wait',
    terminal: {kind: 0, tick: 0, channel: 0},
    start: {recorded: startRecorded, required: 1, capture_failed: false}};
  const adapter = {
    pushIndexed(first, bytes) { events.push(['push', first, bytes.length]); return true; },
    configureLocalInputCapture(port, ticks) { events.push(['capture', port, ticks]); return true; },
    confirmStart() { events.push(['confirm']); return true; },
    terminate(kind, tick, channel) {
      events.push(['terminate', kind, tick, channel]);
      state.terminal = {kind, tick, channel}; state.blocker = 'terminal';
    },
    status() { events.push(['status']); if (statusFailure) throw statusFailure; return structuredClone(state); },
    drain(max) { events.push(['drain', max]); return {count: 0, bytes: new Uint8Array(0)}; },
    dispose() { events.push(['dispose']); return true; },
  };
  return {adapter, state, events};
}

function pairedTransport({agreement, sourceTicks, inputTicks, role}) {
  const remoteRole = role === 'alpha' ? 'beta' : 'alpha';
  const queue = new Set(), events = [];
  let callbacks = null, endpoint = null, closed = false;
  const remote = new LockstepPeer({role: remoteRole, sourceTicks, inputTicks,
    pushFrame: async () => true});
  remote.attach(async text => {
    const operation = Promise.resolve().then(() => callbacks?.onMessage(text));
    queue.add(operation);
    try { await operation; } finally { queue.delete(operation); }
  });
  return {
    events,
    createEndpoint(options) {
      callbacks = options;
      endpoint = {ready: Promise.resolve(), closed: false, errors: [],
        transport: {type: 'webrtc-datachannel', ordered: true, reliable: true},
        send: text => remote.receive(text),
        async drainInbound() { while (queue.size) await Promise.all([...queue]); },
        async close() { endpoint.closed = true; closed = true; await options.onDisconnect(remoteRole, 'local close'); },
      };
      return endpoint;
    },
    async start() { events.push(['transport-start']); await remote.start(agreement); },
    async disconnect(reason = 'remote closed') {
      return callbacks.onDisconnect(remoteRole, reason);
    },
    async close() {
      if (closed) return;
      events.push(['transport-close']); closed = true;
      if (endpoint) endpoint.closed = true;
    },
  };
}

function makeSession({adapterFixture: native = adapterFixture(), role = 'alpha', sourceTicks = 8,
  transportFactory = pairedTransport, peerFactory = undefined} = {}) {
  let transport;
  const session = createRuntimeLockstepSession({Module: {}, role, sourceTicks,
    inputTicks: sourceTicks - 2, url: 'ws://127.0.0.1:8787', roomId: 'a'.repeat(32),
    timeoutMs: 250, createAdapter: () => native.adapter,
    ...(peerFactory ? {createPeer: peerFactory} : {}),
    createTransport(options) { transport = transportFactory(options); return transport; }});
  return {session, native, get transport() { return transport; }};
}

const hostSdp = 'v=0\r\na=candidate:1 1 udp 2122260223 127.0.0.1 8998 typ host\r\n';

function trackListeners(target) {
  const listeners = new Map(), add = target.addEventListener.bind(target), remove = target.removeEventListener.bind(target);
  target.addEventListener = (name, callback, options) => {
    if (!listeners.has(name)) listeners.set(name, new Set());
    listeners.get(name).add(callback); add(name, callback, options);
  };
  target.removeEventListener = (name, callback, options) => {
    listeners.get(name)?.delete(callback); remove(name, callback, options);
  };
  return () => [...listeners.values()].reduce((total, callbacks) => total + callbacks.size, 0);
}

class NeverOpenDataChannel extends EventTarget {
  constructor() {
    super();
    this.readyState = 'connecting';
    this.ordered = true;
    this.maxRetransmits = null;
    this.maxPacketLifeTime = null;
    this.bufferedAmount = 0;
  }
  send() { throw Error('the closed test channel must not send'); }
  close() {
    if (this.readyState === 'closed') return;
    this.readyState = 'closed';
    this.dispatchEvent(new Event('close'));
  }
}

class FakeRoomPeerConnection extends EventTarget {
  constructor(role) {
    super();
    this.role = role;
    this.iceGatheringState = 'complete';
    this.connectionState = 'new';
    this.iceConnectionState = 'new';
    this.localDescription = null;
    this.remoteDescription = null;
    this.channel = null;
    this.iceWaitListeners = 0;
  }
  addEventListener(name, callback, options) {
    if (name === 'icegatheringstatechange') ++this.iceWaitListeners;
    return super.addEventListener(name, callback, options);
  }
  createDataChannel() { this.channel = new NeverOpenDataChannel(); return this.channel; }
  async createOffer() { return {type: 'offer', sdp: hostSdp}; }
  async createAnswer() { return {type: 'answer', sdp: hostSdp}; }
  async setLocalDescription(description) { this.localDescription = description; }
  async setRemoteDescription(description) { this.remoteDescription = description; }
  close() {
    this.connectionState = 'closed';
    this.iceConnectionState = 'closed';
    this.channel?.close();
  }
}

class ReadyButUnpairedWebSocket extends EventTarget {
  static CONNECTING = 0;
  static OPEN = 1;
  static CLOSING = 2;
  static CLOSED = 3;
  constructor(url) {
    super();
    this.url = url;
    this.readyState = ReadyButUnpairedWebSocket.CONNECTING;
    this.bufferedAmount = 0;
    queueMicrotask(() => {
      if (this.readyState !== ReadyButUnpairedWebSocket.CONNECTING) return;
      this.readyState = ReadyButUnpairedWebSocket.OPEN;
      this.dispatchEvent(new Event('open'));
      const ready = new Event('message');
      Object.defineProperty(ready, 'data', {value: JSON.stringify({relay: 1, event: 'ready'})});
      this.dispatchEvent(ready);
    });
  }
  send() {}
  close(code = 1000, reason = '') {
    if (this.readyState === ReadyButUnpairedWebSocket.CLOSED) return;
    this.readyState = ReadyButUnpairedWebSocket.CLOSED;
    queueMicrotask(() => {
      const event = new Event('close');
      Object.defineProperties(event, {code: {value: code}, reason: {value: reason}});
      this.dispatchEvent(event);
    });
  }
}

function emitMessage(socket, data) {
  const event = new Event('message');
  Object.defineProperty(event, 'data', {value: JSON.stringify(data)});
  socket.dispatchEvent(event);
}

function negotiatingSocket(role, roomId) {
  return class extends ReadyButUnpairedWebSocket {
    constructor(url) {
      super(url);
      if (role === 'beta') queueMicrotask(() => emitMessage(this, {
        protocol: 'melee-local-webrtc', version: 1, roomId, from: 'alpha', to: 'beta',
        kind: 'offer', description: {type: 'offer', sdp: hostSdp},
      }));
    }
    send(text) {
      const signal = JSON.parse(text);
      if (role === 'alpha') queueMicrotask(() => emitMessage(this, {...signal,
        from: 'beta', to: 'alpha', kind: 'answer', description: {type: 'answer', sdp: hostSdp}}));
    }
  };
}

test('actual Room transport waits for asynchronous channel attachment, open and PC/ICE connection', async t => {
  const prior = Object.getOwnPropertyDescriptor(globalThis, 'WebSocket');
  const roomId = 'd'.repeat(32);
  async function setup(role, timeoutMs = 200, duplicateBeforeNegotiation = false) {
    Object.defineProperty(globalThis, 'WebSocket', {configurable: true, writable: true,
      value: negotiatingSocket(role, roomId)});
    const pc = new FakeRoomPeerConnection(role), pcListeners = trackListeners(pc), channels = new Set();
    const channel = () => {
      const value = new NeverOpenDataChannel(); value.send = () => {};
      value.listenerCount = trackListeners(value); channels.add(value);
      return value;
    };
    pc.createDataChannel = () => (pc.channel = channel());
    if (duplicateBeforeNegotiation) pc.setRemoteDescription = async description => {
      pc.remoteDescription = description;
      for (let count = 0; count < 2; ++count) {
        const value = channel(); pc.channel ??= value;
        const event = new Event('datachannel'); Object.defineProperty(event, 'channel', {value}); pc.dispatchEvent(event);
      }
    };
    const transport = createRoomTransport({url: 'ws://127.0.0.1:8787', roomId, role, timeoutMs,
      createPeerConnection: () => pc});
    const endpoint = transport.createEndpoint({onMessage() {}, onDisconnect() {}, onEndpointError() {}});
    const started = transport.start(); started.catch(() => {});
    let settled = false; started.then(() => { settled = true; }, () => { settled = true; });
    for (let attempt = 0; attempt < 20 && transport.signalingSnapshot()?.phase !== 'negotiated'; ++attempt) await turn();
    assert.equal(transport.signalingSnapshot()?.phase, 'negotiated', 'Actual signaling completes before beta datachannel is announced');
    const attach = (value = channel()) => {
      pc.channel ??= value;
      const event = new Event('datachannel'); Object.defineProperty(event, 'channel', {value}); pc.dispatchEvent(event);
      return value;
    };
    const connect = (ice = 'connected') => {
      pc.connectionState = 'connected'; pc.iceConnectionState = ice;
      pc.dispatchEvent(new Event('connectionstatechange')); pc.dispatchEvent(new Event('iceconnectionstatechange'));
    };
    const open = value => { value.readyState = 'open'; value.dispatchEvent(new Event('open')); };
    const close = async () => {
      await endpoint.close().catch(() => {}); await transport.close().catch(() => {});
      assert.equal(pcListeners(), 0, 'Transport close removes every owned PC listener');
      for (const channel of channels) assert.equal(channel.listenerCount(), 0, 'Endpoint and connection waits remove every owned channel listener');
    };
    return {pc, transport, endpoint, started, attach, connect, open, close, channel, settled: () => settled};
  }
  try {
    await t.test('beta datachannel arrives after completed answer signaling', async t => {
      const run = await setup('beta'); t.after(run.close);
      run.connect(); await turn(); assert.equal(run.settled(), false, 'Connected PC/ICE cannot replace missing channel attachment');
      const channel = run.attach(); await turn(); assert.equal(run.settled(), false, 'Attachment alone cannot replace open');
      run.open(channel); const result = await run.started;
      assert.equal(result.local_webrtc.attach_source, 'datachannel'); assert.equal(result.local_webrtc.ready_state, 'open');
      assert.equal(result.local_webrtc.connection_state, 'connected'); assert.equal(result.local_webrtc.ice_connection_state, 'connected');
    });
    await t.test('alpha existing channel still requires PC and ICE connected', async t => {
      const run = await setup('alpha'); t.after(run.close);
      run.open(run.pc.channel); await run.endpoint.ready; await turn(); assert.equal(run.settled(), false);
      run.connect('checking'); await turn(); assert.equal(run.settled(), false, 'Open channel and connected PC still require connected ICE');
      run.connect('completed'); assert.equal((await run.started).local_webrtc.attach_source, 'createDataChannel');
    });
    for (const stage of ['before-attachment', 'before-open', 'during-connection'])
      for (const mode of ['normal', 'fatal']) await t.test(`${mode} close ${stage}`, async t => {
        const run = await setup('beta'); t.after(run.close);
        if (stage !== 'before-attachment') {
          const channel = run.attach();
          if (stage === 'during-connection') run.open(channel);
        }
        const rejected = assert.rejects(run.started, /connection failed|cancelled|closed/);
        if (mode === 'fatal') run.transport.cancelPendingStart();
        await run.transport.close(); await rejected;
        assert.equal(run.pc.connectionState, 'closed');
      });
    for (const fault of ['duplicate', 'channel-error', 'channel-closed', 'pc-failed', 'ice-failed'])
      await t.test(`startup rejects ${fault} and retains first failure`, async t => {
        const run = await setup('beta'); t.after(run.close); const channel = run.attach();
        const rejected = assert.rejects(run.started, fault === 'duplicate' ? /announced more than once/ : /connection failed/);
        if (fault === 'duplicate') {
          const duplicate = run.attach(); assert.equal(duplicate.readyState, 'closed', 'Extra channel is retired');
        } else if (fault === 'channel-error') channel.dispatchEvent(new Event('error'));
        else if (fault === 'channel-closed') channel.close();
        else {
          run.pc[fault === 'pc-failed' ? 'connectionState' : 'iceConnectionState'] = 'failed';
          run.pc.dispatchEvent(new Event(fault === 'pc-failed' ? 'connectionstatechange' : 'iceconnectionstatechange'));
        }
        await rejected;
        channel.dispatchEvent(new Event('error'));
        await assert.rejects(run.started, fault === 'duplicate' ? /announced more than once/ : /connection failed/);
      });
    await t.test('missing attachment expires within existing connection timeout', async t => {
      const run = await setup('beta', 30); t.after(run.close); run.connect();
      await assert.rejects(run.started, /connection timed out/);
    });
    await t.test('duplicate channel announced during offer application preserves its first failure', async t => {
      const run = await setup('beta', 200, true); t.after(run.close);
      await assert.rejects(run.started, /announced more than once/);
    });
    await t.test('late attachment does not renew the original connection deadline', async t => {
      t.mock.timers.enable({apis: ['setTimeout']});
      const run = await setup('beta', 40); t.after(run.close);
      t.mock.timers.tick(25);
      run.attach(); run.connect();
      const expired = assert.rejects(run.started, /connection timed out/);
      t.mock.timers.tick(15); await expired;
    });
  } finally {
    if (prior) Object.defineProperty(globalThis, 'WebSocket', prior); else delete globalThis.WebSocket;
  }
});

function makeProductionUnreadyRoomSession(role, pendingIce = false) {
  const native = adapterFixture();
  let pc;
  const session = createRuntimeLockstepSession({Module: {}, role, sourceTicks: 8, inputTicks: 6,
    url: 'ws://127.0.0.1:8787', roomId: 'c'.repeat(32), timeoutMs: 1000,
    createAdapter: () => native.adapter,
    createTransport: options => createRoomTransport({...options,
      createPeerConnection: () => {
        pc = new FakeRoomPeerConnection(role);
        if (pendingIce) pc.iceGatheringState = 'gathering';
        return pc;
      }})});
  return {session, native, get pc() { return pc; }};
}

test('production Room transport early close joins alpha channel-never-opens and beta channel-never-announced', async t => {
  const prior = Object.getOwnPropertyDescriptor(globalThis, 'WebSocket');
  Object.defineProperty(globalThis, 'WebSocket', {configurable: true, writable: true,
    value: ReadyButUnpairedWebSocket});
  try {
    for (const [role, pendingIce] of [['alpha', false], ['beta', false], ['alpha', true]])
      for (const mode of ['normal', 'fatal']) {
      await t.test(`${role} ${mode}${pendingIce ? ' pending ICE' : ''}`, async () => {
        const run = makeProductionUnreadyRoomSession(role, pendingIce);
        const {session, native} = run;
        const started = session.start(start => ({protocol: 'melee-web-local-lockstep-a2-v1', native_start: start}));
        started.catch(() => {});
        native.state.start.recorded = 1;
        session.onFrame();
        for (let attempt = 0; attempt < 10 && (session.snapshot().peer?.nativePump?.rpc_calls ?? 0) === 0; ++attempt)
          await turn();
        assert.ok(session.snapshot().peer, 'the runtime creates the real peer before readiness blocks');
        assert.ok(session.snapshot().peer.nativePump.rpc_calls > 0,
          'the real peer start RPC is queued behind the deferred WebRTC readiness');
        if (pendingIce) assert.ok(run.pc.iceWaitListeners > 0, 'production signaling entered its ICE wait');
        let timer;
        const outcome = await Promise.race([
          session.close({mode}).then(value => ({value}), error => ({error})),
          new Promise(resolve => { timer = setTimeout(() => resolve({timedOut: true}), 300); }),
        ]);
        clearTimeout(timer);
        assert.equal(outcome.timedOut, undefined,
          'close must cancel deferred endpoint readiness instead of waiting for signaling/datachannel timeout');
        const snapshot = session.snapshot();
        assert.equal(native.events.filter(row => row[0] === 'dispose').length, 1);
        assert.ok(native.events.findIndex(row => row[0] === 'dispose') > native.events.findIndex(row => row[0] === 'terminate') || mode === 'fatal',
          'normal early close reaches native disconnect before scratch retirement');
        if (mode === 'normal') {
          assert.equal(native.state.terminal.kind, 2);
          assert.equal(snapshot.peer.nativePump.native_quiescence, 'verified', JSON.stringify({snapshot,
            errors: outcome.error?.errors?.map(error => String(error?.stack || error))}));
        } else {
          assert.equal(native.state.terminal.kind, 0, 'fatal cleanup does not claim native termination');
          assert.equal(snapshot.peer.nativePump.native_quiescence, 'aborted-fatal');
        }
        await started.catch(() => {});
        if (outcome.error) assert.ok(outcome.error instanceof AggregateError);
      });
    }
  } finally {
    if (prior) Object.defineProperty(globalThis, 'WebSocket', prior);
    else delete globalThis.WebSocket;
  }
});

test('runtime session starts one actual peer from frame-captured native identity and closes once', async () => {
  const run = makeSession();
  const agreement = {protocol: 'melee-web-local-lockstep-a2-v1', seed: 5, native_start: {recorded: 1}};
  const started = run.session.start(start => ({...agreement, native_start: start}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  const snapshot = await started;
  assert.equal(snapshot.protocol.ready, true);
  assert.equal(snapshot.protocol.role, 'alpha');
  assert.deepEqual(run.native.events.find(row => row[0] === 'capture'), ['capture', 0, 6]);
  assert.equal(run.transport.events[0][0], 'transport-start');

  const closing = run.session.close();
  assert.equal(run.session.close(), closing, 'concurrent close joins the single owner operation');
  const result = await closing;
  assert.equal(result.native_quiescence, 'verified');
  assert.equal(run.native.events.filter(row => row[0] === 'dispose').length, 1);
  assert.equal(run.native.events.filter(row => row[0] === 'terminate').length, 1,
    'early owner close uses peer disconnect to establish native terminal state');
  const accesses = run.native.events.length;
  run.session.onFrame(); await turn();
  assert.equal(run.native.events.length, accesses, 'late frame progress cannot reach retired adapter scratch');
});

test('runtime session snapshot observes signaling health after negotiation', async () => {
  let health = {phase: 'negotiated', failure_code: null};
  const run = makeSession({transportFactory(options) {
    const transport = pairedTransport(options);
    const start = transport.start;
    return {...transport, async start() { await start(); return {signaling: {...health}}; },
      signalingSnapshot: () => ({...health})};
  }});
  const started = run.session.start(start => ({native_start: start}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await started;
  assert.equal(run.session.snapshot().transport.signaling.failure_code, null);
  health = {phase: 'failed', failure_code: 'signaling-peer-disconnected'};
  assert.equal(run.session.snapshot().transport.signaling.failure_code, 'signaling-peer-disconnected');
  await run.session.close();
});

test('actual runtime session finalizer preserves successful normal close and aborts partial setup', async t => {
  const source = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('  intentionalRelayClose = true;');
  const end = source.indexOf('  pairResults.finished_at =', start);
  for (const passed of [true, false]) await t.test(passed ? 'normal completion' : 'partial setup failure', async () => {
    const run = makeSession();
    const started = run.session.start(nativeStart => ({native_start: nativeStart}));
    run.native.state.start.recorded = 1;
    run.session.onFrame();
    await started;
    if (passed) {
      run.native.state.cursor = 8;
      run.native.state.blocker = 'complete';
      await run.session.close();
      assert.equal(run.session.snapshot().peer.nativePump.native_quiescence, 'verified');
    }
    const modes = [];
    const instance = {closed: false, errors: [],
      runtimeLockstepSnapshot: async () => run.session.snapshot(),
      closeRuntimeLockstep: async mode => { modes.push(mode); return run.session.close({mode}); },
      async freezePeerModuleIdentity() { return []; }, async finishPeerModuleIdentity() { return []; },
      async timingPauseDiagnostics() { return null; }, async unload() {},
      async close() { this.closed = true; return true; },
      async status() { return null; }, async native() { return null; }};
    const context = vm.createContext({browserOwned: true, runtimeOwned: true, runPassed: passed,
      intentionalRelayClose: false, roomRuntime: null,
      instances: {alpha: instance}, instanceRows: {alpha: {}}, checksumFiles: {}, peers: null,
      relay: passed ? {close: mode => instance.closeRuntimeLockstep(mode)} : null,
      transportCallbackQueue: {async drain() { return []; }}, path, output: 'out', childDirectory: role => role,
      pairResults: {outcome: passed ? 'complete' : 'fail', first_error: null, relay_closed: false},
      closeNotes: [], transportErrors: [], callbackErrors: [], fs: {async writeFile() {}}});
    await vm.runInContext(`(async()=>{${source.slice(start, end)}})()`, context);
    assert.deepEqual(modes, [passed ? 'normal' : 'fatal']);
    const snapshot = run.session.snapshot();
    assert.equal(snapshot.close_mode, passed ? 'normal' : 'fatal');
    assert.equal(snapshot.peer.nativePump.native_quiescence, passed ? 'verified' : 'aborted-fatal');
    assert.equal(run.native.events.filter(row => row[0] === 'dispose').length, 1);
    assert.equal(context.instanceRows.alpha.browser_closed, true);
    if (!passed) assert.equal(context.pairResults.runtime_partial_cleanup.alpha.after.close_mode, 'fatal');
  });
});

test('runtime session remote disconnect reaches the native terminal before early close', async () => {
  const run = makeSession();
  const started = run.session.start(start => ({protocol: 'melee-web-local-lockstep-a2-v1', native_start: start}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await started;
  await run.transport.disconnect('remote closed before completion');
  assert.equal(run.native.state.terminal.kind, 2);
  const result = await run.session.close();
  assert.equal(result.native_quiescence, 'verified');
  assert.equal(run.native.events.filter(row => row[0] === 'terminate').length, 1,
    'the already terminal peer is not terminalized a second time');
});

test('runtime session retains the page checksum consumer snapshot within the existing 512-record bound', async () => {
  const record = Array.from({length: 64}, (_, index) => index & 0xff);
  let consume;
  const run = makeSession({peerFactory(options) {
    consume = options.checksumConsumer;
    return {waitForReady: async () => {}, rpc: async () => {
      assert.equal(consume([record]), true);
      return {protocol: {ready: true}};
    }, close: async () => {}, snapshot: () => ({protocol: {ready: true}, nativePump: {native_quiescence: 'verified'}})};
  }});
  const started = run.session.start(() => ({protocol: 'melee-web-local-lockstep-a2-v1'}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await started;
  assert.deepEqual(run.session.snapshot().checksums, [record]);
  await run.session.close();
});

test('runtime session startup failure still closes allocated transport and native scratch', async () => {
  const transportEvents = [];
  const run = makeSession({peerFactory() { throw Error('peer construction failed'); },
    transportFactory() { return {createEndpoint() { assert.fail('peer construction must fail before endpoint allocation'); },
      async start() { transportEvents.push('start'); }, async close() { transportEvents.push('close'); }}; }});
  const started = run.session.start(() => ({protocol: 'melee-web-local-lockstep-a2-v1'}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await assert.rejects(started, /peer construction failed/);
  await assert.rejects(run.session.close({mode: 'fatal'}), error =>
    error instanceof AggregateError && error.errors.some(cause => /peer construction failed/.test(cause.message)));
  assert.deepEqual(transportEvents, ['close']);
  assert.equal(run.native.events.filter(row => row[0] === 'dispose').length, 1);
});

test('fatal escalation joins normal close and preserves aborted native-quiescence status', async () => {
  const join = deferred(), modes = [], events = [], native = adapterFixture();
  let transport;
  const run = makeSession({adapterFixture: native, transportFactory() {
    transport = {createEndpoint() {}, async start() {}, async close() { events.push('transport-close'); }};
    return transport;
  }, peerFactory() {
    let mode = 'normal';
    return {waitForReady: async () => {}, rpc: async () => ({protocol: {ready: true}}),
      close(options) { modes.push(options.mode); if (options.mode === 'fatal') mode = 'fatal'; return join.promise; },
      snapshot: () => ({protocol: {ready: true}, nativePump: {native_quiescence: mode === 'fatal' ? 'aborted-fatal' : 'pending'}})};
  }});
  const started = run.session.start(() => ({protocol: 'melee-web-local-lockstep-a2-v1'}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await started;
  const normal = run.session.close({mode: 'normal'});
  const fatal = run.session.close({mode: 'fatal'});
  assert.equal(fatal, normal, 'fatal escalation joins the existing close operation');
  assert.deepEqual(modes, ['normal', 'fatal']);
  assert.equal(run.native.events.some(row => row[0] === 'dispose'), false,
    'adapter scratch stays allocated until queued peer work joins');
  join.resolve();
  const result = await normal;
  assert.equal(result.native_quiescence, 'aborted-fatal',
    'fatal close never reports successful native quiescence');
  assert.ok(events.includes('transport-close'), 'transport cleanup follows peer join');
  assert.equal(run.native.events.filter(row => row[0] === 'dispose').length, 1);
});

test('peer join failure still closes endpoint transport and disposes adapter scratch', async () => {
  const joinFailure = Error('queued native operation failed'), native = adapterFixture(), events = [];
  let transport;
  const run = makeSession({adapterFixture: native, transportFactory() {
    transport = {createEndpoint() {}, async start() {}, async close() { events.push('transport-close'); }};
    return transport;
  }, peerFactory() {
    return {waitForReady: async () => {}, rpc: async () => ({protocol: {ready: true}}), close: async () => { events.push('peer-close'); throw joinFailure; },
      snapshot: () => ({protocol: {ready: true}, nativePump: {native_quiescence: 'failed'}})};
  }});
  const started = run.session.start(() => ({protocol: 'melee-web-local-lockstep-a2-v1'}));
  run.native.state.start.recorded = 1;
  run.session.onFrame();
  await started;
  await assert.rejects(run.session.close({mode: 'fatal'}), error =>
    error instanceof AggregateError && error.errors.some(cause => cause === joinFailure));
  assert.deepEqual(events, ['peer-close', 'transport-close']);
  assert.equal(native.events.filter(row => row[0] === 'dispose').length, 1);
  assert.equal(run.session.snapshot().closing, true);
});

test('runtime session rejects duplicate starts and incompatible contexts before adapter creation', async () => {
  let allocations = 0;
  const options = {Module: {}, role: 'alpha', sourceTicks: 8, inputTicks: 6,
    url: 'ws://127.0.0.1:8787', roomId: 'b'.repeat(32), createAdapter() { ++allocations; return {}; }};
  assert.throws(() => createRuntimeLockstepSession({...options, inputTicks: 5}), /tick bounds are incompatible/);
  assert.throws(() => createRuntimeLockstepSession({...options, role: 'spectator'}), /role must be alpha or beta/);
  assert.equal(allocations, 0);
  const run = makeSession();
  const first = run.session.start(() => ({protocol: 'melee-web-local-lockstep-a2-v1'}));
  await assert.rejects(run.session.start(() => ({})), /already started/);
  await run.session.close({mode: 'fatal'});
  await assert.rejects(first, /closed before native start identity/);
});


test('actual runtime, native ABI and Room transport await a delayed remote identity hello', async t => {
  for (const delayedConfirmation of [false, true]) await t.test(delayedConfirmation ? 'asynchronous native confirmation' : 'delayed hello', async () => {
  const confirmationGate = deferred();
  const prior = Object.getOwnPropertyDescriptor(globalThis, 'WebSocket');
  const roomId = 'e'.repeat(32), events = [], sent = [];
  Object.defineProperty(globalThis, 'WebSocket', {configurable: true, writable: true,
    value: negotiatingSocket('alpha', roomId)});
  const pc = new FakeRoomPeerConnection('alpha'), channel = new NeverOpenDataChannel();
  pc.createDataChannel = () => channel;
  channel.send = text => { sent.push(JSON.parse(text)); events.push('send-' + JSON.parse(text).type); };
  const state = {active: 1, cursor: 0, ring_pending: 0, blocker: 'start_identity',
    terminal: {kind: 0, tick: 0, channel: 0}, start: {recorded: 1, required: 1, confirmed: 0,
      capture_failed: 0, scene: 1, seed: 305419896, frame: 0, total: 'a8f1925aa316f1e8'}};
  const heap = new Uint8Array(131072);
  const Module = {HEAPU8: heap, _malloc: () => 8192, _free: () => events.push('free'),
    _melee_web_net_push: () => 1, _melee_web_net_push_indexed: () => 1,
    _melee_web_net_enable_local_input_capture: () => 1,
    _melee_web_net_confirm_start: () => { events.push('native-confirm'); state.start.confirmed = 1; return 1; },
    _melee_web_net_terminate: (kind, tick, channel) => { state.terminal = {kind, tick, channel}; state.blocker = 'terminal'; },
    _melee_web_net_checksum_drain: () => 0,
    _melee_web_net_status: () => { const bytes = new TextEncoder().encode(JSON.stringify(state)); heap.set(bytes, 8); heap[8 + bytes.length] = 0; return 8; },
    UTF8ToString: (pointer, length) => new TextDecoder().decode(heap.subarray(pointer, pointer + length))};
  const session = createRuntimeLockstepSession({Module, role: 'alpha', sourceTicks: 8, inputTicks: 6,
    url: 'ws://127.0.0.1:8787', roomId, timeoutMs: 250,
    createTransport: options => createRoomTransport({...options, createPeerConnection: () => pc}),
    ...(delayedConfirmation ? {createPeer(options, dependencies) {
      const native = options.native;
      return createBrowserNativePeer({...options, native: {...native, async confirmStart() {
        events.push('enter-native-confirm'); await confirmationGate.promise; return native.confirmStart();
      }}}, dependencies);
    }} : {})});
  const started = session.start(nativeStart => ({protocol: 'melee-web-local-lockstep-a2-v1', native_start: nativeStart}));
  const observed = started.then(value => ({value}), error => ({error}));
  let settled = false; observed.then(() => { settled = true; });
  try {
    session.onFrame();
    for (let count = 0; count < 10; ++count) await turn();
    channel.readyState = 'open'; channel.dispatchEvent(new Event('open'));
    pc.connectionState = 'connected'; pc.iceConnectionState = 'connected';
    pc.dispatchEvent(new Event('connectionstatechange')); pc.dispatchEvent(new Event('iceconnectionstatechange'));
    for (let count = 0; count < 20 && !sent.some(x => x.type === 'hello'); ++count) await turn();
    assert.ok(sent.some(x => x.type === 'hello'), 'Actual endpoint sends local hello');
    await turn();
    assert.equal(state.start.confirmed, 0); assert.equal(session.snapshot().peer.protocol.ready, false);
    assert.equal(settled, false, 'Connected transport and local hello do not imply remote agreement');
    const local = sent.find(x => x.type === 'hello');
    const event = new Event('message'); Object.defineProperty(event, 'data', {value: JSON.stringify({...local,
      role: 'beta', local_port: 1, remote_port: 0})}); events.push('receive-hello'); channel.dispatchEvent(event);
    if (delayedConfirmation) {
      for (let count = 0; count < 20 && !events.includes('enter-native-confirm'); ++count) await turn();
      assert.ok(events.includes('enter-native-confirm'));
      assert.equal(session.snapshot().peer.protocol.ready, true);
      assert.equal(state.start.confirmed, 0); assert.equal(settled, false);
      confirmationGate.resolve();
    }
    const result = await observed; assert.equal(result.error, undefined, String(result.error));
    assert.equal(result.value.protocol.ready, true); assert.equal(state.start.confirmed, 1);
    assert.ok(events.indexOf('receive-hello') < events.indexOf('native-confirm'));
    assert.equal(state.cursor, 0, 'Readiness does not advance native source ticks');
  } finally {
    confirmationGate.resolve();
    await session.close({mode: 'fatal'}).catch(() => {}); await observed;
    if (prior) Object.defineProperty(globalThis, 'WebSocket', prior); else delete globalThis.WebSocket;
  }
  });
});


test('actual harness reconstructs the runtime hello lifecycle without changing native identity', async () => {
  const harness = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const owner = await fs.readFile(new URL('../web/net_lockstep_development_owner.mjs', import.meta.url), 'utf8');
  const source = harness.slice(harness.indexOf('function createAgreement('), harness.indexOf('async function publishAllInputs'));
  const context = {seed: 305419896, sourceTicks: 8, usedInputs: 6, LOCKSTEP_DELAY: 2,
    inputSampling: false, runtimeOwned: true};
  vm.createContext(context); vm.runInContext(source, context);
  const identity = {wasm: 'a'.repeat(64), disc: {algorithm: 'sha256', dol: 'b'.repeat(64),
    fst: 'c'.repeat(64), dolSize: 4425184, fstSize: 29993}};
  const before = {required: 1, recorded: 1, scene: 1, seed: 305419896, frame: 0, confirmed: 0,
    capture_failed: 0, card: 'f962dd478f0c88aa', pad_history: 'af900bb4021c8716',
    native_context: '53144b77c1cb4fbc', total: 'a8f1925aa316f1e8', pad: '419eb4f4f4bbee75',
    scene_state: '3391b05bd2e60036', object_state: 'd0e868a4883380b5', objects: 33, flags: 10};
  const after = {...before, confirmed: 1};
  let actualRuntimeBuilder;
  const capture = owner.slice(owner.indexOf('      const ready = ownedSession.start(nativeStart'), owner.indexOf('      ready.catch'));
  vm.runInNewContext(capture, {seed: context.seed, sourceTicks: 8, inputDelay: 2, identity,
    ownedSession: {start(builder) { actualRuntimeBuilder = builder; return Promise.resolve(); }}});
  const actual = actualRuntimeBuilder(before), expected = context.createAgreement(identity, after);
  assert.equal(JSON.stringify(expected), JSON.stringify(actual), 'Actual observer reconstructs every field of actual runtime hello');
  assert.equal(after.confirmed, 1, 'Reconstruction never mutates observed native status');
  const hash = value => createHash('sha256').update(JSON.stringify(value)).digest('hex');
  const actualPeer = new LockstepPeer({role: 'alpha', sourceTicks: 8, inputTicks: 6, pushFrame: async () => true});
  const sent = []; actualPeer.attach(async text => sent.push(JSON.parse(text))); await actualPeer.start(actual);
  assert.equal(hash(expected), actualPeer.summary().start_identity_hash);
  assert.equal(sent[0].agreement.native_start.confirmed, 0);
  for (const key of Object.keys(after).filter(key => key !== 'confirmed')) {
    const changed = {...after, [key]: typeof after[key] === 'number' ? after[key] + 1 : after[key] + 'x'};
    assert.notEqual(hash(context.createAgreement(identity, changed)), actualPeer.summary().start_identity_hash,
      `Native identity mutation ${key} remains a hash mismatch`);
  }
  for (const key of Object.keys(identity.disc)) {
    const changed = {...identity, disc: {...identity.disc, [key]: typeof identity.disc[key] === 'number'
      ? identity.disc[key] + 1 : identity.disc[key] + 'x'}};
    assert.notEqual(hash(context.createAgreement(changed, after)), hash(actual), `Disc identity mutation ${key} remains a mismatch`);
  }
  assert.notEqual(hash(context.createAgreement({...identity, wasm: 'd'.repeat(64)}, after)), hash(actual));
  for (const confirmed of [undefined, null, 0, 2, true, '1'])
    assert.throws(() => context.createAgreement(identity, {...after, confirmed}), /requires a confirmed native start/);
  context.runtimeOwned = false; context.inputSampling = true;
  for (const start of [before, after])
    assert.equal(context.createAgreement(identity, start).native_start, start, 'Nonruntime observer retains its unchanged native start object');
  assert.match(harness, /alpha: createAgreement\(identities\[0\], startRows\[0\]\)/);
  assert.match(harness, /beta: createAgreement\(identities\[1\], startRows\[1\]\)/);
  assert.match(harness, /peers\[role\]\.agreementHash !== expectedAgreementHashes\[role\]/,
    'Actual callsite still requires exact agreement hashes for both runtime peers');
});


test('actual autonomous harness observes each checksum ownership mode and freezes failed intervals', async () => {
  const harness = await fs.readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const source = harness.slice(harness.indexOf('function nativePumpChecksumEvidence('), harness.indexOf('async function pollRun()'));
  const {verifyPositivePeerCompletion} = await import('../scripts/net_lockstep_observers.mjs');
  const rows = cursor => {
    const records = Array.from({length: cursor}, (_, tick) => { const b = Array(64).fill(0); b[0] = tick; return b; });
    return {status: {active: 1, cursor, blocker: cursor === 8 ? 'complete' : 'network_wait', terminal: {kind: 0}}, native: {},
      runtime_checksum_records: records, snapshot: {failure: null, endpointErrors: [], nativePump: {enabled: true, rpc_calls: 1},
        exportRecords: 0, checksumConsumer: {enabled: true, accepted_records: cursor, retained_records: 0, pending_batch: null},
        checksumOwnership: {consumer_accepted_records: cursor}, protocol: {terminal: null, checksum_mismatches: [],
          local_checksum_ticks: cursor, remote_checksum_ticks: cursor, next_checksum_compare: cursor, remote_ack_input: 5}}};
  };
  const run = async (runtimeOwned, mutate = () => {}, initial = 0, mutateOwner = value => value) => {
    let iteration = 0, time = 0;
    const before = rows(initial), after = rows(8); mutate(after);
    if (!runtimeOwned) { before.snapshot.exportRecords = initial; after.snapshot.exportRecords = 8; }
    const context = {runtimeOwned, sourceTicks: 8, usedInputs: 6, NET_RECORD_BYTES: 64, deadline: 4, stallMs: 4, pollMs: 1,
      Date: {now: () => time}, sleep: async () => { ++time; }, pairResults: {}, verifyPositivePeerCompletion,
      checkedHealth: async () => { const row = iteration < 2 ? before : after; return {status: row.status, native: row.native}; },
      instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {
        runtimeLockstepSnapshot: async () => { const row = iteration++ < 2 ? before : after; return mutateOwner({armed: true, closing: false, failure: null, peer: row.snapshot, checksums: row.runtime_checksum_records}); },
        readPeerSnapshot: async () => (iteration++ < 2 ? before : after).snapshot,
      }]))};
    vm.createContext(context); vm.runInContext(source, context);
    let error; try { await context.observeNativePumpWithoutRpc(); } catch (value) { error = value; }
    return {context, error, after};
  };
  for (const mode of [true, false]) {
    const valid = await run(mode); assert.equal(valid.error, undefined); assert.equal(valid.context.pairResults.native_pump_interval.complete, true);
    const rpc = await run(mode, row => { row.snapshot.nativePump.rpc_calls++; });
    assert.match(String(rpc.error), /peer RPC occurred/); assert.equal(rpc.context.pairResults.native_pump_interval.no_peer_RPC_during_interval, false);
    assert.equal(rpc.context.pairResults.native_pump_interval.after.alpha.status.cursor, 8);
    const early = await run(mode, () => {}, 8); assert.match(String(early.error), /completed before/);
    assert.equal(early.context.pairResults.native_pump_interval.complete, false);
    const stuck = await run(mode, row => { row.status.cursor = 0; row.status.blocker = 'network_wait'; });
    assert.match(String(stuck.error), /did not complete/); assert.equal(stuck.context.pairResults.native_pump_interval.actual_native_cursor_progress_each, false);
    for (const key of ['remote_checksum_ticks', 'next_checksum_compare', 'remote_ack_input']) {
      const rejected = await run(mode, row => { row.snapshot.protocol[key]--; }); assert.match(String(rejected.error), /did not complete/);
    }
  }
  for (const mutate of [
    row => { delete row.runtime_checksum_records; },
    row => { row.runtime_checksum_records.pop(); },
    row => { row.runtime_checksum_records[7] = row.runtime_checksum_records[6]; },
    row => { row.runtime_checksum_records[7][10] = 256; },
    row => { delete row.runtime_checksum_records[7][10]; },
    row => { row.runtime_checksum_records[7].pop(); },
    row => { row.runtime_checksum_records.push(Array(64).fill(0)); },
    row => { row.snapshot.checksumConsumer.accepted_records = 7; row.snapshot.checksumOwnership.consumer_accepted_records = 7; },
  ]) { const rejected = await run(true, mutate); assert.match(String(rejected.error), /Invalid runtime|not delivered/); }
  for (const mutate of [row => { row.snapshot.exportRecords = 1; }, row => { row.snapshot.checksumConsumer.retained_records = 1; },
    row => { row.snapshot.checksumConsumer.pending_batch = {count: 1}; }]) {
    const pending = await run(true, mutate); assert.match(String(pending.error), /did not complete/);
  }
  for (const mutateOwner of [() => null, owner => ({...owner, armed: false}), owner => ({...owner, closing: true}),
    owner => ({...owner, failure: 'retained owner first error'}), owner => ({...owner, peer: null})]) {
    const rejected = await run(true, () => {}, 0, mutateOwner); assert.match(String(rejected.error), /autonomous runtime owner failed/);
  }
  const diagnostic = await run(false);
  for (const count of [undefined, null, 0, 7, 9, '8'])
    assert.equal(diagnostic.context.nativePumpChecksumEvidence({snapshot: {exportRecords: count}}, false, 8), false);
  assert.equal(diagnostic.context.nativePumpChecksumEvidence({snapshot: {exportRecords: 8},
    runtime_checksum_records: ['irrelevant diagnostic owner shape']}, false, 8), true);
  const pending = await run(true, row => {
    row.snapshot.checksumConsumer.accepted_records = 7; row.snapshot.checksumOwnership.consumer_accepted_records = 7;
    row.snapshot.checksumConsumer.pending_batch = {count: 1};
  }); assert.match(String(pending.error), /did not complete/); // Legitimate delivery interval is incomplete, not malformed.
});
