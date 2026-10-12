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
import {performance} from 'node:perf_hooks';
import {parseArgs} from 'node:util';
import {loadBrowserTools} from './browser_tools.mjs';
import {openNetInstance, firstFatalBrowserError} from './net_session_instance.mjs';
import {startRoomRelayRuntime} from './net_room_relay_runtime_owner.mjs';
import {parseRoomInvite} from './net_lockstep_room_invite.mjs';
import {resolveRoomRelayMode} from './a3_public_relay_preflight.mjs';
import {resolveNetworkSimulation, startNetworkSimulation, calibrateNetworkSimulation,
  observeBrowserNetworkPaths, verifyNetworkProxy, closeNetworkProxy} from './a3_network_simulation.mjs';
import {resolvePostCssStallProbe, runPostCssStallProbe} from './a3_network_stall_probe.mjs';
import {LOCKSTEP_DELAY, LOCKSTEP_MAX_SOURCE_TICKS, parseNetChecksum} from './net_lockstep_core.mjs';
import {checksumCompareReached, checksumWaitDiagnostic} from './a3_checksum_wait_diagnostics.mjs';
import {runA3LocalFullRoute} from './a3_local_room_full_route.mjs';
import {retainCombatChecksumCursorPoll} from './a3_local_room_combat_probe.mjs';
import {incidentRecordingUrl, readIncidentRecorderReadiness, requireIncidentRecorderReadiness,
  summarizeRuntimeIncidents} from './a3_runtime_incidents.mjs';

const USAGE = 'node scripts/a3_local_room_keyboard_browser.mjs --url http://127.0.0.1:8787/runtime.html --disc DISC.ciso --out NEW_DIR [--network-profile control|moderate|asymmetric|constrained] [--post-css-stall-probe] [--relay-url wss://STAGING-WORKER] [--relay-preflight READ_ONLY_PREFLIGHT.json] [--playwright DIR] [--min-checksums 120] [--timeout-ms 300000] [--full-route] [--controlled-cut-after-css] [--gameplay-action-probe] [--gameplay-combat-probe] [--gameplay-combat-exchange] [--gameplay-combat-ko] [--gameplay-combat-ko-charged] [--gameplay-combat-ko-prepared] [--repeat-full-route]';
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, out: {type: 'string'},
  'relay-url': {type: 'string'}, 'relay-preflight': {type: 'string'},
  'network-profile': {type: 'string'},
  'post-css-stall-probe': {type: 'boolean', default: false},
  'controlled-cut-after-css': {type: 'boolean', default: false},
  playwright: {type: 'string'}, 'min-checksums': {type: 'string', default: '120'},
  'timeout-ms': {type: 'string', default: '300000'}, 'full-route': {type: 'boolean', default: false},
  'gameplay-action-probe': {type: 'boolean', default: false},
  'gameplay-combat-probe': {type: 'boolean', default: false},
  'gameplay-combat-exchange': {type: 'boolean', default: false},
  'gameplay-combat-ko': {type: 'boolean', default: false},
  'gameplay-combat-ko-charged': {type: 'boolean', default: false},
  'gameplay-combat-ko-prepared': {type: 'boolean', default: false},
  'repeat-full-route': {type: 'boolean', default: false},
  help: {type: 'boolean', default: false},
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
requireCondition(!values['gameplay-action-probe'] || values['full-route'],
  '--gameplay-action-probe requires --full-route');
requireCondition(!values['gameplay-combat-probe'] || values['full-route'],
  '--gameplay-combat-probe requires --full-route');
requireCondition(!values['gameplay-combat-exchange'] ||
  (values['full-route'] && values['gameplay-combat-probe']),
  '--gameplay-combat-exchange requires --full-route and --gameplay-combat-probe');
requireCondition(!(values['gameplay-combat-probe'] && values['gameplay-action-probe']),
  '--gameplay-combat-probe cannot be combined with --gameplay-action-probe');
requireCondition(!values['repeat-full-route'] || values['full-route'],
  '--repeat-full-route requires --full-route');
requireCondition(!(values['repeat-full-route'] && values['gameplay-action-probe']),
  '--repeat-full-route cannot be combined with --gameplay-action-probe');
requireCondition(!(values['repeat-full-route'] && values['gameplay-combat-probe']),
  '--repeat-full-route cannot be combined with --gameplay-combat-probe');
requireCondition(!(values['repeat-full-route'] && values['gameplay-combat-exchange']),
  '--repeat-full-route cannot be combined with --gameplay-combat-exchange');
requireCondition(!(values['repeat-full-route'] &&
    (values['post-css-stall-probe'] || values['controlled-cut-after-css'])),
  '--repeat-full-route cannot be combined with a stall probe or controlled cut');
requireCondition(!values['gameplay-combat-ko'] || values['gameplay-combat-exchange'],
  '--gameplay-combat-ko requires --gameplay-combat-exchange');
requireCondition(!values['gameplay-combat-ko-charged'] || values['gameplay-combat-exchange'],
  '--gameplay-combat-ko-charged requires --gameplay-combat-exchange');
requireCondition(!values['gameplay-combat-ko-prepared'] || values['gameplay-combat-exchange'],
  '--gameplay-combat-ko-prepared requires --gameplay-combat-exchange');
requireCondition([values['gameplay-combat-ko'], values['gameplay-combat-ko-charged'],
  values['gameplay-combat-ko-prepared']].filter(Boolean).length <= 1,
  'C-stick, charged, and prepared combat KO modes are separate scenarios');
const runtimeUrl = incidentRecordingUrl(values.url);
requireCondition(runtimeUrl.protocol === 'http:' && runtimeUrl.hostname === '127.0.0.1' &&
  runtimeUrl.port === '8787' && runtimeUrl.pathname === '/runtime.html',
  'This harness requires the already-supported client page http://127.0.0.1:8787/runtime.html');
const simulationConfiguration = resolveNetworkSimulation({profileName: values['network-profile'],
  relayUrl: values['relay-url'], preflight: values['relay-preflight']});
let relayPreflightInput;
let relayPreflightSha256 = null;
if (values['relay-preflight'] !== undefined) {
  const preflightBytes = await fs.readFile(path.resolve(values['relay-preflight']));
  requireCondition(preflightBytes.length > 0 && preflightBytes.length <= 65536,
    '--relay-preflight must be a nonempty JSON file no larger than 64 KiB');
  try { relayPreflightInput = JSON.parse(preflightBytes.toString('utf8')); }
  catch (error) { throw Error('--relay-preflight is not valid JSON: ' + errorText(error)); }
  relayPreflightSha256 = sha256(preflightBytes);
}
const relayMode = resolveRoomRelayMode({relayUrl: values['relay-url'], preflight: relayPreflightInput,
  clientOrigin: runtimeUrl.origin});
const postCssStallProbe = resolvePostCssStallProbe({
  enabled: values['post-css-stall-probe'], profileName: values['network-profile'],
  simulationMode: simulationConfiguration?.mode, relayUrl: values['relay-url'],
  relayPreflight: values['relay-preflight'], fullRoute: values['full-route'],
});
if (values['controlled-cut-after-css'] && (simulationConfiguration?.profile_name !== 'moderate' ||
    values['full-route'] || values['relay-url'] || values['relay-preflight'] || postCssStallProbe))
  throw Error('--controlled-cut-after-css requires simulated moderate CSS, without stall or public relay');
if (values['gameplay-combat-probe'] &&
    (simulationConfiguration?.mode !== 'simulated-network' ||
     simulationConfiguration?.profile_name !== 'moderate' ||
     values['relay-url'] || values['relay-preflight'] ||
     values['post-css-stall-probe'] || values['controlled-cut-after-css']))
  throw Error('--gameplay-combat-probe requires only the simulated moderate full-route scenario');
if (values['gameplay-combat-exchange'])
  requireCondition(simulationConfiguration?.mode === 'simulated-network' &&
    simulationConfiguration?.profile_name === 'moderate' && !values['relay-url'] &&
    !values['relay-preflight'] && !values['post-css-stall-probe'] &&
    !values['controlled-cut-after-css'] && !values['gameplay-action-probe'] &&
    !values['repeat-full-route'],
  '--gameplay-combat-exchange requires only its simulated moderate combat-probe scenario');
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
  relay_mode: simulationConfiguration?.mode ?? relayMode.mode,
  evidence_scope: simulationConfiguration
    ? 'same-machine simulated-network functional browser ' + (values['full-route'] ? 'full-route attempt' : 'CSS check') +
      (postCssStallProbe ? ' with an opt-in bounded post-CSS controlled-stall probe' : '') +
      (values['gameplay-combat-probe'] ? ' with a bounded P2 inward approach and jab probe' : '') +
      (values['gameplay-combat-exchange'] ? ' plus one reverse jab and bounded shield exchange' : '') +
      ' using actual Host/Join and keyboard input, real TCP proxy sockets and an owned local Worker; public routing, deployed staging, physical input, two-machine, timing, pixel/PCM equivalence and whole-session acceptance remain pending'
    : relayMode.mode === 'external'
    ? (values['full-route']
      ? 'functional browser route attempt using a supplied public staging Worker preflight and the existing actual Host/Join keyboard path; the harness validates preflight structure/bindings only and does not independently verify the deployed service; RTT, human/physical-input, two-machine/Internet, timing, pixel/PCM-equivalence and whole-session acceptance remain pending'
      : 'functional CSS browser check using a supplied public staging Worker preflight and the existing actual Host/Join keyboard path; the harness validates preflight structure/bindings only and does not independently verify the deployed service; RTT, full match, human/physical-input, two-machine/Internet, timing, pixel/PCM-equivalence and whole-session acceptance remain pending')
    : values['full-route']
      ? values['repeat-full-route']
        ? 'local functional browser route attempt including two same-room natural original four-stock Mario/Final Destination matches; broader original-accuracy, public Worker, human/physical-input, Internet, timing, pixel/PCM-equivalence and whole-session acceptance remain pending'
        : 'local functional browser route attempt including one natural original four-stock Mario/Final Destination match; broader original-accuracy, public Worker, human/physical-input, Internet, timing, pixel/PCM-equivalence and whole-session acceptance remain pending'
      : 'local functional browser check only; public Worker, full match, human/physical input, Internet, timing, pixel/PCM equivalence and whole-session gates remain pending',
  decision: 'FAIL',
  first_error: null,
  source: null,
  browser: null,
  room_worker: null,
  network_simulation: simulationConfiguration ? {configuration: simulationConfiguration,
    ...(postCssStallProbe ? {post_css_stall_probe: {outcome: 'NOT_STARTED',
      requested: true, policy: postCssStallProbe.resultPolicy}} : {}),
    ...(values['controlled-cut-after-css'] ? {controlled_cut_probe: {outcome: 'NOT_STARTED',
      requested: true}} : {})} : null,
  external_relay_preflight: relayMode.mode === 'external' ? {
    file: path.basename(path.resolve(values['relay-preflight'])),
    sha256: relayPreflightSha256,
    binding: relayMode,
    independent_live_provider_verification: false,
  } : null,
  invitation: null,
  profiles: {},
  css_keyboard: {},
  checksum_comparison: null,
  checksum_wait_failure: null,
  full_route: null,
  gameplay_action_probe: values['gameplay-action-probe'],
  gameplay_combat_probe: values['gameplay-combat-probe'],
  gameplay_combat_exchange: values['gameplay-combat-exchange'],
  gameplay_combat_ko: values['gameplay-combat-ko'],
  gameplay_combat_ko_charged: values['gameplay-combat-ko-charged'],
  gameplay_combat_ko_prepared: values['gameplay-combat-ko-prepared'],
  leave_and_cleanup: {},
  runtime_incident_recording: {requested: true, url: runtimeUrl.href, profiles: {},
    policy: 'Existing bounded recorder; staging occupancy and queue-completion callback latency. Forced timing pauses are H1 evidence; no automatic resume added.'},
  exclusions: ['no direct PAD or Gamepad injection', 'no harness-owned peer or checksum pump',
    'no runtime source-tick/checksum-bound changes',
    ...(relayMode.mode === 'external'
      ? ['external Worker is not owned, mutated, or disposed by this harness',
        'preflight structure/bindings only; no independent live deployment verification',
        'RTT is not measured by this harness']
      : ['one local Worker room only'])],
  started_at: new Date().toISOString(),
};
let worker = null;
let networkSimulation = null;
let browserNetworkPaths = null;
const instances = {alpha: null, beta: null};
let workerPassed = false;
let writeRepeatedMatchArtifact = null;
let repeatedPrefixDigest = null;

async function sourceIdentity() {
  const status = gitValue(['status', '--porcelain', '--untracked-files=all']);
  const allowedHarnessFiles = ['scripts/a3_local_room_keyboard_browser.mjs',
    'scripts/a3_runtime_incidents.mjs', 'tests/a3_runtime_incidents.test.mjs',
    'tests/test_a3_runtime_incidents.py',
    'scripts/net_session_instance.mjs', 'scripts/a3_data_package_diagnostics.mjs', 'tests/a3_data_package_diagnostics.test.mjs',
    'scripts/a3_expected_fault_auditor.py', 'scripts/a3_public_relay_preflight.mjs',
    'tests/test_a3_expected_fault_auditor.py', 'tests/test_a3_public_relay_preflight.py',
    'scripts/a3_local_room_full_route.mjs', 'scripts/a3_local_room_combat_probe.mjs',
    'scripts/a3_local_room_combat_ko.mjs',
    'tests/a3_local_room_full_route_combat.test.mjs',
    'tests/test_a3_local_room_full_route_combat.py', 'scripts/a3_checksum_wait_diagnostics.mjs',
    'scripts/a3_source_cursor_join.mjs', 'scripts/run_network_simulation.py',
    'tests/test_run_network_simulation.py', 'tests/test_a3_local_room_full_route_wait.py',
    'scripts/a3_network_simulation.mjs', 'scripts/a3_network_stall_probe.mjs',
    'tests/a3_network_stall_probe.test.mjs', 'tests/test_a3_network_stall_probe.py',
    'tests/a3_local_room_full_route_combat.test.mjs',
    'tests/net_test_tcp_proxy_stall_queue.test.mjs',
    'tests/net_test_tcp_proxy_controlled_faults.test.mjs',
    'scripts/net_test_tcp_proxy.mjs', 'tests/test_a3_network_simulation.py',
    'tests/test_net_test_tcp_proxy.py'];
  const unexpected = status.split('\n').filter(Boolean).filter(row =>
    !allowedHarnessFiles.includes(row.slice(3)));
  requireCondition(unexpected.length === 0,
    'Checkout contains changes outside this harness: ' + JSON.stringify(unexpected));
  const sourceFiles = allowedHarnessFiles.filter(filename => filename.startsWith('scripts/') ||
    filename === 'tests/a3_local_room_full_route_combat.test.mjs' ||
    filename === 'tests/test_run_network_simulation.py');
  const filesSha256 = Object.fromEntries(await Promise.all(sourceFiles.map(async filename =>
    [filename, sha256(await fs.readFile(filename))])));
  return {head: gitValue(['rev-parse', 'HEAD']), tree: gitValue(['rev-parse', 'HEAD^{tree}']),
    status_porcelain: status, allowed_harness_only_change: status.length > 0,
    files_sha256: filesSha256};
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
      menu_message: typeof module?._melee_web_native_menu_message === 'function' &&
        typeof module.UTF8ToString === 'function'
        ? module.UTF8ToString(module._melee_web_native_menu_message()) : null,
      native_status: nativeStatus,
      owner_health: ownerHealth,
    };
  });
}

async function readChecksumWaitFinalState(instance) {
  return instance.page.evaluate(() => {
    const module = globalThis.Module;
    const status = document.querySelector('#status');
    let matchSummary = null;
    let matchObservationError = null;
    try {
      if (typeof module?._melee_web_native_menu_match_observe !== 'function' ||
          typeof module.UTF8ToString !== 'function') {
        matchObservationError = 'native match observer unavailable';
      } else {
        matchSummary = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
      }
    } catch (error) {
      matchObservationError = String(error?.message || error);
    }
    return {
      runtime_error: status?.dataset.runtimeError || null,
      menu_phase: typeof module?._melee_web_native_menu_phase === 'function'
        ? module._melee_web_native_menu_phase() : null,
      menu_running: typeof module?._melee_web_native_menu_running === 'function'
        ? module._melee_web_native_menu_running() : null,
      menu_message: typeof module?._melee_web_native_menu_message === 'function' &&
        typeof module.UTF8ToString === 'function'
        ? module.UTF8ToString(module._melee_web_native_menu_message()) : null,
      native_status: globalThis.__net?.status?.() ?? null,
      owner_health: globalThis.meleeNetRuntimeLockstepHealth?.() ?? null,
      match_summary: matchSummary,
      match_observation_error: matchObservationError,
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

function validateRepeatCheckpointInput(input, port, label) {
  const neutralPair = value => Array.isArray(value) && value.length === 2 &&
    value.every(component => component === 0);
  const pad = input?.pads?.[port];
  requireCondition(input?.ready === 1 && input.focused === 1 && input.visible === 1 &&
    input.active === 1 && input.error === '' && input.keyboard_layout === 0 &&
    Number.isSafeInteger(input.samples) && input.samples > 0 &&
    Number.isSafeInteger(input.keyboard_requested_mask) && input.keyboard_requested_mask >= 0 &&
    Number.isSafeInteger(input.keyboard_active_mask) && input.keyboard_active_mask >= 0 &&
    Number.isSafeInteger(input.physical_mask) && input.physical_mask >= 0 &&
    (input.keyboard_requested_mask & (1 << port)) !== 0 &&
    (input.keyboard_active_mask & (1 << port)) !== 0 &&
    (input.physical_mask & (1 << port)) === 0 &&
    Array.isArray(input.pads) && input.pads.length === 4 && pad?.source === 'keyboard' &&
    pad.err === 0 && pad.buttons === 0 && neutralPair(pad.stick) && neutralPair(pad.cstick) &&
    neutralPair(pad.triggers) && neutralPair(pad.analog) && neutralPair(pad.clamped?.stick) &&
    neutralPair(pad.clamped?.cstick) && neutralPair(pad.clamped?.triggers),
  label + ': source input snapshot lacks the active neutral keyboard port or sample counter');
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

// Explicit input-suffix reads avoid cloning retained checksum/PAD history. Keep
// readPeerSnapshot for full route/fault/final checkpoints that need that history.
async function readPeerInputSuffix(instance, inputFrom) {
  return instance.page.evaluate(inputFrom => {
    const health = globalThis.meleeNetRuntimeLockstepHealth?.({inputFrom});
    const nativeStatus = globalThis.__net?.status?.() ?? null;
    const module = globalThis.Module;
    return {
      snapshot: null,
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

async function waitForBothChecksumCompare(target, phase = 1, boundedDeadline = operationDeadline,
  sourceCursorPolicy = null) {
  if (sourceCursorPolicy !== null)
    requireCondition(Number.isSafeInteger(sourceCursorPolicy.sourceCursorLimit) &&
      sourceCursorPolicy.evidenceSink && typeof sourceCursorPolicy.evidenceSink === 'object',
    'Source-cursor checksum policy is malformed');
  const recent = [];
  let lastRows = null;
  let lastObservedAt = null;
  const retainRows = rows => {
    lastRows = rows;
    lastObservedAt = new Date().toISOString();
    recent.push({observed_at: lastObservedAt, peers: rows.map((row, index) =>
      checksumWaitDiagnostic(row, index === 0 ? 'alpha' : 'beta'))});
    if (recent.length > 32) recent.shift();
  };
  const failWithObservation = async (errorMessage, reason, finalLabel) => {
    const diagnosticBudget = boundedDeadline === operationDeadline ? 1500 :
      Math.max(0, Math.min(1500, boundedDeadline - Date.now()));
    const fresh = diagnosticBudget === 0 ? ['alpha', 'beta'].map(role => ({
      label: finalLabel, observed_at: new Date().toISOString(), role,
      observation_error: 'bounded route deadline expired before fresh diagnostic read'})) :
      await Promise.all(['alpha', 'beta'].map(async role => {
        let timer;
        try {
          const row = await Promise.race([
            readChecksumWaitFinalState(instances[role]),
            new Promise((_, reject) => {
              timer = setTimeout(() => reject(Error(`diagnostic observation exceeded ${diagnosticBudget} ms`)),
                diagnosticBudget);
            }),
          ]);
          return {label: finalLabel, observed_at: new Date().toISOString(),
            state: checksumWaitDiagnostic(row, role)};
        } catch (error) {
          return {label: finalLabel, observed_at: new Date().toISOString(), role,
            observation_error: errorText(error)};
        } finally {
          clearTimeout(timer);
        }
      }));
    const observation = {reason, target, phase, predicate_last_observed_at: lastObservedAt,
      predicate_last_rows: lastRows?.map((row, index) =>
        checksumWaitDiagnostic(row, index === 0 ? 'alpha' : 'beta')) ?? null,
      recent, fresh_final_observation: fresh};
    report.checksum_wait_failure = observation;
    return Error(errorMessage);
  };
  while (Date.now() < boundedDeadline) {
    const states = await Promise.all([readRoomState(instances.alpha), readRoomState(instances.beta)]);
    retainRows(states);
    if (sourceCursorPolicy !== null) {
      try {
        retainCombatChecksumCursorPoll({evidenceSink: sourceCursorPolicy.evidenceSink,
          states, sourceCursorLimit: sourceCursorPolicy.sourceCursorLimit});
      } catch (error) {
        const message = errorText(error);
        throw await failWithObservation(message, message, 'after-source-cursor-budget');
      }
    }
    for (const [index, role] of ['alpha', 'beta'].entries()) {
      const failure = protocolFailure(states[index], role);
      if (failure) throw await failWithObservation(failure, failure, 'after-protocol-failure');
    }
    const reached = states.every(row => checksumCompareReached(row, target, phase));
    if (reached) return states;
    await sleep(50);
  }
  throw await failWithObservation('Both peers did not advance the common checksum boundary after keyboard input',
    'operation deadline reached before the predicate matched',
    'fresh-observation-after-operation-deadline');
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

async function captureScreenshot(instance, filename, operationRemaining = remaining) {
  const pathOnDisk = path.join(output, filename);
  await instance.page.locator('#canvas').scrollIntoViewIfNeeded({timeout: operationRemaining()});
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
  report.decision = report.first_error ? 'FAIL' :
    (report.network_simulation?.post_css_stall_probe?.outcome === 'EXERCISED_AND_RESUMED'
      ? 'PASS_POST_CSS_STALL_PROBE' : 'PASS');
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}

function summarizeCutObservation(row) {
  const peer = row?.snapshot?.peer;
  return {
    native_status: row?.native_status ?? null,
    menu_phase: row?.menu_phase ?? null,
    menu_running: row?.menu_running ?? null,
    protocol_terminal: peer?.protocol?.terminal ?? null,
    endpoint_errors: peer?.endpointErrors ?? null,
    endpoint_closed: peer?.endpointClosed ?? null,
    peer_failure: peer?.failure ?? null,
    checksum_mismatch_count: row?.health?.peer?.protocol?.checksum_mismatch_count ?? null,
    checksum_mismatches: peer?.protocol?.checksum_mismatches ?? null,
    native_local_divergences: peer?.protocol?.native_local_divergences ?? null,
  };
}

async function runControlledCutProbe() {
  requireCondition(values['controlled-cut-after-css'] === true && networkSimulation?.proxy &&
    typeof networkSimulation.proxy.cutConnection === 'function',
    'Controlled cut requires the reviewed owned loopback TCP proxy API');
  const proxyBefore = networkSimulation.proxy.snapshot();
  const active = proxyBefore?.connections?.active;
  const faultsBefore = proxyBefore?.controlledFaults;
  requireCondition(Array.isArray(active) && active.length === 2 &&
    active.every(row => Number.isSafeInteger(row.connectionId) && row.connectionId > 0) &&
    new Set(active.map(row => row.connectionId)).size === 2,
    'Controlled cut requires exactly two active opaque room-path proxy connections');
  requireCondition(Array.isArray(faultsBefore) && faultsBefore.length === 0 &&
    proxyBefore.lifecycle?.cutConnections === 0 && proxyBefore.lifecycle?.failedConnections === 0 &&
    proxyBefore.diagnostics?.integrityFailure === false,
    'Controlled cut requires a fault-free proxy before activation');
  for (const role of ['alpha', 'beta']) {
    report.profiles[role].data_package_diagnostics_before_cut =
      await instances[role].readDataPackageDiagnostics();
  }
  const prePeers = {
    alpha: await readPeerSnapshot(instances.alpha),
    beta: await readPeerSnapshot(instances.beta),
  };
  for (const role of ['alpha', 'beta']) {
    const row = prePeers[role];
    requireCondition(row.menu_phase === 1 && row.menu_running === 1 &&
      row.native_status?.active === 1 && row.native_status?.terminal?.kind === 0 &&
      row.snapshot?.peer?.protocol?.ready === true &&
      row.health?.peer?.protocol?.checksum_mismatch_count === 0 &&
      row.snapshot?.peer?.endpointClosed === false,
      role + ' was not at a healthy terminal-free live CSS boundary before cut');
  }
  const selectedConnectionId = active[0].connectionId;
  const probe = {
    schema: 'melee-web-task13-controlled-cut-observation-v1',
    selection: {active_connection_ids: active.map(row => row.connectionId),
      selected_connection_id: selectedConnectionId,
      scope: 'one active opaque room-path TCP connection; no alpha/beta attribution by accept order'},
    pre_fault: {
      room_id: report.invitation?.room_id,
      invite_sha256: report.invitation?.invite_sha256,
      browser_path: report.network_simulation?.browser_path,
      checksum_comparison: report.checksum_comparison,
      peers: prePeers,
      proxy: proxyBefore,
      source_cursors: Object.fromEntries(['alpha', 'beta'].map(role =>
        [role, prePeers[role].native_status?.cursor ?? null])),
    },
    activation: null,
    proxy_immediately_after_activation: null,
    post_fault: {observations: [], first_terminal_by_role: {}, final_peers: null,
      terminal_observed: false, elapsed_ms: null, actions_after_cut: [], resume_calls_after_cut: 0,
      action_scope: 'read-only terminal polling; final UI Stop/Leave and unload occur later as cleanup'},
  };
  report.network_simulation.controlled_cut_probe = probe;
  const started = Date.now();
  probe.activation = networkSimulation.proxy.cutConnection(selectedConnectionId);
  probe.proxy_immediately_after_activation = networkSimulation.proxy.snapshot();
  requireCondition(probe.activation?.kind === 'cut' &&
    probe.activation.connectionId === selectedConnectionId &&
    probe.activation.scope === 'one opaque owned TCP connection; no inferred player role' &&
    probe.proxy_immediately_after_activation?.controlledFaults?.length === 1,
    'Proxy did not issue the exact one-use opaque cut receipt');
  let lastSignature = null;
  let finalRows = null;
  let unsupportedTerminal = null;
  while (Date.now() < operationDeadline) {
    const rows = {
      alpha: await readPeerSnapshot(instances.alpha),
      beta: await readPeerSnapshot(instances.beta),
    };
    finalRows = rows;
    const compact = Object.fromEntries(['alpha', 'beta'].map(role =>
      [role, summarizeCutObservation(rows[role])]));
    const signature = JSON.stringify(compact);
    if (signature !== lastSignature) {
      if (probe.post_fault.observations.length < 128) {
        probe.post_fault.observations.push({elapsed_ms: Date.now() - started, peers: compact});
      } else {
        probe.post_fault.observations[127] = {elapsed_ms: Date.now() - started, peers: compact};
      }
      lastSignature = signature;
    }
    let bothSupportedTerminals = true;
    for (const role of ['alpha', 'beta']) {
      const row = rows[role];
      const terminal = row.native_status?.blocker === 'terminal'
        ? row.native_status.terminal : null;
      if (terminal && !probe.post_fault.first_terminal_by_role[role]) {
        probe.post_fault.first_terminal_by_role[role] = {
          elapsed_ms: Date.now() - started,
          observation: summarizeCutObservation(row),
        };
      }
      if (terminal && ![2, 3].includes(terminal.kind))
        unsupportedTerminal = {role, terminal};
      if (!terminal || ![2, 3].includes(terminal.kind) ||
          row.snapshot?.peer?.endpointClosed !== true) bothSupportedTerminals = false;
    }
    if (unsupportedTerminal || bothSupportedTerminals) break;
    await sleep(Math.min(50, remaining()));
  }
  probe.post_fault.elapsed_ms = Date.now() - started;
  probe.post_fault.final_peers = finalRows;
  probe.post_fault.terminal_observed = Boolean(finalRows && ['alpha', 'beta'].every(role =>
    finalRows[role].native_status?.blocker === 'terminal' &&
    [2, 3].includes(finalRows[role].native_status?.terminal?.kind) &&
    finalRows[role].snapshot?.peer?.endpointClosed === true));
  if (unsupportedTerminal)
    throw Error('Controlled cut produced an unsupported native terminal: ' + JSON.stringify(unsupportedTerminal));
  requireCondition(probe.post_fault.terminal_observed,
    'Controlled cut did not reach source-supported native terminal kinds before the existing operation deadline');
  throw Error('Expected controlled transport cut observed; route remains FAIL');
}

try {
  report.source = await sourceIdentity();
  report.browser = {node: process.version, url: runtimeUrl.href,
    speaker_output: 'muted through the shared browserLaunchOptions policy; normal audio processing retained',
    profile_model: 'one independent persistent Chrome context per player'};
  if (relayMode.mode === 'local') {
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
  }
  if (simulationConfiguration) {
    networkSimulation = await startNetworkSimulation({worker, configuration: simulationConfiguration});
    report.network_simulation.endpoint = networkSimulation.relayUrl;
    report.network_simulation.upstream = worker.base;
    report.network_simulation.calibration = await calibrateNetworkSimulation({
      workerBase: worker.base, proxyBase: networkSimulation.httpBase, remaining});
    report.network_simulation.proxy_after_calibration = networkSimulation.proxy.snapshot();
    verifyNetworkProxy(report.network_simulation.proxy_after_calibration);
  }
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
    dataPackageDiagnostics: values['controlled-cut-after-css'] === true,
    syntheticGamepad: null,
  });
  instances.alpha = await openRole('alpha');
  instances.beta = await openRole('beta');
  for (const role of ['alpha', 'beta']) {
    const readiness = await readIncidentRecorderReadiness(instances[role].page);
    report.runtime_incident_recording.profiles[role] = {readiness};
    requireIncidentRecorderReadiness(readiness);
  }
  if (networkSimulation)
    browserNetworkPaths = observeBrowserNetworkPaths(instances, networkSimulation.relayUrl);
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

  const relayUrl = networkSimulation?.relayUrl ?? (relayMode.mode === 'external' ? relayMode.endpoint.url :
    'ws://127.0.0.1:' + worker.port);
  await instances.alpha.page.locator('#lockstep-relay-url').fill(relayUrl, {timeout: remaining()});
  await instances.alpha.page.locator('#lockstep-host').click({timeout: remaining()});
  await instances.alpha.page.waitForFunction(() =>
    document.querySelector('#lockstep-host-invite')?.value?.length > 0,
  null, {timeout: remaining()});
  const inviteText = await instances.alpha.page.locator('#lockstep-host-invite').inputValue();
  const invite = parseRoomInvite(inviteText);
  requireCondition(invite.url === relayUrl && invite.sourceTicks === LOCKSTEP_MAX_SOURCE_TICKS &&
    invite.inputDelay === LOCKSTEP_DELAY,
    'Actual Host control produced an invite outside the selected Worker or current source-tick contract');
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
  if (browserNetworkPaths)
    report.network_simulation.browser_path = browserNetworkPaths.verify(invite.roomId);

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
    const readback = await readPeerInputSuffix(instance, inputCount);
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

  if (values['full-route'] && !values['repeat-full-route']) {
    report.full_route = await runA3LocalFullRoute({
      alpha: instances.alpha, beta: instances.beta, report, inputDelay: invite.inputDelay,
      gameplayActionProbe: values['gameplay-action-probe'],
      gameplayCombatProbe: values['gameplay-combat-probe'],
      gameplayCombatExchange: values['gameplay-combat-exchange'],
      gameplayCombatKo: values['gameplay-combat-ko'],
      gameplayCombatKoCharged: values['gameplay-combat-ko-charged'],
      gameplayCombatKoPrepared: values['gameplay-combat-ko-prepared'],
      remaining, readRoomState, readPeerSnapshot, readPeerInputSuffix, readInputDiagnostics,
      waitForBothChecksumCompare, captureScreenshot,
      assertHealthy: (state, role) => {
        const failure = protocolFailure(state, role);
        requireCondition(!failure, failure || role + ' runtime owner is unhealthy');
      },
    });
  } else if (values['repeat-full-route']) {
    const initialPages = {alpha: instances.alpha, beta: instances.beta};
    const repeat = {
      result: 'running', scope: 'two original full routes in one continuously owned room',
      repeat_mode: 'same-room-two-match', gameplay_action_probe: false,
      gameplay_combat_probe: false,
      required_checksum_boundary: 0, active_match_number: null, repeated_matches: [],
      checkpoint_observations: [],
      admission: null, final_prefix: null,
      exclusions: ['same persistent browser pages, Worker, Host/Join room and owner session',
        'no reconfigure, reseed, reload, close, reconnect or synthetic input between matches',
        'no truncation or deadline extension; failed or budget-ineligible second match is not a pass'],
    };
    report.full_route = repeat;
    const roomIdentity = {
      room_id: report.invitation.room_id,
      relay_url: report.invitation.url,
      invite_sha256: report.invitation.invite_sha256,
      network_profile_sha256: report.network_simulation?.configuration?.profile_sha256 ?? null,
      owner_process_id: report.room_worker?.owner_process?.pid ?? null,
      owner_process_group_id: report.room_worker?.owner_process?.processGroupId ?? null,
      worker_hashes: report.room_worker?.hashes ?? null,
      persistent_pages: ['alpha', 'beta'],
    };
    requireCondition(roomIdentity.owner_process_id !== null && roomIdentity.owner_process_group_id !== null &&
      typeof roomIdentity.worker_hashes?.worker === 'string',
    'Repeated route requires the same owned local Worker process for both matches');
    const assertHealthyState = (state, role) => {
      const failure = protocolFailure(state, role);
      requireCondition(!failure, failure || role + ' runtime owner is unhealthy');
      requireCondition(state.menu_phase === 1 && state.menu_running === 1 &&
        state.native_status?.active === 1,
      role + ' left the connected original CSS at a repeat-route boundary');
      const protocol = state.owner_health?.peer?.protocol;
      requireCondition(protocol?.role === role && protocol?.local_port === (role === 'alpha' ? 0 : 1),
        role + ' lockstep owner role/port changed at a repeat-route boundary');
    };
    const collectCheckpoint = async label => {
      requireCondition(instances.alpha === initialPages.alpha && instances.beta === initialPages.beta,
        label + ': persistent browser page instances changed');
      const states = await Promise.all([readRoomState(initialPages.alpha), readRoomState(initialPages.beta)]);
      const snapshots = await Promise.all([readPeerSnapshot(initialPages.alpha), readPeerSnapshot(initialPages.beta)]);
      const inputs = await Promise.all([readInputDiagnostics(initialPages.alpha),
        readInputDiagnostics(initialPages.beta)]);
      const peers = {};
      const checkpoint = {label, observed_at: new Date().toISOString(),
        result: 'observed_pending_validation', room_identity: roomIdentity,
        input_observations: {alpha: inputs[0], beta: inputs[1]}, peers};
      repeat.checkpoint_observations.push(checkpoint);
      for (const [index, role] of ['alpha', 'beta'].entries()) {
        const state = states[index];
        const snapshot = snapshots[index];
        assertHealthyState(state, role);
        const protocol = state.owner_health.peer.protocol;
        const snapshotProtocol = snapshot.health?.peer?.protocol;
        const retained = snapshot.snapshot?.checksums?.length;
        requireCondition(Number.isSafeInteger(state.native_status.cursor) && state.native_status.cursor >= 0 &&
          Number.isSafeInteger(retained) && retained === snapshotProtocol?.local_checksum_ticks &&
          Number.isSafeInteger(snapshotProtocol?.remote_checksum_ticks) &&
          Number.isSafeInteger(snapshotProtocol?.next_checksum_compare),
        label + ': ' + role + ' native cursor/checksum observation is incomplete');
        requireCondition(protocol.role === snapshotProtocol.role && protocol.local_port === snapshotProtocol.local_port,
          label + ': ' + role + ' owner role/port differs between status and checksum snapshot');
        const input = inputs[index];
        validateRepeatCheckpointInput(input, role === 'alpha' ? 0 : 1, label + ': ' + role);
        peers[role] = {
          source_cursor: state.native_status.cursor,
          input_diagnostics: input,
          checksum_counts: {retained, local: snapshotProtocol.local_checksum_ticks,
            remote: snapshotProtocol.remote_checksum_ticks, compared: snapshotProtocol.next_checksum_compare},
          peer_identity: {role: protocol.role, local_port: protocol.local_port,
            owner_seed: state.native_status.seed ?? null},
        };
      }
      checkpoint.result = 'validated_observations';
      return checkpoint;
    };
    const samePageObjects = () => instances.alpha === initialPages.alpha && instances.beta === initialPages.beta;
    let secondRouteRemaining = null;
    let secondRouteDeadline = null;
    const runRetainedMatch = async (matchNumber, entryCheckpoint = null) => {
      requireCondition(samePageObjects(), 'browser page ownership changed before match ' + matchNumber);
      const matchDirectory = path.join(output, `match-${matchNumber}`);
      await fs.mkdir(matchDirectory, {recursive: false});
      const entry = {match_number: matchNumber, result: 'running', route_elapsed_ms: null,
        route: null, entry: entryCheckpoint, first_return: null, second_return: null, report_artifact: null};
      repeat.active_match_number = matchNumber;
      repeat.repeated_matches.push(entry);
      const localReport = {};
      const routeStarted = performance.now();
      const routeRemaining = matchNumber === 1 ? remaining : secondRouteRemaining;
      const routeChecksumWait = matchNumber === 1 ? waitForBothChecksumCompare :
        (target, phase = 1) => waitForBothChecksumCompare(target, phase, secondRouteDeadline);
      try {
        await runA3LocalFullRoute({
          alpha: initialPages.alpha, beta: initialPages.beta, report: localReport,
          inputDelay: invite.inputDelay, gameplayActionProbe: false,
          remaining: routeRemaining,
          readRoomState, readPeerSnapshot, readPeerInputSuffix, readInputDiagnostics,
          waitForBothChecksumCompare: routeChecksumWait,
          captureScreenshot: (instance, filename) =>
            captureScreenshot(instance, `match-${matchNumber}/${filename}`, routeRemaining),
          assertHealthy: (state, role) => {
            const failure = protocolFailure(state, role);
            requireCondition(!failure, failure || role + ' runtime owner is unhealthy');
          },
        });
        entry.route = localReport.full_route;
        entry.route_elapsed_ms = Math.ceil(performance.now() - routeStarted);
        entry.result = 'awaiting_checksum_validation';
        return entry;
      } catch (error) {
        entry.route = localReport.full_route ?? null;
        entry.route_elapsed_ms = Math.ceil(performance.now() - routeStarted);
        entry.result = 'failed';
        throw error;
      }
    };
    writeRepeatedMatchArtifact = async entry => {
      const payload = {schema: 'melee-web-a3-local-room-full-route-match-v1',
        match_number: entry.match_number, result: entry.result,
        route_elapsed_ms: entry.route_elapsed_ms, room_identity: roomIdentity,
        entry: entry.entry, route: entry.route,
        first_return: entry.first_return, second_return: entry.second_return};
      const bytes = Buffer.from(JSON.stringify(payload, null, 2) + '\n');
      const filename = `match-${entry.match_number}/report.json`;
      await fs.mkdir(path.join(output, `match-${entry.match_number}`), {recursive: true});
      await fs.writeFile(path.join(output, filename), bytes);
      entry.report_artifact = {file: filename, bytes: bytes.length, sha256: sha256(bytes)};
    };
    const prefixDigest = (comparison, count) => {
      requireCondition(Number.isSafeInteger(count) && count > 0 && comparison.records.length >= count,
        'Repeated-route checksum prefix is shorter than its CSS boundary');
      const digest = createHash('sha256');
      for (let tick = 0; tick < count; ++tick) {
        const row = comparison.records[tick];
        requireCondition(row.source_tick === tick && row.bytes === 64 &&
          row.alpha_raw_hex === row.beta_raw_hex,
        'Repeated-route raw checksum prefix diverged at source tick ' + tick);
        digest.update(Buffer.from(row.alpha_raw_hex, 'hex'));
      }
      return digest.digest('hex');
    };
    repeatedPrefixDigest = prefixDigest;
    let firstEntry = null;
    try {
      firstEntry = await runRetainedMatch(1);
      requireCondition(firstEntry.route?.result === 'awaiting_final_checksum_validation' &&
        Number.isSafeInteger(firstEntry.route.required_checksum_boundary),
      'First full route did not retain its returned-CSS checksum boundary');
      const firstBoundary = firstEntry.route.required_checksum_boundary;
      await waitForBothChecksumCompare(firstBoundary, 1);
      const firstSnapshots = await waitForChecksumConsumersSettled(firstBoundary);
      const firstComparison = compareSnapshotRecords(firstSnapshots[0], firstSnapshots[1], firstBoundary);
      requireCondition(firstComparison.compared_source_ticks >= firstBoundary,
        'First returned-CSS boundary lacks an exact common raw checksum prefix');
      firstEntry.first_return = await collectCheckpoint('match-1-returned-css');
      for (const [index, role] of ['alpha', 'beta'].entries()) {
        const routeCursor = firstEntry.route.returned_css.source_cursor_by_peer[role];
        requireCondition(firstEntry.first_return.peers[role].source_cursor >= routeCursor,
          'Match one ' + role + ' source cursor moved behind returned CSS');
      }
      firstEntry.first_return.result = 'pass';
      firstEntry.first_return.common_raw_prefix = {
        result: 'pass', source_tick_range: [0, firstBoundary - 1], record_count: firstBoundary,
        bytes_per_record: 64, alpha_equals_beta: true,
        sha256_concatenated_records: prefixDigest(firstComparison, firstBoundary),
        compared_source_ticks_at_capture: firstComparison.compared_source_ticks,
      };
      firstEntry.route.result = 'pass';
      firstEntry.result = 'pass';
      repeat.required_checksum_boundary = firstBoundary;
      await writeRepeatedMatchArtifact(firstEntry);

      const entryCheckpoint = await collectCheckpoint('match-2-entry');
      entryCheckpoint.result = 'pass';
      for (const role of ['alpha', 'beta']) {
        requireCondition(entryCheckpoint.peers[role].source_cursor >= firstEntry.first_return.peers[role].source_cursor,
          'Match two ' + role + ' source cursor regressed before entry');
        requireCondition(entryCheckpoint.peers[role].input_diagnostics.samples >=
          firstEntry.first_return.peers[role].input_diagnostics.samples,
        'Match two ' + role + ' input sample counter regressed before entry');
        for (const counter of ['retained', 'local', 'remote', 'compared'])
          requireCondition(entryCheckpoint.peers[role].checksum_counts[counter] >=
            firstEntry.first_return.peers[role].checksum_counts[counter],
          'Match two ' + role + ' checksum count regressed before entry');
      }
      const firstDuration = firstEntry.route_elapsed_ms;
      const remainingBeforeSecond = remaining();
      const requiredRemaining = firstDuration + 30000;
      const secondSubdeadline = Math.max(0, Math.min(firstDuration + 10000, remainingBeforeSecond - 20000));
      repeat.admission = {first_route_duration_ms: firstDuration,
        remaining_before_second_ms: remainingBeforeSecond, required_remaining_ms: requiredRemaining,
        second_route_subdeadline_ms: secondSubdeadline,
        second_route_elapsed_ms: null, final_reserve_ms_at_second_return: null};
      if (remainingBeforeSecond < requiredRemaining) {
        const notAttempted = {match_number: 2, result: 'not_attempted_budget', route_elapsed_ms: 0,
          route: null, entry: {...entryCheckpoint, result: 'not_attempted'}, first_return: null,
          second_return: null, report_artifact: null};
        repeat.repeated_matches.push(notAttempted);
        repeat.active_match_number = 2;
        await writeRepeatedMatchArtifact(notAttempted);
        throw Error(`Second full route not attempted: ${remainingBeforeSecond} ms remains; ${requiredRemaining} ms required`);
      }
      secondRouteDeadline = Date.now() + repeat.admission.second_route_subdeadline_ms;
      secondRouteRemaining = () => {
        const operationLeft = remaining();
        const routeLeft = secondRouteDeadline - Date.now();
        const finalReserveLeft = operationLeft - 20000;
        if (routeLeft <= 0 || finalReserveLeft <= 0)
          throw Error('Second full route exhausted its admitted route or final-comparison budget');
        return Math.max(1, Math.min(routeLeft, finalReserveLeft));
      };
      const secondEntry = await runRetainedMatch(2, entryCheckpoint);
      repeat.admission.second_route_elapsed_ms = secondEntry.route_elapsed_ms;
      requireCondition(secondEntry.route_elapsed_ms <= repeat.admission.second_route_subdeadline_ms,
        'Second full route exceeded its measured-duration subdeadline');
      requireCondition(secondEntry.route?.result === 'awaiting_final_checksum_validation' &&
        Number.isSafeInteger(secondEntry.route.required_checksum_boundary),
      'Second full route did not retain its returned-CSS checksum boundary');
      secondEntry.second_return = await collectCheckpoint('match-2-returned-css');
      for (const role of ['alpha', 'beta']) {
        const before = secondEntry.entry.peers[role];
        const after = secondEntry.second_return.peers[role];
        const routeCursor = secondEntry.route.returned_css.source_cursor_by_peer[role];
        requireCondition(after.source_cursor >= routeCursor && after.source_cursor > before.source_cursor,
          'Match two ' + role + ' source cursor did not advance through returned CSS');
        requireCondition(after.input_diagnostics.samples >= before.input_diagnostics.samples,
          'Match two ' + role + ' input sample counter regressed by returned CSS');
        for (const counter of ['retained', 'local', 'remote', 'compared'])
          requireCondition(after.checksum_counts[counter] >= before.checksum_counts[counter],
            'Match two ' + role + ' checksum count regressed by returned CSS');
      }
      secondEntry.second_return.result = 'pass';
      repeat.admission.final_reserve_ms_at_second_return = remaining();
      requireCondition(repeat.admission.final_reserve_ms_at_second_return >= 20000,
        'Final comparison reserve was unavailable when match two returned to CSS');
      repeat.required_checksum_boundary = Math.max(firstBoundary,
        secondEntry.route.required_checksum_boundary);
    } catch (error) {
      if (repeat.admission && repeat.admission.second_route_elapsed_ms === null) {
        const second = repeat.repeated_matches.find(entry => entry.match_number === 2);
        if (second && Number.isSafeInteger(second.route_elapsed_ms))
          repeat.admission.second_route_elapsed_ms = second.route_elapsed_ms;
      }
      for (const entry of repeat.repeated_matches) {
        if (entry.result === 'running' || entry.result === 'awaiting_checksum_validation')
          entry.result = 'failed';
        try { await writeRepeatedMatchArtifact(entry); } catch (artifactError) {
          firstError('Repeated-route match artifact write failed: ' + errorText(artifactError));
        }
      }
      throw error;
    }
  }
  if (postCssStallProbe) {
    await runPostCssStallProbe({
      proxy: networkSimulation.proxy,
      readRoomState: role => readRoomState(instances[role]),
      assertHealthy: (state, role) => {
        const failure = protocolFailure(state, role);
        requireCondition(!failure, failure || role + ' runtime owner is unhealthy');
      },
      remaining,
      browserPathVerification: report.network_simulation.browser_path,
      evidence: report.network_simulation.post_css_stall_probe,
    });
  }
  const resumedStallCursors = postCssStallProbe
    ? ['alpha', 'beta'].map(role =>
      report.network_simulation.post_css_stall_probe.resumed.peers[role].source_cursor)
    : [];
  requireCondition(resumedStallCursors.every(Number.isSafeInteger),
    'Post-stall resumed source cursor is unavailable');
  const requiredChecksums = Math.max(minimumChecksums,
    report.full_route?.required_checksum_boundary || 0, ...resumedStallCursors);
  requireCondition(requiredChecksums <= invite.sourceTicks,
    'Required full-route checksum boundary exceeds the Host/Join source-tick limit');
  const progress = await waitForBothCssAndProgress(requiredChecksums);
  const [alphaSnapshot, betaSnapshot] = await waitForChecksumConsumersSettled(requiredChecksums);
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
  report.checksum_comparison = compareSnapshotRecords(alphaSnapshot, betaSnapshot, requiredChecksums);
  if (report.full_route?.repeat_mode === 'same-room-two-match') {
    try {
      const repeat = report.full_route;
      const first = repeat.repeated_matches[0];
      const second = repeat.repeated_matches[1];
      requireCondition(first?.result === 'pass' && second?.route?.result === 'awaiting_final_checksum_validation',
        'Repeated route did not retain a passed first route and complete second route');
      const firstBoundary = first.route.required_checksum_boundary;
      const secondBoundary = second.route.required_checksum_boundary;
      const firstPrefixHash = repeatedPrefixDigest(report.checksum_comparison, firstBoundary);
      const secondPrefixHash = repeatedPrefixDigest(report.checksum_comparison, secondBoundary);
      requireCondition(firstPrefixHash === first.first_return.common_raw_prefix.sha256_concatenated_records,
        'Final common raw checksum prefix changed the pre-second-match records');
      repeat.final_prefix = {result: 'pass', first_boundary: firstBoundary,
        first_prefix_sha256: firstPrefixHash, second_boundary: secondBoundary,
        second_prefix_sha256: secondPrefixHash,
        compared_source_ticks: report.checksum_comparison.compared_source_ticks,
        raw_records_present_in_final_checksum_comparison: true};
      requireCondition(repeat.final_prefix.compared_source_ticks >= secondBoundary &&
        repeat.admission.final_reserve_ms_at_second_return >= 20000,
      'Final exact checksum prefix or reserved comparison window was unavailable after match two');
      second.route.result = 'pass';
      second.result = 'pass';
      await writeRepeatedMatchArtifact(second);
      repeat.active_match_number = null;
    } catch (error) {
      for (const entry of report.full_route.repeated_matches) {
        if (entry.result === 'running' || entry.result === 'awaiting_checksum_validation')
          entry.result = 'failed';
        try { await writeRepeatedMatchArtifact(entry); } catch (artifactError) {
          firstError('Repeated-route match artifact write failed: ' + errorText(artifactError));
        }
      }
      throw error;
    }
  }
  if (report.full_route) report.full_route.result = 'pass';
  report.checksum_comparison.requested_minimum = requiredChecksums;
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
  if (networkSimulation) {
    report.network_simulation.browser_path = browserNetworkPaths.verify(invite.roomId);
    report.network_simulation.proxy_before_leave = networkSimulation.proxy.snapshot();
    report.network_simulation.proxy_health = verifyNetworkProxy(report.network_simulation.proxy_before_leave);
    report.network_simulation.native_network_wait = Object.fromEntries(
      [['alpha', alphaSnapshot], ['beta', betaSnapshot]].map(([role, row]) => [role, {
        source_cursor: row.native_status.cursor, counters: row.native_status.network_wait,
        durations_ms: null, duration_scope: 'native counters only; wall durations are not measured',
        protocol: row.health?.peer?.protocol,
      }]));
  }
  if (values['controlled-cut-after-css']) await runControlledCutProbe();
  workerPassed = true;
} catch (error) {
  if (report.full_route && ['running', 'awaiting_final_checksum_validation'].includes(report.full_route.result))
    report.full_route.result = 'failed';
  const combatProbe = report.full_route?.combat_probe;
  if (combatProbe?.result === 'running') {
    combatProbe.result = 'failed';
    combatProbe.failure = errorText(error);
  }
  firstError(errorText(error));
} finally {
  // Read the actual stop before Leave/unload changes it. No live polling, pause,
  // resume or new recorder is introduced by this evidence boundary.
  for (const role of ['alpha', 'beta']) {
    const instance = instances[role];
    if (!instance) continue;
    const evidence = report.runtime_incident_recording.profiles[role] ||= {};
    try {
      evidence.before_leave = await cleanupBounded(instance.page.evaluate(() =>
        window.meleeNetTimingPauseEligibilitySnapshot?.() ?? null), role + ' incident stop state');
    } catch (error) {
      evidence.before_leave_error = errorText(error);
      firstError(role + ' incident stop state unavailable: ' + errorText(error));
    }
  }
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
    const evidence = report.runtime_incident_recording.profiles[role] ||= {};
    try {
      // The existing export refuses serialization during active gameplay.
      evidence.export = await cleanupBounded(instance.page.evaluate(async () =>
        await window.meleeNetRuntimeDiagnosticExport?.() ?? null), role + ' runtime incident export');
      evidence.summary = summarizeRuntimeIncidents(evidence.export, evidence.before_leave);
      if (evidence.summary.missing_timing_staging_count > 0)
        firstError(role + ' H1 incident is missing the requested staging observation');
      if (evidence.summary.h1_timing_pause_observed) {
        report.runtime_incident_recording.h1_timing_pause_observed = true;
        if (!values['post-css-stall-probe'])
          firstError(role + ' forced timing pause retained as H1 evidence; route incomplete');
      }
    } catch (error) {
      evidence.export_error = errorText(error);
      firstError(role + ' runtime incident evidence unavailable: ' + errorText(error));
    }
    try {
      const fatal = firstFatalBrowserError(instance.errors);
      if (fatal) firstError(role + ' browser error: ' + JSON.stringify(fatal));
      report.leave_and_cleanup[role + '_browser_errors'] = instance.errors;
      report.leave_and_cleanup[role + '_context_close'] = await cleanupBounded(
        instance.close(), role + ' Chrome context close');
      if (instance.closed !== true) firstError(role + ' persistent Chrome context did not close');
    } catch (error) {
      report.leave_and_cleanup[role + '_context_close_error'] = errorText(error);
      report.leave_and_cleanup[role + '_context_closed'] = instance.closed === true;
      firstError(role + ' Chrome context cleanup failed: ' + errorText(error));
    } finally {
      if (values['controlled-cut-after-css'])
        report.leave_and_cleanup[role + '_data_package_diagnostics_after_context_close'] =
          instance.dataPackageDiagnostics ?? null;
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
  if (networkSimulation) {
    try {
      report.network_simulation.browser_websockets = browserNetworkPaths?.snapshot() ?? null;
      browserNetworkPaths?.close();
      report.network_simulation.proxy_after_browser_close = networkSimulation.proxy.snapshot();
      report.leave_and_cleanup.network_proxy = await cleanupBounded(
        closeNetworkProxy(networkSimulation.proxy), 'Network proxy cleanup');
      report.network_simulation.proxy_final = networkSimulation.proxy.snapshot();
      report.network_simulation.proxy_cleanup = verifyNetworkProxy(
        report.network_simulation.proxy_final, {final: true});
    } catch (error) {
      report.leave_and_cleanup.network_proxy_error = errorText(error);
      firstError('Network proxy cleanup failed: ' + errorText(error));
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
  } else if (relayMode.mode === 'external') {
    report.leave_and_cleanup.worker = {attempted: false,
      reason: 'external Worker is not owned by the browser harness'};
  }
  if (report.first_error && report.runtime_incident_recording.h1_timing_pause_observed)
    report.failure_classification = {domain: 'H1', outcome: 'timing_pause_observed',
      route_accepted: false, combat_failure_inferred: false,
      first_error_cause: 'not_inferred_from_co_occurring_timing_pause',
      original_first_error_retained: true};
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
    full_route: report.full_route?.result ?? null,
    worker_port: report.room_worker?.port,
    output: path.join(output, 'report.json')}) + '\n');
}
