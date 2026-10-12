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
import {comparePairedCombatPlayers, decideP2ApproachTap,
  inspectCombatPairRetainingRawRows, observePairedCombatJab,
  retainCombatApproachInputReadback, retainCombatApproachState, validateCombatItemsNone,
  validateP2GameplayInputWindow, validateSingleP2ApproachTapWindow}
  from './a3_local_room_combat_probe.mjs';
export {validateP2GameplayInputWindow} from './a3_local_room_combat_probe.mjs';

/** Synchronous read-only browser predicate for an explicitly observed key press. */
export function observeDesiredSourcePad({inputFrom, port, expected, maxSourceCursor = null}) {
  const runtimeError=globalThis.document?.querySelector('#status')?.dataset.runtimeError;
  if(runtimeError)throw Error(runtimeError);
  const health=globalThis.meleeNetRuntimeLockstepHealth?.({inputFrom});
  if(maxSourceCursor !== null) {
    const cursor=globalThis.__net?.status?.()?.cursor;
    if(!Number.isSafeInteger(cursor)||cursor>maxSourceCursor)
      throw Error('Source PAD observation exceeded its declared source-cursor budget');
  }
  if(health?.failure||health?.peer?.failure||health?.peer?.protocol?.terminal)
    throw Error('Source PAD observation encountered a runtime owner failure or terminal');
  const rows=health?.peer?.localInputCapture?.captures_since;
  if(!Array.isArray(rows))throw Error('Source PAD observation suffix is unavailable');
  for(const row of rows) {
    const bytes=row?.bytes;
    if(!Number.isSafeInteger(row?.input_tick)||row.input_tick<inputFrom||
       row.local_port!==port||row.source_cursor!==row.input_tick||
       !Array.isArray(bytes)||bytes.length!==11||
       !bytes.every(value=>Number.isInteger(value)&&value>=0&&value<=255))continue;
    const signed=value=>value>=128?value-256:value;
    const matches=expected?.button!==undefined
      ? bytes[0]===((expected.button>>>8)&255)&&bytes[1]===(expected.button&255)
      : !!(expected?.x||expected?.y)&&
        (!expected.x||Math.sign(signed(bytes[2]))===expected.x)&&
        (!expected.y||Math.sign(signed(bytes[3]))===expected.y);
    if(matches)return row;
  }
  return false;
}

const FD_SOURCE_ID = 0x20;
const MARIO_SOURCE_ID = 8;
const MARIO_FIGHTER_ID = 0;
const MAX_STAGE_KEY_PULSES = 240;
const MAX_WALKOFF_PULSES_PER_STOCK = 50;
const WALKOFF_HOLD_MS = 250;
const WALKOFF_RELEASE_MS = 25;
const FIRST_USE_RENDERING_MESSAGE = 'Preparing first-use rendering...';
const ACTION_HOLD_MIN_SOURCE_TICKS = 10;
const SOURCE_TICK_RATE_HZ = 60;

function requireValue(condition, message) {
  if (!condition) throw Error(message);
}

// Preserve the source-authored animation cursor in the optional action evidence.
// This is a diagnostic projection only; it does not add an action pass gate.
export function projectActionObservationPlayer(player) {
  requireValue(Number.isFinite(player?.animation_frame),
    'Action observation lacks a finite native animation_frame');
  return {source_port: player.source_port, stocks: player.stocks,
    motion: player.motion, groundAir: player.groundAir,
    damage_percent: player.damage_percent, x: player.x, y: player.y,
    animation_frame: player.animation_frame};
}

// Compare both original source slots at one already-paired common cursor.
// A disagreement is retained as evidence and does not change the existing
// motion/grounded-Wait acceptance predicates.
export function comparePairedActionAnimationFrames(matches) {
  requireValue(Array.isArray(matches) && matches.length === 2 &&
    matches.every(match => Array.isArray(match?.players) && match.players.length === 2),
  'Action animation comparison requires two paired native players');
  return matches[0].players.map((player, index) => {
    const paired = matches[1].players[index];
    requireValue(Number.isSafeInteger(player.source_port) &&
      paired.source_port === player.source_port,
    'Action animation comparison changed source player slots');
    const alphaFrame = projectActionObservationPlayer(player).animation_frame;
    const betaFrame = projectActionObservationPlayer(paired).animation_frame;
    return {source_port: player.source_port,
      alpha_animation_frame: alphaFrame, beta_animation_frame: betaFrame,
      equal_at_common_source_cursor: alphaFrame === betaFrame};
  });
}

function signedByte(value) {
  return value > 127 ? value - 256 : value;
}

function isNeutralPadRow(row) {
  return Array.isArray(row?.bytes) && row.bytes.length === 11 &&
    row.bytes.every(value => value === 0);
}

// Validate one bounded literal keyboard-button window and the other port's
// simultaneous neutral records. This only checks captured source PAD rows.
export function validateButtonGameplayInputWindow({actionRows, otherPortRows,
  pressTick, releaseTick, port, button}) {
  requireValue(Number.isSafeInteger(pressTick) && Number.isSafeInteger(releaseTick) &&
    releaseTick > pressTick, 'Button input window has invalid source ticks');
  requireValue((port === 0 || port === 1) && Number.isSafeInteger(button) &&
    button > 0 && button <= 0xffff && (button & (button - 1)) === 0,
  'Button input window has invalid local port or one-hot button mask');
  const exactRows = (rows, expectedPort, label) => {
    const window = rows.filter(row => row?.input_tick >= pressTick &&
      row.input_tick <= releaseTick);
    assert.equal(window.length, releaseTick - pressTick + 1,
      label + ': literal button window has a missing or duplicate source tick');
    for (let offset = 0; offset < window.length; offset++) {
      const row = window[offset];
      const tick = pressTick + offset;
      assert.equal(row.input_tick, tick, label + ': input ticks are not in source order');
      assert.equal(row.source_cursor, tick, label + ': input record is not source-cursor aligned');
      assert.equal(row.local_port, expectedPort, label + ': local input port changed');
      assert(Array.isArray(row.bytes) && row.bytes.length === 11 &&
        row.bytes.every(value => Number.isInteger(value) && value >= 0 && value <= 255),
      label + ': input row is not an exact 11-byte PAD record');
    }
    return window;
  };
  const active = exactRows(actionRows, port, 'Action port');
  const other = exactRows(otherPortRows, 1 - port, 'Other port');
  const buttons = row => (row.bytes[0] << 8) | row.bytes[1];
  const heldRows = active.slice(0, -1);
  assert(heldRows.some(row => buttons(row) === button),
    'Button input window contains no literal held sample');
  for (const row of heldRows) {
    assert.equal(buttons(row), button,
      'Requested button changed before the literal neutral release');
    assert(row.bytes.slice(2).every(value => value === 0),
      'Button press row contains an unexpected axis, trigger, or extra PAD value');
  }
  assert(isNeutralPadRow(active.at(-1)),
    'Button release is not a literal all-zero 11-byte PAD row');
  assert(other.every(isNeutralPadRow),
    'Other port was not literally neutral across the button input window');
  return {press_input_tick: pressTick, neutral_release_input_tick: releaseTick,
    local_port: port, button, action_sample_count: active.length - 1,
    other_port_neutral_count: other.length};
}

// Actor motion counts as a matched action only when both peer snapshots at
// the caller's already-joined source cursor report the exact same source ID.
export function matchPairedActorMotion(motions, targetMotions) {
  if (!Array.isArray(motions) || motions.length !== 2 ||
      !motions.every(Number.isSafeInteger) || !Array.isArray(targetMotions)) return null;
  return motions[0] === motions[1] && targetMotions.includes(motions[0])
    ? motions[0] : null;
}

// Before the press sample identifies its source window, retain distinct
// gameplay/preparation observations at the same cursor. Network counters are
// intentionally excluded: a wait poll alone must not evict a short action.
export function retainPreStartActionPair(buffer, rows) {
  requireValue(Array.isArray(buffer) && Array.isArray(rows) && rows.length === 2 &&
    Number.isSafeInteger(rows[0]?.source_tick) &&
    rows[0].source_tick === rows[1]?.source_tick,
  'Pre-start action pair requires two actual common-source rows');
  const key = JSON.stringify({source_tick: rows[0].source_tick,
    peers: rows.map(row => ({phase: row.phase, running: row.running,
      message: row.message, match: row.match}))});
  if (buffer.some(entry => entry.key === key)) return false;
  buffer.push({key, rows});
  if (buffer.length > 128) buffer.shift();
  return true;
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
  alpha, beta, report, inputDelay, remaining, readRoomState, readPeerSnapshot, readPeerInputSuffix,
  readInputDiagnostics, waitForBothChecksumCompare, captureScreenshot,
  assertHealthy, gameplayActionProbe = false, gameplayCombatProbe = false,
}) {
  requireValue(!(gameplayActionProbe && gameplayCombatProbe),
    'Gameplay action and combat probes are separate route scenarios');
  const route = {
    result: 'running', scope: 'local keyboard route/lifecycle witness only',
    phases: [], css_roster: [], stage: null, match_start: [], stock_losses: [],
    respawns: [], input_actions: [], terminal_handoff: null, results: null, returned_css: null,
    required_checksum_boundary: null, gameplay_action_probe: gameplayActionProbe,
    gameplay_combat_probe: gameplayCombatProbe,
    exclusions: ['no PAD/Gamepad input injection',
      gameplayActionProbe
        ? 'one short P2 gameplay axis pulse, one P1 grounded jab, and one P2 jump with source motion evidence; no combat-coverage claim'
        : gameplayCombatProbe
          ? 'bounded P2 inward keyboard approach and one P2 jab with paired source damage response; no retail-equivalence or KO claim'
          : 'one short P2 gameplay axis pulse; attack/jump probe not requested, no combat-coverage claim',
      'no forced stock/result or native match-state mutation',
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
  const observeAtCommonSourceCursor = async (observation, shouldStop = () => false,
    sourceCursorLimit = null) => {
    // Each acquisition begins from fresh peer samples. This prevents a common
    // row retained before a keyboard action from satisfying the next wait.
    const sourceCursorJoin = createSourceCursorJoin({retainedCursorsPerPeer: 8});
    const cursorHistory = observation.source_cursor_join ??= {
      samples_by_peer: {alpha: 0, beta: 0}, last_by_peer: {}, recent: [], last_pair: null,
    };
    const retain = sample => {
      const {role, row, observed_at: observedAt} = sample;
      const overBudget = sourceCursorLimit !== null && Number.isSafeInteger(row?.source_tick) &&
        row.source_tick > sourceCursorLimit;
      cursorHistory.samples_by_peer[role]++;
      cursorHistory.last_by_peer[role] = {observed_at: observedAt,
        source_tick: row?.source_tick ?? null, phase: row?.phase ?? null,
        running: row?.running ?? null, message: row?.message ?? null,
        error: sample.error ?? row?.error ?? null,
        ...(sourceCursorLimit !== null ? {match: row?.match ?? null} : {}),
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
      if (overBudget) {
        cursorHistory.source_cursor_limit_refusal = {role,
          source_tick: row.source_tick, source_cursor_limit: sourceCursorLimit,
          observed_at: observedAt};
        throw Error(`${observation.label}: source-cursor budget exhausted during observation acquisition`);
      }
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
    if (shouldStop()) return null;
    let rows = acceptPair();
    if (rows) return rows;

    while (remaining() > 0 && !shouldStop()) {
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
    {joinSourceCursor = false, sourceCursorLimit = null} = {}) => {
    let last = null;
    const observation = {label, started_at: new Date().toISOString(), polls: 0, recent: []};
    route.current_observation = observation;
    try {
      while (remaining() > 0) {
        last = joinSourceCursor
          ? await observeAtCommonSourceCursor(observation, () => false, sourceCursorLimit)
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
  const pulse = async (role, keys, {expected, holdMs = 160, releaseMs = 100,
    label, retainSample = false, observePress = false, sourceCursorLimit = null,
    evidenceSink = null}) => {
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
    if (sourceCursorLimit !== null)
      requireValue(nativeCursor <= sourceCursorLimit,
        `${label}: approach source-cursor budget exhausted before input`);
    // Only explicitly opted-in short taps use bounded source-owned readback.
    const pressObservationCap = observePress ? Math.min(500, Math.floor(remaining() - releaseMs - 1)) : null;
    let observed;
    try {
      observed = await instance.driver.pressChord(keys, {holdMs, releaseMs,
        ...(observePress ? {observeBeforeRelease: {predicate: observeDesiredSourcePad,
          arg: {inputFrom: inputCount, port, expected, maxSourceCursor: sourceCursorLimit},
          timeoutMs: pressObservationCap}} : {})});
    } catch (error) {
      if (evidenceSink) {
        evidenceSink.press_observer_error = String(error?.stack || error);
        try {
          const failureReadback = await readPeerInputSuffix(instance, inputCount);
          const failureCaptures = failureReadback.health?.peer?.localInputCapture?.captures_since;
          evidenceSink.last_input_suffix_readback = {
            native_cursor: failureReadback.native_status?.cursor ?? null,
            runtime_error: failureReadback.runtime_error ?? null,
            captures_since: failureCaptures ?? null};
          const failedSample = Array.isArray(failureCaptures) && failureCaptures.find(row =>
            row.input_tick >= inputCount && row.local_port === port &&
            matchesExpected(row, expected));
          if (failedSample) evidenceSink.movement = {...failedSample, bytes: [...failedSample.bytes]};
        } catch (readbackError) {
          evidenceSink.press_failure_readback_error = String(readbackError?.stack || readbackError);
        }
      }
      throw error;
    }
    let sample = observePress ? observed : null;
    if (evidenceSink && sample) evidenceSink.movement = {...sample, bytes: [...sample.bytes]};
    const captureDeadline = Date.now() + Math.min(1500, remaining());
    while (Date.now() < captureDeadline && !sample) {
      const readback = await readPeerInputSuffix(instance, inputCount);
      const captures = readback.health?.peer?.localInputCapture?.captures_since;
      if (evidenceSink) evidenceSink.last_input_suffix_readback = {
        native_cursor: readback.native_status?.cursor ?? null,
        runtime_error: readback.runtime_error ?? null,
        captures_since: captures ?? null};
      sample = Array.isArray(captures) ? captures.find(row => row.input_tick >= inputCount &&
        row.local_port === port && matchesExpected(row, expected)) : null;
      if (sample && evidenceSink) evidenceSink.movement = {...sample, bytes: [...sample.bytes]};
      requireValue(Array.isArray(captures), `${label}: native local-input suffix is unavailable`);
      if (sourceCursorLimit !== null)
        requireValue(Number.isSafeInteger(readback.native_status?.cursor) &&
          readback.native_status.cursor <= sourceCursorLimit,
          `${label}: approach source-cursor budget exhausted while reading input`);
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
      keyboard_path: 'browser_driver.pressChord',
      ...(observePress ? {press_release_policy: 'first actual desired source-owned PAD capture',
        press_observation_cap_ms: pressObservationCap} : {})};
    if (retainSample) {
      row.observed_pad_sample = {...sample, bytes: [...sample.bytes]};
    }
    route.input_actions.push(row);
    await waitForBothChecksumCompare(requiredBoundary, null, undefined,
      sourceCursorLimit === null ? null : {sourceCursorLimit, evidenceSink: evidenceSink ?? row});
    if (sourceCursorLimit !== null) {
      const settledStates = await Promise.all(['alpha', 'beta'].map(currentRole =>
        readRoomState(instances[currentRole])));
      settledStates.forEach((state, index) => assertHealthy(state, index === 0 ? 'alpha' : 'beta'));
      requireValue(settledStates.every(state => Number.isSafeInteger(state.native_status?.cursor) &&
        state.native_status.cursor <= sourceCursorLimit),
      `${label}: approach source-cursor budget exhausted while settling the checksum boundary`);
    }
    row.checksum_boundary_reached = true;
    route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
      requiredBoundary);
    return row;
  };
  const waitForP2InputWindow = async (movementTick,
    {evidenceSink = p2Prefix.input_window, sourceCursorLimit = null} = {}) => {
    const deadline = Date.now() + Math.min(1500, remaining());
    while (Date.now() < deadline) {
      const p1State = await readRoomState(alpha);
      const p1CaptureCount = sourceCursorLimit === null
        ? p1State.owner_health?.peer?.localInputCapture?.captured_count
        : retainCombatApproachState({evidenceSink, state: p1State, sourceCursorLimit});
      assertHealthy(p1State, 'alpha');
      requireValue(Number.isSafeInteger(p1CaptureCount) && p1CaptureCount >= 0,
        'P1 local input capture count is unavailable during the P2 prefix');
      const [p2Readback, p1Readback] = await Promise.all([
        readPeerInputSuffix(beta, movementTick),
        readPeerInputSuffix(alpha, Math.min(movementTick, p1CaptureCount)),
      ]);
      const p2Captures = p2Readback.health?.peer?.localInputCapture?.captures_since;
      const p1Captures = p1Readback.health?.peer?.localInputCapture?.captures_since;
      if (sourceCursorLimit !== null) {
        retainCombatApproachInputReadback({evidenceSink, p2Readback, p1Readback,
          movementTick, sourceCursorLimit});
      } else if (Array.isArray(p2Captures) && Array.isArray(p1Captures)) {
        const p2Rows = p2Captures.filter(row => row.input_tick >= movementTick);
        const observedEnd = p2Rows.at(-1)?.input_tick ?? null;
        evidenceSink.p2_literal_rows = p2Rows;
        evidenceSink.p1_literal_rows = p1Captures.filter(row =>
          row.input_tick >= movementTick && (observedEnd === null || row.input_tick <= observedEnd));
        evidenceSink.observed_capture_end_tick = observedEnd;
      }
      requireValue(p2Readback.runtime_error == null && p1Readback.runtime_error == null,
        `P2 input-window capture observed a runtime error (${p2Readback.runtime_error || p1Readback.runtime_error})`);
      requireValue(p2Readback.health?.failure == null && p1Readback.health?.failure == null &&
        p2Readback.health?.peer?.failure == null && p1Readback.health?.peer?.failure == null &&
        p2Readback.native_status?.local_capture_failure == null &&
        p1Readback.native_status?.local_capture_failure == null,
      'P2 input-window capture observed a runtime-owner failure');
      requireValue(Array.isArray(p2Captures) && Array.isArray(p1Captures),
        'P2 input-window literal local capture suffix is unavailable');
      const p2FromMovement = p2Captures.filter(row => row.input_tick >= movementTick);
      const observedEnd = p2FromMovement.at(-1)?.input_tick ?? null;
      const release = p2FromMovement.find((row, index) => index > 0 && isNeutralPadRow(row));
      if (release && p1Captures.some(row => row.input_tick === release.input_tick)) {
        const p2Window = p2FromMovement.filter(row => row.input_tick <= release.input_tick);
        const p1Window = p1Captures.filter(row => row.input_tick >= movementTick &&
          row.input_tick <= release.input_tick);
        const direction = Math.sign(signedByte(p2Window[0]?.bytes?.[2] ?? 0));
        const validated = validateP2GameplayInputWindow({p2Rows: p2Window, p1Rows: p1Window,
          movementTick, releaseTick: release.input_tick, direction});
        return {validated, p2Window, p1Window};
      }
      await beta.page.waitForTimeout(Math.min(20, Math.max(1, deadline - Date.now())));
    }
    throw Error('Literal P2 movement and neutral-release capture window did not settle');
  };

  const readCommonNeutralInputs = async (sourceTick, minimumInputTick, sourceCursorLimit = null,
    evidenceSink = null) => {
    requireValue(Number.isSafeInteger(sourceTick) && Number.isSafeInteger(minimumInputTick),
      'Grounded-neutral baseline is missing a source/input boundary');
    const states = await Promise.all(['alpha', 'beta'].map(role => readRoomState(instances[role])));
    if (evidenceSink) evidenceSink.room_state_readbacks = states.map(state => ({
      native_cursor: state.native_status?.cursor ?? null,
      captured_count: state.owner_health?.peer?.localInputCapture?.captured_count ?? null,
      runtime_error: state.runtime_error ?? null,
      owner_failure: state.owner_health?.failure ?? null,
      peer_failure: state.owner_health?.peer?.failure ?? null,
      local_capture_failure: state.native_status?.local_capture_failure ?? null}));
    states.forEach((state, index) => assertHealthy(state, index === 0 ? 'alpha' : 'beta'));
    if (sourceCursorLimit !== null && states.some(state =>
        !Number.isSafeInteger(state.native_status?.cursor) || state.native_status.cursor > sourceCursorLimit))
      throw Error('Grounded-neutral baseline exceeded the approach source-cursor budget while acquiring room state');
    const counts = states.map(state => state.owner_health?.peer?.localInputCapture?.captured_count);
    requireValue(counts.every(Number.isSafeInteger) && counts.every(count => count > 0),
      'Grounded-neutral baseline local-input counts are unavailable');
    const commonTick = Math.min(sourceTick, counts[0] - 1, counts[1] - 1);
    if (commonTick < minimumInputTick || sourceTick - commonTick > 1) return null;
    const snapshots = await Promise.all(['alpha', 'beta'].map(role =>
      readPeerInputSuffix(instances[role], commonTick)));
    if (evidenceSink) evidenceSink.local_input_readbacks = snapshots.map(snapshot => ({
      native_cursor: snapshot.native_status?.cursor ?? null,
      runtime_error: snapshot.runtime_error ?? null,
      owner_failure: snapshot.health?.failure ?? null,
      peer_failure: snapshot.health?.peer?.failure ?? null,
      local_capture_failure: snapshot.native_status?.local_capture_failure ?? null,
      captures_since: snapshot.health?.peer?.localInputCapture?.captures_since ?? null}));
    if (sourceCursorLimit !== null && snapshots.some(snapshot =>
        !Number.isSafeInteger(snapshot.native_status?.cursor) || snapshot.native_status.cursor > sourceCursorLimit))
      throw Error('Grounded-neutral baseline exceeded the approach source-cursor budget while acquiring input suffixes');
    snapshots.forEach((snapshot, index) => {
      const role = index === 0 ? 'alpha' : 'beta';
      requireValue(snapshot.runtime_error == null &&
        snapshot.health?.failure == null && snapshot.health?.peer?.failure == null &&
        snapshot.native_status?.local_capture_failure == null,
      role + ': grounded-neutral baseline observed a runtime/input-capture failure');
    });
    const localRows = snapshots.map((snapshot, index) => {
      const rows = snapshot.health?.peer?.localInputCapture?.captures_since;
      requireValue(Array.isArray(rows),
        (index === 0 ? 'alpha' : 'beta') + ': local PAD suffix is unavailable');
      const row = rows.find(sample => sample.input_tick === commonTick &&
        sample.local_port === rolePort(index === 0 ? 'alpha' : 'beta'));
      requireValue(row && row.source_cursor === commonTick &&
        Array.isArray(row.bytes) && row.bytes.length === 11,
      (index === 0 ? 'alpha' : 'beta') + ': common-source neutral PAD row is missing');
      return {...row, bytes: [...row.bytes]};
    });
    if (!localRows.every(isNeutralPadRow)) return null;
    return {input_tick: commonTick, alpha: localRows[0], beta: localRows[1]};
  };
  const compactGroundedBaseline = (rows, matches, localInputs, label,
    {includeRules = false} = {}) => ({
    label, source_tick: rows[0].source_tick,
    peers: matches.map((match, index) => ({peer: index === 0 ? 'alpha' : 'beta',
      frame: match.frame, ...(includeRules ? {rules: match.rules} : {}),
      players: match.players.map(projectActionObservationPlayer)})),
    local_input_tick: localInputs.input_tick,
    local_inputs: {alpha: localInputs.alpha, beta: localInputs.beta},
  });
  const waitForGroundedNeutralBaseline = async (label, minimumInputTick,
    {recordSink = route.action_prefix.baselines, sourceCursorLimit = null,
      includeRules = false} = {}) => {
    if (sourceCursorLimit !== null && !Number.isSafeInteger(sourceCursorLimit))
      throw Error(label + ': source-cursor limit is invalid');
    while (remaining() > 0) {
      const rows = await waitBoth(label + ' at a common source cursor', candidates => {
        if (sourceCursorLimit !== null && sourceAligned(candidates) &&
            candidates[0].source_tick > sourceCursorLimit)
          throw Error(label + ': approach source-cursor budget exhausted during baseline acquisition');
        return sourceAligned(candidates) && candidates.every(row => row.phase === 7 &&
          row.running === 1 && row.match?.ready === true &&
          row.match?.players?.length === 2 &&
          row.match.players.every(player => player.stocks === 4 &&
            player.motion === 14 && player.groundAir === 0));
      }, 2, {joinSourceCursor: true, sourceCursorLimit});
      const matches = rows.map((row, index) => checkMatchIdentity(row,
        (index === 0 ? 'alpha ' : 'beta ') + label));
      const acquisitionEvidence = sourceCursorLimit === null ? null : {
        label, source_tick: rows[0].source_tick,
        peers: rows.map((row, index) => ({peer: index === 0 ? 'alpha' : 'beta',
          phase: row.phase, running: row.running, match: row.match})),
        room_state_readbacks: null, local_input_readbacks: null};
      if (acquisitionEvidence) route.combat_probe.last_baseline_acquisition = acquisitionEvidence;
      const localInputs = await readCommonNeutralInputs(rows[0].source_tick, minimumInputTick,
        sourceCursorLimit, acquisitionEvidence);
      if (localInputs) {
        const reportRow = compactGroundedBaseline(rows, matches, localInputs, label,
          {includeRules});
        recordSink.push(reportRow);
        return {rows, matches, localInputs, report: reportRow};
      }
      await alpha.page.waitForTimeout(Math.min(2, remaining()));
    }
    throw Error(label + ': grounded neutral [4,4] baseline did not settle before the route deadline');
  };
  const createActionObservation = ({label, role, targetMotions, record,
    pairGuard = null, pairResponse = null, includeRules = false,
    retainInspectedPair = false}) => {
    const actor = rolePort(role);
    let pendingBeforeStart = [];
    let lastRetainedTick = -1;
    let lastRenderWaitKey = null;
    let actionStartTick = null;
    let stopped = false;
    let cancelled = false;
    const joinObservation = {label, source_cursor_join: {
      samples_by_peer: {alpha: 0, beta: 0}, last_by_peer: {}, recent: [], last_pair: null,
    }};
    const inspectPair = async rows => {
      if (retainInspectedPair) inspectCombatPairRetainingRawRows(record, rows);
      if (!sourceAligned(rows)) throw Error(label + ': action observer lost common source alignment');
      const tick = rows[0].source_tick;
      if (actionStartTick === null) {
        retainPreStartActionPair(pendingBeforeStart, rows);
        return;
      }
      if (tick < actionStartTick || tick <= lastRetainedTick) return;
      const endExclusive = actionStartTick + 60;
      if (tick >= endExclusive) {
        stopped = true;
        return;
      }
      const classifyRows = sourceRows => sourceRows.map((row, index) => {
        const peer = index === 0 ? 'alpha' : 'beta';
        // Use the route's existing exact first-use render-wait classifier.
        // It accepts only the healthy authored wait and keeps all other stopped,
        // paused, terminal, and runtime-error states fail-closed.
        const classified = classifyStockObservation(row, peer + ' ' + label);
        assert.deepEqual(classified.match.players.map(player => player.stocks), [4, 4],
          label + ': source stock count changed inside the bounded action prefix');
        return {match: classified.match, renderWait: classified.wait === 'first-use-rendering'};
      });
      const states = classifyRows(rows);
      const matches = states.map(state => state.match);
      pairGuard?.(matches, tick);
      const projectedPlayers = matches.map(match =>
        match.players.map(projectActionObservationPlayer));
      const animationFrameComparison = comparePairedActionAnimationFrames(matches);
      const actorMotions = matches.map(match => match.players[actor].motion);
      const bothActive = states.every(state => !state.renderWait);
      const pairedResponse = bothActive ? pairResponse?.(matches, tick) ?? null : null;
      const renderWaitKey = bothActive ? null : JSON.stringify({source_tick: tick,
        peers: states.map((state, index) => ({peer: index === 0 ? 'alpha' : 'beta',
          waiting: state.renderWait, frame: state.match.frame, message: rows[index].message,
          players: projectedPlayers[index].map(player => [player.motion, player.groundAir,
            player.stocks, player.damage_percent, player.x, player.y,
            player.animation_frame])}))});
      // A first-use render wait does not consume this native source cursor.
      // Retain each distinct wait state once, but permit the active row at the
      // same cursor to be observed later instead of losing it to deduplication.
      if (renderWaitKey !== null && renderWaitKey === lastRenderWaitKey) return;
      const pair = {source_tick: tick, peers: matches.map((match, index) => ({
        peer: index === 0 ? 'alpha' : 'beta', frame: match.frame,
        ...(includeRules ? {rules: match.rules} : {}),
        observation_state: states[index].renderWait ? 'first-use-rendering-wait' : 'active-match',
        message: rows[index].message ?? null,
        ready: match.ready, paused: match.paused, ending: match.ending,
        complete: match.complete, outcome: match.outcome,
        players: projectedPlayers[index]})),
        actor_motion_ids: actorMotions, actor_motion_agreement: actorMotions[0] === actorMotions[1],
        animation_frame_comparison_by_port: animationFrameComparison,
        ...(pairedResponse ? {combat_response: pairedResponse} : {})};
      record.observed_source_pairs.push(pair);
      if (!bothActive) {
        lastRenderWaitKey = renderWaitKey;
        for (const [index, state] of states.entries()) {
          if (state.renderWait) record.first_use_render_wait_source_ticks.push({
            peer: index === 0 ? 'alpha' : 'beta', source_tick: tick});
        }
        return;
      }
      lastRenderWaitKey = null;
      lastRetainedTick = tick;
      const pairedTargetMotion = bothActive
        ? matchPairedActorMotion(actorMotions, targetMotions) : null;
      if (record.target_observed_at_source_tick === null && pairedTargetMotion !== null) {
        record.target_observed_at_source_tick = tick;
        record.target_motion_id = pairedTargetMotion;
      }
      if (pairedResponse?.hit_response_observed &&
          record.defender_damage_response_source_tick === null) {
        record.defender_damage_response_source_tick = tick;
        record.defender_damage_response = pairedResponse;
      }
      if (bothActive && actorMotions[0] === actorMotions[1] && actorMotions[0] === 24)
        record.knee_bend_source_ticks.push(tick);
      const groundedWait = bothActive && matches.every(match => match.players.every(player =>
        player.motion === 14 && player.groundAir === 0));
      if (record.target_observed_at_source_tick !== null &&
          (!pairResponse || record.defender_damage_response_source_tick !== null) && groundedWait) {
        record.grounded_return_source_tick = tick;
        stopped = true;
      }
    };
    return {
      startAt(tick) {
        requireValue(Number.isSafeInteger(tick),
          label + ': source-aligned press sample is unavailable');
        actionStartTick = tick;
        record.action_source_tick_start = tick;
        record.observation_source_tick_range = [tick, tick + 59];
      },
      cancel() {
        cancelled = true;
        stopped = true;
        record.observer_join = joinObservation.source_cursor_join;
      },
      async run() {
        try {
          while (!stopped && !cancelled && remaining() > 0) {
            if (actionStartTick !== null && pendingBeforeStart.length) {
              const buffered = pendingBeforeStart;
              pendingBeforeStart = [];
              for (const {rows: pair} of buffered) {
                await inspectPair(pair);
                if (stopped) break;
              }
              if (stopped) break;
            }
            const rows = await observeAtCommonSourceCursor(joinObservation, () => cancelled);
            if (!rows) break;
            await inspectPair(rows);
          }
          if (cancelled) {
            record.observer_join = joinObservation.source_cursor_join;
            return {cancelled: true};
          }
          requireValue(actionStartTick !== null,
            label + ': action input sample did not establish the source-tick window');
          requireValue(record.target_observed_at_source_tick !== null,
            label + ': target motion was not observed on both peers within 60 source ticks');
          if (pairResponse) requireValue(record.defender_damage_response_source_tick !== null,
            label + ': paired defender damage response was not observed within 60 source ticks');
          requireValue(record.grounded_return_source_tick !== null,
            label + ': both players did not return to grounded Wait within 60 source ticks');
          record.observer_join = joinObservation.source_cursor_join;
          return {target_source_tick: record.target_observed_at_source_tick,
            grounded_return_source_tick: record.grounded_return_source_tick,
            observation_source_tick_range: record.observation_source_tick_range};
        } catch (error) {
          record.observer_join = joinObservation.source_cursor_join;
          throw error;
        }
      },
    };
  };
  const waitForButtonInputWindow = async ({label, role, pressTick, button, retainEvidence}) => {
    const port = rolePort(role);
    const otherRole = role === 'alpha' ? 'beta' : 'alpha';
    const copyRows = rows => rows.map(row => ({...row,
      bytes: Array.isArray(row?.bytes) ? [...row.bytes] : row?.bytes ?? null}));
    const deadline = Date.now() + Math.min(1500, remaining());
    while (Date.now() < deadline) {
      const counts = await Promise.all([role, otherRole].map(async currentRole => {
        const state = await readRoomState(instances[currentRole]);
        assertHealthy(state, currentRole);
        return state.owner_health?.peer?.localInputCapture?.captured_count;
      }));
      requireValue(counts.every(Number.isSafeInteger),
        label + ': live input capture counts are unavailable');
      const [actionReadback, otherReadback] = await Promise.all([
        readPeerInputSuffix(instances[role], Math.min(pressTick, counts[0])),
        readPeerInputSuffix(instances[otherRole], Math.min(pressTick, counts[1])),
      ]);
      requireValue(actionReadback.runtime_error == null && otherReadback.runtime_error == null &&
        actionReadback.health?.failure == null && otherReadback.health?.failure == null &&
        actionReadback.health?.peer?.failure == null && otherReadback.health?.peer?.failure == null &&
        actionReadback.native_status?.local_capture_failure == null &&
        otherReadback.native_status?.local_capture_failure == null,
      label + ': button-window readback observed a runtime-owner failure');
      const actionRows = actionReadback.health?.peer?.localInputCapture?.captures_since;
      const otherRows = otherReadback.health?.peer?.localInputCapture?.captures_since;
      requireValue(Array.isArray(actionRows) && Array.isArray(otherRows),
        label + ': literal local input suffix is unavailable');
      const actionSuffix = actionRows.filter(row => row?.input_tick >= pressTick);
      const release = actionSuffix.find(row => row?.input_tick > pressTick &&
        isNeutralPadRow(row));
      const observedEnd = release?.input_tick ?? actionSuffix.at(-1)?.input_tick ?? pressTick;
      const partialActionRows = actionSuffix.filter(row => row?.input_tick <= observedEnd);
      const partialOtherRows = otherRows.filter(row => row?.input_tick >= pressTick &&
        row?.input_tick <= observedEnd);
      retainEvidence?.({status: release ? 'release-observed' : 'collecting',
        press_input_tick: pressTick, neutral_release_input_tick: release?.input_tick ?? null,
        action_port_literal_rows: copyRows(partialActionRows),
        other_port_literal_rows: copyRows(partialOtherRows)});
      if (release && otherRows.some(row => row.input_tick === release.input_tick)) {
        const actionWindow = actionSuffix.filter(row => row?.input_tick <= release.input_tick);
        const otherWindow = otherRows.filter(row => row?.input_tick >= pressTick &&
          row?.input_tick <= release.input_tick);
        const validated = validateButtonGameplayInputWindow({
          actionRows: actionWindow, otherPortRows: otherWindow,
          pressTick, releaseTick: release.input_tick, port, button});
        const result = {validated, actionRows: copyRows(actionWindow),
          otherPortRows: copyRows(otherWindow)};
        retainEvidence?.({status: 'validated', press_input_tick: pressTick,
          neutral_release_input_tick: release.input_tick, validated,
          action_port_literal_rows: result.actionRows, other_port_literal_rows: result.otherPortRows});
        return result;
      }
      await instances[role].page.waitForTimeout(Math.min(10, Math.max(1, deadline - Date.now())));
    }
    throw Error(label + ': literal 11-byte press/release rows did not settle');
  };
  const runObservedGameplayAction = async ({label, role, key, button,
    targetMotions, beforeBaseline, observePress = false,
    recordSink = route.action_prefix.actions, pairGuard = null, pairResponse = null,
    includeRules = false, retainInspectedPair = false}) => {
    const record = {label, role, local_port: rolePort(role), key, expected_button: button,
      target_motion_ids: targetMotions, target_motion_id: null, max_source_ticks: 60,
      before: beforeBaseline.report,
      press_input_tick: null, action_source_tick_start: null,
      action_source_tick_start_basis: 'input_tick + declared inputDelay + 1; native status cursor is next frame after after_step checksum/increment',
      press_hold_source_tick_budget: null, press_hold_ms: null,
      observation_source_tick_range: null, target_observed_at_source_tick: null,
      grounded_return_source_tick: null, knee_bend_source_ticks: [],
      first_use_render_wait_source_ticks: [], observed_source_pairs: [], input_window: null,
      ...(pairResponse ? {defender_damage_response_source_tick: null,
        defender_damage_response: null} : {})};
    recordSink.push(record);
    const observer = createActionObservation({label, role, targetMotions, record,
      pairGuard, pairResponse, includeRules, retainInspectedPair});
    const observerOutcome = observer.run().then(value => ({value}),
      error => ({error}));
    try {
      // Hold for at least ten source periods, or the declared delay plus four,
      // whichever is larger for the default jab. Jump may instead opt into
      // first-source-capture release. This is a bounded button window, not a guarantee
      // against an arbitrarily long network wait or an extra action episode.
      const holdSourceTicks = observePress ? null : Math.max(ACTION_HOLD_MIN_SOURCE_TICKS, inputDelay + 4);
      const holdMs = observePress ? 0 : Math.ceil(holdSourceTicks * 1000 / SOURCE_TICK_RATE_HZ);
      record.press_hold_source_tick_budget = holdSourceTicks;
      record.press_hold_ms = observePress ? null : holdMs;
      if (observePress) record.press_release_policy =
        'first captured desired PAD; early-release short-hop input hypothesis, not a native short-hop flag observation';
      const action = await pulse(role, [key], {expected: {button}, holdMs, releaseMs: 80,
        label, retainSample: true, observePress});
      if (observePress) record.press_observation_cap_ms = action.press_observation_cap_ms;
      const sample = action.observed_pad_sample;
      requireValue(sample && sample.input_tick === action.source_input_tick &&
        sample.local_port === rolePort(role) && sample.bytes?.length === 11,
      label + ': keyboard action did not retain its literal source PAD row');
      record.press_input_tick = sample.input_tick;
      observer.startAt(sample.input_tick + inputDelay + 1);
      const inputWindow = await waitForButtonInputWindow({label, role,
        pressTick: sample.input_tick, button,
        retainEvidence: evidence => { record.input_window = evidence; }});
      record.input_window = {status: 'validated', validated: inputWindow.validated,
        action_port_literal_rows: inputWindow.actionRows,
        other_port_literal_rows: inputWindow.otherPortRows};
      requireValue(JSON.stringify(inputWindow.actionRows[0].bytes) ===
        JSON.stringify(sample.bytes),
      label + ': first literal input-window row differs from the action PAD sample');
      const observed = await observerOutcome;
      if (observed.error) {
        record.observer_error = String(observed.error?.stack || observed.error);
        throw observed.error;
      }
      const releaseBoundary = inputWindow.validated.neutral_release_input_tick + inputDelay + 1;
      await waitForBothChecksumCompare(releaseBoundary, null);
      route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
        releaseBoundary);
      record.release_checksum_boundary_required = releaseBoundary;
      record.release_checksum_boundary_reached = true;
      record.checksum_boundary_required = action.checksum_boundary_required;
      record.checksum_boundary_reached = action.checksum_boundary_reached;
      record.after_action = observed.value;
      return {record, action, observation: observed.value, inputWindow};
    } finally {
      // Every error after collector start cancels and joins it. The input window
      // is retained progressively above before motion-outcome rejection.
      observer.cancel();
      await observerOutcome;
    }
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
    p2_gameplay_input: 'one short source-port-1 horizontal pulse with literal neutral release; no combat claim',
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
    await pulse('alpha', keys, {expected, holdMs: 40, releaseMs: 80, observePress: true,
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

  // Reacquire after the screenshots, then exercise only the existing P2
  // keyboard axis binding. The literal local capture rows establish both the
  // movement sample and the later neutral release; paired native positions
  // establish the same-source peer response without a combat claim.
  const p2BaselineRows = await waitBoth('fresh same-source 4/4 P2 gameplay baseline', rows =>
    sourceAligned(rows) && rows.every(row => row.phase === 7 && row.running === 1 &&
      row.match?.ready === true && row.match?.players?.length === 2 &&
      row.match.players.every(player => player.stocks === 4)),
  40, {joinSourceCursor: true});
  const p2BaselineMatches = p2BaselineRows.map((row, index) => checkMatchIdentity(row,
    `${index === 0 ? 'alpha' : 'beta'} P2 gameplay baseline`));
  const p2BaselinePositions = p2BaselineMatches.map(match => ({
    p1_x: match.players[0].x, p2_x: match.players[1].x,
  }));
  requireValue(p2BaselinePositions.every(position => Number.isFinite(position.p1_x) &&
    Number.isFinite(position.p2_x) && Math.abs(position.p2_x - position.p1_x) > 20),
  'Fresh P2 gameplay baseline does not expose separated native player positions');
  requireValue(p2BaselinePositions.every(position =>
    position.p1_x === p2BaselinePositions[0].p1_x &&
    position.p2_x === p2BaselinePositions[0].p2_x),
  'Peers disagree about native player positions at the paired P2 baseline cursor');
  requireValue(Math.abs(p2BaselinePositions[0].p2_x) > 0.01,
    'P2 is too close to center to select a source-observed inward direction');
  const p2Direction = -Math.sign(p2BaselinePositions[0].p2_x);
  const p2Key = p2Direction > 0 ? 'ArrowRight' : 'ArrowLeft';
  const p2Prefix = route.p2_gameplay_prefix = {
    source_port: 1, key: p2Key, direction_x: p2Direction,
    direction_policy: 'one short inward step based on the fresh source-aligned P2 position',
    before: {source_tick: p2BaselineRows[0].source_tick,
      peers: p2BaselinePositions, stocks: p2BaselineMatches.map(match =>
        match.players.map(player => player.stocks))},
    input_window: {movement: null, neutral_release: null, source_tick_range: null,
      p2_literal_rows: [], p1_literal_rows: []},
    after: null,
    scope: 'one short actual P2 keyboard axis pulse toward center plus literal neutral release; no combat-coverage claim',
  };
  const p2Action = await pulse('beta', [p2Key], {expected: {x: p2Direction},
    holdMs: 40, releaseMs: 80, observePress: true,
    label: `P2 ${p2Key} short gameplay axis pulse toward center`, retainSample: true});
  const p2MovementSample = p2Action.observed_pad_sample;
  requireValue(p2MovementSample && p2MovementSample.input_tick === p2Action.source_input_tick,
    'P2 gameplay pulse did not retain its literal source PAD sample');
  p2Prefix.input_window.movement = p2MovementSample;
  p2Prefix.input_window.source_tick_range = [p2MovementSample.input_tick, null];
  const p2Window = await waitForP2InputWindow(p2MovementSample.input_tick);
  requireValue(JSON.stringify(p2Window.p2Window[0].bytes) === JSON.stringify(p2MovementSample.bytes),
    'P2 input-window first record differs from the pulse source PAD sample');
  const p2Release = p2Window.p2Window.at(-1);
  p2Prefix.input_window.neutral_release = p2Release;
  p2Prefix.input_window.source_tick_range[1] = p2Release.input_tick;
  p2Prefix.input_window.p2_literal_rows = p2Window.p2Window;
  p2Prefix.input_window.p1_literal_rows = p2Window.p1Window;
  p2Prefix.input_window.p2_literal_capture_count = p2Window.p2Window.length;
  p2Prefix.input_window.p1_neutral_capture_count = p2Window.validated.p1_neutral_capture_count;
  p2Prefix.input_window.p1_neutral_source_tick_range = [p2MovementSample.input_tick,
    p2Release.input_tick];
  const p2RequiredBoundary = p2Release.input_tick + inputDelay + 1;
  await waitForBothChecksumCompare(p2RequiredBoundary, null);
  route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
    p2RequiredBoundary);
  const p2AfterRows = await waitBoth('P2 movement observed on both peers after neutral release', rows =>
    {
      if (!sourceAligned(rows) || !rows.every(row => row.phase === 7 && row.running === 1 &&
          row.match?.ready === true && row.match?.players?.length === 2)) return false;
      assert(rows.every(row => row.match.players.every(player => player.stocks === 4)),
        'A stock changed during the bounded P2 input prefix');
      return rows[0].source_tick >= p2RequiredBoundary;
    },
  40, {joinSourceCursor: true});
  const p2AfterMatches = p2AfterRows.map((row, index) => checkMatchIdentity(row,
    `${index === 0 ? 'alpha' : 'beta'} P2 gameplay response`));
  const p2PositionDeltas = p2AfterMatches.map((match, index) => ({
    peer: index === 0 ? 'alpha' : 'beta',
    before_x: p2BaselineMatches[index].players[1].x,
    after_x: match.players[1].x,
    delta_x: match.players[1].x - p2BaselineMatches[index].players[1].x,
  }));
  requireValue(p2AfterMatches.every((match, index) =>
    match.players[0].x === p2AfterMatches[0].players[0].x &&
    match.players[1].x === p2AfterMatches[0].players[1].x &&
    Math.abs(match.players[1].x - match.players[0].x) > 20) &&
    p2PositionDeltas.every(position =>
      Math.sign(position.delta_x) === p2Direction && Math.abs(position.delta_x) > 0.01),
  'P2 native position did not move in the captured direction on both peers');
  p2Prefix.after = {source_tick: p2AfterRows[0].source_tick,
      checksum_boundary_required: p2RequiredBoundary,
      checksum_boundary_reached: true,
      peers: p2AfterMatches.map(match => ({p1_x: match.players[0].x, p2_x: match.players[1].x,
        stocks: match.players.map(player => player.stocks)})),
      p2_position_delta_by_peer: p2PositionDeltas};


  let walkoffBaseRows = p2AfterRows;
  let walkoffBaseMatches = p2AfterMatches;
  if (gameplayCombatProbe) {
    const initialItemRules = validateCombatItemsNone(initialMatches);
    const expectedItemMask = initialItemRules.item_mask_hex;
    const approachSourceStart = p2AfterRows[0].source_tick;
    const approachSourceLimit = approachSourceStart + 480;
    route.combat_probe = {result: 'running', scope: 'one bounded actual P2 inward keyboard approach and jab response; functional route evidence only',
      source_observer: '_melee_web_native_menu_match_observe via fresh common-source peer rows',
      motion_ids: {ftCo_MS_Wait: 14, ftCo_MS_Attack11: 44,
        ftCo_MS_DamageHi1: 75, ftCo_MS_DamageFlyRoll: 91},
      approach_policy: {maximum_observed_release_taps: 16,
        maximum_source_cursor_span: 480, target_positive_gap: [0, 20],
        target_is_unproven_reach_hypothesis: true,
        overshoot_guard: 'before each tap, remaining positive gap must exceed the largest prior settled inward displacement; crossing is a strict failure'},
      source_cursor_start: approachSourceStart, source_cursor_limit: approachSourceLimit,
      item_rules: initialItemRules, approach: {steps: [], before: null, reached: null},
      baselines: [], actions: []};
    const combatPairGuard = matches => {
      validateCombatItemsNone(matches, expectedItemMask);
      comparePairedCombatPlayers(matches);
      for (const match of matches)
        assert.deepEqual(match.players.map(player => player.stocks), [4, 4],
          'Combat probe stock count changed before the first hit');
    };
    const pairedPositions = matches => {
      combatPairGuard(matches);
      const positions = {p1_x: matches[0].players[0].x, p2_x: matches[0].players[1].x};
      requireValue(Number.isFinite(positions.p1_x) && Number.isFinite(positions.p2_x),
        'Combat approach has a nonfinite source position');
      return positions;
    };
    const initialBaseline = await waitForGroundedNeutralBaseline(
      'P2 combat approach settled baseline', p2RequiredBoundary,
      {recordSink: route.combat_probe.baselines, sourceCursorLimit: approachSourceLimit,
        includeRules: true});
    combatPairGuard(initialBaseline.matches);
    const baselinePositions = pairedPositions(initialBaseline.matches);
    const sourceStartPositions = pairedPositions(p2BaselineMatches);
    const initialSide = Math.sign(sourceStartPositions.p2_x - sourceStartPositions.p1_x);
    requireValue(initialSide !== 0 &&
      Math.sign(baselinePositions.p2_x - baselinePositions.p1_x) === initialSide,
    'P2 combat approach baseline crossed the initial player side');
    const gapAtStart = (sourceStartPositions.p2_x - sourceStartPositions.p1_x) * initialSide;
    const gapAfterWitnessSettle = (baselinePositions.p2_x - baselinePositions.p1_x) * initialSide;
    const witnessDisplacement = gapAtStart - gapAfterWitnessSettle;
    requireValue(witnessDisplacement > 0,
      'Existing P2 axis witness did not produce a settled inward displacement');
    route.combat_probe.approach.before = {source_tick: initialBaseline.rows[0].source_tick,
      source_positions: baselinePositions, gap_from_initial_side: gapAfterWitnessSettle,
      existing_axis_witness_settled_inward_displacement: witnessDisplacement,
      stocks: initialBaseline.matches[0].players.map(player => player.stocks)};
    let currentBaseline = initialBaseline;
    let tapsUsed = 0;
    let largestObservedInwardDisplacement = witnessDisplacement;
    const p2ApproachDirection = -initialSide;
    const p2ApproachKey = p2ApproachDirection < 0 ? 'ArrowLeft' : 'ArrowRight';
    while (true) {
      const sourcePositions = pairedPositions(currentBaseline.matches);
      const signedGap = (sourcePositions.p2_x - sourcePositions.p1_x) * initialSide;
      const decision = decideP2ApproachTap({gap: signedGap, tapCount: tapsUsed,
        sourceTickStart: approachSourceStart,
        sourceTick: currentBaseline.rows[0].source_tick,
        maxObservedInwardDisplacement: largestObservedInwardDisplacement});
      if (decision.decision === 'reached') {
        route.combat_probe.approach.reached = {source_tick: currentBaseline.rows[0].source_tick,
          source_positions: sourcePositions, positive_gap_from_initial_side: signedGap,
          target_upper_bound: 20, decision};
        break;
      }
      requireValue(decision.decision === 'tap',
        `P2 combat approach stopped (${decision.reason}) at gap ${signedGap} after ${tapsUsed} taps`);
      const step = {index: tapsUsed + 1, decision, before_source_tick: currentBaseline.rows[0].source_tick,
        before: sourcePositions, gap_before: signedGap, key: p2ApproachKey,
        direction_x: p2ApproachDirection, input_window: {status: 'pending',
          p2_literal_rows: [], p1_literal_rows: []}, after: null};
      route.combat_probe.approach.steps.push(step);
      const action = await pulse('beta', [p2ApproachKey], {expected: {x: p2ApproachDirection},
        holdMs: 40, releaseMs: 80, observePress: true, retainSample: true,
        sourceCursorLimit: approachSourceLimit, evidenceSink: step.input_window,
        label: `P2 combat approach tap ${step.index}`,});
      const sample = action.observed_pad_sample;
      requireValue(sample && sample.input_tick === action.source_input_tick &&
        sample.local_port === 1 && Array.isArray(sample.bytes) && sample.bytes.length === 11,
      'P2 combat approach tap did not retain its literal source PAD sample');
      step.input_window.movement = sample;
      step.input_window.source_tick_start = sample.input_tick;
      const capturedInputWindow = await waitForP2InputWindow(sample.input_tick,
        {evidenceSink: step.input_window, sourceCursorLimit: approachSourceLimit});
      const inputWindow = validateSingleP2ApproachTapWindow({
        p2Rows: capturedInputWindow.p2Window, p1Rows: capturedInputWindow.p1Window,
        movementTick: sample.input_tick,
        releaseTick: capturedInputWindow.validated.neutral_release_input_tick,
        direction: p2ApproachDirection, expectedMovementBytes: sample.bytes});
      step.input_window.status = 'validated';
      step.input_window.validated = inputWindow.validated;
      const neutralRelease = inputWindow.p2Window.at(-1);
      step.input_window.source_tick_range = [sample.input_tick, neutralRelease.input_tick];
      step.input_window.p2_literal_rows = inputWindow.p2Window;
      step.input_window.p1_literal_rows = inputWindow.p1Window;
      const releaseBoundary = neutralRelease.input_tick + inputDelay + 1;
      await waitForBothChecksumCompare(releaseBoundary, null, undefined,
        {sourceCursorLimit: approachSourceLimit, evidenceSink: step.input_window});
      route.required_checksum_boundary = Math.max(route.required_checksum_boundary || 0,
        releaseBoundary);
      const settledBaseline = await waitForGroundedNeutralBaseline(
        `P2 combat approach tap ${step.index} settled baseline`, releaseBoundary,
        {recordSink: route.combat_probe.baselines, sourceCursorLimit: approachSourceLimit,
        includeRules: true});
      combatPairGuard(settledBaseline.matches);
      const afterPositions = pairedPositions(settledBaseline.matches);
      requireValue(Math.sign(afterPositions.p2_x - afterPositions.p1_x) === initialSide,
        `P2 combat approach tap ${step.index} crossed the initial side`);
      requireValue(afterPositions.p1_x === sourcePositions.p1_x,
        `P2 combat approach tap ${step.index} unexpectedly displaced neutral P1`);
      const afterGap = (afterPositions.p2_x - afterPositions.p1_x) * initialSide;
      const inwardDisplacement = signedGap - afterGap;
      requireValue(Number.isFinite(inwardDisplacement) && inwardDisplacement > 0,
        `P2 combat approach tap ${step.index} did not move inward`);
      step.after = {source_tick: settledBaseline.rows[0].source_tick,
        source_positions: afterPositions, gap_from_initial_side: afterGap,
        measured_inward_displacement: inwardDisplacement,
        source_cursor_delta: settledBaseline.rows[0].source_tick - step.before_source_tick,
        stocks: settledBaseline.matches[0].players.map(player => player.stocks)};
      largestObservedInwardDisplacement = Math.max(largestObservedInwardDisplacement,
        inwardDisplacement);
      currentBaseline = settledBaseline;
      tapsUsed++;
    }

    const combatBeforeJab = currentBaseline;
    combatPairGuard(combatBeforeJab.matches);
    const initialVictimDamage = combatBeforeJab.matches[0].players[0].damage_percent;
    requireValue(Number.isFinite(initialVictimDamage),
      'P1 pre-jab damage baseline is not finite');
    const jab = await runObservedGameplayAction({label: 'P2 grounded keyboard jab',
      role: 'beta', key: 'ShiftRight', button: 0x0100, targetMotions: [44],
      beforeBaseline: combatBeforeJab, recordSink: route.combat_probe.actions,
      pairGuard: combatPairGuard, retainInspectedPair: true,
      pairResponse: matches => observePairedCombatJab(matches, {initialVictimDamage,
        actorPort: 1, victimPort: 0}), includeRules: true});
    const postJab = await waitForGroundedNeutralBaseline(
      'post-jab grounded neutral [4,4] baseline',
      jab.inputWindow.validated.neutral_release_input_tick + inputDelay + 1,
      {recordSink: route.combat_probe.baselines, includeRules: true});
    combatPairGuard(postJab.matches);
    jab.record.after_grounded_neutral_baseline = postJab.report;
    route.combat_probe.after = postJab.report;
    route.combat_probe.result = 'pass';
    walkoffBaseRows = postJab.rows;
    walkoffBaseMatches = postJab.matches;
  } else if (gameplayActionProbe) {
    // Preserve the P2 horizontal pulse witness above, then add one bounded
    // grounded P1 jab and one natural P2 jump using the same live keyboard path.
    route.action_prefix = {scope: 'one grounded P1 jab and one P2 jump with matched source motion evidence; no combat-coverage claim',
      source_observer: '_melee_web_native_menu_match_observe via fresh common-source peer rows',
      motion_ids: {ftCo_MS_Wait: 14, ftCo_MS_KneeBend: 24,
        ftCo_MS_JumpF: 25, ftCo_MS_JumpB: 26, ftCo_MS_Attack11: 44},
      max_source_ticks_per_action: 60, baselines: [], actions: []};
    const actionStartBaseline = await waitForGroundedNeutralBaseline(
      'pre-jab grounded neutral [4,4] baseline', p2RequiredBoundary);
    const p1Jab = await runObservedGameplayAction({label: 'P1 grounded keyboard jab',
      role: 'alpha', key: 'j', button: 0x0100, targetMotions: [44],
      beforeBaseline: actionStartBaseline});
    const p1JabGrounded = await waitForGroundedNeutralBaseline(
      'post-jab grounded neutral [4,4] baseline',
      p1Jab.inputWindow.validated.neutral_release_input_tick + inputDelay + 1);
    p1Jab.record.after_grounded_neutral_baseline = p1JabGrounded.report;
    const p2Jump = await runObservedGameplayAction({label: 'P2 keyboard jump',
      role: 'beta', key: 'Delete', button: 0x0400, targetMotions: [25, 26],
      beforeBaseline: p1JabGrounded, observePress: true});
    const postJumpGrounded = await waitForGroundedNeutralBaseline(
      'post-jump grounded neutral [4,4] baseline',
      p2Jump.inputWindow.validated.neutral_release_input_tick + inputDelay + 1);
    p2Jump.record.after_grounded_neutral_baseline = postJumpGrounded.report;
    route.action_prefix.walkoff_baseline = postJumpGrounded.report;
    walkoffBaseRows = postJumpGrounded.rows;
    walkoffBaseMatches = postJumpGrounded.matches;
    route.action_prefix.result = 'pass';
  }

  // P1 walk-off uses one reconciler at common source ticks before and after
  // each pulse. A natural stock decrement may occur between pulses, so
  // observations accept only the tracked count or
  // exactly one fewer; a detected loss is recorded once and must reach the
  // native grounded-respawn observation before another keyboard action.
  let stocks = 4;
  const firstPlayer = walkoffBaseMatches[0].players[0];
  requireValue(Number.isFinite(firstPlayer.x), 'P1 source position is unavailable at match start');
  let outwardKey = firstPlayer.x <= 0 ? 'a' : 'd';
  let lastStableMatches = walkoffBaseMatches;
  let lastStableRows = walkoffBaseRows;
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
