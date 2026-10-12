import assert from 'node:assert/strict';
import test from 'node:test';
import {observeDesiredSourcePad, observeCombatKoChargedHoldBeforeRelease}
  from '../scripts/a3_local_room_full_route.mjs';
import {COMBAT_KO_SOURCE_TICK_BUDGET, COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS,
  COMBAT_KO_CHARGED_PAD_BYTES,
  COMBAT_KO_PREP_MAX_JABS, COMBAT_KO_PREP_DAMAGE_GOAL, decideCombatKoPreparation,
  captureCombatKoBaseline, createCombatKoRespawnHandoff, advanceCombatKoEvidence,
  validateCombatKoAttackLoss, validateCombatKoChargedAnimation}
  from '../scripts/a3_local_room_combat_ko.mjs';

const SLOT_BY_PORT = [4, 1];
const BASE_COUNTERS = [
  {primary_falls: 2, suicides: 1, kos_by_player_slot: [0, 1, 0, 0, 3, 2]},
  {primary_falls: 7, suicides: 2, kos_by_player_slot: [4, 1, 0, 3, 8, 5]},
];

test('prepared KO selects only observed damage within the unchanged aggregate source and jab bounds', () => {
  assert.deepEqual(decideCombatKoPreparation({sourceTick: 110, sourceStartTick: 100,
    sourceCursorLimit: 100 + COMBAT_KO_SOURCE_TICK_BUDGET, jabCount: 0,
    actualVictimDamage: 17.999}), {decision: 'jab', next_jab: 1,
    actual_victim_damage_percent: 17.999});
  assert.deepEqual(decideCombatKoPreparation({sourceTick: 580, sourceStartTick: 100,
    sourceCursorLimit: 580, jabCount: 4, actualVictimDamage: COMBAT_KO_PREP_DAMAGE_GOAL}),
  {decision: 'ready', jab_count: 4, actual_victim_damage_percent: 18});
  assert.deepEqual(decideCombatKoPreparation({sourceTick: 580, sourceStartTick: 100,
    sourceCursorLimit: 580, jabCount: COMBAT_KO_PREP_MAX_JABS, actualVictimDamage: 17}),
  {decision: 'stop', reason: 'eight-jab-damage-goal-unmet', jab_count: 8,
    actual_victim_damage_percent: 17});
  assert.throws(() => decideCombatKoPreparation({sourceTick: 581, sourceStartTick: 100,
    sourceCursorLimit: 580, jabCount: 4, actualVictimDamage: 18}), /aggregate 480/);
  assert.throws(() => decideCombatKoPreparation({sourceTick: 100, sourceStartTick: 100,
    sourceCursorLimit: 580, jabCount: 4, actualVictimDamage: Number.NaN}), /invalid jab count or actual damage/);
});

function clone(value) {
  return structuredClone(value);
}

function pair({sourceTick = 100, frame = 200, stocks = [4, 4],
  counters = BASE_COUNTERS, mapping = SLOT_BY_PORT, motion = [14, 14],
  groundAir = [0, 0], second = null} = {}) {
  const makePeer = (peerIndex, override = {}) => {
    const row = {
      source_tick: sourceTick,
      error: null,
      match: {
        frame,
        rules: {is_stock: 1, stage: 0x20},
        players: [0, 1].map(port => ({
          source_port: port,
          // Deliberately conflicting decoys: counter indexing must use
          // source_player_index, never source_slot/controller/array position.
          source_slot: port === 0 ? 0 : 2,
          controller: port === 0 ? 1 : 0,
          source_player_index: mapping[port],
          stocks: stocks[port],
          damage_percent: 0,
          primary_falls: counters[port].primary_falls,
          suicides: counters[port].suicides,
          kos_by_player_slot: [...counters[port].kos_by_player_slot],
          motion: motion[port],
          groundAir: groundAir[port],
        })),
      },
    };
    if (override.players) {
      for (const [index, fields] of Object.entries(override.players))
        Object.assign(row.match.players[Number(index)], fields);
    }
    if (override.row) Object.assign(row, override.row);
    return row;
  };
  return [makePeer(0), makePeer(1, second ?? {})];
}

function lossPair() {
  const counters = clone(BASE_COUNTERS);
  counters[0].primary_falls++;
  counters[1].kos_by_player_slot[SLOT_BY_PORT[0]]++;
  return pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters,
    motion: [0, 44], groundAir: [1, 0]});
}

function respawnPair({sourceTick = 160, frame = 260, ...overrides} = {}) {
  const counters = clone(BASE_COUNTERS);
  counters[0].primary_falls++;
  counters[1].kos_by_player_slot[SLOT_BY_PORT[0]]++;
  return pair({sourceTick, frame, stocks: [3, 4], counters,
    motion: [14, 44], groundAir: [0, 0], ...overrides});
}

function padRow(inputTick, localPort, bytes = Array(11).fill(0)) {
  return {input_tick: inputTick, source_cursor: inputTick,
    local_port: localPort, bytes: [...bytes]};
}

function cstickLeftBytes(magnitude = -80) {
  const bytes = Array(11).fill(0);
  bytes[4] = magnitude & 255;
  return bytes;
}
function chargedALeftBytes() { return [...COMBAT_KO_CHARGED_PAD_BYTES]; }

function inputRows({start = 96, end = 156, press = 105, release = 108,
  p1Mutate = null, p2Mutate = null} = {}) {
  const p1 = [], p2 = [];
  for (let tick = start; tick <= end; ++tick) {
    p1.push(padRow(tick, 0));
    p2.push(padRow(tick, 1, tick >= press && tick < release
      ? cstickLeftBytes() : Array(11).fill(0)));
  }
  if (p1Mutate) p1Mutate(p1);
  if (p2Mutate) p2Mutate(p2);
  return {p1Rows: p1, p2Rows: p2};
}

function chargedInputRows({start = 96, end = 156, press = 105, release = 132,
  p1Mutate = null, p2Mutate = null} = {}) {
  const p1 = [], p2 = [];
  for (let tick = start; tick <= end; ++tick) {
    p1.push(padRow(tick, 0));
    p2.push({input_tick: tick, source_cursor: tick, local_port: 1,
      bytes: tick >= press && tick < release ? chargedALeftBytes() : Array(11).fill(0)});
  }
  if (p1Mutate) p1Mutate(p1);
  if (p2Mutate) p2Mutate(p2);
  return {p1Rows: p1, p2Rows: p2};
}

function chargedAnimationPairs({start = 108, end = 138, plateauFrame = 5,
  resumedFrame = 6, resumeAt = 135, omitTick = null, motion = 60, facing = -1,
  varyWhileHeld = false} = {}) {
  const pairs = [];
  for (let tick = start; tick <= end; ++tick) {
    if (tick === omitTick) continue;
    const rows = combatRows({sourceTick: tick, frame: tick + 100, motion: [14, motion]});
    for (const row of rows) for (const player of row.match.players) {
      player.facing_direction = player.source_port === 1 ? facing : 1;
      if (player.source_port === 1)
        player.animation_frame = tick >= resumeAt ? resumedFrame :
          plateauFrame + (varyWhileHeld ? tick - start : 0);
    }
    pairs.push(rows);
  }
  return pairs;
}

function validateDefaultHandoff(options = {}) {
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  const attackLossReceipt = validateCombatKoAttackLoss({baseline, pairedRows: lossPair()});
  const inputs = options.input ?? inputRows();
  return createCombatKoRespawnHandoff({baseline, attackLossReceipt,
    pairedRows: options.finalRows ?? respawnPair(), ...inputs,
    inputStartTick: options.inputStartTick ?? 96,
    inputEndTick: options.inputEndTick ?? 156,
    pressTick: options.pressTick ?? 105,
    releaseTick: options.releaseTick ?? 108});
}

test('KO evidence binds counters to sparse actual source_player_index slots', () => {
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  assert.deepEqual(baseline.source_player_index_by_port, [4, 1]);
  const receipt = validateCombatKoAttackLoss({baseline, pairedRows: lossPair()});
  assert.equal(receipt.victim_source_player_index, 4);
  assert.equal(receipt.source_tick, 130);
  assert.equal(receipt.actual_rows.length, 2);
  assert.equal(receipt.actual_matches.length, 2);
});

test('KO handoff requires first 4/4 baseline, exact 3/4 credit, and grounded neutral respawn', () => {
  const handoff = validateDefaultHandoff();
  assert.equal(handoff.starting_stocks, 3);
  assert.equal(handoff.validated_loss_receipt.kind,
    'validated-source-credited-combat-ko-loss-v1');
  assert.equal(handoff.respawn_source_tick, 160);
  assert.equal(handoff.respawn_source_frame, 260);
  assert.equal(handoff.input_window.start_input_tick, 96,
    'input tick is explicitly independent from the native source cursor');
  assert.equal(handoff.input_window.through_respawn_input_tick, 156);
  assert.equal(handoff.input_window.p1_neutral_row_count, 61);
  assert.equal(handoff.input_window.p2_cstick_left_held_row_count, 3);
  assert.equal(handoff.actual_rows.length, 2);
  assert.equal(handoff.actual_matches.length, 2);
});

test('baseline rejects malformed vectors, duplicate slots, invalid source indexes, and non-grounded starts', () => {
  const missingVector = pair();
  delete missingVector[0].match.players[0].kos_by_player_slot;
  assert.throws(() => captureCombatKoBaseline({pairedRows: missingVector}), /six-entry/);

  const shortVector = pair();
  shortVector[0].match.players[0].kos_by_player_slot.pop();
  assert.throws(() => captureCombatKoBaseline({pairedRows: shortVector}), /six-entry/);

  const sparseVector = pair();
  delete sparseVector[0].match.players[0].kos_by_player_slot[2];
  assert.throws(() => captureCombatKoBaseline({pairedRows: sparseVector}), /safe integer/);

  const unsafeCounter = pair();
  unsafeCounter[0].match.players[0].primary_falls = Number.MAX_SAFE_INTEGER + 1;
  assert.throws(() => captureCombatKoBaseline({pairedRows: unsafeCounter}), /safe integer/);

  const duplicateSlots = pair({mapping: [4, 4]});
  assert.throws(() => captureCombatKoBaseline({pairedRows: duplicateSlots}), /not distinct/);

  const invalidSlot = pair({mapping: [6, 1]});
  assert.throws(() => captureCombatKoBaseline({pairedRows: invalidSlot}), /invalid source_player_index/);

  const airborne = pair({groundAir: [1, 0]});
  assert.throws(() => captureCombatKoBaseline({pairedRows: airborne}), /P1 is not grounded/);
});

test('paired loss rejects mapping drift, peer disagreement, and wrong source ordering', () => {
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  const changedMapping = lossPair();
  changedMapping.forEach(row => { row.match.players[0].source_player_index = 0; });
  assert.throws(() => validateCombatKoAttackLoss({baseline, pairedRows: changedMapping}),
    /changed actual source_player_index mapping/);

  const peerMappingMismatch = lossPair();
  peerMappingMismatch[1].match.players[1].source_player_index = 3;
  assert.throws(() => validateCombatKoAttackLoss({baseline, pairedRows: peerMappingMismatch}),
    /disagree on actual source_player_index mapping/);

  const peerCursorMismatch = lossPair();
  peerCursorMismatch[1].source_tick++;
  assert.throws(() => validateCombatKoAttackLoss({baseline, pairedRows: peerCursorMismatch}),
    /do not share one source_tick/);

  const peerFrameMismatch = lossPair();
  peerFrameMismatch[1].match.frame++;
  assert.throws(() => validateCombatKoAttackLoss({baseline, pairedRows: peerFrameMismatch}),
    /disagree on the source frame/);

  const counterPeerMismatch = lossPair();
  counterPeerMismatch[1].match.players[1].kos_by_player_slot[4]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline, pairedRows: counterPeerMismatch}),
    /disagree on P2 persistent counters/);

  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: baseline.source_tick, frame: 230, stocks: [3, 4]})}),
  /source_tick did not move forward/);
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: baseline.source_frame, stocks: [3, 4]})}),
  /source frame did not move forward/);
});

test('exact counter controls reject wrong-slot credit, suicide, extra KO, and extra falls', () => {
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  const wrongSlot = clone(BASE_COUNTERS);
  wrongSlot[0].primary_falls++;
  wrongSlot[1].kos_by_player_slot[1]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters: wrongSlot})}),
  /KO counter for source slot 1 has an unexpected delta/);

  const suicide = clone(BASE_COUNTERS);
  suicide[0].primary_falls++;
  suicide[0].suicides++;
  suicide[1].kos_by_player_slot[4]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters: suicide})}),
  /P1 suicides changed/);

  const extraKO = clone(BASE_COUNTERS);
  extraKO[0].primary_falls++;
  extraKO[1].kos_by_player_slot[4]++;
  extraKO[1].kos_by_player_slot[5]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters: extraKO})}),
  /KO counter for source slot 5 has an unexpected delta/);

  const extraFall = clone(BASE_COUNTERS);
  extraFall[0].primary_falls += 2;
  extraFall[1].kos_by_player_slot[4]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters: extraFall})}),
  /P1 primary_falls delta is not exact/);

  const attackerFall = clone(BASE_COUNTERS);
  attackerFall[0].primary_falls++;
  attackerFall[1].primary_falls++;
  attackerFall[1].kos_by_player_slot[4]++;
  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [3, 4], counters: attackerFall})}),
  /P2 primary_falls delta is not exact/);

  assert.throws(() => validateCombatKoAttackLoss({baseline,
    pairedRows: pair({sourceTick: 130, frame: 230, stocks: [2, 4]})}),
  /stocks must be \[3,4\]/);
});

test('respawn handoff revalidates exact counters and requires this baseline loss receipt', () => {
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  const receipt = validateCombatKoAttackLoss({baseline, pairedRows: lossPair()});
  const otherBaseline = captureCombatKoBaseline({pairedRows: pair({sourceTick: 101, frame: 201})});
  assert.throws(() => createCombatKoRespawnHandoff({baseline: otherBaseline,
    attackLossReceipt: receipt, pairedRows: respawnPair(), ...inputRows(),
    inputStartTick: 96, inputEndTick: 156, pressTick: 105, releaseTick: 108}),
  /requires this baseline/);
  assert.throws(() => createCombatKoRespawnHandoff({baseline,
    attackLossReceipt: {}, pairedRows: respawnPair(), ...inputRows(),
    inputStartTick: 96, inputEndTick: 156, pressTick: 105, releaseTick: 108}),
  /requires this baseline/);

  const resetCounters = clone(BASE_COUNTERS);
  resetCounters[0].primary_falls++;
  resetCounters[1].kos_by_player_slot[4]++;
  resetCounters[1].kos_by_player_slot[0] = 0;
  assert.throws(() => createCombatKoRespawnHandoff({baseline, attackLossReceipt: receipt,
    pairedRows: respawnPair({counters: resetCounters}), ...inputRows(),
    inputStartTick: 96, inputEndTick: 156, pressTick: 105, releaseTick: 108}),
  /KO counter for source slot 0 has an unexpected delta/);

  const airborne = respawnPair({groundAir: [1, 0], motion: [14, 44]});
  assert.throws(() => createCombatKoRespawnHandoff({baseline, attackLossReceipt: receipt,
    pairedRows: airborne, ...inputRows(), inputStartTick: 96, inputEndTick: 156,
    pressTick: 105, releaseTick: 108}), /P1 is not grounded/);

  const wrongMotion = respawnPair({motion: [4, 44]});
  assert.throws(() => createCombatKoRespawnHandoff({baseline, attackLossReceipt: receipt,
    pairedRows: wrongMotion, ...inputRows(), inputStartTick: 96, inputEndTick: 156,
    pressTick: 105, releaseTick: 108}), /not in grounded Wait motion 14/);
});

test('literal input control enforces one C-stick-left rise, held copies, one release, and neutral P1 through respawn', () => {
  assert.doesNotThrow(() => validateDefaultHandoff());

  const wrongXAxis = inputRows({p2Mutate: rows => { rows[9].bytes[2] = 1; }});
  assert.throws(() => validateDefaultHandoff({input: wrongXAxis}), /contains another input/);

  const button = inputRows({p2Mutate: rows => { rows[9].bytes[1] = 1; }});
  assert.throws(() => validateDefaultHandoff({input: button}), /contains another input/);

  const trigger = inputRows({p2Mutate: rows => { rows[9].bytes[6] = 1; }});
  assert.throws(() => validateDefaultHandoff({input: trigger}), /contains another input/);

  const secondRise = inputRows({p2Mutate: rows => {
    rows[12].bytes = cstickLeftBytes();
  }});
  assert.throws(() => validateDefaultHandoff({input: secondRise}), /unexpected PAD input/);

  const heldAtRelease = inputRows({p2Mutate: rows => {
    rows[108 - 96].bytes = cstickLeftBytes();
  }});
  assert.throws(() => validateDefaultHandoff({input: heldAtRelease}), /unexpected PAD input/);

  const p1Drift = inputRows({p1Mutate: rows => { rows[40].bytes[3] = 1; }});
  assert.throws(() => validateDefaultHandoff({input: p1Drift}), /P1 was not literally neutral/);

  const wrongDirection = inputRows({p2Mutate: rows => { rows[9].bytes[4] = 80; }});
  assert.throws(() => validateDefaultHandoff({input: wrongDirection}), /contains another input/);

  const wrongPort = inputRows({p2Mutate: rows => { rows[9].local_port = 0; }});
  assert.throws(() => validateDefaultHandoff({input: wrongPort}), /changed local port/);

  const malformedBytes = inputRows({p2Mutate: rows => { rows[9].bytes.pop(); }});
  assert.throws(() => validateDefaultHandoff({input: malformedBytes}), /exact 11-byte/);

  const misaligned = inputRows({p2Mutate: rows => { rows[9].source_cursor++; }});
  assert.throws(() => validateDefaultHandoff({input: misaligned}), /not source-cursor aligned/);

  const gap = inputRows({p2Mutate: rows => { rows.splice(20, 1); }});
  assert.throws(() => validateDefaultHandoff({input: gap}), /missing or duplicate source tick/);

  const duplicate = inputRows({p1Mutate: rows => { rows.splice(20, 0, {...rows[19]}); }});
  assert.throws(() => validateDefaultHandoff({input: duplicate}), /missing or duplicate source tick/);
});

test('source tick budget accepts 480 and rejects 481 without inventing input delay mapping', () => {
  assert.equal(COMBAT_KO_SOURCE_TICK_BUDGET, 480);
  const baseline = captureCombatKoBaseline({pairedRows: pair()});
  const receipt = validateCombatKoAttackLoss({baseline, pairedRows: lossPair()});
  const inputs = inputRows({start: 96, end: 576, press: 105, release: 108});
  assert.doesNotThrow(() => createCombatKoRespawnHandoff({baseline,
    attackLossReceipt: receipt, pairedRows: respawnPair({sourceTick: 580, frame: 680}),
    ...inputs, inputStartTick: 96, inputEndTick: 576, pressTick: 105, releaseTick: 108}));
  assert.throws(() => createCombatKoRespawnHandoff({baseline,
    attackLossReceipt: receipt, pairedRows: respawnPair({sourceTick: 581, frame: 681}),
    ...inputs, inputStartTick: 96, inputEndTick: 576, pressTick: 105, releaseTick: 108}),
  /exceeded the 480-tick/);
});


// Explicit synthetic observation sequence; validates classifier mechanics,
// not a recorded browser hit or authored duration.
function combatRows(options = {}) {
  const rows = pair(options);
  for (const row of rows) for (const player of row.match.players) {
    player.damage_percent = player.source_port === 0 ? (options.damage ?? 3) : 0;
    player.facing_direction = player.source_port === 1 ? -1 : 1;
  }
  return rows;
}
function runKoSequence({airMotion = 29, omit = null, landed = false, cliff = null} = {}) {
  const baseline = captureCombatKoBaseline({pairedRows: combatRows()});
  const sequence = {};
  const samples = [
    ['attack', combatRows({sourceTick:110,frame:210,motion:[14,60]})],
    ['hit', combatRows({sourceTick:111,frame:211,motion:[79,14],damage:18})],
    ['air', combatRows({sourceTick:115,frame:215,motion:[airMotion,14],groundAir:[1,0],damage:18})],
  ];
  if (landed) samples.push(['land',combatRows({sourceTick:116,frame:216,damage:18})]);
  if (cliff !== null) samples.push(['cliff',combatRows({sourceTick:116,frame:216,
    motion:[cliff,14],groundAir:[1,0],damage:18})]);
  samples.push(['death',combatRows({sourceTick:120,frame:220,motion:[0,14],groundAir:[1,0],damage:18})]);
  const loss=lossPair();
  for(const row of loss) for(const player of row.match.players) {
    if(player.source_port===0) player.motion=12;
    player.damage_percent=player.source_port===0?18:0;player.facing_direction=player.source_port===1?-1:1;
  }
  samples.push(['loss',loss]);
  const respawn=respawnPair();
  for(const row of respawn) for(const player of row.match.players) {
    player.damage_percent=0;player.facing_direction=player.source_port===1?-1:1;
  }
  samples.push(['respawn',respawn]);
  for(const [name,pairedRows] of samples) if(name!==omit)
    advanceCombatKoEvidence({baseline,sequence,pairedRows,actionStartTick:108});
  return {baseline,sequence};
}

test('one-swing sequence accepts observed damage to ordinary Fall and source-attributed death', () => {
  for(const airMotion of [29,38,87]) {
    const {sequence}=runKoSequence({airMotion});
    assert.equal(sequence.attack,110);assert.equal(sequence.hit,111);
    assert.equal(sequence.air,115);assert.equal(sequence.death,120);
    assert.equal(sequence.loss.source_tick,130);assert.equal(sequence.respawn_rows[0].source_tick,160);
  }
});
test('one-swing sequence refuses missing attack hit air or death and both authored ledge states', () => {
  for(const omit of ['attack','hit','air','death'])
    assert.throws(()=>runKoSequence({omit}),/lacks observed attack\/hit\/air\/death/);
  for(const cliff of [252,253])assert.throws(()=>runKoSequence({cliff}),/grabbed the ledge/);
  assert.throws(()=>runKoSequence({landed:true}),/landed with four stocks/);
});
test('one-swing sequence refuses early counter mutation and source budget excess', () => {
  const baseline=captureCombatKoBaseline({pairedRows:combatRows()});
  const changed=combatRows({sourceTick:110,frame:210});
  changed.forEach(row=>row.match.players[1].kos_by_player_slot[4]++);
  assert.throws(()=>advanceCombatKoEvidence({baseline,sequence:{},pairedRows:changed,actionStartTick:108}),
    /counters changed/);
  assert.throws(()=>advanceCombatKoEvidence({baseline,sequence:{},
    pairedRows:combatRows({sourceTick:581,frame:681}),actionStartTick:108}),/480-source/);
});


test('KO baseline refuses missing and nonfinite native damage', () => {
  for (const damage of [undefined, NaN, Infinity]) {
    const rows = combatRows();
    for (const row of rows) row.match.players[0].damage_percent = damage;
    assert.throws(() => captureCombatKoBaseline({pairedRows: rows}), /damage is not finite/);
  }
});


test('actual desired-PAD predicate accepts only literal left C-stick and enforces source cap', () => {
  const saved = {document:globalThis.document, health:globalThis.meleeNetRuntimeLockstepHealth,
    net:globalThis.__net};
  const row = {input_tick:105, source_cursor:105, local_port:1,
    bytes:[0,0,0,0,129,0,0,0,0,0,0]};
  let cursor=109;
  globalThis.document={querySelector:()=>({dataset:{}})};
  globalThis.__net={status:()=>({cursor})};
  globalThis.meleeNetRuntimeLockstepHealth=()=>({peer:{localInputCapture:{captures_since:[row]}}});
  const options={inputFrom:104,port:1,expected:{cstickX:-1},maxSourceCursor:580};
  try {
    assert.equal(observeDesiredSourcePad(options),row);
    for (const byte of [0,1,2,3,5,6,7,8,9,10]) {
      row.bytes[byte]=1;
      assert.equal(observeDesiredSourcePad(options),false);
      row.bytes[byte]=0;
    }
    row.bytes[4]=127;assert.equal(observeDesiredSourcePad(options),false);row.bytes[4]=129;
    row.source_cursor=106;assert.equal(observeDesiredSourcePad(options),false);row.source_cursor=105;
    row.local_port=0;assert.equal(observeDesiredSourcePad(options),false);row.local_port=1;
    cursor=581;assert.throws(()=>observeDesiredSourcePad(options),/source-cursor budget/);
    cursor=109;
    globalThis.meleeNetRuntimeLockstepHealth=()=>({peer:{protocol:{terminal:{kind:2}}}});
    assert.throws(()=>observeDesiredSourcePad(options),/failure or terminal/);
  } finally {
    globalThis.document=saved.document;globalThis.__net=saved.net;
    globalThis.meleeNetRuntimeLockstepHealth=saved.health;
  }
});

test('charged A+left handoff requires the exact bounded episode and paired S4S60 animation evidence', () => {
  const baseline = captureCombatKoBaseline({pairedRows: combatRows()});
  const animation = validateCombatKoChargedAnimation({baseline,
    pairedRows: chargedAnimationPairs(), pressTick: 105, inputDelay: 2, releaseTick: 132});
  assert.equal(animation.kind, 'observed-s4s60-charge-animation-plateau-and-release-advance-v1');
  assert.equal(animation.action_start_source_tick, 108);
  assert.equal(animation.hold_end_source_tick, 132);
  assert.equal(animation.release_consumed_source_tick, 135);
  assert.equal(animation.plateau_source_tick, 109);
  assert.equal(animation.resumed_source_tick, 135);
  assert.equal(animation.retained_hold_source_cursor_count, COMBAT_KO_CHARGED_HOLD_SOURCE_TICKS);
  const rows = chargedInputRows();
  const handoff = createCombatKoRespawnHandoff({baseline,
    attackLossReceipt: validateCombatKoAttackLoss({baseline, pairedRows: lossPair()}),
    pairedRows: respawnPair(), ...rows, inputStartTick: 96, inputEndTick: 156,
    pressTick: 105, releaseTick: 132, inputMode: 'charged-a-left', inputDelay: 2,
    chargeAnimationEvidence: animation});
  assert.equal(handoff.input_window.kind, 'literal-a-left-charged-forward-smash-v1');
  assert.equal(handoff.input_window.p2_a_left_held_row_count, 27);
  assert.equal(handoff.charge_animation_evidence, animation);
});

test('charged recipe refuses partial input, extra edges, a 31-record span, missing plateau, and early strike', () => {
  const baseline = captureCombatKoBaseline({pairedRows: combatRows()});
  const animation = validateCombatKoChargedAnimation({baseline,
    pairedRows: chargedAnimationPairs(), pressTick: 105, inputDelay: 2, releaseTick: 132});
  const handoffOptions = {baseline,
    attackLossReceipt: validateCombatKoAttackLoss({baseline, pairedRows: lossPair()}),
    pairedRows: respawnPair(), inputStartTick: 96, inputEndTick: 156,
    pressTick: 105, releaseTick: 132, inputMode: 'charged-a-left', inputDelay: 2,
    chargeAnimationEvidence: animation};
  const assertInputRejected = input => assert.throws(() => createCombatKoRespawnHandoff({
    ...handoffOptions, ...input}), /charged A\+left row/);
  assertInputRejected(chargedInputRows({p2Mutate: rows => { rows[9].bytes[1] = 1; }}));
  assertInputRejected(chargedInputRows({p2Mutate: rows => { rows[9].bytes[2] = 0; }}));
  assertInputRejected(chargedInputRows({p2Mutate: rows => { rows[9].bytes[4] = 129; }}));
  const oneRecordSkewAnimation = validateCombatKoChargedAnimation({baseline,
    pairedRows: chargedAnimationPairs({resumeAt: 136}),
    pressTick: 105, inputDelay: 2, releaseTick: 133});
  const oneRecordSkew = createCombatKoRespawnHandoff({...handoffOptions,
    ...chargedInputRows({release: 133}), releaseTick: 133,
    chargeAnimationEvidence: oneRecordSkewAnimation});
  assert.equal(oneRecordSkew.input_window.p2_a_left_held_row_count, 28);
  assert.throws(() => createCombatKoRespawnHandoff({...handoffOptions,
    ...chargedInputRows({release: 133,
      p2Mutate: rows => { rows[36].bytes[2] = 0; }}), releaseTick: 133}),
  /charged A\+left row/);
  assert.throws(() => createCombatKoRespawnHandoff({...handoffOptions,
    ...chargedInputRows({p2Mutate: rows => { rows[37].bytes = chargedALeftBytes(); }})}),
  /unexpected PAD input/);
  assert.throws(() => createCombatKoRespawnHandoff({...handoffOptions,
    ...chargedInputRows({release: 136}), releaseTick: 136, inputDelay: 5}), /30-literal-record/);
  assert.throws(() => validateCombatKoChargedAnimation({baseline,
    pairedRows: chargedAnimationPairs({varyWhileHeld: true}),
    pressTick: 105, inputDelay: 2, releaseTick: 132}), /plateau while A was held/);
  const sequence = {};
  advanceCombatKoEvidence({baseline, sequence,
    pairedRows: combatRows({sourceTick: 110, frame: 210, motion: [14, 60]}),
    actionStartTick: 108, chargeReleaseAcceptedSourceTick: 135});
  assert.throws(() => advanceCombatKoEvidence({baseline, sequence,
    pairedRows: combatRows({sourceTick: 111, frame: 211, motion: [79, 14], damage: 18}),
    actionStartTick: 108, chargeReleaseAcceptedSourceTick: 135}),
  /before the charged release input reached the source/);
});

test('charged hold releases at first observed target crossing and refuses bounds or reordered PAD', () => {
  const saved = {document:globalThis.document, health:globalThis.meleeNetRuntimeLockstepHealth,
    net:globalThis.__net, Module:globalThis.Module};
  const expected = chargedALeftBytes();
  const captures = [];
  for (let tick = 100; tick <= 127; ++tick)
    captures.push({input_tick:tick,source_cursor:tick,local_port:1,
      bytes:tick >= 101 ? [...expected] : Array(11).fill(0)});
  let cursor = 127;
  const match = {players:[
    {source_port:0, stocks:4, damage_percent:3, motion:14, groundAir:0},
    {source_port:1, motion:60, facing_direction:-1, animation_frame:4},
  ]};
  globalThis.document={querySelector:()=>({dataset:{}})};
  globalThis.__net={status:()=>({cursor})};
  globalThis.meleeNetRuntimeLockstepHealth=()=>({peer:{localInputCapture:{captures_since:captures}}});
  globalThis.Module={_melee_web_native_menu_match_observe:()=>1,UTF8ToString:()=>JSON.stringify(match)};
  const options={inputFrom:100,port:1,expectedBytes:expected,inputDelay:2,holdSourceTicks:24,
    maxSourceCursor:500,baselineVictimDamage:3};
  try {
    assert.equal(observeCombatKoChargedHoldBeforeRelease(options),false);
    cursor=128;
    const released=observeCombatKoChargedHoldBeforeRelease(options);
    assert.equal(released.press_sample.input_tick,101);
    assert.equal(released.action_start_source_tick,104);
    assert.equal(released.hold_end_source_tick,128);
    assert.equal(released.observed_release_request_source_cursor,128);
    captures.push(...[128,129].map(tick => ({input_tick:tick,source_cursor:tick,local_port:1,bytes:[...expected]})));
    cursor=130;
    assert.equal(observeCombatKoChargedHoldBeforeRelease(options).observed_release_request_source_cursor,130);
    cursor=132;
    assert.throws(()=>observeCombatKoChargedHoldBeforeRelease(options),/30-literal-record/);
    cursor=128;
    assert.throws(()=>observeCombatKoChargedHoldBeforeRelease({...options,inputDelay:6}),
      /30-literal-record/);
    cursor=501;
    assert.throws(()=>observeCombatKoChargedHoldBeforeRelease(options),/480-source-cursor budget/);
    cursor=128;
    [captures[2],captures[3]]=[captures[3],captures[2]];
    assert.throws(()=>observeCombatKoChargedHoldBeforeRelease(options),/repeats or moves backward/);
    [captures[2],captures[3]]=[captures[3],captures[2]];
    captures[2].bytes[2]=0;
    assert.throws(()=>observeCombatKoChargedHoldBeforeRelease(options),/partial, or early-release/);
  } finally {
    globalThis.document=saved.document;globalThis.__net=saved.net;
    globalThis.meleeNetRuntimeLockstepHealth=saved.health;globalThis.Module=saved.Module;
  }
});


test('sampled charged animation preserves gaps and rejects acquired row reversal', () => {
  const baseline = captureCombatKoBaseline({pairedRows: combatRows()});
  const pairs = chargedAnimationPairs().filter(rows => [108,111,129,135,138].includes(rows[0].source_tick));
  const result = validateCombatKoChargedAnimation({baseline, pairedRows:pairs,
    pressTick:105,inputDelay:2,releaseTick:134});
  assert.equal(result.plateau_source_tick,111);
  assert.equal(result.release_consumed_source_tick,137);
  assert.equal(result.resumed_source_tick,138);
  assert(result.sampled_source_gaps.some(gap => gap.unobserved_cursor_count > 0));
  const handoff = createCombatKoRespawnHandoff({baseline,
    attackLossReceipt:validateCombatKoAttackLoss({baseline,pairedRows:lossPair()}),
    pairedRows:respawnPair(),...chargedInputRows({release:134}),
    inputStartTick:96,inputEndTick:156,pressTick:105,releaseTick:134,
    inputMode:'charged-a-left',inputDelay:2,chargeAnimationEvidence:result});
  assert.equal(handoff.input_window.neutral_release_input_tick,134);
  assert.equal(handoff.charge_animation_evidence.release_consumed_source_tick,137);
  assert.throws(() => validateCombatKoChargedAnimation({baseline,pairedRows:pairs.slice(0,-1),
    pressTick:105,inputDelay:2,releaseTick:134}), /did not advance after/);
  const hitRows = structuredClone(pairs);
  hitRows[1].forEach(row => {row.match.players.find(player => player.source_port===0).damage_percent=18;});
  assert.throws(() => validateCombatKoChargedAnimation({baseline,pairedRows:hitRows,
    pressTick:105,inputDelay:2,releaseTick:134}), /follows a victim hit/);
  assert.throws(() => validateCombatKoChargedAnimation({baseline,
    pairedRows:[pairs[1],pairs[0],...pairs.slice(2)],pressTick:105,inputDelay:2,releaseTick:134}),
    /repeat or move backward/);
  assert.throws(() => validateCombatKoChargedAnimation({baseline,pairedRows:pairs,
    pressTick:105,inputDelay:2,releaseTick:136}), /bounded literal episode/);
});
