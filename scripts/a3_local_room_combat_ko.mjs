import assert from 'node:assert/strict';

export const COMBAT_KO_SOURCE_TICK_BUDGET = 480;
export const COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS = 24;
export const COMBAT_KO_CHARGED_PAD_BYTES = Object.freeze([1, 0, 129, 0, 0, 0, 0, 0, 0, 0, 0]);
export const COMBAT_KO_PREP_MAX_JABS = 8;
export const COMBAT_KO_PREP_DAMAGE_GOAL = 18;
export const COMBAT_KO_PLATFORM_EXIT_MAX_PAD_ROWS = 30;
export const COMBAT_KO_PLATFORM_EXIT_FALL_MOTION = 29;
export const COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION = Object.freeze({
  min_x: -64, max_x: 64, min_y: 32, max_y: 64,
  policy: 'conservative scenario admission only; not authored Final Destination geometry or landing proof',
});

export function decideCombatKoPreparation({sourceTick, sourceStartTick, sourceCursorLimit,
  jabCount, actualVictimDamage}) {
  requireValue(Number.isSafeInteger(sourceStartTick) && Number.isSafeInteger(sourceTick) &&
    Number.isSafeInteger(sourceCursorLimit) && sourceTick >= sourceStartTick &&
    sourceCursorLimit === sourceStartTick + COMBAT_KO_SOURCE_TICK_BUDGET &&
    sourceTick <= sourceCursorLimit,
  'Combat KO preparation exceeded its aggregate 480-source-cursor budget');
  requireValue(Number.isSafeInteger(jabCount) && jabCount >= 0 &&
    jabCount <= COMBAT_KO_PREP_MAX_JABS && Number.isFinite(actualVictimDamage) &&
    actualVictimDamage >= 0,
  'Combat KO preparation has invalid jab count or actual damage');
  if (actualVictimDamage >= COMBAT_KO_PREP_DAMAGE_GOAL)
    return {decision: 'ready', jab_count: jabCount,
      actual_victim_damage_percent: actualVictimDamage};
  if (jabCount >= COMBAT_KO_PREP_MAX_JABS)
    return {decision: 'stop', reason: 'eight-jab-damage-goal-unmet', jab_count: jabCount,
      actual_victim_damage_percent: actualVictimDamage};
  return {decision: 'jab', next_jab: jabCount + 1,
    actual_victim_damage_percent: actualVictimDamage};
}

const BASELINE_BRAND = Symbol('validated combat KO baseline');
const LOSS_BRAND = Symbol('validated combat KO loss');
const LOSS_BASELINE = Symbol('combat KO loss baseline');
const CHARGE_BRAND = Symbol('validated combat KO charge animation');
const CHARGE_BASELINE = Symbol('combat KO charge baseline');
const PLATFORM_GATE_BRAND = Symbol('validated combat KO RebirthWait platform gate');
const PLATFORM_GATE_BASELINE = Symbol('combat KO platform gate baseline');
const PLATFORM_GATE_LOSS = Symbol('combat KO platform gate loss');
const PLATFORM_FALL_BRAND = Symbol('validated combat KO platform-exit Fall');
const PLATFORM_FALL_GATE = Symbol('combat KO platform-exit Fall gate');
const SOURCE_PORTS = [0, 1];
const SOURCE_SLOT_COUNT = 6;

function requireValue(condition, message) {
  if (!condition) throw Error(message);
}

function safeCounter(value, label) {
  requireValue(Number.isSafeInteger(value) && value >= 0,
    `${label} is not a nonnegative safe integer`);
  return value;
}

function readCounters(player, label) {
  const kos = player?.kos_by_player_slot;
  requireValue(Array.isArray(kos) && kos.length === SOURCE_SLOT_COUNT,
    `${label} lacks the full six-entry kos_by_player_slot vector`);
  return Object.freeze({
    primary_falls: safeCounter(player.primary_falls, `${label} primary_falls`),
    suicides: safeCounter(player.suicides, `${label} suicides`),
    kos_by_player_slot: Object.freeze(Array.from(kos, (value, index) =>
      safeCounter(value, `${label} kos_by_player_slot[${index}]`))),
  });
}

function readPeer(row, peerIndex) {
  const label = peerIndex === 0 ? 'alpha' : 'beta';
  requireValue(row && row.error == null,
    `${label} combat observation has an error`);
  requireValue(Number.isSafeInteger(row.source_tick) && row.source_tick >= 0,
    `${label} combat observation has no safe source_tick`);
  const match = row.match;
  requireValue(match && match.observer_error !== true && !match.observer_error_reason,
    `${label} combat observation has no valid native match snapshot`);
  requireValue(Number.isSafeInteger(match.frame) && match.frame >= 0,
    `${label} combat observation has no safe source frame`);
  requireValue(Array.isArray(match.players) && match.players.length === 2,
    `${label} combat observation does not contain exactly two native players`);

  const byPort = new Map();
  for (const player of match.players) {
    requireValue(SOURCE_PORTS.includes(player?.source_port) && !byPort.has(player.source_port),
      `${label} combat observation has a missing or duplicate source port`);
    requireValue(Number.isSafeInteger(player.source_player_index) &&
      player.source_player_index >= 0 && player.source_player_index < SOURCE_SLOT_COUNT,
    `${label} P${player.source_port + 1} has an invalid source_player_index`);
    requireValue(Number.isSafeInteger(player.stocks) && player.stocks >= 0,
      `${label} P${player.source_port + 1} has an invalid stock count`);
    byPort.set(player.source_port, Object.freeze({
      source_port: player.source_port,
      source_player_index: player.source_player_index,
      stocks: player.stocks,
      counters: readCounters(player, `${label} P${player.source_port + 1}`),
      motion: player.motion,
      groundAir: player.groundAir,
    }));
  }
  requireValue(SOURCE_PORTS.every(port => byPort.has(port)),
    `${label} combat observation is missing source port 0 or 1`);
  const slots = SOURCE_PORTS.map(port => byPort.get(port).source_player_index);
  requireValue(slots[0] !== slots[1],
    `${label} P1 and P2 source_player_index values are not distinct`);
  return Object.freeze({source_tick: row.source_tick, source_frame: match.frame,
    match, by_port: byPort, source_player_index_by_port: Object.freeze(slots)});
}

function readPairedRows(pairedRows, label) {
  requireValue(Array.isArray(pairedRows) && pairedRows.length === 2,
    `${label} requires exactly two peer rows`);
  const peers = pairedRows.map(readPeer);
  requireValue(peers[0].source_tick === peers[1].source_tick,
    `${label} peers do not share one source_tick`);
  requireValue(peers[0].source_frame === peers[1].source_frame,
    `${label} peers disagree on the source frame`);
  assert.deepEqual(peers[0].source_player_index_by_port,
    peers[1].source_player_index_by_port,
    `${label} peers disagree on actual source_player_index mapping`);
  for (const port of SOURCE_PORTS) {
    const left = peers[0].by_port.get(port);
    const right = peers[1].by_port.get(port);
    assert.equal(right.stocks, left.stocks,
      `${label} peers disagree on P${port + 1} stocks`);
    assert.equal(right.source_player_index, left.source_player_index,
      `${label} peers disagree on P${port + 1} source_player_index`);
    assert.deepEqual(right.counters, left.counters,
      `${label} peers disagree on P${port + 1} persistent counters`);
  }
  return Object.freeze({source_tick: peers[0].source_tick,
    source_frame: peers[0].source_frame, peers: Object.freeze(peers),
    source_player_index_by_port: peers[0].source_player_index_by_port,
    stocks_by_port: Object.freeze(SOURCE_PORTS.map(port => peers[0].by_port.get(port).stocks)),
    counters_by_port: Object.freeze(SOURCE_PORTS.map(port => peers[0].by_port.get(port).counters))});
}

function requireStocks(state, expected, label) {
  assert.deepEqual(state.stocks_by_port, expected,
    `${label} stocks must be [${expected.join(',')}] by source port`);
}

function requireSameMapping(state, baseline, label) {
  assert.deepEqual(state.source_player_index_by_port, baseline.source_player_index_by_port,
    `${label} changed actual source_player_index mapping`);
}

function requireOrderedAndBounded(state, baseline, label, previous = baseline) {
  requireValue(state.source_tick > previous.source_tick,
    `${label} source_tick did not move forward`);
  requireValue(state.source_frame > previous.source_frame,
    `${label} source frame did not move forward`);
  requireValue(state.source_tick - baseline.source_tick <= COMBAT_KO_SOURCE_TICK_BUDGET,
    `${label} exceeded the ${COMBAT_KO_SOURCE_TICK_BUDGET}-tick combat KO source budget`);
}

function nextCounter(value, label) {
  requireValue(value < Number.MAX_SAFE_INTEGER,
    `${label} cannot be incremented safely`);
  return value + 1;
}

function requireExpectedCounterDeltas(state, baseline, label) {
  const victimSlot = baseline.source_player_index_by_port[0];
  for (const port of SOURCE_PORTS) {
    const before = baseline.counters_by_port[port];
    const after = state.counters_by_port[port];
    const expectedFalls = port === 0
      ? nextCounter(before.primary_falls, `${label} P1 baseline primary_falls`)
      : before.primary_falls;
    assert.equal(after.primary_falls, expectedFalls,
      `${label} P${port + 1} primary_falls delta is not exact`);
    assert.equal(after.suicides, before.suicides,
      `${label} P${port + 1} suicides changed`);
    for (let slot = 0; slot < SOURCE_SLOT_COUNT; ++slot) {
      const expectedKOs = port === 1 && slot === victimSlot
        ? nextCounter(before.kos_by_player_slot[slot],
          `${label} P2 baseline KO credit for source slot ${slot}`)
        : before.kos_by_player_slot[slot];
      assert.equal(after.kos_by_player_slot[slot], expectedKOs,
        `${label} P${port + 1} KO counter for source slot ${slot} has an unexpected delta`);
    }
  }
}

function requireBaseline(baseline) {
  requireValue(baseline?.[BASELINE_BRAND] === true,
    'Combat KO baseline was not produced by captureCombatKoBaseline');
}

function requireLossReceipt(receipt, baseline) {
  requireValue(receipt?.[LOSS_BRAND] === true && receipt[LOSS_BASELINE] === baseline,
    'Combat KO handoff requires this baseline’s validated attack-loss receipt');
}

function freezeEvidenceRows(rows) {
  return Object.freeze(rows.map(row => row));
}

// The caller must also bind every row to one continuously owned active match,
// unchanged rules and a healthy room. This helper validates the native fields
// and ordering it can observe; it does not infer owner continuity.
export function captureCombatKoBaseline({pairedRows}) {
  const state = readPairedRows(pairedRows, 'Combat KO baseline');
  requireStocks(state, [4, 4], 'Combat KO baseline');
  for (const peer of state.peers) {
    for (const port of SOURCE_PORTS) {
      const player = peer.by_port.get(port);
      assert.equal(player.motion, 14,
        `Combat KO baseline P${port + 1} is not in Wait motion 14`);
      assert.equal(player.groundAir, 0,
        `Combat KO baseline P${port + 1} is not grounded`);
      const actual = peer.match.players.find(value => value.source_port === port);
      requireValue(Number.isFinite(actual.damage_percent),
        `Combat KO baseline P${port + 1} damage is not finite`);
    }
  }
  return Object.freeze({
    [BASELINE_BRAND]: true,
    source_tick: state.source_tick,
    source_frame: state.source_frame,
    source_player_index_by_port: state.source_player_index_by_port,
    stocks_by_port: state.stocks_by_port,
    counters_by_port: state.counters_by_port,
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match)),
  });
}

// This validates only exact stock/counter attribution at the observed loss.
// Hit, damage reaction, airborne and death-motion acceptance remain route gates.
export function validateCombatKoAttackLoss({baseline, pairedRows}) {
  requireBaseline(baseline);
  const state = readPairedRows(pairedRows, 'Combat KO attack loss');
  requireSameMapping(state, baseline, 'Combat KO attack loss');
  requireOrderedAndBounded(state, baseline, 'Combat KO attack loss');
  requireStocks(state, [3, 4], 'Combat KO attack loss');
  requireExpectedCounterDeltas(state, baseline, 'Combat KO attack loss');
  const receipt = {
    [LOSS_BRAND]: true,
    [LOSS_BASELINE]: baseline,
    kind: 'validated-source-credited-combat-ko-loss-v1',
    baseline_source_tick: baseline.source_tick,
    baseline_source_frame: baseline.source_frame,
    source_tick: state.source_tick,
    source_frame: state.source_frame,
    source_player_index_by_port: state.source_player_index_by_port,
    stocks_by_port: state.stocks_by_port,
    victim_source_player_index: baseline.source_player_index_by_port[0],
    attacker_ko_credit_delta: 1,
    victim_primary_falls_delta: 1,
    suicides_delta_by_port: Object.freeze([0, 0]),
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match)),
  };
  return Object.freeze(receipt);
}

function signedByte(value) {
  return value > 127 ? value - 256 : value;
}

function isLiteralNeutral(bytes) {
  return Array.from(bytes).every(value => value === 0);
}

function isCStickLeftOnly(bytes) {
  return bytes[0] === 0 && bytes[1] === 0 &&
    bytes[2] === 0 && bytes[3] === 0 && signedByte(bytes[4]) < 0 &&
    bytes[5] === 0 && Array.from(bytes.slice(6)).every(value => value === 0);
}

function isChargedALeftOnly(bytes) {
  return Array.isArray(bytes) && bytes.length === COMBAT_KO_CHARGED_PAD_BYTES.length &&
    bytes.every((value, index) => value === COMBAT_KO_CHARGED_PAD_BYTES[index]);
}

function exactInputRows(rows, startTick, throughTick, port, label) {
  requireValue(Array.isArray(rows), `${label} literal PAD rows are unavailable`);
  const window = rows.filter(row => row?.input_tick >= startTick && row.input_tick <= throughTick);
  const expectedCount = throughTick - startTick + 1;
  assert.equal(window.length, expectedCount,
    `${label} literal PAD window has a missing or duplicate source tick`);
  for (let offset = 0; offset < expectedCount; ++offset) {
    const row = window[offset];
    const tick = startTick + offset;
    assert(Number.isSafeInteger(row?.input_tick),
      `${label} PAD row has an invalid input_tick`);
    assert.equal(row.input_tick, tick,
      `${label} PAD ticks are not in source order`);
    assert.equal(row.source_cursor, tick,
      `${label} PAD row is not source-cursor aligned`);
    assert.equal(row.local_port, port,
      `${label} PAD row changed local port`);
    assert(Array.isArray(row.bytes) && row.bytes.length === 11 &&
      Array.from(row.bytes).every(value => Number.isInteger(value) && value >= 0 && value <= 255),
    `${label} PAD row is not an exact 11-byte record`);
  }
  return window;
}

function validateLiteralInputWindow({p1Rows, p2Rows, inputStartTick, inputEndTick,
  pressTick, releaseTick, inputMode = 'cstick', inputDelay = null}) {
  requireValue(Number.isSafeInteger(inputStartTick) &&
    Number.isSafeInteger(inputEndTick) && inputEndTick >= inputStartTick &&
    Number.isSafeInteger(pressTick) && pressTick > inputStartTick &&
    Number.isSafeInteger(releaseTick) && releaseTick > pressTick &&
    inputEndTick >= releaseTick,
  'Combat KO input window has invalid input ticks');
  const p1 = exactInputRows(p1Rows, inputStartTick, inputEndTick, 0, 'P1');
  const p2 = exactInputRows(p2Rows, inputStartTick, inputEndTick, 1, 'P2');
  for (const row of p1) {
    assert(isLiteralNeutral(row.bytes),
      'P1 was not literally neutral from the four-stock baseline through respawn');
  }
  if (inputMode === 'cstick') {
    for (let offset = 0; offset < p2.length; ++offset) {
      const row = p2[offset];
      if (row.input_tick < pressTick || row.input_tick >= releaseTick) {
        assert(isLiteralNeutral(row.bytes),
          `P2 contains unexpected PAD input at source tick ${row.input_tick}`);
      } else {
        assert(isCStickLeftOnly(row.bytes),
          `P2 C-stick-left row at source tick ${row.input_tick} contains another input`);
      }
    }
    return Object.freeze({start_input_tick: inputStartTick,
      cstick_left_rise_input_tick: pressTick,
      neutral_release_input_tick: releaseTick,
      through_respawn_input_tick: inputEndTick,
      p1_neutral_row_count: p1.length,
      p2_cstick_left_held_row_count: releaseTick - pressTick,
      p1_rows: freezeEvidenceRows(p1),
      p2_rows: freezeEvidenceRows(p2)});
  }
  requireValue(inputMode === 'charged-a-left' && Number.isSafeInteger(inputDelay) && inputDelay >= 0,
    'Combat KO charged input window has an unsupported mode or input delay');
  const expectedReleaseTick = pressTick + inputDelay + 1 + COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS;
  assert(releaseTick >= expectedReleaseTick,
    'Combat KO charged chord released before the declared source-cursor endpoint');
  assert(releaseTick - pressTick <= 30,
    'Combat KO charged chord exceeded the 30-literal-record episode limit');
  for (const row of p2) {
    if (row.input_tick < pressTick || row.input_tick >= releaseTick) {
      assert(isLiteralNeutral(row.bytes),
        `P2 contains unexpected PAD input at source tick ${row.input_tick}`);
    } else {
      assert(isChargedALeftOnly(row.bytes),
        `P2 charged A+left row at source tick ${row.input_tick} is partial or contains another input`);
    }
  }
  return Object.freeze({kind: 'literal-a-left-charged-forward-smash-v1',
    a_left_rise_input_tick: pressTick,
    neutral_release_input_tick: releaseTick,
    through_respawn_input_tick: inputEndTick,
    declared_hold_source_cursors: COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS,
    p1_neutral_row_count: p1.length,
    p2_a_left_held_row_count: releaseTick - pressTick,
    p1_rows: freezeEvidenceRows(p1),
    p2_rows: freezeEvidenceRows(p2)});
}

function requirePlatformGate(gate, baseline, attackLossReceipt) {
  requireValue(gate?.[PLATFORM_GATE_BRAND] === true &&
    gate[PLATFORM_GATE_BASELINE] === baseline &&
    gate[PLATFORM_GATE_LOSS] === attackLossReceipt,
  'Platform exit requires this baseline’s validated loss and RebirthWait gate');
}

function stagePeerState(state, baseline, label, sourceCursorLimit) {
  requireSameMapping(state, baseline, label);
  requireValue(state.source_tick <= sourceCursorLimit &&
    state.source_tick - baseline.source_tick <= COMBAT_KO_SOURCE_TICK_BUDGET,
  `${label} exceeded the aggregate 480-source-cursor budget`);
  requireStocks(state, [3, 4], label);
  requireExpectedCounterDeltas(state, baseline, label);
  for (const peer of state.peers) {
    const match = peer.match;
    requireValue(match.rules?.is_stock === 1 && match.rules?.stage === 0x20 &&
      match.ready === true && match.paused === false && match.complete === false &&
      match.ending === false && match.outcome === 0,
    `${label} is not the active, unpaused Final Destination stock match`);
    const p1 = match.players.find(player => player.source_port === 0);
    requireValue(Number.isFinite(p1?.x) && Number.isFinite(p1?.y),
      `${label} P1 position is not finite`);
  }
}

// This policy-specific gate is the only authority for the new P1 axis episode.
// It is based on a branded credited loss plus paired native RebirthWait rows;
// renderer waits, timers, and an unpaired facing never authorize input.
export function validateCombatKoPlatformExitGate({baseline, attackLossReceipt,
  pairedRows, sourceCursorLimit}) {
  requireBaseline(baseline);
  requireLossReceipt(attackLossReceipt, baseline);
  requireValue(Number.isSafeInteger(sourceCursorLimit) &&
    sourceCursorLimit === baseline.source_tick + COMBAT_KO_SOURCE_TICK_BUDGET,
  'Platform exit gate changed the original aggregate 480-source-cursor limit');
  const state = readPairedRows(pairedRows, 'Combat KO platform exit gate');
  stagePeerState(state, baseline, 'Combat KO platform exit gate', sourceCursorLimit);
  requireValue(state.source_tick > attackLossReceipt.source_tick &&
    state.source_frame > attackLossReceipt.source_frame,
  'Platform exit gate must follow the source-credited loss');
  for (const row of pairedRows) {
    requireValue(row.phase === 7 && row.running === 1 && row.error == null &&
      row.native_status?.active === 1 && row.native_status?.context_applied === 1 &&
      row.native_status?.terminal?.kind === 0,
    'Platform exit gate lacks a healthy active source owner');
  }
  const victims = state.peers.map(peer => peer.match.players.find(player =>
    player.source_port === 0));
  requireValue(victims.every(player => player.motion === 13 && player.groundAir === 1),
    'Platform exit gate requires paired active P1 RebirthWait motion 13 on the source platform');
  const facings = victims.map(player => player.facing_direction);
  requireValue(facings.every(value => value === -1 || value === 1) &&
    facings[0] === facings[1],
  'Platform exit gate requires the same exact native P1 facing (+1 or -1) at both peers');
  const positions = victims.map(player => Object.freeze({x: player.x, y: player.y}));
  assert.deepEqual(positions[0], positions[1],
    'Platform exit gate peers disagree on the native P1 platform position');
  requireValue(positions.every(position =>
    position.x >= COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION.min_x &&
    position.x <= COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION.max_x &&
    position.y >= COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION.min_y &&
    position.y <= COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION.max_y),
  'Platform exit gate position is outside the declared conservative scenario-admission window');
  return Object.freeze({[PLATFORM_GATE_BRAND]: true,
    [PLATFORM_GATE_BASELINE]: baseline, [PLATFORM_GATE_LOSS]: attackLossReceipt,
    kind: 'validated-source-credited-rebirth-wait-platform-gate-v1',
    baseline_source_tick: baseline.source_tick,
    loss_source_tick: attackLossReceipt.source_tick,
    source_tick: state.source_tick, source_frame: state.source_frame,
    source_player_index_by_port: state.source_player_index_by_port,
    facing_direction: facings[0], direction_key: facings[0] === -1 ? 'a' : 'd',
    position_by_peer: positions,
    position_admission_policy: COMBAT_KO_PLATFORM_EXIT_POSITION_ADMISSION,
    position_authority: 'paired finite native Final Destination P1 position while motion 13/groundAir 1 is active; admission window is a conservative policy filter, not authored geometry or landing proof',
    stocks_by_port: state.stocks_by_port,
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match))});
}

function platformGateInputTick(gate, inputDelay) {
  requireValue(Number.isSafeInteger(inputDelay) && inputDelay >= 0,
    'Platform exit input delay is not a nonnegative safe integer');
  const tick = gate.source_tick - inputDelay - 1;
  requireValue(Number.isSafeInteger(tick) && tick >= 0,
    'Platform gate has no valid consumed PAD input boundary');
  return tick;
}

function validatePlatformP2Rows(rows, inputStartTick, throughTick, pressTick, releaseTick) {
  const p2 = exactInputRows(rows, inputStartTick, throughTick, 1, 'P2');
  requireValue(Number.isSafeInteger(pressTick) && pressTick > inputStartTick &&
    Number.isSafeInteger(releaseTick) && releaseTick > pressTick && releaseTick <= throughTick,
  'Platform exit P2 C-stick episode has invalid rise or neutral-release ticks');
  for (const row of p2) {
    if (row.input_tick < pressTick || row.input_tick >= releaseTick) {
      assert(isLiteralNeutral(row.bytes),
        `P2 contains unexpected PAD input at source tick ${row.input_tick}`);
    } else {
      assert(isCStickLeftOnly(row.bytes),
        `P2 C-stick-left row at source tick ${row.input_tick} contains another input`);
    }
  }
  return p2;
}

// Must run before keydown. A missing or nonneutral raw row through the gate
// refuses the experiment before the new episode is issued.
export function validateCombatKoPlatformExitInputPrefix({gate, baseline,
  attackLossReceipt, p1Rows, p2Rows, inputStartTick, inputDelay,
  p2PressTick, p2ReleaseTick}) {
  requirePlatformGate(gate, baseline, attackLossReceipt);
  requireValue(Number.isSafeInteger(inputStartTick) &&
    inputStartTick === baseline.source_tick - inputDelay - 1,
  'Platform exit prefix does not start at the baseline’s declared PAD boundary');
  const gateInputTick = platformGateInputTick(gate, inputDelay);
  requireValue(gateInputTick >= inputStartTick,
    'Platform gate precedes the declared baseline input boundary');
  const p1 = exactInputRows(p1Rows, inputStartTick, gateInputTick, 0, 'P1 platform prefix');
  for (const row of p1) assert(isLiteralNeutral(row.bytes),
    `P1 was not literally neutral before platform-exit keydown at input tick ${row.input_tick}`);
  const p2 = validatePlatformP2Rows(p2Rows, inputStartTick, gateInputTick,
    p2PressTick, p2ReleaseTick);
  return Object.freeze({kind: 'validated-pre-keydown-platform-exit-neutral-prefix-v1',
    gate_source_tick: gate.source_tick, gate_input_tick: gateInputTick,
    p1_neutral_through_gate_row_count: p1.length,
    p2_cstick_left_held_row_count: p2ReleaseTick - p2PressTick,
    p1_rows: freezeEvidenceRows(p1), p2_rows: freezeEvidenceRows(p2)});
}

function isPlatformExitAxisRow(bytes, facingDirection) {
  return Array.isArray(bytes) && bytes.length === 11 &&
    Number.isInteger(bytes[2]) && signedByte(bytes[2]) !== 0 &&
    signedByte(bytes[2]) * facingDirection > 0 &&
    bytes.every((value, index) => index === 2 || value === 0);
}

function validatePlatformExitInputWindow({gate, baseline, attackLossReceipt,
  p1Rows, p2Rows, inputStartTick, inputEndTick, p2PressTick, p2ReleaseTick,
  inputDelay, actionStartTick, movementEndTick, releaseTick}) {
  requirePlatformGate(gate, baseline, attackLossReceipt);
  requireValue(Number.isSafeInteger(inputStartTick) &&
    inputStartTick === baseline.source_tick - inputDelay - 1 &&
    Number.isSafeInteger(inputEndTick) && inputEndTick >= inputStartTick,
  'Platform exit final input window has invalid declared baseline/end boundaries');
  const gateInputTick = platformGateInputTick(gate, inputDelay);
  requireValue(inputEndTick >= gateInputTick &&
    Number.isSafeInteger(actionStartTick) && actionStartTick > gateInputTick &&
    actionStartTick + inputDelay + 1 > gate.source_tick &&
    actionStartTick + inputDelay + 1 > attackLossReceipt.source_tick,
  'Platform exit axis episode was not consumed strictly after the credited platform gate');
  const p1 = exactInputRows(p1Rows, inputStartTick, inputEndTick, 0, 'P1 platform exit');
  const p2 = validatePlatformP2Rows(p2Rows, inputStartTick, inputEndTick,
    p2PressTick, p2ReleaseTick);
  for (let tick = inputStartTick; tick <= gateInputTick; ++tick)
    assert(isLiteralNeutral(p1[tick - inputStartTick].bytes),
      `P1 was not literally neutral through platform gate input tick ${tick}`);
  const episode = [];
  let sawRelease = false;
  for (const row of p1) {
    if (row.input_tick <= gateInputTick) continue;
    if (isLiteralNeutral(row.bytes)) {
      if (episode.length) {
        if (!sawRelease) {
          assert.equal(row.input_tick, episode.at(-1).input_tick + 1,
            'P1 platform exit episode lacks its immediate neutral release row');
          sawRelease = true;
          assert.equal(row.input_tick, releaseTick,
            'P1 platform exit neutral release does not match the observed key-up row');
        }
      }
      continue;
    }
    assert(!sawRelease, 'P1 began a second or disjoint platform exit PAD episode');
    assert(isPlatformExitAxisRow(row.bytes, gate.facing_direction),
      `P1 platform exit row ${row.input_tick} is not the exact facing-directed horizontal-only axis`);
    if (episode.length) {
      assert.equal(row.input_tick, episode.at(-1).input_tick + 1,
        'P1 platform exit axis rows are not one contiguous episode');
      assert.deepEqual(row.bytes, episode[0].bytes,
        'P1 platform exit episode changed its literal horizontal PAD axis byte');
    }
    episode.push(row);
  }
  assert(episode.length >= 1 && episode.length <= COMBAT_KO_PLATFORM_EXIT_MAX_PAD_ROWS,
    `P1 platform exit episode must contain 1..${COMBAT_KO_PLATFORM_EXIT_MAX_PAD_ROWS} held PAD rows`);
  assert(sawRelease, 'P1 platform exit episode lacks its immediate literal neutral release');
  assert.equal(episode[0].input_tick, actionStartTick,
    'P1 first actual axis row differs from the observed keydown input tick');
  assert.equal(movementEndTick, episode.at(-1).input_tick,
    'P1 platform exit final held row differs from the retained movement input');
  return Object.freeze({kind: 'literal-facing-horizontal-platform-exit-v1',
    start_input_tick: inputStartTick, input_delay: inputDelay,
    gate_source_tick: gate.source_tick, gate_input_tick: gateInputTick,
    p1_axis: 'left_x', p1_axis_byte: episode[0].bytes[2],
    facing_direction: gate.facing_direction, direction_key: gate.direction_key,
    p1_axis_rise_input_tick: episode[0].input_tick,
    p1_axis_last_held_input_tick: movementEndTick,
    p1_neutral_release_input_tick: releaseTick,
    p1_axis_held_row_count: episode.length,
    p1_neutral_prefix_row_count: gateInputTick - inputStartTick + 1,
    through_grounded_wait_input_tick: inputEndTick,
    p2_cstick_left_rise_input_tick: p2PressTick,
    p2_cstick_left_neutral_release_input_tick: p2ReleaseTick,
    p2_cstick_left_held_row_count: p2ReleaseTick - p2PressTick,
    p1_rows: freezeEvidenceRows(p1), p2_rows: freezeEvidenceRows(p2)});
}

export function validateCombatKoPlatformExitFall({baseline, gate,
  attackLossReceipt, pairedRows, actionInputTick, inputDelay, sourceCursorLimit}) {
  requireBaseline(baseline);
  requirePlatformGate(gate, baseline, attackLossReceipt);
  requireValue(Number.isSafeInteger(sourceCursorLimit) &&
    sourceCursorLimit === baseline.source_tick + COMBAT_KO_SOURCE_TICK_BUDGET &&
    Number.isSafeInteger(actionInputTick) && Number.isSafeInteger(inputDelay) && inputDelay >= 0,
  'Platform exit Fall changed a declared source/input boundary');
  const state = readPairedRows(pairedRows, 'Combat KO platform exit Fall');
  stagePeerState(state, baseline, 'Combat KO platform exit Fall', sourceCursorLimit);
  const consumed = actionInputTick + inputDelay + 1;
  requireValue(Number.isSafeInteger(consumed) && consumed > gate.source_tick &&
    consumed > attackLossReceipt.source_tick &&
    state.source_tick >= consumed && state.source_tick > gate.source_tick &&
    state.source_frame > gate.source_frame,
  'Platform exit Fall was not observed after the gate-consumed actual P1 input');
  const victims = state.peers.map(peer => peer.by_port.get(0));
  requireValue(victims.every(player => player.motion === COMBAT_KO_PLATFORM_EXIT_FALL_MOTION &&
    player.groundAir === 1),
  'Platform exit requires paired actual source Fall motion 29 while P1 is airborne');
  return Object.freeze({[PLATFORM_FALL_BRAND]: true, [PLATFORM_FALL_GATE]: gate,
    kind: 'validated-post-platform-exit-source-fall-v1',
    source_tick: state.source_tick, source_frame: state.source_frame,
    action_input_tick: actionInputTick, input_delay: inputDelay,
    action_consumed_source_tick: consumed,
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match))});
}

// The new policy has its own final handoff. The legacy neutral-only handoff
// above remains strict and unchanged for every existing route.
export function createCombatKoPlatformExitHandoff({baseline, attackLossReceipt,
  gate, fallReceipt, pairedRows, p1Rows, p2Rows, inputStartTick, inputEndTick,
  p2PressTick, p2ReleaseTick, inputDelay, actionStartTick, movementEndTick, releaseTick}) {
  requireBaseline(baseline);
  requireLossReceipt(attackLossReceipt, baseline);
  requirePlatformGate(gate, baseline, attackLossReceipt);
  requireValue(fallReceipt?.[PLATFORM_FALL_BRAND] === true &&
    fallReceipt[PLATFORM_FALL_GATE] === gate,
  'Platform exit handoff requires this gate’s validated source Fall receipt');
  requireValue(Number.isSafeInteger(actionStartTick) &&
    fallReceipt.action_input_tick === actionStartTick,
  'Platform exit Fall receipt is not bound to the final actual P1 axis action row');
  const consumed = actionStartTick + inputDelay + 1;
  requireValue(Number.isSafeInteger(inputDelay) && inputDelay >= 0 &&
    Number.isSafeInteger(consumed) && fallReceipt.input_delay === inputDelay &&
    fallReceipt.action_consumed_source_tick === consumed &&
    consumed > gate.source_tick && consumed > attackLossReceipt.source_tick &&
    consumed <= fallReceipt.source_tick,
  'Platform exit Fall receipt is not bound to the final consumed input boundary');
  const state = readPairedRows(pairedRows, 'Combat KO platform exit handoff');
  requireSameMapping(state, baseline, 'Combat KO platform exit handoff');
  requireOrderedAndBounded(state, baseline, 'Combat KO platform exit handoff', fallReceipt);
  requireValue(state.source_tick > fallReceipt.source_tick &&
    state.source_frame > fallReceipt.source_frame,
  'Grounded platform exit handoff must follow the observed Fall');
  requireValue(Number.isSafeInteger(inputDelay) && inputDelay >= 0 &&
    Number.isSafeInteger(inputEndTick) && inputEndTick + inputDelay + 1 === state.source_tick,
  'Platform exit final PAD endpoint does not map exactly to the grounded source tick');
  stagePeerState(state, baseline, 'Combat KO platform exit handoff',
    baseline.source_tick + COMBAT_KO_SOURCE_TICK_BUDGET);
  for (const peer of state.peers) {
    const victim = peer.by_port.get(0);
    assert.equal(victim.motion, 14,
      'Platform exit handoff P1 is not in grounded Wait motion 14');
    assert.equal(victim.groundAir, 0,
      'Platform exit handoff P1 is not grounded');
  }
  const inputWindow = validatePlatformExitInputWindow({gate, baseline, attackLossReceipt,
    p1Rows, p2Rows, inputStartTick, inputEndTick, p2PressTick, p2ReleaseTick,
    inputDelay, actionStartTick, movementEndTick, releaseTick});
  return Object.freeze({kind: 'validated-post-credit-platform-exit-combat-ko-handoff-v1',
    input_policy: 'post-credit-platform-exit', starting_stocks: 3,
    validated_loss_receipt: attackLossReceipt, platform_gate: gate,
    platform_exit_fall: fallReceipt, actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match)),
    respawn_source_tick: state.source_tick, respawn_source_frame: state.source_frame,
    input_window: inputWindow});
}

// This accepts only actual common-source rows retained by the live observer.
// It records a sampled S4S60 animation plateau and a later animation advance;
// it cannot infer the hidden charge counter or an authored multiplier.
export function validateCombatKoChargedAnimation({baseline, pairedRows, pressTick,
  inputDelay, releaseTick}) {
  requireBaseline(baseline);
  requireValue(Array.isArray(pairedRows), 'Combat KO charge animation rows are unavailable');
  requireValue(Number.isSafeInteger(pressTick) && Number.isSafeInteger(inputDelay) && inputDelay >= 0,
    'Combat KO charge animation input boundary is invalid');
  const actionStartTick = pressTick + inputDelay + 1;
  const holdEndSourceTick = actionStartTick + COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS;
  requireValue(Number.isSafeInteger(releaseTick) && releaseTick >= holdEndSourceTick &&
    releaseTick - pressTick <= 30,
    'Combat KO charge animation release row is outside the bounded literal episode');
  const releaseConsumedSourceTick = releaseTick + inputDelay + 1;
  const observations = pairedRows.map((rows, index) => {
    const state = readPairedRows(rows, `Combat KO charged animation pair ${index}`);
    requireSameMapping(state, baseline, 'Combat KO charged animation');
    const actors = state.peers.map(peer => peer.match.players.find(player => player.source_port === 1));
    requireValue(actors.every(player => Number.isFinite(player?.animation_frame)),
      'Combat KO charged animation lacks a finite native animation_frame');
    return {source_tick: state.source_tick, source_frame: state.source_frame, actors, rows};
  });
  for (let index = 1; index < observations.length; ++index)
    requireValue(observations[index].source_tick > observations[index - 1].source_tick,
      'Combat KO charged animation observations repeat or move backward');
  const held = observations.filter(row => row.source_tick >= actionStartTick &&
    row.source_tick < holdEndSourceTick);
  const heldInput = observations.filter(row => row.source_tick >= actionStartTick &&
    row.source_tick < releaseConsumedSourceTick);
  let plateau = null;
  for (let index = 0; index < heldInput.length; ++index) {
    const row = heldInput[index];
    if (index > 0)
      requireValue(row.source_frame > heldInput[index - 1].source_frame,
        'Combat KO charged animation source frame did not advance while held');
    requireValue(row.rows.every(peer => {
      const victim = peer.match.players.find(player => player.source_port === 0);
      const original = baseline.actual_matches[0].players.find(player => player.source_port === 0);
      return victim.stocks === 4 && victim.damage_percent === original.damage_percent &&
        !(victim.motion >= 75 && victim.motion <= 91);
    }), 'Combat KO charge plateau observation follows a victim hit');
    requireValue(row.actors.every(player => player.motion === 60 && player.facing_direction === -1),
      'Combat KO charged actor left paired S4S60/facing-1 before release reached the source');
    if (row.source_tick < holdEndSourceTick && index > 0 && row.actors.every((player, peer) =>
        player.animation_frame === heldInput[index - 1].actors[peer].animation_frame))
      plateau ??= row;
  }
  requireValue(plateau !== null,
    'Combat KO charged animation did not show a source-frame-advancing plateau while A was held');
  const resumed = observations.find(row => row.source_tick >= releaseConsumedSourceTick &&
    row.source_frame > plateau.source_frame && row.actors.every((player, peer) => player.motion === 60 &&
      player.facing_direction === -1 && player.animation_frame > plateau.actors[peer].animation_frame));
  requireValue(resumed,
    'Combat KO charged animation did not advance after the released input reached the source');
  return Object.freeze({[CHARGE_BRAND]: true, [CHARGE_BASELINE]: baseline,
    kind: 'observed-s4s60-charge-animation-plateau-and-release-advance-v1',
    action_start_source_tick: actionStartTick,
    hold_end_source_tick: holdEndSourceTick,
    release_consumed_source_tick: releaseConsumedSourceTick,
    plateau_source_tick: plateau.source_tick,
    plateau_source_frame: plateau.source_frame,
    plateau_animation_frame_by_peer: Object.freeze(plateau.actors.map(player => player.animation_frame)),
    resumed_source_tick: resumed.source_tick,
    resumed_source_frame: resumed.source_frame,
    resumed_animation_frame_by_peer: Object.freeze(resumed.actors.map(player => player.animation_frame)),
    observation_scope: 'sampled source-frame-advancing plateau; no continuous or hidden charge-count claim',
    sampled_source_gaps: Object.freeze(observations.slice(1).flatMap((row, index) =>
      row.source_tick > observations[index].source_tick + 1
        ? [Object.freeze({after: observations[index].source_tick, before: row.source_tick,
          unobserved_cursor_count: row.source_tick - observations[index].source_tick - 1})] : [])),
    retained_hold_source_cursor_count: held.length,
    retained_delayed_held_source_cursor_count: heldInput.length - held.length});
}

// Revalidates the same exact counter deltas at the grounded three-stock
// respawn and requires literal neutral input through that observed source tick.
// The route still owns hit/air/death evidence and actual match/room continuity.
export function createCombatKoRespawnHandoff({baseline, attackLossReceipt,
  pairedRows, p1Rows, p2Rows, inputStartTick, inputEndTick,
  pressTick, releaseTick, inputMode = 'cstick', inputDelay = null,
  chargeAnimationEvidence = null}) {
  requireBaseline(baseline);
  requireLossReceipt(attackLossReceipt, baseline);
  const state = readPairedRows(pairedRows, 'Combat KO respawn handoff');
  requireSameMapping(state, baseline, 'Combat KO respawn handoff');
  requireOrderedAndBounded(state, baseline, 'Combat KO respawn handoff', attackLossReceipt);
  requireValue(attackLossReceipt.source_tick > baseline.source_tick &&
    attackLossReceipt.source_frame > baseline.source_frame,
  'Combat KO loss receipt is not ordered after its baseline');
  requireStocks(state, [3, 4], 'Combat KO respawn handoff');
  requireExpectedCounterDeltas(state, baseline, 'Combat KO respawn handoff');
  for (const peer of state.peers) {
    const victim = peer.by_port.get(0);
    assert.equal(victim.motion, 14,
      'Combat KO respawn handoff P1 is not in grounded Wait motion 14');
    assert.equal(victim.groundAir, 0,
      'Combat KO respawn handoff P1 is not grounded');
  }
  // The route derives these raw-input bounds from its declared input delay.
  // PAD input_tick and native source_tick are different clocks; this helper
  // never assumes equality or manufactures the conversion.
  const inputWindow = validateLiteralInputWindow({p1Rows, p2Rows,
    inputStartTick, inputEndTick, pressTick, releaseTick, inputMode, inputDelay});
  if (inputMode === 'charged-a-left')
    requireValue(chargeAnimationEvidence?.[CHARGE_BRAND] === true &&
      chargeAnimationEvidence[CHARGE_BASELINE] === baseline,
    'Combat KO charged handoff requires this baseline’s observed charge animation evidence');
  else requireValue(inputMode === 'cstick', 'Combat KO handoff input mode is unsupported');
  return Object.freeze({starting_stocks: 3,
    validated_loss_receipt: attackLossReceipt,
    ...(inputMode === 'charged-a-left' ? {charge_animation_evidence: chargeAnimationEvidence} : {}),
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match)),
    respawn_source_tick: state.source_tick,
    respawn_source_frame: state.source_frame,
    input_window: inputWindow});
}

// Narrow one-swing observation state. The route supplies already owner/rule-
// checked common-source rows; this validates observed ordering, never fills gaps.
export function advanceCombatKoEvidence({baseline, sequence, pairedRows, actionStartTick,
  chargeReleaseAcceptedSourceTick = null}) {
  requireBaseline(baseline);
  const state = readPairedRows(pairedRows, 'Combat KO sequence');
  requireSameMapping(state, baseline, 'Combat KO sequence');
  requireValue(state.source_tick >= baseline.source_tick &&
    state.source_tick - baseline.source_tick <= COMBAT_KO_SOURCE_TICK_BUDGET,
  'Combat KO sequence exceeded its 480-source-cursor budget');
  if (sequence.last_tick !== undefined) {
    requireValue(state.source_tick >= sequence.last_tick && state.source_frame >= sequence.last_frame,
      'Combat KO sequence moved backward');
  }
  sequence.last_tick = state.source_tick;
  sequence.last_frame = state.source_frame;
  const victim = state.peers[0].by_port.get(0);
  const actor = state.peers[0].by_port.get(1);
  requireValue(actor.stocks === 4 && [4, 3].includes(victim.stocks),
    'Combat KO sequence has an unexpected stock boundary');
  if (victim.stocks === 4) {
    assert.deepEqual(state.counters_by_port, baseline.counters_by_port,
      'Combat KO counters changed before the declared loss');
    requireValue(!sequence.loss, 'Combat KO stock loss reversed');
  } else requireExpectedCounterDeltas(state, baseline, 'Combat KO sequence');
  requireValue(![252, 253].includes(victim.motion),
    'Combat KO victim grabbed the ledge; one-swing hypothesis stopped');
  if (actionStartTick === null || state.source_tick < actionStartTick) {
    requireValue(victim.stocks === 4, 'Combat KO loss preceded consumed attack input');
    return sequence;
  }
  const matches = state.peers.map(peer => peer.match);
  const players = matches.map(match => match.players.find(player => player.source_port === 0));
  const attackers = matches.map(match => match.players.find(player => player.source_port === 1));
  const damage = players.map(player => player.damage_percent);
  requireValue(damage.every(Number.isFinite) && damage[0] === damage[1],
    'Combat KO damage is missing or differs between peers');
  if (attackers.every(player => player.motion === 60 && player.facing_direction === -1))
    sequence.attack ??= state.source_tick;
  const hitObserved = sequence.attack !== undefined && victim.motion >= 75 && victim.motion <= 91 &&
    damage[0] > baseline.actual_matches[0].players.find(player => player.source_port === 0).damage_percent;
  if (hitObserved && chargeReleaseAcceptedSourceTick !== null)
    requireValue(Number.isSafeInteger(chargeReleaseAcceptedSourceTick) &&
      state.source_tick >= chargeReleaseAcceptedSourceTick,
    'Combat KO strike began before the charged release input reached the source; charged evidence is incomplete');
  if (hitObserved)
    sequence.hit ??= state.source_tick;
  if (sequence.hit !== undefined && victim.groundAir === 1 &&
      ((victim.motion >= 75 && victim.motion <= 91) || victim.motion === 38 ||
       (victim.motion >= 29 && victim.motion <= 34))) sequence.air ??= state.source_tick;
  const dead = Number.isInteger(victim.motion) && victim.motion >= 0 && victim.motion <= 10;
  if (sequence.air !== undefined && dead) sequence.death ??= state.source_tick;
  if (sequence.air !== undefined && victim.stocks === 4 && victim.groundAir === 0 && !dead)
    throw Error('Combat KO victim landed with four stocks; one-swing hypothesis stopped');
  if (victim.stocks === 3 && !sequence.loss) {
    requireValue(sequence.attack !== undefined && sequence.hit !== undefined &&
      sequence.air !== undefined && sequence.death !== undefined,
    'Combat KO loss lacks observed attack/hit/air/death sequence');
    sequence.loss = validateCombatKoAttackLoss({baseline, pairedRows});
  }
  if (sequence.loss && state.source_tick > sequence.loss.source_tick &&
      state.source_frame > sequence.loss.source_frame && victim.motion === 14 && victim.groundAir === 0)
    sequence.respawn_rows ??= pairedRows;
  return sequence;
}
