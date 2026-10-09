import assert from 'node:assert/strict';
import test from 'node:test';
import {
  confirmTwoHumanResults,
  RESULTS_TRACE_CAPACITY,
  RESULTS_TRACE_START_MASK,
  RESULTS_CONNECTED_PAD_ERRORS,
  RESULTS_NEXT_SCENE_PREPARATION_MESSAGE,
  resultsPadTraceFailures,
} from './vs_rules_results_confirmation_driver.mjs';

const activeHost = (phase, message = phase === 8 ? 'Original Results' :
  phase === 9 ? 'Original unlock notification' : 'Original character select') => ({
  phase, running: 1, message, status: message, error: null,
  pause_present: true, pause_disabled: false,
});
const preparationHost = (phase, message, label = message) => ({
  phase, running: 0, message, status: `${label} · 25 ms · audio paused`, error: null,
  pause_present: true, pause_disabled: true,
});

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

test('Results PAD trace accepts authored 0-to-1-to-2 entry and rejects malformed phase/state progress', () => {
  const valid = makeTrace([
    makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(2, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ]);
  assert.deepEqual(resultsPadTraceFailures(valid), []);
  assert(resultsPadTraceFailures(null).length > 0);
  assert(resultsPadTraceFailures({...valid, overflow: true}).some(row => row.includes('overflow')));
  assert(resultsPadTraceFailures({...valid, attempts: 2}).some(row => row.includes('accounting')));
  assert(resultsPadTraceFailures({...valid, samples: [{...valid.samples[0], tick_returned: false}]})
    .some(row => row.includes('did not complete')));
  for (const phase of [-1, 5]) {
    assert(resultsPadTraceFailures(makeTrace([makeTraceRow(0, {
      phase, statsPhase: 0, confirmed: [0, 0, 0, 0],
    })])).some(row => row.includes('unsupported original phase')));
  }
  assert(resultsPadTraceFailures(makeTrace([
    makeTraceRow(0, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ])).some(row => row.includes('regressed the original Results phase')));
  assert(resultsPadTraceFailures(makeTrace([
    makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ])).some(row => row.includes('skipped an authored Results phase')));
  assert(resultsPadTraceFailures(makeTrace([
    makeTraceRow(0, {phase: 0, statsPhase: 2, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 1, statsPhase: 1, confirmed: [0, 0, 0, 0]}),
  ])).some(row => row.includes('regressed the original stats phase')));
  assert(resultsPadTraceFailures(makeTrace([makeTraceRow(0, {
    phase: 0, statsPhase: 3, confirmed: [0, 0, 0, 0],
  })])).some(row => row.includes('unsupported original stats phase')));
  assert(resultsPadTraceFailures(makeTrace([makeTraceRow(0, {
    phase: 0, statsPhase: 0, confirmed: [0, 2, 0, 0],
  })])).some(row => row.includes('four binary confirmation states')));
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
  const samples = [
    makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(2, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ];
  const presses = [];
  let hostPhase = 8;
  const snapshot = () => makeTrace(samples);
  const append = (options) => samples.push(makeTraceRow(samples.length, options));
  const result = await confirmTwoHumanResults({
    deadlineAt: Date.now() + 5000,
    observeHost: async () => activeHost(hostPhase),
    observeTrace: async () => snapshot(),
    press: async key => {
      presses.push(key);
      if (presses.length === 1) {
        append({phase: 3, statsPhase: 0, confirmed: [0, 0, 1, 1], startPorts: [0]});
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

test('Results waits for constructed/preparing owner readiness and an asynchronous P2 source edge', async () => {
  const samples = [
    makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(2, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ];
  const presses = [];
  const inputReadiness = [];
  const waits = [];
  let host = preparationHost(8, 'Original Results', 'Preparing original Results...');
  let p2Waits = 0;
  const append = options => samples.push(makeTraceRow(samples.length, options));
  const result = await confirmTwoHumanResults({
    deadlineAt: Date.now() + 5000,
    observeHost: async () => host,
    observeTrace: async () => makeTrace(samples),
    press: async key => {
      presses.push(key);
      inputReadiness.push(host.running);
      if (presses.length === 1) {
        append({phase: 3, statsPhase: 0, confirmed: [0, 0, 1, 1], startPorts: [0]});
        append({phase: 3, statsPhase: 2, confirmed: [0, 0, 1, 1]});
      } else if (presses.length === 2) {
        append({phase: 3, statsPhase: 2, confirmed: [1, 0, 1, 1], startPorts: [0]});
      } else if (presses.length === 3) {
        host = preparationHost(8, 'Preparing first-use rendering...');
      }
    },
    wait: async () => {
      waits.push(presses.length);
      if (presses.length === 0) {
        host = waits.length === 1
          ? preparationHost(8, 'Preparing first-use rendering...')
          : activeHost(8);
      } else if (presses.length === 3) {
        if (p2Waits++ === 0) {
          append({phase: 4, statsPhase: 2, confirmed: [1, 1, 1, 1], startPorts: [1]});
          host = preparationHost(5, RESULTS_NEXT_SCENE_PREPARATION_MESSAGE);
        } else {
          host = activeHost(1);
        }
      }
    },
  });
  assert.deepEqual(presses, ['Enter', 'Enter', 'End']);
  assert.deepEqual(inputReadiness, [1, 1, 1], 'no input is sent while Results or a destination is stopped');
  assert.equal(waits.filter(count => count === 0).length, 2,
    'constructed Results and first-use preparation are observed before any input');
  assert.deepEqual(result.p2_confirmation.consumed_start_ports, [1]);
  assert.equal(result.original_exit.host_phase, 1);
  assert.deepEqual(result.post_confirmation_preparation.map(row => [row.phase, row.reason]), [
    [8, 'first-use-render-settle'], [5, 'asset-transfer'],
  ]);
});

test('Results driver fails on a misrouted P1 presentation Start and stops before statistics inputs', async () => {
  const samples = [makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})];
  const presses = [];
  await assert.rejects(confirmTwoHumanResults({
    deadlineAt: Date.now() + 1000,
    observeHost: async () => activeHost(8),
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
    observeHost: async () => activeHost(hostPhase),
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

test('Results trace is retained before a rejected host observation', async () => {
  const order = [];
  let retainedTrace = null;
  const trace = makeTrace([makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})]);
  await assert.rejects(confirmTwoHumanResults({
    deadlineAt: Date.now() + 1000,
    observeTrace: async () => { order.push('trace'); retainedTrace = trace; return trace; },
    observeHost: async () => { order.push('host'); throw Error('checked host rejected stopped state'); },
    press: async () => assert.fail('host rejection occurs before an input action'),
    wait: async () => {},
  }), /checked host rejected stopped state/);
  assert.deepEqual(order, ['trace', 'host']);
  assert.equal(retainedTrace.retained, 1, 'the latest source ring remains available when host admission fails');
});

test('Results observer completion cannot accept a sample after the shared deadline', async () => {
  const originalNow = Date.now;
  let now = 100;
  const order = [];
  const trace = makeTrace([makeTraceRow(0, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]})]);
  Date.now = () => now;
  try {
    await assert.rejects(confirmTwoHumanResults({
      deadlineAt: 200,
      observeTrace: async () => { order.push('trace'); now = 150; return trace; },
      observeHost: async () => { order.push('host'); now = 201; return activeHost(8); },
      press: async () => assert.fail('expired observation must not issue input'),
      wait: async () => {},
    }), /exceeded its shared wall deadline during observation/);
    assert.deepEqual(order, ['trace', 'host']);
  } finally {
    Date.now = originalNow;
  }
});

test('Results next-scene phase 5 requires the actual P2 edge and waits for an active terminal route', async () => {
  const samples = [
    makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(1, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    makeTraceRow(2, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
  ];
  const order = [];
  const presses = [];
  let host = activeHost(8);
  const append = options => samples.push(makeTraceRow(samples.length, options));
  const result = await confirmTwoHumanResults({
    deadlineAt: Date.now() + 5000,
    observeTrace: async () => { order.push('trace'); return makeTrace(samples); },
    observeHost: async () => { order.push('host'); return host; },
    press: async key => {
      presses.push(key);
      if (presses.length === 1) {
        append({phase: 3, statsPhase: 0, confirmed: [0, 0, 1, 1], startPorts: [0]});
        append({phase: 3, statsPhase: 2, confirmed: [0, 0, 1, 1]});
      } else if (presses.length === 2) {
        append({phase: 3, statsPhase: 2, confirmed: [1, 0, 1, 1], startPorts: [0]});
      } else {
        append({phase: 4, statsPhase: 2, confirmed: [1, 1, 1, 1], startPorts: [1]});
        host = preparationHost(5, RESULTS_NEXT_SCENE_PREPARATION_MESSAGE);
      }
    },
    wait: async () => { host = activeHost(9); },
  });
  assert.deepEqual(presses, ['Enter', 'Enter', 'End']);
  assert.equal(result.p2_confirmation.phase, 4);
  assert.deepEqual(result.p2_confirmation.confirmed, [1, 1, 1, 1]);
  assert.equal(result.original_exit.host_phase, 9,
    'phase 5 is only an intermediate stopped transfer; the helper returns on actual Prize phase 9');
  assert.deepEqual(result.post_confirmation_preparation, [{
    phase: 5, reason: 'asset-transfer',
    preparation_label: RESULTS_NEXT_SCENE_PREPARATION_MESSAGE,
    message: RESULTS_NEXT_SCENE_PREPARATION_MESSAGE, observations: 1,
  }]);
  assert(order.length % 2 === 0);
  for (let index = 0; index < order.length; index += 2)
    assert.deepEqual(order.slice(index, index + 2), ['trace', 'host']);
});

test('Results phase 5 rejects a synthetic P1-only prefix and malformed P2 completion rows', async () => {
  const run = async ({p2Phase = 4, p2Stats = 2, p2Flags = [1, 1, 1, 1],
    finalFlags = p2Flags, rawP2Start = true, message = RESULTS_NEXT_SCENE_PREPARATION_MESSAGE,
    running = 0, p2Edge = true, hostError = null} = {}) => {
    const samples = [
      makeTraceRow(0, {phase: 0, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
      makeTraceRow(1, {phase: 1, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
      makeTraceRow(2, {phase: 2, statsPhase: 0, confirmed: [0, 0, 0, 0]}),
    ];
    const presses = [];
    let host = activeHost(8);
    const append = options => samples.push(makeTraceRow(samples.length, options));
    return confirmTwoHumanResults({
      deadlineAt: Date.now() + 3000,
      observeTrace: async () => makeTrace(samples),
      observeHost: async () => host,
      press: async key => {
        presses.push(key);
        if (presses.length === 1) {
          append({phase: 3, statsPhase: 0, confirmed: [0, 0, 1, 1], startPorts: [0]});
          append({phase: 3, statsPhase: 2, confirmed: [0, 0, 1, 1]});
      } else if (presses.length === 2) {
        append({phase: 3, statsPhase: 2, confirmed: [1, 0, 1, 1], startPorts: [0]});
      } else {
        if (p2Edge) {
          append({phase: p2Phase, statsPhase: p2Stats, confirmed: p2Flags, startPorts: [1]});
          if (!rawP2Start) samples.at(-1).pads[1].button = 0;
          if (finalFlags !== p2Flags)
            append({phase: 4, statsPhase: 2, confirmed: finalFlags});
        }
        host = {...preparationHost(5, message), running, error: hostError,
          pause_disabled: running === 0};
      }
      },
      wait: async () => { host = activeHost(1); },
    });
  };
  await assert.rejects(run({p2Edge: false}), /expected exactly one consumed source Start edge/,
    'a source-derived prefix without a P2 edge cannot authorize phase 5');
  for (const [label, options, pattern] of [
    ['wrong Results phase', {p2Phase: 3}, /source edge: source Results did not retain phase 4/],
    ['wrong stats phase', {p2Stats: 1}, /regressed the original stats phase/],
    ['missing P2 confirmation', {p2Flags: [1, 0, 1, 1]}, /source edge: source Results did not retain phase 4/],
    ['final row loses a confirmation', {finalFlags: [1, 0, 1, 1]}, /final retained row: source Results did not retain phase 4/],
    ['raw P2 Start missing', {rawP2Start: false}, /no matching raw PAD Start sample/],
    ['wrong preparation message', {message: 'Preparing original character select...'}, /not active CSS\/Prize/],
    ['preparation still running', {running: 1}, /not active CSS\/Prize/],
    ['host error during preparation', {hostError: 'native source error'}, /native runtime error/],
  ]) {
    await assert.rejects(run(options), pattern, label);
  }
});
