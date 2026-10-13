import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';
import vm from 'node:vm';
import {validateMatchedSetupCfRequest, validateMatchedSetupCfPreflight,
  validateMatchedSetupCfIdentities, matchedSetupCfRuntimeFailure, PREFIX_V2_SCOPE,
  trackMatchedSetupCfExplicitStep, matchedSetupCfNativeAccessAllowed,
  dispatchMatchedSetupCfNativeEvaluation, guardMatchedSetupCfNativeCleanup,
  ASYNCIFY_IDLE_PREDICATE}
  from './stadium_matched_setup_cf_runner_gate.mjs';

const CANDIDATE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(process.env.MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT || CANDIDATE_ROOT);
const RUNNER = path.join(CANDIDATE_ROOT, 'tests/stadium_c1a_browser_test.mjs');
const HELPER = path.join(CANDIDATE_ROOT, 'tests/stadium_matched_setup_cf_consumer.mjs');
const SCOPE = 'same-run completed Setup, then exact original 86-tick/85-draw prefix; compare C before draw84 and F before draw85';
const EXPORTS = {
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
};
const PREFIX_V2_EXPORTS = {
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
};
const PREFIX_V2_EXPLICIT_STEP_EXPORTS = {
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  step: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
  retire_ready: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready',
  retire: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
  retirement_status: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
};
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const identity = (file, bytes) => ({path: file, basename: path.basename(file),
  bytes: bytes.byteLength, sha256: hash(bytes)});

test('the matched Setup C/F opt-in and preflight gate fail before server/browser startup', async () => {
  const scratch = process.env.MELEE_STADIUM_CF_RUNNER_TEST_ROOT;
  assert.ok(scratch, 'Python OwnedWorkspaceTests scratch path is required');
  const runnerSource = await fs.readFile(RUNNER, 'utf8');
  assert.match(runnerSource,
    /\['first-sss-vs-setup-cf', \{type: 'boolean', default: false\}\]/,
    'the continuation must remain opt-in by default');
  assert.match(runnerSource,
    /\['first-sss-vs-setup-cf-explicit-step', \{type: 'boolean', default: false\}\]/,
    'explicit source stepping must remain a separate opt-in');
  assert.match(runnerSource,
    /module\.ccall\(\s*'melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once'[\s\S]*?\{async: true\}\)/,
    'explicit service must use ccall’s unmangled identifier and await Asyncify');
  assert.match(runnerSource, /stadiumMatchedSetupNativeIdle/,
    'native access must fail closed unless the private Asyncify idle predicate is true');
  assert.match(runnerSource,
    /module\.ccall\([\s\S]*?'melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire'[\s\S]*?\{async: true\}/,
    'terminal native retirement must await the generated Asyncify ccall');
  assert.match(runnerSource, /kind === 'retire-ready'[\s\S]*?kind === 'retire-explicit'/,
    'retirement readiness must be a separate preflighted operation before the yielding ccall');
  const gateSource = await fs.readFile(path.join(CANDIDATE_ROOT,
    'tests/stadium_matched_setup_cf_runner_gate.mjs'), 'utf8');
  assert.match(gateSource, /retire_ready:[\s\S]*?retirement_status:/,
    'retire readiness and archived receipt exports must be separately bound');
  assert.match(runnerSource, /native_cleanup_suppressed_pending_async_step/,
    'a timed-out unsettled native continuation must suppress native cleanup');
  const commandStart = runnerSource.indexOf('async function executeMatchedSetupCfNativeCommand(');
  const commandEnd = runnerSource.indexOf('\nfunction matchedSetupCfNativeAdapter(', commandStart);
  const commandSource = runnerSource.slice(commandStart, commandEnd);
  assert.ok(commandSource.indexOf('dispatchMatchedSetupCfNativeEvaluation') <
    commandSource.indexOf('dispatch: () => page.evaluate('),
  'the pending-step guard must run before starting page.evaluate');
  const cleanupStart = runnerSource.indexOf('async function cleanupOwnedProcesses()');
  const cleanupEnd = runnerSource.indexOf('\ntry {\n  await main();', cleanupStart);
  const cleanupSource = runnerSource.slice(cleanupStart, cleanupEnd);
  assert.ok(cleanupSource.indexOf('guardMatchedSetupCfNativeCleanup') <
    cleanupSource.indexOf('readFirstSssVsEntryObservation()'),
  'the JS-only cleanup idle gate must precede failure-state native getters');
  assert.match(cleanupSource,
    /report\.result !== 'pass'[\s\S]*?!nativeCleanupSuppressed/,
    'failure-state native snapshots must be suppressed when cleanup idle is unsafe');
  assert.match(cleanupSource,
    /if \(driver && !unloadAttempted && page && !page\.isClosed\(\) && !nativeCleanupSuppressed\) \{[\s\S]*?await timeout\(driver\.unload/,
    'native unload must be suppressed when cleanup idle is unsafe');
  const nativeSource = await fs.readFile(
    path.join(CANDIDATE_ROOT, 'src/gameplay_menu_browser.cpp'), 'utf8');
  const tickStart = nativeSource.indexOf('void tick()');
  const tickEnd = nativeSource.indexOf('service_render_cache_writes();', tickStart);
  const tickPrelude = nativeSource.slice(tickStart, tickEnd);
  assert.match(tickPrelude,
    /if\(matched_setup_cf\.explicit_step_mode\)return;/,
    'RAF ticks must not consume actions in explicit-step mode');
  const explicitArmStart = nativeSource.indexOf(
    'melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm');
  const explicitStepStart = nativeSource.indexOf(
    'melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once');
  const explicitStepEnd = nativeSource.indexOf(
    'melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue(', explicitStepStart);
  const explicitStepBody = nativeSource.slice(explicitArmStart, explicitStepEnd);
  assert.ok(explicitArmStart >= 0 && explicitStepStart > explicitArmStart);
  assert.match(explicitStepBody, /emscripten_cancel_main_loop\(\)/,
    'arm must clear MainLoop.func so Asyncify does not restart RAF');
  assert.match(explicitStepBody, /matched_setup_cf_service\(\)/,
    'explicit mode must reuse the single existing source service');
  assert.match(nativeSource,
    /Module\.stadiumMatchedSetupNativeIdle=\(\)=>[\s\S]*?!ABORT[\s\S]*?Asyncify\.state===Asyncify\.State\.Normal[\s\S]*?Asyncify\.currData===null/,
    'the private browser predicate must reject abort, unwind/rewind, and retained Asyncify data');
  const retireStart = nativeSource.indexOf(
    'int melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire(){');
  const retireEnd = nativeSource.indexOf(
    'int melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue(', retireStart);
  const retirementSource = nativeSource.slice(retireStart, retireEnd);
  assert.ok(retireStart >= 0 && retireEnd > retireStart);
  assert.match(retirementSource, /matched_setup_cf_json\(\)/,
    'the terminal C/F receipt must be archived before close resets native diagnostic state');
  assert.ok(retirementSource.indexOf('matched_setup_cf_json()') < retirementSource.indexOf('close()'));
  assert.match(retirementSource, /close\(\)[\s\S]*?melee_web_input_shutdown\(\)[\s\S]*?aurora_shutdown\(\)/,
    'retirement must reuse checked owner close/input/Aurora shutdown');
  assert.doesNotMatch(retirementSource, /matched_setup_cf_service\(\)|tick\(\)/,
    'retirement cannot run another source step or resume the frame loop');
  assert.match(retirementSource, /menuMatchedSetupCfRetired/,
    'retirement must notify the shared runtime owner only after native shutdown');
  const runtimeSource = await fs.readFile(path.join(CANDIDATE_ROOT, 'web/melee-runtime.mjs'), 'utf8');
  const callbackStart = runtimeSource.indexOf('menuMatchedSetupCfRetired(receipt)');
  const callbackEnd = runtimeSource.indexOf('menuFrame(wasRunning)', callbackStart);
  const callbackSource = runtimeSource.slice(callbackStart, callbackEnd);
  assert.ok(callbackStart >= 0 && callbackEnd > callbackStart);
  assert.doesNotMatch(callbackSource, /Module\._melee_web_native_menu_/,
    'the runtime retirement callback must not call a native getter inside the yielding ccall');
  assert.match(callbackSource, /terminalMatchedSetupCfRetirementConsumed = true[\s\S]*?publish\(\)/,
    'retirement publishes inactive runtime state using the retained native receipt');
  assert.match(runnerSource,
    /kind === 'retire-explicit'[\s\S]*?module\.ccall\([\s\S]*?melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire[\s\S]*?\{async: true\}/,
    'terminal retirement must be an awaited generated-runtime ccall');
  assert.match(runnerSource, /pipeline_cache_status_before_shutdown[\s\S]*?pipeline_cache_status_after_shutdown/,
    'the retirement evidence must retain cache status before and after native shutdown');
  assert.match(runnerSource, /javascript_idbfs_persistence: 'not_run'/,
    'the one-shot retirement must not claim ordinary JS IDBFS persistence');
  assert.match(runnerSource,
    /if \(matchedSetupCfExplicitArmed\) \{[\s\S]*?retireMatchedSetupCfExplicitNative\(\)/,
    'the armed explicit path must use one-shot retire instead of the queued ordinary unload');
  assert.match(runnerSource,
    /source_request_scope: firstSssVsSetupCf\s*\? firstSssVsSetupCfV2\s*\?/,
    'the v2 proper-prefix scope must be separately labeled');
  assert.match(runnerSource,
    /exact original 86-tick\/85-draw schedule; compare C after tick84 before draw84 and F after tick85 before draw85/,
    'legacy v1 scope must retain its fixed schedule wording');
  assert.match(runnerSource, /await driver\.arm\(/,
    'arm must await the actual native export result');
  assert.match(runnerSource, /await driver\.submit\(/,
    'each action must await the actual native queue result');
  assert.match(runnerSource, /await driver\.accept\(/,
    'cursor advancement must await native approval');
  assert.doesNotMatch(runnerSource, /adapter\.flush\(/,
    'native actions cannot be staged and acknowledged later');
  const adapterStart = runnerSource.indexOf('function matchedSetupCfNativeAdapter(');
  const adapterEnd = runnerSource.indexOf('async function runMatchedSetupCfContinuation', adapterStart);
  const adapterSource = runnerSource.slice(adapterStart, adapterEnd);
  assert.match(adapterSource,
    /response = await timeout\(executeMatchedSetupCfNativeCommand\(\{\s*\.\.\.command,/,
    'the host driver adapter must await the actual native export response');
  assert.doesNotMatch(adapterSource, /return 1;/,
    'the host driver adapter cannot invent native success');
  const serverStart = runnerSource.indexOf('const port = await allocateLoopbackPort()');
  const packetGate = runnerSource.indexOf('matchedSetupCfInputs = await loadMatchedSetupCfInputs()');
  assert.ok(packetGate > 0 && serverStart > packetGate,
    'the packet identity gate must run before opening the local server');
  assert.equal(validateMatchedSetupCfRequest({enabled: undefined, setup: false,
    textTail: false, packetPath: null, assemblyPath: null}), false);
  assert.equal(validateMatchedSetupCfRequest({enabled: false, setup: true,
    textTail: false, packetPath: null, assemblyPath: null}), false);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: true, setup: true,
    textTail: false, packetPath: null, assemblyPath: '/tmp/assembly.json'}), /packet/);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: true, setup: true,
    textTail: true, packetPath: '/tmp/packet.json', assemblyPath: '/tmp/assembly.json'}), /SIS/);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: false, setup: true,
    textTail: false, packetPath: '/tmp/packet.json', assemblyPath: null}), /opt-in/);
  assert.throws(() => validateMatchedSetupCfRequest({enabled: false, explicitStep: true,
    setup: true, textTail: false, packetPath: '/tmp/packet.json',
    assemblyPath: '/tmp/assembly.json'}), /explicit stepping requires the continuation opt-in/);

  const packet = path.join(scratch, 'packet.json');
  const assembly = path.join(scratch, 'assembly.json');
  const output = path.join(scratch, 'output');
  const build = path.join(CANDIDATE_ROOT, 'build/browser-stadium-c1a-release');
  const packetBytes = Buffer.from('{"deliberately":"not admitted"}\n');
  const assemblyBytes = Buffer.from('{}\n');
  await fs.writeFile(packet, packetBytes);
  await fs.writeFile(assembly, assemblyBytes);
  const helperBytes = await fs.readFile(HELPER);
  const packetId = identity(packet, packetBytes);
  const assemblyId = identity(assembly, assemblyBytes);
  const helperId = identity(HELPER, helperBytes);
  const frozen = {scope: SCOPE, packet: packetId, assembly_manifest: assemblyId,
    helper: helperId, diagnostic_exports: EXPORTS};
  const preflight = {route_mode: 'first-sss-vs-setup', matched_setup_cf: frozen};
  assert.equal(validateMatchedSetupCfPreflight(preflight, {enabled: true,
    packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), frozen);
  const v2Frozen = {...frozen, scope: PREFIX_V2_SCOPE, diagnostic_exports: PREFIX_V2_EXPORTS};
  assert.equal(validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: v2Frozen}, {enabled: true, packetPath: packet,
    assemblyPath: assembly, helperPath: HELPER}), v2Frozen);
  const explicitFrozen = {...v2Frozen,
    diagnostic_exports: PREFIX_V2_EXPLICIT_STEP_EXPORTS,
    asyncify_idle_predicate: ASYNCIFY_IDLE_PREDICATE};
  assert.equal(validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: explicitFrozen}, {enabled: true, explicitStep: true,
    packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), explicitFrozen);
  const missingRetirementExport = {...explicitFrozen,
    diagnostic_exports: {...PREFIX_V2_EXPLICIT_STEP_EXPORTS}};
  delete missingRetirementExport.diagnostic_exports.retirement_status;
  assert.throws(() => validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: missingRetirementExport}, {enabled: true, explicitStep: true,
    packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), /unexpected shape/);
  const extraRetirementExport = {...explicitFrozen,
    diagnostic_exports: {...PREFIX_V2_EXPLICIT_STEP_EXPORTS, unexpected: '_extra_export'}};
  assert.throws(() => validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: extraRetirementExport}, {enabled: true, explicitStep: true,
    packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), /unexpected shape/);
  assert.throws(() => validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: {...explicitFrozen, asyncify_idle_predicate: 'Module.Asyncify'}},
  {enabled: true, explicitStep: true, packetPath: packet,
    assemblyPath: assembly, helperPath: HELPER}), /Asyncify idle predicate differs/);
  assert.throws(() => validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: {...v2Frozen, diagnostic_exports: PREFIX_V2_EXPLICIT_STEP_EXPORTS}},
  {enabled: true, explicitStep: true, packetPath: packet,
    assemblyPath: assembly, helperPath: HELPER}), /unexpected shape/);
  assert.throws(() => validateMatchedSetupCfPreflight({route_mode: 'first-sss-vs-setup',
    matched_setup_cf: {...explicitFrozen, scope: SCOPE}},
  {enabled: true, explicitStep: true, packetPath: packet,
    assemblyPath: assembly, helperPath: HELPER}), /requires the reviewed v2 proper-prefix scope/);
  assert.throws(() => validateMatchedSetupCfPreflight({...preflight,
    matched_setup_cf: {...frozen, packet: {...packetId, path: `${packet}.other`}}},
  {enabled: true, packetPath: packet, assemblyPath: assembly, helperPath: HELPER}), /packet path/);
  assert.throws(() => validateMatchedSetupCfIdentities(frozen, {
    packet: {...packetId, sha256: '0'.repeat(64)}, assembly_manifest: assemblyId,
    helper: helperId}), /packet differs/);

  assert.equal(matchedSetupCfRuntimeFailure({native: {}, consoleWarnings: []}), null);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleWarnings: ['Ignoring nested GX_CMD_CALL_DL']}), /Missing display-list provenance/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleMessages: [{type: 'log', text: 'Ignoring nested GX_CMD_CALL_DL'}]}),
  /Missing display-list provenance/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleWarnings: ['WebGPU uncaptured error: invalid command buffer']}), /GPU reported an error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleMessageCaptureOverflow: true}), /capture overflowed/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    pageErrors: ['uncaught exception']}), /Browser page error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {error: 'runtime failed'}}),
    /Native runtime error/);
  assert.match(matchedSetupCfRuntimeFailure({native: {},
    consoleErrors: ['GPU validation failed']}), /Browser console error/);

  const head = spawnSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'});
  assert.equal(head.status, 0, head.stderr);
  const runnerBytes = await fs.readFile(RUNNER);
  const preflightFile = path.join(scratch, 'preflight.json');
  const preflightBytes = {
    schema: 'melee-web-stadium-c1a-browser-preflight-v1',
    route_mode: 'first-sss-vs-setup', source_revision: head.stdout.trim(),
    build: {path: build},
    disc: {path: '/unused/original.iso', sha256: 'b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c'},
    browser: {playwright_package_path: '/unused/playwright'},
    output: {path: output}, test_script: {path: RUNNER, sha256: hash(runnerBytes)},
    first_sss_vs_setup: {text_tail_probe: false},
    matched_setup_cf: {...frozen, packet: {...packetId, sha256: '0'.repeat(64)}},
  };
  await fs.writeFile(preflightFile, JSON.stringify(preflightBytes));
  const child = spawnSync(process.execPath, [RUNNER,
    '--build', build,
    '--disc', '/unused/original.iso', '--disc-sha256', preflightBytes.disc.sha256,
    '--out', output, '--playwright', '/unused/playwright', '--preflight', preflightFile,
    '--source-revision', head.stdout.trim(), '--first-sss-vs-setup',
    '--first-sss-pair-expected', path.join(scratch, 'unused-pair.json'),
    '--first-sss-tick-expected', path.join(scratch, 'unused-tick.json'),
    '--first-sss-tick-input', path.join(scratch, 'unused-tick-input.json'),
    '--first-sss-draw-expected', path.join(scratch, 'unused-draw.json'),
    '--first-sss-prefix-expected', path.join(scratch, 'unused-prefix.json'),
    '--first-sss-prefix-input', path.join(scratch, 'unused-prefix-input.json'),
    '--first-sss-vs-setup-expected', path.join(scratch, 'unused-setup.json'),
    '--first-sss-vs-entry-expected', path.join(scratch, 'unused-entry.json'),
    '--first-sss-vs-setup-cf', '--first-sss-vs-setup-cf-packet', packet,
    '--first-sss-vs-setup-cf-assembly', assembly], {cwd: ROOT, encoding: 'utf8'});
  assert.notEqual(child.status, 0, 'a mismatched packet hash must refuse the run');
  let reportBytes;
  try { reportBytes = await fs.readFile(path.join(output, 'report.json'), 'utf8'); }
  catch (error) {
    assert.fail(`runner exited before writing its owned report: ${error}; stdout=${child.stdout}; stderr=${child.stderr}`);
  }
  const report = JSON.parse(reportBytes);
  assert.match(report.error, /packet differs from the frozen preflight identity/);
  await assert.rejects(fs.access(path.join(output, 'server.stdout.log')));
  await assert.rejects(fs.access(path.join(output, 'server.stderr.log')));
  assert.equal(report.cleanup.server_process_pid ?? null, null);
  assert.equal(report.cleanup.browser_process_pid ?? null, null);
});


test('native access stays closed across timeout, incomplete Asyncify unwind, and unsafe rejection', async () => {
  let resolveStep;
  const stepPromise = new Promise(resolve => { resolveStep = resolve; });
  const pending = trackMatchedSetupCfExplicitStep(stepPromise, state => {
    state.asyncifyIdle = state.value?.asyncify_idle === true;
  });
  pending.requiresAsyncifyIdle = true;
  let nativeReads = 0;
  const guardedNativeRead = () => {
    if (!matchedSetupCfNativeAccessAllowed(pending)) return false;
    ++nativeReads;
    return true;
  };
  assert.equal(guardedNativeRead(), false, 'unsettled ccall must block native access');
  await assert.rejects(Promise.race([pending.promise, new Promise((_, reject) =>
    setTimeout(() => reject(Error('bounded wait elapsed')), 5))]), /bounded wait elapsed/);
  assert.equal(pending.settled, false, 'host timeout must not fake native-call settlement');
  assert.equal(guardedNativeRead(), false);
  assert.equal(nativeReads, 0);

  resolveStep({result: 1, asyncify_idle: false});
  await pending.promise;
  assert.equal(pending.settled, true);
  assert.equal(pending.asyncifyIdle, false,
    'Asyncify Normal with retained currData/ABORT state remains unsafe');
  assert.equal(guardedNativeRead(), false);
  assert.equal(nativeReads, 0);

  const settledNativeRejection = trackMatchedSetupCfExplicitStep(
    Promise.resolve({result: 0, rejected: true, asyncify_idle: true}), state => {
      state.asyncifyIdle = state.value?.asyncify_idle === true;
    });
  settledNativeRejection.requiresAsyncifyIdle = true;
  await settledNativeRejection.promise;
  assert.equal(matchedSetupCfNativeAccessAllowed(settledNativeRejection), true,
    'a settled native refusal may be inspected only after the runtime predicate proves idle');

  const transportRejected = trackMatchedSetupCfExplicitStep(
    Promise.reject(Error('page transport rejected')), state => {
      state.asyncifyIdle = state.value?.asyncify_idle === true;
    });
  transportRejected.requiresAsyncifyIdle = true;
  await assert.rejects(transportRejected.promise, /page transport rejected/);
  assert.equal(transportRejected.settled, true);
  assert.equal(transportRejected.asyncifyIdle, false);
  assert.equal(matchedSetupCfNativeAccessAllowed(transportRejected), false,
    'a rejected transport without a safe runtime receipt cannot authorize native cleanup');
});

test('pending native command dispatch is refused before page.evaluate starts', () => {
  let evaluateDispatches = 0;
  const pending = {settled: false, requiresAsyncifyIdle: true, asyncifyIdle: false};
  assert.throws(() => dispatchMatchedSetupCfNativeEvaluation({
    command: {requireAsyncifyIdle: true}, pendingStep: pending,
    dispatch: () => { ++evaluateDispatches; return Promise.resolve(); },
  }), /unsettled or unsafe/);
  assert.equal(evaluateDispatches, 0,
    'a second page.evaluate must not be dispatched while the prior native call is pending');

  const safe = {settled: true, requiresAsyncifyIdle: true, asyncifyIdle: true};
  const result = dispatchMatchedSetupCfNativeEvaluation({
    command: {requireAsyncifyIdle: true}, pendingStep: safe,
    dispatch: () => { ++evaluateDispatches; return 'started'; },
  });
  assert.equal(result, 'started');
  assert.equal(evaluateDispatches, 1);
});

test('cleanup idle guard suppresses native getters and unload on false, missing, or timed-out predicate', async () => {
  for (const [label, checkNativeIdle] of [
    ['false', async () => false],
    ['missing', undefined],
    ['timed-out', async () => { throw Error('bounded idle check timed out'); }],
  ]) {
    let nativeGetters = 0, unloads = 0, pageCloses = 0;
    const gate = await guardMatchedSetupCfNativeCleanup({
      explicitStep: true,
      pendingStep: null,
      checkNativeIdle,
      onNativeAccessAllowed: async () => { ++nativeGetters; ++unloads; },
      onNativeAccessBlocked: async () => { ++pageCloses; },
    });
    assert.equal(gate.allowed, false, `${label} idle result must fail closed`);
    assert.equal(nativeGetters, 0, `${label} idle result must suppress native getters`);
    assert.equal(unloads, 0, `${label} idle result must suppress unload`);
    assert.equal(pageCloses, 1, `${label} idle result must close the owned page`);
  }
  const safe = await guardMatchedSetupCfNativeCleanup({
    explicitStep: true,
    pendingStep: null,
    checkNativeIdle: async () => true,
  });
  assert.equal(safe.allowed, true, 'positive JS-only idle proof permits existing cleanup');
});


// Execute the actual runner adapter rather than a hand-written copy of its arm
// state transitions. Transport and deadline are controlled; no browser starts.
async function actualRunnerArmControl(transport) {
  const source = await fs.readFile(RUNNER, 'utf8');
  const start = source.indexOf('function matchedSetupCfNativeAdapter(');
  const end = source.indexOf('async function runMatchedSetupCfContinuation(', start);
  assert(start >= 0 && end > start);
  const dispatches = [];
  const context = vm.createContext({
    report: {cleanup: {}},
    matchedSetupCfExplicitArmed: false,
    matchedSetupCfExplicitNativeAccessClosed: false,
    matchedSetupCfExplicitAsyncifyUnsafe: false,
    matchedSetupCfTiming: null,
    remaining: () => 20,
    timeout: async (operation, milliseconds, label) => {
      let timer;
      try {
        return await Promise.race([operation, new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error(`${label}: control deadline`)), milliseconds);
        })]);
      } finally { clearTimeout(timer); }
    },
    executeMatchedSetupCfNativeCommand: async command => {
      dispatches.push(command.kind);
      if (command.kind === 'arm-explicit-v2')
        assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, true,
          'ordinary native access must close before the arm transport is invoked');
      return transport(command);
    },
  });
  vm.runInContext(source.slice(start, end), context);
  context.adapter = context.matchedSetupCfNativeAdapter({
    mode: 'run2-proper-prefix-v2', explicitStep: true, manifestBytes: new Uint8Array([1]),
  });
  return {context, dispatches};
}


async function actualFirstCssCleanupControl({explicit = true, idle = true, evaluateFailure = null} = {}) {
  const source = await fs.readFile(RUNNER, 'utf8');
  const helperStart = source.indexOf('async function retireMatchedSetupCfExplicitNative(');
  const helperEnd = source.indexOf('\nasync function enableFirstSssTextTailProbe(', helperStart);
  const routeStart = source.indexOf('async function runFirstCssBrowserDraw(');
  const routeEnd = source.indexOf('\nasync function runCssSssTransition(', routeStart);
  assert(helperStart >= 0 && helperEnd > helperStart);
  assert(routeStart >= 0 && routeEnd > routeStart);
  const route = source.slice(routeStart, routeEnd);
  assert.match(route, /await unloadFirstCssDiagnosticOwner\(\);/,
    'the actual First-CSS success route must call the owner-aware unload helper');
  assert.match(route, /await readFirstCssDiagnosticPostUnloadSnapshot\(\)/,
    'the actual route must use the guarded post-unload snapshot');
  assert.match(route, /assertFirstCssDiagnosticMatchedSetupCfPostUnload\(afterUnload\)/,
    'the actual route must execute the extracted retirement assertion seam');
  const calls = [], nativeGetters = [];
  const context = vm.createContext({
    assert,
    report: {cleanup: {}, scenario: {matched_setup_cf: {schedule_actions: 174}}},
    firstSssVsSetupCf: true,
    evaluateFailure,
    matchedSetupCfExplicitArmed: explicit,
    matchedSetupCfExplicitRetirementAttempted: false,
    matchedSetupCfExplicitNativeAccessClosed: false,
    matchedSetupCfExplicitAsyncifyUnsafe: false,
    unloadAttempted: false,
    LIMITS: {unloadMs: 1000, cleanupMs: 1000},
    remaining: () => 1000,
    cleanupRemaining: () => 1000,
    timeout: async (promise, _ms, label) => {
      if (label === 'First-CSS post-unload snapshot' && evaluateFailure === 'timeout') {
        promise.catch(() => {});
        throw Error('controlled page-evaluate timeout');
      }
      return await promise;
    },
    page: {
      waitForTimeout: async () => { calls.push('wait'); },
      evaluate: async (fn, argument) => {
        calls.push('post-unload-evaluate');
        if (evaluateFailure === 'reject') throw Error('controlled page-evaluate rejection');
        if (evaluateFailure === 'timeout') return new Promise(() => {});
        return fn(argument);
      },
    },
    driver: {unload: async () => { calls.push('ordinary-runtime-unload'); return true; }},
    assertMatchedSetupCfAsyncifyIdle: async () => { calls.push('ordinary-idle-check'); return true; },
    executeMatchedSetupCfNativeCommand: async ({kind}) => {
      calls.push(kind);
      if (kind === 'retire-ready') return {ready: 1, asyncify_idle: true};
      if (kind === 'retire-explicit') return {result: 1, asyncify_idle: true,
        phase: 0, running: 0, retirement: {native_retired: true, terminal: true,
          error: null, phase_after_close: 0, running_after_close: 0,
          pipeline_cache_status_before_shutdown: 0, pipeline_cache_status_after_shutdown: 0,
          matched_setup_cf: {complete: true, failed: false, cursor: 174}}};
      throw Error(`Unexpected native command ${kind}`);
    },
    persistReport: async () => { calls.push('persist-retirement'); },
  });
  Object.assign(context, {
    Module: {
      stadiumMatchedSetupNativeIdle: () => { calls.push('asyncify-idle-predicate'); return idle; },
      _melee_web_native_menu_stadium_first_css_draw_observe: () => {
        nativeGetters.push('observer'); return 0;
      },
      _melee_web_native_menu_phase: () => { nativeGetters.push('phase'); return 0; },
      _melee_web_native_menu_running: () => { nativeGetters.push('running'); return 0; },
      UTF8ToString: () => { nativeGetters.push('utf8'); return null; },
    },
    document: {querySelector: selector => selector === '#status'
      ? {dataset: {runtimeError: null}} : {disabled: false}},
  });
  vm.runInContext(source.slice(helperStart, helperEnd), context);
  return {context, calls, nativeGetters};
}

test('actual First-CSS explicit success path retires and retains the archived receipt separately', async () => {
  const {context, calls, nativeGetters} = await actualFirstCssCleanupControl({explicit: true, idle: true});
  await context.unloadFirstCssDiagnosticOwner();
  const afterUnload = await context.readFirstCssDiagnosticPostUnloadSnapshot();
  context.assertFirstCssDiagnosticMatchedSetupCfPostUnload(afterUnload);
  assert.equal(calls.includes('ordinary-runtime-unload'), false,
    'explicit stopped-loop ownership must bypass queued runtime unload');
  assert.deepEqual(calls.filter(call => call.startsWith('retire-')), ['retire-ready', 'retire-explicit']);
  assert.equal(context.report.cleanup.unload_method, 'one-shot-native-retirement');
  assert.equal(context.report.cleanup.unload_completed, true);
  assert.equal(afterUnload.observation, null);
  assert.equal(afterUnload.matched_setup_cf_retirement_receipt.complete, true);
  assert.equal(afterUnload.matched_setup_cf_retirement_receipt.cursor, 174);
  assert(nativeGetters.includes('phase') && nativeGetters.includes('running'));
});

test('actual First-CSS retirement assertion rejects missing schedule or mismatched archived cursor', async () => {
  const {context} = await actualFirstCssCleanupControl({explicit: true, idle: true});
  context.report.scenario.matched_setup_cf.schedule_actions = undefined;
  assert.throws(() => context.assertFirstCssDiagnosticMatchedSetupCfPostUnload({
    matched_setup_cf: null, matched_setup_cf_retirement_receipt: {complete: true, cursor: 174},
  }), /checked matched Setup C\/F schedule length/);
  context.report.scenario.matched_setup_cf.schedule_actions = 174;
  assert.throws(() => context.assertFirstCssDiagnosticMatchedSetupCfPostUnload({
    matched_setup_cf: null, matched_setup_cf_retirement_receipt: {complete: true, cursor: 173},
  }), /cursor differs/);
});

for (const evaluateFailure of ['reject', 'timeout']) {
  test(`actual First-CSS post-unload ${evaluateFailure} closes explicit native access`, async () => {
    const {context, nativeGetters} = await actualFirstCssCleanupControl({
      explicit: true, idle: true, evaluateFailure,
    });
    const pattern = evaluateFailure === 'reject'
      ? /controlled page-evaluate rejection/ : /controlled page-evaluate timeout/;
    await assert.rejects(context.readFirstCssDiagnosticPostUnloadSnapshot(), pattern);
    assert.equal(context.matchedSetupCfExplicitAsyncifyUnsafe, true);
    assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, true);
    assert.deepEqual(nativeGetters, []);
  });
}

test('actual First-CSS ordinary success path keeps the guarded runtime unload', async () => {
  const {context, calls} = await actualFirstCssCleanupControl({explicit: false, idle: true});
  await context.unloadFirstCssDiagnosticOwner();
  assert.deepEqual(calls, ['ordinary-idle-check', 'ordinary-runtime-unload']);
  assert.equal(context.report.cleanup.unload_completed, true);
  assert.equal(context.report.cleanup.matched_setup_cf_retirement, undefined);
});

test('actual First-CSS post-unload snapshot suppresses native getters when Asyncify is unsafe', async () => {
  const {context, calls, nativeGetters} = await actualFirstCssCleanupControl({explicit: true, idle: false});
  await assert.rejects(context.readFirstCssDiagnosticPostUnloadSnapshot(), /snapshot was suppressed/);
  assert.deepEqual(nativeGetters, []);
  assert.equal(context.matchedSetupCfExplicitAsyncifyUnsafe, true);
  assert(calls.includes('asyncify-idle-predicate'));
});

for (const mode of ['lost-response', 'never-settling', 'returned-native-refusal']) {
  test(`actual runner explicit arm ${mode} suppresses all ordinary cleanup`, async () => {
    let nativeArmApplied = 0;
    const {context, dispatches} = await actualRunnerArmControl(async () => {
      ++nativeArmApplied; // Native may have already canceled its ordinary loop.
      if (mode === 'lost-response') throw Error('page response lost after native arm');
      if (mode === 'never-settling') return new Promise(() => {});
      return {result: 0, message: 'refused after possible native mutation'};
    });
    const started = Date.now();
    await assert.rejects(context.adapter.arm(new Uint8Array([1])));
    assert(Date.now() - started < 1000, 'arm failure must remain bounded');
    assert.equal(nativeArmApplied, 1);
    assert.deepEqual(dispatches, ['arm-explicit-v2']);
    assert.equal(context.matchedSetupCfExplicitArmed, false);
    assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, true);
    let getters = 0, unloads = 0;
    const cleanup = await guardMatchedSetupCfNativeCleanup({
      explicitStep: true,
      forcedUnsafe: context.matchedSetupCfExplicitAsyncifyUnsafe ||
        context.matchedSetupCfExplicitNativeAccessClosed,
      checkNativeIdle: async () => true,
      onNativeAccessAllowed: async () => { ++getters; ++unloads; },
    });
    assert.equal(cleanup.allowed, false,
      'even positive JS idle cannot prove an unknown arm left RAF alive');
    assert.equal(getters, 0);
    assert.equal(unloads, 0);
  });
}

test('actual runner confirmed explicit arm opens only the armed retirement path', async () => {
  const {context, dispatches} = await actualRunnerArmControl(async () => ({result: 1}));
  assert.equal(await context.adapter.arm(new Uint8Array([1])), 1);
  assert.equal(context.matchedSetupCfExplicitArmed, true);
  assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, false);
  assert.equal(context.matchedSetupCfExplicitAsyncifyUnsafe, false);
  assert.equal(context.report.cleanup.matched_setup_cf_arm_outcome, 'confirmed-armed');
  assert.deepEqual(dispatches, ['arm-explicit-v2']);
});


test('actual runner step timeout remains browser-only after late transport settlement', async () => {
  let resolveStep;
  const {context, dispatches} = await actualRunnerArmControl(command =>
    command.kind === 'arm-explicit-v2' ? {result: 1}
      : new Promise(resolve => { resolveStep = resolve; }));
  await context.adapter.arm(new Uint8Array([1]));
  await assert.rejects(context.adapter.step(0), /control deadline/);
  assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, true);
  assert.equal(context.matchedSetupCfExplicitAsyncifyUnsafe, true);
  resolveStep({result: 1, asyncify_idle: true});
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(context.matchedSetupCfExplicitNativeAccessClosed, true,
    'late successful settlement must not erase the recorded transport failure');
  assert.deepEqual(dispatches, ['arm-explicit-v2', 'step-explicit']);
  let unloads = 0;
  const cleanup = await guardMatchedSetupCfNativeCleanup({
    explicitStep: true,
    forcedUnsafe: context.matchedSetupCfExplicitNativeAccessClosed,
    checkNativeIdle: async () => true,
    onNativeAccessAllowed: async () => { ++unloads; },
  });
  assert.equal(cleanup.allowed, false);
  assert.equal(unloads, 0);
});


test('actual runner finally retains unsafe arm timeout and closes owned browser without native cleanup', async () => {
  const {context} = await actualRunnerArmControl(() => new Promise(() => {}));
  await assert.rejects(context.adapter.arm(new Uint8Array([1])), /control deadline/);
  const calls = [], persisted = [];
  let closed = false;
  Object.assign(context, {
    firstSssVsSetupCfExplicitStep: true,
    pendingMatchedSetupCfExplicitStep: null,
    unloadAttempted: false,
    failure: 'primary explicit-arm transport timeout',
    report: {result: 'fail', error: 'primary explicit-arm transport timeout',
      scenario: {}, cleanup: {failures: []}},
    LIMITS: {cleanupTotalMs: 1000},
    cleanupDeadline: null,
    cleanupRemaining: () => 1000,
    guardMatchedSetupCfNativeCleanup,
    matchedSetupCfNativeAccessAllowed,
    page: {
      isClosed: () => closed,
      evaluate: async fn => {
        assert.doesNotMatch(String(fn), /_melee_web_native_menu_/,
          'unsafe cleanup may dispatch only the JS idle predicate');
        calls.push('js-idle');
        return true;
      },
      close: async () => { closed = true; calls.push('page-close'); },
    },
    driver: {
      unload: async () => { calls.push('native-unload'); throw Error('forbidden unload'); },
      dispose: () => calls.push('driver-dispose'),
    },
    nativeSnapshot: async () => { calls.push('native-getter'); throw Error('forbidden getter'); },
    context: {close: async () => calls.push('context-close')},
    browserCdp: null,
    browser: {close: async () => calls.push('browser-close')},
    browserServer: {
      close: async () => calls.push('browser-process-close'),
      process: () => ({exitCode: 0, signalCode: null}),
    },
    cleanupServer: async () => {
      calls.push('server-close'); context.report.cleanup.server_process_terminated = true;
    },
    errorText: error => String(error?.message || error),
    persistReport: async () => persisted.push(JSON.parse(JSON.stringify(context.report))),
    pageErrors: [], consoleErrors: [], consoleWarnings: [], consoleMessages: [],
    consoleMessageCaptureOverflow: false, consoleMessageDropped: 0,
    requestRows: [], responseRows: [], externalRequests: [],
  });
  const source = await fs.readFile(RUNNER, 'utf8');
  const cleanupStart = source.indexOf('async function cleanupOwnedProcesses()');
  const cleanupEnd = source.indexOf('\ntry {\n  await main();', cleanupStart);
  assert(cleanupStart >= 0 && cleanupEnd > cleanupStart);
  vm.runInContext(source.slice(cleanupStart, cleanupEnd), context);
  const finallyStart = source.indexOf('  await cleanupOwnedProcesses();', cleanupEnd);
  const finallyEnd = source.indexOf('\n}\n\nif (failure ||', finallyStart);
  assert(finallyStart >= 0 && finallyEnd > finallyStart);
  vm.runInContext(`async function actualRunnerFinally() {${source.slice(finallyStart, finallyEnd)}}`, context);
  await context.actualRunnerFinally();
  assert.equal(context.report.error, 'primary explicit-arm transport timeout');
  assert.equal(context.report.result, 'fail');
  assert.equal(context.report.execution_state, 'completed');
  assert.equal(context.report.cleanup.native_cleanup_suppressed_asyncify, true);
  assert.equal(context.report.cleanup.native_cleanup_suppressed_retirement, true);
  assert.equal(context.report.cleanup.unload_suppressed_pending_async_step, true);
  assert.equal(context.report.cleanup.context_closed, true);
  assert.equal(context.report.cleanup.browser_process_terminated, true);
  assert.equal(context.report.cleanup.server_process_terminated, true);
  assert.equal(persisted.length, 1, 'the actual finally must reach final report persistence');
  for (const required of ['page-close', 'context-close', 'browser-close', 'browser-process-close', 'server-close'])
    assert(calls.includes(required), `${required} must run after unsafe admission refusal`);
  assert.equal(calls.includes('native-unload'), false);
  assert.equal(calls.includes('native-getter'), false);
});
