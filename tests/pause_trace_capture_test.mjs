import assert from 'node:assert/strict';
import vm from 'node:vm';
import {installPauseTraceCapture, readPauseTraceRows, readPauseTraceStatus} from './pause_trace_capture.mjs';

const originalWindow = globalThis.window;
const originalDocument = globalThis.document;
try {
  let cursor = 600;
  globalThis.window = {
    menuRuntimeTiming() {},
    menuDiagnosticSample() {},
    menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => cursor},
  };
  // Reconstruct the function in a separate realm as Playwright does. Calling
  // callback(value) directly would retain Node lexical bindings and hide leaks.
  const page = {evaluate: async (callback, value) => vm.runInNewContext(
    `(${callback.toString()})(argument)`, {window: globalThis.window,
      document: globalThis.document, performance, argument: structuredClone(value)})};
  const installed = await installPauseTraceCapture(page,
    {sourceFrame: 600, replayCursor: 600, durationMs: 2});
  assert.equal(installed.status, 'installed');
  assert.equal(installed.stall_schedule_supported, true);
  const sample = Array(19).fill(0);
  sample[1] = 600;
  globalThis.window.menuDiagnosticSample(...sample);
  globalThis.window.menuRuntimeTiming({frame: 600, total_ms: 2});
  let state = globalThis.window.__meleePauseTrace.state;
  assert.equal(state.stall_schedule.status, 'complete');
  assert.equal(state.stall_schedule.observed_source_frame, 600);
  assert.equal(state.stall_schedule.observed_replay_cursor, 600);
  assert.ok(state.stall_schedule.actual_ms >= 2);
  assert.equal(state.stall_schedule.replay_cursor_after, 600);
  assert.equal(state.errors, 0);
  assert.match(state.stall_schedule.insertion_boundary, /before native callback return/);

  cursor = 601;
  globalThis.window = {
    menuRuntimeTiming() {}, menuDiagnosticSample() {}, menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => cursor},
  };
  await installPauseTraceCapture(page, {sourceFrame: 600, replayCursor: 600, durationMs: 2});
  globalThis.window.menuDiagnosticSample(...sample);
  globalThis.window.menuRuntimeTiming({frame: 600, total_ms: 2});
  state = globalThis.window.__meleePauseTrace.state;
  assert.equal(state.stall_schedule.status, 'failed');
  assert.equal(state.stall_schedule.error, 'target_replay_cursor_mismatch');
  assert.equal(state.stall_schedule.observed_replay_cursor, 601);

  globalThis.window = {
    menuRuntimeTiming() {}, menuDiagnosticSample() {}, menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => 601},
  };
  await installPauseTraceCapture(page, {sourceFrame: 600, replayCursor: 600, durationMs: 2});
  sample[1] = 601;
  globalThis.window.menuDiagnosticSample(...sample);
  globalThis.window.menuRuntimeTiming({frame: 601, total_ms: 2});
  state = globalThis.window.__meleePauseTrace.state;
  assert.equal(state.stall_schedule.status, 'failed');
  assert.equal(state.stall_schedule.error, 'target_source_frame_skipped');
  assert.equal(state.errors, 0);

  cursor = 600;
  globalThis.window = {
    menuRuntimeTiming() {}, menuDiagnosticSample() {}, menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => cursor},
  };
  await installPauseTraceCapture(page, {sourceFrame: null, replayCursor: 600, durationMs: 2});
  sample[1] = 476;
  globalThis.window.menuDiagnosticSample(...sample);
  globalThis.window.menuRuntimeTiming({frame: 600, total_ms: 2});
  state = globalThis.window.__meleePauseTrace.state;
  assert.equal(state.stall_schedule.status, 'complete');
  assert.equal(state.stall_schedule.observed_source_frame, 476);
  assert.equal(state.stall_schedule.observed_replay_cursor, 600);

  cursor = 600;
  globalThis.window = {
    menuRuntimeTiming() {}, menuDiagnosticSample() {}, menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => cursor},
  };
  await installPauseTraceCapture(page, null);
  sample[1] = 476;
  globalThis.window.menuDiagnosticSample(...sample);
  cursor = 601;
  globalThis.window.menuRuntimeTiming({frame: 476, total_ms: 2, preparation_ms: 0});
  state = globalThis.window.__meleePauseTrace.state;
  const cursorColumn = state.columns.indexOf('sample_replay_cursor');
  assert.ok(cursorColumn >= 0);
  assert.equal(state.columns.indexOf('sample_source_frame') < cursorColumn, true);
  assert.equal(globalThis.window.__meleePauseTrace.table[cursorColumn], 601,
    'input cursor is sampled at the timing callback boundary, separately from source frame 476');
  const rows = await readPauseTraceRows(page, 0);
  assert.equal(rows.status, 'read');
  assert.equal(rows.rows[0].sample_source_frame, 476);
  assert.equal(rows.rows[0].sample_replay_cursor, 601);
  assert.equal(rows.rows[0].preparation_ms, 0);
  let nativeReads = 0;
  globalThis.window.Module = {
    _melee_web_native_menu_message: () => {nativeReads++; return 123;},
    UTF8ToString: pointer => {assert.equal(pointer, 123); return 'ready';},
    _melee_web_native_menu_replay_cursor: () => {nativeReads++; return 601;},
    _melee_web_native_menu_running: () => {nativeReads++; return 1;},
    _melee_web_native_menu_phase: () => {nativeReads++; return 2;},
  };
  globalThis.document = {querySelector: selector => selector === '#status'
    ? {dataset: {}, textContent: 'unloaded'} : null};
  const ready = await readPauseTraceStatus(page);
  assert.equal(ready.native_message, 'ready');
  assert.equal(ready.replay_cursor, 601);
  assert.equal(ready.source_running, 1);
  assert.equal(ready.phase, 2);
  assert.equal(nativeReads, 4, 'default status reads native APIs across serialized boundary');
  nativeReads = 0;
  for (const name of Object.keys(globalThis.window.Module))
    globalThis.window.Module[name] = () => {nativeReads++; throw Error('freed module');};
  const afterUnload = await readPauseTraceStatus(page, {readNative: false});
  assert.equal(afterUnload.status, 'installed');
  assert.equal(nativeReads, 0, 'post-unload status must not call any native API');
  for (const name of ['native_message', 'replay_cursor', 'source_running', 'phase'])
    assert.equal(afterUnload[name], null);

  await assert.rejects(() => installPauseTraceCapture(page,
    {sourceFrame: 600, replayCursor: 600, durationMs: 251}), /1\.\.250ms/);
} finally {
  if (originalWindow === undefined) delete globalThis.window;
  else globalThis.window = originalWindow;
  if (originalDocument === undefined) delete globalThis.document;
  else globalThis.document = originalDocument;
}

console.log('Pause trace capture scheduling and exact source-cursor stall checks passed.');
