import assert from 'node:assert/strict';
import test from 'node:test';
import {attemptNetworkTimingPauseResume, canResumeNetworkTimingPause} from '../web/net-timing-pause.mjs';

const timingMessage = 'Paused after a timing disruption. Resume to continue.';
const pausedState = {state: 'paused', paused: true, canPause: true};
const timingPause = overrides => ({networkActive: true, nativeRunning: false,
  message: timingMessage, ownerState: pausedState, ...overrides});

test('only resumes an active source-paused network timing disruption', () => {
  assert.equal(canResumeNetworkTimingPause(timingPause()), true);
  assert.equal(canResumeNetworkTimingPause(timingPause({networkActive: false})), false);
  assert.equal(canResumeNetworkTimingPause(timingPause({nativeRunning: true,
    ownerState: {state: 'match', paused: false, canPause: true}})), false);
  assert.equal(canResumeNetworkTimingPause(timingPause({message: 'Paused by user'})), false);
  assert.equal(canResumeNetworkTimingPause(timingPause({ownerState: {
    state: 'preparing', paused: false, canPause: false}})), false);
  assert.equal(canResumeNetworkTimingPause(timingPause({ownerState: {
    state: 'error', paused: false, canPause: false, requiresReload: true}})), false);
  assert.equal(canResumeNetworkTimingPause(timingPause({nativeRunning: true,
    ownerState: {state: 'match', paused: false, canPause: true}})), false);
});

test('reports a native refusal without claiming a resume', async () => {
  let attempts = 0;
  const result = await attemptNetworkTimingPauseResume({canResume: () => true,
    resume: async () => { attempts++; }, isRunning: () => false});
  assert.equal(result, false);
  assert.equal(attempts, 1);
});
