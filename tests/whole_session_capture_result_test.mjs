import assert from 'node:assert/strict';
import {finalizeSessionCapture, REQUIRED_SESSION_DOWNLOADS,
  validateRuntimeDataAbort, boundedCaptureOperation, retainFirstCaptureError,
  FIRST_REPLAY_BOUNDARY_MARKER_PREFIX, FIRST_REPLAY_BOUNDARY_MARKER_NAMES,
  parseFirstReplayBoundaryMarker, inspectFirstReplayBoundaryMarkers} from '../scripts/whole_session_capture_result.mjs';

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
