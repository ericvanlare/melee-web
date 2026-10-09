import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {
  classifyPrizeReturnReadiness,
  classifyResultsDestinationReadiness,
  classifyResultsOwnerReadiness,
  resultsPadTraceFailures,
  RESULTS_TRACE_START_MASK,
} from './vs_rules_results_confirmation_driver.mjs';
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

const activeHost = (phase, message = phase === 8 ? 'Original Results' :
  phase === 9 ? 'Original unlock notification' : 'Original character select') => ({
  phase, running: 1, message, status: message, error: null,
  pause_present: true, pause_disabled: false,
});
const preparationHost = (phase, message, label = message) => ({
  phase, running: 0, message, status: `${label} · 25 ms · audio paused`, error: null,
  pause_present: true, pause_disabled: true,
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

test('finite source-backed Results and destination readiness matrix requires the live pause control', () => {
  for (const host of [
    activeHost(8),
    preparationHost(8, 'Preparing original Results...'),
    preparationHost(8, 'Original Results', 'Preparing original Results...'),
    preparationHost(8, 'Preparing first-use rendering...'),
  ]) assert.notEqual(classifyResultsOwnerReadiness(host).kind, 'invalid', JSON.stringify(host));
  assert.equal(classifyResultsOwnerReadiness(
    preparationHost(8, 'Original Results', 'Preparing original Results...')).reason,
  'constructed-results-preparation');
  assert.equal(classifyResultsOwnerReadiness(
    preparationHost(8, 'Original Results', 'Preparing original match continuation...')).reason,
  'constructed-results-preparation', 'disc or preparation display does not change native constructed-owner classification');
  for (const host of [
    preparationHost(8, 'Paused.'),
    {...activeHost(8), pause_present: false},
    {...activeHost(8), pause_disabled: true},
    {...preparationHost(8, 'Preparing original Results...'), pause_disabled: false},
    {...activeHost(8), error: 'native failure'},
  ]) assert.equal(classifyResultsOwnerReadiness(host).kind, 'invalid', JSON.stringify(host));

  for (const host of [
    activeHost(1), activeHost(9),
    preparationHost(5, 'Preparing original next scene...'),
    preparationHost(1, 'Original character select', 'Preparing original next scene...'),
    preparationHost(9, 'Original unlock notification', 'Preparing original next scene...'),
    preparationHost(1, 'Preparing first-use rendering...'),
    preparationHost(9, 'Preparing first-use rendering...'),
  ]) assert.notEqual(classifyResultsDestinationReadiness(host).kind, 'invalid', JSON.stringify(host));
  assert.equal(classifyResultsDestinationReadiness(
    preparationHost(9, 'Preparing first-use rendering...')).reason,
  'prize-first-use-render-settle');
  for (const host of [
    preparationHost(5, 'Preparing original character select...'),
    {...preparationHost(1, 'Original character select', 'Preparing original next scene...'), pause_present: false},
    {...activeHost(1), pause_disabled: true},
  ]) assert.equal(classifyResultsDestinationReadiness(host).kind, 'invalid', JSON.stringify(host));

  for (const host of [
    activeHost(1), activeHost(9),
    preparationHost(5, 'Preparing original character select...'),
    preparationHost(1, 'Original character select', 'Preparing original character select...'),
    preparationHost(1, 'Preparing first-use rendering...'),
    preparationHost(9, 'Preparing first-use rendering...'),
  ]) assert.notEqual(classifyPrizeReturnReadiness(host).kind, 'invalid', JSON.stringify(host));
  assert.equal(classifyPrizeReturnReadiness(
    preparationHost(9, 'Preparing first-use rendering...')).reason,
  'prize-first-use-render-settle');
  for (const host of [
    preparationHost(5, 'Preparing original next scene...'),
    {...activeHost(1), pause_present: false},
    {...activeHost(1), pause_disabled: true},
    {...preparationHost(1, 'Original character select', 'Preparing original character select...'), pause_disabled: false},
  ]) assert.equal(classifyPrizeReturnReadiness(host).kind, 'invalid', JSON.stringify(host));
});

test('Prize return sends Start only in phase 9 and remains inside the shared bounded route', async () => {
  assert.equal(COMPETITIVE_PRIZE_MAX_CONFIRMATIONS, 60);
  const presses = [];
  const states = [activeHost(9), activeHost(9), activeHost(1)];
  let observations = 0;
  const order = [];
  const result = await returnFromCompetitivePrize({
    deadlineAt: Date.now() + 5000,
    observeTrace: async () => { order.push('trace'); return completedResultsTrace(); },
    observeHost: async () => { order.push('host'); return states[observations++]; },
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
    observeHost: async () => activeHost(1),
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
    observeHost: async () => activeHost(phase),
    observeTrace: async () => completedResultsTrace(),
    press: async () => { presses++; },
    wait: async () => {},
  }), /unsupported host state/);
  assert.equal(presses, 0);

  phase = 9;
  await assert.rejects(returnFromCompetitivePrize({
    deadlineAt: Date.now() + 45000,
    observeHost: async () => activeHost(phase),
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

test('Prize return waits through Prize render settle, CSS transfer, construction and first-use before active CSS', async () => {
  const states = [
    activeHost(9),
    preparationHost(9, 'Preparing first-use rendering...'),
    activeHost(9),
    preparationHost(5, 'Preparing original character select...'),
    preparationHost(1, 'Original character select', 'Preparing original character select...'),
    preparationHost(1, 'Preparing first-use rendering...'),
    activeHost(1),
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
  assert.deepEqual(presses, [
    {key: 'Enter', timing: {releaseMs: 380}},
    {key: 'Enter', timing: {releaseMs: 380}},
  ]);
  assert.equal(traceReads, 7, 'the retained Results ring is read before each host readiness check');
  assert.equal(waitCount, 4);
  assert.equal(result.final_phase, 1, 'the route only completes on observed active CSS');
  assert.deepEqual(result.deferred_preparation, [
    {phase: 9, reason: 'prize-first-use-render-settle',
      message: 'Preparing first-use rendering...', status: 'Preparing first-use rendering... · 25 ms · audio paused', after_confirmation: 1, observations: 1},
    {phase: 5, reason: 'asset-transfer',
      message: 'Preparing original character select...', status: 'Preparing original character select... · 25 ms · audio paused', after_confirmation: 2, observations: 1},
    {phase: 1, reason: 'destination-construction',
      message: 'Original character select', status: 'Preparing original character select... · 25 ms · audio paused', after_confirmation: 2, observations: 1},
    {phase: 1, reason: 'first-use-render-settle',
      message: 'Preparing first-use rendering...', status: 'Preparing first-use rendering... · 25 ms · audio paused', after_confirmation: 2, observations: 1},
  ]);
  assert.match(result.prize_source_exit_witness, /no dedicated Prize PAD source trace/);
});

test('Prize route rejects phase 5 without prior Prize input, with wrong preparation message, or with invalid retained Results trace', async () => {
  const common = {
    deadlineAt: Date.now() + 45000,
    observeTrace: async () => completedResultsTrace(),
    press: async () => {},
    wait: async () => {},
  };
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => preparationHost(5, 'Preparing original character select...'),
  }), /not active CSS\/Prize or an exact Results-destination preparation state/);

  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => preparationHost(9, 'Paused.'),
  }), /not active CSS\/Prize or an exact Results-destination preparation state/);

  let phaseNineReads = 0;
  let phaseNinePresses = 0;
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => {
      const index = phaseNineReads++;
      return index === 0 ? activeHost(9) : index === 1
        ? preparationHost(9, 'Preparing first-use rendering...') : activeHost(9);
    },
    press: async () => { phaseNinePresses++; },
  }), /within 60 confirmations/,
  'a Prize first-use settle waits for the active Prize owner instead of inputting while stopped');
  assert.equal(phaseNinePresses, COMPETITIVE_PRIZE_MAX_CONFIRMATIONS,
    'only the active Prize owner receives bounded Enter confirmations');

  const afterCssCommit = [
    activeHost(9),
    preparationHost(5, 'Preparing original character select...'),
    preparationHost(9, 'Preparing first-use rendering...'),
    activeHost(9),
  ];
  let afterCssIndex = 0;
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => afterCssCommit[afterCssIndex++],
    press: async () => {},
  }), /CSS transfer entered an unexpected Prize preparation state/,
  'a Prize render settle is valid only before CSS transfer has committed');

  let afterPress = false;
  await assert.rejects(returnFromCompetitivePrize({...common,
    observeHost: async () => afterPress
      ? preparationHost(5, 'Preparing original next scene...')
      : activeHost(9),
    press: async () => { afterPress = true; },
  }), /not active CSS\/Prize or an exact Prize-return preparation state/);

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

const retainedV5TracePath = process.env.MELEE_SD_V5_RESULTS_TRACE;
const retainedV6TracePath = process.env.MELEE_SD_V6_RESULTS_TRACE;
const retainedV6ReportPath = process.env.MELEE_SD_V6_BROWSER_REPORT;
test('retained v5/v6 Results traces and v6 stopped-CSS host projection preserve the readiness boundary', {
  skip: !retainedV5TracePath || !retainedV6TracePath || !retainedV6ReportPath,
}, async () => {
  const [v5Bytes, v6Bytes, reportBytes] = await Promise.all([
    readFile(retainedV5TracePath), readFile(retainedV6TracePath), readFile(retainedV6ReportPath),
  ]);
  assert.equal(createHash('sha256').update(v5Bytes).digest('hex'),
    'fa2a6f48e5dfc520bae1bb7306a96ccb86eab0747bc155e85901587d748cfb57');
  assert.equal(createHash('sha256').update(v6Bytes).digest('hex'),
    '904bda8fc4a43041bb8ec2d4c65abd70ff4cf4ce52f44b55d7d35b4879697ba3');
  assert.equal(createHash('sha256').update(reportBytes).digest('hex'),
    '548372db6963970c98cddd767db42817460a42ed5bce36d445113b43cf16b537');
  const v5 = JSON.parse(v5Bytes.toString('utf8'));
  const v6 = JSON.parse(v6Bytes.toString('utf8'));
  const report = JSON.parse(reportBytes.toString('utf8'));
  assert.deepEqual(resultsPadTraceFailures(v5), []);
  assert.deepEqual(resultsPadTraceFailures(v6), []);
  assert.equal(v5.retained, 271);
  assert.equal(v6.retained, 288);
  assert.equal(v6.attempts, 288);
  assert.equal(v6.overflow, false);

  const starts = (trace, port) => trace.samples.flatMap((row, index) =>
    row.source_consumed_pads[port].trigger & RESULTS_TRACE_START_MASK ? [index] : []);
  assert.deepEqual(starts(v5, 1), [], 'retained v5 is an actual no-P2-completion negative');
  assert.deepEqual(v5.samples.at(-1).results_state_after_tick,
    {source_frame: 271, phase: 3, stats_phase: 2, num_pages: 3,
      players: [{page: 0, confirmed: 1}, {page: 0, confirmed: 0},
        {page: 0, confirmed: 1}, {page: 0, confirmed: 1}]});
  assert.deepEqual(starts(v6, 1), [276]);
  const p2Row = v6.samples[276];
  assert.equal(p2Row.pads[1].button & RESULTS_TRACE_START_MASK, RESULTS_TRACE_START_MASK);
  assert.equal(p2Row.source_consumed_pads[1].trigger & RESULTS_TRACE_START_MASK, RESULTS_TRACE_START_MASK);
  assert.deepEqual(p2Row.results_state_after_tick,
    {source_frame: 277, phase: 4, stats_phase: 2, num_pages: 3,
      players: [1, 1, 1, 1].map(confirmed => ({page: 0, confirmed}))});
  const {source_frame: finalFrame, ...finalState} = v6.samples.at(-1).results_state_after_tick;
  const {source_frame: p2Frame, ...p2State} = p2Row.results_state_after_tick;
  assert(finalFrame > p2Frame, 'captured retained trace continued after the P2 edge');
  assert.deepEqual(finalState, p2State,
    'all later captured Results rows preserve the authored phase, stats and four confirmation flags');

  const capturedHost = report.failure?.state;
  assert.deepEqual({phase: capturedHost?.phase, running: capturedHost?.running,
    message: capturedHost?.message, status: capturedHost?.status, error: capturedHost?.error}, {
    phase: 1, running: 0, message: 'Original character select',
    status: 'Preparing original next scene... · 493 ms · audio paused', error: null,
  });
  assert.equal(Object.hasOwn(capturedHost, 'pause_present'), false,
    'the actual v6 report did not retain pause-control state');

  // Only this test projection adds the unrecorded pause-control fields and
  // post-failure completion states; those fields are synthetic, not v6 output.
  const syntheticStoppedCss = {...capturedHost, pause_present: true, pause_disabled: true};
  const syntheticFirstUseSettle = preparationHost(1, 'Preparing first-use rendering...');
  const syntheticActiveCss = activeHost(1);
  const hosts = [syntheticStoppedCss, syntheticFirstUseSettle, syntheticActiveCss];
  let hostIndex = 0;
  let traceReads = 0;
  let waits = 0;
  const continuation = await returnFromCompetitivePrize({
    deadlineAt: Date.now() + 5000,
    observeTrace: async () => { traceReads++; return v6; },
    observeHost: async () => hosts[hostIndex++],
    press: async () => assert.fail('captured route already returned to CSS; no Prize input is allowed'),
    wait: async () => { waits++; },
  });
  assert.equal(continuation.initial_phase, 1);
  assert.equal(continuation.final_phase, 1);
  assert.equal(continuation.prize_confirmations, 0);
  assert.equal(traceReads, 3);
  assert.equal(waits, 1);
  assert.deepEqual(continuation.deferred_preparation.map(row => row.reason),
    ['destination-construction', 'first-use-render-settle']);
});

// snapshot().message prefers disc progress over preparationLabel and native
// status; development onState and frame hooks can publish either display.
test('readiness uses native identity and live Pause capability across arbitrary progress displays', () => {
  for (const status of ['Reading local data 76/76', 'arbitrary stale progress', '']) {
    for (const [classify, hosts] of [
      [classifyResultsOwnerReadiness, [activeHost(8), preparationHost(8, 'Original Results'),
        preparationHost(8, 'Preparing original Results...'), preparationHost(8, 'Preparing first-use rendering...')]],
      [classifyResultsDestinationReadiness, [activeHost(1), activeHost(9),
        preparationHost(5, 'Preparing original next scene...'), preparationHost(1, 'Original character select')]],
      [classifyPrizeReturnReadiness, [activeHost(1), activeHost(9),
        preparationHost(5, 'Preparing original character select...'), preparationHost(9, 'Preparing first-use rendering...')]],
    ]) for (const host of hosts) {
      assert.notEqual(classify({...host, status}).kind, 'invalid');
      for (const invalid of [{phase: 99}, {running: 2}, {message: 'Paused.'}, {pause_present: false},
        {pause_disabled: !host.pause_disabled}, {error: 'runtime failure'}])
        assert.equal(classify({...host, status, ...invalid}).kind, 'invalid');
    }
  }
});
