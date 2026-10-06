#!/usr/bin/env node
/** One bounded opt-in staging-ring sustained-load reducer; no stalls, hashes or retries. */
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {installPauseTraceCapture, readPauseTraceCapture, readPauseTraceStatus,
  readPauseTraceRows, markPauseTraceBoundary, readRetainedPauseDiagnostics} from './pause_trace_capture.mjs';
import {createHeavyGpuPage, setHeavyGpu, readHeavyGpu} from './pause_trace_perturb.mjs';
import {assessSustainedWindow, SUSTAINED_WINDOW_MS,
  MAX_OBSERVED_GPU_WINDOW_MS, PREPARATION_REASON, summarizeObservedBatchDurations,
  resolveSustainedAttempt, artifactMapDigest, compareArtifactMaps,
  validateProspectiveBuildManifest, validateSelectedRingStatus} from './staging_ring_sustained_decision.mjs';

const ROOT = path.resolve(import.meta.dirname, '..');
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, recipe: {type: 'string'},
  'baseline-report': {type: 'string'}, 'prospective-build-manifest': {type: 'string'}, out: {type: 'string'},
  'build-dir': {type: 'string'}, playwright: {type: 'string'},
  slots: {type: 'string', default: '2'},
  'phase-timeout-ms': {type: 'string', default: '180000'},
  'preparation-timeout-ms': {type: 'string', default: '180000'},
  'window-timeout-ms': {type: 'string', default: '15000'},
}});

function integer(name, min, max) {
  const value = Number(values[name]);
  if (!Number.isInteger(value) || value < min || value > max)
    throw Error(`--${name} must be ${min}..${max}`);
  return value;
}
for (const name of ['url', 'disc', 'recipe', 'baseline-report', 'prospective-build-manifest', 'out', 'build-dir']) {
  if (!values[name]) throw Error(`Missing --${name}`);
}
const slots = Number(values.slots);
if (![2, 4].includes(slots)) throw Error('--slots must be 2 or 4');
const phaseTimeoutMs = integer('phase-timeout-ms', 1000, 600000);
const preparationTimeoutMs = integer('preparation-timeout-ms', 1000, 600000);
const windowTimeoutMs = integer('window-timeout-ms', 3000, 300000);
const baseUrl = new URL(values.url);
if (!['http:', 'https:'].includes(baseUrl.protocol) || !baseUrl.pathname.endsWith('/runtime.html') ||
    !['localhost', '127.0.0.1', '::1', '[::1]'].includes(baseUrl.hostname))
  throw Error('A real loopback HTTP runtime.html URL is required');
baseUrl.searchParams.set('melee-web-staging-slots', String(slots));
baseUrl.searchParams.delete('melee-web-staging-byte-hash');
baseUrl.searchParams.set('melee-web-staging-diagnostics', '1');

const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const browserArtifactNames = JSON.parse(await fs.readFile(path.join(ROOT, 'tools/browser_build_artifacts.json'), 'utf8'));
async function shaFile(filename) {
  const hash = crypto.createHash('sha256');
  const stream = (await import('node:fs')).createReadStream(filename);
  for await (const chunk of stream) hash.update(chunk);
  return hash.digest('hex');
}
async function inputIdentity(filename) {
  const stat = await fs.stat(filename);
  if (!stat.isFile()) throw Error('Experiment input is not a regular file');
  return {bytes: stat.size, sha256: await shaFile(filename)};
}
async function artifactMap(buildDirectory) {
  const artifacts = {};
  for (const name of browserArtifactNames) {
    const local = path.join(buildDirectory, name);
    const stat = await fs.stat(local);
    if (!stat.isFile()) throw Error(`Release artifact is not a file: ${name}`);
    const response = await fetch(new URL(name, baseUrl), {cache: 'no-store', signal: AbortSignal.timeout(30000)});
    if (!response.ok) throw Error(`Served browser artifact ${name}: HTTP ${response.status}`);
    const hash = crypto.createHash('sha256');
    let bytes = 0;
    const reader = response.body.getReader();
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      bytes += value.byteLength;
      if (bytes > 256 * 1024 * 1024) throw Error(`Served artifact exceeds byte bound: ${name}`);
      hash.update(value);
    }
    const servedSha = hash.digest('hex');
    const localSha = await shaFile(local);
    if (servedSha !== localSha || bytes !== stat.size)
      throw Error(`Served Release artifact differs from frozen build: ${name}`);
    artifacts[name] = {bytes, sha256: servedSha};
  }
  return artifacts;
}
async function localArtifactMap(buildDirectory) {
  const artifacts = {};
  for (const name of browserArtifactNames) {
    const filename = path.join(buildDirectory, name);
    const stat = await fs.stat(filename);
    if (!stat.isFile()) throw Error(`Release artifact is not a file: ${name}`);
    artifacts[name] = {bytes: stat.size, sha256: await shaFile(filename)};
  }
  return artifacts;
}
async function sourceIdentity() {
  const git = (...args) => execFileSync('git', args, {cwd: ROOT, encoding: 'utf8'}).trim();
  const dirty = git('status', '--porcelain');
  if (dirty) throw Error('Freeze the harness commit and leave the checkout clean before capture');
  return {commit: git('rev-parse', 'HEAD'), tree: git('rev-parse', 'HEAD^{tree}'),
    tracked_diff_sha256: sha(execFileSync('git', ['diff', '--binary', 'HEAD'], {cwd: ROOT})),
    aurora_patch_sha256: await shaFile(path.join(ROOT, 'patches/aurora-browser.patch'))};
}
function recipeHeader(bytes) {
  if (bytes.length < 20 || bytes.toString('ascii', 0, 4) !== 'MWRC') throw Error('Recipe is not MWRC');
  const header = {version: bytes.readUInt32BE(4), seed_hex: bytes.readUInt32BE(8).toString(16).padStart(8, '0'),
    frames: bytes.readUInt32BE(12), profile_characters_hex: bytes.readUInt16BE(16).toString(16).padStart(4, '0'),
    profile_stages_hex: bytes.readUInt16BE(18).toString(16).padStart(4, '0'), bytes: bytes.length};
  if (header.version !== 4 || header.frames !== 1800 || header.profile_characters_hex !== '07ff' ||
      header.profile_stages_hex !== '01c0' || bytes.length !== 20 + 0x138 + 822 + header.frames * 44)
    throw Error('Recipe is not the declared 1800-frame synthetic four-player MWRC v4 fixture');
  return header;
}
async function nativeSnapshot(page) {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const call = name => {try {return typeof module?.[name] === 'function' ? module[name]() : null;}
      catch (error) {return {error: String(error?.message || error)};}};
    let message = null, frame = null, match = null;
    try {const pointer = call('_melee_web_native_menu_message');
      if (pointer) message = module.UTF8ToString(pointer);} catch (error) {message = {error: String(error)};}
    try {frame = window.menuObservePlayer?.()?.frame ?? null;} catch (error) {frame = {error: String(error)};}
    try {const pointer = call('_melee_web_native_menu_match_observe');
      if (pointer) match = JSON.parse(module.UTF8ToString(pointer));} catch (error) {match = {error: String(error)};}
    return {input_cursor: call('_melee_web_native_menu_replay_cursor'),
      source_running: call('_melee_web_native_menu_running'), phase: call('_melee_web_native_menu_phase'),
      message, original_match_frame: frame, match};
  });
}
async function memorySnapshot(page) {
  return page.evaluate(() => {
    let native = null;
    try {const pointer = globalThis.Module?._melee_web_native_menu_memory?.();
      if (pointer) native = JSON.parse(Module.UTF8ToString(pointer));} catch (error) {native = {error: String(error)};}
    const js = performance.memory ? {used_bytes: performance.memory.usedJSHeapSize,
      total_bytes: performance.memory.totalJSHeapSize, limit_bytes: performance.memory.jsHeapSizeLimit} : null;
    return {native, js_heap: js};
  });
}
function checkCaptureHealth(status, browserErrors) {
  if (status.runtime_error || status.dialog_error)
    return {kind: 'runtime_error', message: status.runtime_error || status.dialog_error};
  if (status.capture_errors || status.dropped || status.incident_overflow)
    return {kind: 'capture_telemetry_fault', message: 'Timing capture lost rows or hook data'};
  if (browserErrors.length) return {kind: 'browser_error', message: browserErrors[0].message};
  if (status.native_message?.error) return {kind: 'native_status_error', message: status.native_message.error};
  return null;
}
function preparationFinding(rows, incidents, startMs, endMs = Infinity) {
  const row = rows.find(value => finite(value.hook_at_ms) && value.hook_at_ms >= startMs &&
    value.hook_at_ms <= endMs && value.sample_source_frame > 0 &&
    finite(value.preparation_ms) && value.preparation_ms !== 0);
  if (row) return {kind: 'preparation_duration', row};
  const event = incidents.find(value => value.reason === PREPARATION_REASON && value.source_frame > 0 &&
    finite(value.at_ms) && value.at_ms >= startMs && value.at_ms <= endMs);
  return event ? {kind: 'preparation_event', event} : null;
}
function missingTelemetryFinding(rows, startMs, endMs = Infinity) {
  const required = ['hook_at_ms', 'total_ms', 'preparation_ms', 'staging_slot_wait_ms',
    'simulation_audio_ms', 'sample_pending_ticks', 'sample_running',
    'sample_source_frame', 'sample_replay_cursor'];
  const row = rows.find(value => finite(value.hook_at_ms) && value.hook_at_ms >= startMs &&
    value.hook_at_ms <= endMs && required.some(name => !finite(value[name])));
  return row ? {kind: 'missing_callback_telemetry', row} : null;
}
function finite(value) { return typeof value === 'number' && Number.isFinite(value); }
async function heavyWindowState(heavy) {
  return heavy.page.evaluate(() => {
    const stats = window.__gpuLoad;
    return {time_origin_ms: performance.timeOrigin, started_at_ms: stats.started_at_ms,
      stopped_at_ms: stats.stopped_at_ms, deadline_fired: stats.deadline_fired === true,
      scheduled_end_epoch_ms: stats.scheduled_end_epoch_ms ?? null, on: stats.on};
  });
}
async function scheduleHeavyStop(heavy, endEpochMs) {
  return heavy.page.evaluate(deadline => {
    const stats = window.__gpuLoad;
    clearTimeout(stats.deadline_timer);
    const nowEpoch = performance.timeOrigin + performance.now();
    const delay = Math.max(0, deadline - nowEpoch);
    stats.deadline_fired = false;
    stats.scheduled_end_epoch_ms = deadline;
    stats.deadline_timer = setTimeout(() => {
      stats.on = false;
      stats.stopped_at_ms = performance.now();
      stats.deadline_fired = true;
    }, delay);
    return {now_epoch_ms: nowEpoch, deadline_epoch_ms: deadline, timer_delay_ms: delay};
  }, endEpochMs);
}
async function stopMeasuredGpu(heavy) {
  // Disable in one same-page task before any end-boundary or final-row reads.
  // If the self-deadline already disabled the load, preserve its exact stop
  // timestamp and separately record when the harness observed that state.
  const state = await heavy.page.evaluate(() => {
    const stats = window.__gpuLoad;
    const observed_at_ms = performance.now();
    const was_on_when_observed = stats.on;
    if (was_on_when_observed) {
      stats.on = false;
      stats.stopped_at_ms = observed_at_ms;
    }
    clearTimeout(stats.deadline_timer);
    return {time_origin_ms: performance.timeOrigin,
      started_at_ms: stats.started_at_ms, stopped_at_ms: stats.stopped_at_ms,
      stop_observed_at_ms: observed_at_ms, was_on_when_observed,
      deadline_fired: stats.deadline_fired === true,
      scheduled_end_epoch_ms: stats.scheduled_end_epoch_ms ?? null, on: stats.on};
  });
  const gpuObservation = await readHeavyGpu(heavy);
  const batchDurations = await heavy.page.evaluate(() => window.__gpuLoad.durations.slice());
  return {state, gpuObservation, batchDurations,
    actual_gpu_load_window_ms: finite(state.started_at_ms) && finite(state.stopped_at_ms)
      ? state.stopped_at_ms - state.started_at_ms : null};
}

let browser, page, driver, heavy;
let traceInstalled = false, runtimeReady = false, gpuOn = false, unloaded = false;
const report = {
  schema: 'melee-web-staging-ring-sustained-v1',
  captured_at: new Date().toISOString(),
  scope: `one synthetic 1800-input fixture; requested 2s ring-${slots} 300-work callback control then one requested 2s 600-work treatment; actual enabled intervals retained, <=100ms observer slack only; headless mechanism diagnostic only`,
  result: 'fail', decision: 'inspect_immediately',
  browser_mode: 'headless installed Chrome; host speakers muted; Web Audio and PCM processing remain enabled',
  treatment: slots === 4 ? 'ring_four' : 'ring_two', requested_frame_slots: slots,
  decision_assessment_semantics: 'legacy frozen fractional-remainder classifier; comparison interpretation is separate; pause_callback fields may refer to preceding active callback rather than actual guard callback',
  hash_capture: false, staging_diagnostics: true,
  host_stall: 'none', adaptive_tuning: false,
  batch_duration_semantics: 'observed WebGL draw plus synchronous readback registration-to-delivery wall duration; not pure GPU execution time',
  requested_control_callback_window_ms: SUSTAINED_WINDOW_MS,
  requested_treatment_gpu_window_ms: SUSTAINED_WINDOW_MS,
  maximum_observed_gpu_window_ms: MAX_OBSERVED_GPU_WINDOW_MS,
  observer_slack_ms: MAX_OBSERVED_GPU_WINDOW_MS - SUSTAINED_WINDOW_MS,
  capture_start: null, calibration: [], control_window: null, treatment_window: null,
  source: null, baseline_binding: null, prospective_build_binding: null,
  build_artifacts_before: null, build_artifacts_after: null,
  inputs: null, recipe_header: null, browser_errors: [], failure: null,
};
const pageError = error => report.browser_errors.push({kind: 'pageerror', message: String(error?.message || error)});
const consoleError = message => {if (message.type() === 'error') report.browser_errors.push({kind: 'console', message: message.text()});};

async function runLoadWindow({iterations, page, heavy, label}) {
  const beforeLoad = await markPauseTraceBoundary(page);
  if (beforeLoad.status !== 'marked') throw Error('Timing callback window could not be prepared');
  // Set the cleanup obligation before crossing the browser boundary: even a
  // partially successful enable call must be followed by a best-effort disable.
  gpuOn = true;
  const enabled = await setHeavyGpu(heavy, true, iterations);
  const gpuStart = await heavyWindowState(heavy);
  if (!finite(gpuStart.started_at_ms) || !finite(gpuStart.time_origin_ms))
    throw Error('GPU workload start time is unavailable');
  const startEpochMs = gpuStart.time_origin_ms + gpuStart.started_at_ms;
  const loadStartMs = startEpochMs - beforeLoad.time_origin;
  const start = {...beforeLoad, at_ms: loadStartMs, callback_window_start_at_ms: null,
    load_start_epoch_ms: startEpochMs, time_origin_mapping: 'performance.timeOrigin + performance.now() across two pages'};
  let callbackWindowStartMs = label === '300_control' ? null : loadStartMs;
  let wallDeadline = callbackWindowStartMs === null ? Infinity : callbackWindowStartMs + SUSTAINED_WINDOW_MS;
  const safetyDeadline = loadStartMs + MAX_OBSERVED_GPU_WINDOW_MS;
  let hardStop = await scheduleHeavyStop(heavy, beforeLoad.time_origin +
    (Number.isFinite(wallDeadline) ? wallDeadline : safetyDeadline));
  if (callbackWindowStartMs !== null) start.callback_window_start_at_ms = callbackWindowStartMs;
  const capturedRows = [];
  let nextRow = start.row_count;
  let terminal = null, end = null, latestStatus = null;
  const hostDeadline = Date.now() + windowTimeoutMs;
  while (Date.now() < hostDeadline) {
    latestStatus = await readPauseTraceStatus(page);
    const batch = await readPauseTraceRows(page, nextRow);
    if (batch.status !== 'read') throw Error('Timing callback rows became unavailable');
    capturedRows.push(...batch.rows);
    nextRow = batch.to_row;
    const health = checkCaptureHealth(latestStatus, report.browser_errors);
    if (health) {terminal = {outcome: 'fault', fault: health,
      end_at_ms: await page.evaluate(() => performance.now())}; break;}
    const gpuErrors = await heavy.page.evaluate(() => window.__gpuLoad.errors.slice());
    if (gpuErrors.length) {terminal = {outcome: 'fault', fault: {kind: 'gpu_workload_error', errors: gpuErrors},
      end_at_ms: await page.evaluate(() => performance.now())}; break;}
    const missing = missingTelemetryFinding(capturedRows, loadStartMs);
    if (missing) {terminal = {outcome: 'fault', fault: missing, end_at_ms: missing.row.hook_at_ms}; break;}
    const prep = preparationFinding(capturedRows, latestStatus.incidents, loadStartMs);
    if (prep) {terminal = {outcome: 'preparation', preparation: prep,
      end_at_ms: prep.row?.hook_at_ms ?? prep.event?.at_ms}; break;}
    if (latestStatus.native_message?.startsWith('Paused after a timing disruption') && latestStatus.source_running === 0) {
      const incident = [...latestStatus.incidents].reverse().find(event => event.source_frame > 0 &&
        event.reason !== PREPARATION_REASON && finite(event.at_ms) && event.at_ms >= loadStartMs);
      terminal = {outcome: 'timing_pause', pause_reason: incident?.reason ?? null,
        pause_incident: incident ?? null, end_at_ms: incident?.at_ms ?? await page.evaluate(() => performance.now())};
      break;
    }
    if (latestStatus.replay_report) {
      terminal = {outcome: 'fixture_completed_early', replay_report: latestStatus.replay_report,
        end_at_ms: await page.evaluate(() => performance.now())};
      break;
    }
    const now = await page.evaluate(() => performance.now());
    if (latestStatus.source_running !== 1) {
      terminal = {outcome: 'fault', fault: {kind: 'source_not_running_during_control_or_treatment',
        source_running: latestStatus.source_running}, end_at_ms: now};
      break;
    }
    if (callbackWindowStartMs === null) {
      const firstActive = capturedRows.find(row => finite(row.hook_at_ms) && row.hook_at_ms >= loadStartMs &&
        row.sample_running === 1 && row.sample_source_frame > 0);
      if (firstActive) {
        callbackWindowStartMs = firstActive.hook_at_ms;
        wallDeadline = callbackWindowStartMs + SUSTAINED_WINDOW_MS;
        start.callback_window_start_at_ms = callbackWindowStartMs;
        if (wallDeadline > safetyDeadline || now >= safetyDeadline) {
          terminal = {outcome: 'fault', fault: {kind: 'control_callback_window_start_outside_observer_slack',
            callback_window_start_ms: callbackWindowStartMs, requested_end_ms: wallDeadline,
            safety_deadline_ms: safetyDeadline}, end_at_ms: now};
          break;
        }
        hardStop = await scheduleHeavyStop(heavy, beforeLoad.time_origin + wallDeadline);
      } else if (now >= safetyDeadline) {
        terminal = {outcome: 'fault', fault: {kind: 'control_callback_window_start_not_observed',
          safety_deadline_ms: safetyDeadline}, end_at_ms: now};
        break;
      }
    }
    if (callbackWindowStartMs !== null && now >= wallDeadline) {
      terminal = {outcome: 'window_complete', end_at_ms: wallDeadline}; break;
    }
    await page.waitForTimeout(15);
  }
  if (!terminal) terminal = {outcome: 'window_timeout', end_at_ms: await page.evaluate(() => performance.now())};
  const stopped = await stopMeasuredGpu(heavy);
  gpuOn = false;
  const stoppedDurationStats = summarizeObservedBatchDurations(stopped.batchDurations);
  const loadValidationErrors = [];
  if (!finite(stopped.actual_gpu_load_window_ms) || stopped.state.on !== false)
    loadValidationErrors.push('gpu_workload_stop_unverified');
  if (finite(stopped.actual_gpu_load_window_ms) && stopped.actual_gpu_load_window_ms <= 0)
    loadValidationErrors.push('gpu_load_window_duration_not_positive');
  if (!stoppedDurationStats || stopped.gpuObservation?.window_batches !== stopped.batchDurations.length)
    loadValidationErrors.push('gpu_batch_duration_samples_missing_or_mismatched');
  if (!Array.isArray(stopped.gpuObservation?.errors) || stopped.gpuObservation.errors.length)
    loadValidationErrors.push('gpu_errors_observed');
  if (stopped.gpuObservation?.on !== false) loadValidationErrors.push('gpu_workload_still_on');
  if (finite(stopped.actual_gpu_load_window_ms) &&
      stopped.actual_gpu_load_window_ms > MAX_OBSERVED_GPU_WINDOW_MS)
    loadValidationErrors.push('gpu_window_observer_slack_exceeded');
  if (loadValidationErrors.length)
    terminal = {outcome: 'fault', fault: {kind: `${label}_measured_gpu_window_invalid`,
      errors: loadValidationErrors, actual_gpu_load_window_ms: stopped.actual_gpu_load_window_ms,
      requested_gpu_load_window_ms: SUSTAINED_WINDOW_MS,
      maximum_observed_gpu_window_ms: MAX_OBSERVED_GPU_WINDOW_MS,
      observer_overshoot_ms: finite(stopped.actual_gpu_load_window_ms)
        ? Math.max(0, stopped.actual_gpu_load_window_ms - SUSTAINED_WINDOW_MS) : null,
      observed_batch_count: stopped.gpuObservation?.window_batches ?? null,
      retained_batch_duration_count: stopped.batchDurations.length,
      gpu_errors: stopped.gpuObservation?.errors ?? null}, end_at_ms: terminal.end_at_ms};
  end = await markPauseTraceBoundary(page);
  if (end.status !== 'marked') throw Error('Timing callback window could not be closed');
  const finalBatch = await readPauseTraceRows(page, nextRow);
  if (finalBatch.status === 'read') capturedRows.push(...finalBatch.rows);
  else throw Error('Timing callback rows could not be finalized');
  const measuredStartMs = callbackWindowStartMs ?? loadStartMs;
  const boundedWindowEndMs = Number.isFinite(wallDeadline) ? wallDeadline : safetyDeadline;
  const logicalEndMs = Math.min(terminal.end_at_ms ?? end.at_ms, boundedWindowEndMs);
  if (finalBatch.capture_errors || finalBatch.dropped || finalBatch.incident_overflow || report.browser_errors.length) {
    terminal = {outcome: 'fault', fault: {kind: 'final_capture_telemetry_fault',
      capture_errors: finalBatch.capture_errors, dropped: finalBatch.dropped,
      incident_overflow: finalBatch.incident_overflow, browser_errors: report.browser_errors.slice()},
    native: terminal.native};
  } else if (missingTelemetryFinding(capturedRows, measuredStartMs, logicalEndMs)) {
    terminal = {outcome: 'fault', fault: missingTelemetryFinding(capturedRows, measuredStartMs, logicalEndMs)};
  } else if (preparationFinding(capturedRows, finalBatch.incidents, measuredStartMs, logicalEndMs)) {
    terminal = {outcome: 'preparation', preparation: preparationFinding(capturedRows, finalBatch.incidents,
      measuredStartMs, logicalEndMs)};
  }

  const rowsRead = {status: 'read', rows: capturedRows, incidents: finalBatch.incidents ?? [],
    captureErrors: finalBatch.capture_errors ?? null, dropped: finalBatch.dropped ?? null,
    incidentOverflow: finalBatch.incident_overflow ?? null};
  const durationMs = label === '600_treatment' && terminal.outcome === 'timing_pause' ? 0 : SUSTAINED_WINDOW_MS;
  const assessment = assessSustainedWindow({rows: rowsRead.rows, incidents: rowsRead.incidents,
    startMs: measuredStartMs, endMs: logicalEndMs, durationMs,
    captureErrors: rowsRead.captureErrors ?? 0, dropped: rowsRead.dropped ?? 0,
    incidentOverflow: rowsRead.incidentOverflow ?? 0, browserErrors: report.browser_errors,
    pauseTerminal: label === '600_treatment' && terminal.outcome === 'timing_pause'});
  if (loadValidationErrors.length) {
    assessment.status = 'inconclusive';
    assessment.problems.push(...loadValidationErrors.map(error => `measured_gpu_window_${error}`));
  }
  const actualOvershoot = finite(stopped.actual_gpu_load_window_ms)
    ? stopped.actual_gpu_load_window_ms - SUSTAINED_WINDOW_MS : null;
  const gpuLoad = {requested_iterations: iterations, requested_gpu_load_window_ms: SUSTAINED_WINDOW_MS,
    load_started_at_ms: stopped.state.started_at_ms,
    load_enable_response_at_ms: enabled.at_ms,
    actual_gpu_load_window_ms: stopped.actual_gpu_load_window_ms,
    gpu_load_actual_minus_requested_ms: actualOvershoot,
    gpu_load_observer_overshoot_ms: actualOvershoot === null ? null : Math.max(0, actualOvershoot),
    gpu_load_early_stop_ms: actualOvershoot === null ? null : Math.max(0, -actualOvershoot),
    gpu_deadline: hardStop, gpu_stop_state: stopped.state,
    gpu_observation: stopped.gpuObservation, batch_durations_ms: stopped.batchDurations,
    batch_duration_stats_ms: stoppedDurationStats,
    batch_duration_semantics: 'observed WebGL draw plus synchronous readback registration-to-delivery wall duration; not pure GPU execution time'};
  return {label, requested_iterations: iterations, enabled,
    start: {...start, callback_window_start_at_ms: callbackWindowStartMs},
    end: {...end, logical_end_at_ms: logicalEndMs}, elapsed_ms: logicalEndMs - measuredStartMs,
    gpu_load: gpuLoad,
    terminal, capture_health: {capture_errors: rowsRead.captureErrors, dropped: rowsRead.dropped,
      incident_overflow: rowsRead.incidentOverflow}, assessment};
}

async function calibration(iterations) {
  const result = {iterations, requested_window_ms: 500, actual_window_ms: null,
    enabled: null, disabled: null, aggregate: null, batch_durations_ms: [],
    batch_duration_stats_ms: null, validation_errors: [],
    batch_duration_semantics: 'observed WebGL draw plus synchronous readback registration-to-delivery wall duration; not pure GPU execution time'};
  let enableError = null;
  gpuOn = true;
  try {
    result.enabled = await setHeavyGpu(heavy, true, iterations);
    await heavy.page.waitForTimeout(500);
  } catch (error) {
    enableError = String(error?.message || error);
    result.validation_errors.push(`enable_or_wait_failed: ${enableError}`);
  } finally {
    try {
      result.disabled = await setHeavyGpu(heavy, false, iterations);
      gpuOn = false;
      if (result.enabled && finite(result.disabled.at_ms) && finite(result.enabled.at_ms))
        result.actual_window_ms = result.disabled.at_ms - result.enabled.at_ms;
    } catch (error) {
      const message = String(error?.message || error);
      result.validation_errors.push(`disable_failed: ${message}`);
      report.gpu_cleanup_error ||= message;
    }
  }
  try {
    result.aggregate = await readHeavyGpu(heavy);
    result.batch_durations_ms = await heavy.page.evaluate(() => window.__gpuLoad.durations.slice());
    result.batch_duration_stats_ms = summarizeObservedBatchDurations(result.batch_durations_ms);
  } catch (error) {
    result.validation_errors.push(`observation_read_failed: ${String(error?.message || error)}`);
  }
  if (enableError) result.validation_errors.push('calibration_not_completed');
  if (result.aggregate?.errors?.length) result.validation_errors.push('gpu_errors_observed');
  if (result.aggregate?.on !== false) result.validation_errors.push('gpu_load_not_confirmed_off');
  if (!finite(result.actual_window_ms) || result.actual_window_ms <= 0)
    result.validation_errors.push('calibration_window_duration_missing');
  if (!result.batch_duration_stats_ms || result.aggregate?.window_batches !== result.batch_durations_ms.length)
    result.validation_errors.push('batch_duration_samples_missing_or_mismatched');
  if (gpuOn) result.validation_errors.push('gpu_load_cleanup_unverified');
  result.valid = result.validation_errors.length === 0;
  return result;
}

try {
  const sourceBytes = await fs.readFile(values.recipe);
  const sidecarBytes = await fs.readFile(`${values.recipe}.json`);
  const sidecar = JSON.parse(sidecarBytes);
  report.recipe_header = recipeHeader(sourceBytes);
  const recipeSha = sha(sourceBytes), sidecarSha = sha(sidecarBytes);
  if (sidecar.fixture_sha256 !== recipeSha || sidecar.recipe_format !== 'MWRC v4' ||
      sidecar.scope !== 'synthetic initial-context diagnostic; no original-match identity claim')
    throw Error('Recipe sidecar does not bind the source-backed synthetic MWRC v4 input');
  const baselineBytes = await fs.readFile(values['baseline-report']);
  const baseline = JSON.parse(baselineBytes);
  const discIdentity = await inputIdentity(values.disc);
  const recipeIdentity = {bytes: sourceBytes.length, sha256: recipeSha};
  if (JSON.stringify(discIdentity) !== JSON.stringify(baseline.inputs?.disc) ||
      JSON.stringify(recipeIdentity) !== JSON.stringify(baseline.inputs?.recipe) ||
      sidecarSha !== baseline.inputs?.recipe_sidecar_sha256)
    throw Error('Disc or fixture identity differs from the authorized prior H1 timing report');
  report.inputs = {disc: discIdentity, recipe: recipeIdentity, recipe_sidecar_sha256: sidecarSha};
  report.source = await sourceIdentity();
  const buildDirectory = path.resolve(values['build-dir']);
  const prospectiveBytes = await fs.readFile(values['prospective-build-manifest']);
  const prospectiveManifest = JSON.parse(prospectiveBytes);
  const localArtifacts = await localArtifactMap(buildDirectory);
  const prospectiveValidation = validateProspectiveBuildManifest(prospectiveManifest, {
    source: report.source, buildDirectory, localArtifacts, expectedNames: browserArtifactNames});
  report.prospective_build_binding = {manifest_path: path.resolve(values['prospective-build-manifest']),
    manifest_sha256: sha(prospectiveBytes), schema: prospectiveManifest.schema ?? null,
    binding_type: prospectiveManifest.binding_type ?? null,
    inventory_artifact_count: prospectiveManifest.inventory?.artifact_count ?? null,
    inventory_artifact_map_sha256: prospectiveManifest.inventory?.artifact_map_sha256 ?? null,
    references: prospectiveManifest.references ?? null,
    limitations: prospectiveManifest.limitations ?? null,
    validation: prospectiveValidation};
  if (!prospectiveValidation.valid)
    throw Error(`Prospective build manifest rejected: ${prospectiveValidation.problems.join(', ')}`);
  const priorArtifacts = baseline.build_artifacts_before ?? {};
  const historicalComparison = compareArtifactMaps(priorArtifacts, prospectiveManifest.inventory.artifacts);
  report.baseline_binding = {report_sha256: sha(baselineBytes),
    captured_source_commit: baseline.source?.commit ?? null,
    historical_artifact_count: Object.keys(priorArtifacts).length,
    historical_artifact_map_sha256: artifactMapDigest(priorArtifacts),
    current_vs_historical_artifact_map_matches: historicalComparison.matches,
    historical_artifact_map_differences: historicalComparison.differences,
    scope: 'prior H1 report binds disc and fixture identity only; its artifact map is historical and is not the prospective build gate'};
  report.baseline_recipe_sha256 = baseline.inputs?.recipe?.sha256 ?? null;
  report.protocol_bounds = {phase_timeout_ms: phaseTimeoutMs,
    preparation_timeout_ms: preparationTimeoutMs, load_window_timeout_ms: windowTimeoutMs,
    calibration_window_ms_each: 500, observation_poll_interval_ms: 15};
  report.build_artifacts_before = await artifactMap(buildDirectory);
  report.build_artifact_map_sha256 = artifactMapDigest(report.build_artifacts_before);
  const beforeManifestComparison = compareArtifactMaps(prospectiveManifest.inventory.artifacts,
    report.build_artifacts_before);
  if (!beforeManifestComparison.matches)
    throw Error(`Fresh pre-capture HTTP artifact map differs from prospective manifest: ${JSON.stringify(beforeManifestComparison.differences)}`);

  const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(values.playwright);
  browser = await chromium.launch({...browserLaunchOptions(launchOptions, {timeout: phaseTimeoutMs}), headless: true});
  report.browser = {executable: path.basename(browserPath), playwright: playwrightPath, version: browser.version()};
  report.machine = {hostname: os.hostname(), platform: process.platform, arch: process.arch};
  page = await browser.newPage({viewport: {width: 900, height: 700}, deviceScaleFactor: 1});
  page.setDefaultTimeout(phaseTimeoutMs);
  page.on('pageerror', pageError);
  page.on('console', consoleError);
  const response = await page.goto(baseUrl.href, {waitUntil: 'domcontentloaded', timeout: phaseTimeoutMs});
  if (response?.status() !== 200 || response.headers()['cross-origin-opener-policy'] !== 'same-origin' ||
      response.headers()['cross-origin-embedder-policy'] !== 'require-corp')
    throw Error('Runtime did not load over isolated loopback HTTP');
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: phaseTimeoutMs});
  await driver.waitForImport();
  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  runtimeReady = true;
  await page.waitForFunction(() => !!window.__meleeWebStagingRingStatus, null, {timeout: phaseTimeoutMs});
  report.ring_status = await page.evaluate(() => window.__meleeWebStagingRingStatus);
  report.ring_status_validation = validateSelectedRingStatus(report.ring_status, slots);
  if (!report.ring_status_validation.valid)
    throw Error(`Requested ring-${slots} identity rejected: ${report.ring_status_validation.problems.join(', ')}`);
  report.memory_before = await memorySnapshot(page);
  heavy = await createHeavyGpuPage(browser);
  report.gpu_config = heavy.config;
  report.gpu_ready = heavy.ready;
  const mainClockBefore = await page.evaluate(() => performance.timeOrigin + performance.now());
  const gpuClock = await heavy.page.evaluate(() => performance.timeOrigin + performance.now());
  const mainClockAfter = await page.evaluate(() => performance.timeOrigin + performance.now());
  report.cross_page_clock_alignment = {main_before_epoch_ms: mainClockBefore,
    gpu_epoch_ms: gpuClock, main_after_epoch_ms: mainClockAfter,
    midpoint_offset_ms: gpuClock - (mainClockBefore + mainClockAfter) / 2,
    measurement_bracket_ms: mainClockAfter - mainClockBefore};
  report.capture_start = await installPauseTraceCapture(page, null);
  traceInstalled = true;
  if (!report.capture_start.timing_hook_present || !report.capture_start.sample_hook_present ||
      !report.capture_start.incident_hook_present || report.capture_start.stall_schedule_supported)
    throw Error('No-stall timing/incident observation hooks are not available');
  await page.locator('summary').filter({hasText: 'Diagnostics'}).click();
  await page.locator('#retail-replay-mode').selectOption('performance');
  await page.locator('#retail-replay-file').setInputFiles(values.recipe);
  await page.waitForFunction(() => {
    const button = document.querySelector('#retail-replay-start');
    return button && !button.disabled;
  }, null, {timeout: phaseTimeoutMs});
  await page.evaluate(() => {window.lastRetailReplayReport = null;});

  // Fixed calibration happens while the replay clock is stopped.
  const calibration300 = await calibration(300);
  report.calibration.push(calibration300);
  if (!calibration300.valid) throw Error(`GPU calibration 300 failed: ${calibration300.validation_errors.join(', ')}`);
  const calibration600 = await calibration(600);
  report.calibration.push(calibration600);
  if (!calibration600.valid) throw Error(`GPU calibration 600 failed: ${calibration600.validation_errors.join(', ')}`);
  const calibrated = await readHeavyGpu(heavy);
  if (calibrated.on) throw Error('GPU calibration load remained enabled before replay start');
  report.calibration_identity = {iterations: report.calibration.map(item => item.iterations),
    fixed_before_game_clock: true};

  await page.locator('#retail-replay-start').click();
  const preparationDeadline = Date.now() + preparationTimeoutMs;
  let readyObservation = null;
  while (Date.now() < preparationDeadline) {
    const status = await readPauseTraceStatus(page);
    const health = checkCaptureHealth(status, report.browser_errors);
    if (health) throw Error(`Preparation fault: ${health.kind}: ${health.message}`);
    if (status.native_message?.startsWith('Paused after a timing disruption') && status.source_running === 0)
      throw Error('Replay paused before the 300-work control window began');
    const observation = await nativeSnapshot(page);
    if (observation.source_running === 1 && typeof observation.original_match_frame === 'number' &&
        observation.original_match_frame > 0 && observation.match?.ready === true) {
      readyObservation = observation;
      break;
    }
    if (status.replay_report) throw Error('1800-frame fixture completed before active gameplay readiness');
    await page.waitForTimeout(50);
  }
  if (!readyObservation) throw Error('Bounded preparation period ended before active gameplay was ready');
  report.gameplay_ready = readyObservation;
  report.control_window = await runLoadWindow({iterations: 300, page, heavy, label: '300_control'});
  if (report.control_window.terminal.outcome === 'window_complete' &&
      report.control_window.assessment.status === 'clean') {
    report.treatment_window = await runLoadWindow({iterations: 600, page, heavy, label: '600_treatment'});
  }
} catch (error) {
  report.failure = String(error?.stack || error);
  report.result = 'inconclusive';
  report.decision = 'inspect_immediately';
  process.exitCode = 1;
} finally {
  // Stop the GPU workload, snapshot small native state, unload the source owner,
  // then export the genuine incident recorder and read the larger trace.
  if (heavy && gpuOn) {
    try {report.gpu_cleanup = await setHeavyGpu(heavy, false); gpuOn = false;}
    catch (error) {report.gpu_cleanup_error = String(error?.message || error); process.exitCode = 1;}
  }
  if (heavy) {
    try {report.gpu_final = await readHeavyGpu(heavy);}
    catch (error) {report.gpu_final_error = String(error?.message || error);}
  }
  if (page && runtimeReady && !unloaded) {
    try {
      report.native_before_unload = await nativeSnapshot(page);
      report.ring_status_before_unload = await page.evaluate(() => window.__meleeWebStagingRingStatus ?? null);
      report.ring_status_before_unload_validation = validateSelectedRingStatus(report.ring_status_before_unload, slots);
      if (!report.ring_status_before_unload_validation.valid)
        throw Error(`Pre-unload ring identity rejected: ${report.ring_status_before_unload_validation.problems.join(', ')}`);
      report.memory_before_unload = await memorySnapshot(page);
    } catch (error) {report.pre_unload_snapshot_error = String(error?.message || error);}
    try {await driver?.unload(); unloaded = true; report.unload = {attempted: true, completed: true};}
    catch (error) {
      unloaded = true;
      report.unload = {attempted: true, completed: false, error: String(error?.message || error)};
      process.exitCode = 1;
    }
  }
  if (traceInstalled && page && !page.isClosed()) {
    try {report.runtime_incident_recorder = await readRetainedPauseDiagnostics(page,
      report.treatment_window?.terminal?.outcome === 'timing_pause');}
    catch (error) {report.recorder_read_error = String(error?.message || error); process.exitCode = 1;}
    try {report.capture_status_after_unload = await readPauseTraceStatus(page, {readNative: false});}
    catch (error) {report.capture_status_read_error = String(error?.message || error);}
    try {report.capture = await readPauseTraceCapture(page,
      report.treatment_window?.terminal?.outcome ?? report.control_window?.terminal?.outcome ?? 'failure');}
    catch (error) {report.capture_read_error = String(error?.message || error); process.exitCode = 1;}
  }
  try {driver?.dispose();}
  catch (error) {report.driver_dispose_error = String(error?.message || error); process.exitCode = 1;}
  try {await browser?.close();}
  catch (error) {report.close_error = String(error?.message || error); process.exitCode = 1;}
  try {
    const buildDirectory = path.resolve(values['build-dir']);
    if (browser && report.build_artifacts_before) {
      report.build_artifacts_after = await artifactMap(buildDirectory);
      report.build_artifact_map_after_sha256 = artifactMapDigest(report.build_artifacts_after);
      if (!compareArtifactMaps(report.build_artifacts_before, report.build_artifacts_after).matches) {
        report.build_changed_during_capture = true;
        report.failure ||= 'Served Release artifact map changed during the capture';
        process.exitCode = 1;
      }
      const postManifestBytes = await fs.readFile(values['prospective-build-manifest']);
      if (sha(postManifestBytes) !== report.prospective_build_binding?.manifest_sha256) {
        report.failure ||= 'Prospective build manifest changed during the capture';
        process.exitCode = 1;
      }
      const postManifest = JSON.parse(postManifestBytes);
      const afterManifestComparison = compareArtifactMaps(postManifest.inventory?.artifacts,
        report.build_artifacts_after);
      report.prospective_build_binding.after_http_artifact_map = {
        matches_manifest: afterManifestComparison.matches,
        differences: afterManifestComparison.differences,
        artifact_map_sha256: report.build_artifact_map_after_sha256};
      if (!afterManifestComparison.matches) {
        report.failure ||= 'Fresh post-capture HTTP artifact map differs from prospective manifest';
        process.exitCode = 1;
      }
    }
  } catch (error) {
    report.artifact_postcheck_error = String(error?.message || error);
    process.exitCode = 1;
  }
  if (report.capture) {
    try {
      const captureBytes = Buffer.from(JSON.stringify(report.capture, null, 2) + '\n');
      await fs.writeFile(path.join(output, 'capture.json'), captureBytes);
      report.capture_file = {name: 'capture.json', bytes: captureBytes.length, sha256: sha(captureBytes)};
    } catch (error) {report.capture_file_error = String(error?.message || error); process.exitCode = 1;}
  }
  if (report.runtime_incident_recorder) {
    try {
      const recorderBytes = Buffer.from(JSON.stringify(report.runtime_incident_recorder, null, 2) + '\n');
      await fs.writeFile(path.join(output, 'recorder.json'), recorderBytes);
      report.recorder_file = {name: 'recorder.json', bytes: recorderBytes.length, sha256: sha(recorderBytes)};
    } catch (error) {report.recorder_file_error = String(error?.message || error); process.exitCode = 1;}
  }
  const integrityErrors = [];
  if (report.failure) integrityErrors.push('runner_failure');
  for (const key of ['gpu_cleanup_error', 'gpu_final_error', 'pre_unload_snapshot_error',
    'recorder_read_error', 'capture_status_read_error', 'capture_read_error',
    'driver_dispose_error', 'close_error', 'artifact_postcheck_error',
    'capture_file_error', 'recorder_file_error']) if (report[key]) integrityErrors.push(key);
  if (report.unload?.attempted && !report.unload.completed) integrityErrors.push('unload_incomplete');
  if (runtimeReady && (!report.unload?.attempted || !report.unload.completed))
    integrityErrors.push('required_runtime_unload_missing_or_incomplete');
  if (traceInstalled && (!report.capture_status_after_unload ||
      report.capture_status_after_unload.status !== 'installed'))
    integrityErrors.push('post_unload_capture_status_missing');
  if (traceInstalled && (!report.runtime_incident_recorder ||
      report.runtime_incident_recorder.read_api !== 'createRuntimeDiagnostics().exportRetained()' ||
      !Array.isArray(report.runtime_incident_recorder.retained_records)))
    integrityErrors.push('genuine_incident_recorder_export_missing');
  if (traceInstalled && (!report.capture || report.capture.status !== 'captured' ||
      !Array.isArray(report.capture.columns) || !Array.isArray(report.capture.table)))
    integrityErrors.push('large_trace_capture_missing');
  if (report.browser_errors.length) integrityErrors.push('browser_errors_observed');
  if (report.capture_status_after_unload?.runtime_error || report.capture_status_after_unload?.dialog_error)
    integrityErrors.push('post_unload_runtime_error');
  if (runtimeReady && (!report.native_before_unload || !report.ring_status_before_unload ||
      !report.memory_before_unload)) integrityErrors.push('required_pre_unload_snapshot_missing');
  if (runtimeReady && [report.native_before_unload?.message,
      report.native_before_unload?.original_match_frame,
      report.native_before_unload?.match].some(value => value && typeof value === 'object' && value.error))
    integrityErrors.push('pre_unload_native_snapshot_error');
  if (report.build_changed_during_capture) integrityErrors.push('served_artifact_map_changed');
  if (heavy && report.gpu_final?.on !== false) integrityErrors.push('gpu_workload_cleanup_missing_or_unverified');
  if (gpuOn) integrityErrors.push('gpu_cleanup_not_verified');
  if (report.capture_status_after_unload && (report.capture_status_after_unload.capture_errors ||
      report.capture_status_after_unload.dropped || report.capture_status_after_unload.incident_overflow))
    integrityErrors.push('capture_status_after_unload_incomplete');
  report.integrity_errors = integrityErrors;
  const runTerminal = report.treatment_window?.terminal ?? report.control_window?.terminal ?? null;
  const control = report.control_window?.assessment ?? null;
  const treatment = report.treatment_window?.assessment ?? null;
  const decision = resolveSustainedAttempt({integrityErrors, control, treatment,
    terminal: runTerminal, recorder: report.runtime_incident_recorder,
    gpuLoad: report.treatment_window?.gpu_load,
    stimulusValid: report.recipe_header?.frames === 1800 &&
      report.inputs?.recipe?.sha256 === report.baseline_recipe_sha256});
  report.decision_evidence = decision;
  report.result = decision.result;
  report.decision = decision.decision;
  if (!runTerminal && !report.failure)
    report.failure = 'No qualified load window reached a terminal outcome';
  if (decision.result === 'inconclusive') process.exitCode = 1;
  const reportBytes = Buffer.from(JSON.stringify(report, null, 2) + '\n');
  try {await fs.writeFile(path.join(output, 'report.json'), reportBytes);}
  catch (error) {report.report_file_error = String(error?.message || error); process.exitCode = 1;}
  console.log(JSON.stringify({result: report.result, decision: report.decision,
    control: report.control_window?.terminal?.outcome ?? null,
    treatment: report.treatment_window?.terminal?.outcome ?? null,
    fixture_sha256: report.inputs?.recipe?.sha256 ?? null,
    failure: report.failure?.split('\n')[0] ?? null}));
}
