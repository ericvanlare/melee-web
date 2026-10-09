import {resultsPadTraceFailures} from './vs_rules_results_confirmation_driver.mjs';

export const COMPETITIVE_TIMEOUT_BOUNDS = Object.freeze({
  stockLossWallMs: 120000,
  noSourceProgressWallMs: 15000,
  resultsTransitionWallMs: 30000,
  resultsReturnWallMs: 45000,
  gameplayWallMs: 660000,
  outwardPulseFrames: 12,
  maximumOutwardPulses: 50,
  stableNeutralFrames: 120,
  snapshotLimit: 12,
  snapshotPeriodFrames: 3600,
});

export const COMPETITIVE_PRIZE_MAX_CONFIRMATIONS = 60;
export const COMPETITIVE_RESULTS_RETURN_POLL_MS = 250;

const PRIZE_NEXT_SCENE_PREPARATION_MESSAGE = 'Preparing original character select...';

// Results confirmation owns phase 8 and stops at its first source exit. The
// original Prize screen is a separate phase-9 route back to CSS; only it may
// receive these bounded follow-up Start inputs.
export async function returnFromCompetitivePrize({
  deadlineAt, observeHost, observeTrace, press, wait,
  pollMs = COMPETITIVE_RESULTS_RETURN_POLL_MS,
}) {
  if (!Number.isFinite(deadlineAt) || deadlineAt <= Date.now() ||
      typeof observeHost !== 'function' || typeof observeTrace !== 'function' ||
      typeof press !== 'function' || typeof wait !== 'function' ||
      !Number.isFinite(pollMs) || pollMs <= 0)
    throw Error('Original Prize return requires a live shared deadline and checked trace/host/input/wait functions');
  const initialBudgetMs = deadlineAt - Date.now();
  const maxPolls = Math.ceil(initialBudgetMs / pollMs) + 2;
  let pollCount = 0;
  const sample = async label => {
    if (Date.now() >= deadlineAt)
      throw Error(`${label}: original Results route exceeded the shared 45-second deadline`);
    if (++pollCount > maxPolls)
      throw Error(`${label}: Results/Prize trace polling exceeded its time-derived bound ${maxPolls}`);
    // The Results trace remains available after its owner closes. Read and
    // retain that bounded source witness before a stopped host can be rejected.
    const trace = await observeTrace();
    const traceFailures = resultsPadTraceFailures(trace);
    if (traceFailures.length)
      throw Error(`${label}: retained original Results trace is invalid: ${JSON.stringify(traceFailures)}`);
    const final = trace.samples.at(-1)?.results_state_after_tick;
    if (final?.phase !== 4 || final.stats_phase !== 2 ||
        JSON.stringify(final.players?.map(player => player.confirmed)) !== JSON.stringify([1, 1, 1, 1]))
      throw Error(`${label}: retained Results trace lacks final phase-4/two-Human confirmation: ${JSON.stringify(final)}`);
    const state = await observeHost(label);
    if (Date.now() >= deadlineAt)
      throw Error(`${label}: original Results route exceeded the shared 45-second deadline while observing host state`);
    if (!state || !Number.isSafeInteger(state.phase) || state.error)
      throw Error(`${label}: original Results/Prize host state is invalid: ${JSON.stringify(state)}`);
    if (state.phase === 9 && state.running !== 1)
      throw Error(`${label}: original Prize must be active before another confirmation: ${JSON.stringify(state)}`);
    return state;
  };
  const initial = await sample('original Results exit before Prize return');
  if (initial.running !== 1)
    throw Error(`Original Results exit has invalid host running state: ${JSON.stringify(initial)}`);
  if (initial.phase === 1)
    return {initial_phase: 1, final_phase: 1, prize_confirmations: 0,
      deferred_preparation: null,
      prize_source_exit_witness: 'unavailable: no dedicated Prize PAD source trace is exported'};
  if (initial.phase !== 9)
    throw Error(`Original Results exited into unsupported host phase ${initial.phase}; Prize Start is not allowed`);

  let state = initial;
  let confirmations = 0;
  let deferredPreparation = null;
  while (state.phase === 9 && confirmations < COMPETITIVE_PRIZE_MAX_CONFIRMATIONS) {
    if (Date.now() >= deadlineAt)
      throw Error('Original Prize return exceeded the shared 45-second Results deadline');
    await press('Enter', {releaseMs: 380});
    confirmations++;
    if (Date.now() >= deadlineAt)
      throw Error('Original Prize return exceeded the shared 45-second Results deadline during input');
    state = await sample(`original Prize confirmation ${confirmations}`);
    if (state.phase === 5) {
      if (state.running !== 0 || state.message !== PRIZE_NEXT_SCENE_PREPARATION_MESSAGE)
        throw Error(`Original Prize phase 5 is admitted only after Prize input during stopped character-select preparation: ${JSON.stringify(state)}`);
      deferredPreparation = {phase: 5, running: 0, message: state.message,
        after_confirmation: confirmations};
      while (Date.now() < deadlineAt) {
        const remaining = deadlineAt - Date.now();
        await wait(Math.min(pollMs, remaining));
        state = await sample('original Prize-to-CSS preparation');
        if (state.phase === 1 && state.running === 1) break;
        if (state.phase !== 5 || state.running !== 0 ||
            state.message !== PRIZE_NEXT_SCENE_PREPARATION_MESSAGE)
          throw Error(`Original Prize preparation reached an unexpected host state before CSS: ${JSON.stringify(state)}`);
      }
      if (state.phase !== 1 || state.running !== 1)
        throw Error('Original Prize did not return to CSS before the shared 45-second deadline');
      break;
    }
    if (state.phase !== 9 && !(state.phase === 1 && state.running === 1))
      throw Error(`Original Prize reached unsupported host phase ${state.phase} after confirmation ${confirmations}`);
  }
  if (state.phase !== 1)
    throw Error(`Original Prize did not return to CSS within ${COMPETITIVE_PRIZE_MAX_CONFIRMATIONS} confirmations`);
  return {initial_phase: initial.phase, final_phase: state.phase, prize_confirmations: confirmations,
    deferred_preparation: deferredPreparation,
    prize_source_exit_witness: 'unavailable: Prize transition is bounded by declared Enter input and checked host phases; no dedicated Prize PAD source trace is exported'};
}

function expect(failures, label, actual, wanted) {
  if (actual !== wanted)
    failures.push(`${label}: expected ${JSON.stringify(wanted)}, got ${JSON.stringify(actual)}`);
}

function stockPair(match) {
  if (!Array.isArray(match?.players) || match.players.length !== 2 ||
      match.players.some(player => !Number.isInteger(player?.stocks))) return null;
  return match.players.map(player => player.stocks);
}

export function competitiveTimeoutProgressFailures(match) {
  const failures = [];
  expect(failures, 'live source match ready', match?.ready, true);
  expect(failures, 'match observer is valid', match?.observer_error ?? false, false);
  expect(failures, 'source match is not paused', match?.paused, false);
  expect(failures, 'source match has not ended', match?.ending, false);
  expect(failures, 'source match is not complete', match?.complete, false);
  if (!Number.isInteger(match?.frame) || match.frame < 0)
    failures.push(`source frame is invalid: ${JSON.stringify(match?.frame)}`);
  const stocks = stockPair(match);
  if (!stocks) failures.push('two live player stock observations are required');
  else if (!(stocks[0] === 4 || stocks[0] === 3) || stocks[1] !== 4)
    failures.push(`only P1 may lose one stock: expected [4,4] or [3,4], got ${JSON.stringify(stocks)}`);
  expect(failures, 'source setup stocks remain [4,4]', JSON.stringify(match?.rules?.player_stocks),
    JSON.stringify([4, 4]));
  return failures;
}

export function competitiveTimeoutFirstLossFailures(before, after) {
  const failures = [];
  expect(failures, 'first observed source match has exactly two live players',
    Array.isArray(before?.players) && before.players.length === 2, true);
  expect(failures, 'source stock-loss observation advanced',
    Number.isInteger(after?.frame) && Number.isInteger(before?.frame) && after.frame > before.frame, true);
  const beforeStocks = stockPair(before);
  const afterStocks = stockPair(after);
  expect(failures, 'pre-input source stocks', JSON.stringify(beforeStocks), JSON.stringify([4, 4]));
  expect(failures, 'first source loss is P1 only', JSON.stringify(afterStocks), JSON.stringify([3, 4]));
  failures.push(...competitiveTimeoutProgressFailures(after));
  return failures;
}

export function competitiveTimeoutStableFailures(match, baselineFrame) {
  const failures = [...competitiveTimeoutProgressFailures(match)];
  const stocks = stockPair(match);
  expect(failures, 'neutral post-loss stocks remain [3,4]', JSON.stringify(stocks), JSON.stringify([3, 4]));
  expect(failures, 'neutral stability interval advances 120 source frames',
    Number.isInteger(match?.frame) && Number.isInteger(baselineFrame) &&
      match.frame - baselineFrame >= COMPETITIVE_TIMEOUT_BOUNDS.stableNeutralFrames, true);
  return failures;
}

export function competitiveTimeoutTerminalFailures(match) {
  const failures = [];
  expect(failures, 'original source timeout outcome', match?.terminal?.outcome, 1);
  expect(failures, 'original source winner list is present',
    Array.isArray(match?.terminal?.winners), true);
  if (Array.isArray(match?.terminal?.winners)) {
    expect(failures, 'original source winner count', match.terminal.winners.length, 1);
    expect(failures, 'original source unique winner is P2', match.terminal.winners[0], 1);
  }
  expect(failures, 'retained terminal source settings use the eight-minute stock timer',
    match?.rules?.time_limit, 480);
  expect(failures, 'retained terminal source match kind is stock', match?.rules?.match_kind, 1);
  expect(failures, 'source setup stocks remain [4,4] in terminal data',
    JSON.stringify(match?.rules?.player_stocks), JSON.stringify([4, 4]));
  return failures;
}

export function competitiveTimeoutReadinessFailures(match) {
  const failures = [];
  if (typeof match?.ending !== 'boolean' || typeof match?.complete !== 'boolean')
    failures.push('source ending/completion fields must remain booleans');
  if (typeof match?.ready !== 'boolean') {
    failures.push('source HUD readiness must remain a boolean');
  } else if (match?.ending === true || match?.complete === true) {
    expect(failures, 'source HUD is disabled after match ending begins', match.ready, false);
  } else {
    expect(failures, 'source HUD is enabled while the match is active', match.ready, true);
  }
  return failures;
}

export function competitiveTimeoutDeferredResultsFailures(state, match, expectedRules) {
  const failures = [...competitiveTimeoutTerminalFailures(match)];
  expect(failures, 'deferred original Results asset phase', state?.phase, 5);
  expect(failures, 'deferred Results asset preparation is stopped', state?.running, 0);
  expect(failures, 'retained terminal match observer is valid', match?.observer_error ?? false, false);
  failures.push(...competitiveTimeoutReadinessFailures(match));
  expect(failures, 'retained terminal match observation is not paused', match?.paused, false);
  if (match?.ending !== true && match?.complete !== true) {
    failures.push('retained terminal match has neither source ending nor completion set');
  }
  const stocks = stockPair(match);
  expect(failures, 'retained terminal live stocks remain [3,4]', JSON.stringify(stocks),
    JSON.stringify([3, 4]));
  if (!expectedRules || typeof expectedRules !== 'object' || Array.isArray(expectedRules)) {
    failures.push('normalized source rules baseline is unavailable');
  } else {
    expect(failures, 'normalized source rules remain unchanged during Results preparation',
      JSON.stringify(match?.rules), JSON.stringify(expectedRules));
  }
  if (!Number.isInteger(match?.frame) || match.frame < 0)
    failures.push(`retained terminal source frame is invalid: ${JSON.stringify(match?.frame)}`);
  return failures;
}

export function runtimeDiagnosticsFailures(capture, {identity, identityScope} = {}) {
  const failures = [];
  expect(failures, 'runtime recorder schema', capture?.schema, 'melee-web-runtime-callback-capture-v1');
  expect(failures, 'runtime recorder status', capture?.status, 'installed');
  expect(failures, 'runtime recorder identity', JSON.stringify(capture?.identity), JSON.stringify(identity));
  expect(failures, 'runtime recorder identity scope',
    JSON.stringify(capture?.identity_scope), JSON.stringify(identityScope));
  expect(failures, 'runtime sample ring capacity', capture?.max_samples, 100);
  expect(failures, 'runtime incident ring capacity', capture?.max_incidents, 24);
  if (!Array.isArray(capture?.samples) || capture.samples.length > 100)
    failures.push('runtime sample ring is missing or exceeds its fixed 100-row bound');
  if (!Array.isArray(capture?.incidents) || capture.incidents.length > 24)
    failures.push('runtime incident ring is missing or exceeds its fixed 24-row bound');
  for (const [label, value] of [
    ['callback_count', capture?.callback_count],
    ['dropped_samples', capture?.dropped_samples],
    ['dropped_incidents', capture?.dropped_incidents],
    ['unknown_reason_count', capture?.unknown_reason_count],
    ['dropped_unknown_reason_count', capture?.dropped_unknown_reason_count],
    ['invalid_preparation_count', capture?.invalid_preparation_count],
    ['dropped_invalid_preparation_count', capture?.dropped_invalid_preparation_count],
    ['invalid_phase_steps', capture?.invalid_phase_steps],
  ]) {
    if (!Number.isSafeInteger(value) || value < 0) failures.push(`runtime ${label} is invalid`);
  }
  const samples = Array.isArray(capture?.samples) ? capture.samples : [];
  if (Number.isSafeInteger(capture?.callback_count) && Number.isSafeInteger(capture?.dropped_samples) &&
      capture.callback_count !== samples.length + capture.dropped_samples)
    failures.push('runtime callback/sample drop accounting does not conserve all callbacks');
  for (const [index, row] of samples.entries()) {
    if (!row || !Number.isFinite(row.at_ms) || !Number.isInteger(row.source_frame) || row.source_frame < -1 ||
        !Number.isInteger(row.scene) || row.scene < 0 || !Number.isFinite(row.source_steps) ||
        row.source_steps < 0 || !Number.isFinite(row.source_draws) || row.source_draws < 0 ||
        !Number.isFinite(row.callback_ms) || row.callback_ms < 0 ||
        !(row.running === null || typeof row.running === 'boolean'))
      failures.push(`runtime sample ${index} is malformed`);
  }
  const phaseSteps = capture?.phase_source_steps;
  if (!Array.isArray(phaseSteps) || phaseSteps.length !== 16 ||
      phaseSteps.some(value => !Number.isSafeInteger(value) || value < 0)) {
    failures.push('runtime phase source-step counters are missing or malformed');
  } else {
    if (phaseSteps[7] === 0) failures.push('runtime recorder observed no gameplay-phase source steps');
    if (phaseSteps[8] === 0) failures.push('runtime recorder observed no Results-phase source steps');
  }
  const reasons = capture?.reason_counts;
  const droppedReasons = capture?.dropped_reason_counts;
  const validReasons = value => Array.isArray(value) && value.length === 10 &&
    value.every(count => Number.isSafeInteger(count) && count >= 0);
  if (!validReasons(reasons)) failures.push('runtime incident reason counters are missing or malformed');
  if (!validReasons(droppedReasons)) failures.push('dropped runtime incident reason counters are missing or malformed');
  if (Number.isSafeInteger(capture?.dropped_incidents) && capture.dropped_incidents !== 0)
    failures.push(`runtime incident ring lost ${capture.dropped_incidents} records`);
  if (Number.isSafeInteger(capture?.unknown_reason_count) && capture.unknown_reason_count !== 0)
    failures.push('runtime recorder observed unknown incident reason codes');
  if (Number.isSafeInteger(capture?.dropped_unknown_reason_count) && capture.dropped_unknown_reason_count !== 0)
    failures.push('runtime recorder lost incidents with unknown reason codes');
  if (Number.isSafeInteger(capture?.invalid_preparation_count) && capture.invalid_preparation_count !== 0)
    failures.push('runtime recorder observed malformed preparation incidents');
  if (Number.isSafeInteger(capture?.dropped_invalid_preparation_count) && capture.dropped_invalid_preparation_count !== 0)
    failures.push('runtime recorder lost malformed preparation incidents');
  if (Number.isSafeInteger(capture?.invalid_phase_steps) && capture.invalid_phase_steps !== 0)
    failures.push('runtime recorder observed invalid publication-phase source-step data');
  if (validReasons(reasons) && reasons.some((count, reason) => reason !== 7 && count !== 0))
    failures.push('runtime recorder observed a non-preparation runtime incident');
  if (validReasons(droppedReasons) && droppedReasons.some(count => count !== 0))
    failures.push('runtime recorder incident ring reports dropped incident records');
  const incidents = Array.isArray(capture?.incidents) ? capture.incidents : [];
  if (validReasons(reasons) && reasons.reduce((sum, count) => sum + count, 0) !== incidents.length)
    failures.push('runtime incident rows do not match lossless reason counters');
  for (const [index, row] of incidents.entries()) {
    if (!Number.isFinite(row?.at_ms) || !Number.isFinite(row?.callback_ms) || row.callback_ms < 0 ||
        row?.reason_code !== 7 || row.value !== 0 || row.threshold !== 0 ||
        row.clock_owner_code !== 0 || !Number.isInteger(row.source_frame) || row.source_frame < -1 ||
        !Number.isInteger(row.scene) || row.scene < 0)
      failures.push(`runtime incident ${index} is not a valid nonterminal preparation observation`);
  }
  return failures;
}

export function runtimeDiagnosticCounterFailures(counters) {
  const failures = [];
  expect(failures, 'runtime recorder counter status', counters?.status, 'installed');
  if (!Number.isSafeInteger(counters?.callback_count) || counters.callback_count < 1)
    failures.push('runtime callback counter is missing or has not observed a callback');
  if (!Number.isSafeInteger(counters?.invalid_phase_steps) || counters.invalid_phase_steps !== 0)
    failures.push('runtime callback counter observed invalid publication-phase source-step data');
  if (!Number.isSafeInteger(counters?.unknown_reason_count) || counters.unknown_reason_count !== 0)
    failures.push('runtime callback counter observed an unknown incident reason');
  if (!Number.isSafeInteger(counters?.invalid_preparation_count) || counters.invalid_preparation_count !== 0)
    failures.push('runtime callback counter observed a malformed preparation incident');
  if (!Array.isArray(counters?.phase_source_steps) || counters.phase_source_steps.length !== 16 ||
      counters.phase_source_steps.some(value => !Number.isSafeInteger(value) || value < 0))
    failures.push('runtime callback phase counters are missing or malformed');
  if (!Array.isArray(counters?.reason_counts) || counters.reason_counts.length !== 10 ||
      counters.reason_counts.some(value => !Number.isSafeInteger(value) || value < 0)) {
    failures.push('runtime callback incident counters are missing or malformed');
  } else {
    const incidentTotal = counters.reason_counts.reduce((sum, count) => sum + count, 0);
    if (incidentTotal > 24)
      failures.push(`runtime callback incident count ${incidentTotal} exceeds the lossless 24-row ring`);
    if (counters.reason_counts.some((count, reason) => reason !== 7 && count !== 0))
      failures.push('runtime callback counter observed a non-preparation runtime incident');
  }
  return failures;
}
