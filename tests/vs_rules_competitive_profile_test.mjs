import assert from 'node:assert/strict';
import test from 'node:test';
import {
  ITEM_ROW_TO_PREFERENCE_BIT,
  SOURCE_ITEM_IDS_BY_PREFERENCE_BIT,
  competitiveMatchStartFailures,
  competitiveProfileFailures,
  deriveAllOffItemMasks,
  deriveNormalizedAllOffItemMask,
  sourceItemClearMask,
} from './vs_rules_competitive_profile_helpers.mjs';

const rows = () => ITEM_ROW_TO_PREFERENCE_BIT.map((preference_bit, cursor) => ({
  cursor, preference_bit, enabled: 0,
}));

test('the 31 source Items rows cover their pinned preference bits without guessing an all-zero mask', () => {
  assert.equal(ITEM_ROW_TO_PREFERENCE_BIT.length, 31);
  assert.equal(new Set(ITEM_ROW_TO_PREFERENCE_BIT).size, 31);
  assert.deepEqual([...ITEM_ROW_TO_PREFERENCE_BIT].sort((a, b) => a - b),
    Array.from({length: 32}, (_, bit) => bit).filter(bit => bit !== 0x1c));

  const masks = deriveAllOffItemMasks({
    initialPreferenceMaskHex: 'ffffffffffffffff',
    rows: rows(),
  });
  assert.equal(masks.clearedPreferenceMaskHex, '00000000efffffff');
  assert.equal(masks.preferenceMaskHex, '0000000010000000');
});

test('original PPC setter sign-widens the complemented 32-bit mask, including bit 31', () => {
  assert.equal(sourceItemClearMask(30), 0xffffffffbfffffffn);
  assert.equal(sourceItemClearMask(31), 0x000000007fffffffn);
  const initial = 0x12345678ffffffffn;
  assert.equal(initial & sourceItemClearMask(30), 0x12345678bfffffffn);
  assert.equal(initial & sourceItemClearMask(31), 0x000000007fffffffn);
  for (const bit of [-1, 32, 1.5, '31'])
    assert.throws(() => sourceItemClearMask(bit), /integer in 0..31/);
});

test('all-off source commits preserve only the initial unmapped bit 28 and clear the high word', () => {
  for (const [initialPreferenceMaskHex, expected] of [
    ['12345678ffffffff', '0000000010000000'],
    ['ffffffffefffffff', '0000000000000000'],
    ['ffffffff10000000', '0000000010000000'],
    ['abcdef0100000000', '0000000000000000'],
  ]) {
    assert.equal(deriveAllOffItemMasks({initialPreferenceMaskHex, rows: rows()}).preferenceMaskHex,
      expected);
  }
});

test('all-off mask derivation rejects incomplete, reordered, duplicated, or still-enabled source rows', () => {
  const input = {
    initialPreferenceMaskHex: 'ffffffffffffffff',
  };
  assert.throws(() => deriveAllOffItemMasks({...input, rows: rows().slice(1)}), /expected 31 observed item rows/);

  const reordered = rows();
  reordered[1].cursor = 0;
  assert.throws(() => deriveAllOffItemMasks({...input, rows: reordered}), /missing or out of source order/);

  const duplicated = rows();
  duplicated[1].preference_bit = duplicated[0].preference_bit;
  assert.throws(() => deriveAllOffItemMasks({...input, rows: duplicated}), /pinned source preference mapping/);

  const enabled = rows();
  enabled[30].enabled = 1;
  assert.throws(() => deriveAllOffItemMasks({...input, rows: enabled}), /row 30 remains enabled/);
});

test('normalized all-off item mask follows original full-width defaults and authored conversion table', () => {
  assert.deepEqual(SOURCE_ITEM_IDS_BY_PREFERENCE_BIT, [
    0x06, 0x0b, 0x07, 0x19, 0x14, 0x12, 0x11, 0x0e,
    0x1c, 0x18, 0x08, 0x1a, 0x04, 0x10, 0x17, 0x22,
    0x20, 0x13, 0x09, 0x21, 0x0d, 0x0f, 0x1f, 0x16,
    0x15, 0x1e, 0x0a, 0x0c, 0x23, 0x05, 0x1d, 0x1b,
  ]);
  assert.equal(deriveNormalizedAllOffItemMask({
    initialRulesMaskHex: 'ffffffffffffffff', preferenceMaskHex: '0000000010000000',
  }), 'fffffff80000000f');
  assert.equal(deriveNormalizedAllOffItemMask({
    initialRulesMaskHex: '0000000000000000', preferenceMaskHex: '0000000010000000',
  }), '0000000000000000', 'Unmapped bits retain their actual initialized value');
  assert.notEqual(deriveNormalizedAllOffItemMask({
    initialRulesMaskHex: 'ffffffffffffffff', preferenceMaskHex: '0000000010000010',
  }), 'fffffff80000000f', 'An enabled source preference restores its authored destination bit');
});

test('competitive profile predicates use GameRules and validate raw CSS provenance without normalized assumptions', () => {
  const source = {
    valid: true,
    scene: 1,
    css_setup: {valid: true},
    rules: {
      mode: 1, stock_count: 4, stock_time_limit: 8, handicap: 0,
      damage_ratio: 10, friendly_fire: 1, pause: 0,
    },
    items: {frequency: -1, mask_hex: '0000000010000000'},
  };
  const rawCssSelection = {
    valid: true, scene: 1, provenance: 'raw_css_vs_start',
    match_kind: 0, timer_enabled: 0, time_limit_seconds: 0,
    disable_pausing: 0, damage_ratio_bits: '00000000', player_stocks: [0, 0],
  };
  const expectedPreferenceMaskHex = '0000000010000000';
  assert.deepEqual(competitiveProfileFailures({source, rawCssSelection, expectedPreferenceMaskHex}), []);

  const changed = structuredClone(source);
  changed.rules.pause = 1;
  const failures = competitiveProfileFailures({source: changed, rawCssSelection, expectedPreferenceMaskHex});
  assert.equal(failures.length, 1);
  assert.match(failures[0], /^pause setting: expected 0, got 1$/);
});

const normalizedCompetitiveMatch = () => ({
  ready: true,
  paused: false,
  ending: false,
  complete: false,
  frame: 180,
  rules: {
    match_kind: 1, stage: 0x20, timer_enabled: 1, time_limit: 480,
    disable_pausing: 1, friendly_fire: 1, damage_ratio_bits: '3f800000', item_frequency: -1,
    item_mask_hex: 'fffffff80000000f', is_teams: 0,
    player_teams: [0, 0], player_stocks: [4, 4], player_source_slots: [0, 0],
    resolved_controller_ports: [0, 1],
    player_slot_types: [0, 0], player_source_stocks: [4, 4],
    player_source_characters: [8, 8],
    player_attack_ratio_bits: ['3f800000', '3f800000'],
    player_defense_ratio_bits: ['3f800000', '3f800000'],
  },
  players: [0, 1].map(index => ({fighter: 0, source_slot: 0, source_port: index,
    source_character: 8, slot_type: 0, human: true, stocks: 4, source_stocks: 4})),
});

test('normalized match oracle binds the source 8-minute conversion and binary32 ratio', () => {
  const match = normalizedCompetitiveMatch();
  const sourcePreferenceMaskHex = '0000000010000000';
  assert.deepEqual(competitiveMatchStartFailures(match, {sourcePreferenceMaskHex}), []);

  const wrongTimer = structuredClone(match);
  wrongTimer.rules.time_limit = 8;
  assert(competitiveMatchStartFailures(wrongTimer, {sourcePreferenceMaskHex}).some(message =>
    message.startsWith('8-minute stock timer seconds from original conversion:')));

  const wrongFloat = structuredClone(match);
  wrongFloat.rules.damage_ratio_bits = '3f7fffff';
  assert(competitiveMatchStartFailures(wrongFloat, {sourcePreferenceMaskHex}).some(message =>
    message.startsWith('original 1.0 damage ratio binary32:')));

  const wrongItemMask = structuredClone(match);
  wrongItemMask.rules.item_mask_hex = '0000000000000000';
  assert(competitiveMatchStartFailures(wrongItemMask, {sourcePreferenceMaskHex}).some(message =>
    message.startsWith('normalized item mask derived from source defaults and preference table:')));

  const outsidePrefix = structuredClone(match);
  outsidePrefix.frame = 241;
  assert(competitiveMatchStartFailures(outsidePrefix, {sourcePreferenceMaskHex}).some(message =>
    message.startsWith('source-frame observation is within the bounded 180–240-frame prefix:')));
});

test('normalized match oracle rejects wrong rules, phase state, non-human ports, and incomplete ownership', () => {
  const wrongStage = structuredClone(normalizedCompetitiveMatch());
  wrongStage.rules.stage = 0;
  assert(competitiveMatchStartFailures(wrongStage, {sourcePreferenceMaskHex: '0000000010000000'}).some(message =>
    message.startsWith('source-selected Final Destination:')));

  const wrongPort = structuredClone(normalizedCompetitiveMatch());
  wrongPort.players[1].source_port = 0;
  assert(competitiveMatchStartFailures(wrongPort, {sourcePreferenceMaskHex: '0000000010000000'}).some(message =>
    message.startsWith('P2 resolved controller port:')));

  const wrongFriendlyFire = structuredClone(normalizedCompetitiveMatch());
  wrongFriendlyFire.rules.friendly_fire = 0;
  assert(competitiveMatchStartFailures(wrongFriendlyFire, {sourcePreferenceMaskHex: '0000000010000000'}).some(message =>
    message.startsWith('normalized friendly fire is enabled:')));

  for (const [field, expectedMessage] of [
    ['paused', 'active match is not paused:'],
    ['ending', 'active match is not ending:'],
    ['complete', 'active match is not complete:'],
    ['observer_error', 'no match observer error:'],
  ]) {
    const invalidState = structuredClone(normalizedCompetitiveMatch());
    invalidState[field] = true;
    assert(competitiveMatchStartFailures(invalidState,
      {sourcePreferenceMaskHex: '0000000010000000'}).some(message => message.startsWith(expectedMessage)));
  }

  const wrongOwner = structuredClone(normalizedCompetitiveMatch());
  wrongOwner.rules.player_slot_types[1] = 1;
  wrongOwner.players[1].human = false;
  const failures = competitiveMatchStartFailures(wrongOwner, {sourcePreferenceMaskHex: '0000000010000000'});
  assert(failures.some(message => message.startsWith('source player kinds remain Human[1]:')));
  assert(failures.some(message => message.startsWith('P2 live owner is Human:')));

  const missingPlayer = structuredClone(normalizedCompetitiveMatch());
  missingPlayer.players.pop();
  assert(competitiveMatchStartFailures(missingPlayer, {sourcePreferenceMaskHex: '0000000010000000'}).some(message =>
    message.startsWith('live players: expected two entries, got ')));
});
