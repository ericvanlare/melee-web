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
