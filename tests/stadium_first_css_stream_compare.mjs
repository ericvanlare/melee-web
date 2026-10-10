const BATCH_COUNT = 148;
const FIRST_CONSUME_SEQUENCE = 839;
const SOURCE_SCENE_CSS = 1;
const MENU_PHASE_CSS = 1;
const SCENE_KIND_CSS = 8;
const ROUTE_KEYS = Object.freeze(['current_game_mode', 'previous_game_mode',
  'current_scene_index', 'previous_scene_index']);

function plainObject(value) {
  return value !== null && typeof value === 'object' &&
    !Array.isArray(value) && typeof value.then !== 'function';
}

function exactRoute(actual, expected) {
  const exactKeys = value => plainObject(value) &&
    Object.keys(value).sort().join('|') === [...ROUTE_KEYS].sort().join('|') &&
    ROUTE_KEYS.every(key => Number.isInteger(value[key]));
  return exactKeys(actual) && exactKeys(expected) &&
    actual.current_game_mode === expected.current_game_mode &&
    actual.previous_game_mode === expected.previous_game_mode &&
    actual.current_scene_index === expected.current_scene_index &&
    actual.previous_scene_index === expected.previous_scene_index;
}

function stableCssOwner(actual) {
  return actual.source_scene === SOURCE_SCENE_CSS &&
    actual.menu_phase === MENU_PHASE_CSS && actual.scene_kind === SCENE_KIND_CSS &&
    actual.world_generation_stable === true && actual.scene_owner_stable === true &&
    actual.seed_owner_stable === true;
}

function compareTick(actual, expected, index) {
  return plainObject(actual) && stableCssOwner(actual) &&
    actual.source_tick === index + 1 && actual.draw_ordinal === index + 1 &&
    actual.scene_frame === expected.native_post_host_tick_frame &&
    actual.pad_state_hex === expected.pad_state_hex &&
    actual.random_seed_hex === expected.random_seed_hex &&
    exactRoute(actual.scene_routing_getters, expected.scene_routing_getters);
}

function compareDraw(actual, expected) {
  return plainObject(actual) && stableCssOwner(actual) &&
    actual.source_tick === expected.source_tick &&
    actual.draw_ordinal === expected.draw_ordinal &&
    actual.scene_frame === expected.scene_frame &&
    actual.pad_state_hex === expected.pad_state_hex &&
    actual.random_seed_hex === expected.random_seed_hex &&
    exactRoute(actual.scene_routing_getters, expected.scene_routing_getters);
}

function validateExpectedPairs(expectedPairs) {
  if (!Array.isArray(expectedPairs) || expectedPairs.length !== BATCH_COUNT)
    throw new TypeError('CSS stream requires exactly 148 host-only expected pairs');
  expectedPairs.forEach((pair, index) => {
    const consume = FIRST_CONSUME_SEQUENCE + index * 5;
    if (!plainObject(pair) || pair.index !== index ||
        pair.consumed_pad_sequence !== consume ||
        pair.source_tick_sequence !== consume + 1 ||
        pair.draw_enter_sequence !== consume + 2 ||
        pair.draw_return_sequence !== consume + 3 ||
        !plainObject(pair.expected_post_tick) ||
        !plainObject(pair.expected_draw_return) ||
        pair.expected_post_tick.source_tick_value !== index + 1 ||
        pair.expected_post_tick.source_draw_ordinal !== index + 1 ||
        pair.expected_draw_return.source_tick !== index + 2 ||
        pair.expected_draw_return.draw_ordinal !== index + 1)
      throw new TypeError(`CSS stream expected pair ${index} has an invalid source boundary`);
  });
}

function validHex(value, byteLength) {
  return typeof value === 'string' && value.length === byteLength * 2 &&
    /^[0-9a-f]+$/.test(value);
}

// This callback receives actual native snapshots only. Expected rows remain in
// the browser page and are never passed into WASM or native source memory.
export function createFirstCssStreamComparator(expectedPairs) {
  validateExpectedPairs(expectedPairs);
  let nextIndex = 0;
  let pendingDraw = false;
  let terminal = false;
  let failed = false;
  let outcome = null;
  let terminalObservation = null;

  const compare = (phase, actualJson) => {
    if (terminal || failed) return false;
    if (typeof phase !== 'string' || typeof actualJson !== 'string') {
      failed = true;
      return false;
    }
    let actual;
    try {
      actual = JSON.parse(actualJson);
    } catch {
      failed = true;
      return false;
    }
    if (!plainObject(actual)) {
      failed = true;
      return false;
    }
    if (phase === 'tick') {
      if (pendingDraw || nextIndex >= BATCH_COUNT ||
          !compareTick(actual, expectedPairs[nextIndex].expected_post_tick, nextIndex)) {
        failed = true;
        return false;
      }
      pendingDraw = true;
      return true;
    }
    if (phase === 'draw') {
      if (!pendingDraw || nextIndex >= BATCH_COUNT ||
          !compareDraw(actual, expectedPairs[nextIndex].expected_draw_return)) {
        failed = true;
        return false;
      }
      pendingDraw = false;
      nextIndex += 1;
      if (nextIndex === BATCH_COUNT) {
        terminal = true;
        outcome = 'bounded_pair_cap';
      }
      return true;
    }
    if (phase === 'transition') {
      if (pendingDraw || actual.tick_result !== 3 ||
          actual.executed_host_ticks !== nextIndex + 2 ||
          actual.stream_input_ordinal !== nextIndex + 1 ||
          actual.consumed_pad_sequence !== FIRST_CONSUME_SEQUENCE + nextIndex * 5 ||
          actual.source_tick !== nextIndex + 1 ||
          actual.draw_ordinal !== nextIndex + 1 ||
          !Number.isInteger(actual.scene_frame) || actual.scene_frame < 0 ||
          actual.terminal_transition !== true ||
          actual.source_scene !== SOURCE_SCENE_CSS ||
          actual.menu_phase !== MENU_PHASE_CSS || actual.scene_kind !== SCENE_KIND_CSS ||
          actual.world_generation_stable !== true || actual.scene_owner_stable !== true ||
          actual.seed_owner_stable !== true ||
          !validHex(actual.random_seed_hex, 4) || !validHex(actual.pad_state_hex, 822) ||
          !exactRoute(actual.scene_routing_getters, actual.scene_routing_getters)) {
        failed = true;
        return false;
      }
      terminalObservation = actual;
      terminal = true;
      if (nextIndex === BATCH_COUNT - 1) {
        // A native transition request on the last retained additional input is
        // a bounded stop before any native draw. It follows 147 approved
        // pairs and is neither a terminal-tick comparison nor completion
        // of all 148 pairs; the original final draw stays unpaired.
        outcome = 'stop_after_last_input_request';
      } else {
        outcome = 'early_source_transition_before_raw_pair_cap';
        failed = true;
      }
      return false;
    }
    failed = true;
    return false;
  };

  return Object.freeze({
    compare,
    status: () => Object.freeze({next_index: nextIndex, pending_draw: pendingDraw,
      terminal, failed, outcome, terminal_observation: terminalObservation}),
  });
}

export function requestSynchronousApproval(callback, phase, actualJson) {
  if (typeof callback !== 'function') return false;
  try {
    return callback(phase, actualJson) === true;
  } catch {
    return false;
  }
}

// A separate one-use comparison after the stream has already stopped. The
// terminal scheduler snapshot remains unpaired; only raw DrawReturn1577 is
// compared here. Neither expected state nor this callback enters WASM.
export function createFirstCssFinalDrawComparator(expectedPair, terminal) {
  const wanted = expectedPair?.expected_draw_return;
  if (!plainObject(expectedPair) || expectedPair.index !== 147 ||
      expectedPair.consumed_pad_sequence !== 1574 ||
      expectedPair.source_tick_sequence !== 1575 ||
      expectedPair.draw_enter_sequence !== 1576 ||
      expectedPair.draw_return_sequence !== 1577 || !plainObject(wanted) ||
      wanted.source_tick !== 149 || wanted.draw_ordinal !== 148 ||
      wanted.scene_kind !== SCENE_KIND_CSS || !Number.isInteger(wanted.scene_frame) ||
      !validHex(wanted.pad_state_hex, 822) || !validHex(wanted.random_seed_hex, 4) ||
      !exactRoute(wanted.scene_routing_getters, wanted.scene_routing_getters))
    throw new TypeError('Final CSS draw requires the exact original row1577');
  if (!plainObject(terminal) || !stableCssOwner(terminal) ||
      !Number.isInteger(terminal.world_generation) || terminal.world_generation <= 0 ||
      terminal.source_tick !== 148 || terminal.draw_ordinal !== 148 ||
      terminal.consumed_pad_sequence !== 1574 || terminal.stream_input_ordinal !== 148 ||
      terminal.executed_host_ticks !== 149 || terminal.tick_result !== 3 ||
      terminal.terminal_transition !== true)
    throw new TypeError('Final CSS draw requires the retained last-input terminal owner');
  const generation = terminal.world_generation;
  let attempted = false;
  let approved = false;
  let failed = false;
  const compare = (phase, actualJson) => {
    if (attempted) { failed = true; return false; }
    attempted = true;
    if (phase !== 'final_draw' || typeof actualJson !== 'string') {
      failed = true; return false;
    }
    let actual;
    try { actual = JSON.parse(actualJson); }
    catch { failed = true; return false; }
    approved = plainObject(actual) && compareDraw(actual, wanted) &&
      actual.world_generation === generation;
    failed = !approved;
    return approved === true;
  };
  return Object.freeze({compare,
    status: () => Object.freeze({attempted, approved, failed})});
}

const FIRST_SSS_STREAM_SHA256 = '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778';
const FIRST_SSS_STATUS_SHA256 = '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f';
const FIRST_SSS_ENTRY_ARGUMENT = '0x80480668';
const FIRST_SSS_RETURN_ARGUMENT = '0x00000000';
const FIRST_SSS_ENTRY_SEQUENCE = 1579;
const FIRST_SSS_RETURN_SEQUENCE = 1603;
const FIRST_SSS_ENTRY_PC = '0x8025a998';
const FIRST_SSS_ENTRY_WORD = '0x7c0802a6';
const FIRST_SSS_RETURN_PC = '0x8025b84c';
const FIRST_SSS_RETURN_WORD = '0x4e800020';
const FIRST_SSS_LR = '0x801a40e8';
const FIRST_SSS_SOURCE_INVENTORY = Object.freeze([
  {tag: 32, flags: 0, address: 0x80480678, size: 0xF0},
  {tag: 35, flags: 0, address: 0x8048066C, size: 1},
  {tag: 33, flags: 0, address: 0x803BB300, size: 0x40},
  {tag: 34, flags: 0, address: 0x804D6038, size: 4},
  {tag: 36, flags: 0, address: 0x8045BF28, size: 2},
  {tag: 37, flags: 0, address: 0x8045BF2A, size: 2},
  {tag: 21, flags: 0, address: 0x804C1F84, size: 0x358},
  {tag: 17, flags: 0, address: 0x80479D30, size: 6},
  {tag: 30, flags: 0, address: 0x80479D58, size: 4},
  {tag: 40, flags: 0, address: 0x803DD9C4, size: 1},
  {tag: 19, flags: 0, address: 0x804D5F94, size: 4},
  {tag: 20, flags: 0, address: 0x804D5F90, size: 4},
]);
const SSS_RULE_KEYS = Object.freeze([
  'match_kind', 'hud_layout', 'timer_enabled', 'timer_counts_up',
  'friendly_fire', 'is_stock', 'single_button', 'disable_pausing', 'is_vs',
  'is_teams', 'item_frequency', 'stage_kind', 'time_limit', 'item_mask',
  'damage_ratio_bits', 'game_speed_bits',
]);
const SSS_NATIVE_RULE_KEYS = Object.freeze({hud_layout: 'x0_3', item_frequency: 'xB'});
const SSS_BOOLEAN_RULE_KEYS = new Set([
  'timer_enabled', 'timer_counts_up', 'friendly_fire', 'is_stock',
  'single_button', 'disable_pausing', 'is_vs',
]);
const SSS_PLAYER_KEYS = Object.freeze([
  'ckind', 'slot_type', 'stocks', 'color', 'slot', 'spawn',
  'spawn_direction', 'sub_color', 'handicap', 'team', 'nametag', 'flags_c',
  'flags_d', 'cpu_kind', 'cpu_level', 'damage_10', 'damage_12', 'hp',
  'attack_ratio_bits', 'defense_ratio_bits', 'model_scale_bits',
]);
const SSS_PLAYER_SIGNED = new Set(['ckind', 'stocks', 'spawn', 'spawn_direction', 'handicap']);
const SSS_PLAYER_U8 = new Set([
  'slot_type', 'color', 'slot', 'sub_color', 'team', 'nametag', 'flags_c',
  'flags_d', 'cpu_kind', 'cpu_level',
]);
const SSS_PLAYER_U16 = new Set(['damage_10', 'damage_12', 'hp']);
const SSS_OWNER_KEYS = Object.freeze([
  'host', 'session', 'world', 'audio', 'vs_mode', 'scene_info', 'payload', 'seed',
]);
const SSS_PAIR_KEYS = Object.freeze([
  'status', 'armed', 'kicked', 'attempted', 'captured', 'compared', 'complete',
  'failed', 'error', 'host_entered', 'session_phase', 'world_generation',
  'audio_generation', 'audio_owner_live', 'audio_render_calls_before_rebuild',
  'audio_render_frames_before_rebuild', 'audio_render_calls_after_sss_enter',
  'audio_render_frames_after_sss_enter', 'sss_host_tick_calls', 'sss_host_draw_calls',
  'entry', 'returned',
]);
const SSS_NOTE_KEYS = Object.freeze([
  'phase', 'host_entered', 'session_phase', 'source_scene', 'scene_kind',
  'scene_frame', 'random_seed_hex', 'pad_state_hex', 'scene_routing_getters',
  'owners', 'world_generation', 'audio_generation', 'session_ticks', 'sss',
]);
const SSS_HEADER_KEYS = Object.freeze([
  'unk_stage', 'x1', 'no_lras', 'force_stage_id', 'start_game',
]);
const SSS_SCALAR_RULE_KEYS = new Set([
  'match_kind', 'hud_layout', 'is_teams', 'item_frequency', 'stage_kind', 'time_limit',
]);
const SSS_HEX_RULE_LENGTHS = Object.freeze({item_mask: 16, damage_ratio_bits: 8, game_speed_bits: 8});

function exactOwnKeys(value, wanted) {
  return plainObject(value) && Object.keys(value).sort().join('|') === [...wanted].sort().join('|');
}

function expectedSssNoteIsValid(note, phase) {
  if (!plainObject(note) || note.phase !== phase || note.scene_frame !== 149 ||
      note.scene_kind !== 9 || !validHex(note.random_seed_hex, 4) ||
      !validHex(note.pad_state_hex, 822) || !exactRoute(note.scene_routing_getters,
        note.scene_routing_getters) || note.scene_routing_getters.current_game_mode !== 2 ||
      !plainObject(note.sss) || !plainObject(note.sss.header) ||
      !Number.isInteger(note.sss.header.start_game) ||
      ![0, 1].includes(note.sss.header.start_game) || !plainObject(note.sss.vs) ||
      !plainObject(note.sss.vs.start) || !plainObject(note.sss.vs.start.rules) ||
      !Array.isArray(note.sss.vs.start.players) || note.sss.vs.start.players.length !== 4 ||
      !exactOwnKeys(note.sss.vs.start.rules, SSS_RULE_KEYS)) return false;
  const rules = note.sss.vs.start.rules;
  for (const key of SSS_RULE_KEYS) {
    const value = rules[key];
    if (SSS_BOOLEAN_RULE_KEYS.has(key)) {
      if (typeof value !== 'boolean') return false;
    } else if (SSS_HEX_RULE_LENGTHS[key]) {
      if (!validHex(value, SSS_HEX_RULE_LENGTHS[key] / 2)) return false;
    } else if (!Number.isSafeInteger(value)) return false;
  }
  return note.sss.vs.start.players.every(player => exactOwnKeys(player, SSS_PLAYER_KEYS) &&
    SSS_PLAYER_KEYS.every(key => {
      const value = player[key];
      if (['attack_ratio_bits', 'defense_ratio_bits', 'model_scale_bits'].includes(key))
        return validHex(value, 4);
      return Number.isSafeInteger(value);
    }));
}

function expectedSssSourceRowIsValid(source, expectedNote, phase) {
  const entry = phase === 'sss_entry';
  const expectedSequence = entry ? FIRST_SSS_ENTRY_SEQUENCE : FIRST_SSS_RETURN_SEQUENCE;
  const expectedPc = entry ? FIRST_SSS_ENTRY_PC : FIRST_SSS_RETURN_PC;
  const expectedWord = entry ? FIRST_SSS_ENTRY_WORD : FIRST_SSS_RETURN_WORD;
  const expectedArgument = entry ? FIRST_SSS_ENTRY_ARGUMENT : FIRST_SSS_RETURN_ARGUMENT;
  if (!plainObject(source) || source.sequence !== expectedSequence || source.pc !== expectedPc ||
      source.word !== expectedWord || source.lr !== FIRST_SSS_LR ||
      source.argument !== expectedArgument || source.source_tick !== 149 ||
      source.draw_ordinal !== 149 || !Array.isArray(source.source_slice_inventory) ||
      source.setup_profile_verified_by_observer !== false ||
      source.setup_receipt_sha256 !== 'e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda' ||
      source.source_slice_inventory.length !== FIRST_SSS_SOURCE_INVENTORY.length ||
      !plainObject(source.source_slices_hex)) return false;
  const inventory = [...source.source_slice_inventory].sort((a, b) => a.tag - b.tag || a.flags - b.flags);
  const expectedInventory = [...FIRST_SSS_SOURCE_INVENTORY].sort((a, b) => a.tag - b.tag || a.flags - b.flags);
  if (inventory.some((item, index) => !plainObject(item) ||
      item.tag !== expectedInventory[index].tag || item.flags !== expectedInventory[index].flags ||
      item.address !== expectedInventory[index].address || item.size !== expectedInventory[index].size))
    return false;
  const expectedRawKeys = FIRST_SSS_SOURCE_INVENTORY.map(item => `${item.tag}:${item.flags}`).sort();
  if (!exactOwnKeys(source.source_slices_hex, expectedRawKeys)) return false;
  for (const slice of FIRST_SSS_SOURCE_INVENTORY) {
    const raw = source.source_slices_hex[`${slice.tag}:${slice.flags}`];
    if (typeof raw !== 'string' || raw.length !== slice.size * 2 || !/^(?:[0-9a-f]{2})+$/.test(raw))
      return false;
  }
  const raw = source.source_slices_hex;
  const expectedGetters = {
    current_game_mode: Number.parseInt(raw['17:0'].slice(0, 2), 16),
    previous_game_mode: Number.parseInt(raw['17:0'].slice(4, 6), 16),
    current_scene_index: Number.parseInt(raw['17:0'].slice(6, 8), 16),
    previous_scene_index: Number.parseInt(raw['17:0'].slice(8, 10), 16),
  };
  return plainObject(expectedNote) &&
    Number.parseInt(raw['35:0'], 16) === expectedNote.sss.header.start_game &&
    raw['30:0'] === '00000095' && raw['40:0'] === '09' &&
    raw['19:0'] === '804d5f90' && raw['20:0'] === expectedNote.random_seed_hex &&
    raw['17:0'].length === 12 && expectedGetters.current_game_mode === 2 &&
    exactRoute(expectedGetters, expectedNote.scene_routing_getters) &&
    source.scene_routing_raw_hex === raw['17:0'] &&
    source.rng_pointer_hex === '804d5f90';
}

function validateFirstSssExpected(expected) {
  if (!plainObject(expected) ||
      expected.schema !== 'melee-web-stadium-first-sss-constructor-pair-diagnostic' ||
      expected.version !== 1 || expected.source_admission !== false ||
      expected.whole_session_equivalent !== false || !plainObject(expected.provenance) ||
      expected.provenance.observer_sha256 !== FIRST_SSS_STREAM_SHA256 ||
      expected.provenance.observer_bytes !== 4397889 ||
      expected.provenance.observer_status_sha256 !== FIRST_SSS_STATUS_SHA256 ||
      expected.provenance.observer_status_bytes !== 515 ||
      expected.provenance.entry_sequence !== FIRST_SSS_ENTRY_SEQUENCE ||
      expected.provenance.return_sequence !== FIRST_SSS_RETURN_SEQUENCE ||
      expected.provenance.entry_pc !== FIRST_SSS_ENTRY_PC ||
      expected.provenance.entry_word !== FIRST_SSS_ENTRY_WORD ||
      expected.provenance.return_pc !== FIRST_SSS_RETURN_PC ||
      expected.provenance.return_word !== FIRST_SSS_RETURN_WORD ||
      expected.provenance.entry_lr !== FIRST_SSS_LR ||
      expected.provenance.return_lr !== FIRST_SSS_LR ||
      expected.provenance.source_tick !== 149 || expected.provenance.draw_ordinal !== 149 ||
      expected.provenance.scene_frame !== 149 || expected.provenance.source_scene_kind !== 9 ||
      !plainObject(expected.source_entry) ||
      expected.source_entry.argument !== FIRST_SSS_ENTRY_ARGUMENT ||
      expected.source_entry.sequence !== FIRST_SSS_ENTRY_SEQUENCE ||
      expected.source_entry.pc !== FIRST_SSS_ENTRY_PC ||
      expected.source_entry.word !== FIRST_SSS_ENTRY_WORD ||
      !expectedSssSourceRowIsValid(expected.source_entry, expected.expected_entry, 'sss_entry') ||
      !plainObject(expected.source_return) ||
      expected.source_return.argument !== FIRST_SSS_RETURN_ARGUMENT ||
      expected.source_return.sequence !== FIRST_SSS_RETURN_SEQUENCE ||
      expected.source_return.pc !== FIRST_SSS_RETURN_PC ||
      expected.source_return.word !== FIRST_SSS_RETURN_WORD ||
      !expectedSssSourceRowIsValid(expected.source_return, expected.expected_return, 'sss_return') ||
      !expectedSssNoteIsValid(expected.expected_entry, 'sss_entry') ||
      !expectedSssNoteIsValid(expected.expected_return, 'sss_return'))
    throw new TypeError('SSS comparison requires the exact retained source constructor pair');
  return expected;
}

function mismatch(path, expected, actual, reason = 'value differs') {
  return Object.freeze({path, expected, actual, reason});
}

function compareExpectedField(actual, expected, path) {
  return Object.is(actual, expected) ? null : mismatch(path, expected, actual);
}

function actualRulesMismatch(actual, expected, prefix) {
  const rules = actual?.sss?.vs?.start?.rules;
  const wanted = expected?.sss?.vs?.start?.rules;
  if (!plainObject(rules) || !plainObject(wanted))
    return mismatch(`${prefix}.sss.vs.start.rules`, 'object', rules, 'missing native rules');
  for (const key of SSS_RULE_KEYS) {
    const nativeKey = SSS_NATIVE_RULE_KEYS[key] || key;
    const value = rules[nativeKey];
    if (SSS_BOOLEAN_RULE_KEYS.has(key)) {
      if (!Number.isInteger(value) || (value !== 0 && value !== 1))
        return mismatch(`${prefix}.sss.vs.start.rules.${nativeKey}`, 'integer 0 or 1', value,
          'native boolean is not explicitly encoded as 0 or 1');
      if ((value === 1) !== wanted[key])
        return mismatch(`${prefix}.sss.vs.start.rules.${nativeKey}`, wanted[key], value);
    } else {
      if (SSS_HEX_RULE_LENGTHS[key] && !validHex(value, SSS_HEX_RULE_LENGTHS[key] / 2))
        return mismatch(`${prefix}.sss.vs.start.rules.${nativeKey}`,
          `${SSS_HEX_RULE_LENGTHS[key]} lowercase hex digits`, value, 'malformed raw bit string');
      if (SSS_SCALAR_RULE_KEYS.has(key) && !Number.isSafeInteger(value))
        return mismatch(`${prefix}.sss.vs.start.rules.${nativeKey}`, 'integer', value,
          'native scalar is not an integer');
      if (!Object.is(value, wanted[key]))
        return mismatch(`${prefix}.sss.vs.start.rules.${nativeKey}`, wanted[key], value);
    }
  }
  return null;
}

function actualPlayersMismatch(actual, expected, prefix) {
  const players = actual?.sss?.vs?.start?.players;
  const wanted = expected?.sss?.vs?.start?.players;
  if (!Array.isArray(players) || players.length !== 6 || !Array.isArray(wanted) || wanted.length !== 4)
    return mismatch(`${prefix}.sss.vs.start.players`, 'six native players/four source rows',
      Array.isArray(players) ? players.length : players, 'player array shape differs');
  for (let index = 0; index < 4; index += 1) {
    if (!plainObject(players[index]))
      return mismatch(`${prefix}.sss.vs.start.players[${index}]`, 'native player object',
        players[index], 'missing native player');
    for (const key of SSS_PLAYER_KEYS) {
      const value = players[index][key];
      const path = `${prefix}.sss.vs.start.players[${index}].${key}`;
      if (SSS_PLAYER_SIGNED.has(key) &&
          (!Number.isInteger(value) || value < -128 || value > 127))
        return mismatch(path, 'signed byte', value, 'native field is outside source signed-byte range');
      if (SSS_PLAYER_U8.has(key) &&
          (!Number.isInteger(value) || value < 0 || value > 255))
        return mismatch(path, 'unsigned byte', value, 'native field is outside source byte range');
      if (SSS_PLAYER_U16.has(key) &&
          (!Number.isInteger(value) || value < 0 || value > 65535))
        return mismatch(path, 'unsigned 16-bit integer', value, 'native field is outside source word range');
      if (['attack_ratio_bits', 'defense_ratio_bits', 'model_scale_bits'].includes(key) &&
          !validHex(value, 4))
        return mismatch(path, 'eight lowercase hex digits', value, 'malformed raw float bits');
      if (!SSS_PLAYER_SIGNED.has(key) && !SSS_PLAYER_U8.has(key) &&
          !SSS_PLAYER_U16.has(key) && !['attack_ratio_bits', 'defense_ratio_bits',
            'model_scale_bits'].includes(key) && !Number.isSafeInteger(value))
        return mismatch(path, 'integer', value, 'native field is not an integer');
      if (!Object.is(value, wanted[index][key]))
        return mismatch(path, wanted[index][key], value);
    }
  }
  return null;
}

function actualSssNoteMismatch(actual, expected, phase, pair) {
  const prefix = phase === 'sss_entry' ? 'entry' : 'returned';
  if (!plainObject(actual)) return mismatch(prefix, 'native note object', actual, 'missing note');
  if (!exactOwnKeys(actual, SSS_NOTE_KEYS))
    return mismatch(`${prefix}`, [...SSS_NOTE_KEYS], actual,
      'native SSS note fields differ from the frozen interface');
  for (const [key, value] of Object.entries({phase, host_entered: false,
    session_phase: 2, source_scene: 2, scene_kind: expected.scene_kind, scene_frame: expected.scene_frame,
    random_seed_hex: expected.random_seed_hex, pad_state_hex: expected.pad_state_hex})) {
    const different = compareExpectedField(actual[key], value, `${prefix}.${key}`);
    if (different) return different;
  }
  if (!Number.isSafeInteger(actual.session_ticks) || actual.session_ticks < 0)
    return mismatch(`${prefix}.session_ticks`, 'nonnegative integer', actual.session_ticks);
  if (actual.world_generation !== pair.world_generation ||
      !Number.isSafeInteger(actual.world_generation) || actual.world_generation <= 0)
    return mismatch(`${prefix}.world_generation`, pair.world_generation, actual.world_generation,
      'note is outside the retained rebuilt world');
  if (actual.audio_generation !== pair.audio_generation ||
      !Number.isSafeInteger(actual.audio_generation) || actual.audio_generation < 0)
    return mismatch(`${prefix}.audio_generation`, pair.audio_generation, actual.audio_generation,
      'note is outside the retained audio owner');
  if (!exactOwnKeys(actual.owners, SSS_OWNER_KEYS))
    return mismatch(`${prefix}.owners`, [...SSS_OWNER_KEYS], actual.owners,
      'owner/liveness keys differ');
  for (const key of SSS_OWNER_KEYS)
    if (actual.owners[key] !== true)
      return mismatch(`${prefix}.owners.${key}`, true, actual.owners[key], 'required owner is not live');
  if (!exactRoute(actual.scene_routing_getters, expected.scene_routing_getters))
    return mismatch(`${prefix}.scene_routing_getters`, expected.scene_routing_getters,
      actual.scene_routing_getters, 'four established source routing getters differ');
  if (!plainObject(actual.sss?.header) || !Number.isInteger(actual.sss.header.start_game) ||
      ![0, 1].includes(actual.sss.header.start_game))
    return mismatch(`${prefix}.sss.header.start_game`, 'integer 0 or 1',
      actual.sss?.header?.start_game, 'native start_game is not an explicit boolean byte');
  if (!exactOwnKeys(actual.sss.header, SSS_HEADER_KEYS))
    return mismatch(`${prefix}.sss.header`, [...SSS_HEADER_KEYS], actual.sss.header,
      'native SSS header fields differ from the frozen interface');
  const startGame = compareExpectedField(actual.sss.header.start_game,
    expected.sss.header.start_game, `${prefix}.sss.header.start_game`);
  if (startGame) return startGame;
  const rules = actualRulesMismatch(actual, expected, prefix);
  if (rules) return rules;
  return actualPlayersMismatch(actual, expected, prefix);
}

function actualFirstSssPairMismatch(actual, expected) {
  if (!plainObject(actual)) return mismatch('pair', 'native pair object', actual, 'missing native pair');
  if (!exactOwnKeys(actual, SSS_PAIR_KEYS))
    return mismatch('pair', [...SSS_PAIR_KEYS], actual,
      'native pair fields differ from the frozen interface');
  for (const [key, value] of Object.entries({status: 'captured', armed: true, kicked: true,
    attempted: true, captured: true, compared: false, complete: false, failed: false,
    host_entered: true, session_phase: 3, audio_owner_live: true, sss_host_tick_calls: 0,
    sss_host_draw_calls: 0})) {
    const different = compareExpectedField(actual[key], value, `pair.${key}`);
    if (different) return different;
  }
  if (actual.error !== null && actual.error !== '')
    return mismatch('pair.error', null, actual.error, 'pre-comparison pair contains an error');
  if (!Number.isSafeInteger(actual.world_generation) || actual.world_generation <= 0)
    return mismatch('pair.world_generation', 'positive generation', actual.world_generation);
  if (!Number.isSafeInteger(actual.audio_generation) || actual.audio_generation < 0)
    return mismatch('pair.audio_generation', 'nonnegative generation', actual.audio_generation);
  for (const key of ['audio_render_calls_before_rebuild', 'audio_render_frames_before_rebuild',
    'audio_render_calls_after_sss_enter', 'audio_render_frames_after_sss_enter'])
    if (!Number.isSafeInteger(actual[key]) || actual[key] < 0)
      return mismatch(`pair.${key}`, 'nonnegative integer', actual[key]);
  if (actual.audio_render_calls_after_sss_enter < actual.audio_render_calls_before_rebuild ||
      actual.audio_render_frames_after_sss_enter < actual.audio_render_frames_before_rebuild)
    return mismatch('pair.audio_render_counters', 'monotonic counters',
      [actual.audio_render_calls_before_rebuild, actual.audio_render_frames_before_rebuild,
        actual.audio_render_calls_after_sss_enter, actual.audio_render_frames_after_sss_enter]);
  if (!Object.hasOwn(actual, 'session_phase') ||
      !(typeof actual.session_phase === 'string' || Number.isSafeInteger(actual.session_phase)))
    return mismatch('pair.session_phase', 'present phase', actual.session_phase);

  // Native pair statuses are captured before this synchronous comparator
  // approves it. Compare entry completely before inspecting returned state.
  const entry = actualSssNoteMismatch(actual.entry, expected.expected_entry, 'sss_entry', actual);
  if (entry) return entry;
  const returned = actualSssNoteMismatch(actual.returned, expected.expected_return,
    'sss_return', actual);
  if (returned) return returned;
  if (actual.entry.session_phase !== actual.returned.session_phase)
    return mismatch('returned.session_phase', actual.entry.session_phase,
      actual.returned.session_phase, 'constructor notes crossed session phases');
  if (actual.entry.session_ticks !== actual.returned.session_ticks)
    return mismatch('returned.session_ticks', actual.entry.session_ticks,
      actual.returned.session_ticks, 'constructor ran a host session tick');
  if (actual.entry.source_scene !== actual.returned.source_scene)
    return mismatch('returned.source_scene', actual.entry.source_scene,
      actual.returned.source_scene, 'constructor notes crossed source scenes');
  return null;
}

// One entry-first comparison for the passive original SSS OnEnter constructor
// pair. The native callback receives actual JSON only; expected data remains
// in the browser and is not allocated or written into WASM memory.
export function createFirstSssConstructorPairComparator(expectedPair) {
  const expected = validateFirstSssExpected(expectedPair);
  let attempted = false;
  let approved = false;
  let failed = false;
  let actualPair = null;
  let firstMismatch = null;
  let error = null;
  const compare = (phase, actualJson) => {
    if (attempted) {
      failed = true;
      firstMismatch ||= mismatch('callback', 'exactly one call', phase,
        'SSS pair comparator was called more than once');
      return false;
    }
    attempted = true;
    if (phase !== 'sss_pair' || typeof actualJson !== 'string') {
      failed = true;
      firstMismatch = mismatch('callback', 'sss_pair JSON string', phase,
        'callback phase or payload type differs');
      return false;
    }
    try { actualPair = JSON.parse(actualJson); }
    catch (cause) {
      error = String(cause?.message || cause).slice(0, 1000);
      failed = true;
      firstMismatch = mismatch('pair', 'valid JSON object', null, 'malformed native JSON');
      return false;
    }
    firstMismatch = actualFirstSssPairMismatch(actualPair, expected);
    approved = firstMismatch === null;
    failed = !approved;
    return approved;
  };
  return Object.freeze({compare, status: () => Object.freeze({attempted, approved, failed,
    first_mismatch: firstMismatch, actual_pair: actualPair, error})});
}
