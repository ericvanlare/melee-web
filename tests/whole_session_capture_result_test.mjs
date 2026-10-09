import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {finalizeSessionCapture, REQUIRED_SESSION_DOWNLOADS,
  validateRuntimeDataAbort, boundedCaptureOperation, retainFirstCaptureError,
  FIRST_REPLAY_BOUNDARY_MARKER_PREFIX, FIRST_REPLAY_BOUNDARY_MARKER_NAMES,
  parseFirstReplayBoundaryMarker, inspectFirstReplayBoundaryMarkers,
  readRequestedEntityPrefix, sessionReplayReportCompleted} from '../scripts/whole_session_capture_result.mjs';

const clean = () => ({
  result: 'fail', first_error: null, browser_errors: [], unexpected_requests: [],
  phases: [{name: 'whole-session-replay', result: 'pass'}],
  browser_report: {complete: true, pass: true, failures: []},
  saved_downloads: REQUIRED_SESSION_DOWNLOADS.map(name => ({name, bytes: 100, sha256: 'a'.repeat(64)})),
});
let checks = 0;
function rejects(mutate, message, result = 'fail') {
  const report = clean();
  // Even an incorrectly preassigned pass cannot bypass finalization.
  report.result = 'pass';
  mutate(report);
  assert.equal(finalizeSessionCapture(report), 1);
  assert.equal(report.result, result);
  assert(report.finalization_failures.some(reason => reason.includes(message)), JSON.stringify(report));
  checks++;
}
const success = clean();
assert.equal(finalizeSessionCapture(success), 0);
assert.equal(success.result, 'pass');
assert.deepEqual(success.finalization_failures, []);
const validRuntimeDataAbort = {
  requestCount: 1, responseCount: 1, failureCount: 1, finishedCount: 0,
  url: 'http://127.0.0.1:8813/gameplay_menu_browser.data',
  expectedUrl: 'http://127.0.0.1:8813/gameplay_menu_browser.data',
  method: 'GET', resourceType: 'fetch', errorText: 'net::ERR_ABORTED',
  responseStatus: 200, contentLength: 3674112, loadedBytes: 3674112,
  totalBytes: 3674112, fileBytes: 3674112, expectedBytes: 3674112,
  expectedSha256: 'a'.repeat(64), actualSha256: 'a'.repeat(64), fromCache: false,
};
assert.equal(validateRuntimeDataAbort(validRuntimeDataAbort), true);
assert.equal(validateRuntimeDataAbort({...validRuntimeDataAbort, loadedBytes: null, totalBytes: null,
  fileBytes: null, expectedBytes: null, expectedSha256: null, actualSha256: null, fromCache: null}), false,
  'the H1 startup report abort without loaded package bytes/hash remains an error');
for (const [key, value] of [
  ['requestCount', 2], ['responseCount', 0], ['failureCount', 2], ['finishedCount', 1],
  ['url', 'http://127.0.0.1:8813/other.data'], ['method', 'POST'], ['resourceType', 'xhr'],
  ['errorText', 'net::ERR_FAILED'], ['responseStatus', 206], ['contentLength', 3674111],
  ['loadedBytes', 3674111], ['totalBytes', 3674111], ['fileBytes', 3674111],
  ['expectedBytes', 3674111], ['actualSha256', 'b'.repeat(64)], ['fromCache', true],
]) {
  assert.equal(validateRuntimeDataAbort({...validRuntimeDataAbort, [key]: value}), false, key);
  checks++;
}
rejects(r => { r.verified_runtime_data_aborts = [{...validRuntimeDataAbort, actualSha256:'b'.repeat(64)}]; },
  'Runtime package abort evidence');
for (const kind of ['pageerror', 'console', 'http', 'requestfailed'])
  rejects(r => { r.browser_errors.push({kind, message: 'fixture'}); }, 'Browser errors');
for (const key of ['first_error', 'failure', 'first_mismatch'])
  rejects(r => { r[key] = 'fixture'; }, 'fatal harness diagnostic');
for (const key of ['download_error', 'cpu_download_error', 'owner_trace_error',
  'source_allocation_trace_error', 'page_dump_error', 'screenshot_error', 'close_error'])
  rejects(r => { r[key] = 'fixture'; }, key);
rejects(r => { r.unexpected_requests.push({method: 'POST'}); }, 'non-GET');
rejects(r => { r.phases[0].result = 'fail'; }, 'did not complete');
for (const key of ['complete', 'pass'])
  rejects(r => { r.browser_report[key] = false; }, 'incomplete or failed');
for (const key of ['failures', 'errors'])
  rejects(r => { r.browser_report[key] = ['fixture']; }, 'incomplete or failed');
for (const key of ['failures', 'errors'])
  for (const value of [{}, null, ''])
    rejects(r => { r.browser_report[key] = value; }, 'incomplete or failed');
rejects(r => { delete r.browser_report.failures; }, 'incomplete or failed');
rejects(r => { r.saved_downloads = []; }, 'Required artifact');
for (const name of REQUIRED_SESSION_DOWNLOADS) {
  rejects(r => { r.saved_downloads = r.saved_downloads.filter(row => row.name !== name); }, name);
  rejects(r => { r.saved_downloads.push(r.saved_downloads.find(row => row.name === name)); }, name);
  for (const bytes of [0, -1, 0.5, undefined])
    rejects(r => { r.saved_downloads.find(row => row.name === name).bytes = bytes; }, name);
  rejects(r => { r.saved_downloads.find(row => row.name === name).sha256 = 'invalid'; }, name);
}
rejects(r => { r.deliberate_prefix_stop = {requested_cursor: 1}; }, 'Deliberate prefix', 'incomplete');
console.log(`Whole-session capture finalization: clean pass and ${checks} rejection controls passed`);

assert.equal(await boundedCaptureOperation(Promise.resolve(7), 100, 'resolved observation'), 7);
await assert.rejects(boundedCaptureOperation(new Promise(() => {}), 5, 'stuck observation'),
  error => error.captureOperationTimeout === true && /stuck observation exceeded 5 ms/.test(error.message));
console.log('Bounded observation rejects a renderer promise that never settles.');

const firstErrorReport = {first_error: null};
retainFirstCaptureError(firstErrorReport, 'observation_timeout', 'snapshot exceeded 5 ms', 'replay');
retainFirstCaptureError(firstErrorReport, 'cleanup', 'TargetClosed', 'finally');
assert.deepEqual(firstErrorReport.first_error,
  {kind: 'observation_timeout', message: 'snapshot exceeded 5 ms', phase: 'replay', details: null});
console.log('A later TargetClosed cleanup error cannot replace the first renderer timeout.');
let markerSequence = 0;
const markerRows = [];
const marker = (name, values = [0, 0, 0]) => markerRows.push({sequence: ++markerSequence,
  marker: name, page_timestamp_ms: markerSequence, values});
marker('header_emit_begin');marker('header_onLog_begin');marker('header_onLog_returned');marker('header_emit_returned');
marker('menu_replay_started_dispatch_begin');marker('menu_replay_started_js_begin');
marker('native_memory_snapshot_begin');marker('native_memory_snapshot_returned');
marker('menu_replay_started_js_returned');marker('menu_replay_started_dispatch_returned');
marker('css_tick_begin', [0, 0, 1]);marker('css_tick_returned', [0, 0, 1]);
marker('session_frame_emit_begin', [0, 1, 1]);marker('frame0_emit_begin', [0, 1, 1]);
marker('session_frame_onLog_begin', [0, 0, 0]);marker('frame0_onLog_begin', [0, 0, 0]);
marker('frame0_onLog_returned', [0, 0, 0]);marker('session_frame_onLog_returned', [0, 0, 0]);
marker('frame0_emit_returned', [0, 1, 1]);marker('session_frame_emit_returned', [0, 1, 1]);
marker('ordinary_audio_boundary_begin', [1, 0, 0]);marker('ordinary_audio_tick_begin', [0, 1, 1]);
marker('ordinary_audio_tick_returned', [0, 1, 1]);marker('ordinary_audio_boundary_returned', [1, 0, 0]);
marker('source_frames_finish_begin', [1, 0, 1]);marker('aurora_begin_frame_begin', [0, 1, 1]);
marker('aurora_begin_frame_returned', [0, 1, 1]);marker('css_host_draw_begin', [0, 1, 1]);
marker('css_host_draw_returned', [0, 1, 1]);marker('aurora_end_frame_begin', [0, 1, 1]);
marker('aurora_end_frame_returned', [0, 0, 1]);marker('source_frames_finish_returned', [1, 1, 1]);
marker('first_replay_callback_tail_begin', [1, 1, 1]);marker('native_pause_begin', [1, 1, 1]);
marker('native_pause_returned', [0, 1, 1]);marker('first_replay_callback_tail_returned', [1, 1, 0]);
const encodedMarker = FIRST_REPLAY_BOUNDARY_MARKER_PREFIX + JSON.stringify(markerRows[0]);
assert.deepEqual(parseFirstReplayBoundaryMarker(encodedMarker), markerRows[0]);
assert.equal(parseFirstReplayBoundaryMarker('ordinary browser console output'), null);
assert.throws(() => parseFirstReplayBoundaryMarker(FIRST_REPLAY_BOUNDARY_MARKER_PREFIX + '{'), /Malformed/);
assert.throws(() => parseFirstReplayBoundaryMarker(FIRST_REPLAY_BOUNDARY_MARKER_PREFIX +
  JSON.stringify({...markerRows[0], marker: 'unrecognized'})), /Malformed/);
assert(FIRST_REPLAY_BOUNDARY_MARKER_NAMES.includes('aurora_end_frame_returned'));
const markerValidation = inspectFirstReplayBoundaryMarkers(markerRows);
assert.equal(markerValidation.complete, true, JSON.stringify(markerValidation));
assert.equal(markerValidation.callback_source_steps, 1);
assert.equal(markerValidation.callback_source_draws, 1);
assert.equal(markerValidation.replay_cursor_at_pause, 1);
assert.equal(markerValidation.source_draw_boundaries.length, 1);
assert.equal(markerValidation.native_pause_running, 0);

const multiStepRows = [];
let multiStepSequence = 0;
const multiStepMarker = (name, values = [0, 0, 0]) => multiStepRows.push({
  sequence: ++multiStepSequence, marker: name, page_timestamp_ms: multiStepSequence, values,
});
const appendMultiStep = row => multiStepMarker(row.marker, row.values);
const frame0ReturnedAt = markerRows.findIndex(row => row.marker === 'frame0_emit_returned');
for (const row of markerRows.slice(0, frame0ReturnedAt + 1)) appendMultiStep(row);
multiStepMarker('session_frame_emit_returned', [0, 1, 1]);
const drawStageNames = new Set(['aurora_begin_frame_begin', 'aurora_begin_frame_returned',
  'css_host_draw_begin', 'css_host_draw_returned', 'aurora_end_frame_begin',
  'aurora_end_frame_returned']);
for (const row of markerRows) if (drawStageNames.has(row.marker)) appendMultiStep(row);
multiStepMarker('css_tick_begin', [1, 1, 1]);multiStepMarker('css_tick_returned', [1, 1, 1]);
multiStepMarker('session_frame_emit_begin', [1, 1, 1]);
multiStepMarker('session_frame_onLog_begin', [1, 0, 0]);
multiStepMarker('session_frame_onLog_returned', [1, 0, 0]);
multiStepMarker('session_frame_emit_returned', [1, 1, 1]);
const audioBeginAt = markerRows.findIndex(row => row.marker === 'ordinary_audio_boundary_begin');
const finishBeginAt = markerRows.findIndex(row => row.marker === 'source_frames_finish_begin');
for (const row of markerRows.slice(audioBeginAt, finishBeginAt)) appendMultiStep(row);
multiStepMarker('source_frames_finish_begin', [2, 1, 2]);
multiStepMarker('aurora_begin_frame_begin', [1, 2, 2]);
multiStepMarker('aurora_begin_frame_returned', [1, 2, 1]);
multiStepMarker('css_host_draw_begin', [1, 2, 1]);
multiStepMarker('css_host_draw_returned', [1, 1, 2]);
multiStepMarker('aurora_end_frame_begin', [1, 2, 2]);
multiStepMarker('aurora_end_frame_returned', [1, 1, 1]);
multiStepMarker('source_frames_finish_returned', [2, 2, 2]);
multiStepMarker('first_replay_callback_tail_begin', [2, 2, 2]);
multiStepMarker('native_pause_begin', [2, 2, 2]);
multiStepMarker('native_pause_returned', [0, 2, 2]);
multiStepMarker('first_replay_callback_tail_returned', [2, 2, 0]);
const multiStepValidation = inspectFirstReplayBoundaryMarkers(multiStepRows);
assert.equal(multiStepValidation.complete, true, JSON.stringify(multiStepValidation));
assert.equal(multiStepValidation.callback_source_steps, 2);
assert.equal(multiStepValidation.callback_source_draws, 2);
assert.equal(multiStepValidation.css_tick_markers, 2);
assert.deepEqual(multiStepValidation.source_draw_boundaries.map(row => row.index), [0, 1]);
const missingReturn = inspectFirstReplayBoundaryMarkers(markerRows.slice(0, -1));
assert.equal(missingReturn.complete, false);
assert.equal(missingReturn.first_unmatched_marker.marker, 'first_replay_callback_tail_begin');
const sequenceGap = markerRows.map(row => ({...row}));sequenceGap[4].sequence++;
assert(inspectFirstReplayBoundaryMarkers(sequenceGap).errors.some(reason => reason.includes('sequence gap')));
console.log('First replay callback marker protocol validates source order, paired boundaries, and pause state.');

// Execute the actual production final-collection try/finally with stubbed renderer handles.
const captureSource = await fs.readFile(new URL('../scripts/capture_whole_session_browser.mjs', import.meta.url), 'utf8');
function extractBetween(start, end) {
  const a = captureSource.indexOf(start), b = captureSource.indexOf(end, a);
  assert(a >= 0 && b > a, `${start} production boundary missing`);
  return captureSource.slice(a, b);
}
const closeSource = extractBetween('async function closeOwnedCaptureBrowser() {', 'const boundaryMarkers = [];');
const observeSource = extractBetween('async function observePageOperation(label, operation) {', 'async function pauseTraceStatus');
const collectSource = extractBetween('  try {\n  if (diagnostic) {\n    if (pageObservationTimedOut)', '  for (const candidate of runtimeDataAbortCandidates)');
async function actualCollection(failure) {
  const calls = [], primary = {kind: 'existing primary', message: 'keep first failure'};
  const report = {first_error: primary};
  const pending = () => new Promise(() => {});
  const page = {
    isClosed: () => false,
    locator: name => ({
      evaluateAll: () => { calls.push('artifacts'); return failure === 'artifacts stall' ? pending() : Promise.resolve([{name: 'retail-port.jsonl', text: 'raw'}]); },
      innerText: () => { calls.push('text'); return Promise.resolve('page'); },
    }),
    evaluate: fn => {
      const text = String(fn);
      if (text.includes('__cpuPrefixRows')) {calls.push('cpu prefix'); return failure === 'CPU prefix stall' ? pending() : Promise.resolve([]);}
      if (text.includes('TraceTotal')) {calls.push('allocation total'); return Promise.resolve(0);}
      calls.push('other evaluate'); return Promise.resolve([]);
    },
    screenshot: () => {calls.push('screenshot'); return Promise.resolve();},
  };
  const scope = vm.createContext({report, page, diagnostic: false, pageObservationTimedOut: false,
    observationTimeoutMs: 5, currentPhase: 'finally', boundedCaptureOperation,
    firstError: (kind, message) => {report.first_error ||= {kind, message};},
    snapshot: async () => ({}), rngDrawProbe: null, hitTransitionProbe: null,
    write: async () => {calls.push('write'); if (failure === 'write failure') throw Error('owned write failed');},
    Buffer, createHash: (await import('node:crypto')).createHash, path: (await import('node:path')).default,
    output: 'unused', driver: {dispose: () => calls.push('dispose')},
    browser: {close: async () => {calls.push('close'); if (failure === 'close failure') throw Error('owned close failed');}},
    browserContext: null, diagnosticCdp: null,
  });
  vm.runInContext(observeSource + closeSource, scope);
  await vm.runInContext(`(async () => {${collectSource}})()`, scope);
  assert.equal(report.first_error, primary, 'later failures must preserve primary');
  assert.equal(calls.at(-1), 'close', 'actual final collection must close its browser last');
  if (failure === 'artifacts stall') {
    assert.deepEqual(calls, ['artifacts', 'dispose', 'close']);
    assert.equal(scope.pageObservationTimedOut, true);
  }
  if (failure === 'CPU prefix stall') {
    assert.deepEqual(calls, ['artifacts', 'write', 'cpu prefix', 'dispose', 'close']);
    assert.equal(scope.pageObservationTimedOut, true);
  }
  if (failure === 'write failure') assert.match(report.download_error, /owned write failed/);
  if (failure === 'close failure') assert.match(report.close_error, /owned close failed/);
  checks++;
}
for (const failure of ['artifacts stall', 'CPU prefix stall', 'write failure', 'close failure']) await actualCollection(failure);
console.log(`Owned capture final collection controls passed: ${checks} total report/ownership controls.`);

// Synthetic transport metadata exercises the production requested-mode parser;
// it is not a substitute for native context/setup/PAD admission.
const prefixDigest = 'c'.repeat(64);
function prefixRecipe(observations = 60) {
  const contextBytes = 0x18 + 0x55e8 + 0x148 + 6;
  const frames = 2 + observations, spans = 28 + contextBytes + 0x138 + 822 + frames * 44;
  const bytes = Buffer.alloc(spans + 2 + 36);
  bytes.write('MWRC'); bytes.writeUInt32BE(8, 4); bytes.writeUInt32BE(frames, 12);
  bytes.writeUInt16BE(2, 20); bytes.writeUInt16BE(1, 22);
  bytes.writeUInt32BE(contextBytes, 24); bytes.writeUInt16BE(3, spans);
  for (let index = 0; index < 3; index++) {
    const offset = spans + 2 + index * 12;
    bytes[offset] = index + 1;
    bytes.writeUInt32BE(index, offset + 4);
    bytes.writeUInt32BE(index === 2 ? frames - 1 : index, offset + 8);
  }
  return {bytes, spans};
}
const requestedPrefix = readRequestedEntityPrefix(prefixRecipe().bytes, prefixDigest);
assert.deepEqual(requestedPrefix, {name: 'jiggly-ice-mario-fox-v1', frames: 62,
  observations: 60, recipe_sha256: prefixDigest});
for (const version of [8, 9, 10]) {
  const {bytes} = prefixRecipe(); bytes.writeUInt32BE(version, 4); bytes.writeUInt16BE(0, 22);
  assert.equal(readRequestedEntityPrefix(bytes, prefixDigest), null, 'flag-zero defaults unchanged');
}
for (const mutate of [
  ({bytes}) => bytes.writeUInt32BE(9, 4),
  ({bytes}) => bytes.writeUInt32BE(10, 4),
  ({bytes}) => bytes.writeUInt16BE(1, 20),
  ({bytes}) => bytes.writeUInt32BE(0, 24),
  ({bytes, spans}) => bytes.writeUInt16BE(2, spans),
  ({bytes, spans}) => bytes.writeUInt32BE(2, spans + 2 + 12 + 4),
  ({bytes, spans}) => {bytes[spans + 2 + 24] = 1;},
  ({bytes, spans}) => bytes.writeUInt16BE(1, spans + 2 + 24 + 2),
]) {
  const recipe = prefixRecipe(); mutate(recipe);
  assert.throws(() => readRequestedEntityPrefix(recipe.bytes, prefixDigest), /prefix/i); checks++;
}
for (const observations of [59, 65])
  assert.throws(() => readRequestedEntityPrefix(prefixRecipe(observations).bytes, prefixDigest), /bound/);
assert.throws(() => readRequestedEntityPrefix(prefixRecipe().bytes.subarray(0, -1), prefixDigest), /interval/);
assert.throws(() => readRequestedEntityPrefix(prefixRecipe().bytes, 'invalid'), /envelope/);
const prefixReport = () => ({schema: 'melee-web-browser-retail-replay', version: 1,
  pass: true, failures: [], complete: false, diagnostic_prefix: requestedPrefix.name,
  diagnostic_prefix_complete: true, whole_session_equivalent: false,
  comparison_source_ticks: 60, mode: 'state_capture', final_scene: 3,
  recipe_sha256: prefixDigest, frames: requestedPrefix.frames,
  source_progress: {observations: 60, bound_observations: 60, first_source_tick: 0, last_source_tick: 59},
  metrics: {sourceFrames: 62, sourceSteps: 62, sourceDraws: 62},
  source_match: {complete: false, outcome: null, winner: null},
});
const prefixCapture = () => ({...clean(), requested_entity_prefix: requestedPrefix,
  browser_report: prefixReport()});
assert.equal(sessionReplayReportCompleted(prefixReport(), requestedPrefix), true);
const prefixSuccess = prefixCapture();
assert.equal(finalizeSessionCapture(prefixSuccess), 0);
assert.equal(prefixSuccess.browser_report.complete, false);
assert.equal(prefixSuccess.browser_report.whole_session_equivalent, false);
for (const mutate of [
  r => {delete r.requested_entity_prefix;},
  r => {r.requested_entity_prefix = {...requestedPrefix, name: 'unsolicited'};},
  r => {r.requested_entity_prefix = {...requestedPrefix, observations: 59};},
  r => {r.browser_report.schema = 'foreign';},
  r => {r.browser_report.version = 2;},
  r => {r.browser_report.diagnostic_prefix = 'foreign';},
  r => {r.browser_report.diagnostic_prefix_complete = false;},
  r => {r.browser_report.complete = true;},
  r => {r.browser_report.whole_session_equivalent = true;},
  r => {r.browser_report.comparison_source_ticks = 59;},
  r => {r.browser_report.mode = 'performance';},
  r => {r.browser_report.final_scene = 1;},
  r => {r.browser_report.recipe_sha256 = 'd'.repeat(64);},
  r => {r.browser_report.frames--;},
  r => {r.browser_report.source_progress.observations--;},
  r => {r.browser_report.source_progress.bound_observations++;},
  r => {r.browser_report.source_progress.first_source_tick = 1;},
  r => {r.browser_report.source_progress.last_source_tick++;},
  ...['sourceFrames', 'sourceSteps', 'sourceDraws'].map(key => r => {r.browser_report.metrics[key]--;}),
  r => {r.browser_report.source_match.complete = true;},
  r => {r.browser_report.source_match.outcome = 1;},
  r => {r.browser_report.source_match.winner = 0;},
  r => {r.browser_report.failures.push('teardown incomplete');},
  r => {r.browser_report.errors = ['runtime failure'];},
  r => {r.first_error = {message: 'retained preparation failure'};},
  r => {r.close_error = 'owned close failed';},
]) {
  const report = prefixCapture(); mutate(report);
  assert.equal(finalizeSessionCapture(report), 1, JSON.stringify(report)); checks++;
}
const unsolicited = clean(); unsolicited.browser_report.diagnostic_prefix = requestedPrefix.name;
assert.equal(finalizeSessionCapture(unsolicited), 1);
console.log(`Requested entity-prefix production parser/completion/finalization controls passed; ${checks} total controls.`);

// Execute the production header-binding block: a failed request retains its
// named identity, while an unsolicited completion can never establish it.
const requestBindingSource = extractBetween('  report.requested_entity_prefix =', '  if (diagnostic && (report.recipe_header.version');
function actualRequestBinding(overrides = {}) {
  const scope = vm.createContext({report: {mode: 'state', inputs: {recipe: {sha256: prefixDigest}}},
    recipeBytes: prefixRecipe().bytes, readRequestedEntityPrefix,
    diagnostic: false, stopAfter: 0, resumeTimingPauses: false, firstReplayCallbackProbe: false,
    captureCpuObservations: false, rngDrawProbe: null, hitTransitionProbe: null, ...overrides});
  let failure;
  try {vm.runInContext(requestBindingSource, scope);} catch (error) {failure = error;}
  return {report: scope.report, failure};
}
assert.equal(actualRequestBinding().failure, undefined);
assert.equal(actualRequestBinding().report.requested_entity_prefix.name, requestedPrefix.name);
for (const overrides of [{diagnostic: true}, {stopAfter: 1}, {resumeTimingPauses: true},
  {firstReplayCallbackProbe: true}, {captureCpuObservations: true}, {rngDrawProbe: {}}, {hitTransitionProbe: {}}]) {
  const {report, failure} = actualRequestBinding(overrides);
  assert.match(failure?.message || '', /unmodified state capture/);
  assert.equal(report.requested_entity_prefix.name, requestedPrefix.name, 'failure preserves requested mode');
}
console.log('Actual harness recipe-binding branch preserves requested mode on incompatible-option failures.');

for (const key of ['diagnostic_prefix_complete', 'whole_session_equivalent', 'comparison_source_ticks', 'source_progress']) {
  const report = clean(); report.browser_report[key] = key === 'source_progress' ? {} : false;
  assert.equal(finalizeSessionCapture(report), 1, `unsolicited ${key}`);
}
if (process.env.ENTITY_PREFIX_RECIPE) {
  const raw = await fs.readFile(process.env.ENTITY_PREFIX_RECIPE);
  const digest = (await import('node:crypto')).createHash('sha256').update(raw).digest('hex');
  assert.equal(digest, 'b30936fad87fe0b3a90375872dbd10074d535f7edde15987fc17cda6a549811e');
  assert.deepEqual(readRequestedEntityPrefix(raw, digest), {name: 'jiggly-ice-mario-fox-v1',
    frames: 1164, observations: 60, recipe_sha256: digest});
  console.log('Actual original-v2 immutable recipe metadata parsed: 1164 input frames, 60 SourceTick observations.');
}
