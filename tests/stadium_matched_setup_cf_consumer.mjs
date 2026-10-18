import path from 'node:path';

const PLAN_SCHEMA = 'melee-web-stadium-typed-pad-action-plan';
const PLAN_BUNDLES = 86;
const PLAN_ACTIONS = 171;
const PAD_STATUS_BYTES = 11;
const PAD_PORTS = 4;

function exactKeys(value, keys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...keys].sort().join(','))
    throw new TypeError(`${label} has an unexpected shape`);
}

export function validateRuntimePayload(payload) {
  exactKeys(payload, ['schema', 'version', 'bundles', 'schedule'], 'runtime payload');
  if (payload.schema !== PLAN_SCHEMA || payload.version !== 1 ||
      !Array.isArray(payload.bundles) || payload.bundles.length !== PLAN_BUNDLES ||
      !Array.isArray(payload.schedule) || payload.schedule.length !== PLAN_ACTIONS)
    throw new TypeError('runtime payload identity or fixed bounds differ');
  for (let bundleIndex = 0; bundleIndex < PLAN_BUNDLES; ++bundleIndex) {
    const bundle = payload.bundles[bundleIndex];
    exactKeys(bundle, ['bundle', 'ports'], `bundle ${bundleIndex}`);
    if (bundle.bundle !== bundleIndex || !Array.isArray(bundle.ports) ||
        bundle.ports.length !== PAD_PORTS)
      throw new TypeError(`bundle ${bundleIndex} is not the exact four-port source bundle`);
    for (let port = 0; port < PAD_PORTS; ++port) {
      const status = bundle.ports[port];
      exactKeys(status, ['port', 'status_hex'], `bundle ${bundleIndex} port ${port}`);
      if (status.port !== port || typeof status.status_hex !== 'string' ||
          !/^[0-9a-f]{22}$/.test(status.status_hex))
        throw new TypeError(`bundle ${bundleIndex} port ${port} lacks exact 11-byte source data`);
    }
  }
  for (let index = 0; index < PLAN_ACTIONS; ++index) {
    const action = payload.schedule[index];
    const ordinal = index < 168 ? Math.floor(index / 2) : index === 168 ? 84 : index === 169 ? 84 : 85;
    const kind = index === 168 || index === 170 || (index < 168 && index % 2 === 0)
      ? 'tick' : 'draw';
    exactKeys(action, kind === 'tick' ? ['action', 'ordinal', 'bundle'] : ['action', 'ordinal'],
      `schedule ${index}`);
    if (action.action !== kind || action.ordinal !== ordinal ||
        (kind === 'tick' && action.bundle !== ordinal))
      throw new TypeError(`schedule ${index} differs from the fixed tick/draw continuation`);
  }
  return payload;
}

export function validateRun2RuntimePayload(payload) {
  exactKeys(payload, ['bundles', 'schedule', 'schema', 'version'], 'run2 runtime payload');
  if (payload.schema !== 'melee-web-stadium-pad-action-plan' || payload.version !== 1 ||
      !Array.isArray(payload.bundles) || !Array.isArray(payload.schedule) ||
      payload.bundles.length === 0 || payload.schedule.length === 0)
    throw new TypeError('run2 action payload identity or arrays differ');
  for (let bundleIndex = 0; bundleIndex < payload.bundles.length; ++bundleIndex) {
    const bundle = payload.bundles[bundleIndex];
    exactKeys(bundle, ['bundle', 'ports'], `run2 bundle ${bundleIndex}`);
    if (bundle.bundle !== bundleIndex || !Array.isArray(bundle.ports) || bundle.ports.length !== PAD_PORTS)
      throw new TypeError(`run2 bundle ${bundleIndex} is not the exact four-port source bundle`);
    for (let port = 0; port < PAD_PORTS; ++port) {
      const status = bundle.ports[port];
      exactKeys(status, ['port', 'status_hex'], `run2 bundle ${bundleIndex} port ${port}`);
      if (status.port !== port || typeof status.status_hex !== 'string' ||
          !/^[0-9a-f]{22}$/.test(status.status_hex))
        throw new TypeError(`run2 bundle ${bundleIndex} port ${port} lacks exact 11-byte source data`);
    }
  }
  let tick = 0, draw = 0;
  for (let index = 0; index < payload.schedule.length; ++index) {
    const action = payload.schedule[index];
    if (!action || action.action_index !== index)
      throw new TypeError(`run2 action ${index} is missing or out of source order`);
    if (action.action === 'tick') {
      exactKeys(action, ['action', 'action_index', 'bundle'], `run2 tick ${index}`);
      if (action.bundle !== tick || action.bundle >= payload.bundles.length)
        throw new TypeError(`run2 tick ${index} has a discontinuous or missing PAD bundle`);
      ++tick;
    } else if (action.action === 'draw') {
      exactKeys(action, ['action', 'action_index', 'draw'], `run2 draw ${index}`);
      if (action.draw !== draw) throw new TypeError(`run2 draw ${index} has a discontinuous draw counter`);
      ++draw;
    } else throw new TypeError(`run2 action ${index} has an unknown action kind`);
  }
  if (tick !== payload.bundles.length)
    throw new TypeError('run2 schedule does not consume every exact PAD bundle once');
  return payload;
}

function run2Coordinate(row, label, event, boundary) {
  const raw = row?.raw_identity;
  if (!raw || raw.event !== event || (boundary && raw.boundary !== boundary) ||
      !Number.isSafeInteger(raw.sequence) || !Number.isSafeInteger(raw.source_tick) ||
      !Number.isSafeInteger(raw.draw_ordinal))
    throw new TypeError(`run2 ${label} source identity is malformed`);
  return raw;
}

export function deriveRun2ProperPrefix(runtimePayload, hostOnly) {
  const payload = validateRun2RuntimePayload(runtimePayload);
  if (!hostOnly || hostOnly.schema !== 'melee-web-stadium-source-host-expectations' ||
      hostOnly.version !== 1 || !hostOnly.expected_state ||
      !Array.isArray(hostOnly.boundary_rng_observations))
    throw new TypeError('run2 host-only typed expectations and boundary RNG observations are required');
  const state = hostOnly.expected_state;
  const go = run2Coordinate(state.GO, 'GO', 'progress', null);
  if (state.GO.raw_identity.phase !== 'go_after') throw new TypeError('run2 GO marker differs');
  const c = run2Coordinate(state.C, 'C', 'boundary', 'source_tick');
  const f = run2Coordinate(state.F, 'F', 'boundary', 'source_tick');
  const go2 = state.GO2 ?? null;
  if (c.sequence === f.sequence || c.source_tick !== go.source_tick ||
      f.source_tick !== c.source_tick + 1 ||
      c.draw_ordinal !== go.draw_ordinal)
    throw new TypeError('run2 GO/C/F source coordinates are not one dynamic adjacent boundary');
  const seedRows = hostOnly.boundary_rng_observations;
  if (seedRows.length !== 2 || seedRows.map(row => row?.label).join(',') !== 'C,F')
    throw new TypeError('run2 host-only C/F seed rows are missing or reordered');
  for (const [row, raw] of [[seedRows[0], c], [seedRows[1], f]]) {
    if (row.sequence !== raw.sequence || row.source_tick !== raw.source_tick ||
        row.draw_ordinal !== raw.draw_ordinal ||
        row.post_go_ordinal !== raw.source_tick - c.source_tick ||
        !/^[0-9a-f]{8}$/.test(row.seed_hex ?? '') ||
        !/^[0-9a-f]{8}$/.test(row.pointer_hex ?? ''))
      throw new TypeError(`run2 ${row.label} seed row differs from its typed raw source identity`);
  }
  let go2ActionIndex = null;
  let go2Expected = null;
  if (go2 !== null) {
    const raw = go2.raw_identity;
    const fields = go2.typed_fields;
    const provenance = go2.provenance;
    const sourceQueue = go2.source_queue;
    const reference = hostOnly.source_identity?.go2_reference;
    if (go2.schema !== 'melee-web-original-stadium-go2-declared-state' ||
        go2.version !== 1 || go2.decision !== 'PASS_SOURCE_TYPED_FIELDS_ONLY' ||
        go2.scope !== 'One actual GO-relative ordinal-2 SourceTick typed snapshot and direct seed bytes; no pointer comparison or whole-state parity.' ||
        !raw || raw.event !== 'boundary' || raw.boundary !== 'source_tick' ||
        !Number.isSafeInteger(raw.sequence) || raw.source_tick !== f.source_tick + 1 ||
        raw.source_tick !== c.source_tick + 2 || raw.draw_ordinal !== c.draw_ordinal + 2 ||
        !fields || fields.label !== 'GO2' || fields.sequence !== raw.sequence ||
        fields.source_tick !== raw.source_tick || fields.draw_ordinal !== raw.draw_ordinal ||
        !fields.map2 || !fields.display || fields.map_root_allocation_lifetime_proved !== false ||
        fields.map_root_object_type_observed !== false ||
        provenance?.scope !== 'same fresh native producer and its own complete MWRI record' ||
        !/^[0-9a-f]{64}$/.test(provenance.source_stream_sha256 ?? '') ||
        !/^[0-9a-f]{64}$/.test(provenance.input_stream_sha256 ?? '') ||
        !/^[0-9a-f]{64}$/.test(provenance.setup_receipt_sha256 ?? '') ||
        !reference || reference.schema !== 'melee-web-stadium-go2-source-reference-v1' ||
        reference.source_stream_sha256 !== provenance.source_stream_sha256 ||
        reference.input_stream_sha256 !== provenance.input_stream_sha256 ||
        reference.setup_receipt_sha256 !== provenance.setup_receipt_sha256)
      throw new TypeError('run2 GO+2 typed state or same-producer provenance is malformed');
    const seed = go2.seed_observation;
    if (!seed || seed.sequence !== raw.sequence || seed.event !== 'boundary' ||
        seed.boundary !== 'source_tick' || seed.source_tick !== raw.source_tick ||
        seed.draw_ordinal !== raw.draw_ordinal || seed.pointer_slot_address !== 0x804D5F94 ||
        !/^[0-9a-f]{8}$/.test(seed.pointer_raw_hex ?? '') ||
        Number.parseInt(seed.pointer_raw_hex, 16) !== seed.seed_address ||
        !/^[0-9a-f]{8}$/.test(seed.seed_hex ?? '') ||
        seed.pointer_equals_seed_address !== true)
      throw new TypeError('run2 GO+2 seed observation is malformed or detached from its SourceTick');
    if (!sourceQueue || sourceQueue.source_tick !== raw.source_tick ||
        sourceQueue.draw_ordinal !== raw.draw_ordinal ||
        !Number.isSafeInteger(sourceQueue.consume_sequence) ||
        !Array.isArray(sourceQueue.queue_slot_status_hex) ||
        sourceQueue.queue_slot_status_hex.length !== 4 ||
        sourceQueue.queue_slot_status_hex.some(value => !/^[0-9a-f]{22}$/.test(value)))
      throw new TypeError('run2 GO+2 source queue is malformed');
    go2ActionIndex = payload.schedule.findIndex(action =>
      action.action === 'tick' && action.bundle === raw.source_tick);
    if (go2ActionIndex < 0 || payload.schedule[go2ActionIndex]?.action !== 'tick' ||
        payload.schedule[go2ActionIndex - 1]?.action !== 'draw' ||
        payload.schedule[go2ActionIndex + 1]?.action !== 'draw' ||
        sourceQueue.queue_slot_status_hex.join(',') !==
          payload.bundles[raw.source_tick]?.ports?.map(port => port.status_hex).join(','))
      throw new TypeError('run2 GO+2 source row is not the literal scheduled four-port input');
    go2Expected = go2;
  }
  const cIndex = payload.schedule.findIndex(action => action.action === 'tick' && action.bundle === c.source_tick);
  const fIndex = payload.schedule.findIndex(action => action.action === 'tick' && action.bundle === f.source_tick);
  if (cIndex < 0 || fIndex <= cIndex ||
      payload.schedule.slice(cIndex + 1, fIndex).some(action => action.action === 'tick') ||
      payload.schedule.slice(0, cIndex).filter(action => action.action === 'draw').length !== c.draw_ordinal ||
      payload.schedule.slice(0, fIndex).filter(action => action.action === 'draw').length !== f.draw_ordinal)
    throw new TypeError('run2 schedule does not contain the unique dynamic C/F tick boundaries');
  if (payload.schedule[fIndex + 1]?.action !== 'draw' ||
      payload.schedule[fIndex + 2]?.action !== 'tick' ||
      payload.schedule[fIndex + 3]?.action !== 'draw')
    throw new TypeError('run2 schedule lacks the next draw/tick/draw continuation beyond F');
  const terminalIndex = fIndex + 3;
  const terminal = payload.schedule[terminalIndex];
  const prefix = {schema: 'melee-web-stadium-run2-proper-prefix-v2', version: 2,
    bundles: payload.bundles.slice(0, terminal.action === 'tick' ? terminal.bundle + 1 :
      payload.schedule.slice(0, terminalIndex + 1).filter(action => action.action === 'tick').length),
    schedule: payload.schedule.slice(0, terminalIndex + 1),
    checkpoints: {c_action_index: cIndex, f_action_index: fIndex,
      c_source_tick: c.source_tick, f_source_tick: f.source_tick,
      terminal_action_index: terminalIndex, terminal_draw_ordinal: terminal.draw}};
  return {prefix, c, f, go, expectedState: state,
    expectedSeeds: {C: seedRows[0].seed_hex, F: seedRows[1].seed_hex,
      ...(go2 ? {GO2: go2.seed_observation.seed_hex} : {})},
    go2Expected, go2ActionIndex};
}

export function deriveRun2FullSchedule(runtimePayload, hostOnly) {
  const derived = deriveRun2ProperPrefix(runtimePayload, hostOnly);
  const payload = validateRun2RuntimePayload(runtimePayload);
  if (payload.schedule.length !== 7167 || payload.bundles.length !== 3585 ||
      payload.schedule.filter(row => row.action === 'draw').length !== 3582 ||
      derived.prefix.checkpoints.c_action_index !== 168 || derived.prefix.checkpoints.f_action_index !== 170 ||
      derived.c.source_tick !== 84 || derived.f.source_tick !== 85 ||
      payload.schedule.at(-1)?.action !== 'draw' || payload.schedule.at(-1)?.draw !== 3581)
    throw new TypeError('original full7167 schedule dimensions or C/F coordinates differ');
  return {...derived, prefix: {schema: 'melee-web-stadium-run2-full-schedule-v3', version: 3,
    bundles: payload.bundles, schedule: payload.schedule,
    checkpoints: {...derived.prefix.checkpoints, terminal_action_index: 7166,
      terminal_draw_ordinal: 3581}}};
}

const COMMON_FIELDS_NATIVE_SAMPLE_SCHEMA = 'melee-web-stadium-per-tick-common-fields-native-v2';
const COMMON_FIELDS_COMPARISON_SCHEMA = 'melee-web-stadium-source-tick-common-fields-comparison-v2';
const COMMON_FIELDS_SAMPLE_MAX_BYTES = 1508;
const COMMON_FIELDS_COMPARISON_MAX_BYTES = 512;
export const IDLE_SEED_REFERENCE_IDENTITY = Object.freeze({
  basename: 'idle3500-reference.json', bytes: 2033197,
  sha256: '562969a7e6a6e7fd43457821f84464852a38da2a83c9dc20672ad713cd6010bc',
  hash_source: 'computed',
});
const COMMON_FIELDS_EXPECTED_KEYS = Object.freeze([
  'map2_countdown', 'map2_phase', 'map2_form', 'map2_history_e0', 'map2_history_e2',
  'map2_gate', 'display_mode', 'display_e6', 'display_e8', 'display_previous_mode',
  'display_selected_player', 'display_f2', 'display_flags',
]);
const COMMON_FIELDS_ACTUAL_KEYS = Object.freeze([
  'schema', 'schedule_index', 'source_tick', 'world_ticks', 'source_frame', 'world_generation',
  ...COMMON_FIELDS_EXPECTED_KEYS, 'seed_hex',
]);
const COMMON_FIELDS_INTEGER_FIELDS = Object.freeze([
  ['map2.countdown_s32', 'map2_countdown', 32],
  ['map2.phase_s16', 'map2_phase', 16],
  ['map2.form_s16', 'map2_form', 16],
  ['map2.previous_form_s16', 'map2_history_e0', 16],
  ['map2.prior_form_s16', 'map2_history_e2', 16],
  ['display.mode_s16', 'display_mode', 16],
  ['display.flags_s16', 'display_e6', 16],
  ['display.previous_flags_s16', 'display_e8', 16],
  ['display.previous_mode_s16', 'display_previous_mode', 16],
  ['display.focused_player_s16', 'display_selected_player', 16],
  ['display.selection_counter_s16', 'display_f2', 16],
]);
function signedRange(value, bits, label) {
  const limit = 2 ** (bits - 1);
  if (!Number.isInteger(value) || value < -limit || value >= limit)
    throw new TypeError(`${label} is not an exact signed${bits} value`);
  return value;
}
function commonOriginalValues(fields, sourceTick) {
  exactKeys(fields, ['display', 'draw_ordinal', 'guest_pointer_policy', 'label', 'map2',
    'map_root_allocation_lifetime_proved', 'map_root_object_type_observed', 'map_root_roles',
    'sequence', 'source_tick', 'title'], `original source tick ${sourceTick} typed fields`);
  const map = fields.map2, display = fields.display;
  exactKeys(map, ['countdown_s32', 'form_s16', 'gate_mask_40', 'gate_mask_80', 'gate_raw_hex',
    'map5_root_pointer_equals_fixed_map5_slot_value', 'phase_s16', 'previous_form_s16',
    'prior_form_s16'], `original source tick ${sourceTick} map2 fields`);
  exactKeys(display, ['flags_s16', 'focused_player_s16', 'mode_s16', 'packed_flags_raw_hex',
    'previous_flags_s16', 'previous_mode_s16', 'selection_counter_s16', 'timer_initialization_observed',
    'timer_raw_hex', 'timer_representation', 'xF0_observed'],
  `original source tick ${sourceTick} display fields`);
  const values = {
    map2_countdown: signedRange(map.countdown_s32, 32, 'original map2.countdown_s32'),
    map2_phase: signedRange(map.phase_s16, 16, 'original map2.phase_s16'),
    map2_form: signedRange(map.form_s16, 16, 'original map2.form_s16'),
    map2_history_e0: signedRange(map.previous_form_s16, 16, 'original map2.previous_form_s16'),
    map2_history_e2: signedRange(map.prior_form_s16, 16, 'original map2.prior_form_s16'),
    map2_gate: requiredBoolean(map.gate_mask_80, 'original map2.gate_mask_80'),
    display_mode: signedRange(display.mode_s16, 16, 'original display.mode_s16'),
    display_e6: signedRange(display.flags_s16, 16, 'original display.flags_s16'),
    display_e8: signedRange(display.previous_flags_s16, 16, 'original display.previous_flags_s16'),
    display_previous_mode: signedRange(display.previous_mode_s16, 16, 'original display.previous_mode_s16'),
    display_selected_player: signedRange(display.focused_player_s16, 16, 'original display.focused_player_s16'),
    display_f2: signedRange(display.selection_counter_s16, 16, 'original display.selection_counter_s16'),
  };
  if (typeof map.gate_mask_40 !== 'boolean' || !/^[0-9a-fA-F]{2}$/.test(map.gate_raw_hex ?? '') ||
      ((Number.parseInt(map.gate_raw_hex, 16) & 0x80) !== 0) !== map.gate_mask_80 ||
      typeof map.map5_root_pointer_equals_fixed_map5_slot_value !== 'boolean' ||
      fields.map_root_allocation_lifetime_proved !== false ||
      fields.map_root_object_type_observed !== false ||
      !Array.isArray(fields.map_root_roles) || fields.map_root_roles.length !== 4 ||
      fields.map_root_roles.some((role, index) => role?.fixed_slot_map_id !== [0, 1, 2, 5][index]) ||
      !/^[0-9a-fA-F]{2}$/.test(display.packed_flags_raw_hex ?? '') ||
      typeof display.timer_initialization_observed !== 'boolean' ||
      typeof display.xF0_observed !== 'boolean' ||
      !/^[0-9a-fA-F]{8}$/.test(display.timer_raw_hex ?? '') ||
      typeof display.timer_representation !== 'string')
    throw new TypeError(`original source tick ${sourceTick} unpaired/common evidence shape is malformed`);
  const packed = Number.parseInt(display.packed_flags_raw_hex, 16);
  values.display_flags = ((packed & 0x80) ? 1 : 0) |
      ((packed & 0x40) ? 2 : 0) | ((packed & 0x20) ? 4 : 0);
  exactKeys(values, COMMON_FIELDS_EXPECTED_KEYS, `original source tick ${sourceTick} common values`);
  return values;
}

// One O(n) pass binds all host original action provenance to the already
// validated native schedule, then binds the 3501 typed source rows to ticks.
export function deriveRun2CommonTickExpectations(runtimePayload, hostOnly) {
  const payload = validateRun2RuntimePayload(runtimePayload);
  const route = hostOnly?.source_identity?.strict_route;
  const source = route?.source;
  if (source?.dol_sha256 !== 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646' ||
      source?.dol_sha1 !== '08e0bf20134dfcb260699671004527b2d6bb1a45' ||
      route?.stream_sha256 !== '45de31f20751b87414fe5ca5d3569940ce706bb029e30beb142f33fac546ff3a' ||
      route?.stream_records !== 20482 ||
      hostOnly?.source_identity?.source_run?.source_consumed_pad?.artifact?.sha256 !==
        '45de31f20751b87414fe5ca5d3569940ce706bb029e30beb142f33fac546ff3a')
    throw new TypeError('per-tick original DOL or consumed-stream identity differs');
  const provenance = hostOnly.action_provenance;
  if (!Array.isArray(provenance) || provenance.length !== 7167 ||
      payload.schedule.length !== 7167 || payload.bundles.length !== 3585)
    throw new TypeError('per-tick comparison requires all7167 original provenance/action rows');
  const tickProvenance = new Map();
  let tick = 0, draw = 0, previousSequence = -1;
  for (let index = 0; index < 7167; ++index) {
    const action = payload.schedule[index], original = provenance[index];
    if (!original || original.action_index !== index || original.action !== action.action)
      throw new TypeError(`original action provenance ${index} is missing, duplicate, reordered, or mismatched`);
    let eventSequences;
    if (action.action === 'tick') {
      exactKeys(original, ['action', 'action_index', 'consume_sequence', 'draw_ordinal',
        'intervening_progress_sequences', 'source_tick', 'source_tick_sequence'],
      `original tick provenance ${index}`);
      if (action.bundle !== tick || original.source_tick !== tick || original.draw_ordinal !== draw ||
          !Array.isArray(original.intervening_progress_sequences))
        throw new TypeError(`original tick provenance ${index} differs from its runtime bundle/draw cursor`);
      const progress = original.intervening_progress_sequences;
      if (progress.some(sequence => !Number.isSafeInteger(sequence)) ||
          progress.some((sequence, position) => position > 0 && sequence <= progress[position - 1]))
        throw new TypeError(`original tick provenance ${index} has malformed progress ordering`);
      eventSequences = [original.consume_sequence, ...progress, original.source_tick_sequence];
      if (eventSequences.some(sequence => !Number.isSafeInteger(sequence) || sequence < 0) ||
          eventSequences.some((sequence, position) => position > 0 && sequence <= eventSequences[position - 1]))
        throw new TypeError(`original tick provenance ${index} violates consume-before-tick sequence`);
      tickProvenance.set(tick, original);
      ++tick;
    } else {
      exactKeys(original, ['action', 'action_index', 'draw_enter_sequence', 'draw_ordinal',
        'draw_return_sequence', 'source_tick'], `original draw provenance ${index}`);
      if (action.draw !== draw || original.draw_ordinal !== draw || original.source_tick !== tick)
        throw new TypeError(`original draw provenance ${index} differs from its source tick/draw cursor`);
      eventSequences = [original.draw_enter_sequence, original.draw_return_sequence];
      if (eventSequences.some(sequence => !Number.isSafeInteger(sequence) || sequence < 0) ||
          eventSequences[1] <= eventSequences[0])
        throw new TypeError(`original draw provenance ${index} violates draw enter/return ordering`);
      ++draw;
    }
    if (eventSequences[0] <= previousSequence)
      throw new TypeError(`original action provenance ${index} is not in source event order`);
    previousSequence = eventSequences.at(-1);
  }
  if (tick !== 3585 || draw !== 3582 ||
      payload.schedule[7165]?.action !== 'tick' || payload.schedule[7165]?.bundle !== 3584 ||
      payload.schedule[7166]?.action !== 'draw' || payload.schedule[7166]?.draw !== 3581 ||
      tickProvenance.get(84)?.action_index !== 168 ||
      tickProvenance.get(84)?.source_tick_sequence !== 2984 ||
      tickProvenance.get(84)?.draw_ordinal !== 84 ||
      tickProvenance.get(85)?.action_index !== 170 ||
      tickProvenance.get(85)?.source_tick_sequence !== 2989 ||
      tickProvenance.get(85)?.draw_ordinal !== 85 ||
      tickProvenance.get(3584)?.action_index !== 7165 ||
      tickProvenance.get(3584)?.source_tick_sequence !== 20478 ||
      tickProvenance.get(3584)?.draw_ordinal !== 3581)
    throw new TypeError('original 7167-action source/tick/draw terminal mapping differs');
  const sourceRows = hostOnly?.expected_state?.per_source_tick;
  if (!Array.isArray(sourceRows) || sourceRows.length !== 3501)
    throw new TypeError('exact3501 typed original source tick rows are required');
  const expectedByAction = new Map();
  for (let rowIndex = 0; rowIndex < 3501; ++rowIndex) {
    const sourceTick = 84 + rowIndex, row = sourceRows[rowIndex];
    if (!row || !['C', 'F', 'IDLE'].includes(row.boundary_role))
      throw new TypeError(`typed original source row ${rowIndex} has an unknown role`);
    exactKeys(row.raw_identity, ['boundary', 'draw_ordinal', 'event', 'sequence', 'source_tick'],
      `typed original source row ${rowIndex} raw identity`);
    const raw = row.raw_identity, role = sourceTick === 84 ? 'C' : sourceTick === 85 ? 'F' : 'IDLE';
    if (row.boundary_role !== role || raw.event !== 'boundary' || raw.boundary !== 'source_tick' ||
        raw.source_tick !== sourceTick || !Number.isSafeInteger(raw.sequence) || raw.sequence < 0 ||
        !Number.isSafeInteger(raw.draw_ordinal) || raw.draw_ordinal < 0 ||
        row.typed_fields?.label !== role || row.typed_fields.sequence !== raw.sequence ||
        row.typed_fields.source_tick !== raw.source_tick || row.typed_fields.draw_ordinal !== raw.draw_ordinal)
      throw new TypeError(`typed original source row ${rowIndex} differs from its raw source identity`);
    const action = tickProvenance.get(sourceTick);
    if (!action || action.source_tick !== raw.source_tick ||
        action.source_tick_sequence !== raw.sequence || action.draw_ordinal !== raw.draw_ordinal ||
        payload.schedule[action.action_index]?.action !== 'tick' ||
        payload.schedule[action.action_index]?.bundle !== sourceTick ||
        expectedByAction.has(action.action_index))
      throw new TypeError(`typed original source row ${rowIndex} does not bijectively map to one runtime tick`);
    const values = commonOriginalValues(row.typed_fields, sourceTick);
    expectedByAction.set(action.action_index, {action_index: action.action_index,
      boundary_role: role, source_tick: sourceTick, draw_ordinal: raw.draw_ordinal,
      raw_identity: {sequence: raw.sequence, source_tick: raw.source_tick,
        draw_ordinal: raw.draw_ordinal}, values});
  }
  if (expectedByAction.size !== 3501 || !expectedByAction.has(168) ||
      !expectedByAction.has(170) || !expectedByAction.has(7165))
    throw new TypeError('per-tick common expectation index is incomplete');
  return expectedByAction;
}

// The RNG reference remains a separate, independently hash-bound original
// artifact. This joins its existing tick rows; it does not infer RNG calls or
// compare pointer values across owners.
export function deriveRun2CommonSeedExpectations(runtimePayload, hostOnly,
  commonRowsByAction, seedSidecar, seedIdentity) {
  const payload = validateRun2RuntimePayload(runtimePayload);
  const binding = hostOnly?.source_identity?.source_run?.run_artifacts?.['idle3500-reference.json'];
  exactKeys(binding, ['bytes', 'hash_source', 'path', 'sha256'], 'original idle seed artifact binding');
  exactKeys(seedIdentity, ['basename', 'bytes', 'path', 'sha256'], 'loaded idle seed artifact identity');
  if (typeof binding.path !== 'string' || !path.isAbsolute(binding.path) ||
      path.resolve(binding.path) !== binding.path ||
      path.basename(binding.path) !== IDLE_SEED_REFERENCE_IDENTITY.basename ||
      binding.bytes !== IDLE_SEED_REFERENCE_IDENTITY.bytes ||
      binding.sha256 !== IDLE_SEED_REFERENCE_IDENTITY.sha256 ||
      binding.hash_source !== IDLE_SEED_REFERENCE_IDENTITY.hash_source ||
      seedIdentity.path !== binding.path ||
      seedIdentity.basename !== IDLE_SEED_REFERENCE_IDENTITY.basename ||
      seedIdentity.bytes !== binding.bytes || seedIdentity.sha256 !== binding.sha256 ||
      !Number.isSafeInteger(seedIdentity.bytes) || !/^[0-9a-f]{64}$/.test(seedIdentity.sha256))
    throw new TypeError('retained original idle seed sidecar is not the exact packet-bound artifact');
  if (!seedSidecar || seedSidecar.schema !== 'melee-web-original-stadium-idle-3500' ||
      seedSidecar.version !== 1 || seedSidecar.decision !== 'PASS_ORIGINAL_3500_IDLE_REFERENCE_ONLY' ||
      seedSidecar.observed_tick_count !== 3501 ||
      seedSidecar.comparison_post_go_ticks !== 3500 ||
      seedSidecar.cross_owner_rng_ledger_complete !== false ||
      seedSidecar.native_parity !== false || seedSidecar.lifetime_pairing_observed !== false ||
      !Array.isArray(seedSidecar.seed_observations) || seedSidecar.seed_observations.length !== 3501 ||
      !Array.isArray(seedSidecar.source_action_schedule) || seedSidecar.source_action_schedule.length !== 10497 ||
      !(commonRowsByAction instanceof Map) || commonRowsByAction.size !== 3501)
    throw new TypeError('retained original idle seed sidecar scope or dimensions differ');

  const expectedBySequence = new Map();
  for (const expected of commonRowsByAction.values()) {
    if (!expected || expectedBySequence.has(expected.raw_identity?.sequence))
      throw new TypeError('typed source tick seed join contains duplicate identities');
    expectedBySequence.set(expected.raw_identity.sequence, expected);
  }
  const seedSequences = new Set(), seedValues = new Set();
  const joined = new Map();
  const provenance = hostOnly.action_provenance;
  let scheduleCursor = 0, previousSequence = -1;
  for (let ordinal = 0; ordinal < 3501; ++ordinal) {
    const sourceTick = 84 + ordinal;
    const seed = seedSidecar.seed_observations[ordinal];
    exactKeys(seed, ['ordinal', 'pointer_hex', 'seed_hex', 'sequence'],
      `original seed observation ${ordinal}`);
    if (seed.ordinal !== ordinal || !Number.isSafeInteger(seed.sequence) || seed.sequence < 0 ||
        !/^[0-9a-f]{8}$/.test(seed.pointer_hex ?? '') || seed.pointer_hex !== '804d5f90' ||
        !/^[0-9a-f]{8}$/.test(seed.seed_hex ?? '') || seedSequences.has(seed.sequence) ||
        seedValues.has(seed.seed_hex))
      throw new TypeError(`original idle seed row ${ordinal} is missing, repeated, reordered, or malformed`);
    seedSequences.add(seed.sequence); seedValues.add(seed.seed_hex);
    const expected = expectedBySequence.get(seed.sequence);
    if (!expected || expected.source_tick !== sourceTick ||
        expected.raw_identity.sequence !== seed.sequence ||
        expected.draw_ordinal !== expected.raw_identity.draw_ordinal ||
        expected.action_index < 0 || expected.action_index >= payload.schedule.length ||
        payload.schedule[expected.action_index]?.action !== 'tick' ||
        payload.schedule[expected.action_index]?.bundle !== sourceTick)
      throw new TypeError(`original seed row ${ordinal} does not join one typed row and full schedule tick`);
    const referenceTick = seedSidecar.source_action_schedule[scheduleCursor++];
    exactKeys(referenceTick, ['action', 'bundle', 'draw_ordinal', 'ordinal', 'sequence'],
      `original seed action ${ordinal}`);
    if (referenceTick.action !== 'tick' || referenceTick.ordinal !== ordinal ||
        referenceTick.bundle !== ordinal || referenceTick.sequence !== seed.sequence ||
        referenceTick.draw_ordinal !== expected.draw_ordinal)
      throw new TypeError(`original seed action ${ordinal} differs from its tick/sequence join`);
    if (seed.sequence <= previousSequence)
      throw new TypeError(`original seed action ${ordinal} is not in event order`);
    previousSequence = seed.sequence;
    const actionIndex = expected.action_index;
    const action = payload.schedule[actionIndex];
    const draw = payload.schedule[actionIndex + 1];
    if (draw?.action === 'draw') {
      const drawSource = provenance[actionIndex + 1];
      if (!drawSource || drawSource.action !== 'draw' || drawSource.action_index !== actionIndex + 1 ||
          drawSource.draw_ordinal !== draw.draw || drawSource.source_tick !== sourceTick + 1)
        throw new TypeError(`original seed action ${ordinal} lacks the unique following draw provenance`);
      for (const [name, sequence] of [['draw_enter', drawSource.draw_enter_sequence],
        ['draw_return', drawSource.draw_return_sequence]]) {
        const sourceEvent = seedSidecar.source_action_schedule[scheduleCursor++];
        exactKeys(sourceEvent, ['action', 'draw_ordinal', 'ordinal', 'sequence'],
          `original seed ${name} action ${ordinal}`);
        if (sourceEvent.action !== name || sourceEvent.ordinal !== ordinal ||
            sourceEvent.draw_ordinal !== draw.draw || sourceEvent.sequence !== sequence ||
            !Number.isSafeInteger(sequence) || sequence <= previousSequence)
          throw new TypeError(`original seed ${name} action ${ordinal} differs from full draw provenance`);
        previousSequence = sequence;
      }
    } else if (draw !== undefined && draw.action !== 'tick') {
      throw new TypeError(`original seed action ${ordinal} has an unknown following full action`);
    }
    joined.set(actionIndex, {...expected, seed_ordinal: ordinal, seed_hex: seed.seed_hex});
  }
  if (scheduleCursor !== seedSidecar.source_action_schedule.length || joined.size !== 3501 ||
      seedSequences.size !== 3501 || seedValues.size !== 3501)
    throw new TypeError('original idle seed schedule has extra or missing tick/draw evidence');
  const originalCFS = hostOnly.boundary_rng_observations;
  if (!Array.isArray(originalCFS) || originalCFS.length !== 2 ||
      originalCFS[0]?.label !== 'C' || originalCFS[1]?.label !== 'F' ||
      joined.get(168)?.seed_hex !== originalCFS[0].seed_hex ||
      joined.get(170)?.seed_hex !== originalCFS[1].seed_hex)
    throw new TypeError('original seed sidecar C/F values differ from existing boundary RNG evidence');
  if (joined.get(7165)?.seed_hex !== '312a19c4')
    throw new TypeError('original seed sidecar terminal value differs from its reviewed endpoint');
  return joined;
}

export function compareOriginalCommonTickFields({expected, actual, actionIndex, sourceFramesAfter,
  admittedWorldGeneration}) {
  const refusal = (path, want, got, count = 0) => ({
    schema: COMMON_FIELDS_COMPARISON_SCHEMA,
    scope: 'exact host-original typed source row and hash-bound seed versus one completed post-tick native value-copy',
    action_index: Number.isSafeInteger(actionIndex) ? actionIndex : null,
    source_tick: Number.isSafeInteger(expected?.source_tick) ? expected.source_tick : null,
    original_sequence: Number.isSafeInteger(expected?.raw_identity?.sequence)
      ? expected.raw_identity.sequence : null,
    draw_ordinal: Number.isSafeInteger(expected?.draw_ordinal) ? expected.draw_ordinal : null,
    compared_field_count: count, seed_value_count: count ? 1 : 0,
    exact_value_count: count ? count + 1 : 0,
    decision: 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH',
    first_mismatch: {path, expected: want ?? null, actual: got ?? null},
  });
  try {
    exactKeys(expected, ['action_index', 'boundary_role', 'draw_ordinal', 'raw_identity',
      'seed_hex', 'seed_ordinal', 'source_tick', 'values'], 'common-field expected source row');
    exactKeys(expected.raw_identity, ['draw_ordinal', 'sequence', 'source_tick'],
      'common-field expected raw identity');
    exactKeys(expected.values, COMMON_FIELDS_EXPECTED_KEYS, 'common-field expected values');
    if (!Number.isSafeInteger(actionIndex) || actionIndex !== expected.action_index ||
        !Number.isSafeInteger(expected.source_tick) || expected.source_tick < 84 ||
        expected.source_tick > 3584 || expected.raw_identity.source_tick !== expected.source_tick ||
        expected.raw_identity.draw_ordinal !== expected.draw_ordinal ||
        !Number.isSafeInteger(expected.raw_identity.sequence) || expected.raw_identity.sequence < 0 ||
        expected.seed_ordinal !== expected.source_tick - 84 ||
        !/^[0-9a-f]{8}$/.test(expected.seed_hex ?? '') ||
        expected.boundary_role !== (expected.source_tick === 84 ? 'C' :
          expected.source_tick === 85 ? 'F' : 'IDLE'))
      return refusal('original.action_mapping', expected?.action_index, actionIndex);
    if (!Number.isSafeInteger(admittedWorldGeneration) || admittedWorldGeneration <= 0)
      return refusal('admitted.world_generation', 'positive safe integer from checked Setup', admittedWorldGeneration);
    if (!actual || typeof actual !== 'object' || Array.isArray(actual))
      return refusal('native.common_fields_sample', 'one object', actual);
    let serialized;
    try { serialized = JSON.stringify(actual); }
    catch { return refusal('native.serialization', 'bounded JSON object', 'unserializable'); }
    if (Buffer.byteLength(serialized, 'utf8') > COMMON_FIELDS_SAMPLE_MAX_BYTES)
      return refusal('native.sample_bytes', `<=${COMMON_FIELDS_SAMPLE_MAX_BYTES}`, Buffer.byteLength(serialized, 'utf8'));
    try { exactKeys(actual, COMMON_FIELDS_ACTUAL_KEYS, 'native per-tick common sample'); }
    catch { return refusal('native.keys', COMMON_FIELDS_ACTUAL_KEYS, Object.keys(actual).sort()); }
    if (actual.schema !== COMMON_FIELDS_NATIVE_SAMPLE_SCHEMA ||
        actual.schedule_index !== actionIndex || actual.source_tick !== expected.source_tick ||
        actual.world_ticks !== expected.source_tick + 1 ||
        !Number.isSafeInteger(actual.world_generation) || actual.world_generation !== admittedWorldGeneration ||
        !Number.isSafeInteger(actual.source_frame) || actual.source_frame < 0 ||
        actual.source_frame > 0xffffffff || !Number.isSafeInteger(sourceFramesAfter) ||
        sourceFramesAfter < 0 || sourceFramesAfter > 0xffffffff ||
        actual.source_frame !== sourceFramesAfter)
      return refusal('native.action_or_owner_identity',
        {schedule_index: actionIndex, source_tick: expected.source_tick,
          world_ticks: expected.source_tick + 1, world_generation: admittedWorldGeneration,
          source_frame: sourceFramesAfter},
        {schedule_index: actual.schedule_index, source_tick: actual.source_tick,
          world_ticks: actual.world_ticks, world_generation: actual.world_generation,
          source_frame: actual.source_frame, receipt_source_frames_after: sourceFramesAfter});
    if (!/^[0-9a-f]{8}$/.test(actual.seed_hex ?? ''))
      return refusal('native.seed_hex', 'exact 8-lowercase-hex value', actual.seed_hex);
    if (typeof actual.map2_gate !== 'boolean' ||
        !Number.isInteger(actual.display_flags) || actual.display_flags < 0 || actual.display_flags > 7)
      return refusal('native.boolean_or_packed_flags',
        {map2_gate: 'boolean', display_flags: '0..7'},
        {map2_gate: actual.map2_gate, display_flags: actual.display_flags});
    for (const [, key, bits] of COMMON_FIELDS_INTEGER_FIELDS) {
      try { signedRange(expected.values[key], bits, `expected ${key}`); }
      catch { return refusal(`original.${key}`, `signed${bits}`, expected.values[key]); }
      try { signedRange(actual[key], bits, `native ${key}`); }
      catch { return refusal(`native.${key}`, `signed${bits}`, actual[key]); }
    }
    if (typeof expected.values.map2_gate !== 'boolean')
      return refusal('original.map2_gate', 'boolean', expected.values.map2_gate);
    if (!Number.isInteger(expected.values.display_flags) ||
        expected.values.display_flags < 0 || expected.values.display_flags > 7)
      return refusal('original.display_flags', 'integer 0..7', expected.values.display_flags);
    const pairs = COMMON_FIELDS_INTEGER_FIELDS.map(([path, key]) => [path, expected.values[key], actual[key]]);
    pairs.splice(5, 0, ['map2.gate_mask_80', expected.values.map2_gate, actual.map2_gate]);
    for (const [index, mask] of [0x80, 0x40, 0x20].entries())
      pairs.push([`display.packed_flags_raw_hex_bit_${mask.toString(16)}`,
        (expected.values.display_flags & (1 << index)) !== 0 ? 1 : 0,
        (actual.display_flags & (1 << index)) !== 0 ? 1 : 0]);
    pairs.push(['rng.seed_hex', expected.seed_hex, actual.seed_hex]);
    let firstMismatch = null;
    for (const [path, want, got] of pairs) {
      const equal = Object.is(want, got);
      if (!equal && !firstMismatch) firstMismatch = {path, expected: want, actual: got};
    }
    const result = {schema: COMMON_FIELDS_COMPARISON_SCHEMA,
      scope: 'exact host-original typed source row and hash-bound seed versus one completed post-tick native value-copy',
      action_index: actionIndex, source_tick: expected.source_tick,
      original_sequence: expected.raw_identity.sequence, draw_ordinal: expected.draw_ordinal,
      compared_field_count: 15, seed_value_count: 1, exact_value_count: 16,
      decision: firstMismatch ? 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH'
        : 'PASS_EXACT_15_COMMON_FIELDS_AND_SEED',
      first_mismatch: firstMismatch};
    if (Buffer.byteLength(JSON.stringify(result), 'utf8') > COMMON_FIELDS_COMPARISON_MAX_BYTES)
      return refusal('comparison.summary_bytes', `<=${COMMON_FIELDS_COMPARISON_MAX_BYTES}`, 'overflow', 15);
    return result;
  } catch (error) {
    return refusal('common_fields.validation', 'strict typed record', String(error?.message ?? error));
  }
}

export function encodeRun2FullNativeManifest(full) {
  exactKeys(full, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'], 'full schedule manifest');
  exactKeys(full.checkpoints, ['c_action_index', 'c_source_tick', 'f_action_index',
    'f_source_tick', 'terminal_action_index', 'terminal_draw_ordinal'], 'full schedule checkpoints');
  validateRun2RuntimePayload({schema: 'melee-web-stadium-pad-action-plan', version: 1,
    bundles: full.bundles, schedule: full.schedule});
  const c = full.checkpoints;
  if (full.schema !== 'melee-web-stadium-run2-full-schedule-v3' || full.version !== 3 ||
      full.schedule.length !== 7167 || full.bundles.length !== 3585 ||
      full.schedule.filter(row => row.action === 'draw').length !== 3582 ||
      c.c_action_index !== 168 || c.f_action_index !== 170 || c.c_source_tick !== 84 ||
      c.f_source_tick !== 85 || c.terminal_action_index !== 7166 || c.terminal_draw_ordinal !== 3581 ||
      full.schedule[168]?.action !== 'tick' || full.schedule[168]?.bundle !== 84 ||
      full.schedule[170]?.action !== 'tick' || full.schedule[170]?.bundle !== 85 ||
      full.schedule[171]?.action !== 'draw' || full.schedule[172]?.action !== 'tick' ||
      full.schedule[173]?.action !== 'draw' || full.schedule.at(-1)?.action !== 'draw' ||
      full.schedule.at(-1)?.draw !== 3581)
    throw new TypeError('original full7167 manifest dimensions or terminal differ');
  return encodeNativeManifestRows(full, [0x4d, 0x57, 0x46, 0x33]);
}

export function encodeRun2FullCommonNativeManifest(full) {
  const bytes = encodeRun2FullNativeManifest(full);
  bytes.set([0x4d, 0x57, 0x43, 0x35]);
  return bytes;
}

export function encodeRun2NativeManifest(prefix) {
  exactKeys(prefix, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'],
    'run2 native prefix manifest');
  exactKeys(prefix.checkpoints, ['c_action_index', 'c_source_tick', 'f_action_index',
    'f_source_tick', 'terminal_action_index', 'terminal_draw_ordinal'],
  'run2 native prefix checkpoints');
  if (prefix.schema !== 'melee-web-stadium-run2-proper-prefix-v2' || prefix.version !== 2 ||
      !prefix.checkpoints || prefix.schedule.length > 7167 ||
      prefix.schedule.length !== prefix.checkpoints.terminal_action_index + 1 ||
      prefix.checkpoints.f_action_index + 3 !== prefix.schedule.length - 1 ||
      prefix.checkpoints.c_source_tick + 1 !== prefix.checkpoints.f_source_tick)
    throw new TypeError('run2 native prefix dimensions differ');
  validateRun2RuntimePayload({schema: 'melee-web-stadium-pad-action-plan', version: 1,
    bundles: prefix.bundles, schedule: prefix.schedule});
  const c = prefix.schedule[prefix.checkpoints.c_action_index];
  const f = prefix.schedule[prefix.checkpoints.f_action_index];
  if (c?.action !== 'tick' || c.bundle !== prefix.checkpoints.c_source_tick ||
      f?.action !== 'tick' || f.bundle !== prefix.checkpoints.f_source_tick ||
      prefix.schedule.slice(c.action_index + 1, f.action_index).some(action => action.action === 'tick') ||
      prefix.schedule[f.action_index + 1]?.action !== 'draw' ||
      prefix.schedule[f.action_index + 2]?.action !== 'tick' ||
      prefix.schedule.at(-1)?.action !== 'draw' ||
      prefix.schedule.at(-1)?.draw !== prefix.checkpoints.terminal_draw_ordinal)
    throw new TypeError('run2 native prefix C/F and terminal actions differ');
  return encodeNativeManifestRows(prefix, [0x4d, 0x57, 0x50, 0x32]);
}

function encodeNativeManifestRows(prefix, magic) {
  const bytes = new Uint8Array(24 + prefix.schedule.length * 57);
  bytes.set(magic);
  const write32 = (offset, value) => {
    if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)
      throw new TypeError('run2 native manifest integer is outside uint32');
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >>> 8) & 0xff;
    bytes[offset + 2] = (value >>> 16) & 0xff;
    bytes[offset + 3] = (value >>> 24) & 0xff;
  };
  write32(4, prefix.schedule.length);
  write32(8, prefix.checkpoints.c_action_index);
  write32(12, prefix.checkpoints.f_action_index);
  write32(16, prefix.checkpoints.c_source_tick);
  write32(20, prefix.checkpoints.f_source_tick);
  let draws = 0;
  for (let index = 0; index < prefix.schedule.length; ++index) {
    const action = prefix.schedule[index], offset = 24 + index * 57;
    const tick = action.action === 'tick';
    bytes[offset] = tick ? 1 : 2;
    write32(offset + 1, tick ? action.bundle : action.draw);
    write32(offset + 5, tick ? action.bundle : 0xffffffff);
    write32(offset + 9, draws);
    if (tick) bytes.set(bundleBytes(prefix.bundles[action.bundle]), offset + 13);
    else ++draws;
  }
  return bytes;
}

function requiredHex32(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-fA-F]{8}$/.test(value))
    throw new TypeError(`${label} is not an observed 32-bit source word`);
  return value.toLowerCase();
}

function compareField(fields, path, expected, actual) {
  fields.push({path, expected, actual});
  return expected === actual ? null : {path, expected, actual};
}

function requiredInteger(value, label) {
  if (!Number.isSafeInteger(value)) throw new TypeError(`${label} is not an observed integer`);
  return value;
}

function requiredBoolean(value, label) {
  if (typeof value !== 'boolean') throw new TypeError(`${label} is not an observed boolean`);
  return value;
}

export function compareOriginalIdleSnapshot({label, expected, expectedGo, expectedSeedHex, actual,
  coordinates = null}) {
  if (!['C', 'F', 'GO2'].includes(label))
    throw new TypeError('only C, F, and the explicit GO+2 checkpoint are comparable boundaries');
  if (!expected || expected.label !== label || !expectedGo || expectedGo.label !== 'GO' ||
      !actual || typeof actual !== 'object')
    throw new TypeError(`${label} comparison lacks its exact host-only source records`);
  const dynamic = coordinates ?? {cTick: 84, fTick: 85, goSequence: 2925,
    cSequence: 2926, fSequence: 2931, goDraw: 84, cDraw: 84, fDraw: 85};
  if (label === 'GO2' && (!Number.isSafeInteger(dynamic.go2Tick) ||
      !Number.isSafeInteger(dynamic.go2Sequence) || !Number.isSafeInteger(dynamic.go2Draw)))
    throw new TypeError('GO+2 comparison lacks its packet-bound dynamic source coordinates');
  const ordinal = label === 'C' ? dynamic.cTick : label === 'F' ? dynamic.fTick : dynamic.go2Tick;
  const drawOrdinal = label === 'C' ? dynamic.cDraw : label === 'F' ? dynamic.fDraw : dynamic.go2Draw;
  const goSequence = requiredInteger(expectedGo.sequence, 'original GO event sequence');
  const rowSequence = requiredInteger(expected.sequence, `${label} original state sequence`);
  if (goSequence !== dynamic.goSequence ||
      rowSequence !== (label === 'C' ? dynamic.cSequence : label === 'F' ? dynamic.fSequence : dynamic.go2Sequence) ||
      expectedGo.source_tick !== dynamic.cTick || expectedGo.draw_ordinal !== dynamic.goDraw ||
      expected.source_tick !== ordinal ||
      expected.draw_ordinal !== drawOrdinal || actual.ordinal !== ordinal || actual.label !== label)
    throw new TypeError(`${label} comparison is attached to the wrong input boundary`);

  const source = expected;
  if (source.map_root_allocation_lifetime_proved !== false ||
      source.map_root_object_type_observed !== false)
    throw new TypeError(`${label} original map-root evidence scope changed`);
  const idle = actual.idle_snapshot;
  if (!idle || typeof idle !== 'object') throw new TypeError(`${label} native idle receipt is absent`);
  if (source.display.timer_initialization_observed !== false || source.display.xF0_observed !== false ||
      idle.display_timer_initialization_observed !== 0 ||
      !/^[0-9a-fA-F]{8}$/.test(source.display.timer_raw_hex ?? '') ||
      !Array.isArray(idle.display_timer_native_bytes) || idle.display_timer_native_bytes.length !== 4 ||
      idle.display_timer_native_bytes.some(byte => !Number.isInteger(byte) || byte < 0 || byte > 255))
    throw new TypeError(`${label} raw timer/F0 initialization evidence is malformed`);
  const originalFlags = Number.parseInt(source.display.packed_flags_raw_hex, 16);
  if (!/^[0-9a-fA-F]{2}$/.test(source.display.packed_flags_raw_hex ?? '') ||
      !Number.isInteger(idle.display_flags) || idle.display_flags < 0 || idle.display_flags > 7 ||
      (idle.display_flags & ~7) !== 0)
    throw new TypeError(`${label} packed flag observations are malformed`);

  const fields = [];
  let mismatch = null;
  const add = (path, expectedValue, actualValue) => {
    const found = compareField(fields, path, expectedValue, actualValue);
    if (!mismatch && found) mismatch = found;
  };
  const sourceMap = source.map2, sourceDisplay = source.display;
  if (!sourceMap || !sourceDisplay) throw new TypeError(`${label} original typed state is absent`);
  const integerPairs = [
    ['map2.countdown_s32', sourceMap.countdown_s32, idle.map2_countdown],
    ['map2.phase_s16', sourceMap.phase_s16, idle.map2_phase],
    ['map2.form_s16', sourceMap.form_s16, idle.map2_form],
    ['map2.previous_form_s16', sourceMap.previous_form_s16, idle.map2_history_e0],
    ['map2.prior_form_s16', sourceMap.prior_form_s16, idle.map2_history_e2],
    ['map2.gate_mask_80', Number(requiredBoolean(sourceMap.gate_mask_80, `${label} map2 gate mask 0x80`)), idle.map2_gate],
    ['display.mode_s16', sourceDisplay.mode_s16, idle.display_mode],
    ['display.flags_s16', sourceDisplay.flags_s16, idle.display_e6],
    ['display.previous_flags_s16', sourceDisplay.previous_flags_s16, idle.display_e8],
    ['display.previous_mode_s16', sourceDisplay.previous_mode_s16, idle.display_previous_mode],
    ['display.focused_player_s16', sourceDisplay.focused_player_s16, idle.display_selected_player],
    ['display.selection_counter_s16', sourceDisplay.selection_counter_s16, idle.display_f2],
  ];
  for (const [path, expectedValue, actualValue] of integerPairs)
    add(path, requiredInteger(expectedValue, `${label} original ${path}`),
      requiredInteger(actualValue, `native ${path}`));
  for (const [index, mask] of [0x80, 0x40, 0x20].entries())
    add(`display.F8_${index}`, (originalFlags & mask) !== 0 ? 1 : 0,
      (idle.display_flags & (1 << index)) !== 0 ? 1 : 0);
  add('seed.raw32', requiredHex32(expectedSeedHex, `${label} original seed`),
    requiredHex32(actual.seed_hex, `${label} native seed`));

  const go = actual.go_trace;
  if (!go || go.failure !== 0 || go.armed !== 1 || actual.expected_branch !== 0 ||
      !Number.isSafeInteger(idle.world_generation) || idle.world_generation <= 0 ||
      go.generation !== idle.world_generation ||
      go.armed_world_ticks !== 0 || go.armed_source_frame !== 0 ||
      go.expected_branch !== 0 || go.stage_before?.sequence !== 1 ||
      go.stage_after?.sequence !== 2 || go.go_after?.sequence !== 3 ||
      go.hud_after?.sequence !== 0 || go.go_after?.source_branch !== 0 ||
      go.go_after?.world_ticks !== dynamic.cTick || idle.world_ticks !== ordinal + 1 ||
      [go.stage_before, go.stage_after, go.go_after].some(event =>
        event?.world_ticks !== dynamic.cTick || event?.generation !== idle.world_generation) ||
      go.next_sequence !== 3 ||
      go.stage_before?.sequence >= go.go_after?.sequence ||
      go.stage_after?.sequence >= go.go_after?.sequence)
    mismatch ??= {path: 'go_trace.membership', expected: `one ordered GO event in tick ${dynamic.cTick}`,
      actual: go ?? null};

  const approved = mismatch === null;
  const schema = label === 'GO2' ? 'melee-web-stadium-go2-declared-comparison-v1'
    : 'melee-web-stadium-matched-setup-cf-comparison-v1';
  const passDecision = label === 'GO2' ? 'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED'
    : 'PASS_EXACT_DECLARED_C_F_FIELDS';
  return {
    schema,
    phase: label,
    decision: approved ? passDecision : label === 'GO2'
      ? 'REFUSE_FIRST_GO2_FIELD_MISMATCH' : 'REFUSE_FIRST_C_F_FIELD_MISMATCH',
    compared_fields: fields,
    first_mismatch: mismatch,
    unpaired: {
      original_timer_raw_hex: source.display.timer_raw_hex,
      native_timer_raw_hex: idle.display_timer_native_bytes.map(byte => byte.toString(16).padStart(2, '0')).join(''),
      timer_initialization_observed: false,
      original_f0_observed: false,
      original_title: source.title,
      native_sis: {
        font_slot: idle.sis_font_slot,
        static_text_role: idle.sis_static_text_role,
        dynamic_text_role: idle.sis_dynamic_text_role,
        title_entry_seen: idle.sis_title_entry_seen,
        title_entry_index: idle.sis_title_entry_index,
        title_bytes_captured: idle.title_bytes_captured,
      },
      original_map_root_lifetime_proved: source.map_root_allocation_lifetime_proved,
      original_map_root_object_type_observed: source.map_root_object_type_observed,
      original_gate_mask_40: source.map2.gate_mask_40,
    },
  };
}

export function createHostCallbacks({setupApproved, originalSnapshots, originalSeeds, coordinates = null,
  go2Snapshot = null}) {
  if (typeof setupApproved !== 'function') throw new TypeError('strict Setup approval callback is required');
  if (!Array.isArray(originalSnapshots) || originalSnapshots.length !== 3 ||
      originalSnapshots.some(snapshot => !snapshot || !['GO', 'C', 'F'].includes(snapshot.label)))
    throw new TypeError('host-only original GO/C/F state records are required exactly once');
  const byLabel = new Map(structuredClone(originalSnapshots).map(snapshot => [snapshot.label, snapshot]));
  const seeds = new Map(Object.entries(originalSeeds ?? {}));
  const hasGo2 = go2Snapshot !== null;
  if (hasGo2 && (!go2Snapshot || go2Snapshot.label !== 'GO2' ||
      !Number.isSafeInteger(go2Snapshot.sequence) ||
      !Number.isSafeInteger(go2Snapshot.source_tick) ||
      !Number.isSafeInteger(go2Snapshot.draw_ordinal) || !seeds.has('GO2')))
    throw new TypeError('explicit GO+2 checkpoint and seed are malformed');
  if (byLabel.size !== 3 || !['C', 'F'].every(label => seeds.has(label)) ||
      [...seeds.keys()].some(label => !['C', 'F', ...(hasGo2 ? ['GO2'] : [])].includes(label)))
    throw new TypeError('host-only original GO/C/F state and observed C/F RNG evidence are required');
  const receipts = [];
  let nextBoundary = 'C';
  let refused = false;
  return {
    setupApproved() { return setupApproved() === true; },
    compare(phase, actualJson) {
      if (refused) return false;
      if (phase !== nextBoundary) {
        refused = true;
        receipts.push({schema: phase === 'GO2'
          ? 'melee-web-stadium-go2-declared-comparison-v1'
          : 'melee-web-stadium-matched-setup-cf-comparison-v1',
          phase, decision: phase === 'GO2' ? 'REFUSE_GO2_ORDER' : 'REFUSE_C_F_ORDER',
          expected_phase: nextBoundary});
        return false;
      }
      try {
        const actual = JSON.parse(actualJson);
        const expected = phase === 'GO2' ? go2Snapshot : byLabel.get(phase);
        const receipt = compareOriginalIdleSnapshot({label: phase,
          expected, expectedGo: byLabel.get('GO'),
          expectedSeedHex: seeds.get(phase), actual, coordinates});
        receipts.push(receipt);
        const accepted = receipt.decision === (phase === 'GO2'
          ? 'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED' : 'PASS_EXACT_DECLARED_C_F_FIELDS');
        if (accepted) nextBoundary = phase === 'C' ? 'F' : phase === 'F' && hasGo2 ? 'GO2' : 'complete';
        else refused = true;
        return accepted;
      } catch (error) {
        refused = true;
        receipts.push({schema: phase === 'GO2'
          ? 'melee-web-stadium-go2-declared-comparison-v1'
          : 'melee-web-stadium-matched-setup-cf-comparison-v1',
          phase, decision: phase === 'GO2' ? 'REFUSE_INVALID_GO2_RECEIPT'
            : 'REFUSE_INVALID_C_F_RECEIPT', error: String(error)});
        return false;
      }
    },
    receipts() { return structuredClone(receipts); },
  };
}

const HSD_QUEUE_OBSERVATION_SCHEMA = 'melee-web-stadium-hsd-pad-queue-observation';
const HSD_QUEUE_ACTUAL_OBSERVATION_VERSION = 2;
const HSD_QUEUE_EXPECTED_PLAYERS = Object.freeze([
  {player_index: 0, setup_port_1based: 1, source_slot_index: 0, controller_index: 0},
  {player_index: 1, setup_port_1based: 2, source_slot_index: 1, controller_index: 1},
]);
function queueObservationPlayersMatch(players, expectedPlayers) {
  if (!Array.isArray(players) || players.length !== expectedPlayers.length) return false;
  return players.every((row, index) => {
    const expected = expectedPlayers[index];
    if (!row || typeof row !== 'object' || Array.isArray(row) ||
        Object.keys(row).sort().join(',') !== Object.keys(expected).sort().join(',')) return false;
    return row.player_index === expected.player_index &&
      row.setup_port_1based === expected.setup_port_1based &&
      row.source_slot_index === expected.source_slot_index &&
      row.controller_index === expected.controller_index;
  });
}
function actualQueueObservationPlayersMatch(players, expectedPlayers) {
  if (!Array.isArray(players) || players.length !== expectedPlayers.length) return false;
  return players.every((row, index) => {
    const expected = expectedPlayers[index];
    const keys = ['controller_index', 'player_index', 'setup_port_1based',
      'source_slot_index', 'start_slot_field_raw'];
    if (!row || typeof row !== 'object' || Array.isArray(row) ||
        Object.keys(row).sort().join(',') !== keys.join(',')) return false;
    const raw = row.start_slot_field_raw;
    if (!Number.isSafeInteger(raw) || raw < 0 || raw > 255) return false;
    const sourcePort = raw === 0 ? row.source_slot_index : raw - 1;
    const effectivePort = sourcePort + 1;
    return Number.isSafeInteger(effectivePort) &&
      row.player_index === expected.player_index &&
      row.setup_port_1based === effectivePort &&
      row.setup_port_1based === expected.setup_port_1based &&
      row.source_slot_index === expected.source_slot_index &&
      row.controller_index === expected.controller_index;
  });
}
function validateRequiredQueueObservation(requirement, runtimePayload) {
  exactKeys(requirement, ['bundle', 'expected_players', 'expected_queue_slot_status_hex',
    'schema', 'source_sequence', 'source_tick', 'version'], 'required HSD queue observation');
  if (requirement.schema !== HSD_QUEUE_OBSERVATION_SCHEMA || requirement.version !== 1 ||
      requirement.bundle !== 0 || requirement.source_tick !== 0 ||
      requirement.source_sequence !== 2504)
    throw new TypeError('required HSD queue observation identity differs');
  if (!Array.isArray(requirement.expected_queue_slot_status_hex) ||
      requirement.expected_queue_slot_status_hex.length !== 4 ||
      requirement.expected_queue_slot_status_hex.some(value =>
        typeof value !== 'string' || !/^[0-9a-f]{22}$/.test(value)))
    throw new TypeError('required HSD queue observation expected rows are malformed');
  if (!queueObservationPlayersMatch(requirement.expected_players, HSD_QUEUE_EXPECTED_PLAYERS))
    throw new TypeError('required HSD queue observation Setup-derived player map differs');
  if (runtimePayload?.schedule?.[0]?.action !== 'tick' ||
      runtimePayload.schedule[0].ordinal !== 0 || runtimePayload.schedule[0].bundle !== 0 ||
      runtimePayload.bundles?.[0]?.bundle !== 0 ||
      runtimePayload.bundles[0].ports.map(row => row.status_hex).join(',') !==
        requirement.expected_queue_slot_status_hex.join(','))
    throw new TypeError('required HSD queue observation does not bind runtime bundle zero literally');
  return structuredClone(requirement);
}
function validateRequiredQueueSourceEvent(requirement, sourceEvents) {
  const event = sourceEvents?.[0];
  if (!Array.isArray(sourceEvents) || !event || event.ordinal !== 0 ||
      event.consumed_pad?.sequence !== requirement.source_sequence ||
      event.consumed_pad?.source_tick !== requirement.source_tick ||
      event.consumed_pad?.draw_ordinal !== requirement.source_tick ||
      event.consumed_pad?.event !== 'boundary')
    throw new TypeError('required HSD queue observation does not bind same-v6 consumed sequence 2504');
  return true;
}

export function hostEvidenceFromPacket(packet, {fullSchedule = false, commonFields = false,
  seedSidecar = null, seedIdentity = null} = {}) {
  if (typeof fullSchedule !== 'boolean' || typeof commonFields !== 'boolean')
    throw new TypeError('fullSchedule and commonFields must be boolean');
  if (commonFields && !fullSchedule)
    throw new TypeError('Per-tick common fields require the explicit full schedule');
  if (packet?.schema === 'melee-web-stadium-source-action-replay-packet' && packet.version === 2) {
    if (packet.host_only && typeof packet.host_only === 'object' &&
        Object.hasOwn(packet.host_only, 'required_queue_observation'))
      throw new TypeError('required HSD queue observation is unsupported for run2 action packets');
    return hostEvidenceFromRun2Packet(packet, {fullSchedule, commonFields, seedSidecar, seedIdentity});
  }
  if (fullSchedule || commonFields) throw new TypeError('Full schedule requires original run2 packet');
  exactKeys(packet, ['schema', 'version', 'host_only', 'runtime_payload'], 'matched Setup packet');
  if (packet.schema !== 'melee-web-stadium-setup-cf-input-schedule' ||
      packet.version !== 1 || !packet.host_only || typeof packet.host_only !== 'object')
    throw new TypeError('matched Setup packet identity differs');
  validateRuntimePayload(packet.runtime_payload);
  const summary = packet.host_only.expected_original_state?.summary;
  const snapshots = summary?.snapshots;
  const rng = packet.host_only.boundary_rng_observations;
  if (summary?.decision !== 'PASS_ORIGINAL_STATE_ONLY_COUNTERPART' ||
      summary?.schema !== 'melee-web-original-stadium-idle-state' ||
      summary?.source?.dol_sha256 !== 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646' ||
      summary?.source?.dol_sha1 !== '08e0bf20134dfcb260699671004527b2d6bb1a45' ||
      !Array.isArray(snapshots) || snapshots.length !== 3 ||
      snapshots.map(row => row?.label).join(',') !== 'GO,C,F' ||
      summary.guest_pointer_numeric_comparison !== false ||
      !Array.isArray(rng) || rng.length !== 3 ||
      rng.map(row => row?.label).join(',') !== 'GO,C,F')
    throw new TypeError('matched Setup host-only original evidence differs');
  const seeds = {};
  for (const row of rng) {
    if (row.sequence !== ({GO: 2925, C: 2926, F: 2931})[row.label] ||
        row.pc !== (row.label === 'GO' ? '0x8016b824' : '0x80390eb4') ||
        row.pointer_slot_address !== 0x804D5F94 ||
        !/^[0-9a-f]{8}$/.test(row.pointer_raw_hex ?? '') ||
        Number.parseInt(row.pointer_raw_hex, 16) !== row.seed_address ||
        row.seed_raw_hex !== row.seed_hex ||
        row.event !== (row.label === 'GO' ? 'progress' : 'boundary') ||
        row.source_tick !== (row.label === 'F' ? 85 : 84) ||
        row.draw_ordinal !== 84 + (row.label === 'F' ? 1 : 0) ||
        row.pointer_equals_seed_address !== true ||
        !/^[0-9a-f]{8}$/.test(row.seed_hex ?? '') ||
        !/^[0-9a-f]{8}$/.test(row.seed_raw_hex ?? ''))
      throw new TypeError(`original ${row.label} RNG observation is malformed`);
    if (row.label === 'C' || row.label === 'F') seeds[row.label] = row.seed_hex;
  }
  const requiredQueueObservation = Object.hasOwn(packet.host_only, 'required_queue_observation')
    ? validateRequiredQueueObservation(packet.host_only.required_queue_observation,
        packet.runtime_payload)
    : null;
  if (requiredQueueObservation)
    validateRequiredQueueSourceEvent(requiredQueueObservation,
      packet.host_only.source_event_provenance?.events);
  return {runtimePayload: packet.runtime_payload, originalSnapshots: snapshots,
    originalSeeds: seeds, requiredQueueObservation};
}

export function hostEvidenceFromRun2Packet(packet, {fullSchedule = false, commonFields = false,
  seedSidecar = null, seedIdentity = null} = {}) {
  if (typeof fullSchedule !== 'boolean' || typeof commonFields !== 'boolean' ||
      (commonFields && !fullSchedule))
    throw new TypeError('Per-tick common fields require a boolean full-schedule opt-in');
  exactKeys(packet, ['host_only', 'runtime_payload', 'schema', 'version'], 'run2 action packet');
  if (packet.schema !== 'melee-web-stadium-source-action-replay-packet' || packet.version !== 2)
    throw new TypeError('run2 action packet identity differs');
  const hostOnly = packet.host_only;
  if (hostOnly?.schema !== 'melee-web-stadium-source-host-expectations' || hostOnly.version !== 1 ||
      hostOnly.source_identity?.strict_route?.source?.dol_sha256 !==
        'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646' ||
      hostOnly.source_identity?.strict_route?.source?.dol_sha1 !==
        '08e0bf20134dfcb260699671004527b2d6bb1a45' ||
      hostOnly.expected_state?.comparison_scope?.setup_profile_verified_by_observer !== false ||
      hostOnly.expected_state?.comparison_scope?.profile_equivalence?.startsWith('pending') !== true)
    throw new TypeError('run2 host-only source identity or evidence scope differs');
  if ((fullSchedule || commonFields) && hostOnly.expected_state.GO2 !== undefined)
    throw new TypeError('GO+2 declared-state comparison is scoped only to the bounded proper-prefix branch');
  const derived = fullSchedule ? deriveRun2FullSchedule(packet.runtime_payload, hostOnly)
    : deriveRun2ProperPrefix(packet.runtime_payload, hostOnly);
  const normalize = label => {
    const row = derived.expectedState[label];
    const raw = row?.raw_identity;
    const fields = row?.typed_fields;
    if (row?.boundary_role !== label || !raw || raw.event !== 'boundary' ||
        raw.boundary !== 'source_tick' || !fields || fields.label !== label ||
        fields.sequence !== raw.sequence || fields.source_tick !== raw.source_tick ||
        fields.draw_ordinal !== raw.draw_ordinal || !fields.map2 || !fields.display ||
        fields.map_root_allocation_lifetime_proved !== false ||
        fields.map_root_object_type_observed !== false)
      throw new TypeError(`run2 ${label} typed state expectation is malformed`);
    return {label, sequence: raw.sequence, source_tick: raw.source_tick,
      draw_ordinal: raw.draw_ordinal, map2: fields.map2, display: fields.display,
      title: fields.title, map_root_allocation_lifetime_proved:
        fields.map_root_allocation_lifetime_proved,
      map_root_object_type_observed: fields.map_root_object_type_observed};
  };
  const goRow = derived.expectedState.GO;
  const goRaw = goRow?.raw_identity, goFields = goRow?.typed_fields;
  if (!goRaw || goRaw.event !== 'progress' || !goFields || goFields.label !== 'GO' ||
      goFields.sequence !== goRaw.sequence || goFields.source_tick !== goRaw.source_tick ||
      goFields.draw_ordinal !== goRaw.draw_ordinal || goRaw.phase !== 'go_after')
    throw new TypeError('run2 GO expected source row is malformed');
  const go = {label: 'GO', sequence: goRaw.sequence, source_tick: goRaw.source_tick,
    draw_ordinal: goRaw.draw_ordinal};
  const snapshots = [go, normalize('C'), normalize('F')];
  let go2Snapshot = null, go2QueueExpectation = null;
  if (derived.go2Expected) {
    const state = derived.go2Expected;
    const raw = state.raw_identity;
    const fields = state.typed_fields;
    go2Snapshot = {label: 'GO2', sequence: raw.sequence, source_tick: raw.source_tick,
      draw_ordinal: raw.draw_ordinal, map2: fields.map2, display: fields.display,
      title: fields.title, map_root_allocation_lifetime_proved:
        fields.map_root_allocation_lifetime_proved,
      map_root_object_type_observed: fields.map_root_object_type_observed};
    go2QueueExpectation = {
      schema: 'melee-web-stadium-go2-queue-expectation-v1',
      source_sequence: state.source_queue.consume_sequence,
      source_tick: raw.source_tick, draw_ordinal: raw.draw_ordinal,
      expected_queue_slot_status_hex: [...state.source_queue.queue_slot_status_hex],
      expected_players: structuredClone(HSD_QUEUE_EXPECTED_PLAYERS),
    };
  }
  const commonFieldRowsByAction = commonFields
    ? deriveRun2CommonTickExpectations(packet.runtime_payload, hostOnly) : null;
  const commonRowsByAction = commonFields
    ? deriveRun2CommonSeedExpectations(packet.runtime_payload, hostOnly,
      commonFieldRowsByAction, seedSidecar, seedIdentity) : null;
  const coordinates = {cTick: derived.c.source_tick, fTick: derived.f.source_tick,
    goSequence: go.sequence, cSequence: derived.c.sequence, fSequence: derived.f.sequence,
    goDraw: go.draw_ordinal, cDraw: derived.c.draw_ordinal, fDraw: derived.f.draw_ordinal,
    ...(go2Snapshot ? {go2Sequence: go2Snapshot.sequence,
      go2Tick: go2Snapshot.source_tick, go2Draw: go2Snapshot.draw_ordinal} : {})};
  return {runtimePayload: derived.prefix, sourceRuntimePayload: packet.runtime_payload,
    originalSnapshots: snapshots, originalSeeds: derived.expectedSeeds, coordinates,
    go2Snapshot, go2QueueExpectation, go2ActionIndex: derived.go2ActionIndex,
    checkpoints: derived.prefix.checkpoints,
    mode: commonFields ? 'run2-full-common-fields-seed-v1' :
      fullSchedule ? 'run2-full-schedule-v3' : 'run2-proper-prefix-v2',
    ...(commonFields ? {commonRowsByAction} : {})};
}

function bundleBytes(bundle) {
  const hex = bundle.ports.map(port => port.status_hex).join('');
  return Uint8Array.from(hex.match(/.{2}/g), pair => Number.parseInt(pair, 16));
}

export class StadiumSetupCfActionDriver {
  constructor({runtimePayload, native, commonFields = false, commonRowsByAction = null,
    admittedWorldGeneration = null, requiredQueueObservation = null,
    go2QueueExpectation = null}) {
    if (typeof commonFields !== 'boolean' || (commonFields &&
        runtimePayload?.schema !== 'melee-web-stadium-run2-full-schedule-v3'))
      throw new TypeError('Per-tick common fields require an explicit full-schedule manifest');
    this.mode = commonFields ? 'run2-full-common-fields-seed-v1'
      : runtimePayload?.schema === 'melee-web-stadium-run2-full-schedule-v3'
      ? 'run2-full-schedule-v3' : runtimePayload?.schema === 'melee-web-stadium-run2-proper-prefix-v2'
      ? 'run2-proper-prefix-v2' : 'legacy-v1';
    this.commonFields = commonFields;
    this.commonRowsByAction = commonFields ? commonRowsByAction : null;
    const generationBound = commonFields || requiredQueueObservation !== null || go2QueueExpectation !== null;
    if (generationBound && (!Number.isSafeInteger(admittedWorldGeneration) || admittedWorldGeneration <= 0))
      throw new TypeError('Per-tick common fields or queue observation require the positive safe generation from checked Setup');
    this.admittedWorldGeneration = generationBound ? admittedWorldGeneration : null;
    if (commonFields && (!(commonRowsByAction instanceof Map) || commonRowsByAction.size !== 3501))
      throw new TypeError('Per-tick common source expectations must contain exactly3501 mapped rows');
    if (commonFields) {
      for (let index = 0; index < runtimePayload.schedule.length; ++index) {
        const action = runtimePayload.schedule[index];
        const selected = action.action === 'tick' && action.bundle >= 84 && action.bundle <= 3584;
        const row = commonRowsByAction.get(index);
        if (selected ? (!row || row.action_index !== index || row.source_tick !== action.bundle)
          : row !== undefined)
          throw new TypeError(`Per-tick common source expectation ${index} is outside its exact action mapping`);
      }
    }
    if (this.mode === 'legacy-v1') this.payload = validateRuntimePayload(runtimePayload);
    else {
      exactKeys(runtimePayload, ['bundles', 'checkpoints', 'schedule', 'schema', 'version'],
        'run2 proper-prefix manifest');
      if (runtimePayload.version !== (['run2-full-schedule-v3', 'run2-full-common-fields-seed-v1'].includes(this.mode) ? 3 : 2) || !Array.isArray(runtimePayload.bundles) ||
          !Array.isArray(runtimePayload.schedule) || !runtimePayload.schedule.length ||
          runtimePayload.schedule.length !== runtimePayload.checkpoints.terminal_action_index + 1 ||
          runtimePayload.schedule.at(-1)?.action !== 'draw')
        throw new TypeError('run2 proper-prefix manifest is malformed');
      this.payload = runtimePayload;
      this.nativeManifest = this.mode === 'run2-full-common-fields-seed-v1'
        ? encodeRun2FullCommonNativeManifest(runtimePayload)
        : this.mode === 'run2-full-schedule-v3'
          ? encodeRun2FullNativeManifest(runtimePayload) : encodeRun2NativeManifest(runtimePayload);
    }
    this.requiredQueueObservation = requiredQueueObservation === null ? null
      : validateRequiredQueueObservation(requiredQueueObservation, this.payload);
    this.go2QueueExpectation = null;
    if (go2QueueExpectation !== null) {
      exactKeys(go2QueueExpectation, ['draw_ordinal', 'expected_players',
        'expected_queue_slot_status_hex', 'schema', 'source_sequence', 'source_tick'],
      'GO+2 queue expectation');
      const actionIndex = this.payload.schedule.findIndex(action =>
        action.action === 'tick' && action.bundle === go2QueueExpectation.source_tick);
      if (go2QueueExpectation.schema !== 'melee-web-stadium-go2-queue-expectation-v1' ||
          !Number.isSafeInteger(go2QueueExpectation.source_sequence) ||
          go2QueueExpectation.source_tick !== go2QueueExpectation.draw_ordinal ||
          !Array.isArray(go2QueueExpectation.expected_queue_slot_status_hex) ||
          go2QueueExpectation.expected_queue_slot_status_hex.length !== 4 ||
          go2QueueExpectation.expected_queue_slot_status_hex.some(value =>
            typeof value !== 'string' || !/^[0-9a-f]{22}$/.test(value)) ||
          !queueObservationPlayersMatch(go2QueueExpectation.expected_players, HSD_QUEUE_EXPECTED_PLAYERS) ||
          actionIndex < 0 || this.payload.schedule[actionIndex - 1]?.action !== 'draw' ||
          this.payload.schedule[actionIndex + 1]?.action !== 'draw' ||
          this.payload.bundles[go2QueueExpectation.source_tick]?.ports?.map(port => port.status_hex).join(',') !==
            go2QueueExpectation.expected_queue_slot_status_hex.join(','))
        throw new TypeError('GO+2 queue expectation is not the exact same-producer scheduled four-port row');
      this.go2QueueExpectation = {...structuredClone(go2QueueExpectation), action_index: actionIndex};
    }
    if (!native || typeof native.arm !== 'function' || typeof native.queue !== 'function' ||
        typeof native.approve !== 'function')
      throw new TypeError('bounded native arm/queue/approval API is required');
    this.native = native;
    this.cursor = 0;
    this.armed = false;
    this.pending = false;
    this.inFlight = false;
    this.failed = false;
    this.complete = false;
    this.firstError = null;
    this.sourceFrame = 0; // Completed Setup is admitted at original match clock zero.
  }

  async arm(setupApproved) {
    if (this.inFlight || this.armed || this.failed || this.complete)
      return this._fail('duplicate or terminal arm');
    if (setupApproved !== true)
      return this._fail('fresh completed Setup owner/approval was refused');
    this.inFlight = true;
    try {
      const manifest = this.mode === 'legacy-v1' ? null : this.nativeManifest;
      if (await this.native.arm(manifest) !== 1)
        return this._fail('fresh completed Setup owner/approval was refused');
      if (this.failed) return false;
      this.armed = true;
      return true;
    } catch (error) {
      return this._fail(`native arm rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  async submit(action) {
    if (this.inFlight || !this.armed || this.failed || this.complete || this.pending)
      return this._fail('action submitted without a ready one-use continuation');
    const expected = this.payload.schedule[this.cursor];
    if (!expected || JSON.stringify(action) !== JSON.stringify(expected))
      return this._fail('out-of-order or altered source action');
    const scheduleIndex = this.cursor;
    const bytes = action.action === 'tick'
      ? bundleBytes(this.payload.bundles[action.bundle]) : new Uint8Array();
    this.inFlight = true;
    try {
      if (await this.native.queue(scheduleIndex, action, bytes) !== 1)
        return this._fail('native source action was refused');
      if (this.failed) return false;
      this.pending = true;
      return true;
    } catch (error) {
      return this._fail(`native queue rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  async accept(observation, hostComparison = null, commonComparison = null) {
    if (this.inFlight || !this.pending || this.failed || this.complete)
      return this._fail('unexpected or repeated native action receipt');
    const action = this.payload.schedule[this.cursor];
    if (this.mode === 'run2-full-schedule-v3' && (observation?.schema !== 'melee-web-stadium-source-action-native-v3' ||
        observation?.scope !== 'original-full7167-schedule-observation-only' || observation?.action_count !== 7167))
      return this._fail('Full schedule native observation scope differs');
    if (this.mode === 'run2-full-common-fields-seed-v1' &&
        (observation?.schema !== 'melee-web-stadium-source-action-native-v5' ||
         observation?.scope !== 'original-full7167-common-fields-seed-observation-only' ||
         observation?.common_field_count !== 15 || observation?.seed_value_count !== 1 ||
         observation?.action_count !== 7167))
      return this._fail('Per-tick common-field native observation scope differs');
    const result = observation?.last_action;
    const ordinal = this.mode === 'legacy-v1' ? action?.ordinal :
      action?.action === 'tick' ? action.bundle : action?.draw;
    if (this.mode !== 'legacy-v1') {
      const ticksBefore = this.payload.schedule.slice(0, this.cursor)
        .filter(row => row.action === 'tick').length;
      const drawsBefore = this.payload.schedule.slice(0, this.cursor)
        .filter(row => row.action === 'draw').length;
      const ticksAfter = ticksBefore + (action.action === 'tick' ? 1 : 0);
      const drawsAfter = drawsBefore + (action.action === 'draw' ? 1 : 0);
      const consumed = action.action === 'tick'
        ? this.payload.bundles[action.bundle].ports.map(port => port.status_hex).join('') : null;
      if (result?.source_ticks_before !== ticksBefore || result?.source_ticks_after !== ticksAfter ||
          result?.source_draws_before !== drawsBefore || result?.source_draws_after !== drawsAfter ||
          result?.world_ticks !== ticksAfter ||
          result?.source_tick_input_pad_hex !== consumed)
        return this._fail('native v2 receipt counters or scheduled raw-argument PAD differ from the manifest');
      const before = result.source_frames_before, after = result.source_frames_after;
      // Original fn_8016CD98 may keep the match clock still during a tick
      // (notably HUD0 Ready); draw/pending/ack intervals cannot advance it.
      if (!Number.isSafeInteger(before) || !Number.isSafeInteger(after) ||
          before < 0 || after < before || before > 0xffffffff || after > 0xffffffff ||
          before !== this.sourceFrame || result.source_frames !== after ||
          (action.action === 'tick' ? after - before > 1 : after !== before))
        return this._fail('native v2 original match clock transition or continuity differs');
    }
    if (observation?.failed || !result || result.complete !== true ||
        result.schedule_index !== this.cursor || result.action !== action.action ||
        result.ordinal !== ordinal ||
        (action.action === 'tick' && result.bundle !== action.bundle))
      return this._fail(observation?.error || 'native completion receipt differs from the pending action');
    const cIndex = this.mode === 'legacy-v1' ? 168 : this.payload.checkpoints.c_action_index;
    const fIndex = this.mode === 'legacy-v1' ? 170 : this.payload.checkpoints.f_action_index;
    const isBoundary = action.action === 'tick' && (this.cursor === cIndex || this.cursor === fIndex);
    const isGo2 = this.go2QueueExpectation !== null &&
      action.action === 'tick' && this.cursor === this.go2QueueExpectation.action_index;
    let refusal = null;
    if (this.requiredQueueObservation && action.action === 'tick' &&
        action.bundle === this.requiredQueueObservation.bundle) {
      const queue = result.pad_hsd_queue_observation;
      const expectedPlayers = this.requiredQueueObservation.expected_players;
      if (queue?.schema !== HSD_QUEUE_OBSERVATION_SCHEMA ||
          queue.version !== HSD_QUEUE_ACTUAL_OBSERVATION_VERSION ||
          result.world_generation !== this.admittedWorldGeneration ||
          queue.source_generation !== this.admittedWorldGeneration ||
          queue.source_tick_before !== this.requiredQueueObservation.source_tick ||
          queue.source_tick_after !== this.requiredQueueObservation.source_tick + 1 ||
          queue.qnum_before !== 1 || queue.qread_before !== 0 || queue.qcount_before !== 1 ||
          queue.qcount_after_hsd !== 0 || queue.player_count !== expectedPlayers.length ||
          queue.tick_complete !== true ||
          !actualQueueObservationPlayersMatch(queue.players, expectedPlayers) ||
          !Array.isArray(queue.queue_slot_status_hex) || queue.queue_slot_status_hex.length !== 4 ||
          queue.queue_slot_status_hex.some((status, index) =>
            status !== this.requiredQueueObservation.expected_queue_slot_status_hex[index]))
        refusal = 'HSD queue-slot observation, Setup map, world generation, or completed tick differs';
    }
    if (isGo2) {
      const queue = result.pad_hsd_queue_observation;
      const expected = this.go2QueueExpectation;
      if (action.bundle !== expected.source_tick || result.world_generation !== this.admittedWorldGeneration ||
          queue?.schema !== HSD_QUEUE_OBSERVATION_SCHEMA ||
          queue.version !== HSD_QUEUE_ACTUAL_OBSERVATION_VERSION ||
          queue.source_generation !== this.admittedWorldGeneration ||
          queue.source_tick_before !== expected.source_tick ||
          queue.source_tick_after !== expected.source_tick + 1 ||
          queue.qnum_before !== 1 || queue.qread_before !== 0 || queue.qcount_before !== 1 ||
          queue.qcount_after_hsd !== 0 || queue.player_count !== expected.expected_players.length ||
          queue.tick_complete !== true ||
          !actualQueueObservationPlayersMatch(queue.players, expected.expected_players) ||
          !Array.isArray(queue.queue_slot_status_hex) || queue.queue_slot_status_hex.length !== 4 ||
          queue.queue_slot_status_hex.some((status, index) =>
            status !== expected.expected_queue_slot_status_hex[index]))
        refusal ??= 'GO+2 HSD queue, Setup map, admitted generation, or completed tick differs';
    }
    if (isBoundary) {
      const expectedLabel = this.cursor === cIndex ? 'C' : 'F';
      if (this.mode !== 'legacy-v1' &&
          result.observation?.idle_snapshot?.source_frame !== result.source_frames_after)
        refusal = `${expectedLabel} snapshot match clock differs from its action receipt`;
      if (hostComparison?.phase !== expectedLabel ||
          hostComparison?.decision !== 'PASS_EXACT_DECLARED_C_F_FIELDS')
        refusal = `${expectedLabel} host comparison failed or is absent`;
    } else if (isGo2) {
      if (result.source_frames_after !== result.go2_state_observation?.idle_snapshot?.source_frame ||
          hostComparison?.phase !== 'GO2' ||
          hostComparison?.decision !== 'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED')
        refusal ??= 'GO+2 declared state/seed comparison or action clock binding failed';
    } else if (hostComparison !== null) {
      refusal = 'host comparison was supplied outside C/F/GO2';
    }
    if (this.mode === 'run2-full-common-fields-seed-v1') {
      const selectedTick = action.action === 'tick' && action.bundle >= 84 && action.bundle <= 3584;
      const expectedCommon = this.commonRowsByAction.get(this.cursor);
      if (selectedTick) {
        if (!expectedCommon || expectedCommon.source_tick !== action.bundle ||
            expectedCommon.action_index !== this.cursor ||
            result.common_fields_sample?.schema !== COMMON_FIELDS_NATIVE_SAMPLE_SCHEMA ||
            result.common_fields_sample?.schedule_index !== this.cursor ||
            result.common_fields_sample?.source_tick !== action.bundle ||
            result.common_fields_sample?.world_generation !== this.admittedWorldGeneration ||
            commonComparison?.schema !== COMMON_FIELDS_COMPARISON_SCHEMA ||
            commonComparison?.action_index !== this.cursor ||
            commonComparison?.source_tick !== action.bundle ||
            commonComparison?.original_sequence !== expectedCommon.raw_identity.sequence ||
            commonComparison?.compared_field_count !== 15 ||
            commonComparison?.seed_value_count !== 1 || commonComparison?.exact_value_count !== 16 ||
            commonComparison?.decision !== 'PASS_EXACT_15_COMMON_FIELDS_AND_SEED')
          refusal ??= 'per-tick common-field comparison failed or is absent';
      } else if (expectedCommon || result.common_fields_sample !== null || commonComparison !== null) {
        refusal ??= 'per-tick common-field sample/comparison was supplied outside its source-tick scope';
      }
    } else if (commonComparison !== null) {
      refusal ??= 'common-field comparison was supplied without the explicit scope';
    }
    this.inFlight = true;
    if (refusal) this._fail(refusal);
    try {
      const approved = await this.native.approve(this.cursor, refusal === null);
      if (approved !== 1)
        return this._fail('native source owner refused the one-use acknowledgement');
      if (this.failed) return false;
      if (this.mode !== 'legacy-v1') this.sourceFrame = result.source_frames_after;
      this.pending = false;
      ++this.cursor;
      if (this.cursor === (this.mode === 'legacy-v1' ? PLAN_ACTIONS : this.payload.schedule.length))
        this.complete = true;
      return true;
    } catch (error) {
      return this._fail(`native approval rejected: ${error?.message ?? String(error)}`);
    } finally { this.inFlight = false; }
  }

  status() {
    return {cursor: this.cursor, armed: this.armed, pending: this.pending, in_flight: this.inFlight,
      failed: this.failed, complete: this.complete, first_error: this.firstError};
  }

  _fail(error) {
    this.failed = true;
    this.pending = false;
    this.firstError ??= error;
    return false;
  }
}
