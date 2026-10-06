import assert from 'node:assert/strict';
import {installPauseTraceCapture} from './pause_trace_capture.mjs';

const originalWindow = globalThis.window;
try {
  let cursor = 600;
  globalThis.window = {
    menuRuntimeTiming() {},
    menuDiagnosticSample() {},
    menuDiagnosticIncident() {},
    Module: {_melee_web_native_menu_replay_cursor: () => cursor},
  };
  const page = {evaluate: async (callback, value) => callback(value)};
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

  await assert.rejects(() => installPauseTraceCapture(page,
    {sourceFrame: 600, replayCursor: 600, durationMs: 251}), /1\.\.250ms/);
} finally {
  if (originalWindow === undefined) delete globalThis.window;
  else globalThis.window = originalWindow;
}

console.log('Pause trace capture scheduling and exact source-cursor stall checks passed.');
