#!/usr/bin/env node
/*
 * Headless whole-session MWRC diagnostic.
 *
 * This drives only the development replay file input. The disc import and the
 * MWRC input are separate browser uploads; no keyboard/menu intention is
 * supplied. Native source phase/cursor snapshots are retained at bounded
 * changes, and every failure writes the report before the browser closes.
 */
import fs from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {finalizeSessionCapture, validateRuntimeDataAbort, boundedCaptureOperation,
  retainFirstCaptureError} from './whole_session_capture_result.mjs';
import {parseRngDrawProbe, validateRngDrawProbeRows} from './rng_draw_probe.mjs';
import {NATURAL_PAUSE_PROTOCOL, STOPPED_SCENE_PAIR_PROTOCOL, resolveCaptureMode,
  validateNaturalPauseManifest, validateStoppedScenePairManifest, naturalPauseRuntimeUrl,
  validateNaturalPauseBrowserIdentity, validateDefaultTwoRingStatus, firstNaturalPauseStop,
  firstStoppedScenePairStop, validateStoppedScenePairBoundary, summarizeStoppedSourceInterval,
  stopSourceBeforeDiagnosticExport,
  readNaturalPauseBrowserCommandLine} from './natural_pause_diagnostic.mjs';
import {installPauseTraceCapture, readPauseTraceCapture, readPauseTraceStatus,
  readRetainedPauseDiagnostics} from '../tests/pause_trace_capture.mjs';
import {traceSettings, finalizeTrace} from './run_hitch_matrix.mjs';

const {values, tokens} = parseArgs({tokens: true, options: {
  url: {type: 'string'},
  disc: {type: 'string'},
  recipe: {type: 'string'},
  manifest: {type: 'string'},
  'runtime-data': {type: 'string'},
  out: {type: 'string'},
  mode: {type: 'string', default: 'state'},
  'stopped-scene-pair': {type: 'boolean', default: false},
  'diagnostic-manifest': {type: 'string'},
  'build-dir': {type: 'string'},
  'browser-profile': {type: 'string'},
  playwright: {type: 'string'},
  'phase-timeout': {type: 'string', default: '120000'},
  'replay-timeout': {type: 'string', default: '900000'},
  'poll-ms': {type: 'string', default: '250'},
  'replay-poll-ms': {type: 'string'},
  'stop-after-source-frames': {type: 'string'},
  'resume-timing-pauses': {type: 'boolean', default: false},
  'cpu-observations': {type: 'boolean', default: false},
  'rng-draw-probe-range': {type: 'string'},
  'rng-draw-probe-cursors': {type: 'string'},
}});

function integer(name, minimum, maximum) {
  const value = Number(values[name]);
  if (!Number.isInteger(value) || value < minimum || value > maximum)
    throw Error(`--${name} must be an integer between ${minimum} and ${maximum}`);
  return value;
}

if (!values.url || !values.disc || !values.recipe || !values.out)
  throw Error('Use --url http://127.0.0.1:PORT/runtime.html --disc PATH --recipe PATH --out NEW_DIRECTORY [--manifest PATH]');
const url = new URL(values.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html'))
  throw Error('A real HTTP development runtime.html URL is required');
const captureMode = resolveCaptureMode(values.mode, values['diagnostic-manifest']);
const diagnostic = captureMode.diagnostic;
const stoppedScenePair = values['stopped-scene-pair'];
if (stoppedScenePair && !diagnostic)
  throw Error('--stopped-scene-pair requires performance mode and a diagnostic manifest');
const diagnosticProtocol = stoppedScenePair ? STOPPED_SCENE_PAIR_PROTOCOL : NATURAL_PAUSE_PROTOCOL;
const diagnosticUrl = diagnostic ? naturalPauseRuntimeUrl(url.href) : url;
const diagnosticManifestPath = diagnostic ? path.resolve(values['diagnostic-manifest']) : null;
let diagnosticManifest = null;
let diagnosticManifestSha256 = null;
let diagnosticArtifactNames = [];
if (diagnostic) {
  if (!values['build-dir'] || !values['browser-profile'])
    throw Error('Performance capture requires --build-dir and --browser-profile');
  const mutableBounds = new Set(['phase-timeout', 'replay-timeout', 'poll-ms', 'replay-poll-ms']);
  if (tokens.some(token => mutableBounds.has(token.name)))
    throw Error('Performance capture uses the frozen manifest timeouts and polling intervals');
  if (values['runtime-data'])
    throw Error('Performance capture does not read or hash runtime-data');
  const manifestBytes = await fs.readFile(diagnosticManifestPath);
  diagnosticManifestSha256 = createHash('sha256').update(manifestBytes).digest('hex');
  try { diagnosticManifest = JSON.parse(manifestBytes); }
  catch (error) { throw Error(`Diagnostic manifest is not JSON: ${error.message}`); }
  diagnosticArtifactNames = JSON.parse(await fs.readFile(new URL('../tools/browser_build_artifacts.json', import.meta.url)));
  if (stoppedScenePair)
    validateStoppedScenePairManifest(diagnosticManifest, diagnosticArtifactNames);
  else validateNaturalPauseManifest(diagnosticManifest, diagnosticArtifactNames);
  if (new URL(diagnosticManifest.runtime_url).href !== url.href ||
      path.resolve(diagnosticManifest.inputs.disc.path) !== path.resolve(values.disc) ||
      path.resolve(diagnosticManifest.inputs.recipe.path) !== path.resolve(values.recipe) ||
      path.resolve(diagnosticManifest.build.directory) !== path.resolve(values['build-dir']) ||
      path.resolve(diagnosticManifest.browser.profile_path) !== path.resolve(values['browser-profile']))
    throw Error('Diagnostic manifest disagrees with URL, input paths, Release build directory, or fresh profile');
  try {
    await fs.lstat(path.resolve(values['browser-profile']));
    throw Error('Performance capture requires a profile path that does not already exist');
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
  }
  if (diagnosticManifest.protocol.mode !== values.mode)
    throw Error('Diagnostic manifest mode disagrees with --mode');
}
const phaseTimeoutMs = diagnostic ? diagnosticProtocol.phase_timeout_ms : integer('phase-timeout', 1000, 300000);
const replayTimeoutMs = diagnostic ? diagnosticProtocol.replay_timeout_ms : integer('replay-timeout', 1000, 1800000);
const pollMs = diagnostic ? diagnosticProtocol.phase_observation_interval_ms : integer('poll-ms', 50, 2000);
const replayPollMs = diagnostic ? diagnosticProtocol.replay_poll_interval_ms :
  values['replay-poll-ms'] === undefined ? pollMs : integer('replay-poll-ms', 1, 2000);
const requestedStopAfter = values['stop-after-source-frames'] ? integer('stop-after-source-frames',1,108000) : null;
const stopAfter = diagnostic ? diagnosticProtocol.source_cursor_limit : requestedStopAfter;
if (diagnostic && requestedStopAfter !== null && requestedStopAfter !== stopAfter)
  throw Error('Performance capture cursor bound is frozen by the diagnostic manifest');
const resumeTimingPauses = diagnostic ? false : values['resume-timing-pauses'];
if (diagnostic && values['resume-timing-pauses']) throw Error('Performance diagnosis cannot resume a timing pause');
const captureCpuObservations = diagnostic ? false : values['cpu-observations'];
const rngDrawProbe = parseRngDrawProbe({range: values['rng-draw-probe-range'],
  cursors: values['rng-draw-probe-cursors']});
if (diagnostic && (values['cpu-observations'] || rngDrawProbe))
  throw Error('Performance diagnosis does not enable CPU-prefix or RNG observers');
const runtimeDataUrl = new URL('gameplay_menu_browser.data', url).href;
const output = path.resolve(values.out);
const observationTimeoutMs = diagnostic ? diagnosticProtocol.observation_timeout_ms : Math.min(phaseTimeoutMs, 5000);
const inputPaths = [values.disc, values.recipe, values.manifest, values['runtime-data']]
  .filter(Boolean).map(value => path.resolve(value));

await fs.mkdir(output, {recursive: false});
const report = {
  schema: 'melee-web-headless-whole-session-replay-v1',
  scope: stoppedScenePair ? 'One bounded paired stopped-scene screenshot timing check; image presence does not establish visible gameplay' :
    diagnostic ? 'One bounded headless natural-pause performance diagnosis; no admission, pixel, PCM, foreground, or physical-input claim' :
    'Single headless browser MWRC v8/v9 diagnostic; no pixel, PCM, performance, or admission claim',
  result: 'fail',
  url: values.url,
  mode: captureMode.mode,
  ...(diagnostic ? {diagnostic_manifest: {path: diagnosticManifestPath, sha256: diagnosticManifestSha256}} : {}),
  ...(diagnostic ? {diagnostic_protocol: diagnosticProtocol,
    diagnostic_kind: stoppedScenePair ? 'paired_stopped_scene_screenshots' : 'natural_pause'} : {}),
  phase_timeout_ms: phaseTimeoutMs,
  replay_timeout_ms: replayTimeoutMs,
  observation_timeout_ms: observationTimeoutMs,
  poll_ms: pollMs,
  replay_poll_ms: replayPollMs,
  resume_timing_pauses: resumeTimingPauses,
  timing_pause_resumes: [],
  cpu_observations: captureCpuObservations ? 'second_match_only' : 'not_captured',
  rng_draw_probe: rngDrawProbe ? {request: rngDrawProbe.request,
    selected_cursors: rngDrawProbe.selected, complete: false} : 'not_captured',
  verified_runtime_data_aborts: [],
  phases: [],
  snapshots: [],
  first_error: null,
  first_mismatch: null,
  browser_errors: [],
  unexpected_requests: [],
};
let browser;
let page;
let driver;
let currentPhase = 'startup';
let lastSnapshotKey = '';
let lastCssStateKey = '';
const pageErrors = [];
const runtimeDataNetwork = {
  requestCount: 0,
  responseCount: 0,
  failureCount: 0,
  finishedCount: 0,
  request: null,
  responseStatus: null,
  contentLength: null,
};
const runtimeDataAbortCandidates = [];
let browserContext;
let diagnosticCdp;
let traceStarted = false;
let naturalPauseTraceSettings = null;
let diagnosticPreflightValid = false;
let browserTools = null;
let pageObservationTimedOut = false;

const write = async (name, value) => {
  await fs.writeFile(path.join(output, name), typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n');
};
const digest = async filename => {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest('hex');
};
const statInput = async filename => {
  const stat = await fs.stat(filename);
  if (!stat.isFile()) throw Error(`Input is not a regular file: ${filename}`);
  return {path: filename, bytes: stat.size, sha256: await digest(filename)};
};
function git(...args) {
  return execFileSync('git', args, {cwd: path.resolve(import.meta.dirname, '..'), encoding: 'utf8'}).trim();
}
function compareIdentityMaps(expected, actual) {
  const differences = [];
  if (!expected || typeof expected !== 'object' || Array.isArray(expected) ||
      !actual || typeof actual !== 'object' || Array.isArray(actual) ||
      Object.keys(expected).length !== Object.keys(actual).length) return {matches: false, differences: ['inventory']};
  for (const [name, identity] of Object.entries(expected)) {
    if (!Object.hasOwn(actual, name) || actual[name]?.bytes !== identity?.bytes ||
        actual[name]?.sha256 !== identity?.sha256) differences.push(name);
  }
  return {matches: differences.length === 0, differences};
}
async function sourceIdentity() {
  if (git('status', '--porcelain')) throw Error('Freeze the diagnostic harness commit and leave the checkout clean');
  return {commit: git('rev-parse', 'HEAD'), tree: git('rev-parse', 'HEAD^{tree}')};
}
async function releaseArtifactMap(buildDirectory, baseUrl) {
  const map = {};
  for (const name of diagnosticArtifactNames) {
    const filename = path.join(buildDirectory, name);
    const localStat = await fs.stat(filename);
    if (!localStat.isFile()) throw Error(`Release artifact is not a regular file: ${name}`);
    const localSha256 = await digest(filename);
    const response = await fetch(new URL(name, baseUrl), {cache: 'no-store', signal: AbortSignal.timeout(30000)});
    if (!response.ok) throw Error(`Served Release artifact ${name}: HTTP ${response.status}`);
    const hash = createHash('sha256');
    let bytes = 0;
    for await (const chunk of response.body) {
      bytes += chunk.byteLength;
      if (bytes > 1024 * 1024 * 1024) throw Error(`Served Release artifact exceeds the 1 GiB identity bound: ${name}`);
      hash.update(chunk);
    }
    const sha256 = hash.digest('hex');
    if (bytes !== localStat.size || sha256 !== localSha256)
      throw Error(`Served Release artifact differs from local build: ${name}`);
    map[name] = {bytes, sha256};
  }
  return map;
}

function firstError(kind, message, details = null) {
  retainFirstCaptureError(report, kind, message, currentPhase, details);
}

async function observePageOperation(label, operation) {
  if (pageObservationTimedOut) throw Error('Page observation already timed out; renderer evidence is unknown');
  try {
    return await boundedCaptureOperation(operation, observationTimeoutMs, label);
  } catch (error) {
    if (error.captureOperationTimeout) {
      pageObservationTimedOut = true;
      report.renderer_observation_timeout = {label, timeout_ms: observationTimeoutMs,
        phase: currentPhase, last_successful_snapshot: report.last_successful_snapshot ?? null};
      firstError('observation_timeout', error.message || error, report.last_successful_snapshot ?? null);
    }
    throw error;
  }
}

async function pauseTraceStatus(options = {}) {
  return observePageOperation('pause trace status', readPauseTraceStatus(page, options));
}

function stoppedSceneSourceCounters(status) {
  const callback = status?.latest_callback;
  return {at_ms: status?.at_ms ?? null, replay_cursor: status?.replay_cursor ?? null,
    source_running: status?.source_running ?? null, callback_rows: status?.rows ?? null,
    match_source_frame: callback?.sample_source_frame ?? null,
    callback_replay_cursor: callback?.sample_replay_cursor ?? null,
    callback_source_steps: callback?.source_steps ?? null,
    callback_source_draws: callback?.source_draws ?? null,
    callback_draw_calls: callback?.draw_calls ?? null};
}

async function captureStoppedSceneImage(name, expectedCursor, initialStatus = null) {
  const before = initialStatus ?? await pauseTraceStatus({includeLatestCallback: true});
  if (before?.source_running !== 0 || !Number.isSafeInteger(before?.replay_cursor))
    throw Error(`Refusing ${name} screenshot without a stopped source and exact cursor`);
  if (expectedCursor !== null && before.replay_cursor !== expectedCursor)
    throw Error(`${name} screenshot cursor changed before capture: ${before.replay_cursor} != ${expectedCursor}`);
  const screenshotPath = path.join(output, `${name}.png`);
  const capturedAt = before.at_ms;
  await observePageOperation(`${name} stopped-scene screenshot`,
    page.screenshot({path: screenshotPath, fullPage: false}));
  const after = await pauseTraceStatus({includeLatestCallback: true});
  if (after?.source_running !== 0 || after.replay_cursor !== before.replay_cursor)
    throw Error(`${name} screenshot interval changed source running/cursor state`);
  const screenshotStat = await fs.stat(screenshotPath);
  if (!screenshotStat.isFile() || screenshotStat.size <= 0)
    throw Error(`${name} screenshot artifact is empty`);
  return {image: {name, path: screenshotPath, bytes: screenshotStat.size,
    sha256: await digest(screenshotPath), capture_window_page_ms: {started_after: capturedAt,
      completed_before: after.at_ms ?? null}},
  counters: {before: stoppedSceneSourceCounters(before), after: stoppedSceneSourceCounters(after),
    exact_cursor: before.replay_cursor, source_running_zero_before_and_after: true,
    cursor_unchanged_during_capture: true}};
}

async function snapshot(reason = 'poll', {captureCss = !diagnostic} = {}) {
  if (!page || page.isClosed()) return null;
  let value;
  try {
    if (pageObservationTimedOut) return {reason, snapshot_error: 'Page observation already timed out'};
    value = await observePageOperation(`snapshot ${reason}`, page.evaluate(({reason, captureCss}) => {
      const module = globalThis.Module;
      const ready = globalThis.__meleeNativeRuntimeReady === true;
      const call = name => {
        if (!ready) return null;
        try { return typeof module?.[name] === 'function' ? module[name]() : null; }
        catch (error) { return {error: String(error?.message || error)}; }
      };
      const text = id => document.querySelector(id)?.textContent?.slice(-4000) || null;
      const status = document.querySelector('#status');
      const reportText = document.querySelector('#retail-replay-report')?.textContent || '';
      let replayReport = null;
      try { replayReport = reportText.trim().startsWith('{') ? JSON.parse(reportText) : null; } catch {}
      const phase = call('_melee_web_native_menu_phase');
      let css = null;
      if (captureCss && phase === 1 && module?._melee_web_css_observe_port) {
        const ids = module._malloc(16), geometry = module._malloc(32);
        if (!ids || !geometry) throw Error('CSS observation allocation failed');
        try {
          css = [];
          for (let port = 0; port < 4; ++port) {
            if (!module._melee_web_css_observe_port(port, 8, ids, geometry)) { css.push(null); continue; }
            css.push({ids: Array.from(module.HEAP32.subarray(ids >> 2, (ids >> 2) + 4)), geometry: Array.from(module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))});
          }
        } finally { module._free(ids); module._free(geometry); }
      }
      return {
        reason,
        css,
        css_setup: phase === 1 ? globalThis.menuObserveCssSetup?.() ?? null : null,
        at_ms: performance.now(),
        hidden: document.hidden,
        focused: document.hasFocus(),
        phase,
        running: call('_melee_web_native_menu_running'),
        source_cursor: call('_melee_web_native_menu_replay_cursor'),
        status: text('#status'),
        runtime_error: status?.dataset.runtimeError || null,
        log: text('#log'),
        replay_report: replayReport,
        replay_downloads: [...document.querySelectorAll('#retail-replay-downloads a')].map(link => link.download),
      };
    }, {reason, captureCss}));
  } catch (error) {
    firstError('snapshot', error.message || error);
    return {reason, snapshot_error: String(error.message || error)};
  }
  report.last_successful_snapshot = value;
  const key = JSON.stringify({phase: value.phase, running: value.running, cursor: value.source_cursor,
    error: value.runtime_error, report: value.replay_report?.result || null,
    report_pass: value.replay_report?.pass ?? null});
  // Keep a state transition that occurs between source-cursor increments too,
  // while excluding animated geometry so focused CSS traces remain bounded.
  const cssStateKey = captureCss && value.phase === 1 ? JSON.stringify({
    cursor: value.source_cursor,
    css_ids: value.css?.map(row => row?.ids ?? null) ?? null,
    cursors: value.css_setup?.cursors ?? null,
    doors: value.css_setup?.doors ?? null,
  }) : '';
  const periodicPoll = reason === 'poll' || reason === 'replay-poll' ||
    reason.startsWith('phase-poll:');
  if (key !== lastSnapshotKey || cssStateKey !== lastCssStateKey || !periodicPoll) {
    if (report.snapshots.length < 2048) report.snapshots.push(value);
  }
  lastSnapshotKey = key;
  lastCssStateKey = cssStateKey;
  if (value.runtime_error) firstError('runtime', value.runtime_error, value);
  return value;
}

async function phase(name, task, timeoutMs = phaseTimeoutMs, observationIntervalMs = pollMs) {
  currentPhase = name;
  const started = Date.now();
  const row = {name, started_at: new Date(started).toISOString(), timeout_ms: timeoutMs, result: 'fail'};
  report.phases.push(row);
  const initial = await snapshot('phase-start:' + name);
  if (initial?.snapshot_error) throw Error(initial.snapshot_error);
  const pendingPolls = new Set();
  const timer = observationIntervalMs > 0 ? setInterval(() => {
    if (pendingPolls.size || pageObservationTimedOut) return;
    const pending = snapshot('phase-poll:' + name);
    pendingPolls.add(pending);
    void pending.finally(() => pendingPolls.delete(pending));
  }, observationIntervalMs) : null;
  let timeout;
  try {
    await Promise.race([
      task(),
      new Promise((_, reject) => { timeout=setTimeout(() => reject(Error(`${name} exceeded ${timeoutMs} ms`)), timeoutMs); }),
    ]);
    row.result = 'pass';
  } catch (error) {
    firstError(name, error.message || error, report.last_successful_snapshot ?? null);
    await write('report.json', report);
    if (!pageObservationTimedOut && !pendingPolls.size) await snapshot('phase-error:' + name);
    throw error;
  } finally {
    if (timer) clearInterval(timer);
    clearTimeout(timeout);
    try {
      await boundedCaptureOperation(Promise.all(pendingPolls), observationTimeoutMs,
        'pending phase observations');
    } catch (error) {
      report.pending_observation_error = String(error.message || error);
      pageObservationTimedOut = true;
    }
    row.elapsed_ms = Date.now() - started;
    if (row.result === 'pass' && !pageObservationTimedOut) await snapshot('phase-end:' + name);
  }
}

try {
  report.inputs = {disc: await statInput(diagnostic ? path.resolve(values.disc) : values.disc),
    recipe: await statInput(diagnostic ? path.resolve(values.recipe) : values.recipe)};
  if (diagnostic) {
    const source = await sourceIdentity();
    if (source.commit !== diagnosticManifest.source.commit || source.tree !== diagnosticManifest.source.tree)
      throw Error('Current source commit/tree differs from the frozen diagnostic manifest');
    report.source_identity = source;
    for (const name of ['disc', 'recipe']) {
      const expected = diagnosticManifest.inputs[name], actual = report.inputs[name];
      if (actual.path !== expected.path || actual.bytes !== expected.bytes || actual.sha256 !== expected.sha256)
        throw Error(`Current ${name} input differs from the frozen diagnostic manifest`);
    }
    report.producer = {configuration: diagnosticManifest.build.configuration,
      target: diagnosticManifest.build.target, directory: diagnosticManifest.build.directory,
      source_commit: source.commit, source_tree: source.tree,
      runtime_producer_source: diagnosticManifest.build.producer_source ?? null,
      expected_artifacts: diagnosticManifest.build.artifacts};
    report.build_artifacts_before = await releaseArtifactMap(diagnosticManifest.build.directory, url);
    const beforeComparison = compareIdentityMaps(diagnosticManifest.build.artifacts, report.build_artifacts_before);
    report.build_artifact_preflight = {matches_manifest: beforeComparison.matches,
      differences: beforeComparison.differences};
    if (!beforeComparison.matches)
      throw Error(`Fresh local/served Release artifact map differs from manifest: ${JSON.stringify(beforeComparison.differences)}`);
  }
  if (stoppedScenePair) {
    // Reuse the independently checked frozen package identity, before replay
    // timing starts. An aborted transport still requires loaded bytes/hash.
    const name = 'gameplay_menu_browser.data';
    const identity = diagnosticManifest.build.artifacts[name];
    if (!identity) throw Error('Stopped-scene pair requires a frozen runtime data artifact');
    report.inputs.runtime_data = {path: path.join(diagnosticManifest.build.directory, name), ...identity};
  } else if (values['runtime-data'])
    report.inputs.runtime_data = await statInput(values['runtime-data']);
  if (values.manifest) {
    report.inputs.manifest = await statInput(values.manifest);
    const manifestText = await fs.readFile(values.manifest, 'utf8');
    try { report.manifest = JSON.parse(manifestText); }
    catch (error) { throw Error(`Manifest is not JSON: ${error.message}`); }
  }
  const recipeBytes = await fs.readFile(values.recipe);
  if (recipeBytes.length < 20 || recipeBytes.subarray(0, 4).toString() !== 'MWRC')
    throw Error('Recipe is not an MWRC transport');
  report.recipe_header = {
    version: recipeBytes.readUInt32BE(4), seed: recipeBytes.readUInt32BE(8),
    frames: recipeBytes.readUInt32BE(12), bytes: recipeBytes.length,
  };
  if (![8, 9].includes(report.recipe_header.version) ||
      report.recipe_header.frames < 1 || report.recipe_header.frames > 108000)
    throw Error('Whole-session replay requires a valid MWRC v8/v9 frame count');
  if (diagnostic && (report.recipe_header.version !== diagnosticManifest.inputs.recipe.header.version ||
      report.recipe_header.seed !== diagnosticManifest.inputs.recipe.header.seed ||
      report.recipe_header.frames !== diagnosticManifest.inputs.recipe.header.frames ||
      report.recipe_header.bytes !== report.inputs.recipe.bytes))
    throw Error('MWRC header differs from the frozen diagnostic recipe identity');
  if (rngDrawProbe && rngDrawProbe.selected.some(cursor => cursor >= report.recipe_header.frames))
    throw Error('RNG draw probe cursor must be inside the source recipe frame count');
  if (captureCpuObservations && report.recipe_header.version !== 9)
    throw Error('--cpu-observations is restricted to MWRC v9 second-match diagnostics');
  const cpuObservationRowLimit = report.recipe_header.frames;
  report.cpu_observation_row_limit = cpuObservationRowLimit;
  browserTools ||= await loadBrowserTools(values.playwright);
  const {chromium, browser: launchOptions, browserPath, playwrightPath} = browserTools;
  report.browser = {executable: diagnostic ? path.resolve(browserPath) : path.basename(browserPath),
    playwright: playwrightPath};
  const browserOptions = {...browserLaunchOptions(launchOptions, {timeout: phaseTimeoutMs}), headless: true,
    ...(diagnostic ? {args: [...browserLaunchOptions(launchOptions).args, '--enable-automation'],
      viewport: {width: diagnosticProtocol.viewport_width,
      height: diagnosticProtocol.viewport_height},
      deviceScaleFactor: diagnosticProtocol.device_scale_factor} : {})};
  if (diagnostic) {
    const chromeVersionOutput = execFileSync(browserPath, ['--version'], {encoding: 'utf8'}).trim();
    const chromeVersion = chromeVersionOutput.match(/\d+(?:\.\d+){2,3}/)?.[0] ?? null;
    const identity = {executable_path: path.resolve(browserPath), version: chromeVersion,
      profile_path: path.resolve(values['browser-profile']), profile_existed_before_launch: false};
    report.browser_identity_preflight = validateNaturalPauseBrowserIdentity(diagnosticManifest.browser, identity);
    if (!report.browser_identity_preflight.valid)
      throw Error(`Installed Chrome or fresh profile differs from manifest: ${JSON.stringify(report.browser_identity_preflight)}`);
    diagnosticPreflightValid = true;
    report.browser.installed_version_output = chromeVersionOutput;
    const profile = path.resolve(values['browser-profile']);
    browserContext = await chromium.launchPersistentContext(profile, browserOptions);
    browser = browserContext.browser();
    if (!browser) throw Error('Persistent Chrome context did not expose its owned browser process');
    report.browser.profile = profile;
  } else {
    browser = await chromium.launch(browserOptions);
  }
  report.browser.version = browser.version();
  if (diagnostic) {
    const identity = {executable_path: path.resolve(browserPath), version: report.browser.version,
      profile_path: path.resolve(values['browser-profile']), profile_existed_before_launch: false};
    report.browser_identity_postlaunch = validateNaturalPauseBrowserIdentity(diagnosticManifest.browser, identity);
    if (!report.browser_identity_postlaunch.valid)
      throw Error(`Launched Chrome identity differs from manifest: ${JSON.stringify(report.browser_identity_postlaunch)}`);
    const browserCdp = await browser.newBrowserCDPSession();
    try {
      report.browser.command_line = await readNaturalPauseBrowserCommandLine(
        browserCdp, path.resolve(values['browser-profile']), async inventory => {
          report.browser_process_info = inventory;
          await write('report.json', report);
        });
    } finally { await browserCdp.detach(); }
    // Make owned Chrome attribution durable before navigation or replay can
    // block the renderer and outlive the strict external owner deadline.
    await write('report.json', report);
  }
  page = diagnostic ? (browserContext.pages()[0] || await browserContext.newPage()) :
    await browser.newPage({viewport: {width: 900, height: 700}, deviceScaleFactor: 1});
  await page.addInitScript(({cpuObservationRowLimit, captureCpuObservations,
    rngDrawProbeSelection}) => {
    window.__meleeNativeRuntimeReady = false;
    const module = globalThis.Module || {};
    module.onRuntimeInitialized = () => { window.__meleeNativeRuntimeReady = true; };
    globalThis.Module = module;
    window.__cpuPrefixRows = [];
    window.__cpuItemEventRows = [];
    window.__rngDrawProbeRows = [];
    window.__rngDrawProbeSelection = rngDrawProbeSelection;
    window.__meleeRngDrawProbeCursors = rngDrawProbeSelection?.request.kind === 'cursors'
      ? rngDrawProbeSelection.selected : null;
    window.__meleeRngDrawProbeRange = rngDrawProbeSelection?.request.kind === 'range'
      ? [rngDrawProbeSelection.request.first, rngDrawProbeSelection.request.last] : null;
    window.__meleeCaptureWholeSessionCpuObservation = captureCpuObservations;
    window.__meleeSourceOwnerTrace = [];
    window.__meleeSourceAllocationTrace = [];
    window.__meleeSourceAllocationTraceTotal = 0;
    window.meleeCpuObservation = text => {
      if (window.__cpuPrefixRows.length >= cpuObservationRowLimit) throw Error('CPU prefix diagnostic exceeded source recipe frame bound');
      window.__cpuPrefixRows.push(text);
    };
    window.meleeCpuItemEvent = text => {
      if (window.__cpuItemEventRows.length >= 512) throw Error('CPU item event diagnostic exceeded bound');
      window.__cpuItemEventRows.push(text);
    };
    window.meleeRngDrawObservation = text => {
      const selection = window.__rngDrawProbeSelection;
      if (!selection) throw Error('Unexpected RNG draw observation without a selected probe');
      if (window.__rngDrawProbeRows.length >= selection.selected.length)
        throw Error('RNG draw probe exceeded its selected-cursor bound');
      const row = JSON.parse(text);
      if (!selection.selected.includes(row.source_cursor))
        throw Error('RNG draw probe emitted an unselected source cursor');
      if (window.__rngDrawProbeRows.some(existing =>
          JSON.parse(existing).source_cursor === row.source_cursor))
        throw Error('RNG draw probe emitted a duplicate source cursor');
      window.__rngDrawProbeRows.push(text);
    };
  }, {cpuObservationRowLimit, captureCpuObservations,
    rngDrawProbeSelection: rngDrawProbe});
  page.setDefaultTimeout(phaseTimeoutMs);
  page.setDefaultNavigationTimeout(phaseTimeoutMs);
  page.on('pageerror', error => { const row = {kind: 'pageerror', message: error.stack || error.message}; pageErrors.push(row); firstError(row.kind, row.message); });
  page.on('console', message => { if (message.type() === 'error') { const row = {kind: 'console', message: message.text()}; pageErrors.push(row); firstError(row.kind, row.message); } });
  page.on('request', request => {
    if (request.url() === runtimeDataUrl) {
      ++runtimeDataNetwork.requestCount;
      runtimeDataNetwork.request = {
        url: request.url(), method: request.method(), resourceType: request.resourceType(),
      };
    }
    if (request.method() !== 'GET') report.unexpected_requests.push({method: request.method(), url: request.url()});
  });
  page.on('response', response => {
    if (response.url() === runtimeDataUrl) {
      ++runtimeDataNetwork.responseCount;
      runtimeDataNetwork.responseStatus = response.status();
      runtimeDataNetwork.contentLength = Number(response.headers()['content-length'] || 0);
    }
    if (response.status() >= 400) {
      const row = {kind: 'http', status: response.status(), url: response.url()};
      pageErrors.push(row); firstError(row.kind, `${response.status()} ${response.url()}`);
    }
  });
  page.on('requestfinished', request => {
    if (request.url() === runtimeDataUrl) ++runtimeDataNetwork.finishedCount;
  });
  page.on('requestfailed', request => {
    const row = {kind: 'requestfailed', message: request.failure()?.errorText || 'Request failed', url: request.url()};
    if (request.url() === runtimeDataUrl) {
      ++runtimeDataNetwork.failureCount;
      if (row.message === 'net::ERR_ABORTED') {
        runtimeDataAbortCandidates.push({row, request: runtimeDataNetwork.request});
        return;
      }
    }
    pageErrors.push(row); firstError(row.kind, `${row.message} ${row.url}`);
  });
  await phase('http-load', async () => {
    const response = await page.goto(diagnosticUrl.href, {waitUntil: 'domcontentloaded'});
    if (response?.status() !== 200) throw Error(`runtime.html returned HTTP ${response?.status()}`);
    const headers = response.headers();
    if (headers['cross-origin-opener-policy'] !== 'same-origin' || headers['cross-origin-embedder-policy'] !== 'require-corp')
      throw Error('Runtime did not load over COOP/COEP HTTP isolation');
    if (!await page.evaluate(() => crossOriginIsolated)) throw Error('Browser page is not cross-origin isolated');
  });
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: phaseTimeoutMs});
  await phase('runtime-ready', () => driver.waitForImport());
  if (diagnostic) {
    report.pause_trace_installation = await installPauseTraceCapture(page, null);
    if (!report.pause_trace_installation.timing_hook_present ||
        !report.pause_trace_installation.sample_hook_present ||
        !report.pause_trace_installation.incident_hook_present ||
        report.pause_trace_installation.stall_schedule_supported)
      throw Error('Natural-pause capture hooks are missing or unexpectedly support an injected stall');
  }
  if (report.inputs.runtime_data) {
    report.runtime_data_load = await observePageOperation('loaded runtime data identity',
      page.evaluate(async expectedUrl => {
      const module = globalThis.Module;
      const bytes = module.FS.readFile('/initial_pipeline_cache.db');
      const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
      const sha256 = Array.from(digest, value => value.toString(16).padStart(2, '0')).join('');
      const download = module.dataFileDownloads?.[expectedUrl] || null;
      const preload = module.preloadResults?.['gameplay_menu_browser.data'] || null;
      return {
        file_bytes: bytes.byteLength, sha256,
        loaded_bytes: download?.loaded ?? null, total_bytes: download?.total ?? null,
        from_cache: preload?.fromCache ?? null,
      };
    }, runtimeDataUrl));
    const expected = report.inputs.runtime_data;
    const actual = report.runtime_data_load;
    if (actual.file_bytes !== expected.bytes || actual.sha256 !== expected.sha256 ||
        actual.loaded_bytes !== expected.bytes || actual.total_bytes !== expected.bytes)
      throw Error('Loaded Emscripten runtime data differs from the explicitly bound artifact');
  }
  await page.evaluate(() => { window.__meleeNativeRuntimeReady = true; });
  await phase('disc-import', () => driver.selectDisc(values.disc));
  await phase('asset-preparation', () => driver.waitForStart());
  if (diagnostic) {
    report.ring_status = await page.evaluate(() => window.__meleeWebStagingRingStatus ?? null);
    report.ring_status_validation = validateDefaultTwoRingStatus(report.ring_status);
    if (!report.ring_status_validation.valid)
      throw Error(`Default two-slot staging identity rejected: ${report.ring_status_validation.problems.join(', ')}`);
  }
  // The development UI import is deliberately separate from the disc import.
  // It is the only replay input supplied after the fresh runtime is prepared.
  const uiTimeout = Math.min(phaseTimeoutMs, 10000);
  await phase('diagnostics-open', async () => {
    await page.locator('summary').filter({hasText: 'Diagnostics'}).click();
  }, uiTimeout);
  await phase('replay-mode-import', async () => {
    await page.locator('#retail-replay-mode').selectOption(diagnostic ? 'performance' : 'state');
  }, uiTimeout);
  await phase('replay-file-import', async () => {
    await page.locator('#retail-replay-file').setInputFiles(values.recipe);
  }, uiTimeout);
  await phase('replay-file-ready', async () => {
    await page.waitForFunction(() => {
      const button = document.querySelector('#retail-replay-start');
      return button && !button.disabled;
    }, null, {timeout: uiTimeout});
  }, uiTimeout);
  if (diagnostic) {
    diagnosticCdp = await browserContext.newCDPSession(page);
    naturalPauseTraceSettings = traceSettings();
    await diagnosticCdp.send('Tracing.start', naturalPauseTraceSettings.trace);
    traceStarted = true;
    report.trace_configuration = naturalPauseTraceSettings.trace;
    report.trace_started_at_utc = new Date().toISOString();
  }
  await phase('whole-session-replay', async () => {
    const replayStartedAt = Date.now();
    let lastProgressWriteAt = 0;
    await page.locator('#retail-replay-start').click();
    const deadline = replayStartedAt + replayTimeoutMs;
    let last = null;
    const pairReplayState = {started: false};
    while (Date.now() < deadline) {
      last = await snapshot('replay-poll', {captureCss: !diagnostic});
      if (!last) throw Error('Browser page closed before the recorded-session replay completed');
      if (last.snapshot_error) throw Error(last.snapshot_error);
      if (Date.now() - lastProgressWriteAt >= diagnosticProtocol.progress_write_interval_ms) {
        await write('progress.json', {harness_phase:currentPhase,phase:last?.phase,cursor:last?.source_cursor,
          status:last?.status,error:last?.runtime_error,log:last?.log?.slice(-1600),
          browser_report:last?.replay_report,at_ms:last?.at_ms});
        lastProgressWriteAt = Date.now();
      }
      if (diagnostic) {
        const observed = await pauseTraceStatus({includeLatestCallback: stoppedScenePair});
        const observedTerminal = {source_running: observed.source_running, source_phase: observed.phase,
          source_cursor: observed.replay_cursor, runtime_error: observed.runtime_error,
          dialog_error: observed.dialog_error, native_message: observed.native_message,
          status_text: observed.status_text, incidents: observed.incidents,
          latest_callback: observed.latest_callback};
        const elapsedMs = Date.now() - replayStartedAt;
        const stop = stoppedScenePair
          ? firstStoppedScenePairStop(observedTerminal, elapsedMs, pairReplayState)
          : firstNaturalPauseStop(observedTerminal, elapsedMs);
        if (stop) {
          report.natural_pause_terminal = {...stop, observed_at_ms: observed.at_ms,
            elapsed_ms: elapsedMs,
            ...(stop.outcome === 'paired_screenshot_target'
              ? {trigger_cursor: observed.replay_cursor}
              : {source_cursor: observed.replay_cursor}),
            source_running: observed.source_running, phase: observed.phase};
          if (stop.outcome === 'paired_screenshot_target')
            report.stopped_scene_pair_target = {trigger_cursor: observed.replay_cursor,
              source_running: observed.source_running, latest_callback: observed.latest_callback};
          return;
        }
        if (pageErrors.length) {
          report.natural_pause_terminal = {outcome: 'browser_error', error: pageErrors[0],
            elapsed_ms: Date.now() - replayStartedAt, source_cursor: observed.replay_cursor,
            source_running: observed.source_running, phase: observed.phase};
          return;
        }
        if ((last?.replay_downloads || []).includes('retail-browser-report.json')) {
          report.browser_report = last.replay_report;
          report.natural_pause_terminal = {outcome: 'replay_completed', complete: last.replay_report?.complete === true,
            pass: last.replay_report?.pass === true, elapsed_ms: Date.now() - replayStartedAt,
            source_cursor: observed.replay_cursor, source_running: observed.source_running};
          return;
        }
      }
      if (!resumeTimingPauses && last.status?.startsWith('Paused after a timing disruption'))
        if (!diagnostic) throw Error(`Recorded-session replay paused after a timing disruption at cursor ${last.source_cursor}`);
      if (resumeTimingPauses && last?.status?.startsWith('Paused after a timing disruption') &&
          (last?.running === 0 || last?.running === false)) {
        const cursor = last.source_cursor;
        const resumed = await page.evaluate(() => {
          const pause = globalThis.Module?._melee_web_native_menu_pause;
          if (typeof pause !== 'function') return false;
          pause(0);
          return globalThis.Module?._melee_web_native_menu_running?.() === 1;
        });
        report.timing_pause_resumes.push({cursor, status:last.status, resumed});
        if (!resumed) throw Error(`Could not resume the recorded-session replay after timing pause at cursor ${cursor}`);
      }
      const links = last?.replay_downloads || [];
      if (links.includes('retail-browser-report.json')) {
        report.browser_report = last.replay_report;
        if (last.replay_report?.complete && last.replay_report?.pass) return;
        if (!report.first_mismatch && last.replay_report?.failures?.length)
          report.first_mismatch = {phase: currentPhase, failure: last.replay_report.failures[0], snapshot: last};
        throw Error(`Browser replay report failed: ${JSON.stringify(last.replay_report?.failures || [])}`);
      }
      if (last?.runtime_error) {
        // The page finalizes an aborted state trace asynchronously after its
        // source clock has stopped. Give it a bounded chance to publish the
        // partial trace/report before closing Chrome, while keeping the capture
        // result failed and preserving the original native error.
        const runtimeError = last.runtime_error;
        const settleStarted = Date.now();
        const settleDeadline = settleStarted + 2000;
        while (Date.now() < settleDeadline) {
          last = await snapshot('runtime-error-finalize');
          if ((last?.replay_downloads || []).includes('retail-browser-report.json')) {
            report.runtime_error_finalize_wait_ms = Date.now() - settleStarted;
            report.browser_report = last.replay_report;
            break;
          }
          await new Promise(resolve => setTimeout(resolve, 25));
        }
        throw Error(runtimeError);
      }
      if (!diagnostic && stopAfter && !report.deliberate_prefix_stop && last?.source_cursor >= stopAfter) {
        report.deliberate_prefix_stop = {requested_cursor: stopAfter, observed_cursor: last.source_cursor, reason: 'Bounded first-divergence diagnostic; incomplete replay expected'};
        await page.locator('#unload').click();
      }
      await new Promise(resolve => setTimeout(resolve, replayPollMs));
    }
    if (diagnostic) {
      report.natural_pause_terminal = {outcome: 'replay_timeout', elapsed_ms: Date.now() - replayStartedAt,
        source_cursor: last?.source_cursor ?? null, source_running: last?.running ?? null};
      return;
    }
    throw Error(`whole-session replay exceeded ${replayTimeoutMs} ms; last snapshot ${JSON.stringify(last)}`);
  }, diagnostic ? diagnosticProtocol.replay_phase_timeout_ms : replayTimeoutMs,
  diagnostic ? 0 : pollMs);
} catch (error) {
  firstError(currentPhase, error.message || error, report.last_successful_snapshot ?? null);
  report.failure = String(error.stack || error);
} finally {
  if (diagnostic) {
    if (pageObservationTimedOut) {
      report.natural_pause_terminal ||= {outcome: 'observation_timeout', source_cursor: null,
        source_running: null, phase: currentPhase};
      report.trace_finalization_skipped = 'Renderer observation timed out; terminal cursor unknown and no pause, trace, screenshot, or capture export was attempted.';
      process.exitCode = 1;
    } else if (page && !page.isClosed() && report.pause_trace_installation) {
      try {
        const stopped = await stopSourceBeforeDiagnosticExport({
          readStatus: () => pauseTraceStatus({includeLatestCallback: stoppedScenePair}),
          stopPlayback: async () => {
            const pause = await observePageOperation('native source pause', page.evaluate(() => {
              const module = globalThis.Module;
              if (typeof module?._melee_web_native_menu_pause !== 'function' ||
                  typeof module?._melee_web_native_menu_running !== 'function')
                throw Error('Native pause and running exports are unavailable');
              const before = module._melee_web_native_menu_running();
              if (before === 1) module._melee_web_native_menu_pause(1);
              return {before, after: module._melee_web_native_menu_running()};
            }));
            report.source_pause = pause;
            if (pause.after !== 0) throw Error('Native pause did not stop source playback');
          },
          captureImmediate: stoppedScenePair &&
            report.natural_pause_terminal?.outcome === 'paired_screenshot_target'
            ? async status => {
              report.stopped_scene_pair ||= {images: [], source_counters: {}, render_environment: null,
                visible_gameplay_observed: 'not_assessed', capture_order: []};
              const boundary = validateStoppedScenePairBoundary(report.natural_pause_terminal, status);
              report.stopped_scene_pair.stop_boundary = boundary;
              report.stopped_scene_pair.capture_order.push('source_stop_boundary_validated');
              if (!boundary.valid) {
                report.stopped_scene_pair.capture_skipped =
                  `Stopped source boundary failed validation: ${boundary.problems.join(', ')}`;
                process.exitCode = 1;
                return {valid: false, boundary};
              }
              const immediate = await captureStoppedSceneImage('stopped-scene-immediate',
                boundary.stopped_cursor, status);
              report.stopped_scene_pair.capture_order.push('source_running_zero_verified',
                'immediate_screenshot_complete_before_trace_finalization');
              report.stopped_scene_pair.images.push(immediate.image);
              report.stopped_scene_pair.source_counters.immediate_before = immediate.counters.before;
              report.stopped_scene_pair.source_counters.immediate_after = immediate.counters.after;
              return {valid: true, boundary, image: immediate};
            } : undefined,
          finalizeTrace: async () => {
            if (!traceStarted || !diagnosticCdp || !naturalPauseTraceSettings)
              return {paths: [], complete: false, reusable: false, error: 'Trace was not started'};
            const value = await boundedCaptureOperation(
              finalizeTrace(diagnosticCdp, output, naturalPauseTraceSettings),
              diagnosticProtocol.phase_timeout_ms, 'trace finalization');
            traceStarted = false;
            report.trace = value;
            if (stoppedScenePair && report.stopped_scene_pair)
              report.stopped_scene_pair.capture_order.push('trace_finalization_returned');
            if (!value.complete) process.exitCode = 1;
            return value;
          },
          readEvidence: async () => {
            const pairEligible = stoppedScenePair &&
              report.natural_pause_terminal?.outcome === 'paired_screenshot_target' &&
              report.stopped_scene_pair?.stop_boundary?.valid === true &&
              report.stopped_scene_pair?.images?.length === 1;
            let stoppedVisual = null;
            if (!stoppedScenePair || pairEligible) {
              stoppedVisual = await observePageOperation('stopped scene and GPU status', page.evaluate(async () => {
                const canvas = document.querySelector('#canvas');
                const rect = canvas?.getBoundingClientRect();
                let adapterAvailable = false;
                if (navigator.gpu && typeof navigator.gpu.requestAdapter === 'function') {
                  try { adapterAvailable = (await navigator.gpu.requestAdapter()) !== null; } catch {}
                }
                let contextAvailable = false;
                try { contextAvailable = canvas?.getContext('webgpu') !== null; } catch {}
                return {viewport: {inner_width: innerWidth, inner_height: innerHeight,
                    visual_width: visualViewport?.width ?? null, visual_height: visualViewport?.height ?? null,
                    device_pixel_ratio: devicePixelRatio},
                  canvas: canvas ? {buffer_width: canvas.width, buffer_height: canvas.height,
                    css_width: rect.width, css_height: rect.height} : null,
                  gpu_status: {secure_context: isSecureContext, cross_origin_isolated: crossOriginIsolated,
                    adapter_available: adapterAvailable,
                    preferred_canvas_format: navigator.gpu?.getPreferredCanvasFormat?.() ?? null,
                    webgpu_context_available: contextAvailable}};
              }));
              if (stoppedScenePair) {
                report.stopped_scene_pair.capture_order.push('gpu_status_queried');
                const delayed = await captureStoppedSceneImage('stopped-scene-after-export',
                  report.stopped_scene_pair.stop_boundary.stopped_cursor);
                report.stopped_scene_pair.images.push(delayed.image);
                report.stopped_scene_pair.capture_order.push('delayed_screenshot_complete_after_trace_and_gpu_query');
                report.stopped_scene_pair.source_counters.delayed_before = delayed.counters.before;
                report.stopped_scene_pair.source_counters.delayed_after = delayed.counters.after;
                report.stopped_scene_pair.render_environment = stoppedVisual;
              } else {
                const screenshotPath = path.join(output, 'stopped-scene.png');
                await observePageOperation('stopped scene screenshot',
                  page.screenshot({path: screenshotPath, fullPage: false}));
                const screenshotStat = await fs.stat(screenshotPath);
                report.stopped_scene = {...stoppedVisual, screenshot: {path: screenshotPath,
                  bytes: screenshotStat.size, sha256: await digest(screenshotPath)}};
              }
            } else {
              report.stopped_scene_pair ||= {images: [], source_counters: {}, render_environment: null,
                visible_gameplay_observed: 'not_assessed', capture_order: []};
              report.stopped_scene_pair.capture_skipped =
                report.stopped_scene_pair.capture_skipped ||
                'Validated stopped-source boundary was not reached; no paired images were produced.';
            }
            report.runtime_incident_recorder = await observePageOperation('retained incident recorder',
              readRetainedPauseDiagnostics(page,
                ['timing_pause', 'runtime_error', 'preparation_error'].includes(report.natural_pause_terminal?.outcome)));
            report.staging_incident_summaries = (report.runtime_incident_recorder.retained_records || [])
              .map(record => ({id: record.id, reason: record.reason, timestamp: record.timestamp,
                staging: record.staging ?? null}));
            report.capture_status_after_stop = await pauseTraceStatus({readNative: false,
              includeLatestCallback: stoppedScenePair});
            report.capture = await observePageOperation('callback capture export',
              readPauseTraceCapture(page, report.natural_pause_terminal?.outcome ?? 'capture_failure'));
            if (stoppedScenePair && pairEligible) {
              const counters = report.stopped_scene_pair.source_counters;
              const firstRow = counters.immediate_before.callback_rows;
              const endRow = counters.delayed_after.callback_rows;
              const interval = summarizeStoppedSourceInterval(report.capture, firstRow, endRow);
              const snapshots = [counters.immediate_before, counters.immediate_after,
                counters.delayed_before, counters.delayed_after];
              const targetCursor = report.stopped_scene_pair.stop_boundary.stopped_cursor;
              const sourceStopped = snapshots.every(row => row.source_running === 0);
              const cursorUnchanged = snapshots.every(row => row.replay_cursor === targetCursor);
              report.stopped_scene_pair.source_counters.interval = {
                trigger_cursor: report.stopped_scene_pair.stop_boundary.trigger_cursor,
                stopped_cursor: targetCursor,
                exact_cursor_before_and_after_both_images: cursorUnchanged,
                source_running_zero_at_all_image_boundaries: sourceStopped,
                source_cursor_start: counters.immediate_before.replay_cursor,
                source_cursor_end: counters.delayed_before.replay_cursor,
                callback_interval: interval,
                no_source_advance_verified: cursorUnchanged && sourceStopped &&
                  interval.all_observed_callbacks_zero_source_steps_and_draws,
              };
              report.stopped_scene_pair.artifact_set_complete = report.stopped_scene_pair.images.length === 2 &&
                report.stopped_scene_pair.images.every(image => Number.isSafeInteger(image.bytes) &&
                  image.bytes > 0 && /^[0-9a-f]{64}$/.test(image.sha256 || ''));
            }
            return {runtime_incident_recorder: report.runtime_incident_recorder,
              capture_rows: report.capture.rows, capture_errors: report.capture.errors,
              capture_dropped: report.capture.dropped};
          },
          cleanupAfterEvidence: async () => {
            const unloaded = await observePageOperation('native session unload', page.evaluate(() => {
              const module = globalThis.Module;
              if (typeof module?._melee_web_native_menu_unload !== 'function')
                throw Error('Native unload export is unavailable after evidence export');
              const result = module._melee_web_native_menu_unload();
              return {result, phase: module._melee_web_native_menu_phase?.() ?? null,
                source_running: module._melee_web_native_menu_running?.() ?? null};
            }));
            if (unloaded.result !== 1 || unloaded.phase !== 0 || unloaded.source_running !== 0)
              throw Error(`Native unload did not verify cleanly: ${JSON.stringify(unloaded)}`);
            if (diagnosticCdp) {
              await boundedCaptureOperation(diagnosticCdp.detach(), observationTimeoutMs, 'CDP detach');
              diagnosticCdp = null;
            }
            report.cleanup = {native_unload: unloaded, cdp_detached: true};
            return unloaded;
          },
        });
        report.source_stop = {before: stopped.before, after: stopped.stopped,
          source_stopped_before_trace_stream_and_capture_export: true,
          cleanup_after_export: stopped.cleanup};
        report.natural_pause_observation_limits = {
          source_guard_value: 'integer triggering_value and threshold from the actual source incident hook',
          fractional_pre_guard_demand: 'not retained by the source hook; not inferred across reset or early-break boundaries',
          staging: 'aggregate occupancy and registration/callback counters plus peak callback wall duration; no one-to-one completion IDs',
          callback_history: 'full preceding callback rows from the bounded capture table',
        };
        report.final_snapshot = await snapshot('finally-after-source-stop');
      } catch (error) {
        report.source_stop_error = String(error?.stack || error);
        process.exitCode = 1;
      }
      if (pageObservationTimedOut) {
        report.natural_pause_terminal ||= {outcome: 'observation_timeout', source_cursor: null,
          source_running: null, phase: currentPhase};
        report.trace_finalization_skipped ||= 'Renderer observation timed out; remaining diagnostic exports were skipped.';
        process.exitCode = 1;
      }
    } else if (traceStarted) {
      // Without a readable stopped page there is no safe large trace export.
      report.trace_finalization_skipped = 'Source-stop state could not be verified; trace bytes were not streamed.';
      process.exitCode = 1;
    }
  } else if (page && !page.isClosed() && !pageObservationTimedOut) {
    const final = await snapshot('finally');
    report.final_snapshot = final;
    if (pageObservationTimedOut) {
      report.page_exports_skipped = 'Renderer observation timed out; last successful snapshot retained and page exports were skipped.';
    } else {
    try {
      const artifacts = await page.locator('#retail-replay-downloads a').evaluateAll(async links => {
        const retained = [];
        for (const link of links) {
          if (!['retail-port.jsonl', 'retail-timer.jsonl', 'retail-browser-report.json'].includes(link.download) || !link.href.startsWith('blob:')) continue;
          const response = await fetch(link.href);
          if (!response.ok) throw Error(`Artifact ${link.download} returned HTTP ${response.status}`);
          retained.push({name: link.download, text: await response.text()});
        }
        return retained;
      });
      report.saved_downloads = [];
      for (const artifact of artifacts) {
        if (report.saved_downloads.some(row => row.name === artifact.name))
          throw Error(`Duplicate exported artifact: ${artifact.name}`);
        await write(artifact.name, artifact.text);
        report.saved_downloads.push({name: artifact.name, bytes: Buffer.byteLength(artifact.text), sha256: createHash('sha256').update(artifact.text).digest('hex')});
      }
    } catch (error) { report.download_error = String(error?.message || error); }
    try { const rows = await page.evaluate(() => window.__cpuPrefixRows || []); if(rows.length) await write('cpu-prefix.jsonl',rows.join('\n')+'\n'); } catch(error) { report.cpu_download_error = String(error); }
    try { const rows = await page.evaluate(() => window.__cpuItemEventRows || []); await write('cpu-item-events.jsonl',rows.length ? rows.join('\n')+'\n' : ''); report.cpu_item_event_count = rows.length; } catch(error) { report.cpu_item_event_error = String(error); }
    if (rngDrawProbe) {
      try {
        const rows = await page.evaluate(() => window.__rngDrawProbeRows || []);
        const validation = validateRngDrawProbeRows(rows, rngDrawProbe, {
          observedCursor: report.final_snapshot?.source_cursor,
          deliberateStop: report.deliberate_prefix_stop,
        });
        const text = validation.rows.length ? validation.rows.join('\n') + '\n' : '';
        await write('rng-draw-probe.jsonl', text);
        const artifact = {name: 'rng-draw-probe.jsonl', bytes: Buffer.byteLength(text),
          sha256: createHash('sha256').update(text).digest('hex')};
        report.saved_downloads ||= [];
        report.saved_downloads.push(artifact);
        report.rng_draw_probe = {...report.rng_draw_probe, complete: validation.complete,
          captured_cursors: validation.rows.length ? validation.rows.map(row => JSON.parse(row).source_cursor) : [],
          missing_cursors: validation.missing_cursors, artifact};
      } catch (error) {
        report.rng_draw_probe_error = String(error?.message || error);
      }
    }
    try { await write('source-owner-trace.json', await page.evaluate(() => window.__meleeSourceOwnerTrace || [])); } catch(error) { report.owner_trace_error = String(error); }
    try { await write('source-main-allocation-trace.json', {total: await page.evaluate(() => window.__meleeSourceAllocationTraceTotal || 0), events: await page.evaluate(() => window.__meleeSourceAllocationTrace || [])}); } catch(error) { report.source_allocation_trace_error = String(error); }
    try { await write('page.txt', await page.locator('body').innerText()); } catch (error) { report.page_dump_error = String(error); }
    try { await page.screenshot({path: path.join(output, 'final.png'), fullPage: false}); } catch (error) { report.screenshot_error = String(error); }
    }
  }
  try { driver?.dispose(); } catch (error) { report.close_error = String(error); }
  // Quiesce browser callbacks before deciding whether diagnostics permit success.
  try {
    if (diagnostic && browserContext) {
      await boundedCaptureOperation(browserContext.close(), observationTimeoutMs, 'owned browser context close');
      report.cleanup ||= {};
      report.cleanup.browser_context_closed = true;
    }
    else if (browser) await boundedCaptureOperation(browser.close(), observationTimeoutMs, 'owned browser close');
  } catch (error) { report.close_error = String(error); }
  try {
    if (diagnosticCdp)
      await boundedCaptureOperation(diagnosticCdp.detach(), observationTimeoutMs, 'CDP detach');
  } catch (error) { report.cdp_close_error = String(error); }
  for (const candidate of runtimeDataAbortCandidates) {
    const expected = report.inputs?.runtime_data;
    const actual = report.runtime_data_load;
    const evidence = {
      requestCount: runtimeDataNetwork.requestCount,
      responseCount: runtimeDataNetwork.responseCount,
      failureCount: runtimeDataNetwork.failureCount,
      finishedCount: runtimeDataNetwork.finishedCount,
      url: candidate.request?.url,
      expectedUrl: runtimeDataUrl,
      method: candidate.request?.method,
      resourceType: candidate.request?.resourceType,
      errorText: candidate.row.message,
      responseStatus: runtimeDataNetwork.responseStatus,
      contentLength: runtimeDataNetwork.contentLength,
      loadedBytes: actual?.loaded_bytes ?? null,
      totalBytes: actual?.total_bytes ?? null,
      fileBytes: actual?.file_bytes ?? null,
      expectedBytes: expected?.bytes ?? null,
      expectedSha256: expected?.sha256 ?? null,
      actualSha256: actual?.sha256 ?? null,
      fromCache: actual?.from_cache ?? null,
    };
    if (validateRuntimeDataAbort(evidence)) {
      report.verified_runtime_data_aborts.push(evidence);
    } else {
      pageErrors.push(candidate.row);
      firstError('requestfailed', `${candidate.row.message} ${candidate.row.url}`, evidence);
    }
  }
  report.browser_errors = pageErrors.slice(0, 256);
  if (diagnostic) {
    if (diagnosticManifest) {
      try {
        const afterSource = await sourceIdentity();
        const afterArtifacts = await releaseArtifactMap(diagnosticManifest.build.directory, url);
        const artifactComparison = compareIdentityMaps(diagnosticManifest.build.artifacts, afterArtifacts);
        const manifestAfter = await fs.readFile(diagnosticManifestPath);
        report.producer_postflight = {source_unchanged: JSON.stringify(afterSource) === JSON.stringify(report.source_identity),
          build_artifacts_match_manifest: artifactComparison.matches,
          build_artifact_differences: artifactComparison.differences,
          manifest_unchanged: createHash('sha256').update(manifestAfter).digest('hex') === diagnosticManifestSha256,
          build_artifacts_after: afterArtifacts};
        if (!report.producer_postflight.source_unchanged || !artifactComparison.matches ||
            !report.producer_postflight.manifest_unchanged) process.exitCode = 1;
      } catch (error) {
          report.producer_postflight_error = String(error?.stack || error);
          process.exitCode = 1;
      }
    }
    const terminal = report.natural_pause_terminal;
    const validTerminalOutcomes = new Set(['timing_pause', 'runtime_error', 'cursor_limit',
      'replay_timeout', 'replay_completed', 'paired_screenshot_target',
      'positive_match_frame_not_observed', 'pair_replay_timeout',
      'source_stopped_before_screenshot_target']);
    const preflightValid = diagnosticPreflightValid &&
      report.browser_identity_preflight?.valid === true && report.browser_identity_postlaunch?.valid === true &&
      report.ring_status_validation?.valid === true;
    const postflightValid = report.producer_postflight?.source_unchanged === true &&
      report.producer_postflight?.build_artifacts_match_manifest === true &&
      report.producer_postflight?.manifest_unchanged === true;
    const cleanupValid = report.cleanup?.native_unload?.result === 1 &&
      report.cleanup.native_unload.phase === 0 && report.cleanup.native_unload.source_running === 0 &&
      report.cleanup.cdp_detached === true && report.cleanup.browser_context_closed === true &&
      report.source_stop?.source_stopped_before_trace_stream_and_capture_export === true &&
      !report.source_stop_error && !report.close_error && !report.cdp_close_error;
    const stoppedSceneArtifactsComplete = stoppedScenePair
      ? report.stopped_scene_pair?.artifact_set_complete === true
      : Number.isSafeInteger(report.stopped_scene?.screenshot?.bytes) &&
      report.stopped_scene.screenshot.bytes > 0 &&
      Number.isFinite(report.stopped_scene?.viewport?.device_pixel_ratio) &&
      !!report.stopped_scene?.gpu_status;
    const stoppedScenePairValid = !stoppedScenePair || (
      terminal?.outcome === 'paired_screenshot_target' &&
      report.stopped_scene_pair?.stop_boundary?.valid === true &&
      report.stopped_scene_pair?.source_counters?.interval?.no_source_advance_verified === true);
    const captureValid = preflightValid && postflightValid && cleanupValid &&
      stoppedSceneArtifactsComplete && stoppedScenePairValid &&
      validTerminalOutcomes.has(terminal?.outcome) && report.trace?.complete === true &&
      report.capture?.status === 'captured' && report.capture.rows > 0 &&
      report.capture.errors === 0 && report.capture.dropped === 0 && report.capture.incident_overflow === 0;
    report.diagnostic_capture_valid = captureValid;
    report.diagnostic_capture_validity_scope =
      'Protocol and artifact completeness only; no image pixel or visible-gameplay interpretation.';
    report.diagnostic_validity = {preflight: preflightValid, ring_default_two: report.ring_status_validation?.valid === true,
      explicit_terminal_outcome: validTerminalOutcomes.has(terminal?.outcome), trace_complete: report.trace?.complete === true,
      callback_capture_complete: report.capture?.status === 'captured' && report.capture?.rows > 0 &&
        report.capture?.errors === 0 && report.capture?.dropped === 0 && report.capture?.incident_overflow === 0,
      stopped_scene_artifacts_complete: stoppedSceneArtifactsComplete,
      visible_gameplay_observed: 'not_assessed',
      ...(stoppedScenePair ? {paired_source_interval_zero_steps_and_draws:
        report.stopped_scene_pair?.source_counters?.interval?.no_source_advance_verified === true,
      stopped_scene_pair_stop_boundary_valid: report.stopped_scene_pair?.stop_boundary?.valid === true} : {}),
      producer_postflight: postflightValid, cleanup: cleanupValid,
      clean_prefix: captureValid && ['cursor_limit', 'replay_timeout', 'paired_screenshot_target'].includes(terminal?.outcome),
      terminal_failure: terminal?.outcome === 'runtime_error'};
    report.result = captureValid ? 'captured' : 'incomplete';
  } else finalizeSessionCapture(report);
  await write('report.json', report);
  if (report.failure) await write('failure.txt', report.failure + '\n');
}

if (diagnostic) {
  if (report.result !== 'captured' || process.exitCode) process.exitCode = 1;
  else console.log(`captured: ${report.natural_pause_terminal?.outcome || 'unknown'}; diagnosis only`);
} else if (report.result !== 'pass') process.exitCode = 1;
else console.log(`pass: ${report.recipe_header.frames} frames; source cursor and phase diagnostics retained`);
