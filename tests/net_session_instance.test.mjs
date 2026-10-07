import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {firstFatalBrowserError, openNetInstance} from '../scripts/net_session_instance.mjs';

function fakeChrome(goto) {
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

test('passes bounded room-signaling options through the page invocation', async () => {
  await withProfile(async profile => {
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
      assert.equal(await instance.close(), true);
    }
  });
});
