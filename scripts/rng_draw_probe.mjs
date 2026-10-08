/** Bounded HSD RNG draw-probe request and artifact validation. */

const MAX_REPLAY_FRAMES = 108000;
const MAX_SELECTED_CURSORS = 9;
const MAX_DRAWS_PER_CURSOR = 1024;
const SEED = /^[0-9a-f]{8}$/;
const MAX_HIT_TRANSITION_ROWS = 64;
const MAX_CANDIDATE_HIT_TRANSITION_ROWS = 76;

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
  let captureVersion = null;
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
    const candidateEnabled = Boolean(row && typeof row === 'object' && Object.hasOwn(row,'candidate_enabled'));
    const candidateHeaderValid = candidateEnabled && row.source_cursor === 5239 && row.candidate_enabled === true &&
      uint(row.candidate_passes) && row.candidate_passes > 0 && uint(row.candidate_pairs);
    if (!row || row.schema !== 'melee-web-hit-transition-probe' ||
        ![1,2].includes(row.version) || (captureVersion !== null && row.version !== captureVersion) ||
        !selection.selected.includes(row.source_cursor) || seen.has(row.source_cursor) ||
        row.source_cursor <= previousCursor || row.overflowed !== false ||
        !Array.isArray(row.events) || row.events.length < 2 ||
        row.events.length > (candidateHeaderValid ? MAX_CANDIDATE_HIT_TRANSITION_ROWS : MAX_HIT_TRANSITION_ROWS) ||
        !Array.isArray(row.hook_counts) || row.hook_counts.length !== 3 ||
        !row.hook_counts.every(uint)) throw Error('Invalid hit transition row identity, version, or bound');
    if(captureVersion===null)captureVersion=row.version;
    seen.add(row.source_cursor); previousCursor = row.source_cursor;
    const counts = [0,0,0], stack = [], pending = new Map();
    let ordinal = 0, passOrdinal = 0, pairOrdinal = 0, geometryOrdinal = 0, producerOrdinal = 0;
    let candidatePass = null, candidatePair = null, candidateProducer = null, expectedPasses = 0;
    if (candidateEnabled && (row.source_cursor !== 5239 || row.candidate_enabled !== true ||
        !uint(row.candidate_passes) || !uint(row.candidate_pairs)))
      throw Error('Invalid selected candidate protocol summary');
    const candidateBits = (values,n) => Array.isArray(values) && values.length === n &&
      values.every(value => typeof value === 'string' && SEED.test(value));
    const candidateLogs = event => Number.isInteger(event.log0_count) && event.log0_count >= 0 && event.log0_count <= 20 &&
      Number.isInteger(event.log1_count) && event.log1_count >= 0 && event.log1_count <= 20;
    const identity = row.events[0];
    if (!uint(identity.match_index) || !uint(identity.generation))
      throw Error('Invalid hit transition entity generation');
    for (const [index,event] of row.events.entries()) {
      if (!event || event.sequence !== index || event.slot !== 1 || event.entity_index !== 0 ||
          event.match_index !== identity.match_index || event.generation !== identity.generation ||
          event.fighter_player_id !== 1 || event.fighter_gobj_linked !== true ||
          typeof event.rng !== 'string' || !SEED.test(event.rng) ||
          !uint(event.invocation) || !Number.isInteger(event.kind) || event.kind < 0 || event.kind > 4)
        throw Error('Invalid hit transition event identity or sequence');
      const marker = index === 0 || index === row.events.length - 1;
      if (marker) {
        if (event.phase !== (index === 0 ? 'scheduler_start' : 'scheduler_return') ||
            event.kind !== 3 || event.invocation !== 0 || (index && stack.length))
          throw Error('Missing scheduler marker or unmatched hook');
      } else if (event.kind === 4) {
        if (!candidateEnabled || pending.get(stack.at(-1))?.kind !== 0 ||
            event.attacker_slot !== 0 || event.receiver_slot !== 1 || !uint(event.pass) || !uint(event.pair) ||
            !Number.isInteger(event.hit_index) || event.hit_index < -1 || event.hit_index >= 4 ||
            !Number.isInteger(event.hurt_index) || event.hurt_index < -1 || event.hurt_index >= 15)
          throw Error('Invalid selected pair identity or authored bounds');
        if (event.phase === 'pass_entry') {
          if (candidatePass || candidatePair || candidateProducer || event.invocation !== ++passOrdinal ||
              event.pass !== event.invocation || event.pair !== 0 || event.hit_index !== -1 || event.hurt_index !== -1)
            throw Error('Duplicate or invalid candidate pass');
          candidatePass = event.invocation;
        } else if (event.phase === 'pass_return') {
          if (event.invocation !== candidatePass || event.pass !== candidatePass || event.pair !== 0 ||
              candidatePair || candidateProducer) throw Error('Incomplete candidate pass');
          candidatePass = null;
        } else if (event.phase === 'pair_entry') {
          if (!candidatePass || candidatePair || candidateProducer || event.pass !== candidatePass ||
              event.invocation !== ++pairOrdinal || event.pair !== event.invocation ||
              event.hit_index !== -1 || event.hurt_index !== -1 || !uint(event.encounter) ||
              typeof event.self_seen !== 'boolean' || !uint(event.attacker_generation) ||
              !Number.isInteger(event.hurt_length) || event.hurt_length < 0 || event.hurt_length > 15 ||
              !sint(event.receiver_ground_air) || !sint(event.receiver_x1988) || !sint(event.receiver_x198c) ||
              typeof event.receiver_shield !== 'boolean' || typeof event.receiver_x221d_b6 !== 'boolean')
            throw Error('Invalid pair entry or runtime hurt length');
          candidatePair = {id:event.pair,pass:event.pass,encounter:event.encounter,self:event.self_seen,
            candidates:0,hurtLength:event.hurt_length,geometry:null};
        } else {
          if (!candidatePass || !candidatePair || event.pass !== candidatePair.pass || event.pair !== candidatePair.id)
            throw Error('Candidate event outside its selected pair');
          if (event.phase === 'candidate') {
            if (candidateProducer || event.invocation !== candidatePair.id ||
                event.hit_index !== candidatePair.candidates++ || event.hurt_index !== -1 ||
                !sint(event.hit_state) || !uint(event.hit_element) || !Array.isArray(event.hit_flags) ||
                event.hit_flags.length !== 6 || !event.hit_flags.every(v => v === 0 || v === 1) ||
                !candidateBits([event.hit_damage_bits,event.hit_radius_bits],2))
              throw Error('Invalid ordered hit candidate');
            candidatePair.geometry = null;
          } else if (event.phase === 'geometry') {
            if (candidateProducer || event.invocation !== ++geometryOrdinal ||
                event.hit_index !== candidatePair.candidates - 1 || event.hurt_index < 0 ||
                event.hurt_index >= candidatePair.hurtLength || ![0,1].includes(event.result) || !sint(event.mode) ||
                typeof event.cache_before !== 'boolean' || typeof event.cache_after !== 'boolean' ||
                typeof event.matrix_present !== 'boolean' || !candidateBits(event.geometry_before,18) ||
                !candidateBits(event.geometry_after,10) || !candidateBits(event.arguments,3) ||
                !candidateBits(event.matrix_bits,12)) throw Error('Invalid actual geometry completion');
            if(row.version===2) {
              const bonePointer=event.hurt_bone_pointer;
              if(!Number.isInteger(event.hurt_index)||!sint(event.outer_hurt_state) ||
                 typeof event.hit_radius_direct!=='boolean' ||
                 typeof event.hurt_bone_present!=='boolean' || !sint(event.hurt_bone_idx) ||
                 !pointer(bonePointer) ||
                 event.hurt_bone_present!==(bonePointer!=='0000000000000000') ||
                 !candidateBits(event.outer_offsets_bits,6) ||
                 ![0,1].includes(event.inner_call_count) ||
                 typeof event.inner_matrix_present!=='boolean')
                throw Error('Invalid version-2 outer geometry identity');
              if(event.inner_call_count===1) {
                if(event.inner_skip_reason!==null || event.mode!==0 || event.outer_hurt_state===2 ||
                   !candidateBits(event.inner_endpoints_bits,12) ||
                   !candidateBits(event.inner_effective_arguments_bits,3) ||
                   (event.inner_matrix_present ? !candidateBits(event.inner_matrix_bits,12) :
                     event.inner_matrix_bits!==null))
                  throw Error('Invalid version-2 actual inner-helper input');
              } else {
                const expectedSkip=event.outer_hurt_state===2?'intangible':
                  event.mode!==0?'mode_nonzero':null;
                if(expectedSkip===null || event.inner_skip_reason!==expectedSkip ||
                   event.inner_endpoints_bits!==null || event.inner_matrix_present!==false ||
                   event.inner_matrix_bits!==null || event.inner_effective_arguments_bits!==null)
                  throw Error('Unexplained version-2 inner-helper omission');
              }
            }
            candidatePair.geometry = {result:event.result,hit:event.hit_index,hurt:event.hurt_index,consumed:false};
          } else if (event.phase === 'producer_entry') {
            const geometry = candidatePair.geometry;
            if (candidateProducer || !geometry || geometry.result !== 1 || geometry.consumed ||
                event.hit_index !== geometry.hit || event.hurt_index !== geometry.hurt ||
                event.invocation !== ++producerOrdinal || !candidateLogs(event))
              throw Error('Producer lacks a fresh actual geometry completion');
            geometry.consumed = true;
            candidateProducer = {id:event.invocation,phase:-1,log0:event.log0_count,log1:event.log1_count};
          } else if (event.phase === 'producer_branch') {
            if (!candidateProducer || candidateProducer.id !== event.invocation || !candidateLogs(event) ||
                !Number.isInteger(event.branch) || event.branch < 0 || event.branch > 5)
              throw Error('Invalid original producer branch');
            const previous = event.branch === 0 || event.branch === 3 ? -1 :
              event.branch === 1 ? 0 : event.branch === 2 ? 1 : event.branch === 4 ? 3 : 4;
            if (candidateProducer.phase !== previous ||
                (event.branch === 2 && event.log1_count !== candidateProducer.log1 + 1) ||
                (event.branch === 5 && event.log0_count !== candidateProducer.log0 + 1))
              throw Error('Duplicate or reordered producer branch/log append');
            candidateProducer = {...candidateProducer,phase:event.branch,log0:event.log0_count,log1:event.log1_count};
          } else if (event.phase === 'producer_return') {
            if (!candidateProducer || candidateProducer.id !== event.invocation || !candidateLogs(event) ||
                candidateProducer.phase < 0 || event.result !== (candidateProducer.phase === 0 ? 0 : 1))
              throw Error('Incomplete actual producer return');
            candidateProducer = null;
          } else if (event.phase === 'pair_return') {
            if (candidateProducer || event.invocation !== candidatePair.id ||
                event.encounter !== candidatePair.encounter || event.self_seen !== candidatePair.self ||
                event.candidates !== candidatePair.candidates) throw Error('Incomplete candidate pair');
            candidatePair = null;
          } else throw Error('Unexpected candidate observation phase');
        }
      } else if (event.phase === 'entry') {
        if (event.kind === 3 || event.invocation !== ++ordinal ||
            !sint(event.requested_motion) || !['flags','frame_bits','speed_bits','blend_bits']
              .every(key => typeof event[key] === 'string' && SEED.test(event[key])) ||
            !Number.isInteger(event.log_count) || event.log_count < 0 || event.log_count > 20 ||
            ![0,1].includes(event.log_kind) || (event.kind !== 1 && event.log_count !== 0))
          throw Error('Invalid hook entry or authored log bound');
        counts[event.kind]++;
        if (event.kind === 0 && !event.gate_x221f_b3 && !event.gate_x2219_b1) expectedPasses++;
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
    if (candidatePass || candidatePair || candidateProducer || (candidateEnabled &&
        (passOrdinal !== row.candidate_passes || pairOrdinal !== row.candidate_pairs ||
         passOrdinal !== expectedPasses))) throw Error('Missing candidate pass/pair completion');
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
