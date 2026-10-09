import assert from 'node:assert/strict';
import test from 'node:test';
import {
  COMPETITIVE_TIMEOUT_BOUNDS,
  competitiveTimeoutDeferredResultsFailures,
  competitiveTimeoutFirstLossFailures,
  competitiveTimeoutProgressFailures,
  competitiveTimeoutReadinessFailures,
  competitiveTimeoutStableFailures,
  competitiveTimeoutTerminalFailures,
  runtimeDiagnosticCounterFailures,
  runtimeDiagnosticsFailures,
  COMPETITIVE_PRIZE_MAX_CONFIRMATIONS,
  returnFromCompetitivePrize,
} from './vs_rules_timeout_route_helpers.mjs';
import {RESULTS_TRACE_CAPACITY, RESULTS_CONNECTED_PAD_ERRORS} from './vs_rules_results_confirmation_driver.mjs';

const completedResultsTrace = () => ({
  schema: 'melee-web-results-pad-trace-v1', attempts: 1, retained: 1,
  capacity: RESULTS_TRACE_CAPACITY, overflow: false,
  samples: [{source_frame: 0, tick_returned: true,
    pads: RESULTS_CONNECTED_PAD_ERRORS.map(err => ({button: 0, err})),
    source_consumed_pads: RESULTS_CONNECTED_PAD_ERRORS.map(err => ({trigger: 0, err})),
    results_state_after_tick: {source_frame: 1, phase: 4, stats_phase: 2,
      players: [1, 1, 1, 1].map(confirmed => ({confirmed}))}}],
});

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

test('Prize return sends Start only in phase 9 and remains inside the shared bounded route', async () => {
  assert.equal(COMPETITIVE_PRIZE_MAX_CONFIRMATIONS, 60);
  const presses = [];
  const states = [9, 9, 1];
  let observations = 0;
  const order = [];
  const result = await returnFromCompetitivePrize({
    deadlineAt: Date.now() + 5000,
    observeTrace: async () => { order.push('trace'); return completedResultsTrace(); },
    observeHost: async () => { order.push('host'); return {phase: states[observations++], running: 1}; },
    press: async (key, timing) => { presses.push({key, timing}); },
    wait: async () => {},
  });
  assert.deepEqual(result, {initial_phase: 9, final_phase: 1, prize_confirmations: 2,
    deferred_preparation: null,
    prize_source_exit_witness: 'unavailable: Prize transition is bounded by declared Enter input and checked host phases; no dedicated Prize PAD source trace is exported'});
  assert.equal(observations, 3, 'observe the initial Prize state and each post-input phase through the checked route callback');
  for (let index = 0; index < order.length; index += 2)
    assert.deepEqual(order.slice(index, index + 2), ['trace', 'host']);
  assert.deepEqual(presses, [
    {key: 'Enter', timing: {releaseMs: 380}},
    {key: 'Enter', timing: {releaseMs: 380}},
  ]);

  const alreadyCss = await returnFromCompetitivePrize({
    deadlineAt: Date.now() + 1000,
    observeHost: async () => ({phase: 1, running: 1}),
    observeTrace: async () => completedResultsTrace(),
    press: async () => assert.fail('CSS return must not send a Prize Start'),
    wait: async () => {},
  });
  assert.equal(alreadyCss.initial_phase, 1);
  assert.equal(alreadyCss.final_phase, 1);
  assert.equal(alreadyCss.prize_confirmations, 0);
});

test('Prize return rejects non-Prize phases and stops at the exact confirmation cap', async () => {
  let phase = 8;
  let presses = 0;
  await assert.rejects(returnFromCompetitivePrize({
    deadlineAt: Date.now() + 1000,
    observeHost: async () => ({phase, running: 1}),
    observeTrace: async () => completedResultsTrace(),
    press: async () => { presses++; },
    wait: async () => {},
  }), /Start is not allowed/);
  assert.equal(presses, 0);

  phase = 9;
  await assert.rejects(returnFromCompetitivePrize({
    deadlineAt: Date.now() + 45000,
    observeHost: async () => ({phase, running: 1}),
    observeTrace: async () => completedResultsTrace(),
    press: async () => { presses++; },
    wait: async () => {},
  }), /within 60 confirmations/);
  assert.equal(presses, COMPETITIVE_PRIZE_MAX_CONFIRMATIONS);

  await assert.rejects(returnFromCompetitivePrize({
    deadlineAt: Date.now() - 1,
    observeHost: async () => assert.fail('expired route must not poll the host'),
    observeTrace: async () => assert.fail('expired route must not read the retained trace'),
    press: async () => assert.fail('expired route must not send input'),
    wait: async () => assert.fail('expired route must not wait'),
  }), /live shared deadline/);
});

test('Prize phase 5 is admitted only after a declared Prize input and exact character-select preparation', async () => {
  const states = [
    {phase: 9, running: 1, message: 'Original unlock notification'},
    {phase: 5, running: 0, message: 'Preparing original character select...'},
    {phase: 5, running: 0, message: 'Preparing original character select...'},
    {phase: 1, running: 1, message: 'Original character select'},
  ];
  let observation = 0;
  let traceReads = 0;
  let waitCount = 0;
  const presses = [];
  const result = await returnFromCompetitivePrize({
    deadlineAt: Date.now() + 5000,
    observeTrace: async () => { traceReads++; return completedResultsTrace(); },
    observeHost: async () => states[observation++],
    press: async (key, timing) => presses.push({key, timing}),
    wait: async () => { waitCount++; },
  });
  assert.deepEqual(presses, [{key: 'Enter', timing: {releaseMs: 380}}]);
  assert.equal(traceReads, 4, 'the retained Results ring is read before each host status check');
  assert.equal(waitCount, 2);
  assert.equal(result.final_phase, 1, 'the route only completes on observed active CSS');
  assert.deepEqual(result.deferred_preparation, {
    phase: 5, running: 0, message: 'Preparing original character select...', after_confirmation: 1,
  });
  assert.match(result.prize_source_exit_witness, /no dedicated Prize PAD source trace/);
});

test('Prize route rejects phase 5 without prior Prize input, with wrong preparation message, or with invalid retained Results trace', async () => {
  const common = {
    deadlineAt: Date.now() + 3000,
    observeTrace: async () => completedResultsTrace(),
    press: async () => {},
    wait: async () => {},
  };
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => ({phase: 5, running: 0, message: 'Preparing original character select...'}),
  }), /invalid host running state/);

  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => ({phase: 9, running: 0, message: 'Original unlock notification'}),
  }), /Prize must be active before another confirmation/);

  let phaseNineReads = 0;
  let phaseNinePresses = 0;
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => phaseNineReads++ === 0
      ? {phase: 9, running: 1, message: 'Original unlock notification'}
      : {phase: 9, running: 0, message: 'Original unlock notification'},
    press: async () => { phaseNinePresses++; },
  }), /Prize must be active before another confirmation/);
  assert.equal(phaseNinePresses, 1, 'a stopped Prize never receives a repeated Enter');

  let afterPress = false;
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => afterPress
      ? {phase: 5, running: 0, message: 'Preparing original next scene...'}
      : {phase: 9, running: 1, message: 'Original unlock notification'},
    press: async () => { afterPress = true; },
  }), /phase 5 is admitted only/);

  await assert.rejects(returnFromCompetitivePrize({...common,
    observeTrace: async () => ({schema: 'missing'}),
    observeHost: async () => assert.fail('invalid retained trace must be rejected before host read'),
  }), /retained original Results trace is invalid/);
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

test('source HUD readiness is active only before the original match ending/completion', () => {
  assert.deepEqual(competitiveTimeoutReadinessFailures({ready: true, ending: false, complete: false}), []);
  assert.deepEqual(competitiveTimeoutReadinessFailures({ready: false, ending: true, complete: true}), []);
  assert.deepEqual(competitiveTimeoutReadinessFailures({ready: false, ending: false, complete: true}), []);
  assert(competitiveTimeoutReadinessFailures({ready: false, ending: false, complete: false})
    .some(row => row.includes('enabled while the match is active')));
  assert(competitiveTimeoutReadinessFailures({ready: true, ending: true, complete: false})
    .some(row => row.includes('disabled after match ending')));
  assert(competitiveTimeoutReadinessFailures({ready: 0, ending: true, complete: true})
    .some(row => row.includes('must remain a boolean')));
  assert(competitiveTimeoutReadinessFailures({ready: false, ending: 1, complete: true})
    .some(row => row.includes('ending/completion fields')));
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
    ['HUD remains enabled after terminal', {...snapshot, ready: true}],
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
