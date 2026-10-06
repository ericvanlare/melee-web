import assert from 'node:assert/strict';
import test from 'node:test';
import {classifyStagingByteReplayCompletion as classify} from './staging_ring_replay_completion.mjs';
const report = () => ({complete: true, frames: 600, pass: false,
  instrumented_timing_resumes: 0, failures: ['livePipelinesCreated', 'preparationPauses'],
  metrics: {sourceFrames: 600, sourceSteps: 600, sourceDraws: 600, focusLost: false}});
test('retains only the two explicit orthogonal performance reasons', () => {
  assert.deepEqual(classify(report()).excluded_performance_failures, report().failures);
});
test('rejects structural, timing, audio, focus, resume and incomplete timeline failures', () => {
  for (const reason of ['teardown incomplete', 'source tick/draw count mismatch', 'browserCallbackGaps',
    'nativeCallbacksOver33ms', 'audioUnderrunFrames', 'focusLost', 'unexpected reason']) {
    const value = report(); value.failures.push(reason);
    assert.throws(() => classify(value), /blocking failures/);
  }
  for (const field of ['sourceFrames', 'sourceSteps', 'sourceDraws']) {
    const value = report(); value.metrics[field] = 599;
    assert.throws(() => classify(value), /exact 600/);
  }
  for (const overrides of [{complete: false}, {instrumented_timing_resumes: 1}])
    assert.throws(() => classify({...report(), ...overrides}), /exact 600/);
  const focus = report(); focus.metrics.focusLost = true;
  assert.throws(() => classify(focus), /exact 600/);
});
