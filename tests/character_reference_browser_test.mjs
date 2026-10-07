#!/usr/bin/env node
/*
 * Headless whole-session MWRC character diagnostic.
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
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {requireConnectedPage, requireReplaySnapshot} from './character_reference_health.mjs';

const {values} = parseArgs({options: {
  url: {type: 'string'},
  disc: {type: 'string'},
  recipe: {type: 'string'},
  manifest: {type: 'string'},
  out: {type: 'string'},
  playwright: {type: 'string'},
  'phase-timeout': {type: 'string', default: '120000'},
  'replay-timeout': {type: 'string', default: '900000'},
  'poll-ms': {type: 'string', default: '250'},
  'stop-after-source-cursor': {type: 'string'},
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
const phaseTimeoutMs = integer('phase-timeout', 1000, 300000);
const replayTimeoutMs = integer('replay-timeout', 1000, 1800000);
const pollMs = integer('poll-ms', 50, 2000);
const stopAfterCursor = values['stop-after-source-cursor'] ? integer('stop-after-source-cursor',1,108000) : null;
const output = path.resolve(values.out);
const inputPaths = [values.disc, values.recipe, values.manifest].filter(Boolean).map(value => path.resolve(value));

await fs.mkdir(output, {recursive: false});
const report = {
  schema: 'melee-web-headless-whole-session-replay-v1',
  scope: 'Single headless browser MWRC v8 diagnostic; no pixel, PCM, performance, or admission claim',
  result: 'fail',
  url: values.url,
  mode: 'state',
  cpu_mode: 'source CPUs recompute decisions; CPU observations not requested',
  scheduling_scope: 'MWRC v8 source-consumed PAD per tick; draw cadence equivalence not evaluated',
  phase_timeout_ms: phaseTimeoutMs,
  replay_timeout_ms: replayTimeoutMs,
  poll_ms: pollMs,
  phases: [],
  snapshots: [],
  first_error: null,
  first_mismatch: null,
  browser_errors: [],
  unexpected_requests: [],
  screenshots: [],
};
let browser;
let page;
let driver;
let currentPhase = 'startup';
let lastSnapshotKey = '';
const pageErrors = [];
const itemDrawTrace = [];
const artifactReads = [];
report.served_artifacts = [];
report.harness_sha256 = await digestHarness();

async function digestHarness() {
  return createHash('sha256').update(await fs.readFile(new URL(import.meta.url))).digest('hex');
}

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

function firstError(kind, message, details = null) {
  if (!report.first_error) report.first_error = {kind, message: String(message), phase: currentPhase, details};
}

async function snapshot(reason = 'poll') {
  if (!page) return null;
  let value;
  try {
    requireConnectedPage(browser, page);
    value = await page.evaluate(reason => {
      const module = globalThis.Module;
      const call = name => {
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
      // CSS_PORT_OBSERVER_ABI_BEGIN
      if (phase === 1 && module?._melee_web_css_observe_port) {
        const ids = module._malloc(14 * 4), geometry = module._malloc(8 * 4);
        if (!ids || !geometry) {
          if (ids) module._free(ids);
          if (geometry) module._free(geometry);
          throw Error('CSS observation allocation failed');
        }
        try {
          css = [];
          for (let port = 0; port < 4; ++port) {
            if (!module._melee_web_css_observe_port(port, 8, ids, geometry)) { css.push(null); continue; }
            css.push({ids: Array.from(module.HEAP32.subarray(ids >> 2, (ids >> 2) + 4)), geometry: Array.from(module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))});
          }
        } finally { module._free(ids); module._free(geometry); }
      }
      // CSS_PORT_OBSERVER_ABI_END
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
        item_draws: globalThis.__meleeWebItemDraws || [],
        log: text('#log'),
        replay_report: replayReport,
        replay_downloads: [...document.querySelectorAll('#retail-replay-downloads a')].map(link => link.download),
      };
    }, reason);
  } catch (error) {
    firstError('snapshot', error.message || error);
    return {reason, snapshot_error: String(error.message || error)};
  }
  const key = JSON.stringify({phase: value.phase, running: value.running, cursor: value.source_cursor,
    error: value.runtime_error, report: value.replay_report?.result || null,
    report_pass: value.replay_report?.pass ?? null});
  if (key !== lastSnapshotKey || reason !== 'poll') {
    lastSnapshotKey = key;
    if (report.snapshots.length < 2048) report.snapshots.push(value);
  }
  if (value.runtime_error) firstError('runtime', value.runtime_error, value);
  return value;
}

async function phase(name, task, timeoutMs = phaseTimeoutMs) {
  currentPhase = name;
  const started = Date.now();
  const row = {name, started_at: new Date(started).toISOString(), timeout_ms: timeoutMs, result: 'fail'};
  report.phases.push(row);
  await snapshot('phase-start:' + name);
  const timer = setInterval(() => { void snapshot('phase-poll:' + name); }, pollMs);
  let timeout;
  try {
    const value = await Promise.race([
      task(),
      new Promise((_, reject) => { timeout=setTimeout(() => reject(Error(`${name} exceeded ${timeoutMs} ms`)), timeoutMs); }),
    ]);
    row.result = 'pass';
    return value;
  } catch (error) {
    firstError(name, error.message || error, await snapshot('phase-error:' + name));
    throw error;
  } finally {
    clearInterval(timer);
    clearTimeout(timeout);
    row.elapsed_ms = Date.now() - started;
    await snapshot('phase-end:' + name);
  }
}

try {
  report.inputs = {disc: await statInput(values.disc), recipe: await statInput(values.recipe)};
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
  const {chromium, browser: launchOptions, browserPath, playwrightPath} =
    await loadBrowserTools(values.playwright);
  report.browser = {executable: path.basename(browserPath), playwright: playwrightPath};
  browser = await chromium.launch({...browserLaunchOptions(launchOptions, {timeout: phaseTimeoutMs}), headless: true});
  report.browser.version = browser.version();
  page = await browser.newPage({viewport: {width: 900, height: 700}, deviceScaleFactor: 1});
  // Probe-free character prefix: source CPUs recompute decisions; no CPU-output injection or CPU probe hook.
  page.setDefaultTimeout(phaseTimeoutMs);
  page.setDefaultNavigationTimeout(phaseTimeoutMs);
  page.on('pageerror', error => { const row = {kind: 'pageerror', message: error.stack || error.message}; pageErrors.push(row); firstError(row.kind, row.message); });
  page.on('crash', () => firstError('browser-crash', 'Headless browser page crashed'));
  page.on('console', message => {
    const text = message.text();
    if (text.startsWith('ITEMDRAW')) itemDrawTrace.push(text);
    if (message.type() === 'error') { const row = {kind: 'console', message: text}; pageErrors.push(row); firstError(row.kind, row.message); }
  });
  page.on('request', request => { if (request.method() !== 'GET') report.unexpected_requests.push({method: request.method(), url: request.url()}); });
  page.on('response', response => { if (response.status() >= 400) { const row = {kind: 'http', status: response.status(), url: response.url()}; pageErrors.push(row); firstError(row.kind, `${response.status()} ${response.url()}`); } });
  page.on('response', response => {
    const pathname = new URL(response.url()).pathname;
    if (!/\/gameplay_menu_browser\.(js|wasm)$/.test(pathname)) return;
    artifactReads.push(response.body().then(bytes => {
      report.served_artifacts.push({url:response.url(), status:response.status(),
        bytes:bytes.length, sha256:createHash('sha256').update(bytes).digest('hex')});
    }).catch(error => firstError('artifact-provenance', String(error))));
  });
  await phase('http-load', async () => {
    const response = await page.goto(values.url, {waitUntil: 'domcontentloaded'});
    if (response?.status() !== 200) throw Error(`runtime.html returned HTTP ${response?.status()}`);
    const headers = response.headers();
    if (headers['cross-origin-opener-policy'] !== 'same-origin' || headers['cross-origin-embedder-policy'] !== 'require-corp')
      throw Error('Runtime did not load over COOP/COEP HTTP isolation');
    report.gpu = await page.evaluate(async () => ({isolated: crossOriginIsolated, adapter: !!await navigator.gpu?.requestAdapter()}));
    if (!report.gpu.isolated || !report.gpu.adapter) throw Error('Rendered prefix requires an isolated WebGPU page');
  });
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: phaseTimeoutMs});
  await phase('runtime-ready', () => driver.waitForImport());
  await phase('disc-import', () => driver.selectDisc(values.disc));
  await phase('asset-preparation', () => driver.waitForStart());
  // The development UI import is deliberately separate from the disc import.
  // It is the only replay input supplied after the fresh runtime is prepared.
  const uiTimeout = Math.min(phaseTimeoutMs, 10000);
  await phase('diagnostics-open', async () => {
    await page.locator('summary').filter({hasText: 'Diagnostics'}).click();
  }, uiTimeout);
  await phase('replay-mode-import', async () => {
    await page.locator('#retail-replay-mode').selectOption('state');
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
  const replayOutcome = await phase('whole-session-replay', async () => {
    await page.locator('#retail-replay-start').click();
    const deadline = Date.now() + replayTimeoutMs;
    let last = null;
    let observedPhase = null;
    let phaseEntryCursor = 0;
    let capturedPhase = false;
    while (Date.now() < deadline) {
      requireConnectedPage(browser, page);
      last = await snapshot('replay-poll');
      await write('progress.json', {phase:last?.phase,cursor:last?.source_cursor,status:last?.status,error:last?.runtime_error || last?.snapshot_error,at_ms:last?.at_ms});
      requireReplaySnapshot(last);
      if (report.first_error) throw Error(report.first_error.message);
      if (last.phase !== observedPhase) {
        observedPhase = last.phase;
        phaseEntryCursor = last.source_cursor;
        capturedPhase = false;
      }
      // Retain an advancing rendered match/Results scene, not just the blank
      // post-unload canvas. This is an observer operation, never a game call.
      if (!capturedPhase && [7, 8].includes(last.phase) &&
          last.source_cursor >= phaseEntryCursor + 120) {
        const renderCanvas = page.locator('canvas').first();
        const canvas = await renderCanvas.boundingBox();
        if (!canvas || canvas.width < 1 || canvas.height < 1)
          throw Error('Advancing replay has no visible render canvas');
        const name = `scene-${report.screenshots.length + 1}-phase${last.phase}-input${last.source_cursor}.png`;
        await renderCanvas.screenshot({path:path.join(output, name)});
        const afterCapture = await snapshot('after-canvas-capture');
        requireReplaySnapshot(afterCapture);
        report.screenshots.push({name, phase:last.phase, cursor_before_capture:last.source_cursor,
          cursor_after_capture:afterCapture.source_cursor, scope:'rendered canvas; no exact draw-ordinal attribution', canvas});
        capturedPhase = true;
      }
      const links = last?.replay_downloads || [];
      if (links.includes('retail-browser-report.json')) {
        report.browser_report = last.replay_report;
        if (last.replay_report?.complete && last.replay_report?.pass) return;
        if (!report.first_mismatch && last.replay_report?.failures?.length)
          report.first_mismatch = {phase: currentPhase, failure: last.replay_report.failures[0], snapshot: last};
        throw Error(`Browser replay report failed: ${JSON.stringify(last.replay_report?.failures || [])}`);
      }
      if (last?.runtime_error) throw Error(last.runtime_error);
      if (stopAfterCursor && !report.deliberate_prefix_stop && last?.source_cursor >= stopAfterCursor) {
        report.deliberate_prefix_stop = {requested_cursor: stopAfterCursor, observed_cursor: last.source_cursor, reason: 'Bounded first-divergence diagnostic; incomplete replay expected'};
        try { await page.screenshot({path: path.join(output, 'prefix.png'), fullPage: false}); }
        catch (error) { report.prefix_screenshot_error = String(error?.message || error); }
        await page.locator('#unload').click();
        await page.locator('#retail-replay-downloads a[download="retail-port.jsonl"]').waitFor({state:'attached'});
        await page.locator('#retail-replay-downloads a[download="retail-browser-report.json"]').waitFor({state:'attached'});
        return 'prefix';
      }
      await new Promise(resolve => setTimeout(resolve, pollMs));
    }
    throw Error(`whole-session replay exceeded ${replayTimeoutMs} ms; last snapshot ${JSON.stringify(last)}`);
  }, replayTimeoutMs);
  report.result = replayOutcome === 'prefix' ? 'prefix' : 'pass';
  if(report.result==='pass'&&!report.browser_report?.complete)
    throw Error('Replay harness cannot claim pass without a completed browser report');
} catch (error) {
  firstError(currentPhase, error.message || error, await snapshot('fatal'));
  report.failure = String(error.stack || error);
} finally {
  if (page && !page.isClosed()) {
    const final = await snapshot('finally');
    report.final_snapshot = final;
    try {
      const artifacts = await page.locator('#retail-replay-downloads a').evaluateAll(async links => {
        const retained = [];
        for (const link of links) {
          if (!['retail-port.jsonl', 'retail-timer.jsonl', 'retail-browser-report.json'].includes(link.download) || !link.href.startsWith('blob:')) continue;
          retained.push({name: link.download, text: await (await fetch(link.href)).text()});
        }
        return retained;
      });
      report.saved_downloads = [];
      for (const artifact of artifacts) {
        await write(artifact.name, artifact.text);
        report.saved_downloads.push({name: artifact.name, bytes: Buffer.byteLength(artifact.text), sha256: createHash('sha256').update(artifact.text).digest('hex')});
      }
    } catch (error) { report.download_error = String(error?.message || error); }
    try { const rows = await page.evaluate(() => window.__cpuPrefixRows || []); if(rows.length) await write('cpu-prefix.jsonl',rows.join('\n')+'\n'); } catch(error) { report.cpu_download_error = String(error); }
    try { await write('page.txt', await page.locator('body').innerText()); } catch {}
    try { await page.screenshot({path: path.join(output, 'final.png'), fullPage: false}); } catch {}
  }
  report.browser_errors = pageErrors.slice(0, 256);
  await Promise.all(artifactReads);
  if (!['js', 'wasm'].every(extension => report.served_artifacts.some(row =>
      new URL(row.url).pathname.endsWith('gameplay_menu_browser.' + extension) && row.status === 200)))
    firstError('artifact-provenance', 'Served JS/Wasm provenance is incomplete');
  if (report.first_error) report.result = 'fail';
  if (itemDrawTrace.length) await write('item-draw-trace.txt', itemDrawTrace.join('\n') + '\n');
  await write('report.json', report);
  if (report.failure) await write('failure.txt', report.failure + '\n');
  try { driver?.dispose(); } catch {}
  if (browser) await browser.close().catch(() => {});
}

if (report.result === 'prefix') console.log(`prefix: source cursor ${report.deliberate_prefix_stop.observed_cursor}; rendering and diagnostics retained`);
else if (report.result !== 'pass') process.exitCode = 1;
else console.log(`pass: ${report.recipe_header.frames} frames; source cursor and phase diagnostics retained`);
