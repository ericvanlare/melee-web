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
import {fileURLToPath} from 'node:url';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const LIMITS = Object.freeze({
  serverStartMs: 10000,
  browserLaunchMs: 30000,
  pageStartMs: 15000,
  discImportMs: 60000,
  sourceTransitionMs: 20000,
  stageDriveMs: 30000,
  manifestHandoffMs: 20000,
  unloadMs: 15000,
  cleanupMs: 5000,
  cleanupTotalMs: 15000,
  captureWorkMs: 150000,
  stageDriveFrames: 180,
});

const {values} = parseArgs({options: Object.fromEntries(
  ['build', 'disc', 'disc-sha256', 'out', 'playwright', 'preflight', 'source-revision']
    .map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['build', 'disc', 'disc-sha256', 'out', 'playwright', 'preflight', 'source-revision'])
  if (!values[name]) throw Error(`Missing --${name}`);

const preflightPath = path.resolve(values.preflight);
const preflightBytes = await fs.readFile(preflightPath);
const preflight = JSON.parse(preflightBytes.toString('utf8'));
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: false});

const digest = bytes => createHash('sha256').update(bytes).digest('hex');
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
    armed_before_css_entry: false,
    original_css_phase: null,
    original_sss_phase: null,
    raw_pad_stage_drive_frames: 0,
    source_selection: null,
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
    http_requests: [],
    external_http_requests: [],
  },
  cleanup: {unload_attempted: false, unload_completed: false, observation_cleared: false,
    context_closed: false, browser_connection_closed: false, browser_process_terminated: false,
    browser_process_pid: null, server_process_terminated: false, server_process_pid: null,
    server_process_exit: null, failures: []},
  source_request_scope: 'C1a stops after exact local-disc preparation. It does not instrument or prove a runtime source archive file-service request; full C1 remains open.',
  error: null,
};

const persistReport = async () => {
  const temporary = `${inventoryOutput}.tmp`;
  await fs.writeFile(temporary, JSON.stringify(report, null, 2) + '\n');
  await fs.rename(temporary, inventoryOutput);
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
let unloadAttempted = false;
let requestRows = [];
let responseRows = [];
let externalRequests = [];
let pageErrors = [];
let consoleErrors = [];
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

function nativeSnapshot() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_message?.();
    const detail = module?._melee_web_native_menu_diagnostics?.();
    return {
      phase: module?._melee_web_native_menu_phase?.() ?? null,
      running: module?._melee_web_native_menu_running?.() ?? null,
      message: pointer ? module.UTF8ToString(pointer) : null,
      diagnostics: detail ? module.UTF8ToString(detail) : null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
    };
  });
}

async function sourceSnapshot(label) {
  return timeout(nativeSnapshot(), Math.min(5000, remaining()), label);
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

  const artifacts = JSON.parse(await fs.readFile(path.join(ROOT, 'tools/browser_build_artifacts.json'), 'utf8'));
  assert.equal(artifacts.length, 32, 'The checked-in browser artifact inventory changed; review the preflight');
  assert.equal(new Set(artifacts).size, artifacts.length, 'Browser artifact inventory contains duplicates');
  assert.ok(artifacts.every(name => typeof name === 'string' && name === path.basename(name)),
    'Browser artifact inventory must contain plain build filenames');

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
  page.on('console', message => { if (message.type() === 'error') consoleErrors.push(message.text().slice(0, 1000)); });
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
  await driver.selectDisc(values.disc);
  await timeout(driver.waitForStart(), Math.min(LIMITS.discImportMs, remaining()), 'Disc import and native prep');

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

  await timeout(driver.pressChord(['Enter'], {holdMs: 120, releaseMs: 150}),
    Math.min(LIMITS.sourceTransitionMs, remaining()), 'Raw-PAD CSS-to-SSS confirmation');
  await timeout(driver.waitForPhase(3), Math.min(LIMITS.sourceTransitionMs, remaining()),
    'Original SSS transition');
  report.scenario.original_sss_phase = await sourceSnapshot('Original SSS native snapshot');
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
  await timeout(driver.pressChord(['Enter'], {holdMs: 120, releaseMs: 150}),
    Math.min(LIMITS.sourceTransitionMs, remaining()), 'Raw-PAD SSS confirmation');
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
  await timeout(driver.unload(), LIMITS.unloadMs, 'Recoverable native menu unload');
  report.cleanup.unload_completed = true;
  const afterUnload = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module._melee_web_native_menu_stadium_c1a_observe?.();
    return {phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running(),
      observation: pointer ? module.UTF8ToString(pointer) : null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled};
  }), Math.min(5000, remaining()), 'Post-unload native snapshot');
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
    'One or more of the 32 served build artifacts changed during the browser attempt');
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(externalRequests, []);
  report.result = 'pass';
}

async function cleanupOwnedProcesses() {
  cleanupDeadline = Date.now() + LIMITS.cleanupTotalMs;
  const cleanupFailures = report.cleanup.failures;
  if (report.result !== 'pass' && page && !page.isClosed()) {
    try { report.scenario.failure_snapshot = await timeout(nativeSnapshot(),
      Math.min(2000, cleanupRemaining()), 'Failure-state native snapshot'); }
    catch (error) { cleanupFailures.push({step: 'failure-snapshot', error: errorText(error)}); }
    try { await timeout(saveScreenshot('failure-state'), Math.min(5000, cleanupRemaining()), 'Failure screenshot'); }
    catch (error) { cleanupFailures.push({step: 'failure-screenshot', error: errorText(error)}); }
  }
  if (driver && !unloadAttempted && page && !page.isClosed()) {
    report.cleanup.unload_attempted = true;
    unloadAttempted = true;
    try {
      await timeout(driver.unload(), Math.min(LIMITS.cleanupMs, cleanupRemaining()), 'Failure-path unload');
      report.cleanup.unload_completed = true;
    } catch (error) { cleanupFailures.push({step: 'unload', error: errorText(error)}); }
  }
  if (context) {
    try { await timeout(context.close(), cleanupRemaining(), 'Browser context close'); report.cleanup.context_closed = true; }
    catch (error) { cleanupFailures.push({step: 'context-close', error: errorText(error)}); }
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
} finally {
  await cleanupOwnedProcesses();
  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
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
