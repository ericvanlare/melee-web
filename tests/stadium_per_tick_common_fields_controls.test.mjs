import assert from 'node:assert/strict';
import {test} from 'node:test';
import {
  compareOriginalCommonTickFields,
  deriveRun2CommonTickExpectations,
  deriveRun2CommonSeedExpectations,
  encodeRun2FullCommonNativeManifest,
  encodeRun2FullNativeManifest,
  hostEvidenceFromRun2Packet,
  IDLE_SEED_REFERENCE_IDENTITY,
  StadiumSetupCfActionDriver,
} from './stadium_matched_setup_cf_consumer.mjs';
import {
  FULL_COMMON_FIELDS_SEED_V1_SCOPE,
  FULL_V3_SCOPE,
  validateMatchedSetupCfPreflight,
} from './stadium_matched_setup_cf_runner_gate.mjs';

const DOL_SHA256 = 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646';
const DOL_SHA1 = '08e0bf20134dfcb260699671004527b2d6bb1a45';
const STREAM_SHA256 = '45de31f20751b87414fe5ca5d3569940ce706bb029e30beb142f33fac546ff3a';
const ADMITTED_WORLD_GENERATION = 7;
const seedIdentity = Object.freeze({
  path: '/fixture/idle3500-reference.json',
  basename: IDLE_SEED_REFERENCE_IDENTITY.basename,
  bytes: IDLE_SEED_REFERENCE_IDENTITY.bytes,
  sha256: IDLE_SEED_REFERENCE_IDENTITY.sha256,
});
const FIELD_CHANGES = [
  ['map2.countdown_s32', sample => ({map2_countdown: sample.map2_countdown + 1})],
  ['map2.phase_s16', sample => ({map2_phase: sample.map2_phase + 1})],
  ['map2.form_s16', sample => ({map2_form: sample.map2_form + 1})],
  ['map2.previous_form_s16', sample => ({map2_history_e0: sample.map2_history_e0 + 1})],
  ['map2.prior_form_s16', sample => ({map2_history_e2: sample.map2_history_e2 + 1})],
  ['map2.gate_mask_80', sample => ({map2_gate: !sample.map2_gate})],
  ['display.mode_s16', sample => ({display_mode: sample.display_mode + 1})],
  ['display.flags_s16', sample => ({display_e6: sample.display_e6 + 1})],
  ['display.previous_flags_s16', sample => ({display_e8: sample.display_e8 + 1})],
  ['display.previous_mode_s16', sample => ({display_previous_mode: sample.display_previous_mode + 1})],
  ['display.focused_player_s16', sample => ({display_selected_player: sample.display_selected_player + 1})],
  ['display.selection_counter_s16', sample => ({display_f2: sample.display_f2 + 1})],
  ...[[1, 0x80], [2, 0x40], [4, 0x20]].map(([nativeMask, originalMask]) =>
    [`display.packed_flags_raw_hex_bit_${originalMask.toString(16)}`, sample =>
      ({display_flags: sample.display_flags ^ nativeMask})]),
];

function fullPacket() {
  const bundles = Array.from({length: 3585}, (_, bundle) => ({bundle,
    ports: Array.from({length: 4}, (_, port) => ({port,
      status_hex: `${(bundle & 255).toString(16).padStart(2, '0')}${'00'.repeat(10)}`}))}));
  const schedule = [];
  let draw = 0;
  for (let bundle = 0; bundle < bundles.length; ++bundle) {
    schedule.push({action: 'tick', action_index: schedule.length, bundle});
    if (![1375, 2376, 3377].includes(bundle))
      schedule.push({action: 'draw', action_index: schedule.length, draw: draw++});
  }
  const provenance = [];
  const tickSequences = new Map();
  let sequence = 0, ticks = 0, draws = 0;
  for (const action of schedule) {
    if (action.action === 'tick') {
      const consume = sequence++;
      const intervening = [];
      if (action.bundle === 0) {
        // The large initial progress span keeps the three pinned boundary
        // sequence numbers in the exact retained source coordinate space.
        for (let i = 0; i < 2647; ++i) intervening.push(sequence++);
      } else if (action.bundle === 85 || action.bundle >= 86) {
        intervening.push(sequence++);
      }
      const sourceTickSequence = sequence++;
      tickSequences.set(action.bundle, sourceTickSequence);
      provenance.push({action: 'tick', action_index: action.action_index,
        consume_sequence: consume, intervening_progress_sequences: intervening,
        source_tick: ticks, source_tick_sequence: sourceTickSequence, draw_ordinal: draws});
      ++ticks;
    } else {
      provenance.push({action: 'draw', action_index: action.action_index,
        draw_enter_sequence: sequence++, draw_return_sequence: sequence++,
        draw_ordinal: draws, source_tick: ticks});
      ++draws;
    }
  }
  assert.equal(tickSequences.get(84), 2984);
  assert.equal(tickSequences.get(85), 2989);
  assert.equal(tickSequences.get(3584), 20478);

  const typed = (label, sourceTick, drawOrdinal, sourceSequence) => {
    const flags = sourceTick & 7;
    const packed = ((flags & 1) ? 0x80 : 0) | ((flags & 2) ? 0x40 : 0) |
      ((flags & 4) ? 0x20 : 0);
    return {label, sequence: sourceSequence, source_tick: sourceTick,
      draw_ordinal: drawOrdinal,
      map2: {countdown_s32: 4096 - sourceTick, phase_s16: (sourceTick % 31) - 15,
        form_s16: 5, previous_form_s16: -1, prior_form_s16: -2,
        gate_mask_40: (sourceTick & 2) !== 0, gate_mask_80: (sourceTick & 1) !== 0,
        gate_raw_hex: (sourceTick & 1) !== 0 ? '80' : '00',
        map5_root_pointer_equals_fixed_map5_slot_value: false},
      display: {mode_s16: 11, flags_s16: (sourceTick % 23) - 11,
        previous_flags_s16: 64, previous_mode_s16: 10,
        focused_player_s16: sourceTick % 4, selection_counter_s16: sourceTick % 7,
        packed_flags_raw_hex: packed.toString(16).padStart(2, '0'),
        timer_raw_hex: '00000000', timer_initialization_observed: false,
        timer_representation: 'synthetic-shaped-only', xF0_observed: false},
      title: {dynamic_title_text_bytes_observed: false},
      map_root_allocation_lifetime_proved: false,
      map_root_object_type_observed: false,
      guest_pointer_policy: 'fixed-slot role and pointer equality only; no object-type or allocation-lifetime proof; numeric guest pointers are not cross-runtime comparison fields',
      map_root_roles: [0, 1, 2, 5].map(fixed_slot_map_id => ({fixed_slot_map_id}))};
  };
  const perSourceTick = Array.from({length: 3501}, (_, offset) => {
    const sourceTick = 84 + offset;
    const action = schedule.find(row => row.action === 'tick' && row.bundle === sourceTick);
    const actionReceipt = provenance[action.action_index];
    return {boundary_role: sourceTick === 84 ? 'C' : sourceTick === 85 ? 'F' : 'IDLE',
      raw_identity: {event: 'boundary', boundary: 'source_tick',
        sequence: actionReceipt.source_tick_sequence, source_tick: sourceTick,
        draw_ordinal: actionReceipt.draw_ordinal},
      typed_fields: typed(sourceTick === 84 ? 'C' : sourceTick === 85 ? 'F' : 'IDLE',
        sourceTick, actionReceipt.draw_ordinal, actionReceipt.source_tick_sequence)};
  });
  const c = perSourceTick[0], f = perSourceTick[1];
  const hostOnly = {
    schema: 'melee-web-stadium-source-host-expectations', version: 1,
    source_identity: {
      strict_route: {source: {dol_sha256: DOL_SHA256, dol_sha1: DOL_SHA1},
        stream_sha256: STREAM_SHA256, stream_records: 20482},
    source_run: {source_consumed_pad: {artifact: {sha256: STREAM_SHA256}},
      run_artifacts: {'idle3500-reference.json': {
        path: seedIdentity.path, bytes: IDLE_SEED_REFERENCE_IDENTITY.bytes,
        sha256: IDLE_SEED_REFERENCE_IDENTITY.sha256,
        hash_source: IDLE_SEED_REFERENCE_IDENTITY.hash_source}}},
    },
    action_provenance: provenance,
    expected_state: {
      comparison_scope: {setup_profile_verified_by_observer: false,
        profile_equivalence: 'pending; fixture is not a source capture'},
      GO: {raw_identity: {event: 'progress', phase: 'go_after', sequence: 2925,
        source_tick: 84, draw_ordinal: 84},
      typed_fields: {label: 'GO', sequence: 2925, source_tick: 84, draw_ordinal: 84}},
      C: {boundary_role: 'C', raw_identity: c.raw_identity, typed_fields: c.typed_fields},
      F: {boundary_role: 'F', raw_identity: f.raw_identity, typed_fields: f.typed_fields},
      per_source_tick: perSourceTick,
    },
    boundary_rng_observations: [
      {label: 'C', sequence: c.raw_identity.sequence, source_tick: 84, draw_ordinal: 84,
        post_go_ordinal: 0, pc: '0x80390eb4', pointer_slot_address: 0x804d5f94,
        pointer_raw_hex: '804d5f90', seed_address: 0x804d5f90,
        pointer_hex: '804d5f90', seed_raw_hex: '868d77be', seed_hex: '868d77be',
        event: 'boundary', pointer_equals_seed_address: true},
      {label: 'F', sequence: f.raw_identity.sequence, source_tick: 85, draw_ordinal: 85,
        post_go_ordinal: 1, pc: '0x80390eb4', pointer_slot_address: 0x804d5f94,
        pointer_raw_hex: '804d5f90', seed_address: 0x804d5f90,
        pointer_hex: '804d5f90', seed_raw_hex: 'b50f624b', seed_hex: 'b50f624b',
        event: 'boundary', pointer_equals_seed_address: true},
    ],
  };
  return {schema: 'melee-web-stadium-source-action-replay-packet', version: 2,
    runtime_payload: {schema: 'melee-web-stadium-pad-action-plan', version: 1,
      bundles, schedule}, host_only: hostOnly};
}

function idleSeedSidecar(packet) {
  const seed_observations = [], source_action_schedule = [];
  for (let ordinal = 0; ordinal < 3501; ++ordinal) {
    const sourceTick = 84 + ordinal;
    const actionIndex = packet.runtime_payload.schedule.findIndex(row =>
      row.action === 'tick' && row.bundle === sourceTick);
    const action = packet.runtime_payload.schedule[actionIndex];
    const source = packet.host_only.action_provenance[actionIndex];
    const seed_hex = ordinal === 0 ? '868d77be' : ordinal === 1 ? 'b50f624b'
      : ordinal === 2 ? '60d3e16c'
      : ordinal === 3500 ? '312a19c4' : ordinal.toString(16).padStart(8, '0');
    seed_observations.push({ordinal, sequence: source.source_tick_sequence,
      pointer_hex: '804d5f90', seed_hex});
    source_action_schedule.push({action: 'tick', bundle: ordinal,
      draw_ordinal: source.draw_ordinal, ordinal, sequence: source.source_tick_sequence});
    const draw = packet.runtime_payload.schedule[actionIndex + 1];
    if (draw?.action === 'draw') {
      const drawSource = packet.host_only.action_provenance[actionIndex + 1];
      source_action_schedule.push({action: 'draw_enter', draw_ordinal: draw.draw,
        ordinal, sequence: drawSource.draw_enter_sequence});
      source_action_schedule.push({action: 'draw_return', draw_ordinal: draw.draw,
        ordinal, sequence: drawSource.draw_return_sequence});
    }
  }
  return {schema: 'melee-web-original-stadium-idle-3500', version: 1,
    decision: 'PASS_ORIGINAL_3500_IDLE_REFERENCE_ONLY', observed_tick_count: 3501,
    comparison_post_go_ticks: 3500, cross_owner_rng_ledger_complete: false,
    native_parity: false, lifetime_pairing_observed: false,
    seed_observations, source_action_schedule};
}

function nativeSample(expected, overrides = {}) {
  return {schema: 'melee-web-stadium-per-tick-common-fields-native-v2',
    schedule_index: expected.action_index, source_tick: expected.source_tick,
    world_ticks: expected.source_tick + 1, source_frame: expected.source_tick + 1,
    world_generation: 7, ...expected.values, seed_hex: expected.seed_hex, ...overrides};
}

test('common-field-plus-seed mode is explicit, preserves MWF3 and pins MWC5', () => {
  const packet = fullPacket();
  const full = {schema: 'melee-web-stadium-run2-full-schedule-v3', version: 3,
    bundles: packet.runtime_payload.bundles, schedule: packet.runtime_payload.schedule,
    checkpoints: {c_action_index: 168, c_source_tick: 84, f_action_index: 170,
      f_source_tick: 85, terminal_action_index: 7166, terminal_draw_ordinal: 3581}};
  const oldBytes = encodeRun2FullNativeManifest(full);
  const commonBytes = encodeRun2FullCommonNativeManifest(full);
  assert.equal(Buffer.from(oldBytes.subarray(0, 4)).toString(), 'MWF3');
  assert.equal(Buffer.from(commonBytes.subarray(0, 4)).toString(), 'MWC5');
  assert.deepEqual(commonBytes.subarray(4), oldBytes.subarray(4));
  const file = {path: '/fixture/packet.json', basename: 'packet.json', bytes: 1,
    sha256: '0'.repeat(64)};
  const exports = {arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm',
    queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
    step: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
    retire_ready: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready',
    retire: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
    retirement_status: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status',
    approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve'};
  const preflight = {route_mode: 'first-sss-vs-setup', matched_setup_cf: {
    scope: FULL_COMMON_FIELDS_SEED_V1_SCOPE, packet: file, assembly_manifest: file, helper: file,
    diagnostic_exports: exports,
    asyncify_idle_predicate: 'Module.stadiumMatchedSetupNativeIdle'}};
  const options = {enabled: true, explicitStep: true, fullSchedule: true, commonFields: true,
    packetPath: file.path, assemblyPath: file.path, helperPath: file.path};
  assert.equal(validateMatchedSetupCfPreflight(preflight, options), preflight.matched_setup_cf);
  assert.throws(() => validateMatchedSetupCfPreflight(preflight,
    {...options, commonFields: false}), /scope/);
  const oldScope = structuredClone(preflight);
  oldScope.matched_setup_cf.scope = FULL_V3_SCOPE;
  assert.throws(() => validateMatchedSetupCfPreflight(oldScope, options), /scope|common-field/);
});

test('one exact O(n) map joins all 7167 actions and 3501 typed ticks at raw identities', () => {
  const packet = fullPacket();
  const rows = deriveRun2CommonTickExpectations(packet.runtime_payload, packet.host_only);
  assert.equal(rows.size, 3501);
  assert.deepEqual([168, 170, 7165].map(index => {
    const row = rows.get(index);
    return [row.action_index, row.source_tick, row.draw_ordinal, row.raw_identity.sequence];
  }), [[168, 84, 84, 2984], [170, 85, 85, 2989], [7165, 3584, 3581, 20478]]);
  assert.equal(rows.get(169), undefined);
  assert.equal(rows.get(7166), undefined);
  const sidecar = idleSeedSidecar(packet);
  assert.equal(sidecar.source_action_schedule.length, 10497);
  const evidence = hostEvidenceFromRun2Packet(packet, {fullSchedule: true, commonFields: true,
    seedSidecar: sidecar, seedIdentity});
  assert.equal(evidence.mode, 'run2-full-common-fields-seed-v1');
  assert.equal(evidence.runtimePayload.schedule.length, 7167);
  assert.equal(evidence.commonRowsByAction.size, 3501);
  assert.equal(evidence.commonRowsByAction.get(168).seed_hex, '868d77be');
  assert.equal(evidence.commonRowsByAction.get(170).seed_hex, 'b50f624b');
  assert.equal(evidence.commonRowsByAction.get(7165).seed_hex, '312a19c4');
  assert.deepEqual([168, 170, 172, 7165].map(index => {
    const row = evidence.commonRowsByAction.get(index);
    return [row.seed_ordinal, row.source_tick, row.raw_identity.sequence, row.seed_hex];
  }), [[0, 84, 2984, '868d77be'], [1, 85, 2989, 'b50f624b'],
    [2, 86, 2994, '60d3e16c'], [3500, 3584, 20478, '312a19c4']]);

  const commonRows = deriveRun2CommonTickExpectations(packet.runtime_payload, packet.host_only);
  assert.throws(() => deriveRun2CommonSeedExpectations(packet.runtime_payload,
    packet.host_only, commonRows, null, seedIdentity), /scope or dimensions/);
  for (const changedIdentity of [
    {...seedIdentity, sha256: '0'.repeat(64)},
    {...seedIdentity, bytes: seedIdentity.bytes - 1},
    {...seedIdentity, path: '/fixture/renamed.json'},
    {...seedIdentity, basename: 'renamed.json'},
  ])
    assert.throws(() => deriveRun2CommonSeedExpectations(packet.runtime_payload,
      packet.host_only, commonRows, sidecar, changedIdentity), /packet-bound artifact/);
  for (const mutatePacket of [
    value => { value.host_only.source_identity.source_run.run_artifacts['idle3500-reference.json'].path += '.old'; },
    value => { value.host_only.source_identity.source_run.run_artifacts['idle3500-reference.json'].bytes--; },
    value => { value.host_only.source_identity.source_run.run_artifacts['idle3500-reference.json'].hash_source = 'declared'; },
  ]) {
    const badPacket = structuredClone(packet); mutatePacket(badPacket);
    const badRows = deriveRun2CommonTickExpectations(
      packet.runtime_payload, packet.host_only);
    assert.throws(() => deriveRun2CommonSeedExpectations(badPacket.runtime_payload,
      badPacket.host_only, badRows, sidecar, seedIdentity), /packet-bound artifact/);
  }
  for (const mutate of [
    value => { value.seed_observations.pop(); },
    value => { value.seed_observations.push({...value.seed_observations.at(-1)}); },
    value => { value.seed_observations[17].ordinal = 18; },
    value => { value.seed_observations[17].sequence = value.seed_observations[18].sequence; },
    value => { value.seed_observations[17].sequence++; },
    value => { value.seed_observations[17].seed_hex = 'bad'; },
    value => { value.seed_observations[17].seed_hex = 1; },
    value => { value.seed_observations[17].seed_hex = value.seed_observations[18].seed_hex; },
    value => { value.seed_observations[17].pointer_hex = '804d5f91'; },
    value => { value.seed_observations[17].extra = true; },
    value => { value.source_action_schedule[0].bundle = 1; },
    value => { value.source_action_schedule[0].draw_ordinal++; },
    value => { value.source_action_schedule[1].sequence++; },
    value => { value.source_action_schedule.push({...value.source_action_schedule.at(-1)}); },
    value => { value.seed_observations[0].seed_hex = '00000001'; },
    value => { value.seed_observations[3500].seed_hex = '00000001'; },
  ]) {
    const bad = structuredClone(sidecar); mutate(bad);
    assert.throws(() => deriveRun2CommonSeedExpectations(packet.runtime_payload,
      packet.host_only, commonRows, bad, seedIdentity));
  }

  for (const mutate of [
    value => { value.host_only.action_provenance[1].action_index = 0; },
    value => { value.host_only.action_provenance[7165].source_tick_sequence = 20477; },
    value => { value.host_only.action_provenance.pop(); },
    value => { value.host_only.expected_state.per_source_tick[2].raw_identity.boundary = 'draw'; },
    value => { value.host_only.expected_state.per_source_tick[2].typed_fields.sequence++; },
    value => { value.host_only.expected_state.per_source_tick[2].typed_fields.source_tick++; },
    value => { value.host_only.expected_state.per_source_tick[2].typed_fields.draw_ordinal++; },
    value => { value.host_only.expected_state.per_source_tick[2].boundary_role = 'C'; },
    value => { value.host_only.expected_state.per_source_tick.pop(); },
    value => { value.host_only.source_identity.strict_route.stream_records++; },
  ]) {
    const bad = structuredClone(packet); mutate(bad);
    assert.throws(() => deriveRun2CommonTickExpectations(bad.runtime_payload, bad.host_only));
  }
});

test('fifteen common fields plus retained seed compare exactly and any first mismatch refuses', () => {
  const packet = fullPacket();
  const commonRows = deriveRun2CommonTickExpectations(packet.runtime_payload, packet.host_only);
  const expected = deriveRun2CommonSeedExpectations(packet.runtime_payload, packet.host_only,
    commonRows, idleSeedSidecar(packet), seedIdentity).get(168);
  const sample = nativeSample(expected);
  const passed = compareOriginalCommonTickFields({expected, actual: sample,
    actionIndex: 168, sourceFramesAfter: 85, admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
  assert.equal(passed.decision, 'PASS_EXACT_15_COMMON_FIELDS_AND_SEED');
  assert.equal(passed.compared_field_count, 15);
  assert.equal(passed.seed_value_count, 1);
  assert.equal(passed.exact_value_count, 16);
  assert.equal(passed.first_mismatch, null);
  assert.equal(FIELD_CHANGES.length, 15);
  for (const [field, change] of FIELD_CHANGES) {
    const changed = {...sample, ...change(sample)};
    const result = compareOriginalCommonTickFields({expected, actual: changed,
      actionIndex: 168, sourceFramesAfter: 85, admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
    assert.equal(result.decision, 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH', field);
    assert.equal(result.compared_field_count, 15, field);
    assert.equal(result.seed_value_count, 1, field);
    assert.equal(result.first_mismatch.path, field, field);
  }
  const changedSeed = {...sample, seed_hex: '868d77bf'};
  const seedResult = compareOriginalCommonTickFields({expected, actual: changedSeed,
    actionIndex: 168, sourceFramesAfter: 85, admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
  assert.equal(seedResult.decision, 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH');
  assert.equal(seedResult.compared_field_count, 15);
  assert.equal(seedResult.seed_value_count, 1);
  assert.equal(seedResult.exact_value_count, 16);
  assert.deepEqual(seedResult.first_mismatch, {path: 'rng.seed_hex',
    expected: expected.seed_hex, actual: changedSeed.seed_hex});
  for (const [actionIndex, sourceTick] of [[168, 84], [170, 85], [172, 86], [7165, 3584]]) {
    const row = deriveRun2CommonSeedExpectations(packet.runtime_payload, packet.host_only,
      commonRows, idleSeedSidecar(packet), seedIdentity).get(actionIndex);
    const actual = nativeSample(row);
    const replacement = row.seed_hex.at(-1) === '0' ? '1' : '0';
    const result = compareOriginalCommonTickFields({expected: row,
      actual: {...actual, seed_hex: `${row.seed_hex.slice(0, -1)}${replacement}`},
      actionIndex, sourceFramesAfter: sourceTick + 1,
      admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
    assert.equal(result.decision, 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH',
      `source tick ${sourceTick}`);
    assert.equal(result.first_mismatch.path, 'rng.seed_hex', `source tick ${sourceTick}`);
  }
  for (const [actual, index, sourceFrame] of [
    [nativeSample(expected, {map2_gate: 1}), 168, 85],
    [nativeSample(expected, {display_flags: 8}), 168, 85],
    [nativeSample(expected, {map2_countdown: true}), 168, 85],
    [nativeSample(expected, {seed_hex: 0x868d77be}), 168, 85],
    [nativeSample(expected, {seed_hex: '868d77b'}), 168, 85],
    [nativeSample(expected, {unexpected: 1}), 168, 85],
    [nativeSample(expected, {schedule_index: 170}), 168, 85],
    [nativeSample(expected, {world_generation: 0}), 168, 85],
    [nativeSample(expected, {source_frame: 84}), 168, 85],
    [nativeSample(expected), 170, 85],
  ]) {
    const result = compareOriginalCommonTickFields({expected, actual,
      actionIndex: index, sourceFramesAfter: sourceFrame,
      admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
    assert.equal(result.decision, 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH');
  }
  assert.equal(compareOriginalCommonTickFields({expected, actual: null,
    actionIndex: 168, sourceFramesAfter: 85,
    admittedWorldGeneration: ADMITTED_WORLD_GENERATION}).decision,
  'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH');
});

test('persisted selected-tick comparison is in the same action receipt before approval; mismatch ACK is false', async () => {
  const packet = fullPacket();
  const evidence = hostEvidenceFromRun2Packet(packet, {fullSchedule: true, commonFields: true,
    seedSidecar: idleSeedSidecar(packet), seedIdentity});
  const expected = evidence.commonRowsByAction.get(168);
  const approval = [];
  const driver = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
    native: {arm: async manifest => {
      assert.equal(Buffer.from(manifest.subarray(0, 4)).toString(), 'MWC5'); return 1;
    }, queue: async () => 1, approve: async (_index, accept) => { approval.push(accept); return 1; }},
    commonFields: true, commonRowsByAction: evidence.commonRowsByAction,
    admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
  assert.equal(await driver.arm(true), true);
  driver.cursor = 168;
  driver.sourceFrame = 84;
  assert.equal(await driver.submit(evidence.runtimePayload.schedule[168]), true);
  const sample = nativeSample(expected);
  const observation = {schema: 'melee-web-stadium-source-action-native-v5',
    scope: 'original-full7167-common-fields-seed-observation-only', common_field_count: 15,
    seed_value_count: 1,
    action_count: 7167, source_ticks: 85, source_draws: 84, state: 'awaiting-approval',
    failed: false, last_action: {schedule_index: 168, action: 'tick', ordinal: 84,
      bundle: 84, complete: true, source_ticks_before: 84, source_ticks_after: 85,
      source_draws_before: 84, source_draws_after: 84, world_ticks: 85,
      source_tick_input_pad_hex: evidence.runtimePayload.bundles[84].ports
        .map(port => port.status_hex).join(''), source_frames_before: 84,
      source_frames_after: 85, source_frames: 85, common_fields_sample: sample,
      observation: {idle_snapshot: {source_frame: 85}}}};
  const compare = actual => compareOriginalCommonTickFields({expected, actual,
    actionIndex: 168, sourceFramesAfter: 85,
    admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
  const common = compare(sample);
  assert.equal(common.decision, 'PASS_EXACT_15_COMMON_FIELDS_AND_SEED');
  assert.equal(await driver.accept(observation,
    {phase: 'C', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'}, common), true);
  assert.deepEqual(approval, [true]);
  assert.equal(driver.status().cursor, 169);

  const refusedDriver = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
    native: {arm: async () => 1, queue: async () => 1,
      approve: async (_index, accept) => { approval.push(accept); return 1; }},
    commonFields: true, commonRowsByAction: evidence.commonRowsByAction,
    admittedWorldGeneration: ADMITTED_WORLD_GENERATION});
  assert.equal(await refusedDriver.arm(true), true);
  refusedDriver.cursor = 168; refusedDriver.sourceFrame = 84;
  assert.equal(await refusedDriver.submit(evidence.runtimePayload.schedule[168]), true);
  const last = sample.seed_hex.at(-1);
  const mismatch = {...sample, seed_hex: `${sample.seed_hex.slice(0, -1)}${last === '0' ? '1' : '0'}`};
  const refused = compare(mismatch);
  assert.equal(refused.decision, 'REFUSE_FIRST_COMMON_FIELD_OR_SEED_MISMATCH');
  assert.equal(refused.first_mismatch.path, 'rng.seed_hex');
  assert.equal(await refusedDriver.accept({...observation,
    last_action: {...observation.last_action, common_fields_sample: mismatch}},
  {phase: 'C', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'}, refused), false);
  assert.deepEqual(approval, [true, false]);
  assert.equal(refusedDriver.status().cursor, 168);
  assert.match(refusedDriver.status().first_error, /common-field/);
});
