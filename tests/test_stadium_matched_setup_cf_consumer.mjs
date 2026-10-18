import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {test} from 'node:test';
import {readFileSync} from 'node:fs';
import {
  compareOriginalIdleSnapshot,
  createHostCallbacks,
  hostEvidenceFromPacket,
  deriveRun2ProperPrefix,
  encodeRun2NativeManifest,
  StadiumSetupCfActionDriver,
  validateGo2StartContextExpectations,
  validateRuntimePayload,
} from './stadium_matched_setup_cf_consumer.mjs';

function runtimePayload() {
  const bundles = Array.from({length: 86}, (_, bundle) => ({bundle,
    ports: Array.from({length: 4}, (_, port) => ({port, status_hex: '0000000000000000000000'}))}));
  const schedule = [];
  for (let ordinal = 0; ordinal < 84; ++ordinal) {
    schedule.push({action: 'tick', ordinal, bundle: ordinal}, {action: 'draw', ordinal});
  }
  schedule.push({action: 'tick', ordinal: 84, bundle: 84},
    {action: 'draw', ordinal: 84}, {action: 'tick', ordinal: 85, bundle: 85});
  return {schema: 'melee-web-stadium-typed-pad-action-plan', version: 1, bundles, schedule};
}

function run2Packet({go2 = false} = {}) {
  const bundles = Array.from({length: 4}, (_, bundle) => ({bundle,
    ports: Array.from({length: 4}, (_, port) => ({port,
      status_hex: `${bundle.toString(16).padStart(2, '0')}${'00'.repeat(10)}`}))}));
  const schedule = [
    {action: 'tick', action_index: 0, bundle: 0},
    {action: 'draw', action_index: 1, draw: 0},
    {action: 'tick', action_index: 2, bundle: 1},
    {action: 'draw', action_index: 3, draw: 1},
    {action: 'tick', action_index: 4, bundle: 2},
    {action: 'draw', action_index: 5, draw: 2},
    {action: 'tick', action_index: 6, bundle: 3},
    {action: 'draw', action_index: 7, draw: 3},
  ];
  const typed = (label, tick, draw, sequence) => ({boundary_role: label,
    raw_identity: {sequence, event: 'boundary', boundary: 'source_tick',
      source_tick: tick, draw_ordinal: draw},
    typed_fields: {...expected(label), sequence, source_tick: tick, draw_ordinal: draw}});
  const host_only = {schema: 'melee-web-stadium-source-host-expectations', version: 1,
    source_identity: {strict_route: {source: {
      dol_sha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
      dol_sha1: '08e0bf20134dfcb260699671004527b2d6bb1a45'}}},
    expected_state: {
      comparison_scope: {setup_profile_verified_by_observer: false,
        profile_equivalence: 'pending; browser Setup receipt must be separately matched'},
      GO: {raw_identity: {sequence: 100, event: 'progress', phase: 'go_after',
        source_tick: 1, draw_ordinal: 1}, typed_fields: {label: 'GO', sequence: 100,
        source_tick: 1, draw_ordinal: 1}},
      C: typed('C', 1, 1, 105), F: typed('F', 2, 2, 110),
    },
    boundary_rng_observations: [
      {label: 'C', sequence: 105, source_tick: 1, draw_ordinal: 1,
        post_go_ordinal: 0, pointer_hex: '804d5f90', seed_hex: '01234567'},
      {label: 'F', sequence: 110, source_tick: 2, draw_ordinal: 2,
        post_go_ordinal: 1, pointer_hex: '804d5f90', seed_hex: '89abcdef'},
    ]};
  if (go2) {
    const startBytes = 'ab'.repeat(0x138);
    const start = {rules: {fixture_rule: 1}, players: [{fixture_player: 0}]};
    const provenance = {source_stream_sha256: 'a'.repeat(64),
      input_stream_sha256: 'b'.repeat(64), setup_receipt_sha256: 'c'.repeat(64),
      scope: 'same fresh native producer and its own complete MWRI record'};
    const startContextProvenance = {
      source_stream_sha256: provenance.source_stream_sha256, source_stream_bytes: 4096,
      observer_status_sha256: 'd'.repeat(64), observer_status_bytes: 512,
      input_stream_sha256: provenance.input_stream_sha256,
      input_status_sha256: 'e'.repeat(64), input_status_bytes: 256,
      setup_receipt_sha256: provenance.setup_receipt_sha256,
      menu_recipe_sha256: 'f'.repeat(64), input_plan_sha256: null,
      input_intentions_sha256: '1'.repeat(64),
      dolphin_commit: 'c77bbaa0f372c3f72281602a8b087206706542cb',
      dol_sha1: '08e0bf20134dfcb260699671004527b2d6bb1a45',
      dol_sha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
    };
    const phaseContext = (phase, sequence, seed) => {
      const entry = phase === 'vs_entry';
      const source_slices_hex = {'4:0': startBytes, '19:0': '804d5f90', '20:0': seed};
      return {phase, sequence, event: 'boundary', boundary: entry ? 'entry' : 'setup',
        pc: entry ? '0x8016e934' : '0x8016e9c4', source_tick: 0, draw_ordinal: 0,
        start_slice_address: 0x80480530,
        start_sha256: createHash('sha256').update(Buffer.from(startBytes, 'hex')).digest('hex'),
        typed_start: structuredClone(start), rng_pointer_address: 0x804d5f94,
        rng_pointer_hex: '804d5f90', seed_address: 0x804d5f90, seed_hex: seed,
        source_slices_hex,
        source_slice_inventory: [
          {tag: 4, flags: 0, address: 0x80480530, size: 0x138},
          {tag: 19, flags: 0, address: 0x804d5f94, size: 4},
          {tag: 20, flags: 0, address: 0x804d5f90, size: 4},
        ]};
    };
    const start_context = {schema: 'melee-web-stadium-go2-same-producer-start-context-v1',
      version: 1, entry: phaseContext('vs_entry', 80, '01234567'),
      setup: phaseContext('vs_setup', 90, '89abcdef'),
      scope: 'Same retained fresh producer\'s actual ordered Entry and completed Setup StartMeleeData plus pointer-owned RNG scalar; no browser or whole-session parity.',
      provenance: startContextProvenance};
    host_only.source_identity.go2_reference = {
      schema: 'melee-web-stadium-go2-source-reference-v2',
      source_stream_sha256: provenance.source_stream_sha256,
      input_stream_sha256: provenance.input_stream_sha256,
      setup_receipt_sha256: provenance.setup_receipt_sha256,
      ...startContextProvenance};
    const rows = bundles[3].ports.map(port => port.status_hex);
    host_only.expected_state.GO2 = {
      schema: 'melee-web-original-stadium-go2-declared-state', version: 1,
      decision: 'PASS_SOURCE_TYPED_FIELDS_ONLY',
      raw_identity: {sequence: 115, event: 'boundary', boundary: 'source_tick',
        source_tick: 3, draw_ordinal: 3},
      typed_fields: {...expected('GO2'), sequence: 115, source_tick: 3, draw_ordinal: 3},
      seed_observation: {sequence: 115, event: 'boundary', boundary: 'source_tick',
        source_tick: 3, draw_ordinal: 3, pointer_slot_address: 0x804D5F94,
        pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90,
        seed_hex: 'fedcba98', pointer_equals_seed_address: true},
      source_queue: {consume_sequence: 114, source_tick: 3, draw_ordinal: 3,
        queue_slot_status_hex: rows},
      provenance, start_context,
      scope: 'One actual GO-relative ordinal-2 SourceTick typed snapshot and direct seed bytes; no pointer comparison or whole-state parity.',
    };
  }
  return {schema: 'melee-web-stadium-source-action-replay-packet', version: 2,
    runtime_payload: {schema: 'melee-web-stadium-pad-action-plan', version: 1,
      bundles, schedule}, host_only};
}

function sameV6QueueObservationPacket() {
  const packet = {schema: 'melee-web-stadium-setup-cf-input-schedule', version: 1,
    runtime_payload: runtimePayload(), host_only: {
      expected_original_state: {summary: {decision: 'PASS_ORIGINAL_STATE_ONLY_COUNTERPART',
        schema: 'melee-web-original-stadium-idle-state', guest_pointer_numeric_comparison: false,
        source: {dol_sha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
          dol_sha1: '08e0bf20134dfcb260699671004527b2d6bb1a45'},
        snapshots: [expected('GO'), expected('C'), expected('F')]}},
      boundary_rng_observations: [
        {sequence: 2925, pc: '0x8016b824', pointer_slot_address: 0x804D5F94,
          pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'GO', event: 'progress', source_tick: 84, draw_ordinal: 84,
          pointer_equals_seed_address: true, seed_hex: 'f228f136', seed_raw_hex: 'f228f136'},
        {sequence: 2926, pc: '0x80390eb4', pointer_slot_address: 0x804D5F94,
          pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'C', event: 'boundary', source_tick: 84, draw_ordinal: 84,
          pointer_equals_seed_address: true, seed_hex: '868d77be', seed_raw_hex: '868d77be'},
        {sequence: 2931, pc: '0x80390eb4', pointer_slot_address: 0x804D5F94,
          pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'F', event: 'boundary', source_tick: 85, draw_ordinal: 85,
          pointer_equals_seed_address: true, seed_hex: 'b50f624b', seed_raw_hex: 'b50f624b'},
      ],
      source_event_provenance: {events: [{ordinal: 0,
        consumed_pad: {sequence: 2504, event: 'boundary', source_tick: 0, draw_ordinal: 0}}]},
    }};
  const rows = ['0000000000000000000000', '0000000000000000000000',
    '00000000000000000000ff', '00000000000000000000ff'];
  packet.runtime_payload.bundles[0].ports.forEach((port, index) => {
    port.status_hex = rows[index];
  });
  packet.host_only.required_queue_observation = {schema: 'melee-web-stadium-hsd-pad-queue-observation',
    version: 1, bundle: 0, source_tick: 0, source_sequence: 2504,
    expected_queue_slot_status_hex: rows,
    expected_players: [
      {player_index: 0, setup_port_1based: 1, source_slot_index: 0, controller_index: 0},
      {player_index: 1, setup_port_1based: 2, source_slot_index: 1, controller_index: 1},
    ]};
  return packet;
}

function expected(label) {
  const ordinal = label === 'C' ? 84 : label === 'F' ? 85 : label === 'GO2' ? 86 : 84;
  return {label, sequence: label === 'GO' ? 2925 : label === 'C' ? 2926 :
    label === 'F' ? 2931 : label === 'GO2' ? 2938 : 2931,
    source_tick: ordinal, draw_ordinal: ordinal,
    map2: {countdown_s32: 3628 - (label === 'F' ? 1 : 0), phase_s16: 0, form_s16: 5,
      previous_form_s16: -1, prior_form_s16: -1, gate_mask_40: false, gate_mask_80: false},
    display: {mode_s16: 11, flags_s16: 64, previous_flags_s16: 64, previous_mode_s16: 10,
      focused_player_s16: 99, selection_counter_s16: 0, packed_flags_raw_hex: label === 'F' ? '81' : '01',
      timer_raw_hex: '00000000', timer_initialization_observed: false, xF0_observed: false},
    title: {dynamic_title_text_bytes_observed: false, sis_table_entry_relationship_observed: false},
    map_root_allocation_lifetime_proved: false, map_root_object_type_observed: false};
}

function actual(label) {
  const ordinal = label === 'C' ? 84 : label === 'F' ? 85 : 86;
  const original = expected(label);
  return {label, ordinal, seed_hex: label === 'C' ? '868d77be' :
    label === 'F' ? 'b50f624b' : 'fedcba98',
    expected_branch: 0,
    idle_snapshot: {world_generation: 7, world_ticks: ordinal + 1, source_frame: ordinal + 1,
      map2_gate: 0, map2_countdown: original.map2.countdown_s32, map2_phase: 0,
      map2_form: 5, map2_history_e0: -1, map2_history_e2: -1, display_mode: 11,
      display_e6: 64, display_e8: 64, display_previous_mode: 10, display_selected_player: 99,
      display_f2: 0, display_flags: label === 'F' ? 1 : 0,
      display_timer_native_bytes: [0, 0, 0, 0], display_timer_initialization_observed: 0,
      sis_font_slot: 1, sis_static_text_role: 0, sis_dynamic_text_role: 1,
      sis_title_entry_seen: 1, sis_title_entry_index: 19, title_bytes_captured: 0},
    go_trace: {generation: 7, armed: 1, failure: 0, expected_branch: 0, next_sequence: 3,
      armed_world_ticks: 0, armed_source_frame: 0,
      stage_before: {sequence: 1, generation: 7, world_ticks: 84},
      stage_after: {sequence: 2, generation: 7, world_ticks: 84},
      go_after: {sequence: 3, generation: 7, world_ticks: 84, source_branch: 0},
      hud_after: {sequence: 0, generation: 0, world_ticks: 0}}};
}

test('accepts the exact 86-bundle/171-action plan and rejects host-only leakage', async () => {
  const payload = runtimePayload();
  assert.equal(validateRuntimePayload(payload), payload);
  const leaked = {...payload, host_only: {expected_state: {}}};
  assert.throws(() => validateRuntimePayload(leaked), /unexpected shape/);
  const bad = runtimePayload();
  bad.schedule[169] = {action: 'draw', ordinal: 85};
  assert.throws(() => validateRuntimePayload(bad), /fixed tick\/draw/);
});

test('derives a proper run2 prefix and dynamic C/F seed identities from host-only rows', async () => {
  const packet = run2Packet();
  const evidence = hostEvidenceFromPacket(packet);
  assert.equal(evidence.mode, 'run2-proper-prefix-v2');
  assert.equal(evidence.runtimePayload.schedule.length, 8);
  assert.deepEqual(evidence.checkpoints, {c_action_index: 2, f_action_index: 4,
    c_source_tick: 1, f_source_tick: 2, terminal_action_index: 7, terminal_draw_ordinal: 3});
  assert.deepEqual(evidence.originalSeeds, {C: '01234567', F: '89abcdef'});
  assert.deepEqual(evidence.coordinates, {cTick: 1, fTick: 2, goSequence: 100,
    cSequence: 105, fSequence: 110, goDraw: 1, cDraw: 1, fDraw: 2});

  const cActual = actual('C');
  cActual.ordinal = 1;
  cActual.idle_snapshot.world_ticks = 2;
  cActual.idle_snapshot.source_frame = 2;
  cActual.go_trace.armed_world_ticks = 0;
  cActual.go_trace.armed_source_frame = 0;
  for (const event of [cActual.go_trace.stage_before, cActual.go_trace.stage_after,
    cActual.go_trace.go_after]) event.world_ticks = 1;
  cActual.seed_hex = '01234567';
  const result = compareOriginalIdleSnapshot({label: 'C', expected: evidence.originalSnapshots[1],
    expectedGo: evidence.originalSnapshots[0], expectedSeedHex: evidence.originalSeeds.C,
    actual: cActual, coordinates: evidence.coordinates});
  assert.equal(result.decision, 'PASS_EXACT_DECLARED_C_F_FIELDS');

  const altered = structuredClone(packet);
  altered.host_only.boundary_rng_observations[0].sequence = 104;
  assert.throws(() => hostEvidenceFromPacket(altered), /seed row differs/);
  const wrongGoOrdinal = structuredClone(packet);
  wrongGoOrdinal.host_only.boundary_rng_observations[1].post_go_ordinal = 2;
  assert.throws(() => hostEvidenceFromPacket(wrongGoOrdinal), /seed row differs/);
  const wrongSequence = structuredClone(packet);
  wrongSequence.runtime_payload.schedule[4].bundle = 1;
  assert.throws(() => deriveRun2ProperPrefix(wrongSequence.runtime_payload,
    wrongSequence.host_only), /discontinuous/);
});

test('opt-in same-producer GO+2 branch binds typed state, seed, source queue and dynamic action', () => {
  const packet = run2Packet({go2: true});
  const evidence = hostEvidenceFromPacket(packet);
  assert.equal(evidence.mode, 'run2-proper-prefix-v2');
  assert.equal(evidence.runtimePayload.schedule.length, 8);
  assert.equal(evidence.go2ActionIndex, 6);
  assert.deepEqual(evidence.coordinates, {cTick: 1, fTick: 2, goSequence: 100,
    cSequence: 105, fSequence: 110, goDraw: 1, cDraw: 1, fDraw: 2,
    go2Sequence: 115, go2Tick: 3, go2Draw: 3});
  assert.equal(evidence.go2Snapshot.label, 'GO2');
  assert.equal(evidence.originalSeeds.GO2, 'fedcba98');
  assert.equal(evidence.go2StartContext.entry.sequence, 80);
  assert.equal(evidence.go2StartContext.setup.sequence, 90);
  assert.deepEqual(evidence.go2QueueExpectation.expected_queue_slot_status_hex,
    packet.runtime_payload.bundles[3].ports.map(port => port.status_hex));
  assert.throws(() => hostEvidenceFromPacket(packet, {fullSchedule: true}),
    /scoped only to the bounded proper-prefix branch/);
  assert.throws(() => hostEvidenceFromPacket(packet, {fullSchedule: true, commonFields: true}),
    /scoped only to the bounded proper-prefix branch/);

  const go2 = actual('GO2');
  go2.ordinal = 3;
  go2.idle_snapshot.world_ticks = 4;
  go2.idle_snapshot.source_frame = 4;
  for (const event of [go2.go_trace.stage_before, go2.go_trace.stage_after,
    go2.go_trace.go_after]) event.world_ticks = 1;
  const result = compareOriginalIdleSnapshot({label: 'GO2',
    expected: evidence.go2Snapshot, expectedGo: evidence.originalSnapshots[0],
    expectedSeedHex: evidence.originalSeeds.GO2, actual: go2,
    coordinates: evidence.coordinates});
  assert.equal(result.decision, 'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED');
  const altered = structuredClone(go2);
  altered.seed_hex = 'fedcba99';
  assert.equal(compareOriginalIdleSnapshot({label: 'GO2', expected: evidence.go2Snapshot,
    expectedGo: evidence.originalSnapshots[0], expectedSeedHex: evidence.originalSeeds.GO2,
    actual: altered, coordinates: evidence.coordinates}).decision,
  'REFUSE_FIRST_GO2_FIELD_MISMATCH');

  const callbacks = createHostCallbacks({setupApproved: () => true,
    originalSnapshots: evidence.originalSnapshots, originalSeeds: evidence.originalSeeds,
    coordinates: evidence.coordinates, go2Snapshot: evidence.go2Snapshot});
  const c = actual('C'); c.ordinal = 1; c.idle_snapshot.world_ticks = 2;
  c.idle_snapshot.source_frame = 2;
  for (const event of [c.go_trace.stage_before, c.go_trace.stage_after, c.go_trace.go_after])
    event.world_ticks = 1;
  c.seed_hex = '01234567';
  const f = actual('F'); f.ordinal = 2; f.idle_snapshot.world_ticks = 3;
  f.idle_snapshot.source_frame = 3;
  for (const event of [f.go_trace.stage_before, f.go_trace.stage_after, f.go_trace.go_after])
    event.world_ticks = 1;
  f.seed_hex = '89abcdef';
  assert.equal(callbacks.compare('C', JSON.stringify(c)), true);
  assert.equal(callbacks.compare('F', JSON.stringify(f)), true);
  assert.equal(callbacks.compare('GO2', JSON.stringify(go2)), true);
  assert.deepEqual(callbacks.receipts().map(row => row.phase), ['C', 'F', 'GO2']);

  const wrongProducer = structuredClone(packet);
  wrongProducer.host_only.source_identity.go2_reference.input_stream_sha256 = 'd'.repeat(64);
  assert.throws(() => hostEvidenceFromPacket(wrongProducer), /same-producer provenance/);
  const wrongQueue = structuredClone(packet);
  wrongQueue.host_only.expected_state.GO2.source_queue.queue_slot_status_hex[3] = '00'.repeat(11);
  assert.throws(() => hostEvidenceFromPacket(wrongQueue), /literal scheduled four-port input/);
  const missingContext = structuredClone(packet);
  delete missingContext.host_only.expected_state.GO2.start_context;
  assert.throws(() => hostEvidenceFromPacket(missingContext), /unexpected shape/);
  const wrongPointer = structuredClone(packet);
  wrongPointer.host_only.expected_state.GO2.start_context.entry.source_slices_hex['19:0'] = '804d5f94';
  assert.throws(() => hostEvidenceFromPacket(wrongPointer), /pointer ownership/);
  const wrongOrder = structuredClone(packet);
  wrongOrder.host_only.expected_state.GO2.start_context.setup.sequence = 120;
  assert.throws(() => hostEvidenceFromPacket(wrongOrder), /order or provenance differs/);
  const wrongPhase = structuredClone(packet);
  wrongPhase.host_only.expected_state.GO2.start_context.entry.phase = 'vs_setup';
  assert.throws(() => hostEvidenceFromPacket(wrongPhase), /unexpected shape|source identity/);
  const wrongReference = structuredClone(packet);
  wrongReference.host_only.source_identity.go2_reference.menu_recipe_sha256 = '0'.repeat(64);
  assert.throws(() => hostEvidenceFromPacket(wrongReference), /order or provenance differs/);
});

test('GO+2 same-producer start preflight admits dynamic pointer targets and rejects historical/mutated context', () => {
  const packet = run2Packet({go2: true});
  const alternateTarget = 0x804d5fa0;
  for (const phase of ['entry', 'setup']) {
    const row = packet.host_only.expected_state.GO2.start_context[phase];
    row.seed_address = alternateTarget;
    row.rng_pointer_hex = alternateTarget.toString(16);
    row.seed_hex = phase === 'entry' ? '01234567' : '89abcdef';
    row.source_slices_hex['19:0'] = row.rng_pointer_hex;
    row.source_slices_hex['20:0'] = row.seed_hex;
    row.source_slice_inventory.find(item => item.tag === 20).address = alternateTarget;
  }
  const evidence = hostEvidenceFromPacket(packet);
  const context = evidence.go2StartContext;
  const expected = (record, phase) => ({
    provenance: {observer_sha256: context.provenance.source_stream_sha256,
      observer_bytes: context.provenance.source_stream_bytes,
      observer_status_sha256: context.provenance.observer_status_sha256,
      observer_status_bytes: context.provenance.observer_status_bytes,
      sequence: record.sequence, pc: record.pc, source_tick: record.source_tick,
      draw_ordinal: record.draw_ordinal},
    [phase === 'vs_entry' ? 'source_entry' : 'source_setup']: {
      sequence: record.sequence, event: record.event, boundary: record.boundary,
      pc: Number.parseInt(record.pc, 16), source_tick: record.source_tick,
      draw_ordinal: record.draw_ordinal, start_slice_address: record.start_slice_address,
      source_slices_hex: structuredClone(record.source_slices_hex),
      source_slice_inventory: structuredClone(record.source_slice_inventory)},
    expected: {phase, random_seed_hex: record.seed_hex,
      start: structuredClone(record.typed_start)},
  });
  const entry = expected(context.entry, 'vs_entry');
  const setup = expected(context.setup, 'vs_setup');
  const admission = validateGo2StartContextExpectations(context, entry, setup);
  assert.equal(admission.decision, 'PASS_SELECTED_ENTRY_SETUP_MATCH_SAME_PRODUCER');
  const historic = structuredClone(entry);
  historic.provenance.observer_sha256 = '5'.repeat(64);
  assert.throws(() => validateGo2StartContextExpectations(context, historic, setup),
    /same-producer vs_entry context differs/);
  const changedSeed = structuredClone(setup);
  changedSeed.expected.random_seed_hex = '00000000';
  assert.throws(() => validateGo2StartContextExpectations(context, entry, changedSeed),
    /same-producer vs_setup context differs/);
  const changedStart = structuredClone(entry);
  changedStart.expected.start.players[0].fixture_player = 1;
  assert.throws(() => validateGo2StartContextExpectations(context, changedStart, setup),
    /same-producer vs_entry context differs/);
  const changedSeedTarget = structuredClone(setup);
  changedSeedTarget.source_setup.source_slice_inventory.find(item => item.tag === 20).address += 4;
  assert.throws(() => validateGo2StartContextExpectations(context, entry, changedSeedTarget),
    /same-producer vs_setup context differs/);
  const unownedTarget = structuredClone(packet);
  unownedTarget.host_only.expected_state.GO2.start_context.setup.rng_pointer_hex = '804d5fb0';
  assert.throws(() => hostEvidenceFromPacket(unownedTarget), /source identity, tagged pointer ownership/);
  const unalignedTarget = structuredClone(packet);
  unalignedTarget.host_only.expected_state.GO2.start_context.setup.seed_address = 0x804d5fa1;
  assert.throws(() => hostEvidenceFromPacket(unalignedTarget), /source identity, tagged pointer ownership/);
});

test('GO+2 strict driver compares the actual HSD queue and rejects byte drift', async () => {
  const evidence = hostEvidenceFromPacket(run2Packet({go2: true}));
  const mkDriver = approvals => {
    const driver = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
      admittedWorldGeneration: 7, go2QueueExpectation: evidence.go2QueueExpectation,
      native: {arm: async () => 1, queue: async () => 1,
        approve: async (_index, accepted) => { approvals.push(accepted); return 1; }}});
    driver.armed = true; driver.cursor = evidence.go2ActionIndex; driver.pending = true;
    driver.sourceFrame = 3;
    return driver;
  };
  const nativeReceipt = () => {
    const statuses = evidence.go2QueueExpectation.expected_queue_slot_status_hex;
    const bundleHex = evidence.runtimePayload.bundles[3].ports.map(port => port.status_hex).join('');
    return {failed: false, last_action: {complete: true, schedule_index: 6,
      action: 'tick', ordinal: 3, bundle: 3, source_ticks_before: 3, source_ticks_after: 4,
      source_draws_before: 3, source_draws_after: 3, world_ticks: 4,
      world_generation: 7,
      source_tick_input_pad_hex: bundleHex, source_frames_before: 3, source_frames_after: 4,
      source_frames: 4, go2_state_observation: {idle_snapshot: {source_frame: 4}},
      pad_hsd_queue_observation: {schema: 'melee-web-stadium-hsd-pad-queue-observation',
        version: 2, source_generation: 7, source_tick_before: 3, source_tick_after: 4,
        qnum_before: 1, qread_before: 0, qcount_before: 1, qcount_after_hsd: 0,
        player_count: 2, tick_complete: true,
        players: [{player_index: 0, setup_port_1based: 1, source_slot_index: 0,
          controller_index: 0, start_slot_field_raw: 0},
        {player_index: 1, setup_port_1based: 2, source_slot_index: 1,
          controller_index: 1, start_slot_field_raw: 0}],
        queue_slot_status_hex: statuses}}};
  };
  const hostComparison = {phase: 'GO2', decision: 'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED'};
  const approvals = [];
  const driver = mkDriver(approvals);
  assert.equal(await driver.accept(nativeReceipt(), hostComparison), true);
  assert.deepEqual(approvals, [true]);

  const rejectedApprovals = [];
  const rejected = mkDriver(rejectedApprovals);
  const mismatch = nativeReceipt();
  mismatch.last_action.pad_hsd_queue_observation.queue_slot_status_hex[3] = '00'.repeat(11);
  assert.equal(await rejected.accept(mismatch, hostComparison), false);
  assert.deepEqual(rejectedApprovals, [false]);
  assert.match(rejected.status().first_error, /GO\+2 HSD queue/);
});

test('encodes and gates each v2 manifest action with independent counters and C/F approval', async () => {
  const evidence = hostEvidenceFromPacket(run2Packet());
  const payload = evidence.runtimePayload;
  const manifest = encodeRun2NativeManifest(payload);
  const read32 = offset => manifest[offset] | (manifest[offset + 1] << 8) |
    (manifest[offset + 2] << 16) | (manifest[offset + 3] << 24);
  assert.equal(Buffer.from(manifest.subarray(0, 4)).toString('ascii'), 'MWP2');
  assert.deepEqual([read32(4), read32(8), read32(12), read32(16), read32(20)],
    [8, 2, 4, 1, 2]);
  assert.equal(manifest.byteLength, 24 + payload.schedule.length * 57);
  assert.deepEqual([...manifest.subarray(24, 37)], [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  assert.equal(Buffer.from(manifest.subarray(37, 81)).toString('hex'),
    payload.bundles[0].ports.map(port => port.status_hex).join(''));
  assert.deepEqual([...manifest.subarray(24 + 57, 24 + 57 + 57)],
    [2, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, ...new Array(44).fill(0)]);
  const malformed = structuredClone(payload);
  malformed.checkpoints.f_action_index = 5;
  assert.throws(() => encodeRun2NativeManifest(malformed), /dimensions differ/);

  let pending = null;
  const approvals = [];
  const native = {
    arm: bytes => bytes instanceof Uint8Array && bytes.length === manifest.length ? 1 : 0,
    queue: (index, action, bytes) => {
      pending = {index, action, bytes};
      return 1;
    },
    approve: (index, accepted) => {
      assert.equal(index, pending.index);
      approvals.push(accepted);
      pending = null;
      return 1;
    },
  };
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload, native});
  assert.equal(await driver.arm(true), true);
  let ticks = 0, draws = 0;
  for (let index = 0; index < payload.schedule.length; ++index) {
    const action = payload.schedule[index];
    const ticksBefore = ticks, drawsBefore = draws;
    if (action.action === 'tick') ++ticks;
    else ++draws;
    assert.equal(await driver.submit(action), true);
    assert.deepEqual(pending.bytes, action.action === 'tick'
      ? Uint8Array.from(payload.bundles[action.bundle].ports.flatMap(port =>
        port.status_hex.match(/../g).map(byte => Number.parseInt(byte, 16))))
      : new Uint8Array());
    const observation = {last_action: {complete: true, schedule_index: index,
      action: action.action, ordinal: action.action === 'tick' ? action.bundle : action.draw,
      ...(action.action === 'tick' ? {bundle: action.bundle} : {}),
      source_ticks_before: ticksBefore, source_ticks_after: ticks,
      source_draws_before: drawsBefore, source_draws_after: draws,
      source_frames_before: 0, source_frames_after: 0,
      source_frames: 0, world_ticks: ticks,
      observation: {idle_snapshot: {source_frame: 0}},
      source_tick_input_pad_hex: action.action === 'tick'
        ? payload.bundles[action.bundle].ports.map(port => port.status_hex).join('') : null}};
    const comparison = index === evidence.checkpoints.c_action_index
      ? {phase: 'C', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'}
      : index === evidence.checkpoints.f_action_index
        ? {phase: 'F', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'} : null;
    assert.equal(await driver.accept(observation, comparison), true,
      driver.status().first_error || `v2 action ${index} was refused`);
  }
  assert.equal(driver.status().complete, true);
  assert.deepEqual(approvals, new Array(payload.schedule.length).fill(true));

  const rejected = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: () => 1, queue: () => 1, approve: () => assert.fail('bad receipt was acknowledged')}});
  assert.equal(await rejected.arm(true), true);
  assert.equal(await rejected.submit(payload.schedule[0]), true);
  const badReceipt = {last_action: {complete: true, schedule_index: 0, action: 'tick',
    ordinal: 0, bundle: 0, source_ticks_before: 0, source_ticks_after: 1,
    source_draws_before: 0, source_draws_after: 0, source_frames_before: 0,
    source_frames_after: 1, source_frames: 1, world_ticks: 1,
    source_tick_input_pad_hex: 'ff'.repeat(44)}};
  assert.equal(await rejected.accept(badReceipt), false);
  assert.match(rejected.status().first_error, /scheduled raw-argument PAD differ/);
});

test('compares only the declared C/F scalars, named PPC flag masks, and owned source RNG', async () => {
  for (const label of ['C', 'F']) {
    const result = compareOriginalIdleSnapshot({label, expected: expected(label), expectedGo: expected('GO'),
      expectedSeedHex: label === 'C' ? '868d77be' : 'b50f624b', actual: actual(label)});
    assert.equal(result.decision, 'PASS_EXACT_DECLARED_C_F_FIELDS');
    assert.equal(result.unpaired.native_timer_raw_hex, '00000000');
    assert.equal(result.unpaired.original_f0_observed, false);
    assert.equal(result.unpaired.native_sis.title_entry_index, 19);
    assert.equal(result.unpaired.original_title.dynamic_title_text_bytes_observed, false);
  }
});

test('retains the first strict mismatch without tolerating a countdown or RNG offset', async () => {
  const wrongCountdown = actual('C');
  wrongCountdown.idle_snapshot.map2_countdown++;
  const result = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'), expectedGo: expected('GO'),
    expectedSeedHex: '868d77be', actual: wrongCountdown});
  assert.equal(result.decision, 'REFUSE_FIRST_C_F_FIELD_MISMATCH');
  assert.deepEqual(result.first_mismatch, {path: 'map2.countdown_s32', expected: 3628, actual: 3629});
  const wrongSeed = actual('F');
  wrongSeed.seed_hex = '868d77be';
  const seedResult = compareOriginalIdleSnapshot({label: 'F', expected: expected('F'), expectedGo: expected('GO'),
    expectedSeedHex: 'b50f624b', actual: wrongSeed});
  assert.equal(seedResult.first_mismatch.path, 'seed.raw32');
});

test('rejects a wrong GO membership/order receipt at C', async () => {
  const wrong = actual('C');
  wrong.go_trace.go_after.sequence = 4;
  const result = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'), expectedGo: expected('GO'),
    expectedSeedHex: '868d77be', actual: wrong});
  assert.equal(result.first_mismatch.path, 'go_trace.membership');
});

test('requires Setup approval and refuses duplicate arm or stale native ownership', async () => {
  const native = {arm: () => 0, queue: () => 1, approve: () => 1};
  const driver = new StadiumSetupCfActionDriver({runtimePayload: runtimePayload(), native});
  assert.equal(await driver.arm(false), false);
  assert.equal(driver.status().first_error, 'fresh completed Setup owner/approval was refused');
  const good = new StadiumSetupCfActionDriver({runtimePayload: runtimePayload(),
    native: {arm: () => 1, queue: () => 1, approve: () => 1}});
  assert.equal(await good.arm(true), true);
  assert.equal(await good.arm(true), false);
  assert.equal(good.status().first_error, 'duplicate or terminal arm');
});

test('rejects out-of-order actions and keeps the first native refusal sticky', async () => {
  const driver = new StadiumSetupCfActionDriver({runtimePayload: runtimePayload(),
    native: {arm: () => 1, queue: () => 0, approve: () => 1}});
  assert.equal(await driver.arm(true), true);
  assert.equal(await driver.submit({action: 'draw', ordinal: 0}), false);
  assert.equal(driver.status().first_error, 'out-of-order or altered source action');
  const refused = new StadiumSetupCfActionDriver({runtimePayload: runtimePayload(),
    native: {arm: () => 1, queue: () => 0, approve: () => 1}});
  await refused.arm(true);
  assert.equal(await refused.submit(runtimePayload().schedule[0]), false);
  assert.equal(refused.status().first_error, 'native source action was refused');
  assert.equal(await refused.submit(runtimePayload().schedule[0]), false);
  assert.equal(refused.status().first_error, 'native source action was refused');
});

test('does not advance past C or F without the exact host comparison receipt', async () => {
  let calls = 0;
  const native = {arm: () => 1, queue: () => { ++calls; return 1; }, approve: () => 1};
  const driver = new StadiumSetupCfActionDriver({runtimePayload: runtimePayload(), native});
  assert.equal(await driver.arm(true), true);
  for (let cursor = 0; cursor <= 168; ++cursor) {
    const action = runtimePayload().schedule[cursor];
    assert.equal(await driver.submit(action), true);
    const last_action = {...action, schedule_index: cursor, complete: true};
    const comparison = cursor === 168 ? {phase: 'C', decision: 'REFUSE_FIRST_C_F_FIELD_MISMATCH'} : null;
    assert.equal(await driver.accept({last_action}, comparison), cursor !== 168);
    if (cursor === 168) break;
  }
  assert.equal(driver.status().failed, true);
  assert.equal(driver.status().cursor, 168);
  assert.equal(calls, 169);
});

test('finishes only after F approval and never queues draw85', async () => {
  const queued = [];
  const payload = runtimePayload();
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: () => 1, queue: (index, action, bytes) => {
      queued.push({index, action, bytes: bytes.length}); return 1;
    }, approve: () => 1}});
  assert.equal(await driver.arm(true), true);
  for (let cursor = 0; cursor < payload.schedule.length; ++cursor) {
    const action = payload.schedule[cursor];
    assert.equal(await driver.submit(action), true);
    const label = cursor === 168 ? 'C' : cursor === 170 ? 'F' : null;
    const last_host_comparison = label
      ? {phase: label, decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'} : null;
    assert.equal(await driver.accept({last_action: {...action, schedule_index: cursor, complete: true}},
      last_host_comparison), true);
  }
  assert.equal(driver.status().complete, true);
  assert.equal(driver.status().cursor, 171);
  assert.equal(queued.length, 171);
  assert.deepEqual(queued.slice(-3).map(row => row.action), payload.schedule.slice(-3));
  assert.deepEqual(queued.slice(-3).map(row => row.bytes), [44, 0, 44]);
  assert.equal(queued.some(row => row.action.action === 'draw' && row.action.ordinal === 85), false);
});

test('rejects missing typed source observations instead of treating absent fields as equal', async () => {
  const incomplete = expected('C');
  delete incomplete.map2.phase_s16;
  assert.throws(() => compareOriginalIdleSnapshot({label: 'C', expected: incomplete, expectedGo: expected('GO'),
    expectedSeedHex: '868d77be', actual: actual('C')}), /observed integer/);
  const missingNative = actual('C');
  delete missingNative.idle_snapshot.map2_phase;
  assert.throws(() => compareOriginalIdleSnapshot({label: 'C', expected: expected('C'), expectedGo: expected('GO'),
    expectedSeedHex: '868d77be', actual: missingNative}), /observed integer/);
});

test('host callback keeps expected state in closure and refuses invalid C/F records', async () => {
  const host = createHostCallbacks({setupApproved: () => true,
    originalSnapshots: [expected('GO'), expected('C'), expected('F')],
    originalSeeds: {C: '868d77be', F: 'b50f624b'}});
  assert.equal(host.setupApproved(), true);
  assert.equal(host.compare('C', JSON.stringify(actual('C'))), true);
  assert.equal(host.compare('F', '{bad json'), false);
  assert.equal(host.receipts()[0].decision, 'PASS_EXACT_DECLARED_C_F_FIELDS');
  assert.equal(host.receipts()[1].decision, 'REFUSE_INVALID_C_F_RECEIPT');
  assert.equal(host.compare('F', JSON.stringify(actual('F'))), false,
    'a refusal must remain sticky and must not accept later boundaries');
});

test('host callback refuses F-before-C, repeated C, and relocated GO membership', async () => {
  const makeHost = () => createHostCallbacks({setupApproved: () => true,
    originalSnapshots: [expected('GO'), expected('C'), expected('F')],
    originalSeeds: {C: '868d77be', F: 'b50f624b'}});
  const beforeC = makeHost();
  assert.equal(beforeC.compare('F', JSON.stringify(actual('F'))), false);
  assert.equal(beforeC.receipts()[0].decision, 'REFUSE_C_F_ORDER');

  const repeatedC = makeHost();
  assert.equal(repeatedC.compare('C', JSON.stringify(actual('C'))), true);
  assert.equal(repeatedC.compare('C', JSON.stringify(actual('C'))), false);
  assert.equal(repeatedC.receipts()[1].decision, 'REFUSE_C_F_ORDER');

  const relocated = actual('C');
  relocated.go_trace.go_after.world_ticks = 98;
  relocated.idle_snapshot.world_ticks = 99;
  relocated.idle_snapshot.source_frame = 99;
  const result = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'),
    expectedGo: expected('GO'), expectedSeedHex: '868d77be', actual: relocated});
  assert.equal(result.first_mismatch.path, 'go_trace.membership');
  const branchOne = actual('C');
  branchOne.expected_branch = 1;
  branchOne.go_trace.expected_branch = 1;
  branchOne.go_trace.go_after.source_branch = 1;
  const branchResult = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'),
    expectedGo: expected('GO'), expectedSeedHex: '868d77be', actual: branchOne});
  assert.equal(branchResult.first_mismatch.path, 'go_trace.membership');
  const prematureHud = actual('F');
  prematureHud.go_trace.hud_after = {sequence: 4, generation: 7, world_ticks: 84};
  prematureHud.go_trace.next_sequence = 4;
  const hudResult = compareOriginalIdleSnapshot({label: 'F', expected: expected('F'),
    expectedGo: expected('GO'), expectedSeedHex: 'b50f624b', actual: prematureHud});
  assert.equal(hudResult.first_mismatch.path, 'go_trace.membership');
});

test('binds packet schema and exact DOL identity before constructing host expectations', async () => {
  const snapshots = [expected('GO'), expected('C'), expected('F')];
  const boundary_rng_observations = [
    {sequence: 2925, pc: '0x8016b824', pointer_slot_address: 0x804D5F94,
      pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'GO', event: 'progress', source_tick: 84, draw_ordinal: 84,
      pointer_equals_seed_address: true, seed_hex: 'f228f136', seed_raw_hex: 'f228f136'},
    {sequence: 2926, pc: '0x80390eb4', pointer_slot_address: 0x804D5F94,
      pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'C', event: 'boundary', source_tick: 84, draw_ordinal: 84,
      pointer_equals_seed_address: true, seed_hex: '868d77be', seed_raw_hex: '868d77be'},
    {sequence: 2931, pc: '0x80390eb4', pointer_slot_address: 0x804D5F94,
      pointer_raw_hex: '804d5f90', seed_address: 0x804D5F90, label: 'F', event: 'boundary', source_tick: 85, draw_ordinal: 85,
      pointer_equals_seed_address: true, seed_hex: 'b50f624b', seed_raw_hex: 'b50f624b'},
  ];
  const packet = {schema: 'melee-web-stadium-setup-cf-input-schedule', version: 1,
    runtime_payload: runtimePayload(), host_only: {
      expected_original_state: {summary: {decision: 'PASS_ORIGINAL_STATE_ONLY_COUNTERPART',
        schema: 'melee-web-original-stadium-idle-state', guest_pointer_numeric_comparison: false,
        source: {dol_sha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
          dol_sha1: '08e0bf20134dfcb260699671004527b2d6bb1a45'}, snapshots}},
      boundary_rng_observations}};
  const evidence = hostEvidenceFromPacket(packet);
  assert.equal(evidence.runtimePayload, packet.runtime_payload);
  assert.deepEqual(evidence.originalSeeds, {C: '868d77be', F: 'b50f624b'});
  const wrong = structuredClone(packet);
  wrong.host_only.expected_original_state.summary.source.dol_sha256 = '00'.repeat(32);
  assert.throws(() => hostEvidenceFromPacket(wrong), /host-only original evidence differs/);
});

test('strict same-v6 HSD queue gate binds the literal native rows and Setup map', async () => {
  const packet = sameV6QueueObservationPacket();
  const evidence = hostEvidenceFromPacket(packet);
  assert.equal(evidence.requiredQueueObservation.source_sequence, 2504);
  assert.deepEqual(evidence.requiredQueueObservation.expected_queue_slot_status_hex,
    packet.runtime_payload.bundles[0].ports.map(port => port.status_hex));

  const native = {approvals: [], arm: () => 1, queue: () => 1,
    approve(index, accepted) { this.approvals.push([index, accepted]); return 1; }};
  const accepted = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
    admittedWorldGeneration: 7, requiredQueueObservation: evidence.requiredQueueObservation, native});
  assert.throws(() => new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
    requiredQueueObservation: evidence.requiredQueueObservation, native}),
  /positive safe generation from checked Setup/);
  assert.equal(await accepted.arm(true), true);
  assert.equal(await accepted.submit(evidence.runtimePayload.schedule[0]), true);
  const queueObservation = {schema: 'melee-web-stadium-hsd-pad-queue-observation', version: 2,
    source_generation: 7, source_tick_before: 0, source_tick_after: 1,
    qnum_before: 1, qread_before: 0, qcount_before: 1, qcount_after_hsd: 0,
    player_count: 2, players: packet.host_only.required_queue_observation.expected_players.map(row => ({
      ...row, start_slot_field_raw: 0,
    })),
    queue_slot_status_hex: [...packet.host_only.required_queue_observation.expected_queue_slot_status_hex],
    tick_complete: true};
  const good = {last_action: {complete: true, schedule_index: 0, action: 'tick',
    ordinal: 0, bundle: 0, world_generation: 7, pad_hsd_queue_observation: queueObservation}};
  assert.equal(await accepted.accept(good), true, accepted.status().first_error || 'strict queue receipt refused');
  assert.deepEqual(native.approvals, [[0, true]]);

  const explicitPortDriver = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
    admittedWorldGeneration: 7, requiredQueueObservation: evidence.requiredQueueObservation,
    native: {arm: () => 1, queue: () => 1, approve: () => 1}});
  assert.equal(await explicitPortDriver.arm(true), true);
  assert.equal(await explicitPortDriver.submit(evidence.runtimePayload.schedule[0]), true);
  const explicitPortReceipt = structuredClone(good);
  explicitPortReceipt.last_action.pad_hsd_queue_observation.players[0].start_slot_field_raw = 1;
  explicitPortReceipt.last_action.pad_hsd_queue_observation.players[1].start_slot_field_raw = 2;
  assert.equal(await explicitPortDriver.accept(explicitPortReceipt), true,
    explicitPortDriver.status().first_error || 'explicit Start port fields were refused');

  const badReceipts = [
    ['missing observation', receipt => { delete receipt.last_action.pad_hsd_queue_observation; }],
    ['schema', receipt => { receipt.last_action.pad_hsd_queue_observation.schema = 'wrong'; }],
    ['legacy actual observation version', receipt => { receipt.last_action.pad_hsd_queue_observation.version = 1; }],
    ['generation', receipt => { receipt.last_action.pad_hsd_queue_observation.source_generation++; }],
    ['self-consistent but unadmitted generation', receipt => {
      receipt.last_action.world_generation = 8;
      receipt.last_action.pad_hsd_queue_observation.source_generation = 8;
    }],
    ['tick completion', receipt => { receipt.last_action.pad_hsd_queue_observation.tick_complete = false; }],
    ['HSD count', receipt => { receipt.last_action.pad_hsd_queue_observation.qcount_after_hsd = 1; }],
    ['queue number', receipt => { receipt.last_action.pad_hsd_queue_observation.qnum_before = 2; }],
    ['queue read index', receipt => { receipt.last_action.pad_hsd_queue_observation.qread_before = 1; }],
    ['queue count before HSD', receipt => { receipt.last_action.pad_hsd_queue_observation.qcount_before = 0; }],
    ['source tick before', receipt => { receipt.last_action.pad_hsd_queue_observation.source_tick_before = 1; }],
    ['source tick after', receipt => { receipt.last_action.pad_hsd_queue_observation.source_tick_after = 0; }],
    ['Setup map', receipt => { receipt.last_action.pad_hsd_queue_observation.players[1].controller_index = 0; }],
    ['missing raw Start slot field', receipt => {
      delete receipt.last_action.pad_hsd_queue_observation.players[0].start_slot_field_raw;
    }],
    ['effective port differs from raw Start slot field', receipt => {
      receipt.last_action.pad_hsd_queue_observation.players[0].setup_port_1based = 0;
    }],
    ['cross-slot explicit Start port', receipt => {
      receipt.last_action.pad_hsd_queue_observation.players[0].start_slot_field_raw = 2;
      receipt.last_action.pad_hsd_queue_observation.players[0].setup_port_1based = 2;
    }],
    ['duplicate player map', receipt => { receipt.last_action.pad_hsd_queue_observation.players[1].player_index = 0; }],
    ['reordered player map', receipt => {
      receipt.last_action.pad_hsd_queue_observation.players.reverse();
    }],
    ['inactive queue byte', receipt => {
      receipt.last_action.pad_hsd_queue_observation.queue_slot_status_hex[3] =
        '0000000000000000000000';
    }],
  ];
  for (let slot = 0; slot < 4; ++slot) {
    for (let byteIndex = 0; byteIndex < 11; ++byteIndex) {
      badReceipts.push([`queue slot ${slot} semantic byte ${byteIndex}`, receipt => {
        const queue = receipt.last_action.pad_hsd_queue_observation;
        const hex = queue.queue_slot_status_hex[slot];
        const offset = byteIndex * 2;
        const changed = (Number.parseInt(hex.slice(offset, offset + 2), 16) ^ 1)
          .toString(16).padStart(2, '0');
        queue.queue_slot_status_hex[slot] = hex.slice(0, offset) + changed + hex.slice(offset + 2);
      }]);
    }
  }
  for (const [label, mutate] of badReceipts) {
    const rejectNative = {approvals: [], arm: () => 1, queue: () => 1,
      approve(index, approved) { this.approvals.push([index, approved]); return 1; }};
    const driver = new StadiumSetupCfActionDriver({runtimePayload: evidence.runtimePayload,
      admittedWorldGeneration: 7, requiredQueueObservation: evidence.requiredQueueObservation,
      native: rejectNative});
    assert.equal(await driver.arm(true), true);
    assert.equal(await driver.submit(evidence.runtimePayload.schedule[0]), true);
    const receipt = structuredClone(good);
    mutate(receipt);
    assert.equal(await driver.accept(receipt), false, label);
    assert.match(driver.status().first_error, /HSD queue-slot observation/);
    assert.deepEqual(rejectNative.approvals, [[0, false]], label);
  }

  for (const mutate of [
    value => { value.host_only.required_queue_observation.version = 2; },
    value => { value.host_only.required_queue_observation.expected_players[1].controller_index = 0; },
    value => { value.host_only.required_queue_observation.expected_queue_slot_status_hex[3] = '00'.repeat(11); },
    value => { value.host_only.source_event_provenance.events[0].consumed_pad.sequence++; },
    value => { delete value.host_only.source_event_provenance; },
  ]) {
    const malformed = sameV6QueueObservationPacket();
    mutate(malformed);
    assert.throws(() => hostEvidenceFromPacket(malformed), /required HSD queue observation/);
  }

  const unsupportedRun2 = run2Packet();
  unsupportedRun2.host_only.required_queue_observation = {};
  assert.throws(() => hostEvidenceFromPacket(unsupportedRun2), /unsupported for run2 action packets/);
});

test('browser caller forwards the strict queue gate and checked Setup generation', () => {
  const source = readFileSync(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const driverCall = source.match(/const driver = new helper\.StadiumSetupCfActionDriver\(\{([\s\S]*?)\n\s*\}\);/)?.[1];
  assert.ok(driverCall, 'actual browser harness driver construction is present');
  assert.match(driverCall, /requiredQueueObservation:\s*hostEvidence\.requiredQueueObservation/);
  assert.match(driverCall,
    /admittedWorldGeneration:\s*\(commonFieldsMode \|\| hostEvidence\.requiredQueueObservation \|\|\s*hostEvidence\.go2QueueExpectation\)\s*\?\s*admittedWorldGeneration\s*:\s*null/);
  assert.match(driverCall, /go2QueueExpectation:\s*hostEvidence\.go2QueueExpectation\s*\?\?\s*null/);
  assert.match(source, /const admittedWorldGeneration = setupNative\.first_sss_vs_setup\.match_world_generation/);
});


test('checks each observed GO event generation and exact branch-zero membership', async () => {
  for (const event of ['stage_before', 'stage_after', 'go_after']) {
    const wrong = actual('C');
    wrong.go_trace[event].generation++;
    const result = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'),
      expectedGo: expected('GO'), expectedSeedHex: '868d77be', actual: wrong});
    assert.equal(result.first_mismatch.path, 'go_trace.membership');
  }
  const impossibleCounter = actual('C');
  impossibleCounter.go_trace.next_sequence = 5;
  const result = compareOriginalIdleSnapshot({label: 'C', expected: expected('C'),
    expectedGo: expected('GO'), expectedSeedHex: '868d77be', actual: impossibleCounter});
  assert.equal(result.first_mismatch.path, 'go_trace.membership');
});

test('retains raw unpaired bytes while comparing only the named gate and flag bits', async () => {
  const source = expected('F');
  source.map2.gate_mask_40 = true;
  source.display.packed_flags_raw_hex = '9f'; // same declared MSB masks as 81
  const native = actual('F');
  native.idle_snapshot.display_timer_native_bytes = [254, 255, 255, 255];
  const compare = () => compareOriginalIdleSnapshot({label: 'F', expected: source,
    expectedGo: expected('GO'), expectedSeedHex: 'b50f624b', actual: native});
  const unpaired = compare();
  assert.equal(unpaired.decision, 'PASS_EXACT_DECLARED_C_F_FIELDS');
  assert.equal(unpaired.unpaired.native_timer_raw_hex, 'feffffff');
  assert.equal(unpaired.unpaired.original_gate_mask_40, true);
  native.idle_snapshot.display_flags = 3;
  assert.equal(compare().first_mismatch.path, 'display.F8_1');
  native.idle_snapshot.map2_gate = 1;
  assert.equal(compare().first_mismatch.path, 'map2.gate_mask_80');
});

test('native approval refusal stops the driver before another action', async () => {
  let approvals = 0, queues = 0;
  const payload = runtimePayload();
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: () => 1, queue: () => { ++queues; return 1; },
      approve: () => { ++approvals; return 0; }}});
  await driver.arm(true);
  assert.equal(await driver.submit(payload.schedule[0]), true);
  assert.equal(await driver.accept({last_action: {...payload.schedule[0], schedule_index: 0,
    complete: true}}), false);
  assert.equal(await driver.submit(payload.schedule[1]), false);
  assert.equal(driver.status().cursor, 0);
  assert.equal(queues, 1);
  assert.equal(approvals, 1);
});

test('duplicate completion and mismatched action cannot acknowledge another source action', async () => {
  for (const duplicate of [false, true]) {
    const payload = runtimePayload();
    let approvals = 0;
    const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
      native: {arm: () => 1, queue: () => 1, approve: () => { ++approvals; return 1; }}});
    await driver.arm(true); await driver.submit(payload.schedule[0]);
    const receipt = {last_action: {...payload.schedule[0], schedule_index: 0, complete: true}};
    if (duplicate) {
      assert.equal(await driver.accept(receipt), true);
      assert.equal(await driver.accept(receipt), false);
      assert.equal(approvals, 1);
    } else {
      receipt.last_action.ordinal = 1;
      assert.equal(await driver.accept(receipt), false);
      assert.equal(approvals, 0);
    }
    assert.equal(driver.status().failed, true);
  }
});

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
}
function receipt(payload, index = 0) {
  return {last_action: {...payload.schedule[index], schedule_index: index, complete: true}};
}

test('async native responses are authoritative for arm, queue and acknowledgement', async () => {
  const payload = runtimePayload(), arm = deferred(), queue = deferred(), approve = deferred();
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: () => arm.promise, queue: () => queue.promise, approve: () => approve.promise}});
  const armed = driver.arm(true);
  assert.equal(driver.status().armed, false);
  assert.equal(driver.status().in_flight, true);
  arm.resolve(1); assert.equal(await armed, true);
  const submitted = driver.submit(payload.schedule[0]);
  assert.equal(driver.status().pending, false);
  assert.equal(driver.status().in_flight, true);
  queue.resolve(1); assert.equal(await submitted, true);
  const accepted = driver.accept(receipt(payload));
  assert.equal(driver.status().cursor, 0);
  assert.equal(driver.status().pending, true);
  assert.equal(driver.status().in_flight, true);
  approve.resolve(1); assert.equal(await accepted, true);
  assert.equal(driver.status().cursor, 1);
});

test('pending native acknowledgement blocks next action and remains terminal after response', async () => {
  const payload = runtimePayload(), ack = deferred(); let queues = 0;
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: async () => 1, queue: async () => { ++queues; return 1; },
      approve: () => ack.promise}});
  await driver.arm(true); await driver.submit(payload.schedule[0]);
  const accepted = driver.accept(receipt(payload));
  assert.equal(await driver.submit(payload.schedule[1]), false);
  const first = driver.status().first_error;
  ack.resolve(1); assert.equal(await accepted, false);
  assert.equal(driver.status().cursor, 0);
  assert.equal(driver.status().first_error, first);
  assert.equal(queues, 1);
});

test('concurrent arm or queue cannot turn a late actual success into driver progress', async () => {
  for (const stage of ['arm', 'queue']) {
    const payload = runtimePayload(), response = deferred(); let calls = 0;
    const native = {arm: async () => 1, queue: async () => 1, approve: async () => 1};
    native[stage] = () => { ++calls; return response.promise; };
    const driver = new StadiumSetupCfActionDriver({runtimePayload: payload, native});
    if (stage === 'queue') await driver.arm(true);
    const pending = stage === 'arm' ? driver.arm(true) : driver.submit(payload.schedule[0]);
    assert.equal(await (stage === 'arm' ? driver.arm(true) : driver.submit(payload.schedule[0])), false);
    response.resolve(1); assert.equal(await pending, false);
    assert.equal(calls, 1); assert.equal(driver.status().failed, true);
    assert.equal(driver.status().cursor, 0);
    assert.equal(driver.status().pending, false);
  }
});

test('async refusal and rejection at each native call remain sticky without advancing', async () => {
  for (const stage of ['arm', 'queue', 'approve']) for (const reject of [false, true]) {
    const payload = runtimePayload(); let calls = 0;
    const native = {arm: async () => 1, queue: async () => 1, approve: async () => 1};
    native[stage] = async () => { ++calls; if (reject) throw new Error('actual native error'); return 0; };
    const driver = new StadiumSetupCfActionDriver({runtimePayload: payload, native});
    const armed = await driver.arm(true);
    if (stage === 'arm') assert.equal(armed, false);
    else {
      assert.equal(armed, true);
      const submitted = await driver.submit(payload.schedule[0]);
      if (stage === 'queue') assert.equal(submitted, false);
      else { assert.equal(submitted, true); assert.equal(await driver.accept(receipt(payload)), false); }
    }
    const first = driver.status().first_error;
    assert.equal(driver.status().cursor, 0); assert.equal(driver.status().failed, true);
    assert.equal(await driver.submit(payload.schedule[0]), false);
    assert.equal(driver.status().first_error, first); assert.equal(calls, 1);
  }
});


// These are mocked owner-seam controls, not game execution or clock prediction.
function clockReceipt(payload, index, before, after) {
  const action = payload.schedule[index];
  const ticks = payload.schedule.slice(0, index).filter(row => row.action === 'tick').length;
  const draws = index - ticks;
  return {last_action: {complete: true, schedule_index: index, action: action.action,
    ordinal: action.action === 'tick' ? action.bundle : action.draw,
    ...(action.action === 'tick' ? {bundle: action.bundle} : {}),
    source_ticks_before: ticks, source_ticks_after: ticks + (action.action === 'tick' ? 1 : 0),
    source_draws_before: draws, source_draws_after: draws + (action.action === 'draw' ? 1 : 0),
    source_frames_before: before, source_frames_after: after, source_frames: after,
    world_ticks: ticks + (action.action === 'tick' ? 1 : 0),
    source_tick_input_pad_hex: action.action === 'tick'
      ? payload.bundles[action.bundle].ports.map(port => port.status_hex).join('') : null,
    observation: {idle_snapshot: {source_frame: after}}}};
}

function clockDriver() {
  const payload = hostEvidenceFromPacket(run2Packet()).runtimePayload;
  const approvals = [];
  const driver = new StadiumSetupCfActionDriver({runtimePayload: payload,
    native: {arm: () => 1, queue: () => 1,
      approve: (index, accepted) => {approvals.push({index, accepted}); return 1;}}});
  return {payload, driver, approvals};
}

test('v2 keeps the HUD-gated match clock separate from traversal ticks and acknowledges once', async () => {
  const {payload, driver} = clockDriver();
  assert.equal(await driver.arm(true), true);
  // HUD0 Ready: traversal tick advances while match clock stays at zero.
  assert.equal(await driver.submit(payload.schedule[0]), true);
  assert.equal(await driver.accept(clockReceipt(payload, 0, 0, 0)), true);
  assert.equal(await driver.submit(payload.schedule[1]), true);
  assert.equal(await driver.accept(clockReceipt(payload, 1, 0, 0)), true);
  // A source-eligible tick may instead advance the separate clock once.
  assert.equal(await driver.submit(payload.schedule[2]), true);
  assert.equal(await driver.accept(clockReceipt(payload, 2, 0, 1),
    {phase: 'C', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'}), true);
  assert.equal(await driver.submit(payload.schedule[3]), true);
  assert.equal(await driver.accept(clockReceipt(payload, 3, 1, 1)), true);
  assert.equal(await driver.accept(clockReceipt(payload, 3, 1, 1)), false);
});

test('v2 refuses clock mutation, discontinuity, malformed values and C/F receipt mismatch', async () => {
  for (const [label, index, before, after] of [
    ['clock before first tick changed while idle/queued', 0, 1, 1],
    ['tick jumps twice', 0, 0, 2],
    ['tick goes backward', 0, 0, -1],
    ['tick clock malformed', 0, 0, 0.5],
    ['tick clock overflow', 0, 0, 0x100000000],
    ['draw changes clock', 1, 0, 1],
    ['clock changes between acknowledgement and next action', 1, 1, 1],
  ]) {
    const {payload, driver, approvals} = clockDriver();
    assert.equal(await driver.arm(true), true);
    if (index === 1) {
      assert.equal(await driver.submit(payload.schedule[0]), true);
      assert.equal(await driver.accept(clockReceipt(payload, 0, 0, 0)), true);
    }
    assert.equal(await driver.submit(payload.schedule[index]), true);
    assert.equal(await driver.accept(clockReceipt(payload, index, before, after)), false, label);
    assert.match(driver.status().first_error, /match clock transition or continuity/, label);
    assert.equal(approvals.length, index, label);
  }
  for (const [boundaryIndex, label] of [[2, 'C'], [4, 'F']]) {
    const {payload, driver, approvals} = clockDriver();
    assert.equal(await driver.arm(true), true);
    for (let index = 0; index < boundaryIndex; ++index) {
      assert.equal(await driver.submit(payload.schedule[index]), true);
      const comparison = index === 2
        ? {phase: 'C', decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'} : null;
      assert.equal(await driver.accept(clockReceipt(payload, index, 0, 0), comparison), true);
    }
    assert.equal(await driver.submit(payload.schedule[boundaryIndex]), true);
    const mismatch = clockReceipt(payload, boundaryIndex, 0, 0);
    mismatch.last_action.observation.idle_snapshot.source_frame = 1;
    assert.equal(await driver.accept(mismatch,
      {phase: label, decision: 'PASS_EXACT_DECLARED_C_F_FIELDS'}), false);
    assert.match(driver.status().first_error, /snapshot match clock differs/);
    assert.deepEqual(approvals.at(-1), {index: boundaryIndex, accepted: false});
  }
});

test('native source clock predicate and owner wiring preserve separate wait/action ledgers', () => {
  const source = readFileSync(new URL('../src/gameplay_menu_browser.cpp', import.meta.url), 'utf8');
  const body = source.match(/bool matched_setup_cf_clock_transition\([^)]*\)\{([\s\S]*?)\n\}/)?.[1];
  assert.ok(body, 'actual native transition predicate is present');
  // Evaluate the unchanged boolean expression over bounded exact integers. This
  // is source-predicate coverage only, not a native compilation/runtime claim.
  const transition = new Function('tick_action', 'before', 'after', 'UINT32_MAX', body);
  for (const [tick, before, after, expected] of [
    [true, 0, 0, true], [true, 0, 1, true], [false, 0, 0, true],
    [true, 0, 2, false], [true, 1, 0, false], [false, 0, 1, false],
    [true, 0xffffffff, 0x100000000, false],
  ]) assert.equal(transition(tick, before, after, 0xffffffff), expected);
  const ownerStart = source.indexOf('void matched_setup_cf_check_owner(');
  const owner = source.slice(ownerStart, source.indexOf('bool matched_setup_cf_service()', ownerStart));
  assert.match(owner, /match->source_frames\(\)==state.last_source_frame_after/);
  assert.match(owner, /match->source_frames\(\)==state.queued_source_frame_before/);
  assert.doesNotMatch(owner, /match->source_frames\(\)==expected_ticks/);
  assert.match(source, /matched_setup_cf_clock_transition\(tick_action,state.queued_source_frame_before,/);
  assert.match(source, /state.idle.source_frame==state.last_source_frame_after/);
  assert.match(source,
    /state\.version2&&!state\.full_schedule&&!state\.common_fields&&tick_action&&\s*ordinal==c_tick\+2/,
    'GO+2 diagnostic serialization is restricted to its explicit bounded proper-prefix mode');
});
