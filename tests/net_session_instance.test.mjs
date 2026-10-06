import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {openNetInstance} from '../scripts/net_session_instance.mjs';

function fakeChrome(goto) {
  let closeCalls = 0;
  let removeCalls = 0;
  const page = {
    on() {},
    off() { ++removeCalls; },
    async addInitScript() {},
    setDefaultTimeout() {},
    setDefaultNavigationTimeout() {},
    goto,
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
