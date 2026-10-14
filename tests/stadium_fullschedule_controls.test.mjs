import assert from 'node:assert/strict';
import {test} from 'node:test';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import * as helper from './stadium_matched_setup_cf_consumer.mjs';
import {encodeRun2FullNativeManifest, encodeRun2NativeManifest,
  StadiumSetupCfActionDriver} from './stadium_matched_setup_cf_consumer.mjs';
import {validateMatchedSetupCfRequest, validateMatchedSetupCfIdentities,
  validateMatchedSetupCfPreflight, matchedSetupCfRuntimeFailure, FULL_V3_SCOPE, PREFIX_V2_SCOPE}
  from './stadium_matched_setup_cf_runner_gate.mjs';
import {isStadiumC3ReportCheckpointEligible} from './stadium_c3_report_checkpoint.mjs';
function fullManifest() {
  const bundles = Array.from({length: 3585}, (_, bundle) => ({bundle,
    ports: Array.from({length: 4}, (_, port) => ({port, status_hex: '00'.repeat(11)}))}));
  const schedule = []; let draw = 0;
  for (let bundle = 0; bundle < bundles.length; ++bundle) {
    schedule.push({action: 'tick', action_index: schedule.length, bundle});
    if (![2000, 2500, 3000].includes(bundle))
      schedule.push({action: 'draw', action_index: schedule.length, draw: draw++});
  }
  return {schema: 'melee-web-stadium-run2-full-schedule-v3', version: 3, bundles, schedule,
    checkpoints: {c_action_index: 168, c_source_tick: 84, f_action_index: 170,
      f_source_tick: 85, terminal_action_index: 7166, terminal_draw_ordinal: 3581}};
}
test('full encoding is explicit; prefix guard and malformed dimensions remain strict', () => {
  const full = fullManifest(); const encoded = encodeRun2FullNativeManifest(full);
  assert.equal(encoded.length, 24 + 7167 * 57);
  assert.equal(Buffer.from(encoded.subarray(0, 4)).toString(), 'MWF3');
  const prefix = {...full, schema: 'melee-web-stadium-run2-proper-prefix-v2', version: 2,
    bundles: full.bundles.slice(0, 87), schedule: full.schedule.slice(0, 174),
    checkpoints: {...full.checkpoints, terminal_action_index: 173, terminal_draw_ordinal: 86}};
  assert.equal(encodeRun2NativeManifest(prefix).length, 24 + 174 * 57);
  assert.throws(() => encodeRun2FullNativeManifest(prefix));
  assert.throws(() => encodeRun2NativeManifest(full));
  const falsePrefix = {...full, schema: 'melee-web-stadium-run2-proper-prefix-v2', version: 2};
  assert.throws(() => encodeRun2NativeManifest(falsePrefix), /dimensions/);
  for (const mutate of [x => x.schedule.pop(), x => x.schedule.push({...x.schedule.at(-1)}),
    x => x.schedule[1].draw++, x => x.checkpoints.f_action_index++,
    x => { x.bundles[0].ports[0].status_hex = 'ff'; }]) {
    const bad = structuredClone(full); mutate(bad);
    assert.throws(() => encodeRun2FullNativeManifest(bad));
  }
  assert.throws(() => validateMatchedSetupCfRequest({enabled: true, setup: true,
    fullSchedule: true, explicitStep: false, packetPath: 'p', assemblyPath: 'a'}));
  assert.equal(isStadiumC3ReportCheckpointEligible({scenario: {matched_setup_cf:
    {mode: 'run2-full-schedule-v3', explicit_step_mode: true, schedule_actions: 7167}}}), true);
});
test('valid PAD mutation is structurally possible; pinned host identity and consumed receipt catch it', async () => {
  const full = fullManifest(); const changed = structuredClone(full);
  changed.bundles[0].ports[0].status_hex = '01' + '00'.repeat(10);
  assert.notDeepEqual(encodeRun2FullNativeManifest(changed), encodeRun2FullNativeManifest(full));
  const identity = value => ({path: '/fixture/packet.json', basename: 'packet.json',
    bytes: Buffer.byteLength(JSON.stringify(value)),
    sha256: crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex')});
  const original = identity(full), altered = identity(changed);
  assert.throws(() => validateMatchedSetupCfIdentities(
    {packet: original, assembly_manifest: original, helper: original},
    {packet: altered, assembly_manifest: original, helper: original}), /frozen preflight identity/);
  let approvals = 0;
  const driver = new StadiumSetupCfActionDriver({runtimePayload: full,
    native: {arm: async () => 1, queue: async () => 1, approve: async () => { ++approvals; return 1; }}});
  assert.equal(await driver.arm(true), true);
  assert.equal(await driver.submit(full.schedule[0]), true);
  const observation = {schema: 'melee-web-stadium-source-action-native-v3',
    scope: 'original-full7167-schedule-observation-only', action_count: 7167,
    last_action: {complete: true, schedule_index: 0, action: 'tick', ordinal: 0, bundle: 0,
      source_ticks_before: 0, source_ticks_after: 1, source_draws_before: 0, source_draws_after: 0,
      world_ticks: 1, source_tick_input_pad_hex: changed.bundles[0].ports.map(x => x.status_hex).join(''),
      source_frames_before: 0, source_frames_after: 1, source_frames: 1}};
  assert.equal(await driver.accept(observation), false);
  assert.equal(approvals, 0);
  assert.match(driver.status().first_error, /literal consumed PAD/);
});

test('full scope requires exact explicit preflight/export mapping; defaults remain prefix', () => {
  const file = {path: '/fixture/value', basename: 'value', bytes: 1, sha256: '0'.repeat(64)};
  const exports = {
    arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm',
    queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
    step: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
    retire_ready: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready',
    retire: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
    retirement_status: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status',
    approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
  };
  const frozen = {scope: FULL_V3_SCOPE, packet: file, assembly_manifest: file, helper: file,
    diagnostic_exports: exports, asyncify_idle_predicate: 'Module.stadiumMatchedSetupNativeIdle'};
  const preflight = {route_mode: 'first-sss-vs-setup', matched_setup_cf: frozen};
  const options = {enabled: true, explicitStep: true, fullSchedule: true,
    packetPath: file.path, assemblyPath: file.path, helperPath: file.path};
  assert.equal(validateMatchedSetupCfPreflight(preflight, options), frozen);
  assert.throws(() => validateMatchedSetupCfPreflight(preflight, {...options, fullSchedule: false}));
  assert.throws(() => validateMatchedSetupCfPreflight(preflight, {...options, explicitStep: false}));
  for (const mutate of [x => { x.scope = PREFIX_V2_SCOPE; },
    x => { delete x.diagnostic_exports.step; },
    x => { x.diagnostic_exports.extra = '_other'; },
    x => { x.diagnostic_exports.arm = '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm'; }]) {
    const bad = structuredClone(preflight); mutate(bad.matched_setup_cf);
    assert.throws(() => validateMatchedSetupCfPreflight(bad, options));
  }
});

async function runnerSource() {
  return fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
}
test('actual full-route construction has one encoder owner and dispatches full manifest', async () => {
  const source = await runnerSource();
  const adapterStart = source.indexOf('function matchedSetupCfNativeAdapter(');
  const adapterEnd = source.indexOf('\nasync function runMatchedSetupCfContinuation(', adapterStart);
  const constructionStart = source.indexOf('  const nativeAdapter = matchedSetupCfNativeAdapter({mode,', adapterEnd);
  const constructionEnd = source.indexOf('  await page.evaluate(', constructionStart);
  assert.ok(adapterStart >= 0 && adapterEnd > adapterStart && constructionStart > adapterEnd && constructionEnd > constructionStart);
  const calls = [];
  const context = {helper, mode: 'run2-full-schedule-v3', commonFieldsMode: false,
    firstSssVsSetupCfExplicitStep: true,
    hostEvidence: {runtimePayload: fullManifest()}, report: {cleanup: {}},
    remaining: () => 5000, timeout: promise => promise,
    matchedSetupCfExplicitNativeAccessClosed: false, matchedSetupCfExplicitArmed: false,
    matchedSetupCfExplicitAsyncifyUnsafe: false,
    executeMatchedSetupCfNativeCommand: async command => { calls.push(command); return {result: 1}; }};
  vm.createContext(context);
  vm.runInContext(source.slice(adapterStart, adapterEnd) + '\n' +
    source.slice(constructionStart, constructionEnd) + '\nglobalThis.actualDriver = driver;', context);
  assert.equal(await context.actualDriver.arm(true), true);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].kind, 'arm-explicit-full');
  assert.equal(calls[0].requireAsyncifyIdle, true);
  assert.equal(Buffer.from(calls[0].manifestBytes.slice(0, 4)).toString(), 'MWF3');
});

test('actual receipt/diagnostic/persist/accept tail saves raw evidence before ACK and stops on write failure', async () => {
  const source = await runnerSource();
  const diagnosticStart = source.indexOf('  const checkRuntimeDiagnostics = async (');
  const diagnosticEnd = source.indexOf('  const hostCallbacks =', diagnosticStart);
  const tailStart = source.indexOf('        const actionReceipt = timingSample', diagnosticEnd);
  const tailEnd = source.indexOf("        if (timingSample) {\n          timedActionSync(timingSample, 'accept_validation'", tailStart);
  assert.ok(diagnosticStart >= 0 && diagnosticEnd > diagnosticStart && tailStart > diagnosticEnd && tailEnd > tailStart);
  for (const failPersist of [false, true]) {
    const full = fullManifest(); const events = []; const committed = [];
    const driver = new StadiumSetupCfActionDriver({runtimePayload: full,
      native: {arm: async () => 1, queue: async () => 1, approve: async () => { events.push('ACK'); return 1; }}});
    assert.equal(await driver.arm(true), true); assert.equal(await driver.submit(full.schedule[0]), true);
    const receipt = {complete: true, schedule_index: 0, action: 'tick', ordinal: 0, bundle: 0,
      source_ticks_before: 0, source_ticks_after: 1, source_draws_before: 0, source_draws_after: 0,
      world_ticks: 1, source_tick_input_pad_hex: '00'.repeat(44),
      source_frames_before: 0, source_frames_after: 1, source_frames: 1};
    const cf = {actions: [], host_comparisons: [], runtime_diagnostics: [], last_native_state: null};
    const context = {assert, structuredClone, driver, cf, commonFieldsMode: false,
      timingSample: null, matchedSetupCfTiming: null,
      cfState: {schema: 'melee-web-stadium-source-action-native-v3',
        scope: 'original-full7167-schedule-observation-only', action_count: 7167, last_action: receipt},
      action: full.schedule[0], scheduleIndex: 0, cIndex: 168, fIndex: 170,
      pageErrors: [], consoleErrors: [], consoleWarnings: [], consoleMessages: [],
      observedPageErrors: 0, observedConsoleErrors: 0, observedConsoleWarnings: 0, observedConsoleMessages: 0,
      consoleMessageCaptureOverflow: false, consoleMessageDropped: 0,
      report: {scenario: {}}, matchedSetupCfRuntimeFailure,
      sourceSnapshot: async () => ({message: null, diagnostics: null, error: null}),
      persistReport: async () => {
        events.push('PERSIST');
        assert.equal(cf.actions.length, 1);
        assert.equal(cf.actions[0].evidence_scope, 'completed_native_receipt_before_host_approval');
        assert.deepEqual(cf.actions[0].native, receipt);
        assert.deepEqual(cf.last_native_state.last_action, receipt);
        if (failPersist) throw Error('controlled report write refusal');
        committed.push(structuredClone(cf));
      }};
    vm.createContext(context);
    vm.runInContext(source.slice(diagnosticStart, diagnosticEnd) + '\nglobalThis.runTail = async () => {\n' +
      source.slice(tailStart, tailEnd) + '\nreturn accepted; };', context);
    if (failPersist) {
      await assert.rejects(context.runTail(), /controlled report write refusal/);
      assert.deepEqual(events, ['PERSIST']); assert.equal(driver.status().cursor, 0);
    } else {
      assert.equal(await context.runTail(), true);
      assert.deepEqual(events, ['PERSIST', 'ACK', 'PERSIST']);
      assert.equal(committed[0].actions.length, 1); assert.equal(driver.status().cursor, 1);
    }
  }
});
