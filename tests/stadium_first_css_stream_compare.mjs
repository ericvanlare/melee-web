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
// Native PlayerInitData serialization also exposes its defined xB byte. The
// retained source/player decoder does not assign this byte a semantic name, so
// it is validated as a native byte but deliberately excluded from equality.
const SSS_NATIVE_UNPAIRED_PLAYER_KEYS = Object.freeze(['xB']);
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
    const nativeKeys = [...SSS_PLAYER_KEYS, ...SSS_NATIVE_UNPAIRED_PLAYER_KEYS];
    if (!exactOwnKeys(players[index], nativeKeys))
      return mismatch(`${prefix}.sss.vs.start.players[${index}]`, 'native player object',
        players[index], 'native player fields differ from the typed source schema');
    for (const key of SSS_NATIVE_UNPAIRED_PLAYER_KEYS) {
      const value = players[index][key];
      if (!Number.isInteger(value) || value < 0 || value > 255)
        return mismatch(`${prefix}.sss.vs.start.players[${index}].${key}`,
          'unsigned byte (unpaired)', value, 'native unpaired field is outside byte range');
    }
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

const FIRST_SSS_TICK_STREAM_SHA256 = FIRST_SSS_STREAM_SHA256;
const FIRST_SSS_TICK_STATUS_SHA256 = FIRST_SSS_STATUS_SHA256;
const FIRST_SSS_TICK_EXPECTED_KEYS = Object.freeze([
  'schema', 'version', 'scope', 'provenance', 'source_scheduler_end', 'input_bundle',
  'comparison_fields', 'native_protocol_requirements', 'whole_session_equivalent',
  'source_admission',
]);
const FIRST_SSS_TICK_SOURCE_INVENTORY = Object.freeze([
  {tag: 2, flags: 0, address: 0x804C1F78, size: 0x0C},
  {tag: 17, flags: 0, address: 0x80479D30, size: 6},
  {tag: 19, flags: 0, address: 0x804D5F94, size: 4},
  {tag: 20, flags: 0, address: 0x804D5F90, size: 4},
  {tag: 21, flags: 0, address: 0x804C1F84, size: 0x358},
  {tag: 30, flags: 0, address: 0x80479D58, size: 4},
  {tag: 36, flags: 0, address: 0x8045BF28, size: 2},
  {tag: 37, flags: 0, address: 0x8045BF2A, size: 2},
  {tag: 40, flags: 0, address: 0x803DD9C4, size: 1},
]);
const FIRST_SSS_TICK_ROUTE = Object.freeze({current_game_mode: 2,
  previous_game_mode: 1, current_scene_index: 1, previous_scene_index: 0});
const FIRST_SSS_TICK_PORT_STATUS = Object.freeze([
  '0000000000000000000000', '0000000000000000000000',
  '00000000000000000000ff', '00000000000000000000ff',
]);
const FIRST_SSS_TICK_COMPARISON_FIELDS = Object.freeze([
  'scheduler_end.scene_frame', 'scheduler_end.scene_kind',
  'scheduler_end.pad_state_hex', 'scheduler_end.random_seed_hex',
  'scheduler_end.scene_routing_getters', 'consumed_pad_status_hex',
]);
const FIRST_SSS_TICK_OWNER_KEYS = Object.freeze(SSS_OWNER_KEYS);
const FIRST_SSS_TICK_PROTOCOL = Object.freeze({
  scheduler_sample_scene_frame: 0, post_host_frame_after_clock_post: 1,
  clock_post_succeeded: true, tick_result: 1, transition_requested: false,
  host_tick_calls: 1, host_draw_calls: 0, all_owners_true: true,
});

function sameJsonValue(actual, expected) {
  if (actual === expected) return true;
  if (Array.isArray(actual) || Array.isArray(expected))
    return Array.isArray(actual) && Array.isArray(expected) &&
      actual.length === expected.length && actual.every((value, index) =>
        sameJsonValue(value, expected[index]));
  if (!plainObject(actual) || !plainObject(expected)) return false;
  const actualKeys = Object.keys(actual).sort();
  const expectedKeys = Object.keys(expected).sort();
  return actualKeys.join('|') === expectedKeys.join('|') &&
    actualKeys.every(key => sameJsonValue(actual[key], expected[key]));
}

function validateFirstSssConsumedTickExpected(expected) {
  const fail = () => { throw new TypeError('SSS tick comparison requires the exact retained row1604/1608 bundle'); };
  if (!exactOwnKeys(expected, FIRST_SSS_TICK_EXPECTED_KEYS) ||
      expected.schema !== 'melee-web-stadium-first-sss-consumed-pad-tick-diagnostic' ||
      expected.version !== 1 || expected.scope !== 'one consumed SSS PAD and its scheduler-end SourceTick only' ||
      expected.whole_session_equivalent !== false || expected.source_admission !== false ||
      !plainObject(expected.provenance) || !plainObject(expected.source_scheduler_end) ||
      !plainObject(expected.source_scheduler_end.expected) || !plainObject(expected.input_bundle)) fail();
  const provenance = expected.provenance;
  if (!sameJsonValue(provenance, {
    observer_bytes: 4397889, observer_sha256: FIRST_SSS_TICK_STREAM_SHA256,
    observer_status_bytes: 515, observer_status_sha256: FIRST_SSS_TICK_STATUS_SHA256,
    consumed_pad_sequence: 1604, scheduler_end_sequence: 1608,
    scheduler_end_pc: '0x80390eb4', source_tick: 0, draw_ordinal: 149,
    original_source_frame: 0,
  })) fail();
  const source = expected.source_scheduler_end;
  if (!Array.isArray(source.source_slice_inventory) ||
      !sameJsonValue(source.source_slice_inventory, FIRST_SSS_TICK_SOURCE_INVENTORY) ||
      !exactOwnKeys(source.source_slices_hex, FIRST_SSS_TICK_SOURCE_INVENTORY.map(
        item => `${item.tag}:${item.flags}`)) || source.scene_routing_raw_hex !== '020201010000' ||
      source.rng_pointer_hex !== '804d5f90' || source.setup_profile_verified_by_observer !== false)
    fail();
  for (const item of FIRST_SSS_TICK_SOURCE_INVENTORY) {
    const raw = source.source_slices_hex[`${item.tag}:${item.flags}`];
    if (!validHex(raw, item.size)) fail();
  }
  const raw = source.source_slices_hex;
  const wanted = source.expected;
  if (raw['17:0'] !== source.scene_routing_raw_hex || raw['30:0'] !== '00000000' ||
      raw['40:0'] !== '09' || raw['19:0'] !== source.rng_pointer_hex ||
      raw['20:0'] !== wanted.random_seed_hex ||
      !validHex(wanted.pad_state_hex, 822) || wanted.scene_frame !== 0 || wanted.scene_kind !== 9 ||
      wanted.random_seed_hex !== '3bb84c53' ||
      !exactRoute(wanted.scene_routing_getters, FIRST_SSS_TICK_ROUTE) ||
      !sameJsonValue(wanted.consumed_pad_status_hex, FIRST_SSS_TICK_PORT_STATUS) ||
      !sameJsonValue(wanted.required_owners,
        Object.fromEntries(FIRST_SSS_TICK_OWNER_KEYS.map(key => [key, true])))) fail();
  const input = expected.input_bundle;
  if (!sameJsonValue(input, {
    magic_hex: '5354433153535331', version: 1, bytes: 96,
    sha256: input.sha256, contains_expected_state: false,
    port_status_hex: FIRST_SSS_TICK_PORT_STATUS,
  }) || !/^[0-9a-f]{64}$/.test(input.sha256)) fail();
  if (!sameJsonValue(expected.comparison_fields, FIRST_SSS_TICK_COMPARISON_FIELDS) ||
      !sameJsonValue(expected.native_protocol_requirements, FIRST_SSS_TICK_PROTOCOL)) fail();
  const route = {
    current_game_mode: Number.parseInt(raw['17:0'].slice(0, 2), 16),
    previous_game_mode: Number.parseInt(raw['17:0'].slice(4, 6), 16),
    current_scene_index: Number.parseInt(raw['17:0'].slice(6, 8), 16),
    previous_scene_index: Number.parseInt(raw['17:0'].slice(8, 10), 16),
  };
  if (!exactRoute(route, wanted.scene_routing_getters)) fail();
  return expected;
}

export function validateFirstSssConsumedTickExpectedSource(expected) {
  return validateFirstSssConsumedTickExpected(expected);
}

function validateFirstSssConsumedTickPair(pair) {
  if (!plainObject(pair) || pair.complete !== true || pair.compared !== true ||
      pair.failed !== false || pair.host_entered !== true || pair.session_phase !== 3 ||
      pair.sss_host_tick_calls !== 0 || pair.sss_host_draw_calls !== 0 ||
      !Number.isSafeInteger(pair.world_generation) || pair.world_generation <= 0 ||
      !Number.isSafeInteger(pair.audio_generation) || pair.audio_generation < 0)
    throw new TypeError('SSS tick requires an approved completed constructor pair');
  return pair;
}

// The source sample is the exact frame-zero scheduler-end row1608. The
// post-host frame-one counter is checked separately by the caller; it is not
// normalized into this original comparison.
export function createFirstSssConsumedPadTickComparator(expectedTick, constructorPair) {
  const expected = validateFirstSssConsumedTickExpected(expectedTick);
  const pair = validateFirstSssConsumedTickPair(constructorPair);
  let attempted = false;
  let approved = false;
  let failed = false;
  let actual = null;
  let firstMismatch = null;
  let error = null;
  const compare = (phase, actualJson) => {
    if (attempted) {
      failed = true;
      firstMismatch ||= mismatch('callback', 'exactly one call', phase,
        'SSS consumed-tick comparator was called more than once');
      return false;
    }
    attempted = true;
    if (phase !== 'scheduler_end' || typeof actualJson !== 'string') {
      failed = true;
      firstMismatch = mismatch('callback', 'scheduler_end JSON string', phase,
        'callback phase or payload type differs');
      return false;
    }
    try { actual = JSON.parse(actualJson); }
    catch (cause) {
      error = String(cause?.message || cause).slice(0, 1000);
      failed = true;
      firstMismatch = mismatch('scheduler_end', 'valid JSON object', null,
        'malformed native JSON');
      return false;
    }
    const wanted = expected.source_scheduler_end.expected;
    const fields = [
      ['phase', 'scheduler_end'], ['source_scene', 2], ['scene_kind', wanted.scene_kind],
      ['scene_frame', wanted.scene_frame], ['random_seed_hex', wanted.random_seed_hex],
      ['pad_state_hex', wanted.pad_state_hex],
    ];
    for (const [key, value] of fields) {
      const different = compareExpectedField(actual?.[key], value, `scheduler_end.${key}`);
      if (different) { firstMismatch = different; failed = true; return false; }
    }
    if (!exactOwnKeys(actual, ['phase', 'source_scene', 'scene_kind', 'scene_frame',
      'random_seed_hex', 'pad_state_hex', 'scene_routing_getters', 'owners',
      'world_generation', 'audio_generation', 'consumed_pad_hex'])) {
      firstMismatch = mismatch('scheduler_end', 'exact native tick snapshot fields', actual,
        'native snapshot fields differ from the frozen interface');
      failed = true;
      return false;
    }
    if (!exactRoute(actual.scene_routing_getters, wanted.scene_routing_getters)) {
      firstMismatch = mismatch('scheduler_end.scene_routing_getters',
        wanted.scene_routing_getters, actual.scene_routing_getters,
        'four established source routing getters differ');
      failed = true;
      return false;
    }
    const consumed = wanted.consumed_pad_status_hex.join('');
    if (!validHex(actual.consumed_pad_hex, 44) || actual.consumed_pad_hex !== consumed) {
      firstMismatch = mismatch('scheduler_end.consumed_pad_hex', consumed,
        actual.consumed_pad_hex, 'four consumed PADStatus values differ');
      failed = true;
      return false;
    }
    if (!exactOwnKeys(actual.owners, FIRST_SSS_TICK_OWNER_KEYS) ||
        FIRST_SSS_TICK_OWNER_KEYS.some(key => actual.owners[key] !== true)) {
      firstMismatch = mismatch('scheduler_end.owners',
        Object.fromEntries(FIRST_SSS_TICK_OWNER_KEYS.map(key => [key, true])),
        actual.owners, 'required owner/liveness fields are not all true');
      failed = true;
      return false;
    }
    for (const [key, value] of [['world_generation', pair.world_generation],
      ['audio_generation', pair.audio_generation]]) {
      if (!Number.isSafeInteger(actual[key]) || actual[key] !== value) {
        firstMismatch = mismatch(`scheduler_end.${key}`, value, actual[key],
          'tick snapshot crossed the constructor pair owner generation');
        failed = true;
        return false;
      }
    }
    approved = true;
    return true;
  };
  return Object.freeze({compare, status: () => Object.freeze({attempted, approved, failed,
    first_mismatch: firstMismatch, actual, error})});
}

const FIRST_SSS_DRAW_SOURCE_INVENTORY = Object.freeze([
  {tag: 17, flags: 0, address: 0x80479d30, size: 6},
  {tag: 19, flags: 0, address: 0x804d5f94, size: 4},
  {tag: 20, flags: 0, address: 0x804d5f90, size: 4},
  {tag: 21, flags: 0, address: 0x804c1f84, size: 0x358},
  {tag: 30, flags: 0, address: 0x80479d58, size: 4},
  {tag: 36, flags: 0, address: 0x8045bf28, size: 2},
  {tag: 37, flags: 0, address: 0x8045bf2a, size: 2},
  {tag: 40, flags: 0, address: 0x803dd9c4, size: 1},
  {tag: 2, flags: 0, address: 0x804c1f78, size: 0x0c},
]);
const FIRST_SSS_EXIT_SOURCE_INVENTORY = FIRST_SSS_SOURCE_INVENTORY;
const FIRST_SSS_SELECTION_SOURCE_INVENTORY = Object.freeze([
  {tag: 1, flags: 0, address: 0x804ee74c, size: 0x30},
  {tag: 27, flags: 0, address: 0x804ee724, size: 4},
  {tag: 2, flags: 0, address: 0x804c1f78, size: 0x0c},
  {tag: 21, flags: 0, address: 0x804c1f84, size: 0x358},
  {tag: 22, flags: 0, address: 0x804d7420, size: 4},
  {tag: 23, flags: 0, address: 0x804a7f98, size: 4},
  {tag: 17, flags: 0, address: 0x80479d30, size: 6},
  {tag: 40, flags: 0, address: 0x803dd9c4, size: 1},
  {tag: 41, flags: 0, address: 0x804d6cae, size: 1},
  {tag: 42, flags: 0, address: 0x803f08d3, size: 1},
]);
const FIRST_SSS_SOURCE_STABILITY_BASIS = Object.freeze({
  evidence: 'root-sss-selection-stability-review-v1/review.json',
  sha256: 'f7fefd1627ce7ce2b51dae3ea10628337ace2910541ad83b7fb23e1837856063',
  claim: 'The pinned original scene/mode exit path has no writer to the SSS selected index or authored table-kind before source row2427.',
});
const FIRST_SSS_BOUNDARY_SOURCE_KEYS = Object.freeze([
  'event', 'sequence', 'source_tick', 'draw_ordinal', 'boundary', 'phase',
  'pc', 'word', 'lr', 'argument', 'source_slice_inventory', 'source_slices_hex',
  'scene_routing_raw_hex', 'rng_pointer_hex',
]);
const FIRST_SSS_EXIT_SOURCE_KEYS = Object.freeze([
  'event', 'sequence', 'source_tick', 'draw_ordinal', 'boundary', 'phase',
  'pc', 'word', 'lr', 'argument', 'setup_receipt_sha256',
  'setup_profile_verified_by_observer', 'source_slice_inventory',
  'source_slices_hex', 'scene_routing_raw_hex', 'rng_pointer_hex',
]);
const FIRST_SSS_SELECTION_SOURCE_KEYS = Object.freeze([
  'event', 'sequence', 'source_tick', 'draw_ordinal', 'boundary', 'phase',
  'pc', 'word', 'lr', 'argument', 'scene_kind', 'scene_routing_raw_hex',
  'source_slice_inventory', 'source_slices_hex',
  'selected_stage_is_a_later_source_witness', 'not_paired_with_row2426_exit_note',
]);
const FIRST_SSS_DRAW_FIELDS = Object.freeze([
  'scene_frame', 'scene_kind', 'pad_state_hex', 'random_seed_hex',
  'scene_routing_getters',
]);
const FIRST_SSS_PREFIX_OWNER_KEYS = SSS_OWNER_KEYS;
const FIRST_SSS_PREFIX_ACTUAL_KEYS = Object.freeze([
  'phase', 'source_scene', 'scene_kind', 'scene_frame', 'random_seed_hex',
  'pad_state_hex', 'scene_routing_getters', 'owners', 'world_generation',
  'audio_generation', 'input_index', 'input_ordinal', 'consumed_pad_sequence',
]);
const FIRST_SSS_PREFIX_SCHEDULER_KEYS = Object.freeze([
  ...FIRST_SSS_PREFIX_ACTUAL_KEYS, 'consumed_pad_hex',
]);
const FIRST_SSS_PREFIX_EXIT_COMPARISON_FIELDS = Object.freeze([
  'scene_frame', 'scene_kind', 'pad_state_hex', 'random_seed_hex',
  'scene_routing_getters', 'sss.header.start_game', 'sss.vs.start.rules',
  'sss.vs.start.players[0:4]',
]);
const FIRST_SSS_DRAW_NATIVE_PROTOCOL = Object.freeze({
  host_draw_calls: 1, host_tick_calls: 0, aurora_begin_calls: 1,
  aurora_end_calls: 1, frame_end_returned: true, all_owners_true: true,
  audio_render_calls_delta: 0, audio_render_frames_delta: 0,
  audio_phase_unchanged: true,
});
const FIRST_SSS_PREFIX_TICK_PROTOCOL = Object.freeze({
  ticked_sample_count: 123, transition_requested_sample_count: 1,
  last_tick_result: 3, final_transition_request_retained: true,
  host_ticks: 124, host_draws: 124, ordinary_draws: 123,
  checked_pending_sss_draws: 1, extra_inputs: 0, extra_ticks: 0,
  automatic_leave: false,
  audio_render_calls_per_approved_tick: 1,
  audio_frames_per_tick_numerator: 32000,
  audio_frames_per_tick_denominator: 60,
  audio_phase_modulus: 60,
  audio_render_after_approved_tick_before_draw: true,
  no_audio_render_after_refused_tick: true,
});

export function firstSssAudioAccounting(actual, renderedTicks) {
  const result = {valid: false, rendered_ticks: renderedTicks, before: null, after: null,
    calls_delta: null, frames_delta: null, error: null};
  const fail = error => { result.error = error; return Object.freeze(result); };
  if (!plainObject(actual) || !Number.isSafeInteger(renderedTicks) || renderedTicks < 0)
    return fail('audio snapshot or rendered-tick count is invalid');
  const keys = ['audio_render_calls_before', 'audio_render_calls_after',
    'audio_render_frames_before', 'audio_render_frames_after',
    'audio_phase_before', 'audio_phase_after'];
  if (keys.some(key => !Number.isSafeInteger(actual[key]) || actual[key] < 0))
    return fail('audio counters and phase must be nonnegative integers');
  const [callsBefore, callsAfter, framesBefore, framesAfter, phaseBefore, phaseAfter] =
    keys.map(key => actual[key]);
  if (phaseBefore >= 60 || phaseAfter >= 60)
    return fail('audio_phase is outside its 32000/60 carry range');
  result.before = {calls: callsBefore, frames: framesBefore, phase: phaseBefore};
  result.after = {calls: callsAfter, frames: framesAfter, phase: phaseAfter};
  result.calls_delta = callsAfter - callsBefore;
  result.frames_delta = framesAfter - framesBefore;
  if (callsAfter < callsBefore || framesAfter < framesBefore)
    return fail('audio counters moved backwards');
  const total = phaseBefore + 32000 * renderedTicks;
  const expectedFrames = Math.floor(total / 60);
  const expectedPhase = total % 60;
  if (result.calls_delta !== renderedTicks)
    return fail(`audio render-call delta ${result.calls_delta} != approved tick count ${renderedTicks}`);
  if (result.frames_delta !== expectedFrames)
    return fail(`audio frame delta ${result.frames_delta} != 32000/60 carry result ${expectedFrames}`);
  if (phaseAfter !== expectedPhase)
    return fail(`audio_phase after ${phaseAfter} != preserved carry ${expectedPhase}`);
  result.valid = true;
  return Object.freeze(result);
}

function firstSssSourceRowIsPinned(source, sequence, boundary, pc, word, drawOrdinal,
    sourceTick, expectedSceneFrame = 1, lr = 0x801a5048) {
  if (!exactOwnKeys(source, FIRST_SSS_BOUNDARY_SOURCE_KEYS) || source.sequence !== sequence ||
      source.event !== 'boundary' || source.phase !== null || source.boundary !== boundary ||
      source.pc !== pc || source.word !== word || source.lr !== lr ||
      source.draw_ordinal !== drawOrdinal || source.source_tick !== sourceTick ||
      source.argument !== null ||
      !Array.isArray(source.source_slice_inventory) || !plainObject(source.source_slices_hex))
    return false;
  if (!sourceInventoryMatches(source.source_slice_inventory, FIRST_SSS_DRAW_SOURCE_INVENTORY) ||
      !sourceSlicesMatch(source.source_slices_hex, FIRST_SSS_DRAW_SOURCE_INVENTORY)) return false;
  const raw = source.source_slices_hex;
  return raw['17:0'] === '020201010000' && raw['19:0'] === '804d5f90' &&
    raw['20:0'] === '3bb84c53' && raw['30:0'] === expectedSceneFrame.toString(16).padStart(8, '0') &&
    raw['40:0'] === '09' && source.scene_routing_raw_hex === raw['17:0'] &&
    source.rng_pointer_hex === raw['19:0'];
}

function sourceInventoryMatches(actual, expected) {
  if (!Array.isArray(actual) || actual.length !== expected.length) return false;
  const left = [...actual].sort((a, b) => a.tag - b.tag || a.flags - b.flags);
  const right = [...expected].sort((a, b) => a.tag - b.tag || a.flags - b.flags);
  return left.every((item, index) => plainObject(item) &&
    item.tag === right[index].tag && item.flags === right[index].flags &&
    item.address === right[index].address && item.size === right[index].size);
}

function sourceSlicesMatch(actual, expected) {
  if (!plainObject(actual) || !exactOwnKeys(actual, expected.map(item => `${item.tag}:0`)))
    return false;
  return expected.every(item => validHex(actual[`${item.tag}:0`], item.size));
}

export function validateFirstSssDrawExpectedSource(expected) {
  const fail = () => { throw new TypeError('first SSS draw requires exact retained rows1609/1610'); };
  if (!plainObject(expected) || expected.schema !== 'melee-web-stadium-first-sss-draw-diagnostic' ||
      expected.version !== 1 || expected.scope !== 'original first SSS DrawEnter1609 through DrawReturn1610 only' ||
      expected.source_admission !== false || expected.whole_session_equivalent !== false ||
      !plainObject(expected.provenance) || !plainObject(expected.expected_draw_enter) ||
      !plainObject(expected.expected_draw_return) || !plainObject(expected.source_draw_enter) ||
      !plainObject(expected.source_draw_return)) fail();
  const provenance = expected.provenance;
  if (provenance.observer_bytes !== 4397889 || provenance.observer_sha256 !== FIRST_SSS_STREAM_SHA256 ||
      provenance.observer_status_bytes !== 515 || provenance.observer_status_sha256 !== FIRST_SSS_STATUS_SHA256 ||
      provenance.draw_enter_sequence !== 1609 || provenance.draw_return_sequence !== 1610 ||
      provenance.source_tick !== 1 || provenance.draw_ordinal !== 149 ||
      provenance.scene_frame !== 1 || provenance.scene_kind !== 9 ||
      provenance.setup_profile_verified_by_observer !== false ||
      !sameJsonValue(expected.comparison_fields, FIRST_SSS_DRAW_FIELDS) ||
      !sameJsonValue(expected.native_protocol_requirements, FIRST_SSS_DRAW_NATIVE_PROTOCOL)) fail();
  for (const [note, sequence, boundary, pc] of [
    [expected.expected_draw_enter, 1609, 'draw_enter', 0x80390fc0],
    [expected.expected_draw_return, 1610, 'draw_return', 0x80391040],
  ]) {
    if (!exactOwnKeys(note, ['phase', 'scene_frame', 'scene_kind', 'pad_state_hex',
        'random_seed_hex', 'scene_routing_getters']) || note.phase !== boundary ||
        note.scene_frame !== 1 || note.scene_kind !== 9 ||
        !validHex(note.pad_state_hex, 822) || note.random_seed_hex !== '3bb84c53' ||
        !exactRoute(note.scene_routing_getters, {current_game_mode: 2,
          previous_game_mode: 1, current_scene_index: 1, previous_scene_index: 0})) fail();
    const source = boundary === 'draw_enter' ? expected.source_draw_enter : expected.source_draw_return;
    if (!firstSssSourceRowIsPinned(source, sequence, boundary, pc, null, 149, 1, 1)) fail();
  }
  for (const key of FIRST_SSS_DRAW_FIELDS)
    if (!sameJsonValue(expected.expected_draw_enter[key], expected.expected_draw_return[key])) fail();
  return expected;
}

function approvedFirstSssPrerequisites(pair, tick) {
  return plainObject(pair) && pair.complete === true && pair.compared === true &&
    pair.failed === false && pair.host_entered === true &&
    pair.sss_host_tick_calls === 0 && pair.sss_host_draw_calls === 0 &&
    Number.isSafeInteger(pair.world_generation) && pair.world_generation > 0 &&
    Number.isSafeInteger(pair.audio_generation) && pair.audio_generation >= 0 &&
    plainObject(tick) && tick.complete === true && tick.compared === true &&
    tick.captured === true && tick.failed === false && tick.host_tick_calls === 1 &&
    tick.host_draw_calls === 0 && tick.clock_post_succeeded === true &&
    tick.tick_result === 1 && tick.transition_requested === false &&
    tick.post_host_frame_captured === true && tick.post_host_frame === 1;
}

function firstSssActualSnapshotMismatch(actual, expected, phase, pair, prefix, withInput = false) {
  const wantedKeys = withInput ?
    (phase === 'scheduler_end' ? FIRST_SSS_PREFIX_SCHEDULER_KEYS : FIRST_SSS_PREFIX_ACTUAL_KEYS) :
    ['phase', 'source_scene', 'scene_kind', 'scene_frame', 'random_seed_hex', 'pad_state_hex',
      'scene_routing_getters', 'owners', 'world_generation', 'audio_generation'];
  if (!exactOwnKeys(actual, wantedKeys))
    return mismatch(prefix, wantedKeys, actual, 'native snapshot fields differ from the frozen interface');
  for (const [key, value] of Object.entries({phase, source_scene: 2,
    scene_kind: expected.scene_kind, scene_frame: expected.scene_frame,
    random_seed_hex: expected.random_seed_hex, pad_state_hex: expected.pad_state_hex})) {
    const difference = compareExpectedField(actual[key], value, `${prefix}.${key}`);
    if (difference) return difference;
  }
  if (!exactRoute(actual.scene_routing_getters, expected.scene_routing_getters))
    return mismatch(`${prefix}.scene_routing_getters`, expected.scene_routing_getters,
      actual.scene_routing_getters, 'four established source routing getters differ');
  if (!exactOwnKeys(actual.owners, FIRST_SSS_PREFIX_OWNER_KEYS) ||
      FIRST_SSS_PREFIX_OWNER_KEYS.some(key => actual.owners[key] !== true))
    return mismatch(`${prefix}.owners`, Object.fromEntries(FIRST_SSS_PREFIX_OWNER_KEYS.map(k => [k, true])),
      actual.owners, 'required owner/liveness fields are not all true');
  if (!Number.isSafeInteger(actual.world_generation) || actual.world_generation !== pair.world_generation ||
      !Number.isSafeInteger(actual.audio_generation) || actual.audio_generation !== pair.audio_generation)
    return mismatch(`${prefix}.owner_generation`, [pair.world_generation, pair.audio_generation],
      [actual.world_generation, actual.audio_generation], 'snapshot crossed the constructor-pair owners');
  return null;
}

export function createFirstSssDrawComparator(expectedDraw, constructorPair, consumedTick) {
  const expected = validateFirstSssDrawExpectedSource(expectedDraw);
  if (!approvedFirstSssPrerequisites(constructorPair, consumedTick))
    throw new TypeError('first SSS draw requires approved constructor-pair and consumed-tick prerequisites');
  const phases = ['draw_enter', 'draw_return'];
  let index = 0, failed = false, firstMismatch = null;
  const rows = [];
  const compare = (phase, actualJson) => {
    const row = {phase, actual: null, approved: false};
    rows.push(row);
    if (failed || phase !== phases[index] || typeof actualJson !== 'string') {
      failed = true;
      firstMismatch ||= mismatch('callback.phase', phases[index] ?? 'no further callback', phase,
        'first SSS draw callback was repeated or out of order');
      return false;
    }
    try { row.actual = JSON.parse(actualJson); }
    catch (error) {
      failed = true; firstMismatch = mismatch(`draw.${phase}`, 'valid JSON', null, String(error));
      return false;
    }
    firstMismatch = firstSssActualSnapshotMismatch(row.actual,
      expected[`expected_${phase}`], phase, constructorPair, `draw.${phase}`);
    if (firstMismatch) { failed = true; return false; }
    row.approved = true; index += 1;
    return true;
  };
  return Object.freeze({compare, status: () => Object.freeze({attempted: rows.length,
    approved: index === 2 && !failed, failed, next_phase: phases[index] ?? null,
    first_mismatch: firstMismatch, rows})});
}

function validatePrefixInputRows(expected, inputRows) {
  if (!Array.isArray(expected.records) || expected.records.length !== 124 ||
      !Array.isArray(inputRows) || inputRows.length !== 124) return false;
  let previous = 1610;
  for (let index = 0; index < 124; index += 1) {
    const record = expected.records[index], input = inputRows[index];
    if (!plainObject(record) || !plainObject(input) || record.input_index !== index ||
        record.input_ordinal !== index + 1 || input.input_index !== index ||
        record.consumed_pad_sequence !== input.consumed_pad_sequence ||
        record.scheduler_end_sequence !== input.scheduler_end_sequence ||
        record.draw_enter_sequence !== input.draw_enter_sequence ||
        record.draw_return_sequence !== input.draw_return_sequence ||
        record.consumed_pad_sequence <= previous ||
        record.consumed_pad_sequence >= record.scheduler_end_sequence ||
        record.scheduler_end_sequence >= record.draw_enter_sequence ||
        record.draw_enter_sequence >= record.draw_return_sequence ||
        record.draw_return_sequence <= previous ||
        !sameJsonValue(record.consumed_pad_status_hex, input.port_status_hex) ||
        !exactOwnKeys(record.scheduler_end, ['phase', 'scene_frame', 'scene_kind',
          'pad_state_hex', 'random_seed_hex', 'scene_routing_getters', 'consumed_pad_hex']) ||
        !exactOwnKeys(record.draw_enter, ['phase', 'scene_frame', 'scene_kind', 'pad_state_hex',
          'random_seed_hex', 'scene_routing_getters']) ||
        !exactOwnKeys(record.draw_return, ['phase', 'scene_frame', 'scene_kind', 'pad_state_hex',
          'random_seed_hex', 'scene_routing_getters'])) return false;
    const consumeHex = record.consumed_pad_status_hex.join('');
    if (record.scheduler_end.consumed_pad_hex !== consumeHex ||
        record.scheduler_end.phase !== 'scheduler_end' || record.draw_enter.phase !== 'draw_enter' ||
        record.draw_return.phase !== 'draw_return' || record.scheduler_end.scene_frame !== index + 1 ||
        record.draw_enter.scene_frame !== index + 2 || record.draw_return.scene_frame !== index + 2 ||
        record.scheduler_end.scene_kind !== 9 || record.draw_enter.scene_kind !== 9 ||
        record.draw_return.scene_kind !== 9 || record.scheduler_end.random_seed_hex !== '3bb84c53' ||
        record.draw_enter.random_seed_hex !== '3bb84c53' || record.draw_return.random_seed_hex !== '3bb84c53' ||
        !validHex(record.scheduler_end.pad_state_hex, 822) ||
        !validHex(record.draw_enter.pad_state_hex, 822) || !validHex(record.draw_return.pad_state_hex, 822) ||
        !exactRoute(record.scheduler_end.scene_routing_getters,
          {current_game_mode: 2, previous_game_mode: 1, current_scene_index: 1, previous_scene_index: 0}) ||
        !sameJsonValue(record.scheduler_end.scene_routing_getters, record.draw_enter.scene_routing_getters) ||
        !sameJsonValue(record.draw_enter.scene_routing_getters, record.draw_return.scene_routing_getters) ||
        !sameJsonValue(record.draw_enter.pad_state_hex, record.draw_return.pad_state_hex)) return false;
    if (!plainObject(record.source_rows) ||
        !firstSssSourceRowIsPinned(record.source_rows.scheduler_end,
          record.scheduler_end_sequence, 'source_tick', 0x80390eb4, null,
          150 + index, index + 1, index + 1, 0x801a4fa4) ||
        !firstSssSourceRowIsPinned(record.source_rows.draw_enter,
          record.draw_enter_sequence, 'draw_enter', 0x80390fc0, null,
          150 + index, index + 2, index + 2) ||
        !firstSssSourceRowIsPinned(record.source_rows.draw_return,
          record.draw_return_sequence, 'draw_return', 0x80391040, null,
          150 + index, index + 2, index + 2)) return false;
    previous = record.draw_return_sequence;
  }
  return expected.records[0].consumed_pad_sequence === 1612 &&
    expected.records[123].consumed_pad_sequence === 2420 &&
    expected.records[123].draw_return_sequence === 2424;
}

export function validateFirstSssPrefixExpectedSource(expected, inputRows) {
  const fail = () => { throw new TypeError('first SSS prefix requires exact retained rows1609–2427'); };
  if (!plainObject(expected) || expected.schema !== 'melee-web-stadium-first-sss-prefix-diagnostic' ||
      expected.version !== 1 || expected.scope !== '124 exact original SSS consumed-PAD/tick/draw samples through passive SSS exit' ||
      expected.source_admission !== false || expected.whole_session_equivalent !== false ||
      expected.prepared_output_unpaired !== true || !plainObject(expected.provenance) ||
      !plainObject(expected.input_bundle) || !plainObject(expected.exit_note) ||
      !plainObject(expected.selected_stage_source_witness)) fail();
  const p = expected.provenance;
  if (p.observer_bytes !== 4397889 || p.observer_sha256 !== FIRST_SSS_STREAM_SHA256 ||
      p.observer_status_bytes !== 515 || p.observer_status_sha256 !== FIRST_SSS_STATUS_SHA256 ||
      p.first_draw_enter_sequence !== 1609 || p.first_draw_return_sequence !== 1610 ||
      p.first_prefix_consume_sequence !== 1612 || p.last_prefix_consume_sequence !== 2420 ||
      p.sample_count !== 124 || p.exit_note_sequence !== 2426 ||
      p.selected_stage_source_witness_sequence !== 2427 ||
      p.setup_profile_verified_by_observer !== false ||
      !sameJsonValue(expected.comparison_fields, ['input_index', 'input_ordinal',
        'consumed_pad_sequence', 'consumed_pad_status_hex', 'scheduler_end', 'draw_enter', 'draw_return']) ||
      !sameJsonValue(expected.tick_protocol, FIRST_SSS_PREFIX_TICK_PROTOCOL) ||
      !validatePrefixInputRows(expected, inputRows)) fail();
  const bundle = expected.input_bundle;
  if (!sameJsonValue(bundle, {magic_hex: '5354433153535350', version: 1, bytes: 7488,
      sha256: bundle.sha256, sample_count: 124, record_bytes: 60, contains_expected_state: false}) ||
      !/^[0-9a-f]{64}$/.test(bundle.sha256)) fail();
  const exit = expected.exit_note;
  if (exit.sequence !== 2426 || exit.phase !== 'actual SSS scene OnExit return before VS SSS mode OnExit' ||
      !sameJsonValue(exit.comparison_fields, FIRST_SSS_PREFIX_EXIT_COMPARISON_FIELDS) ||
      !plainObject(exit.expected) || exit.expected.phase !== 'sss_exit' ||
      exit.expected.scene_frame !== 125 || exit.expected.scene_kind !== 9 ||
      exit.expected.random_seed_hex !== '3bb84c53' || !validHex(exit.expected.pad_state_hex, 822) ||
      !exactRoute(exit.expected.scene_routing_getters,
        {current_game_mode: 2, previous_game_mode: 1, current_scene_index: 1, previous_scene_index: 0}) ||
      !exactOwnKeys(exit.expected.sss?.header, ['start_game']) || exit.expected.sss.header.start_game !== 1 ||
      !plainObject(exit.expected.sss?.vs?.start?.rules) ||
      !exactOwnKeys(exit.expected.sss.vs.start.rules, SSS_RULE_KEYS) ||
      !Array.isArray(exit.expected.sss?.vs?.start?.players) ||
      exit.expected.sss.vs.start.players.length !== 4 ||
      exit.expected.sss.vs.start.players.some(player => !exactOwnKeys(player, SSS_PLAYER_KEYS)) ||
      !exactOwnKeys(exit.source, FIRST_SSS_EXIT_SOURCE_KEYS) ||
      exit.source.event !== 'progress' || exit.source.sequence !== 2426 ||
      exit.source.boundary !== null || exit.source.phase !== 'sss_exit' ||
      exit.source.pc !== '0x8025bbd0' || exit.source.word !== '0x4e800020' ||
      exit.source.lr !== '0x801a4124' || exit.source.argument !== '0x00000000' ||
      exit.source.source_tick !== 125 || exit.source.draw_ordinal !== 274 ||
      exit.source.setup_receipt_sha256 !== 'e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda' ||
      exit.source.setup_profile_verified_by_observer !== false ||
      !sourceInventoryMatches(exit.source.source_slice_inventory, FIRST_SSS_EXIT_SOURCE_INVENTORY) ||
      !sourceSlicesMatch(exit.source.source_slices_hex, FIRST_SSS_EXIT_SOURCE_INVENTORY) ||
      exit.source.source_slices_hex?.['35:0'] !== '01' ||
      exit.source.source_slices_hex?.['30:0'] !== '0000007d' ||
      exit.source.source_slices_hex?.['40:0'] !== '09' ||
      exit.source.source_slices_hex?.['20:0'] !== '3bb84c53' ||
      exit.source.scene_routing_raw_hex !== '020201010000' ||
      exit.source.rng_pointer_hex !== '804d5f90') fail();
  const selection = expected.selected_stage_source_witness;
  if (selection.sequence !== 2427 || selection.native_phase !== 'after VS SSS mode OnExit and before vs_mode_end' ||
      !sameJsonValue(selection.comparison_fields, ['index', 'kind']) ||
      !sameJsonValue(selection.selected_stage, {index: 18, kind: 3}) ||
      !sameJsonValue(selection.source_stability_basis, FIRST_SSS_SOURCE_STABILITY_BASIS) ||
      !exactOwnKeys(selection.source, FIRST_SSS_SELECTION_SOURCE_KEYS) ||
      selection.source.event !== 'boundary' || selection.source.sequence !== 2427 ||
      selection.source.source_tick !== 125 || selection.source.draw_ordinal !== 274 ||
      selection.source.boundary !== 'pad_poll' || selection.source.phase !== null ||
      selection.source.pc !== 0x8034dd8c || selection.source.lr !== 0x8034db8c ||
      selection.source.word !== null || selection.source.argument !== null ||
      selection.source.scene_kind !== 9 || selection.source.selected_stage_is_a_later_source_witness !== true ||
      selection.source.not_paired_with_row2426_exit_note !== true ||
      !sourceInventoryMatches(selection.source.source_slice_inventory,
        FIRST_SSS_SELECTION_SOURCE_INVENTORY) ||
      !sourceSlicesMatch(selection.source.source_slices_hex,
        FIRST_SSS_SELECTION_SOURCE_INVENTORY) ||
      selection.source.scene_routing_raw_hex !== selection.source.source_slices_hex?.['17:0'] ||
      selection.source.source_slices_hex?.['40:0'] !== '09' ||
      selection.source.source_slices_hex?.['41:0'] !== '12' ||
      selection.source.source_slices_hex?.['42:0'] !== '03') fail();
  return expected;
}

export function decodeFirstSssPrefixInputBundle(bytes) {
  const fail = () => { throw new TypeError('SSS prefix input must be the exact 7,488-byte STC1SSSP bundle'); };
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== 7488) fail();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const magic = [...bytes.subarray(0, 8)].map(value => String.fromCharCode(value)).join('');
  if (magic !== 'STC1SSSP' || view.getUint32(8, false) !== 1 ||
      [...bytes.subarray(12, 44)].map(value => value.toString(16).padStart(2, '0')).join('') !==
        FIRST_SSS_STREAM_SHA256 || view.getUint32(44, false) !== 124) fail();
  const records = [];
  let prior = 1610, offset = 48;
  for (let index = 0; index < 124; index += 1) {
    const sequences = [0, 4, 8, 12].map(position => view.getUint32(offset + position, false));
    const [consumed_pad_sequence, scheduler_end_sequence, draw_enter_sequence,
      draw_return_sequence] = sequences;
    if (!(prior < consumed_pad_sequence && consumed_pad_sequence < scheduler_end_sequence &&
        scheduler_end_sequence < draw_enter_sequence && draw_enter_sequence < draw_return_sequence)) fail();
    const port_status_hex = [];
    for (let port = 0; port < 4; port += 1) {
      const start = offset + 16 + port * 11;
      port_status_hex.push([...bytes.subarray(start, start + 11)]
        .map(value => value.toString(16).padStart(2, '0')).join(''));
    }
    records.push({input_index: index, input_ordinal: index + 1,
      consumed_pad_sequence, scheduler_end_sequence, draw_enter_sequence,
      draw_return_sequence, port_status_hex});
    prior = draw_return_sequence;
    offset += 60;
  }
  if (offset !== bytes.byteLength || records[0].consumed_pad_sequence !== 1612 ||
      records.at(-1).consumed_pad_sequence !== 2420 ||
      records.at(-1).draw_return_sequence !== 2424) fail();
  return Object.freeze(records);
}

export function createFirstSssPrefixComparator(expectedPrefix, inputRows,
    constructorPair, consumedTick, firstDrawObservation) {
  const expected = validateFirstSssPrefixExpectedSource(expectedPrefix, inputRows);
  if (!approvedFirstSssPrerequisites(constructorPair, consumedTick) ||
      firstDrawObservation?.complete !== true || firstDrawObservation?.compared !== true ||
      firstDrawObservation?.failed !== false || firstDrawObservation?.host_draw_calls !== 1 ||
      firstDrawObservation?.host_tick_calls !== 0 || firstDrawObservation?.aurora_begin_calls !== 1 ||
      firstDrawObservation?.aurora_end_calls !== 1 || firstDrawObservation?.frame_end_returned !== true)
    throw new TypeError('SSS prefix requires approved constructor, tick and first-draw prerequisites');
  const required = [];
  for (let index = 0; index < 124; index += 1)
    required.push(['scheduler_end', index], ['draw_enter', index], ['draw_return', index]);
  required.push(['sss_exit', null], ['selected_stage', null]);
  const rows = [];
  let next = 0, failed = false, firstMismatch = null;
  const ownerWorld = constructorPair.world_generation;
  const ownerAudio = constructorPair.audio_generation;
  const compare = (phase, actualJson) => {
    const wantedPhase = required[next]?.[0] ?? null;
    const row = {phase, actual: null, approved: false};
    rows.push(row);
    if (failed || phase !== wantedPhase || typeof actualJson !== 'string') {
      failed = true;
      firstMismatch ||= mismatch('callback.phase', wantedPhase, phase,
        'SSS prefix callback was repeated or out of order');
      return false;
    }
    try { row.actual = JSON.parse(actualJson); }
    catch (error) {
      failed = true; firstMismatch = mismatch(`prefix.${phase}`, 'valid JSON', null, String(error));
      return false;
    }
    const recordIndex = required[next][1];
    if (phase === 'selected_stage') {
      const wanted = expected.selected_stage_source_witness.selected_stage;
      if (!exactOwnKeys(row.actual, ['index', 'kind']) || row.actual.index !== wanted.index ||
          row.actual.kind !== wanted.kind)
        firstMismatch = mismatch('selected_stage', wanted, row.actual,
          'later row2427 selected-stage tuple differs');
    } else if (phase === 'sss_exit') {
      const actual = row.actual, wanted = expected.exit_note.expected;
      if (!exactOwnKeys(actual, SSS_NOTE_KEYS))
        firstMismatch = mismatch('sss_exit', [...SSS_NOTE_KEYS], actual,
          'native exit-note fields differ from the frozen interface');
      else {
        const common = firstSssActualSnapshotMismatch({
          phase: actual.phase, source_scene: actual.source_scene, scene_kind: actual.scene_kind,
          scene_frame: actual.scene_frame, random_seed_hex: actual.random_seed_hex,
          pad_state_hex: actual.pad_state_hex, scene_routing_getters: actual.scene_routing_getters,
          owners: actual.owners, world_generation: actual.world_generation,
          audio_generation: actual.audio_generation,
        }, wanted, 'sss_exit', constructorPair, 'sss_exit');
        firstMismatch = common || compareExpectedField(actual.sss?.header?.start_game,
          wanted.sss.header.start_game, 'sss_exit.sss.header.start_game') ||
          actualRulesMismatch(actual, wanted, 'sss_exit') || actualPlayersMismatch(actual, wanted, 'sss_exit');
        if (!firstMismatch && (actual.host_entered !== true || actual.session_phase !== 3 ||
            actual.session_ticks !== 125))
          firstMismatch = mismatch('sss_exit.session', {host_entered: true, session_phase: 3,
            session_ticks: 125}, {host_entered: actual.host_entered,
            session_phase: actual.session_phase, session_ticks: actual.session_ticks},
          'exit note was not captured after the bounded 124-tick prefix');
      }
    } else {
      const record = expected.records[recordIndex];
      const expectedState = record[phase];
      firstMismatch = firstSssActualSnapshotMismatch(row.actual, expectedState, phase,
        constructorPair, `prefix[${recordIndex}].${phase}`, true);
      if (!firstMismatch && (row.actual.input_index !== record.input_index ||
          row.actual.input_ordinal !== record.input_ordinal ||
          row.actual.consumed_pad_sequence !== record.consumed_pad_sequence))
        firstMismatch = mismatch(`prefix[${recordIndex}].input_identity`,
          {input_index: record.input_index, input_ordinal: record.input_ordinal,
            consumed_pad_sequence: record.consumed_pad_sequence},
          {input_index: row.actual.input_index, input_ordinal: row.actual.input_ordinal,
            consumed_pad_sequence: row.actual.consumed_pad_sequence},
          'native sample is not bound to its exact input-only source record');
      if (!firstMismatch && phase === 'scheduler_end' &&
          row.actual.consumed_pad_hex !== record.consumed_pad_status_hex.join(''))
        firstMismatch = mismatch(`prefix[${recordIndex}].consumed_pad_hex`,
          record.consumed_pad_status_hex.join(''), row.actual.consumed_pad_hex,
          'consumed PAD bytes differ from the exact source statuses');
    }
    if (firstMismatch) { failed = true; return false; }
    row.approved = true; next += 1;
    return true;
  };
  return Object.freeze({compare, status: () => Object.freeze({attempted: rows.length,
    approved: next === required.length && !failed, failed, next_phase: required[next]?.[0] ?? null,
    next_index: required[next]?.[1] ?? null, first_mismatch: firstMismatch, rows,
    owner_world_generation: ownerWorld, owner_audio_generation: ownerAudio})});
}
