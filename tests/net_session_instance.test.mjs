import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {firstFatalBrowserError, openNetInstance} from '../scripts/net_session_instance.mjs';

function fakeChrome(goto) {
  let closeCalls = 0;
  let removeCalls = 0;
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
    goto: (...args) => goto(emit, ...args),
  };
  const context = {
    pages: () => [page],
    browser: () => null,
    async close() { ++closeCalls; },
  };
  return {
    chromium: {async launchPersistentContext() { return context; }},
    context,
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
  });
});
