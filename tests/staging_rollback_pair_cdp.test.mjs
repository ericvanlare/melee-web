import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import {validateBrowserProcessInfo} from '../scripts/net_lockstep_browser_handshake_probe.mjs';

const driver = fs.readFileSync(new URL('../scripts/staging_rollback_pair_browser.mjs', import.meta.url), 'utf8');
const start = 'async function withinBound(';
const end = 'async function settlePair(';
assert.equal(driver.split(start).length, 2);
assert.equal(driver.split(end).length, 2);
const startAt = driver.indexOf(start), endAt = driver.indexOf(end);
assert(endAt > startAt);
const {collectBrowserProcessInfo, flattenBrowserProcessInfo} = vm.runInNewContext(
  driver.slice(startAt, endAt) + '\n({collectBrowserProcessInfo, flattenBrowserProcessInfo})',
  {assert, validateBrowserProcessInfo, setTimeout, clearTimeout, remaining: stopAt => stopAt - Date.now()},
);

const root = id => ({id, type: 'browser'});
const renderer = id => ({id, type: 'renderer'});

function ownedBrowser(processInfo, {detachFailure = null} = {}) {
  const calls = [];
  return {calls, async newBrowserCDPSession() {
    calls.push('acquire');
    return {
      async send(command) { calls.push(command); return {processInfo}; },
      async detach() { calls.push('detach'); if (detachFailure) throw detachFailure; },
    };
  }};
}

test('collects and detaches each browser CDP inventory and flattens distinct owners', async () => {
  const alpha = ownedBrowser([{...root(101), cpuTime: 20}, renderer(102)]);
  const beta = ownedBrowser([root(201), renderer(202)]);
  const stopAt = Date.now() + 1000;
  const rows = flattenBrowserProcessInfo({
    alpha: await collectBrowserProcessInfo(alpha, 'alpha', stopAt),
    beta: await collectBrowserProcessInfo(beta, 'beta', stopAt),
  });
  assert.deepEqual(JSON.parse(JSON.stringify(rows)), {browserRoots: {alpha: 101, beta: 201}, rows: [
    {id: 101, type: 'browser', owner_role: 'alpha'}, {id: 102, type: 'renderer', owner_role: 'alpha'},
    {id: 201, type: 'browser', owner_role: 'beta'}, {id: 202, type: 'renderer', owner_role: 'beta'},
  ]});
  for (const browser of [alpha, beta])
    assert.deepEqual(browser.calls, ['acquire', 'SystemInfo.getProcessInfo', 'detach']);
});

test('malformed inventory and missing or repeated roots fail closed and still detach', async () => {
  await assert.rejects(collectBrowserProcessInfo(null, 'alpha', Date.now() + 1000), /lacks an owned/);
  for (const [value, message] of [[undefined, /nonempty typed positive/], [[], /nonempty typed positive/],
    [[root(-1)], /nonempty typed positive/], [[root(101), renderer(101)], /duplicate PIDs/],
    [[renderer(102)], /exactly one browser root/], [[root(101), root(102)], /exactly one browser root/]]) {
    const browser = ownedBrowser(value);
    await assert.rejects(collectBrowserProcessInfo(browser, 'alpha', Date.now() + 1000), message);
    assert.equal(browser.calls.at(-1), 'detach');
  }
  const cannotDetach = ownedBrowser([root(101)], {detachFailure: Error('detach failed')});
  await assert.rejects(collectBrowserProcessInfo(cannotDetach, 'alpha', Date.now() + 1000), /detach failed/);
});

test('joint attribution refuses a missing owner and any cross-owner PID overlap', () => {
  assert.throws(() => flattenBrowserProcessInfo({alpha: [root(101)]}), /nonempty typed positive/);
  assert.throws(() => flattenBrowserProcessInfo({alpha: [root(101)], beta: [root(101)]}), /overlaps/);
  assert.throws(() => flattenBrowserProcessInfo({
    alpha: [root(101), renderer(102)], beta: [root(201), renderer(102)],
  }), /overlaps/);
});
