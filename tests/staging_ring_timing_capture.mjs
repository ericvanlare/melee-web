#!/usr/bin/env node
/** One fresh conditioned staging-ring timing cell; no hashes or automatic resume. */
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {installPauseTraceCapture, readPauseTraceCapture, readPauseTraceStatus,
  readRetainedPauseDiagnostics} from './pause_trace_capture.mjs';
import {createHeavyGpuPage, setHeavyGpu, readHeavyGpu} from './pause_trace_perturb.mjs';

const ROOT = path.resolve(import.meta.dirname, '..');
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, recipe: {type: 'string'}, out: {type: 'string'},
  'build-dir': {type: 'string'}, slots: {type: 'string'}, playwright: {type: 'string'},
  'phase-timeout-ms': {type: 'string', default: '180000'},
  'capture-timeout-ms': {type: 'string', default: '120000'},
  'stall-ms': {type: 'string'},
}});

function integer(name, min, max) {
  const value = Number(values[name]);
  if (!Number.isInteger(value) || value < min || value > max) throw Error(`--${name} must be ${min}..${max}`);
  return value;
}
if (!values.url || !values.disc || !values.recipe || !values.out || !values['build-dir'] || !values.slots) {
  throw Error('Use --url LOOPBACK_RUNTIME_URL --disc OWNED_CISO --recipe SYNTHETIC_MWRC_V4 --out NEW_RUN_DIRECTORY --build-dir BUILT_RUNTIME_DIRECTORY --slots 2|4 [--playwright PACKAGE_DIR]');
}
const stallMs = Number(values['stall-ms']);
if (![35, 100].includes(stallMs)) throw Error('--stall-ms must be 35 or 100');
const slots = Number(values.slots);
if (![2, 4].includes(slots)) throw Error('--slots must be 2 or 4');
const phaseTimeoutMs = integer('phase-timeout-ms', 1000, 600000);
const captureTimeoutMs = integer('capture-timeout-ms', 1000, 1200000);
const url = new URL(values.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html') ||
    !['localhost', '127.0.0.1', '::1', '[::1]'].includes(url.hostname)) {
  throw Error('A real loopback HTTP runtime.html URL is required for the opt-in experiment');
}
url.searchParams.set('melee-web-staging-slots', String(slots));
url.searchParams.delete('melee-web-staging-byte-hash');
url.searchParams.set('melee-web-staging-diagnostics', '1');
const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});

const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function shaFile(filename) {
  const hash = crypto.createHash('sha256');
  const stream = (await import('node:fs')).createReadStream(filename);
  for await (const chunk of stream) hash.update(chunk);
  return hash.digest('hex');
}
async function inputIdentity(filename, includePath = false) {
  const stat = await fs.stat(filename);
  if (!stat.isFile()) throw Error('Experiment input is not a regular file');
  return {bytes: stat.size, sha256: await shaFile(filename), ...(includePath ? {path: path.resolve(filename)} : {})};
}
async function buildArtifacts(buildDirectory) {
  const names = JSON.parse(await fs.readFile(path.join(ROOT, 'tools/browser_build_artifacts.json'), 'utf8'));
  const artifacts = {};
  for (const name of names) {
    const local = path.join(buildDirectory, name);
    const stat = await fs.stat(local);
    if (!stat.isFile()) throw Error(`Built browser artifact is not a file: ${name}`);
    const response = await fetch(new URL(name, url), {cache: 'no-store', signal: AbortSignal.timeout(30000)});
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
    if (servedSha !== localSha || bytes !== stat.size) throw Error(`Served artifact differs from build directory: ${name}`);
    artifacts[name] = {bytes, sha256: servedSha};
  }
  return artifacts;
}
async function sourceIdentity() {
  const git = (...args) => execFileSync('git', args, {cwd: ROOT, encoding: 'utf8'}).trim();
  return {
    commit: git('rev-parse', 'HEAD'),
    tree: git('rev-parse', 'HEAD^{tree}'),
    tracked_diff_sha256: sha(execFileSync('git', ['diff', '--binary', 'HEAD'], {cwd: ROOT})),
    aurora_patch_sha256: await shaFile(path.join(ROOT, 'patches/aurora-browser.patch')),
  };
}
function recipeHeader(bytes) {
  if (bytes.length < 20 || bytes.toString('ascii', 0, 4) !== 'MWRC') throw Error('Recipe is not MWRC');
  const header = {version: bytes.readUInt32BE(4), seed_hex: bytes.readUInt32BE(8).toString(16).padStart(8, '0'),
    frames: bytes.readUInt32BE(12), profile_characters_hex: bytes.readUInt16BE(16).toString(16).padStart(4, '0'),
    profile_stages_hex: bytes.readUInt16BE(18).toString(16).padStart(4, '0'), bytes: bytes.length};
  if (header.version !== 4 || header.frames !== 1800 || header.profile_characters_hex !== '07ff' ||
      header.profile_stages_hex !== '01c0' || bytes.length !== 20 + 0x138 + 822 + header.frames * 44) {
    throw Error('Recipe header/envelope is not the declared 1800-frame synthetic MWRC v4 four-player profile');
  }
  return header;
}
async function memorySnapshot(page) {
  return page.evaluate(() => {
    let native = null;
    try {
      const module = globalThis.Module;
      const pointer = module?._melee_web_native_menu_memory?.();
      if (pointer) native = JSON.parse(module.UTF8ToString(pointer));
    } catch (error) { native = {error: String(error)}; }
    const js = performance.memory ? {used_bytes: performance.memory.usedJSHeapSize,
      total_bytes: performance.memory.totalJSHeapSize, limit_bytes: performance.memory.jsHeapSizeLimit} : null;
    return {native, js_heap: js};
  });
}
async function graphicsCheck(page) {
  return page.evaluate(async () => {
    const canvas = document.querySelector('#canvas');
    if (!canvas) return {available: false, reason: 'runtime canvas missing'};
    const rect = canvas.getBoundingClientRect();
    let adapterAvailable = false;
    if (navigator.gpu && typeof navigator.gpu.requestAdapter === 'function') {
      try { adapterAvailable = (await navigator.gpu.requestAdapter()) !== null; } catch {}
    }
    let contextAvailable = false;
    try { contextAvailable = canvas.getContext('webgpu') !== null; } catch {}
    const result = {
      secure_context: isSecureContext,
      cross_origin_isolated: crossOriginIsolated,
      adapter_available: adapterAvailable,
      preferred_canvas_format: navigator.gpu?.getPreferredCanvasFormat?.() ?? null,
      webgpu_context_available: contextAvailable,
      canvas: {buffer_width: canvas.width, buffer_height: canvas.height,
        css_width: rect.width, css_height: rect.height},
    };
    result.available = result.secure_context && result.cross_origin_isolated &&
      result.adapter_available && result.preferred_canvas_format !== null &&
      result.webgpu_context_available && result.canvas.buffer_width === 640 &&
      result.canvas.buffer_height === 480 && result.canvas.css_width > 0 && result.canvas.css_height > 0;
    return result;
  });
}
async function takeScreenshot(page, output, name) {
  const filename = `${name}.png`;
  const bytes = await page.screenshot({path: path.join(output, filename), fullPage: true, timeout: 10000});
  return {file: filename, bytes: bytes.length, sha256: sha(bytes)};
}
async function applicationFailure(page) {
  return page.evaluate(() => {
    const status = document.querySelector('#status');
    const dialog = document.querySelector('#error-dialog[open]');
    const runtimeError = status?.dataset.runtimeError || null;
    const dialogError = dialog ? document.querySelector('#error')?.textContent?.trim() || 'Application error' : null;
    const replayReport = window.lastRetailReplayReport ?? null;
    return {runtimeError, dialogError, replayReport};
  });
}
const report = {
  schema: 'melee-web-staging-ring-conditioned-timing-v1',
  scope: 'synthetic MWRC v4 conditioned timing component; headless callback-observer stall under a separate GPU-load page; no original-game, foreground, pixel or timing acceptance claim',
  result: 'fail',
  requested_frame_slots: slots,
  stall_ms: stallMs,
  target_replay_cursor: 600,
  original_match_counter: 'retained at actual insertion; not an input ordinal',
  instrumentation: 'pause timing table and staging diagnostics enabled uniformly in every cell; overhead not isolated',
  gpu_windows: [],
  browser_mode: 'headless installed Chrome, muted host output; no submitted-byte hashing and no automatic timing resume',
  source: null,
  build_artifacts_before: null,
  build_artifacts_after: null,
  inputs: null,
  recipe_header: null,
  ring_selection: null,
  ring_status: null,
  memory_before: null,
  memory_after: null,
  graphics_check: null,
  screenshots: {},
  live_gpu_memory: 'not exposed by the browser WebGPU API; report staging-buffer reservation separately',
  capture_start: null,
  capture: null,
  browser_errors: [],
  failure: null,
};
let browser, page, driver, heavy;
let traceInstalled = false;
const pageError = error => report.browser_errors.push({kind: 'pageerror', message: String(error?.message || error)});
const consoleError = message => { if (message.type() === 'error') report.browser_errors.push({kind: 'console', message: message.text()}); };

try {
  const recipeBytes = await fs.readFile(values.recipe);
  report.recipe_header = recipeHeader(recipeBytes);
  const recipeSidecar = JSON.parse(await fs.readFile(values.recipe + '.json', 'utf8'));
  if (recipeSidecar.fixture_sha256 !== sha(recipeBytes) || recipeSidecar.recipe_format !== 'MWRC v4' ||
      recipeSidecar.scope !== 'synthetic initial-context diagnostic; no original-match identity claim') {
    throw Error('Recipe sidecar does not bind the source-backed synthetic MWRC v4 input');
  }
  report.inputs = {disc: await inputIdentity(values.disc), recipe: {bytes: recipeBytes.length, sha256: sha(recipeBytes)},
    recipe_sidecar_sha256: sha(await fs.readFile(values.recipe + '.json'))};
  report.source = await sourceIdentity();
  const buildDirectory = path.resolve(values['build-dir']);
  report.build_artifacts_before = await buildArtifacts(buildDirectory);
  const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(values.playwright);
  browser = await chromium.launch({...browserLaunchOptions(launchOptions, {timeout: phaseTimeoutMs}), headless: true});
  report.browser = {executable: path.basename(browserPath), playwright: playwrightPath, version: browser.version()};
  report.machine = {hostname: os.hostname(), platform: process.platform, arch: process.arch};
  page = await browser.newPage({viewport: {width: 900, height: 700}, deviceScaleFactor: 1});
  page.setDefaultTimeout(phaseTimeoutMs);
  page.on('pageerror', pageError);
  page.on('console', consoleError);
  const response = await page.goto(url.href, {waitUntil: 'domcontentloaded', timeout: phaseTimeoutMs});
  if (response?.status() !== 200 || response.headers()['cross-origin-opener-policy'] !== 'same-origin' ||
      response.headers()['cross-origin-embedder-policy'] !== 'require-corp') {
    throw Error('Runtime did not load over isolated loopback HTTP');
  }
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: phaseTimeoutMs});
  await driver.waitForImport();
  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  await page.waitForFunction(() => !!window.__meleeWebStagingRingStatus, null, {timeout: phaseTimeoutMs});
  report.ring_status = await page.evaluate(() => window.__meleeWebStagingRingStatus);
  report.ring_selection = report.ring_status?.selection ?? null;
  if (report.ring_status?.frame_slots !== slots || report.ring_status?.staging_buffers !== slots) {
    throw Error(`Requested ${slots} staging slots but runtime selected ${report.ring_status?.frame_slots}; ${JSON.stringify(report.ring_selection)}`);
  }
  if (report.ring_status?.selection?.byte_hash_enabled !== false ||
      report.ring_status?.selection?.staging_diagnostics_enabled !== true) {
    throw Error('Conditioned timing requires hashing disabled and staging diagnostics enabled');
  }
  report.memory_before = await memorySnapshot(page);
  report.graphics_check = await graphicsCheck(page);
  if (!report.graphics_check.available) throw Error(`Headless WebGPU/canvas check failed: ${JSON.stringify(report.graphics_check)}`);
  heavy = await createHeavyGpuPage(browser);
  report.gpu_config = heavy.config;
  report.gpu_ready = heavy.ready;
  report.capture_start = await installPauseTraceCapture(page,
    {sourceFrame: null, replayCursor: 600, durationMs: stallMs});
  if (!report.capture_start.timing_hook_present || !report.capture_start.sample_hook_present)
    throw Error('Source timing observation hooks are unavailable');
  traceInstalled = true;
  await page.locator('summary').filter({hasText: 'Diagnostics'}).click();
  await page.locator('#retail-replay-mode').selectOption('performance');
  await page.locator('#retail-replay-file').setInputFiles(values.recipe);
  await page.waitForFunction(() => {
    const button = document.querySelector('#retail-replay-start');
    return button && !button.disabled;
  }, null, {timeout: phaseTimeoutMs});
  await page.evaluate(() => { window.lastRetailReplayReport = null; });
  await page.locator('#retail-replay-start').click();
  const deadline = Date.now() + captureTimeoutMs;
  let status = null, gpuOn = false, terminal = null;
  while (Date.now() < deadline) {
    status = await readPauseTraceStatus(page);
    if (status.runtime_error || status.dialog_error) throw Error(status.runtime_error || status.dialog_error);
    if (status.capture_errors || status.dropped || status.incident_overflow)
      throw Error('Conditioned timing observation lost rows or hook data');
    if (status.stall_schedule?.status === 'failed') throw Error(JSON.stringify(status.stall_schedule));
    const native = await page.evaluate(() => ({cursor: Module._melee_web_native_menu_replay_cursor(),
      frame: window.menuObservePlayer?.()?.frame ?? null,
      message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
      running: Module._melee_web_native_menu_running(), at_ms: performance.now(),
      epoch_ms: performance.timeOrigin + performance.now()}));
    if (!gpuOn && native.cursor >= 400) {
      report.match_before_gpu_load = await page.evaluate(() =>
        JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_observe())));
      if (report.match_before_gpu_load?.ready !== true || !(native.frame > 0) || !native.running)
        throw Error('Conditioned fixture did not enter active gameplay before GPU-load treatment');
      const load = await setHeavyGpu(heavy, true);
      report.gpu_windows.push({event: 'on', native, load,
        boundary: 'harness command after observed source cursor; actual command time retained'});
      gpuOn = true;
    }
    if (native.message.startsWith('Paused after a timing disruption') && !native.running) {
      terminal = {outcome: 'timing_pause', native};
      break;
    }
    if (status.replay_report) {
      if (status.replay_report.pass !== true) throw Error(JSON.stringify(status.replay_report));
      terminal = {outcome: 'replay_complete', native};
      break;
    }
    if (native.cursor >= 900) { terminal = {outcome: 'post_stall_source_window_complete', native}; break; }
    await page.waitForTimeout(50);
  }
  if (!terminal) throw Error('Conditioned timing cell exceeded its wall-time bound');
  report.terminal = terminal;
  report.capture_status_at_stop = status;
  if (gpuOn) {
    report.gpu_windows.push({event: 'off', terminal, load: await setHeavyGpu(heavy, false)});
    gpuOn = false;
  }
  report.gpu_observation = await readHeavyGpu(heavy);
  report.capture = await readPauseTraceCapture(page, terminal.outcome);
  report.runtime_incident_recorder = await readRetainedPauseDiagnostics(page,
    terminal.outcome === 'timing_pause');
  const columns = report.capture.columns, width = columns.length;
  const frameColumn = columns.indexOf('sample_source_frame');
  const preparationColumn = columns.indexOf('preparation_ms');
  const timeColumn = columns.indexOf('hook_at_ms');
  report.post_entry_preparation_rows = [];
  for (let index = 0; index < report.capture.rows; index++) {
    const base = index * width;
    if (report.capture.table[base + frameColumn] > 0 && report.capture.table[base + preparationColumn] > 0)
      report.post_entry_preparation_rows.push({row: index,
        hook_at_ms: report.capture.table[base + timeColumn],
        original_match_counter: report.capture.table[base + frameColumn],
        preparation_ms: report.capture.table[base + preparationColumn]});
  }

  if (terminal.outcome === 'timing_pause' && !report.runtime_incident_recorder.retained_records.some(
    record => record.incident?.staging !== null && record.incident?.staging !== undefined))
    throw Error('Timing pause occurred but its runtime recorder staging incident was not retained');
  report.memory_after = await memorySnapshot(page);
  report.ring_status_after = await page.evaluate(() => window.__meleeWebStagingRingStatus);
  if (report.ring_status_after?.frame_slots !== slots || report.ring_status_after?.staging_buffers !== slots)
    throw Error('Active staging ring identity changed during the timing cell');
  if (report.capture.stall_schedule?.status !== 'complete')
    throw Error('Cell stopped before the exact source-cursor stall treatment; retained as inconclusive');
  if (report.capture.stall_schedule.replay_cursor_after !== 600)
    throw Error('Source advanced during the declared host stall');
  if (!report.gpu_observation.window_batches || report.gpu_observation.errors.length)
    throw Error('GPU-load window was not observed cleanly');
  report.build_artifacts_after = await buildArtifacts(buildDirectory);
  if (JSON.stringify(report.build_artifacts_before) !== JSON.stringify(report.build_artifacts_after)) {
    throw Error('Served build artifacts changed during byte capture');
  }
  if (report.browser_errors.length) throw Error('Browser errors were retained during byte capture');
  report.screenshots.success = await takeScreenshot(page, output, 'capture-success');
  report.result = report.post_entry_preparation_rows.length ? 'inconclusive' : 'observed';
  if (report.post_entry_preparation_rows.length) report.confound = 'Native preparation occurred after the original match counter began advancing; the conditioned pause comparison is inconclusive';
} catch (error) {
  report.failure = String(error?.stack || error);
  if (traceInstalled && page && !page.isClosed()) {
    try { report.capture = await readPauseTraceCapture(page, 'failure'); } catch (readError) {
      report.capture_read_error = String(readError?.message || readError);
    }
    try { report.capture_status_at_stop = await readPauseTraceStatus(page); } catch {}
    try { report.runtime_incident_recorder = await readRetainedPauseDiagnostics(page, true); }
    catch (readError) { report.recorder_read_error = String(readError); }
    try { report.memory_after = await memorySnapshot(page); } catch {}
  }
  if (page && !page.isClosed()) {
    try { report.screenshots.failure = await takeScreenshot(page, output, 'capture-failure'); }
    catch (screenshotError) { report.screenshot_error = String(screenshotError?.message || screenshotError); }
  }
  process.exitCode = 1;
} finally {
  if (heavy) {
    try { report.gpu_final = await readHeavyGpu(heavy); await setHeavyGpu(heavy, false); }
    catch (error) { report.gpu_cleanup_error = String(error); }
  }
  if (traceInstalled && page && !page.isClosed()) {
    try {
      report.runtime_incident_recorder_final = await readRetainedPauseDiagnostics(page, true);
    } catch (error) { report.recorder_final_read_error = String(error); }
  }
  try { driver?.dispose(); } catch {}
  try { await browser?.close(); } catch (error) {
    report.close_error = String(error?.message || error);
    report.failure ||= 'Browser close failed';
    report.result = 'fail';
    process.exitCode = 1;
  }
  await fs.writeFile(path.join(output, 'capture.json'), JSON.stringify(report.capture ?? null, null, 2) + '\n');
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({result: report.result, requested_slots: slots,
    observed_slots: report.ring_status?.frame_slots ?? null,
    stall_status: report.capture?.stall_schedule?.status ?? null,
    fixture_sha256: report.inputs?.recipe?.sha256 ?? null,
    failure: report.failure?.split('\n')[0] ?? null}));
}
