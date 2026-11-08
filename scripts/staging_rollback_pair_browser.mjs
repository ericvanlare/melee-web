#!/usr/bin/env node
/*
 * Bounded two-page checks of the real online staging UI. Both pages load their
 * own local disc, use the original CSS/SSS, negotiate through the page-owned
 * relay setup, and start the existing rollback loop. The full route uses
 * browser inputs to finish the original match and return from Results to CSS.
 * The harness never advances source ticks or calls a native input/write API.
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
import {validateBrowserProcessInfo} from './net_lockstep_browser_handshake_probe.mjs';
import {confirmTwoHumanResults} from '../tests/vs_rules_results_confirmation_driver.mjs';
import {returnFromCompetitivePrize} from '../tests/vs_rules_timeout_route_helpers.mjs';
import {standardPad} from '../tests/controller-fixtures.mjs';

const {values} = parseArgs({options: {
  'served-root': {type: 'string'},
  url: {type: 'string'},
  disc: {type: 'string'},
  output: {type: 'string'},
  playwright: {type: 'string'},
  'start-only': {type: 'boolean', default: false},
  'full-match': {type: 'boolean', default: false},
  'source-idle-probe': {type: 'boolean', default: false},
  'lobby-recovery': {type: 'boolean', default: false},
  'guest-gamepad': {type: 'boolean', default: false},
  'timeout-ms': {type: 'string', default: '300000'},
}, strict: true});
const root = values['served-root'] ? path.resolve(values['served-root']) : null;
const output = values.output ? path.resolve(values.output) : null;
const disc = values.disc || process.env.MELEE_DISC_IMAGE || null;
const timeoutMs = Number(values['timeout-ms']);
const idleProbe = values['source-idle-probe'];
const fullMatch = values['full-match'];
const lobbyRecovery = values['lobby-recovery'];
const guestGamepad = values['guest-gamepad'];
if (!root || !disc || !output || [values['start-only'], idleProbe, fullMatch].filter(Boolean).length !== 1 ||
    !Number.isSafeInteger(timeoutMs) || timeoutMs < 30000 || timeoutMs > 300000)
  throw Error('Usage: node scripts/staging_rollback_pair_browser.mjs (--start-only | --source-idle-probe | --full-match) [--lobby-recovery] [--guest-gamepad] --served-root <immutable-staging-package> [--url <exact-staging-Pages-online-route>] --disc <local-iso> --output <new-external-dir> [--playwright <installed-package-dir>] [--timeout-ms 300000]');

const SERVER_HOST = '127.0.0.1';
const SERVER_PORT = 8787;
const ROUTE = '/online/index.html';
const DIAGNOSTIC_QUERY = '?melee-web-staging-diagnostics=1';
const hostedUrl = values.url ? checkedHostedUrl(values.url) : null;
const RELAY_ORIGIN = 'wss://melee-web-room-relay-staging-288.mywebcomic.workers.dev';
const REMOTE_LEAVE = 'Room disconnected: 4001: peer disconnected';
const OBSERVER_BUSY = 'Runtime history Asyncify call is in progress';
const CLEANUP_RESERVE_MS = 20000;
const CSS_SETTLE_MS = 5000;
const SOURCE_READY_TIMEOUT_MS = 30000;
const SOURCE_KEY_TIMEOUT_MS = 500;
const SSS_MOVE_LIMIT = 240;
const STAGE_BATTLEFIELD = 31;
const STAGE_FINAL_DESTINATION = 32;
const MARIO_SOURCE_KIND = 8;
const CSS_START_BUTTON = 0x1000;
const SSS_CONFIRM_BUTTON = 0x0100;
const MATCH_STATE_TIMEOUT_MS = 15000;
const MATCH_READY_MOTION = 14;
const REBIRTH_WAIT_MOTION = 13;
const REBIRTH_EXIT_MOTION = 29;

const mediaTypes = new Map([
  ['.css', 'text/css; charset=utf-8'], ['.data', 'application/octet-stream'],
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'], ['.mjs', 'text/javascript; charset=utf-8'],
  ['.svg', 'image/svg+xml'], ['.wasm', 'application/wasm'],
  ['.woff', 'font/woff'], ['.woff2', 'font/woff2'],
]);

const report = {
  schema: 'melee-web-staging-rollback-pair-start-only-v1',
  mode: fullMatch ? 'full-match' : idleProbe ? 'source-idle-probe' : 'start-only',
  lobbyRecovery, guestGamepad,
  scope: (fullMatch ? 'two actual online pages; original CSS, SSS, four-stock match, Results, and returned CSS' : idleProbe ? 'two actual online pages; no gameplay input injection through source frame 320 or first failure; source-file boundary reducer; no full-match, correction, timing, or acceptance claim' : 'two real online staging pages; original CSS/SSS; page-owned match agreement and rollback start; one actual P2 input pulse; UI Leave; no full-match, correction, timing, or acceptance claim') +
    (guestGamepad ? '; authored browser Gamepad fixture, no physical-controller claim' : '; keyboard input'),
  result: 'not_started',
  hypothesis: fullMatch ? 'Real P1 walk-offs and respawns reach a common native match-final receipt and original Results before both pages return to CSS and Leave.' : idleProbe ? 'Retain the first native source-file refusal through frame 320 without gameplay key injection and with a nonblocking observation across takeover.' : 'Two independently loaded staging pages can reach the original agreed Mario/FD match, consume one actual local P2 input, and leave through the page-owned cleanup path.',
  producer: null,
  disc: null,
  relay: {owner: 'existing deployed staging relay selected by the online page', harness_owned: false},
  server: hostedUrl ? {hosted: true, owned: false, url: hostedUrl} :
    {host: SERVER_HOST, port: SERVER_PORT, owned: false, closed: false},
  pages: {alpha: null, beta: null},
  browser_process_info_by_role: {},
  browser_process_info: [],
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

function checkedHostedUrl(value) {
  assert(/^https:\/\/(?:[0-9a-f]{8}\.)?webmelee-staging\.pages\.dev\/online\/(?:index\.html)?$/.test(value),
    '--url must be the exact stable or immutable staging Pages online route');
  return new URL('/online/' + DIAGNOSTIC_QUERY, value).href;
}

async function verifyHostedProducer(url, producer) {
  const observed = [];
  for (const record of producer.files) {
    const fileUrl = new URL(record.name === 'online/index.html' ? '/online/' : '/' + record.name, url);
    const response = await fetch(fileUrl, {redirect: 'error', cache: 'no-store',
      headers: {'User-Agent': 'WebMelee-Release-Audit/1.0'},
      signal: AbortSignal.timeout(Math.min(30000, remaining()))});
    assert.equal(response.status, 200, `Hosted producer returned HTTP ${response.status} for ${record.name}`);
    const body = Buffer.from(await response.arrayBuffer());
    assert.equal(body.length, record.bytes, `Hosted producer size differs for ${record.name}`);
    assert.equal(sha256(body), record.sha256, `Hosted producer hash differs for ${record.name}`);
    observed.push(record.name);
  }
  return observed;
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
  source = 'keyboard',
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
          row.source === source && row.err === 0 &&
          (source === 'gamepad' ? (input.physical_mask & (1 << port)) !== 0 :
            input.keyboard_layout === 0 && (input.keyboard_active_mask & (1 << port)) !== 0) &&
          (buttonMask === null || (row.buttons & buttonMask) === buttonMask) &&
          (stickX === null || Math.sign(row.stick?.[0] || 0) === stickX) &&
          (stickY === null || Math.sign(row.stick?.[1] || 0) === stickY)) {
        return {sample: row.sample, port: row.port, source: row.source, err: row.err,
          buttons: row.buttons, stick: row.stick, cstick: row.cstick, triggers: row.triggers,
          keyboard_active_mask: input.keyboard_active_mask, physical_mask: input.physical_mask,
          online_local_device_port: input.online_local_device_port,
          online_local_logical_port: input.online_local_logical_port};
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
  source = false, diagnostics = false, stageKind, timeoutMs = Infinity, bound = deadline,
  expectedError = null, inspectFailure = false,
} = {}) {
  const stopAt = Math.min(bound, Date.now() + timeoutMs);
  let last = null;
  while (Date.now() < stopAt) {
    // Classify the exact native observer exception before Playwright wraps
    // it with its own evaluation error and stack. Every other error escapes.
    const snapshot = await bracketLive(item, label, () =>
      withinBound(() => item.page.evaluate(async ({source, diagnostics, stageKind, busy}) => {
        try { return await window.onlineSessionSnapshot({source, diagnostics, stageKind}); }
        catch (error) {
          if (error.message === busy) return null;
          throw error;
        }
      }, {source, diagnostics, stageKind, busy: OBSERVER_BUSY}), stopAt));
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

async function collectBrowserProcessInfo(browser, role, stopAt) {
  assert(typeof browser?.newBrowserCDPSession === 'function', `${role} lacks an owned browser CDP session`);
  const session = await withinBound(() => browser.newBrowserCDPSession(), stopAt);
  try {
    const response = await withinBound(() => session.send('SystemInfo.getProcessInfo'), stopAt);
    const rows = validateBrowserProcessInfo(response?.processInfo);
    assert.equal(rows.filter(row => row.type === 'browser').length, 1,
      `${role} Chrome CDP inventory must identify exactly one browser root`);
    return rows;
  } finally {
    await withinBound(() => session.detach(), stopAt);
  }
}

function flattenBrowserProcessInfo(byRole) {
  const roles = ['alpha', 'beta'];
  const browserRoots = {};
  const seen = new Set();
  const rows = roles.flatMap(role => {
    const inventory = validateBrowserProcessInfo(byRole[role]);
    const roots = inventory.filter(row => row.type === 'browser');
    assert.equal(roots.length, 1, `${role} Chrome CDP inventory must identify exactly one browser root`);
    browserRoots[role] = roots[0].id;
    return inventory.map(row => {
      assert(!seen.has(row.id), `Chrome CDP PID ${row.id} overlaps the two browser owners`);
      seen.add(row.id);
      return {...row, owner_role: role};
    });
  });
  return {rows, browserRoots};
}

async function settlePair(actions) {
  const results = await Promise.allSettled(actions);
  const rejected = results.find(result => result.status === 'rejected');
  if (rejected) throw rejected.reason;
  return results.map(result => result.value);
}

const useGamepad = item => guestGamepad && item.role === 'beta';
const gamepadActions = {
  Enter: {button: 9}, j: {button: 0},
  a: {axis: 0, value: -1}, d: {axis: 0, value: 1},
  w: {axis: 1, value: -1}, s: {axis: 1, value: 1},
};

async function pressGamepad(item, key, observation, releaseMs) {
  assert(gamepadActions[key], `No authored browser Gamepad action for ${key}`);
  const action = {...gamepadActions[key]};
  // A half-stick menu movement reaches the source tile without oscillating
  // across its center. Gameplay is logical P2 and retains the full-stick pulse.
  if (action.axis !== undefined && observation.port === 0) action.value *= 0.5;
  assert(SOURCE_KEY_TIMEOUT_MS + releaseMs < remaining(), 'Gamepad sequence exceeds remaining wall-time bound');
  await item.page.locator('#canvas').focus({timeout: remaining()});
  const set = (pressed, bound) => withinBound(() => item.page.evaluate(({action, pressed}) => {
    const pad = window.stagingPairGamepad;
    if (action.button !== undefined) pad.buttons[action.button] = {pressed, value: Number(pressed)};
    else pad.axes[action.axis] = pressed ? action.value : 0;
  }, {action, pressed}), bound);
  let receipt;
  try {
    await set(true, deadline);
    const result = await item.page.waitForFunction(sourceInputObserved, observation,
      {timeout: Math.min(SOURCE_KEY_TIMEOUT_MS, remaining()), polling: 10});
    try { receipt = await result.jsonValue(); }
    finally { await result.dispose(); }
  } finally {
    await set(false, hardDeadline - 1000);
  }
  if (releaseMs) await item.page.waitForTimeout(releaseMs);
  return receipt;
}

async function pressWithReceipt(item, key, label, expected, port = 0, releaseMs = 100) {
  const before = await readSnapshot(item, true);
  const afterSample = Number(before.input?.last_non_neutral?.sample ?? 0);
  const gamepad = useGamepad(item) && key !== 'End';
  const source = gamepad ? 'gamepad' : 'keyboard';
  const observation = {afterSample, port, source, observerBusy: OBSERVER_BUSY,
    observationMs: SOURCE_KEY_TIMEOUT_MS - 25, ...expected};
  const event = {key, label, port, down: gamepad ? 'authored browser Gamepad fixture' : 'Playwright page keyboard', releaseMs};
  item.inputEvents.push(event);
  const observed = await bracketLive(item, label, () => gamepad ? pressGamepad(item, key, observation, releaseMs) :
    item.driver.pressChord([key], {holdMs: 0, releaseMs,
      observeBeforeRelease: {predicate: sourceInputObserved, arg: observation, timeoutMs: SOURCE_KEY_TIMEOUT_MS}}));
  assert(observed && observed.port === port && observed.source === source && observed.sample > afterSample,
    `${item.role} ${label} did not reach a new actual ${source} PAD poll: ${JSON.stringify(observed)}`);
  event.receipt = observed;
  return observed;
}

async function setControls(item) {
  const page = item.page;
  const playerOne = useGamepad(item) ? 'controller' : 'keyboard';
  await page.locator('#controls-open').click({timeout: remaining()});
  await page.locator('#controls-dialog[open]').waitFor({timeout: remaining()});
  await page.locator('#keyboard-layout').selectOption('two', {timeout: remaining()});
  await page.locator('#player-one-source').selectOption(playerOne, {timeout: remaining()});
  await page.locator('#player-two-source').selectOption('keyboard', {timeout: remaining()});
  const configured = await page.evaluate(() => ({layout: document.querySelector('#keyboard-layout')?.value,
    playerOne: document.querySelector('#player-one-source')?.value,
    playerTwo: document.querySelector('#player-two-source')?.value,
    statusOne: document.querySelector('#player-one-source-status')?.textContent?.trim(),
    statusTwo: document.querySelector('#player-two-source-status')?.textContent?.trim()}));
  assert.deepEqual([configured.layout, configured.playerOne, configured.playerTwo, configured.statusTwo],
    ['two', playerOne, 'keyboard', 'Keyboard'], `${item.role} controls were not selected in the UI`);
  if (useGamepad(item))
    assert.match(configured.statusOne, /^Controller only · Xbox Wireless Controller/,
      'Guest fixture was not recognized as a ready P1 controller');
  else assert.equal(configured.statusOne, 'Keyboard');
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

async function hoverStage(item, stageKind) {
  const options = {source: true, stageKind, timeoutMs: SOURCE_READY_TIMEOUT_MS};
  const ready = value => value.phase === 3 && value.sss?.gateObserved && value.sss.cooldown === 0;
  let snapshot = await waitSnapshot(item,
    ready, 'SSS source input readiness', options);
  const movements = [];
  for (let attempt = 0; attempt < SSS_MOVE_LIMIT && snapshot.sss.ids?.[1] !== stageKind; attempt++) {
    const [x, y, targetX, targetY] = snapshot.sss.geometry;
    const dx = targetX - x, dy = targetY - y;
    const key = Math.abs(dx) > 1.2 ? (dx > 0 ? 'd' : 'a') :
      Math.abs(dy) > 1.2 ? (dy > 0 ? 'w' : 's') : (dx >= 0 ? 'd' : 'a');
    const expected = key === 'a' ? {stickX: -1} : key === 'd' ? {stickX: 1} :
      key === 'w' ? {stickY: 1} : {stickY: -1};
    const input = await pressWithReceipt(item, key, `P1 SSS cursor movement ${attempt + 1}`, expected, 0, 40);
    snapshot = await waitSnapshot(item, ready, `SSS source readiness after movement ${attempt + 1}`, options);
    movements.push({key, input, selectedStage: snapshot.sss.ids?.[1],
      cursor: snapshot.sss.geometry.slice(0, 2), target: snapshot.sss.geometry.slice(2, 4)});
  }
  assert.equal(snapshot.sss?.ids?.[1], stageKind,
    `${item.role} original SSS cursor did not reach source stage ${stageKind}: ${JSON.stringify(snapshot.sss)}`);
  return {stage: snapshot.sss.ids[1], movements,
    cursor: snapshot.sss.geometry.slice(0, 2), target: snapshot.sss.geometry.slice(2, 4),
    cooldown: snapshot.sss.cooldown};
}

async function confirmFinalDestination(item) {
  item.traceLive = true;
  await pressWithReceipt(item, 'j', 'P1 A: original SSS confirms Final Destination',
    {buttonMask: SSS_CONFIRM_BUTTON});
}

async function waitForMatchScene(item) {
  const matchScene = await waitSnapshot(item, value => value.phase === 7,
    'original SSS→match scene', {timeoutMs: SOURCE_READY_TIMEOUT_MS});
  item.source.match_scene = {phase: matchScene.phase, roomStatus: matchScene.roomStatus};
}

async function rejectUnsupportedStage(beta) {
  const hover = await hoverStage(beta, STAGE_BATTLEFIELD);
  const input = await pressWithReceipt(beta, 'j', 'P1 A: attempt unsupported Battlefield',
    {buttonMask: SSS_CONFIRM_BUTTON});
  const observed = await readSnapshot(beta, true, {stageKind: STAGE_BATTLEFIELD});
  assert.equal(observed.phase, 3, 'Unsupported confirmation left the original stage screen');
  assert.equal(observed.startState, 1, 'Unsupported confirmation created a native offer');
  assert.equal(observed.sss?.ids?.[1], STAGE_BATTLEFIELD, 'Unsupported confirmation changed the hovered stage');
  beta.source.unsupportedStage = {hover, input, phase: observed.phase, startState: observed.startState};
}

async function verifySharedReady(alpha, beta) {
  const guest = await waitSnapshot(beta, value => value.session?.lobby?.alpha?.phase === 'selected' &&
    value.session.lobby.beta?.phase === 'sss',
    'guest sees host ready while guest remains in stage select', {diagnostics: true, timeoutMs: SOURCE_READY_TIMEOUT_MS});
  const host = await readSnapshot(alpha, false, {diagnostics: true});
  assert.equal(host.startState, 2, 'Host did not hold its original source offer while waiting for the guest');
  assert.equal(guest.startState, 1, 'Guest started before confirming the admitted stage');
  assert.equal(guest.phase, 3, 'Guest left original SSS before confirming the admitted stage');
  assert.equal(guest.session.lobby.alpha.stage, STAGE_FINAL_DESTINATION);
  assert(!host.session.start && !guest.session.start, 'Match agreement appeared before both confirmations');
  const guestHostLabel = await beta.page.locator('#lobby-alpha').textContent({timeout: remaining()});
  assert.match(guestHostLabel, /ready · Mario \/ Final Destination/);
  report.lobbyRecoveryProof = {hostStartState: host.startState, guestStartState: guest.startState,
    guestPhase: guest.phase, guestLobby: guest.session.lobby, guestHostLabel};
}

async function openPage(item, chromium, browserConfig, browserPath, playwrightPath, url) {
  item.browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false, timeout: Math.min(60000, remaining())}));
  item.context = await item.browser.newContext({viewport: {width: 1280, height: 960}, deviceScaleFactor: 1});
  item.page = await item.context.newPage();
  if (useGamepad(item)) await item.page.addInitScript(pad => {
    window.stagingPairGamepad = pad;
    Object.defineProperty(navigator, 'getGamepads', {value: () => [pad]});
  }, standardPad());
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
  assert(!response.request().redirectedFrom() && response.url() === url && item.page.url() === url,
    `${item.role} online route redirected or changed during navigation`);
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
    url_path: new URL(url).pathname + DIAGNOSTIC_QUERY, configured_controls: null, input_events: item.inputEvents,
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
  await setControls(item);
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
  const beforeFrame = frameOf(before);
  assert.equal(before.localPort, 1, 'Beta page is not bound to source P2');
  const actual = await pressWithReceipt(beta, 'd', 'P2 short gameplay pulse', {stickX: 1}, 1, 120);
  assert.equal(actual.online_local_device_port, 0, 'Guest pulse did not use the local P1 device');
  assert.equal(actual.online_local_logical_port, 1, 'Guest pulse did not reach the logical P2 route');
  const neutral = await waitSnapshot(beta, value => value.input?.pads?.[1]?.source === actual.source &&
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

function stockSample(snapshot) {
  const match = snapshot.match;
  if (!match || !Number.isSafeInteger(match.frame)) return null;
  if (snapshot.owner?.ownerState === 1)
    assert.equal(match.frame, snapshot.owner.metadata?.currentFrame, 'Match sample differed from the live source cursor');
  const [p1, p2] = match.players ?? [];
  assert(p1?.human && p2?.human && p1.source_port === 0 && p2.source_port === 1 &&
    p1.source_character === MARIO_SOURCE_KIND && p2.source_character === MARIO_SOURCE_KIND &&
    Number.isInteger(p1.stocks) && p1.stocks >= 0 && p1.stocks <= 4 && p2.stocks === 4,
  `Unexpected source stock state: ${JSON.stringify(match.players)}`);
  return {frame: match.frame, ending: match.ending, players: [p1, p2].map(player => ({
    stocks: player.stocks, motion: player.motion, groundAir: player.groundAir,
    x: player.x, y: player.y, facing: player.facing_direction,
  }))};
}

function p1Direction(player, facingOnly = false) {
  assert(Number.isFinite(player.x) && Number.isFinite(player.y) && [-1, 1].includes(player.facing),
    'P1 direction requires finite source position and authored facing');
  const stickX = facingOnly ? player.facing : Math.sign(player.x) || player.facing;
  return {key: stickX < 0 ? 'a' : 'd', stickX};
}

async function waitStock(item, stocks, motion, groundAir, label) {
  const snapshot = await waitSnapshot(item, value => {
    const player = stockSample(value)?.players[0];
    return player && player.stocks === stocks && player.motion === motion && player.groundAir === groundAir;
  }, label, {source: true, timeoutMs: MATCH_STATE_TIMEOUT_MS});
  return stockSample(snapshot);
}

async function walkOff(item, initial) {
  const stockBefore = initial.players[0].stocks;
  const {key, stickX} = p1Direction(initial.players[0]);
  const before = await readSnapshot(item, true, {label: 'P1 pre-walk input snapshot'});
  const afterSample = Number(before.input?.last_non_neutral?.sample ?? 0);
  const event = {key, label: `P1 walk off from ${stockBefore} stocks`, port: 0,
    down: 'Playwright page keyboard', initial};
  item.inputEvents.push(event);
  await item.page.locator('#canvas').focus({timeout: remaining()});
  try {
    await bracketLive(item, `${event.label} keydown`, () =>
      withinBound(() => item.page.keyboard.down(key), deadline));
    event.receipt = await bracketLive(item, `${event.label} PAD observation`, () =>
      withinBound(() => item.page.evaluate(sourceInputObserved, {
        afterSample, port: 0, stickX, observerBusy: OBSERVER_BUSY,
        observationMs: SOURCE_KEY_TIMEOUT_MS - 25,
      }), Math.min(deadline, Date.now() + SOURCE_KEY_TIMEOUT_MS)));
    assert(event.receipt && event.receipt.sample > afterSample, `${event.label} lacked an actual PAD receipt`);
    const ending = await waitSnapshot(item, value => {
      if (stockBefore === 1 && value.result === 'MATCH_FINAL_COMPLETE') return true;
      const sample = stockSample(value);
      if (!sample) return false;
      const stocks = sample.players[0].stocks;
      assert(stocks === stockBefore || stocks === stockBefore - 1, 'P1 source stock loss skipped a stock');
      return stockBefore === 1 ? sample.ending === true && stocks === 0 : stocks === stockBefore - 1;
    }, `${event.label} source outcome`, {source: true, timeoutMs: MATCH_STATE_TIMEOUT_MS});
    event.outcome = stockSample(ending) ?? {frame: frameOf(ending), result: ending.result, phase: ending.phase};
    return event.outcome;
  } finally {
    await bracketLive(item, `${event.label} keyup`, () =>
      withinBound(() => item.page.keyboard.up(key), hardDeadline - 1000));
  }
}

async function playFullMatch(alpha, beta) {
  await sendP2Pulse(beta);
  const cycles = [];
  alpha.source.stockCycles = cycles;
  let grounded = await waitStock(alpha, 4, MATCH_READY_MOTION, 0, 'P1 initial grounded Wait');
  for (const stockBefore of [4, 3, 2]) {
    const stockAfter = stockBefore - 1;
    const cycle = {stockBefore, stockAfter};
    cycles.push(cycle);
    cycle.loss = await walkOff(alpha, grounded);
    cycle.platform = await waitStock(alpha, stockAfter, REBIRTH_WAIT_MOTION, 1, 'P1 airborne RebirthWait');
    const direction = p1Direction(cycle.platform.players[0], true);
    cycle.exitInput = await pressWithReceipt(alpha, direction.key, 'P1 facing-directed respawn-platform exit',
      {stickX: direction.stickX}, 0, 0);
    cycle.fall = await waitStock(alpha, stockAfter, REBIRTH_EXIT_MOTION, 1, 'P1 airborne Fall after platform exit');
    grounded = cycle.grounded = await waitStock(alpha, stockAfter, MATCH_READY_MOTION, 0, 'P1 grounded Wait after respawn');
    assert(cycle.loss.frame < cycle.platform.frame && cycle.platform.frame < cycle.fall.frame &&
      cycle.fall.frame < grounded.frame, 'P1 respawn source frames did not advance in order');
  }
  alpha.source.finalWalk = await walkOff(alpha, grounded);
}

function requireDirtyCorrection(matches) {
  const frameBytes = value => Array.isArray(value) && value.length === 44 &&
    value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255);
  const portBytes = (frame, port) => frame.slice(port * 11, (port + 1) * 11);
  for (const [localPort, match] of matches.entries()) {
    const remotePort = 1 - localPort;
    const role = localPort === 0 ? 'alpha' : 'beta';
    assert.equal(match.role, role);
    assert(Array.isArray(match.corrections) && match.corrections.length <= match.retention?.limit &&
      match.retention.limit === 64, `${role} lacks its bounded streaming correction journal`);
    for (const correction of match.corrections) {
      const {dirty, pre, frontier, confirmed, replay, kind} = correction;
      if (dirty === null) continue;
      assert((kind === 'fully-confirmed-correction' || kind === 'terminal-replay-selection') &&
        [dirty, pre, frontier, confirmed].every(Number.isSafeInteger) &&
        pre >= 0 && pre <= dirty && dirty < frontier && frontier - pre <= 7 && confirmed === frontier,
      `${role} dirty correction lacks a bounded, fully confirmed replay interval`);
      const {plan, rows} = replay ?? {};
      assert(plan?.kind === 'replay' && plan.from === dirty && plan.through === frontier - 1 &&
        plan.speculative_ticks === 0 && plan.frames?.length === frontier - dirty,
      `${role} dirty correction lacks its actual replay plan`);
      const [actual] = plan.frames;
      const receipt = rows?.[dirty - pre];
      const row = receipt?.row;
      const status = receipt?.status;
      assert(row?.sourceFrame === dirty && row.ordinal === dirty - pre && row.state === 2 &&
        row.localPort === localPort && row.remotePort === remotePort &&
        row.schema === 1 && row.bytes === 176 && Number.isSafeInteger(row.ticketId) && row.ticketId > 0 &&
        typeof row.pollSerial === 'string' && /^[1-9][0-9]*$/.test(row.pollSerial) &&
        receipt.unchangedPrefix === false && status?.ownerState === 1 && status.pendingCommand === 0 &&
        status.lastCompletedCommand === 15 && status.completeSequence === status.requestSequence &&
        status.completeSequence > row.ticketId && status.metadata?.preFrame === pre &&
        status.metadata.incarnation === row.incarnation && status.metadata.restoreCount >= 1 &&
        status.metadata.currentFrame === dirty + (receipt.terminal === true ? 0 : 1),
      `${role} dirty correction lacks its retained native replay ticket and poll identity`);
      assert([row.raw, row.forward, row.selected, actual?.frame].every(frameBytes),
        `${role} dirty correction lacks its exact four-port PAD bytes`);
      assert(actual.tick === dirty && actual.remote_source === 'actual' && actual.speculative === false,
        `${role} dirty replay selected a speculative or unindexed remote PAD`);
      assert.deepEqual(row.selected, actual.frame, `${role} native replay did not consume its actual plan`);
      assert.deepEqual(actual.local_pad, portBytes(row.raw, localPort));
      assert.deepEqual(actual.remote_pad, portBytes(row.selected, remotePort));
      for (const port of [localPort, 2, 3]) {
        assert.deepEqual(portBytes(row.forward, port), portBytes(row.raw, port));
        assert.deepEqual(portBytes(row.selected, port), portBytes(row.raw, port));
      }
      const predictedRemote = portBytes(row.forward, remotePort);
      assert.notDeepEqual(actual.remote_pad, predictedRemote,
        `${role} dirty replay did not replace the originally predicted remote bytes`);

      // The bridge requires an indexed lockstep-protocol receipt for every
      // actual remote plan row. Join independent retained journals when those
      // still cover this tick. The terminal tail can age the 64-row rings out.
      const admission = match.events?.find(event => event.operation === 'episodeAdmit' && event.value?.tick === dirty);
      const localPoll = match.polls?.find(poll => poll.inputOrdinal === undefined && poll.sourceFrame === dirty);
      const remotePoll = matches[remotePort].polls?.find(poll =>
        poll.inputOrdinal === undefined && poll.sourceFrame === dirty);
      if (admission) {
        const peer = admission.result?.receipt;
        assert(peer?.accepted_by === 'lockstep-protocol' && peer.sender_role === matches[remotePort].role &&
          peer.protocol_input_tick === dirty && peer.remote_port === remotePort,
        `${role} retained admission did not bind the actual peer packet`);
        assert.deepEqual(peer.pad, actual.remote_pad);
      }
      if (localPoll) {
        assert.equal(localPoll.ticketId, row.ticketId);
        assert.equal(localPoll.pollSerial, row.pollSerial);
        assert.deepEqual(localPoll.raw, row.raw);
      }
      if (remotePoll) {
        assert.equal(remotePoll.localPort, remotePort);
        assert(frameBytes(remotePoll.raw));
        assert.deepEqual(portBytes(remotePoll.raw, remotePort), actual.remote_pad);
      }
      return {role, kind, dirty, pre, frontier, confirmed, localPort, remotePort,
        raw: row.raw, forward: row.forward, selected: row.selected, ticketId: row.ticketId,
        pollSerial: row.pollSerial, replayRequestSequence: status.completeSequence,
        predictedRemote, actualRemote: actual.remote_pad, remoteSource: actual.remote_source,
        retainedJoins: {localPoll: !!localPoll, remotePoll: !!remotePoll, peerAdmission: !!admission}};
    }
  }
  throw Error('Full match lacks a retained non-vacuous actual late-input correction');
}

function compareTerminalReceipts(matches) {
  for (const [index, match] of matches.entries()) {
    const label = index === 0 ? 'Alpha' : 'Beta';
    const receipt = match?.terminal_final?.receipt;
    const agreement = match?.terminal_final?.agreement;
    const timeline = match?.finalTimeline;
    assert.equal(match?.result, 'MATCH_FINAL_COMPLETE', `${label} lacks native terminal completion`);
    assert(Number.isSafeInteger(receipt?.sourceFrame) && receipt.sourceFrame > 0 &&
      Number.isSafeInteger(receipt.lastInputOrdinal) && receipt.lastInputOrdinal >= 0 &&
      receipt.consumedOrdinal === receipt.lastInputOrdinal + 1 &&
      receipt.consumedOrdinal === match.actualPollCount && receipt.consumedOrdinal === match.input_ticks,
    `${label} terminal receipt lacks the consumed source input count`);
    assert.equal(receipt.outcome, 2, `${label} native terminal outcome was not elimination`);
    assert.equal(receipt.winner, -1, `${label} native pre-Results winner publication changed`);
    assert.equal(timeline?.dirty_from, null, `${label} terminal timeline remained dirty`);
    assert.equal(timeline?.plan_outstanding, false, `${label} terminal timeline retained an execution plan`);
    assert.equal(timeline?.simulated_through, receipt.lastInputOrdinal);
    assert(timeline?.remote_contiguous_through >= receipt.lastInputOrdinal);
    const checksum = match.terminal_final.checksum;
    assert(Array.isArray(checksum) && checksum.length === 64 &&
      checksum.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255));
    assert(agreement && agreement.input_ordinal === receipt.lastInputOrdinal &&
      agreement.source_frame === receipt.sourceFrame && agreement.outcome === receipt.outcome &&
      agreement.winner === receipt.winner && agreement.matched_bytes === 64 &&
      /^[0-9a-f]{64}$/.test(agreement.agreement_hash), `${label} terminal wire agreement was incomplete`);
    assert.equal(match.finalOwner?.ownerState, 2);
    assert.equal(match.finalOwner?.lastCompletedCommand, 30);
  }
  const [alpha, beta] = matches.map(match => match.terminal_final);
  for (const field of ['consumedOrdinal', 'lastInputOrdinal', 'sourceFrame', 'outcome', 'winner', 'randomSeed'])
    assert.equal(alpha.receipt[field], beta.receipt[field], `Native terminal receipts differed at ${field}`);
  assert.deepEqual(alpha.checksum, beta.checksum, 'Native terminal checksums differed');
  assert.equal(alpha.agreement.agreement_hash, beta.agreement.agreement_hash, 'Final wire agreement hashes differed');
  return alpha;
}

async function waitForOriginalResults(item) {
  const value = await waitSnapshot(item, snapshot => snapshot.result === 'MATCH_FINAL_COMPLETE' &&
    snapshot.phase === 8 && snapshot.running === 1 && snapshot.match?.terminal,
  'original Results and native terminal publication', {source: true, timeoutMs: 30000});
  const match = value.match;
  assert.equal(match.leg, 'vs');
  assert.equal(match.complete, true);
  assert.deepEqual(match.observed_player_source_slots, [0, 1]);
  assert.deepEqual(match.players?.map(player => player.human), [true, true]);
  assert.deepEqual(match.terminal, {outcome: 2, winners: [1]});
  item.source.originalResults = match;
}

async function returnFromResults(item) {
  const resultsDeadline = Math.min(deadline, Date.now() + 45000);
  const observeSample = async label => {
    const value = await readSnapshot(item, true, {label, bound: resultsDeadline});
    return {
      host: {phase: value.phase, running: value.running, message: value.nativeMessage,
        status: value.roomStatus, error: value.error,
        owner_capabilities: {schema: 'ordinary-runtime-v1', canPause: value.canPause}},
      trace: value.resultsTrace,
    };
  };
  const press = key => pressWithReceipt(item, key, `Original Results/Prize ${key}`,
    {buttonMask: CSS_START_BUTTON}, key === 'End' ? 1 : 0, 150);
  const wait = ms => item.page.waitForTimeout(ms);
  const confirmation = await confirmTwoHumanResults({deadlineAt: resultsDeadline, sourcePorts: [0, 1],
    observeSample, press: () => press('Enter'), pressSecond: () => press('End'), wait});
  item.source.resultsConfirmation = confirmation;
  if (confirmation.original_exit.host_phase === 9)
    item.source.prizeReturn = await returnFromCompetitivePrize({deadlineAt: resultsDeadline, sourcePorts: [0, 1],
      observeSample, press: () => press('Enter'), wait});
  const css = await waitSnapshot(item, value => value.phase === 1 && value.running === 1 && value.css?.doors?.length === 40,
    'return to original CSS after Results', {source: true, bound: resultsDeadline});
  item.source.returnedCss = {phase: css.phase, running: css.running, doors: cssDoors(css.css)};
  await item.page.screenshot({path: path.join(output, `${item.role}-returned-css.png`), timeout: remaining()});
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
  const expectedResult = fullMatch ? 'MATCH_FINAL_COMPLETE' : local ? 'LEFT_EARLY' : 'FAIL';
  const final = await waitSnapshot(item, value => value.closed && value.stage === 'closed' &&
    ownerDrained(value) && (!local || value.roomStatus === 'Room closed. Reload to play again.'),
    local ? 'local Leave and native retirement' : 'remote disconnect and native drain',
    {expectedError, timeoutMs: 30000});
  const diagnostics = await readSnapshot(item, false, {expectedError, diagnostics: true});
  assert.equal(final.error, expectedError, `${item.role} teardown error differed`);
  assert.equal(final.result, expectedResult, `${item.role} teardown result differed`);
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
  let url = hostedUrl;
  if (url) {
    report.server.verified_producer_paths = await verifyHostedProducer(url, producer);
  } else {
    const served = await makeStaticServer(root);
    server = served.server;
    report.server.owned = true;
    report.server.url = url = served.url;
  }
  const tools = await loadBrowserTools(values.playwright);
  ({chromium, browser: browserConfig, browserPath, playwrightPath} = tools);
  await settlePair(pages.map(item => openPage(item, chromium, browserConfig, browserPath,
    playwrightPath, url)));
  await settlePair(pages.map(loadDiscAndConfigure));
  report.checks.push(guestGamepad ?
    'Both pages imported the same local disc independently. Controls selected the authored browser Gamepad for guest P1 and keyboard for the other ports.' :
    'Both pages imported the same local disc independently and selected P1/P2 keyboard input through Controls.');

  const [alpha, beta] = pages;
  await hostThenJoin(alpha, beta);
  await settlePair(pages.map(enterSss));
  if (lobbyRecovery) {
    await rejectUnsupportedStage(beta);
    report.checks.push('Guest attempted to confirm visible Battlefield with actual P1 input and remained at original SSS without producing a native offer.');
  }
  await settlePair(pages.map(async item => {
    item.source.sss_selection = await hoverStage(item, STAGE_FINAL_DESTINATION);
  }));
  if (lobbyRecovery) {
    await confirmFinalDestination(alpha);
    await verifySharedReady(alpha, beta);
    report.checks.push('Guest saw the host ready for Final Destination while guest remained in SSS and host held its native offer.');
    await confirmFinalDestination(beta);
  } else await settlePair(pages.map(confirmFinalDestination));
  await settlePair(pages.map(waitForMatchScene));
  report.checks.push('Both pages used actual P1 input to traverse the original CSS and source-observed SSS to Final Destination.');

  await settlePair(pages.map(waitForLiveMatch));
  report.checks.push('Page-owned negotiation reached the same original four-stock Mario/Final Destination rollback session on both roles.');

  let finalMatches;
  if (fullMatch) {
    await playFullMatch(alpha, beta);
    await settlePair(pages.map(waitForOriginalResults));
    const terminalExports = await settlePair(pages.map(item =>
      retainDiagnostics(item, deadline, 'match-final-diagnostics')));
    finalMatches = terminalExports.map(exported => exported.session?.match);
    report.terminalFinal = compareTerminalReceipts(finalMatches);
    for (const item of pages) {
      assert.equal(item.source.originalResults.frame, report.terminalFinal.receipt.sourceFrame);
      assert.equal(item.source.originalResults.outcome, report.terminalFinal.receipt.outcome);
    }
    report.checks.push('Both pages completed the same native match-final wire agreement and original Results published P2 as winner.');
    await settlePair(pages.map(item => item.page.screenshot({
      path: path.join(output, `${item.role}-original-results.png`), timeout: remaining(),
    })));
    await settlePair(pages.map(returnFromResults));
    report.checks.push('Both pages used original Results keyboard confirmations and returned to original CSS.');
  } else if (idleProbe) {
    const observations = await settlePair(pages.map(item => waitSnapshot(item,
      value => value.stage === 'playing' && frameOf(value) >= 320,
      'source-file boundary through frame 320', {timeoutMs: 15000})));
    for (const [index, item] of pages.entries()) item.source.idleProbe = {frame: frameOf(observations[index])};
    report.checks.push('Both page-owned loops reached source frame 320 without gameplay key injection and with no source boundary failure.');
  } else {
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
    report.checks.push(`Guest P2 right pulse was observed in a real native ${beta.source.gameplay_pulse.input.source} input poll; both page-owned loops progressed across the pulse interval.`);
  }

  for (const item of pages) {
    assert.deepEqual(item.websockets, [RELAY_ORIGIN], `${item.role} did not use the expected staging relay`);
  }
  if (fullMatch) {
    await settlePair(pages.map(async item => {
      await clickLeave(item);
      await observeLeave(item, true);
    }));
    report.checks.push('Both pages used UI Leave after returning to CSS and retained their completed-match result.');
  } else {
    // After an early Leave the second page observes the relay partner disconnect.
    await clickLeave(alpha);
    await settlePair([observeLeave(alpha, true), observeLeave(beta, false)]);
    report.checks.push('Alpha used UI Leave. Beta observed the expected relay disconnect. Both native owners retired and drained renderer work.');
  }
  const diagnosticExports = await settlePair(pages.map(item =>
    retainDiagnostics(item, deadline, 'after-leave-diagnostics')));
  for (const diagnostic of diagnosticExports) {
    assert.equal(diagnostic.stagingDiagnosticsEnabled, true, 'H1 recorder state was lost');
    assert(diagnostic.incidents && diagnostic.retainedIncidents && diagnostic.session?.start,
      'Staging diagnostic exports or page-owned agreement were unavailable');
    if (hostedUrl) {
      for (const field of ['sourceSha', 'profileIdentity', 'wasmSha256', 'tableSha256', 'policySha256'])
        assert.equal(diagnostic.session.producer?.[field], producer[field], `Hosted page producer differed at ${field}`);
    }
  }
  const [alphaStart, betaStart] = diagnosticExports.map(row => row.session.start);
  assert.equal(alphaStart.localPort, 0);
  assert.equal(betaStart.localPort, 1);
  for (const field of ['seed', 'agreement', 'liveIdentity', 'heldProof']) {
    assert.deepEqual(alphaStart[field], betaStart[field], `Page-owned ${field} receipts differ`);
  }
  assert.deepEqual(pages.flatMap(item => item.errors), [], 'One or both online pages reported browser/network errors');
  if (fullMatch) {
    // Finish the original UI route and retain its scoped evidence even if the
    // live relay did not produce a non-vacuous correction in this run.
    report.dirtyCorrection = requireDirtyCorrection(finalMatches);
    report.checks.push('A retained native replay used actual late remote PAD bytes that differed from prediction on its original ticket.');
  }
  report.result = fullMatch ? 'FULL_MATCH_UI_PASS' : idleProbe ? 'IDLE_320_PASS' : 'START_ONLY_PASS';
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
  // Sample both still-live browser owners before either can close or reuse a PID.
  const inventoryDeadline = Math.min(hardDeadline - 1500, Date.now() + 2500);
  await Promise.all(pages.map(async item => {
    try {
      report.browser_process_info_by_role[item.role] =
        await collectBrowserProcessInfo(item.browser, item.role, inventoryDeadline);
    } catch (error) {
      (report.cleanup.errors ||= []).push(`${item.role} Chrome CDP inventory: ${error.message || error}`);
    }
  }));
  try {
    const inventory = flattenBrowserProcessInfo(report.browser_process_info_by_role);
    report.browser_process_info = inventory.rows;
    report.browser_process_roots_by_role = inventory.browserRoots;
  } catch (error) {
    (report.cleanup.errors ||= []).push(`Chrome CDP owner attribution: ${error.message || error}`);
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
