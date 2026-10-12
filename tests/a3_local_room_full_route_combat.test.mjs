import assert from 'node:assert/strict';
import test from 'node:test';
import {comparePairedCombatPlayers, decideP2ApproachTap,
  comparePairedShieldObservation, inspectCombatPairRetainingRawRows,
  observePairedCombatJab, observePairedShieldJab, retainCombatChecksumCursorPoll,
  assertPairedVictimDamageUnchanged, validateReverseJabSetup,
  retainCombatApproachInputReadback,
  retainCombatApproachState, validateCombatItemsNone,
  validateP2GameplayInputWindow, validateSingleP2ApproachTapWindow} from '../scripts/a3_local_room_combat_probe.mjs';
import {observeShieldSetOffBeforeRelease, validateButtonGameplayInputWindow,
  validateSimultaneousButtonGameplayInputWindows} from '../scripts/a3_local_room_full_route.mjs';

function combatMatch(overrides = {}) {
  const players = [
    {source_port: 0, motion: 14, groundAir: 0, stocks: 4,
      damage_percent: 0, x: -30, y: 0, animation_frame: 0,
      shield_health: 60, facing_direction: 1},
    {source_port: 1, motion: 14, groundAir: 0, stocks: 4,
      damage_percent: 0, x: 30, y: 0, animation_frame: 0,
      shield_health: 60, facing_direction: -1},
  ];
  for (const [port, fields] of Object.entries(overrides.players ?? {}))
    Object.assign(players[Number(port)], fields);
  return {rules: {item_frequency: -1, item_mask_hex: 'ffffffffffffffff'}, players};
}
function paired(overridesA = {}, overridesB = {}) {
  return [combatMatch(overridesA), combatMatch(overridesB)];
}
function padRow(inputTick, localPort, buttons = 0, stickX = 0) {
  return {input_tick: inputTick, source_cursor: inputTick, local_port: localPort,
    bytes: [(buttons >>> 8) & 255, buttons & 255, stickX, 0, 0, 0, 0, 0, 0, 0, 0]};
}

test('P2 approach accepts only bounded positive-gap tap hypotheses', () => {
  assert.equal(decideP2ApproachTap({gap: 42, tapCount: 0,
    sourceTickStart: 100, sourceTick: 120,
    maxObservedInwardDisplacement: 17.64}).decision, 'tap');
  assert.equal(decideP2ApproachTap({gap: 18, tapCount: 2,
    sourceTickStart: 100, sourceTick: 300,
    maxObservedInwardDisplacement: 17.64}).decision, 'reached');
  assert.equal(decideP2ApproachTap({gap: 20.1, tapCount: 1,
    sourceTickStart: 100, sourceTick: 150,
    maxObservedInwardDisplacement: 20.1}).reason,
  'next-tap-could-cross-from-measured-displacement');
  assert.equal(decideP2ApproachTap({gap: 0, tapCount: 1,
    sourceTickStart: 100, sourceTick: 150,
    maxObservedInwardDisplacement: 10}).reason, 'crossed-initial-side');
  assert.equal(decideP2ApproachTap({gap: 40, tapCount: 16,
    sourceTickStart: 100, sourceTick: 150,
    maxObservedInwardDisplacement: 10}).reason, 'tap-budget-exhausted');
  assert.equal(decideP2ApproachTap({gap: 40, tapCount: 1,
    sourceTickStart: 100, sourceTick: 580,
    maxObservedInwardDisplacement: 10}).reason, 'source-cursor-budget-exhausted');
  assert.throws(() => decideP2ApproachTap({gap: Number.NaN, tapCount: 0,
    sourceTickStart: 100, sourceTick: 100}));
});

test('P2 approach window retains one literal axis sample, neutral release, and neutral P1', () => {
  const result = validateP2GameplayInputWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(21, 1)],
    p1Rows: [padRow(20, 0), padRow(21, 0)],
    movementTick: 20, releaseTick: 21, direction: -1,
  });
  assert.equal(result.p2_direction, -1);
  assert.equal(result.p1_neutral_capture_count, 2);
  assert.deepEqual(validateSingleP2ApproachTapWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(21, 1)],
    p1Rows: [padRow(20, 0), padRow(21, 0)], movementTick: 20, releaseTick: 21,
    direction: -1, expectedMovementBytes: padRow(20, 1, 0, 255).bytes,
  }).validated, result);
  assert.throws(() => validateSingleP2ApproachTapWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(21, 1, 0, 255), padRow(22, 1)],
    p1Rows: [padRow(20, 0), padRow(21, 0), padRow(22, 0)],
    movementTick: 20, releaseTick: 22, direction: -1,
    expectedMovementBytes: padRow(20, 1, 0, 255).bytes,
  }), /exactly one captured active source row/);
  assert.throws(() => validateSingleP2ApproachTapWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(21, 1)],
    p1Rows: [padRow(20, 0), padRow(21, 0)], movementTick: 20, releaseTick: 21,
    direction: -1, expectedMovementBytes: padRow(20, 1, 0, 64).bytes,
  }), /differs from the retained movement sample/);
  assert.throws(() => validateP2GameplayInputWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(22, 1)],
    p1Rows: [padRow(20, 0), padRow(22, 0)],
    movementTick: 20, releaseTick: 22, direction: -1,
  }), /missing or duplicate tick/);
  assert.throws(() => validateP2GameplayInputWindow({
    p2Rows: [padRow(20, 1, 0, 255), padRow(21, 1)],
    p1Rows: [padRow(20, 0), padRow(21, 0, 0x0100)],
    movementTick: 20, releaseTick: 21, direction: -1,
  }), /P1 was not literally neutral/);
});

test('P2 jab window preserves A press/release rows and neutral P1 rows', () => {
  const result = validateButtonGameplayInputWindow({
    actionRows: [padRow(40, 1, 0x0100), padRow(41, 1)],
    otherPortRows: [padRow(40, 0), padRow(41, 0)],
    pressTick: 40, releaseTick: 41, port: 1, button: 0x0100,
  });
  assert.equal(result.action_sample_count, 1);
  assert.equal(result.other_port_neutral_count, 2);
});

test('exchange input windows require exact simultaneous A/R bytes and both neutral releases', () => {
  const p1Rows = [padRow(20, 0), padRow(21, 0, 0x0100), padRow(22, 0, 0x0100),
    padRow(23, 0), padRow(24, 0)];
  const p2Rows = [padRow(20, 1, 0x0020), padRow(21, 1, 0x0020),
    padRow(22, 1, 0x0020), padRow(23, 1, 0x0020), padRow(24, 1)];
  assert.deepEqual(validateSimultaneousButtonGameplayInputWindows({
    p1Rows, p2Rows, p1PressTick: 21, p1ReleaseTick: 23,
    p2PressTick: 20, p2ReleaseTick: 24,
  }), {p1_press_input_tick: 21, p1_neutral_release_input_tick: 23,
    neutral_release_input_tick: 23, p2_press_input_tick: 20,
    p2_neutral_release_input_tick: 24, union_start_input_tick: 20,
    union_end_input_tick: 24, p1_active_samples: 2, p2_active_samples: 4,
    simultaneous_held_input_ticks: 2, p1_expected_button: 0x0100, p2_expected_button: 0x0020});
  const extraP1BeforeA = structuredClone(p1Rows);
  extraP1BeforeA[0].bytes[1] = 0x20;
  assert.throws(() => validateSimultaneousButtonGameplayInputWindows({
    p1Rows: extraP1BeforeA, p2Rows, p1PressTick: 21, p1ReleaseTick: 23,
    p2PressTick: 20, p2ReleaseTick: 24,
  }), /exact neutral outside/);
  const extraP2AfterR = [...structuredClone(p2Rows), padRow(25, 1, 0x0100), padRow(26, 1)];
  const laterP1Release = Array.from({length: 7}, (_, offset) =>
    padRow(20 + offset, 0, offset >= 1 && offset < 6 ? 0x0100 : 0));
  assert.throws(() => validateSimultaneousButtonGameplayInputWindows({
    p1Rows: laterP1Release, p2Rows: extraP2AfterR,
    p1PressTick: 21, p1ReleaseTick: 26, p2PressTick: 20, p2ReleaseTick: 24,
  }), /P2 R window.*exact neutral outside/);
  assert.throws(() => validateSimultaneousButtonGameplayInputWindows({
    p1Rows: [padRow(20, 0), padRow(21, 0, 0x0100), padRow(23, 0), padRow(24, 0)], p2Rows,
    p1PressTick: 21, p1ReleaseTick: 23, p2PressTick: 20, p2ReleaseTick: 24,
  }), /missing or duplicate/);
  assert.throws(() => validateSimultaneousButtonGameplayInputWindows({
    p1Rows, p2Rows, p1PressTick: 21, p1ReleaseTick: 23,
    p2PressTick: 20, p2ReleaseTick: 51,
  }), /30-input-tick/);
  const capP1 = Array.from({length: 31}, (_, offset) =>
    padRow(20 + offset, 0, offset === 0 ? 0x0100 : 0));
  const capP2 = Array.from({length: 31}, (_, offset) =>
    padRow(20 + offset, 1, offset < 30 ? 0x0020 : 0));
  assert.equal(validateSimultaneousButtonGameplayInputWindows({
    p1Rows: capP1, p2Rows: capP2, p1PressTick: 20, p1ReleaseTick: 21,
    p2PressTick: 20, p2ReleaseTick: 50,
  }).p2_active_samples, 30);
});

test('reverse-jab setup rejects crossed positions and backwards P1 facing', () => {
  const valid = paired({players: {0: {x: 0}, 1: {x: 15}}},
    {players: {0: {x: 0}, 1: {x: 15}}});
  assert.deepEqual(validateReverseJabSetup(valid, {maxGap: 20}), {
    p1_x_by_peer: [0, 0], p2_x_by_peer: [15, 15],
    signed_gap_by_peer: [15, 15], p1_facing_direction_by_peer: [1, 1]});
  const crossed = paired({players: {0: {x: 10}, 1: {x: -10}}},
    {players: {0: {x: 10}, 1: {x: -10}}});
  assert.throws(() => validateReverseJabSetup(crossed), /signed P2x-P1x/);
  const backwards = paired({players: {0: {x: 0, facing_direction: -1}, 1: {x: 15}}},
    {players: {0: {x: 0, facing_direction: -1}, 1: {x: 15}}});
  assert.throws(() => validateReverseJabSetup(backwards), /P1 facing_direction \+1/);
});

test('P2 damage guard rejects changes after 181 and at grounded recovery', () => {
  const post181Damage = paired({players: {1: {motion: 181, damage_percent: 1}}},
    {players: {1: {motion: 181, damage_percent: 1}}});
  assert.throws(() => assertPairedVictimDamageUnchanged(post181Damage, {
    initialVictimDamage: 0, victimPort: 1}), /P2 damage changed/);
  const damagedRecovery = paired({players: {1: {motion: 14, damage_percent: 1}}},
    {players: {1: {motion: 14, damage_percent: 1}}});
  assert.throws(() => assertPairedVictimDamageUnchanged(damagedRecovery, {
    initialVictimDamage: 0, victimPort: 1}), /P2 damage changed/);
});

test('shield response requires paired 181, unchanged damage, and guard-entry-relative shield loss', () => {
  const hit = paired({players: {0: {motion: 44}, 1: {motion: 181, shield_health: 54}}},
    {players: {0: {motion: 44}, 1: {motion: 181, shield_health: 54}}});
  assert.equal(comparePairedShieldObservation(hit), true);
  const response = observePairedShieldJab(hit, {initialVictimDamage: 0,
    guardEntryShieldHealth: 60});
  assert.equal(response.guard_setoff_181_observed, true);
  assert.equal(response.victim_damage_unchanged, true);
  assert.equal(response.victim_shield_decreased_from_guard_entry, true);
  assert.equal(response.hit_response_observed, true);
  assert.equal(response.shield_hit_response_observed, true);

  const passiveDrain = paired({players: {1: {motion: 179, shield_health: 54}}},
    {players: {1: {motion: 179, shield_health: 54}}});
  const drainResponse = observePairedShieldJab(passiveDrain, {initialVictimDamage: 0,
    guardEntryShieldHealth: 60});
  assert.equal(drainResponse.victim_shield_decreased_from_guard_entry, true);
  assert.equal(drainResponse.hit_response_observed, false);
  assert.equal(drainResponse.shield_hit_response_observed, false);
  const damaged = paired({players: {1: {motion: 181, shield_health: 54, damage_percent: 1}}},
    {players: {1: {motion: 181, shield_health: 54, damage_percent: 1}}});
  assert.equal(observePairedShieldJab(damaged, {initialVictimDamage: 0,
    guardEntryShieldHealth: 60}).victim_damage_unchanged, false);
  const disagreement = structuredClone(hit);
  disagreement[1].players[1].shield_health = 53;
  assert.throws(() => comparePairedShieldObservation(disagreement), /disagree on P2 shield_health/);
  const missing = structuredClone(hit);
  delete missing[1].players[1].facing_direction;
  assert.throws(() => comparePairedShieldObservation(missing), /facing_direction is not finite/);
});

test('shield hold page predicate retains first R row and latest native observation at bounded refusal', () => {
  const original = Object.fromEntries(['document', '__net', 'Module',
    'meleeNetRuntimeLockstepHealth'].map(key => [key,
    Object.getOwnPropertyDescriptor(globalThis, key)]));
  let cursor = 132;
  let motion = 179;
  const rRow = padRow(100, 1, 0x0020);
  const install = rows => {
    globalThis.document = {querySelector: () => ({dataset: {}})};
    globalThis.__net = {status: () => ({cursor})};
    globalThis.Module = {_melee_web_native_menu_match_observe: () => 1,
      UTF8ToString: () => JSON.stringify({players: [combatMatch().players[0],
        {...combatMatch().players[1], motion}]})};
    globalThis.meleeNetRuntimeLockstepHealth = () => ({failure: null,
      peer: {failure: null, localInputCapture: {captures_since: rows}}});
  };
  try {
    install([rRow]);
    assert.equal(observeShieldSetOffBeforeRelease({inputFrom: 100, inputDelay: 2,
      maxSourceTicks: 30}), false);
    motion = 181;
    const observed = observeShieldSetOffBeforeRelease({inputFrom: 100, inputDelay: 2,
      maxSourceTicks: 30});
    assert.equal(observed.status, 'guard-setoff-observed');
    assert.equal(observed.first_r_input_row, rRow);
    assert.equal(observed.consumed_input_boundary, 103);
    assert.equal(observed.last_native_observation.source_cursor, 132);
    motion = 179;
    cursor = 134;
    const refused = observeShieldSetOffBeforeRelease({inputFrom: 100, inputDelay: 2,
      maxSourceTicks: 30});
    assert.equal(refused.status, 'source-budget-exhausted');
    assert.equal(refused.first_r_input_row, rRow);
    assert.equal(refused.last_native_observation.source_cursor, 134);
    motion = 181;
    cursor = 134;
    const lateSetOff = observeShieldSetOffBeforeRelease({inputFrom: 100, inputDelay: 2,
      maxSourceTicks: 30});
    assert.equal(lateSetOff.status, 'source-budget-exhausted');
    const wrong = {...rRow, bytes: padRow(100, 1, 0x0100).bytes};
    install([wrong]);
    assert.equal(observeShieldSetOffBeforeRelease({inputFrom: 100, inputDelay: 2,
      maxSourceTicks: 30}).reason, 'unexpected-first-pad-row');
  } finally {
    for (const [key, descriptor] of Object.entries(original)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
});

test('combat requires paired Items None rules and preserves the observed mask', () => {
  assert.deepEqual(validateCombatItemsNone(paired()), {
    item_frequency: -1, item_mask_hex: 'ffffffffffffffff'});
  assert.throws(() => validateCombatItemsNone(paired(), '0000000000000000'), /disagree/);
  const differentMasks = paired();
  differentMasks[1].rules.item_mask_hex = '0000000000000000';
  assert.throws(() => validateCombatItemsNone(differentMasks), /disagree/);
  const itemFrequency = paired();
  itemFrequency[0].rules.item_frequency = 0;
  assert.throws(() => validateCombatItemsNone(itemFrequency), /Items None/);
});

test('paired combat fields must match at the common source cursor', () => {
  assert.equal(comparePairedCombatPlayers(paired()), true);
  for (const field of ['source_port', 'motion', 'groundAir', 'stocks',
    'damage_percent', 'x', 'y', 'animation_frame']) {
    const mismatched = paired();
    mismatched[1].players[1][field] = field === 'source_port' ? 0 : 1;
    assert.throws(() => comparePairedCombatPlayers(mismatched), new RegExp('P2 ' + field));
  }
});

test('combat action retains complete inspected native pair before a guard rejects it', () => {
  const rows = [
    {source_tick: 311, phase: 7, running: 1, message: 'active', match: combatMatch()},
    {source_tick: 311, phase: 7, running: 1, message: 'active', match: combatMatch()},
  ];
  rows[1].match.players[0].stocks = 3;
  const callbackRecord = {};
  assert.throws(() => inspectCombatPairRetainingRawRows(callbackRecord, rows, inspected =>
    comparePairedCombatPlayers(inspected.map(row => row.match))), /P1 stocks/);
  assert.equal(callbackRecord.last_inspected_pair.peers, rows,
    'raw native rows must remain available after the paired guard throws');

  const observerRecord = {};
  assert.equal(inspectCombatPairRetainingRawRows(observerRecord, rows), rows,
    'the production observer retention call preserves the original rows');
  assert.equal(observerRecord.last_inspected_pair.source_tick, 311);
  assert.deepEqual(observerRecord.last_inspected_pair.peers, rows);
});

test('each approach checksum poll retains both native cursors and refuses the first over-limit poll', () => {
  const evidence = {};
  const limit = 480;
  const compareTarget = 900;
  const belowTarget = states => states.every(state =>
    state.owner_health.peer.protocol.next_checksum_compare >= compareTarget);
  const below = [
    {native_status: {cursor: limit}, owner_health: {peer: {protocol: {next_checksum_compare: 300}}}},
    {native_status: {cursor: limit - 1}, owner_health: {peer: {protocol: {next_checksum_compare: 299}}}},
  ];
  assert.equal(belowTarget(below), false, 'the bounded prefix has not reached its checksum target');
  retainCombatChecksumCursorPoll({evidenceSink: evidence, states: below, sourceCursorLimit: limit});
  assert.equal(evidence.checksum_cursor_poll_count, 1);
  assert.deepEqual(evidence.last_checksum_cursor_poll.peers, [
    {peer: 'alpha', source_cursor: limit}, {peer: 'beta', source_cursor: limit - 1},
  ]);

  const crossing = [
    {native_status: {cursor: limit + 1}, owner_health: {peer: {protocol: {next_checksum_compare: 301}}}},
    {native_status: {cursor: limit}, owner_health: {peer: {protocol: {next_checksum_compare: 300}}}},
  ];
  assert.equal(belowTarget(crossing), false, 'the crossing poll is still below the checksum target');
  assert.throws(() => retainCombatChecksumCursorPoll({evidenceSink: evidence,
    states: crossing, sourceCursorLimit: limit}), /alpha: 481 > 480/);
  assert.equal(evidence.checksum_cursor_poll_count, 2);
  assert.deepEqual(evidence.checksum_cursor_budget_failure.peers, [
    {peer: 'alpha', source_cursor: 481}, {peer: 'beta', source_cursor: 480},
  ], 'the offending two-peer native cursor pair must be retained before refusal');
  assert.throws(() => retainCombatChecksumCursorPoll({evidenceSink: {},
    states: [{native_status: {cursor: 480}}, {native_status: {cursor: null}}],
    sourceCursorLimit: limit}), /beta: null/);
});

test('paired jab requires Attack11 and an increased victim damage reaction', () => {
  const hit = paired(
    {players: {0: {motion: 75, damage_percent: 3.2}, 1: {motion: 44}}},
    {players: {0: {motion: 75, damage_percent: 3.2}, 1: {motion: 44}}});
  const response = observePairedCombatJab(hit, {initialVictimDamage: 0});
  assert.equal(response.actor_attack11_observed, true);
  assert.equal(response.victim_damage_motion_observed, true);
  assert.equal(response.hit_response_observed, true);
  const staggerAfterAttackMotion = paired(
    {players: {0: {motion: 75, damage_percent: 3.2}, 1: {motion: 14}}},
    {players: {0: {motion: 75, damage_percent: 3.2}, 1: {motion: 14}}});
  const laterResponse = observePairedCombatJab(staggerAfterAttackMotion,
    {initialVictimDamage: 0});
  assert.equal(laterResponse.actor_attack11_observed, false);
  assert.equal(laterResponse.hit_response_observed, true,
    'paired victim reaction is observed at its own cursor after the jab motion');
  const noHit = paired(
    {players: {0: {motion: 75, damage_percent: 0}, 1: {motion: 44}}},
    {players: {0: {motion: 75, damage_percent: 0}, 1: {motion: 44}}});
  assert.equal(observePairedCombatJab(noHit, {initialVictimDamage: 0}).hit_response_observed, false);
  const wrongVictimMotion = paired(
    {players: {0: {motion: 14, damage_percent: 3.2}, 1: {motion: 44}}},
    {players: {0: {motion: 14, damage_percent: 3.2}, 1: {motion: 44}}});
  assert.equal(observePairedCombatJab(wrongVictimMotion,
    {initialVictimDamage: 0}).hit_response_observed, false);
  const peerMismatch = paired(
    {players: {0: {motion: 75, damage_percent: 3.2}, 1: {motion: 44}}},
    {players: {0: {motion: 14, damage_percent: 3.2}, 1: {motion: 44}}});
  assert.throws(() => observePairedCombatJab(peerMismatch,
    {initialVictimDamage: 0}), /disagree on P1 motion/);
});


test('approach budget refusal retains the over-limit state and literal PAD suffix first', () => {
  const stateEvidence = {};
  assert.throws(() => retainCombatApproachState({evidenceSink: stateEvidence,
    state: {native_status: {cursor: 581, local_capture_failure: null},
      runtime_error: null, owner_health: {failure: null, peer: {failure: null,
        localInputCapture: {captured_count: 582}}}}, sourceCursorLimit: 580}),
  /state acquisition exceeded/);
  assert.deepEqual(stateEvidence.last_p1_state_readback,
    {native_cursor: 581, captured_count: 582, runtime_error: null,
      owner_failure: null, peer_failure: null, local_capture_failure: null});

  const readbackEvidence = {};
  const rows = [padRow(20, 1, 0x0100), padRow(21, 1)];
  assert.throws(() => retainCombatApproachInputReadback({evidenceSink: readbackEvidence,
    p2Readback: {native_status: {cursor: 581}, health: {failure: null, peer: {
      failure: null, localInputCapture: {captures_since: rows}}}},
    p1Readback: {native_status: {cursor: 581}, health: {failure: null, peer: {
      failure: null, localInputCapture: {captures_since: [padRow(20, 0), padRow(21, 0)]}}}},
    movementTick: 20, sourceCursorLimit: 580}), /input suffix acquisition exceeded/);
  assert.deepEqual(readbackEvidence.p2_literal_rows, rows);
  assert.equal(readbackEvidence.observed_capture_end_tick, 21);
  assert.equal(readbackEvidence.last_suffix_readback.p2_native_cursor, 581);
});
