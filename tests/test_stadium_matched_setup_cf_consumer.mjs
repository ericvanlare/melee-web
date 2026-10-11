import assert from 'node:assert/strict';
import {test} from 'node:test';
import {
  compareOriginalIdleSnapshot,
  createHostCallbacks,
  hostEvidenceFromPacket,
  StadiumSetupCfActionDriver,
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

function expected(label) {
  const ordinal = label === 'C' ? 84 : label === 'F' ? 85 : 84;
  return {label, sequence: label === 'GO' ? 2925 : label === 'C' ? 2926 : 2931,
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
  const ordinal = label === 'C' ? 84 : 85;
  const original = expected(label);
  return {label, ordinal, seed_hex: label === 'C' ? '868d77be' : 'b50f624b',
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
