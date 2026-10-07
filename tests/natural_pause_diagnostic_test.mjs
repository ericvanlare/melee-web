import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {NATURAL_PAUSE_PROTOCOL, resolveCaptureMode, validateNaturalPauseManifest,
  naturalPauseRuntimeUrl, validateNaturalPauseBrowserIdentity, validateDefaultTwoRingStatus, firstNaturalPauseStop,
  stopSourceBeforeNaturalPauseExport} from '../scripts/natural_pause_diagnostic.mjs';

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

assert.deepEqual(resolveCaptureMode(), {mode: 'state', diagnostic: false});
assert.deepEqual(resolveCaptureMode('performance', '/private/plan.json'),
  {mode: 'performance', diagnostic: true});
assert.throws(() => resolveCaptureMode('performance'), /requires --diagnostic-manifest/);
assert.throws(() => resolveCaptureMode('state', '/private/plan.json'), /restricted/);
assert.throws(() => resolveCaptureMode('hitch'), /must be state or performance/);

assert.equal(validateNaturalPauseManifest(manifest(), ['runtime.js']).schema,
  'melee-web-natural-pause-diagnostic-manifest-v1');
assert.equal(NATURAL_PAUSE_PROTOCOL.replay_phase_timeout_ms, 65000);
assert.equal(NATURAL_PAUSE_PROTOCOL.overall_timeout_ms, 95000);
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

const order = [];
let statusReads = 0;
const lifecycle = await stopSourceBeforeNaturalPauseExport({
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
const unsafeOrder = [];
await assert.rejects(() => stopSourceBeforeNaturalPauseExport({
  readStatus: async () => {unsafeOrder.push('status'); return {source_running: 1};},
  stopPlayback: async () => {unsafeOrder.push('stop');},
  finalizeTrace: async () => {unsafeOrder.push('finalize');},
  readEvidence: async () => {unsafeOrder.push('export');},
}), /Refusing large diagnostic exports/);
assert.deepEqual(unsafeOrder, ['status', 'stop', 'status'],
  'trace stream and capture reads are skipped unless source_running is confirmed zero');

const runner = await readFile(new URL('../scripts/capture_whole_session_browser.mjs', import.meta.url), 'utf8');
const readyAt = runner.indexOf("await phase('runtime-ready'");
const installAt = runner.indexOf('report.pause_trace_installation = await installPauseTraceCapture(page, null)');
const importAt = runner.indexOf("await phase('disc-import', () => driver.selectDisc(values.disc))");
assert.ok(readyAt >= 0 && readyAt < installAt && installAt < importAt,
  'pause and incident observers install after runtime init but before disc import');

assert.ok(runner.includes("snapshot('replay-poll', {captureCss: !diagnostic})"),
  'state mode preserves CSS observations while performance polling avoids allocations');
assert.ok(runner.includes("browserCdp.send('SystemInfo.getProcessInfo')"),
  'strict owner cleanup receives a CDP process inventory before measurement');
assert.ok(runner.includes('if (!diagnostic && stopAfter && !report.deliberate_prefix_stop'),
  'the natural-pause loop cannot enter state-mode cursor-stop/unload handling');

console.log('Natural-pause mode, frozen preflight, first-stop classification and export ordering passed.');
