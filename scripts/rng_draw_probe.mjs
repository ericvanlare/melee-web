/** Bounded HSD RNG draw-probe request and artifact validation. */

const MAX_REPLAY_FRAMES = 108000;
const MAX_SELECTED_CURSORS = 9;
const MAX_DRAWS_PER_CURSOR = 1024;
const SEED = /^[0-9a-f]{8}$/;

function decimal(value, name) {
  if (!/^(0|[1-9][0-9]*)$/.test(value))
    throw Error(`${name} must contain unsigned decimal cursor indexes`);
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed >= MAX_REPLAY_FRAMES)
    throw Error(`${name} cursor indexes must be below ${MAX_REPLAY_FRAMES}`);
  return parsed;
}

export function parseRngDrawProbe({range, cursors}) {
  if (range !== undefined && cursors !== undefined)
    throw Error('--rng-draw-probe-range and --rng-draw-probe-cursors are mutually exclusive');
  if (range === undefined && cursors === undefined) return null;
  let selected;
  let request;
  if (range !== undefined) {
    const match = /^(0|[1-9][0-9]*):(0|[1-9][0-9]*)$/.exec(range);
    if (!match) throw Error('--rng-draw-probe-range must be START:END with unsigned decimal cursors');
    const first = decimal(match[1], '--rng-draw-probe-range');
    const last = decimal(match[2], '--rng-draw-probe-range');
    if (last < first || last - first >= MAX_SELECTED_CURSORS)
      throw Error('--rng-draw-probe-range must select 1 to 9 adjacent cursors');
    selected = Array.from({length: last - first + 1}, (_, index) => first + index);
    request = {kind: 'range', first, last};
  } else {
    const parts = cursors.split(',');
    if (parts.length < 1 || parts.length > MAX_SELECTED_CURSORS)
      throw Error('--rng-draw-probe-cursors must select 1 to 9 cursors');
    selected = parts.map((value, index) => decimal(value, `cursor ${index + 1}`));
    if (selected.some((value, index) => index > 0 && value <= selected[index - 1]))
      throw Error('--rng-draw-probe-cursors must be strictly ascending and unique');
    request = {kind: 'cursors', cursors: selected};
  }
  return {request, selected};
}

export function validateRngDrawProbeRows(textRows, selection, {observedCursor, deliberateStop} = {}) {
  if (!selection) throw Error('RNG draw rows were supplied without a probe request');
  if (!Array.isArray(textRows) || textRows.length > selection.selected.length)
    throw Error('RNG draw probe exceeded its selected-cursor bound');
  const requested = new Set(selection.selected);
  const seen = new Set();
  const rows = [];
  for (const text of textRows) {
    let row;
    try { row = JSON.parse(text); }
    catch { throw Error('RNG draw probe row is not valid JSON'); }
    if (!row || row.schema !== 'melee-web-rng-draw-probe' || row.version !== 1 ||
        !Number.isSafeInteger(row.source_cursor) || !requested.has(row.source_cursor) ||
        seen.has(row.source_cursor) || row.overflowed !== false || !Array.isArray(row.draws) ||
        row.draws.length > MAX_DRAWS_PER_CURSOR)
      throw Error('RNG draw probe row has an invalid identity, cursor, or bounded payload');
    for (const draw of row.draws) {
      if (!draw || !['HSD_Rand', 'HSD_Randf'].includes(draw.kind) ||
          typeof draw.seed_after !== 'string' || !SEED.test(draw.seed_after))
        throw Error('RNG draw probe row contains an invalid kind or 32-bit post-update seed');
    }
    seen.add(row.source_cursor);
    rows.push({text, row});
  }
  for (let index = 1; index < rows.length; ++index) {
    if (rows[index].row.source_cursor <= rows[index - 1].row.source_cursor)
      throw Error('RNG draw probe rows are not in increasing source-cursor order');
  }
  const reached = cursor => Number.isSafeInteger(observedCursor) && cursor <= observedCursor;
  const missing = selection.selected.filter(cursor => !seen.has(cursor));
  const unexpectedlyMissing = missing.filter(cursor => reached(cursor) ||
    !deliberateStop || cursor <= deliberateStop.requested_cursor);
  if (unexpectedlyMissing.length)
    throw Error(`RNG draw probe omitted reached selected cursors: ${unexpectedlyMissing.join(',')}`);
  return {
    rows: rows.map(value => value.text),
    missing_cursors: missing,
    complete: missing.length === 0,
  };
}
