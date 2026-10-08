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

/** Fixed Bowser failure reducer; reuses the existing cursor request parser. */
export function parseHitTransitionProbe(cursors) {
  if (cursors === undefined) return null;
  const selection = parseRngDrawProbe({cursors});
  if (selection.selected.join(',') !== '5238,5239,5240')
    throw Error('Hit transition probe requires exactly cursors 5238,5239,5240');
  return selection;
}

export function validateHitTransitionProbeRows(textRows, selection,
  {observedCursor, deliberateStop} = {}) {
  if (!selection || selection.selected.join(',') !== '5238,5239,5240' ||
      !Array.isArray(textRows) || textRows.length > selection.selected.length)
    throw Error('Invalid hit transition request or cursor-row bound');
  const seen = new Set();
  let previousCursor = -1;
  const pointer = value => typeof value === 'string' && /^[0-9a-f]{16}$/.test(value);
  const uint = value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
  const sint = value => Number.isInteger(value) && value >= -0x80000000 && value <= 0x7fffffff;
  const vector = value => Array.isArray(value) && value.length === 3 && value.every(v => typeof v === 'string' && SEED.test(v));
  const source = event => pointer(event.source_gobj) && Number.isInteger(event.source_slot) &&
    event.source_slot >= -1 && event.source_slot <= 3 &&
    (event.source_slot === -1 || event.source_gobj !== '0000000000000000');
  for (const text of textRows) {
    let row;
    try { row = JSON.parse(text); } catch { throw Error('Invalid hit transition JSON'); }
    if (!row || row.schema !== 'melee-web-hit-transition-probe' || row.version !== 1 ||
        !selection.selected.includes(row.source_cursor) || seen.has(row.source_cursor) ||
        row.source_cursor <= previousCursor || row.overflowed !== false ||
        !Array.isArray(row.events) || row.events.length < 2 || row.events.length > 64 ||
        !Array.isArray(row.hook_counts) || row.hook_counts.length !== 3 ||
        !row.hook_counts.every(uint)) throw Error('Invalid hit transition row identity or bound');
    seen.add(row.source_cursor); previousCursor = row.source_cursor;
    const counts = [0,0,0], stack = [], pending = new Map();
    let ordinal = 0;
    const identity = row.events[0];
    if (!uint(identity.match_index) || !uint(identity.generation))
      throw Error('Invalid hit transition entity generation');
    for (const [index,event] of row.events.entries()) {
      if (!event || event.sequence !== index || event.slot !== 1 || event.entity_index !== 0 ||
          event.match_index !== identity.match_index || event.generation !== identity.generation ||
          event.fighter_player_id !== 1 || event.fighter_gobj_linked !== true ||
          typeof event.rng !== 'string' || !SEED.test(event.rng) ||
          !uint(event.invocation) || !Number.isInteger(event.kind) || event.kind < 0 || event.kind > 3)
        throw Error('Invalid hit transition event identity or sequence');
      const marker = index === 0 || index === row.events.length - 1;
      if (marker) {
        if (event.phase !== (index === 0 ? 'scheduler_start' : 'scheduler_return') ||
            event.kind !== 3 || event.invocation !== 0 || (index && stack.length))
          throw Error('Missing scheduler marker or unmatched hook');
      } else if (event.phase === 'entry') {
        if (event.kind === 3 || event.invocation !== ++ordinal ||
            !sint(event.requested_motion) || !['flags','frame_bits','speed_bits','blend_bits']
              .every(key => typeof event[key] === 'string' && SEED.test(event[key])) ||
            !Number.isInteger(event.log_count) || event.log_count < 0 || event.log_count > 20 ||
            ![0,1].includes(event.log_kind) || (event.kind !== 1 && event.log_count !== 0))
          throw Error('Invalid hook entry or authored log bound');
        counts[event.kind]++;
        pending.set(event.invocation,{kind:event.kind,count:event.log_count,logs:0});
        stack.push(event.invocation);
      } else if (event.phase === 'log') {
        const entry = pending.get(event.invocation);
        if (!entry || stack.at(-1) !== event.invocation || entry.kind !== 1 || event.kind !== 1 ||
            event.log_index !== entry.logs++ || event.log_index >= entry.count ||
            !sint(event.entity_kind) || !sint(event.fighter_kind) || !source(event) ||
            !pointer(event.hit) || !pointer(event.hurt) || !vector(event.position_bits) ||
            typeof event.damage_bits !== 'string' || !SEED.test(event.damage_bits) ||
            !uint(event.hit_bytes)) throw Error('Invalid ordered damage log observation');
      } else if (event.phase === 'return') {
        const entry = pending.get(event.invocation);
        if (!entry || stack.pop() !== event.invocation || event.kind !== entry.kind ||
            entry.logs !== entry.count) throw Error('Unmatched hook return or incomplete log');
        pending.delete(event.invocation);
      } else throw Error('Unexpected hit transition phase');
      if (event.phase !== 'log' && (!sint(event.motion) || !sint(event.animation) ||
          typeof event.gate_x221f_b3 !== 'boolean' || typeof event.gate_x2219_b1 !== 'boolean' ||
          !['damage_bits','damage_temp_bits','hitlag_bits','x18a0_bits']
            .every(key => typeof event[key] === 'string' && SEED.test(event[key])) ||
          !sint(event.damage_applied) || !sint(event.time_since_hit) || !uint(event.source_player) ||
          !vector(event.knockback_bits) || !source(event)))
        throw Error('Invalid exact hit transition state bits');
    }
    if (pending.size || counts.some((value,index) => value !== row.hook_counts[index]))
      throw Error('Incomplete hit transition hooks or count disagreement');
  }
  // Use the same reached-cursor rule as the RNG companion without trusting a caller completion flag.
  const missing = selection.selected.filter(cursor => !seen.has(cursor));
  if (missing.some(cursor => (Number.isSafeInteger(observedCursor) && cursor <= observedCursor) ||
      !deliberateStop || cursor <= deliberateStop.requested_cursor))
    throw Error('Hit transition probe omitted reached selected cursors');
  return {rows: textRows, missing_cursors: missing, complete: missing.length === 0};
}
