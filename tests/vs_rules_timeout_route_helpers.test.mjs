import assert from 'node:assert/strict';
import test from 'node:test';
import {
  COMPETITIVE_TIMEOUT_BOUNDS,
  competitiveTimeoutDeferredResultsFailures,
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
  players: stocks.map(value => ({stocks: value})), rules: {player_stocks: [4, 4]},
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
  const changedSetup = match({stocks: [3, 4]});
  changedSetup.rules.player_stocks = [3, 4];
  assert(competitiveTimeoutProgressFailures(changedSetup).some(row => row.includes('setup stocks')));
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
  const terminal = {terminal: {outcome: 1, winners: [1]},
    rules: {time_limit: 480, match_kind: 1, player_stocks: [4, 4]}};
  assert.deepEqual(competitiveTimeoutTerminalFailures(terminal), []);
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 2, winners: [1]}}).some(row => row.includes('timeout outcome')));
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 1, winners: [0, 1]}}).some(row => row.includes('winner count')));
  assert(competitiveTimeoutTerminalFailures({...terminal,
    terminal: {outcome: 1, winners: [0]}}).some(row => row.includes('unique winner is P2')));
  assert(competitiveTimeoutTerminalFailures({...terminal,
    rules: {...terminal.rules, player_stocks: [3, 4]}}).some(row => row.includes('setup stocks')));
});

test('phase-5 Results preparation requires the exact retained terminal and unchanged normalized rules', () => {
  const rules = {match_kind: 1, stage: 0x20, timer_enabled: 1, time_limit: 480,
    disable_pausing: 0, damage_ratio_bits: '3f800000', item_frequency: -1,
    item_mask_hex: 'fffffff80000000f', is_teams: 0, player_teams: [0, 0],
    player_stocks: [4, 4], door_teams: [0, 0, 0, 0], friendly_fire: 1,
    player_source_slots: [0, 0], resolved_controller_ports: [0, 1],
    player_slot_types: [0, 0], player_source_stocks: [4, 4],
    player_source_characters: [0, 0], player_attack_ratio_bits: ['3f800000', '3f800000'],
    player_defense_ratio_bits: ['3f800000', '3f800000']};
  const snapshot = {ready: false, paused: false, ending: false, complete: true,
    frame: 28800, rules: structuredClone(rules), players: [{stocks: 3}, {stocks: 4}],
    terminal: {outcome: 1, winners: [1]}};
  assert.deepEqual(competitiveTimeoutDeferredResultsFailures({phase: 5, running: 0}, snapshot, rules), []);
  assert(competitiveTimeoutDeferredResultsFailures({phase: 4, running: 0}, snapshot, rules)
    .some(row => row.includes('asset phase')));
  assert(competitiveTimeoutDeferredResultsFailures({phase: 5, running: false}, snapshot, rules)
    .some(row => row.includes('stopped')));

  for (const [label, changed] of [
    ['missing terminal', {...snapshot, terminal: undefined}],
    ['wrong outcome', {...snapshot, terminal: {outcome: 2, winners: [1]}}],
    ['wrong winner', {...snapshot, terminal: {outcome: 1, winners: [0]}}],
    ['wrong stocks', {...snapshot, players: [{stocks: 3}, {stocks: 3}]}],
    ['not terminal', {...snapshot, ending: false, complete: false}],
    ['untyped HUD readiness', {...snapshot, ready: 0}],
    ['paused', {...snapshot, paused: true}],
    ['changed setup', {...snapshot, rules: {...rules, player_stocks: [3, 4]}}],
    ['changed rule', {...snapshot, rules: {...rules, item_frequency: 0}}],
  ]) {
    assert(competitiveTimeoutDeferredResultsFailures({phase: 5, running: 0}, changed, rules).length > 0, label);
  }
  assert(competitiveTimeoutDeferredResultsFailures({phase: 5, running: 0}, {observer_error: true}, rules)
    .some(row => row.includes('timeout outcome')));
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
