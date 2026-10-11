#!/usr/bin/env node
/**
 * One bounded A3 local-room browser check through the actual development Host
 * and Join controls. The runtime owns both peer progress and checksum capture;
 * this script only supplies real canvas keyboard events and reads observations.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {parseArgs} from 'node:util';
import {loadBrowserTools} from './browser_tools.mjs';
import {openNetInstance, firstFatalBrowserError} from './net_session_instance.mjs';
import {startRoomRelayRuntime} from './net_room_relay_runtime_owner.mjs';
import {parseRoomInvite} from './net_lockstep_room_invite.mjs';
import {LOCKSTEP_DELAY, LOCKSTEP_MAX_SOURCE_TICKS, parseNetChecksum} from './net_lockstep_core.mjs';

const USAGE = 'node scripts/a3_local_room_keyboard_browser.mjs --url http://127.0.0.1:8787/runtime.html --disc DISC.ciso --out NEW_DIR [--playwright DIR] [--min-checksums 120] [--timeout-ms 300000]';
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, out: {type: 'string'},
  playwright: {type: 'string'}, 'min-checksums': {type: 'string', default: '120'},
  'timeout-ms': {type: 'string', default: '300000'}, help: {type: 'boolean', default: false},
}, allowPositionals: false});
if (values.help) {
  process.stdout.write(USAGE + '\n');
  process.exit(0);
}

function boundedInteger(name, value, min, max) {
  const result = Number(value);
  if (!Number.isSafeInteger(result) || result < min || result > max)
    throw Error('--' + name + ' must be an integer from ' + min + ' through ' + max);
  return result;
}
function requireCondition(condition, message) {
  if (!condition) throw Error(message);
}
function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}
function stableJson(value) {
  if (Array.isArray(value)) return '[' + value.map(stableJson).join(',') + ']';
  if (value && typeof value === 'object')
    return '{' + Object.keys(value).sort().map(key => JSON.stringify(key) + ':' + stableJson(value[key])).join(',') + '}';
  return JSON.stringify(value);
}
function gitValue(args) {
  return execFileSync('git', args, {encoding: 'utf8'}).trim();
}
function sleep(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}
function errorText(error) {
  return String(error?.stack || error?.message || error);
}

const minimumChecksums = boundedInteger('min-checksums', values['min-checksums'], 16, 512);
const timeoutMs = boundedInteger('timeout-ms', values['timeout-ms'], 60000, 600000);
if (!values.url || !values.disc || !values.out)
  throw Error('Required: ' + USAGE);
const runtimeUrl = new URL(values.url);
requireCondition(runtimeUrl.protocol === 'http:' && runtimeUrl.hostname === '127.0.0.1' &&
  runtimeUrl.port === '8787' && runtimeUrl.pathname === '/runtime.html',
  'The direct Worker allowlist requires the owned runtime page at http://127.0.0.1:8787/runtime.html');
const discPath = path.resolve(values.disc);
const discInfo = await fs.stat(discPath);
requireCondition(discInfo.isFile() && discInfo.size > 0, '--disc must name a nonempty local file');

const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});
const finalDeadline = Date.now() + timeoutMs;
const cleanupReserveMs = 30000;
const operationDeadline = finalDeadline - cleanupReserveMs;
const remaining = () => {
  const value = operationDeadline - Date.now();
  if (value <= 0) throw Error('Local room keyboard scenario exceeded its operation deadline');
  return Math.max(1, Math.floor(value));
};
const cleanupRemaining = () => {
  const value = finalDeadline - Date.now();
  if (value <= 0) throw Error('Local room cleanup deadline expired');
  return Math.max(1, Math.floor(value));
};
async function cleanupBounded(promise, label) {
  let timer;
  try {
    return await Promise.race([Promise.resolve(promise), new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error(label + ' exceeded the cleanup reserve')), cleanupRemaining());
    })]);
  } finally { clearTimeout(timer); }
}
const report = {
  schema: 'melee-web-a3-local-room-keyboard-v1',
  evidence_scope: 'local functional browser check only; public Worker, full match, human/physical input, Internet, timing, pixel/PCM equivalence and whole-session gates remain pending',
  decision: 'FAIL',
  first_error: null,
  source: null,
  browser: null,
  room_worker: null,
  invitation: null,
  profiles: {},
  css_keyboard: {},
  checksum_comparison: null,
  leave_and_cleanup: {},
  exclusions: ['no direct PAD or Gamepad injection', 'no harness-owned peer or checksum pump',
    'no runtime source-tick/checksum-bound changes', 'one local Worker room only'],
  started_at: new Date().toISOString(),
};
let worker = null;
const instances = {alpha: null, beta: null};
let workerPassed = false;

function sourceIdentity() {
  const status = gitValue(['status', '--porcelain', '--untracked-files=all']);
  const unexpected = status.split('\n').filter(Boolean).filter(row =>
    !row.endsWith('scripts/a3_local_room_keyboard_browser.mjs'));
  requireCondition(unexpected.length === 0,
    'Checkout contains changes outside this harness: ' + JSON.stringify(unexpected));
  return {head: gitValue(['rev-parse', 'HEAD']), tree: gitValue(['rev-parse', 'HEAD^{tree}']),
    status_porcelain: status, allowed_harness_only_change: status.length > 0};
}

async function setupKeyboard(instance, role) {
  const page = instance.page;
  await page.locator('#controls-open').click({timeout: remaining()});
  await page.locator('#keyboard-layout').waitFor({state: 'visible', timeout: remaining()});
  await page.locator('#keyboard-layout').selectOption('two', {timeout: remaining()});
  const expected = role === 'alpha' ? ['keyboard', 'off'] : ['off', 'keyboard'];
  await page.locator('#player-one-source').selectOption(expected[0], {timeout: remaining()});
  await page.locator('#player-two-source').selectOption(expected[1], {timeout: remaining()});
  const actual = await page.evaluate(() => ({
    layout: document.querySelector('#keyboard-layout')?.value,
    player_one: document.querySelector('#player-one-source')?.value,
    player_two: document.querySelector('#player-two-source')?.value,
  }));
  requireCondition(actual.layout === 'two' && actual.player_one === expected[0] &&
    actual.player_two === expected[1], role + ' Controls did not retain the intended keyboard port');
  await page.locator('#controls-close').click({timeout: remaining()});
  return actual;
}

async function readRoomState(instance) {
  return instance.page.evaluate(() => {
    const module = globalThis.Module;
    const status = document.querySelector('#status');
    const ownerHealth = globalThis.meleeNetRuntimeLockstepHealth?.() ?? null;
    const nativeStatus = globalThis.__net?.status?.() ?? null;
    return {
      runtime_error: status?.dataset.runtimeError || null,
      menu_phase: typeof module?._melee_web_native_menu_phase === 'function'
        ? module._melee_web_native_menu_phase() : null,
      menu_running: typeof module?._melee_web_native_menu_running === 'function'
        ? module._melee_web_native_menu_running() : null,
      native_status: nativeStatus,
      owner_health: ownerHealth,
    };
  });
}

async function readCssCursor(instance, port) {
  return instance.page.evaluate(port => {
    const module = globalThis.Module;
    if (!module || typeof module._melee_web_css_observe_port !== 'function' ||
        typeof module._malloc !== 'function' || typeof module._free !== 'function' ||
        !module.HEAP32 || !module.HEAPF32)
      throw Error('Existing read-only CSS port observer is unavailable');
    if (module._melee_web_native_menu_phase() !== 1)
      throw Error('CSS cursor observation requires the live original CSS');
    const ids = module._malloc(14 * 4);
    const geometry = module._malloc(8 * 4);
    if (!ids || !geometry) {
      if (ids) module._free(ids);
      if (geometry) module._free(geometry);
      throw Error('CSS observer scratch allocation failed');
    }
    try {
      if (!module._melee_web_css_observe_port(port, 8, ids, geometry))
        throw Error('Existing CSS observer refused port ' + port);
      return {
        port,
        ids: Array.from(module.HEAP32.subarray(ids >> 2, (ids >> 2) + 14)),
        geometry: Array.from(module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8)),
        cursor: {x: module.HEAPF32[geometry >> 2], y: module.HEAPF32[(geometry >> 2) + 1]},
      };
    } finally {
      module._free(ids);
      module._free(geometry);
    }
  }, port);
}

async function readRoomCursors(port) {
  const rows = await Promise.all([
    readCssCursor(instances.alpha, port), readCssCursor(instances.beta, port),
  ]);
  return {alpha: rows[0], beta: rows[1]};
}

async function readInputDiagnostics(instance) {
  return instance.page.evaluate(() => {
    const module = globalThis.Module;
    if (!module || typeof module._melee_web_input_message !== 'function' ||
        typeof module.UTF8ToString !== 'function')
      throw Error('Existing read-only keyboard/PAD diagnostics are unavailable');
    const text = module.UTF8ToString(module._melee_web_input_message());
    return JSON.parse(text);
  });
}

async function readPeerSnapshot(instance, inputFrom = null) {
  return instance.page.evaluate(inputFrom => {
    const snapshot = globalThis.meleeNetRuntimeLockstepSnapshot?.();
    const health = globalThis.meleeNetRuntimeLockstepHealth?.(
      inputFrom === null ? undefined : {inputFrom});
    const nativeStatus = globalThis.__net?.status?.() ?? null;
    const module = globalThis.Module;
    return {
      snapshot,
      health,
      native_status: nativeStatus,
      menu_phase: module?._melee_web_native_menu_phase?.() ?? null,
      menu_running: module?._melee_web_native_menu_running?.() ?? null,
      runtime_error: document.querySelector('#status')?.dataset.runtimeError || null,
    };
  }, inputFrom);
}

async function readBrowserProcessInfo(instance, role) {
  const browser = instance.context.browser();
  requireCondition(browser && typeof browser.newBrowserCDPSession === 'function',
    role + ' persistent Chrome context has no owned Browser CDP session');
  const cdp = await browser.newBrowserCDPSession();
  try {
    const result = await cdp.send('SystemInfo.getProcessInfo');
    requireCondition(Array.isArray(result?.processInfo) && result.processInfo.length > 0,
      role + ' Chrome CDP returned no process inventory');
    return result.processInfo;
  } finally {
    await cdp.detach();
  }
}

async function waitForChecksumConsumersSettled(minimum) {
  while (Date.now() < operationDeadline) {
    const rows = await Promise.all([
      readPeerSnapshot(instances.alpha), readPeerSnapshot(instances.beta),
    ]);
    for (const [index, role] of ['alpha', 'beta'].entries()) {
      const row = rows[index];
      const failure = protocolFailure({owner_health: row.health, runtime_error: row.runtime_error}, role);
      if (failure) throw Error(failure);
    }
    const settled = rows.every(row => {
      const peerHealth = row.health?.peer;
      const protocol = peerHealth?.protocol;
      const healthConsumer = peerHealth?.checksumConsumer;
      const snapshotConsumer = row.snapshot?.peer?.checksumConsumer;
      const count = protocol?.local_checksum_ticks;
      return Number.isSafeInteger(count) && count >= minimum &&
        healthConsumer?.enabled === true && healthConsumer.pending_batch === false &&
        healthConsumer.accepted_records === count &&
        row.health?.checksum_record_count === count &&
        row.snapshot?.checksums?.length === count &&
        snapshotConsumer?.pending_batch === null && snapshotConsumer.accepted_records === count;
    });
    if (settled) return rows;
    await sleep(25);
  }
  throw Error('Runtime-owned checksum consumers did not settle at a complete common boundary');
}

function protocolFailure(row, role) {
  const health = row?.owner_health;
  const peer = health?.peer;
  const protocol = peer?.protocol;
  if (row?.runtime_error) return role + ' runtime error: ' + row.runtime_error;
  if (health?.failure) return role + ' runtime-owner failure: ' + health.failure;
  if (peer?.failure) return role + ' peer failure: ' + peer.failure;
  if (protocol?.terminal) return role + ' lockstep terminal: ' + JSON.stringify(protocol.terminal);
  if (peer?.endpoint_error_count) return role + ' relay endpoint reported errors';
  return null;
}

async function waitForBothCssAndProgress(minimum) {
  while (Date.now() < operationDeadline) {
    const states = await Promise.all([readRoomState(instances.alpha), readRoomState(instances.beta)]);
    for (const [index, role] of ['alpha', 'beta'].entries()) {
      const failure = protocolFailure(states[index], role);
      if (failure) throw Error(failure);
    }
    const ready = states.every((row, index) => {
      const protocol = row.owner_health?.peer?.protocol;
      return row.menu_phase === 1 && row.menu_running === 1 &&
        row.native_status?.active === 1 &&
        protocol?.ready === true && protocol.role === (index === 0 ? 'alpha' : 'beta') &&
        protocol.local_port === (index === 0 ? 0 : 1) &&
        protocol.local_checksum_ticks >= minimum &&
        protocol.remote_checksum_ticks >= minimum &&
        protocol.next_checksum_compare >= minimum &&
        protocol.checksum_mismatch_count === 0;
    });
    if (ready) return states;
    await sleep(100);
  }
  throw Error('Both runtime-owned peers did not reach the bounded common CSS checksum prefix');
}

async function waitForBothChecksumCompare(target) {
  while (Date.now() < operationDeadline) {
    const states = await Promise.all([readRoomState(instances.alpha), readRoomState(instances.beta)]);
    for (const [index, role] of ['alpha', 'beta'].entries()) {
      const failure = protocolFailure(states[index], role);
      if (failure) throw Error(failure);
    }
    const reached = states.every(row => {
      const protocol = row.owner_health?.peer?.protocol;
      return row.menu_phase === 1 && row.menu_running === 1 &&
        protocol?.ready === true && protocol.next_checksum_compare >= target &&
        protocol.checksum_mismatch_count === 0;
    });
    if (reached) return states;
    await sleep(50);
  }
  throw Error('Both peers did not advance the common checksum boundary after keyboard input');
}

function recordHex(record) {
  requireCondition(Array.isArray(record) && record.length === 64 &&
    record.every(value => Number.isInteger(value) && value >= 0 && value <= 255),
    'Runtime-owned checksum record is not one complete 64-byte record');
  return Buffer.from(record).toString('hex');
}

function compareSnapshotRecords(alpha, beta, minimum) {
  const left = alpha?.snapshot?.checksums;
  const right = beta?.snapshot?.checksums;
  const alphaCompared = alpha?.health?.peer?.protocol?.next_checksum_compare;
  const betaCompared = beta?.health?.peer?.protocol?.next_checksum_compare;
  requireCondition(Array.isArray(left) && Array.isArray(right),
    'Runtime owner did not expose its retained native checksum records');
  requireCondition(Number.isSafeInteger(alphaCompared) && Number.isSafeInteger(betaCompared),
    'Runtime peer did not expose its common checksum comparison boundary');
  const common = Math.min(left.length, right.length, alphaCompared, betaCompared);
  requireCondition(common >= minimum, 'Common checksum prefix is shorter than the requested bound');
  const rows = [];
  for (let tick = 0; tick < common; ++tick) {
    const leftBytes = Uint8Array.from(left[tick] || []);
    const rightBytes = Uint8Array.from(right[tick] || []);
    const leftParsed = parseNetChecksum(leftBytes);
    const rightParsed = parseNetChecksum(rightBytes);
    requireCondition(leftParsed.tick === tick && rightParsed.tick === tick,
      'Runtime checksum sequence is not contiguous at source tick ' + tick);
    const leftHex = recordHex(left[tick]);
    const rightHex = recordHex(right[tick]);
    requireCondition(leftHex === rightHex,
      'Runtime-owned native checksums first differ at source tick ' + tick);
    rows.push({source_tick: tick, bytes: 64, alpha_raw_hex: leftHex, beta_raw_hex: rightHex,
      sha256_alpha: sha256(Buffer.from(left[tick])),
      sha256_beta: sha256(Buffer.from(right[tick])), parsed: leftParsed});
  }
  const retainTail = rows => rows.map((raw, offset) => {
    const parsed = parseNetChecksum(Uint8Array.from(raw));
    return {source_tick: parsed.tick, raw_hex: recordHex(raw)};
  });
  return {
    compared_source_ticks: common,
    exact_record_count_each: common,
    source_tick_range: [0, common - 1],
    records: rows,
    unpaired_tail: {
      alpha: {count: left.length - common, records: retainTail(left.slice(common))},
      beta: {count: right.length - common, records: retainTail(right.slice(common))},
    },
    owner_counts: {
      alpha: {retained: left.length, local: alpha.health?.peer?.protocol?.local_checksum_ticks,
        remote: alpha.health?.peer?.protocol?.remote_checksum_ticks,
        compared: alpha.health?.peer?.protocol?.next_checksum_compare},
      beta: {retained: right.length, local: beta.health?.peer?.protocol?.local_checksum_ticks,
        remote: beta.health?.peer?.protocol?.remote_checksum_ticks,
        compared: beta.health?.peer?.protocol?.next_checksum_compare},
    },
  };
}

async function captureScreenshot(instance, filename) {
  const pathOnDisk = path.join(output, filename);
  await instance.page.locator('#canvas').scrollIntoViewIfNeeded({timeout: remaining()});
  await instance.screenshot(pathOnDisk);
  const bytes = await fs.readFile(pathOnDisk);
  requireCondition(bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])),
    'Browser screenshot is not a PNG');
  return {file: filename, bytes: bytes.length, sha256: sha256(bytes)};
}

async function waitForCursorChange(instance, port, before) {
  while (Date.now() < operationDeadline) {
    const after = await readCssCursor(instance, port);
    if (Math.hypot(after.cursor.x - before.cursor.x, after.cursor.y - before.cursor.y) > 0.01)
      return after;
    await sleep(50);
  }
  throw Error('Actual canvas keyboard input did not move CSS port ' + port + ' source cursor');
}

async function leaveThroughUi(instance, role) {
  if (!instance) return {attempted: false, reason: 'browser instance was not created'};
  const page = instance.page;
  const leave = page.locator('#lockstep-leave');
  const enabled = await leave.isEnabled().catch(() => false);
  if (!enabled) return {attempted: false, reason: 'Stop / leave control was not enabled'};
  await leave.click({timeout: Math.max(1, Math.min(cleanupRemaining(), 10000))});
  await page.waitForFunction(() =>
    document.querySelector('#lockstep-status')?.textContent?.trim() ===
      'Disconnected by your choice. Route outcome is not assessed.',
  null, {timeout: Math.max(1, Math.min(cleanupRemaining(), 15000))});
  const state = await readPeerSnapshot(instance);
  requireCondition(state.snapshot?.close_mode === 'normal' &&
    state.snapshot?.peer?.endpointClosed === true &&
    state.snapshot?.transport_cleanup?.closed === true,
    role + ' runtime owner did not confirm normal relay teardown');
  return {attempted: true, status: 'Disconnected by your choice. Route outcome is not assessed.',
    close_mode: state.snapshot.close_mode,
    endpoint_closed: state.snapshot.peer.endpointClosed,
    transport_cleanup: state.snapshot.transport_cleanup};
}

async function unloadRuntime(instance, role) {
  if (!instance) return {attempted: false, reason: 'browser instance was not created'};
  try {
    await instance.unload();
    const native = await instance.native();
    requireCondition(native.phase === 0 && native.running === 0,
      role + ' runtime did not unload to the stopped phase');
    return {attempted: true, phase: native.phase, running: native.running};
  } catch (error) {
    return {attempted: true, error: errorText(error)};
  }
}

function firstError(message) {
  if (!report.first_error) report.first_error = message;
}
async function writeReport() {
  report.finished_at = new Date().toISOString();
  report.decision = report.first_error ? 'FAIL' : 'PASS';
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}

try {
  report.source = sourceIdentity();
  report.browser = {node: process.version, url: runtimeUrl.href,
    speaker_output: 'muted through the shared browserLaunchOptions policy; normal audio processing retained',
    profile_model: 'one independent persistent Chrome context per player'};
  worker = await startRoomRelayRuntime({
    evidenceDir: path.join(output, 'worker-evidence'),
    producer: {commit: report.source.head, tree: report.source.tree},
    diagnostic: message => {
      if (!report.worker_diagnostics) report.worker_diagnostics = [];
      report.worker_diagnostics.push(String(message));
    },
  });
  report.room_worker = {base: worker.base, port: worker.port, owner_process: worker.ownerProcess,
    runtime_identity: worker.runtimeIdentity, hashes: worker.hashes};
  const tools = await loadBrowserTools(values.playwright);
  const playwrightPackage = JSON.parse(await fs.readFile(path.join(tools.playwrightPath, 'package.json'), 'utf8'));
  const profileRoot = path.join(output, 'profiles');
  const openRole = async role => openNetInstance({
    chromium: tools.chromium,
    launchOptions: tools.browser,
    url: runtimeUrl.href,
    disc: discPath,
    userDataDir: path.join(profileRoot, role),
    label: role,
    timeoutMs: Math.min(120000, remaining()),
    deadline: finalDeadline,
    runtimeOwned: true,
    syntheticGamepad: null,
  });
  instances.alpha = await openRole('alpha');
  instances.beta = await openRole('beta');
  report.browser = {...report.browser, executable: tools.browserPath,
    playwright_path: tools.playwrightPath, playwright_version: playwrightPackage.version,
    headless: true, audio_muted: true, audio_processing_disabled: false,
    chrome_version: instances.alpha.browserVersion,
    profile_dirs: {alpha: 'profiles/alpha', beta: 'profiles/beta'}};
  await Promise.all([instances.alpha.importDisc(), instances.beta.importDisc()]);
  const identities = await Promise.all([instances.alpha.peerIdentity(), instances.beta.peerIdentity()]);
  requireCondition(stableJson(identities[0]) === stableJson(identities[1]),
    'The two prepared browser profiles loaded different runtime/disc identities');
  const wasm = await Promise.all([instances.alpha.freezeLoadedWasmIdentity(),
    instances.beta.freezeLoadedWasmIdentity()]);
  requireCondition(wasm[0].sha256 === wasm[1].sha256,
    'The two independent profiles received different gameplay Wasm bytes');
  report.profiles = {
    alpha: {identity: identities[0], wasm_response: wasm[0],
      graphics: await instances.alpha.graphics(),
      controls: await setupKeyboard(instances.alpha, 'alpha')},
    beta: {identity: identities[1], wasm_response: wasm[1],
      graphics: await instances.beta.graphics(),
      controls: await setupKeyboard(instances.beta, 'beta')},
  };
  for (const role of ['alpha', 'beta']) {
    const graphics = report.profiles[role].graphics;
    requireCondition(graphics?.cross_origin_isolated === true &&
      graphics.canvas?.width === 640 && graphics.canvas?.height === 480,
      role + ' browser did not retain the isolated 640x480 runtime canvas');
  }

  const relayUrl = 'ws://127.0.0.1:' + worker.port;
  await instances.alpha.page.locator('#lockstep-relay-url').fill(relayUrl, {timeout: remaining()});
  await instances.alpha.page.locator('#lockstep-host').click({timeout: remaining()});
  await instances.alpha.page.waitForFunction(() =>
    document.querySelector('#lockstep-host-invite')?.value?.length > 0,
  null, {timeout: remaining()});
  const inviteText = await instances.alpha.page.locator('#lockstep-host-invite').inputValue();
  const invite = parseRoomInvite(inviteText);
  requireCondition(invite.url === relayUrl && invite.sourceTicks === LOCKSTEP_MAX_SOURCE_TICKS &&
    invite.inputDelay === LOCKSTEP_DELAY,
    'Actual Host control produced an invite outside the local Worker or current source-tick contract');
  await instances.alpha.page.locator('#lockstep-copy').click({timeout: remaining()});
  await instances.beta.page.locator('#lockstep-join-invite').fill(inviteText, {timeout: remaining()});
  requireCondition(await instances.beta.page.locator('#lockstep-join-invite').inputValue() === inviteText,
    'The Join invite field did not retain the exact Host-generated invite');
  await instances.beta.page.locator('#lockstep-join').click({timeout: remaining()});
  report.invitation = {host_control_clicked: true, copy_control_clicked: true, join_control_clicked: true,
    exact_invite_transferred: true, invite_chars: inviteText.length,
    invite_sha256: sha256(Buffer.from(inviteText, 'utf8')), url: invite.url, room_id: invite.roomId,
    seed: invite.seed, source_ticks: invite.sourceTicks, input_delay: invite.inputDelay};
  const connectedStates = await waitForBothCssAndProgress(minimumChecksums);
  for (const [index, role] of ['alpha', 'beta'].entries()) {
    requireCondition(connectedStates[index].menu_phase === 1 &&
      connectedStates[index].owner_health?.peer?.protocol?.ready === true,
      role + ' did not reach connected original CSS');
    await instances[role].page.locator('#canvas').focus({timeout: remaining()});
    requireCondition(await instances[role].page.evaluate(() => document.activeElement?.id === 'canvas'),
      role + ' canvas did not retain focus after actual Host/Join connection');
  }
  report.canvas_focus_after_connection = {alpha: true, beta: true};

  const roleSpecs = [
    {role: 'alpha', port: 0, key: 'd', label: 'P1 D / right'},
    {role: 'beta', port: 1, key: 'ArrowRight', label: 'P2 right arrow'},
  ];
  for (const spec of roleSpecs) {
    const instance = instances[spec.role];
    const beforeState = await readRoomState(instance);
    const protocol = beforeState.owner_health?.peer?.protocol;
    requireCondition(protocol?.role === spec.role && protocol.local_port === spec.port,
      spec.role + ' runtime owner did not retain its authored local port');
    const cursorBefore = await readRoomCursors(spec.port);
    const roomBefore = await Promise.all([readRoomState(instances.alpha), readRoomState(instances.beta)]);
    const commonBoundaryBefore = Math.min(...roomBefore.map(row =>
      row.owner_health?.peer?.protocol?.next_checksum_compare ?? -1));
    requireCondition(Number.isSafeInteger(commonBoundaryBefore) && commonBoundaryBefore >= minimumChecksums,
      'Common source checksum boundary was unavailable before keyboard input');
    const inputCount = beforeState.owner_health?.peer?.localInputCapture?.captured_count;
    const nativeCursorBefore = beforeState.native_status?.cursor;
    requireCondition(Number.isSafeInteger(inputCount) && Number.isSafeInteger(nativeCursorBefore) &&
      (inputCount === nativeCursorBefore || inputCount === nativeCursorBefore + 1),
      spec.role + ' live PAD capture count is outside the current native cursor boundary');
    const inputBefore = await readInputDiagnostics(instance);
    requireCondition(inputBefore.keyboard_layout === 0 &&
      (inputBefore.keyboard_active_mask & (1 << spec.port)) !== 0 &&
      inputBefore.pads?.[spec.port]?.source === 'keyboard',
      spec.role + ' runtime does not report the expected active keyboard source');
    const screenshotsBefore = await Promise.all([
      captureScreenshot(instances.alpha, spec.role + '-css-alpha-before.png'),
      captureScreenshot(instances.beta, spec.role + '-css-beta-before.png'),
    ]);
    await instance.driver.pressChord([spec.key], {holdMs: 250, releaseMs: 120});
    const canvasFocused = await instance.page.evaluate(() => document.activeElement?.id === 'canvas');
    requireCondition(canvasFocused, spec.role + ' real keyboard path did not focus the original canvas');
    const cursorAfter = await Promise.all([
      waitForCursorChange(instances.alpha, spec.port, cursorBefore.alpha),
      waitForCursorChange(instances.beta, spec.port, cursorBefore.beta),
    ]);
    const after = {alpha: cursorAfter[0], beta: cursorAfter[1]};
    const readback = await readPeerSnapshot(instance, inputCount);
    const inputAfter = await readInputDiagnostics(instance);
    const captures = readback.health?.peer?.localInputCapture?.captures_since;
    requireCondition(Array.isArray(captures) && captures.length > 0,
      spec.role + ' runtime did not retain local browser keyboard PAD samples');
    requireCondition(captures.every((row, index) => row.source_cursor === inputCount + index &&
      row.input_tick === inputCount + index && row.local_port === spec.port &&
      Array.isArray(row.bytes) && row.bytes.length === 11),
      spec.role + ' runtime local PAD capture has a gap, wrong port, or malformed record');
    const movedSample = captures.find(row => row.bytes[2] !== 0 || row.bytes[3] !== 0);
    requireCondition(movedSample, spec.role + ' actual keyboard event did not appear in source-owned PAD bytes');
    const requiredChecksumBoundary = movedSample.input_tick + invite.inputDelay + 1;
    const settledProgress = await waitForBothChecksumCompare(requiredChecksumBoundary);
    requireCondition(inputAfter.last_non_neutral?.source === 'keyboard' &&
      inputAfter.last_non_neutral?.port === spec.port &&
      inputAfter.keyboard_active_mask & (1 << spec.port),
      spec.role + ' source input diagnostic did not identify the expected keyboard port');
    const screenshotsAfter = await Promise.all([
      captureScreenshot(instances.alpha, spec.role + '-css-alpha-after.png'),
      captureScreenshot(instances.beta, spec.role + '-css-beta-after.png'),
    ]);
    const geometryDelta = {
      alpha: {x: after.alpha.cursor.x - cursorBefore.alpha.cursor.x,
        y: after.alpha.cursor.y - cursorBefore.alpha.cursor.y},
      beta: {x: after.beta.cursor.x - cursorBefore.beta.cursor.x,
        y: after.beta.cursor.y - cursorBefore.beta.cursor.y},
    };
    requireCondition(Object.values(geometryDelta).every(delta => Math.hypot(delta.x, delta.y) > 0.01),
      spec.role + ' keyboard input did not move the same source port on both peers');
    report.css_keyboard[spec.role] = {
      local_port: spec.port, key: spec.label,
      canvas_focused_for_keyboard: canvasFocused,
      input_source_before: inputBefore.pads[spec.port].source,
      input_source_after: inputAfter.last_non_neutral.source,
      cursor_before_by_peer: cursorBefore, cursor_after_by_peer: after, geometry_delta_by_peer: geometryDelta,
      screenshots_before_by_peer: {alpha: screenshotsBefore[0], beta: screenshotsBefore[1]},
      screenshots_after_by_peer: {alpha: screenshotsAfter[0], beta: screenshotsAfter[1]},
      retained_input_sample_count: captures.length,
      first_non_neutral_sample: movedSample,
      source_input_diagnostic_after: inputAfter.last_non_neutral,
      source_tick_before: {alpha: roomBefore[0].native_status.cursor, beta: roomBefore[1].native_status.cursor},
      source_tick_after: {alpha: settledProgress[0].native_status.cursor,
        beta: settledProgress[1].native_status.cursor},
      common_checksum_boundary_before: commonBoundaryBefore,
      common_checksum_boundary_after: settledProgress.map(row => row.owner_health.peer.protocol.next_checksum_compare),
      moved_sample_input_tick: movedSample.input_tick,
      invite_input_delay: invite.inputDelay,
      required_checksum_boundary_after_movement: requiredChecksumBoundary,
      keyboard_path: 'Playwright page.keyboard through existing browser driver; no PAD/Gamepad injection',
    };
  }

  const progress = await waitForBothCssAndProgress(minimumChecksums);
  const [alphaSnapshot, betaSnapshot] = await waitForChecksumConsumersSettled(minimumChecksums);
  for (const [index, role] of ['alpha', 'beta'].entries()) {
    const snapshot = index === 0 ? alphaSnapshot : betaSnapshot;
    const failure = protocolFailure({owner_health: snapshot.health, runtime_error: snapshot.runtime_error}, role);
    requireCondition(!failure, failure || role + ' owner reported an error');
    requireCondition(snapshot.menu_phase === 1 && snapshot.menu_running === 1 &&
      snapshot.native_status?.active === 1,
      role + ' left the original CSS source before checksum capture');
    requireCondition(snapshot.snapshot?.peer?.protocol?.role === role,
      role + ' runtime snapshot role changed');
    requireCondition(snapshot.snapshot.checksums.length === snapshot.snapshot.peer.protocol.local_checksum_ticks,
      role + ' retained checksum count disagrees with runtime peer progress');
    requireCondition(snapshot.health?.peer?.protocol?.checksum_mismatch_count === 0 &&
      Array.isArray(snapshot.snapshot.peer.protocol.checksum_mismatches) &&
      snapshot.snapshot.peer.protocol.checksum_mismatches.length === 0 &&
      snapshot.snapshot.peer.protocol.native_local_divergences === 0,
      role + ' runtime peer reported a native or checksum mismatch');
  }
  report.checksum_comparison = compareSnapshotRecords(alphaSnapshot, betaSnapshot, minimumChecksums);
  report.checksum_comparison.requested_minimum = minimumChecksums;
  report.checksum_comparison.freeze_before_leave = true;
  report.checksum_comparison.final_status = {alpha: alphaSnapshot.native_status,
    beta: betaSnapshot.native_status};
  report.progress_wait = progress.map(row => ({
    cursor: row.native_status.cursor, phase: row.menu_phase,
    role: row.owner_health.peer.protocol.role,
    local_port: row.owner_health.peer.protocol.local_port,
    local_checksum_ticks: row.owner_health.peer.protocol.local_checksum_ticks,
    remote_checksum_ticks: row.owner_health.peer.protocol.remote_checksum_ticks,
    next_checksum_compare: row.owner_health.peer.protocol.next_checksum_compare,
  }));
  workerPassed = true;
} catch (error) {
  firstError(errorText(error));
} finally {
  const processInfo = {};
  for (const role of ['alpha', 'beta']) {
    const instance = instances[role];
    if (!instance) continue;
    try {
      processInfo[role] = await cleanupBounded(readBrowserProcessInfo(instance, role), role + ' Chrome process inventory');
    } catch (error) {
      processInfo[role] = {error: errorText(error)};
      firstError(role + ' Chrome process inventory failed: ' + errorText(error));
    }
  }
  report.browser_process_info_by_role = processInfo;
  report.browser_process_info = Object.entries(processInfo).flatMap(([role, rows]) =>
    Array.isArray(rows) ? rows.map(row => ({...row, browser_role: role})) : []);
  for (const role of ['alpha', 'beta']) {
    const instance = instances[role];
    if (!instance) continue;
    try {
      const result = await cleanupBounded(leaveThroughUi(instance, role), role + ' UI Stop / leave');
      report.leave_and_cleanup[role + '_leave'] = result;
      if (workerPassed && !result.attempted)
        firstError(role + ' did not perform an explicit UI Stop / leave');
    } catch (error) {
      report.leave_and_cleanup[role + '_leave'] = {attempted: true, error: errorText(error)};
      firstError(role + ' UI Stop / leave failed: ' + errorText(error));
    }
  }
  for (const role of ['alpha', 'beta']) {
    const instance = instances[role];
    if (!instance) continue;
    try {
      report.leave_and_cleanup[role + '_unload'] = await cleanupBounded(
        unloadRuntime(instance, role), role + ' runtime unload');
    } catch (error) {
      report.leave_and_cleanup[role + '_unload'] = {attempted: true, error: errorText(error)};
    }
    if (report.leave_and_cleanup[role + '_unload'].error)
      firstError(role + ' runtime unload failed: ' + report.leave_and_cleanup[role + '_unload'].error);
    try {
      const fatal = firstFatalBrowserError(instance.errors);
      if (fatal) firstError(role + ' browser error: ' + JSON.stringify(fatal));
      report.leave_and_cleanup[role + '_browser_errors'] = instance.errors;
      report.leave_and_cleanup[role + '_context_close'] = await cleanupBounded(
        instance.close(), role + ' Chrome context close');
      if (instance.closed !== true) firstError(role + ' persistent Chrome context did not close');
    } catch (error) {
      report.leave_and_cleanup[role + '_context_close_error'] = errorText(error);
      firstError(role + ' Chrome context cleanup failed: ' + errorText(error));
    } finally {
      const nativeCleanup = instance.nativeNetworkCleanup ?? null;
      report.leave_and_cleanup[role + '_native_network_cleanup'] = nativeCleanup;
      if (workerPassed) {
        const result = nativeCleanup?.runtime_lockstep?.result;
        if (nativeCleanup?.errors?.length || result?.native_quiescence !== 'verified' ||
            result?.transport_cleanup?.closed !== true ||
            nativeCleanup?.runtime_lockstep?.after?.peer?.closed !== true)
          firstError(role + ' native runtime did not confirm quiescent disconnect and closed transport');
      }
    }
  }
  if (worker) {
    try {
      report.leave_and_cleanup.worker = await worker.close(!report.first_error && workerPassed);
      const workerCleanup = report.leave_and_cleanup.worker;
      const clean = workerCleanup.ready === true && workerCleanup.disposed === true &&
        workerCleanup.exit?.code === 0 && !workerCleanup.exit?.signal &&
        workerCleanup.processClose && !workerCleanup.groupAlive && !workerCleanup.fallbackUsed &&
        workerCleanup.ownedTempPathsClean === true && workerCleanup.runtimeErrors?.length === 0 &&
        workerCleanup.structuredErrors?.length === 0 && workerCleanup.stderrErrors === false &&
        !workerCleanup.cleanupEvents?.some(row => row.error);
      if (!clean)
        firstError('Local Worker did not verify clean disposal and process-group absence');
    } catch (error) {
      report.leave_and_cleanup.worker_error = errorText(error);
      firstError('Local Worker cleanup failed: ' + errorText(error));
    }
  }
  if (!report.first_error && workerPassed) {
    for (const role of ['alpha', 'beta']) {
      const profile = path.join(output, 'profiles', role);
      try {
        await fs.rm(profile, {recursive: true, force: false});
        report.leave_and_cleanup[role + '_profile_removed_after_success'] = true;
      } catch (error) {
        report.leave_and_cleanup[role + '_profile_remove_error'] = errorText(error);
        firstError(role + ' successful browser profile scratch could not be removed');
      }
    }
    try {
      await fs.rmdir(path.join(output, 'profiles'));
      report.leave_and_cleanup.profile_root_removed_after_success = true;
    } catch (error) {
      report.leave_and_cleanup.profile_root_remove_error = errorText(error);
      firstError('Successful browser profile parent scratch could not be removed');
    }
  } else {
    const profiles = {};
    for (const role of ['alpha', 'beta']) {
      try {
        const info = await fs.stat(path.join(output, 'profiles', role));
        profiles[role] = {present: true, directory: info.isDirectory()};
      } catch (error) {
        profiles[role] = {present: false, reason: error.code || errorText(error)};
      }
    }
    report.leave_and_cleanup.failed_profiles_retained = profiles;
  }
  try {
    await writeReport();
  } catch (error) {
    process.stderr.write('Could not save local room browser report: ' + errorText(error) + '\n');
    firstError('Could not save final report');
  }
}

if (report.first_error) {
  process.stderr.write(report.first_error + '\n');
  process.exitCode = 1;
} else {
  process.stdout.write(JSON.stringify({decision: report.decision,
    compared_source_ticks: report.checksum_comparison?.compared_source_ticks,
    worker_port: report.room_worker?.port,
    output: path.join(output, 'report.json')}) + '\n');
}
