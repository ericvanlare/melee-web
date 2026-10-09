import assert from 'node:assert/strict';
import test from 'node:test';
import {
  confirmTwoHumanResults,
  RESULTS_TRACE_CAPACITY,
  RESULTS_TRACE_START_MASK,
  RESULTS_CONNECTED_PAD_ERRORS,
  resultsPadTraceFailures,
} from './vs_rules_results_confirmation_driver.mjs';

function makeTraceRow(sourceFrame, {phase, statsPhase, confirmed, startPorts = []}) {
  return {
    source_frame: sourceFrame,
    tick_returned: true,
    pads: Array.from({length: 4}, (_, port) => ({
      button: startPorts.includes(port) ? RESULTS_TRACE_START_MASK : 0,
      err: RESULTS_CONNECTED_PAD_ERRORS[port],
    })),
    source_consumed_pads: Array.from({length: 4}, (_, port) => ({
      trigger: startPorts.includes(port) ? RESULTS_TRACE_START_MASK : 0,
      err: RESULTS_CONNECTED_PAD_ERRORS[port],
    })),
    results_state_after_tick: {
      source_frame: sourceFrame + 1,
      phase,
      stats_phase: statsPhase,
      players: confirmed.map(value => ({confirmed: value})),
    },
  };
}

function makeTrace(samples) {
  return {
    schema: 'melee-web-results-pad-trace-v1',
    attempts: samples.length,
    retained: samples.length,
    capacity: RESULTS_TRACE_CAPACITY,
    overflow: false,
    samples,
  };
}

test('Results PAD trace rejects incomplete rows, overflow, failed ticks and unknown phases', () => {
  const valid = makeTrace([makeTraceRow(0, {phase: 2, statsPhase: 1, confirmed: [0, 0, 1, 1]})]);
  assert.deepEqual(resultsPadTraceFailures(valid), []);
  assert(resultsPadTraceFailures(null).length > 0);
  assert(resultsPadTraceFailures({...valid, overflow: true}).some(row => row.includes('overflow')));
  assert(resultsPadTraceFailures({...valid, attempts: 2}).some(row => row.includes('accounting')));
  assert(resultsPadTraceFailures({...valid, samples: [{...valid.samples[0], tick_returned: false}]})
    .some(row => row.includes('did not complete')));
  assert(resultsPadTraceFailures(makeTrace([makeTraceRow(0, {
    phase: 5, statsPhase: 1, confirmed: [0, 0, 1, 1],
  })])).some(row => row.includes('unsupported original phase')));
  assert(resultsPadTraceFailures(makeTrace([
    makeTraceRow(0, {phase: 2, statsPhase: 1, confirmed: [0, 0, 1, 1]}),
    makeTraceRow(2, {phase: 3, statsPhase: 2, confirmed: [0, 0, 1, 1]}),
  ])).some(row => row.includes('out of source-frame order')));
});

test('Results PAD trace requires the two-connected/two-disconnected raw and copied PAD error profile', () => {
  const valid = makeTrace([makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})]);
  assert.deepEqual(resultsPadTraceFailures(valid), []);
  const rawDisconnectedHuman = structuredClone(valid);
  rawDisconnectedHuman.samples[0].pads[1].err = -1;
  assert(resultsPadTraceFailures(rawDisconnectedHuman).some(row => row.includes('raw PAD error profile')));
  const copiedDisconnectedHuman = structuredClone(valid);
  copiedDisconnectedHuman.samples[0].source_consumed_pads[0].err = -1;
  assert(resultsPadTraceFailures(copiedDisconnectedHuman).some(row => row.includes('copied PAD error profile')));
  const missingRawError = structuredClone(valid);
  delete missingRawError.samples[0].pads[2].err;
  assert(resultsPadTraceFailures(missingRawError).some(row => row.includes('lacks four raw PAD samples')));
  const missingCopiedError = structuredClone(valid);
  delete missingCopiedError.samples[0].source_consumed_pads[3].err;
  assert(resultsPadTraceFailures(missingCopiedError).some(row => row.includes('lacks four source-consumed PAD states')));
});

test('two-Human Results driver gates presentation and each port confirmation on copied source state', async () => {
  const samples = [makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})];
  const presses = [];
  let hostPhase = 8;
  const snapshot = () => makeTrace(samples);
  const append = (options) => samples.push(makeTraceRow(samples.length, options));
  const result = await confirmTwoHumanResults({
    deadlineAt: Date.now() + 5000,
    observeHost: async () => ({phase: hostPhase, running: 1, message: null}),
    observeTrace: async () => snapshot(),
    press: async key => {
      presses.push(key);
      if (presses.length === 1) {
        append({phase: 3, statsPhase: 1, confirmed: [0, 0, 1, 1], startPorts: [0]});
        append({phase: 3, statsPhase: 2, confirmed: [0, 0, 1, 1]});
      } else if (presses.length === 2) {
        append({phase: 3, statsPhase: 2, confirmed: [1, 0, 1, 1], startPorts: [0]});
      } else if (presses.length === 3) {
        append({phase: 4, statsPhase: 2, confirmed: [1, 1, 1, 1], startPorts: [1]});
        hostPhase = 1;
      }
    },
    wait: async () => {},
  });
  assert.deepEqual(presses, ['Enter', 'Enter', 'End']);
  assert.deepEqual(result.statistics_ready.confirmed, [0, 0, 1, 1]);
  assert.deepEqual(result.p1_confirmation.confirmed, [1, 0, 1, 1]);
  assert.deepEqual(result.p2_confirmation.confirmed, [1, 1, 1, 1]);
  assert.deepEqual(result.p1_confirmation.consumed_start_ports, [0]);
  assert.deepEqual(result.p2_confirmation.consumed_start_ports, [1]);
  assert.equal(result.original_exit.host_phase, 1);
});

test('Results driver fails on a misrouted P1 presentation Start and stops before statistics inputs', async () => {
  const samples = [makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})];
  const presses = [];
  await assert.rejects(confirmTwoHumanResults({
    deadlineAt: Date.now() + 1000,
    observeHost: async () => ({phase: 8, running: 1}),
    observeTrace: async () => makeTrace(samples),
    press: async key => {
      presses.push(key);
      samples.push(makeTraceRow(samples.length, {
        phase: 3, statsPhase: 1, confirmed: [0, 0, 1, 1], startPorts: [1],
      }));
    },
    wait: async () => {},
  }), /unexpected connected port consumed Start/);
  assert.deepEqual(presses, ['Enter']);
});

test('Results driver does not continue after the source exits before a required gate', async () => {
  const samples = [makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})];
  const presses = [];
  let hostPhase = 8;
  await assert.rejects(confirmTwoHumanResults({
    deadlineAt: Date.now() + 1000,
    observeHost: async () => ({phase: hostPhase, running: 1}),
    observeTrace: async () => makeTrace(samples),
    press: async key => {
      presses.push(key);
      samples.push(makeTraceRow(samples.length, {
        phase: 3, statsPhase: 1, confirmed: [0, 0, 1, 1], startPorts: [0],
      }));
      hostPhase = 1;
    },
    wait: async () => {},
  }), /exited before this confirmation boundary/);
  assert.deepEqual(presses, ['Enter']);
});
