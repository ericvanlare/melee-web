import assert from 'node:assert/strict';
import {accountLongTasks, timingFailures} from '../scripts/versus_sequence_timing.mjs';

function metrics(overrides = {}) {
  const base = {
    schema: 'melee-web-menu-sequence-metrics',
    sequence: {started_ms: 0},
    source: {steps: 10, draws: 10},
    native: {
      callbacks: {total: 4, live: 4, preparation: 0},
      budget: {total: 0},
      over_33ms: {total: 0},
    },
    browser: {
      callbacks: 4,
      gaps_over_33ms: 0,
      longtasks: {supported: true, count: 0, records: []},
    },
    pipelines: {queued: 0, created: 0},
    interval: {audio_underruns: 0, audio_overflows: 0},
    scenes: {entries: []},
    state: {
      fatal: false, timing_invalid: false, preparation_active: false, running: true,
      focus: {sequence_lost: false, hidden: false, has_focus: true},
    },
  };
  return {
    ...base, ...overrides,
    sequence: {...base.sequence, ...overrides.sequence},
    source: {...base.source, ...overrides.source},
    native: {...base.native, ...overrides.native,
      callbacks: {...base.native.callbacks, ...overrides.native?.callbacks},
      budget: {...base.native.budget, ...overrides.native?.budget},
      over_33ms: {...base.native.over_33ms, ...overrides.native?.over_33ms}},
    browser: {...base.browser, ...overrides.browser,
      longtasks: {...base.browser.longtasks, ...overrides.browser?.longtasks}},
    pipelines: {...base.pipelines, ...overrides.pipelines},
    interval: {...base.interval, ...overrides.interval},
    scenes: {...base.scenes, ...overrides.scenes},
    state: {...base.state, ...overrides.state,
      focus: {...base.state.focus, ...overrides.state?.focus}},
  };
}

const noErrors = {errors: []};

const clean = timingFailures(metrics(), noErrors);
assert.deepEqual(clean, [], 'a supported zero-baseline interval should pass');

const split = accountLongTasks([
  {started: 12, duration_ms: 4, name: 'inside-first'},
  {started: 25, duration_ms: 10, name: 'crosses-gap'},
  {started: 35, duration_ms: 3, name: 'inside-second'},
], [{requested_at: 10, preparation_done: 20}, {requested_at: 30, preparation_done: 40}]);
assert.equal(split.raw_records.length, 3, 'raw long tasks are retained');
assert.deepEqual(split.preparation_intervals, [[10, 20], [30, 40]]);
assert.equal(split.excluded_count, 2);
assert.equal(split.remaining.length, 1);
assert.equal(split.remaining[0].record.name, 'crosses-gap');

const boundary = accountLongTasks([
  {started: 8, duration_ms: 4, name: 'crosses-preparation-start'},
  {started: 20, duration_ms: 4, name: 'crosses-preparation-end'},
  {started: 12, duration_ms: 4, name: 'inside'},
], [{requested_at: 10, preparation_done: 20}]);
assert.equal(boundary.excluded_count, 1);
assert.deepEqual(boundary.remaining.map(item => item.record.name), [
  'crosses-preparation-start', 'crosses-preparation-end']);

const clipped = accountLongTasks([
  {started: -10, duration_ms: 5, name: 'before-sequence'},
  {started: -2, duration_ms: 5, name: 'crosses-sequence-start'},
], [], {sequenceStart: 0});
assert.equal(clipped.records.length, 1);
assert.equal(clipped.records[0].duration_ms, 3);

const crossingFailure = timingFailures(metrics({
  browser: {longtasks: {count: 1, records: [{started: 5, duration_ms: 20, name: 'crossing'}]}},
  scenes: {entries: [{requested_at: 10, preparation_done: 20}]},
}), noErrors);
assert(crossingFailure.some(value => value.includes('live browser long tasks')));

const unsupported = timingFailures(metrics({
  browser: {longtasks: {supported: false, records: []}},
}), noErrors);
assert(unsupported.some(value => value.includes('capability unsupported')));
const missingCapability = timingFailures(metrics({
  browser: {longtasks: {supported: null, capability: null, records: []}}, diagnostic_capture: undefined,
}), noErrors);
assert(missingCapability.some(value => value.includes('capability missing')));
const loafUnsupported = timingFailures(metrics({
  diagnostic_capture: {capabilities: {longtask: {supported: true}, loaf: {supported: false}}},
}), noErrors);
assert.deepEqual(loafUnsupported, [], 'unsupported LoAF does not fail a long-task interval');

const missingRecords = timingFailures(metrics({
  browser: {longtasks: {records: undefined}},
}), noErrors);
assert(missingRecords.some(value => value.includes('long-task records missing')));
const mismatchedCount = timingFailures(metrics({
  browser: {longtasks: {count: 0, records: [{started: 1, duration_ms: 1}]}},
}), noErrors);
assert(mismatchedCount.some(value => value.includes('record count mismatch')));
const overflow = timingFailures(metrics({
  browser: {longtasks: {overflow: true, count: 1, records: []}},
}), noErrors);
assert(overflow.some(value => value.includes('record overflow')));
assert.equal(accountLongTasks([{started: '0', duration_ms: 1}], []).invalid_count, 1,
  'non-numeric wall times are not coerced into timing boundaries');

const browserError = timingFailures(metrics(), {errors: [{kind: 'console', message: 'GPU validation'}]});
assert(browserError.some(value => value.includes('browser diagnostics errors: 1')));
assert.throws(() => timingFailures(metrics(), {errors: 'not-an-array'}), /errors must be an array/);

const missingCounts = timingFailures(metrics({
  source: {steps: 0, draws: 0},
  native: {callbacks: {total: 0}},
  browser: {callbacks: 0},
}), noErrors);
assert(missingCounts.some(value => value.startsWith('source steps')));
assert(missingCounts.some(value => value.startsWith('source draws')));
assert(missingCounts.some(value => value.startsWith('native callbacks')));
assert(missingCounts.some(value => value.startsWith('browser callbacks')));

const nativeGate = timingFailures(metrics({
  native: {budget: {total: 1}, over_33ms: {total: 0}},
}), noErrors);
assert(nativeGate.some(value => value.includes('native callback target misses')),
  'native total remains the active-duration gate even when live is zero');

for (const [field, value] of [
  ['fatal', true], ['timing_invalid', true], ['preparation_active', true],
]) {
  const failures = timingFailures(metrics({state: {[field]: value}}), noErrors);
  assert(failures.some(item => item.includes(field === 'timing_invalid' ? 'invalid timing' :
    field === 'preparation_active' ? 'unfinished preparation' : 'fatal runtime')));
}

console.log('Versus sequence timing accounting boundaries passed');
