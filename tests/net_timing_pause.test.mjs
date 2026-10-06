import assert from 'node:assert/strict';
import test from 'node:test';
import {attemptNetworkTimingPauseResume, evaluateNetworkTimingPause} from '../web/net-timing-pause.mjs';

const timingMessage = 'Paused after a timing disruption. Resume to continue.';
const pausedState = {state: 'paused', paused: true, canPause: true};
const timingPause = overrides => ({networkStatus: JSON.parse('{"active":1,"context_applied":1}'),
  nativeRunning: 0, message: timingMessage, ownerState: pausedState, ...overrides});

test('only resumes an active source-paused network timing disruption', () => {
  assert.deepEqual(evaluateNetworkTimingPause(timingPause()), {eligible: true, reasons: []});
  assert(evaluateNetworkTimingPause(timingPause({networkStatus: {active: 0}})).reasons.includes('network-inactive'));
  assert(evaluateNetworkTimingPause(timingPause({networkStatus: {active: true}})).reasons.includes('network-inactive'),
    'Native JSON status is an integer; a boolean must not pass by coercion');
  assert(evaluateNetworkTimingPause(timingPause({nativeRunning: 1})).reasons.includes('native-running'));
  assert(evaluateNetworkTimingPause(timingPause({message: 'Paused by user'})).reasons.includes('message-not-timing-pause'));
  assert(evaluateNetworkTimingPause(timingPause({ownerState: {
    state: 'preparing', paused: false, canPause: false}})).reasons.includes('owner-not-paused'));
  assert(evaluateNetworkTimingPause(timingPause({ownerState: {
    state: 'error', paused: false, canPause: false, requiresReload: true}})).reasons.includes('owner-cannot-pause'));
});

test('reports a native refusal without claiming a resume', async () => {
  let attempts = 0;
  const result = await attemptNetworkTimingPauseResume({canResume: () => true,
    resume: async () => { attempts++; }, isRunning: () => false});
  assert.equal(result, false);
  assert.equal(attempts, 1);
});
