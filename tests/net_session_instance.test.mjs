import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {closePageNativeNetworkOwnership, firstFatalBrowserError, openNetInstance, PAGE_HELPERS} from '../scripts/net_session_instance.mjs';
import {createBrowserNativePeer} from '../scripts/net_lockstep_browser_peer.mjs';
import {createNetLockstepNativeAdapter} from '../scripts/net_lockstep_native_adapter.mjs';
import {LOCKSTEP_DELAY} from '../scripts/net_lockstep_core.mjs';
import {installNetSourceAccounting, readNetSourceAccounting} from '../scripts/net_source_accounting.mjs';

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
