import assert from 'node:assert/strict';
import test from 'node:test';
import {readFileSync} from 'node:fs';
import {comparePairedCombatPlayers, decideP2ApproachTap,
  comparePairedShieldObservation, inspectCombatPairRetainingRawRows,
  observePairedCombatJab, observePairedShieldJab, retainCombatChecksumCursorPoll,
  assertPairedVictimDamageUnchanged, validateReverseJabSetup,
  retainCombatApproachInputReadback,
  retainCombatApproachState, validateCombatItemsNone,
  validateP2GameplayInputWindow, validateSingleP2ApproachTapWindow,
  validatePreparedCombatInputContinuity} from '../scripts/a3_local_room_combat_probe.mjs';
import {actionObservationTickDisposition, acquireAndInspectActionPair,
  observeShieldSetOffBeforeRelease, retainPreStartActionPair,
  retainAndGuardPreparedPair, retainPreparedKoPreparation, classifyPreparedCombatPair,
  waitForCombatKoPlatformExitGate, findCombatKoPlatformExitFallPair,
  isCombatKoPlatformExitGateCandidate, initializeCombatKoAction,
  preparedKoSourceCursorLimit,
  observeNativePeerPageSample, inspectPreparedApproachTransition,
  validatePreparedApproachTransition,
  retainShieldHoldOutcome, validateButtonGameplayInputWindow,
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

test('prepared approach measures actual P2 inward travel and guards paired recovery damage', () => {
  const pairAt = (p1x, p2x, p1Damage = 14.55, p2Damage = 3) => {
    const overrides = {players: {
      0: {x: p1x, damage_percent: p1Damage}, 1: {x: p2x, damage_percent: p2Damage},
    }};
    return paired(overrides, overrides);
  };
  const before = pairAt(-66.77314, -46.3515167);
  const after = pairAt(-68.8731613, -62.0715294);
  const measured = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: after, label: 'retained source pair 850→875'});
  validatePreparedApproachTransition(measured, {requireProgress: true});
  assert.equal(measured.p1_x_displacement.toFixed(7), '-2.1000213');
  assert.equal(measured.p2_inward_displacement.toFixed(7), '15.7200127');
  assert.equal(measured.gap_reduction.toFixed(7), '13.6199914');
  assert.notEqual(measured.p2_inward_displacement, measured.gap_reduction);
  assert.equal(measured.damage_unchanged, true);

  const noP2Motion = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: pairAt(-67, -46.3515167), label: 'no P2 inward movement'});
  assert.throws(() => validatePreparedApproachTransition(noP2Motion,
    {requireProgress: true}), /P2 did not make positive measured inward progress/);
  const noGapReduction = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: pairAt(-68, -47.5783767), label: 'no positive gap reduction'});
  assert.throws(() => validatePreparedApproachTransition(noGapReduction,
    {requireProgress: true}), /positive player gap did not reduce/);
  const crossed = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: pairAt(-50, -52), label: 'crossed player sides'});
  assert.throws(() => validatePreparedApproachTransition(crossed), /lost positive side order/);
  const nonfinite = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: pairAt(-60, Infinity), label: 'nonfinite position'});
  assert.throws(() => validatePreparedApproachTransition(nonfinite), /not finite/);
  const changedDamage = inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: pairAt(-67, -55, 14.56, 3), label: 'changed damage'});
  assert.throws(() => validatePreparedApproachTransition(changedDamage), /damage changed/);
  const peerDisagreement = pairAt(-67, -55);
  peerDisagreement[1].players[0].x = -66;
  assert.throws(() => inspectPreparedApproachTransition({beforeMatches: before,
    afterMatches: peerDisagreement, label: 'peer scorer disagreement'}),
  /Combat peers disagree on P1 x/);
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

test('prepared combat requires a complete P1-neutral/P2-verified input history', () => {
  const p1Rows = Array.from({length: 7}, (_, index) => padRow(20 + index, 0));
  const p2Rows = [padRow(20, 1), padRow(21, 1, 0, 255), padRow(22, 1),
    padRow(23, 1), padRow(24, 1), padRow(25, 1, 0x0100), padRow(26, 1)];
  const result = validatePreparedCombatInputContinuity({p1Rows, p2Rows,
    inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]});
  assert.deepEqual(result, {status: 'validated', input_start_tick: 20,
    input_end_tick: 26, source_input_rows: 7, verified_p2_non_neutral_rows: 2,
    p1_neutral: true, p2_neutral_outside_verified_windows: true});
  assert.throws(() => validatePreparedCombatInputContinuity({p1Rows: p1Rows.slice(0, -1),
    p2Rows, inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]}), /missing or duplicate/);
  assert.throws(() => validatePreparedCombatInputContinuity({p1Rows: [
    ...p1Rows.slice(0, 2), padRow(22, 0, 0x0100), ...p1Rows.slice(3)],
    p2Rows, inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]}), /P1 was not literally neutral/);
  assert.throws(() => validatePreparedCombatInputContinuity({p1Rows,
    p2Rows: [...p2Rows.slice(0, 4), padRow(24, 1, 0x0020), ...p2Rows.slice(5)],
    inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]}), /unverified or non-neutral/);
  assert.throws(() => validatePreparedCombatInputContinuity({p1Rows,
    p2Rows: [...p2Rows].reverse(), inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]}), /exact, ordered/);
  assert.throws(() => validatePreparedCombatInputContinuity({p1Rows, p2Rows,
    inputStartTick: 20, inputEndTick: 26,
    verifiedP2Rows: [p2Rows[1], p2Rows[1], p2Rows[2], p2Rows[5], p2Rows[6]]}), /overlap/);
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

test('combined peer sample uses one page evaluation and preserves the row and room mapping', async () => {
  const names = ['document', 'Module', '__net', 'menuObserveCssSetup',
    'menuObserveStage', 'meleeNetRuntimeLockstepHealth'];
  const original = Object.fromEntries(names.map(key => [key,
    Object.getOwnPropertyDescriptor(globalThis, key)]));
  let evaluations = 0;
  let matchReads = 0;
  let statusReads = 0;
  let healthReads = 0;
  const nativeStatus = {cursor: 668, blocker: 'network_wait'};
  const match = {frame: 228, players: [
    {source_port: 0, motion: 14}, {source_port: 1, motion: 179}],
  };
  const ownerHealth = {failure: null, peer: {failure: null, endpoint_error_count: 0}};
  globalThis.document = {querySelector: () => ({dataset: {runtimeError: ''}})};
  globalThis.Module = {
    _melee_web_native_menu_phase: () => 7,
    _melee_web_native_menu_running: () => 1,
    _melee_web_native_menu_message: () => 23,
    _melee_web_native_menu_match_observe: () => { matchReads++; return 11; },
    UTF8ToString: pointer => pointer === 23 ? 'Original match' : JSON.stringify(match),
  };
  globalThis.__net = {status: () => { statusReads++; return nativeStatus; }};
  globalThis.menuObserveCssSetup = () => ({doors: [1, 2]});
  globalThis.menuObserveStage = id => ({stage_id: id});
  globalThis.meleeNetRuntimeLockstepHealth = () => { healthReads++; return ownerHealth; };
  const page = {evaluate: async (fn, arg) => { evaluations++; return fn(arg); }};
  try {
    const sample = await page.evaluate(observeNativePeerPageSample, {includeRoom: true});
    assert.equal(evaluations, 1);
    assert.equal(matchReads, 1);
    assert.equal(statusReads, 1);
    assert.equal(healthReads, 1);
    assert.deepEqual(sample.row, {phase: 7, running: 1, message: 'Original match',
      error: null, source_tick: 668, native_status: nativeStatus,
      css: {doors: [1, 2]}, stage: {stage_id: 0x20}, match});
    assert.deepEqual(sample.room, {runtime_error: null, menu_phase: sample.row.phase,
      menu_running: sample.row.running, menu_message: sample.row.message,
      native_status: sample.row.native_status, owner_health: ownerHealth});
    assert.equal(sample.error, null);
    const rowOnly = await page.evaluate(observeNativePeerPageSample, {includeRoom: false});
    assert.deepEqual(rowOnly, sample.row);
    assert.equal(evaluations, 2);
    assert.equal(healthReads, 1);
  } finally {
    for (const [key, descriptor] of Object.entries(original)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
});

test('combined peer sample retains the native row when owner health throws', async () => {
  const names = ['document', 'Module', '__net', 'menuObserveCssSetup',
    'menuObserveStage', 'meleeNetRuntimeLockstepHealth'];
  const original = Object.fromEntries(names.map(key => [key,
    Object.getOwnPropertyDescriptor(globalThis, key)]));
  const nativeStatus = {cursor: 679};
  const match = {frame: 239, players: [{source_port: 0, motion: 44},
    {source_port: 1, motion: 181}]};
  globalThis.document = {querySelector: () => ({dataset: {runtimeError: null}})};
  globalThis.Module = {
    _melee_web_native_menu_phase: () => 7,
    _melee_web_native_menu_running: () => 1,
    _melee_web_native_menu_message: () => 29,
    _melee_web_native_menu_match_observe: () => 11,
    UTF8ToString: pointer => pointer === 29 ? 'Original match' : JSON.stringify(match),
  };
  globalThis.__net = {status: () => nativeStatus};
  globalThis.menuObserveCssSetup = () => null;
  globalThis.menuObserveStage = () => null;
  globalThis.meleeNetRuntimeLockstepHealth = () => { throw Error('fixture health decode failed'); };
  let evaluations = 0;
  const page = {evaluate: async (fn, arg) => { evaluations++; return fn(arg); }};
  try {
    const sample = await page.evaluate(observeNativePeerPageSample, {includeRoom: true});
    assert.equal(evaluations, 1);
    assert.equal(sample.row.source_tick, 679);
    assert.equal(sample.row.match.players[1].motion, 181);
    assert.equal(sample.room, null);
    assert.match(sample.error, /fixture health decode failed/);
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


test('action observer drains pre-start pairs before the pair returned by an in-flight await', async () => {
  const pair = (tick, p2Motion, message = 'active') => Array.from({length: 2}, (_, index) => ({
    source_tick: tick, phase: 7, running: 1, message,
    match: {players: [{source_port: 0, motion: 44}, {source_port: 1, motion: p2Motion}]},
    peer: index === 0 ? 'alpha' : 'beta',
  }));
  const pendingBeforeStart = [];
  for (let tick = 671; tick <= 677; tick++)
    assert.equal(retainPreStartActionPair(pendingBeforeStart,
      pair(tick, tick === 677 ? 181 : 179)), true);

  let actionStartTick = null;
  let lastRetainedTick = -1;
  let stopped = false;
  const observed = [];
  const inspectPair = async rows => {
    const tick = rows[0].source_tick;
    const disposition = actionObservationTickDisposition(tick,
      actionStartTick, lastRetainedTick);
    if (disposition === 'skip') return;
    if (disposition === 'stop') {
      stopped = true;
      return;
    }
    observed.push({source_tick: tick, p2_motion: rows[0].match.players[1].motion});
    lastRetainedTick = tick;
  };
  let resolvePair;
  const acquiredPair = new Promise(resolve => { resolvePair = resolve; });
  const acquisition = acquireAndInspectActionPair({
    acquirePair: () => acquiredPair,
    pendingBeforeStart,
    actionStarted: () => actionStartTick !== null,
    inspectPair,
    shouldStop: () => stopped,
  });
  actionStartTick = 671; // startAt lands while observeAtCommonSourceCursor is awaiting
  resolvePair(pair(678, 180));
  await acquisition;

  assert.deepEqual(observed.map(row => row.source_tick),
    [671, 672, 673, 674, 675, 676, 677, 678]);
  assert.equal(observed.find(row => row.source_tick === 677).p2_motion, 181,
    'paired GuardSetOff must reach the observer before later Guard motion');
});

test('prepared platform gate wait fails promptly on observer error and joins the settled outcome', async () => {
  const observerError = Error('bounded KO observer failed before RebirthWait');
  let resolveOutcome;
  const outcome = new Promise(resolve => { resolveOutcome = resolve; });
  let joined = false;
  const joinedOutcome = outcome.then(() => { joined = true; });
  const gateNeverArrives = new Promise(() => {});
  const waiting = waitForCombatKoPlatformExitGate(gateNeverArrives, outcome);
  resolveOutcome({error: observerError});
  await assert.rejects(waiting, /bounded KO observer failed before RebirthWait/);
  await joinedOutcome;
  assert.equal(joined, true);
  assert.deepEqual(await waitForCombatKoPlatformExitGate(Promise.resolve({source_tick: 12}),
    new Promise(() => {})), {source_tick: 12});
});

test('platform Fall lookup retains the brief paired row captured before action receipt resolves', () => {
  const pair = (source_tick, {fall = true, active = true} = {}) => ({source_tick,
    active_match: active, peers: [0, 1].map(() => ({match: {players: [{source_port: 0,
      motion: fall ? 29 : 13, groundAir: fall ? 1 : 0}]}}))});
  const acquiredWhileReceiptPending = pair(145);
  const retained = [pair(140), pair(143, {active:false}), acquiredWhileReceiptPending];
  const found = findCombatKoPlatformExitFallPair(retained, {
    gateSourceTick:136, actionInputTick:137, inputDelay:3});
  assert.equal(found, acquiredWhileReceiptPending);
  assert.equal(findCombatKoPlatformExitFallPair([acquiredWhileReceiptPending], {
    gateSourceTick:136, actionInputTick:142, inputDelay:3}), null,
  'a Fall before the actual action reaches source must not satisfy the handoff');
});

test('action observer cancellation during acquisition inspects neither buffered nor returned pairs', async () => {
  const pair = tick => Array.from({length: 2}, (_, index) => ({source_tick: tick,
    phase: 7, running: 1, message: 'active', match: {frame: tick},
    peer: index === 0 ? 'alpha' : 'beta'}));
  const pendingBeforeStart = [];
  retainPreStartActionPair(pendingBeforeStart, pair(671));
  retainPreStartActionPair(pendingBeforeStart, pair(672));
  let cancelled = false;
  const inspected = [];
  let resolvePair;
  const acquiredPair = new Promise(resolve => { resolvePair = resolve; });
  const acquisition = acquireAndInspectActionPair({
    acquirePair: () => acquiredPair,
    pendingBeforeStart,
    actionStarted: () => true,
    inspectPair: async rows => inspected.push(rows[0].source_tick),
    shouldStop: () => cancelled,
  });
  cancelled = true;
  resolvePair(pair(678));

  assert.equal(await acquisition, null);
  assert.deepEqual(inspected, []);
  assert.deepEqual(pendingBeforeStart.map(entry => entry.rows[0].source_tick), [671, 672]);
});

test('prepared source-pair guards run once in acquisition order before buffered inspection', async () => {
  const pair = tick => Array.from({length: 2}, (_, index) => ({source_tick: tick,
    phase: 7, running: 1, message: 'active', match: {frame: tick},
    peer: index === 0 ? 'alpha' : 'beta'}));
  const pendingBeforeStart = [];
  const record = {};
  const guardedTicks = [];
  const inspectedTicks = [];
  let started = false;
  const inspectPair = async rows => {
    if (!started) {
      retainPreStartActionPair(pendingBeforeStart, rows);
      return;
    }
    inspectedTicks.push(rows[0].source_tick);
  };
  const acquire = rows => acquireAndInspectActionPair({
    acquirePair: async () => rows,
    pendingBeforeStart,
    actionStarted: () => started,
    inspectPair,
    guardAcquiredPair: pairRows => retainAndGuardPreparedPair(record, pairRows, rawRows => {
      guardedTicks.push(rawRows[0].source_tick);
    }),
  });

  await acquire(pair(671));
  started = true;
  await acquire(pair(672));
  assert.deepEqual(guardedTicks, [671, 672]);
  assert.deepEqual(inspectedTicks, [671, 672]);
  assert.equal(record.last_inspected_pair.source_tick, 672);

  const failureRecord = {};
  const failurePending = [];
  const attemptedTicks = [];
  const inspectionsAfterRefusal = [];
  let failureStarted = false;
  const invalidThenValid = [pair(700), pair(701), pair(702)];
  const acquireUntilRefusal = async () => {
    for (const rows of invalidThenValid) {
      attemptedTicks.push(rows[0].source_tick);
      if (rows[0].source_tick === 701) failureStarted = true;
      await acquireAndInspectActionPair({
        acquirePair: async () => rows,
        pendingBeforeStart: failurePending,
        actionStarted: () => failureStarted,
        inspectPair: async pairRows => {
          if (!failureStarted) retainPreStartActionPair(failurePending, pairRows);
          else inspectionsAfterRefusal.push(pairRows[0].source_tick);
        },
        guardAcquiredPair: pairRows => retainAndGuardPreparedPair(
          failureRecord, pairRows, rawRows => {
            if (rawRows[0].source_tick === 701) throw Error('transient prep guard refusal');
          }),
      });
    }
  };
  await assert.rejects(acquireUntilRefusal, /transient prep guard refusal/);
  assert.deepEqual(attemptedTicks, [700, 701], 'the later valid pair must not be fetched after refusal');
  assert.equal(failureRecord.last_inspected_pair.source_tick, 701,
    'the offending actual pair must be retained before the guard refuses');
  assert.deepEqual(inspectionsAfterRefusal, [],
    'no buffered or current pair may be inspected after an acquisition guard refuses');
});

test('prepared KO running evidence remains reachable when the first acquisition rejects', async () => {
  const combatProbe = {};
  const preparation = {result: 'running', jabs: [], last_inspected_pair: null};
  const attempt = async () => {
    retainPreparedKoPreparation(combatProbe, preparation);
    await Promise.reject(Error('fresh baseline acquisition refused'));
  };
  await assert.rejects(attempt, /fresh baseline acquisition refused/);
  assert.strictEqual(combatProbe.prepared_ko_preparation, preparation);
  preparation.last_inspected_pair = {source_tick: 240, peers: [{source_tick: 240}, {source_tick: 240}]};
  assert.equal(combatProbe.prepared_ko_preparation.last_inspected_pair.source_tick, 240);
});

test('prepared KO gives the initial baseline acquisition a finite cap then starts 480 at the fresh baseline', () => {
  const previousBoundary = 100;
  const freshBaselineTick = 240;
  assert.equal(preparedKoSourceCursorLimit(previousBoundary), 580,
    'initial fresh-baseline acquisition remains bounded from the prior boundary');
  assert.equal(preparedKoSourceCursorLimit(freshBaselineTick), 720,
    'declared preparation budget begins at the actual fresh baseline');
  assert.throws(() => preparedKoSourceCursorLimit(Number.MAX_SAFE_INTEGER), /safe nonnegative/);
});

test('action observer preserves same-cursor WAIT-to-active transition, skips duplicates, and keeps 60-tick cap', async () => {
  const pair = (tick, message, p2Motion = 179) => Array.from({length: 2}, (_, index) => ({
    source_tick: tick, phase: 7, running: 1, message,
    match: {players: [{source_port: 0, motion: 44}, {source_port: 1, motion: p2Motion}]},
    peer: index === 0 ? 'alpha' : 'beta',
  }));
  const pendingBeforeStart = [];
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(671, 'wait')), true);
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(671, 'wait')), false);
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(671, 'active')), true);
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(671, 'active')), false);

  let lastRetainedTick = -1;
  let stopped = false;
  const observed = [];
  const inspectPair = async rows => {
    const tick = rows[0].source_tick;
    const disposition = actionObservationTickDisposition(tick, 671, lastRetainedTick);
    if (disposition === 'skip') return;
    if (disposition === 'stop') {
      stopped = true;
      return;
    }
    observed.push({source_tick: tick, message: rows[0].message});
    if (rows[0].message === 'active') lastRetainedTick = tick;
  };
  const acquire = rows => acquireAndInspectActionPair({
    acquirePair: async () => rows,
    pendingBeforeStart,
    actionStarted: () => true,
    inspectPair,
    shouldStop: () => stopped,
  });
  await acquire(pair(671, 'active')); // duplicate of the already-drained active row
  await acquire(pair(730, 'active')); // actionStartTick + 59 remains inside the window
  await acquire(pair(731, 'active')); // actionStartTick + 60 closes the window

  assert.deepEqual(observed, [
    {source_tick: 671, message: 'wait'},
    {source_tick: 671, message: 'active'},
    {source_tick: 730, message: 'active'},
  ]);
  assert.equal(stopped, true);
  assert.equal(actionObservationTickDisposition(670, 671, -1), 'skip');
  assert.equal(actionObservationTickDisposition(671, 671, -1), 'inspect');
  assert.equal(actionObservationTickDisposition(671, 671, 671), 'skip');
  assert.equal(actionObservationTickDisposition(730, 671, 671), 'inspect');
  assert.equal(actionObservationTickDisposition(731, 671, 730), 'stop');
});

test('pre-start pair retention preserves its 128-row bound and exact-duplicate filter', () => {
  const pair = tick => Array.from({length: 2}, (_, index) => ({source_tick: tick,
    phase: 7, running: 1, message: 'active', match: {frame: tick},
    peer: index === 0 ? 'alpha' : 'beta'}));
  const pendingBeforeStart = [];
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(10)), true);
  assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(10)), false);
  for (let tick = 11; tick <= 138; tick++)
    assert.equal(retainPreStartActionPair(pendingBeforeStart, pair(tick)), true);
  assert.equal(pendingBeforeStart.length, 128);
  assert.equal(pendingBeforeStart[0].rows[0].source_tick, 11);
  assert.equal(pendingBeforeStart.at(-1).rows[0].source_tick, 138);
});

test('shield hold fulfillment is retained after the action fails without fabricating a valid hold', async () => {
  const shield = {hold: null};
  const holdState = {settled: false, outcome: null};
  let resolveHold;
  const holdOutcome = retainShieldHoldOutcome(new Promise(resolve => { resolveHold = resolve; }),
    shield, holdState);
  const actionError = await Promise.reject(Error('paired defender response not observed'))
    .catch(error => error);
  assert.match(actionError.message, /paired defender response/);
  const holdValue = {status: 'source-budget-exhausted', reason: '30-source-tick-bound',
    first_r_input_row: {input_tick: 657, local_port: 1},
    last_native_observation: {source_cursor: 701}};
  resolveHold(holdValue);
  const outcome = await holdOutcome;

  assert.equal(holdState.settled, true);
  assert.deepEqual(outcome, {value: holdValue});
  assert.deepEqual(shield.hold_outcome,
    {promise_status: 'fulfilled', value: holdValue});
  assert.equal(shield.hold, null,
    'a fulfilled hold promise with a refusal result is not a validated GuardSetOff observation');
});

test('shield hold promise rejection is retained as failure evidence', async () => {
  const shield = {hold: null};
  const holdState = {settled: false, outcome: null};
  const failure = Error('shield predicate timed out');
  const outcome = await retainShieldHoldOutcome(Promise.reject(failure), shield, holdState);

  assert.equal(holdState.settled, true);
  assert.equal(outcome.error, failure);
  assert.equal(shield.hold_outcome.promise_status, 'rejected');
  assert.match(shield.hold_outcome.error, /shield predicate timed out/);
  assert.equal(shield.hold, null);
});

// The retained native WAIT fixture has three stocks. Adapt only stocks to the
// prepared four-stock boundary; this is a classifier control, not new gameplay.
function preparedRenderWaitPair() {
  const fixture = JSON.parse(readFileSync(new URL(
    './fixtures/a3_first_use_render_wait_observation.json', import.meta.url), 'utf8'));
  const wait = structuredClone(fixture.beta);
  wait.match.players[0].stocks = 4;
  return [structuredClone(wait), structuredClone(wait)];
}

test('actual preparation guard preserves authored WAIT then active at the same cursor', () => {
  const rows = preparedRenderWaitPair();
  const record = {};
  const guard = pair => retainAndGuardPreparedPair(record, pair,
    actual => classifyPreparedCombatPair(actual, 'prepared WAIT control'));
  const waiting = guard(rows);
  assert.deepEqual(waiting.map(match => match.players.map(player => player.stocks)),
    [[4, 4], [4, 4]]);
  assert.equal(record.last_inspected_pair.peers[0].running, 0);
  const active = structuredClone(rows);
  for (const row of active) {
    row.running = 1;
    row.message = 'Original Mario vs Mario match on Final Destination';
  }
  assert.equal(active[0].source_tick, rows[0].source_tick);
  assert.equal(guard(active)[0].ready, true);
  assert.equal(record.last_inspected_pair.peers[0].running, 1);
});

test('actual preparation guard refuses unexpected stopped and counterfeit WAIT rows', () => {
  for (const mutate of [
    row => { row.message = 'Paused after a timing disruption in the audio clock. Resume to continue.'; },
    row => { row.native_status.local_capture_failure = 'capture failed'; },
    row => { row.match.players[0].stocks = 3; },
  ]) {
    const pair = preparedRenderWaitPair();
    mutate(pair[0]);
    const record = {};
    assert.throws(() => retainAndGuardPreparedPair(record, pair,
      rows => classifyPreparedCombatPair(rows, 'prepared negative control')));
    assert.deepEqual(record.last_inspected_pair.peers[0].match, pair[0].match,
      'offending actual match remains retained before refusal');
  }
});

test('platform gate scheduling reads checked match.frame from retained raw RebirthWait shape', () => {
  // Exact cursor/frame/motion projection of prepared-3 paired cursor1192.
  // Raw native observations have no top-level source_frame.
  const rows = [0, 1].map(() => ({source_tick:1192, phase:7, running:1,
    match:{frame:751, players:[{source_port:0,motion:13,groundAir:1,
      stocks:3,x:16,y:45,facing_direction:-1}]}}));
  const loss = {source_tick:1072,source_frame:631};
  const matches = rows.map(row => row.match);
  assert.equal(Object.hasOwn(rows[0], 'source_frame'), false);
  assert.equal(isCombatKoPlatformExitGateCandidate(rows, matches, loss), true);
  assert.equal(isCombatKoPlatformExitGateCandidate(rows, matches, null), false);
  const mismatched = structuredClone(matches); mismatched[1].frame++;
  assert.equal(isCombatKoPlatformExitGateCandidate(rows, mismatched, loss), false);
  const beforeLoss = {...loss,source_frame:751};
  assert.equal(isCombatKoPlatformExitGateCandidate(rows, matches, beforeLoss), false);
});

test('charged KO pending inspection starts after planned release fields and retains early failure action', () => {
  const ko = {};
  const press = {source_input_tick:100,observed_pad_sample:{input_tick:100},
    hold_observation:{action_start_source_tick:103,hold_end_source_tick:127,
      desired_release_source_cursor:127,observed_release_request_source_cursor:127}};
  const failure = Error('pending source pair refused');
  assert.throws(() => initializeCombatKoAction(ko,press,2,true,started => {
    assert.equal(started,103);
    assert.equal(ko.hold_end_source_tick,127);
    assert.equal(ko.planned_release_consumed_source_tick,130);
    assert.equal(ko.press,press);
    assert.equal(ko.action_start_tick,103);
    throw failure;
  }), error => error === failure);
  assert.equal(ko.press.observed_pad_sample.input_tick,100);
  const neutral = {};
  assert.equal(initializeCombatKoAction(neutral,press,2,false,started => {
    assert.equal(started,103);
    assert.equal(neutral.planned_release_consumed_source_tick,undefined);
  }),103);
  const invalid = {};
  assert.throws(() => initializeCombatKoAction(invalid,
    {...press,hold_observation:null},2,true,() => assert.fail('must not inspect invalid charge')),
  /hold observer/);
  assert.equal(invalid.press.source_input_tick,100);
});
