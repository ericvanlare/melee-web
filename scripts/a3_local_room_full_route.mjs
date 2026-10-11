/*
 * Optional local-room route extension. It observes source-owned CSS/SSS/match/
 * Results state and supplies only ordinary Playwright keyboard chords. The
 * native lockstep owner remains the only source-progress/checksum pump.
 */
import assert from 'node:assert/strict';
import {observeRuntimeOwner} from '../tests/runtime_owner_observation.mjs';
import {confirmTwoHumanResults} from '../tests/vs_rules_results_confirmation_driver.mjs';
import {returnFromCompetitivePrize} from '../tests/vs_rules_timeout_route_helpers.mjs';
import {createSourceCursorJoin} from './a3_source_cursor_join.mjs';

const FD_SOURCE_ID = 0x20;
const MARIO_SOURCE_ID = 8;
const MARIO_FIGHTER_ID = 0;
const MAX_STAGE_KEY_PULSES = 240;
const MAX_WALKOFF_PULSES_PER_STOCK = 50;
const WALKOFF_HOLD_MS = 250;
const WALKOFF_RELEASE_MS = 25;
const FIRST_USE_RENDERING_MESSAGE = 'Preparing first-use rendering...';

function requireValue(condition, message) {
  if (!condition) throw Error(message);
}

function signedByte(value) {
  return value > 127 ? value - 256 : value;
}

function cssDoor(setup, port) {
  const row = setup?.doors?.slice(port * 10, port * 10 + 10);
  requireValue(row?.length === 10, `Original CSS door ${port} is unavailable`);
  return {p_kind: row[0], character: row[3], slot_type: row[4], slot: row[6],
    source_port: row[6] ? row[6] - 1 : port};
}

function checkMarioTwoHumanCss(setup, label) {
  requireValue(setup?.doors?.length === 40 && setup?.cursors?.length === 16 &&
    setup?.geometry?.length === 48, `${label}: original CSS setup observer is incomplete`);
  const doors = [0, 1, 2, 3].map(port => cssDoor(setup, port));
  assert.deepEqual(doors.map(door => [door.p_kind, door.slot_type]),
    [[0, 0], [0, 0], [3, 3], [3, 3]],
    `${label}: CSS must retain two Human doors and two empty doors`);
  assert.deepEqual(doors.slice(0, 2).map(door => door.character),
    [MARIO_SOURCE_ID, MARIO_SOURCE_ID], `${label}: both Human doors must be Mario`);
  assert.deepEqual(doors.slice(0, 2).map(door => door.source_port), [0, 1],
    `${label}: CSS must retain source controller ports 0 and 1`);
  return doors;
}

function checkMatchIdentity(observation, label, {active = true, allowCompletion = false,
  phase = active ? 7 : 8} = {}) {
  const match = observation?.match;
  requireValue(observation?.error == null, `${label}: native runtime error ${observation?.error}`);
  requireValue(match && match.observer_error !== true && !match.observer_error_reason,
    `${label}: native match observer refused the source state`);
  assert.equal(observation.phase, phase, `${label}: unexpected native scene phase`);
  assert.deepEqual(match.observed_player_source_slots, [0, 1],
    `${label}: source player slots changed`);
  assert.equal(match.leg, 'vs', `${label}: source VS match identity changed`);
  assert.equal(match.rules?.stage, FD_SOURCE_ID, `${label}: source stage is not Final Destination`);
  assert.equal(match.rules?.is_stock, 1, `${label}: source match is not Stock`);
  assert.equal(match.rules?.is_vs, 1, `${label}: source match is not VS`);
  assert.equal(match.rules?.is_teams, 0, `${label}: source match unexpectedly enabled Teams`);
  assert.deepEqual(match.rules?.player_stocks, [4, 4],
    `${label}: authored match setup did not begin with four stocks each`);
  assert.equal(match.players?.length, 2, `${label}: two native players were not observed`);
  for (const [index, player] of match.players.entries()) {
    assert.equal(player.source_port, index, `${label}: P${index + 1} source port changed`);
    assert.equal(player.source_character, MARIO_SOURCE_ID, `${label}: P${index + 1} is not Mario`);
    assert.equal(player.fighter, MARIO_FIGHTER_ID, `${label}: P${index + 1} runtime fighter changed`);
    assert.equal(player.slot_type, 0, `${label}: P${index + 1} is not a Human source slot`);
    assert(Number.isInteger(player.stocks) && player.stocks >= 0 && player.stocks <= 4,
      `${label}: P${index + 1} source stock count is invalid`);
  }
  assert.deepEqual(match.source_pad_errors, [0, 0, -1, -1],
    `${label}: copied source PAD availability changed`);
  if (active) {
    assert.equal(observation.running, 1, `${label}: source gameplay owner stopped`);
    if (allowCompletion) {
      assert.equal(match.players[0].stocks, 0, `${label}: completion was allowed before P1 lost all stocks`);
    } else {
      assert.equal(match.ready, true, `${label}: source match is not ready`);
      assert.equal(match.complete, false, `${label}: source match ended before the walk-off route`);
      assert.equal(match.ending, false, `${label}: source match is already ending`);
      assert.equal(match.outcome, 0, `${label}: source match already has an outcome`);
    }
    assert.equal(match.paused, false, `${label}: source match is paused`);
  }
  return match;
}

function checkPreparingTerminalTransfer(observation, label) {
  const match = checkMatchIdentity(observation, label, {active: false, phase: 7});
  assert.equal(observation.running, 0, `${label}: terminal preparation must be stopped`);
  assert.deepEqual(match.players.map(player => player.stocks), [0, 4],
    `${label}: preparation stopped before the natural [0,4] terminal stocks`);
  assert.equal(match.complete, true, `${label}: terminal preparation is not complete`);
  assert.equal(match.ending, true, `${label}: terminal preparation is not ending`);
  assert.equal(match.outcome, 2, `${label}: terminal preparation outcome changed`);
  assert.equal(match.paused, false, `${label}: terminal preparation is unexpectedly paused`);
  assert.equal(observation.message, 'Preparing original match continuation...',
    `${label}: stopped phase-7 state is not the authored Results transfer preparation`);
  return match;
}

function checkAssetTransferredTerminal(observation, label) {
  const match = checkMatchIdentity(observation, label, {active: false, phase: 5});
  assert.equal(observation.running, 0, `${label}: Results asset transfer must be stopped`);
  assert.deepEqual(match.players.map(player => player.stocks), [0, 4],
    `${label}: Results transfer changed the natural terminal stocks`);
  assert.equal(match.complete, true, `${label}: Results transfer is not complete`);
  assert.equal(match.ending, true, `${label}: Results transfer is not ending`);
  assert.deepEqual(match.terminal, {outcome: 2, winners: [1]},
    `${label}: cached native terminal winner changed during Results transfer`);
  return match;
}

function isHealthyFirstUseRenderWait(row) {
  const status = row?.native_status;
  const match = row?.match;
  return row?.phase === 7 && row?.running === 0 &&
    row?.message === FIRST_USE_RENDERING_MESSAGE && row?.error === null &&
    status?.active === 1 && status?.blocker === 'none' &&
    status?.local_capture_failure === null && status?.network_wait?.active === 0 &&
    status?.terminal?.kind === 0 &&
    status?.indexed?.duplicates === 0 && status?.indexed?.conflicts === 0 &&
    status?.indexed?.gaps === 0 && status?.indexed?.invalid === 0 &&
    match?.ready === true && match?.paused === false && match?.complete === false &&
    match?.ending === false && match?.outcome === 0;
}

// This is a pure classification of an already observed native row. A render
// settle is a bounded wait state; every other stopped state continues through
// the existing strict terminal/unsupported-state assertions.
export function classifyStockObservation(row, label = 'stock observation') {
  if (row?.phase === 7 && row?.running === 1)
    return {match: checkMatchIdentity(row, label, {
      allowCompletion: row.match?.players?.[0]?.stocks === 0})};
  if (row?.phase === 7 && row?.running === 0) {
    if (isHealthyFirstUseRenderWait(row)) {
      const match = checkMatchIdentity(row, label, {active: false, phase: 7});
      assert.equal(match.ready, true, `${label}: first-use render wait is not a ready match`);
      assert.equal(match.paused, false, `${label}: source match is paused`);
      assert.equal(match.complete, false, `${label}: source match completed during render wait`);
      assert.equal(match.ending, false, `${label}: source match began ending during render wait`);
      assert.equal(match.outcome, 0, `${label}: source match has an outcome during render wait`);
      return {match, wait: 'first-use-rendering'};
    }
    return {match: checkPreparingTerminalTransfer(row, label), handoff: 'preparing-results'};
  }
  if (row?.phase === 5 && row?.running === 0)
    return {match: checkAssetTransferredTerminal(row, label), handoff: 'results-assets-ready'};
  if (row?.phase === 8) {
    const match = checkMatchIdentity(row, label, {active: false, phase: 8});
    assert.deepEqual(match.players.map(player => player.stocks), [0, 4],
      `${label}: Results appeared before the natural [0,4] elimination`);
    assert.deepEqual(match.terminal, {outcome: 2, winners: [1]},
      `${label}: Results terminal winner changed`);
    return {match, handoff: 'results'};
  }
  throw Error(`${label}: unsupported phase/running state ${row?.phase}/${row?.running}`);
}

/*
 * `pulse` binds each keyboard action to its literal local input record, then
 * waits for the runtime-owned peer comparison to cross that record's declared
 * input-delay boundary. `expected` is a PAD-byte readback only, never input.
 */
export async function runA3LocalFullRoute({
  alpha, beta, report, inputDelay, remaining, readRoomState, readPeerSnapshot,
  readInputDiagnostics, waitForBothChecksumCompare, captureScreenshot,
  assertHealthy,
}) {
  const route = {
    result: 'running', scope: 'local keyboard route/lifecycle witness only',
    phases: [], css_roster: [], stage: null, match_start: [], stock_losses: [],
    respawns: [], input_actions: [], terminal_handoff: null, results: null, returned_css: null,
    required_checksum_boundary: null,
    exclusions: ['no PAD/Gamepad input injection', 'P2 remains neutral during gameplay',
      'no combat-coverage claim', 'no forced stock/result or native match-state mutation',
      'no retail, public Worker, physical-input, pixel, PCM, or timing-equivalence claim'],
  };
  report.full_route = route;
  const instances = {alpha, beta};
  const observe = async instance => instance.page.evaluate(() => {
    const module = globalThis.Module;
    const phase = module?._melee_web_native_menu_phase?.() ?? null;
    const match = typeof module?._melee_web_native_menu_match_observe === 'function'
      ? JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe())) : null;
    const nativeStatus = globalThis.__net?.status?.() ?? null;
    const message = typeof module?._melee_web_native_menu_message === 'function'
      ? module.UTF8ToString(module._melee_web_native_menu_message()) : null;
    return {phase, running: module?._melee_web_native_menu_running?.() ?? null, message,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      source_tick: nativeStatus?.cursor ?? null, native_status: nativeStatus,
      css: globalThis.menuObserveCssSetup?.() ?? null,
      stage: globalThis.menuObserveStage?.(0x20) ?? null,
      match};
  });
  const observePeerSample = async role => {
    let row = null;
    let observedAt = new Date().toISOString();
    try {
      row = await observe(instances[role]);
      observedAt = new Date().toISOString();
      const room = await readRoomState(instances[role]);
      return {role, row, room, observed_at: observedAt, error: null};
    } catch (error) {
      return {role, row, room: null, observed_at: observedAt,
        error: String(error?.stack || error)};
    }
  };
  const assertPeerSampleHealthy = sample => {
    if (sample.error) throw Error(`${sample.role} observation failed: ${sample.error}`);
    assertHealthy(sample.room, sample.role);
    requireValue(sample.row?.error == null,
      `${sample.role}: native runtime error ${sample.row?.error}`);
  };
  const observeBoth = async () => {
    const samples = await Promise.all(['alpha', 'beta'].map(observePeerSample));
    samples.forEach(assertPeerSampleHealthy);
    return samples.map(sample => sample.row);
  };
  let lastMatchedSourceCursor = -1;
  const observeAtCommonSourceCursor = async observation => {
    // Each acquisition begins from fresh peer samples. This prevents a common
    // row retained before a keyboard action from satisfying the next wait.
    const sourceCursorJoin = createSourceCursorJoin({retainedCursorsPerPeer: 8});
    const cursorHistory = observation.source_cursor_join ??= {
      samples_by_peer: {alpha: 0, beta: 0}, last_by_peer: {}, recent: [], last_pair: null,
    };
    const retain = sample => {
      const {role, row, observed_at: observedAt} = sample;
      cursorHistory.samples_by_peer[role]++;
      cursorHistory.last_by_peer[role] = {observed_at: observedAt,
        source_tick: row?.source_tick ?? null, phase: row?.phase ?? null,
        running: row?.running ?? null, message: row?.message ?? null,
        error: sample.error ?? row?.error ?? null,
        native_status: row?.native_status ?? null, room: sample.room ?? null};
      cursorHistory.recent.push({role, observed_at: observedAt,
        source_tick: row?.source_tick ?? null, phase: row?.phase ?? null,
        running: row?.running ?? null, message: row?.message ?? null,
        error: sample.error ?? row?.error ?? null});
      if (cursorHistory.recent.length > 16) cursorHistory.recent.shift();
      cursorHistory.join = sourceCursorJoin.status();
      // Preserve the actual row and any health/read error before the join
      // validates its source cursor. A malformed row must remain diagnosable.
      if (row) sourceCursorJoin.observe(role, row, observedAt);
      cursorHistory.join = sourceCursorJoin.status();
    };
    const acceptPair = () => {
      const pair = sourceCursorJoin.takePair();
      cursorHistory.join = sourceCursorJoin.status();
      if (!pair) return null;
      requireValue(pair.source_tick >= lastMatchedSourceCursor,
        `Joined source cursor moved backward (${lastMatchedSourceCursor} -> ${pair.source_tick})`);
      lastMatchedSourceCursor = pair.source_tick;
      cursorHistory.last_pair = {source_tick: pair.source_tick,
        observed_at_by_peer: pair.observed_at_by_peer,
        observation_sequence_by_peer: pair.observation_sequence_by_peer};
      return pair.rows;
    };
    const first = await Promise.all(['alpha', 'beta'].map(observePeerSample));
    first.forEach(retain);
    first.forEach(assertPeerSampleHealthy);
    let rows = acceptPair();
    if (rows) return rows;

    while (remaining() > 0) {
      const role = sourceCursorJoin.laggingPeer();
      if (role) {
        const sample = await observePeerSample(role);
        retain(sample);
        assertPeerSampleHealthy(sample);
      } else {
        // Equal latest cursors normally pair immediately. If an unchanged pair
        // was already emitted, fresh reads at that same cursor may still reveal
        // a preparation-state update; they remain independently observed rows.
        const fresh = await Promise.all(['alpha', 'beta'].map(observePeerSample));
        fresh.forEach(retain);
        fresh.forEach(assertPeerSampleHealthy);
      }
      rows = acceptPair();
      if (rows) return rows;
      const nextRole = sourceCursorJoin.laggingPeer() ?? 'alpha';
      await instances[nextRole].page.waitForTimeout(Math.min(2, remaining()));
    }
    return null;
  };
  const waitBoth = async (label, predicate, pollMs = 40,
    {joinSourceCursor = false} = {}) => {
    let last = null;
    const observation = {label, started_at: new Date().toISOString(), polls: 0, recent: []};
    route.current_observation = observation;
    try {
      while (remaining() > 0) {
        last = joinSourceCursor
          ? await observeAtCommonSourceCursor(observation)
          : await observeBoth();
        observation.polls++;
        if (!last) {
          observation.last_rows = null;
          continue;
        }
        // Keep the refusal itself and the recent cursor progression. A stopped
        // owner must remain a failure until its actual message explains why.
        observation.last_rows = last;
        observation.recent.push(last.map(row => ({phase: row.phase, running: row.running,
          source_tick: row.source_tick, match_frame: row.match?.frame ?? null,
          stocks: row.match?.players?.map(player => player.stocks) ?? null,
          message: row.message})));
        if (observation.recent.length > 32) observation.recent.shift();
        if (predicate(last)) return last;
        await alpha.page.waitForTimeout(Math.min(pollMs, remaining()));
      }
      throw Error(`${label} exceeded the scenario deadline`);
    } catch (error) {
      route.failure_observation = {...observation, failed_at: new Date().toISOString(),
        error: String(error?.stack || error)};
      throw error;
    }
  };
  const sourceAligned = rows => Number.isSafeInteger(rows[0]?.source_tick) &&
    rows[0].source_tick === rows[1]?.source_tick;
  const rolePort = role => role === 'alpha' ? 0 : 1;
  const asSignedAxis = (bytes, index) => signedByte(bytes[index]);
  const matchesExpected = (row, expected) => {
    if (!Array.isArray(row?.bytes) || row.bytes.length !== 11) return false;
    if (expected?.button !== undefined) {
      const value = expected.button;
      return row.bytes[0] === ((value >>> 8) & 0xff) && row.bytes[1] === (value & 0xff);
    }
    if (expected?.x && Math.sign(asSignedAxis(row.bytes, 2)) !== expected.x) return false;
    if (expected?.y && Math.sign(asSignedAxis(row.bytes, 3)) !== expected.y) return false;
    return !!(expected?.x || expected?.y);
  };
  const pulse = async (role, keys, {expected, holdMs = 160, releaseMs = 100, label}) => {
    requireValue(Array.isArray(keys) && keys.length > 0, `${label}: keyboard chord is empty`);
    const instance = instances[role];
    const port = rolePort(role);
    const before = await readRoomState(instance);
    assertHealthy(before, role);
    const inputCount = before.owner_health?.peer?.localInputCapture?.captured_count;
    const nativeCursor = before.native_status?.cursor;
    requireValue(Number.isSafeInteger(inputCount) && Number.isSafeInteger(nativeCursor) &&
      (inputCount === nativeCursor || inputCount === nativeCursor + 1),
      `${label}: local-input capture cursor is outside the observed native source boundary`);
    await instance.driver.pressChord(keys, {holdMs, releaseMs});
    let sample = null;
    const captureDeadline = Date.now() + Math.min(1500, remaining());
    while (Date.now() < captureDeadline && !sample) {
      const readback = await readPeerSnapshot(instance, inputCount);
      const captures = readback.health?.peer?.localInputCapture?.captures_since;
      requireValue(Array.isArray(captures), `${label}: native local-input suffix is unavailable`);
      sample = captures.find(row => row.input_tick >= inputCount && row.local_port === port &&
        matchesExpected(row, expected));
      if (!sample) await instance.page.waitForTimeout(20);
    }
    requireValue(sample && sample.source_cursor === sample.input_tick &&
      sample.bytes?.length === 11,
      `${label}: real keyboard chord did not appear in a source-aligned 11-byte PAD capture`);
    const input = await readInputDiagnostics(instance);
    requireValue(input.last_non_neutral?.source === 'keyboard' &&
      input.last_non_neutral?.port === port && (input.keyboard_active_mask & (1 << port)),
      `${label}: the live input observer did not identify the configured keyboard port`);
    const requiredBoundary = sample.input_tick + inputDelay + 1;
    const row = {label, role, port, keys, source_input_tick: sample.input_tick,
      checksum_boundary_required: requiredBoundary, checksum_boundary_reached: false,
      expected_pad_readback: expected, input_diagnostic: input.last_non_neutral,
      keyboard_path: 'browser_driver.pressChord'};
    route.input_actions.push(row);
    await waitForBothChecksumCompare(requiredBoundary, null);
    row.checksum_boundary_reached = true;
    route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
      requiredBoundary);
    return row;
  };
  const screenshotPair = async name => {
    const [a, b] = await Promise.all([
      captureScreenshot(alpha, `full-route-${name}-alpha.png`),
      captureScreenshot(beta, `full-route-${name}-beta.png`),
    ]);
    return {alpha: a, beta: b};
  };

  let cssRows = await observeBoth();
  requireValue(cssRows.every(row => row.phase === 1 && row.css),
    'Full route must start from the connected original CSS phase');
  const cssRoster = cssRows.map((row, index) => ({
    peer: index === 0 ? 'alpha' : 'beta',
    doors: checkMarioTwoHumanCss(row.css, index === 0 ? 'alpha connected CSS' : 'beta connected CSS'),
  }));
  route.css_roster = {
    connected_setup_by_peer: cssRoster,
    no_roster_changes: 'existing Host/Join CSS state retained; both peers observed two Human Mario slots',
    gameplay_keyboard_ports: {alpha: 0, beta: 1},
    p2_gameplay_input: 'neutral; P2 only confirms original Results',
  };
  route.phases.push({phase: 1, label: 'connected original CSS with two Human Mario doors'});
  route.screenshots = {css_two_human: await screenshotPair('css-two-human')};

  await pulse('alpha', ['Enter'], {expected: {button: 0x1000}, holdMs: 160,
    releaseMs: 150, label: 'P1 keyboard Start from CSS to SSS'});
  let sceneRows = await waitBoth('original SSS entry and active source owner', rows =>
    rows.every(row => row.phase === 3 && row.running === 1) && sourceAligned(rows),
  40, {joinSourceCursor: true});
  route.phases.push({phase: 3, label: 'original SSS'});
  route.screenshots.sss_entry = await screenshotPair('sss-entry');

  let selectedStage = null;
  for (let attempt = 0; attempt < MAX_STAGE_KEY_PULSES; attempt++) {
    sceneRows = await waitBoth('active SSS stage observer at a common source tick', rows =>
      rows.every(row => row.phase === 3 && row.running === 1 && row.stage?.ids?.length === 4 &&
        row.stage?.geometry?.length === 6) && sourceAligned(rows),
    40, {joinSourceCursor: true});
    const first = sceneRows[0].stage, second = sceneRows[1].stage;
    const atFd = stage => stage.ids[1] === FD_SOURCE_ID &&
      Math.abs(stage.geometry[2] - stage.geometry[0]) <= 1.125 &&
      Math.abs(stage.geometry[3] - stage.geometry[1]) <= 1.125;
    if (atFd(first) && atFd(second)) {
      selectedStage = first;
      break;
    }
    const dx = first.geometry[2] - first.geometry[0];
    const dy = first.geometry[3] - first.geometry[1];
    const xClose = Math.abs(dx) <= 1.125, yClose = Math.abs(dy) <= 1.125;
    const keys = [!xClose ? (dx > 0 ? 'd' : 'a') : null,
      !yClose ? (dy > 0 ? 'w' : 's') : null].filter(Boolean);
    requireValue(keys.length > 0, 'FD SSS cursor did not converge to the authored target center');
    const expected = {...(!xClose ? {x: Math.sign(dx)} : {}),
      ...(!yClose ? {y: Math.sign(dy)} : {})};
    await pulse('alpha', keys, {expected, holdMs: 40, releaseMs: 80,
      label: `keyboard P1 SSS FD cursor step ${attempt + 1}`});
  }
  requireValue(selectedStage && selectedStage.ids[1] === FD_SOURCE_ID,
    `P1 keyboard did not highlight Final Destination within ${MAX_STAGE_KEY_PULSES} source-observed steps`);
  route.stage = {source_stage_kind: selectedStage.ids[1], selected_tile_index: selectedStage.ids[0],
    target_tile_index: selectedStage.ids[2], input: 'P1 W/A/S/D keyboard chords only',
    observed_by_peer: sceneRows.map((row, index) => ({peer: index === 0 ? 'alpha' : 'beta',
      ids: row.stage.ids, cursor: row.stage.geometry.slice(0, 2),
      target: row.stage.geometry.slice(2, 4)}))};
  route.screenshots.fd_selected = await screenshotPair('fd-selected');
  await pulse('alpha', ['j'], {expected: {button: 0x0100}, holdMs: 160,
    releaseMs: 150, label: 'P1 keyboard A confirms source-selected Final Destination'});
  sceneRows = await waitBoth('original Mario/FD match ready', rows => rows.every(row =>
    row.phase === 7 && row.running === 1 && row.match?.ready === true &&
    row.match?.frame > 0) && sourceAligned(rows), 40, {joinSourceCursor: true});
  const initialMatches = sceneRows.map((row, index) => checkMatchIdentity(row,
    `${index === 0 ? 'alpha' : 'beta'} initial Mario/FD match`));
  assert.deepEqual(initialMatches.map(match => match.players.map(player => player.stocks)),
    [[4, 4], [4, 4]], 'Original Mario/FD match did not begin at [4,4] stocks');
  route.match_start = sceneRows.map((row, index) => ({peer: index === 0 ? 'alpha' : 'beta',
    frame: row.match.frame, rules: row.match.rules, players: row.match.players,
    source_rng: row.match.rng, source_pad_errors: row.match.source_pad_errors}));
  route.phases.push({phase: 7, label: 'active two-Human Mario/Final Destination match'});
  route.screenshots.match_start = await screenshotPair('match-start');

  // P2 receives no gameplay key. One reconciler samples both peers at a common
  // source tick before and after every pulse. A natural stock decrement may
  // occur between pulses, so observations accept only the tracked count or
  // exactly one fewer; a detected loss is recorded once and must reach the
  // native grounded-respawn observation before another keyboard action.
  let stocks = 4;
  const firstPlayer = initialMatches[0].players[0];
  requireValue(Number.isFinite(firstPlayer.x), 'P1 source position is unavailable at match start');
  let outwardKey = firstPlayer.x <= 0 ? 'a' : 'd';
  let lastStableMatches = initialMatches;
  let lastStableRows = sceneRows;
  let lastCompletedAction = null;
  let attemptsForCurrentStock = 0;
  const recordTerminalHandoff = (rows, states, label) => {
    if (!states.some(state => state.handoff)) return;
    route.terminal_handoff = {first_observed_at: label, source_tick: rows[0].source_tick,
      peers: states.map((state, index) => ({peer: index === 0 ? 'alpha' : 'beta',
        state: state.handoff ?? 'same-source-tick-active-peer', phase: rows[index].phase,
        running: rows[index].running, message: rows[index].message ?? null,
        match: state.match}))};
  };
  const reconcileP1Stock = async label => {
    const rows = await waitBoth(label, candidates => {
      if (!sourceAligned(candidates)) return false;
      const states = candidates.map((row, index) => classifyStockObservation(row,
        `${index === 0 ? 'alpha' : 'beta'} ${label}`));
      if (states.some(state => state.wait === 'first-use-rendering')) return false;
      const matches = states.map(state => state.match);
      assert(matches.every(match => match.players[1].stocks === 4),
        `${label}: P2 lost a stock during P1 walk-off`);
      const observed = matches.map(match => match.players[0].stocks);
      assert.equal(observed[0], observed[1],
        `${label}: peers disagree on P1 stocks at common source tick ${candidates[0].source_tick}`);
      assert(observed[0] === stocks || observed[0] === stocks - 1,
        `${label}: P1 stock observation skipped or reversed a boundary (${stocks} -> ${observed[0]})`);
      recordTerminalHandoff(candidates, states, label);
      return true;
    }, 40, {joinSourceCursor: true});
    const states = rows.map((row, index) => classifyStockObservation(row,
      `${index === 0 ? 'alpha' : 'beta'} ${label}`));
    const matches = states.map(state => state.match);
    recordTerminalHandoff(rows, states, label);
    requireValue(rows[0].source_tick >= lastStableRows[0].source_tick,
      `${label}: common source cursor moved backward`);
    const observedStocks = matches[0].players[0].stocks;
    if (observedStocks === stocks) {
      lastStableRows = rows;
      lastStableMatches = matches;
      return {loss: false, rows, matches};
    }

    const beforeMatches = lastStableMatches;
    const beforeRows = lastStableRows;
    assert.equal(observedStocks, stocks - 1,
      `${label}: stock count did not decrease by exactly one`);
    stocks = observedStocks;
    const loss = {
      remaining_p1_stocks: stocks,
      before_frame: beforeMatches[0].frame,
      first_observed_frame: matches[0].frame,
      first_observed_source_tick: rows[0].source_tick,
      before_source_tick: beforeRows[0].source_tick,
      observed_after_action_label: lastCompletedAction?.label ?? null,
      attribution: 'observation window only; no causal claim about the keyboard pulse',
      observed_by_peer: matches.map((match, index) => ({
        peer: index === 0 ? 'alpha' : 'beta', source_frame: match.frame,
        stocks: match.players.map(player => player.stocks),
        position: match.players[0].x, motion: match.players[0].motion,
        groundAir: match.players[0].groundAir,
      })),
    };
    route.stock_losses.push(loss);
    if (stocks === 0) return {loss: true, terminal: true, rows, matches};

    const lossFrame = Math.max(...matches.map(match => match.frame));
    const respawnRows = await waitBoth(`P1 source respawn after stock loss ${4 - stocks}`,
      candidates => candidates.every(row => row.phase === 7 && row.running === 1 &&
        row.match?.ready === true && row.match?.players?.[0]?.stocks === stocks &&
        row.match.frame > lossFrame && row.match.players[0].motion === 14 &&
        row.match.players[0].groundAir === 0) && sourceAligned(candidates),
    16, {joinSourceCursor: true});
    const respawnMatches = respawnRows.map((row, index) => checkMatchIdentity(row,
      `${index === 0 ? 'alpha' : 'beta'} grounded respawn after stock loss`));
    const respawns = respawnMatches.map((match, index) => ({peer: index === 0 ? 'alpha' : 'beta',
      source_frame: match.frame, stocks: match.players[0].stocks, x: match.players[0].x,
      motion: match.players[0].motion, groundAir: match.players[0].groundAir}));
    route.respawns.push({after_loss: 4 - stocks,
      source_signal: 'native match observer motion=14 and groundAir=0 after the recorded stock decrement',
      peers: respawns});
    outwardKey = respawnMatches[0].players[0].x <= 0 ? 'a' : 'd';
    lastStableRows = respawnRows;
    lastStableMatches = respawnMatches;
    return {loss: true, terminal: false, rows: respawnRows, matches: respawnMatches};
  };
  while (stocks > 0) {
    const current = await reconcileP1Stock('P1 stock before next keyboard pulse');
    if (current.loss) {
      attemptsForCurrentStock = 0;
      continue;
    }
    if (attemptsForCurrentStock >= MAX_WALKOFF_PULSES_PER_STOCK)
      throw Error(`P1 did not lose exactly one stock within ${MAX_WALKOFF_PULSES_PER_STOCK} keyboard walk-off pulses`);
    const p0 = current.matches[0].players[0];
    if (Math.abs(p0.x) > 0.5) outwardKey = p0.x < 0 ? 'a' : 'd';
    lastCompletedAction = await pulse('alpha', [outwardKey], {
      expected: {x: outwardKey === 'a' ? -1 : 1},
      holdMs: WALKOFF_HOLD_MS, releaseMs: WALKOFF_RELEASE_MS,
      label: `P1 outward ${outwardKey.toUpperCase()} walk-off attempt from ${stocks} stocks`});
    attemptsForCurrentStock++;
    const afterPulse = await reconcileP1Stock('P1 stock after completed keyboard pulse');
    if (afterPulse.loss) attemptsForCurrentStock = 0;
  }

  const terminalRows = await waitBoth('original elimination Results entry', rows => {
    if (!sourceAligned(rows)) return false;
    const states = rows.map((row, index) => {
      const label = `${index === 0 ? 'alpha' : 'beta'} terminal handoff`;
      if (row.phase === 8) {
        const match = checkMatchIdentity(row, label, {active: false, phase: 8});
        assert.deepEqual(match.terminal, {outcome: 2, winners: [1]},
          `${label}: P2 must win the naturally completed original elimination match`);
        return {state: 'results', match, ready: true};
      }
      if (row.phase === 7 && row.running === 1 && row.match?.players?.[0]?.stocks === 0)
        return {state: 'active-match-end',
          match: checkMatchIdentity(row, label, {allowCompletion: true}), ready: false};
      if (row.phase === 7 && row.running === 0) {
        const stopped = classifyStockObservation(row, label);
        if (stopped.wait === 'first-use-rendering')
          return {state: 'first-use-rendering-wait', match: stopped.match, ready: false};
        return {state: 'preparing-results', match: stopped.match, ready: false};
      }
      if (row.phase === 5 && row.running === 0)
        return {state: 'results-assets-ready', match: checkAssetTransferredTerminal(row, label), ready: false};
      throw Error(`${label}: unsupported phase/running state ${row.phase}/${row.running}`);
    });
    if (states.some(state => state.state !== 'results'))
      route.terminal_handoff = {first_observed_at: 'awaiting original Results',
        source_tick: rows[0].source_tick,
        peers: states.map((state, index) => ({peer: index === 0 ? 'alpha' : 'beta',
          state: state.state, phase: rows[index].phase, running: rows[index].running,
          message: rows[index].message ?? null, match: state.match}))};
    return states.every(state => state.ready);
  }, 40, {joinSourceCursor: true});
  for (const [index, row] of terminalRows.entries()) {
    const label = index === 0 ? 'alpha terminal' : 'beta terminal';
    assert.deepEqual(row.match.terminal, {outcome: 2, winners: [1]},
      `${label}: P2 must win the naturally completed original elimination match`);
    assert.deepEqual(row.match.players.map(player => player.stocks), [0, 4],
      `${label}: final original stocks must be [0,4]`);
    assert.equal(row.match.rules.stage, FD_SOURCE_ID, `${label}: source stage changed before Results`);
  }
  route.terminal = terminalRows.map((row, index) => ({peer: index === 0 ? 'alpha' : 'beta',
    phase: row.phase, match: row.match}));
  route.phases.push({phase: 8, label: 'original Results after P1 elimination'});
  route.screenshots.results = await screenshotPair('results');

  const resultsRetention = {};
  const observeResults = async label => {
    const row = await observeRuntimeOwner(alpha.page, resultsRetention, label,
      {includeMatch: true, includeResultsTrace: true});
    requireValue(row.state?.error == null, `${label}: original Results native error ${row.state?.error}`);
    return {host: row.state, trace: row.trace};
  };
  const p1ResultsPress = async (key, options = {}) => pulse('alpha', [key], {
    expected: {button: 0x1000}, holdMs: 160, releaseMs: options.releaseMs ?? 150,
    label: `P1 Results ${key} keyboard confirmation`});
  const p2ResultsPress = async (key, options = {}) => pulse('beta', [key], {
    expected: {button: 0x1000}, holdMs: 160, releaseMs: options.releaseMs ?? 150,
    label: `P2 Results ${key} keyboard confirmation`});
  const resultsDeadline = Date.now() + Math.max(1, remaining());
  const confirmation = await confirmTwoHumanResults({deadlineAt: resultsDeadline,
    sourcePorts: [0, 1], observeSample: observeResults, press: p1ResultsPress,
    pressSecond: () => p2ResultsPress('End'), wait: ms => alpha.page.waitForTimeout(ms)});
  let prizeReturn = null;
  if (confirmation.original_exit.host_phase === 9) {
    prizeReturn = await returnFromCompetitivePrize({deadlineAt: resultsDeadline,
      sourcePorts: [0, 1], observeSample: observeResults,
      press: p1ResultsPress, wait: ms => alpha.page.waitForTimeout(ms)});
  }
  route.results = {confirmation, prize_return: prizeReturn,
    source_trace: resultsRetention.latestOwnerObservation?.trace ?? null};
  cssRows = await waitBoth('original CSS returned from Results', rows => rows.every(row =>
    row.phase === 1 && row.css?.doors?.length === 40));
  route.returned_css = {peers: cssRows.map((row, index) => ({peer: index === 0 ? 'alpha' : 'beta',
    phase: row.phase, doors: checkMarioTwoHumanCss(row.css,
      `${index === 0 ? 'alpha' : 'beta'} Results return`)})),
    result_source_phase: confirmation.original_exit.host_phase};
  route.phases.push({phase: 1, label: 'original CSS after Results confirmation'});
  route.screenshots.returned_css = await screenshotPair('returned-css');
  const returnRows = await Promise.all(['alpha', 'beta'].map(role => readRoomState(instances[role])));
  const returnedCursors = returnRows.map((row, index) => {
    assertHealthy(row, index === 0 ? 'alpha' : 'beta');
    const cursor = row.native_status?.cursor;
    requireValue(Number.isSafeInteger(cursor) && cursor >= 0,
      `${index === 0 ? 'alpha' : 'beta'} returned-CSS source cursor is unavailable`);
    return cursor;
  });
  const returnedCssBoundary = Math.max(...returnedCursors) + inputDelay + 1;
  route.returned_css.source_cursor_by_peer = {alpha: returnedCursors[0], beta: returnedCursors[1]};
  route.returned_css.checksum_boundary_after_confirmation_and_delay = returnedCssBoundary;
  route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
    returnedCssBoundary);
  route.result = 'awaiting_final_checksum_validation';
  return route;
}
