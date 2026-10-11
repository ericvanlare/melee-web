/* Source-observed, one-shot confirmation of the original two-Human Results
 * scene. Keyboard actions are issued only after the copied Results PAD trace
 * proves the preceding authored source state. */

export const RESULTS_TRACE_START_MASK = 0x1000;
export const RESULTS_TRACE_CAPACITY = 8192;
export const RESULTS_CONFIRMATION_POLL_MS = 250;
export const RESULTS_CONNECTED_PAD_ERRORS = Object.freeze([0, 0, -1, -1]);
export const RESULTS_NEXT_SCENE_PREPARATION_MESSAGE = 'Preparing original next scene...';

const RESULTS_PREPARATION_MESSAGE = 'Preparing original Results...';
const MATCH_CONTINUATION_PREPARATION_MESSAGE = 'Preparing original match continuation...';
const CHARACTER_SELECT_PREPARATION_MESSAGE = 'Preparing original character select...';
const FIRST_USE_PREPARATION_MESSAGE = 'Preparing first-use rendering...';

function invalidReadiness(reason) {
  return {kind: 'invalid', reason};
}

function checkHostReadinessShape(host) {
  if (!host || typeof host !== 'object' || Array.isArray(host) ||
      !Number.isSafeInteger(host.phase) || ![0, 1].includes(host.running))
    return 'native host phase/running state is missing or malformed';
  if (host.error) return `native runtime error: ${host.error}`;
  if (host.pause_present !== true || typeof host.pause_disabled !== 'boolean')
    return 'development pause-control presence/disabled state is missing';
  return null;
}

function activeOwner(host, phase, message, owner) {
  if (host.phase !== phase || host.running !== 1 || host.message !== message ||
      host.pause_disabled !== false) return null;
  return {kind: 'active', owner, phase};
}

function stoppedPreparation(host, phase, nativeMessage, reason) {
  if (host.phase !== phase || host.running !== 0 || host.message !== nativeMessage ||
      host.pause_disabled !== true) return null;
  return {kind: 'preparing', phase, reason, message: nativeMessage,
    status: host.status};
}

export function classifyResultsOwnerReadiness(host) {
  const invalid = checkHostReadinessShape(host);
  if (invalid) return invalidReadiness(invalid);
  const active = activeOwner(host, 8, 'Original Results', 'results');
  if (active) return active;
  // Status text has independent disc-progress and development-frame writers.
  // Native owner identity and the live Pause capability define readiness;
  // retain DOM text for diagnostics without treating it as a state signal.
  const preparing = stoppedPreparation(host, 8, RESULTS_PREPARATION_MESSAGE, 'scene-preparation') ||
    stoppedPreparation(host, 8, MATCH_CONTINUATION_PREPARATION_MESSAGE, 'scene-preparation') ||
    // Native construction publishes the owner before preparation is armed.
    stoppedPreparation(host, 8, 'Original Results', 'constructed-results-preparation') ||
    stoppedPreparation(host, 8, FIRST_USE_PREPARATION_MESSAGE, 'first-use-render-settle');
  return preparing || invalidReadiness('host is not an active Results owner or an exact stopped Results preparation state');
}

export function classifyResultsDestinationReadiness(host) {
  const invalid = checkHostReadinessShape(host);
  if (invalid) return invalidReadiness(invalid);
  const css = activeOwner(host, 1, 'Original character select', 'css');
  if (css) return css;
  const prize = activeOwner(host, 9, 'Original unlock notification', 'prize');
  if (prize) return prize;

  const nextScene = RESULTS_NEXT_SCENE_PREPARATION_MESSAGE;
  const preparing = stoppedPreparation(host, 5, nextScene, 'asset-transfer') ||
    stoppedPreparation(host, 1, 'Original character select', 'destination-construction') ||
    stoppedPreparation(host, 9, 'Original unlock notification', 'destination-construction') ||
    stoppedPreparation(host, 1, FIRST_USE_PREPARATION_MESSAGE, 'first-use-render-settle') ||
    stoppedPreparation(host, 9, FIRST_USE_PREPARATION_MESSAGE, 'prize-first-use-render-settle');
  return preparing || invalidReadiness('host is not active CSS/Prize or an exact Results-destination preparation state');
}

export function classifyPrizeReturnReadiness(host) {
  const invalid = checkHostReadinessShape(host);
  if (invalid) return invalidReadiness(invalid);
  const css = activeOwner(host, 1, 'Original character select', 'css');
  if (css) return css;
  const prize = activeOwner(host, 9, 'Original unlock notification', 'prize');
  if (prize) return prize;

  const preparing = stoppedPreparation(host, 5, CHARACTER_SELECT_PREPARATION_MESSAGE, 'asset-transfer') ||
    stoppedPreparation(host, 1, 'Original character select', 'destination-construction') ||
    stoppedPreparation(host, 1, FIRST_USE_PREPARATION_MESSAGE, 'first-use-render-settle') ||
    stoppedPreparation(host, 9, FIRST_USE_PREPARATION_MESSAGE, 'prize-first-use-render-settle');
  return preparing || invalidReadiness('host is not active CSS/Prize or an exact Prize-return preparation state');
}

function confirmationState(row) {
  return row?.results_state_after_tick ?? null;
}

function startedPorts(row) {
  const consumed = row?.source_consumed_pads;
  if (!Array.isArray(consumed)) return [];
  return consumed.flatMap((pad, port) =>
    Number.isSafeInteger(pad?.trigger) && (pad.trigger & RESULTS_TRACE_START_MASK)
      ? [port] : []);
}

function rowSummary(row) {
  const state = confirmationState(row);
  return {
    source_frame_before_tick: row?.source_frame ?? null,
    source_frame_after_tick: state?.source_frame ?? null,
    phase: state?.phase ?? null,
    stats_phase: state?.stats_phase ?? null,
    confirmed: Array.isArray(state?.players) ? state.players.map(player => player.confirmed) : null,
    consumed_start_ports: startedPorts(row),
    raw_pad_errors: Array.isArray(row?.pads) ? row.pads.map(pad => pad?.err ?? null) : null,
    copied_pad_errors: Array.isArray(row?.source_consumed_pads)
      ? row.source_consumed_pads.map(pad => pad?.err ?? null) : null,
  };
}

function traceSummary(trace) {
  const samples = Array.isArray(trace?.samples) ? trace.samples : [];
  return {
    schema: trace?.schema ?? null,
    attempts: trace?.attempts ?? null,
    retained: trace?.retained ?? null,
    capacity: trace?.capacity ?? null,
    overflow: trace?.overflow ?? null,
    latest: samples.length ? rowSummary(samples[samples.length - 1]) : null,
  };
}

export function resultsConnectedPadErrors(sourcePorts = [0, 1]) {
  if (JSON.stringify(sourcePorts) !== '[0,1]' && JSON.stringify(sourcePorts) !== '[0,2]')
    throw Error('Results requires explicitly supported original source ports [0,1] or [0,2]');
  return Array.from({length:4}, (_, port) => sourcePorts.includes(port) ? 0 : -1);
}

export function resultsPadTraceFailures(trace, sourcePorts = [0, 1]) {
  const connectedErrors = resultsConnectedPadErrors(sourcePorts);
  const failures = [];
  if (!trace || typeof trace !== 'object' || Array.isArray(trace))
    return ['original Results PAD trace is missing or malformed'];
  if (trace.schema !== 'melee-web-results-pad-trace-v1')
    failures.push('original Results PAD trace schema is missing or unexpected');
  if (!Number.isSafeInteger(trace.capacity) || trace.capacity !== RESULTS_TRACE_CAPACITY)
    failures.push('original Results PAD trace capacity is missing or unexpected');
  if (trace.overflow !== false)
    failures.push('original Results PAD trace reports overflow or lacks an explicit no-overflow value');
  if (!Number.isSafeInteger(trace.attempts) || trace.attempts < 0 ||
      !Number.isSafeInteger(trace.retained) || trace.retained < 0 ||
      trace.attempts !== trace.retained)
    failures.push('original Results PAD trace attempt/retained accounting is invalid or incomplete');
  if (!Array.isArray(trace.samples) || trace.samples.length > RESULTS_TRACE_CAPACITY) {
    failures.push('original Results PAD trace samples are missing or exceed capacity');
    return failures;
  }
  if (trace.samples.length !== trace.retained)
    failures.push('original Results PAD trace row count does not match retained count');

  let priorPhase = null;
  let priorStatsPhase = 0;
  for (const [index, row] of trace.samples.entries()) {
    if (!row || typeof row !== 'object' || Array.isArray(row)) {
      failures.push(`original Results PAD trace row ${index} is malformed`);
      continue;
    }
    if (!Number.isSafeInteger(row.source_frame) || row.source_frame !== index)
      failures.push(`original Results PAD trace row ${index} is missing, duplicated, or out of source-frame order`);
    if (row.tick_returned !== true)
      failures.push(`original Results PAD trace row ${index} did not complete its source tick`);
    const state = confirmationState(row);
    if (!state || typeof state !== 'object' || Array.isArray(state)) {
      failures.push(`original Results PAD trace row ${index} lacks its after-tick Results state`);
      continue;
    }
    if (!Number.isSafeInteger(state.source_frame) || state.source_frame !== row.source_frame + 1)
      failures.push(`original Results PAD trace row ${index} has an invalid after-tick source frame`);
    if (!Number.isInteger(state.phase) || state.phase < 0 || state.phase > 4)
      failures.push(`original Results PAD trace row ${index} has an unsupported original phase`);
    else if (priorPhase !== null && state.phase < priorPhase)
      failures.push(`original Results PAD trace row ${index} regressed the original Results phase`);
    else if (priorPhase !== null && state.phase > priorPhase + 1)
      failures.push(`original Results PAD trace row ${index} skipped an authored Results phase`);
    else priorPhase = state.phase;
    if (!Number.isInteger(state.stats_phase) || state.stats_phase < 0 || state.stats_phase > 2)
      failures.push(`original Results PAD trace row ${index} has an unsupported original stats phase`);
    else if (state.stats_phase < priorStatsPhase)
      failures.push(`original Results PAD trace row ${index} regressed the original stats phase`);
    else priorStatsPhase = state.stats_phase;
    if (!Array.isArray(state.players) || state.players.length !== 4 ||
        state.players.some(player => !player || ![0, 1].includes(player.confirmed)))
      failures.push(`original Results PAD trace row ${index} lacks four binary confirmation states`);
    if (!Array.isArray(row.pads) || row.pads.length !== 4 ||
        row.pads.some(pad => !pad || !Number.isSafeInteger(pad.button) || pad.button < 0 || pad.button > 0xffff ||
          !Number.isSafeInteger(pad.err))) {
      failures.push(`original Results PAD trace row ${index} lacks four raw PAD samples`);
    } else if (JSON.stringify(row.pads.map(pad => pad.err)) !== JSON.stringify(connectedErrors)) {
      failures.push(`original Results PAD trace row ${index} has an unexpected raw PAD error profile: ${JSON.stringify(row.pads.map(pad => pad.err))}`);
    }
    if (!Array.isArray(row.source_consumed_pads) || row.source_consumed_pads.length !== 4 ||
        row.source_consumed_pads.some(pad => !pad || !Number.isSafeInteger(pad.trigger) ||
          pad.trigger < 0 || pad.trigger > 0xffff || !Number.isSafeInteger(pad.err))) {
      failures.push(`original Results PAD trace row ${index} lacks four source-consumed PAD states`);
    } else if (JSON.stringify(row.source_consumed_pads.map(pad => pad.err)) !==
        JSON.stringify(connectedErrors)) {
      failures.push(`original Results PAD trace row ${index} has an unexpected copied PAD error profile: ${JSON.stringify(row.source_consumed_pads.map(pad => pad.err))}`);
    }
  }
  return failures;
}

function traceStartRows(trace, fromIndex) {
  return trace.samples.slice(fromIndex).flatMap((row, relativeIndex) =>
    startedPorts(row).map(port => ({row, index: fromIndex + relativeIndex, port})));
}

function assertOnePortStart(trace, fromIndex, expectedPort, label) {
  const events = traceStartRows(trace, fromIndex);
  if (events.length !== 1)
    throw Error(`${label}: expected exactly one consumed source Start edge after the action, got ${JSON.stringify(events.map(event => ({port: event.port, ...rowSummary(event.row)})))}`);
  const event = events[0];
  if (event.port !== expectedPort)
    throw Error(`${label}: source consumed Start on port ${event.port}, expected connected port ${expectedPort}`);
  const rawPads = event.row.pads;
  if (!(rawPads[expectedPort].button & RESULTS_TRACE_START_MASK))
    throw Error(`${label}: source-consumed Start has no matching raw PAD Start sample`);
  for (let port = 0; port < 4; port++) {
    if (port !== expectedPort && (rawPads[port].button & RESULTS_TRACE_START_MASK))
      throw Error(`${label}: source raw PAD Start also appeared on unexpected port ${port}`);
  }
  return event;
}

function assertHostInResults(host, label) {
  if (!host || !Number.isSafeInteger(host.phase))
    throw Error(`${label}: current native host phase is missing or malformed`);
  if (host.phase !== 8)
    throw Error(`${label}: original Results exited before this confirmation boundary (host phase ${host.phase})`);
}

function assertCompletedTwoHumanResultsRow(row, label) {
  const state = confirmationState(row);
  if (state?.phase !== 4 || state.stats_phase !== 2 ||
      JSON.stringify(state.players?.map(player => player.confirmed)) !== JSON.stringify([1, 1, 1, 1]))
    throw Error(`${label}: source Results did not retain phase 4, statistics phase 2, and all four confirmations: ${JSON.stringify(rowSummary(row))}`);
}

function assertP2CompletionTrace(trace, fromIndex, label, secondPort = 1) {
  const event = assertOnePortStart(trace, fromIndex, secondPort, label);
  assertCompletedTwoHumanResultsRow(event.row, `${label} source edge`);
  assertCompletedTwoHumanResultsRow(trace.samples.at(-1), `${label} final retained row`);
  return event;
}

export async function confirmTwoHumanResults({
  deadlineAt,
  observeHost,
  observeTrace,
  observeSample,
  press,
  wait,
  pollMs = RESULTS_CONFIRMATION_POLL_MS,
  sourcePorts = [0, 1],
  pressSecond,
}) {
  resultsConnectedPadErrors(sourcePorts);
  const secondPort = sourcePorts[1];
  const secondLabel = `P${secondPort + 1}`;
  if (secondPort === 2 && typeof pressSecond !== 'function')
    throw Error('Sparse Results requires an explicit source2 input callback');
  const readyFlags = Array.from({length:4}, (_, port) => sourcePorts.includes(port) ? 0 : 1);
  const firstFlags = readyFlags.map((value, port) => port === 0 ? 1 : value);
  if (![deadlineAt, pollMs].every(Number.isFinite) || deadlineAt <= Date.now() || pollMs <= 0)
    throw Error('Original Results confirmation needs a live deadline and positive poll interval');
  if (![press, wait].every(value => typeof value === 'function') ||
      (observeSample !== undefined && typeof observeSample !== 'function') ||
      (typeof observeSample !== 'function' &&
       ![observeHost, observeTrace].every(value => typeof value === 'function')))
    throw Error('Original Results confirmation requires native observers and checked input/wait functions');

  const initialBudgetMs = deadlineAt - Date.now();
  const maxPolls = Math.ceil(initialBudgetMs / pollMs) + 2;
  let pollCount = 0;
  const sample = async label => {
    if (Date.now() >= deadlineAt)
      throw Error(`${label}: original Results route exceeded its shared wall deadline`);
    if (++pollCount > maxPolls)
      throw Error(`${label}: original Results trace polling exceeded its time-derived bound ${maxPolls}`);
    // Retain the source-owned ring before the host callback can reject a
    // stopped asset-transfer boundary. This is a snapshot read, not a tick.
    const coherent = observeSample ? await observeSample(label) : null;
    const trace = observeSample ? coherent?.trace : await observeTrace();
    const failures = resultsPadTraceFailures(trace, sourcePorts);
    if (failures.length)
      throw Error(`${label}: original Results trace is incomplete: ${JSON.stringify({failures, trace: traceSummary(trace)})}`);
    const host = observeSample ? coherent?.host : await observeHost(label);
    if (Date.now() >= deadlineAt)
      throw Error(`${label}: original Results route exceeded its shared wall deadline during observation`);
    if (!host || !Number.isSafeInteger(host.phase))
      throw Error(`${label}: native host running/phase state is missing or invalid`);
    return {host, trace};
  };
  const pause = async label => {
    const remaining = deadlineAt - Date.now();
    if (remaining <= 0) throw Error(`${label}: original Results route exceeded its shared wall deadline`);
    await wait(Math.min(pollMs, remaining));
  };
  let p2StartIndex = null;
  const postConfirmationPreparation = [];
  const waitFor = async (predicate, label, {
    allowHostExit = false,
    allowResultsPreparationFromIndex = null,
  } = {}) => {
    let observed;
    while (Date.now() < deadlineAt) {
      observed = await sample(label);
      const readiness = observed.host.phase === 8
        ? classifyResultsOwnerReadiness(observed.host)
        : allowHostExit
          ? classifyResultsDestinationReadiness(observed.host)
          : invalidReadiness(`original Results exited before this confirmation boundary (host phase ${observed.host.phase})`);
      if (readiness.kind === 'invalid')
        throw Error(`${label}: ${readiness.reason}: ${JSON.stringify(observed.host)}`);
      // P2's copied PAD edge is asynchronous relative to the UI observer. Keep
      // polling Results, including its checked preparation states, until that
      // edge arrives; require it before leaving Results for a destination.
      if (allowResultsPreparationFromIndex !== null && observed.host.phase !== 8)
        assertP2CompletionTrace(observed.trace, allowResultsPreparationFromIndex, label, secondPort);
      if (readiness.kind === 'preparing' && allowResultsPreparationFromIndex !== null) {
        const existing = postConfirmationPreparation.find(row => row.phase === readiness.phase &&
          row.reason === readiness.reason && row.message === readiness.message);
        if (existing) existing.observations++;
        else postConfirmationPreparation.push({phase: readiness.phase, reason: readiness.reason,
          message: readiness.message,
          status: readiness.status, observations: 1});
      }
      if (readiness.kind === 'active' && predicate(observed)) return observed;
      await pause(label);
    }
    throw Error(`${label}: timed out before the source Results condition; last state ${JSON.stringify({host: observed?.host, trace: traceSummary(observed?.trace)})}`);
  };
  const eventFor = async (fromIndex, port, label) => waitFor(observed => {
    const events = traceStartRows(observed.trace, fromIndex);
    if (events.some(event => event.port !== port))
      throw Error(`${label}: unexpected connected port consumed Start: ${JSON.stringify(events.map(event => ({port: event.port, ...rowSummary(event.row)})))}`);
    if (events.length > 1)
      throw Error(`${label}: repeated source Start edges exceeded the one-action contract`);
    if (events.length !== 1) return false;
    if (port === secondPort) assertP2CompletionTrace(observed.trace, fromIndex, label, secondPort);
    return true;
  }, label, {allowHostExit: port === secondPort,
    allowResultsPreparationFromIndex: port === secondPort ? fromIndex : null})
    .then(observed => ({observed, event: assertOnePortStart(observed.trace, fromIndex, port, label)}));

  let observed = await waitFor(snapshot => {
    const state = confirmationState(snapshot.trace.samples.at(-1));
    return state?.phase === 2;
  }, 'original Results phase-2 presentation gate');
  const presentationStartIndex = observed.trace.retained;
  await press('Enter');
  const presentation = await eventFor(presentationStartIndex, 0, 'P1 presentation Start');
  if (confirmationState(presentation.event.row).phase !== 3)
    throw Error(`P1 presentation Start did not advance original Results to phase 3: ${JSON.stringify(rowSummary(presentation.event.row))}`);
  observed = await waitFor(snapshot => {
    const state = confirmationState(snapshot.trace.samples.at(-1));
    return state?.phase === 3 && state.stats_phase === 2;
  }, 'original Results statistics-ready gate');
  const statisticsReadyRow = observed.trace.samples.at(-1);
  const readyState = confirmationState(statisticsReadyRow);
  if (JSON.stringify(readyState.players.map(player => player.confirmed)) !== JSON.stringify(readyFlags))
    throw Error(`original Results statistics-ready state changed connected/empty-port confirmations: ${JSON.stringify(rowSummary(observed.trace.samples.at(-1)))}`);

  const p1StartIndex = observed.trace.retained;
  await press('Enter');
  const p1 = await eventFor(p1StartIndex, 0, 'P1 statistics confirmation');
  const p1State = confirmationState(p1.event.row);
  if (p1State.phase !== 3 || p1State.stats_phase !== 2 ||
      JSON.stringify(p1State.players.map(player => player.confirmed)) !== JSON.stringify(firstFlags))
    throw Error(`P1 source Start did not confirm only P1 during original statistics phase: ${JSON.stringify(rowSummary(p1.event.row))}`);
  assertHostInResults(p1.observed.host, 'after P1-only Results confirmation');

  p2StartIndex = p1.observed.trace.retained;
  if (pressSecond) await pressSecond();
  else await press('End');
  const p2 = await eventFor(p2StartIndex, secondPort, `${secondLabel} statistics confirmation`);
  const p2State = confirmationState(p2.event.row);
  if (p2State.phase !== 4 || p2State.stats_phase !== 2 ||
      JSON.stringify(p2State.players.map(player => player.confirmed)) !== JSON.stringify([1, 1, 1, 1]))
    throw Error(`P2 source Start did not complete the original two-Human Results confirmations: ${JSON.stringify(rowSummary(p2.event.row))}`);

  observed = await waitFor(snapshot => snapshot.host.phase === 1 || snapshot.host.phase === 9,
    'first original Results exit after both connected Humans confirm', {allowHostExit: true,
      allowResultsPreparationFromIndex: p2StartIndex});
  assertCompletedTwoHumanResultsRow(observed.trace.samples.at(-1),
    'Original Results exit final retained row');

  return {
    schema: 'melee-web-two-human-results-confirmation-v1',
    source_ports: [...sourcePorts],
    source_trace_capacity: observed.trace.capacity,
    source_trace_rows: observed.trace.retained,
    source_trace_attempts: observed.trace.attempts,
    poll_count: pollCount,
    post_confirmation_preparation: postConfirmationPreparation,
    presentation_start: rowSummary(presentation.event.row),
    statistics_ready: rowSummary(statisticsReadyRow),
    p1_confirmation: rowSummary(p1.event.row),
    p2_confirmation: rowSummary(p2.event.row),
    original_exit: {host_phase: observed.host.phase, host_message: observed.host.message ?? null,
      source: rowSummary(observed.trace.samples.at(-1))},
  };
}
