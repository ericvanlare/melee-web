import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {closePageNativeNetworkOwnership, firstFatalBrowserError, openNetInstance, PAGE_HELPERS, installRuntimeInputFixtureInPage} from '../scripts/net_session_instance.mjs';
import {createBrowserNativePeer} from '../scripts/net_lockstep_browser_peer.mjs';
import {createNetLockstepNativeAdapter} from '../scripts/net_lockstep_native_adapter.mjs';
import {LOCKSTEP_DELAY} from '../scripts/net_lockstep_core.mjs';
import {installNetSourceAccounting, readNetSourceAccounting} from '../scripts/net_source_accounting.mjs';
import {buildRuntimeCssSssGamepadSamples, RUNTIME_CSS_SSS_INPUT_TICKS} from '../scripts/net_lockstep_browser_modes.mjs';
import {createControllerManager} from '../web/controller-input.mjs';
import {standardPad} from './controller-fixtures.mjs';

function fakeChrome(goto, evaluate = async () => undefined) {
  let closeCalls = 0;
  let removeCalls = 0;
  const cdpListeners = new Map(), cdpCalls = [];
  let cdpDetachCalls = 0, cdpSessions = 0;
  const cdp = {
    on(name, listener) { assert(!cdpListeners.has(name)); cdpListeners.set(name, listener); },
    off(name, listener) { assert.equal(cdpListeners.get(name), listener); cdpListeners.delete(name); },
    async send(name, params) {
      cdpCalls.push({name, params});
      assert.equal(name, 'Network.enable', 'Startup failure fixture must not fabricate a loaded Wasm body');
      assert.deepEqual(params, {maxResourceBufferSize: 64 * 1024 * 1024, maxTotalBufferSize: 128 * 1024 * 1024});
      return {};
    },
    async detach() { ++cdpDetachCalls; assert.equal(cdpListeners.size, 0); },
  };
  const listeners = new Map();
  const emit = (kind, value) => { for (const listener of listeners.get(kind) || []) listener(value); };
  const page = {
    on(kind, listener) {
      const entries = listeners.get(kind) || [];
      entries.push(listener);
      listeners.set(kind, entries);
    },
    off() { ++removeCalls; },
    async addInitScript() {},
    evaluate: (...args) => evaluate(...args),
    async waitForFunction() {
      return {jsonValue: async () => ({ready: true}), dispose: async () => {}};
    },
    setDefaultTimeout() {},
    setDefaultNavigationTimeout() {},
    goto: (...args) => {
      assert.equal(cdpSessions, 1, 'Wasm CDP observer must attach before navigation');
      assert.equal(cdpCalls[0]?.name, 'Network.enable');
      assert.equal(cdpListeners.size, 4, 'All loaded-Wasm lifecycle events must be observed');
      return goto(emit, ...args);
    },
  };
  const context = {
    pages: () => [page],
    browser: () => null,
    async newCDPSession(target) { assert.equal(target, page); ++cdpSessions; return cdp; },
    async close() { ++closeCalls; },
  };
  return {
    chromium: {async launchPersistentContext() { return context; }},
    context,
    get cdpDetachCalls() { return cdpDetachCalls; },
    get cdpSessions() { return cdpSessions; },
    get cdpCalls() { return cdpCalls; },
    get closeCalls() { return closeCalls; },
    get removeCalls() { return removeCalls; },
  };
}

async function withProfile(run) {
  const profile = await mkdtemp(path.join(os.tmpdir(), 'melee-net-instance-'));
  try { await run(profile); }
  finally { await rm(profile, {recursive: true, force: true}); }
}

const common = {
  launchOptions: {args: []},
  url: 'http://127.0.0.1:18941/runtime.html',
  disc: '/unused/owned-disc.ciso',
  label: 'failure-test',
  timeoutMs: 1000,
};

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

test('retains request failures as diagnostics while keeping HTTP and page errors fatal', () => {
  const abortedData = {kind: 'requestfailed', method: 'GET', url: '/gameplay_menu_browser.data', failure: 'net::ERR_ABORTED'};
  assert.equal(firstFatalBrowserError([abortedData]), null);
  assert.equal(firstFatalBrowserError([abortedData, {kind: 'http', status: 404}]).kind, 'http');
  assert.equal(firstFatalBrowserError([abortedData, {kind: 'pageerror', message: 'module failed'}]).kind, 'pageerror');
});

test('closes Chrome and removes driver listeners when runtime navigation fails', async () => {
  await withProfile(async profile => {
    const chrome = fakeChrome(async () => { throw Error('net::ERR_CONNECTION_REFUSED'); });
    let failure;
    try { await openNetInstance({...common, chromium: chrome.chromium, userDataDir: profile}); }
    catch (error) { failure = error; }
    assert.match(failure?.message ?? '', /ERR_CONNECTION_REFUSED/);
    assert.equal(failure.browserClosed, true);
    assert.equal(chrome.closeCalls, 1);
    assert.equal(chrome.cdpSessions, 1);
    assert.equal(chrome.cdpDetachCalls, 1, 'Wasm observer session closes on startup failure');
    assert.deepEqual(chrome.cdpCalls.map(row => row.name), ['Network.enable']);
    assert.equal(chrome.removeCalls, 2);
  });
});

test('preserves HTTP, request and page errors when runtime startup fails before returning an instance', async () => {
  await withProfile(async profile => {
    const chrome = fakeChrome(async (emit) => {
      emit('response', {status: () => 404, url: () => 'http://127.0.0.1/net-timing-pause.mjs'});
      emit('requestfailed', {method: () => 'GET', url: () => 'http://127.0.0.1/net-timing-pause.mjs',
        failure: () => ({errorText: 'net::ERR_ABORTED'})});
      emit('pageerror', Error('Failed to load net-timing-pause.mjs'));
      const error = Error('startup did not reach runtime');
      error.diagnostics = {errors: [{kind: 'http', message: 'HTTP 404 module request'}]};
      throw error;
    });
    let failure;
    try { await openNetInstance({...common, chromium: chrome.chromium, userDataDir: profile}); }
    catch (error) { failure = error; }
    assert.equal(failure.browserClosed, true);
    assert.deepEqual(failure.browserErrors.map(row => row.kind), ['http', 'requestfailed', 'pageerror']);
    assert.equal(failure.browserErrors[0].status, 404);
    assert.equal(failure.browserErrors[1].failure, 'net::ERR_ABORTED');
    assert.match(failure.startupDiagnostics.errors[0].message, /404/);
    assert.equal(chrome.closeCalls, 1);
    assert.equal(chrome.cdpSessions, 1);
    assert.equal(chrome.cdpDetachCalls, 1, 'Wasm observer session closes on startup failure');
    assert.deepEqual(chrome.cdpCalls.map(row => row.name), ['Network.enable']);
  });
});

test('bounds a pending navigation by the session deadline and closes Chrome', async () => {
  await withProfile(async profile => {
    const chrome = fakeChrome(() => new Promise(() => {}));
    let failure;
    try {
      await openNetInstance({...common, chromium: chrome.chromium, userDataDir: profile,
        deadline: Date.now() + 40});
    } catch (error) { failure = error; }
    assert.match(failure?.message ?? '', /wall-time bound exhausted/);
    assert.equal(failure.browserClosed, true);
    assert.equal(chrome.closeCalls, 1);
    assert.equal(chrome.cdpSessions, 1);
    assert.equal(chrome.cdpDetachCalls, 1, 'Wasm observer session closes on startup failure');
    assert.deepEqual(chrome.cdpCalls.map(row => row.name), ['Network.enable']);
  });
});

test('startup failure without a peer disposes the imported adapter and exposes cleanup failure', async () => {
  await withProfile(async profile => {
    let adapterImported = false, adapterDisposed = false;
    const disposeFailure = Error('native adapter disposal failed');
    const chrome = fakeChrome(async () => ({status: () => 200, headers: () => ({
      'cross-origin-opener-policy': 'same-origin',
      'cross-origin-embedder-policy': 'require-corp',
    })}), async fn => {
      const source = String(fn);
      if (source.includes("import('./net_lockstep_native_adapter.mjs')")) {
        adapterImported = true;
        return undefined;
      }
      if (source.includes('crossOriginIsolated')) return true;
      if (source.includes('navigator.userAgent')) throw Error('injected startup failure after page helpers');
      if (source.includes('__meleeWebNetNativeAdapter?.dispose()')) {
        assert.equal(adapterImported, true, 'the staged adapter is initialized before runtime startup continues');
        adapterDisposed = true;
        throw disposeFailure;
      }
      return undefined;
    });
    let failure;
    try { await openNetInstance({...common, chromium: chrome.chromium, userDataDir: profile}); }
    catch (error) { failure = error; }
    assert.match(failure?.message ?? '', /injected startup failure after page helpers/);
    assert.equal(failure.browserClosed, true);
    assert.equal(adapterDisposed, true, 'no-peer startup failure still runs adapter cleanup');
    assert.equal(failure.cleanupError instanceof AggregateError, true);
    assert(failure.cleanupError.errors.includes(disposeFailure));
    assert.equal(chrome.closeCalls, 1);
  });
});

test('peer close releases progress ownership while the instance keeps native status for post-close accounting', async () => {
  const saved = new Map(['window', 'Module'].map(name => [name, Object.getOwnPropertyDescriptor(globalThis, name)]));
  const frees = [];
  let heap = new Uint8Array(128), allocation = 8;
  const status = {active: 1, cursor: 0, blocker: 'start_identity'};
  const Module = {
    get HEAPU8() { return heap; },
    _malloc(size) {
      const pointer = allocation;
      allocation += size + 8;
      if (allocation > heap.length) {
        const grown = new Uint8Array(allocation);
        grown.set(heap);
        heap = grown;
      }
      return pointer;
    },
    _free(pointer) { frees.push(pointer); },
    _melee_web_net_push() { return 1; },
    _melee_web_net_push_indexed() { return 1; },
    _melee_web_net_enable_local_input_capture() { return 1; },
    _melee_web_net_confirm_start() { return 1; },
    _melee_web_net_terminate() {},
    _melee_web_net_checksum_drain() { return 0; },
    _melee_web_net_status() { return 4; },
    UTF8ToString() { return JSON.stringify(status); },
  };
  const window = {menuRuntimeTiming() {}};
  const page = {evaluate(fn, argument) { return fn(argument); }};
  const restoreGlobal = (name, descriptor) => {
    if (descriptor) Object.defineProperty(globalThis, name, descriptor);
    else delete globalThis[name];
  };
  try {
    globalThis.window = window;
    globalThis.Module = Module;
    await PAGE_HELPERS(async () => createNetLockstepNativeAdapter);
    await installNetSourceAccounting(page);
    assert.equal(Object.hasOwn(window.__meleeWebNetNativePeerApi(), 'dispose'), false,
      'the peer borrows adapter lifetime from its enclosing instance');

    const endpoint = {ready: Promise.resolve(), errors: [], closed: false,
      transport: {type: 'test'}, async send() {}, async drainInbound() {},
      async close() { this.closed = true; }};
    const peer = createBrowserNativePeer({role: 'alpha', sourceTicks: LOCKSTEP_DELAY,
      inputTicks: 0, relayUrl: 'ws://example.test', roomId: 'a'.repeat(32), timeoutMs: 500,
      agreement: {build: 'same'}, native: window.__meleeWebNetNativePeerApi(), autonomousPump: true},
    {createEndpoint: () => endpoint});
    window.__netPeer = peer;

    status.cursor = LOCKSTEP_DELAY;
    status.blocker = 'complete';
    status.terminal = {kind: 0};
    status.ring_pending = 0;
    await peer.close();

    const accounting = await readNetSourceAccounting(page, {freeze: true});
    assert.equal(accounting.frozen, true, 'peer close released its source-progress subscription');
    assert.equal(accounting.final.cursor, LOCKSTEP_DELAY);
    assert.equal(window.__net.status().blocker, 'complete', 'the borrowed adapter remains usable after peer close');

    await closePageNativeNetworkOwnership();
    assert.equal(frees.length, 1, 'instance cleanup frees the shared adapter scratch once');
    assert.throws(() => window.__net.status(), /adapter is disposed/);
    await closePageNativeNetworkOwnership();
    assert.equal(frees.length, 1, 'repeated instance cleanup keeps adapter disposal idempotent');
  } finally {
    restoreGlobal('window', saved.get('window'));
    restoreGlobal('Module', saved.get('Module'));
  }
});

test('page cleanup joins the actual runtime owner and retains its close failures', async () => {
  const prior = Object.getOwnPropertyDescriptor(globalThis, 'window');
  const failure = Error('runtime owner cleanup failed');
  let modeSeen = null, legacyClose = false, legacyDispose = false;
  const state = {closing: false, close_mode: null, peer: {nativePump: {native_quiescence: 'aborted-fatal'}}};
  globalThis.window = {
    meleeNetRuntimeLockstepSnapshot: () => state,
    async meleeNetCloseRuntimeLockstep(mode) { modeSeen = mode; throw failure; },
    __netPeer: {async close() { legacyClose = true; }},
    __meleeWebNetNativeAdapter: {dispose() { legacyDispose = true; }},
  };
  try {
    const cleanup = await closePageNativeNetworkOwnership();
    assert.equal(modeSeen, 'fatal');
    assert.equal(legacyClose, false, 'runtime mode must not fall back to the stale peer facade');
    assert.equal(legacyDispose, false, 'runtime session remains the sole scratch owner');
    assert.match(cleanup.runtime_lockstep.error, /runtime owner cleanup failed/);
    assert.deepEqual(cleanup.errors, [String(failure.stack || failure.message)]);

    state.closing = true;
    state.close_mode = 'normal';
    globalThis.window.meleeNetCloseRuntimeLockstep = async mode => { modeSeen = mode; return {closed: true}; };
    const repeated = await closePageNativeNetworkOwnership();
    assert.equal(modeSeen, 'normal', 'completed normal close does not get escalated during browser cleanup');
    assert.deepEqual(repeated.errors, []);
  } finally {
    if (prior) Object.defineProperty(globalThis, 'window', prior);
    else delete globalThis.window;
  }
});

test('passes bounded room-signaling options through the page invocation', async () => {
  await withProfile(async profile => {
    const cleanupStarted = deferred(), allowCleanup = deferred();
    const listeners = new Map();
    const cdp = {
      on(name, listener) { listeners.set(name, listener); },
      off(name, listener) { assert.equal(listeners.get(name), listener); listeners.delete(name); },
      async send(name) { assert.equal(name, 'Network.enable'); return {}; },
      async detach() { assert.equal(listeners.size, 0); },
    };
    let invocation;
    const page = {
      on() {}, off() {},
      setDefaultTimeout() {}, setDefaultNavigationTimeout() {},
      async addInitScript() {},
      async goto() { return {status: () => 200, headers: () => ({
        'cross-origin-opener-policy': 'same-origin',
        'cross-origin-embedder-policy': 'require-corp',
      })}; },
      async evaluate(fn, argument) {
        if (String(fn).includes('__meleeWebNetNativeAdapter?.dispose()')) {
          cleanupStarted.resolve();
          await allowCleanup.promise;
          return undefined;
        }
        if (argument && typeof argument === 'object' && 'roomId' in argument) {
          invocation = {argument, source: String(fn)};
          return {peer: {ready: true}};
        }
        if (String(fn).includes('Room WebRTC signaling is unavailable')) {
          const hadWindow = Object.hasOwn(globalThis, 'window');
          const oldWindow = globalThis.window;
          globalThis.window = {};
          try { return fn(); }
          finally {
            if (hadWindow) globalThis.window = oldWindow;
            else delete globalThis.window;
          }
        }
        if (String(fn).includes('crossOriginIsolated')) return true;
        return undefined;
      },
      async waitForFunction() {
        return {jsonValue: async () => ({ready: true}), dispose: async () => {}};
      },
    };
    const context = {
      pages: () => [page], browser: () => ({version: () => 'fake-chrome'}),
      async newCDPSession(target) { assert.equal(target, page); return cdp; },
      async close() {},
    };
    const chromium = {async launchPersistentContext() { return context; }};
    const instance = await openNetInstance({...common, chromium, userDataDir: profile});
    try {
      const result = await instance.startRoomSignaledLocalWebRtc({url: 'ws://127.0.0.1:8787',
        roomId: 'a'.repeat(32), role: 'beta', timeoutMs: 800});
      assert.deepEqual(result, {peer: {ready: true}});
      assert.equal(invocation.argument.url, 'ws://127.0.0.1:8787');
      assert.equal(invocation.argument.roomId, 'a'.repeat(32));
      assert.equal(invocation.argument.role, 'beta');
      assert.ok(invocation.argument.timeoutMs > 0 && invocation.argument.timeoutMs <= 800);
      assert.match(invocation.source, /createRoomWebRtcSignaler/);
      await assert.rejects(instance.assertRoomSignalingHealthy(), /Room WebRTC signaling is unavailable/);
    } finally {
      const closing = instance.close();
      let settled = false;
      closing.then(() => { settled = true; }, () => { settled = true; });
      try {
        await cleanupStarted.promise;
        assert.equal(instance.close(), closing, 'repeated close joins the same cleanup operation');
        await Promise.resolve();
        assert.equal(settled, false, 'close remains pending while page-owned cleanup is pending');
      } finally { allowCleanup.resolve(); }
      assert.equal(await closing, true);
    }
  });
});


test('runtime fixture tail changes only the next ordinary Gamepad sample and retains waits', async () => {
  const {standardPad} = await import('./controller-fixtures.mjs');
  const {createControllerManager} = await import('../web/controller-input.mjs');
  const prior = globalThis.window, pad = standardPad();
  const manager = createControllerManager({getGamepads: () => [pad], storage: null, userAgent: 'Chrome/154'});
  const discovered = manager.inspect(); manager.assign(discovered[0].key, 0);
  const captures = [], events = [], owner = {armed: true, closing: false, failure: null, peer: null};
  const original = () => { events.push('original'); };
  const page = {menuFrame: original, meleeNetRuntimeLockstepSnapshot: () => owner,
    __meleeSyntheticPadState: 'neutral', __meleeSyntheticPadTransition(state) {
      events.push(state); this.__meleeSyntheticPadState = state; pad.buttons[0] = {pressed: state === 'A', value: state === 'A' ? 1 : 0};
    }};
  globalThis.window = page;
  try {
    installRuntimeInputFixtureInPage({role: 'alpha', inputTicks: 6});
    page.menuFrame(); assert.deepEqual(events, ['original']);
    owner.peer = {failure: null, protocol: {ready: true, terminal: null},
      localInputCapture: {mode: 'live', enabled: true, captures}};
    for (let tick = 0; tick < 6; ++tick) {
      const poll = manager.sample()[0].output;
      assert.equal(poll.buttons, tick === 1 ? 0x0100 : 0, 'Ordinary controller manager sees transition only on the next poll');
      // Explicit native ABI stand-in; production PAD conversion remains native.
      const bytes = Array(11).fill(0); bytes[0] = poll.buttons >>> 8; bytes[1] = poll.buttons & 255;
      captures.push({source_cursor: tick, input_tick: tick, local_port: 0, poll_serial: tick + 10, bytes});
      page.menuFrame(); const transitions = page.__meleeRuntimeInputFixture.snapshot().transitions.length;
      page.menuFrame(); assert.equal(page.__meleeRuntimeInputFixture.snapshot().transitions.length, transitions, 'Wait callback does not resample or repeat transition');
    }
    assert.deepEqual(page.__meleeRuntimeInputFixture.snapshot().transitions.map(x => x.state), ['A', 'release']);
    assert.equal(page.__meleeRuntimeInputFixture.snapshot().captured_count, 6);
    assert.equal(page.__meleeRuntimeInputFixture.dispose().disposed, true); assert.equal(page.menuFrame, original);
    assert.equal(pad.buttons[0].pressed, false); page.__meleeRuntimeInputFixture.dispose();
    assert.throws(() => installRuntimeInputFixtureInPage({role: 'alpha', inputTicks: 6}), /fresh neutral/);
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});

test('CSS-to-SSS fixture drives the ordinary controller manager one selected sample at a time', () => {
  const prior = globalThis.window, frames = Buffer.alloc(153 * 44);
  for (let tick = 0; tick < 153; ++tick) {
    frames[tick * 44 + 32] = 0xff;
    frames[tick * 44 + 43] = 0xff;
  }
  const setPort = (tick, port, buttons, x, y) => {
    const offset = tick * 44 + port * 11;
    frames.writeUInt16BE(buttons, offset); frames.writeInt8(x, offset + 2); frames.writeInt8(y, offset + 3);
  };
  setPort(20, 0, 0, 80, 0); setPort(21, 1, 0, -60, 40);
  for (let tick = 150; tick <= 152; ++tick) setPort(tick, 0, 0x1000, 0, 0);
  const samples = buildRuntimeCssSssGamepadSamples(frames);
  const pages = {}, counters = {};
  const encode = output => [output.buttons >>> 8, output.buttons & 255,
    ...output.stick.map(value => value & 255), ...output.cstick.map(value => value & 255),
    ...output.triggers, 0, 0, 0];
  const setup = role => {
    const port = role === 'alpha' ? 0 : 1, pad = standardPad(port);
    const manager = createControllerManager({getGamepads: () => [pad], storage: null, userAgent: 'Chrome/154'});
    const discovered = manager.inspect(); manager.assign(discovered[0].key, port); manager.inspect();
    const captures = [], events = [], status = {active: 1, cursor: 0, blocker: 'network_wait', terminal: {kind: 0}};
    const native = {phase: 1, running: 1}; let pollAllowed = true, pollCalls = 0, originalCalls = 0;
    const owner = {armed: true, closing: false, failure: null, peer: {failure: null,
      protocol: {ready: true, terminal: null, local_checksum_ticks: 520, remote_checksum_ticks: 520,
        next_checksum_compare: 520, remote_ack_input: 517},
      localInputCapture: {mode: 'live', enabled: true, captures}}};
    const original = () => {
      ++originalCalls; events.push('original');
      if (pollAllowed && captures.length < RUNTIME_CSS_SSS_INPUT_TICKS) {
        ++pollCalls;
        const row = manager.sample().find(item => item.port === port);
        assert.ok(row?.active, `${role} controller remains active on its explicit port`);
        const tick = captures.length, bytes = encode(row.output);
        assert.deepEqual(bytes, samples[role][tick].bytes, `${role} ordinary conversion matches recipe tick ${tick}`);
        captures.push({source_cursor: tick, input_tick: tick, local_port: port,
          poll_serial: tick * 2 + 10, bytes});
        status.cursor = tick + 1; native.phase = tick < 153 ? 1 : 3;
      } else if (captures.length === RUNTIME_CSS_SSS_INPUT_TICKS) {
        status.cursor = 520; status.blocker = 'complete'; native.phase = 3;
      }
    };
    const page = {menuFrame: original, meleeNetRuntimeLockstepSnapshot: () => owner,
      __net: {status: () => status, native: () => native}, __meleeSyntheticPadState: 'neutral',
      __meleeSyntheticPadTransition(state) {
        if (typeof state === 'string') {
          this.__meleeSyntheticPadState = state;
          pad.buttons = Array.from({length: 17}, (_, index) => ({pressed: state === 'A' && index === 0,
            value: state === 'A' && index === 0 ? 1 : 0})); pad.axes = [0, 0, 0, 0];
        } else {
          this.__meleeSyntheticPadState = `css-start-to-sss:${state.input_tick}`;
          pad.buttons = state.buttons.map(button => ({...button})); pad.axes = [...state.axes];
        }
      }};
    globalThis.window = page;
    installRuntimeInputFixtureInPage({role, inputTicks: RUNTIME_CSS_SSS_INPUT_TICKS,
      variant: 'css-start-to-sss', samples: samples[role]});
    pages[role] = {page, original, status, native, owner, captures, events, counters: () => ({pollCalls, originalCalls}),
      setPollAllowed(value) { pollAllowed = value; }};
  };
  try {
    setup('alpha'); setup('beta');
    for (const role of ['alpha', 'beta']) {
      const {page, original, captures, events, counters, setPollAllowed, status, owner} = pages[role];
      globalThis.window = page;
      for (let tick = 0; tick < RUNTIME_CSS_SSS_INPUT_TICKS; ++tick) {
        if (tick === 10) {
          const before = captures.length, transitions = page.__meleeRuntimeInputFixture.snapshot().transitions.length;
          setPollAllowed(false); page.menuFrame(); page.menuFrame(); setPollAllowed(true);
          assert.equal(captures.length, before); assert.equal(page.__meleeRuntimeInputFixture.snapshot().transitions.length, transitions);
        }
        page.menuFrame();
        assert.equal(captures.length, tick + 1);
      }
      page.menuFrame();
      status.cursor = 520; status.blocker = 'complete';
      owner.peer.protocol.local_checksum_ticks = 520; owner.peer.protocol.remote_checksum_ticks = 520;
      owner.peer.protocol.next_checksum_compare = 520; owner.peer.protocol.remote_ack_input = 517;
      const frozen = page.__meleeRuntimeInputFixture.freeze();
      assert.equal(frozen.frozen, true); assert.equal(frozen.captured_count, RUNTIME_CSS_SSS_INPUT_TICKS);
      assert.deepEqual(frozen.phase_samples.map(row => row.phase).filter((phase, index, rows) => !index || phase !== rows[index - 1]), [1, 3]);
      assert.equal(page.menuFrame, original, 'fixture restores the callback it captured');
      assert.equal(page.__meleeSyntheticPadState, 'neutral');
      assert.equal(counters().pollCalls, RUNTIME_CSS_SSS_INPUT_TICKS);
      assert.equal(counters().originalCalls, RUNTIME_CSS_SSS_INPUT_TICKS + 3,
        'the original synchronous callback runs once for every input and wait frame');
      assert.equal(events.length, counters().originalCalls);
      assert.equal(frozen.captures.length, RUNTIME_CSS_SSS_INPUT_TICKS);
    }
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});

test('CSS-to-SSS fixture refuses to freeze unless native status ends in SSS', () => {
  const prior = globalThis.window;
  const frames = Buffer.alloc(153 * 44);
  for (let tick = 0; tick < 153; ++tick) {
    frames[tick * 44 + 32] = 0xff;
    frames[tick * 44 + 43] = 0xff;
  }
  for (let tick = 150; tick <= 152; ++tick) frames.writeUInt16BE(0x1000, tick * 44);
  const samples = buildRuntimeCssSssGamepadSamples(frames).alpha;
  const captures = [], status = {active: 1, cursor: 0, blocker: 'network_wait', terminal: {kind: 0}};
  const native = {phase: 1, running: 1};
  const owner = {armed: true, closing: false, failure: null, peer: {failure: null,
    protocol: {ready: true, terminal: null, local_checksum_ticks: 520, remote_checksum_ticks: 520,
      next_checksum_compare: 520, remote_ack_input: 517},
    localInputCapture: {mode: 'live', enabled: true, captures}}};
  const page = {menuFrame() {
    const tick = captures.length;
    captures.push({source_cursor: tick, input_tick: tick, local_port: 0, poll_serial: tick + 1,
      bytes: samples[tick].bytes});
    status.cursor = tick + 1; native.phase = tick < 153 ? 1 : 3;
  }, meleeNetRuntimeLockstepSnapshot: () => owner,
  __net: {status: () => status, native: () => native}, __meleeSyntheticPadState: 'neutral',
  __meleeSyntheticPadTransition(state) { this.__meleeSyntheticPadState = state; }};
  try {
    globalThis.window = page;
    installRuntimeInputFixtureInPage({role: 'alpha', inputTicks: RUNTIME_CSS_SSS_INPUT_TICKS,
      variant: 'css-start-to-sss', samples});
    for (let tick = 0; tick < RUNTIME_CSS_SSS_INPUT_TICKS; ++tick) page.menuFrame();
    status.cursor = 520; status.blocker = 'complete'; native.phase = 1;
    assert.throws(() => page.__meleeRuntimeInputFixture.freeze(), /cannot freeze/);
    assert.equal(page.__meleeRuntimeInputFixture.snapshot().frozen, false);
    assert.equal(page.__meleeRuntimeInputFixture.snapshot().captured_count, RUNTIME_CSS_SSS_INPUT_TICKS);
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});

test('actual runtime fixture rejects malformed ownership, nonneutral pattern and asynchronous callback', () => {
  const prior = globalThis.window;
  const row = () => ({source_cursor: 0, input_tick: 0, local_port: 0, poll_serial: 10, bytes: Array(11).fill(0)});
  const run = mutate => {
    const captures = [row()], owner = {armed: true, closing: false, failure: null,
      peer: {failure: null, protocol: {ready: true, terminal: null}, localInputCapture: {mode: 'live', enabled: true, captures}}};
    const page = {menuFrame() {}, meleeNetRuntimeLockstepSnapshot: () => owner,
      __meleeSyntheticPadState: 'neutral', __meleeSyntheticPadTransition(state) { this.__meleeSyntheticPadState = state; }};
    globalThis.window = page; mutate({page, owner, captures});
    installRuntimeInputFixtureInPage({role: 'alpha', inputTicks: 6});
    assert.throws(() => page.menuFrame()); assert.ok(page.__meleeRuntimeInputFixture.snapshot().failure);
    assert.equal(page.__meleeSyntheticPadState, 'neutral'); page.__meleeRuntimeInputFixture.dispose();
  };
  try {
    for (const mutate of [
      ({owner}) => { owner.closing = true; }, ({owner}) => { owner.failure = 'first owner failure'; },
      ({owner}) => { owner.armed = false; }, ({owner}) => { owner.peer.protocol.ready = false; },
      ({captures}) => { captures[0].local_port = 1; }, ({captures}) => { captures[0].input_tick = 1; },
      ({captures}) => { captures[0].poll_serial = -1; }, ({captures}) => { captures[0].bytes.pop(); },
      ({captures}) => { captures[0].bytes[4] = 256; }, ({captures}) => { captures[0].bytes[0] = 1; },
      ({captures}) => { captures.push({...row(), source_cursor: 1, input_tick: 1}); },
      ({page}) => { page.menuFrame = () => Promise.resolve(); },
      ({page}) => { page.menuFrame = () => { throw Error('original first error'); }; },
    ]) run(mutate);
    for (const lost of ['owner', 'peer']) {
      let owner = {armed: true, closing: false, failure: null, peer: {failure: null,
        protocol: {ready: true, terminal: null}, localInputCapture: {mode: 'live', enabled: true, captures: [row()]}}};
      const page = {menuFrame() {}, meleeNetRuntimeLockstepSnapshot: () => owner,
        __meleeSyntheticPadState: 'neutral', __meleeSyntheticPadTransition(state) { this.__meleeSyntheticPadState = state; }};
      globalThis.window = page; installRuntimeInputFixtureInPage({role: 'alpha', inputTicks: 6});
      page.menuFrame();
      if (lost === 'owner') owner = null; else owner.peer = null;
      assert.throws(() => page.menuFrame(), /lost its active/);
      assert.equal(page.__meleeSyntheticPadState, 'neutral');
      page.__meleeRuntimeInputFixture.dispose();
    }
    const page = {menuFrame() {}, meleeNetRuntimeLockstepSnapshot: () => null,
      __meleeSyntheticPadState: 'neutral', __meleeSyntheticPadTransition() {}};
    globalThis.window = page; installRuntimeInputFixtureInPage({role: 'beta', inputTicks: 6});
    const other = () => {}; page.menuFrame = other;
    assert.throws(() => page.__meleeRuntimeInputFixture.dispose(), /lost callback ownership/); assert.equal(page.menuFrame, other);
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});


test('beta runtime fixture stays neutral and refuses changed retained rows or extra capture', () => {
  const prior = globalThis.window;
  try {
    for (const mutation of ['retained', 'duplicate', 'extra']) {
      const captures = [], page = {menuFrame() {}, __meleeSyntheticPadState: 'neutral',
        __meleeSyntheticPadTransition(state) { this.__meleeSyntheticPadState = state; },
        meleeNetRuntimeLockstepSnapshot: () => ({armed: true, closing: false, failure: null,
          peer: {failure: null, protocol: {ready: true, terminal: null}, localInputCapture: {mode: 'live', enabled: true, captures}}})};
      globalThis.window = page; installRuntimeInputFixtureInPage({role: 'beta', inputTicks: 6});
      for (let tick = 0; tick < 6; ++tick) {
        captures.push({source_cursor: tick, input_tick: tick, local_port: 1, poll_serial: tick + 10, bytes: Array(11).fill(0)});
        page.menuFrame(); page.menuFrame(); assert.equal(page.__meleeSyntheticPadState, 'neutral');
      }
      assert.deepEqual(page.__meleeRuntimeInputFixture.snapshot().transitions, []);
      if (mutation === 'retained') captures[0].poll_serial = 9;
      if (mutation === 'duplicate') captures[5] = {...captures[4]};
      if (mutation === 'extra') captures.push({...captures[5], source_cursor: 6, input_tick: 6, poll_serial: 16});
      assert.throws(() => page.menuFrame());
      const first = page.__meleeRuntimeInputFixture.snapshot().failure;
      page.menuFrame(); assert.equal(page.__meleeRuntimeInputFixture.snapshot().failure, first);
      page.__meleeRuntimeInputFixture.dispose();
    }
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});


test('actual harness freezes complete fixture before asynchronous normal close callbacks', async () => {
  const {readFile} = await import('node:fs/promises');
  const vm = await import('node:vm');
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf('    if (runtimeOwned) {', source.indexOf('  stopRouteCaptureWatchers = true;'));
  const end = source.indexOf('    } else {', start);
  assert(start >= 0 && end > start);
  const prior = globalThis.window, pages = {}, owners = {}, calls = {alpha: 0, beta: 0};
  const setup = (role, count = 6) => {
    const captures = [], owner = {armed: true, closing: false, failure: null,
      peer: {failure: null, protocol: {ready: true, terminal: null, local_checksum_ticks: 8,
        remote_checksum_ticks: 8, next_checksum_compare: 8, remote_ack_input: 5},
        localInputCapture: {mode: 'live', enabled: true, captures}}};
    const page = {menuFrame() { ++calls[role]; },
      meleeNetRuntimeLockstepSnapshot: () => owner,
      __net: {status: () => ({active: 1, cursor: 8, blocker: 'complete', terminal: {kind: 0}})},
      __meleeSyntheticPadState: 'neutral', __meleeSyntheticPadTransition(state) { this.__meleeSyntheticPadState = state; }};
    globalThis.window = page; installRuntimeInputFixtureInPage({role, inputTicks: 6});
    for (let tick = 0; tick < count; ++tick) {
      const bytes = Array(11).fill(0); if (role === 'alpha' && tick === 1) bytes[0] = 1;
      captures.push({source_cursor: tick, input_tick: tick, local_port: role === 'alpha' ? 0 : 1, poll_serial: tick + 10, bytes});
      page.menuFrame();
    }
    pages[role] = page; owners[role] = owner; return page;
  };
  const invoke = (page, method) => { globalThis.window = page; return page.__meleeRuntimeInputFixture[method](); };
  try {
    for (const role of ['alpha', 'beta']) setup(role);
    let closeCalls = 0;
    const instanceRows = {alpha: {}, beta: {}}, pairResults = {};
    const relay = {async close() {
      ++closeCalls;
      for (const role of ['alpha', 'beta']) {
        const page = pages[role]; assert.equal(page.__meleeRuntimeInputFixture.snapshot().frozen, true);
        owners[role].closing = true; globalThis.window = page; page.menuFrame();
      }
      await Promise.resolve();
      for (const role of ['alpha', 'beta']) { globalThis.window = pages[role]; pages[role].menuFrame(); }
    }};
    const context = {runtimeOwned: true, runtimeInputFixture: true, instanceRows, pairResults, relay,
      instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {freezeRuntimeInputFixture: async () => invoke(pages[role], 'freeze')}]))};
    await vm.runInNewContext(`(async()=>{${source.slice(start, end)}\n}})()`, context);
    assert.equal(closeCalls, 1); assert.equal(pairResults.relay_closed, true);
    for (const role of ['alpha', 'beta']) {
      assert.equal(calls[role], 8, 'Original callback runs exactly once per capture/render callback');
      const witness = invoke(pages[role], 'snapshot');
      assert.equal(witness.frozen, true); assert.equal(witness.disposed, false); assert.equal(witness.failure, null);
      assert.equal(witness.captures.length, 6); assert.equal(pages[role].__meleeSyntheticPadState, 'neutral');
      witness.captures[0].bytes[0] = 255;
      assert.equal(invoke(pages[role], 'snapshot').captures[0].bytes[0], 0, 'Frozen witness is isolated from caller mutation');
      assert.equal(invoke(pages[role], 'dispose').frozen, true, 'Finalizer fallback preserves successful freeze');
    }
    for (const mutate of [
      () => { owners.alpha.closing = true; },
      () => { owners.alpha.failure = 'owner first cause'; },
      () => { pages.alpha.__net.status = () => ({active: 1, cursor: 7, blocker: 'network_wait', terminal: {kind: 0}}); },
      () => { owners.alpha.peer.protocol.remote_ack_input = 4; },
      () => { owners.alpha.peer.localInputCapture.captures[0].poll_serial = 9; },
    ]) {
      setup('alpha'); mutate();
      assert.throws(() => invoke(pages.alpha, 'freeze'), /cannot freeze/);
      assert.equal(pages.alpha.__meleeRuntimeInputFixture.snapshot().frozen, false);
      invoke(pages.alpha, 'dispose');
    }
    setup('alpha', 5); setup('beta'); closeCalls = 0;
    await assert.rejects(vm.runInNewContext(`(async()=>{${source.slice(start, end)}\n}})()`, context), /cannot freeze/);
    assert.equal(closeCalls, 0, 'Incomplete witness stops before normal relay close');
    assert.equal(pages.alpha.__meleeRuntimeInputFixture.snapshot().frozen, false);
    assert.ok(pages.alpha.__meleeRuntimeInputFixture.snapshot().failure);
    assert.equal(invoke(pages.alpha, 'dispose').disposed, true, 'Failed freeze retains finalizer fallback');
  } finally { if (prior === undefined) delete globalThis.window; else globalThis.window = prior; }
});
