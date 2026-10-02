#!/usr/bin/env node
/**
 * Headless browser boundary check for the packaged audio-player diagnostics.
 *
 * The normal package is served byte-for-byte from --site.  The only extra
 * route is a local fixture that imports that package's audio-preview runtime
 * and exposes its owner to this test.  No production page or runtime module
 * is changed by the fixture.
 */
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {parseArgs} from 'node:util';

import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const DIAGNOSTIC_META_ID = 'runtime-diagnostic-identity';
const DIAGNOSTIC_SCHEMA_VERSION = 1;
const SIMULATION_DEBT_REASON = 'simulation_debt';
const AUDIO_DEBT_REASON = 'audio_debt';
const SIMULATION_DEBT_THRESHOLD = 8;
const AUDIO_DEBT_THRESHOLD = 60;
const COLLECTION_CALLBACKS = 600;
const COLLECTION_TIMEOUT_MS = 30000;
const STALL_TIMEOUT_MS = 10000;
const SAFE_REPORT_KEYS = new Set([
  'schema', 'version', 'session_id', 'identity', 'environment', 'client', 'active',
  'capabilities', 'native', 'audio', 'lifecycle', 'limits', 'flags', 'incidents',
]);
const MIME_TYPES = Object.freeze({
  '.css': 'text/css; charset=utf-8',
  '.data': 'application/octet-stream',
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
  '.wasm': 'application/wasm',
});

function fail(message) {
  throw new Error(message);
}

function requireValue(condition, message) {
  if (!condition) fail(message);
}

function parseIdentity(value, label) {
  requireValue(value && typeof value === 'object' && !Array.isArray(value), `${label} must be an object`);
  const keys = Object.keys(value).sort();
  requireValue(JSON.stringify(keys) === JSON.stringify([
    'build_profile', 'runtime_hash', 'schema_version', 'source_commit',
  ]), `${label} has an unexpected field set`);
  requireValue(value.schema_version === DIAGNOSTIC_SCHEMA_VERSION,
    `${label} has an unsupported schema version`);
  requireValue(typeof value.source_commit === 'string' && /^[0-9a-f]{40}$/.test(value.source_commit),
    `${label} has an invalid source commit`);
  requireValue(typeof value.runtime_hash === 'string' && /^[0-9a-f]{16}$/.test(value.runtime_hash),
    `${label} has an invalid runtime hash`);
  requireValue(typeof value.build_profile === 'string' &&
    ['audio-player', 'audio-preview', 'player'].includes(value.build_profile),
  `${label} has an invalid build profile`);
  return {
    schema_version: value.schema_version,
    source_commit: value.source_commit,
    runtime_hash: value.runtime_hash,
    build_profile: value.build_profile,
  };
}

function decodeHtml(value) {
  return value
    .replace(/&quot;/g, '"')
    .replace(/&#x27;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

function parseDiagnosticMeta(html, label) {
  const tags = html.match(/<meta\b[^>]*>/gi) || [];
  const matches = [];
  for (const tag of tags) {
    const id = tag.match(/\bid\s*=\s*(["'])(.*?)\1/i)?.[2];
    if (id !== DIAGNOSTIC_META_ID) continue;
    const content = tag.match(/\bcontent\s*=\s*(["'])(.*?)\1/i)?.[2];
    requireValue(content !== undefined, `${label} identity metadata is missing content`);
    matches.push(content);
  }
  requireValue(matches.length === 1, `${label} must contain exactly one diagnostic identity meta`);
  let value;
  try {
    value = JSON.parse(decodeHtml(matches[0]));
  } catch (error) {
    throw new Error(`${label} identity metadata is not valid JSON: ${error.message}`);
  }
  return parseIdentity(value, label);
}

function digest(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

async function listFiles(root, prefix = '') {
  const result = [];
  const entries = await fs.readdir(root, {withFileTypes: true});
  for (const entry of entries) {
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    const file = path.join(root, entry.name);
    if (entry.isSymbolicLink()) fail(`Package contains a symlink: ${relative}`);
    if (entry.isDirectory()) result.push(...await listFiles(file, relative));
    else if (entry.isFile()) result.push(relative);
    else fail(`Package contains a non-file entry: ${relative}`);
  }
  return result.sort();
}

async function verifyAuditedPackage(site, manifest, expectedIdentity) {
  requireValue(manifest && typeof manifest === 'object', 'Audio manifest must be an object');
  requireValue(manifest.profile === 'audio-player', 'Browser check requires the audited audio-player profile');
  requireValue(typeof manifest.source_sha === 'string' && /^[0-9a-f]{40}$/.test(manifest.source_sha),
    'Audio manifest source identity is invalid');
  requireValue(typeof manifest.runtime_hash === 'string' && /^[0-9a-f]{16}$/.test(manifest.runtime_hash),
    'Audio manifest runtime identity is invalid');
  requireValue(JSON.stringify(expectedIdentity) === JSON.stringify({
    schema_version: 1,
    source_commit: manifest.source_sha,
    runtime_hash: manifest.runtime_hash,
    build_profile: 'audio-player',
  }), 'Manifest identity differs from the expected safe identity');
  requireValue(Array.isArray(manifest.files), 'Audio manifest inventory is missing');
  const records = new Map();
  for (const record of manifest.files) {
    requireValue(record && typeof record === 'object', 'Audio manifest has an invalid inventory record');
    requireValue(typeof record.path === 'string' && !record.path.startsWith('/') &&
      !record.path.includes('\\') && path.posix.normalize(record.path) === record.path &&
      !record.path.split('/').includes('..'), 'Audio manifest contains an invalid path');
    requireValue(!records.has(record.path), `Audio manifest repeats ${record.path}`);
    requireValue(Number.isInteger(record.size) && record.size >= 0 &&
      typeof record.sha256 === 'string' && /^[0-9a-f]{64}$/.test(record.sha256),
    `Audio manifest has an invalid record for ${record.path}`);
    records.set(record.path, record);
  }
  const actualPaths = await listFiles(site);
  requireValue(actualPaths.length === records.size, 'Audited package inventory size changed');
  for (const relative of actualPaths) {
    const record = records.get(relative);
    requireValue(record, `Audited package contains an unreviewed file: ${relative}`);
    const bytes = await fs.readFile(path.join(site, relative));
    requireValue(bytes.byteLength === record.size && digest(bytes) === record.sha256,
      `Audited package bytes changed: ${relative}`);
  }
  const runtimePrefix = `runtime/${expectedIdentity.runtime_hash}/`;
  const runtimeModule = `${runtimePrefix}audio-preview-runtime.mjs`;
  requireValue(records.has(runtimeModule), 'Audited package omits audio-preview-runtime.mjs');
  requireValue(records.has(`${runtimePrefix}runtime-diagnostics.mjs`),
    'Audited package omits runtime-diagnostics.mjs');
  const index = await fs.readFile(path.join(site, 'index.html'), 'utf8');
  requireValue(JSON.stringify(parseDiagnosticMeta(index, 'packaged index')) === JSON.stringify(expectedIdentity),
    'Packaged HTML identity differs from the audited manifest identity');
  return {runtimePrefix, runtimeModule};
}

function fixtureMarkup(runtimeModule, identity) {
  const moduleUrl = `/${runtimeModule}`;
  const identityJson = JSON.stringify(identity);
  const script = `
import {mountMeleeRuntime} from ${JSON.stringify(moduleUrl)};
const identity = ${identityJson};
const enabled = new URL(location.href).searchParams.get('enabled') !== '0';
const fixture = globalThis.__runtimeDiagnosticsFixture = {
  identity, enabled, ready: false, load: {state: 'booting'},
  errors: [], states: [], samples: [], triggerCosts: [], owner: null, player: null,
};
const sample = globalThis.menuDiagnosticSample;
let previousSampleAt = null;
fixture.resetSamples = () => { fixture.samples.length = 0; previousSampleAt = null; };
function recordSample(args, duration) {
  const at = performance.now();
  const sourceFrame = Number.isFinite(Number(args[1])) ? Number(args[1]) : null;
  const row = {
    at, gap_ms: previousSampleAt === null ? null : Math.max(0, at - previousSampleAt),
    callback_ms: duration, source_frame: sourceFrame,
    debt_ticks: Number(args[8]) || 0, update_ms: Number(args[9]) || 0,
    draw_ms: Number(args[10]) || 0, total_ms: Number(args[11]) || 0,
    preparation_ms: Number(args[12]) || 0, source_steps: Number(args[16]) || 0,
    source_draws: Number(args[17]) || 0, running: args[18] === true || args[18] === 1,
  };
  previousSampleAt = at;
  if (fixture.samples.length < 5000) fixture.samples.push(row);
}
function exposeOwner(owner) {
  fixture.owner = owner;
  const original = globalThis.menuDiagnosticSample;
  globalThis.menuDiagnosticSample = (...args) => {
    const started = performance.now();
    let result;
    try { result = original?.(...args); }
    finally { recordSample(args, Math.max(0, performance.now() - started)); }
    return result;
  };
  owner.callbacks.menuDiagnosticSample = globalThis.menuDiagnosticSample;
  const originalIncident = globalThis.menuDiagnosticIncident;
  globalThis.menuDiagnosticIncident = (...args) => {
    const started = performance.now();
    try { return originalIncident?.(...args); }
    finally {
      if (fixture.triggerCosts.length < 32) fixture.triggerCosts.push({reason: args[0],
        elapsed_ms: performance.now() - started});
    }
  };
  owner.callbacks.menuDiagnosticIncident = globalThis.menuDiagnosticIncident;
}
function exposeState(next) {
  fixture.load.last = next;
  if (fixture.states.length < 512) fixture.states.push({at: performance.now(), ...next});
}
try {
  fixture.player = await mountMeleeRuntime({
    canvas: document.getElementById('canvas'),
    diagnosticIdentity: identity,
    recordDiagnostics: enabled,
    onOwner: exposeOwner,
    onState: exposeState,
    onError: error => fixture.errors.push(String(error?.message || error)),
  });
  fixture.ready = true;
  fixture.load = {state: 'ready'};
} catch (error) {
  fixture.load = {state: 'error', message: String(error?.message || error)};
  fixture.errors.push(fixture.load.message);
}
const input = document.getElementById('disc-file');
document.getElementById('choose-disc').onclick = () => {
  try { void fixture.player?.activateAudio(); } catch (error) { fixture.errors.push(String(error)); }
  input.click();
};
input.onchange = async () => {
  const file = input.files?.[0];
  if (!file || !fixture.player) return;
  fixture.load = {state: 'configuring'};
  try {
    await fixture.player.configureSaveProfile('everything');
    fixture.load = {state: 'importing', name: file.name};
    await fixture.player.importDisc(file);
    fixture.load = {state: 'starting'};
    await fixture.player.start();
    fixture.load = {state: 'started'};
  } catch (error) {
    fixture.load = {state: 'error', message: String(error?.message || error)};
    fixture.errors.push(fixture.load.message);
  }
};
`;
  return `<!doctype html><html><head><meta charset="utf-8"><title>Local runtime diagnostics fixture</title></head>
<body><button id="choose-disc" type="button">Choose local disc</button>
<input id="disc-file" type="file" accept=".iso,.gcm,.ciso" hidden>
<canvas id="canvas" width="640" height="480" tabindex="0"></canvas>
<script type="module">${script}</script></body></html>`;
}

function contentType(file) {
  return MIME_TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
}

async function createServer(site, fixtureHtml) {
  const requests = [];
  const server = http.createServer(async (request, response) => {
    const parsed = new URL(request.url || '/', 'http://127.0.0.1');
    const requestRecord = {method: request.method || 'GET', path: parsed.pathname};
    requests.push(requestRecord);
    const fixturePath = '/__runtime-diagnostics-fixture/';
    if (parsed.pathname === fixturePath) {
      if (request.method !== 'GET' && request.method !== 'HEAD') {
        response.writeHead(405, {'Content-Type': 'text/plain; charset=utf-8'}); response.end(); return;
      }
      response.writeHead(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Cache-Control': 'no-store',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
      });
      response.end(request.method === 'HEAD' ? undefined : fixtureHtml);
      return;
    }
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      response.writeHead(405, {'Content-Type': 'text/plain; charset=utf-8'}); response.end(); return;
    }
    let relative;
    try {
      relative = decodeURIComponent(parsed.pathname).replace(/^\/+/, '') || 'index.html';
    } catch {
      response.writeHead(400); response.end(); return;
    }
    if (relative === '') relative = 'index.html';
    const local = path.resolve(site, relative);
    if (!local.startsWith(`${path.resolve(site)}${path.sep}`) && local !== path.resolve(site)) {
      response.writeHead(404); response.end(); return;
    }
    let stat, bytes;
    try {
      stat = await fs.lstat(local);
      if (!stat.isFile() || stat.isSymbolicLink()) throw Error('not a regular file');
      bytes = await fs.readFile(local);
    } catch {
      response.writeHead(404, {
        'Content-Type': 'text/plain; charset=utf-8',
        'X-Robots-Tag': 'noindex, nofollow, noarchive',
      });
      response.end(request.method === 'HEAD' ? undefined : 'Not found');
      return;
    }
    const isRuntime = relative.startsWith('runtime/');
    const headers = {
      'Content-Type': contentType(local),
      'Content-Length': bytes.byteLength,
      'Cache-Control': isRuntime ? 'public, max-age=31536000, immutable' : 'no-store',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'same-origin',
    };
    if (relative.endsWith('.html') || relative === 'robots.txt') headers['X-Robots-Tag'] = 'noindex, nofollow, noarchive';
    response.writeHead(200, headers);
    response.end(request.method === 'HEAD' ? undefined : bytes);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  requireValue(address && typeof address === 'object', 'Local HTTP server did not expose an address');
  return {
    server, requests,
    origin: `http://127.0.0.1:${address.port}`,
    async close() { await new Promise(resolve => server.close(() => resolve())); },
  };
}

function assertSafeReport(report, identity, label) {
  requireValue(report && typeof report === 'object', `${label} did not export a report`);
  requireValue(Object.keys(report).every(key => SAFE_REPORT_KEYS.has(key)), `${label} exported an unsafe root field`);
  requireValue(report.schema === 'melee-web-runtime-diagnostics' && report.version === 1,
    `${label} exported an unexpected diagnostics schema`);
  requireValue(JSON.stringify(report.identity) === JSON.stringify(identity), `${label} identity changed`);
  requireValue(report.environment?.kind === 'local' &&
    (report.environment.host === '127.0.0.1' || report.environment.host === 'localhost'),
  `${label} did not report the actual local environment`);
  requireValue(report.client && typeof report.client.platform === 'string' &&
    typeof report.client.browser === 'string', `${label} omitted coarse client identity`);
  requireValue(Array.isArray(report.incidents), `${label} incidents are not bounded array data`);
  const encoded = Buffer.byteLength(JSON.stringify(report));
  requireValue(encoded <= report.limits.max_report_bytes, `${label} exceeded max report bytes`);
  const serialized = JSON.stringify(report);
  for (const forbidden of ['/Users/', '/Volumes/', '/private/var/', 'release-manifest',
    'operator', 'contact', 'sendBeacon', '/upload', '__melee_evidence']) {
    requireValue(!serialized.includes(forbidden), `${label} leaked ${forbidden}`);
  }
  return {report_bytes: encoded, serialized};
}

function summarizeSamples(samples) {
  requireValue(Array.isArray(samples) && samples.length > 0, 'No native diagnostic callbacks were observed');
  const finiteValues = key => samples.map(item => item[key]).filter(value => typeof value === 'number' && Number.isFinite(value));
  const max = key => { const values = finiteValues(key); return values.length ? Math.max(...values) : null; };
  const gaps = finiteValues('gap_ms');
  const costs = finiteValues('callback_ms').sort((a, b) => a - b);
  const first = samples[0], last = samples[samples.length - 1];
  return {
    callback_count: samples.length,
    worst_callback_ms: max('callback_ms'),
    median_callback_ms: costs[Math.floor(costs.length / 2)],
    p95_callback_ms: costs[Math.floor((costs.length - 1) * 0.95)],
    worst_callback_gap_ms: gaps.length ? Math.max(...gaps) : null,
    max_update_ms: max('update_ms'),
    max_draw_ms: max('draw_ms'),
    max_total_ms: max('total_ms'),
    max_preparation_ms: max('preparation_ms'),
    max_debt_ticks: max('debt_ticks'),
    first_source_frame: first.source_frame,
    last_source_frame: last.source_frame,
    max_source_steps: max('source_steps'),
    max_source_draws: max('source_draws'),
    running_false_callbacks: samples.filter(item => !item.running).length,
  };
}

async function fixtureState(page) {
  return page.evaluate(() => {
    const fixture = globalThis.__runtimeDiagnosticsFixture;
    const state = fixture?.player?.getState?.() || null;
    return {
      ready: fixture?.ready === true,
      load: fixture?.load || null,
      state,
      errors: fixture?.errors || [],
      samples: fixture?.samples || [],
      trigger_costs: fixture?.triggerCosts || [],
      report: fixture?.owner?.diagnostics?.exportReports?.() || null,
      constants: fixture?.owner?.diagnostics?.constants || null,
      memory: performance.memory ? {
        used_js_heap_bytes: performance.memory.usedJSHeapSize,
        total_js_heap_bytes: performance.memory.totalJSHeapSize,
        js_heap_limit_bytes: performance.memory.jsHeapSizeLimit,
      } : null,
    };
  });
}

async function waitForCss(page, timeout = 90000) {
  await page.waitForFunction(() => {
    const fixture = globalThis.__runtimeDiagnosticsFixture;
    const state = fixture?.player?.getState?.();
    return fixture?.load?.state === 'error' || state?.requiresReload || fixture?.errors?.length ||
      fixture?.ready === true && fixture?.load?.state === 'started' &&
      state?.scene === 'css' && state?.phase === 1 && state?.running === true;
  }, null, {timeout});
  const current = await fixtureState(page);
  requireValue(current.errors.length === 0, `Fixture reported an error: ${current.errors.join('; ')}`);
  requireValue(current.state?.scene === 'css' && current.state?.running === true,
    'Fixture did not reach running original CSS');
}

async function assertStillCss(page, label) {
  const state = await fixtureState(page);
  requireValue(state.errors.length === 0, `${label} reported an error: ${state.errors.join('; ')}`);
  requireValue(state.state?.scene === 'css' && state.state?.phase === 1 && state.state?.running === true,
    `${label} encountered an unplanned pause or scene transition`);
}

async function selectDiscAndStart(page, disc, timeout = 90000) {
  await page.waitForFunction(() => {
    const fixture = globalThis.__runtimeDiagnosticsFixture;
    return fixture?.player?.getState?.().canImport || fixture?.load?.state === 'error';
  }, null, {timeout});
  const chooser = page.waitForEvent('filechooser', {timeout});
  await page.locator('#choose-disc').click({timeout});
  const fileChooser = await chooser;
  await fileChooser.setFiles(disc, {timeout});
  await waitForCss(page, timeout);
}

async function readReport(page, identity, label) {
  const value = await fixtureState(page);
  requireValue(value.report, `${label} has no diagnostics recorder`);
  const safe = assertSafeReport(value.report, identity, label);
  const samples = summarizeSamples(value.samples);
  const constants = value.constants;
  requireValue(constants && Number.isInteger(constants.max_report_bytes) &&
    Number.isInteger(constants.max_storage_bytes), `${label} omitted recorder bounds`);
  return {
    report: value.report,
    constants,
    report_bytes: safe.report_bytes,
    samples,
    memory: value.memory,
    trigger_costs: value.trigger_costs,
  };
}

async function measurePersistence(page, identity, label) {
  const result = await page.evaluate(async () => {
    const diagnostics = globalThis.__runtimeDiagnosticsFixture?.owner?.diagnostics;
    if (!diagnostics) return {persisted: false, reason: 'disabled'};
    const started = performance.now();
    const persisted = await diagnostics.persist();
    const elapsed = performance.now() - started;
    const serializationStarted = performance.now();
    const report = diagnostics.exportReports();
    const serialized = JSON.stringify(report);
    const serializationElapsed = performance.now() - serializationStarted;
    const database = await new Promise((resolve, reject) => {
      const request = indexedDB.open('melee-web-runtime-diagnostics', 1);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || Error('indexedDB open failed'));
    });
    const records = await new Promise((resolve, reject) => {
      const request = database.transaction('incidents', 'readonly').objectStore('incidents').getAll();
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error || Error('indexedDB read failed'));
    });
    database.close();
    const encoded = records.map(record => new TextEncoder().encode(JSON.stringify(record)).byteLength);
    return {persisted, elapsed_ms: elapsed, serialization_ms: serializationElapsed,
      export_bytes: new TextEncoder().encode(serialized).byteLength,
      records, bytes: encoded.reduce((sum, size) => sum + size, 0), report};
  });
  requireValue(result.persisted?.persisted === true, `${label} diagnostics did not persist`);
  requireValue(Array.isArray(result.records), `${label} IndexedDB records are invalid`);
  requireValue(result.bytes <= result.report.limits.max_storage_bytes,
    `${label} IndexedDB data exceeded max storage bytes`);
  for (const record of result.records) {
    requireValue(record.schema === 'melee-web-runtime-diagnostics' && record.version === 1,
      `${label} IndexedDB schema mismatch`);
    requireValue(JSON.stringify(record.identity) === JSON.stringify(identity), `${label} IndexedDB identity mismatch`);
    requireValue(record.incident && typeof record.incident.id === 'string', `${label} IndexedDB incident shape mismatch`);
  }
  return {
    persisted: result.persisted,
    elapsed_ms: result.elapsed_ms,
    serialization_ms: result.serialization_ms,
    export_bytes: result.export_bytes,
    record_count: result.records.length,
    bytes: result.bytes,
  };
}

async function retainPage(page, label) {
  const out = path.resolve(values.out);
  try { await page.screenshot({path: path.join(out, `${label}.png`)}); } catch {}
  try { await fs.writeFile(path.join(out, `${label}.json`), JSON.stringify(await fixtureState(page), null, 2), {flag: 'wx'}); } catch {}
}

async function runCollection(browser, url, disc, identity, enabled, timeout, label) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  try {
    const response = await page.goto(`${url}?enabled=${enabled ? '1' : '0'}`, {timeout});
    requireValue(response?.status() === 200, `Diagnostics fixture returned HTTP ${response?.status()}`);
    requireValue(await page.evaluate(() => crossOriginIsolated === true && !!navigator.gpu),
      'Diagnostics fixture lacks cross-origin isolation or WebGPU');
    await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture?.ready === true ||
      globalThis.__runtimeDiagnosticsFixture?.load?.state === 'error', null, {timeout});
    const initial = await fixtureState(page);
    requireValue(initial.errors.length === 0, `Diagnostics fixture startup failed: ${initial.errors.join('; ')}`);
    await selectDiscAndStart(page, disc, timeout);
    await page.evaluate(() => globalThis.__runtimeDiagnosticsFixture.resetSamples());
    const deadline = Date.now() + COLLECTION_TIMEOUT_MS;
    while (Date.now() < deadline) {
      // Poll scalar readiness only. Exporting the history here would measure
      // the test's repeated serialization rather than normal recording.
      const state = await page.evaluate(() => {
        const fixture = globalThis.__runtimeDiagnosticsFixture;
        return {state: fixture.player.getState(), errors: fixture.errors,
          callback_count: fixture.samples.length};
      });
      requireValue(state.errors.length === 0, `CSS collection failed: ${state.errors.join('; ')}`);
      if (state.state?.scene !== 'css' || state.state?.running !== true)
        fail('CSS collection encountered a natural pause or scene transition');
      if (state.callback_count >= COLLECTION_CALLBACKS) break;
      await page.waitForTimeout(100);
    }
    await assertStillCss(page, `${enabled ? 'enabled' : 'disabled'} collection`);
    const value = await fixtureState(page);
    await page.screenshot({path: path.join(path.resolve(values.out), `${label}.png`)});
    requireValue(value.samples.length >= COLLECTION_CALLBACKS,
      `CSS collection stopped before ${COLLECTION_CALLBACKS} callbacks`);
    if (!enabled) {
      requireValue(value.report === null, 'Disabled recorder unexpectedly exported diagnostics');
      return {enabled: false, samples: summarizeSamples(value.samples), memory: value.memory};
    }
    const result = await readReport(page, identity, 'Enabled collection');
    return {enabled: true, ...result};
  } catch (error) {
    await retainPage(page, `${label}-failure`);
    throw error;
  } finally {
    await page.close();
  }
}

async function runSimulationStall(browser, url, disc, identity, timeout) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  try {
    await page.goto(`${url}?enabled=1`, {timeout});
    await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture?.ready === true, null, {timeout});
    await selectDiscAndStart(page, disc, timeout);
    await assertStillCss(page, 'Simulation stall precondition');
    await page.evaluate(() => { const until = performance.now() + 200; while (performance.now() < until) {} });
    await page.waitForFunction(() => {
      const report = globalThis.__runtimeDiagnosticsFixture?.owner?.diagnostics?.exportReports?.();
      return report?.incidents?.some(item => item.reason === 'simulation_debt' &&
        item.value > 8 && item.threshold === 8);
    }, null, {timeout: STALL_TIMEOUT_MS});
    const before = await readReport(page, identity, 'Simulation debt');
    const debt = before.report.incidents.find(item => item.reason === SIMULATION_DEBT_REASON);
    requireValue(debt && debt.value > SIMULATION_DEBT_THRESHOLD && debt.threshold === SIMULATION_DEBT_THRESHOLD,
      'Simulation debt incident did not retain the native value and threshold');
    const beforeRecovery = await page.evaluate(() => globalThis.__runtimeDiagnosticsFixture.player.getState());
    requireValue(beforeRecovery.running === false, 'Simulation guard did not pause the native CSS owner');
    await page.evaluate(() => globalThis.__runtimeDiagnosticsFixture.player.resume());
    await page.waitForFunction(() => {
      const fixture = globalThis.__runtimeDiagnosticsFixture;
      const report = fixture?.owner?.diagnostics?.exportReports?.();
      const debt = report?.incidents?.find(item => item.reason === 'simulation_debt');
      const state = fixture?.player?.getState?.();
      return debt?.closed === true && debt.recovery === 'resume' && state?.scene === 'css' && state?.running === true;
    }, null, {timeout: STALL_TIMEOUT_MS});
    const after = await readReport(page, identity, 'Simulation recovery');
    const persistence = await measurePersistence(page, identity, 'Simulation recovery');
    return {incident: debt, before: before.samples, after: after.samples, report: after.report,
      report_bytes: after.report_bytes, memory: after.memory, persistence};
  } catch (error) {
    await retainPage(page, 'simulation-stall-failure');
    throw error;
  } finally {
    await page.close();
  }
}

async function runAudioStall(browser, url, disc, identity, timeout) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  try {
    await page.goto(`${url}?enabled=1`, {timeout});
    await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture?.ready === true, null, {timeout});
    await selectDiscAndStart(page, disc, timeout);
    await assertStillCss(page, 'Audio stall precondition');
    await page.evaluate(() => { const until = performance.now() + 1100; while (performance.now() < until) {} });
    await page.waitForFunction(() => {
      const report = globalThis.__runtimeDiagnosticsFixture?.owner?.diagnostics?.exportReports?.();
      return report?.incidents?.some(item => item.reason === 'audio_debt' && item.threshold === 60);
    }, null, {timeout: STALL_TIMEOUT_MS});
    const result = await readReport(page, identity, 'Audio debt');
    const debt = result.report.incidents.find(item => item.reason === AUDIO_DEBT_REASON);
    requireValue(debt && debt.threshold === AUDIO_DEBT_THRESHOLD && debt.value > AUDIO_DEBT_THRESHOLD,
      'Audio debt incident did not retain the native value above threshold');
    const state = await page.evaluate(() => globalThis.__runtimeDiagnosticsFixture.player.getState());
    requireValue(state.running === false, 'Audio guard did not pause the native CSS owner');
    const persistence = await measurePersistence(page, identity, 'Audio debt');
    return {incident: debt, report: result.report, report_bytes: result.report_bytes,
      memory: result.memory, samples: result.samples, persistence};
  } catch (error) {
    await retainPage(page, 'audio-stall-failure');
    throw error;
  } finally {
    await page.close();
  }
}

async function runSupportedFailure(browser, url, disc, identity, timeout) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  try {
    await page.goto(`${url}?enabled=1`, {timeout});
    await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture?.ready === true, null, {timeout});
    await selectDiscAndStart(page, disc, timeout);
    await assertStillCss(page, 'Runtime failure precondition');
    await page.evaluate(() => window.dispatchEvent(new ErrorEvent('error', {
      message: 'Synthetic private /synthetic-private/tester/secret.gci https://private.invalid/?token=secret',
    })));
    const result = await readReport(page, identity, 'Supported browser failure');
    requireValue(result.report.incidents.some(incident => incident.reason === 'runtime_failure'),
      'Supported error did not create a generic runtime failure incident');
    requireValue(!JSON.stringify(result.report).includes('private.invalid') &&
      !JSON.stringify(result.report).includes('secret.gci'), 'Exception text crossed the report boundary');
    const state = await page.evaluate(() => globalThis.__runtimeDiagnosticsFixture.player.getState());
    requireValue(state.requiresReload && !state.running, 'Error was hidden behind successful player state');
    return {report: result.report, trigger_costs: result.trigger_costs,
      report_bytes: result.report_bytes, persistence: await measurePersistence(page, identity, 'Runtime failure')};
  } catch (error) {
    await retainPage(page, 'runtime-failure-check-failure');
    throw error;
  } finally { await page.close(); }
}

const {values} = parseArgs({options: {
  site: {type: 'string'}, manifest: {type: 'string'}, disc: {type: 'string'},
  playwright: {type: 'string'}, out: {type: 'string'}, help: {type: 'boolean'},
  'startup-only': {type: 'boolean'},
}});
if (values.help) {
  console.log('Usage: node tests/runtime_diagnostics_browser_test.mjs --site AUDITED_AUDIO_PLAYER --manifest MANIFEST --disc OWNED_ISO --out FRESH_REPORT_DIR [--playwright PLAYWRIGHT_DIR]');
  process.exit(0);
}
for (const name of ['site', 'manifest', 'disc', 'out']) requireValue(values[name], `--${name} is required`);

const report = {
  schema: 'melee-web-runtime-diagnostics-browser-v1',
  scope: 'Headless local HTTP recorder overhead and induced-stall detection; same-build CSS in balanced enabled/disabled order. Shared host; no quiet-machine, foreground timing, pixel, PCM, or sustained gameplay admission claim.',
  result: 'fail', checks: [], started_at: new Date().toISOString(),
};
let browser;
let context;
let localServer;
try {
  const site = path.resolve(values.site);
  const manifestPath = path.resolve(values.manifest);
  const disc = path.resolve(values.disc);
  const out = path.resolve(values.out);
  await fs.mkdir(path.dirname(out), {recursive: true});
  await fs.mkdir(out, {recursive: false});
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const expectedIdentity = parseIdentity({
    schema_version: 1, source_commit: manifest.source_sha,
    runtime_hash: manifest.runtime_hash, build_profile: manifest.profile,
  }, 'manifest safe identity');
  const packageInfo = await verifyAuditedPackage(site, manifest, expectedIdentity);
  report.identity = expectedIdentity;
  report.checks.push('audited package inventory and HTML identity verified');
  const fixtureHtml = fixtureMarkup(packageInfo.runtimeModule, expectedIdentity);
  localServer = await createServer(site, fixtureHtml);
  report.origin = localServer.origin;
  const {chromium, browser: launchOptions} = await loadBrowserTools(values.playwright);
  browser = await chromium.launch(browserLaunchOptions(launchOptions, {
    headed: false, audible: false, timeout: 120000,
  }));
  report.browser = browser.version();
  report.browser_mode = 'headless';
  report.audio_output = 'muted by shared browser launch policy';
  context = await browser.newContext();
  const fixtureUrl = `${localServer.origin}/__runtime-diagnostics-fixture/`;
  report.collection = [];
  if (values['startup-only']) {
    const page = await context.newPage({viewport: {width: 1280, height: 960}});
    try {
      await page.goto(`${fixtureUrl}?enabled=1`, {timeout: 120000});
      await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture?.ready === true ||
        globalThis.__runtimeDiagnosticsFixture?.load?.state === 'error', null, {timeout: 120000});
      await selectDiscAndStart(page, disc, 120000);
      await page.waitForFunction(() => globalThis.__runtimeDiagnosticsFixture.samples.length >= 10 ||
        globalThis.__runtimeDiagnosticsFixture.errors.length, null, {timeout: 10000});
      await assertStillCss(page, 'Reduced startup boundary');
      const prefix = await fixtureState(page);
      requireValue(prefix.samples.length >= 10 && prefix.report?.native?.callback_count >= 10 &&
        prefix.report.capabilities.native.observed, 'Native scalar feed did not cross the compiled browser bridge');
      await retainPage(page, 'startup-boundary');
      report.checks.push('reduced fixture startup reaches original CSS');
      report.scope = 'Reduced packaged fixture startup/readiness/save ownership boundary only; no overhead or induced-stall claim.';
      report.result = 'pass';
    } catch (error) { await retainPage(page, 'startup-boundary-failure'); throw error; }
    finally { await page.close(); }
  } else {
  for (const [index, enabled] of [true, false, false, true].entries()) {
    const label = `css-${index + 1}-${enabled ? 'enabled' : 'disabled'}`;
    report.collection.push(await runCollection(context, fixtureUrl, disc, expectedIdentity, enabled, 120000, label));
  }
  report.checks.push('bounded CSS callback collection completed with recorder enabled and disabled');
  report.simulation_stall = await runSimulationStall(context, fixtureUrl, disc, expectedIdentity, 120000);
  report.checks.push('simulation debt incident retained native value and manual recovery');
  report.audio_stall = await runAudioStall(context, fixtureUrl, disc, expectedIdentity, 120000);
  report.checks.push('audio debt incident retained native value on a fresh page');
  report.supported_failure = await runSupportedFailure(context, fixtureUrl, disc, expectedIdentity, 120000);
  report.checks.push('supported runtime error captured without arbitrary exception text');
  requireValue(report.audio_stall.persistence.record_count >= 2,
    'IndexedDB did not preserve the previous page incident');
  requireValue(report.supported_failure.persistence.record_count >= 3 &&
    report.supported_failure.persistence.record_count <= 4, 'Cross-page retention exceeded bounds or lost incidents');
  report.checks.push('isolated IndexedDB retained bounded incidents across fresh pages');
  const forbiddenRequests = localServer.requests.filter(request => request.method !== 'GET' && request.method !== 'HEAD' ||
    /(?:upload|evidence|manifest|__melee_evidence)/i.test(request.path));
  requireValue(forbiddenRequests.length === 0,
    `Application issued forbidden network requests: ${JSON.stringify(forbiddenRequests)}`);
  report.network = {request_count: localServer.requests.length, application_uploads: 0};
  report.result = 'pass';
  }
} catch (error) {
  report.failure = String(error?.stack || error?.message || error);
  process.exitCode = 1;
} finally {
  try { await context?.close(); } catch (error) { report.cleanup_error = String(error); process.exitCode = 1; }
  try { await browser?.close(); } catch (error) { report.cleanup_error = String(error); process.exitCode = 1; }
  try { await localServer?.close(); } catch (error) { report.cleanup_error = String(error); process.exitCode = 1; }
  report.finished_at = new Date().toISOString();
  if (values.out) {
    const output = path.resolve(values.out, 'report.json');
    try { await fs.writeFile(output, `${JSON.stringify(report, null, 2)}\n`, {flag: 'wx'}); report.report = output; }
    catch (error) { report.write_error = String(error); process.exitCode = 1; }
  }
}
console.log(JSON.stringify({result: report.result, checks: report.checks,
  ...(report.failure ? {failure: report.failure} : {}), report: report.report || null}));
