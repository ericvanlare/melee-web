import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

// Execute the shipped observer with synthetic DOM, runtime, and native leaves.
const shell = fs.readFileSync(new URL('../web/online/online-shell.mjs', import.meta.url), 'utf8');
const start = 'export async function onlineSessionSnapshot(';
const end = 'window.onlineSessionSnapshot=onlineSessionSnapshot;';
assert.equal(shell.split(start).length, 2);
assert.equal(shell.split(end).length, 2);
const startAt = shell.indexOf(start);
const endAt = shell.indexOf(end);
assert(endAt > startAt);
const observer = shell.slice(startAt + 'export '.length, endAt);

function fixture() {
  const calls = [];
  const native = {phase: 7, running: 0, ownerState: 1, statusFailure: null};
  const owner = () => ({
    ownerState: native.ownerState, pendingCommand: 0, completeSequence: 1, requestSequence: 1,
    gpuPending: 0, endpointActive: 0, firstFailure: 0, rendererErrors: 0,
  });
  const report = {roomId: 'fixture-room', result: 'pending', failure: null};
  const world = vm.createContext({
    structuredClone, report, role: 'alpha', state: {}, roomUsed: true, discReady: true,
    startCommitted: true, historyOwned: false, nativeRetired: false, closed: false,
    loopWork: null, $: () => ({textContent: 'Fixture room status'}),
    controller: {status() {
      calls.push('typed status');
      if (native.statusFailure) throw native.statusFailure;
      return owner();
    }},
    context: {
      boundary(sample) { calls.push('ordinary boundary'); return sample(); },
      Module: {
        _melee_web_native_menu_phase() { calls.push('native phase'); return native.phase; },
        _melee_web_native_menu_running() { calls.push('native running'); return native.running; },
        _melee_web_input_message() { calls.push('native input'); return 1; },
        _melee_web_native_menu_match_observe() { calls.push('native match'); return 2; },
        UTF8ToString(pointer) { return pointer === 1 ? '{"ready":true}' : '{"frame":0}'; },
      },
      callbacks: {menuStagingDiagnosticsEnabled: () => true},
      diagnostics: {
        exportReports() { calls.push('current incidents'); return {incidents: []}; },
        async exportRetained() { calls.push('retained incidents'); return {records: []}; },
      },
    },
  });
  const snapshot = vm.runInContext(observer + '\nonlineSessionSnapshot', world);
  return {snapshot, world, report, calls, native};
}

test('committed start observes the transition without ordinary queue or native reads', async () => {
  const {snapshot, calls} = fixture();
  const value = await snapshot({source: true});
  assert.equal(value.stage, 'starting');
  assert.equal(value.phase, null);
  assert.equal(value.running, null);
  assert.equal(value.owner, null);
  assert.equal(value.input, undefined);
  assert.equal(value.stagingDiagnosticsEnabled, true);
  assert.deepEqual(calls, []);
});

test('typed history guard resumes reads after the same committed start takes ownership', async () => {
  const {snapshot, world, calls, native} = fixture();
  world.historyOwned = true;
  world.loopWork = Promise.resolve();
  native.statusFailure = Error('Runtime history Asyncify call is in progress');
  await assert.rejects(snapshot({source: true}), error => error === native.statusFailure);
  assert.deepEqual(calls.splice(0), ['typed status']);

  native.statusFailure = null;
  const value = await snapshot({source: true});
  assert.equal(value.stage, 'playing');
  assert.equal(value.phase, 7);
  assert.equal(value.input.ready, true);
  assert.equal(value.match.frame, 0);
  assert.deepEqual(calls, ['typed status', 'native phase', 'native running', 'native input', 'native match']);
});

test('a committed-start failure retains the JS report without touching either native owner', async () => {
  const {snapshot, world, report, calls} = fixture();
  report.failure = 'Fixture start commit refused';
  report.result = 'FAIL';
  report.closed = true;
  world.closed = true;
  const value = await snapshot({source: true, diagnostics: true});
  assert.equal(value.stage, 'closed');
  assert.equal(value.closed, true);
  assert.equal(value.error, report.failure);
  assert.equal(value.session.failure, report.failure);
  assert.equal(value.session.result, 'FAIL');
  assert.equal(value.owner, null);
  assert.deepEqual(value.incidents, {incidents: []});
  assert.deepEqual(value.retainedIncidents, {records: []});
  assert.deepEqual(calls, ['current incidents', 'retained incidents']);
});

test('ordinary menus use the queued boundary and a retired native owner bypasses it', async () => {
  const {snapshot, world, calls, native} = fixture();
  world.startCommitted = false;
  native.phase = 1;
  native.running = 1;
  native.ownerState = 0;
  const ordinary = await snapshot();
  assert.equal(ordinary.phase, 1);
  assert.equal(ordinary.running, 1);
  assert.deepEqual(calls.splice(0), ['ordinary boundary', 'typed status', 'native phase', 'native running']);

  world.startCommitted = true;
  world.nativeRetired = true;
  world.closed = true;
  native.phase = 0;
  native.running = 0;
  native.ownerState = 2;
  const retired = await snapshot();
  assert.equal(retired.stage, 'closed');
  assert.equal(retired.phase, 0);
  assert.equal(retired.owner.ownerState, 2);
  assert.deepEqual(calls, ['typed status', 'native phase', 'native running']);
});
