#!/usr/bin/env node
/** Capture 600 actual Queue.WriteBuffer ranges from a synthetic MWRC v4 match. */
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {installStagingRingByteCapture, readStagingRingByteCapture,
  readStagingRingByteCaptureStatus, armStagingRingByteCapture} from './staging_ring_byte_capture.mjs';

import {classifyStagingByteReplayCompletion} from './staging_ring_replay_completion.mjs';

const ROOT = path.resolve(import.meta.dirname, '..');
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, recipe: {type: 'string'}, out: {type: 'string'},
  'build-dir': {type: 'string'}, slots: {type: 'string'}, playwright: {type: 'string'},
  'phase-timeout-ms': {type: 'string', default: '180000'},
  'capture-timeout-ms': {type: 'string', default: '300000'},
  'capture-prefix': {type: 'string', default: '600'},
}});

function integer(name, min, max) {
  const value = Number(values[name]);
  if (!Number.isInteger(value) || value < min || value > max) throw Error(`--${name} must be ${min}..${max}`);
  return value;
}
if (!values.url || !values.disc || !values.recipe || !values.out || !values['build-dir'] || !values.slots) {
  throw Error('Use --url LOOPBACK_RUNTIME_URL --disc OWNED_CISO --recipe SYNTHETIC_MWRC_V4 --out NEW_RUN_DIRECTORY --build-dir BUILT_RUNTIME_DIRECTORY --slots 2|4 [--playwright PACKAGE_DIR]');
}
const slots = Number(values.slots);
if (![2, 4].includes(slots)) throw Error('--slots must be 2 or 4');
const prefixFrames = integer('capture-prefix', 1, 600);
const phaseTimeoutMs = integer('phase-timeout-ms', 1000, 600000);
const captureTimeoutMs = integer('capture-timeout-ms', 1000, 1200000);
const url = new URL(values.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html') ||
    !['localhost', '127.0.0.1', '::1', '[::1]'].includes(url.hostname)) {
  throw Error('A real loopback HTTP runtime.html URL is required for the opt-in experiment');
}
url.searchParams.set('melee-web-staging-slots', String(slots));
url.searchParams.set('melee-web-staging-byte-hash', '1');
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
  if (header.version !== 4 || header.frames !== 600 || header.profile_characters_hex !== '07ff' ||
      header.profile_stages_hex !== '01c0' || bytes.length !== 20 + 0x138 + 822 + header.frames * 44) {
    throw Error('Recipe header/envelope is not the declared 600-frame synthetic MWRC v4 four-player profile');
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
  schema: 'melee-web-staging-ring-byte-capture-v1',
  scope: 'synthetic MWRC v4 diagnostic; first 600 gameplay source submissions; actual Queue.WriteBuffer bytes; no pixel or GPU-output equality claim',
  result: 'fail',
  requested_frame_slots: slots,
  capture_cap: 600,
  browser_mode: 'headless installed Chrome, muted host output; functional hash capture only',
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
let browser, page, driver;
let liveCapture = false;
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
  if (report.ring_status?.selection?.byte_hash_enabled !== true) {
    throw Error('Requested submitted-byte hashing was not enabled by the loopback desktop gate');
  }
  report.memory_before = await memorySnapshot(page);
  report.graphics_check = await graphicsCheck(page);
  if (!report.graphics_check.available) throw Error(`Headless WebGPU/canvas check failed: ${JSON.stringify(report.graphics_check)}`);
  const installed = await installStagingRingByteCapture(page);
  if (installed.status !== 'installed' || !installed.source_draw_tag_supported) {
    throw Error('The source draw capture tag was unavailable');
  }
  // Arm before recipe upload and replay launch so cursor zero cannot be
  // confused with an arbitrary running-match prefix.
  report.capture_start = await armStagingRingByteCapture(page);
  liveCapture = true;
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
  let status = {status: 'capturing'};
  let replayReport = null;
  while (Date.now() < deadline) {
    status = await readStagingRingByteCaptureStatus(page);
    const failure = await applicationFailure(page);
    if (failure.runtimeError || failure.dialogError) {
      throw Error(`Runtime/preparation failure during capture: ${failure.runtimeError || failure.dialogError}`);
    }
    if (status.status === 'incomplete' || status.status === 'overflow') {
      throw Error(`Submitted-byte capture failed: ${JSON.stringify(status)}`);
    }
    replayReport = failure.replayReport;
    if (replayReport) report.native_replay_report = replayReport;
    if (replayReport) {
      report.functional_completion = classifyStagingByteReplayCompletion(replayReport);
      if (status.status !== 'complete') {
        throw Error(`Native replay completed before the byte capture reached 600 rows: ${JSON.stringify(status)}`);
      }
    }
    if (prefixFrames < 600 && status.frame_count >= prefixFrames) break;
    if (status.status === 'complete' && replayReport?.complete === true) break;
    await page.waitForTimeout(100);
  }
  report.capture_status_at_stop = status;
  report.capture = await readStagingRingByteCapture(page);
  if (!report.capture || typeof report.capture !== 'object') throw Error('Capture read returned no report');
  report.capture.requested_slots = slots;
  report.capture.ring_status = report.ring_status;
  report.native_replay_report = replayReport;
  report.memory_after = await memorySnapshot(page);
  report.ring_status_after = await page.evaluate(() => window.__meleeWebStagingRingStatus);
  if (report.ring_status_after?.frame_slots !== slots || report.ring_status_after?.staging_buffers !== slots) {
    throw Error('Active staging ring identity changed during capture');
  }
  if (Date.now() >= deadline && status.status !== 'complete' && status.frame_count < prefixFrames) throw Error('600-frame capture exceeded its wall-time bound');
  if (prefixFrames === 600 && (report.capture.status !== 'complete' || report.capture.frame_count !== 600 ||
      report.capture.pending_frame_count !== 0 || report.capture.errors?.length || report.capture.overflow)) {
    throw Error(`600-frame capture failed: ${JSON.stringify(status)}`);
  }
  if (prefixFrames === 600) report.functional_completion = classifyStagingByteReplayCompletion(replayReport);
  report.build_artifacts_after = await buildArtifacts(buildDirectory);
  if (JSON.stringify(report.build_artifacts_before) !== JSON.stringify(report.build_artifacts_after)) {
    throw Error('Served build artifacts changed during byte capture');
  }
  if (report.browser_errors.length) throw Error('Browser errors were retained during byte capture');
  report.screenshots.success = await takeScreenshot(page, output, 'capture-success');
  report.result = prefixFrames === 600 ? 'captured' : 'prefix_captured';
  report.requested_prefix_frames = prefixFrames;
  report.fixture_gameplay_progression = report.capture.entry_progress;
  report.fixture_active_gameplay_observed = report.capture.entry_progress.some(row => row.match?.ready === true && row.source_frame > 0);
} catch (error) {
  report.failure = String(error?.stack || error);
  if (liveCapture && page && !page.isClosed()) {
    try { report.capture = await readStagingRingByteCapture(page); } catch (readError) {
      report.capture_read_error = String(readError?.message || readError);
    }
    if (!report.capture || typeof report.capture !== 'object') report.capture = {status: 'unavailable'};
    report.capture.requested_slots = slots;
    report.capture.ring_status = report.ring_status;
    try { report.memory_after = await memorySnapshot(page); } catch {}
  }
  if (page && !page.isClosed()) {
    try { report.screenshots.failure = await takeScreenshot(page, output, 'capture-failure'); }
    catch (screenshotError) { report.screenshot_error = String(screenshotError?.message || screenshotError); }
  }
  process.exitCode = 1;
} finally {
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
    capture_status: report.capture?.status ?? null,
    fixture_sha256: report.inputs?.recipe?.sha256 ?? null,
    failure: report.failure?.split('\n')[0] ?? null}));
}
