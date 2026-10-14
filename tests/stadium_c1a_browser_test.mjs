#!/usr/bin/env node
/* Browser exercise for the private C1a source-menu preparation checkpoint.
 * The native archive request and match/stage lifecycle remain out of scope. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawn, execFileSync} from 'node:child_process';
import {createWriteStream} from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import net from 'node:net';
import {finished} from 'node:stream/promises';
import {parseArgs} from 'node:util';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {validateBrowserArtifactNames} from '../scripts/browser_artifact_inventory.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {firstSssAudioAccounting} from './stadium_first_css_stream_compare.mjs';
import {createCssSssTransitionCapture, CSS_SSS_TRANSITION_LIMITS,
  cssReadinessEvidence, cssSssOutcome, hasCssMenuReadiness} from './stadium_c1a_css_sss_reducer.mjs';
import {validateMatchedSetupCfRequest, validateMatchedSetupCfPreflight,
  validateMatchedSetupCfIdentities, matchedSetupCfRuntimeFailure, PREFIX_V2_SCOPE,
  trackMatchedSetupCfExplicitStep, matchedSetupCfNativeAccessAllowed,
  dispatchMatchedSetupCfNativeEvaluation, guardMatchedSetupCfNativeCleanup}
  from './stadium_matched_setup_cf_runner_gate.mjs';
import {elapsedMs, measureAsync, measureSync, monotonicNowNs, roundMs,
  summarizeFailedActionSamples, summarizeStageEvents, summarizeTimingValues}
  from './stadium_c3_timing.mjs';
import {createStadiumC3ReportCheckpoint} from './stadium_c3_report_checkpoint.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {values} = parseArgs({options: Object.fromEntries(
  ['build', 'disc', 'disc-sha256', 'out', 'playwright', 'preflight', 'source-revision',
    'first-css-context', 'first-css-consumed-pad', 'first-css-return-expected',
    'first-css-tick-expected', 'first-css-draw-expected',
    'first-css-postdraw-input', 'first-css-postdraw-expected',
    'first-sss-pair-expected', 'first-sss-tick-expected', 'first-sss-tick-input',
    'first-sss-draw-expected', 'first-sss-prefix-expected', 'first-sss-prefix-input',
    'first-sss-vs-entry-expected', 'first-sss-vs-setup-expected',
    'first-sss-vs-setup-cf-packet', 'first-sss-vs-setup-cf-assembly']
    .map(name => [name, {type: 'string'}]).concat([
      ['css-sss-reducer', {type: 'boolean', default: false}],
      ['first-css-browser-draw', {type: 'boolean', default: false}],
      ['first-css-browser-stream', {type: 'boolean', default: false}],
      ['first-css-browser-final-draw', {type: 'boolean', default: false}],
      ['first-sss-constructor-pair', {type: 'boolean', default: false}],
      ['first-sss-consumed-pad-tick', {type: 'boolean', default: false}],
      ['first-sss-draw', {type: 'boolean', default: false}],
      ['first-sss-prefix', {type: 'boolean', default: false}],
      ['first-sss-vs-entry', {type: 'boolean', default: false}],
      ['first-sss-vs-setup', {type: 'boolean', default: false}],
      ['first-sss-vs-setup-cf', {type: 'boolean', default: false}],
      ['first-sss-vs-setup-cf-explicit-step', {type: 'boolean', default: false}],
      ['first-sss-vs-setup-text-tail', {type: 'boolean', default: false}],
    ])), strict: true});
const cssSssReducer = values['css-sss-reducer'];
const firstSssVsSetup = values['first-sss-vs-setup'];
const firstSssVsSetupTextTail = values['first-sss-vs-setup-text-tail'];
const firstSssVsSetupCfExplicitStep = values['first-sss-vs-setup-cf-explicit-step'];
const firstSssVsSetupCf = validateMatchedSetupCfRequest({
  enabled: values['first-sss-vs-setup-cf'], explicitStep: firstSssVsSetupCfExplicitStep,
  setup: firstSssVsSetup,
  textTail: firstSssVsSetupTextTail,
  packetPath: values['first-sss-vs-setup-cf-packet'] || null,
  assemblyPath: values['first-sss-vs-setup-cf-assembly'] || null,
});
const firstSssVsEntry = values['first-sss-vs-entry'] || firstSssVsSetup;
const firstSssPrefix = values['first-sss-prefix'] || firstSssVsEntry;
const firstSssDraw = values['first-sss-draw'] || firstSssPrefix;
const firstSssConsumedPadTick = values['first-sss-consumed-pad-tick'] || firstSssDraw || firstSssPrefix;
const firstSssConstructorPair = values['first-sss-constructor-pair'] || firstSssConsumedPadTick;
const firstCssBrowserFinalDraw = values['first-css-browser-final-draw'] || firstSssConstructorPair;
const firstCssBrowserStream = values['first-css-browser-stream'] || firstCssBrowserFinalDraw;
const firstCssBrowserDraw = values['first-css-browser-draw'] || firstCssBrowserStream;
if ((firstSssVsSetup && values['first-sss-vs-entry']) ||
    (firstSssVsSetupTextTail && !firstSssVsSetup) ||
    (firstSssVsSetup && !values['first-sss-vs-setup-expected']) ||
    (!firstSssVsSetup && values['first-sss-vs-setup-expected']) ||
    (firstSssVsEntry && ['css-sss-reducer', 'first-css-browser-draw',
      'first-css-browser-stream', 'first-css-browser-final-draw', 'first-sss-constructor-pair',
      'first-sss-consumed-pad-tick', 'first-sss-draw', 'first-sss-prefix'].some(key => values[key])) ||
    (firstSssVsEntry && !values['first-sss-vs-entry-expected']) ||
    (!firstSssVsEntry && values['first-sss-vs-entry-expected']) ||
    (cssSssReducer && firstCssBrowserDraw) ||
    (values['first-css-browser-draw'] && firstCssBrowserStream) ||
    (values['first-css-browser-stream'] && values['first-css-browser-final-draw']) ||
    (firstSssConstructorPair && !values['first-sss-pair-expected']) ||
    (!firstSssConstructorPair && values['first-sss-pair-expected']) ||
    (values['first-sss-draw'] && firstSssPrefix) ||
    ((firstSssDraw || firstSssPrefix) &&
      (values['first-sss-constructor-pair'] || values['first-sss-consumed-pad-tick'])) ||
    (firstSssConsumedPadTick && (!values['first-sss-tick-expected'] ||
      !values['first-sss-tick-input'])) ||
    (!firstSssConsumedPadTick && (values['first-sss-tick-expected'] ||
      values['first-sss-tick-input'])) ||
    (firstSssDraw && !values['first-sss-draw-expected']) ||
    (firstSssPrefix && (!values['first-sss-draw-expected'] ||
      !values['first-sss-prefix-expected'] || !values['first-sss-prefix-input'])) ||
    (!firstSssPrefix && (values['first-sss-prefix-expected'] ||
      values['first-sss-prefix-input'])) ||
    (!firstSssDraw && values['first-sss-draw-expected']))
  throw Error('Choose one private C1a diagnostic route');
const LIMITS = Object.freeze({
  serverStartMs: 10000,
  browserLaunchMs: 30000,
  pageStartMs: 15000,
  discImportMs: 60000,
  sourceTransitionMs: CSS_SSS_TRANSITION_LIMITS.transitionMs,
  stageDriveMs: 30000,
  manifestHandoffMs: 20000,
  unloadMs: 15000,
  cleanupMs: 5000,
  cleanupTotalMs: cssSssReducer ? 5000 : 15000,
  captureWorkMs: cssSssReducer ? 95000 : 150000,
  stageDriveFrames: 180,
});
for (const name of ['build', 'disc', 'disc-sha256', 'out', 'playwright', 'preflight', 'source-revision'])
  if (!values[name]) throw Error(`Missing --${name}`);

const preflightPath = path.resolve(values.preflight);
const preflightBytes = await fs.readFile(preflightPath);
const preflight = JSON.parse(preflightBytes.toString('utf8'));
const matchedSetupCfPreflight = validateMatchedSetupCfPreflight(preflight, {
  enabled: firstSssVsSetupCf,
  explicitStep: firstSssVsSetupCfExplicitStep,
  packetPath: values['first-sss-vs-setup-cf-packet']
    ? path.resolve(values['first-sss-vs-setup-cf-packet']) : null,
  assemblyPath: values['first-sss-vs-setup-cf-assembly']
    ? path.resolve(values['first-sss-vs-setup-cf-assembly']) : null,
  helperPath: path.join(ROOT, 'tests/stadium_matched_setup_cf_consumer.mjs'),
});
const firstSssVsSetupCfV2 = firstSssVsSetupCf &&
  matchedSetupCfPreflight.scope === PREFIX_V2_SCOPE;
const matchedSetupCfTiming = firstSssVsSetupCfV2 ? {
  actionSamples: [], scheduledActions: null, actionLoopActive: false, actionLoopStartedNs: null,
  actionLoopMs: null, globalCaptureDeadlineRemainingAtActionLoopStartMs: null,
  queueRpcMsByIndex: new Map(), approvalRpcMsByIndex: new Map(),
  persistence: [], currentActionSample: null,
  explicitStep: firstSssVsSetupCfExplicitStep,
} : null;
const finishMatchedSetupCfActionLoop = () => {
  if (!matchedSetupCfTiming?.actionLoopActive) return;
  matchedSetupCfTiming.actionLoopMs = elapsedMs(matchedSetupCfTiming.actionLoopStartedNs);
  matchedSetupCfTiming.actionLoopActive = false;
};
const timedActionAsync = async (sample, stage, field, operation, after = null) => {
  try {
    return await measureAsync(sample.stage_events, stage, operation);
  } finally {
    const event = sample.stage_events.at(-1);
    if (event?.stage === stage) {
      if (field) sample[field] = event.elapsed_ms;
      if (after) after(event);
    }
  }
};
const timedActionSync = (sample, stage, field, operation, after = null) => {
  try {
    return measureSync(sample.stage_events, stage, operation);
  } finally {
    const event = sample.stage_events.at(-1);
    if (event?.stage === stage) {
      if (field) sample[field] = event.elapsed_ms;
      if (after) after(event);
    }
  }
};
const summarizeMatchedSetupCfTiming = timing => {
  const fields = ['action_wall_ms', 'queue_ms', 'queue_rpc_ms', 'post_submit_receipt_ms',
    'receipt_read_ms', 'first_read_ms', 'poll_wait_ms', 'explicit_step_ms',
    'before_diagnostics_ms', 'before_snapshot_ms', 'compare_ms', 'accept_ms', 'approval_rpc_ms',
    'after_diagnostics_ms', 'after_snapshot_ms'];
  const summarizeRows = rows => ({
    count: rows.length,
    completed_count: rows.filter(row => row.completed).length,
    failed_count: rows.filter(row => !row.completed).length,
    first_read_complete_count: rows.filter(row => row.first_read_complete === true).length,
    first_read_incomplete_count: rows.filter(row => row.first_read_complete === false).length,
    receipt_read_count: rows.reduce((sum, row) => sum + row.receipt_reads, 0),
    poll_wait_count: rows.reduce((sum, row) => sum + row.poll_waits, 0),
    timings_ms: Object.fromEntries(fields.map(field => [field,
      summarizeTimingValues(rows.map(row => row[field]))])),
  });
  const samples = timing.actionSamples;
  const phaseActionKeys = [...new Set(samples.map(row => `${row.phase}_${row.action}`))];
  const persistence = timing.persistence;
  const byteValues = persistence.map(row => row.serialized_bytes).filter(Number.isFinite);
  return {
    schema: 'melee-web-stadium-c3-host-timing-v2',
    scope: 'Host-observed timing for the selected run2 prefix; no native or GPU timers.',
    clock: 'process.hrtime.bigint',
    action_schedule_limit_ms: LIMITS.captureWorkMs,
    global_capture_deadline_remaining_at_action_loop_start_ms:
      timing.globalCaptureDeadlineRemainingAtActionLoopStartMs === null ? null
        : roundMs(timing.globalCaptureDeadlineRemainingAtActionLoopStartMs),
    effective_action_time_ceiling_ms: timing.globalCaptureDeadlineRemainingAtActionLoopStartMs === null
      ? null : roundMs(Math.max(0, Math.min(LIMITS.captureWorkMs,
        timing.globalCaptureDeadlineRemainingAtActionLoopStartMs))),
    scheduled_actions: timing.scheduledActions,
    action_loop_ms: timing.actionLoopMs === null ? null : roundMs(timing.actionLoopMs),
    actions: {
      all: summarizeRows(samples),
      by_kind: Object.fromEntries(['tick', 'draw'].map(kind => [kind,
        summarizeRows(samples.filter(row => row.action === kind))])),
      by_phase_action: Object.fromEntries(phaseActionKeys.map(key => [key,
        summarizeRows(samples.filter(row => `${row.phase}_${row.action}` === key))])),
      stage_timings: summarizeStageEvents(samples),
      failed_samples: summarizeFailedActionSamples(samples),
    },
    report_persistence: {
      attempted_count: persistence.length,
      completed_count: persistence.filter(row => row.completed).length,
      failed_count: persistence.filter(row => !row.completed).length,
      serialized_bytes: byteValues.length
        ? byteValues.reduce((sum, value) => sum + value, 0) : null,
      timings_ms: {
        serialize: summarizeTimingValues(persistence.map(row => row.serialize_ms)),
        file_write: summarizeTimingValues(persistence.map(row => row.write_ms)),
        rename: summarizeTimingValues(persistence.map(row => row.rename_ms)),
        total: summarizeTimingValues(persistence.map(row => row.total_ms)),
      },
    },
    interval_semantics: {
      interpretation: 'Measurements are inclusive and nested. Do not add category totals or infer native/source time from these host intervals.',
      nesting: {
        queue_rpc_ms: 'subset of queue_ms; queue_ms measures driver.submit from call through resolution/rejection.',
        post_submit_receipt_ms: 'starts only after driver.submit resolves and ends at the matching receipt or a failed read/wait/check; it excludes queue_ms and includes the explicit native service call when enabled, receipt reads, and any legacy conditional waits.',
        explicit_step_ms: 'the explicit async Module.ccall service call, nested in post_submit_receipt_ms and action_wall_ms; it measures host-observed call completion, not a GPU queue fence.',
        receipt_read_ms: 'sum of attempted readFirstSssVsEntryObservation waits, including a rejected read; reads are inside post_submit_receipt_ms when that interval has started.',
        poll_wait_ms: 'sum of attempted existing 50 ms waits, including a rejected wait; waits are inside post_submit_receipt_ms.',
        approval_rpc_ms: 'subset of accept_ms; accept_ms measures driver.accept through resolution/rejection.',
        before_snapshot_ms: 'subset of before_diagnostics_ms; diagnostic time also includes the existing report persistence and checks.',
        after_snapshot_ms: 'subset of after_diagnostics_ms; diagnostic time also includes the existing report persistence and checks.',
        report_persistence: 'serialization, write, rename, and total are nested within each persistence call; action-time persistence calls are inside action_wall_ms, and calls made by diagnostics are also inside their diagnostic interval.',
        action_wall_ms: 'encloses the complete scheduled action, including nested intervals and local comparisons/checks.',
      },
      host_timing_limit: 'The clock measures Node host elapsed time around existing calls. It does not isolate source scheduling, native execution, browser scheduling, or GPU work.',
    },
  };
};
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: false});

const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const sortedJsonValue = value => Array.isArray(value) ? value.map(sortedJsonValue) :
  value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort()
    .map(key => [key, sortedJsonValue(value[key])])) : value;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const timeout = (promise, ms, label) => {
  let timer;
  return Promise.race([
    Promise.resolve(promise),
    new Promise((_, reject) => { timer = setTimeout(() => reject(Error(`${label} exceeded ${ms} ms`)), ms); }),
  ]).finally(() => clearTimeout(timer));
};
const errorText = error => String(error?.stack || error?.message || error).slice(0, 6000);
const summarizeStorage = state => ({cookies: state.cookies.length,
  origins: state.origins.map(origin => ({origin: origin.origin,
    local_storage_entries: origin.localStorage.length}))});
const expectedDiscSha256 = 'b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c';
const expectedBuild = path.join(ROOT, 'build/browser-stadium-c1a-release');
const defaultBuild = path.join(ROOT, 'build/browser-release');
const build = path.resolve(values.build);
const deadline = Date.now() + LIMITS.captureWorkMs;
const remaining = (cap = LIMITS.captureWorkMs) => {
  const ms = Math.min(cap, deadline - Date.now());
  if (ms <= 0) throw Error('C1a browser run wall-time bound exhausted');
  return ms;
};
const hashFile = async file => digest(await fs.readFile(file));
const inventoryOutput = path.join(output, 'report.json');

const report = {
  schema: 'melee-web-stadium-c1a-browser-v1',
  result: 'fail',
  execution_state: 'running',
  node_pid: process.pid,
  browser_process_info: [],
  source_revision: values['source-revision'],
  frozen_preflight: {path: preflightPath, sha256: digest(preflightBytes)},
  browser_build: path.relative(ROOT, build),
  disc: {basename: path.basename(values.disc), sha256: null},
  limits_ms: LIMITS,
  profile: {mode: 'one fresh in-memory Playwright BrowserContext', before: null, after: null},
  browser: null,
  local_http_artifacts: {count: 0, before: null, after: null, unchanged: false},
  scenario: {
    route_mode: firstSssVsSetup ? 'first-sss-vs-setup' : firstSssVsEntry ? 'first-sss-vs-entry' : firstSssPrefix ? 'first-sss-prefix' : firstSssDraw ? 'first-sss-draw' :
      firstSssConsumedPadTick ? 'first-sss-consumed-pad-tick' :
      firstSssConstructorPair ? 'first-sss-constructor-pair' :
      firstCssBrowserFinalDraw ? 'first-css-browser-final-pending-draw' :
      firstCssBrowserStream ? 'first-css-browser-postdraw-stream' :
      firstCssBrowserDraw ? 'first-css-browser-draw' :
      values['css-sss-reducer'] ? 'css-sss-reducer' : 'full-c1a',
    armed_before_css_entry: false,
    controls: null,
    input_recipe: null,
    original_css_phase: null,
    original_sss_phase: null,
    raw_pad_stage_drive_frames: 0,
    source_selection: null,
    css_sss_reducer: values['css-sss-reducer'] ? createCssSssTransitionCapture(
      'Original CSS -> original SSS only; one ordinary B0XX Start chord after observed source and input readiness. No SSS selection, stage, match, or source archive request.') : null,
    css_sss_transition: null,
    first_sss_constructor_pair: null,
    first_sss_consumed_tick: null,
    first_sss_draw: null,
    first_sss_prefix: null,
    first_sss_vs_entry: null,
    first_sss_vs_setup: null,
    matched_setup_cf: null,
    requested_committed_manifest: null,
    native_observation: null,
    final_phase: null,
    final_running: null,
    native_message: null,
    native_diagnostics: null,
    gpu: null,
    screenshots: {},
    page_errors: [],
    console_errors: [],
    console_warnings: [],
    console_messages: [],
    console_message_capture_overflow: false,
    http_requests: [],
    external_http_requests: [],
  },
  cleanup: {unload_attempted: false, unload_completed: false, observation_cleared: false,
    context_closed: false, browser_connection_closed: false, browser_process_terminated: false,
    browser_process_pid: null, server_process_terminated: false, server_process_pid: null,
    server_process_exit: null, failures: []},
  source_request_scope: firstSssVsSetupCf
    ? firstSssVsSetupCfV2
      ? 'Strict same-run Entry and completed Setup comparison, then the extracted run2 proper prefix from action0 through the next draw/tick/draw after dynamically mapped C/F; compare C/F before their following draws, then stop.'
      : 'Strict same-run Entry and completed Setup comparison, then the exact original 86-tick/85-draw schedule; compare C after tick84 before draw84 and F after tick85 before draw85, then stop.'
    : firstSssVsSetup
    ? `Strict Entry then checked async exact Stadium asset handoff and existing deferred Session Setup endpoint; stop before gameplay tick/draw/GO${firstSssVsSetupTextTail?'; opt-in bounded SIS text-tail diagnostics only':''}`
    : firstSssVsEntry
    ? 'Strict SSS prefix then read-only actual match Start/seed Entry endpoint; no match construction'
    : firstSssPrefix
    ? 'Original CSS final draw, SSS constructor and frame-zero consumed PAD prerequisites, first original SSS draw, then a finite 124-record SSS input/tick/draw prefix through passive SSS exit and later selected-stage tuple. Prepared VS output remains unpaired; stops before ordinary VS scene entry/setup.'
    : firstSssDraw
    ? 'Original CSS final draw, SSS constructor and frame-zero consumed PAD prerequisites, then exact original first SSS DrawEnter1609/DrawReturn1610. Stops before the finite SSS continuation; no whole-session acceptance.'
    : firstSssConsumedPadTick
    ? 'Original CSS final draw, original SSS constructor pair, and one consumed SSS PAD/scheduler-end comparison. The run stops before any SSS draw or further input; no whole-session acceptance.'
    : firstSssConstructorPair
    ? 'Original SSS constructor entry1579/return1603 pair after the approved first-CSS boundary. Stops before any SSS host tick/draw; no SSS selection, VS/GO equality or whole-session acceptance.'
    : firstCssBrowserStream
    ? 'CSS-only source-consumed input/tick/draw comparison; terminal transition snapshot and final original draw remain unpaired. No SSS admission, VS/GO equality or whole-session acceptance.'
    : 'C1a stops after exact local-disc preparation. It does not instrument or prove a runtime source archive file-service request; full C1 remains open.',
  error: null,
};

const stadiumC3ReportCheckpoint = createStadiumC3ReportCheckpoint({reportPath: inventoryOutput});
const persistReport = async () => {
  const sample = matchedSetupCfTiming?.currentActionSample ?? null;
  const measure = matchedSetupCfTiming?.actionLoopActive === true && sample !== null;
  const totalStarted = measure ? monotonicNowNs() : null;
  let serialized = null, serializeMs = null, writeMs = null, renameMs = null;
  let completed = false;
  try {
    const terminal = report.execution_state === 'completed';
    const checkpointReport = await stadiumC3ReportCheckpoint.prepare(report, {terminal});
    const checkpointed = stadiumC3ReportCheckpoint.shouldUse(report);
    const temporary = checkpointed
      ? stadiumC3ReportCheckpoint.nextReportTemporaryPath()
      : `${inventoryOutput}.tmp`;
    serialized = measure
      ? timedActionSync(sample, 'report_serialize', null,
        () => JSON.stringify(checkpointReport) + '\n', event => { serializeMs = event.elapsed_ms; })
      : JSON.stringify(checkpointReport) + '\n';
    if (measure) {
      await timedActionAsync(sample, 'report_write', null,
        () => fs.writeFile(temporary, serialized), event => { writeMs = event.elapsed_ms; });
      await timedActionAsync(sample, 'report_rename', null,
        () => fs.rename(temporary, inventoryOutput), event => { renameMs = event.elapsed_ms; });
    } else {
      await fs.writeFile(temporary, serialized);
      await fs.rename(temporary, inventoryOutput);
    }
    if (checkpointed && !terminal)
      stadiumC3ReportCheckpoint.commitPrepared(checkpointReport);
    completed = true;
  } finally {
    if (measure) matchedSetupCfTiming.persistence.push({
      serialized_bytes: serialized === null ? null : Buffer.byteLength(serialized),
      serialize_ms: serializeMs, write_ms: writeMs, rename_ms: renameMs,
      total_ms: elapsedMs(totalStarted), completed,
    });
  }
};

let failure = null;
let serverProcess = null;
let serverSpawnError = null;
let serverOut = null;
let serverErr = null;
let serverOutDone = null;
let serverErrDone = null;
let browserServer = null;
let browser = null;
let browserCdp = null;
let browserProcess = null;
let context = null;
let page = null;
let driver = null;
let nativeObservation = null;
let pendingMatchedSetupCfExplicitStep = null;
let matchedSetupCfExplicitAsyncifyUnsafe = false;
let matchedSetupCfExplicitArmed = false;
let matchedSetupCfExplicitRetirementAttempted = false;
let matchedSetupCfExplicitNativeAccessClosed = false;
let unloadAttempted = false;
let requestRows = [];
let responseRows = [];
let externalRequests = [];
let pageErrors = [];
let consoleErrors = [];
let consoleWarnings = [];
let consoleMessages = [];
let consoleMessageCaptureOverflow = false;
let consoleMessageDropped = 0;
let consoleMessageBytes = 0;
const MAX_CONSOLE_MESSAGES = 4096;
const MAX_CONSOLE_MESSAGE_BYTES = 4096;
const MAX_CONSOLE_CAPTURE_BYTES = 8 * 1024 * 1024;
let cleanupDeadline = null;

function cleanupRemaining(cap = LIMITS.cleanupMs) {
  if (cleanupDeadline === null) throw Error('Cleanup deadline was not started');
  const ms = Math.min(cap, cleanupDeadline - Date.now());
  if (ms <= 0) throw Error('C1a owned-process cleanup deadline exhausted');
  return ms;
}

async function allocateLoopbackPort() {
  const reservation = net.createServer();
  await new Promise((resolve, reject) => {
    reservation.once('error', reject);
    reservation.listen(0, '127.0.0.1', resolve);
  });
  const port = reservation.address().port;
  await new Promise((resolve, reject) => reservation.close(error => error ? reject(error) : resolve()));
  return port;
}

async function waitForServer(baseUrl) {
  const end = Date.now() + LIMITS.serverStartMs;
  let lastError = null;
  while (Date.now() < end) {
    if (serverSpawnError) throw Error(`Local server spawn failed: ${serverSpawnError}`);
    if (serverProcess.exitCode !== null || serverProcess.signalCode !== null)
      throw Error(`Local server exited during startup (code=${serverProcess.exitCode}, signal=${serverProcess.signalCode})`);
    try {
      const response = await fetch(`${baseUrl}/runtime.html`, {signal: AbortSignal.timeout(1500)});
      if (response.status === 200) return;
      lastError = Error(`Local server readiness returned HTTP ${response.status}`);
    } catch (error) { lastError = error; }
    await sleep(100);
  }
  throw Error(`Local server did not become ready: ${errorText(lastError)}`);
}

let firstCssBrowserDrawInputs = null;
let firstSssConstructorPairInputs = null;
let firstSssConsumedPadTickInputs = null;
let firstSssDrawInputs = null;
let firstSssPrefixInputs = null;
let firstSssVsEntryInputs = null;
let firstSssVsSetupInputs = null;
let matchedSetupCfInputs = null;
async function loadFirstCssBrowserDrawInputs() {
  if (!firstCssBrowserDraw) return null;
  const specs = {
    context_bundle: values['first-css-context'],
    consumed_pad_bundle: values['first-css-consumed-pad'],
    return_expected: values['first-css-return-expected'],
    tick_expected: values['first-css-tick-expected'],
    draw_expected: values['first-css-draw-expected'],
  };
  const frozen = preflight.first_css_browser_draw?.inputs;
  assert.ok(frozen && typeof frozen === 'object',
    'First-CSS browser-draw preflight omitted exact external input identities');
  const bytes = {};
  for (const [name, fileValue] of Object.entries(specs)) {
    assert.ok(fileValue, `First-CSS browser-draw requires --${name.replaceAll('_', '-')}`);
    const file = path.resolve(fileValue);
    const binding = frozen[name];
    assert.ok(binding && typeof binding === 'object', `Preflight omitted ${name}`);
    assert.equal(path.resolve(binding.path), file, `Preflight path differs for ${name}`);
    const data = await fs.readFile(file);
    assert.equal(data.byteLength, binding.bytes, `Preflight byte count differs for ${name}`);
    assert.equal(digest(data), binding.sha256, `Preflight hash differs for ${name}`);
    bytes[name] = data;
  }
  const contextExpected = JSON.parse(bytes.return_expected.toString('utf8'));
  const tickExpected = JSON.parse(bytes.tick_expected.toString('utf8'));
  const drawExpected = JSON.parse(bytes.draw_expected.toString('utf8'));
  assert.equal(contextExpected.schema, 'melee-web-stadium-first-css-context-diagnostic');
  assert.equal(tickExpected.schema, 'melee-web-stadium-first-css-consumed-tick-diagnostic');
  assert.equal(drawExpected.schema, 'melee-web-stadium-first-css-first-draw-diagnostic');
  assert.equal(contextExpected.provenance.stream_bytes, 4397889);
  assert.equal(contextExpected.provenance.stream_sha256,
    '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778');
  assert.equal(bytes.context_bundle.byteLength, contextExpected.input_bundle.bytes);
  assert.equal(digest(bytes.context_bundle), contextExpected.input_bundle.sha256);
  assert.equal(bytes.context_bundle.subarray(0, 8).toString('ascii'), 'STC1INPT');
  assert.equal(bytes.context_bundle.readUInt32BE(8), 1);
  assert.equal(bytes.context_bundle.subarray(12, 44).toString('hex'),
    contextExpected.provenance.stream_sha256);
  assert.equal(bytes.context_bundle.readUInt32BE(44), 704);
  assert.equal(bytes.context_bundle.readUInt32BE(48), 833);
  assert.equal(bytes.consumed_pad_bundle.byteLength, tickExpected.input_bundle.bytes);
  assert.equal(digest(bytes.consumed_pad_bundle), tickExpected.input_bundle.sha256);
  assert.equal(bytes.consumed_pad_bundle.subarray(0, 8).toString('ascii'), 'STC1PAD1');
  assert.equal(bytes.consumed_pad_bundle.subarray(12, 44).toString('hex'),
    tickExpected.provenance.stream_sha256);
  assert.equal(tickExpected.provenance.stream_sha256,
    contextExpected.provenance.stream_sha256);
  assert.equal(bytes.consumed_pad_bundle.readUInt32BE(44), 834);
  assert.equal(bytes.consumed_pad_bundle.readUInt32BE(48), 835);
  assert.equal(tickExpected.expected_post_tick.source_tick_sequence, 835);
  assert.equal(drawExpected.provenance.stream_sha256,
    contextExpected.provenance.stream_sha256);
  assert.equal(drawExpected.provenance.source_tick_sequence, 835);
  assert.equal(drawExpected.provenance.draw_enter_sequence, 836);
  assert.equal(drawExpected.provenance.draw_return_sequence, 837);
  assert.equal(contextExpected.expected_return.scene_kind, 8);
  assert.deepEqual(drawExpected.comparison_fields, [
    'source_tick', 'draw_ordinal', 'pad_state_hex', 'random_seed_hex',
    'scene_frame', 'scene_kind', 'scene_routing_getters']);
  return {bytes, contextExpected, tickExpected, drawExpected,
    bundle_hashes: Object.fromEntries(Object.entries(bytes).map(([name, data]) =>
      [name, {bytes: data.byteLength, sha256: digest(data)}]))};
}

let firstCssBrowserStreamInputs = null;
async function loadFirstCssBrowserStreamInputs() {
  if (!firstCssBrowserStream) return null;
  const frozen = preflight.first_css_browser_stream;
  assert.ok(frozen?.inputs && frozen.comparator,
    'CSS stream preflight omitted exact input and host-comparator identities');
  const bytes = {};
  for (const [name, option] of Object.entries({postdraw_input: 'first-css-postdraw-input',
    postdraw_expected: 'first-css-postdraw-expected'})) {
    assert.ok(values[option], `CSS stream requires --${option}`);
    const file = path.resolve(values[option]);
    const binding = frozen.inputs[name];
    assert.ok(binding, `CSS stream preflight omitted ${name}`);
    assert.equal(path.resolve(binding.path), file);
    const data = await fs.readFile(file);
    assert.equal(data.byteLength, binding.bytes, `CSS stream byte count differs for ${name}`);
    assert.equal(digest(data), binding.sha256, `CSS stream hash differs for ${name}`);
    bytes[name] = data;
  }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  assert.equal(path.resolve(frozen.comparator.path), helperPath);
  const helperBytes = await fs.readFile(helperPath);
  assert.equal(helperBytes.byteLength, frozen.comparator.bytes);
  assert.equal(digest(helperBytes), frozen.comparator.sha256);
  const expected = JSON.parse(bytes.postdraw_expected.toString('utf8'));
  assert.equal(expected.schema, 'melee-web-stadium-first-css-postdraw-stream-diagnostic');
  assert.equal(expected.version, 1);
  assert.equal(expected.provenance.stream_bytes, 4397889);
  assert.equal(expected.provenance.stream_sha256,
    firstCssBrowserDrawInputs.contextExpected.provenance.stream_sha256);
  assert.equal(expected.provenance.baseline_draw_return_sequence, 837);
  assert.equal(expected.provenance.source_batch_count, 148);
  assert.equal(expected.provenance.first_consumed_pad_sequence, 839);
  assert.equal(expected.provenance.last_consumed_pad_sequence, 1574);
  assert.equal(expected.provenance.first_draw_return_sequence, 842);
  assert.equal(expected.provenance.last_draw_return_sequence, 1577);
  assert.equal(expected.provenance.stop_before_sss_admission, true);
  assert.equal(expected.whole_session_equivalent, false);
  assert.equal(expected.source_admission, false);
  assert.deepEqual(expected.unpaired_routing_fields, ['pending_mode', 'next_state_id']);
  assert.deepEqual(expected.excluded_source_tags, [2, 36, 37]);
  assert.deepEqual(expected.draw_comparison_fields,
    firstCssBrowserDrawInputs.drawExpected.comparison_fields);
  const input = bytes.postdraw_input;
  assert.equal(input.byteLength, 6564);
  assert.equal(input.byteLength, expected.input_bundle.bytes);
  assert.equal(digest(input), expected.input_bundle.sha256);
  assert.equal(input.subarray(0, 8).toString('ascii'), 'STC1PSTR');
  assert.equal(input.readUInt32BE(8), 1);
  assert.equal(input.subarray(12, 44).toString('hex'), expected.provenance.stream_sha256);
  assert.equal(input.readUInt32BE(44), 839);
  assert.equal(input.readUInt32BE(48), 148);
  assert.equal(expected.input_bundle.sample_count, 148);
  assert.equal(expected.input_bundle.contains_expected_post_tick_state, false);
  assert.equal(expected.input_bundle.contains_expected_draw_state, false);
  // Validate the actual pure comparator's expected-boundary contract before
  // server/browser startup; this does not execute source or apply expected data.
  const helper = await import(pathToFileURL(helperPath).href);
  helper.createFirstCssStreamComparator(expected.expected_pairs);
  assert.equal(input.subarray(52).toString('hex'), expected.expected_pairs
    .flatMap(pair => pair.input_port_status_hex).join(''),
  'CSS stream input payload differs from the bound original consumed statuses');
  return {bytes, expected, helperSource: helperBytes.toString('utf8'),
    bundle_hashes: Object.fromEntries(Object.entries(bytes).map(([name, data]) =>
      [name, {bytes: data.byteLength, sha256: digest(data)}])),
    comparator: {path: helperPath, bytes: helperBytes.byteLength, sha256: digest(helperBytes)}};
}

async function loadFirstSssConstructorPairInputs() {
  if (!firstSssConstructorPair) return null;
  const file = path.resolve(values['first-sss-pair-expected']);
  const frozen = preflight.first_sss_constructor_pair?.expected_json;
  assert.ok(frozen && typeof frozen === 'object',
    'SSS-pair preflight omitted the exact retained original expected JSON identity');
  assert.equal(path.resolve(frozen.path), file,
    'SSS-pair expected JSON path differs from the frozen preflight');
  assert.equal(path.basename(file), frozen.basename,
    'SSS-pair expected JSON basename differs from the frozen preflight');
  const bytes = await fs.readFile(file);
  assert.equal(bytes.byteLength, frozen.bytes,
    'SSS-pair expected JSON byte count differs from the frozen preflight');
  assert.equal(digest(bytes), frozen.sha256,
    'SSS-pair expected JSON hash differs from the frozen preflight');
  let expected;
  try { expected = JSON.parse(bytes.toString('utf8')); }
  catch (error) { throw Error(`SSS-pair expected JSON is malformed: ${errorText(error)}`); }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  const helper = await import(pathToFileURL(helperPath).href);
  helper.createFirstSssConstructorPairComparator(expected);
  assert.equal(expected.schema, 'melee-web-stadium-first-sss-constructor-pair-diagnostic');
  assert.equal(expected.provenance.observer_sha256,
    firstCssBrowserDrawInputs.contextExpected.provenance.stream_sha256);
  assert.equal(expected.source_entry.argument, '0x80480668');
  assert.equal(expected.source_return.argument, '0x00000000');
  return {bytes, expected, expected_identity: {path: file, basename: path.basename(file),
    bytes: bytes.byteLength, sha256: digest(bytes)}};
}

async function loadFirstSssConsumedPadTickInputs() {
  if (!firstSssConsumedPadTick) return null;
  const specs = {
    expected_json: values['first-sss-tick-expected'],
    input_bundle: values['first-sss-tick-input'],
  };
  const frozen = preflight.first_sss_consumed_tick;
  assert.ok(frozen && typeof frozen === 'object',
    'SSS tick preflight omitted its exact expected/input identities');
  const bytes = {};
  const identities = {};
  for (const [name, fileValue] of Object.entries(specs)) {
    const file = path.resolve(fileValue);
    const identity = frozen[name];
    assert.ok(identity && typeof identity === 'object',
      `SSS tick preflight omitted ${name} identity`);
    assert.equal(path.resolve(identity.path), file,
      `SSS tick ${name} path differs from the frozen preflight`);
    assert.equal(path.basename(file), identity.basename,
      `SSS tick ${name} basename differs from the frozen preflight`);
    bytes[name] = await fs.readFile(file);
    assert.equal(bytes[name].byteLength, identity.bytes,
      `SSS tick ${name} byte count differs from the frozen preflight`);
    assert.equal(digest(bytes[name]), identity.sha256,
      `SSS tick ${name} hash differs from the frozen preflight`);
    identities[name] = {path: file, basename: path.basename(file),
      bytes: bytes[name].byteLength, sha256: digest(bytes[name])};
  }
  let expected;
  try { expected = JSON.parse(bytes.expected_json.toString('utf8')); }
  catch (error) { throw Error(`SSS tick expected JSON is malformed: ${errorText(error)}`); }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  const helper = await import(pathToFileURL(helperPath).href);
  helper.validateFirstSssConsumedTickExpectedSource(expected);
  const input = bytes.input_bundle;
  assert.equal(input.byteLength, expected.input_bundle.bytes);
  assert.equal(digest(input), expected.input_bundle.sha256);
  assert.equal(input.subarray(0, 8).toString('ascii'), 'STC1SSS1');
  assert.equal(input.readUInt32BE(8), 1);
  assert.equal(input.subarray(12, 44).toString('hex'), expected.provenance.observer_sha256);
  assert.equal(input.readUInt32BE(44), 1604);
  assert.equal(input.readUInt32BE(48), 1608);
  assert.equal(input.subarray(52).toString('hex'),
    expected.source_scheduler_end.expected.consumed_pad_status_hex.join(''),
    'SSS tick bundle differs from the exact four original PAD statuses');
  assert.deepEqual(expected.input_bundle.port_status_hex,
    expected.source_scheduler_end.expected.consumed_pad_status_hex);
  assert.equal(expected.input_bundle.contains_expected_state, false,
    'SSS tick input must contain only consumed PAD statuses and source bindings');
  return {bytes, expected, identities};
}

async function loadBoundFirstSssExpected(mode, optionName, expectedSchema) {
  const fileValue = values[optionName];
  const file = path.resolve(fileValue);
  const frozen = preflight[mode];
  assert.ok(frozen?.expected_json && frozen?.comparator,
    `${mode} preflight omitted expected JSON or comparator identities`);
  const identity = frozen.expected_json;
  assert.equal(path.resolve(identity.path), file, `${mode} expected path differs from preflight`);
  assert.equal(path.basename(file), identity.basename, `${mode} expected basename differs from preflight`);
  const bytes = await fs.readFile(file);
  assert.equal(bytes.byteLength, identity.bytes, `${mode} expected byte count differs from preflight`);
  assert.equal(digest(bytes), identity.sha256, `${mode} expected hash differs from preflight`);
  let expected;
  try { expected = JSON.parse(bytes.toString('utf8')); }
  catch (error) { throw Error(`${mode} expected JSON is malformed: ${errorText(error)}`); }
  assert.equal(expected.schema, expectedSchema);
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  assert.equal(path.resolve(frozen.comparator.path), helperPath,
    `${mode} comparator path differs from preflight`);
  const helperBytes = await fs.readFile(helperPath);
  assert.equal(helperBytes.byteLength, frozen.comparator.bytes,
    `${mode} comparator byte count differs from preflight`);
  assert.equal(digest(helperBytes), frozen.comparator.sha256,
    `${mode} comparator hash differs from preflight`);
  const helper = await import(pathToFileURL(helperPath).href);
  if (mode === 'first_sss_draw') helper.validateFirstSssDrawExpectedSource(expected);
  return {bytes, expected, helperPath, helperBytes,
    identity: {path: file, basename: path.basename(file), bytes: bytes.byteLength, sha256: digest(bytes)},
    comparator: {path: helperPath, bytes: helperBytes.byteLength, sha256: digest(helperBytes)}};
}

async function loadFirstSssDrawInputs() {
  if (!firstSssDraw) return null;
  const result = await loadBoundFirstSssExpected('first_sss_draw',
    'first-sss-draw-expected', 'melee-web-stadium-first-sss-draw-diagnostic');
  assert.equal(result.expected.provenance.observer_sha256,
    firstSssConstructorPairInputs.expected.provenance.observer_sha256);
  assert.equal(result.expected.provenance.observer_sha256,
    firstSssConsumedPadTickInputs.expected.provenance.observer_sha256);
  return result;
}

async function loadFirstSssPrefixInputs() {
  if (!firstSssPrefix) return null;
  const drawInputs = firstSssDrawInputs;
  const result = await loadBoundFirstSssExpected('first_sss_prefix',
    'first-sss-prefix-expected', 'melee-web-stadium-first-sss-prefix-diagnostic');
  const inputFile = path.resolve(values['first-sss-prefix-input']);
  const binding = preflight.first_sss_prefix.input_bundle;
  assert.ok(binding && typeof binding === 'object', 'SSS prefix preflight omitted input-bundle identity');
  assert.equal(path.resolve(binding.path), inputFile, 'SSS prefix input path differs from preflight');
  assert.equal(path.basename(inputFile), binding.basename, 'SSS prefix input basename differs from preflight');
  const inputBytes = await fs.readFile(inputFile);
  assert.equal(inputBytes.byteLength, binding.bytes, 'SSS prefix input length differs from preflight');
  assert.equal(digest(inputBytes), binding.sha256, 'SSS prefix input hash differs from preflight');
  assert.equal(inputBytes.byteLength, result.expected.input_bundle.bytes);
  assert.equal(digest(inputBytes), result.expected.input_bundle.sha256);
  const inputRows = result.helperBytes.byteLength
    ? (await import(pathToFileURL(result.helperPath).href))
      .decodeFirstSssPrefixInputBundle(inputBytes)
    : null;
  const helper = await import(pathToFileURL(result.helperPath).href);
  helper.validateFirstSssPrefixExpectedSource(result.expected, inputRows);
  assert.equal(result.expected.provenance.observer_sha256,
    drawInputs.expected.provenance.observer_sha256,
    'Prefix and first-draw expectations do not bind the same retained observer');
  assert.equal(result.expected.input_bundle.contains_expected_state, false);
  return {...result, inputBytes, inputIdentity: {path: inputFile,
    basename: path.basename(inputFile), bytes: inputBytes.byteLength, sha256: digest(inputBytes)},
    inputRows};
}

async function loadFirstSssVsSetupInputs() {
  if (!firstSssVsSetup) return null;
  const result = await loadBoundFirstSssExpected('first_sss_vs_setup',
    'first-sss-vs-setup-expected', 'melee-web-stadium-first-sss-vs-setup-endpoint');
  assert.equal(preflight.first_sss_vs_setup.diagnostic_export,
    '_melee_web_native_menu_stadium_first_sss_vs_setup_begin');
  assert.equal(preflight.first_sss_vs_setup.expected_state_host_only, true);
  const textTailBinding = preflight.first_sss_vs_setup.text_tail_probe;
  assert.ok(textTailBinding === undefined || typeof textTailBinding === 'boolean',
    'Setup text-tail preflight binding must be absent or boolean');
  assert.equal(textTailBinding ?? false, firstSssVsSetupTextTail,
    'Setup text-tail opt-in differs from the exact preflight route binding');
  const helper = await import(pathToFileURL(result.helperPath).href);
  helper.validateFirstSssVsSetupExpectedSource(result.expected);
  for (const key of ['observer_sha256', 'observer_status_sha256', 'observer_bytes', 'observer_status_bytes'])
    assert.equal(result.expected.provenance[key], firstSssVsEntryInputs.expected.provenance[key],
      `VS Setup/Entry source provenance differs at ${key}`);
  return result;
}

async function loadMatchedSetupCfInputs() {
  if (!firstSssVsSetupCf) return null;
  const packetPath = path.resolve(values['first-sss-vs-setup-cf-packet']);
  const assemblyPath = path.resolve(values['first-sss-vs-setup-cf-assembly']);
  const helperPath = path.join(ROOT, 'tests/stadium_matched_setup_cf_consumer.mjs');
  const readIdentity = async file => {
    const bytes = await fs.readFile(file);
    return {path: path.resolve(file), basename: path.basename(file),
      bytes: bytes.byteLength, sha256: digest(bytes), content: bytes};
  };
  const [packetIdentity, assemblyIdentity, helperIdentity] = await Promise.all([
    readIdentity(packetPath), readIdentity(assemblyPath), readIdentity(helperPath),
  ]);
  validateMatchedSetupCfIdentities(matchedSetupCfPreflight, {
    packet: Object.fromEntries(Object.entries(packetIdentity).filter(([key]) => key !== 'content')),
    assembly_manifest: Object.fromEntries(Object.entries(assemblyIdentity).filter(([key]) => key !== 'content')),
    helper: Object.fromEntries(Object.entries(helperIdentity).filter(([key]) => key !== 'content')),
  });
  let packet, assembly;
  try { packet = JSON.parse(packetIdentity.content.toString('utf8')); }
  catch (error) { throw Error(`Matched Setup C/F packet is malformed: ${errorText(error)}`); }
  try { assembly = JSON.parse(assemblyIdentity.content.toString('utf8')); }
  catch (error) { throw Error(`Matched Setup C/F evidence assembly is malformed: ${errorText(error)}`); }
  const helper = await import(pathToFileURL(helperPath).href);
  const hostEvidence = helper.hostEvidenceFromPacket(packet);
  assert.equal(hostEvidence.mode === 'run2-proper-prefix-v2', firstSssVsSetupCfV2,
    'packet schema differs from the preflight-bound continuation version');
  if (hostEvidence.mode === 'run2-proper-prefix-v2') {
    assert.deepEqual(Object.keys(assembly).sort(),
      ['host_only_expectations_sha256', 'runtime_payload_sha256', 'schema', 'scope'].sort());
    assert.equal(assembly.schema, 'melee-web-stadium-run2-source-action-assembly-v1');
    assert.equal(assembly.scope,
      'Source-only run2 action payload and host-only expectation identity; no runtime or profile acceptance.');
    assert.equal(assembly.runtime_payload_sha256, digest(Buffer.from(
      JSON.stringify(sortedJsonValue(packet.runtime_payload), null, 2) + '\n')));
    assert.equal(assembly.host_only_expectations_sha256, digest(Buffer.from(
      JSON.stringify(sortedJsonValue(packet.host_only), null, 2) + '\n')));
  } else {
    assert.equal(assembly.schema, 'melee-web-stadium-setup-cf-external-evidence-assembly-v1');
    assert.equal(assembly.scope,
      'Historical retained evidence basis only; does not admit a future source, producer, fresh Setup or runtime continuation. Never forward into runtime.');
  }
  return {helper, hostEvidence, packetIdentity: Object.fromEntries(
    Object.entries(packetIdentity).filter(([key]) => key !== 'content')),
    assemblyIdentity: Object.fromEntries(
      Object.entries(assemblyIdentity).filter(([key]) => key !== 'content')),
    helperIdentity: Object.fromEntries(
      Object.entries(helperIdentity).filter(([key]) => key !== 'content')),
    assemblySchema: assembly.schema, runtimePayloadSha256: digest(Buffer.from(
      JSON.stringify(sortedJsonValue(packet.runtime_payload), null, 2) + '\n'))};
}

async function loadFirstSssVsEntryInputs() {
  if (!firstSssVsEntry) return null;
  const result = await loadBoundFirstSssExpected('first_sss_vs_entry',
    'first-sss-vs-entry-expected', 'melee-web-stadium-first-sss-vs-entry-endpoint');
  assert.equal(preflight.first_sss_vs_entry.diagnostic_export,
    '_melee_web_native_menu_stadium_first_sss_vs_entry_kick',
    'VS Entry diagnostic export differs from frozen preflight');
  assert.equal(preflight.first_sss_vs_entry.expected_state_host_only, true,
    'VS Entry expected state must remain host-only');
  const helper = await import(pathToFileURL(result.helperPath).href);
  helper.validateFirstSssVsEntryExpectedSource(result.expected);
  for (const key of ['observer_sha256', 'observer_status_sha256',
    'observer_bytes', 'observer_status_bytes'])
    assert.equal(result.expected.provenance[key], firstSssPrefixInputs.expected.provenance[key],
      `VS Entry and prefix source provenance differs at ${key}`);
  return result;
}

async function buildArtifactInventory(baseUrl, names) {
  return Object.fromEntries(await Promise.all(names.map(async name => {
    const local = await fs.readFile(path.join(build, name));
    const response = await fetch(`${baseUrl}/${encodeURIComponent(name)}`, {
      redirect: 'error', signal: AbortSignal.timeout(15000),
    });
    const body = new Uint8Array(await response.arrayBuffer());
    assert.equal(response.status, 200, `HTTP build artifact ${name} returned ${response.status}`);
    const localHash = digest(local), httpHash = digest(body);
    assert.equal(httpHash, localHash, `HTTP bytes differ from local build artifact ${name}`);
    return [name, {bytes: local.byteLength, local_sha256: localHash,
      http_status: response.status, http_bytes: body.byteLength, http_sha256: httpHash}];
  })));
}

function assertProducerArtifacts(inventory, names, phase) {
  const expected = preflight.producer?.browser_artifacts;
  assert.ok(expected && typeof expected === 'object', 'Frozen preflight omitted the producer artifact receipt');
  assert.deepEqual(Object.keys(expected).sort(), [...names].sort(),
    'Frozen producer artifact set differs from the declared browser artifact inventory');
  for (const name of names) {
    const item = inventory[name];
    const producer = expected[name];
    assert.ok(item && producer, `${phase} artifact inventory omitted ${name}`);
    assert.equal(item.bytes, producer.bytes, `${phase} artifact length differs from the frozen producer for ${name}`);
    assert.equal(item.local_sha256, producer.sha256,
      `${phase} local build artifact differs from the frozen producer for ${name}`);
  }
}

async function saveScreenshot(name) {
  if (!page || page.isClosed()) return null;
  const file = path.join(output, `${name}.png`);
  await timeout(page.screenshot({path: file, fullPage: true, animations: 'disabled'}),
    10000, `Screenshot ${name}`);
  const item = {file: path.basename(file), sha256: await hashFile(file)};
  report.scenario.screenshots[name] = item;
  return item;
}

async function nativeSnapshot() {
  const snapshot = await page.evaluate(requireAsyncifyIdle => {
    const module = globalThis.Module;
    if (requireAsyncifyIdle &&
        (typeof module?.stadiumMatchedSetupNativeIdle !== 'function' ||
         module.stadiumMatchedSetupNativeIdle() !== true))
      return {__native_asyncify_idle: false};
    const pointer = module?._melee_web_native_menu_message?.();
    const detail = module?._melee_web_native_menu_diagnostics?.();
    return {
      phase: module?._melee_web_native_menu_phase?.() ?? null,
      running: module?._melee_web_native_menu_running?.() ?? null,
      message: pointer ? module.UTF8ToString(pointer) : null,
      diagnostics: detail ? module.UTF8ToString(detail) : null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
    };
  }, firstSssVsSetupCfExplicitStep);
  if (snapshot?.__native_asyncify_idle === false) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    throw Error('Asyncify is not idle; native snapshot was suppressed');
  }
  return snapshot;
}

async function sourceSnapshot(label) {
  return timeout(nativeSnapshot(), Math.min(5000, remaining()), label);
}

function captureBrowserConsoleMessage(message) {
  const type = message.type();
  const fullText = message.text();
  const bytes = Buffer.byteLength(fullText, 'utf8');
  if (type === 'error') consoleErrors.push(fullText.slice(0, 1000));
  if (type === 'warning') consoleWarnings.push(fullText.slice(0, 1000));
  const text = fullText.slice(0, MAX_CONSOLE_MESSAGE_BYTES);
  const storedBytes = Buffer.byteLength(text, 'utf8');
  if (consoleMessages.length >= MAX_CONSOLE_MESSAGES ||
      consoleMessageBytes + storedBytes > MAX_CONSOLE_CAPTURE_BYTES) {
    consoleMessageCaptureOverflow = true;
    ++consoleMessageDropped;
    return;
  }
  consoleMessages.push({type, text, bytes,
    truncated: bytes > MAX_CONSOLE_MESSAGE_BYTES});
  consoleMessageBytes += storedBytes;
  if (bytes > MAX_CONSOLE_MESSAGE_BYTES) consoleMessageCaptureOverflow = true;
}

async function observeCssInputFrame(label, operationDeadline = deadline) {
  const operationTimeout = Math.min(5000, remaining(), operationDeadline - Date.now());
  if (operationTimeout <= 0) throw Error(`${label} deadline exhausted`);
  return timeout(page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => {
    const module = globalThis.Module;
    const phase = module?._melee_web_native_menu_phase?.() ?? null;
    const pointer = module?._melee_web_native_menu_message?.();
    const diagnosticsPointer = module?._melee_web_native_menu_diagnostics?.();
    let css = null, observerError = null;
    if (phase === 1) {
      if (typeof module?._melee_web_css_observe_port !== 'function') {
        observerError = 'CSS source observer export is absent';
      } else {
        const ids = module._malloc(14 * 4), geometry = module._malloc(8 * 4);
        if (!ids || !geometry) {
          if (ids) module._free(ids);
          if (geometry) module._free(geometry);
          observerError = 'CSS source observer allocation failed';
        } else {
          try {
            if (module._melee_web_css_observe_port(0, 8, ids, geometry)) {
              css = {ids: Array.from(module.HEAP32.subarray(ids >> 2, (ids >> 2) + 14)),
                geometry: Array.from(module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))};
            }
          } catch (error) { observerError = String(error?.message || error); }
          finally { module._free(ids); module._free(geometry); }
        }
      }
    }
    let input = null;
    try {
      if (typeof module?._melee_web_input_message === 'function')
        input = JSON.parse(module.UTF8ToString(module._melee_web_input_message()));
      else observerError ||= 'Input state export is absent';
    } catch (error) { observerError ||= `Input snapshot failed: ${String(error?.message || error)}`; }
    resolve({at_ms: performance.now(), phase,
      running: module?._melee_web_native_menu_running?.() ?? null,
      message: pointer ? module.UTF8ToString(pointer) : null,
      diagnostics: diagnosticsPointer ? module.UTF8ToString(diagnosticsPointer) : null,
      runtime_error: document.querySelector('#status')?.dataset.runtimeError || null,
      hidden: document.hidden, focused: document.hasFocus(), css, input, observer_error: observerError});
  }))), operationTimeout, label);
}

function assertFirstCssBrowserEntry(observation, expected) {
  assert.ok(observation?.entry, 'First-CSS browser entry was not captured');
  assert.equal(observation.source_stream_sha256, expected.provenance.stream_sha256);
  const actual = observation.entry;
  const wanted = expected.expected_return;
  assert.equal(actual.source_scene, 1, 'Native first-CSS return is not the original CSS scene');
  assert.equal(actual.scene_kind, wanted.scene_kind);
  assert.equal(actual.random_seed_hex, wanted.random_seed_hex);
  assert.equal(actual.pad_state_hex, wanted.pad_state_hex);
  assert.equal(actual.ko_counts_hex, wanted.ko_counts_hex);
  const actualCss = structuredClone(actual.css);
  const wantedCss = structuredClone(wanted.css);
  assert.ok(actualCss?.vs?.start?.rules &&
    Object.prototype.hasOwnProperty.call(actualCss.vs.start.rules, 'pad_x5C'),
  'Native first-CSS typed rules omitted the explicit ABI-padding field');
  delete actualCss.vs.start.rules.pad_x5C;
  for (const field of ['on_unpause_override', 'on_pause_override',
    'check_for_pauser_override', 'on_match_start', 'on_frame_start',
    'on_frame_end', 'on_match_end', 'x54_pointer', 'x58_pointer']) {
    assert.equal(actualCss.vs.start.rules[field], 'null',
      `Returned source pointer field is not null: ${field}`);
    actualCss.vs.start.rules[field] = null;
  }
  assert.deepEqual(actualCss, wantedCss,
    'Full typed first-CSS return differs from the captured original CSS return');
  assert.deepEqual(actual.abi_padding_excluded, ['pad_x5C']);
}

function assertFirstCssBrowserTick(observation, expected) {
  const actual = observation?.tick;
  const wanted = expected.expected_post_tick;
  assert.ok(actual, 'The one native first-CSS tick was not captured');
  for (const key of ['source_stream_sha256', 'consumed_pad_sequence', 'source_tick_sequence',
    'source_tick_value', 'source_draw_ordinal', 'original_source_frame',
    'phase_relation', 'source_scene_kind', 'host_source_scene', 'host_menu_phase',
    'native_post_host_tick_frame', 'random_seed_hex', 'pad_state_hex'])
    assert.equal(actual[key], wanted[key], `First-CSS tick differs at ${key}`);
  assert.deepEqual(actual.scene_routing_getters, wanted.scene_routing_getters,
    'First-CSS tick scene-routing getters differ');
  assert.equal(actual.host_tick_calls, 1);
  assert.equal(actual.host_tick_result, 1);
  assert.equal(actual.world_generation_stable, true);
  assert.equal(actual.source_scene_stable, true);
  assert.equal(actual.seed_owner_stable, true);
  assert.deepEqual(actual.routing_raw_fields_excluded, ['pending_mode', 'next_state_id']);
}

function assertFirstCssBrowserDraw(observation, expected) {
  const actual = observation?.draw;
  const wanted = expected.expected_draw_return;
  assert.ok(actual, 'The original first-CSS host draw return was not captured');
  for (const key of expected.comparison_fields)
    assert.deepEqual(actual[key], wanted[key], `First-CSS browser draw differs at ${key}`);
  assert.equal(actual.boundary, 'browser_host_draw_return');
  assert.equal(actual.observation_phase,
    'after source host draw, before Aurora end-frame');
  assert.equal(actual.host_draw_calls, 1);
  assert.equal(actual.aurora_begin_calls, 1);
  assert.equal(actual.source_scene, 1);
  assert.equal(actual.menu_phase, 1);
  assert.equal(actual.scene_kind, 8);
  assert.equal(actual.world_generation_stable, true);
  assert.equal(actual.scene_owner_stable, true);
  assert.equal(actual.seed_owner_stable, true);
  assert.equal(actual.world_generation, observation.tick.world_generation);
}

async function readFirstCssBrowserDrawObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const observe = module?._melee_web_native_menu_stadium_first_css_draw_observe;
    if (typeof observe !== 'function') throw Error('First-CSS browser-draw observer export is absent');
    const pointer = observe();
    return pointer ? {...JSON.parse(module.UTF8ToString(pointer)),
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null;
  });
}

async function readFirstCssBrowserStreamObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstCssStreamRead?.() ?? null};
  });
}

function assertFirstCssBrowserStreamTerminal(evidence) {
  const observation = evidence?.native;
  const native = observation?.postdraw_stream;
  const host = evidence?.host;
  assert.ok(native && host, 'CSS stream terminal evidence is absent');
  assert.equal(native.failed, false, native.error || 'Native CSS stream failed');
  assert.equal(native.terminal, true);
  assert.equal(native.error, null);
  assert.equal(observation.native_phase, 1, 'CSS-only stream left its CSS owner');
  assert.equal(observation.running, 0, 'Terminal CSS stream source clock is still running');
  assert.equal(host.hook_error, null);
  assert.equal(host.status.failed, false);
  assert.equal(host.status.terminal, true);
  assert.equal(host.status.pending_draw, false);
  assert.equal(native.input_source_stream_sha256,
    firstCssBrowserStreamInputs.expected.provenance.stream_sha256);
  assert.equal(native.consumed_inputs, 148);
  assert.equal(native.host_tick_calls, 148);
  assert.equal(native.executed_host_ticks, 149);
  assert.equal(native.current_input_index, 147);
  assert.equal(native.current_input_ordinal, 148);
  assert.equal(native.current_consumed_pad_sequence, 1574);
  if (native.outcome === 'stop_after_last_input_request') {
    assert.equal(native.status, 'terminal');
    assert.equal(native.complete, false);
    assert.equal(native.tick_result, 3);
    assert.equal(native.frame_end_returned, false,
      'Terminal transition unexpectedly started a source presentation');
    assert.equal(native.input_index, 147);
    assert.equal(native.matched_ticks, 147);
    assert.equal(native.matched_draws, 147);
    assert.equal(native.host_draw_calls, 147);
    assert.equal(native.aurora_begin_calls, 147);
    assert.equal(native.aurora_end_calls, 147);
    assert.equal(host.status.outcome, 'stop_after_last_input_request');
    assert.equal(host.status.next_index, 147);
    assert.equal(host.rows.length, 295);
    const last = host.rows.at(-1);
    assert.equal(last.phase, 'transition');
    assert.equal(last.approved, false, 'Terminal transition was incorrectly compared/approved');
    assert.deepEqual(last.actual, native.transition_snapshot);
    assert.deepEqual(host.status.terminal_observation, native.transition_snapshot);
    assert.equal(last.actual.consumed_pad_sequence, 1574);
    assert.equal(last.actual.executed_host_ticks, 149);
    assert.equal(last.actual.stream_input_ordinal, 148);
    assert.equal(last.actual.terminal_transition, true);
  } else {
    assert.equal(native.outcome, 'bounded_pair_cap');
    assert.equal(native.status, 'bounded-pair-cap');
    assert.equal(native.complete, true);
    assert.equal(native.tick_result, 1);
    assert.equal(native.frame_end_returned, true);
    assert.equal(native.input_index, 148);
    assert.equal(native.matched_ticks, 148);
    assert.equal(native.matched_draws, 148);
    assert.equal(native.host_draw_calls, 148);
    assert.equal(native.aurora_begin_calls, 148);
    assert.equal(native.aurora_end_calls, 148);
    assert.equal(host.status.outcome, 'bounded_pair_cap');
    assert.equal(host.status.next_index, 148);
    assert.equal(host.rows.length, 296);
    assert.equal(host.rows.at(-1).phase, 'draw');
    assert.equal(host.rows.at(-1).approved, true);
  }
  // Every paired boundary was synchronously approved in original input order;
  // only the explicitly unpaired final transition is a permitted false result.
  for (let index = 0; index < host.status.next_index; index++) {
    assert.equal(host.rows[index * 2].phase, 'tick');
    assert.equal(host.rows[index * 2 + 1].phase, 'draw');
    assert.equal(host.rows[index * 2].approved, true);
    assert.equal(host.rows[index * 2 + 1].approved, true);
  }
}

async function runFirstCssBrowserPostdrawStream(baseline) {
  // This call is reached only after every existing full typed entry/tick/draw
  // assertion and one-shot lifecycle assertion has succeeded in Node.
  assert.equal(baseline.state, 'complete');
  assert.equal(baseline.host_tick_calls, 1);
  assert.equal(baseline.host_draw_calls, 1);
  assert.equal(baseline.running, 0);
  report.scenario.first_css_postdraw_inputs = firstCssBrowserStreamInputs.bundle_hashes;
  report.scenario.first_css_stream_comparator = firstCssBrowserStreamInputs.comparator;
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedPairs}) => {
    if (globalThis.__meleeWebStadiumFirstCssStreamCompare ||
        globalThis.__meleeWebStadiumFirstCssStreamRead)
      throw Error('CSS stream host comparator was already registered');
    const {createFirstCssStreamComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstCssStreamComparator(expectedPairs);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstCssStreamRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstCssStreamCompare = (phase, actualJson) => {
      if (rows.length >= 297) {
        hookError = 'CSS stream callback count exceeded the bound';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      // No runtime exports or guest writes are called by this synchronous hook.
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      rows.push({phase, actual, approved});
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
        Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
      expectedPairs: firstCssBrowserStreamInputs.expected.expected_pairs}),
  Math.min(5000, remaining()), 'Register host-only synchronous CSS stream comparison');
  const arm = await timeout(page.evaluate(inputBytes => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_css_postdraw_stream_arm;
    if (typeof armFunction !== 'function' ||
        typeof globalThis.__meleeWebStadiumFirstCssStreamCompare !== 'function')
      throw Error('Private CSS stream arm or host comparator is absent');
    const pointer = module._malloc(inputBytes.length);
    if (!pointer) throw Error('CSS stream input allocation failed');
    try {
      // Only fixed input-only PAD bytes enter WASM; expected rows remain above.
      module.HEAPU8.set(inputBytes, pointer);
      const result = armFunction(pointer, inputBytes.length);
      if (result !== 1) {
        const message = module._melee_web_native_menu_message();
        throw Error(message ? module.UTF8ToString(message) : 'CSS stream arm refused');
      }
      return result;
    } finally { module._free(pointer); }
  }, [...firstCssBrowserStreamInputs.bytes.postdraw_input]),
  Math.min(5000, remaining()), 'Arm input-only post-draw CSS stream');
  assert.equal(arm, 1);
  const armed = await timeout(readFirstCssBrowserStreamObservation(),
    Math.min(5000, remaining()), 'Armed CSS stream evidence before kick');
  report.scenario.first_css_stream_armed = armed;
  await persistReport();
  assert.equal(armed.native?.postdraw_stream?.status, 'armed');
  assert.equal(armed.native.running, 0);
  assert.equal(armed.host.rows.length, 0);
  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_css_postdraw_stream_kick;
    if (typeof kickFunction !== 'function') throw Error('CSS stream kick export is absent');
    const result = kickFunction();
    if (result !== 1) {
      const message = module._melee_web_native_menu_message();
      throw Error(message ? module.UTF8ToString(message) : 'CSS stream kick refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release bounded source-consumed CSS stream');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const stream = JSON.parse(module.UTF8ToString(pointer)).postdraw_stream;
    return stream?.terminal === true || stream?.complete === true || stream?.failed === true;
  }, null, {timeout: Math.min(60000, remaining())}),
  Math.min(60000, remaining()), 'CSS stream first mismatch, transition or bounded cap');
  const evidence = await timeout(readFirstCssBrowserStreamObservation(),
    Math.min(5000, remaining()), 'Terminal CSS stream native and host evidence');
  report.scenario.first_css_browser_stream = evidence;
  await persistReport();
  assertFirstCssBrowserStreamTerminal(evidence);
  report.scenario.css_stream_claim = evidence.native.postdraw_stream.outcome ===
    'stop_after_last_input_request'
    ? '147 additional tick/draw pairs compared; last additional input consumed and native transition retained unpaired; original final draw1577 unpaired. No SSS admission or transition-state equivalence.'
    : '148 additional tick/draw pairs compared; bounded pair cap reached without native transition. No SSS admission or transition equivalence.';
  return evidence.native;
}

async function readFirstCssBrowserFinalDrawObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstCssFinalDrawRead?.() ?? null};
  });
}

async function readFirstSssConstructorPairObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssPairRead?.() ?? null};
  });
}

async function readFirstSssConsumedTickObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssTickRead?.() ?? null};
  });
}

async function readFirstSssDrawObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssDrawRead?.() ?? null};
  });
}

async function readFirstSssPrefixObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssPrefixRead?.() ?? null};
  });
}

async function readFirstSssPairPostUnloadState() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {phase: module?._melee_web_native_menu_phase?.() ?? null,
      running: module?._melee_web_native_menu_running?.() ?? null,
      observation: pointer ? module.UTF8ToString(pointer) : null,
      first_sss_constructor_pair: observation?.first_sss_constructor_pair ?? null,
      first_sss_consumed_tick: observation?.first_sss_consumed_tick ?? null,
      first_sss_draw: observation?.first_sss_draw ?? null,
      first_sss_prefix: observation?.first_sss_prefix ?? null,
      first_sss_vs_entry: observation?.first_sss_vs_entry ?? null,
      first_sss_vs_setup: observation?.first_sss_vs_setup ?? null,
      matched_setup_cf: observation?.first_sss_vs_setup?.matched_setup_cf ?? null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') &&
        !document.querySelector('#disc').disabled};
  });
}

function assertFirstCssBrowserFinalDraw(evidence, stoppedStream,
    {expectedNativePhase = 1} = {}) {
  const observation = evidence?.native;
  const final = observation?.final_pending_css_draw;
  const host = evidence?.host;
  assert.ok(final && host, 'Final pending-CSS draw evidence is absent');
  assert.deepEqual(observation.postdraw_stream, stoppedStream,
    'Separate final draw changed the retained terminal stream');
  assert.equal(observation.native_phase, expectedNativePhase);
  assert.equal(observation.running, 0);
  for (const key of ['armed', 'kicked', 'attempted', 'captured', 'compared',
    'complete', 'frame_end_returned']) assert.equal(final[key], true, key);
  assert.equal(final.failed, false, final.error || 'Final CSS draw failed');
  assert.equal(final.error, null);
  assert.equal(final.input_ordinal, 148);
  assert.equal(final.consumed_pad_sequence, 1574);
  assert.equal(final.terminal_tick_sequence, 1575);
  assert.equal(final.draw_enter_sequence, 1576);
  assert.equal(final.draw_return_sequence, 1577);
  assert.equal(final.source_steps, 0);
  assert.equal(final.source_draws, 0);
  assert.equal(final.source_pending, false);
  assert.equal(final.host_draw_calls, 1);
  assert.equal(final.aurora_begin_calls, 1);
  assert.equal(final.aurora_end_calls, 1);
  assert.equal(host.hook_error, null);
  assert.deepEqual(host.status, {attempted: true, approved: true, failed: false});
  assert.equal(host.rows.length, 1);
  assert.equal(host.rows[0].phase, 'final_draw');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.rows[0].actual, final.actual_draw);
  const wanted = firstCssBrowserStreamInputs.expected.expected_pairs[147].expected_draw_return;
  for (const key of firstCssBrowserDrawInputs.drawExpected.comparison_fields)
    assert.deepEqual(final.actual_draw[key], wanted[key], `Final CSS draw differs at ${key}`);
  assert.equal(final.actual_draw.world_generation, stoppedStream.transition_snapshot.world_generation);
  for (const key of ['world_generation_stable', 'scene_owner_stable', 'seed_owner_stable'])
    assert.equal(final.actual_draw[key], true, key);
  assert.equal(final.actual_draw.source_scene, 1);
  assert.equal(final.actual_draw.menu_phase, 1);
  assert.equal(final.actual_draw.scene_kind, 8);
}

async function runFirstCssBrowserFinalPendingDraw() {
  // Preserve and assert the completed stream before authorizing any final draw.
  const stopped = report.scenario.first_css_browser_stream;
  assertFirstCssBrowserStreamTerminal(stopped);
  assert.equal(stopped.native.postdraw_stream.outcome, 'stop_after_last_input_request',
    'Final pending draw requires the retained result3 boundary, not the pair cap');
  const stoppedStream = structuredClone(stopped.native.postdraw_stream);
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedPair, terminal}) => {
    if (globalThis.__meleeWebStadiumFirstCssFinalDrawCompare ||
        globalThis.__meleeWebStadiumFirstCssFinalDrawRead)
      throw Error('Final CSS draw host comparator was already registered');
    const {createFirstCssFinalDrawComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstCssFinalDrawComparator(expectedPair, terminal);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstCssFinalDrawRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstCssFinalDrawCompare = (phase, actualJson) => {
      if (rows.length >= 1) { hookError = 'Final CSS draw callback repeated'; return false; }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      rows.push({phase, actual, approved});
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedPair: firstCssBrowserStreamInputs.expected.expected_pairs[147],
    terminal: stoppedStream.transition_snapshot}), Math.min(5000, remaining()),
  'Register separate host-only final CSS draw comparison');
  // No input or expected state is allocated/copied into WASM here.
  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_css_final_draw_arm;
    if (typeof fn !== 'function') throw Error('Final CSS draw arm export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Final CSS draw arm refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Arm one checked final pending-CSS draw');
  assert.equal(arm, 1);
  report.scenario.first_css_final_draw_armed = await timeout(
    readFirstCssBrowserFinalDrawObservation(), Math.min(5000, remaining()),
    'Final CSS draw evidence before kick');
  await persistReport();
  assert.deepEqual(report.scenario.first_css_final_draw_armed.native.postdraw_stream, stoppedStream);
  assert.equal(report.scenario.first_css_final_draw_armed.native.running, 0);
  assert.equal(report.scenario.first_css_final_draw_armed.native.final_pending_css_draw.armed, true);
  assert.equal(report.scenario.first_css_final_draw_armed.host.rows.length, 0);
  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_css_final_draw_kick;
    if (typeof fn !== 'function') throw Error('Final CSS draw kick export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Final CSS draw kick refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release one source draw without another tick');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const final = JSON.parse(module.UTF8ToString(pointer)).final_pending_css_draw;
    return final?.complete === true || final?.failed === true;
  }, null, {timeout: Math.min(30000, remaining())}), Math.min(30000, remaining()),
  'Final CSS draw comparison and paired Aurora end');
  const evidence = await timeout(readFirstCssBrowserFinalDrawObservation(),
    Math.min(5000, remaining()), 'Retain final CSS draw actual comparison before cleanup');
  report.scenario.first_css_final_pending_draw = evidence;
  await persistReport();
  assertFirstCssBrowserFinalDraw(evidence, stoppedStream);
  report.scenario.css_final_draw_claim =
    'Original final CSS DrawReturn1577 seven fields compared after147 additional pairs; terminal scheduler snapshot and raw pending/next remain unpaired. No SSS admission.';
  return evidence.native;
}

function assertFirstSssPostCaptureState(evidence, baseline) {
  const native = evidence?.native;
  const pair = native?.first_sss_constructor_pair;
  assert.ok(pair, 'Native SSS constructor-pair observation is absent');
  assert.equal(pair.captured, true, 'SSS constructor notes were not captured');
  assert.equal(pair.host_entered, true, 'SSS host-enter did not complete');
  assert.equal(pair.sss_host_tick_calls, 0, 'SSS host tick ran before terminal capture');
  assert.equal(pair.sss_host_draw_calls, 0, 'SSS host draw ran before terminal capture');
  assert.equal(native.running, 0, 'Host time continued after SSS constructor capture');
  assert.deepEqual(native.entry, baseline.entry,
    'Original typed CSS entry changed during SSS capture');
  assert.deepEqual(native.tick, baseline.tick,
    'Original typed CSS tick changed during SSS capture');
  assert.deepEqual(native.draw, baseline.draw,
    'Original typed CSS draw changed during SSS capture');
  assert.deepEqual(native.postdraw_stream, baseline.postdraw_stream,
    'Original CSS postdraw stream changed during SSS capture');
  assert.deepEqual(native.final_pending_css_draw, baseline.final_pending_css_draw,
    'Original final CSS draw changed during SSS capture');
}

async function runFirstSssConstructorPair() {
  const finalCss = report.scenario.first_css_final_pending_draw;
  const stoppedStream = report.scenario.first_css_browser_stream;
  assertFirstCssBrowserFinalDraw(finalCss, stoppedStream.native.postdraw_stream);
  assert.equal(stoppedStream.native.postdraw_stream.outcome,
    'stop_after_last_input_request');
  assert.equal(finalCss.native.native_phase, 1,
    'SSS pair requires the ordinary pending CSS leave to remain entered');
  assert.equal(finalCss.native.final_pending_css_draw.frame_end_returned, true);
  assert.equal(finalCss.native.final_pending_css_draw.complete, true);
  assert.equal(finalCss.native.final_pending_css_draw.compared, true);
  report.scenario.first_sss_constructor_pair = {
    expected_json: firstSssConstructorPairInputs.expected_identity,
    arm_snapshot: null,
    kick_result: null,
    observation: null,
    claim: null,
  };
  await persistReport();

  await timeout(page.evaluate(async ({moduleUrl, expectedPair}) => {
    if (globalThis.__meleeWebStadiumFirstSssPairCompare ||
        globalThis.__meleeWebStadiumFirstSssPairRead)
      throw Error('SSS pair host comparator was already registered');
    const {createFirstSssConstructorPairComparator, requestSynchronousApproval} =
      await import(moduleUrl);
    const comparator = createFirstSssConstructorPairComparator(expectedPair);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssPairRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssPairCompare = (phase, actualJson) => {
      if (rows.length >= 1) {
        hookError = 'SSS pair comparison callback repeated';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      // Retain the complete actual pair before invoking the strict compare.
      const row = {phase, actual, approved: null};
      rows.push(row);
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedPair: firstSssConstructorPairInputs.expected}),
  Math.min(5000, remaining()), 'Register host-only original SSS constructor-pair comparison');

  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_pair_arm;
    if (typeof fn !== 'function') throw Error('Original SSS pair arm export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Original SSS pair arm refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Arm checked pending CSS-to-SSS pair route');
  assert.equal(arm, 1);
  const beforeKick = await timeout(readFirstSssConstructorPairObservation(),
    Math.min(5000, remaining()), 'Retain armed SSS constructor-pair state');
  report.scenario.first_sss_constructor_pair.arm_snapshot = beforeKick;
  await persistReport();
  assert.equal(beforeKick.native?.final_pending_css_draw?.complete, true);
  assert.equal(beforeKick.native?.final_pending_css_draw?.failed, false);
  assert.equal(beforeKick.native?.first_sss_constructor_pair?.armed, true);
  assert.equal(beforeKick.native?.first_sss_constructor_pair?.kicked, false);
  assert.deepEqual(beforeKick.native?.postdraw_stream, stoppedStream.native.postdraw_stream,
    'SSS arm changed the approved CSS source stream');
  assert.deepEqual(beforeKick.native?.final_pending_css_draw,
    finalCss.native.final_pending_css_draw,
    'SSS arm changed the approved final CSS draw observation');
  assert.equal(beforeKick.host?.rows?.length, 0);

  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_pair_kick;
    if (typeof fn !== 'function') throw Error('Original SSS pair kick export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Original SSS pair kick refused');
    }
    return result;
  }), Math.min(5000, remaining()),
  'Complete pending CSS leave/rebuild and enter SSS without another input or tick');
  report.scenario.first_sss_constructor_pair.kick_result = kick;
  await persistReport();
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const pair = JSON.parse(module.UTF8ToString(pointer)).first_sss_constructor_pair;
    return pair?.complete === true || pair?.failed === true;
  }, null, {timeout: Math.min(LIMITS.sourceTransitionMs, remaining())}),
  Math.min(LIMITS.sourceTransitionMs, remaining()),
  'Original SSS host-enter constructor pair and first mismatch');

  const evidence = await timeout(readFirstSssConstructorPairObservation(),
    Math.min(5000, remaining()), 'Retain both original SSS constructor notes before cleanup');
  report.scenario.first_sss_constructor_pair.observation = evidence;
  report.scenario.first_sss_constructor_pair.claim =
    'SSS constructor observation attempt retained; capture, stop guarantees and semantic comparison have not yet passed their checks. No equivalence or acceptance claim.';
  await persistReport();

  const postcaptureCssBaseline = await timeout(readFirstCssBrowserFinalDrawObservation(),
    Math.min(5000, remaining()), 'Retain the current CSS baseline after SSS capture');
  report.scenario.first_sss_constructor_pair.postcapture_css_baseline = postcaptureCssBaseline;
  await persistReport();

  const nativePair = evidence.native?.first_sss_constructor_pair;
  assertFirstSssPostCaptureState(evidence, {
    entry: report.scenario.first_css_browser_draw.entry,
    tick: report.scenario.first_css_browser_draw.tick,
    draw: report.scenario.first_css_browser_draw.draw,
    postdraw_stream: stoppedStream.native.postdraw_stream,
    final_pending_css_draw: finalCss.native.final_pending_css_draw,
  });
  assertFirstCssBrowserEntry(evidence.native, firstCssBrowserDrawInputs.contextExpected);
  assertFirstCssBrowserTick(evidence.native, firstCssBrowserDrawInputs.tickExpected);
  assertFirstCssBrowserDraw(evidence.native, firstCssBrowserDrawInputs.drawExpected);
  assertFirstCssBrowserFinalDraw(postcaptureCssBaseline,
    stoppedStream.native.postdraw_stream, {expectedNativePhase: 3});
  const host = evidence.host;
  assert.ok(nativePair, 'Native observation omitted the first_sss_constructor_pair sibling');
  assert.equal(nativePair.failed, false, nativePair.error || 'Native SSS pair failed');
  assert.equal(nativePair.status, 'complete');
  for (const key of ['armed', 'kicked', 'attempted', 'captured', 'compared', 'complete'])
    assert.equal(nativePair[key], true, key);
  assert.equal(nativePair.error, null);
  assert.equal(nativePair.host_entered, true);
  assert.equal(nativePair.sss_host_tick_calls, 0);
  assert.equal(nativePair.sss_host_draw_calls, 0);
  assert.equal(evidence.native.running, 0,
    'Host time continued after paired SSS constructor capture');
  assert.equal(host?.hook_error, null);
  assert.equal(host?.status?.attempted, true);
  assert.equal(host?.status?.approved, true);
  assert.equal(host?.status?.failed, false);
  assert.equal(host?.status?.first_mismatch, null);
  assert.equal(host?.status?.error, null);
  assert.equal(host?.rows?.length, 1);
  assert.equal(host.rows[0].phase, 'sss_pair');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.status.actual_pair, host.rows[0].actual);
  assert.deepEqual(host.rows[0].actual.entry, nativePair.entry);
  assert.deepEqual(host.rows[0].actual.returned, nativePair.returned);
  assert.equal(nativePair.entry.host_entered, false);
  assert.equal(nativePair.returned.host_entered, false);
  assert.equal(nativePair.entry.session_ticks, nativePair.returned.session_ticks);
  report.scenario.first_sss_constructor_pair.claim =
    'Original SSS OnEnter entry1579/return1603 approved with both passive notes retained; zero SSS host ticks/draws, before SSS input or progression. No whole-session acceptance.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-constructor-pair');
  return evidence.native;
}

function assertFirstSssConsumedTickStopState(evidence, cssBaseline, constructorPair) {
  const native = evidence?.native;
  const tick = native?.first_sss_consumed_tick;
  assert.ok(tick, 'Native SSS consumed-tick observation is absent');
  assert.equal(tick.attempted, true, 'The one authorized SSS host tick was not attempted');
  assert.equal(tick.host_tick_calls, 1, 'The authorized SSS host tick count differs');
  assert.equal(tick.host_draw_calls, 0, 'An SSS host draw ran after consumed-tick capture');
  assert.equal(native.running, 0, 'Host source time continued after the one-shot SSS tick');
  assert.equal(tick.post_host_frame_captured, true,
    'The actual post-host frame was not captured after the scheduler sample');
  assert.equal(tick.post_host_frame, 1,
    'The one host tick did not finish at source frame one');
  assert.deepEqual(native.first_sss_constructor_pair, constructorPair,
    'The approved constructor pair or its zero-tick/draw counters changed during the tick');
  assert.deepEqual(native.entry, cssBaseline.entry,
    'Original typed CSS entry changed during SSS tick capture');
  assert.deepEqual(native.tick, cssBaseline.tick,
    'Original typed CSS tick changed during SSS tick capture');
  assert.deepEqual(native.draw, cssBaseline.draw,
    'Original typed CSS draw changed during SSS tick capture');
  assert.deepEqual(native.postdraw_stream, cssBaseline.postdraw_stream,
    'Original CSS postdraw stream changed during SSS tick capture');
  assert.deepEqual(native.final_pending_css_draw, cssBaseline.final_pending_css_draw,
    'Original final CSS draw changed during SSS tick capture');
}

function retainFirstSssConsumedTickEvidence(scenario, evidence, kickResult) {
  scenario.kick_result = kickResult;
  scenario.observation = evidence;
  scenario.claim = 'One-shot SSS scheduler-end sample attempt retained; comparison and protocol gates are pending.';
}

function assertFirstSssConsumedTickCompared(evidence, kickResult) {
  const nativeTick = evidence?.native?.first_sss_consumed_tick;
  const host = evidence?.host;
  assert.equal(kickResult?.result, 1,
    kickResult?.message || 'SSS consumed-tick kick failed');
  assert.equal(nativeTick?.captured, true, 'Scheduler-end SSS state was not captured');
  assert.equal(nativeTick?.host_draw_calls, 0);
  assert.equal(nativeTick?.clock_post_succeeded, true,
    'The one SSS host tick did not complete its ordinary menu clock post');
  assert.equal(nativeTick?.tick_result, 1, 'The ordinary SSS host tick did not return success');
  assert.equal(nativeTick?.transition_requested, false,
    'The single retained PAD input requested a scene transition');
  assert.equal(nativeTick?.post_host_frame_captured, true);
  assert.equal(nativeTick?.post_host_frame, 1);
  assert.equal(nativeTick?.failed, false, nativeTick?.error || 'SSS consumed-tick host failed');
  assert.equal(nativeTick?.compared, true);
  assert.equal(nativeTick?.complete, true);
  assert.equal(host?.hook_error, null);
  assert.equal(host?.status?.attempted, true);
  assert.equal(host?.status?.approved, true);
  assert.equal(host?.status?.failed, false);
  assert.equal(host?.status?.first_mismatch, null);
  assert.equal(host?.rows?.length, 1);
  assert.equal(host.rows[0].phase, 'scheduler_end');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.status.actual, host.rows[0].actual);
}

async function runFirstSssConsumedPadTick(constructorNative) {
  assert.ok(constructorNative?.first_sss_constructor_pair,
    'First SSS consumed tick requires the retained constructor-pair observation');
  const constructorPair = constructorNative.first_sss_constructor_pair;
  assert.equal(constructorPair.complete, true);
  assert.equal(constructorPair.compared, true);
  assert.equal(constructorPair.failed, false);
  assert.equal(constructorPair.host_entered, true);
  assert.equal(constructorPair.sss_host_tick_calls, 0);
  assert.equal(constructorPair.sss_host_draw_calls, 0);
  const cssBaseline = report.scenario.first_sss_constructor_pair.postcapture_css_baseline.native;
  assert.ok(cssBaseline, 'First SSS tick requires the retained postconstructor CSS baseline');
  report.scenario.first_sss_consumed_tick = {
    expected_json: firstSssConsumedPadTickInputs.identities.expected_json,
    input_bundle: firstSssConsumedPadTickInputs.identities.input_bundle,
    arm_snapshot: null,
    kick_result: null,
    observation: null,
    claim: 'One-shot SSS scheduler-end sample attempt retained; comparison and protocol gates are pending.',
  };
  await persistReport();

  await timeout(page.evaluate(async ({moduleUrl, expectedTick, pair}) => {
    if (globalThis.__meleeWebStadiumFirstSssTickCompare ||
        globalThis.__meleeWebStadiumFirstSssTickRead)
      throw Error('SSS consumed-tick comparator was already registered');
    const {createFirstSssConsumedPadTickComparator, requestSynchronousApproval} =
      await import(moduleUrl);
    const comparator = createFirstSssConsumedPadTickComparator(expectedTick, pair);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssTickRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssTickCompare = (phase, actualJson) => {
      if (rows.length >= 1) {
        hookError = 'SSS consumed-tick comparison callback repeated';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const row = {phase, actual, approved: null};
      rows.push(row);
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedTick: firstSssConsumedPadTickInputs.expected, pair: constructorPair}),
  Math.min(5000, remaining()), 'Register exact original SSS frame-zero comparison');

  const arm = await timeout(page.evaluate(bundle => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_sss_tick_arm;
    if (typeof armFunction !== 'function') throw Error('SSS consumed-tick arm export is absent');
    if (module._melee_web_native_menu_running() !== 0)
      throw Error('SSS consumed-tick arm requires the stopped constructor-only host');
    const pointer = module._malloc(bundle.length);
    if (!pointer) throw Error('SSS consumed-tick input allocation failed');
    try {
      module.HEAPU8.set(bundle, pointer);
      const result = armFunction(pointer, bundle.length);
      const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
      return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
    } finally { module._free(pointer); }
  }, Array.from(firstSssConsumedPadTickInputs.bytes.input_bundle)),
  Math.min(5000, remaining()), 'Arm one exact consumed SSS PAD input bundle');
  const armSnapshot = await timeout(readFirstSssConsumedTickObservation(),
    Math.min(5000, remaining()), 'Retain armed SSS consumed-tick state');
  report.scenario.first_sss_consumed_tick.arm_snapshot = armSnapshot;
  await persistReport();
  assert.equal(arm.result, 1, arm.message || 'SSS consumed-tick arm refused');
  assert.equal(armSnapshot.native?.first_sss_consumed_tick?.armed, true);
  assert.equal(armSnapshot.native?.first_sss_consumed_tick?.kicked, false);
  assert.deepEqual(armSnapshot.native?.first_sss_constructor_pair, constructorPair,
    'SSS tick arm changed the approved constructor pair');
  assert.deepEqual(armSnapshot.native?.entry, cssBaseline.entry,
    'SSS tick arm changed the original typed CSS entry');
  assert.deepEqual(armSnapshot.native?.tick, cssBaseline.tick,
    'SSS tick arm changed the original typed CSS tick');
  assert.deepEqual(armSnapshot.native?.draw, cssBaseline.draw,
    'SSS tick arm changed the original typed CSS draw');
  assert.deepEqual(armSnapshot.native?.postdraw_stream, cssBaseline.postdraw_stream,
    'SSS tick arm changed the original CSS stream');
  assert.deepEqual(armSnapshot.native?.final_pending_css_draw, cssBaseline.final_pending_css_draw,
    'SSS tick arm changed the approved final CSS draw');

  const kickResult = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_sss_tick_kick;
    if (typeof kickFunction !== 'function') throw Error('SSS consumed-tick kick export is absent');
    const result = kickFunction();
    const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
    return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
  }), Math.min(10000, remaining()), 'Run exactly one ordinary SSS host tick and stop before draw');
  report.scenario.first_sss_consumed_tick.kick_result = kickResult;
  await persistReport();

  const evidence = await timeout(readFirstSssConsumedTickObservation(),
    Math.min(5000, remaining()), 'Retain scheduler-end sample before success checks');
  retainFirstSssConsumedTickEvidence(report.scenario.first_sss_consumed_tick, evidence, kickResult);
  await persistReport();

  // Keep stop, counter and old-boundary checks independent from a source-state
  // mismatch. A mismatch remains a failing comparison after these assertions.
  assertFirstSssConsumedTickStopState(evidence, cssBaseline, constructorPair);
  assertFirstSssConsumedTickCompared(evidence, kickResult);
  report.scenario.first_sss_consumed_tick.claim =
    'Original SSS consumed PAD row1604 and frame-zero scheduler-end row1608 matched; exactly one normal host tick completed to frame1 with no draw or transition. This is one bounded boundary comparison, not whole-session acceptance.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-consumed-pad-tick');
  return evidence.native;
}

function assertSssOldBaselineUnchanged(native, baseline, constructorPair, consumedTick,
    firstDraw = null) {
  assert.deepEqual(native.first_sss_constructor_pair, constructorPair,
    'SSS continuation changed the approved constructor pair');
  assert.deepEqual(native.first_sss_consumed_tick, consumedTick,
    'SSS continuation changed the approved consumed-tick observation');
  assert.deepEqual(native.entry, baseline.entry, 'Original typed CSS entry changed during SSS continuation');
  assert.deepEqual(native.tick, baseline.tick, 'Original typed CSS tick changed during SSS continuation');
  assert.deepEqual(native.draw, baseline.draw, 'Original typed CSS draw changed during SSS continuation');
  assert.deepEqual(native.postdraw_stream, baseline.postdraw_stream,
    'Original CSS postdraw stream changed during SSS continuation');
  assert.deepEqual(native.final_pending_css_draw, baseline.final_pending_css_draw,
    'Original final CSS draw changed during SSS continuation');
  if (firstDraw) assert.deepEqual(native.first_sss_draw, firstDraw,
    'SSS prefix changed the approved first SSS draw observation');
}

async function runFirstSssDraw(constructorNative) {
  const pair = constructorNative?.first_sss_constructor_pair;
  const consumedTick = constructorNative?.first_sss_consumed_tick;
  assert.ok(pair && consumedTick, 'First SSS draw requires completed constructor and consumed-tick observations');
  const baseline = report.scenario.first_sss_constructor_pair.postcapture_css_baseline.native;
  const expected = firstSssDrawInputs.expected;
  const helperSource = firstCssBrowserStreamInputs.helperSource;
  report.scenario.first_sss_draw = {
    expected_json: firstSssDrawInputs.identity,
    arm_snapshot: null, kick_result: null, observation: null,
    claim: 'First SSS draw attempt retained; actual snapshots and comparison are pending.',
  };
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedDraw, pair, tick}) => {
    if (globalThis.__meleeWebStadiumFirstSssDrawCompare ||
        globalThis.__meleeWebStadiumFirstSssDrawRead)
      throw Error('First SSS draw comparator was already registered');
    const {createFirstSssDrawComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstSssDrawComparator(expectedDraw, pair, tick);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssDrawRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssDrawCompare = (phase, actualJson) => {
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const row = {phase, actual, approved: null};
      rows.push(row);
      if (rows.length > 2) { hookError = 'First SSS draw callback repeated'; return false; }
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' + Buffer.from(helperSource).toString('base64'),
    expectedDraw: expected, pair, tick: consumedTick}), Math.min(5000, remaining()),
  'Register exact original SSS first-draw comparison');

  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_draw_arm;
    if (typeof fn !== 'function') throw Error('First SSS draw arm export is absent');
    if (module._melee_web_native_menu_running() !== 0)
      throw Error('First SSS draw arm requires the stopped post-tick host');
    const result = fn();
    const pointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
    return {result, message: pointer ? module.UTF8ToString(pointer) : null};
  }), Math.min(5000, remaining()), 'Arm one original first SSS draw');
  const armSnapshot = await timeout(readFirstSssDrawObservation(),
    Math.min(5000, remaining()), 'Retain first SSS draw arm state');
  report.scenario.first_sss_draw.arm_snapshot = armSnapshot;
  await persistReport();
  const armAudio = firstSssAudioAccounting(armSnapshot.native?.first_sss_draw, 0);
  report.scenario.first_sss_draw.arm_audio_accounting = armAudio;
  await persistReport();
  assert.equal(armAudio.valid, true, armAudio.error || 'First SSS draw arm changed audio carry');

  let kickResult = null;
  if (arm.result === 1) {
    kickResult = await timeout(page.evaluate(() => {
      const module = globalThis.Module;
      const fn = module?._melee_web_native_menu_stadium_first_sss_draw_kick;
      if (typeof fn !== 'function') throw Error('First SSS draw kick export is absent');
      const result = fn();
      const pointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
      return {result, message: pointer ? module.UTF8ToString(pointer) : null};
    }), Math.min(5000, remaining()), 'Run the single approved first SSS draw');
  }
  report.scenario.first_sss_draw.kick_result = kickResult;
  await persistReport();
  if (arm.result === 1) {
    await timeout(page.waitForFunction(() => {
      const pointer = Module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
      if (!pointer) return false;
      const draw = JSON.parse(Module.UTF8ToString(pointer)).first_sss_draw;
      return draw?.complete === true || draw?.failed === true;
    }, null, {timeout: Math.min(10000, remaining())}),
    Math.min(10000, remaining()), 'First SSS draw result or retained mismatch');
  }
  const evidence = await timeout(readFirstSssDrawObservation(),
    Math.min(5000, remaining()), 'Retain first SSS draw snapshots and first mismatch');
  report.scenario.first_sss_draw.observation = evidence;
  await persistReport();

  assert.ok(evidence.native, 'First SSS draw native observation is absent');
  assertSssOldBaselineUnchanged(evidence.native, baseline, pair, consumedTick);
  assert.equal(evidence.native.running, 0, 'Host time resumed after the first SSS draw');
  const draw = evidence.native.first_sss_draw;
  assert.ok(draw, 'Native observation omitted first_sss_draw');
  const audio = firstSssAudioAccounting(draw, 0);
  report.scenario.first_sss_draw.audio_accounting = audio;
  await persistReport();
  assert.equal(audio.valid, true, audio.error || 'First SSS draw changed audio carry');
  assert.equal(draw.host_tick_calls, 0, 'First-draw diagnostic ran an extra SSS tick');
  assert.equal(draw.host_draw_calls, 1, 'First-draw diagnostic did not execute exactly one SSS draw');
  assert.equal(draw.aurora_begin_calls, 1);
  assert.equal(draw.aurora_end_calls, 1);
  assert.equal(draw.frame_end_returned, true);
  assert.equal(arm.result, 1, arm.message || 'First SSS draw arm refused');
  assert.equal(kickResult?.result, 1, kickResult?.message || 'First SSS draw kick failed');
  assert.equal(draw.failed, false, draw.error || 'First SSS draw failed');
  for (const key of ['armed', 'kicked', 'attempted', 'captured', 'compared', 'complete'])
    assert.equal(draw[key], true, key);
  assert.equal(draw.error, null);
  assert.equal(evidence.host?.hook_error, null);
  assert.equal(evidence.host?.status?.approved, true);
  assert.equal(evidence.host?.status?.failed, false);
  assert.equal(evidence.host?.status?.first_mismatch, null);
  assert.equal(evidence.host?.rows?.length, 2);
  assert.deepEqual(evidence.host.rows.map(row => row.phase), ['draw_enter', 'draw_return']);
  assert.ok(evidence.host.rows.every(row => row.approved === true));
  assert.deepEqual(draw.draw_enter, evidence.host.rows[0].actual);
  assert.deepEqual(draw.draw_return, evidence.host.rows[1].actual);
  report.scenario.first_sss_draw.claim =
    'Original SSS DrawEnter1609 and DrawReturn1610 matched after the approved constructor and consumed-PAD prerequisites. No additional SSS tick or continuation is included.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-draw');
  return evidence.native;
}

async function runFirstSssPrefix(constructorNative, firstDrawNative) {
  const pair = constructorNative?.first_sss_constructor_pair;
  const consumedTick = constructorNative?.first_sss_consumed_tick;
  const firstDraw = firstDrawNative?.first_sss_draw;
  assert.ok(pair && consumedTick && firstDraw,
    'SSS prefix requires completed constructor, consumed-tick and first-draw prerequisites');
  const baseline = report.scenario.first_sss_constructor_pair.postcapture_css_baseline.native;
  const expected = firstSssPrefixInputs.expected;
  report.scenario.first_sss_prefix = {
    expected_json: firstSssPrefixInputs.identity,
    input_bundle: firstSssPrefixInputs.inputIdentity,
    arm_snapshot: null, kick_result: null, observation: null,
    claim: 'Bounded SSS prefix attempt retained; per-boundary comparison and final stop checks are pending.',
  };
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedPrefix, inputRows, pair, tick, draw}) => {
    if (globalThis.__meleeWebStadiumFirstSssPrefixCompare ||
        globalThis.__meleeWebStadiumFirstSssPrefixRead)
      throw Error('First SSS prefix comparator was already registered');
    const {createFirstSssPrefixComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstSssPrefixComparator(expectedPrefix, inputRows, pair, tick, draw);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssPrefixRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssPrefixCompare = (phase, actualJson) => {
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const row = {phase, actual, approved: null};
      rows.push(row);
      if (rows.length > 374) { hookError = 'First SSS prefix callback exceeded its fixed sample cap'; return false; }
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' + Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedPrefix: expected, inputRows: firstSssPrefixInputs.inputRows,
    pair, tick: consumedTick, draw: firstDraw}), Math.min(5000, remaining()),
  'Register exact 124-record SSS prefix and passive exit comparisons');

  const arm = await timeout(page.evaluate(bundle => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_prefix_arm;
    if (typeof fn !== 'function') throw Error('First SSS prefix arm export is absent');
    if (module._melee_web_native_menu_running() !== 0)
      throw Error('First SSS prefix arm requires the stopped first-draw host');
    const pointer = module._malloc(bundle.length);
    if (!pointer) throw Error('First SSS prefix input allocation failed');
    try {
      module.HEAPU8.set(bundle, pointer);
      const result = fn(pointer, bundle.length);
      const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
      return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
    } finally { module._free(pointer); }
  }, Array.from(firstSssPrefixInputs.inputBytes)),
  Math.min(5000, remaining()), 'Arm the exact input-only 124-record SSS prefix');
  const armSnapshot = await timeout(readFirstSssPrefixObservation(),
    Math.min(5000, remaining()), 'Retain armed SSS prefix state');
  report.scenario.first_sss_prefix.arm_snapshot = armSnapshot;
  await persistReport();
  const armAudio = firstSssAudioAccounting(armSnapshot.native?.first_sss_prefix, 0);
  report.scenario.first_sss_prefix.arm_audio_accounting = armAudio;
  await persistReport();
  assert.equal(armAudio.valid, true, armAudio.error || 'SSS prefix arm changed audio carry');
  assert.equal(arm.result, 1, arm.message || 'First SSS prefix arm refused');
  assert.equal(armSnapshot.native?.first_sss_prefix?.armed, true);
  assert.equal(armSnapshot.native?.first_sss_prefix?.kicked, false);
  assertSssOldBaselineUnchanged(armSnapshot.native, baseline, pair, consumedTick, firstDraw);

  const kickResult = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_prefix_kick;
    if (typeof fn !== 'function') throw Error('First SSS prefix kick export is absent');
    const result = fn();
    const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
    return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
  }), Math.min(10000, remaining()), 'Start the finite SSS input/tick/draw continuation');
  report.scenario.first_sss_prefix.kick_result = kickResult;
  await persistReport();
  await timeout(page.waitForFunction(() => {
    const pointer = Module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const prefix = JSON.parse(Module.UTF8ToString(pointer)).first_sss_prefix;
    return prefix?.complete === true || prefix?.failed === true;
  }, null, {timeout: remaining()}), remaining(),
  'Finite SSS prefix complete result or first retained mismatch');

  const evidence = await timeout(readFirstSssPrefixObservation(),
    Math.min(10000, remaining()), 'Retain all SSS prefix callbacks and passive exit notes');
  report.scenario.first_sss_prefix.observation = evidence;
  await persistReport();

  assert.ok(evidence.native, 'SSS prefix native observation is absent');
  assertSssOldBaselineUnchanged(evidence.native, baseline, pair, consumedTick, firstDraw);
  assert.equal(evidence.native.running, 0, 'Host time continued beyond bounded SSS prefix');
  const prefix = evidence.native.first_sss_prefix;
  assert.ok(prefix, 'Native observation omitted first_sss_prefix');
  const audio = firstSssAudioAccounting(prefix,
    Number.isSafeInteger(prefix.matched_ticks) ? prefix.matched_ticks : -1);
  report.scenario.first_sss_prefix.audio_accounting = audio;
  await persistReport();
  assert.equal(prefix.matched_ticks <= prefix.host_tick_calls, true,
    'Matched SSS tick callbacks exceed consumed host tick attempts');
  assert.equal(prefix.matched_draw_enters <= prefix.host_draw_calls, true,
    'Matched SSS draw-enter callbacks exceed consumed host draw attempts');
  assert.equal(prefix.matched_draw_returns <= prefix.matched_draw_enters, true,
    'SSS draw-return approvals exceed draw-enter approvals');
  assert.equal(prefix.host_draw_calls <= prefix.matched_ticks, true,
    'A draw was attempted without a preceding approved tick');
  assert.equal(audio.valid, true, audio.error || 'SSS prefix audio carry differs from approved ticks');
  assert.equal(prefix.host_tick_calls, 124, 'SSS prefix host tick count differs from fixed bound');
  assert.equal(prefix.host_draw_calls, 124, 'SSS prefix host draw count differs from fixed bound');
  assert.equal(prefix.matched_ticks, 124);
  assert.equal(prefix.matched_draw_enters, 124);
  assert.equal(prefix.matched_draw_returns, 124);
  assert.equal(prefix.aurora_begin_calls, 124);
  assert.equal(prefix.aurora_end_calls, 124);
  assert.equal(prefix.frame_end_returned, true);
  assert.equal(prefix.last_tick_result, 3);
  assert.equal(prefix.transition_requested, true,
    'Final source request must remain pending through its checked draw');
  assert.equal(arm.result, 1, arm.message || 'First SSS prefix arm refused');
  assert.equal(kickResult.result, 1, kickResult.message || 'First SSS prefix kick failed');
  assert.equal(prefix.failed, false, prefix.error || 'SSS prefix failed');
  assert.equal(prefix.complete, true);
  assert.equal(prefix.error, null);
  assert.ok(prefix.exit_note, 'Passive SSS exit note was not retained');
  assert.equal(prefix.exit_note.phase, 'sss_exit');
  assert.equal(prefix.selected_stage_captured, true);
  assert.deepEqual(prefix.selected_stage_note, {index: 18, kind: 3});
  assert.ok(prefix.prepared_output_unpaired &&
    typeof prefix.prepared_output_unpaired === 'object',
    'Native observation must retain the actual prepared VS output');
  assert.equal(prefix.prepared_output_comparison, 'unpaired',
    'Prepared VS output must remain outside source comparison');
  report.scenario.first_sss_prefix.prepared_output_unpaired = {
    actual: prefix.prepared_output_unpaired, comparison: 'unpaired'};
  assert.equal(evidence.host?.hook_error, null);
  assert.equal(evidence.host?.status?.approved, true);
  assert.equal(evidence.host?.status?.failed, false);
  assert.equal(evidence.host?.status?.attempted, 374);
  assert.equal(evidence.host?.status?.first_mismatch, null);
  assert.equal(evidence.host?.rows?.length, 374);
  assert.ok(evidence.host.rows.every(row => row.approved === true));
  assert.equal(evidence.host.rows[0].phase, 'scheduler_end');
  assert.equal(evidence.host.rows[371].phase, 'draw_return');
  assert.equal(evidence.host.rows[372].phase, 'sss_exit');
  assert.equal(evidence.host.rows[373].phase, 'selected_stage');
  assert.deepEqual(evidence.host.rows[372].actual, prefix.exit_note);
  assert.deepEqual(evidence.host.rows[373].actual, prefix.selected_stage_note);
  report.scenario.first_sss_prefix.claim =
    '124 exact SSS consumed-PAD/tick/draw records matched, including the final retained transition request and checked pending draw; the passive row2426 exit and separate row2427 selected-stage tuple matched. Prepared VS output remains unpaired; no ordinary VS scene entry/setup or whole-session acceptance is claimed.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-prefix');
  return evidence.native;
}

async function readFirstSssVsEntryObservation(matchedActionOnly = false) {
  const native = await page.evaluate(({requireAsyncifyIdle, matchedActionOnly} = {}) => {
    const module = globalThis.Module;
    if (requireAsyncifyIdle &&
        (typeof module?.stadiumMatchedSetupNativeIdle !== 'function' ||
         module.stadiumMatchedSetupNativeIdle() !== true))
      return {__native_asyncify_idle: false};
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    const native = observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null;
    // Preserve callback ordering and thrown errors, but keep its unused rows in-page.
    globalThis.__meleeWebStadiumFirstSssPrefixRead?.() ?? null;
    // The action loop consumes only this complete receipt. Keep the original
    // getter, parse and callback order; full setup/final/failure reads stay intact.
    if (matchedActionOnly && native) return {
      first_sss_vs_setup: {matched_setup_cf: native.first_sss_vs_setup?.matched_setup_cf ?? null},
      native_phase: native.native_phase, running: native.running,
    };
    return native;
  }, {requireAsyncifyIdle: firstSssVsSetupCfExplicitStep, matchedActionOnly});
  if (native?.__native_asyncify_idle === false) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    throw Error('Asyncify is not idle; VS Entry native observation was suppressed');
  }
  return native;
}

async function assertMatchedSetupCfAsyncifyIdle(label) {
  if (!firstSssVsSetupCfExplicitStep) return true;
  const idle = await timeout(page.evaluate(() => {
    const predicate = globalThis.Module?.stadiumMatchedSetupNativeIdle;
    return typeof predicate === 'function' && predicate() === true;
  }), Math.min(2000, remaining()), label);
  if (idle !== true) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    throw Error('Asyncify is not idle; native operation was suppressed');
  }
  return true;
}
async function retireMatchedSetupCfExplicitNative({cleanupPath = false} = {}) {
  if (!matchedSetupCfExplicitArmed)
    throw Error('Explicit C/F retirement requested before the native owner was armed');
  if (matchedSetupCfExplicitRetirementAttempted)
    throw Error('Explicit C/F retirement is one-use and was already attempted');
  matchedSetupCfExplicitRetirementAttempted = true;
  matchedSetupCfExplicitNativeAccessClosed = true;
  unloadAttempted = true;
  report.cleanup.unload_attempted = true;
  report.cleanup.unload_method = 'one-shot-native-retirement';
  report.cleanup.matched_setup_cf_retirement ??= {
    attempts: 0, readiness_observations: 0,
    javascript_idbfs_persistence: 'not_run',
    ordinary_unload_cache_parity: 'not_claimed',
  };
  const evidence = report.cleanup.matched_setup_cf_retirement;
  evidence.attempts += 1;
  const budget = () => cleanupPath
    ? cleanupRemaining(LIMITS.cleanupMs) : remaining(LIMITS.unloadMs);
  let ready;
  for (;;) {
    const response = await timeout(executeMatchedSetupCfNativeCommand({
      kind: 'retire-ready', requireAsyncifyIdle: true}),
    Math.min(1000, budget()), 'Check explicit C/F retirement readiness');
    ++evidence.readiness_observations;
    if (response?.asyncify_idle !== true) {
      matchedSetupCfExplicitAsyncifyUnsafe = true;
      throw Error('Asyncify idle proof failed before native retirement');
    }
    ready = response.ready;
    if (ready === 1) break;
    if (ready !== 0)
      throw Error(response?.message || 'Native owner refused explicit C/F retirement readiness');
    await page.waitForTimeout(Math.min(20, budget()));
  }
  evidence.readiness = 'ready';
  const response = await timeout(executeMatchedSetupCfNativeCommand({
    kind: 'retire-explicit', requireAsyncifyIdle: true}),
  Math.min(LIMITS.unloadMs, budget()), 'Await explicit C/F native retirement');
  if (response?.asyncify_idle !== true) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    throw Error('Asyncify did not return to a safe idle state after native retirement');
  }
  evidence.result = response.result;
  evidence.retirement = response.retirement;
  evidence.post_retirement_native = {phase: response.phase, running: response.running};
  evidence.pipeline_cache_status_before_shutdown =
    response.retirement?.pipeline_cache_status_before_shutdown ?? null;
  evidence.pipeline_cache_status_after_shutdown =
    response.retirement?.pipeline_cache_status_after_shutdown ?? null;
  evidence.javascript_idbfs_persistence = 'not_run';
  evidence.ordinary_unload_cache_parity = 'not_claimed';
  await persistReport();
  if (response.result !== 1 || response.retirement?.native_retired !== true ||
      response.retirement?.terminal !== true || response.retirement?.error !== null ||
      response.phase !== 0 || response.running !== 0)
    throw Error(response.message || response.retirement?.error ||
      'Explicit C/F native retirement did not prove a stopped, retired owner');
  report.cleanup.unload_completed = true;
  report.cleanup.native_retired = true;
  matchedSetupCfExplicitNativeAccessClosed = false;
  return response.retirement;
}

async function unloadFirstCssDiagnosticOwner() {
  report.cleanup.unload_attempted = true;
  unloadAttempted = true;
  if (matchedSetupCfExplicitArmed) {
    await retireMatchedSetupCfExplicitNative();
    return;
  }
  await assertMatchedSetupCfAsyncifyIdle(
    'Check Asyncify before First-CSS diagnostic native menu unload');
  await timeout(driver.unload(), LIMITS.unloadMs,
    'First-CSS diagnostic native menu unload');
  report.cleanup.unload_completed = true;
}

async function readFirstCssDiagnosticPostUnloadSnapshot() {
  let afterUnload;
  try {
    afterUnload = await timeout(page.evaluate(requireAsyncifyIdle => {
      const module = globalThis.Module;
      if (requireAsyncifyIdle &&
          (typeof module?.stadiumMatchedSetupNativeIdle !== 'function' ||
           module.stadiumMatchedSetupNativeIdle() !== true))
        return {__native_asyncify_idle: false};
      const pointer = module._melee_web_native_menu_stadium_first_css_draw_observe?.();
      const observationText = pointer ? module.UTF8ToString(pointer) : null;
      const observation = observationText ? JSON.parse(observationText) : null;
      const retainedTextTail = observation?.schema ===
        'melee-web-stadium-first-sss-text-tail-retained-v1';
      return {phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running(),
        observation: retainedTextTail ? null : observationText,
        retained_text_tail_wrapper: retainedTextTail,
        first_sss_constructor_pair: observation?.first_sss_constructor_pair ?? null,
        first_sss_consumed_tick: observation?.first_sss_consumed_tick ?? null,
        first_sss_draw: observation?.first_sss_draw ?? null,
        first_sss_prefix: observation?.first_sss_prefix ?? null,
        first_sss_vs_entry: observation?.first_sss_vs_entry ?? null,
        first_sss_vs_setup: observation?.first_sss_vs_setup ?? null,
        matched_setup_cf: observation?.first_sss_vs_setup?.matched_setup_cf ?? null,
        error: document.querySelector('#status')?.dataset.runtimeError || null,
        import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled};
    }, matchedSetupCfExplicitArmed), Math.min(5000, remaining()),
    'First-CSS post-unload snapshot');
  } catch (error) {
    if (matchedSetupCfExplicitArmed) {
      matchedSetupCfExplicitAsyncifyUnsafe = true;
      matchedSetupCfExplicitNativeAccessClosed = true;
    }
    throw error;
  }
  if (afterUnload?.__native_asyncify_idle === false) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    matchedSetupCfExplicitNativeAccessClosed = true;
    throw Error('Asyncify is not idle; first-CSS post-unload native snapshot was suppressed');
  }
  if (matchedSetupCfExplicitArmed) {
    const archived = report.cleanup.matched_setup_cf_retirement?.retirement?.matched_setup_cf;
    const expectedCursor = report.scenario.matched_setup_cf?.schedule_actions;
    if (!archived || archived.complete !== true || archived.cursor !== expectedCursor)
      throw Error('Explicit retirement did not retain the complete terminal C/F receipt separately');
    afterUnload.matched_setup_cf_retirement_receipt = archived;
  }
  return afterUnload;
}

function assertFirstCssDiagnosticMatchedSetupCfPostUnload(afterUnload) {
  if (!firstSssVsSetupCf) return;
  assert.equal(afterUnload.matched_setup_cf, null,
    'Live matched Setup C/F observer survived native owner close');
  if (!matchedSetupCfExplicitArmed) return;
  const expectedCursor = report.scenario.matched_setup_cf?.schedule_actions;
  assert.ok(Number.isSafeInteger(expectedCursor) && expectedCursor > 0,
    'The report omitted the checked matched Setup C/F schedule length');
  assert.equal(afterUnload.matched_setup_cf_retirement_receipt?.complete, true,
    'Archived terminal C/F receipt was not retained separately from the cleared live observer');
  assert.equal(afterUnload.matched_setup_cf_retirement_receipt?.cursor, expectedCursor,
    'Archived terminal C/F cursor differs from the checked schedule length');
}

async function enableFirstSssTextTailProbe() {
  return page.evaluate(() => {
    const probe = globalThis.__meleeWebStadiumTextTailProbe;
    return probe && typeof probe.enable === 'function' && probe.enable() === true;
  });
}

async function snapshotFirstSssTextTailLog() {
  return page.evaluate(() => globalThis.__meleeWebStadiumTextTailProbe?.snapshot?.() ?? null);
}

function validateFirstSssTextTailLog(snapshot) {
  const errors = [];
  if (!snapshot || typeof snapshot !== 'object')
    return {state: 'OPEN', complete: false, errors: ['missing log snapshot']};
  if (snapshot.schema !== 'melee-web-stadium-first-sss-text-tail-log-v1')
    errors.push('log schema mismatch');
  if (snapshot.enabled !== true) errors.push('log sink was not enabled at capture');
  if (!Array.isArray(snapshot.records)) errors.push('log records are unavailable');
  const nonnegativeInteger = value => Number.isSafeInteger(value) && value >= 0;
  const counters = ['retained_records', 'retained_bytes', 'total_records', 'total_bytes',
    'dropped_records', 'dropped_bytes'];
  for (const key of counters)
    if (!nonnegativeInteger(snapshot[key])) errors.push(`invalid ${key}`);
  if (Array.isArray(snapshot.records)) {
    if (snapshot.retained_records !== snapshot.records.length)
      errors.push('retained record count mismatch');
    const bytes = snapshot.records.reduce((sum, record) => {
      if (typeof record !== 'string' ||
          !['C1_SIS_TEXT_EVENT ', 'C1_HUD_TEXT_PHASE ', 'C1_NAMETAG_TEXT ',
            'STADIUM_TEXT_MEMBERSHIP_OBSERVATION '].some(prefix => record.startsWith(prefix))) {
        errors.push('invalid or unallowlisted event record');
        return sum;
      }
      return sum + Buffer.byteLength(record, 'utf8');
    }, 0);
    if (snapshot.retained_bytes !== bytes) errors.push('retained byte count mismatch');
  }
  if (counters.every(key => nonnegativeInteger(snapshot[key]))) {
    if (snapshot.total_records !== snapshot.retained_records + snapshot.dropped_records)
      errors.push('record accounting mismatch');
    if (snapshot.total_bytes !== snapshot.retained_bytes + snapshot.dropped_bytes)
      errors.push('byte accounting mismatch');
  }
  const limits = snapshot.limits;
  if (limits?.records !== 8192 || limits?.bytes !== 1024 * 1024)
    errors.push('log limits mismatch');
  if (snapshot.retained_records > 8192 || snapshot.retained_bytes > 1024 * 1024)
    errors.push('retained log exceeds its declared limits');
  const overflow = snapshot.overflow === true || snapshot.dropped_records > 0 ||
    snapshot.dropped_bytes > 0;
  if (snapshot.overflow !== overflow) errors.push('overflow/drop state mismatch');
  if (overflow) errors.push('log overflow leaves observation open');
  return {state: errors.length ? 'OPEN' : 'retained', complete: errors.length === 0, errors,
    retained_records: snapshot.retained_records ?? null, retained_bytes: snapshot.retained_bytes ?? null,
    dropped_records: snapshot.dropped_records ?? null, dropped_bytes: snapshot.dropped_bytes ?? null,
    overflow};
}

async function harvestFirstSssTextTailAfterUnload() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const raw = pointer ? module.UTF8ToString(pointer) : null;
    const parsed = raw ? JSON.parse(raw) : null;
    const retainedSchema = 'melee-web-stadium-first-sss-text-tail-retained-v1';
    const nativeTail = parsed?.schema === retainedSchema
      ? parsed.first_sss_vs_setup_text_tail
      : parsed?.first_sss_vs_setup?.text_tail_probe ?? null;
    const logProbe = globalThis.__meleeWebStadiumTextTailProbe;
    const textTailLog = logProbe?.snapshot?.() ?? null;
    logProbe?.disable?.();
    logProbe?.reset?.();
    const logProbeAfterReset = logProbe?.snapshot?.() ?? null;
    return {
      phase: module?._melee_web_native_menu_phase?.() ?? null,
      running: module?._melee_web_native_menu_running?.() ?? null,
      ordinary_observation_present: !!parsed?.first_sss_vs_setup,
      retained_wrapper_present: parsed?.schema === retainedSchema,
      text_tail_probe: nativeTail,
      text_tail_log: textTailLog,
      log_probe_after_reset: logProbeAfterReset,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') &&
        !document.querySelector('#disc').disabled,
    };
  });
}

function assertFirstSssVsEntryUnchanged(native, before) {
  assert.ok(native, 'VS Entry native observation is absent');
  const actual = structuredClone(native);
  delete actual.first_sss_vs_entry;
  assert.deepEqual(actual, before, 'VS Entry read changed the full stopped native observation');
  assert.equal(native.running, 0, 'VS Entry endpoint advanced source time');
}

async function runFirstSssVsEntry(prefixNative) {
  const expected = firstSssVsEntryInputs.expected;
  const helper = await import(pathToFileURL(firstSssVsEntryInputs.helperPath).href);
  const comparator = helper.createFirstSssVsEntryComparator(expected, prefixNative.first_sss_prefix);
  report.scenario.first_sss_vs_entry = {expected_json: firstSssVsEntryInputs.identity,
    prefix_approved: true, before: structuredClone(prefixNative),
    result: null, observation: null, comparison: null};
  await persistReport();
  // Installed only after runFirstSssPrefix returned from every strict assertion,
  // including374 callbacks, audio accounting, prerequisites and checked leave.
  await timeout(page.evaluate(() => {
    if (globalThis.__meleeWebStadiumFirstSssPrefixApproved)
      throw Error('VS Entry prefix approval was already installed');
    globalThis.__meleeWebStadiumFirstSssPrefixApproved = () => {
      const read = globalThis.__meleeWebStadiumFirstSssPrefixRead;
      const result = typeof read === 'function' ? read() : null;
      return result?.hook_error === null && result?.status?.approved === true &&
        result?.status?.failed === false && result?.status?.attempted === 374 &&
        result?.status?.first_mismatch === null && result?.rows?.length === 374 &&
        result.rows.every(row => row.approved === true);
    };
  }), Math.min(5000, remaining()), 'Approve only the already compared stopped SSS prefix');
  const result = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_vs_entry_kick;
    if (typeof fn !== 'function') return {result: 0, message: 'VS Entry endpoint export is absent'};
    const result = fn();
    const message = module._melee_web_native_menu_message?.();
    return {result, message: message ? module.UTF8ToString(message) : null};
  }), Math.min(5000, remaining()), 'Read the existing checked Stadium match Start/seed');
  report.scenario.first_sss_vs_entry.result = result;
  // Persist refusal/actual data before any success-only assertion or unload.
  const native = await timeout(readFirstSssVsEntryObservation(), Math.min(5000, remaining()),
    'Retain actual VS Entry endpoint and first refusal');
  report.scenario.first_sss_vs_entry.observation = native;
  await persistReport();
  assertFirstSssVsEntryUnchanged(native, prefixNative);
  const approved = comparator.compare(native.first_sss_vs_entry);
  report.scenario.first_sss_vs_entry.comparison = comparator.status();
  await persistReport();
  assert.equal(result.result, 1, native.first_sss_vs_entry?.error || result.message || 'VS Entry getter refused');
  assert.equal(approved, true, JSON.stringify(comparator.status().first_mismatch));
  report.scenario.first_sss_vs_entry.claim =
    'Declared actual match Start fields/player0-3 and host seed compared to original Entry2468 after strict prefix completion; no match world construction, Setup/GO or C3 acceptance.';
  await persistReport();
  return native;
}

function assertFirstSssVsSetupHistory(native, before) {
  assert.ok(native, 'VS Setup observation missing');
  assert.equal(native.running, 0, 'VS Setup started the general source loop');
  const actual = structuredClone(native), historical = structuredClone(before);
  delete actual.first_sss_vs_setup;
  // The old native observations are explicitly retained at Setup begin. Live
  // phase may change as the actual menu world retires and match world is acquired.
  delete actual.native_phase; delete historical.native_phase;
  assert.deepEqual(actual, historical, 'VS Setup changed historical prerequisite observations');
}

function assertFirstSssVsSetupOwners(setup) {
  const owners = setup.current_owners;
  assert.equal(setup.menu_retired, true);
  assert.equal(setup.parent_seed_owner_value_restored, true);
  assert.equal(setup.parent_seed_before_retirement, setup.parent_seed_after_retirement);
  assert.ok(setup.requested_generation > 0);
  assert.equal(setup.committed_generation, setup.requested_generation);
  assert.ok(setup.match_world_generation > 0);
  assert.notEqual(setup.match_world_generation, setup.prior_world_generation);
  for (const key of ['host', 'match', 'cache', 'source_world_exists']) assert.equal(owners[key], true, key);
  for (const key of ['menu_world', 'host_entered', 'running', 'pending', 'first_use_draw_pending'])
    assert.equal(owners[key], false, key);
  assert.equal(owners.host_phase, 5);
  assert.equal(owners.source_world_generation, setup.match_world_generation);
  assert.equal(owners.pending_scope_generation, 0);
  assert.equal(owners.asset_generation, setup.committed_generation);
  assert.ok(owners.active_files > 0);
  assert.equal(setup.audio_render_calls_after, setup.audio_render_calls_before);
  assert.equal(setup.audio_render_frames_after, setup.audio_render_frames_before);
}

async function runFirstSssVsSetup(entryNative) {
  const inputs = firstSssVsSetupInputs;
  const helper = await import(pathToFileURL(inputs.helperPath).href);
  const entryStatus = report.scenario.first_sss_vs_entry.comparison;
  const comparator = helper.createFirstSssVsSetupComparator(inputs.expected,
    entryNative.first_sss_prefix, entryStatus);
  report.scenario.first_sss_vs_setup = {expected_json: inputs.identity,
    before: structuredClone(entryNative), begin: null, observation: null, comparison: null,
    text_tail_probe: firstSssVsSetupTextTail
      ? {requested: true, log_before_unload: null, after_unload: null,
        failure_time_log: null, failure_time_log_validation: null,
        failure_time_unload_attempted: null} : null};
  await persistReport();
  await page.evaluate(() => {
    if (globalThis.__meleeWebStadiumFirstSssVsEntryApproved)
      throw Error('Setup Entry approval already installed');
    // Installed only after Node has completed unchanged strict Entry assertions.
    globalThis.__meleeWebStadiumFirstSssVsEntryApproved = () => true;
  });
  if (firstSssVsSetupTextTail)
    assert.equal(await enableFirstSssTextTailProbe(), true,
      'Bounded Setup text-tail log probe is unavailable');
  const begin = await timeout(page.evaluate((textTailProbe) => {
    const m = globalThis.Module, f = m?._melee_web_native_menu_stadium_first_sss_vs_setup_begin;
    if (typeof f !== 'function') return {result: 0, message: 'VS Setup export missing'};
    const result = f(textTailProbe ? 1 : 0),
      p = m._melee_web_native_menu_message?.();
    return {result, message: p ? m.UTF8ToString(p) : null};
  }, firstSssVsSetupTextTail), Math.min(5000, remaining()),
    'Request the checked asynchronous Setup asset handoff');
  report.scenario.first_sss_vs_setup.begin = begin;
  let native = await readFirstSssVsEntryObservation();
  report.scenario.first_sss_vs_setup.observation = native;
  await persistReport();
  // Preserve first native refusal before success-only assertions or cleanup.
  assert.equal(begin.result, 1, native?.first_sss_vs_setup?.error || begin.message);
  const started = Date.now();
  while (!native?.first_sss_vs_setup?.complete && !native?.first_sss_vs_setup?.failed) {
    assert.ok(Date.now() - started < LIMITS.discImportMs + LIMITS.manifestHandoffMs,
      'VS Setup asset/construction deadline exceeded');
    await timeout(page.waitForTimeout(50), Math.min(1000, remaining()), 'Wait for bounded Setup callback');
    native = await timeout(readFirstSssVsEntryObservation(), Math.min(3000, remaining()),
      'Read actual Setup progress and first failure');
    report.scenario.first_sss_vs_setup.observation = native;
    await persistReport();
  }
  assertFirstSssVsSetupHistory(native, entryNative);
  const approved = comparator.compare(native.first_sss_vs_setup);
  report.scenario.first_sss_vs_setup.comparison = comparator.status();
  await persistReport();
  assert.equal(approved, true, JSON.stringify(comparator.status().first_mismatch));
  assertFirstSssVsSetupOwners(native.first_sss_vs_setup);
  if (firstSssVsSetupTextTail) {
    const nativeTextTail = native.first_sss_vs_setup.text_tail_probe;
    assert.equal(nativeTextTail?.opted_in, true);
    assert.equal(nativeTextTail?.error, null,
      'Exact SIS lease or Stadium membership acquisition failed');
    assert.equal(nativeTextTail?.sis_lease_captured, true);
    assert.equal(nativeTextTail?.membership_snapshot_started, true);
    assert.equal(nativeTextTail?.membership_snapshot_active, true);
    const logBeforeUnload = await snapshotFirstSssTextTailLog();
    const logValidation = validateFirstSssTextTailLog(logBeforeUnload);
    report.scenario.first_sss_vs_setup.text_tail_probe.log_before_unload = logBeforeUnload;
    report.scenario.first_sss_vs_setup.text_tail_probe.log_validation_before_unload = logValidation;
    assert.equal(logValidation.complete, true,
      `Setup text-tail log is incomplete: ${logValidation.errors.join('; ')}`);
    await persistReport();
  }
  if (firstSssVsSetupCf) native = await runMatchedSetupCfContinuation(native);
  report.scenario.first_sss_vs_setup.claim = firstSssVsSetupCf
    ? (report.scenario.matched_setup_cf?.mode === 'run2-proper-prefix-v2'
      ? 'Strict same-run Setup2503 endpoint plus the extracted run2 prefix through draw86 after dynamic C/F; no later lifetime or whole-session acceptance.'
      : 'Strict same-run Setup2503 Start/player0-3/seed endpoint plus the bounded exact matched original C/F prefix; no later match route, source draw85, or whole-session acceptance.')
    : 'Declared Setup2503 Start/player0-3/seed endpoint only; zero gameplay ticks/draws and stop before Ready/GO. Other setup fields and per-owner RNG remain unpaired.';
  await persistReport();
  return native;
}

async function executeMatchedSetupCfNativeCommand(command) {
  const evaluatePromise = dispatchMatchedSetupCfNativeEvaluation({
    command, pendingStep: pendingMatchedSetupCfExplicitStep,
    dispatch: () => page.evaluate(({kind, scheduleIndex, action, ordinal, bundle, bytes,
    manifestBytes, accepted, requireAsyncifyIdle}) => {
    const module = globalThis.Module;
    const asyncifyIdle = () => typeof module?.stadiumMatchedSetupNativeIdle === 'function' &&
      module.stadiumMatchedSetupNativeIdle() === true;
    if (requireAsyncifyIdle && !asyncifyIdle())
      return {result: 0, message: 'Asyncify is not idle; native command was suppressed',
        asyncify_idle: false};
    const readMessage = () => {
      const pointer = module?._melee_web_native_menu_message?.();
      return pointer ? module.UTF8ToString(pointer) : null;
    };
    if (!module) return {result: 0, message: 'Native module is absent'};
    if (kind === 'arm') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F arm export is absent'};
      const result = fn();
      return {result, message: result === 1 ? null : readMessage()};
    }
    if (kind === 'arm-explicit-v2') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F explicit arm export is absent'};
      const pointer = module._malloc(manifestBytes.length);
      if (!pointer) return {result: 0, message: 'Matched Setup C/F explicit manifest allocation failed'};
      try {
        module.HEAPU8.set(manifestBytes, pointer);
        const result = fn(pointer, manifestBytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally { module._free(pointer); }
    }
    if (kind === 'arm-v2') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F v2 arm export is absent'};
      const pointer = module._malloc(manifestBytes.length);
      if (!pointer) return {result: 0, message: 'Matched Setup C/F v2 manifest allocation failed'};
      try {
        module.HEAPU8.set(manifestBytes, pointer);
        const result = fn(pointer, manifestBytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally { module._free(pointer); }
    }
    if (kind === 'queue') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F queue export is absent'};
      const pointer = bytes.length ? module._malloc(bytes.length) : 0;
      if (bytes.length && !pointer) return {result: 0, message: 'Matched Setup C/F PAD copy allocation failed'};
      try {
        if (bytes.length) module.HEAPU8.set(bytes, pointer);
        const result = fn(scheduleIndex, action === 'tick' ? 1 : 2,
          ordinal, bundle, pointer, bytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally {
        if (pointer) module._free(pointer);
      }
    }
    if (kind === 'step-explicit') {
      if (typeof module.ccall !== 'function')
        throw Error('Matched Setup C/F explicit step requires generated runtime ccall');
      const call = module.ccall(
        'melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
        'number', [], [], {async: true});
      if (!call || typeof call.then !== 'function')
        throw Error('Generated explicit native step did not return its Asyncify promise');
      return Promise.resolve(call).then(result => {
        const idle = asyncifyIdle();
        return {result, message: idle ? result === 1 ? null : readMessage() : null,
          asyncify_idle: idle};
      }, error => {
        const idle = asyncifyIdle();
        const rejection = String(error?.message || error);
        const nativeMessage = idle ? readMessage() : null;
        return {result: 0,
          message: idle ? `${nativeMessage || 'Asyncify native step rejected'}: ${rejection}`
            : `Asyncify became unsafe after native step rejection: ${rejection}`,
          asyncify_idle: idle};
      });
    }
    if (kind === 'retire-ready') {
      if (!asyncifyIdle()) return {ready: -1, asyncify_idle: false};
      const fn = module?._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready;
      if (typeof fn !== 'function')
        return {ready: -1, asyncify_idle: true, message: 'Matched Setup C/F retire-ready export is absent'};
      return {ready: fn(), asyncify_idle: true};
    }
    if (kind === 'retire-explicit') {
      if (!asyncifyIdle()) return {result: 0, asyncify_idle: false};
      if (typeof module.ccall !== 'function')
        throw Error('Matched Setup C/F retirement requires generated runtime ccall');
      const call = module.ccall(
        'melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
        'number', [], [], {async: true});
      if (!call || typeof call.then !== 'function')
        throw Error('Generated native retirement did not return its Asyncify promise');
      const settled = result => {
        const idle = asyncifyIdle();
        if (!idle) return {result, asyncify_idle: false};
        const statusFn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status;
        let retirement = null;
        if (typeof statusFn === 'function') {
          const pointer = statusFn();
          retirement = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
        }
        return {result, asyncify_idle: true, retirement,
          phase: module._melee_web_native_menu_phase(),
          running: module._melee_web_native_menu_running(),
          message: result === 1 ? null : readMessage()};
      };
      return Promise.resolve(call).then(settled, error => {
        const idle = asyncifyIdle();
        if (!idle) return {result: 0, asyncify_idle: false,
          rejection: String(error?.message || error)};
        const response = settled(0);
        response.rejection = String(error?.message || error);
        return response;
      });
    }
    if (kind === 'queue-v2') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F v2 queue export is absent'};
      const result = fn(scheduleIndex, action === 'tick' ? 1 : 2,
        ordinal, action === 'tick' ? bundle : -1);
      return {result, message: result === 1 ? null : readMessage()};
    }
    if (kind === 'approve') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F approval export is absent'};
      const result = fn(scheduleIndex, accepted ? 1 : 0);
      return {result, message: result === 1 ? null : readMessage()};
    }
    return {result: 0, message: `Unknown matched Setup C/F native command ${kind}`};
  }, command)});
  if (command.kind === 'step-explicit' || command.kind === 'retire-explicit') {
    const tracked = trackMatchedSetupCfExplicitStep(evaluatePromise, state => {
      state.asyncifyIdle = state.value?.asyncify_idle === true;
    });
    tracked.requiresAsyncifyIdle = true;
    tracked.scheduleIndex = command.scheduleIndex ?? null;
    tracked.commandKind = command.kind;
    tracked.outcome = 'pending';
    tracked.promise = tracked.promise.then(value => {
      tracked.outcome = 'resolved';
      return value;
    }, error => {
      tracked.outcome = 'rejected';
      tracked.error = errorText(error);
      throw error;
    });
    pendingMatchedSetupCfExplicitStep = tracked;
    return tracked.promise;
  }
  return evaluatePromise;
}

function matchedSetupCfNativeAdapter({mode, manifestBytes, explicitStep = false}) {
  const execute = async (command, label) => {
    const explicitArm = command.kind === 'arm-explicit-v2';
    if (explicitArm) {
      // The native arm may cancel RAF before its page response reaches the host.
      // Until confirmed success, neither a lost response nor native result 0
      // proves that the ordinary command loop is still available for cleanup.
      matchedSetupCfExplicitNativeAccessClosed = true;
      report.cleanup.matched_setup_cf_arm_outcome = 'dispatched-uncertain';
    }
    let response;
    try {
      response = await timeout(executeMatchedSetupCfNativeCommand({
        ...command, requireAsyncifyIdle: explicitStep}),
        Math.min(5000, remaining()), label);
    } catch (error) {
      if (explicitStep) {
        // Host timeout/rejection does not cancel a possibly applied command or
        // yielded step. Even late settlement cannot reopen native cleanup.
        matchedSetupCfExplicitNativeAccessClosed = true;
        matchedSetupCfExplicitAsyncifyUnsafe = true;
        if (explicitArm)
          report.cleanup.matched_setup_cf_arm_outcome = 'response-uncertain';
      }
      throw error;
    }
    if (explicitStep && response?.asyncify_idle === false)
      matchedSetupCfExplicitAsyncifyUnsafe = true;
    if (command.kind === 'step-explicit' && response?.asyncify_idle !== true)
      throw Error('Explicit Asyncify step settled without a safe native-idle state');
    if (response?.result !== 1)
      throw Error(response?.message || `${label} was refused by the native owner`);
    if (explicitArm) {
      matchedSetupCfExplicitArmed = true;
      matchedSetupCfExplicitNativeAccessClosed = false;
      report.cleanup.matched_setup_cf_arm_outcome = 'confirmed-armed';
    }
    return response.result;
  };
  return {
    arm: async suppliedManifest => mode === 'run2-proper-prefix-v2'
      ? execute({kind: explicitStep ? 'arm-explicit-v2' : 'arm-v2',
        manifestBytes: Array.from(suppliedManifest)},
        'Arm the one-use run2 proper-prefix continuation')
      : execute({kind: 'arm'}, 'Arm the one-use matched Setup C/F continuation'),
    step: async scheduleIndex => execute({kind: 'step-explicit', scheduleIndex},
      `Service explicit run2 source action ${scheduleIndex}`),
    queue: async (scheduleIndex, action, bytes) => {
      const actionSample = matchedSetupCfTiming?.currentActionSample ?? null;
      const queueOperation = () => mode === 'run2-proper-prefix-v2'
        ? execute({kind: 'queue-v2', scheduleIndex, action: action.action,
          ordinal: action.action === 'tick' ? action.bundle : action.draw,
          bundle: action.bundle ?? -1}, `Queue run2 source action ${scheduleIndex}`)
        : execute({kind: 'queue', scheduleIndex,
          action: action.action, ordinal: action.ordinal, bundle: action.bundle ?? -1,
          bytes: Array.from(bytes)}, `Queue matched Setup C/F action ${scheduleIndex}`);
      try {
        return actionSample
          ? await measureAsync(actionSample.stage_events, 'queue_rpc', queueOperation)
          : await queueOperation();
      } finally {
        if (actionSample) matchedSetupCfTiming.queueRpcMsByIndex.set(scheduleIndex,
          actionSample.stage_events.at(-1)?.stage === 'queue_rpc'
            ? actionSample.stage_events.at(-1).elapsed_ms : null);
      }
    },
    approve: async (scheduleIndex, accepted) => {
      const actionSample = matchedSetupCfTiming?.currentActionSample ?? null;
      const approvalOperation = () => execute({kind: 'approve', scheduleIndex, accepted},
        `Acknowledge matched Setup C/F action ${scheduleIndex}`);
      try {
        return actionSample
          ? await measureAsync(actionSample.stage_events, 'approval_rpc', approvalOperation)
          : await approvalOperation();
      } finally {
        if (actionSample) matchedSetupCfTiming.approvalRpcMsByIndex.set(scheduleIndex,
          actionSample.stage_events.at(-1)?.stage === 'approval_rpc'
            ? actionSample.stage_events.at(-1).elapsed_ms : null);
      }
    },
  };
}

async function runMatchedSetupCfContinuation(setupNative) {
  const inputs = matchedSetupCfInputs;
  const {helper, hostEvidence} = inputs;
  const mode = hostEvidence.mode ?? 'legacy-v1';
  const isV2 = mode === 'run2-proper-prefix-v2';
  const actualSetup = setupNative?.first_sss_vs_setup?.actual;
  assert.equal(actualSetup?.phase, 'vs_setup');
  assert.equal(actualSetup?.start?.rules?.x1_3, 0,
    'Matched Setup C/F supports only the strictly compared source branch-0 Setup');
  const setupStatus = report.scenario.first_sss_vs_setup.comparison;
  assert.equal(setupStatus?.approved, true,
    'Matched Setup C/F arm requires the completed strict Setup comparison');
  assertFirstSssVsSetupOwners(setupNative.first_sss_vs_setup);

  const cf = report.scenario.matched_setup_cf = {
    decision: isV2 ? 'RUNNING_RUN2_PROPER_PREFIX_V2' : 'RUNNING_BOUNDED_MATCHED_SETUP_CF',
    mode,
    packet: inputs.packetIdentity, assembly_manifest: inputs.assemblyIdentity,
    host_helper: inputs.helperIdentity, assembly_schema: inputs.assemblySchema,
    runtime_payload_sha256: inputs.runtimePayloadSha256,
    schedule_actions: hostEvidence.runtimePayload.schedule.length,
    explicit_step_mode: firstSssVsSetupCfExplicitStep,
    asyncify_idle_predicate: firstSssVsSetupCfExplicitStep
      ? 'Module.stadiumMatchedSetupNativeIdle' : null,
    input_bundles: hostEvidence.runtimePayload.bundles.length,
    setup_comparison: structuredClone(setupStatus), branch: 0,
    actions: [], host_comparisons: [], runtime_diagnostics: [], last_native_state: null,
    unpaired: {raw_timer_initialization: 'unobserved', original_xF0: 'unobserved',
      original_SIS_title_bytes: 'unobserved', original_map_root_allocation_lifetime: 'unproved',
      original_object_type: 'unobserved', native_flow_clock: 'reported separately',
      nested_display_list_provenance: 'not instrumented in the admitted profile; the nested GX_CMD_CALL_DL warning is a stop condition'},
    claim: isV2
      ? 'Fresh strict Setup followed by the extracted run2 action0 prefix through the next draw/tick/draw beyond dynamically mapped C/F; exact declared C/F state and seed comparisons; proper-prefix completion only, no whole-session acceptance.'
      : 'One matched original input prefix after the fresh strict Setup comparison; state-only C/F checks, no whole-session acceptance.',
  };
  await persistReport();

  let setupApproved = true;
  let observedPageErrors = 0, observedConsoleErrors = 0, observedConsoleWarnings = 0,
    observedConsoleMessages = 0;
  const checkRuntimeDiagnostics = async (scheduleIndex, action, nativeObservation) => {
    const activeSample = matchedSetupCfTiming?.currentActionSample ?? null;
    const snapshotStage = action.endsWith('-before-ack') ? 'before_snapshot'
      : action.endsWith('-after-ack') ? 'after_snapshot' : null;
    const snapshotField = snapshotStage === 'before_snapshot' ? 'before_snapshot_ms'
      : snapshotStage === 'after_snapshot' ? 'after_snapshot_ms' : null;
    const snapshotOperation = () => sourceSnapshot(
      `Matched Setup C/F runtime diagnostics after action ${scheduleIndex}`);
    const runtime = activeSample && snapshotStage
      ? await timedActionAsync(activeSample, snapshotStage, snapshotField, snapshotOperation)
      : await snapshotOperation();
    const sample = {schedule_index: scheduleIndex, action, message: runtime.message,
      diagnostics: runtime.diagnostics, runtime_error: runtime.error,
      page_errors: pageErrors.slice(observedPageErrors),
      console_errors: consoleErrors.slice(observedConsoleErrors),
      console_warnings: consoleWarnings.slice(observedConsoleWarnings),
      console_messages: consoleMessages.slice(observedConsoleMessages),
      console_message_capture_overflow: consoleMessageCaptureOverflow};
    observedPageErrors = pageErrors.length;
    observedConsoleErrors = consoleErrors.length;
    observedConsoleWarnings = consoleWarnings.length;
    observedConsoleMessages = consoleMessages.length;
    cf.runtime_diagnostics.push(sample);
    report.scenario.console_warnings = [...consoleWarnings];
    report.scenario.console_messages = [...consoleMessages];
    report.scenario.console_message_capture_overflow = consoleMessageCaptureOverflow;
    report.scenario.console_message_dropped_count = consoleMessageDropped;
    await persistReport();
    const stop = matchedSetupCfRuntimeFailure({native: runtime,
      pageErrors, consoleErrors, consoleWarnings, consoleMessages,
      consoleMessageCaptureOverflow});
    if (stop) {
      cf.decision = 'STOP_MATCHED_SETUP_CF_RUNTIME_DIAGNOSTIC';
      cf.stop_reason = stop;
      cf.last_native_state = structuredClone(nativeObservation);
      await persistReport();
      throw Error(stop);
    }
  };
  const hostCallbacks = helper.createHostCallbacks({
    setupApproved: () => setupApproved,
    originalSnapshots: hostEvidence.originalSnapshots,
    originalSeeds: hostEvidence.originalSeeds,
    coordinates: hostEvidence.coordinates ?? null,
  });
  const nativeAdapter = matchedSetupCfNativeAdapter({mode,
    explicitStep: firstSssVsSetupCfExplicitStep,
    manifestBytes: isV2 ? helper.encodeRun2NativeManifest(hostEvidence.runtimePayload) : null});
  const driver = new helper.StadiumSetupCfActionDriver({
    runtimePayload: hostEvidence.runtimePayload, native: nativeAdapter});
  await page.evaluate(() => {
    if (globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved)
      throw Error('Matched Setup C/F approval callback already exists');
    globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved = () => true;
  });
  try {
    assert.equal(await driver.arm(hostCallbacks.setupApproved()), true,
      `Matched Setup C/F driver refused its fresh Setup: ${driver.status().first_error}`);
    let currentNative = await readFirstSssVsEntryObservation();
    assert.equal(currentNative?.first_sss_vs_setup?.matched_setup_cf?.armed, true,
      'Native matched Setup C/F arm was not retained');
    await checkRuntimeDiagnostics(-1, 'arm',
      currentNative?.first_sss_vs_setup?.matched_setup_cf ?? null);
    await persistReport();

    const actionStarted = Date.now();
    if (matchedSetupCfTiming) {
      matchedSetupCfTiming.scheduledActions = hostEvidence.runtimePayload.schedule.length;
      matchedSetupCfTiming.actionLoopActive = true;
      matchedSetupCfTiming.actionLoopStartedNs = monotonicNowNs();
      matchedSetupCfTiming.globalCaptureDeadlineRemainingAtActionLoopStartMs =
        Math.max(0, deadline - actionStarted);
    }
    for (let scheduleIndex = 0; scheduleIndex < hostEvidence.runtimePayload.schedule.length;
      ++scheduleIndex) {
      const action = hostEvidence.runtimePayload.schedule[scheduleIndex];
      const cIndex = isV2 ? hostEvidence.checkpoints.c_action_index : 168;
      const fIndex = isV2 ? hostEvidence.checkpoints.f_action_index : 170;
      const timingSample = matchedSetupCfTiming ? {
        schedule_index: scheduleIndex, action: action.action,
        phase: scheduleIndex < cIndex ? 'before_c' : scheduleIndex <= fIndex ? 'c_through_f' : 'after_f',
        completed: false, stage_events: [], queue_ms: null, queue_rpc_ms: null,
        post_submit_receipt_ms: null, explicit_step_ms: null,
        receipt_reads: 0, first_read_ms: null,
        first_read_complete: null, receipt_read_ms: null, poll_waits: 0,
        poll_wait_ms: null, before_diagnostics_ms: null, before_snapshot_ms: null,
        compare_ms: null, accept_ms: null, approval_rpc_ms: null,
        after_diagnostics_ms: null, after_snapshot_ms: null, action_wall_ms: null,
      } : null;
      const actionStartedNs = timingSample ? monotonicNowNs() : null;
      let postSubmitReceiptStartedNs = null;
      if (timingSample) matchedSetupCfTiming.currentActionSample = timingSample;
      try {
        if (timingSample) {
          timedActionSync(timingSample, 'action_deadline_check', null, () =>
            assert.ok(Date.now() - actionStarted < LIMITS.captureWorkMs,
              'Matched Setup C/F action schedule exceeded its bounded continuation deadline'));
          await timedActionAsync(timingSample, 'driver_submit', 'queue_ms', async () => {
            assert.equal(await driver.submit(action), true,
              `Matched Setup C/F host action driver refused ${scheduleIndex}: ${driver.status().first_error}`);
          });
          timingSample.queue_rpc_ms = matchedSetupCfTiming.queueRpcMsByIndex.get(scheduleIndex) ?? null;
          postSubmitReceiptStartedNs = monotonicNowNs();
        } else {
          assert.ok(Date.now() - actionStarted < LIMITS.captureWorkMs,
            'Matched Setup C/F action schedule exceeded its bounded continuation deadline');
          assert.equal(await driver.submit(action), true,
            `Matched Setup C/F host action driver refused ${scheduleIndex}: ${driver.status().first_error}`);
        }

        if (firstSssVsSetupCfExplicitStep) {
          const stepOperation = () => nativeAdapter.step(scheduleIndex);
          if (timingSample)
            await timedActionAsync(timingSample, 'explicit_step', 'explicit_step_ms', stepOperation);
          else await stepOperation();
        }

        const receiptStarted = Date.now();
        let cfState = null;
        for (;;) {
          const readOrdinal = timingSample?.receipt_reads ?? 0;
          const readOperation = () => timeout(readFirstSssVsEntryObservation(true),
            Math.min(3000, remaining()), `Read matched Setup C/F action ${scheduleIndex}`);
          if (timingSample) {
            currentNative = await timedActionAsync(timingSample, 'receipt_read', null,
              readOperation, event => {
                ++timingSample.receipt_reads;
                timingSample.receipt_read_ms = (timingSample.receipt_read_ms ?? 0) + event.elapsed_ms;
                if (readOrdinal === 0) timingSample.first_read_ms = event.elapsed_ms;
              });
          } else {
            currentNative = await readOperation();
          }
          cfState = currentNative?.first_sss_vs_setup?.matched_setup_cf ?? null;
          const receiptComplete = timingSample
            ? timedActionSync(timingSample, 'receipt_check', null, () => {
              if (cfState?.failed)
                throw Error(cfState.error || `Native matched Setup C/F stopped at action ${scheduleIndex}`);
              return cfState?.last_action?.complete === true &&
                cfState.last_action.schedule_index === scheduleIndex;
            })
            : (() => {
              if (cfState?.failed)
                throw Error(cfState.error || `Native matched Setup C/F stopped at action ${scheduleIndex}`);
              return cfState?.last_action?.complete === true &&
                cfState.last_action.schedule_index === scheduleIndex;
            })();
          if (timingSample && readOrdinal === 0) timingSample.first_read_complete = receiptComplete;
          if (receiptComplete) {
            if (timingSample)
              timingSample.post_submit_receipt_ms = elapsedMs(postSubmitReceiptStartedNs);
            break;
          }
          if (firstSssVsSetupCfExplicitStep)
            throw Error(`Explicit native service resolved without the complete receipt for action ${scheduleIndex}`);
          if (timingSample) {
            timedActionSync(timingSample, 'receipt_deadline_check', null, () =>
              assert.ok(Date.now() - receiptStarted < remaining(),
                `Native matched Setup C/F action ${scheduleIndex} did not complete before deadline`));
            await timedActionAsync(timingSample, 'poll_wait', null,
              () => timeout(page.waitForTimeout(50), Math.min(1000, remaining()),
                `Wait for matched Setup C/F action ${scheduleIndex}`), event => {
                ++timingSample.poll_waits;
                timingSample.poll_wait_ms = (timingSample.poll_wait_ms ?? 0) + event.elapsed_ms;
              });
          } else {
            assert.ok(Date.now() - receiptStarted < remaining(),
              `Native matched Setup C/F action ${scheduleIndex} did not complete before deadline`);
            await timeout(page.waitForTimeout(50), Math.min(1000, remaining()),
              `Wait for matched Setup C/F action ${scheduleIndex}`);
          }
        }
        const actionReceipt = timingSample
          ? timedActionSync(timingSample, 'receipt_clone', null,
            () => structuredClone(cfState.last_action))
          : structuredClone(cfState.last_action);
        const boundary = action.action === 'tick' &&
          (scheduleIndex === cIndex || scheduleIndex === fIndex);
        let comparison = null;
        if (boundary) {
          const label = scheduleIndex === cIndex ? 'C' : 'F';
          const compareOperation = () => {
            const accepted = hostCallbacks.compare(label, JSON.stringify(actionReceipt.observation));
            comparison = hostCallbacks.receipts().at(-1) ?? null;
            cf.host_comparisons.push(comparison);
            assert.equal(accepted, comparison?.decision === 'PASS_EXACT_DECLARED_C_F_FIELDS',
              `${label} callback acceptance differs from its strict host receipt`);
          };
          if (timingSample)
            timedActionSync(timingSample, 'comparison', 'compare_ms', compareOperation);
          else compareOperation();
        }
        if (timingSample) {
          await timedActionAsync(timingSample, 'before_diagnostics', 'before_diagnostics_ms',
            () => checkRuntimeDiagnostics(scheduleIndex, `${action.action}-before-ack`, cfState));
        } else {
          await checkRuntimeDiagnostics(scheduleIndex, `${action.action}-before-ack`, cfState);
        }
        let accepted;
        if (timingSample) {
          accepted = await timedActionAsync(timingSample, 'driver_accept', 'accept_ms',
            () => driver.accept(cfState, comparison), () => {
              timingSample.approval_rpc_ms =
                matchedSetupCfTiming.approvalRpcMsByIndex.get(scheduleIndex) ?? null;
            });
        } else {
          accepted = await driver.accept(cfState, comparison);
        }
        const recordAction = () => {
          cf.actions.push({schedule_index: scheduleIndex, action: action.action,
            ordinal: action.ordinal ?? (action.action === 'tick' ? action.bundle : action.draw),
            bundle: action.bundle ?? null,
            native: actionReceipt, host_comparison: comparison});
          cf.last_native_state = structuredClone(cfState);
        };
        if (timingSample)
          timedActionSync(timingSample, 'action_record', null, recordAction);
        else recordAction();
        if (timingSample) {
          await timedActionAsync(timingSample, 'after_diagnostics', 'after_diagnostics_ms',
            () => checkRuntimeDiagnostics(scheduleIndex, `${action.action}-after-ack`, cfState));
        } else {
          await checkRuntimeDiagnostics(scheduleIndex, `${action.action}-after-ack`, cfState);
        }
        if (scheduleIndex % 16 === 15 || boundary || !accepted) await persistReport();
        if (timingSample) {
          timedActionSync(timingSample, 'accept_validation', null, () =>
            assert.equal(accepted, true,
              `Matched Setup C/F stopped at action ${scheduleIndex}: ${driver.status().first_error}`));
          timingSample.completed = true;
        } else {
          assert.equal(accepted, true,
            `Matched Setup C/F stopped at action ${scheduleIndex}: ${driver.status().first_error}`);
        }
      } finally {
        if (timingSample) {
          if (postSubmitReceiptStartedNs !== null && timingSample.post_submit_receipt_ms === null)
            timingSample.post_submit_receipt_ms = elapsedMs(postSubmitReceiptStartedNs);
          timingSample.queue_rpc_ms ??=
            matchedSetupCfTiming.queueRpcMsByIndex.get(scheduleIndex) ?? null;
          timingSample.approval_rpc_ms ??=
            matchedSetupCfTiming.approvalRpcMsByIndex.get(scheduleIndex) ?? null;
          timingSample.action_wall_ms = elapsedMs(actionStartedNs);
          matchedSetupCfTiming.actionSamples.push(timingSample);
          matchedSetupCfTiming.currentActionSample = null;
        }
      }
    }
    finishMatchedSetupCfActionLoop();
    if (matchedSetupCfTiming) {
      assert.equal(matchedSetupCfTiming.actionSamples.length,
        hostEvidence.runtimePayload.schedule.length,
        'Matched Setup C/F timing did not retain one host sample per scheduled action');
      assert.ok(matchedSetupCfTiming.actionSamples.every(sample =>
        sample.stage_events.length > 0 && Number.isFinite(sample.action_wall_ms)),
      'Matched Setup C/F timing sample is missing an attempted stage or action duration');
    }
    assert.equal(driver.status().complete, true);
    currentNative = await readFirstSssVsEntryObservation();
    const finalState = currentNative?.first_sss_vs_setup?.matched_setup_cf;
    assert.equal(finalState?.complete, true, finalState?.error || 'Native matched Setup C/F did not complete');
    assert.equal(finalState?.failed, false);
    assert.equal(finalState?.cursor, hostEvidence.runtimePayload.schedule.length);
    assert.equal(finalState?.last_action?.schedule_index,
      hostEvidence.runtimePayload.schedule.length - 1);
    assert.equal(finalState?.last_action?.action, isV2 ? 'draw' : 'tick');
    assert.equal(finalState?.last_action?.ordinal, isV2
      ? hostEvidence.checkpoints.terminal_draw_ordinal : 85);
    cf.last_native_state = structuredClone(finalState);
    cf.decision = isV2 ? 'PASS_RUN2_PROPER_PREFIX_C_F_STATE_ONLY'
      : 'PASS_MATCHED_SETUP_CF_C_AND_F_STATE_ONLY';
    cf.actions_completed = cf.actions.length;
    await persistReport();
    return currentNative;
  } finally {
    finishMatchedSetupCfActionLoop();
    setupApproved = false;
    await page.evaluate(() => { delete globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved; });
  }
}

async function runFirstCssBrowserDraw({baseUrl, artifacts, before}) {
  const prearm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const memory = module?._melee_web_native_menu_memory;
    if (typeof memory !== 'function')
      throw Error('First-CSS preparation ownership observer is absent');
    const pointer = memory();
    if (!pointer) throw Error('First-CSS preparation ownership observation is absent');
    return {phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running(),
      memory: JSON.parse(module.UTF8ToString(pointer))};
  }), Math.min(5000, remaining()), 'Prepared first-CSS imported-disc ownership');
  report.scenario.first_css_prearm = prearm;
  await persistReport();
  assert.equal(prearm.phase, 0);
  assert.equal(prearm.running, 0);
  assert.equal(prearm.memory.scoped_assets, true,
    'First-CSS diagnostic requires the canonical imported-disc asset scope');
  assert.equal(prearm.memory.source_session_owned, true);
  assert.equal(prearm.memory.menu_present, true);
  assert.equal(prearm.memory.menu_host_entered, false);
  assert.equal(prearm.memory.menu_phase, 0);
  assert.equal(prearm.memory.staged_asset_files, 0);
  assert.equal(prearm.memory.asset_generation, 0);
  assert.ok(prearm.memory.asset_files > 0,
    'First-CSS diagnostic requires the committed menu assets');
  const arm = await timeout(page.evaluate(({contextBytes, consumedBytes}) => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_css_draw_arm;
    const observe = module?._melee_web_native_menu_stadium_first_css_draw_observe;
    if (typeof armFunction !== 'function' || typeof observe !== 'function')
      throw Error('Private first-CSS browser-draw exports are absent');
    if (module._melee_web_native_menu_phase() !== 0 || module._melee_web_native_menu_running() !== 0)
      throw Error('First-CSS bundles must be applied to the fresh, unentered VS owner');
    const context = module._malloc(contextBytes.length);
    const consumed = module._malloc(consumedBytes.length);
    if (!context || !consumed) {
      if (context) module._free(context);
      if (consumed) module._free(consumed);
      throw Error('First-CSS input bundle allocation failed');
    }
    try {
      module.HEAPU8.set(contextBytes, context);
      module.HEAPU8.set(consumedBytes, consumed);
      const result = armFunction(context, contextBytes.length, consumed, consumedBytes.length);
      if (result !== 1) {
        const pointer = module._melee_web_native_menu_message();
        throw Error(pointer ? module.UTF8ToString(pointer) : 'First-CSS source context arm was rejected');
      }
      return {result, phase: module._melee_web_native_menu_phase(),
        running: module._melee_web_native_menu_running()};
    } finally {
      module._free(context);
      module._free(consumed);
    }
  }, {contextBytes: [...firstCssBrowserDrawInputs.bytes.context_bundle],
      consumedBytes: [...firstCssBrowserDrawInputs.bytes.consumed_pad_bundle]}),
  Math.min(10000, remaining()), 'Arm retained first-CSS browser draw inputs');
  assert.equal(arm.result, 1);
  assert.equal(arm.phase, 0);
  assert.equal(arm.running, 0);
  report.scenario.armed_before_css_entry = true;
  report.scenario.first_css_input_bundles = firstCssBrowserDrawInputs.bundle_hashes;

  await timeout(driver.launch(1), Math.min(LIMITS.sourceTransitionMs, remaining()),
    'Original CSS launch with source time held before the retained tick');
  report.scenario.original_css_phase = await sourceSnapshot('Original first-CSS phase before retained tick');
  assert.equal(report.scenario.original_css_phase.phase, 1);
  assert.equal(report.scenario.original_css_phase.running, 1);
  let observation = await timeout(readFirstCssBrowserDrawObservation(),
    Math.min(5000, remaining()), 'Immediate first-CSS browser entry observation');
  assert.equal(observation?.state, 'entered',
    'First-CSS browser entry was not captured before a tick');
  assert.equal(observation.native_phase, 1);
  assert.equal(observation.running, 1);
  assertFirstCssBrowserEntry(observation, firstCssBrowserDrawInputs.contextExpected);
  assert.equal(observation.source_steps, 0);
  assert.equal(observation.source_draws, 0);
  assert.equal(observation.host_tick_calls, 0);
  assert.equal(observation.host_draw_calls, 0);
  report.scenario.first_css_browser_draw = observation;
  await persistReport();

  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_css_draw_kick;
    if (typeof kickFunction !== 'function') throw Error('First-CSS one-shot kick export is absent');
    const result = kickFunction();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'First-CSS one-shot kick was rejected');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release one retained CSS source tick');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const state = JSON.parse(module.UTF8ToString(pointer)).state;
    return state === 'complete' || state === 'failed';
  }, null, {timeout: Math.min(60000, remaining())}),
  Math.min(60000, remaining()), 'One native CSS tick and one browser source draw');
  observation = await timeout(readFirstCssBrowserDrawObservation(),
    Math.min(5000, remaining()), 'Final first-CSS browser draw observation');
  report.scenario.first_css_browser_draw = observation;
  await persistReport();
  assert.equal(observation.state, 'complete',
    `First-CSS browser draw stopped at ${observation.state}: ${observation.error || 'no detail'}`);
  assertFirstCssBrowserEntry(observation, firstCssBrowserDrawInputs.contextExpected);
  assertFirstCssBrowserTick(observation, firstCssBrowserDrawInputs.tickExpected);
  assertFirstCssBrowserDraw(observation, firstCssBrowserDrawInputs.drawExpected);
  assert.ok(Number.isSafeInteger(observation.source_callbacks_before_kick) &&
      observation.source_callbacks_before_kick >= 0,
    'First-CSS pre-kick callback count is not a nonnegative integer');
  assert.equal(observation.source_steps, 1);
  assert.equal(observation.source_draws, 1);
  assert.equal(observation.host_tick_calls, 1);
  assert.equal(observation.host_draw_calls, 1);
  assert.equal(observation.aurora_begin_calls, 1);
  assert.equal(observation.aurora_end_calls, 1);
  assert.equal(observation.frame_end_returned, true);
  assert.equal(observation.preparation_source_callbacks, 0);
  assert.equal(observation.native_phase, 1);
  assert.equal(observation.running, 0,
    'Completed one-shot CSS draw did not stop the native source clock');
  if (firstCssBrowserStream) {
    await saveScreenshot('stadium-first-css-browser-draw');
    observation = await runFirstCssBrowserPostdrawStream(observation);
    if (firstCssBrowserFinalDraw) {
      observation = await runFirstCssBrowserFinalPendingDraw();
      if (firstSssConstructorPair) {
        observation = await runFirstSssConstructorPair();
        if (firstSssConsumedPadTick) {
          const constructorAndTick = await runFirstSssConsumedPadTick(observation);
          observation = constructorAndTick;
          if (firstSssDraw) {
            const firstDraw = await runFirstSssDraw(constructorAndTick);
            observation = firstDraw;
            if (firstSssPrefix) {
              observation = await runFirstSssPrefix(constructorAndTick, firstDraw);
              if (firstSssVsEntry) observation = await runFirstSssVsEntry(observation);
              if (firstSssVsSetup) observation = await runFirstSssVsSetup(observation);
            }
          }
        }
      }
    }
  }
  report.scenario.final_phase = observation.native_phase;
  report.scenario.final_running = observation.running;
  report.scenario.native_observation = observation;
  report.scenario.native_message = report.scenario.original_css_phase.message;
  report.scenario.native_diagnostics = report.scenario.original_css_phase.diagnostics;
  report.scenario.gpu = await timeout(page.evaluate(async () => {
    let adapter = null, failure = null;
    try { adapter = await navigator.gpu?.requestAdapter(); }
    catch (error) { failure = String(error); }
    const info = adapter?.info ? {vendor: adapter.info.vendor || null,
      architecture: adapter.info.architecture || null, device: adapter.info.device || null,
      description: adapter.info.description || null} : null;
    return {cross_origin_isolated: crossOriginIsolated === true, webgpu_api: !!navigator.gpu,
      adapter_available: !!adapter, adapter_info: info, failure,
      canvas: [Module.canvas?.width ?? null, Module.canvas?.height ?? null]};
  }), Math.min(10000, remaining()), 'First-CSS browser WebGPU snapshot');
  assert.equal(report.scenario.gpu.cross_origin_isolated, true);
  assert.equal(report.scenario.gpu.adapter_available, true);
  await saveScreenshot(firstSssPrefix ? 'stadium-first-sss-prefix-final' :
    firstSssDraw ? 'stadium-first-sss-draw-final' :
    firstSssConstructorPair ? 'stadium-first-sss-constructor-pair-final' :
    firstCssBrowserFinalDraw ? 'stadium-first-css-browser-final-pending-draw' :
    firstCssBrowserStream ? 'stadium-first-css-browser-postdraw-stream' : 'stadium-first-css-browser-draw');
  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  report.scenario.console_warnings = consoleWarnings;
  report.scenario.console_messages = consoleMessages;
  report.scenario.console_message_capture_overflow = consoleMessageCaptureOverflow;
  report.scenario.console_message_dropped_count = consoleMessageDropped;
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(externalRequests, []);

  await unloadFirstCssDiagnosticOwner();
  const afterUnload = await readFirstCssDiagnosticPostUnloadSnapshot();
  assert.equal(afterUnload.phase, 0);
  assert.equal(afterUnload.running, 0);
  assert.equal(afterUnload.observation, null);
  assert.equal(afterUnload.error, null);
  assert.equal(afterUnload.import_enabled, true);
  if (firstSssConstructorPair)
    assert.equal(afterUnload.first_sss_constructor_pair, null,
      'SSS pair diagnostic ownership survived teardown');
  if (firstSssConsumedPadTick)
    assert.equal(afterUnload.first_sss_consumed_tick, null,
      'SSS consumed-tick diagnostic ownership survived teardown');
  if (firstSssDraw)
    assert.equal(afterUnload.first_sss_draw, null,
      'SSS first-draw diagnostic ownership survived teardown');
  if (firstSssPrefix)
    assert.equal(afterUnload.first_sss_prefix, null,
      'SSS prefix diagnostic ownership survived teardown');
  if (firstSssVsSetup) assert.equal(afterUnload.first_sss_vs_setup, null);
  assertFirstCssDiagnosticMatchedSetupCfPostUnload(afterUnload);
  if (firstSssVsSetupTextTail) {
    assert.equal(afterUnload.retained_text_tail_wrapper, true,
      'Native unload did not retain the scalar text-tail outcome');
    const harvested = await timeout(harvestFirstSssTextTailAfterUnload(),
      Math.min(5000, remaining()), 'Harvest bounded text-tail logs and native cleanup state');
    report.scenario.first_sss_vs_setup.text_tail_probe.after_unload = harvested;
    afterUnload.text_tail_probe = harvested.text_tail_probe;
    afterUnload.text_tail_log = harvested.text_tail_log;
    afterUnload.log_probe_after_reset = harvested.log_probe_after_reset;
    await persistReport();
    assert.equal(harvested.ordinary_observation_present, false,
      'Ordinary Setup observation survived checked unload');
    assert.equal(harvested.text_tail_probe?.cleanup_done, true);
    assert.equal(harvested.text_tail_probe?.probe_enabled, false);
    assert.equal(harvested.text_tail_probe?.membership_snapshot_active, false);
    assert.equal(harvested.text_tail_probe?.membership_snapshot_ended, true);
    const logValidation = validateFirstSssTextTailLog(harvested.text_tail_log);
    report.scenario.first_sss_vs_setup.text_tail_probe.log_validation_after_unload = logValidation;
    assert.equal(logValidation.complete, true,
      `Setup text-tail log is incomplete: ${logValidation.errors.join('; ')}`);
    assert.equal(harvested.log_probe_after_reset?.enabled, false);
    assert.equal(harvested.log_probe_after_reset?.retained_records, 0);
  }
  if (firstSssVsEntry) assert.equal(afterUnload.first_sss_vs_entry, null,
    'VS Entry native observation survived teardown');
  report.cleanup.native_after_unload = afterUnload;
  report.cleanup.observation_cleared = true;
  await saveScreenshot('stadium-first-css-browser-draw-after-unload');
  report.profile.after = summarizeStorage(await timeout(context.storageState(),
    Math.min(5000, remaining()), 'First-CSS post-run browser profile snapshot'));
  const after = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(after, artifacts, 'After first-CSS run');
  report.local_http_artifacts.after = after;
  report.local_http_artifacts.unchanged = JSON.stringify(before) === JSON.stringify(after);
  assert.equal(report.local_http_artifacts.unchanged, true);
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(consoleErrors, []);
  report.result = 'pass';
}

async function runCssSssTransition({startKey, capture, routeName}) {
  const samples = capture.samples;
  await timeout(page.locator('#canvas').focus({timeout: Math.min(5000, remaining())}),
    Math.min(5000, remaining()), `${routeName} canvas focus before CSS readiness observation`);
  capture.canvas_focused_before_observation = true;
  for (let frame = 0; frame < capture.max_readiness_frames; frame++) {
    const sample = await observeCssInputFrame(`${routeName} CSS readiness RAF sample ${frame}`);
    samples.push(sample);
    if (sample.observer_error || sample.runtime_error) {
      capture.outcome = {accepted: false, failure_boundary: sample.runtime_error
        ? 'native-runtime-error' : 'css-or-input-observer-error', sample_index: frame,
        detail: sample.runtime_error || sample.observer_error};
      await persistReport();
      throw Error(`${capture.outcome.failure_boundary}: ${capture.outcome.detail}`);
    }
    const progress = cssReadinessEvidence(samples);
    if (hasCssMenuReadiness(sample) && progress.input_progressed && progress.source_callback_progressed) {
      capture.readiness = {...progress, ready_sample: frame,
        source_start_cooldown: sample.css.ids[5], source_pending_scene: sample.css.ids[7],
        source_start_ready: sample.css.ids[8], source_callback_count: sample.css.ids[10],
        input_sample_count: sample.input.samples, p1: sample.input.pads[0]};
      break;
    }
    if (frame % 10 === 9) await persistReport();
  }
  const lastReadySample = samples.at(-1);
  const readiness = cssReadinessEvidence(samples);
  capture.readiness ||= {...readiness, source_start_cooldown: lastReadySample?.css?.ids?.[5] ?? null,
    source_pending_scene: lastReadySample?.css?.ids?.[7] ?? null,
    source_start_ready: lastReadySample?.css?.ids?.[8] ?? null,
    source_callback_count: lastReadySample?.css?.ids?.[10] ?? null,
    input_sample_count: lastReadySample?.input?.samples ?? null,
    p1: lastReadySample?.input?.pads?.[0] ?? null};
  if (!readiness.readiness_observed || !readiness.input_progressed || !readiness.source_callback_progressed ||
      !hasCssMenuReadiness(lastReadySample)) {
    capture.outcome = {accepted: false, failure_boundary: !readiness.readiness_observed
      ? 'css-readiness-not-observed' : !readiness.input_progressed || !readiness.source_callback_progressed
      ? 'css-source-or-input-progress-not-observed' : 'css-readiness-not-current-at-start-gate'};
    await persistReport();
    throw Error(`${routeName} stopped before sending Start: ${capture.outcome.failure_boundary}`);
  }

  const baselineSamples = lastReadySample.input.samples;
  if (lastReadySample.input.pads[0].buttons !== 0) {
    capture.outcome = {accepted: false, failure_boundary: 'p1-not-neutral-before-start',
      buttons: lastReadySample.input.pads[0].buttons};
    await persistReport();
    throw Error(`${routeName} stopped because P1 raw PAD was not neutral before Start`);
  }
  capture.input_before_start = lastReadySample.input;
  capture.start_key = startKey;
  const postStartSampleStart = samples.length;
  await persistReport();
  const transitionStartedAt = Date.now();
  const transitionDeadline = Math.min(deadline, transitionStartedAt + LIMITS.sourceTransitionMs);
  try {
    const chordTimeout = Math.min(transitionDeadline - Date.now(), remaining());
    if (chordTimeout <= 0) throw Error('Source transition deadline expired before the Start chord');
    await timeout(driver.pressChord([startKey], {holdMs: 120, releaseMs: 150}), chordTimeout,
      `${routeName} single ordinary B0XX CSS-to-SSS Start chord`);
  } catch (error) {
    capture.transition = {started_at_epoch_ms: transitionStartedAt,
      elapsed_ms: Date.now() - transitionStartedAt, sample_count: 0,
      sample_cap: capture.max_post_start_frames, sample_cap_hz: capture.post_start_sample_cap_hz,
      deadline_ms: LIMITS.sourceTransitionMs,
      stop: 'start-chord-error', observation_error: errorText(error)};
    capture.outcome = {accepted: false, failure_boundary: 'start-chord-delivery-error',
      detail: errorText(error)};
    await persistReport();
    throw error;
  }
  let finalSample = null;
  let transitionStop = 'sample-cap';
  let transitionError = null;
  for (let frame = 0; frame < capture.max_post_start_frames; frame++) {
    if (Date.now() >= transitionDeadline) { transitionStop = 'source-transition-deadline'; break; }
    try {
      finalSample = await observeCssInputFrame(`${routeName} post-Start RAF sample ${frame}`,
        transitionDeadline);
    } catch (error) {
      transitionStop = Date.now() >= transitionDeadline ? 'source-transition-deadline' : 'observation-error';
      transitionError = errorText(error);
      break;
    }
    samples.push(finalSample);
    if (finalSample.observer_error || finalSample.runtime_error) {
      transitionStop = finalSample.runtime_error ? 'native-runtime-error' : 'css-or-input-observer-error';
      transitionError = finalSample.runtime_error || finalSample.observer_error;
      break;
    }
    if (frame % 60 === 59) await persistReport();
    if (finalSample.phase === 3 && finalSample.running === 1) {
      transitionStop = 'sss-phase-3-running';
      break;
    }
  }
  capture.transition = {started_at_epoch_ms: transitionStartedAt,
    elapsed_ms: Date.now() - transitionStartedAt,
    sample_count: samples.length - postStartSampleStart,
    sample_cap: capture.max_post_start_frames, sample_cap_hz: capture.post_start_sample_cap_hz,
    deadline_ms: LIMITS.sourceTransitionMs,
    stop: transitionStop, observation_error: transitionError};
  capture.input_after_start = finalSample?.input ?? null;
  const lastSourceSample = [...samples].reverse().find(sample => sample.css?.ids?.length === 14);
  capture.last_source_start = lastSourceSample ? {
    trigger: lastSourceSample.css.ids[11], ready_at_trigger: lastSourceSample.css.ids[12],
    pending_at_trigger: lastSourceSample.css.ids[13], callback_count: lastSourceSample.css.ids[10],
    phase: lastSourceSample.phase, running: lastSourceSample.running,
  } : null;
  capture.outcome = cssSssOutcome(samples, finalSample, baselineSamples);
  if (transitionError) {
    capture.outcome.failure_boundary = finalSample?.runtime_error ? 'native-runtime-error' :
      finalSample?.observer_error ? 'css-or-input-observer-error' : 'css-to-sss-observation-error';
    capture.outcome.detail = transitionError;
    capture.outcome.accepted = false;
  }
  report.scenario.final_phase = finalSample?.phase ?? null;
  report.scenario.final_running = finalSample?.running ?? null;
  report.scenario.native_message = finalSample?.message ?? null;
  report.scenario.native_diagnostics = finalSample?.diagnostics ?? null;
  await persistReport();
  return {finalSample, baselineSamples, outcome: capture.outcome};
}

async function runCssSssReducer({startKey, baseUrl, artifacts, before}) {
  const reducer = report.scenario.css_sss_reducer;
  const transition = await runCssSssTransition({startKey, capture: reducer, routeName: 'CSS-to-SSS reducer'});
  await saveScreenshot(transition.finalSample?.phase === 3
    ? 'stadium-c1a-sss-after-start' : 'stadium-c1a-css-after-start');
  if (!transition.outcome.accepted)
    throw Error(`CSS-to-SSS reducer failed at ${transition.outcome.failure_boundary}`);

  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(pageErrors, [], 'Browser page raised JavaScript errors during CSS-to-SSS');
  assert.deepEqual(externalRequests, [], 'CSS-to-SSS reducer attempted a non-loopback HTTP request');

  report.cleanup.unload_attempted = true;
  unloadAttempted = true;
  await timeout(driver.unload(), LIMITS.unloadMs, 'Recoverable CSS-to-SSS reducer unload');
  report.cleanup.unload_completed = true;
  const afterUnload = await timeout(page.evaluate(() => ({
    phase: Module._melee_web_native_menu_phase(), running: Module._melee_web_native_menu_running(),
    error: document.querySelector('#status')?.dataset.runtimeError || null,
    import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled,
  })), Math.min(5000, remaining()), 'CSS-to-SSS reducer post-unload snapshot');
  assert.equal(afterUnload.phase, 0);
  assert.equal(afterUnload.running, 0);
  assert.equal(afterUnload.error, null);
  assert.equal(afterUnload.import_enabled, true);
  report.cleanup.native_after_unload = afterUnload;
  report.cleanup.observation_cleared = true;
  await saveScreenshot('stadium-c1a-css-sss-after-unload');
  report.profile.after = summarizeStorage(await timeout(context.storageState(),
    Math.min(5000, remaining()), 'Post-reducer browser profile snapshot'));
  const after = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(after, artifacts, 'Reducer after-run');
  report.local_http_artifacts.after = after;
  report.local_http_artifacts.unchanged = JSON.stringify(before) === JSON.stringify(after);
  assert.equal(report.local_http_artifacts.unchanged, true,
    'One or more of the declared served artifacts changed during the CSS-to-SSS reducer');
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  report.result = 'pass';
}

async function cleanupServer() {
  if (!serverProcess) return;
  const child = serverProcess;
  const pid = child.pid ?? null;
  report.cleanup.server_process_pid = pid;
  let sent = null;
  const exit = () => child.exitCode !== null || child.signalCode !== null;
  const waitExit = ms => timeout(new Promise(resolve => {
    if (exit()) return resolve(true);
    const done = () => resolve(true);
    child.once('exit', done);
  }), Math.min(ms, cleanupRemaining()), 'Server process exit').catch(() => false);
  if (!exit()) {
    sent = 'SIGTERM';
    child.kill('SIGTERM');
    if (!await waitExit(3000)) {
      sent = 'SIGKILL';
      child.kill('SIGKILL');
      await waitExit(3000).catch(() => false);
    }
  }
  report.cleanup.server_process_terminated = exit();
  report.cleanup.server_process_exit = {pid, sent_signal: sent,
    exit_code: child.exitCode, signal: child.signalCode};
  if (serverOut) { serverOut.end(); await serverOutDone?.catch(() => {}); }
  if (serverErr) { serverErr.end(); await serverErrDone?.catch(() => {}); }
}

async function main() {
  assert.equal(build, expectedBuild, 'Only the isolated C1a diagnostic runtime may be served');
  assert.equal(preflight.schema, 'melee-web-stadium-c1a-browser-preflight-v1');
  if (firstSssVsSetup) {
    assert.equal(preflight.route_mode, 'first-sss-vs-setup');
    const textTailBinding = preflight.first_sss_vs_setup?.text_tail_probe;
    assert.ok(textTailBinding === undefined || typeof textTailBinding === 'boolean',
      'Setup text-tail preflight binding must be absent or boolean');
    assert.equal(textTailBinding ?? false, firstSssVsSetupTextTail,
      'Setup text-tail CLI flag differs from the exact preflight');
  }
  else if (firstSssVsEntry)
    assert.equal(preflight.route_mode, 'first-sss-vs-entry',
      'VS Entry command requires its exact frozen preflight route');
  else if (firstSssPrefix)
    assert.equal(preflight.route_mode, 'first-sss-prefix',
      'SSS prefix command requires its exact frozen preflight route');
  else if (values['first-sss-draw'])
    assert.equal(preflight.route_mode, 'first-sss-draw',
      'SSS first-draw command requires its exact frozen preflight route');
  else if (firstSssConsumedPadTick)
    assert.equal(preflight.route_mode, 'first-sss-consumed-pad-tick',
      'SSS consumed-tick command requires its exact frozen preflight route');
  else if (firstSssConstructorPair)
    assert.equal(preflight.route_mode, 'first-sss-constructor-pair',
      'SSS constructor-pair command requires its exact frozen preflight route');
  else if (firstCssBrowserDraw)
    assert.equal(preflight.route_mode, firstCssBrowserStream
      ? (firstCssBrowserFinalDraw ? 'first-css-browser-final-pending-draw' :
        'first-css-browser-postdraw-stream') : 'first-css-browser-draw',
      'First-CSS diagnostic command requires its exact frozen preflight route');
  else if (values['css-sss-reducer'])
    assert.equal(preflight.route_mode, 'css-sss-reducer', 'Reducer command requires a frozen CSS-to-SSS preflight');
  else if (preflight.route_mode)
    assert.equal(preflight.route_mode, 'full-c1a', 'Full C1a command differs from its frozen preflight route');
  assert.equal(preflight.source_revision, values['source-revision'],
    'Command source revision differs from the frozen preflight');
  assert.equal(path.resolve(preflight.build.path), build, 'Served build differs from frozen preflight');
  assert.equal(path.resolve(preflight.disc.path), path.resolve(values.disc),
    'Command disc differs from frozen preflight');
  assert.equal(preflight.disc.sha256, values['disc-sha256'],
    'Command disc hash differs from frozen preflight');
  assert.equal(path.resolve(preflight.browser.playwright_package_path), path.resolve(values.playwright),
    'Command Playwright package differs from frozen preflight');
  assert.equal(path.resolve(preflight.output.path), output,
    'Fresh run output differs from frozen preflight');
  const runnerPath = path.resolve(fileURLToPath(import.meta.url));
  assert.equal(path.resolve(preflight.test_script.path), runnerPath,
    'Browser runner path differs from frozen preflight');
  assert.equal(await hashFile(runnerPath), preflight.test_script.sha256,
    'Browser runner bytes differ from frozen preflight');
  matchedSetupCfInputs = await loadMatchedSetupCfInputs();
  if (values['css-sss-reducer']) {
    const helperPath = path.join(ROOT, 'tests/stadium_c1a_css_sss_reducer.mjs');
    assert.equal(path.resolve(preflight.reducer_helper?.path || ''), helperPath,
      'Reducer command omitted or changed its acceptance helper path');
    assert.equal(await hashFile(helperPath), preflight.reducer_helper.sha256,
      'Reducer acceptance helper bytes differ from the frozen preflight');
  }
  firstCssBrowserDrawInputs = await loadFirstCssBrowserDrawInputs();
  firstCssBrowserStreamInputs = await loadFirstCssBrowserStreamInputs();
  firstSssConstructorPairInputs = await loadFirstSssConstructorPairInputs();
  firstSssConsumedPadTickInputs = await loadFirstSssConsumedPadTickInputs();
  firstSssDrawInputs = await loadFirstSssDrawInputs();
  firstSssPrefixInputs = await loadFirstSssPrefixInputs();
  firstSssVsEntryInputs = await loadFirstSssVsEntryInputs();
  firstSssVsSetupInputs = await loadFirstSssVsSetupInputs();
  assert.deepEqual(preflight.limits_ms, LIMITS, 'Runtime bounds differ from frozen preflight');
  assert.equal(LIMITS.captureWorkMs,
    preflight.owner_deadline.overall_timeout_ms - preflight.owner_deadline.cleanup_reserve_ms,
    'Inner capture bound differs from the external owner deadline and cleanup reserve');
  assert.ok(LIMITS.cleanupTotalMs <= preflight.owner_deadline.cleanup_reserve_ms,
    'Inner cleanup budget exceeds the external owner cleanup reserve');
  assert.equal(execFileSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'}).trim(),
    values['source-revision'], 'Checkout HEAD changed after preflight');
  assert.equal(execFileSync('git', ['status', '--porcelain', '--untracked-files=no'],
    {cwd: ROOT, encoding: 'utf8'}).trim(), '', 'Tracked source must be clean for browser evidence');
  assert.ok(typeof preflight.timing_marker_path === 'string' && preflight.timing_marker_path,
    'Frozen preflight omitted the shared timing lane marker path');
  const timingMarker = path.resolve(preflight.timing_marker_path);
  const marker = await fs.stat(timingMarker).then(() => true, error => error.code === 'ENOENT' ? false : Promise.reject(error));
  assert.equal(marker, false, 'Refusing browser launch while the shared timing lane marker exists');
  const discHash = await hashFile(values.disc);
  assert.equal(discHash, expectedDiscSha256, 'Owned original disc identity changed');
  assert.equal(values['disc-sha256'], expectedDiscSha256, 'Command did not pin the approved disc identity');
  report.disc.sha256 = discHash;

  const diagCache = await fs.readFile(path.join(build, 'CMakeCache.txt'), 'utf8');
  assert.match(diagCache, /^MELEE_WEB_STADIUM_C1A_DIAGNOSTIC:BOOL=ON$/m);
  const defaultCache = await fs.readFile(path.join(defaultBuild, 'CMakeCache.txt'), 'utf8');
  assert.match(defaultCache, /^MELEE_WEB_STADIUM_C1A_DIAGNOSTIC:BOOL=OFF$/m);
  const defaultJs = await fs.readFile(path.join(defaultBuild, 'gameplay_menu_browser.js'), 'utf8');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_c1a_arm'), false,
    'Ordinary default runtime unexpectedly exports the private C1a arm gate');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_css_draw_arm'), false,
    'Ordinary default runtime unexpectedly exports the private first-CSS diagnostic gate');

  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_css_postdraw_stream_arm'), false,
    'Ordinary default runtime unexpectedly exports the private CSS stream gate');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_tick_arm'), false,
    'Ordinary default runtime unexpectedly exports the private SSS tick diagnostic gate');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_draw_arm'), false,
    'Ordinary default runtime unexpectedly exports the private SSS draw diagnostic gate');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_vs_setup_begin'), false,
    'Ordinary build exposes private VS Setup diagnostic');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_vs_entry_kick'), false,
    'VS Entry endpoint export leaked into ordinary runtime');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_prefix_arm'), false,
    'Ordinary default runtime unexpectedly exports the private SSS prefix diagnostic gate');
  for (const name of ['arm', 'queue', 'approve'])
    assert.equal(defaultJs.includes(`_melee_web_native_menu_stadium_first_sss_vs_setup_cf_${name}`), false,
      `Ordinary default runtime unexpectedly exports matched Setup C/F ${name}`);

  const artifacts = JSON.parse(await fs.readFile(path.join(ROOT, 'tools/browser_build_artifacts.json'), 'utf8'));
  validateBrowserArtifactNames(artifacts);

  report.started_at = new Date().toISOString();
  await persistReport();

  const port = await allocateLoopbackPort();
  const baseUrl = `http://127.0.0.1:${port}`;
  serverOut = createWriteStream(path.join(output, 'server.stdout.log'));
  serverErr = createWriteStream(path.join(output, 'server.stderr.log'));
  serverOutDone = finished(serverOut);
  serverErrDone = finished(serverErr);
  serverProcess = spawn('python3', [path.join(ROOT, 'scripts/serve.py'),
    '--directory', build, '--port', String(port)], {cwd: ROOT, stdio: ['ignore', 'pipe', 'pipe']});
  serverProcess.once('error', error => { serverSpawnError = errorText(error); });
  report.cleanup.server_process_pid = serverProcess.pid ?? null;
  await persistReport();
  serverProcess.stdout.pipe(serverOut);
  serverProcess.stderr.pipe(serverErr);
  await waitForServer(baseUrl);

  const before = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(before, artifacts, 'Before-run');
  report.local_http_artifacts.count = artifacts.length;
  report.local_http_artifacts.before = before;

  const tools = await loadBrowserTools(values.playwright);
  assert.equal(path.resolve(tools.browserPath), path.resolve(preflight.browser.executable_path),
    'Resolved installed browser differs from frozen preflight');
  const playwrightPackage = JSON.parse(await fs.readFile(path.join(values.playwright, 'package.json'), 'utf8'));
  assert.equal(playwrightPackage.version, preflight.browser.playwright_version,
    'Resolved Playwright version differs from frozen preflight');
  const launchConfig = browserLaunchOptions(tools.browser, {headed: false, audible: false,
    timeout: LIMITS.browserLaunchMs});
  browserServer = await timeout(tools.chromium.launchServer(launchConfig), LIMITS.browserLaunchMs,
    'Headless installed Chrome launch');
  browserProcess = browserServer.process();
  report.cleanup.browser_process_pid = browserProcess.pid ?? null;
  browser = await timeout(tools.chromium.connect(browserServer.wsEndpoint()),
    LIMITS.browserLaunchMs, 'Playwright connection to owned Chrome');
  const browserVersion = browser.version();
  assert.equal(browserVersion, preflight.browser.version,
    'Installed Chrome changed since the frozen preflight');
  browserCdp = await timeout(browser.newBrowserCDPSession(), LIMITS.browserLaunchMs,
    'Chrome browser identity session');
  const cdpIdentity = await timeout(browserCdp.send('Browser.getVersion'), LIMITS.browserLaunchMs,
    'Chrome Browser.getVersion');
  assert.equal(cdpIdentity.product, `Chrome/${preflight.browser.version}`,
    'CDP product identity differs from the pinned installed Chrome');
  report.browser = {engine: 'installed Google Chrome', executable_path: tools.browserPath,
    version: browserVersion, cdp_product: cdpIdentity.product,
    cdp_protocol_version: cdpIdentity.protocolVersion,
    cdp_revision: cdpIdentity.revision, cdp_user_agent: cdpIdentity.userAgent,
    cdp_javascript_version: cdpIdentity.jsVersion,
    playwright_version: playwrightPackage.version, playwright_package_path: tools.playwrightPath,
    process_pid: browserProcess.pid ?? null, headless: true, speaker_output_muted: true,
    audio_processing_disabled: false,
    launch: {headless: launchConfig.headless, chromium_sandbox: launchConfig.chromiumSandbox,
      muted_audio: launchConfig.args.includes('--mute-audio')}};
  const cdpProcesses = await timeout(browserCdp.send('SystemInfo.getProcessInfo'),
    LIMITS.browserLaunchMs, 'Chrome SystemInfo.getProcessInfo');
  assert.ok(Array.isArray(cdpProcesses.processInfo) && cdpProcesses.processInfo.length > 0,
    'Chrome CDP did not report its live process inventory');
  report.browser_process_info = cdpProcesses.processInfo;
  report.browser.cdp_process_inventory_count = cdpProcesses.processInfo.length;
  await persistReport();
  context = await timeout(browser.newContext({viewport: {width: 1100, height: 820}, acceptDownloads: false}),
    Math.min(5000, remaining()), 'Fresh browser context creation');
  const storageBefore = await timeout(context.storageState(), Math.min(5000, remaining()),
    'Fresh browser profile snapshot');
  assert.deepEqual(storageBefore.cookies, [], 'Fresh browser context unexpectedly has cookies');
  assert.deepEqual(storageBefore.origins, [], 'Fresh browser context unexpectedly has origins');
  report.profile.before = summarizeStorage(storageBefore);
  page = await timeout(context.newPage(), Math.min(5000, remaining()), 'Fresh browser page creation');
  page.setDefaultTimeout(10000);
  page.setDefaultNavigationTimeout(30000);
  page.on('pageerror', error => pageErrors.push(errorText(error)));
  page.on('console', captureBrowserConsoleMessage);
  page.on('request', request => {
    const url = new URL(request.url());
    const row = {event: 'request', method: request.method(), origin: url.origin,
      path: url.pathname, resource_type: request.resourceType()};
    requestRows.push(row);
    if (['http:', 'https:'].includes(url.protocol) && url.origin !== baseUrl) externalRequests.push(row);
  });
  page.on('response', response => {
    const url = new URL(response.url());
    responseRows.push({event: 'response', origin: url.origin, path: url.pathname,
      status: response.status(), from_service_worker: response.fromServiceWorker()});
  });
  page.on('requestfailed', request => {
    const url = new URL(request.url());
    requestRows.push({event: 'requestfailed', method: request.method(), origin: url.origin,
      path: url.pathname, failure: request.failure()?.errorText || 'unknown'});
  });
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: LIMITS.pageStartMs,
    deadline});

  await timeout(page.goto(`${baseUrl}/runtime.html`, {waitUntil: 'commit', timeout: remaining(30000)}),
    remaining(30000), 'Runtime page navigation');
  await driver.waitForImport();
  const readControls = () => timeout(page.evaluate(() => ({
    layout: document.querySelector('#keyboard-layout')?.value ?? null,
    sources: ['#player-one-source', '#player-two-source'].map(selector =>
      document.querySelector(selector)?.value ?? null),
    key_bindings: [...document.querySelectorAll('#keyboard-bindings tbody tr')].map(row =>
      [...row.cells].map(cell => cell.textContent.trim())),
  })), Math.min(5000, remaining()), 'Runtime Controls snapshot');
  await page.locator('#controls-open').click();
  const controlsBefore = await readControls();
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.getByLabel('Player 1 input source', {exact: true}).selectOption('keyboard');
  await page.getByLabel('Player 2 input source', {exact: true}).selectOption('off');
  const controlsForRecipe = await readControls();
  assert.deepEqual({layout: controlsForRecipe.layout, sources: controlsForRecipe.sources},
    {layout: 'boxx', sources: ['keyboard', 'off']},
    'C1a must use the established single-player B0XX keyboard Controls recipe');
  const startKey = controlsForRecipe.key_bindings.find(([action]) => action === 'Start')?.[1];
  const attackKey = controlsForRecipe.key_bindings.find(([action]) => action === 'Attack')?.[1];
  assert.equal(startKey, '7', 'The live B0XX Controls table must map source PAD Start to 7');
  assert.equal(attackKey, 'M', 'The live B0XX Controls table must map source PAD A to M');
  report.scenario.controls = {initial: controlsBefore, configured: controlsForRecipe};
  report.scenario.input_recipe = {
    layout: 'boxx', player1: 'keyboard', player2: 'off',
    css_to_sss_start_key: startKey, sss_a_confirm_key: attackKey,
  };
  await page.locator('#controls-close').click();
  await driver.selectDisc(values.disc);
  await timeout(driver.waitForStart(), Math.min(LIMITS.discImportMs, remaining()), 'Disc import and native prep');
  if (firstCssBrowserDraw) {
    await runFirstCssBrowserDraw({baseUrl, artifacts, before});
    return;
  }

  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    if (!module || typeof module._melee_web_native_menu_stadium_c1a_arm !== 'function')
      throw Error('Private C1a arm export is absent from this runtime');
    if (module._melee_web_native_menu_running() !== 0)
      throw Error('C1a arm was attempted after source time started');
    const observe = module._melee_web_native_menu_stadium_c1a_observe;
    if (typeof observe !== 'function') throw Error('Private C1a observation export is absent');
    window.__meleeStadiumC1aPayload = null;
    window.menuStadiumC1aPrepared = payload => { window.__meleeStadiumC1aPayload = payload; };
    const result = module._melee_web_native_menu_stadium_c1a_arm();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'C1a arm rejected the prepared VS session');
    }
    return {result, phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running()};
  }), Math.min(5000, remaining()), 'Arm scoped C1a menu session');
  assert.equal(arm.result, 1);
  assert.equal(arm.running, 0);
  report.scenario.armed_before_css_entry = true;

  await timeout(driver.launch(1), Math.min(LIMITS.sourceTransitionMs, remaining()),
    'Original CSS launch');
  report.scenario.original_css_phase = await sourceSnapshot('Original CSS native snapshot');
  assert.equal(report.scenario.original_css_phase.phase, 1, 'Original CSS phase did not start');
  assert.equal(report.scenario.original_css_phase.running, 1, 'Original CSS source time did not run');
  if (values['css-sss-reducer']) {
    await runCssSssReducer({startKey, baseUrl, artifacts, before});
    return;
  }

  report.scenario.css_sss_transition = createCssSssTransitionCapture(
    'Original CSS -> original SSS, then continue the existing raw-PAD Stadium tile selection and C1a preparation route.');
  const cssSssTransition = await runCssSssTransition({startKey,
    capture: report.scenario.css_sss_transition, routeName: 'Full C1a route'});
  await saveScreenshot(cssSssTransition.finalSample?.phase === 3
    ? 'stadium-c1a-sss-after-start' : 'stadium-c1a-css-after-start');
  assert.equal(cssSssTransition.outcome.accepted, true,
    `Full C1a CSS-to-SSS transition failed at ${cssSssTransition.outcome.failure_boundary}`);
  report.scenario.original_sss_phase = {
    phase: cssSssTransition.finalSample.phase,
    running: cssSssTransition.finalSample.running,
    message: cssSssTransition.finalSample.message,
    diagnostics: cssSssTransition.finalSample.diagnostics,
    error: cssSssTransition.finalSample.runtime_error,
  };
  assert.equal(report.scenario.original_sss_phase.phase, 3, 'Original SSS phase did not start');
  let reached = false;
  const stageDeadline = Math.min(deadline, Date.now() + LIMITS.stageDriveMs);
  const stageRemaining = () => {
    const ms = stageDeadline - Date.now();
    if (ms <= 0) throw Error('C1a Stadium raw-PAD stage-drive deadline exhausted');
    return ms;
  };
  for (let frame = 0; frame < LIMITS.stageDriveFrames; frame++) {
    const state = await timeout(page.evaluate(() => {
      const module = globalThis.Module;
      if (typeof module?._melee_web_native_menu_drive_stage !== 'function')
        throw Error('Source-observer raw-PAD stage driver is absent');
      return module._melee_web_native_menu_drive_stage(3);
    }), Math.min(5000, stageRemaining()), 'Raw-PAD source stage observation');
    if (state === 2) { reached = true; report.scenario.raw_pad_stage_drive_frames = frame; break; }
    assert.equal(state, 1, 'Raw-PAD source stage driver failed');
    await timeout(page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => resolve()))),
      Math.min(5000, stageRemaining()), 'Raw-PAD source frame');
  }
  assert.equal(reached, true, 'Bounded raw-PAD input did not reach source Stadium SSS tile');
  await saveScreenshot('stadium-c1a-sss-stadium-selected');
  await timeout(driver.pressChord([attackKey.toLowerCase()], {holdMs: 120, releaseMs: 150}),
    Math.min(LIMITS.sourceTransitionMs, remaining()), 'Raw-PAD B0XX SSS A confirmation');
  await timeout(page.waitForFunction(() => typeof window.__meleeStadiumC1aPayload === 'string' &&
    window.__meleeStadiumC1aPayload.length > 0, null,
  {timeout: Math.min(LIMITS.manifestHandoffMs, remaining())}),
  Math.min(LIMITS.manifestHandoffMs, remaining()), 'C1a exact manifest handoff');

  const result = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module._melee_web_native_menu_stadium_c1a_observe();
    const observation = pointer ? module.UTF8ToString(pointer) : null;
    return {callback: window.__meleeStadiumC1aPayload, observation,
      phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running(),
      message: module.UTF8ToString(module._melee_web_native_menu_message()),
      diagnostics: module.UTF8ToString(module._melee_web_native_menu_diagnostics())};
  }), Math.min(5000, remaining()), 'C1a native manifest observation');
  assert.equal(result.observation, result.callback, 'Native and browser C1a observations disagree');
  const observed = JSON.parse(result.callback);
  assert.equal(observed.checkpoint, 'C1a');
  assert.equal(observed.raw_sss_stkind, 3);
  assert.equal(observed.prepared_stkind, 3);
  assert.equal(observed.manifest_committed, true);
  assert.equal(observed.resolvedname, null);
  assert.equal(observed.runtime_source_file_service_request_observed, false);
  assert.equal(observed.match_constructed, false);
  assert.equal(observed.stage_constructed, false);
  assert.equal(observed.recoverable_stop, 'before source archive request and match admission');
  assert.ok(Array.isArray(observed.manifest_files) && observed.manifest_files.length > 0);
  assert.equal(new Set(observed.manifest_files).size, observed.manifest_files.length,
    'Exact native manifest contains duplicate filenames');
  for (const name of ['GrPs.usd','GrPs1.dat','GrPs2.dat','GrPs3.dat','GrPs4.dat',
    'pstadium.hps','pokesta.hps','pstadium.ssm'])
    assert.ok(observed.manifest_files.includes(name), `Exact native manifest omitted ${name}`);
  assert.notEqual(result.phase, 7, 'A match was constructed');
  assert.notEqual(result.phase, 8, 'Results were entered');
  assert.notEqual(result.phase, 9, 'Prize scene was entered');
  assert.equal(result.running, 0, 'C1a did not stop source time at its recoverable boundary');
  assert.match(result.message, /C1a preparation complete/);
  report.scenario.source_selection = observed;
  report.scenario.requested_committed_manifest = {
    generator: 'melee_web::stadium_c1a_asset_names(asset_selection)',
    exact_names: observed.manifest_files,
    native_request_equals_committed_manifest: observed.manifest_committed,
    native_equality_boundary: 'finish_asset_handoff verifies requested_assets equality and complete imported file scope',
  };
  report.scenario.native_observation = result.observation;
  report.scenario.final_phase = result.phase;
  report.scenario.final_running = result.running;
  report.scenario.native_message = result.message;
  report.scenario.native_diagnostics = result.diagnostics;

  report.scenario.gpu = await timeout(page.evaluate(async () => {
    let adapter = null, failure = null;
    try { adapter = await navigator.gpu?.requestAdapter(); }
    catch (error) { failure = String(error); }
    const info = adapter?.info ? {
      vendor: adapter.info.vendor || null, architecture: adapter.info.architecture || null,
      device: adapter.info.device || null, description: adapter.info.description || null,
    } : null;
    return {cross_origin_isolated: crossOriginIsolated === true, webgpu_api: !!navigator.gpu,
      adapter_available: !!adapter, adapter_info: info, failure,
      canvas: [Module.canvas?.width ?? null, Module.canvas?.height ?? null]};
  }), Math.min(10000, remaining()), 'WebGPU adapter snapshot');
  assert.equal(report.scenario.gpu.cross_origin_isolated, true, 'Runtime page is not cross-origin isolated');
  assert.equal(report.scenario.gpu.adapter_available, true, 'Headless Chrome has no WebGPU adapter');
  await saveScreenshot('stadium-c1a-pre-unload');
  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(pageErrors, [], 'Browser page raised JavaScript errors');
  assert.deepEqual(externalRequests, [], 'C1a browser page attempted a non-loopback HTTP request');

  report.cleanup.unload_attempted = true;
  unloadAttempted = true;
  if (matchedSetupCfExplicitArmed) {
    await retireMatchedSetupCfExplicitNative();
  } else {
    await assertMatchedSetupCfAsyncifyIdle('Check Asyncify before recoverable native menu unload');
    await timeout(driver.unload(), LIMITS.unloadMs, 'Recoverable native menu unload');
    report.cleanup.unload_completed = true;
  }
  const afterUnload = await timeout(page.evaluate(requireAsyncifyIdle => {
    const module = globalThis.Module;
    if (requireAsyncifyIdle &&
        (typeof module?.stadiumMatchedSetupNativeIdle !== 'function' ||
         module.stadiumMatchedSetupNativeIdle() !== true))
      return {__native_asyncify_idle: false};
    const pointer = module._melee_web_native_menu_stadium_c1a_observe?.();
    return {phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running(),
      observation: pointer ? module.UTF8ToString(pointer) : null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled};
  }, firstSssVsSetupCfExplicitStep), Math.min(5000, remaining()), 'Post-unload native snapshot');
  if (afterUnload?.__native_asyncify_idle === false) {
    matchedSetupCfExplicitAsyncifyUnsafe = true;
    throw Error('Asyncify is not idle; post-unload native snapshot was suppressed');
  }
  assert.equal(afterUnload.phase, 0, 'Unload did not restore the native menu host to closed phase');
  assert.equal(afterUnload.running, 0);
  assert.equal(afterUnload.observation, null, 'C1a permission/observation survived teardown');
  assert.equal(afterUnload.error, null);
  assert.equal(afterUnload.import_enabled, true);
  report.cleanup.native_after_unload = afterUnload;
  report.cleanup.observation_cleared = true;
  await saveScreenshot('stadium-c1a-after-unload');
  report.profile.after = summarizeStorage(await timeout(context.storageState(),
    Math.min(5000, remaining()), 'Post-run browser profile snapshot'));
  const after = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(after, artifacts, 'After-run');
  report.local_http_artifacts.after = after;
  report.local_http_artifacts.unchanged = JSON.stringify(before) === JSON.stringify(after);
  assert.equal(report.local_http_artifacts.unchanged, true,
    'One or more of the declared served build artifacts changed during the browser attempt');
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(externalRequests, []);
  report.result = 'pass';
}

async function cleanupOwnedProcesses() {
  cleanupDeadline = Date.now() + LIMITS.cleanupTotalMs;
  const cleanupFailures = report.cleanup.failures;
  const cleanupGate = await guardMatchedSetupCfNativeCleanup({
    explicitStep: firstSssVsSetupCfExplicitStep,
    pendingStep: pendingMatchedSetupCfExplicitStep,
    forcedUnsafe: matchedSetupCfExplicitAsyncifyUnsafe ||
      matchedSetupCfExplicitNativeAccessClosed,
    checkNativeIdle: async () => {
      if (!page || page.isClosed()) return false;
      return await timeout(page.evaluate(() => {
        const predicate = globalThis.Module?.stadiumMatchedSetupNativeIdle;
        return typeof predicate === 'function' && predicate() === true;
      }), Math.min(1000, cleanupRemaining()),
      'JS-only Asyncify native-idle check at cleanup entry') === true;
    },
    onNativeAccessAllowed: async status => {
      report.cleanup.asyncify_cleanup_entry_check = status.idle_check;
      report.cleanup.asyncify_cleanup_entry_pending_safe = status.pending_safe;
    },
    onNativeAccessBlocked: async status => {
      report.cleanup.native_cleanup_suppressed_asyncify = true;
      report.cleanup.asyncify_cleanup_entry_check = status.idle_check;
      report.cleanup.asyncify_cleanup_entry_pending_safe = status.pending_safe;
      if (status.error) report.cleanup.asyncify_cleanup_entry_error = status.error;
      if (report.result === 'pass') {
        report.result = 'fail';
        if (!failure) {
          failure = 'Asyncify native-idle state could not be proven at cleanup entry';
          report.error ??= failure;
        }
      }
      if (page && !page.isClosed()) {
        try {
          await timeout(page.close(), Math.min(1000, cleanupRemaining()),
            'Close page after unsafe Asyncify cleanup-entry check');
          report.cleanup.page_closed_asyncify_unsafe = true;
        } catch (error) {
          cleanupFailures.push({step: 'unsafe-asyncify-page-close', error: errorText(error)});
        }
      }
    },
  });
  const explicitStepStillPending = matchedSetupCfExplicitAsyncifyUnsafe ||
    (pendingMatchedSetupCfExplicitStep &&
     !matchedSetupCfNativeAccessAllowed(pendingMatchedSetupCfExplicitStep));
  let nativeCleanupSuppressed = cleanupGate.allowed !== true;
  if (nativeCleanupSuppressed && !report.cleanup.native_cleanup_suppressed_asyncify)
    report.cleanup.native_cleanup_suppressed_asyncify = true;
  if (matchedSetupCfExplicitNativeAccessClosed)
    report.cleanup.native_cleanup_suppressed_retirement = true;
  if (explicitStepStillPending) {
    report.cleanup.native_cleanup_suppressed_pending_async_step = true;
    report.cleanup.pending_async_step_schedule_index =
      pendingMatchedSetupCfExplicitStep?.scheduleIndex ?? null;
    report.cleanup.pending_async_step_state =
      matchedSetupCfExplicitAsyncifyUnsafe ? 'asyncify-not-idle-at-cleanup-entry'
        : 'unsettled-at-cleanup-entry';
  }
  if (report.result !== 'pass' && page && !page.isClosed() && !nativeCleanupSuppressed) {
    if (firstCssBrowserFinalDraw) {
      try { report.scenario.first_css_final_draw_failure = await timeout(
        readFirstCssBrowserFinalDrawObservation(), Math.min(2000, cleanupRemaining()),
        'Final CSS draw failure evidence before unload'); }
      catch (error) { cleanupFailures.push({step: 'final-draw-failure-snapshot', error: errorText(error)}); }
    }
    if (firstCssBrowserStream) {
      try { report.scenario.first_css_stream_failure = await timeout(
        readFirstCssBrowserStreamObservation(), Math.min(2000, cleanupRemaining()),
        'CSS stream failure evidence before unload'); }
      catch (error) { cleanupFailures.push({step: 'stream-failure-snapshot', error: errorText(error)}); }
    }
    if (firstSssConstructorPair) {
      try {
        report.scenario.first_sss_pair_failure_snapshot =
          await timeout(readFirstSssConstructorPairObservation(),
            Math.min(2000, cleanupRemaining()),
            'SSS pair actual entry/return and first-mismatch evidence before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-pair-failure-snapshot', error: errorText(error)});
      }
    }
    if (firstSssConsumedPadTick) {
      try {
        report.scenario.first_sss_tick_failure_snapshot =
          await timeout(readFirstSssConsumedTickObservation(),
            Math.min(2000, cleanupRemaining()),
            'SSS tick scheduler sample and first-mismatch evidence before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-tick-failure-snapshot', error: errorText(error)});
      }
    }
    if (firstSssDraw) {
      try {
        report.scenario.first_sss_draw_failure_snapshot =
          await timeout(readFirstSssDrawObservation(), Math.min(2000, cleanupRemaining()),
            'First SSS draw actual snapshots and mismatch before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-draw-failure-snapshot', error: errorText(error)});
      }
    }
    if (firstSssPrefix) {
      try {
        report.scenario.first_sss_prefix_failure_snapshot =
          await timeout(readFirstSssPrefixObservation(), Math.min(3000, cleanupRemaining()),
            'SSS prefix actual samples and exit notes before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-prefix-failure-snapshot', error: errorText(error)});
      }
    }
    if (firstSssVsSetup) {
      try {
        report.scenario.first_sss_vs_setup_failure_snapshot = await timeout(
          readFirstSssVsEntryObservation(), Math.min(3000, cleanupRemaining()),
          'VS Setup partial owners/actual/error before unload');
        if (firstSssVsSetupCf) {
          const runtime = await timeout(nativeSnapshot(), Math.min(2000, cleanupRemaining()),
            'Matched Setup C/F runtime diagnostics at failure');
          const stop = matchedSetupCfRuntimeFailure({native: runtime, pageErrors,
            consoleErrors, consoleWarnings, consoleMessages, consoleMessageCaptureOverflow});
          report.scenario.matched_setup_cf_failure_diagnostics = {
            native: runtime, stop_reason: stop,
            console_errors: [...consoleErrors], console_warnings: [...consoleWarnings],
            console_messages: [...consoleMessages],
            console_message_capture_overflow: consoleMessageCaptureOverflow,
            console_message_dropped_count: consoleMessageDropped};
          if (stop && report.scenario.matched_setup_cf)
            report.scenario.matched_setup_cf.stop_reason ??= stop;
        }
        if (firstSssVsSetupTextTail && report.scenario.first_sss_vs_setup?.text_tail_probe) {
          const unloadAttemptedAtFailureSnapshot = unloadAttempted;
          const failureTimeLog = await snapshotFirstSssTextTailLog();
          const failureTimeValidation = validateFirstSssTextTailLog(failureTimeLog);
          const textTailProbe = report.scenario.first_sss_vs_setup.text_tail_probe;
          textTailProbe.failure_time_log = failureTimeLog;
          textTailProbe.failure_time_log_validation = failureTimeValidation;
          textTailProbe.failure_time_unload_attempted = unloadAttemptedAtFailureSnapshot;
          textTailProbe.observation_status =
            failureTimeValidation.complete ? 'retained-awaiting-review' : 'OPEN';
        }
        await persistReport();
      } catch (error) { cleanupFailures.push({step: 'vs-setup-failure-snapshot', error: errorText(error)}); }
    }
    if (firstSssVsEntry) {
      try {
        report.scenario.first_sss_vs_entry_failure_snapshot = await timeout(
          readFirstSssVsEntryObservation(), Math.min(3000, cleanupRemaining()),
          'VS Entry actual/error evidence before unload');
        await persistReport();
      } catch (error) { cleanupFailures.push({step: 'vs-entry-failure-snapshot', error: errorText(error)}); }
    }
    try { report.scenario.failure_snapshot = await timeout(nativeSnapshot(),
      Math.min(2000, cleanupRemaining()), 'Failure-state native snapshot'); }
    catch (error) { cleanupFailures.push({step: 'failure-snapshot', error: errorText(error)}); }
    try { await timeout(saveScreenshot('failure-state'), Math.min(5000, cleanupRemaining()), 'Failure screenshot'); }
    catch (error) { cleanupFailures.push({step: 'failure-screenshot', error: errorText(error)}); }
  }
  if (driver && !unloadAttempted && page && !page.isClosed() && !nativeCleanupSuppressed) {
    if (matchedSetupCfExplicitArmed) {
      try {
        await retireMatchedSetupCfExplicitNative({cleanupPath: true});
      } catch (error) {
        nativeCleanupSuppressed = true;
        report.cleanup.native_cleanup_suppressed_retirement = true;
        cleanupFailures.push({step: 'explicit-native-retirement', error: errorText(error)});
      }
    } else {
      report.cleanup.unload_attempted = true;
      unloadAttempted = true;
      try {
        await assertMatchedSetupCfAsyncifyIdle('Check Asyncify before failure-path unload');
        await timeout(driver.unload(), Math.min(LIMITS.cleanupMs, cleanupRemaining()), 'Failure-path unload');
        report.cleanup.unload_completed = true;
      } catch (error) { cleanupFailures.push({step: 'unload', error: errorText(error)}); }
    }
  }
  if (!nativeCleanupSuppressed && firstSssVsSetupTextTail && unloadAttempted && page && !page.isClosed() &&
      report.scenario.first_sss_vs_setup?.text_tail_probe &&
      !report.scenario.first_sss_vs_setup.text_tail_probe.after_unload) {
    try {
      const harvested = await timeout(harvestFirstSssTextTailAfterUnload(),
        Math.min(5000, cleanupRemaining()), 'Harvest text-tail evidence after unload attempt');
      const logValidation = validateFirstSssTextTailLog(harvested.text_tail_log);
      harvested.text_tail_log_validation = logValidation;
      report.scenario.first_sss_vs_setup.text_tail_probe.after_unload = harvested;
      report.scenario.first_sss_vs_setup.text_tail_probe.log_validation_after_unload = logValidation;
      report.scenario.first_sss_vs_setup.text_tail_probe.observation_status =
        logValidation.complete ? 'retained-awaiting-review' : 'OPEN';
      report.cleanup.native_after_unload = harvested;
      report.cleanup.observation_cleared = !harvested.ordinary_observation_present;
      await persistReport();
      if (harvested.text_tail_probe?.probe_enabled !== false ||
          harvested.text_tail_probe?.membership_snapshot_active !== false ||
          harvested.text_tail_probe?.cleanup_done !== true)
        cleanupFailures.push({step: 'text-tail-native-reset',
          error: 'Native text probe or membership token was not cleared after unload attempt'});
      if (!logValidation.complete)
        cleanupFailures.push({step: 'text-tail-log-incomplete',
          error: `Text-tail observation remains OPEN: ${logValidation.errors.join('; ')}`});
    } catch (error) {
      cleanupFailures.push({step: 'text-tail-post-unload-harvest', error: errorText(error)});
    }
  }
  if (!nativeCleanupSuppressed && firstSssConstructorPair && report.cleanup.unload_completed &&
      !report.cleanup.native_after_unload && page && !page.isClosed()) {
    try {
      const afterUnload = await timeout(readFirstSssPairPostUnloadState(),
        Math.min(3000, cleanupRemaining()), 'SSS-pair failure-path post-unload ownership check');
      report.cleanup.native_after_unload = afterUnload;
      assert.equal(afterUnload.phase, 0);
      assert.equal(afterUnload.running, 0);
      assert.equal(afterUnload.observation, null);
      assert.equal(afterUnload.first_sss_constructor_pair, null);
      if (firstSssConsumedPadTick)
        assert.equal(afterUnload.first_sss_consumed_tick, null);
      if (firstSssDraw)
        assert.equal(afterUnload.first_sss_draw, null);
      if (firstSssPrefix)
        assert.equal(afterUnload.first_sss_prefix, null);
      if (firstSssVsEntry) assert.equal(afterUnload.first_sss_vs_entry, null);
      if (firstSssVsSetup) assert.equal(afterUnload.first_sss_vs_setup, null);
      assert.equal(afterUnload.error, null);
      assert.equal(afterUnload.import_enabled, true);
      report.cleanup.observation_cleared = true;
      await persistReport();
    } catch (error) {
      cleanupFailures.push({step: 'sss-pair-post-unload-check', error: errorText(error)});
    }
  }
  if (driver && !unloadAttempted && nativeCleanupSuppressed) {
    if (explicitStepStillPending) report.cleanup.unload_suppressed_pending_async_step = true;
    else report.cleanup.unload_suppressed_asyncify_idle = true;
  }
  if (context) {
    try { await timeout(context.close(), cleanupRemaining(), 'Browser context close'); report.cleanup.context_closed = true; }
    catch (error) { cleanupFailures.push({step: 'context-close', error: errorText(error)}); }
  }
  if (explicitStepStillPending) {
    report.cleanup.pending_async_step_state = matchedSetupCfNativeAccessAllowed(
      pendingMatchedSetupCfExplicitStep) ? 'settled-after-context-close' : 'unsettled-after-context-close';
    report.cleanup.pending_async_step_outcome =
      pendingMatchedSetupCfExplicitStep?.outcome ?? 'pending';
    if (pendingMatchedSetupCfExplicitStep?.error)
      report.cleanup.pending_async_step_error = pendingMatchedSetupCfExplicitStep.error;
  }
  if (driver) { try { driver.dispose(); } catch {} }
  if (browserCdp) {
    try { await timeout(browserCdp.detach(), cleanupRemaining(), 'Chrome identity session detach'); }
    catch (error) { cleanupFailures.push({step: 'chrome-identity-session-detach', error: errorText(error)}); }
  }
  if (browser) {
    try { await timeout(browser.close(), cleanupRemaining(), 'Browser connection close'); report.cleanup.browser_connection_closed = true; }
    catch (error) { cleanupFailures.push({step: 'browser-connection-close', error: errorText(error)}); }
  }
  if (browserServer) {
    try {
      await timeout(browserServer.close(), cleanupRemaining(), 'Owned Chrome process close');
    } catch (error) {
      cleanupFailures.push({step: 'browser-process-close', error: errorText(error)});
      try { await timeout(browserServer.kill(), cleanupRemaining(), 'Owned Chrome process kill'); }
      catch (killError) { cleanupFailures.push({step: 'browser-process-kill', error: errorText(killError)}); }
    }
    const proc = browserServer.process();
    report.cleanup.browser_process_terminated = proc.exitCode !== null || proc.signalCode !== null;
  }
  try { await cleanupServer(); }
  catch (error) { cleanupFailures.push({step: 'server-process-cleanup', error: errorText(error)}); }
}

try {
  await main();
} catch (error) {
  failure = errorText(error);
  report.error = failure;
  if (error.diagnostics) report.driverFailure = {step:error.step, ...error.diagnostics};
} finally {
  await cleanupOwnedProcesses();
  if (matchedSetupCfTiming)
    report.scenario.matched_setup_cf_timing = summarizeMatchedSetupCfTiming(matchedSetupCfTiming);
  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  report.scenario.console_warnings = consoleWarnings;
  report.scenario.console_messages = consoleMessages;
  report.scenario.console_message_capture_overflow = consoleMessageCaptureOverflow;
  report.scenario.console_message_dropped_count = consoleMessageDropped;
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  if (report.result !== 'pass') report.result = 'fail';
  report.execution_state = 'completed';
  report.completed_at = new Date().toISOString();
  await persistReport();
}

if (failure || report.cleanup.failures.length || !report.cleanup.browser_process_terminated ||
    !report.cleanup.server_process_terminated) {
  console.error(failure || 'C1a browser run failed an owned-process cleanup assertion');
  process.exitCode = 1;
} else {
  console.log(`C1a browser preparation checkpoint passed; report: ${inventoryOutput}`);
}
