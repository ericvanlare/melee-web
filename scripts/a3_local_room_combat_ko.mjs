import assert from 'node:assert/strict';

export const COMBAT_KO_SOURCE_TICK_BUDGET = 480;

const BASELINE_BRAND = Symbol('validated combat KO baseline');
const LOSS_BRAND = Symbol('validated combat KO loss');
const LOSS_BASELINE = Symbol('combat KO loss baseline');
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
  pressTick, releaseTick}) {
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

// Revalidates the same exact counter deltas at the grounded three-stock
// respawn and requires literal neutral input through that observed source tick.
// The route still owns hit/air/death evidence and actual match/room continuity.
export function createCombatKoRespawnHandoff({baseline, attackLossReceipt,
  pairedRows, p1Rows, p2Rows, inputStartTick, inputEndTick,
  pressTick, releaseTick}) {
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
    inputStartTick, inputEndTick, pressTick, releaseTick});
  return Object.freeze({starting_stocks: 3,
    validated_loss_receipt: attackLossReceipt,
    actual_rows: freezeEvidenceRows(pairedRows),
    actual_matches: Object.freeze(pairedRows.map(row => row.match)),
    respawn_source_tick: state.source_tick,
    respawn_source_frame: state.source_frame,
    input_window: inputWindow});
}

// Narrow one-swing observation state. The route supplies already owner/rule-
// checked common-source rows; this validates observed ordering, never fills gaps.
export function advanceCombatKoEvidence({baseline, sequence, pairedRows, actionStartTick}) {
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
  if (sequence.attack !== undefined && victim.motion >= 75 && victim.motion <= 91 &&
      damage[0] > baseline.actual_matches[0].players.find(player => player.source_port === 0).damage_percent)
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
