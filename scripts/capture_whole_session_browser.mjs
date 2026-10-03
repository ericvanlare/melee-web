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
import {parseArgs} from 'node:util';
import {loadBrowserTools, browserLaunchOptions} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {finalizeSessionCapture, validateRuntimeDataAbort} from './whole_session_capture_result.mjs';
import {parseRngDrawProbe, validateRngDrawProbeRows} from './rng_draw_probe.mjs';

const {values} = parseArgs({options: {
  url: {type: 'string'},
  disc: {type: 'string'},
  recipe: {type: 'string'},
  manifest: {type: 'string'},
  'runtime-data': {type: 'string'},
  out: {type: 'string'},
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
const phaseTimeoutMs = integer('phase-timeout', 1000, 300000);
const replayTimeoutMs = integer('replay-timeout', 1000, 1800000);
const pollMs = integer('poll-ms', 50, 2000);
const replayPollMs = values['replay-poll-ms'] === undefined
  ? pollMs : integer('replay-poll-ms', 1, 2000);
const stopAfter = values['stop-after-source-frames'] ? integer('stop-after-source-frames',1,108000) : null;
const resumeTimingPauses = values['resume-timing-pauses'];
const captureCpuObservations = values['cpu-observations'];
const rngDrawProbe = parseRngDrawProbe({range: values['rng-draw-probe-range'],
  cursors: values['rng-draw-probe-cursors']});
const runtimeDataUrl = new URL('gameplay_menu_browser.data', url).href;
const output = path.resolve(values.out);
const inputPaths = [values.disc, values.recipe, values.manifest, values['runtime-data']]
  .filter(Boolean).map(value => path.resolve(value));

await fs.mkdir(output, {recursive: false});
const report = {
  schema: 'melee-web-headless-whole-session-replay-v1',
  scope: 'Single headless browser MWRC v8/v9 diagnostic; no pixel, PCM, performance, or admission claim',
  result: 'fail',
  url: values.url,
  mode: 'state',
  phase_timeout_ms: phaseTimeoutMs,
  replay_timeout_ms: replayTimeoutMs,
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
  if (!page || page.isClosed()) return null;
  let value;
  try {
    value = await page.evaluate(reason => {
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
      if (phase === 1 && module?._melee_web_css_observe_port) {
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
    }, reason);
  } catch (error) {
    firstError('snapshot', error.message || error);
    return {reason, snapshot_error: String(error.message || error)};
  }
  const key = JSON.stringify({phase: value.phase, running: value.running, cursor: value.source_cursor,
    error: value.runtime_error, report: value.replay_report?.result || null,
    report_pass: value.replay_report?.pass ?? null});
  // Keep a state transition that occurs between source-cursor increments too,
  // while excluding animated geometry so focused CSS traces remain bounded.
  const cssStateKey = value.phase === 1 ? JSON.stringify({
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

async function phase(name, task, timeoutMs = phaseTimeoutMs) {
  currentPhase = name;
  const started = Date.now();
  const row = {name, started_at: new Date(started).toISOString(), timeout_ms: timeoutMs, result: 'fail'};
  report.phases.push(row);
  await snapshot('phase-start:' + name);
  const pendingPolls = new Set();
  const timer = setInterval(() => {
    const pending = snapshot('phase-poll:' + name);
    pendingPolls.add(pending);
    void pending.finally(() => pendingPolls.delete(pending));
  }, pollMs);
  let timeout;
  try {
    await Promise.race([
      task(),
      new Promise((_, reject) => { timeout=setTimeout(() => reject(Error(`${name} exceeded ${timeoutMs} ms`)), timeoutMs); }),
    ]);
    row.result = 'pass';
  } catch (error) {
    firstError(name, error.message || error, await snapshot('phase-error:' + name));
    throw error;
  } finally {
    clearInterval(timer);
    clearTimeout(timeout);
    await Promise.all(pendingPolls);
    row.elapsed_ms = Date.now() - started;
    await snapshot('phase-end:' + name);
  }
}

try {
  report.inputs = {disc: await statInput(values.disc), recipe: await statInput(values.recipe)};
  if (values['runtime-data'])
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
  if (rngDrawProbe && rngDrawProbe.selected.some(cursor => cursor >= report.recipe_header.frames))
    throw Error('RNG draw probe cursor must be inside the source recipe frame count');
  if (captureCpuObservations && report.recipe_header.version !== 9)
    throw Error('--cpu-observations is restricted to MWRC v9 second-match diagnostics');
  const cpuObservationRowLimit = report.recipe_header.frames;
  report.cpu_observation_row_limit = cpuObservationRowLimit;
  const {chromium, browser: launchOptions, browserPath, playwrightPath} =
    await loadBrowserTools(values.playwright);
  report.browser = {executable: path.basename(browserPath), playwright: playwrightPath};
  browser = await chromium.launch({...browserLaunchOptions(launchOptions, {timeout: phaseTimeoutMs}), headless: true});
  report.browser.version = browser.version();
  page = await browser.newPage({viewport: {width: 900, height: 700}, deviceScaleFactor: 1});
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
    const response = await page.goto(values.url, {waitUntil: 'domcontentloaded'});
    if (response?.status() !== 200) throw Error(`runtime.html returned HTTP ${response?.status()}`);
    const headers = response.headers();
    if (headers['cross-origin-opener-policy'] !== 'same-origin' || headers['cross-origin-embedder-policy'] !== 'require-corp')
      throw Error('Runtime did not load over COOP/COEP HTTP isolation');
    if (!await page.evaluate(() => crossOriginIsolated)) throw Error('Browser page is not cross-origin isolated');
  });
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: phaseTimeoutMs});
  await phase('runtime-ready', () => driver.waitForImport());
  if (values['runtime-data']) {
    report.runtime_data_load = await page.evaluate(async expectedUrl => {
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
    }, runtimeDataUrl);
    const expected = report.inputs.runtime_data;
    const actual = report.runtime_data_load;
    if (actual.file_bytes !== expected.bytes || actual.sha256 !== expected.sha256 ||
        actual.loaded_bytes !== expected.bytes || actual.total_bytes !== expected.bytes)
      throw Error('Loaded Emscripten runtime data differs from the explicitly bound artifact');
  }
  await page.evaluate(() => { window.__meleeNativeRuntimeReady = true; });
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
  await phase('whole-session-replay', async () => {
    await page.locator('#retail-replay-start').click();
    const deadline = Date.now() + replayTimeoutMs;
    let last = null;
    while (Date.now() < deadline) {
      last = await snapshot('replay-poll');
      if (!last) throw Error('Browser page closed before the recorded-session replay completed');
      await write('progress.json', {harness_phase:currentPhase,phase:last?.phase,cursor:last?.source_cursor,
        status:last?.status,error:last?.runtime_error,log:last?.log?.slice(-1600),
        browser_report:last?.replay_report,at_ms:last?.at_ms});
      if (!resumeTimingPauses && last.status?.startsWith('Paused after a timing disruption'))
        throw Error(`Recorded-session replay paused after a timing disruption at cursor ${last.source_cursor}`);
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
      if (stopAfter && !report.deliberate_prefix_stop && last?.source_cursor >= stopAfter) {
        report.deliberate_prefix_stop = {requested_cursor: stopAfter, observed_cursor: last.source_cursor, reason: 'Bounded first-divergence diagnostic; incomplete replay expected'};
        await page.locator('#unload').click();
      }
      await new Promise(resolve => setTimeout(resolve, replayPollMs));
    }
    throw Error(`whole-session replay exceeded ${replayTimeoutMs} ms; last snapshot ${JSON.stringify(last)}`);
  }, replayTimeoutMs);
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
  try { driver?.dispose(); } catch (error) { report.close_error = String(error); }
  // Quiesce browser callbacks before deciding whether diagnostics permit success.
  try { if (browser) await browser.close(); } catch (error) { report.close_error = String(error); }
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
  finalizeSessionCapture(report);
  await write('report.json', report);
  if (report.failure) await write('failure.txt', report.failure + '\n');
}

if (report.result !== 'pass') process.exitCode = 1;
else console.log(`pass: ${report.recipe_header.frames} frames; source cursor and phase diagnostics retained`);
