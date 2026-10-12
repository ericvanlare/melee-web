import assert from 'node:assert/strict';
import test from 'node:test';
import {comparePairedCombatPlayers, decideP2ApproachTap,
  inspectCombatPairRetainingRawRows, observePairedCombatJab, retainCombatChecksumCursorPoll,
  retainCombatApproachInputReadback,
  retainCombatApproachState, validateCombatItemsNone,
  validateP2GameplayInputWindow, validateSingleP2ApproachTapWindow} from '../scripts/a3_local_room_combat_probe.mjs';
import {validateButtonGameplayInputWindow} from '../scripts/a3_local_room_full_route.mjs';

function combatMatch(overrides = {}) {
  const players = [
    {source_port: 0, motion: 14, groundAir: 0, stocks: 4,
      damage_percent: 0, x: -30, y: 0, animation_frame: 0},
    {source_port: 1, motion: 14, groundAir: 0, stocks: 4,
      damage_percent: 0, x: 30, y: 0, animation_frame: 0},
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
