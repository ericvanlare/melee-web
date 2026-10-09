import assert from 'node:assert/strict';
import test from 'node:test';
import {
  ITEM_ROW_TO_PREFERENCE_BIT,
  competitiveProfileFailures,
  deriveAllOffItemMasks,
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
