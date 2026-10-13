import assert from 'node:assert/strict';
import test from 'node:test';
import {requestFallbackSignalIfChildLive, waitForOwnedProcessGroupToDisappear} from '../scripts/net_room_relay_runtime_owner.mjs';

test('owned runtime close verifies the recorded group is gone after its leader exits', async t => {
  const kill = t.mock.method(process, 'kill', () => { throw Error('Unexpected signal'); });
  const events = [];
  const outcome = await waitForOwnedProcessGroupToDisappear({pid: 43127,
    leaderExitObserved: true, timeoutMs: 0,
    groupAliveProbe: pid => { assert.equal(pid, 43127); return false; },
    record: (event, details) => events.push({event, ...details}),
  });
  assert.deepEqual(outcome, {alive: false, verifiedGone: true});
  assert.equal(kill.mock.callCount(), 0);
  assert.deepEqual(events, [{event: 'owner-process-group-check', pid: 43127,
    alive: false, leaderExitObserved: true}]);
});

test('owned runtime close preserves an unproven remaining or reused PGID', async t => {
  const kill = t.mock.method(process, 'kill', () => { throw Error('Unexpected signal'); });
  const events = [];
  const outcome = await waitForOwnedProcessGroupToDisappear({pid: 43127,
    leaderExitObserved: true, timeoutMs: 0,
    groupAliveProbe: pid => { assert.equal(pid, 43127); return true; },
    pause: async () => {},
    record: (event, details) => events.push({event, ...details}),
  });
  assert.deepEqual(outcome, {alive: true, verifiedGone: false});
  assert.equal(kill.mock.callCount(), 0);
  assert.deepEqual(events.map(row => row.event), [
    'owner-process-group-check', 'post-leader-exit-group-preserved-unproven',
    'owner-process-group-settled-check',
  ]);
  assert.equal(events.some(row => /signal|kill/i.test(row.event)), false);
});


test('already signaled child is observed before either fallback signal', () => {
  const originalKill = process.kill;
  const calls = [];
  const events = [];
  process.kill = (pid, signal) => { calls.push({pid, signal}); };
  try {
    const child = {pid: 43127, exitCode: null, signalCode: 'SIGTERM'};
    const term = requestFallbackSignalIfChildLive({child,
      recordedExit: {code: null, signal: 'SIGTERM'}, signal: 'SIGTERM', timeoutMs: 3000,
      record: (event, details) => events.push({event, ...details})});
    const kill = requestFallbackSignalIfChildLive({child, signal: 'SIGKILL', timeoutMs: 3000,
      record: (event, details) => events.push({event, ...details})});
    assert.equal(term.signalled, false);
    assert.deepEqual(term.exit, {code: null, signal: 'SIGTERM'});
    assert.equal(kill.signalled, false);
    assert.deepEqual(kill.exit, {code: null, signal: 'SIGTERM'});
    assert.deepEqual(calls, []);
    assert.deepEqual(events.map(row => row.event), [
      'owner-fallback-signal-skipped-after-exit', 'owner-fallback-signal-skipped-after-exit',
    ]);
  } finally {
    process.kill = originalKill;
  }
});

test('fallback rechecks child exit state before escalating from TERM to KILL', () => {
  const originalKill = process.kill;
  const calls = [];
  const events = [];
  const child = {pid: 43127, exitCode: null, signalCode: null};
  process.kill = (pid, signal) => {
    calls.push({pid, signal});
    child.signalCode = signal;
  };
  try {
    const term = requestFallbackSignalIfChildLive({child, signal: 'SIGTERM', timeoutMs: 3000,
      record: (event, details) => events.push({event, ...details})});
    const kill = requestFallbackSignalIfChildLive({child, signal: 'SIGKILL', timeoutMs: 3000,
      record: (event, details) => events.push({event, ...details})});
    assert.equal(term.signalled, true);
    assert.equal(kill.signalled, false);
    assert.deepEqual(calls, [{pid: -43127, signal: 'SIGTERM'}]);
    assert.equal(calls.some(row => row.signal === 'SIGKILL'), false);
    assert.deepEqual(kill.exit, {code: null, signal: 'SIGTERM'});
    assert.deepEqual(events.map(row => row.event), [
      'fallback-sigterm-request', 'owner-fallback-signal-skipped-after-exit',
    ]);
  } finally {
    process.kill = originalKill;
  }
});
