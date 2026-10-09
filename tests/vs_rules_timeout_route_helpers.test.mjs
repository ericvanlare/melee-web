import assert from 'node:assert/strict';
import test from 'node:test';
import {
  COMPETITIVE_TIMEOUT_BOUNDS,
  competitiveTimeoutFirstLossFailures,
  competitiveTimeoutProgressFailures,
  competitiveTimeoutStableFailures,
  competitiveTimeoutTerminalFailures,
  runtimeDiagnosticCounterFailures,
  runtimeDiagnosticsFailures,
} from './vs_rules_timeout_route_helpers.mjs';

const match = ({frame = 180, stocks = [4, 4], paused = false, ending = false,
  complete = false, observerError = false} = {}) => ({
  ready: true, frame, paused, ending, complete, observer_error: observerError,
  players: stocks.map(value => ({stocks: value})),
});

test('timeout input and wait bounds stay finite and source-tick based', () => {
  assert.deepEqual(COMPETITIVE_TIMEOUT_BOUNDS, {
    stockLossWallMs: 120000, noSourceProgressWallMs: 15000,
    resultsTransitionWallMs: 30000, resultsReturnWallMs: 45000, gameplayWallMs: 660000,
    outwardPulseFrames: 12, maximumOutwardPulses: 50,
    stableNeutralFrames: 120, snapshotLimit: 12, snapshotPeriodFrames: 3600,
  });
  assert.equal(COMPETITIVE_TIMEOUT_BOUNDS.outwardPulseFrames *
    COMPETITIVE_TIMEOUT_BOUNDS.maximumOutwardPulses, 600);
});

test('stock-loss policy accepts only the first P1 decrement with P2 unchanged', () => {
  assert.deepEqual(competitiveTimeoutFirstLossFailures(
    match({frame: 200}), match({frame: 212, stocks: [3, 4]})), []);
  assert(competitiveTimeoutFirstLossFailures(
    match({frame: 200}), match({frame: 212, stocks: [2, 4]})).some(row => row.includes('[3,4]')));
  assert(competitiveTimeoutFirstLossFailures(
    match({frame: 200}), match({frame: 212, stocks: [3, 3]})).some(row => row.includes('[3,4]')));
  assert(competitiveTimeoutProgressFailures(match({stocks: [4, 3]})).length > 0);
  assert(competitiveTimeoutProgressFailures(match({paused: true})).some(row => row.includes('not paused')));
  assert(competitiveTimeoutProgressFailures(match({observerError: true})).some(row => row.includes('observer')));
});

test('neutral interval requires 120 advancing source frames with exact [3,4] stocks', () => {
  assert.deepEqual(competitiveTimeoutStableFailures(match({frame: 400, stocks: [3, 4]}), 280), []);
  assert(competitiveTimeoutStableFailures(match({frame: 399, stocks: [3, 4]}), 280)
    .some(row => row.includes('120 source frames')));
  assert(competitiveTimeoutStableFailures(match({frame: 400, stocks: [3, 3]}), 280)
    .some(row => row.includes('[3,4]')));
});

test('terminal acceptance uses the original timeout result and unique source winner', () => {
  const terminal = {terminal: {outcome: 1, winners: [1]}, rules: {time_limit: 480, match_kind: 1}};
  assert.deepEqual(competitiveTimeoutTerminalFailures(terminal), []);
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 2, winners: [1]}}).some(row => row.includes('timeout outcome')));
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 1, winners: [0, 1]}}).some(row => row.includes('winner count')));
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 1, winners: [0]}}).some(row => row.includes('unique winner is P2')));
});

const identity = {scenario: 'rules-timeout-route-v1'};
const identityScope = {phases: [7, 8], evidence: 'bounded callbacks'};
const validCapture = () => ({
  schema: 'melee-web-runtime-callback-capture-v1', status: 'installed',
  identity, identity_scope: identityScope, max_samples: 100, max_incidents: 24,
  samples: Array(100).fill({at_ms: 1, source_frame: 1, scene: 7, source_steps: 1,
    source_draws: 1, callback_ms: 1, running: true}), dropped_samples: 5,
  callback_count: 105, dropped_incidents: 0,
  phase_source_steps: Array.from({length: 16}, (_, index) => index === 7 || index === 8 ? 10 : 0),
  invalid_phase_steps: 0, reason_counts: [0, 0, 0, 0, 0, 0, 0, 1, 0, 0],
  unknown_reason_count: 0, dropped_reason_counts: Array(10).fill(0),
  dropped_unknown_reason_count: 0, invalid_preparation_count: 0,
  dropped_invalid_preparation_count: 0,
  incidents: [{at_ms: 1, callback_ms: 1, reason_code: 7, value: 0, threshold: 0,
    clock_owner_code: 0, source_frame: 210, scene: 7}],
});

test('diagnostic policy allows sample thinning but accounts lossless preparation incidents', () => {
  assert.deepEqual(runtimeDiagnosticsFailures(validCapture(), {identity, identityScope}), []);
});

test('diagnostic policy rejects missing, malformed, non-preparation, or lost incident evidence', () => {
  assert(runtimeDiagnosticsFailures({status: 'unavailable'}, {identity, identityScope}).length > 0);
  const invalidPhase = validCapture();
  invalidPhase.invalid_phase_steps = 1;
  assert(runtimeDiagnosticsFailures(invalidPhase, {identity, identityScope}).some(row => row.includes('invalid publication-phase')));
  const runtimeError = validCapture();
  runtimeError.reason_counts[3] = 1;
  runtimeError.incidents.push({reason_code: 3});
  assert(runtimeDiagnosticsFailures(runtimeError, {identity, identityScope}).some(row => row.includes('non-preparation')));
  const dropped = validCapture();
  dropped.dropped_incidents = 1;
  dropped.dropped_reason_counts[7] = 1;
  assert(runtimeDiagnosticsFailures(dropped, {identity, identityScope}).some(row => row.includes('lost 1 records')));
  const badRow = validCapture();
  badRow.incidents[0].threshold = 1;
  assert(runtimeDiagnosticsFailures(badRow, {identity, identityScope}).some(row => row.includes('not a valid')));
  const badSample = validCapture();
  badSample.samples[0].scene = null;
  assert(runtimeDiagnosticsFailures(badSample, {identity, identityScope}).some(row => row.includes('sample 0 is malformed')));
  const badAccounting = validCapture();
  badAccounting.callback_count++;
  assert(runtimeDiagnosticsFailures(badAccounting, {identity, identityScope}).some(row => row.includes('conserve all callbacks')));
});

test('live recorder counter polling stops on invalid observations and runtime incidents', () => {
  const counters = {status: 'installed', callback_count: 4,
    phase_source_steps: Array(16).fill(0), invalid_phase_steps: 0,
    reason_counts: Array(10).fill(0), unknown_reason_count: 0, invalid_preparation_count: 0};
  assert.deepEqual(runtimeDiagnosticCounterFailures(counters), []);
  assert(runtimeDiagnosticCounterFailures({...counters, reason_counts: [0, 1, 0, 0, 0, 0, 0, 0, 0, 0]})
    .some(row => row.includes('non-preparation')));
  const fullIncidentRing = Array(10).fill(0);
  fullIncidentRing[7] = 24;
  assert.deepEqual(runtimeDiagnosticCounterFailures({...counters, reason_counts: fullIncidentRing}), []);
  const overflowingIncidentRing = [...fullIncidentRing];
  overflowingIncidentRing[7] = 25;
  assert(runtimeDiagnosticCounterFailures({...counters, reason_counts: overflowingIncidentRing})
    .some(row => row.includes('exceeds the lossless 24-row ring')));
  assert(runtimeDiagnosticCounterFailures({...counters, invalid_phase_steps: 1})
    .some(row => row.includes('invalid publication-phase')));
  assert(runtimeDiagnosticCounterFailures({...counters, status: 'unavailable'})
    .some(row => row.includes('status')));
});
