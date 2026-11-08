#!/usr/bin/env node
/*
 * Bounded two-page check of the real online staging UI. Both pages load their
 * own local disc, use the original CSS/SSS, negotiate through the page-owned
 * relay setup, start the existing rollback loop, send one actual P2 keyboard
 * pulse, and leave through the UI. The harness never advances source ticks or
 * calls a native input/write API.
 */
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {createReadStream} from 'node:fs';
import {performance} from 'node:perf_hooks';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from './browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from './browser_tools.mjs';

const {values} = parseArgs({options: {
  'served-root': {type: 'string'},
  disc: {type: 'string'},
  output: {type: 'string'},
  playwright: {type: 'string'},
  'start-only': {type: 'boolean', default: false},
  'timeout-ms': {type: 'string', default: '300000'},
}, strict: true});
const root = values['served-root'] ? path.resolve(values['served-root']) : null;
const output = values.output ? path.resolve(values.output) : null;
const disc = values.disc || process.env.MELEE_DISC_IMAGE || null;
const timeoutMs = Number(values['timeout-ms']);
if (!root || !disc || !output || !values['start-only'] ||
    !Number.isSafeInteger(timeoutMs) || timeoutMs < 30000 || timeoutMs > 300000)
  throw Error('Usage: node scripts/staging_rollback_pair_browser.mjs --start-only --served-root <immutable-staging-package> --disc <local-iso> --output <new-external-dir> [--playwright <installed-package-dir>] [--timeout-ms 300000]');

const SERVER_HOST = '127.0.0.1';
const SERVER_PORT = 8787;
const ROUTE = '/online/index.html';
const DIAGNOSTIC_QUERY = '?melee-web-staging-diagnostics=1';
const RELAY_ORIGIN = 'wss://melee-web-room-relay-staging-288.mywebcomic.workers.dev';
const REMOTE_LEAVE = 'Room disconnected: 4001: peer disconnected';
const OBSERVER_BUSY = 'Runtime history Asyncify call is in progress';
const CLEANUP_RESERVE_MS = 20000;
const CSS_SETTLE_MS = 5000;
const SOURCE_READY_TIMEOUT_MS = 30000;
const SOURCE_KEY_TIMEOUT_MS = 500;
const SSS_MOVE_LIMIT = 240;
const STAGE_FINAL_DESTINATION = 32;
const MARIO_SOURCE_KIND = 8;
const CSS_START_BUTTON = 0x1000;
const SSS_CONFIRM_BUTTON = 0x0100;

const mediaTypes = new Map([
  ['.css', 'text/css; charset=utf-8'], ['.data', 'application/octet-stream'],
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'], ['.mjs', 'text/javascript; charset=utf-8'],
  ['.svg', 'image/svg+xml'], ['.wasm', 'application/wasm'],
  ['.woff', 'font/woff'], ['.woff2', 'font/woff2'],
]);

const report = {
  schema: 'melee-web-staging-rollback-pair-start-only-v1',
  scope: 'two real online staging pages; original CSS/SSS; page-owned match agreement and rollback start; one actual P2 keyboard pulse; UI Leave; no full-match, correction, timing, or acceptance claim',
  result: 'not_started',
  hypothesis: 'Two independently loaded staging pages can reach the original agreed Mario/FD match, consume one actual local P2 input, and leave through the page-owned cleanup path.',
  producer: null,
  disc: null,
  relay: {owner: 'existing deployed staging relay selected by the online page', harness_owned: false},
  server: {host: SERVER_HOST, port: SERVER_PORT, owned: false, closed: false},
  pages: {alpha: null, beta: null},
  checks: [],
  timing: {
    live_match_screenshots: false,
    host_time_origin_ms: performance.timeOrigin,
    bracket_columns: ['operation', 'host_start_ms', 'host_end_ms'],
  },
  cleanup: {browsersClosed: {}, driversDisposed: {}, serverClosed: false},
  failure: null,
};

const hardDeadline = Date.now() + timeoutMs;
const deadline = hardDeadline - CLEANUP_RESERVE_MS;
const remaining = (stopAt = deadline) => {
  const ms = stopAt - Date.now();
  if (ms <= 0) throw Error('Two-page staging start-only check exceeded its wall-time bound.');
  return Math.max(1, Math.floor(ms));
};
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function hashFile(filename) {
  const hash = crypto.createHash('sha256');
  for await (const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest('hex');
}
function compactInput(input) {
  if (!input) return null;
  const last = input.last_non_neutral;
  return {
    ready: input.ready, focused: input.focused, visible: input.visible, active: input.active,
    keyboard_requested_mask: input.keyboard_requested_mask,
    keyboard_active_mask: input.keyboard_active_mask,
    keyboard_layout: input.keyboard_layout, physical_mask: input.physical_mask,
    samples: input.samples, error: input.error,
    pads: Array.isArray(input.pads) ? input.pads.map(row => ({source: row.source, err: row.err,
      buttons: row.buttons, stick: row.stick, cstick: row.cstick, triggers: row.triggers})) : null,
    last_non_neutral: last ? {port: last.port, sample: last.sample, source: last.source,
      err: last.err, buttons: last.buttons, stick: last.stick, cstick: last.cstick,
      triggers: last.triggers} : null,
  };
}
function frameOf(snapshot) {
  const candidates = [snapshot?.owner?.metadata?.currentFrame, snapshot?.progress?.frame];
  return candidates.find(Number.isSafeInteger) ?? null;
}
function requireHealthy(item, snapshot, label, expectedError = null) {
  if (snapshot?.error && snapshot.error !== expectedError) throw Error(`${item.role} ${label}: ${String(snapshot.error)}`);
  const fatal = item.errors.find(row => ['pageerror', 'console', 'http', 'requestfailed'].includes(row.kind));
  if (fatal) throw Error(`${item.role} ${label}: browser ${fatal.kind}: ${fatal.message || fatal.url || fatal.status}`);
}

async function makeStaticServer(directory) {
  const realRoot = await fs.realpath(directory);
  const index = path.join(realRoot, 'online', 'index.html');
  if (!(await fs.stat(index)).isFile()) throw Error('--served-root must contain online/index.html');
  const server = http.createServer(async (request, response) => {
    try {
      const url = new URL(request.url || '/', `http://${SERVER_HOST}:${SERVER_PORT}`);
      const decoded = decodeURIComponent(url.pathname);
      const relative = decoded === '/' ? ROUTE : decoded;
      const filename = path.resolve(realRoot, `.${relative}`);
      if (!filename.startsWith(`${realRoot}${path.sep}`)) throw Error('Static path escaped served root');
      const resolved = await fs.realpath(filename);
      if (!resolved.startsWith(`${realRoot}${path.sep}`)) throw Error('Static symlink escaped served root');
      const stat = await fs.stat(resolved);
      if (!stat.isFile()) throw Error('Static path is not a regular file');
      response.writeHead(200, {
        'Content-Type': mediaTypes.get(path.extname(resolved)) || 'application/octet-stream',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
        'Cache-Control': 'no-store',
      });
      createReadStream(resolved).pipe(response);
    } catch (error) {
      response.writeHead(error?.code === 'ENOENT' ? 404 : 400,
        {'Content-Type': 'text/plain; charset=utf-8'});
      response.end(String(error?.message || error));
    }
  });
  await new Promise((resolve, reject) => {
    const onError = error => { server.off('listening', resolve); reject(error); };
    server.once('error', onError);
    server.once('listening', () => { server.off('error', onError); resolve(); });
    server.listen(SERVER_PORT, SERVER_HOST);
  });
  return {server, root: realRoot, url: `http://${SERVER_HOST}:${SERVER_PORT}${ROUTE}${DIAGNOSTIC_QUERY}`};
}

async function producerIdentity(servedRoot) {
  const files = [
    'online/index.html', 'online/web/online/online-shell.mjs', 'online/runtime/rollback.json',
    'online/runtime/linked-statics.json', 'online/runtime/gameplay_menu_browser.js',
    'online/runtime/gameplay_menu_browser.wasm', 'online/runtime/gameplay_menu_browser.data',
  ];
  const records = [];
  for (const name of files) {
    const filename = path.join(servedRoot, name);
    const stat = await fs.stat(filename);
    assert(stat.isFile(), `Served producer file is not regular: ${name}`);
    records.push({name, bytes: stat.size, sha256: await hashFile(filename)});
  }
  const manifest = JSON.parse(await fs.readFile(path.join(servedRoot, 'online/runtime/rollback.json'), 'utf8'));
  assert.equal(manifest.schema, 'melee-web-rollback-staging-player-v1', 'Staging manifest schema changed');
  for (const name of ['gameplay_menu_browser.js', 'gameplay_menu_browser.wasm', 'gameplay_menu_browser.data']) {
    assert.equal(records.find(row => row.name === `online/runtime/${name}`)?.sha256,
      manifest.artifacts?.[name], `Staging ${name} bytes differ from the reviewed manifest`);
  }
  assert.equal(manifest.artifacts?.['gameplay_menu_browser.wasm'], manifest.wasmSha256,
    'Staging manifest Wasm identity is inconsistent');
  assert.equal(records.find(row => row.name === 'online/runtime/linked-statics.json')?.sha256,
    manifest.tableSha256, 'Staging linked statics bytes differ from the reviewed manifest');
  return {sourceSha: manifest.sourceSha, profileIdentity: manifest.profileIdentity,
    wasmSha256: manifest.wasmSha256, tableSha256: manifest.tableSha256,
    policySha256: manifest.policySha256, files: records};
}

async function sourceInputObserved({
  afterSample, port, observerBusy, observationMs, buttonMask = null, stickX = null, stickY = null,
}) {
  // Keep this asynchronous observation within the existing key-hold budget.
  // Only the explicit native observer-busy refusal permits another read.
  const stopAt = performance.now() + observationMs;
  do {
    try {
      const snapshot = await window.onlineSessionSnapshot({source: true});
      if (snapshot.error) throw Error(snapshot.error);
      const input = snapshot.input, row = input?.last_non_neutral;
      if (row && row.sample > afterSample && row.port === port &&
          row.source === 'keyboard' && row.err === 0 && input.keyboard_layout === 0 &&
          (input.keyboard_active_mask & (1 << port)) !== 0 &&
          (buttonMask === null || (row.buttons & buttonMask) === buttonMask) &&
          (stickX === null || Math.sign(row.stick?.[0] || 0) === stickX) &&
          (stickY === null || Math.sign(row.stick?.[1] || 0) === stickY)) {
        return {sample: row.sample, port: row.port, source: row.source, err: row.err,
          buttons: row.buttons, stick: row.stick, cstick: row.cstick, triggers: row.triggers,
          keyboard_active_mask: input.keyboard_active_mask};
      }
    } catch (error) {
      if (error.message !== observerBusy) throw error;
    }
    await new Promise(resolve => setTimeout(resolve, 10));
  } while (performance.now() < stopAt);
  return false;
}

function newPageOwner(role) {
  return {role, browser: null, context: null, page: null, driver: null, errors: [],
    websockets: [], roomCode: null, configured: null, source: {},
    inputEvents: [], lastSnapshot: null, roomActionAttempted: false, leaveAttempted: false,
    traceLive: false, liveTiming: {anchor: null, brackets: []}};
}

async function bracketLive(item, label, action) {
  if (!item.traceLive) return action();
  const started = performance.now();
  try {
    return await action();
  } finally {
    item.liveTiming.brackets.push([label, started, performance.now()]);
  }
}

async function readSnapshot(item, source = false, {label = 'snapshot', ...options} = {}) {
  return waitSnapshot(item, () => true, label, {...options, source});
}

async function waitSnapshot(item, predicate, label, {
  source = false, diagnostics = false, timeoutMs = Infinity, bound = deadline,
  expectedError = null, inspectFailure = false,
} = {}) {
  const stopAt = Math.min(bound, Date.now() + timeoutMs);
  let last = null;
  while (Date.now() < stopAt) {
    // Classify the exact native observer exception before Playwright wraps
    // it with its own evaluation error and stack. Every other error escapes.
    const snapshot = await bracketLive(item, label, () =>
      withinBound(() => item.page.evaluate(async ({source, diagnostics, busy}) => {
        try { return await window.onlineSessionSnapshot({source, diagnostics}); }
        catch (error) {
          if (error.message === busy) return null;
          throw error;
        }
      }, {source, diagnostics, busy: OBSERVER_BUSY}), stopAt));
    if (snapshot) {
      last = snapshot;
      const {session, incidents, retainedIncidents, ...summary} = snapshot;
      item.lastSnapshot = summary;
      if (snapshot.closed) item.traceLive = false;
      if (!inspectFailure) requireHealthy(item, snapshot, label, expectedError);
      if (predicate(snapshot)) return snapshot;
    }
    await item.page.waitForTimeout(25);
  }
  throw Error(`${item.role} ${label} was not observed: ${JSON.stringify(last ?? item.lastSnapshot)}`);
}

async function withinBound(action, stopAt) {
  const ms = remaining(stopAt);
  let timer;
  try {
    return await Promise.race([action(), new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error('Bounded browser operation expired.')), ms);
    })]);
  } finally { clearTimeout(timer); }
}

async function settlePair(actions) {
  const results = await Promise.allSettled(actions);
  const rejected = results.find(result => result.status === 'rejected');
  if (rejected) throw rejected.reason;
  return results.map(result => result.value);
}

async function pressWithReceipt(item, key, label, expected, port = 0, releaseMs = 100) {
  const before = await readSnapshot(item, true);
  const afterSample = Number(before.input?.last_non_neutral?.sample ?? 0);
  const event = {key, label, port, down: 'Playwright page keyboard', releaseMs};
  item.inputEvents.push(event);
  const observed = await bracketLive(item, label, () =>
    item.driver.pressChord([key], {holdMs: 0, releaseMs,
      observeBeforeRelease: {predicate: sourceInputObserved,
        arg: {afterSample, port, observerBusy: OBSERVER_BUSY,
          observationMs: SOURCE_KEY_TIMEOUT_MS - 25, ...expected}, timeoutMs: SOURCE_KEY_TIMEOUT_MS}}));
  assert(observed && observed.port === port && observed.sample > afterSample,
    `${item.role} ${label} did not reach a new actual keyboard PAD poll: ${JSON.stringify(observed)}`);
  event.receipt = observed;
  return observed;
}

async function setKeyboardControls(item) {
  const page = item.page;
  await page.locator('#controls-open').click({timeout: remaining()});
  await page.locator('#controls-dialog[open]').waitFor({timeout: remaining()});
  await page.locator('#keyboard-layout').selectOption('two', {timeout: remaining()});
  await page.locator('#player-one-source').selectOption('keyboard', {timeout: remaining()});
  await page.locator('#player-two-source').selectOption('keyboard', {timeout: remaining()});
  const configured = await page.evaluate(() => ({layout: document.querySelector('#keyboard-layout')?.value,
    playerOne: document.querySelector('#player-one-source')?.value,
    playerTwo: document.querySelector('#player-two-source')?.value,
    statusOne: document.querySelector('#player-one-source-status')?.textContent?.trim(),
    statusTwo: document.querySelector('#player-two-source-status')?.textContent?.trim()}));
  assert.deepEqual(configured, {layout: 'two', playerOne: 'keyboard', playerTwo: 'keyboard',
    statusOne: 'Keyboard', statusTwo: 'Keyboard'}, `${item.role} keyboard controls were not selected in the UI`);
  await page.locator('#controls-close').click({timeout: remaining()});
  await page.locator('#controls-dialog').waitFor({state: 'hidden', timeout: remaining()});
  item.configured = configured;
}

function cssDoors(css) {
  assert(css?.doors?.length === 40 && css?.cursors?.length === 16 && css?.geometry?.length === 48,
    'Original CSS observer returned malformed source extents');
  return [0, 1, 2, 3].map(port => ({kind: css.doors[port * 10],
    slotType: css.doors[port * 10 + 4], character: css.doors[port * 10 + 3],
    rawSlot: css.doors[port * 10 + 6]}));
}

async function enterSss(item) {
  const before = await waitSnapshot(item, snapshot => snapshot.phase === 1 && snapshot.css,
    'original CSS source observer', {source: true, timeoutMs: SOURCE_READY_TIMEOUT_MS});
  const doors = cssDoors(before.css);
  assert.deepEqual(doors.map(row => [row.kind, row.slotType]),
    [[0, 0], [0, 0], [3, 3], [3, 3]], `${item.role} original CSS is not two Human doors`);
  assert.deepEqual(doors.slice(0, 2).map(row => row.character), [MARIO_SOURCE_KIND, MARIO_SOURCE_KIND],
    `${item.role} original CSS defaults are not two Mario selections`);
  assert.deepEqual(doors.slice(0, 2).map(row => row.rawSlot), [0, 0],
    `${item.role} original CSS changed authored player slots`);
  item.source.css = {doors, cursor: before.css.geometry.slice(0, 2),
    input: compactInput(before.input)};
  await item.page.screenshot({path: path.join(output, `${item.role}-original-css.png`), timeout: remaining()});

  // Reuse the reviewed single-page route's finite CSS UI settle. It is not a
  // source-tick estimate or a readiness claim.
  await item.page.waitForTimeout(CSS_SETTLE_MS);
  const settled = await readSnapshot(item, true);
  assert.equal(settled.phase, 1, `${item.role} left CSS during its UI-only settle interval`);
  item.source.css_settle = {milliseconds: CSS_SETTLE_MS, phase: settled.phase,
    running: settled.running, cursor: settled.css?.geometry?.slice(0, 2)};
  await pressWithReceipt(item, 'Enter', 'P1 original CSS Start', {buttonMask: CSS_START_BUTTON});
  const sss = await waitSnapshot(item, snapshot => snapshot.phase === 3 && snapshot.sss?.gateObserved && snapshot.sss.cooldown === 0,
    'original CSS→SSS source transition', {source: true, timeoutMs: SOURCE_READY_TIMEOUT_MS});
  item.source.css_to_sss = {phase: sss.phase, stage: sss.sss.ids[1], cooldown: sss.sss.cooldown};
  await item.page.screenshot({path: path.join(output, `${item.role}-original-sss.png`), timeout: remaining()});
}

async function selectFinalDestination(item) {
  let snapshot = await waitSnapshot(item,
    value => value.phase === 3 && value.sss?.gateObserved && value.sss.cooldown === 0,
    'SSS source input readiness', {source: true, timeoutMs: SOURCE_READY_TIMEOUT_MS});
  const movements = [];
  for (let attempt = 0; attempt < SSS_MOVE_LIMIT && snapshot.sss.ids?.[1] !== STAGE_FINAL_DESTINATION; attempt++) {
    const [x, y, targetX, targetY] = snapshot.sss.geometry;
    const dx = targetX - x, dy = targetY - y;
    const key = Math.abs(dx) > 1.2 ? (dx > 0 ? 'd' : 'a') :
      Math.abs(dy) > 1.2 ? (dy > 0 ? 'w' : 's') : (dx >= 0 ? 'd' : 'a');
    const expected = key === 'a' ? {stickX: -1} : key === 'd' ? {stickX: 1} :
      key === 'w' ? {stickY: 1} : {stickY: -1};
    const input = await pressWithReceipt(item, key, `P1 SSS cursor movement ${attempt + 1}`, expected, 0, 40);
    snapshot = await waitSnapshot(item,
      value => value.phase === 3 && value.sss?.gateObserved && value.sss.cooldown === 0,
      `SSS source readiness after movement ${attempt + 1}`, {source: true, timeoutMs: SOURCE_READY_TIMEOUT_MS});
    movements.push({key, input, selectedStage: snapshot.sss.ids?.[1],
      cursor: snapshot.sss.geometry.slice(0, 2), target: snapshot.sss.geometry.slice(2, 4)});
  }
  assert.equal(snapshot.sss?.ids?.[1], STAGE_FINAL_DESTINATION,
    `${item.role} original SSS cursor did not reach Final Destination: ${JSON.stringify(snapshot.sss)}`);
  item.source.sss_selection = {stage: snapshot.sss.ids[1], movements,
    cursor: snapshot.sss.geometry.slice(0, 2), target: snapshot.sss.geometry.slice(2, 4),
    cooldown: snapshot.sss.cooldown};
  item.traceLive = true;
  await pressWithReceipt(item, 'j', 'P1 A: original SSS confirms Final Destination',
    {buttonMask: SSS_CONFIRM_BUTTON});
  const matchScene = await waitSnapshot(item, value => value.phase === 7,
    'original SSS→match scene', {timeoutMs: SOURCE_READY_TIMEOUT_MS});
  item.source.match_scene = {phase: matchScene.phase, roomStatus: matchScene.roomStatus};
}

async function openPage(item, chromium, browserConfig, browserPath, playwrightPath, url) {
  item.browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false, timeout: Math.min(60000, remaining())}));
  item.context = await item.browser.newContext({viewport: {width: 1280, height: 960}, deviceScaleFactor: 1});
  item.page = await item.context.newPage();
  item.page.setDefaultTimeout(remaining());
  item.page.setDefaultNavigationTimeout(remaining());
  item.page.on('pageerror', error => item.errors.push({kind: 'pageerror', message: error.message}));
  item.page.on('console', message => {
    if (message.type() === 'error') item.errors.push({kind: 'console', message: message.text()});
  });
  item.page.on('response', response => {
    if (response.status() >= 400) item.errors.push({kind: 'http', status: response.status(), url: response.url()});
  });
  item.page.on('requestfailed', request => item.errors.push({kind: 'requestfailed',
    url: request.url(), message: request.failure()?.errorText || 'request failed'}));
  item.page.on('websocket', socket => item.websockets.push(new URL(socket.url()).origin));
  item.driver = createBrowserDriver(item.page,
    {surface: 'development', timeoutMs: remaining(), deadline});
  const response = await item.page.goto(url, {waitUntil: 'load', timeout: remaining()});
  assert.equal(response?.status(), 200, `${item.role} online route did not return HTTP 200`);
  // One early clock observation lets the host brackets be compared with the
  // page's retained callback timestamps without adding live browser reads.
  const hostStart = performance.now();
  const pageClock = await withinBound(() => item.page.evaluate(() => ({
    page_time_origin_ms: performance.timeOrigin,
    page_now_ms: performance.now(),
  })), deadline);
  item.liveTiming.anchor = {
    host_start_ms: hostStart, host_end_ms: performance.now(), ...pageClock,
  };
  report.pages[item.role] = {browser: {version: item.browser.version(), executable: path.basename(browserPath),
      playwright: path.basename(playwrightPath), mode: 'headless', speaker_output: 'muted'},
    url_path: ROUTE + DIAGNOSTIC_QUERY, configured_controls: null, input_events: item.inputEvents,
    source: item.source, observed_websocket_origins: item.websockets, live_timing: item.liveTiming,
    error_count: 0, leave: null};
}

async function loadDiscAndConfigure(item) {
  // The ordinary gesture opens the shell's disclosure dialog and activates
  // audio before opening the actual file chooser.
  const ready = await item.page.waitForFunction(() => {
    const error = document.querySelector('#error:not([hidden])')?.textContent;
    if (error) return {error};
    return !document.querySelector('#choose-disc')?.disabled &&
      typeof window.onlineSessionSnapshot === 'function' && {ready: true};
  }, null, {timeout: Math.min(120000, remaining()), polling: 40});
  try {
    const state = await ready.jsonValue();
    if (state.error) throw Error(`${item.role} staging mount: ${state.error}`);
  } finally { await ready.dispose(); }
  await item.page.locator('#choose-disc').click({timeout: remaining()});
  await item.page.locator('#disc-dialog[open]').waitFor({timeout: remaining()});
  const [chooser] = await settlePair([
    item.page.waitForEvent('filechooser', {timeout: remaining()}),
    item.page.locator('#disc-confirm').click({timeout: remaining()}),
  ]);
  await chooser.setFiles(disc, {timeout: remaining()});
  await waitSnapshot(item, value => value.stage === 'ready',
    'local disc import', {timeoutMs: 180000});
  const discReady = await readSnapshot(item, false, {diagnostics: true});
  assert.equal(discReady.stagingDiagnosticsEnabled, true, `${item.role} H1 recorder was not enabled`);
  assert(discReady.incidents, `${item.role} diagnostics recorder did not export a report`);
  await item.page.locator('#host:enabled').waitFor({timeout: remaining()});
  await item.page.locator('#join:enabled').waitFor({timeout: remaining()});
  await setKeyboardControls(item);
  report.pages[item.role].configured_controls = item.configured;
}

async function hostThenJoin(alpha, beta) {
  alpha.roomActionAttempted = true;
  await alpha.page.locator('#host').click({timeout: remaining()});
  const snapshot = await waitSnapshot(alpha, value => value.stage === 'starting' &&
    /^[A-Za-z0-9_-]{24,64}$/.test(value.roomId || ''), 'Host room creation', {timeoutMs: 30000});
  alpha.roomCode = snapshot.roomId;
  const roomInput = await alpha.page.locator('#room-code').inputValue();
  assert.equal(roomInput, alpha.roomCode, 'Host UI room code differs from the page-owned room identity');
  beta.roomCode = alpha.roomCode;
  await beta.page.locator('#room-code').fill(beta.roomCode, {timeout: remaining()});
  beta.roomActionAttempted = true;
  await beta.page.locator('#join').click({timeout: remaining()});
  await waitSnapshot(beta, value => value.stage === 'starting' && value.roomId === beta.roomCode,
    'Join room admission', {timeoutMs: 30000});
  report.room = {code: alpha.roomCode, host: 'alpha', join: 'beta',
    page_owned_setup: true, native_seed_or_selection_writes: false};
}

function verifyMatch(item, match) {
  assert(match?.ready === true && match.paused === false && match.ending === false && match.complete === false,
    `${item.role} source match is not active and ready: ${JSON.stringify(match)}`);
  const rules = match.rules;
  assert.equal(rules?.stage, STAGE_FINAL_DESTINATION, `${item.role} source stage is not Final Destination`);
  assert.equal(rules?.match_kind, 1, `${item.role} source match kind changed`);
  assert.equal(rules?.is_stock, 1, `${item.role} source match is not stock mode`);
  assert.equal(rules?.is_vs, 1, `${item.role} source match is not VS`);
  assert.equal(rules?.is_teams, 0, `${item.role} source match unexpectedly uses teams`);
  assert.deepEqual(rules?.player_source_characters, [8, 8], `${item.role} source characters are not two Mario`);
  assert.deepEqual(rules?.player_slot_types, [0, 0], `${item.role} source slots are not two Humans`);
  assert.deepEqual(rules?.player_source_stocks, [4, 4], `${item.role} source stocks are not four each`);
  assert.deepEqual(rules?.resolved_controller_ports, [0, 1], `${item.role} source ports changed`);
  return {frame: match.frame, ready: match.ready, paused: match.paused,
    ending: match.ending, complete: match.complete, rules: {
      stage: rules.stage, match_kind: rules.match_kind, is_stock: rules.is_stock,
      is_vs: rules.is_vs, is_teams: rules.is_teams,
      player_source_characters: rules.player_source_characters,
      player_slot_types: rules.player_slot_types, player_source_stocks: rules.player_source_stocks,
      resolved_controller_ports: rules.resolved_controller_ports}};
}

async function waitForLiveMatch(item) {
  const live = await waitSnapshot(item, value => value.stage === 'playing' &&
    value.owner?.ownerState === 1 && Number.isSafeInteger(frameOf(value)) && value.progress,
    'page-owned rollback loop start', {timeoutMs: 120000});
  const withMatch = await waitSnapshot(item, value => value.stage === 'playing' &&
    value.match?.ready === true, 'quiescent source match observation',
    {source: true, timeoutMs: 15000});
  const match = verifyMatch(item, withMatch.match);
  item.source.match = match;
  item.source.live = {stage: live.stage, localPort: live.localPort,
    ownerState: live.owner.ownerState, currentFrame: frameOf(live), progressFrame: live.progress?.frame ?? null};
  return live;
}

async function sendP2Pulse(beta) {
  const before = await readSnapshot(beta, true, {label: 'P2 pre-key input snapshot'});
  const beforeSample = Number(before.input?.last_non_neutral?.sample ?? 0);
  const beforeFrame = frameOf(before);
  assert.equal(before.localPort, 1, 'Beta page is not bound to source P2');
  const actual = await bracketLive(beta, 'P2 ArrowRight key and PAD observation', () =>
    beta.driver.pressChord(['ArrowRight'], {holdMs: 0, releaseMs: 120,
      observeBeforeRelease: {predicate: sourceInputObserved,
        arg: {afterSample: beforeSample, port: 1, stickX: 1, observerBusy: OBSERVER_BUSY,
          observationMs: SOURCE_KEY_TIMEOUT_MS - 25}, timeoutMs: SOURCE_KEY_TIMEOUT_MS}}));
  assert(actual && actual.port === 1 && actual.source === 'keyboard' && actual.stick?.[0] > 0,
    `Beta P2 right pulse was not observed by an actual native input poll: ${JSON.stringify(actual)}`);
  beta.inputEvents.push({key: 'ArrowRight', label: 'P2 short gameplay pulse', port: 1,
    down: 'Playwright page keyboard', receipt: actual});
  const neutral = await waitSnapshot(beta, value => value.input?.pads?.[1]?.source === 'keyboard' &&
    value.input.pads[1].err === 0 && value.input.pads[1].stick?.[0] === 0 &&
    value.input.samples > actual.sample,
    'actual P2 neutral release poll', {source: true, timeoutMs: 3000});
  beta.source.gameplay_pulse = {before_frame: beforeFrame, input: actual,
    release_sample: neutral.input.samples,
    post_release_port2: neutral.input.pads[1]};
}

async function waitForProgress(item, previousFrame) {
  return waitSnapshot(item, value => value.stage === 'playing' &&
    Number.isSafeInteger(frameOf(value)) && frameOf(value) > previousFrame,
    'source progress after P2 pulse', {timeoutMs: 5000});
}

async function clickLeave(item, bound = deadline) {
  item.leaveAttempted = true;
  await bracketLive(item, 'UI Leave click', () =>
    item.page.locator('#leave').click({timeout: remaining(bound)}));
}

function ownerDrained(snapshot) {
  const owner = snapshot.owner;
  return owner?.ownerState === 2 && owner.pendingCommand === 0 &&
    owner.gpuPending === 0 && owner.endpointActive === 0 &&
    owner.completeSequence === owner.requestSequence && snapshot.phase === 0 && snapshot.running === 0;
}

async function observeLeave(item, local) {
  const expectedError = local ? null : REMOTE_LEAVE;
  const final = await waitSnapshot(item, value => value.closed && value.stage === 'closed' &&
    ownerDrained(value) && (!local || value.roomStatus === 'Room closed. Reload to play again.'),
    local ? 'local Leave and native retirement' : 'remote disconnect and native drain',
    {expectedError, timeoutMs: 30000});
  const diagnostics = await readSnapshot(item, false, {expectedError, diagnostics: true});
  assert.equal(final.error, expectedError, `${item.role} teardown error differed`);
  assert.equal(final.result, local ? 'LEFT_EARLY' : 'FAIL', `${item.role} teardown result differed`);
  assert.equal(final.owner.firstFailure, 0, `${item.role} reported a native failure during teardown`);
  assert.equal(final.owner.rendererErrors, 0, `${item.role} reported a renderer failure during teardown`);
  item.source.leave = {action: local ? 'local UI Leave' : 'remote relay disconnect after alpha Leave',
    stage: final.stage, result: final.result, error: final.error, phase: final.phase,
    running: final.running, owner: final.owner, loopResult: diagnostics.session?.match?.result ?? null,
    loopFailure: diagnostics.session?.match?.failure ?? null, observedFrame: frameOf(final)};
  await item.page.screenshot({path: path.join(output, `${item.role}-after-leave.png`), timeout: remaining()});
  report.pages[item.role].leave = item.source.leave;
}

async function retainDiagnostics(item, bound, suffix) {
  const snapshot = await readSnapshot(item, false, {diagnostics: true, bound, inspectFailure: true});
  const {session, incidents, retainedIncidents, stagingDiagnosticsEnabled, ...boundary} = snapshot;
  const filename = `${item.role}-${suffix}.json`;
  await fs.writeFile(path.join(output, filename), `${JSON.stringify({
    boundary, stagingDiagnosticsEnabled, session, current: incidents ?? null,
    retained: retainedIncidents ?? null,
  }, null, 2)}\n`, {flag: 'wx'});
  if (report.pages[item.role]) (report.pages[item.role].diagnostic_exports ||= []).push(filename);
  return {stagingDiagnosticsEnabled, incidents, retainedIncidents, session};
}

let server = null;
let chromium = null;
let browserConfig = null;
let browserPath = null;
let playwrightPath = null;
const pages = [newPageOwner('alpha'), newPageOwner('beta')];
let failure = null;
let outputOwned = false;

async function main() {
  await fs.mkdir(output, {recursive: false});
  outputOwned = true;
  const [discStat, producer] = await Promise.all([
    fs.stat(disc), producerIdentity(root),
  ]);
  assert(discStat.isFile(), 'Disc input must be a local regular file');
  report.disc = {name: path.basename(disc), bytes: discStat.size, sha256: await hashFile(disc)};
  report.producer = producer;
  const served = await makeStaticServer(root);
  server = served.server;report.server.owned = true;report.server.url = served.url;
  const tools = await loadBrowserTools(values.playwright);
  ({chromium, browser: browserConfig, browserPath, playwrightPath} = tools);
  await settlePair(pages.map(item => openPage(item, chromium, browserConfig, browserPath,
    playwrightPath, served.url)));
  await settlePair(pages.map(loadDiscAndConfigure));
  report.checks.push('Both pages imported the same local disc independently and selected P1/P2 keyboard input through Controls.');

  const [alpha, beta] = pages;
  await hostThenJoin(alpha, beta);
  await settlePair(pages.map(enterSss));
  await settlePair(pages.map(selectFinalDestination));
  report.checks.push('Both pages used actual P1 keyboard events to traverse the original CSS and source-observed SSS to Final Destination.');

  await settlePair(pages.map(waitForLiveMatch));
  report.checks.push('Page-owned negotiation reached the same original four-stock Mario/Final Destination rollback session on both roles.');

  const pulseBaseline = await settlePair(pages.map(item =>
    readSnapshot(item, false, {label: 'P2 pre-pulse source frame baseline'})));
  for (const [index, item] of pages.entries()) {
    const frame = frameOf(pulseBaseline[index]);
    assert(Number.isSafeInteger(frame), `${item.role} lacked a source frame before P2 pulse`);
    item.source.live.frameBeforePulse = frame;
  }
  await sendP2Pulse(beta);
  const progressed = await settlePair(pages.map((item, index) =>
    waitForProgress(item, frameOf(pulseBaseline[index]))));
  report.pages.alpha.source.live.frameAfterPulse = frameOf(progressed[0]);
  report.pages.beta.source.live.frameAfterPulse = frameOf(progressed[1]);
  report.checks.push('Beta P2 ArrowRight was observed in a real native keyboard input poll; both page-owned loops progressed across the pulse interval.');

  for (const item of pages) {
    assert.deepEqual(item.websockets, [RELAY_ORIGIN], `${item.role} did not use the expected staging relay`);
  }
  // The second page observes the relay's clean partner disconnect. Two racing
  // Leave clicks would make the remote outcome ambiguous.
  await clickLeave(alpha);
  await settlePair([observeLeave(alpha, true), observeLeave(beta, false)]);
  report.checks.push('Alpha used UI Leave. Beta observed the expected relay disconnect. Both native owners retired and drained renderer work.');
  const diagnosticExports = await settlePair(pages.map(item =>
    retainDiagnostics(item, deadline, 'after-leave-diagnostics')));
  for (const diagnostic of diagnosticExports) {
    assert.equal(diagnostic.stagingDiagnosticsEnabled, true, 'H1 recorder state was lost');
    assert(diagnostic.incidents && diagnostic.retainedIncidents && diagnostic.session?.start,
      'Staging diagnostic exports or page-owned agreement were unavailable');
  }
  const [alphaStart, betaStart] = diagnosticExports.map(row => row.session.start);
  assert.equal(alphaStart.localPort, 0);
  assert.equal(betaStart.localPort, 1);
  for (const field of ['seed', 'agreement', 'liveIdentity', 'heldProof']) {
    assert.deepEqual(alphaStart[field], betaStart[field], `Page-owned ${field} receipts differ`);
  }
  assert.deepEqual(pages.flatMap(item => item.errors), [], 'One or both online pages reported browser/network errors');
  report.result = 'START_ONLY_PASS';
}

try {
  await main();
} catch (error) {
  failure = error;
  report.result = 'FAIL';
  report.failure = {message: error.message || String(error), stack: error.stack || null,
    pages: Object.fromEntries(pages.map(item => [item.role, {
      errors: item.errors, lastSnapshot: item.lastSnapshot || null,
      roomCode: item.roomCode || null, source: item.source,
    }]))};
} finally {
  if (failure) {
    // Leave time for diagnostic exports and browser closure even when the
    // scenario reaches its own deadline. The outer owner bounds process cleanup.
    const drainDeadline = Math.min(hardDeadline - 7000, Date.now() + 12000);
    const exportDeadline = hardDeadline - 3000;
    await Promise.allSettled(pages.filter(item => item.page).map(item =>
      retainDiagnostics(item, Math.min(drainDeadline, Date.now() + 2000), 'failure-before-cleanup')
        .catch(error => { (report.cleanup.diagnosticErrors ||= []).push(`${item.role}: ${error.message}`); })));
    await Promise.allSettled(pages.filter(item => item.page).map(item =>
      item.page.screenshot({path: path.join(output, `${item.role}-failure.png`),
        timeout: remaining(Math.min(drainDeadline, Date.now() + 1500))})));
    const roomPages = pages.filter(item => item.page && item.roomActionAttempted);
    try {
      // An attempted click is not proof of drain. Once either side has
      // attempted Leave, observe both outcomes without another racing click.
      if (!roomPages.some(item => item.leaveAttempted)) {
        for (const item of roomPages) {
          if (await withinBound(() => item.page.locator('#leave').isEnabled(), drainDeadline)) {
            await clickLeave(item, drainDeadline);
            break;
          }
        }
      }
    } catch (error) {
      (report.cleanup.leaveErrors ||= []).push(error.message);
    }
    await Promise.allSettled(roomPages.map(async item => {
      try {
        const final = await waitSnapshot(item, value => value.closed &&
          (value.owner?.ownerState === 0 || ownerDrained(value)),
          'failure-path UI cleanup', {bound: drainDeadline, inspectFailure: true});
        (report.cleanup.roomBoundaries ||= {})[item.role] = final;
      } catch (error) { (report.cleanup.leaveErrors ||= []).push(`${item.role}: ${error.message}`); }
    }));
    await Promise.allSettled(pages.filter(item => item.page).map(item =>
      retainDiagnostics(item, exportDeadline, 'failure-after-cleanup')
        .catch(error => { (report.cleanup.diagnosticErrors ||= []).push(`${item.role}: ${error.message}`); })));
  }
  for (const item of pages) {
    try { item.driver?.dispose();report.cleanup.driversDisposed[item.role] = true; }
    catch (error) { report.cleanup.driversDisposed[item.role] = false;
      (report.cleanup.errors ||= []).push(`${item.role} driver: ${error.message || error}`); }
  }
  for (const item of pages) {
    if (!item.browser) { report.cleanup.browsersClosed[item.role] = true;continue; }
    try { await withinBound(() => item.browser.close(), hardDeadline - 1000);report.cleanup.browsersClosed[item.role] = true; }
    catch (error) { report.cleanup.browsersClosed[item.role] = false;
      (report.cleanup.errors ||= []).push(`${item.role} browser: ${error.message || error}`); }
  }
  if (server) {
    try {
      await withinBound(() => new Promise((resolve, reject) =>
        server.close(error => error ? reject(error) : resolve())), hardDeadline - 500);
      report.cleanup.serverClosed = true;report.server.closed = true;
    } catch (error) {
      report.cleanup.serverClosed = false;
      (report.cleanup.errors ||= []).push(`Static server: ${error.message || error}`);
    }
  } else report.cleanup.serverClosed = true;
  const cleanupErrors = report.cleanup.errors || [];
  if (cleanupErrors.length || Object.values(report.cleanup.browsersClosed).some(value => value !== true) ||
      report.cleanup.serverClosed !== true) {
    report.result = 'FAIL';
    report.failure ||= {message: cleanupErrors.join('; ') || 'Owned browser/server cleanup did not complete.'};
    failure ||= Error(report.failure.message);
  }
  for (const item of pages) {
    if (!report.pages[item.role]) continue;
    report.pages[item.role].error_count = item.errors.length;
    report.pages[item.role].last_snapshot = item.lastSnapshot;
    report.pages[item.role].source ||= item.source;
    report.pages[item.role].websocket_origins ||= item.websockets;
  }
  if (outputOwned) {
    try {
      await fs.writeFile(path.join(output, 'result.json'), `${JSON.stringify(report, null, 2)}\n`, {flag: 'wx'});
    } catch (error) {
      report.result = 'FAIL';report.resultWriteError = error.message || String(error);
      report.failure ||= {message: report.resultWriteError};failure ||= Error(`Could not retain result.json: ${report.resultWriteError}`);
    }
  }
}
if (failure) throw failure;
console.log(JSON.stringify({result: report.result, checks: report.checks, output}));
