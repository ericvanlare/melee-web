export const ITEM_ROW_TO_PREFERENCE_BIT = Object.freeze([
  0x05, 0x12, 0x0a, 0x1e, 0x0d, 0x18, 0x03, 0x0e,
  0x17, 0x1b, 0x01, 0x09, 0x08, 0x07, 0x15, 0x04,
  0x06, 0x02, 0x0f, 0x00, 0x11, 0x0b, 0x1f, 0x1a,
  0x14, 0x19, 0x10, 0x16, 0x13, 0x1d, 0x0c,
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

export function deriveAllOffItemMasks({initialPreferenceMaskHex, rows}) {
  if (!Array.isArray(rows) || rows.length !== ITEM_ROW_TO_PREFERENCE_BIT.length)
    throw new Error(`expected ${ITEM_ROW_TO_PREFERENCE_BIT.length} observed item rows`);

  let switchMask = 0n;
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
  }

  const initialPreferences = parseMask(initialPreferenceMaskHex, 'initial preference mask');
  const preferenceMask = initialPreferences & ~switchMask & UINT64_MASK;
  return {
    preferenceMaskHex: formatMask(preferenceMask),
    clearedPreferenceMaskHex: formatMask(switchMask),
  };
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
