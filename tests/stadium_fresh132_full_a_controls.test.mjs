import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {buildFresh132FullAProjection} from './fresh132_full_a_projection.mjs';
import * as consumer from './stadium_matched_setup_cf_consumer.mjs';
import * as compare from './stadium_first_css_stream_compare.mjs';
import * as gate from './stadium_matched_setup_cf_runner_gate.mjs';

const [runRootArg] = process.argv.slice(2);
assert.ok(runRootArg, 'pass the retained run root as the first argument');
const runRoot = path.resolve(runRootArg);
const read = relative => fs.readFile(path.join(runRoot, relative));
const readyExpectedBytes = await read(
  'fresh-sss-ready-start-implementation-candidate-v1/data/fresh-sss-132-ready-start-expected-v1.json');
const readyExpected = JSON.parse(readyExpectedBytes.toString('utf8'));
const inputBytes = {
  extract: await read('fresh132-original-idle3500-postrun-independent-v3/adapter-extract-v1.json'),
  binding: await read('fresh132-original-idle3500-postrun-independent-v3/adapter-binding-v1.json'),
  postrun: await read('fresh132-original-idle3500-postrun-independent-v3/result-v1.json'),
  readyExpected: readyExpectedBytes,
};
const projection = buildFresh132FullAProjection(inputBytes, {
  validateRun2RuntimePayload: consumer.validateRun2RuntimePayload,
  validateFreshSss132ReadyStartExpectedSource: compare.validateFreshSss132ReadyStartExpectedSource,
});
const evidence = consumer.hostEvidenceFromFresh132FullAProjection(projection);
assert.equal(evidence.mode, 'fresh132-full-a-v1');
assert.equal(evidence.runtimePayload.schedule.length, 7168);
assert.equal(evidence.runtimePayload.bundles.length, 3585);
assert.equal(evidence.commonRowsByAction.size, 3501);
assert.equal(evidence.runtimePayload.schedule.at(-1).action, 'draw');
assert.equal(evidence.runtimePayload.schedule.at(-1).draw, 3582);
assert.equal(evidence.go2ActionIndex, 172);

const contextAdmission = await gate.validateFresh132FullARunnerSource({
  helperPath: fileURLToPath(new URL('./stadium_first_css_stream_compare.mjs', import.meta.url)),
  context: evidence.sourceContext,
  readyExpected,
});
assert.equal(contextAdmission.entry.sequence, 2544);
assert.equal(contextAdmission.setup.sequence, 2584);
assert.equal(contextAdmission.entry.source_tick, 131);
assert.equal(contextAdmission.constructor_observation.constructor_entry_count, 1);

const prefix = {
  complete: true, failed: false, current_host_entered: false,
  current_source_scene: 0, current_session_phase: 5,
  host_tick_calls: 132, host_draw_calls: 132,
  matched_ticks: 132, matched_draw_enters: 132, matched_draw_returns: 132,
  armed: true, kicked: true, attempted: true, captured: true, compared: true,
  selected_stage_captured: true, input_index: 132, consumed_inputs: 132,
  frame_end_returned: true, aurora_begin_calls: 132, aurora_end_calls: 132,
  last_tick_result: 3, transition_requested: true,
  audio_render_calls_before: 0, audio_render_calls_after: 132,
  audio_render_frames_before: 0, audio_render_frames_after: 70400,
  audio_phase_before: 0, audio_phase_after: 0,
};
const constructor = evidence.sourceContext.constructor_observation;
const setupSource = evidence.sourceContext.setup;
const nativeRules = Object.fromEntries(Object.entries(setupSource.typed_start.rules).map(([key, value]) => [
  key === 'hud_layout' ? 'x0_3' : key === 'item_frequency' ? 'xB' : key,
  typeof value === 'boolean' ? Number(value) : value,
]));
const nativePlayers = setupSource.typed_start.players.map(player => ({...player, xB: 0}));
nativePlayers.push({}, {});
const setupComparator = compare.createFresh132FullASetupComparator(
  evidence.sourceContext, readyExpected, prefix,
  {attempted: true, approved: true, failed: false, first_mismatch: null});
const setupEndpoint = {
  attempted: true, captured: true, complete: true, failed: false, error: null,
  construction_complete: true, source_frames: 0, host_tick_calls: 0, host_draw_calls: 0,
  actual: {phase: 'vs_setup', random_seed_hex: setupSource.seed_hex,
    start: {rules: nativeRules, players: nativePlayers}},
  stadium_setup_state: {
    schema: 'melee-web-stadium-zero-tick-setup-state-v1',
    constructor_callback_count: constructor.constructor_entry_count,
    constructor_randi_call_count: Number(constructor.random_call_observed),
    parameter_x0: constructor.parameter_x0, parameter_x4: constructor.parameter_x4,
    source_seed_before_hex: constructor.seed_before_hex,
    source_seed_after_hex: constructor.seed_after_hex,
    constructor_randi_bound: constructor.random_bound,
    constructor_randi_result: constructor.random_result,
    source_parameter_roots_agree: true,
  },
};
assert.equal(setupComparator.compare(setupEndpoint), true, JSON.stringify(setupComparator.status()));
assert.equal(setupComparator.status().scope,
  'same Fresh132 prepared Start and completed Setup, original Entry2544/Setup2584 at tick131, one constructor entry; zero-frame Setup only');

const identities = (basename, pathname) => ({path: pathname, basename, bytes: 1, sha256: 'a'.repeat(64)});
const packet = identities('full-a-packet.json', '/candidate/full-a-packet.json');
const assembly = identities('full-a-assembly.json', '/candidate/full-a-assembly.json');
const helperIdentity = identities('stadium_matched_setup_cf_consumer.mjs',
  fileURLToPath(new URL('./stadium_matched_setup_cf_consumer.mjs', import.meta.url)));
const legacyPreflight = JSON.parse((await read(
  'fresh-setup-go2-capture-candidate-v3/preflight-candidate-v1.json')).toString('utf8'));
const legacyReadyStartBinding = legacyPreflight.first_sss_fresh_ready_start;
const comparatorPath = fileURLToPath(new URL('./stadium_first_css_stream_compare.mjs', import.meta.url));
const comparatorBytes = await fs.readFile(comparatorPath);
const comparatorIdentity = {path: comparatorPath,
  bytes: comparatorBytes.byteLength,
  sha256: createHash('sha256').update(comparatorBytes).digest('hex')};
const preflight = {
  route_mode: gate.FRESH132_FULL_A_ROUTE,
  first_sss_fresh_ready_start: legacyReadyStartBinding,
  first_sss_fresh132_full_a: {
    schema: 'melee-web-stadium-fresh132-full-a-context-comparator-v1',
    context_comparator: comparatorIdentity,
  },
  matched_setup_cf: {
    scope: gate.FRESH132_FULL_A_SCOPE, packet, assembly_manifest: assembly,
    helper: helperIdentity,
    diagnostic_exports: {
      arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm',
      queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
      step: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
      retire_ready: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready',
      retire: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
      retirement_status: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status',
      approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
    },
    asyncify_idle_predicate: 'Module.stadiumMatchedSetupNativeIdle',
  },
};
assert.equal(gate.validateMatchedSetupCfRequest({enabled: true, explicitStep: true,
  fresh132FullA: true, setup: true, packetPath: packet.path, assemblyPath: assembly.path}), true);
assert.equal(gate.validateMatchedSetupCfPreflight(preflight, {
  enabled: true, freshSetupGo2: false, fresh132FullA: true,
  explicitStep: true, fullSchedule: false, commonFields: false,
  packetPath: packet.path, assemblyPath: assembly.path,
  helperPath: helperIdentity.path,
}), preflight.matched_setup_cf);
assert.equal(gate.resolveFreshSssReadyStartComparator(preflight, {fresh132FullA: true}),
  comparatorIdentity);
assert.equal(gate.validateFreshSssReadyStartComparator(preflight,
  {fresh132FullA: true, actual: comparatorIdentity}), comparatorIdentity);
assert.notEqual(legacyReadyStartBinding.comparator.sha256, comparatorIdentity.sha256,
  'The candidate comparator must not reuse the frozen legacy full-file pin');
const legacyRoute = {...preflight, route_mode: 'first-css-fresh-finite-sss-132-ready-start-v1'};
assert.equal(gate.resolveFreshSssReadyStartComparator(legacyRoute, {fresh132FullA: false}),
  legacyReadyStartBinding.comparator, 'Legacy ready-start retains its original comparator binding');
assert.throws(() => gate.resolveFreshSssReadyStartComparator(legacyRoute, {fresh132FullA: true}),
  /distinct frozen route/);
assert.throws(() => gate.resolveFreshSssReadyStartComparator(preflight, {fresh132FullA: false}),
  /cannot be consumed by the legacy/);
assert.throws(() => gate.validateFreshSssReadyStartComparator(preflight,
  {fresh132FullA: true, actual: {...comparatorIdentity, sha256: '0'.repeat(64)}}),
  /differs from the frozen preflight identity/);
assert.throws(() => gate.validateFreshSssReadyStartComparator({...preflight,
  first_sss_fresh132_full_a: {...preflight.first_sss_fresh132_full_a,
    context_comparator: legacyReadyStartBinding.comparator}},
  {fresh132FullA: true, actual: comparatorIdentity}), /differs from the frozen preflight identity/);
assert.throws(() => gate.validateMatchedSetupCfPreflight({...preflight,
  route_mode: gate.FRESH_SETUP_GO2_ROUTE}, {
  enabled: true, freshSetupGo2: false, fresh132FullA: true,
  explicitStep: true, fullSchedule: false, commonFields: false,
  packetPath: packet.path, assemblyPath: assembly.path, helperPath: helperIdentity.path,
}), /distinct frozen route/);
assert.throws(() => gate.validateMatchedSetupCfRequest({enabled: true, explicitStep: true,
  fresh132FullA: true, fullSchedule: true, setup: true,
  packetPath: packet.path, assemblyPath: assembly.path}));
assert.throws(() => gate.validateMatchedSetupCfPreflight(preflight, {
  enabled: true, freshSetupGo2: true, fresh132FullA: true,
  explicitStep: true, fullSchedule: false, commonFields: false,
  packetPath: packet.path, assemblyPath: assembly.path, helperPath: helperIdentity.path,
}));

const callbacks = consumer.createHostCallbacks({setupApproved: () => true,
  originalSnapshots: evidence.originalSnapshots, originalSeeds: evidence.originalSeeds,
  coordinates: evidence.coordinates, go2Snapshot: evidence.go2Snapshot});
const goEvent = (worldGeneration, goSeed) => ({
  failure: 0, armed: 1, generation: worldGeneration, armed_world_ticks: 0,
  armed_source_frame: 0, expected_branch: 0, next_sequence: 3,
  stage_before: {sequence: 1, world_ticks: 84, generation: worldGeneration},
  stage_after: {sequence: 2, world_ticks: 84, generation: worldGeneration},
  go_after: {sequence: 3, source_branch: 0, world_ticks: 84, generation: worldGeneration,
    source_seed_hex: goSeed, source_seed_valid: 1},
  hud_after: {sequence: 0},
});
function makeBoundaryActual(label, worldGeneration, sourceFrame) {
  const expected = label === 'GO2' ? evidence.go2Snapshot
    : evidence.originalSnapshots.find(row => row.label === label);
  const source = evidence.commonRowsByAction.get(
    label === 'GO2' ? evidence.go2ActionIndex : label === 'C' ? 168 : 170);
  const typed = projection.host_expectations.typed_rows[source.seed_ordinal].typed_fields;
  const flags = Number.parseInt(typed.display.packed_flags_raw_hex, 16);
  const displayFlags = ((flags & 0x80) ? 1 : 0) | ((flags & 0x40) ? 2 : 0) | ((flags & 0x20) ? 4 : 0);
  const idle = {
    source_frame: sourceFrame, world_generation: worldGeneration,
    world_ticks: expected.source_tick + 1,
    map2_countdown: typed.map2.countdown_s32, map2_phase: typed.map2.phase_s16,
    map2_form: typed.map2.form_s16, map2_history_e0: typed.map2.previous_form_s16,
    map2_history_e2: typed.map2.prior_form_s16, map2_gate: Number(typed.map2.gate_mask_80),
    display_mode: typed.display.mode_s16, display_e6: typed.display.flags_s16,
    display_e8: typed.display.previous_flags_s16,
    display_previous_mode: typed.display.previous_mode_s16,
    display_selected_player: typed.display.focused_player_s16,
    display_f2: typed.display.selection_counter_s16, display_flags: displayFlags,
    display_timer_initialization_observed: 0,
    display_timer_native_bytes: [...Buffer.from(typed.display.timer_raw_hex, 'hex')],
  };
  return {label, ordinal: expected.source_tick, expected_branch: 0,
    seed_hex: evidence.originalSeeds[label], idle_snapshot: idle,
    go_trace: goEvent(worldGeneration, evidence.originalSeeds.GO)};
}
const generation = 17;
const cHost = JSON.parse(JSON.stringify(makeBoundaryActual('C', generation, 85)));
const fHost = makeBoundaryActual('F', generation, 86);
const go2Host = makeBoundaryActual('GO2', generation, 87);
assert.equal(callbacks.compare('C', JSON.stringify(cHost)), true);
assert.equal(callbacks.compare('F', JSON.stringify(fHost)), true);
assert.equal(callbacks.compare('GO2', JSON.stringify(go2Host)), true);
assert.deepEqual(callbacks.receipts().map(row => row.decision), [
  'PASS_EXACT_DECLARED_C_F_FIELDS', 'PASS_EXACT_DECLARED_C_F_FIELDS',
  'PASS_EXACT_DECLARED_GO2_FIELDS_AND_SEED',
]);

let queued = 0, approved = 0;
const driver = new consumer.StadiumSetupCfActionDriver({
  runtimePayload: evidence.runtimePayload,
  native: {
    arm: async bytes => { assert.equal(bytes.length, 408600); assert.equal(Buffer.from(bytes).toString('ascii', 0, 4), 'MWC6'); return 1; },
    queue: async () => { ++queued; return 1; },
    approve: async () => { ++approved; return 1; },
  },
  fresh132FullA: true, commonFields: false,
  commonRowsByAction: evidence.commonRowsByAction,
  admittedWorldGeneration: generation, go2ActionIndex: evidence.go2ActionIndex,
});
assert.equal(await driver.arm(callbacks.setupApproved()), true);
let ticks = 0, draws = 0, sourceFrame = 0, commonPassed = 0;
for (let index = 0; index < evidence.runtimePayload.schedule.length; ++index) {
  const action = evidence.runtimePayload.schedule[index];
  const ticksBefore = ticks, drawsBefore = draws, framesBefore = sourceFrame;
  if (action.action === 'tick') { ++ticks; ++sourceFrame; }
  else ++draws;
  assert.equal(await driver.submit(action), true, `submit ${index}`);
  const expectedRow = evidence.commonRowsByAction.get(index);
  const selected = action.action === 'tick' && action.bundle >= 84 && action.bundle <= 3584;
  const sample = selected ? {
    schema: 'melee-web-stadium-per-tick-common-fields-native-v2',
    schedule_index: index, source_tick: action.bundle, world_ticks: ticks,
    source_frame: sourceFrame, world_generation: generation,
    ...expectedRow.values, seed_hex: expectedRow.seed_hex,
  } : null;
  let commonComparison = null;
  if (selected) {
    commonComparison = consumer.compareOriginalCommonTickFields({expected: expectedRow,
      actual: sample, actionIndex: index, sourceFramesAfter: sourceFrame,
      admittedWorldGeneration: generation});
    assert.equal(commonComparison.decision, 'PASS_EXACT_15_COMMON_FIELDS_AND_SEED', `compare ${index}`);
    ++commonPassed;
  }
  const boundaryLabel = index === 168 ? 'C' : index === 170 ? 'F' : index === evidence.go2ActionIndex ? 'GO2' : null;
  const lastAction = {
    complete: true, schedule_index: index, action: action.action,
    ordinal: action.action === 'tick' ? action.bundle : action.draw,
    ...(action.action === 'tick' ? {bundle: action.bundle,
      source_tick_input_pad_hex: evidence.runtimePayload.bundles[action.bundle].ports.map(port => port.status_hex).join('')} :
      {source_tick_input_pad_hex: null}),
    source_ticks_before: ticksBefore, source_ticks_after: ticks,
    source_draws_before: drawsBefore, source_draws_after: draws,
    world_ticks: ticks, source_frames_before: framesBefore,
    source_frames_after: sourceFrame, source_frames: sourceFrame,
    world_generation: generation,
    common_fields_sample: sample,
    ...(boundaryLabel === 'C' || boundaryLabel === 'F'
      ? {observation: {idle_snapshot: {source_frame: sourceFrame}}} : {}),
    ...(boundaryLabel === 'GO2' ? {go2_state_observation: {
      schema: 'melee-web-stadium-go2-declared-state-native-v1', label: 'GO2', ordinal: 86,
      idle_snapshot: {world_generation: generation, world_ticks: 87, source_frame: sourceFrame},
    }} : {}),
  };
  const hostComparison = boundaryLabel === 'C' ? callbacks.receipts()[0]
    : boundaryLabel === 'F' ? callbacks.receipts()[1]
    : boundaryLabel === 'GO2' ? callbacks.receipts()[2] : null;
  const nativeObservation = {
    schema: 'melee-web-stadium-source-action-native-v6',
    scope: 'fresh132-full-a-7168-common-fields-seed-observation-only',
    common_field_count: 15, seed_value_count: 1, action_count: 7168,
    failed: false, last_action: lastAction,
  };
  assert.equal(await driver.accept(nativeObservation, hostComparison, commonComparison), true, `accept ${index}`);
}
assert.equal(driver.status().complete, true);
assert.equal(driver.status().cursor, 7168);
assert.equal(queued, 7168);
assert.equal(approved, 7168);
assert.equal(ticks, 3585);
assert.equal(draws, 3583);
assert.equal(commonPassed, 3501);

const wrongActionDriver = new consumer.StadiumSetupCfActionDriver({
  runtimePayload: evidence.runtimePayload,
  native: {arm: async () => 1, queue: async () => 1, approve: async () => 1},
  fresh132FullA: true, commonRowsByAction: evidence.commonRowsByAction,
  admittedWorldGeneration: generation, go2ActionIndex: evidence.go2ActionIndex,
});
assert.equal(await wrongActionDriver.arm(true), true);
assert.equal(await wrongActionDriver.submit(evidence.runtimePayload.schedule[1]), false);
assert.match(wrongActionDriver.status().first_error, /out-of-order or altered source action/);

const alteredContext = structuredClone(evidence.sourceContext);
alteredContext.setup.source_tick += 1;
await assert.rejects(gate.validateFresh132FullARunnerSource({
  helperPath: fileURLToPath(new URL('./stadium_first_css_stream_compare.mjs', import.meta.url)),
  context: alteredContext, readyExpected,
}));
const missingRow = structuredClone(projection);
missingRow.host_expectations.typed_rows.pop();
assert.throws(() => consumer.hostEvidenceFromFresh132FullAProjection(missingRow));

console.log(JSON.stringify({
  result: 'PASS_FRESH132_FULL_A_MWC6_PROJECTION_SETUP_GATE_HOST_AND_DRIVER_CONTROLS',
  dimensions: {actions: queued, ticks, draws, comparison_rows: commonPassed},
  setup: {entry_sequence: 2544, setup_sequence: 2584, source_tick: 131, constructor_entries: 1},
  dynamic_boundaries: {GO: evidence.goSeedObservation.sequence, C: 84, F: 85, GO2: 86},
  exact_common_seed_comparisons: 56016,
  negative_controls: ['mixed_mode_request', 'mixed_mode_preflight',
    'fresh132_full_a_comparator_wrong_hash', 'fresh132_full_a_comparator_crossed_with_legacy_pin',
    'fresh132_full_a_old_route_rejected', 'fresh132_full_a_route_cannot_use_legacy_comparator_binding',
    'wrong_action_order', 'altered_context', 'missing_typed_row'],
  limits: ['no browser or native runtime executed', 'no full-session or C3 acceptance',
    'no complete typed parity or per-owner RNG claim'],
}, null, 2));
