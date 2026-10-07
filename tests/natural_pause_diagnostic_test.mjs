import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {installPauseTraceCapture, readPauseTraceStatus} from './pause_trace_capture.mjs';
import {NATURAL_PAUSE_PROTOCOL, STOPPED_SCENE_PAIR_PROTOCOL, resolveCaptureMode, validateNaturalPauseManifest,
  validateStoppedScenePairManifest, firstNaturalPauseIncident, firstStoppedScenePairStop,
  validateStoppedScenePairBoundary, summarizeStoppedSourceInterval,
  naturalPauseRuntimeUrl, validateNaturalPauseBrowserIdentity, validateDefaultTwoRingStatus, firstNaturalPauseStop,
  stopSourceBeforeDiagnosticExport, readNaturalPauseBrowserCommandLine} from '../scripts/natural_pause_diagnostic.mjs';

const sha = 'a'.repeat(64);
const manifest = () => ({
  schema: 'melee-web-natural-pause-diagnostic-manifest-v1',
  runtime_url: 'http://127.0.0.1:8795/runtime.html',
  source: {commit: '1'.repeat(40), tree: '2'.repeat(40)},
  build: {configuration: 'Release', target: 'runtime', directory: '/tmp/build',
    artifacts: {'runtime.js': {bytes: 12, sha256: sha}}},
  browser: {executable_path: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    version: '140.0.7339.80', profile_path: '/tmp/fresh-chrome-profile'},
  inputs: {disc: {path: '/tmp/disc.ciso', bytes: 123, sha256: sha},
    recipe: {path: '/tmp/recipe.mwrc', bytes: 456, sha256: sha,
      header: {version: 8, seed: 3980218793, frames: 42127}}},
  protocol: {...NATURAL_PAUSE_PROTOCOL},
});
const scenePairManifest = () => ({...manifest(),
  schema: 'melee-web-stopped-scene-pair-diagnostic-manifest-v1',
  build: {...manifest().build,
    producer_source: {commit: 'b'.repeat(40), tree: 'c'.repeat(40)}},
  protocol: {...STOPPED_SCENE_PAIR_PROTOCOL}});

assert.deepEqual(resolveCaptureMode(), {mode: 'state', diagnostic: false});
assert.deepEqual(resolveCaptureMode('performance', '/private/plan.json'),
  {mode: 'performance', diagnostic: true});
assert.throws(() => resolveCaptureMode('performance'), /requires --diagnostic-manifest/);
assert.throws(() => resolveCaptureMode('state', '/private/plan.json'), /restricted/);
assert.throws(() => resolveCaptureMode('hitch'), /must be state or performance/);

assert.equal(validateNaturalPauseManifest(manifest(), ['runtime.js']).schema,
  'melee-web-natural-pause-diagnostic-manifest-v1');
assert.equal(validateStoppedScenePairManifest(scenePairManifest(), ['runtime.js']).schema,
  'melee-web-stopped-scene-pair-diagnostic-manifest-v1');
assert.equal(NATURAL_PAUSE_PROTOCOL.replay_phase_timeout_ms, 65000);
assert.equal(NATURAL_PAUSE_PROTOCOL.overall_timeout_ms, 95000);
assert.equal(NATURAL_PAUSE_PROTOCOL.observation_timeout_ms, 5000);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.source_cursor_target, 1600);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.source_cursor_limit, 1800);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.replay_timeout_ms, 35000);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.replay_phase_timeout_ms, 40000);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.expected_frame_slots, 2);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.auto_resume, false);
assert.equal(STOPPED_SCENE_PAIR_PROTOCOL.require_zero_source_steps_and_draws_between_images, true);
for (const mutate of [
  value => {
    value.protocol.process_timeout_ms = value.protocol.replay_phase_timeout_ms;
    delete value.protocol.replay_phase_timeout_ms;
  },
  value => {value.protocol.replay_timeout_ms = 90000;},
  value => {value.protocol.injected_stall = true;},
  value => {value.build.configuration = 'Debug';},
  value => {value.inputs.recipe.header.version = 10;},
  value => {value.build.artifacts['other.js'] = {bytes: 12, sha256: sha};},
]) {
  const value = manifest(); mutate(value);
  assert.throws(() => validateNaturalPauseManifest(value, ['runtime.js']));
}
for (const mutate of [
  value => {value.protocol.source_cursor_target = 1599;},
  value => {value.protocol.auto_resume = true;},
  value => {value.protocol.expected_frame_slots = 4;},
  value => {value.build.configuration = 'Debug';},
  value => {delete value.build.producer_source;},
]) {
  const value = scenePairManifest(); mutate(value);
  assert.throws(() => validateStoppedScenePairManifest(value, ['runtime.js']));
}

const configuredUrl = naturalPauseRuntimeUrl('http://127.0.0.1:8795/runtime.html');
assert.equal(configuredUrl.searchParams.get('hitch-capture'), '1');
assert.equal(configuredUrl.searchParams.get('hitch-marks'), '1');
assert.equal(configuredUrl.searchParams.get('melee-web-staging-diagnostics'), '1');
assert.equal(configuredUrl.searchParams.has('melee-web-staging-slots'), false);
assert.equal(configuredUrl.searchParams.has('melee-web-staging-byte-hash'), false);
assert.throws(() => naturalPauseRuntimeUrl('http://127.0.0.1:8795/runtime.html?melee-web-staging-slots=4'), /forbids/);
assert.throws(() => naturalPauseRuntimeUrl('http://127.0.0.1:8795/runtime.html?melee-web-staging-byte-hash=1'), /forbids/);
assert.throws(() => naturalPauseRuntimeUrl('http://127.0.0.1:8795/runtime.html?hitch-ui-paint=hidden'), /forbids/);
assert.throws(() => naturalPauseRuntimeUrl('https://example.com/runtime.html'), /loopback/);
const expectedBrowser = manifest().browser;
assert.equal(validateNaturalPauseBrowserIdentity(expectedBrowser, {...expectedBrowser,
  profile_existed_before_launch: false}).valid, true);
assert.equal(validateNaturalPauseBrowserIdentity(expectedBrowser, {...expectedBrowser,
  profile_existed_before_launch: true}).valid, false, 'a reused profile fails preflight');
assert.equal(validateNaturalPauseBrowserIdentity(expectedBrowser, {...expectedBrowser,
  version: '141.0.1.2', profile_existed_before_launch: false}).valid, false,
  'a changed installed Chrome version fails preflight');

const defaultRing = {frame_slots: 2, staging_buffers: 2, selection: {
  requested_frame_slots: null, selected_frame_slots: 2, reason: 'default_two',
  byte_hash_requested: false, byte_hash_enabled: false,
  staging_diagnostics_requested: true, staging_diagnostics_enabled: true,
  staging_diagnostics_reason: 'explicit_loopback_opt_in'}};
assert.equal(validateDefaultTwoRingStatus(defaultRing).valid, true);
assert.equal(validateDefaultTwoRingStatus({...defaultRing, frame_slots: 4}).valid, false);
assert.equal(validateDefaultTwoRingStatus({...defaultRing, selection: {
  ...defaultRing.selection, staging_diagnostics_enabled: false}}).valid, false);

const ordinaryPreparation = {source_running: 1, source_cursor: 80,
  incidents: [{reason: 7, at_ms: 10, value: 0, threshold: 1}]};
assert.equal(firstNaturalPauseStop(ordinaryPreparation, 100), null,
  'reason 7 is ordinary render-preparation settling, not a terminal error');
assert.equal(firstNaturalPauseStop({source_running: 0, status_text: 'Paused after a timing disruption. Resume to continue.',
  incidents: [{reason: 7, at_ms: 10, value: 0, threshold: 1}]}, 100).outcome, 'timing_pause',
  'status text identifies a stopped timing pause when the numeric incident is unavailable');
const followedByDebt = {...ordinaryPreparation, source_running: 0, incidents: [
  ...ordinaryPreparation.incidents, {reason: 1, at_ms: 20, value: 9, threshold: 8}]};
assert.equal(firstNaturalPauseStop(followedByDebt, 200).outcome, 'timing_pause');
assert.equal(firstNaturalPauseStop({source_running: 1, incidents: [
  {reason: 4, at_ms: 5, value: 0, threshold: 0}]}, 10).outcome, 'runtime_error',
  'incident-only runtime_failure remains an error and is never called a timing pause');
assert.equal(firstNaturalPauseStop({source_running: 0, incidents: [
  {reason: 1, at_ms: 5, value: 9, threshold: 8},
  {reason: 4, at_ms: 10, value: 0, threshold: 0}]}, 20).outcome, 'timing_pause',
  'a later runtime failure cannot replace the first terminal timing pause');
assert.equal(firstNaturalPauseStop({source_running: 0, incidents: [
  {reason: 7, at_ms: 1, value: 0, threshold: 1},
  {reason: 4, at_ms: 5, value: 0, threshold: 0},
  {reason: 1, at_ms: 10, value: 9, threshold: 8}]}, 20).outcome, 'runtime_error',
  'an earlier runtime failure remains first when a timing incident follows');
assert.equal(firstNaturalPauseStop({source_running: 1, source_cursor: 3000}, 1).outcome, 'cursor_limit');
assert.equal(firstNaturalPauseStop({source_running: 1, source_cursor: 2999}, 60000).outcome, 'replay_timeout');
assert.equal(firstNaturalPauseStop({source_running: 1, source_cursor: 3000, runtime_error: 'fatal',
  incidents: [{reason: 1, at_ms: 5, value: 9, threshold: 8}]}, 1).outcome,
  'runtime_error', 'a runtime failure remains terminal while source is still running');
assert.equal(firstNaturalPauseIncident({source_running: 1, source_cursor: 3000,
  incidents: [{reason: 7, value: 0, threshold: 0}]}), null,
  'ordinary preparation settling does not end the short visual prefix');
const pairStartupState = {started: false};
const preparedPair = {source_phase: 0, source_running: 0, source_cursor: 0,
  latest_callback: {row: 27, source_steps: 0, source_draws: 0,
    sample_source_frame: -1, sample_replay_cursor: 0}};
assert.equal(firstStoppedScenePairStop(preparedPair, 30, pairStartupState), null,
  'async recipe/audio preparation has not yet launched source simulation');
assert.equal(pairStartupState.started, false);
assert.equal(firstStoppedScenePairStop({...preparedPair, incidents: [{reason: 7}]}, 30, pairStartupState), null,
  'preparation settling retains its existing nonterminal classification');
assert.equal(firstStoppedScenePairStop({...preparedPair, dialog_error: 'audio preparation failed'}, 30,
  pairStartupState).outcome, 'runtime_error');
assert.equal(firstStoppedScenePairStop({...preparedPair, latest_callback: null}, 100, pairStartupState), null);
assert.equal(firstStoppedScenePairStop(preparedPair, 35000, pairStartupState).outcome, 'pair_replay_timeout',
  'waiting for launch consumes the original replay deadline');
assert.equal(firstStoppedScenePairStop({...preparedPair, runtime_error: 'preparation failed'}, 30,
  pairStartupState).outcome, 'runtime_error');
assert.equal(firstStoppedScenePairStop({...preparedPair, incidents: [{reason: 4}]}, 30,
  pairStartupState).outcome, 'runtime_error');
assert.equal(firstStoppedScenePairStop({...preparedPair, incidents: [{reason: 1}]}, 30,
  pairStartupState).outcome, 'timing_pause');
assert.equal(firstStoppedScenePairStop({...preparedPair, source_running: 1}, 250, pairStartupState), null);
assert.equal(pairStartupState.started, true, 'native running state is sufficient even before the first tick');
assert.equal(firstStoppedScenePairStop(preparedPair, 500, pairStartupState).outcome,
  'source_stopped_before_screenshot_target', 'a later zero-cursor stop cannot look like startup');
assert.equal(firstStoppedScenePairStop({...preparedPair, source_cursor: 1}, 250, {started: false}).outcome,
  'source_stopped_before_screenshot_target', 'a first observation after progress still detects an unexplained stop');
const progressedPairState = {started: false};
assert.equal(firstStoppedScenePairStop({...preparedPair, source_running: 1, source_cursor: 1,
  latest_callback: {...preparedPair.latest_callback, source_steps: 1, sample_replay_cursor: 1}},
  250, progressedPairState), null);
assert.equal(progressedPairState.started, true);
assert.equal(firstStoppedScenePairStop({...preparedPair, source_phase: 1}, 30, {started: false}).outcome,
  'source_stopped_before_screenshot_target', 'an unexplained stopped phase is not ignored');
const transitioningPairState = {started: true};
const preparingMatch = {source_phase: 7, source_running: 0, source_cursor: 1458,
  preparation: {active: true, source: 'menuPreparation', error: null}};
assert.equal(firstStoppedScenePairStop(preparingMatch, 24819, transitioningPairState), null,
  'source-owned inter-scene preparation is not an unexplained source stop');
assert.equal(firstStoppedScenePairStop({...preparingMatch, source_running: 1,
  preparation: {active: false}, source_cursor: 1500}, 25000, transitioningPairState), null,
  'preparation can arm the live source again within the fixed deadline');
assert.equal(firstStoppedScenePairStop({...preparingMatch, preparation: {active: false}}, 25000,
  transitioningPairState).outcome, 'source_stopped_before_screenshot_target');
assert.equal(firstStoppedScenePairStop({...preparingMatch, runtime_error: 'construction failed'}, 25000,
  transitioningPairState).outcome, 'runtime_error');
assert.equal(firstStoppedScenePairStop({...preparingMatch, incidents: [{reason: 1}]}, 25000,
  transitioningPairState).outcome, 'timing_pause', 'an actual incident still wins during preparation');
assert.equal(firstStoppedScenePairStop({...preparingMatch,
  preparation: {active: false, error: 'source preparation failed'}}, 25000,
  transitioningPairState).outcome, 'runtime_error');
assert.equal(firstStoppedScenePairStop(preparingMatch, 35000, transitioningPairState).outcome,
  'pair_replay_timeout', 'preparation cannot widen the fixed replay deadline');
assert.equal(firstStoppedScenePairStop({...preparingMatch, source_cursor: 1600, source_running: 1,
  latest_callback: {sample_replay_cursor: 1600, sample_source_frame: 1}}, 25000,
  transitioningPairState), null, 'an active preparation owner cannot qualify an image target');

// Exercise the actual browser observer without launching Chrome: evaluate has
// no Node closures and chains every original source hook with its receiver.
const previousWindow = globalThis.window, previousDocument = globalThis.document;
const chainedPreparationHooks = [];
const observerWindow = {};
for (const name of ['menuRuntimeTiming', 'menuDiagnosticSample', 'menuDiagnosticIncident',
  'menuPreparation', 'menuPreparationDone', 'menuPreparationCanceled',
  'menuPreparationFailed', 'menuPreparationProfile']) {
  observerWindow[name] = function(...args) {
    assert.equal(this, observerWindow);
    chainedPreparationHooks.push({name, args});
    return name;
  };
}
try {
  globalThis.window = observerWindow;
  globalThis.document = {querySelector: () => null};
  const fakeObserverPage = {evaluate: async (fn, args) => vm.runInNewContext(
    `(${fn.toString()})(argument)`, {window: observerWindow, document: globalThis.document,
      performance, argument: structuredClone(args)})};
  const installation = await installPauseTraceCapture(fakeObserverPage);
  assert.equal(installation.preparation_hooks_present, true);
  const preparationStatus = async () => (await readPauseTraceStatus(fakeObserverPage,
    {readNative: false})).preparation;
  assert.equal(observerWindow.menuPreparation('next scene', false), 'menuPreparation');
  assert.equal((await preparationStatus()).active, true);
  assert.equal(observerWindow.menuPreparationDone(), 'menuPreparationDone');
  assert.equal((await preparationStatus()).active, false);
  assert.equal(observerWindow.menuDiagnosticIncident(7), 'menuDiagnosticIncident');
  assert.equal((await preparationStatus()).active, true, 'reason7 covers render-only preparation');
  assert.equal(observerWindow.menuPreparationProfile({gpu_completion_ready: true}), 'menuPreparationProfile');
  assert.equal((await preparationStatus()).active, false, 'native arm profile closes render-only preparation');
  observerWindow.menuPreparation('next scene');
  observerWindow.menuPreparationCanceled();
  assert.equal((await preparationStatus()).active, false);
  observerWindow.menuPreparation('next scene');
  observerWindow.menuPreparationFailed('construction failed');
  assert.equal((await preparationStatus()).error, 'construction failed');
  const beforePauseEvents = observerWindow.__meleePauseTrace.state.manual_pause_events;
  observerWindow.menuDiagnosticIncident(5);
  assert.equal(observerWindow.__meleePauseTrace.state.manual_pause_events, beforePauseEvents + 1);
  assert.deepEqual(chainedPreparationHooks.map(row => row.name), ['menuPreparation', 'menuPreparationDone',
    'menuDiagnosticIncident', 'menuPreparationProfile', 'menuPreparation', 'menuPreparationCanceled',
    'menuPreparation', 'menuPreparationFailed', 'menuDiagnosticIncident']);
  assert.deepEqual(chainedPreparationHooks[0].args, ['next scene', false]);
  observerWindow.menuPreparationProfile({gpu_completion_ready: false});
  assert.equal((await preparationStatus()).active, true);
  assert.match((await preparationStatus()).error, /lacks GPU completion/,
    'an incomplete profile cannot release preparation ownership');
} finally {
  if (previousWindow === undefined) delete globalThis.window; else globalThis.window = previousWindow;
  if (previousDocument === undefined) delete globalThis.document; else globalThis.document = previousDocument;
}
assert.equal(firstStoppedScenePairStop({source_running: 1, source_cursor: 1599,
  latest_callback: {sample_source_frame: 100, sample_replay_cursor: 1599}}, 5000), null,
  'a positive match frame before the fixed cursor target is insufficient');
const pairTrigger = firstStoppedScenePairStop({source_running: 1, source_cursor: 1601,
  latest_callback: {sample_source_frame: 1, sample_replay_cursor: 1600, row: 55}}, 5000);
assert.equal(pairTrigger.outcome, 'paired_screenshot_target',
  'the short target stops after the first observed positive match source frame');
assert.equal(pairTrigger.trigger_cursor, 1601,
  'the cursor seen when the pair target fires remains a separate trigger boundary');
assert.equal(Object.hasOwn(pairTrigger, 'source_cursor'), false,
  'the trigger cursor is not mistaken for the later stopped cursor');
const stoppedPairBoundary = validateStoppedScenePairBoundary(pairTrigger, {source_running: 0,
  replay_cursor: 1603, latest_callback: {sample_source_frame: 2, sample_replay_cursor: 1602, row: 57}});
assert.deepEqual(stoppedPairBoundary, {valid: true, problems: [], trigger_cursor: 1601, stopped_cursor: 1603,
  source_running: 0,
  latest_callback: {sample_source_frame: 2, sample_replay_cursor: 1602, row: 57}},
  'the actual stopped cursor may advance during the pause round trip but is pinned independently');
assert.equal(validateStoppedScenePairBoundary(pairTrigger, {source_running: 0, replay_cursor: 1801,
  latest_callback: {sample_source_frame: 2, sample_replay_cursor: 1800, row: 77}}).valid, false,
  'a stop cursor that advanced beyond 1800 cannot qualify');
assert.ok(validateStoppedScenePairBoundary(pairTrigger, {source_running: 1, replay_cursor: 1603,
  latest_callback: {sample_source_frame: 2, sample_replay_cursor: 1602, row: 57}}).problems.includes('source_not_stopped'));
assert.ok(validateStoppedScenePairBoundary(pairTrigger, {source_running: 0, replay_cursor: 1603,
  latest_callback: {sample_source_frame: 0, sample_replay_cursor: 1602, row: 57}}).problems.includes('latest_callback_not_positive_match'));
assert.equal(firstStoppedScenePairStop({source_running: 1, source_cursor: 1601,
  latest_callback: {sample_source_frame: 0, sample_replay_cursor: 1600, row: 55}}, 5000), null,
  'the target does not stop on pre-match callback rows');
assert.equal(firstStoppedScenePairStop({source_running: 0, source_cursor: 1601,
  latest_callback: {sample_source_frame: 1, sample_replay_cursor: 1600, row: 55}}, 5000).outcome,
  'source_stopped_before_screenshot_target',
  'a source that stopped without a classified timing/runtime incident is not accepted as the pair target');
assert.equal(firstStoppedScenePairStop({source_running: 1, source_cursor: 1800,
  latest_callback: {sample_source_frame: 0, sample_replay_cursor: 1800, row: 77}}, 5000).outcome,
  'positive_match_frame_not_observed');
assert.equal(firstStoppedScenePairStop({source_running: 1, source_cursor: 1500}, 35000).outcome,
  'pair_replay_timeout');
assert.equal(firstStoppedScenePairStop({source_running: 0, source_cursor: 1600,
  incidents: [{reason: 1, value: 9, threshold: 8}], latest_callback: {sample_source_frame: 1,
    sample_replay_cursor: 1600}}, 5000).outcome, 'timing_pause',
  'a real source timing stop takes precedence over the screenshot target');

const sourceIntervalCapture = {rows: 3, columns: ['source_steps', 'source_draws', 'frame'],
  table: [0, 0, 1, 0, 0, 2, 1, 0, 3]};
assert.deepEqual(summarizeStoppedSourceInterval(sourceIntervalCapture, 0, 2), {
  first_row_inclusive: 0, end_row_exclusive: 2, callback_rows_observed: 2,
  source_steps: 0, source_draws: 0, all_observed_callbacks_zero_source_steps_and_draws: true,
});
assert.equal(summarizeStoppedSourceInterval(sourceIntervalCapture, 1, 3)
  .all_observed_callbacks_zero_source_steps_and_draws, false);
assert.throws(() => summarizeStoppedSourceInterval(sourceIntervalCapture, 0, 4), /complete callback table span/);
assert.throws(() => summarizeStoppedSourceInterval({rows: 2, columns: ['source_steps', 'source_draws'],
  table: [1, 0, -1, 0]}, 0, 2), /unknown source counters/,
  'negative step counters cannot cancel positive source work to manufacture a zero interval');
assert.throws(() => summarizeStoppedSourceInterval({rows: 2, columns: ['source_steps', 'source_draws'],
  table: [0, 1, 0, -1]}, 0, 2), /unknown source counters/,
  'negative draw counters cannot cancel positive source work to manufacture a zero interval');

const order = [];
let statusReads = 0;
const lifecycle = await stopSourceBeforeDiagnosticExport({
  readStatus: async () => {order.push('status'); return {source_running: ++statusReads === 1 ? 1 : 0};},
  stopPlayback: async () => {order.push('stop');},
  finalizeTrace: async () => {order.push('finalize'); return {complete: true};},
  readEvidence: async () => {order.push('export'); return {rows: 1};},
  cleanupAfterEvidence: async () => {order.push('cleanup'); return {unloaded: true};},
});
assert.deepEqual(order, ['status', 'stop', 'status', 'finalize', 'export', 'cleanup']);
assert.equal(lifecycle.stopped.source_running, 0);
assert.deepEqual(lifecycle.evidence, {rows: 1});
assert.deepEqual(lifecycle.cleanup, {unloaded: true});
const pairOrder = [];
const pairLifecycle = await stopSourceBeforeDiagnosticExport({
  readStatus: async () => {pairOrder.push('status'); return {source_running: pairOrder.length === 1 ? 1 : 0};},
  stopPlayback: async () => {pairOrder.push('stop');},
  captureImmediate: async status => {assert.equal(status.source_running, 0); pairOrder.push('immediate-image');},
  finalizeTrace: async () => {pairOrder.push('trace');},
  readEvidence: async () => {pairOrder.push('delayed-image-and-export');},
  cleanupAfterEvidence: async () => {pairOrder.push('unload');},
});
assert.deepEqual(pairOrder, ['status', 'stop', 'status', 'immediate-image', 'trace',
  'delayed-image-and-export', 'unload']);
assert.equal(pairLifecycle.immediate, undefined);
const failedImageOrder = [];
await assert.rejects(() => stopSourceBeforeDiagnosticExport({
  readStatus: async () => {failedImageOrder.push('status'); return {source_running: failedImageOrder.length === 1 ? 1 : 0};},
  stopPlayback: async () => {failedImageOrder.push('stop');},
  captureImmediate: async () => {failedImageOrder.push('immediate-image'); throw Error('image boundary failed');},
  finalizeTrace: async () => {failedImageOrder.push('trace');},
  readEvidence: async () => {failedImageOrder.push('delayed-image');},
}), /image boundary failed/);
assert.deepEqual(failedImageOrder, ['status', 'stop', 'status', 'immediate-image'],
  'an incomplete immediate image prevents later trace/export work from masking the first boundary');
const unsafeOrder = [];
await assert.rejects(() => stopSourceBeforeDiagnosticExport({
  readStatus: async () => {unsafeOrder.push('status'); return {source_running: 1};},
  stopPlayback: async () => {unsafeOrder.push('stop');},
  finalizeTrace: async () => {unsafeOrder.push('finalize');},
  readEvidence: async () => {unsafeOrder.push('export');},
}), /Refusing large diagnostic exports/);
assert.deepEqual(unsafeOrder, ['status', 'stop', 'status'],
  'trace stream and capture reads are skipped unless source_running is confirmed zero');
for (const stoppedPreparation of [true, false]) {
  const refusedPauseOrder = [];
  await assert.rejects(() => stopSourceBeforeDiagnosticExport({
    requirePauseAcknowledgement: true,
    readStatus: async () => {refusedPauseOrder.push('status'); return {source_running: 0,
      preparation: {active: stoppedPreparation}};},
    stopPlayback: async () => {refusedPauseOrder.push('pause'); return {acknowledged: false};},
    captureImmediate: async () => {refusedPauseOrder.push('image');},
    finalizeTrace: async () => {refusedPauseOrder.push('trace');},
    readEvidence: async () => {refusedPauseOrder.push('export');},
  }), /fresh native pause acknowledgement/);
  assert.deepEqual(refusedPauseOrder, ['status', 'pause'],
    'running0 cannot authorize exports when busy/pending preparation refuses the manual pause');
}
for (const resumedStatus of [{source_running: 1, preparation: {active: false}},
  {source_running: 0, preparation: {active: true}}]) {
  const autoResumeOrder = [];
  await assert.rejects(() => stopSourceBeforeDiagnosticExport({
    requirePauseAcknowledgement: true,
    readStatus: async () => {autoResumeOrder.push('status'); return autoResumeOrder.length === 1
      ? {source_running: 0, preparation: {active: true}} : resumedStatus;},
    stopPlayback: async () => {autoResumeOrder.push('pause'); return {acknowledged: true};},
    finalizeTrace: async () => {autoResumeOrder.push('trace');},
    readEvidence: async () => {autoResumeOrder.push('export');},
  }), /Refusing large diagnostic exports/);
  assert.deepEqual(autoResumeOrder, ['status', 'pause', 'status'],
    'auto-resume or renewed preparation observed before export rejects even an earlier acknowledgement');
}
const acknowledgedPauseOrder = [];
await stopSourceBeforeDiagnosticExport({
  requirePauseAcknowledgement: true,
  readStatus: async () => {acknowledgedPauseOrder.push('status'); return {source_running: 0,
    preparation: {active: false}};},
  stopPlayback: async () => {acknowledgedPauseOrder.push('pause'); return {acknowledged: true};},
  finalizeTrace: async () => {acknowledgedPauseOrder.push('trace');},
  readEvidence: async () => {acknowledgedPauseOrder.push('export');},
});
assert.deepEqual(acknowledgedPauseOrder, ['status', 'pause', 'status', 'trace', 'export'],
  'an already stopped pair still requires fresh source acknowledgement before export');
const timedOutStatusOrder = [];
await assert.rejects(() => stopSourceBeforeDiagnosticExport({
  readStatus: async () => {timedOutStatusOrder.push('status'); throw Error('renderer observation timed out');},
  stopPlayback: async () => {timedOutStatusOrder.push('stop');},
  finalizeTrace: async () => {timedOutStatusOrder.push('finalize');},
  readEvidence: async () => {timedOutStatusOrder.push('export');},
}), /renderer observation timed out/);
assert.deepEqual(timedOutStatusOrder, ['status'],
  'a timed-out renderer status observation cannot trigger stop or large exports');

const runner = await readFile(new URL('../scripts/capture_whole_session_browser.mjs', import.meta.url), 'utf8');
const readyAt = runner.indexOf("await phase('runtime-ready'");
const installAt = runner.indexOf('report.pause_trace_installation = await installPauseTraceCapture(page, null)');
const importAt = runner.indexOf("await phase('disc-import', () => driver.selectDisc(values.disc))");
assert.ok(readyAt >= 0 && readyAt < installAt && installAt < importAt,
  'pause and incident observers install after runtime init but before disc import');

assert.ok(runner.includes("snapshot('replay-poll', {captureCss: !diagnostic})"),
  'state mode preserves CSS observations while performance polling avoids allocations');
assert.ok(runner.includes("readNaturalPauseBrowserCommandLine("),
  'strict owner cleanup receives a CDP process inventory before measurement');
const processInfoAt = runner.indexOf("report.browser.command_line = await readNaturalPauseBrowserCommandLine(");
const durableInventoryAt = runner.indexOf("await write('report.json', report);", processInfoAt);
const pageCreateAt = runner.indexOf('page = diagnostic ?');
const navigationAt = runner.indexOf('page.goto(');
assert.ok(processInfoAt >= 0 && durableInventoryAt > processInfoAt &&
  pageCreateAt > durableInventoryAt && navigationAt > durableInventoryAt,
  'owned Chrome CDP identity is durable before page creation, navigation, or measurement');
assert.ok(runner.includes('report.last_successful_snapshot = value'),
  'the first successful observation remains available when later renderer work hangs');
assert.ok(runner.includes("outcome: 'observation_timeout', source_cursor: null"),
  'an unobservable terminal cursor is explicitly incomplete rather than inferred');
assert.ok(runner.includes("Renderer observation timed out; terminal cursor unknown and no pause, trace, screenshot, or capture export was attempted."),
  'renderer observation timeout skips native pause and every large evidence export');
const finalSnapshotAt = runner.indexOf("const final = await snapshot('finally')");
const stateExportTimeoutGuardAt = runner.indexOf('if (pageObservationTimedOut)', finalSnapshotAt);
assert.ok(stateExportTimeoutGuardAt > finalSnapshotAt &&
  runner.indexOf('page_exports_skipped', stateExportTimeoutGuardAt) > stateExportTimeoutGuardAt,
  'state-mode final exports are skipped if its final renderer snapshot times out');
assert.ok(runner.includes('if (!diagnostic && stopAfter && !report.deliberate_prefix_stop'),
  'the natural-pause loop cannot enter state-mode cursor-stop/unload handling');
assert.ok(runner.includes("'stopped-scene-pair': {type: 'boolean', default: false}"),
  'the short stopped-scene pair requires a separate explicit opt-in');
const immediateCaptureAt = runner.indexOf('captureImmediate: stoppedScenePair &&');
const traceFinalizeAt = runner.indexOf('finalizeTrace: async () =>');
const gpuQueryAt = runner.indexOf("observePageOperation('stopped scene and GPU status'");
const delayedCaptureAt = runner.indexOf("captureStoppedSceneImage('stopped-scene-after-export'");
assert.ok(immediateCaptureAt >= 0 && immediateCaptureAt < traceFinalizeAt &&
  traceFinalizeAt < gpuQueryAt && gpuQueryAt < delayedCaptureAt,
  'paired screenshot order is stop, immediate image, trace delay, GPU query, delayed image');
assert.ok(runner.includes('boundary.stopped_cursor, status'),
  'the immediate image is bound to the cursor observed after native stop');
assert.ok(runner.includes('report.stopped_scene_pair.stop_boundary.stopped_cursor'),
  'the delayed image is bound to that same pinned stopped cursor');
assert.ok(runner.includes('const firstRow = counters.immediate_before.callback_rows'),
  'the verified source interval begins at the immediate pre-image status row count');
assert.ok(runner.includes('const endRow = counters.delayed_after.callback_rows'),
  'the verified source interval ends at the delayed post-image status row count');
assert.ok(runner.includes('runtime_producer_source: diagnosticManifest.build.producer_source ?? null'),
  'the capture report distinguishes current harness source from runtime artifact producer source');
assert.ok(runner.includes("observePageOperation('loaded runtime data identity',\n      page.evaluate("),
  'frozen runtime-data byte/hash verification is bounded by the renderer observation timeout');
assert.ok(!runner.includes('stopped_scene_visual'),
  'artifact presence is never serialized under a visual-completeness claim');
assert.ok(runner.includes('stopped_scene_artifacts_complete') &&
  runner.includes("visible_gameplay_observed: 'not_assessed'"),
  'artifact completeness and actual visible gameplay are recorded separately');

console.log('Natural-pause mode, frozen preflight, first-stop classification and export ordering passed.');

const browserIdentityOrder = [];
const fakeInventory = [{id: 123, type: 'browser', cpuTime: 0}];
await assert.rejects(readNaturalPauseBrowserCommandLine({send: async name => {
  browserIdentityOrder.push(name);
  if (name === 'SystemInfo.getProcessInfo') return {processInfo: fakeInventory};
  throw Error('Command line not returned because --enable-automation not set');
}}, '/tmp/fresh', async inventory => {
  assert.deepEqual(inventory, fakeInventory); browserIdentityOrder.push('persist');
}), /enable-automation/);
assert.deepEqual(browserIdentityOrder, ['SystemInfo.getProcessInfo', 'persist', 'Browser.getBrowserCommandLine']);
await assert.rejects(readNaturalPauseBrowserCommandLine({send: async name =>
  name === 'SystemInfo.getProcessInfo' ? {processInfo: fakeInventory} :
    {arguments: ['--user-data-dir=/tmp/wrong-profile']}}, '/tmp/fresh', async () => {}),
  /exact diagnostic profile/);
assert.deepEqual(await readNaturalPauseBrowserCommandLine({send: async name =>
  name === 'SystemInfo.getProcessInfo' ? {processInfo: fakeInventory} :
    {arguments: ['--user-data-dir', '/tmp/fresh']}}, '/tmp/fresh', async () => {}),
  {arguments: ['--user-data-dir', '/tmp/fresh']});
assert.ok(runner.includes("'--enable-automation'"), 'diagnostic launch enables the established Chrome command-line API');
console.log('Browser identity retains process attribution before command-line failure and rejects wrong profiles.');
console.log('Stopped-scene pair target, source interval, order, and scoped image completeness passed.');
