import {createHash} from 'node:crypto';

export const FRESH132_FULL_A_INPUT_PINS = Object.freeze({
  extract: {sha256: 'c70a1063ac8af4f7a4b00a5f300ab0f4d7cf4359d6ac456f0e10e2b743a31463'},
  binding: {sha256: 'a5367916b0a48908d6f4c787b35f2417f463524d1696f2be8da98d27e60ec028'},
  postrun: {sha256: '591a478d31d936065df595fd8e240da7304341ee463b6fbe897d74d3a5a5d635'},
  readyExpected: {sha256: '5011f00a1ac910adbbe8d8f6b9793bc6e2c88129d8c24b33a21d3e2211dae9d1'},
});
const CANONICAL_PINS = Object.freeze({
  bundles: '2b745819e14839a7928fa789790502158705eff92ad75226a2be0f644d4fdd3c',
  schedule: '218d0886bf05cf61237dd5a981319ef8f83e54d0a41e44cddd7284a4d5a52fd7',
  typedStates: '47bb362f896df4a07604d42560e909f5488f055a6bc67eafb19c06eceba6b835',
  actionProvenance: '74bad45630213f93dce97aac377983fb7f2c358971f424362bdfc72d813afc4d',
  bundleProvenance: '9fe732562a49660244fbce1613400ff5ee4dddaa2b083e2ce72758b093202abd',
  startContext: '57b95818b795601fa7bd68160a8b923f3d4d8c1ec4332746076e5dfacd55a245',
  goSeed: 'dc9ee607e7016067af8dab40b8193c57dda32abea2402b510be558676ec476cf',
});
const STREAM_SHA256 = '2c1b69f6afbb47f1d71de2f6628b2c62d16839bbb7859f630c20082d6e363452';
const INPUT_SHA256 = 'c401f8598df35d404aae7208f3499b5945fc976e3878bee6517a95a8a942a79b';
const DOL_SHA1 = '08e0bf20134dfcb260699671004527b2d6bb1a45';
const DOL_SHA256 = 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646';
const DOLPHIN_COMMIT = 'c77bbaa0f372c3f72281602a8b087206706542cb';
const EXPECTED_SCOPE = 'fresh prepared Start followed by same-producer original VS Setup typed Start, RNG and one-entry constructor scalar projection; no browser, whole-session, lifetime-B or C3 claim.';
const GUEST_POINTER_POLICY = 'fixed-slot role and pointer equality only; no object-type or allocation-lifetime proof; numeric guest pointers are not cross-runtime comparison fields';

function fail(message) {
  throw new TypeError(message);
}

function exactKeys(value, keys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...keys].sort().join(','))
    fail(label + ' has an unexpected shape');
}

function canonical(value) {
  if (Array.isArray(value)) return '[' + value.map(canonical).join(',') + ']';
  if (value && typeof value === 'object')
    return '{' + Object.keys(value).sort().map(key => JSON.stringify(key) + ':' + canonical(value[key])).join(',') + '}';
  return JSON.stringify(value);
}

function hash(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

function canonicalHash(value) {
  return hash(Buffer.from(canonical(value)));
}

function same(left, right) {
  return canonical(left) === canonical(right);
}

function parsePinned(name, value, pin) {
  const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value ?? []);
  if (hash(bytes) !== pin.sha256) fail(name + ' bytes do not match the pinned input identity');
  try {
    return JSON.parse(bytes.toString('utf8'));
  } catch {
    fail(name + ' is not valid UTF-8 JSON');
  }
}

export function validateFresh132FullAProducerJoin(extract, binding, postrun) {
  const route = extract?.host_only?.route;
  const files = binding?.files;
  const joins = postrun?.same_run_joins;
  exactKeys(binding, ['files', 'producer', 'schema', 'version'], 'Fresh132 adapter binding');
  if (extract?.schema !== 'melee-web-fresh132-idle-reference-extract' || extract.version !== 1 ||
      extract.decision !== 'PASS_OFFLINE_ORIGINAL_IDLE_REFERENCE_COMPONENT_ONLY' ||
      extract.same_run_provenance_verified !== false ||
      extract.claims?.browser_execution !== false || extract.claims?.lifetime_B !== false ||
      extract.claims?.per_owner_rng_equivalence !== false ||
      extract.claims?.actual_capture_acceptance !== false || extract.claims?.C3 !== false ||
      binding.schema !== 'melee-web-fresh132-idle-reference-binding' || binding.version !== 1 ||
      !same(extract.source_identity, binding) ||
      postrun?.result !== 'PASS_ACTUAL_SAME_RUN_JOINS_AND_FRESH132_IDLE_REFERENCE_COMPONENT' ||
      postrun.counts?.ticks !== 3585 || postrun.counts?.combined_draws !== 3583 ||
      postrun.counts?.combined_driver_actions !== 7168 || postrun.counts?.post_go_raw_rows !== 10499 ||
      postrun.counts?.post_go_driver_actions !== 7000 ||
      postrun.binding?.sha256 !== FRESH132_FULL_A_INPUT_PINS.binding.sha256 ||
      postrun.extract?.sha256 !== FRESH132_FULL_A_INPUT_PINS.extract.sha256 ||
      postrun.adapter_same_run_provenance_verified !== false ||
      postrun.external_same_run_joins_verified !== true ||
      !joins?.producer_joins?.same_run_binary_manifest_profile_GCI_report_launch_hash_joins ||
      joins?.producer_joins?.requests_are_not_consumed_schedule !== true ||
      joins?.source_consumption?.request_history_separate !== true ||
      joins?.source_consumption?.driver_phase_receipts_equal_raw !== true ||
      joins?.source_consumption?.actual_consumed_four_port_rows !== 3999 ||
      joins?.source_consumption?.post_GO_samples !== 3501 ||
      joins?.source_consumption?.dynamic_C !== 84 ||
      joins?.source_consumption?.comparison_end_tick !== 3584 ||
      joins?.source_consumption?.natural_tail_ticks !== 0)
    fail('Fresh132 extract, binding and independent postrun producer joins differ');
  if (files?.observer?.sha256 !== STREAM_SHA256 || files.observer.bytes !== route.stream_bytes ||
      route.stream_sha256 !== STREAM_SHA256 || route.stream_records !== 20507 ||
      files.inputs?.sha256 !== INPUT_SHA256 ||
      binding.producer?.build?.dolphin_commit !== DOLPHIN_COMMIT ||
      binding.producer?.build?.target !== 'dolphin-nogui' ||
      binding.producer?.build?.binary_sha256 !== '4bebd3a21cf48da558c7380f45b73e394fe46f47cef774d939cd4659051918fa' ||
      binding.producer?.build?.sha256 !== '2da988e27c334b4530935ea32af3edc319707482e276b064e9b08c132d4cedbd' ||
      route.source?.dolphin_commit !== DOLPHIN_COMMIT ||
      route.source?.dol_sha1 !== DOL_SHA1 || route.source?.dol_sha256 !== DOL_SHA256 ||
      binding.producer?.profile_gci_sha256 !== '5184f7f9bfcbd35ea7cc07904cbed557b8a7fc9e624a05aa02c8d1d308d4d729' ||
      binding.producer?.menu_recipe_sha256 !== '85d64433715ba61db429b0c15de70f7c48b6689c877e061c5111f58bd7c2957f' ||
      files.recipe_input?.sha256 !== binding.producer.menu_recipe_sha256 ||
      files.menu_recipe?.sha256 !== '5f4dd95fb2e00cd35aa9fa42de10758acc4998624c964a1a077d683ebebe95f8' ||
      files.intentions?.sha256 !== '2c1a504d4ae598a9015991d9d383bde557d549763b32ba5a8d8d484f11415289' ||
      files.launch?.sha256 !== 'c637b2d317a34bd89d1b2101421d5a471de1e5544a84ce7fd0bedf1841e096e7')
    fail('Fresh132 producer identity or original-input binding differs');
  const counts = extract.host_only?.counts;
  if (counts?.ticks !== 3585 || counts?.combined_draws !== 3583 ||
      counts?.combined_driver_actions !== 7168 || counts?.post_go_raw_row_counts?.source_tick !== 3501 ||
      counts?.post_go_raw_row_counts?.draw_enter !== 3499 ||
      counts?.post_go_raw_row_counts?.draw_return !== 3499 ||
      counts?.final_draw_return_sequence !== 20505 ||
      joins.source_consumption.records !== 20507 ||
      joins.source_consumption.post_GO_samples !== 3501)
    fail('Fresh132 producer terminal counts differ');
  return Object.freeze({stream_sha256: STREAM_SHA256, input_sha256: INPUT_SHA256,
    dol_sha1: DOL_SHA1, dol_sha256: DOL_SHA256, dolphin_commit: DOLPHIN_COMMIT,
    original_recipe_sha256: binding.producer.menu_recipe_sha256,
    canonical_recipe_sha256: files.menu_recipe.sha256,
    manifest_sha256: binding.producer.build.sha256,
    binary_sha256: binding.producer.build.binary_sha256,
    profile_gci_sha256: binding.producer.profile_gci_sha256});
}

function validateContextRecord(record, phase, sequence, seed) {
  const isEntry = phase === 'vs_entry';
  exactKeys(record, ['phase', 'sequence', 'event', 'boundary', 'pc', 'source_tick',
    'draw_ordinal', 'start_slice_address', 'start_sha256', 'typed_start',
    'rng_pointer_address', 'rng_pointer_hex', 'seed_address', 'seed_hex',
    'source_slices_hex', 'source_slice_inventory'], phase + ' source context');
  const expectedInventory = [
    {tag: 4, flags: 0, address: 0x80480530, size: 0x138},
    {tag: 19, flags: 0, address: 0x804d5f94, size: 4},
    {tag: 20, flags: 0, address: 0x804d5f90, size: 4},
  ];
  exactKeys(record.source_slices_hex, ['19:0', '20:0', '4:0'], phase + ' selected slices');
  if (record.phase !== phase || record.sequence !== sequence || record.event !== 'boundary' ||
      record.boundary !== (isEntry ? 'entry' : 'setup') ||
      record.pc !== (isEntry ? '0x8016e934' : '0x8016e9c4') ||
      record.source_tick !== 131 || record.draw_ordinal !== 0 ||
      record.start_slice_address !== 0x80480530 ||
      record.start_sha256 !== '918a23d68a3b5024f94baf1f5e26e6c0db56c5fbbbffdc79c5e620e82af3b728' ||
      record.rng_pointer_address !== 0x804d5f94 || record.seed_address !== 0x804d5f90 ||
      record.rng_pointer_hex !== '804d5f90' || Number.parseInt(record.rng_pointer_hex, 16) !== record.seed_address ||
      record.seed_hex !== seed ||
      record.source_slices_hex['19:0'] !== record.rng_pointer_hex ||
      record.source_slices_hex['20:0'] !== record.seed_hex ||
      Buffer.from(record.source_slices_hex['4:0'], 'hex').length !== 0x138 ||
      hash(Buffer.from(record.source_slices_hex['4:0'], 'hex')) !== record.start_sha256 ||
      !same(record.source_slice_inventory, expectedInventory) ||
      record.source_slices_hex['4:0'].length !== 0x138 * 2 ||
      !/^[0-9a-f]{8}$/.test(record.seed_hex) ||
      !/^[0-9a-f]{8}$/.test(record.rng_pointer_hex) ||
      !record.typed_start || typeof record.typed_start !== 'object' ||
      !Array.isArray(record.typed_start.players) || record.typed_start.players.length !== 4 ||
      !record.typed_start.rules)
    fail(phase + ' raw Entry/Setup context differs from the pinned same producer');
  return record;
}

export function validateFresh132FullAContext(startContext, readyExpected,
  validateReadyExpectedSource) {
  exactKeys(startContext, ['entry', 'setup', 'constructor'], 'Fresh132 original Entry/Setup/constructor projection');
  if (typeof validateReadyExpectedSource !== 'function') fail('Fresh132 ready-start decoder is unavailable');
  const validated = validateReadyExpectedSource(readyExpected);
  const entry = validateContextRecord(startContext.entry, 'vs_entry', 2544, 'e2624ac3');
  const setup = validateContextRecord(startContext.setup, 'vs_setup', 2584, '083a62a9');
  exactKeys(startContext.constructor, ['schema', 'constructor_entry_count', 'source_sequences',
    'seed_before_hex', 'seed_after_hex', 'parameter_x0', 'parameter_x4', 'random_call_observed',
    'random_bound_valid', 'random_bound', 'random_result_valid', 'random_result'],
  'Fresh132 constructor observation');
  const ctor = startContext.constructor;
  if (entry.sequence >= ctor.source_sequences?.[0] ||
      !Array.isArray(ctor.source_sequences) || ctor.source_sequences.length !== 5 ||
      ctor.source_sequences.some((sequence, index) => !Number.isSafeInteger(sequence) ||
        (index > 0 && sequence <= ctor.source_sequences[index - 1])) ||
      ctor.source_sequences.at(-1) >= setup.sequence ||
      !same(ctor.source_sequences, [2566, 2567, 2568, 2569, 2570]) ||
      ctor.schema !== 'melee-web-original-stadium-constructor-state-projection-v2' ||
      ctor.constructor_entry_count !== 1 ||
      ctor.seed_before_hex !== entry.seed_hex || ctor.seed_before_hex !== 'e2624ac3' ||
      ctor.seed_after_hex !== '55248a7a' || ctor.parameter_x0 !== 3600 ||
      ctor.parameter_x4 !== 3800 || ctor.random_call_observed !== true ||
      ctor.random_bound_valid !== true || ctor.random_bound !== 200 ||
      ctor.random_result_valid !== true || ctor.random_result !== 66 ||
      !same(entry.typed_start, setup.typed_start) ||
      !same(setup.typed_start, validated.expected?.start) ||
      validated.expected?.random_seed_hex !== entry.seed_hex)
    fail('Fresh132 Entry/Setup or one-entry constructor projection differs');
  return Object.freeze({entry: structuredClone(entry), setup: structuredClone(setup),
    constructor_observation: structuredClone(ctor),
    witnesses: [...ctor.source_sequences],
    scope: EXPECTED_SCOPE});
}

function validateTypedFields(row, expectedOrdinal) {
  exactKeys(row, ['ordinal', 'scope', 'typed_fields', 'seed_hex'], 'Fresh132 typed source row ' + expectedOrdinal);
  if (row.ordinal !== expectedOrdinal || row.scope !== 'comparison' ||
      !/^[0-9a-f]{8}$/.test(row.seed_hex ?? ''))
    fail('Fresh132 typed source row ordinal/scope/seed differs at ' + expectedOrdinal);
  const fields = row.typed_fields;
  exactKeys(fields, ['display', 'draw_ordinal', 'guest_pointer_policy', 'label', 'map2',
    'map_root_allocation_lifetime_proved', 'map_root_object_type_observed', 'map_root_roles',
    'sequence', 'source_tick', 'title'], 'Fresh132 typed fields ' + expectedOrdinal);
  exactKeys(fields.map2, ['countdown_s32', 'form_s16', 'gate_mask_40', 'gate_mask_80', 'gate_raw_hex',
    'map5_root_pointer_equals_fixed_map5_slot_value', 'phase_s16', 'previous_form_s16', 'prior_form_s16'],
  'Fresh132 map2 fields ' + expectedOrdinal);
  exactKeys(fields.display, ['flags_s16', 'focused_player_s16', 'mode_s16', 'packed_flags_raw_hex',
    'previous_flags_s16', 'previous_mode_s16', 'selection_counter_s16', 'timer_initialization_observed',
    'timer_raw_hex', 'timer_representation', 'xF0_observed'], 'Fresh132 display fields ' + expectedOrdinal);
  exactKeys(fields.title, ['scratch_bytes', 'scratch_sha256', 'font1_slot_pointer_present',
    'title_child_present', 'text_wrapper_present', 'static_text_pointer_present',
    'dynamic_text_pointer_present', 'sis_table_entry_relationship_observed',
    'static_title_text_bytes_observed', 'dynamic_title_text_bytes_observed'],
  'Fresh132 title fields ' + expectedOrdinal);
  const map = fields.map2, display = fields.display, title = fields.title;
  const signed = (value, bits) => Number.isInteger(value) && value >= -(2 ** (bits - 1)) && value < 2 ** (bits - 1);
  if (fields.label !== 'IDLE' || !Number.isSafeInteger(fields.sequence) ||
      !Number.isSafeInteger(fields.source_tick) || !Number.isSafeInteger(fields.draw_ordinal) ||
      fields.map_root_object_type_observed !== false ||
      fields.map_root_allocation_lifetime_proved !== false ||
      fields.guest_pointer_policy !== GUEST_POINTER_POLICY ||
      !Array.isArray(fields.map_root_roles) || fields.map_root_roles.length !== 4 ||
      !same(fields.map_root_roles.map(role => role.fixed_slot_map_id), [0, 1, 2, 5]) ||
      fields.map_root_roles.some(role => role.root_pointer_range_valid !== true ||
        role.ground_pointer_range_valid !== true) ||
      !signed(map.countdown_s32, 32) || !signed(map.form_s16, 16) ||
      !signed(map.phase_s16, 16) || !signed(map.previous_form_s16, 16) ||
      !signed(map.prior_form_s16, 16) ||
      typeof map.gate_mask_40 !== 'boolean' || typeof map.gate_mask_80 !== 'boolean' ||
      !/^[0-9a-f]{2}$/.test(map.gate_raw_hex ?? '') ||
      ((Number.parseInt(map.gate_raw_hex, 16) & 0x80) !== 0) !== map.gate_mask_80 ||
      typeof map.map5_root_pointer_equals_fixed_map5_slot_value !== 'boolean' ||
      !signed(display.flags_s16, 16) || !signed(display.focused_player_s16, 16) ||
      !signed(display.mode_s16, 16) || !signed(display.previous_flags_s16, 16) ||
      !signed(display.previous_mode_s16, 16) || !signed(display.selection_counter_s16, 16) ||
      !/^[0-9a-f]{2}$/.test(display.packed_flags_raw_hex ?? '') ||
      !/^[0-9a-f]{8}$/.test(display.timer_raw_hex ?? '') ||
      typeof display.timer_representation !== 'string' ||
      typeof display.timer_initialization_observed !== 'boolean' ||
      typeof display.xF0_observed !== 'boolean' ||
      title.scratch_bytes !== 256 || !/^[0-9a-f]{64}$/.test(title.scratch_sha256 ?? '') ||
      ['font1_slot_pointer_present', 'title_child_present', 'text_wrapper_present',
        'static_text_pointer_present', 'dynamic_text_pointer_present',
        'sis_table_entry_relationship_observed', 'static_title_text_bytes_observed',
        'dynamic_title_text_bytes_observed'].some(key => typeof title[key] !== 'boolean'))
    fail('Fresh132 typed source fields are malformed at ' + expectedOrdinal);
  return row;
}

export function validateFresh132FullASchedule(runtimePayload, actionProvenance,
  bundleProvenance, route, validateRuntimePayload) {
  if (typeof validateRuntimePayload !== 'function') fail('run2 runtime decoder is unavailable');
  validateRuntimePayload(runtimePayload);
  if (canonicalHash(runtimePayload.bundles) !== CANONICAL_PINS.bundles ||
      canonicalHash(runtimePayload.schedule) !== CANONICAL_PINS.schedule ||
      canonicalHash(actionProvenance) !== CANONICAL_PINS.actionProvenance ||
      canonicalHash(bundleProvenance) !== CANONICAL_PINS.bundleProvenance)
    fail('Fresh132 action, literal PAD or source provenance digest differs');
  if (runtimePayload.bundles.length !== 3585 || runtimePayload.schedule.length !== 7168 ||
      runtimePayload.schedule.filter(row => row.action === 'tick').length !== 3585 ||
      runtimePayload.schedule.filter(row => row.action === 'draw').length !== 3583 ||
      actionProvenance.length !== 7168 || bundleProvenance.length !== 3585 ||
      route?.go_source_tick_C !== 84 || route?.first_full_post_go_source_tick_F !== 85 ||
      route?.comparison_end_tick !== 3584 || route?.tail_ticks_after_comparison_end !== 0)
    fail('Fresh132 full-A action dimensions or dynamic coordinates differ');
  let tick = 0, draw = 0, previousSequence = -1;
  const tickRows = new Map();
  for (let index = 0; index < runtimePayload.schedule.length; ++index) {
    const action = runtimePayload.schedule[index], row = actionProvenance[index];
    if (!row || row.action_index !== index || row.action !== action.action)
      fail('Fresh132 action provenance is missing, extra or reordered at ' + index);
    let sequences;
    if (action.action === 'tick') {
      exactKeys(row, ['action', 'action_index', 'consume_sequence', 'draw_ordinal',
        'intervening_progress_sequences', 'source_tick', 'source_tick_sequence'],
      'Fresh132 tick provenance ' + index);
      const bundle = bundleProvenance[tick];
      exactKeys(bundle, ['bundle', 'source_tick', 'draw_ordinal', 'consume_sequence',
        'source_tick_sequence', 'consume_event', 'source_tick_event'],
      'Fresh132 bundle provenance ' + tick);
      if (action.bundle !== tick || row.source_tick !== tick || row.draw_ordinal !== draw ||
          bundle.bundle !== tick || bundle.source_tick !== tick || bundle.draw_ordinal !== draw ||
          bundle.consume_sequence !== row.consume_sequence ||
          bundle.source_tick_sequence !== row.source_tick_sequence ||
          bundle.consume_event !== 'boundary' || bundle.source_tick_event !== 'boundary' ||
          !Array.isArray(row.intervening_progress_sequences) ||
          row.intervening_progress_sequences.some((seq, at, arr) =>
            !Number.isSafeInteger(seq) || (at > 0 && seq <= arr[at - 1])))
        fail('Fresh132 tick/PAD bundle provenance differs at ' + index);
      sequences = [row.consume_sequence, ...row.intervening_progress_sequences, row.source_tick_sequence];
      tickRows.set(tick, row);
      ++tick;
    } else {
      exactKeys(row, ['action', 'action_index', 'draw_enter_sequence', 'draw_ordinal',
        'draw_return_sequence', 'source_tick'], 'Fresh132 draw provenance ' + index);
      if (action.draw !== draw || row.draw_ordinal !== draw || row.source_tick !== tick)
        fail('Fresh132 draw action/source cursor differs at ' + index);
      sequences = [row.draw_enter_sequence, row.draw_return_sequence];
      ++draw;
    }
    if (sequences.some(seq => !Number.isSafeInteger(seq) || seq < 0) ||
        sequences.some((seq, at) => at > 0 && seq <= sequences[at - 1]) ||
        sequences[0] <= previousSequence)
      fail('Fresh132 source action event order differs at ' + index);
    previousSequence = sequences.at(-1);
  }
  if (tick !== 3585 || draw !== 3583 ||
      runtimePayload.schedule[7166]?.action !== 'tick' ||
      runtimePayload.schedule[7166]?.bundle !== 3584 ||
      runtimePayload.schedule[7167]?.action !== 'draw' ||
      runtimePayload.schedule[7167]?.draw !== 3582 ||
      runtimePayload.schedule.at(-1)?.action_index !== 7167 ||
      previousSequence !== 20505)
    fail('Fresh132 full-A terminal action or source event differs');
  const c = tickRows.get(84), f = tickRows.get(85), go2 = tickRows.get(86);
  if (!c || !f || !go2 || c.action_index !== 168 || f.action_index !== 170 ||
      go2.action_index !== 172 ||
      runtimePayload.schedule[169]?.action !== 'draw' ||
      runtimePayload.schedule[171]?.action !== 'draw' ||
      runtimePayload.schedule[173]?.action !== 'draw')
    fail('Fresh132 dynamic GO/C/F/GO2 action mapping differs');
  return Object.freeze({tickRows, cActionIndex: c.action_index,
    fActionIndex: f.action_index, go2ActionIndex: go2.action_index,
    prefixActionCountThroughGo2Draw: 174});
}

export function validateFresh132FullATypedRows(typedStates, tickRows, route) {
  if (canonicalHash(typedStates) !== CANONICAL_PINS.typedStates ||
      !Array.isArray(typedStates) || typedStates.length !== 3501)
    fail('Fresh132 typed host row digest or 3501-row count differs');
  const rows = [];
  for (let ordinal = 0; ordinal < typedStates.length; ++ordinal) {
    const row = validateTypedFields(typedStates[ordinal], ordinal);
    const fields = row.typed_fields;
    const tick = 84 + ordinal;
    const action = tickRows.get(tick);
    const role = tick === 84 ? 'C' : tick === 85 ? 'F' : 'IDLE';
    if (!action || action.source_tick !== tick || fields.source_tick !== tick ||
        fields.draw_ordinal !== action.draw_ordinal ||
        fields.sequence !== action.source_tick_sequence ||
        row.scope !== 'comparison' || route?.comparison_end_tick < tick)
      fail('Fresh132 typed row does not bijectively join to source tick ' + tick);
    rows.push({ordinal, boundary_role: role, source_tick: tick,
      draw_ordinal: fields.draw_ordinal, source_sequence: fields.sequence,
      seed_hex: row.seed_hex, typed_fields: structuredClone(fields)});
  }
  if (rows[0]?.source_tick !== 84 || rows[1]?.source_tick !== 85 ||
      rows[2]?.source_tick !== 86 || rows.at(-1)?.source_tick !== 3584)
    fail('Fresh132 typed row range endpoints differ');
  return rows;
}

function validateGoSeed(go, tickC) {
  exactKeys(go, ['sequence', 'event', 'phase', 'source_tick', 'draw_ordinal',
    'pointer_slot_address', 'pointer_raw_hex', 'seed_address', 'seed_hex',
    'pointer_equals_seed_address'], 'Fresh132 dynamic GO seed observation');
  if (go.event !== 'progress' || go.phase !== 'go_after' ||
      go.sequence !== tickC.intervening_progress_sequences[0] ||
      go.source_tick !== 84 || go.draw_ordinal !== 84 ||
      go.pointer_slot_address !== 0x804d5f94 || go.pointer_raw_hex !== '804d5f90' ||
      go.seed_address !== 0x804d5f90 || go.seed_hex !== 'ff0c2607' ||
      go.pointer_equals_seed_address !== true)
    fail('Fresh132 dynamic GO marker is not joined between C consume and SourceTick');
  return Object.freeze({sequence: go.sequence, source_tick: go.source_tick,
    draw_ordinal: go.draw_ordinal, seed_hex: go.seed_hex});
}

export function buildFresh132FullAProjection(inputBytes, decoders) {
  exactKeys(inputBytes, ['extract', 'binding', 'postrun', 'readyExpected'],
    'Fresh132 full-A raw input byte set');
  const extract = parsePinned('Fresh132 adapter extract', inputBytes.extract, FRESH132_FULL_A_INPUT_PINS.extract);
  const binding = parsePinned('Fresh132 adapter binding', inputBytes.binding, FRESH132_FULL_A_INPUT_PINS.binding);
  const postrun = parsePinned('Fresh132 independent postrun result', inputBytes.postrun, FRESH132_FULL_A_INPUT_PINS.postrun);
  const readyExpected = parsePinned('Fresh132 ready-start expectation', inputBytes.readyExpected,
    FRESH132_FULL_A_INPUT_PINS.readyExpected);
  const producer = validateFresh132FullAProducerJoin(extract, binding, postrun);
  const validatePayload = decoders?.validateRun2RuntimePayload;
  const validateReadyExpectedSource = decoders?.validateFreshSss132ReadyStartExpectedSource;
  if (typeof validatePayload !== 'function' || typeof validateReadyExpectedSource !== 'function')
    fail('Existing checked Fresh132/run2 decoders are required');
  const sourceContext = validateFresh132FullAContext(extract.host_only.start_context,
    readyExpected, validateReadyExpectedSource);
  if (canonicalHash(extract.host_only.start_context) !== CANONICAL_PINS.startContext ||
      canonicalHash(extract.host_only.go_seed_observation) !== CANONICAL_PINS.goSeed)
    fail('Fresh132 Entry/Setup/constructor or GO source context digest differs');
  if (postrun.entry?.sequence !== sourceContext.entry.sequence ||
      postrun.setup?.sequence !== sourceContext.setup.sequence ||
      !same(postrun.entry, sourceContext.entry) || !same(postrun.setup, sourceContext.setup) ||
      !same(postrun.constructor, sourceContext.constructor_observation))
    fail('Fresh132 context projection differs from the independent same-run result');
  const schedule = validateFresh132FullASchedule(extract.runtime_payload,
    extract.host_only.action_provenance, extract.host_only.bundle_provenance,
    extract.host_only.route, validatePayload);
  const cRow = schedule.tickRows.get(84), fRow = schedule.tickRows.get(85),
    go2Row = schedule.tickRows.get(86);
  const go = validateGoSeed(extract.host_only.go_seed_observation, cRow);
  const typedRows = validateFresh132FullATypedRows(extract.host_only.typed_states,
    schedule.tickRows, extract.host_only.route);
  if (typedRows[2].source_sequence !== go2Row.source_tick_sequence ||
      typedRows[2].seed_hex !== 'ce999b35' ||
      cRow.source_tick_sequence !== 3007 || fRow.source_tick_sequence !== 3012 ||
      go2Row.source_tick_sequence !== 3017 ||
      extract.host_only.typed_states[0].seed_hex !== 'bc2cb82f' ||
      extract.host_only.typed_states[1].seed_hex !== '399f0260')
    fail('Fresh132 C/F/GO+2 typed source marker identities differ');
  return {
    schema: 'melee-web-stadium-fresh132-full-a-host-projection',
    version: 1,
    decision: 'PASS_PINNED_ORIGINAL_IDLE_FULL_A_PROJECTION_ONLY',
    claim_scope: 'Original producer schedule/PAD actions and declared host typed rows only; no browser execution, Setup admission, recreation B, lifetime equivalence, per-owner RNG, C3, pixels, PCM or acceptance.',
    source_identity: producer,
    dimensions: {ticks: 3585, draws: 3583, actions: 7168,
      comparison_rows: 3501, c_action_index: schedule.cActionIndex,
      f_action_index: schedule.fActionIndex, go_action_index: 168,
      go2_action_index: schedule.go2ActionIndex,
      actions_through_go2_draw: schedule.prefixActionCountThroughGo2Draw,
      terminal_tick: 3584, terminal_action_index: 7167,
      terminal_draw: 3582},
    runtime_payload: structuredClone(extract.runtime_payload),
    host_expectations: {
      same_producer_entry_setup_constructor: sourceContext,
      go_seed_observation: go,
      boundaries: {
        C: {source_tick: 84, draw_ordinal: 84, sequence: cRow.source_tick_sequence,
          seed_hex: typedRows[0].seed_hex},
        F: {source_tick: 85, draw_ordinal: 85, sequence: fRow.source_tick_sequence,
          seed_hex: typedRows[1].seed_hex},
        GO2: {source_tick: 86, draw_ordinal: 86, sequence: go2Row.source_tick_sequence,
          seed_hex: typedRows[2].seed_hex},
      },
      typed_rows: typedRows,
    },
  };
}
