import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('../scripts/a3_local_room_keyboard_browser.mjs', import.meta.url), 'utf8');
const start = source.indexOf('async function leaveThroughUi(');
const end = source.indexOf('\nasync function unloadRuntime', start);
assert.ok(start >= 0 && end > start, 'leaveThroughUi function is present');
const leaveThroughUiSource = source.slice(start, end);

const statusFor = hostedStaging => hostedStaging
  ? 'You left the room. Reload and import your disc to try again.'
  : 'Disconnected by your choice. Route outcome is not assessed.';

async function invokeLeave({hostedStaging, status = statusFor(hostedStaging), snapshot = {
  close_mode: 'normal', peer: {endpointClosed: true}, transport_cleanup: {closed: true},
}}) {
  const calls = {click: 0, snapshot: 0};
  const page = {
    locator(selector) {
      assert.equal(selector, '#lockstep-leave');
      return {isEnabled: async () => true, click: async options => {
        calls.click++;
        assert.ok(options.timeout > 0 && options.timeout <= 10000);
      }};
    },
    async waitForFunction(predicate, expected, options) {
      assert.ok(options.timeout > 0 && options.timeout <= 15000);
      assert.equal(predicate(expected), true, 'leave status must match the active UI');
    },
  };
  const context = {
    hostedStaging,
    cleanupRemaining: () => 5000,
    document: {querySelector(selector) {
      assert.equal(selector, '#lockstep-status');
      return {textContent: status};
    }},
    readPeerSnapshot: async () => { calls.snapshot++; return {snapshot}; },
    requireCondition(condition, message) { if (!condition) throw new Error(message); },
  };
  vm.runInNewContext(`${leaveThroughUiSource}\nglobalThis.invokeLeave = leaveThroughUi;`, context);
  const result = await context.invokeLeave({page}, 'alpha');
  return {result, calls};
}

test('leaveThroughUi accepts the exact hosted and local status while requiring owner teardown', async () => {
  for (const hostedStaging of [false, true]) {
    const {result, calls} = await invokeLeave({hostedStaging});
    assert.equal(result.status, statusFor(hostedStaging));
    assert.equal(result.close_mode, 'normal');
    assert.equal(result.endpoint_closed, true);
    assert.equal(result.transport_cleanup.closed, true);
    assert.deepEqual(calls, {click: 1, snapshot: 1});
  }
});

test('leaveThroughUi refuses a stale message and incomplete endpoint or transport cleanup', async () => {
  await assert.rejects(invokeLeave({hostedStaging: true, status: statusFor(false)}),
    /leave status must match the active UI/);
  for (const snapshot of [
    {close_mode: 'fatal', peer: {endpointClosed: true}, transport_cleanup: {closed: true}},
    {close_mode: 'normal', peer: {endpointClosed: false}, transport_cleanup: {closed: true}},
    {close_mode: 'normal', peer: {endpointClosed: true}, transport_cleanup: {closed: false}},
  ]) {
    await assert.rejects(invokeLeave({hostedStaging: true, snapshot}),
      /runtime owner did not confirm normal relay teardown/);
  }
});
