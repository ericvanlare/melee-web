export const ITEM_ROW_TO_PREFERENCE_BIT = Object.freeze([
  0x05, 0x12, 0x0a, 0x1e, 0x0d, 0x18, 0x03, 0x0e,
  0x17, 0x1b, 0x01, 0x09, 0x08, 0x07, 0x15, 0x04,
  0x06, 0x02, 0x0f, 0x00, 0x11, 0x0b, 0x1f, 0x1a,
  0x14, 0x19, 0x10, 0x16, 0x13, 0x1d, 0x0c,
]);

// Original gm_80167BC8's preference-index -> StartMeleeRules::x20 bit table
// (gm_1601.static.h: lbl_803B7844). Index 28 is the 0x23 sentinel and is
// skipped by the source conversion loop.
export const SOURCE_ITEM_IDS_BY_PREFERENCE_BIT = Object.freeze([
  0x06, 0x0b, 0x07, 0x19, 0x14, 0x12, 0x11, 0x0e,
  0x1c, 0x18, 0x08, 0x1a, 0x04, 0x10, 0x17, 0x22,
  0x20, 0x13, 0x09, 0x21, 0x0d, 0x0f, 0x1f, 0x16,
  0x15, 0x1e, 0x0a, 0x0c, 0x23, 0x05, 0x1d, 0x1b,
]);

const UINT64_MASK = (1n << 64n) - 1n;

function parseMask(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-f]{16}$/i.test(value))
    throw new Error(`${label} must be a 16-digit hexadecimal mask`);
  return BigInt(`0x${value}`);
}

function formatMask(value) {
  return (value & UINT64_MASK).toString(16).padStart(16, '0');
}

// Original mn_8022E978: slw -> nor -> srawi 31 widens the signed
// 32-bit complement into the high word of GameRules.item_mask (u64).
// Clearing bit 31 therefore clears the high word; other rows retain it.
export function sourceItemClearMask(preferenceBit) {
  if (!Number.isInteger(preferenceBit) || preferenceBit < 0 || preferenceBit > 31)
    throw new Error('source item preference bit must be an integer in 0..31');
  return BigInt.asUintN(64, BigInt.asIntN(32, ~(1n << BigInt(preferenceBit))));
}

export function deriveAllOffItemMasks({initialPreferenceMaskHex, rows}) {
  if (!Array.isArray(rows) || rows.length !== ITEM_ROW_TO_PREFERENCE_BIT.length)
    throw new Error(`expected ${ITEM_ROW_TO_PREFERENCE_BIT.length} observed item rows`);

  let switchMask = 0n;
  let preferenceMask = parseMask(initialPreferenceMaskHex, 'initial preference mask');
  for (let index = 0; index < rows.length; index++) {
    const row = rows[index];
    if (row?.cursor !== index)
      throw new Error(`item row ${index} is missing or out of source order`);
    if (row.preference_bit !== ITEM_ROW_TO_PREFERENCE_BIT[index])
      throw new Error(`item row ${index} does not match the pinned source preference mapping`);
    if (row.enabled !== 0)
      throw new Error(`item row ${index} remains enabled`);
    const bit = 1n << BigInt(row.preference_bit);
    if (switchMask & bit)
      throw new Error(`item preference bit ${row.preference_bit} appears more than once`);
    switchMask |= bit;
    preferenceMask &= sourceItemClearMask(row.preference_bit);
  }

  return {
    preferenceMaskHex: formatMask(preferenceMask),
    clearedPreferenceMaskHex: formatMask(switchMask),
  };
}

export function deriveNormalizedAllOffItemMask({initialRulesMaskHex, preferenceMaskHex}) {
  const initialRulesMask = parseMask(initialRulesMaskHex, 'initial normalized item mask');
  const preferenceMask = parseMask(preferenceMaskHex, 'source item preference mask');
  if (SOURCE_ITEM_IDS_BY_PREFERENCE_BIT.length !== 32 ||
      new Set(SOURCE_ITEM_IDS_BY_PREFERENCE_BIT).size !== 32 ||
      SOURCE_ITEM_IDS_BY_PREFERENCE_BIT.some(item => !Number.isInteger(item) || item < 0 || item > 63))
    throw new Error('pinned source preference-to-item table is invalid');

  let normalizedMask = initialRulesMask;
  for (let preferenceBit = 0; preferenceBit < SOURCE_ITEM_IDS_BY_PREFERENCE_BIT.length; preferenceBit++) {
    const item = SOURCE_ITEM_IDS_BY_PREFERENCE_BIT[preferenceBit];
    if (item === 0x23) continue;
    const itemMask = 1n << BigInt(item);
    if (preferenceMask & (1n << BigInt(preferenceBit)))
      normalizedMask |= itemMask;
    else
      normalizedMask &= ~itemMask;
  }
  return formatMask(normalizedMask);
}

export function competitiveProfileFailures({source, rawCssSelection, expectedPreferenceMaskHex}) {
  const failures = [];
  const expect = (label, actual, expected) => {
    if (actual !== expected) failures.push(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  };
  expect('source scene', source?.scene, 1);
  expect('source validity', source?.valid, true);
  expect('CSS selection validity', source?.css_setup?.valid, true);
  expect('rules mode', source?.rules?.mode, 1);
  expect('stock count', source?.rules?.stock_count, 4);
  expect('stock timer minutes', source?.rules?.stock_time_limit, 8);
  expect('handicap', source?.rules?.handicap, 0);
  expect('damage ratio menu value', source?.rules?.damage_ratio, 10);
  expect('friendly fire menu value', source?.rules?.friendly_fire, 1);
  expect('pause setting', source?.rules?.pause, 0);
  expect('item frequency preference', source?.items?.frequency, -1);
  expect('all-off item preference mask', source?.items?.mask_hex, expectedPreferenceMaskHex);

  // CSS still owns raw VsModeData here. The original gm_80167BC8 conversion
  // happens later, when VS enters the match after SSS; these fields are retained
  // as provenance and are not a normalized profile oracle.
  expect('raw CSS selection validity', rawCssSelection?.valid, true);
  expect('raw CSS selection scene', rawCssSelection?.scene, 1);
  expect('raw CSS selection provenance', rawCssSelection?.provenance, 'raw_css_vs_start');
  return failures;
}

export function competitiveMatchStartFailures(match, {sourcePreferenceMaskHex} = {}) {
  const failures = [];
  const expect = (label, actual, expected) => {
    if (actual !== expected)
      failures.push(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  };
  const expectArray = (label, actual, expected) => {
    if (!Array.isArray(actual) || actual.length !== expected.length) {
      failures.push(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
      return;
    }
    for (let index = 0; index < expected.length; index++)
      expect(`${label}[${index}]`, actual[index], expected[index]);
  };

  expect('match ready', match?.ready, true);
  expect('no match observer error', match?.observer_error ?? false, false);
  expect('active match is not paused', match?.paused, false);
  expect('active match is not ending', match?.ending, false);
  expect('active match is not complete', match?.complete, false);
  expect('source-frame observation is within the bounded 180–240-frame prefix',
    Number.isInteger(match?.frame) && match.frame >= 180 && match.frame <= 240, true);
  expect('normalized stock match kind', match?.rules?.match_kind, 1);
  expect('source-selected Final Destination', match?.rules?.stage, 0x20);
  expect('normalized friendly fire is enabled', match?.rules?.friendly_fire, 1);
  expect('8-minute stock timer enabled by original conversion', match?.rules?.timer_enabled, 1);
  expect('8-minute stock timer seconds from original conversion', match?.rules?.time_limit, 480);
  expect('original Rules Plus pause-off conversion', match?.rules?.disable_pausing, 1);
  expect('original 1.0 damage ratio binary32', match?.rules?.damage_ratio_bits, '3f800000');
  expect('all-off normalized item frequency', match?.rules?.item_frequency, -1);
  const expectedNormalizedItemMask = sourcePreferenceMaskHex
    ? deriveNormalizedAllOffItemMask({
      initialRulesMaskHex: 'ffffffffffffffff',
      preferenceMaskHex: sourcePreferenceMaskHex,
    })
    : null;
  expect('normalized item mask derived from source defaults and preference table',
    match?.rules?.item_mask_hex, expectedNormalizedItemMask);
  expect('source CSS Teams setting', match?.rules?.is_teams, 0);
  expectArray('normalized source player teams', match?.rules?.player_teams, [0, 0]);
  expectArray('normalized source player stocks', match?.rules?.player_stocks, [4, 4]);
  expectArray('raw source player slot fields', match?.rules?.player_source_slots, [0, 0]);
  expectArray('resolved controller ports retain original player indices',
    match?.rules?.resolved_controller_ports, [0, 1]);
  expectArray('source player kinds remain Human', match?.rules?.player_slot_types, [0, 0]);
  expectArray('source normalized player stocks', match?.rules?.player_source_stocks, [4, 4]);
  expectArray('source normalized characters are Mario', match?.rules?.player_source_characters, [8, 8]);
  expectArray('handicap-off source conversion attack ratios',
    match?.rules?.player_attack_ratio_bits, ['3f800000', '3f800000']);
  expectArray('handicap-off source conversion defense ratios',
    match?.rules?.player_defense_ratio_bits, ['3f800000', '3f800000']);
  if (!Array.isArray(match?.players) || match.players.length !== 2) {
    failures.push(`live players: expected two entries, got ${JSON.stringify(match?.players)}`);
    return failures;
  }
  for (let index = 0; index < 2; index++) {
    const player = match.players[index];
    expect(`P${index + 1} live fighter is Mario`, player?.fighter, 0);
    expect(`P${index + 1} raw source slot`, player?.source_slot, 0);
    expect(`P${index + 1} resolved controller port`, player?.source_port, index);
    expect(`P${index + 1} source character`, player?.source_character, 8);
    expect(`P${index + 1} source slot is Human`, player?.slot_type, 0);
    expect(`P${index + 1} live owner is Human`, player?.human, true);
    expect(`P${index + 1} live stocks`, player?.stocks, 4);
    expect(`P${index + 1} source stocks`, player?.source_stocks, 4);
  }
  return failures;
}

// Separate original-default context. These predicates never alter the observation
// or call the competitive profile predicate with fabricated replacement fields.
export function defaultFirstDrawCssFailures(css) {
  const failures=[];
  const expect=(label,value,want)=>{if(JSON.stringify(value)!==JSON.stringify(want))failures.push(label);};
  expect('default CSS source',[css?.source?.valid,css?.source?.scene,css?.source?.css_setup?.valid],[true,1,true]);
  const r=css?.source?.rules;
  for(const [key,want] of Object.entries({mode:1,stock_count:4,time_limit:2,stock_time_limit:0,
    handicap:0,damage_ratio:10,friendly_fire:0,pause:1}))expect('default CSS '+key,r?.[key],want);
  expect('default CSS preferences',css?.source?.items,{frequency:-1,mask_hex:'ffffffffffffffff'});
  const raw=css?.selection;
  expect('default raw CSS provenance',[raw?.valid,raw?.scene,raw?.provenance],[true,1,'raw_css_vs_start']);
  for(const [key,want] of Object.entries({match_kind:0,timer_enabled:0,time_limit_seconds:0,
    friendly_fire:0,disable_pausing:0,damage_ratio_bits:'3f800000',item_frequency:2,
    item_mask_hex:'ffffffffffffffff',player_stocks:[0,0]}))expect('default raw CSS '+key,raw?.[key],want);
  expect('default CSS teams',[css?.source?.css_setup?.is_teams,css?.source?.css_setup?.player_teams],[0,[0,0]]);
  return failures;
}
export function defaultFirstDrawMatchFailures(match,css) {
  const failures=defaultFirstDrawCssFailures(css);
  const expect=(label,value,want)=>{if(JSON.stringify(value)!==JSON.stringify(want))failures.push(label);};
  expect('default live match state',[match?.leg,match?.ready,match?.paused,match?.ending,match?.complete,
    match?.observer_error??false,match?.outcome],['vs',true,false,false,false,false,0]);
  if(!Number.isSafeInteger(match?.frame)||match.frame<180||match.frame>240)failures.push('default source frame180..240');
  if(!Number.isInteger(match?.rng)||match.rng<0||match.rng>0xffffffff)failures.push('default actual RNG missing');
  // gm_80167BC8 leaves raw time_limit unchanged when stock_time_limit is zero;
  // it copies item frequency and converts actual preference bits by the source table.
  const rules=match?.rules;
  for(const [key,want] of Object.entries({match_kind:1,stage:32,timer_enabled:0,
    time_limit:css?.selection?.time_limit_seconds,disable_pausing:0,friendly_fire:0,
    damage_ratio_bits:'3f800000',item_frequency:-1,item_mask_hex:'ffffffffffffffff',
    is_stock:1,is_vs:1,source_sudden_death_flag:0,is_teams:0,
    player_teams:[0,0],player_stocks:[4,4],player_source_slots:[0,0],resolved_controller_ports:[0,1],
    player_slot_types:[0,0],player_source_stocks:[4,4],player_source_characters:[8,8],
    player_attack_ratio_bits:['3f800000','3f800000'],player_defense_ratio_bits:['3f800000','3f800000']}))
    expect('default normalized '+key,rules?.[key],want);
  expect('default original source slots',match?.observed_player_source_slots,[0,1]);
  if(!Array.isArray(match?.players)||match.players.length!==2)failures.push('default two live players required');
  else for(let i=0;i<2;i++){
    const p=match.players[i];
    for(const [key,want] of Object.entries({fighter:0,source_player_index:i,source_slot:0,source_port:i,
      source_character:8,slot_type:0,human:true,stocks:4,source_stocks:4,source_initial_damage:0,damage_percent:0}))
      expect('default P'+(i+1)+' '+key,p?.[key],want);
  }
  return failures;
}
