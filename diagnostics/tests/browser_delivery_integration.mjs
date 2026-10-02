#!/usr/bin/env node
/*
 * Bounded browser -> local Pages/D1 delivery integration.
 *
 * This test is intentionally opt-in: without --run-browser it only prints
 * help and never launches Chrome. With the flag, it stages the real browser
 * recorder, delivery adapter, preference settings, schema, Pages Functions,
 * and D1 migration into a retained local fixture. A CONNECT proxy accepts
 * only the known staging/production hosts and forwards them to local Pages;
 * no arbitrary or hosted network target is permitted.
 */

import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import https from 'node:https';
import net from 'node:net';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {promisify} from 'node:util';

import {browserLaunchOptions, loadBrowserTools} from '../../scripts/browser_tools.mjs';

const execFileAsync = promisify(execFile);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SOURCE = path.join(ROOT, 'diagnostics');
const WEB = path.join(ROOT, 'web');
const WORK_ROOT = path.join(ROOT, 'work', 'diagnostics');
const WRANGLER = process.env.WRANGLER_BIN || path.join(ROOT, '.tools/wrangler/node_modules/.bin/wrangler');
const WRANGLER_VERSION = JSON.parse(await fs.readFile(path.join(ROOT, 'dependencies.lock.json'), 'utf8')).deployment_tools.wrangler.version;
const ADMIN_TOKEN = 'browser-local-admin-token-0123456789';
const SOURCE_COMMIT = '0123456789abcdef0123456789abcdef01234567';
const RUNTIME_HASH = '0123456789abcdef';
const ALLOWED_PROXY_HOSTS = new Set(['staging.webmelee.gg', 'www.webmelee.gg']);
const RELEASES = JSON.stringify({
  staging: [{source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player'}],
  production: [{source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player'}],
},);

const args = parseArgs({options: {
  'run-browser': {type: 'boolean', default: false},
  out: {type: 'string'},
  playwright: {type: 'string'},
  site: {type: 'string'},
  manifest: {type: 'string'},
  disc: {type: 'string'},
  timeout: {type: 'string', default: '45000'},
  help: {type: 'boolean', short: 'h', default: false},
}}).values;

function usage() {
  return `Usage: python3 scripts/agent_workspace.py run -- node diagnostics/tests/browser_delivery_integration.mjs --run-browser [--out DIR] [--playwright DIR] [--site DIR --manifest FILE --disc FILE]`;
}

if (args.help || !args['run-browser']) {
  console.log(usage());
  if (!args.help) console.log('No browser was launched; pass --run-browser only after the browser lane is authorized.');
  process.exit(0);
}

const timeoutMs = Number(args.timeout);
assert(Number.isInteger(timeoutMs) && timeoutMs >= 5000 && timeoutMs <= 120000, '--timeout must be 5000..120000');
const nativeArgs = [args.site, args.manifest, args.disc].filter(value => value !== undefined);
assert(nativeArgs.length === 0 || nativeArgs.length === 3,
  'native packaged mode requires --site, --manifest and --disc together');
await fs.access(WRANGLER);
const versionResult = await execFileAsync(WRANGLER, ['--version'], {timeout: 20000});
assert.equal(versionResult.stdout.trim().split('\n').at(-1), WRANGLER_VERSION, 'use the exact locked Wrangler version');
await fs.mkdir(WORK_ROOT, {recursive: true});
const fixture = await fs.mkdtemp(path.join(WORK_ROOT, 'browser-delivery-'));
const evidence = path.resolve(args.out || path.join(fixture, 'evidence'));
await fs.mkdir(evidence, {recursive: true});

const sha256 = async file => crypto.createHash('sha256').update(await fs.readFile(file)).digest('hex');

const HEX40 = /^[0-9a-f]{40}$/;
const HEX16 = /^[0-9a-f]{16}$/;
const HEX64 = /^[0-9a-f]{64}$/;

async function regularPath(file, label) {
  const info = await fs.lstat(file);
  assert(!info.isSymbolicLink() && info.isFile(), `${label} must be a regular file`);
  return file;
}

async function packageFiles(root, prefix = '') {
  const output = [];
  const entries = await fs.readdir(root, {withFileTypes: true});
  for (const entry of entries) {
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
    const full = path.join(root, entry.name);
    assert(!entry.isSymbolicLink(), `packaged site may not contain symlinks: ${relative}`);
    if (entry.isDirectory()) output.push(...await packageFiles(full, relative));
    else if (entry.isFile()) output.push({relative, full});
    else throw Error(`packaged site contains unsupported entry: ${relative}`);
  }
  return output;
}

async function loadNativePackage() {
  const site = path.resolve(args.site);
  const manifestPath = path.resolve(args.manifest);
  const disc = path.resolve(args.disc);
  const siteInfo = await fs.lstat(site);
  assert(!siteInfo.isSymbolicLink() && siteInfo.isDirectory(), '--site must be a regular directory');
  await regularPath(manifestPath, '--manifest');
  await regularPath(disc, '--disc');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  assert(manifest && typeof manifest === 'object' && !Array.isArray(manifest), 'audio manifest must be an object');
  assert.equal(manifest.profile, 'audio-player', 'native mode requires an audited audio-player package');
  assert.equal(manifest.project, 'webmelee', 'native mode requires the production release identity');
  assert(HEX40.test(manifest.source_sha), 'manifest source_sha must be 40 lowercase hex characters');
  assert(HEX16.test(manifest.runtime_hash), 'manifest runtime_hash must be 16 lowercase hex characters');
  assert(HEX64.test(manifest.identity_sha256), 'manifest identity_sha256 must be 64 lowercase hex characters');
  assert(Array.isArray(manifest.files) && manifest.files.length > 0, 'audio manifest files are required');
  const inventory = new Map();
  for (const record of manifest.files) {
    assert(record && typeof record === 'object' && !Array.isArray(record), 'invalid audio manifest record');
    assert(typeof record.path === 'string' && record.path === path.posix.normalize(record.path) &&
      !record.path.startsWith('/') && !record.path.startsWith('../') && !record.path.includes('/../') &&
      !record.path.includes('\\') && !record.path.includes(':'),
    `unsafe audio manifest path: ${record.path}`);
    assert(Number.isSafeInteger(record.size) && record.size >= 0, `invalid audio manifest size: ${record.path}`);
    assert(HEX64.test(record.sha256), `invalid audio manifest hash: ${record.path}`);
    assert(!inventory.has(record.path), `duplicate audio manifest path: ${record.path}`);
    inventory.set(record.path, record);
  }
  const actualFiles = await packageFiles(site);
  assert.equal(actualFiles.length, inventory.size, 'packaged site file inventory differs from manifest');
  for (const file of actualFiles) {
    const record = inventory.get(file.relative);
    assert(record, `packaged site contains an unlisted file: ${file.relative}`);
    const stat = await fs.stat(file.full);
    assert.equal(stat.size, record.size, `packaged site size differs: ${file.relative}`);
    assert.equal(await sha256(file.full), record.sha256, `packaged site hash differs: ${file.relative}`);
  }
  const runtimePrefix = `runtime/${manifest.runtime_hash}/`;
  for (const required of [
    `${runtimePrefix}audio-preview-runtime.mjs`, `${runtimePrefix}melee-runtime.mjs`,
    `${runtimePrefix}gameplay_audio_preview.js`, `${runtimePrefix}gameplay_audio_preview.wasm`,
    `${runtimePrefix}gameplay_audio_preview.data`, 'index.html', '_headers',
  ]) assert(inventory.has(required), `audited package is missing ${required}`);
  const index = await fs.readFile(path.join(site, 'index.html'), 'utf8');
  const identityMatch = index.match(/<meta\s+id=["']runtime-diagnostic-identity["']\s+content=["']([^"']+)["'][^>]*>/i);
  assert(identityMatch, 'packaged index is missing its diagnostic identity');
  const identity = JSON.parse(identityMatch[1].replaceAll('&quot;', '"').replaceAll('&#x27;', "'"));
  assert.deepEqual(identity, {schema_version: 1, source_commit: manifest.source_sha,
    runtime_hash: manifest.runtime_hash, build_profile: manifest.profile});
  return {site, manifestPath, disc, manifest, identity, inventory, indexHash: await sha256(path.join(site, 'index.html'))};
}

async function copyTree(source, destination) {
  const entries = await fs.readdir(source, {withFileTypes: true});
  await fs.mkdir(destination, {recursive: true});
  for (const entry of entries) {
    const from = path.join(source, entry.name), to = path.join(destination, entry.name);
    assert(!entry.isSymbolicLink(), `cannot copy symlink from packaged site: ${entry.name}`);
    if (entry.isDirectory()) await copyTree(from, to);
    else if (entry.isFile()) await copy(from, to);
    else throw Error(`unsupported packaged site entry: ${entry.name}`);
  }
}

const nativePackage = nativeArgs.length ? await loadNativePackage() : null;

async function copy(source, destination) {
  await fs.mkdir(path.dirname(destination), {recursive: true});
  await fs.copyFile(source, destination);
}

function fixtureHtml() {
  const identity = JSON.stringify({schema_version: 1, source_commit: SOURCE_COMMIT,
    runtime_hash: RUNTIME_HASH, build_profile: 'player'});
  return `<!doctype html>
<meta charset="utf-8"><title>Diagnostics browser delivery fixture</title>
<label><input id="automatic-diagnostics" type="checkbox"> Automatic diagnostics</label>
<p id="diagnostics-description"></p>
<button id="export-diagnostics" type="button">Export diagnostics</button>
<p id="diagnostics-export-status"></p>
<script type="module">
import {createRuntimeDiagnostics} from './runtime-diagnostics.mjs';
import {createDiagnosticsDelivery} from './runtime-diagnostics-delivery.mjs';
import {DIAGNOSTICS_PREFERENCE_KEY, readDiagnosticsPreference, writeDiagnosticsPreference, mountDiagnosticsSettings} from './diagnostics-settings.mjs';

const identity = ${identity};
document.cookie = 'browser-fixture-secret=present; Path=/; SameSite=Lax';
const fixture = globalThis.__browserDeliveryFixture = {
  identity, requests: [], active: true, automatic: readDiagnosticsPreference(),
  lastLocal: null, lastDelivery: null, timings: null, player: null,
};
const originalFetch = globalThis.fetch.bind(globalThis);
const observedFetch = async (url, options = {}) => {
  const record = {url: String(url), method: options.method, headers: {...(options.headers || {})},
    credentials: options.credentials, mode: options.mode, redirect: options.redirect,
    cache: options.cache, referrerPolicy: options.referrerPolicy,
    bodyBytes: typeof options.body === 'string' ? new TextEncoder().encode(options.body).byteLength : null,
    cookiePresentOnPage: document.cookie.includes('browser-fixture-secret=present')};
  fixture.requests.push(record);
  return originalFetch(url, options);
};
const recorder = createRuntimeDiagnostics({
  globalThis, origin: location.origin, identity, audioAvailable: false,
  longtaskAvailable: false, historySeconds: 1, historyHz: 10, postWindowMs: 20,
});
const delivery = createDiagnosticsDelivery({
  globalThis, origin: location.origin, fetch: observedFetch, optOut: !fixture.automatic,
});
const player = {
  getDiagnosticsSettings() {
    return {eligible: delivery.getStatus().eligible,
      automatic: fixture.automatic};
  },
  setAutomaticDiagnostics(enabled) {
    fixture.automatic = enabled === true;
    writeDiagnosticsPreference(fixture.automatic);
    delivery.setOptOut(!fixture.automatic);
    return fixture.automatic;
  },
  getState() { return {running: false, busy: false}; },
  async exportDiagnostics() {
    return {current: recorder.exportReports(), retained: await recorder.exportRetained()};
  },
};
fixture.player = player;
const settings = mountDiagnosticsSettings({document, root: globalThis});
settings.bindPlayer(player);

function syntheticSample(at) {
  recorder.observeNative(at, 12, 7, 1, 1, 1, 0, 0, 9, 2, 1, 9, 0, 0, 0, 0, 1, 1, true);
}

fixture.seed = () => {
  syntheticSample(performance.now());
  recorder.lifecycle('scene_enter', {timestamp: performance.now(), source_frame: 12, scene: 'match'});
  const id = recorder.trigger('simulation_debt', 9, 8, 12, 'match', 1);
  return {id, active: fixture.active, queue: delivery.getStatus()};
};
fixture.queueCurrent = () => {
  const started = performance.now();
  const local = recorder.exportReports();
  const encoded = JSON.stringify(local);
  const serializedAt = performance.now();
  const queued = delivery.enqueue(local);
  fixture.lastLocal = local;
  fixture.timings = {serialization_ms: serializedAt - started, serialized_bytes: new TextEncoder().encode(encoded).byteLength};
  return {queued, active: fixture.active, requestCount: fixture.requests.length};
};
fixture.goInactiveAndFlush = async () => {
  const started = performance.now();
  fixture.active = false;
  await recorder.setActive(false);
  delivery.setActive(false);
  const result = await delivery.flushWhenInactive();
  fixture.timings.upload_await_ms = performance.now() - started;
  fixture.lastDelivery = result;
  settings.bindPlayer(player);
  return {result, status: delivery.getStatus(), requests: fixture.requests.slice(-4), timings: fixture.timings};
};
fixture.goActive = async () => {
  fixture.active = true;
  await recorder.setActive(true);
  delivery.setActive(true);
  settings.bindPlayer(player);
};
fixture.disableFromSettings = async () => {
  const toggle = document.getElementById('automatic-diagnostics');
  toggle.checked = false;
  toggle.dispatchEvent(new Event('change', {bubbles: true}));
  await new Promise(resolve => setTimeout(resolve, 20));
  return {automatic: fixture.automatic, status: delivery.getStatus(), preference: localStorage.getItem(DIAGNOSTICS_PREFERENCE_KEY)};
};
fixture.enableFromSettings = async () => {
  const toggle = document.getElementById('automatic-diagnostics');
  toggle.checked = true;
  toggle.dispatchEvent(new Event('change', {bubbles: true}));
  await new Promise(resolve => setTimeout(resolve, 20));
  return {automatic: fixture.automatic, status: delivery.getStatus(), preference: localStorage.getItem(DIAGNOSTICS_PREFERENCE_KEY)};
};
fixture.exportLocal = async () => player.exportDiagnostics();
fixture.offlineQueue = async () => {
  await fixture.goActive();
  fixture.seed();
  const queued = fixture.queueCurrent();
  delivery.setOnline(false);
  fixture.active = false;
  await recorder.setActive(false);
  delivery.setActive(false);
  const result = await delivery.flushWhenInactive();
  return {queued, result, status: delivery.getStatus(), requestCount: fixture.requests.length};
};
fixture.rejectedServerReport = async () => {
  await fixture.goActive();
  delivery.setOnline(true);
  fixture.seed();
  const local = structuredClone(recorder.exportReports());
  local.identity.runtime_hash = 'fedcba9876543210';
  const queued = delivery.enqueue(local);
  fixture.active = false;
  await recorder.setActive(false);
  delivery.setActive(false);
  const result = await delivery.flushWhenInactive();
  return {queued, result, status: delivery.getStatus(), request: fixture.requests.at(-1)};
};
fixture.retryCap = async () => {
  const retryRequests = [];
  let calls = 0;
  let retryNow = 0;
  const retryStorage = {values: [], tombstones: [], async load() { return this.values; },
    async save(values) { this.values = structuredClone(values); },
    async loadTombstones() { return this.tombstones; },
    async saveTombstones(values) { this.tombstones = structuredClone(values); }};
  const retryDelivery = createDiagnosticsDelivery({
    globalThis, origin: location.origin, storage: retryStorage, now: () => retryNow, retryBaseMs: 100000,
    random: () => 0.5,
    fetch: async (url, options) => {
      retryRequests.push({url: String(url), method: options.method});
      calls += 1;
      return {status: 503};
    },
  });
  retryDelivery.setOnline(true);
  retryDelivery.setActive(false);
  const local = recorder.exportReports();
  const queued = retryDelivery.enqueue({...local, incidents: local.incidents.slice(-1)});
  const first = await retryDelivery.flushWhenInactive();
  retryNow = 100000;
  const second = await retryDelivery.flushWhenInactive();
  retryNow = 300000;
  const third = await retryDelivery.flushWhenInactive();
  const result = {queued, first, second, third, calls, pending: retryDelivery.getStatus().queued, retryRequests};
  retryDelivery.dispose();
  return result;
};
fixture.ready = true;
</script>`;
}

async function stageFixture(packaged = null) {
  const publicRoot = path.join(fixture, 'public');
  if (packaged) {
    // The static package is copied byte-for-byte from the audited directory.
    // Functions are staged beside it, so test-only files never become public
    // package bytes or browser imports.
    await copyTree(packaged.site, publicRoot);
  } else {
    await fs.mkdir(publicRoot, {recursive: true});
    await fs.writeFile(path.join(publicRoot, 'index.html'), fixtureHtml());
    await fs.writeFile(path.join(publicRoot, '_routes.json'), await fs.readFile(path.join(SOURCE, '_routes.json')));
    await fs.writeFile(path.join(publicRoot, 'blocked.txt'), 'BROWSER-FIXTURE-STATIC');
  }
  await copy(path.join(SOURCE, 'pages-function-adapter.mjs'), path.join(fixture, 'functions/api/diagnostics.js'));
  await copy(path.join(SOURCE, 'pages-function-catchall-adapter.mjs'), path.join(fixture, 'functions/api/diagnostics/[[report]].js'));
  await copy(path.join(SOURCE, 'worker.mjs'), path.join(fixture, 'functions/api/worker.mjs'));
  await copy(path.join(SOURCE, 'schema.mjs'), path.join(fixture, 'functions/api/schema.mjs'));
  await fs.mkdir(path.join(fixture, 'migrations'), {recursive: true});
  await copy(path.join(SOURCE, 'migrations/0001_diagnostics.sql'), path.join(fixture, 'migrations/0001_diagnostics.sql'));
  if (!packaged) {
    for (const name of ['runtime-diagnostics.mjs', 'runtime-diagnostics-delivery.mjs', 'diagnostics-settings.mjs', 'diagnostics-schema.mjs']) {
      await copy(path.join(WEB, name), path.join(publicRoot, name));
    }
  }
  await fs.writeFile(path.join(fixture, 'wrangler.jsonc'), JSON.stringify({
    name: 'diag-browser-local', compatibility_date: '2026-09-18', pages_build_output_dir: './public',
    d1_databases: [{binding: 'DIAGNOSTICS_DB', database_name: 'diag-browser-local',
      database_id: '00000000-0000-0000-0000-000000000002', migrations_dir: './migrations'}],
  }, null, 2) + '\n');
  await execFileAsync(WRANGLER, ['--cwd', fixture, 'd1', 'migrations', 'apply', 'diag-browser-local', '--local', '--persist-to', 'state'], {timeout: 45000});
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      server.close(error => error ? reject(error) : resolve(port));
    });
  });
}

async function waitForPort(port, protocol = 'http') {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try {
      await new Promise((resolve, reject) => {
        const request = (protocol === 'https' ? https.request : http.request)({
          hostname: '127.0.0.1', port, path: '/', method: 'GET', rejectUnauthorized: false, timeout: 1000,
        }, response => { response.resume(); response.once('end', resolve); });
        request.once('error', reject); request.end();
      });
      return;
    } catch { await new Promise(resolve => setTimeout(resolve, 200)); }
  }
  throw new Error(`local server did not become ready on ${port}`);
}

function startConnectProxy(targetPort) {
  const server = http.createServer((_request, response) => { response.writeHead(404); response.end(); });
  server.on('connect', (request, client, head) => {
    const host = String(request.url || '').split(':')[0].toLowerCase();
    if (!ALLOWED_PROXY_HOSTS.has(host)) {
      client.write('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
      client.destroy();
      return;
    }
    const target = net.connect(targetPort, '127.0.0.1', () => {
      client.write('HTTP/1.1 200 Connection Established\r\n\r\n');
      if (head.length) target.write(head);
      target.pipe(client); client.pipe(target);
    });
    target.once('error', () => client.destroy());
    client.once('error', () => target.destroy());
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => resolve({server, port: server.address().port}));
  });
}

async function stopProcess(process) {
  if (!process || process.exitCode !== null) return;
  process.kill('SIGTERM');
  await new Promise(resolve => {
    const timer = setTimeout(() => { try { process.kill('SIGKILL'); } catch {} resolve(); }, 5000);
    process.once('exit', () => { clearTimeout(timer); resolve(); });
  });
}

async function startPages(releases = RELEASES) {
  const port = await freePort();
  const bindings = ['--binding', `DIAGNOSTICS_ADMIN_TOKEN=${ADMIN_TOKEN}`,
    '--binding', `DIAGNOSTICS_ALLOWED_RELEASES=${releases}`, '--binding', 'DIAGNOSTICS_RATE_LIMIT=60',
    '--binding', 'DIAGNOSTICS_DAILY_REPORT_CAP=1000', '--binding', 'DIAGNOSTICS_DAILY_BYTE_CAP=16777216'];
  const process = execFile(WRANGLER, ['--cwd', fixture, 'pages', 'dev', 'public', '--port', String(port),
    '--local-protocol', 'https', '--persist-to', 'state', ...bindings, '--log-level', 'error',
    '--show-interactive-dev-session', 'false']);
  process.stderr?.resume(); process.stdout?.resume();
  await waitForPort(port, 'https');
  return {process, port};
}

function requestAdmin(port, host, query = '') {
  return new Promise((resolve, reject) => {
    const request = https.request({hostname: '127.0.0.1', port,
      path: `/api/diagnostics${query}`, method: 'GET', rejectUnauthorized: false,
      headers: {Host: host, Authorization: `Bearer ${ADMIN_TOKEN}`}}, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => { try { resolve({status: response.statusCode, body: JSON.parse(Buffer.concat(chunks))}); } catch (error) { reject(error); } });
    });
    request.once('error', reject); request.end();
  });
}

async function assertFixturePage(page, origin) {
  const response = await page.goto(`${origin}/`, {waitUntil: 'load', timeout: timeoutMs});
  assert.equal(response?.status(), 200);
  await page.waitForFunction(() => globalThis.__browserDeliveryFixture?.ready === true, null, {timeout: timeoutMs});
}

function requestStatic(port, host, pathname) {
  return new Promise((resolve, reject) => {
    const request = https.request({hostname: '127.0.0.1', port, path: pathname, method: 'GET',
      rejectUnauthorized: false, headers: {Host: host}}, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve({status: response.statusCode, headers: response.headers,
        body: Buffer.concat(chunks)}));
    });
    request.once('error', reject); request.end();
  });
}

async function installNativeAudioTrace(page) {
  await page.addInitScript(() => {
    const trace = {contexts: [], worklets: [], errors: []};
    globalThis.__nativeAudioTrace = trace;
    const NativeAudioContext = globalThis.AudioContext;
    if (NativeAudioContext) {
      const AudioContextProxy = function(...args) {
        const context = new NativeAudioContext(...args);
        const record = {context, closed: false};
        trace.contexts.push(record);
        const close = context.close;
        if (typeof close === 'function') {
          try {
            context.close = async (...closeArgs) => {
              const result = await close.apply(context, closeArgs);
              record.closed = true;
              return result;
            };
          } catch (error) { trace.errors.push(String(error?.message || error)); }
        }
        return context;
      };
      AudioContextProxy.prototype = NativeAudioContext.prototype;
      try { Object.defineProperty(globalThis, 'AudioContext', {value: AudioContextProxy, configurable: true, writable: true}); }
      catch (error) { trace.errors.push(String(error?.message || error)); }
    }
    const NativeAudioWorkletNode = globalThis.AudioWorkletNode;
    if (NativeAudioWorkletNode) {
      const AudioWorkletNodeProxy = function(context, name, options) {
        const node = new NativeAudioWorkletNode(context, name, options);
        trace.worklets.push({name: String(name), context});
        return node;
      };
      AudioWorkletNodeProxy.prototype = NativeAudioWorkletNode.prototype;
      try { Object.defineProperty(globalThis, 'AudioWorkletNode', {value: AudioWorkletNodeProxy, configurable: true, writable: true}); }
      catch (error) { trace.errors.push(String(error?.message || error)); }
    }
  });
}

async function runNativeBrowser({page, pages, receipt, packaged}) {
  const origin = 'https://staging.webmelee.gg';
  const nativeRequests = [];
  await installNativeAudioTrace(page);
  page.on('request', async request => {
    try {
      const url = new URL(request.url());
      if (url.pathname !== '/api/diagnostics' || request.method() !== 'POST') return;
      const headers = await request.allHeaders();
      nativeRequests.push({method: request.method(), sameOrigin: url.origin === origin,
        contentType: headers['content-type'] === 'application/json',
        cookie: Object.hasOwn(headers, 'cookie'), authorization: Object.hasOwn(headers, 'authorization'),
        referer: Object.hasOwn(headers, 'referer'), origin: headers.origin === origin});
    } catch {}
  });

  const indexResponse = await requestStatic(pages.port, 'staging.webmelee.gg', '/');
  assert.equal(indexResponse.status, 200, 'audited package index must be reachable');
  const indexHash = crypto.createHash('sha256').update(indexResponse.body).digest('hex');
  assert.equal(indexHash, packaged.indexHash, 'local Pages changed the audited index bytes');
  const indexText = indexResponse.body.toString('utf8');
  const identityMatch = indexText.match(/<meta\s+id=["']runtime-diagnostic-identity["']\s+content=["']([^"']+)["'][^>]*>/i);
  assert(identityMatch, 'served package index is missing its diagnostic identity');
  assert.deepEqual(JSON.parse(identityMatch[1].replaceAll('&quot;', '"').replaceAll('&#x27;', "'")), packaged.identity);
  receipt.checks.push('audited packaged bytes and release identity');

  const response = await page.goto(`${origin}/notices.html`, {waitUntil: 'load', timeout: timeoutMs});
  assert.equal(response?.status(), 200);
  const responseHeaders = response?.headers() || {};
  assert.equal(responseHeaders['cross-origin-opener-policy'], 'same-origin');
  assert.equal(responseHeaders['cross-origin-embedder-policy'], 'require-corp');
  await page.evaluate(() => {
    document.cookie = 'diagnostic-fixture-cookie=present; Path=/; SameSite=Lax';
    document.body.innerHTML = '<canvas id="canvas" tabindex="0"></canvas>' +
      '<input id="disc-file" type="file" accept=".iso,.gcm,.rvz">' +
      '<button id="start-player" type="button">Start</button>' +
      '<button id="pause-player" type="button">Pause</button>';
  });
  const moduleUrl = `/runtime/${packaged.identity.runtime_hash}/audio-preview-runtime.mjs`;
  await page.evaluate(async ({moduleUrl, identity}) => {
    const fixture = globalThis.__nativeDeliveryFixture = {
      states: [], ownerCount: 0, guards: [], samples: [], errors: [], saveConfigured: false,
      started: false, paused: false, stallMs: null, player: null,
      upload: {armed: false, armAt: null, serializations: [], fetches: []},
    };
    const nativeStringify = JSON.stringify;
    JSON.stringify = function(value, ...options) {
      const started = performance.now();
      const encoded = nativeStringify.call(JSON, value, ...options);
      const elapsed = performance.now() - started;
      if (fixture.upload.armed && value?.schema === 'melee-web-diagnostics' &&
          fixture.upload.serializations.length < 8) {
        fixture.upload.serializations.push({main_thread_ms: elapsed,
          serialized_bytes: new TextEncoder().encode(encoded).byteLength,
          inactive_processing: fixture.state?.running !== true,
          arm_to_serialize_ms: fixture.upload.armAt === null ? null : started - fixture.upload.armAt});
      }
      return encoded;
    };
    const nativeFetch = globalThis.fetch.bind(globalThis);
    globalThis.fetch = function(url, options) {
      const target = (() => { try { return new URL(typeof url === 'string' ? url : url.url).pathname; } catch { return ''; } })();
      const started = performance.now();
      const response = nativeFetch(url, options);
      if (!fixture.upload.armed || target !== '/api/diagnostics') return response;
      const row = {dispatch_ms: null, await_ms: null, inactive_processing: fixture.state?.running !== true,
        arm_to_dispatch_ms: fixture.upload.armAt === null ? null : started - fixture.upload.armAt};
      if (fixture.upload.fetches.length < 8) fixture.upload.fetches.push(row);
      row.dispatch_ms = performance.now() - started;
      Promise.resolve(response).then(() => { row.await_ms = performance.now() - started; }, () => { row.await_ms = null; });
      return response;
    };
    const imported = await import(moduleUrl);
    fixture.player = await imported.mountMeleeRuntime({
      canvas: document.querySelector('#canvas'), diagnosticIdentity: identity,
      onOwner(owner) { fixture.ownerCount++; fixture.owner = owner; },
      onState(state) {
        fixture.state = {state: state.state, scene: state.scene, running: state.running,
          paused: state.paused, graphicsReady: state.graphicsReady, canImport: state.canImport,
          canStart: state.canStart, audio: state.audio};
        if (fixture.states.length < 32) fixture.states.push(fixture.state);
      },
      onError(error) { if (fixture.errors.length < 8) fixture.errors.push(String(error?.message || error)); },
    });
    fixture.player.setAutomaticDiagnostics(true);
    const input = document.querySelector('#disc-file');
    input.addEventListener('change', () => {
      fixture.importPromise = fixture.player.importDisc(input.files[0]).then(() => {
        fixture.imported = true;
      }).catch(error => {
        fixture.importError = String(error?.message || error);
      });
    });
    const start = document.querySelector('#start-player');
    start.addEventListener('click', () => {
      fixture.startPromise = fixture.player.start().then(() => {
        fixture.started = true;
      }).catch(error => {
        fixture.startError = String(error?.message || error);
      });
    });
    const pause = document.querySelector('#pause-player');
    pause.addEventListener('click', () => {
      fixture.pausePromise = fixture.player.pause().then(() => {
        fixture.paused = true;
      }).catch(error => {
        fixture.pauseError = String(error?.message || error);
      });
    });
  }, {moduleUrl, identity: packaged.identity});
  await page.waitForFunction(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    const state = fixture?.state;
    return fixture?.ownerCount === 1 && state?.canImport === true && state?.graphicsReady === true;
  }, null, {timeout: timeoutMs});
  receipt.checks.push('single onOwner and canImport/graphicsReady gate before chooser');
  await page.evaluate(async () => {
    const fixture = globalThis.__nativeDeliveryFixture;
    await fixture.player.configureSaveProfile('everything', null);
    fixture.saveConfigured = true;
  });
  await page.locator('#disc-file').setInputFiles(packaged.disc, {timeout: timeoutMs});
  await page.waitForFunction(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    return fixture?.imported === true || fixture?.importError;
  }, null, {timeout: timeoutMs});
  const importState = await page.evaluate(() => ({error: globalThis.__nativeDeliveryFixture.importError || null}));
  assert.equal(importState.error, null, `native package import failed: ${importState.error}`);
  await page.waitForFunction(() => globalThis.__nativeDeliveryFixture?.state?.canStart === true,
    null, {timeout: timeoutMs});
  await page.locator('#start-player').click({timeout: timeoutMs});
  await page.waitForFunction(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    return fixture?.started === true || fixture?.startError;
  }, null, {timeout: timeoutMs});
  const startState = await page.evaluate(() => ({error: globalThis.__nativeDeliveryFixture.startError || null}));
  assert.equal(startState.error, null, `native package start failed: ${startState.error}`);
  await page.waitForFunction(() => {
    const state = globalThis.__nativeDeliveryFixture?.state;
    return state?.scene === 'css' && state.running === true;
  }, null, {timeout: timeoutMs});
  const graphics = await page.evaluate(async () => {
    const requestAdapter = typeof globalThis.navigator?.gpu?.requestAdapter === 'function';
    let adapterAvailable = false;
    if (requestAdapter) {
      try { adapterAvailable = !!await globalThis.navigator.gpu.requestAdapter(); } catch {}
    }
    return {secure: globalThis.isSecureContext === true, gpu: !!globalThis.navigator?.gpu,
      crossOriginIsolated: globalThis.crossOriginIsolated === true, requestAdapter, adapterAvailable};
  });
  assert(graphics.secure && graphics.gpu && graphics.crossOriginIsolated &&
    graphics.requestAdapter && graphics.adapterAvailable,
  'native package did not reach a secure, isolated WebGPU page with an available adapter');
  await page.screenshot({path: path.join(evidence, 'native-css.png'), fullPage: true});
  receipt.graphics = graphics;
  receipt.checks.push('actual packaged CSS scene and native audio start');

  await page.evaluate(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    const originalSample = globalThis.menuDiagnosticSample;
    const originalIncident = globalThis.menuDiagnosticIncident;
    if (typeof originalSample !== 'function' || typeof originalIncident !== 'function')
      throw Error('native diagnostic bridge is unavailable');
    let stalled = false;
    globalThis.menuDiagnosticSample = (...values) => {
      if (!stalled) {
        stalled = true;
        const started = performance.now(), deadline = started + 200;
        while (performance.now() < deadline) {}
        fixture.stallMs = performance.now() - started;
      }
      if (fixture.samples.length < 32) fixture.samples.push({source_frame: values[1], scene: values[2]});
      return originalSample(...values);
    };
    globalThis.menuDiagnosticIncident = (...values) => {
      const guard = {reason: values[0], value: values[1], threshold: values[2],
        source_frame: values[3], scene: values[4], clock_owner: values[5]};
      if (fixture.guards.length < 8) fixture.guards.push(guard);
      return originalIncident(...values);
    };
  });
  await page.waitForFunction(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    return Number(fixture?.stallMs) >= 180 && fixture.guards.some(guard => guard.reason === 1);
  }, null, {timeout: timeoutMs});
  const bridge = await page.evaluate(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    return {stallMs: fixture.stallMs, guard: fixture.guards.find(guard => guard.reason === 1) || null,
      state: fixture.state};
  });
  assert(bridge.guard, 'controlled callback stall did not produce the source simulation guard');
  receipt.native_guard = {reason: bridge.guard.reason, value: bridge.guard.value,
    threshold: bridge.guard.threshold, source_frame: bridge.guard.source_frame,
    scene: bridge.guard.scene, clock_owner: bridge.guard.clock_owner, stall_ms: bridge.stallMs};
  assert.equal(bridge.guard.reason, 1, 'native source reason must be simulation debt');
  assert.equal(bridge.guard.clock_owner, 1, 'native source guard must retain simulation clock ownership');
  assert(Number.isFinite(bridge.guard.value) && Number.isFinite(bridge.guard.threshold),
    'native source guard must retain finite value and threshold');
  assert.equal(nativeRequests.length, 0, 'active native incident must remain deferred');
  await page.evaluate(() => {
    const upload = globalThis.__nativeDeliveryFixture.upload;
    upload.armAt = performance.now();
    upload.armed = true;
  });
  receipt.checks.push('controlled native callback stall and source simulation guard');

  await page.locator('#pause-player').click({timeout: timeoutMs});
  await page.waitForFunction(() => {
    const fixture = globalThis.__nativeDeliveryFixture;
    return fixture?.paused === true || fixture?.pauseError;
  }, null, {timeout: timeoutMs});
  const pauseState = await page.evaluate(() => ({error: globalThis.__nativeDeliveryFixture.pauseError || null}));
  assert.equal(pauseState.error, null, `native pause failed: ${pauseState.error}`);
  await page.waitForFunction(() => globalThis.__nativeDeliveryFixture?.paused === true &&
    globalThis.__nativeDeliveryFixture?.state?.running === false, null, {timeout: timeoutMs});
  await page.waitForFunction(() => globalThis.__nativeAudioTrace?.contexts?.length >= 1,
    null, {timeout: timeoutMs});
  const audio = await page.evaluate(() => ({contexts: globalThis.__nativeAudioTrace.contexts.map(record => ({
    state: record.context.state, closed: record.closed})),
  worklets: globalThis.__nativeAudioTrace.worklets.map(record => record.name),
  errors: [...globalThis.__nativeAudioTrace.errors]}));
  assert.equal(audio.contexts.length, 1, 'native package must have one isolated AudioContext owner');
  assert.equal(audio.worklets.filter(name => name === 'melee-audio-output').length, 1,
    'native package must have one isolated audio worklet owner');
  assert.equal(audio.errors.length, 0, 'native audio ownership trace installation failed');
  receipt.audio = {context_count: audio.contexts.length, worklet_names: audio.worklets,
    live_contexts: audio.contexts.filter(record => !record.closed).length};
  await page.waitForFunction(() => globalThis.__nativeDeliveryFixture?.paused === true &&
    globalThis.__nativeDeliveryFixture?.state?.running === false &&
    globalThis.__nativeDeliveryFixture?.state?.paused === true, null, {timeout: timeoutMs});
  const requestDeadline = Date.now() + timeoutMs;
  while (!nativeRequests.length && Date.now() < requestDeadline) await new Promise(resolve => setTimeout(resolve, 100));
  assert.equal(nativeRequests.length, 1, 'inactive native incident must produce one deferred POST');
  await page.waitForFunction(() => globalThis.__nativeDeliveryFixture?.upload?.fetches?.some(row =>
    Number.isFinite(row.await_ms)), null, {timeout: timeoutMs});
  assert.deepEqual(nativeRequests[0], {method: 'POST', sameOrigin: true, contentType: true,
    cookie: false, authorization: false, referer: false, origin: true});
  const upload = await page.evaluate(() => ({
    serializations: globalThis.__nativeDeliveryFixture.upload.serializations,
    fetches: globalThis.__nativeDeliveryFixture.upload.fetches,
  }));
  assert.equal(upload.serializations.length, 1, 'native delivery must serialize one strict wire report');
  assert.equal(upload.fetches.length, 1, 'native delivery must dispatch one diagnostics request');
  const serialization = upload.serializations[0], fetchTiming = upload.fetches[0];
  assert(serialization.inactive_processing && fetchTiming.inactive_processing,
    'upload timing must be captured after native processing is inactive');
  assert(Number.isFinite(serialization.main_thread_ms) && serialization.main_thread_ms >= 0);
  assert(Number.isSafeInteger(serialization.serialized_bytes) && serialization.serialized_bytes <= 64 * 1024);
  assert(Number.isFinite(fetchTiming.dispatch_ms) && fetchTiming.dispatch_ms >= 0);
  assert(Number.isFinite(fetchTiming.await_ms) && fetchTiming.await_ms >= fetchTiming.dispatch_ms);
  assert(Number.isFinite(fetchTiming.arm_to_dispatch_ms) && fetchTiming.arm_to_dispatch_ms >= 0);
  receipt.upload_timing = {
    strict_wire_serialization_main_thread_ms: serialization.main_thread_ms,
    strict_wire_serialized_bytes: serialization.serialized_bytes,
    fetch_dispatch_main_thread_ms: fetchTiming.dispatch_ms,
    fetch_response_await_ms: fetchTiming.await_ms,
    inactive_to_fetch_dispatch_ms: fetchTiming.arm_to_dispatch_ms,
    processing_active: false,
    note: 'dispatch is synchronous main-thread work; arm-to-dispatch includes inactive scheduling and bounded IDB work; response await is separate network latency',
  };
  receipt.checks.push('deferred known-origin POST header policy');

  let admin;
  const adminDeadline = Date.now() + timeoutMs;
  while (Date.now() < adminDeadline) {
    admin = await requestAdmin(pages.port, 'staging.webmelee.gg', '?environment=staging&limit=20');
    if (admin.status === 200 && admin.body.reports?.length) break;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert.equal(admin?.status, 200);
  const reports = admin.body.reports || [];
  const matching = reports.find(row => row.report?.incident?.reason === 'simulation_debt');
  assert(matching, 'D1 did not retain the native simulation guard report');
  const report = matching.report;
  assert.deepEqual(report.identity, {source_commit: packaged.identity.source_commit,
    runtime_hash: packaged.identity.runtime_hash, build_profile: packaged.identity.build_profile});
  assert.deepEqual(report.environment, {env: 'staging', origin});
  assert.equal(report.incident.reason, 'simulation_debt');
  assert.equal(report.incident.value, bridge.guard.value);
  assert.equal(report.incident.threshold, bridge.guard.threshold);
  assert.equal(report.incident.source_frame, bridge.guard.source_frame === -1 ? null : bridge.guard.source_frame);
  assert.equal(report.incident.clock_owner, 'simulation');
  const forbiddenKeys = new Set(['stack', 'file', 'path', 'name', 'input', 'memory', 'cookie', 'token', 'authorization', 'referer']);
  const inspect = value => {
    if (!value || typeof value !== 'object') return;
    if (Array.isArray(value)) { for (const item of value) inspect(item); return; }
    for (const [key, child] of Object.entries(value)) {
      assert(!forbiddenKeys.has(key.toLowerCase()), `sanitized wire contains private key ${key}`);
      inspect(child);
    }
  };
  inspect(report);
  const reportBytes = Buffer.from(JSON.stringify(report));
  assert(reportBytes.byteLength <= 64 * 1024, 'native wire report exceeds the 64 KiB bound');
  receipt.report = {count: reports.length, sha256: crypto.createHash('sha256').update(reportBytes).digest('hex'),
    bytes: reportBytes.byteLength, reason: report.incident.reason, environment: report.environment.env,
    source_commit: report.identity.source_commit, runtime_hash: report.identity.runtime_hash};
  receipt.checks.push('strict sanitized wire, identity and guard values in D1');
  assert.equal(await page.evaluate(() => globalThis.__nativeDeliveryFixture?.saveConfigured), true);
  receipt.checks.push('everything save profile configured before import');
  receipt.automatic_resume = false;
  await page.evaluate(async () => { await globalThis.__nativeDeliveryFixture.player.destroy(); });
  receipt.checks.push('native owner destroyed without automatic resume');
}

async function runBrowser() {
  await stageFixture(nativePackage);
  const nativeReleases = nativePackage ? JSON.stringify({
    staging: [{source_commit: nativePackage.identity.source_commit,
      runtime_hash: nativePackage.identity.runtime_hash, build_profile: nativePackage.identity.build_profile}],
    production: [{source_commit: nativePackage.identity.source_commit,
      runtime_hash: nativePackage.identity.runtime_hash, build_profile: nativePackage.identity.build_profile}],
  }) : RELEASES;
  const pages = await startPages(nativeReleases);
  const proxy = await startConnectProxy(pages.port);
  let browser;
  const receipt = {
    schema: 'melee-web-diagnostics-browser-delivery-receipt-v1', result: 'running', fixture,
    mode: nativePackage ? 'packaged-native' : 'synthetic',
    wrangler: WRANGLER, compatibility_date: '2026-09-18', proxy_allowlist: [...ALLOWED_PROXY_HOSTS].sort(),
    ...(nativePackage ? {packaged_identity: nativePackage.identity,
      packaged_manifest_sha256: await sha256(nativePackage.manifestPath)} :
      {synthetic_identity: {source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player'}}),
    source_hashes: {}, checks: [], privacy: nativePackage ?
      'induced native callback-stall incident only; no token, cookie, raw request metadata, or hosted endpoint retained' :
      'synthetic incident only; no token, cookie, raw request metadata, or hosted endpoint retained',
  };
  try {
    receipt.source_hashes['runner.mjs'] = await sha256(fileURLToPath(import.meta.url));
    receipt.fixture_config_sha256 = await sha256(path.join(fixture, 'wrangler.jsonc'));
    for (const name of ['runtime-diagnostics.mjs', 'runtime-diagnostics-delivery.mjs', 'diagnostics-settings.mjs', 'diagnostics-schema.mjs']) receipt.source_hashes[name] = await sha256(path.join(WEB, name));
    receipt.source_hashes['pages-function-adapter.mjs'] = await sha256(path.join(SOURCE, 'pages-function-adapter.mjs'));
    receipt.source_hashes['worker.mjs'] = await sha256(path.join(SOURCE, 'worker.mjs'));
    receipt.source_hashes['schema.mjs'] = await sha256(path.join(SOURCE, 'schema.mjs'));
    const loaded = await loadBrowserTools(args.playwright);
    browser = await loaded.chromium.launch({...browserLaunchOptions(loaded.browser, {timeout: timeoutMs}),
      proxy: {server: `http://127.0.0.1:${proxy.port}`}});
    const context = await browser.newContext({ignoreHTTPSErrors: true, viewport: {width: 900, height: 700}});
    const page = await context.newPage();
    if (nativePackage) {
      await runNativeBrowser({page, pages, receipt, packaged: nativePackage});
      await context.close();
      receipt.browser = {version: browser.version(), mode: 'headless', graphics_claim: 'packaged native CSS only'};
      receipt.result = 'pass';
    } else {
      await assertFixturePage(page, 'https://staging.webmelee.gg');
      await page.screenshot({path: path.join(evidence, 'staging-fixture.png')});
    const active = await page.evaluate(() => { const f = globalThis.__browserDeliveryFixture; f.seed(); return new Promise(resolve => setTimeout(() => resolve({requests: f.requests.length, queue: f.player ? f.player.getDiagnosticsSettings() : null}), 50)); });
    assert.equal(active.requests, 0, 'active synthetic incident must not POST');
    const delivered = await page.evaluate(async () => { const f = globalThis.__browserDeliveryFixture; f.queueCurrent(); return f.goInactiveAndFlush(); });
    assert.equal(delivered.result.sent, 1);
    assert.equal(delivered.requests.at(-1).method, 'POST');
    assert.equal(delivered.requests.at(-1).credentials, 'omit');
    assert.equal(delivered.requests.at(-1).mode, 'same-origin');
    assert.equal(delivered.requests.at(-1).redirect, 'error');
    assert.equal(delivered.requests.at(-1).cache, 'no-store');
    assert.equal(delivered.requests.at(-1).referrerPolicy, 'no-referrer');
    assert.equal(delivered.requests.at(-1).headers.cookie, undefined);
    assert.equal(delivered.requests.at(-1).cookiePresentOnPage, true);
    receipt.checks.push('active suppression and inactive POST policy');
    receipt.timings = delivered.timings;
    const staging = await requestAdmin(pages.port, 'staging.webmelee.gg', '?environment=staging&limit=10');
    assert.equal(staging.status, 200); assert.equal(staging.body.reports.length, 1);
    assert.equal(staging.body.reports[0].report.identity.source_commit, SOURCE_COMMIT);
    assert.equal(staging.body.reports[0].report.environment.env, 'staging');
    receipt.checks.push('staging D1 delivery and identity');

    const exported = await page.evaluate(() => globalThis.__browserDeliveryFixture.exportLocal());
    assert.equal(Array.isArray(exported.retained.records), true);
    receipt.checks.push('local retained export');
    const offline = await page.evaluate(() => globalThis.__browserDeliveryFixture.offlineQueue());
    assert.equal(offline.result.offline, true); assert.equal(offline.result.sent, 0);
    const disabled = await page.evaluate(() => globalThis.__browserDeliveryFixture.disableFromSettings());
    assert.equal(disabled.automatic, false); assert.equal(disabled.preference, 'off');
    receipt.checks.push('offline queue and disabled preference clear');
    const reload = await context.newPage();
    await assertFixturePage(reload, 'https://staging.webmelee.gg');
    const denied = await reload.evaluate(async () => { const f = globalThis.__browserDeliveryFixture; f.seed(); f.queueCurrent(); return f.goInactiveAndFlush(); });
    assert.equal(denied.result.sent, 0); assert.equal(denied.status.queued, 0);
    receipt.checks.push('denied preference persists for next visit');
    await reload.close();
    await page.evaluate(() => globalThis.__browserDeliveryFixture.enableFromSettings());
    const rejected = await page.evaluate(() => globalThis.__browserDeliveryFixture.rejectedServerReport());
    assert.equal(rejected.result.sent, 0); assert.equal(rejected.status.queued, 0);
    receipt.checks.push('server rejection is bounded and dropped');
    const retry = await page.evaluate(() => globalThis.__browserDeliveryFixture.retryCap());
    assert.equal(retry.calls, 3); assert.equal(retry.pending, 0);
    receipt.checks.push('retry cap');

    const production = await context.newPage();
    await assertFixturePage(production, 'https://www.webmelee.gg');
    const productionResult = await production.evaluate(async () => { const f = globalThis.__browserDeliveryFixture; f.seed(); f.queueCurrent(); return f.goInactiveAndFlush(); });
    assert.equal(productionResult.result.sent, 1);
    const productionRows = await requestAdmin(pages.port, 'www.webmelee.gg', '?environment=production&limit=10');
    assert.equal(productionRows.status, 200); assert.equal(productionRows.body.reports.length, 1);
    assert.equal(productionRows.body.reports[0].report.environment.env, 'production');
    const stagingAfterProduction = await requestAdmin(pages.port, 'staging.webmelee.gg', '?environment=staging&limit=10');
    assert.equal(stagingAfterProduction.status, 200); assert.equal(stagingAfterProduction.body.reports.length, 1);
    receipt.checks.push('production origin isolation');
      await production.close();
      await context.close();
      receipt.browser = {version: browser.version(), mode: 'headless', graphics_claim: 'none; synthetic DOM fixture only'};
      receipt.result = 'pass';
    }
  } catch (error) {
    receipt.result = 'fail'; receipt.failure = String(error?.stack || error);
    throw error;
  } finally {
    receipt.finished_at = new Date().toISOString();
    try {
      await fs.writeFile(path.join(evidence, 'receipt.json'), JSON.stringify(receipt, null, 2) + '\n');
    } finally {
      try { await browser?.close(); }
      finally {
        try { await new Promise(resolve => proxy.server.close(resolve)); }
        finally { await stopProcess(pages.process); }
      }
    }
  }
  console.log(JSON.stringify({result: receipt.result, checks: receipt.checks, receipt: path.join(evidence, 'receipt.json')}));
}

await runBrowser();
