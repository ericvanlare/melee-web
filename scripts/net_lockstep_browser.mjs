#!/usr/bin/env node
/*
 * Track A2 functional two-peer lockstep. Each peer owns one local PAD port;
 * the relay forwards opaque packets and each peer validates identities,
 * acknowledgements, remote PAD contributions and delayed checksums before it
 * submits complete indexed frames to its own native queue. --relay-url selects
 * the room WebSocket relay explicitly; omission preserves the TCP baseline.
 */
import fs from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {createRoomId} from './net_lockstep_websocket_relay.mjs';
import {parseArgs} from 'node:util';
import {loadBrowserTools} from './browser_tools.mjs';
import {classifyRoute, collapseConsecutiveScenes, expectedFullSceneOrder, validateFullRoute} from './net_determinism_contract.mjs';
import {NET_FRAME_BYTES, NET_RECORD_BYTES, firstFatalBrowserError, openNetInstance, browserPeerFacade} from './net_session_instance.mjs';
import {createTransportCallbackQueue, describeLockstepTransport, describeLockstepTransportAttempt,
  openLockstepPeerPair, recordAvailableTransportMetrics} from './net_lockstep_transport.mjs';
import {LOCKSTEP_DELAY, LockstepPeer, parseNetChecksum, TERMINAL} from './net_lockstep_protocol.mjs';
import {readyRenderEvent, renderEventSignatures, verifyFirstChecksumMismatch,
  verifyTerminalHold} from './net_lockstep_observers.mjs';
import {verifyNetSourceAccounting} from './net_source_accounting.mjs';

const HEADER_BYTES = 16;
const POSITIVE_ROUTE_BOUNDARIES = Object.freeze([
  Object.freeze({name: 'css-start', phase: 1, label: 'original CSS at the first ready source draw'}),
  Object.freeze({name: 'sss', phase: 3, label: 'original SSS'}),
  Object.freeze({name: 'match', phase: 7, label: 'original match'}),
  Object.freeze({name: 'results', phase: 8, label: 'original Results'}),
  Object.freeze({name: 'css-return', phase: 1, label: 'original CSS after Results'}),
]);
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, script: {type: 'string'}, out: {type: 'string'},
  playwright: {type: 'string'}, seed: {type: 'string'}, scenario: {type: 'string', default: 'probe'},
  'relay-url': {type: 'string'},
  'peer-owner': {type: 'string', default: 'node'},
  'source-ticks': {type: 'string', default: '8'}, 'timeout-ms': {type: 'string', default: '3600000'},
  'stall-ms': {type: 'string', default: '120000'}, 'poll-ms': {type: 'string', default: '50'},
  'delay-ms': {type: 'string', default: '250'}, 'flip': {type: 'string'},
  'disconnect-at': {type: 'string'},
}});
const integer = (name, min, max, fallback = undefined) => {
  const value = values[name] === undefined ? fallback : Number(values[name]);
  if (!Number.isInteger(value) || value < min || value > max)
    throw Error(`--${name} must be an integer between ${min} and ${max}`);
  return value;
};
const uint32 = text => {
  if (typeof text !== 'string' || !/^(?:0[xX][0-9a-fA-F]+|[0-9]+)$/.test(text))
    throw Error('--seed must be an unsigned 32-bit integer');
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)
    throw Error('--seed must be an unsigned 32-bit integer');
  return value;
};
if (!values.url || !values.disc || !values.script || !values.out || !values.seed)
  throw Error('Required: --url runtime.html --disc DISC --script route1.mwni --seed U32 --out NEW_DIR');
if (!['probe', 'positive', 'flip', 'disconnect'].includes(values.scenario))
  throw Error('--scenario must be probe, positive, flip, or disconnect');
const scenario = values.scenario;
if (!['node', 'browser'].includes(values['peer-owner'])) throw Error('--peer-owner must be node or browser');
const browserOwned = values['peer-owner'] === 'browser';
if (browserOwned && !values['relay-url']) throw Error('Browser-owned peers require --relay-url');
const url = new URL(values.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html'))
  throw Error('A real HTTP development runtime.html URL is required');
const seed = uint32(values.seed);
const timeoutMs = integer('timeout-ms', 1000, 21600000);
const stallMs = integer('stall-ms', 1000, 3600000);
const pollMs = integer('poll-ms', 10, 2000);
const delayMs = integer('delay-ms', 50, 10000);
const probeSourceTicks = integer('source-ticks', 4, 216000);
const flip = values.flip ? (() => {
  const fields = values.flip.split(':').map(Number);
  if (fields.length !== 4 || !fields.every(Number.isInteger) || fields[0] < 0 ||
      fields[1] < 0 || fields[1] > 10 || fields[2] < 0 || fields[2] > 7 ||
      (fields[3] !== 0 && fields[3] !== 1))
    throw Error('--flip format is INPUT_TICK:PAD_BYTE:BIT:LOCAL_PORT (port 0 or 1)');
  return {tick: fields[0], byte: fields[1], bit: fields[2], port: fields[3]};
})() : null;
if (values.scenario === 'flip' && !flip) throw Error('The flip scenario requires --flip INPUT_TICK:PAD_BYTE:BIT:LOCAL_PORT');
const disconnectAt = values['disconnect-at'] === undefined ? null : integer('disconnect-at', 1, 216000);
if (values.scenario === 'disconnect' && disconnectAt === null)
  throw Error('The disconnect scenario requires --disconnect-at SOURCE_TICK');
if (values.scenario === 'disconnect' && disconnectAt < 3)
  throw Error('The bounded disconnect must occur after the two neutral-prefix source ticks');

const scriptBytes = await fs.readFile(values.script);
if (scriptBytes.length < HEADER_BYTES || scriptBytes.subarray(0, 4).toString() !== 'MWNI' ||
    scriptBytes.readUInt32BE(4) !== 1)
  throw Error('Script is not an MWNI v1 input recipe');
const inputCount = scriptBytes.readUInt32BE(8);
if (scriptBytes.length !== HEADER_BYTES + inputCount * NET_FRAME_BYTES)
  throw Error('Script length disagrees with its declared input count');
const scriptFrames = scriptBytes.subarray(HEADER_BYTES);
const probe = values.scenario === 'probe';
const usedInputs = probe ? probeSourceTicks - LOCKSTEP_DELAY :
  scenario === 'flip' && flip ? Math.min(inputCount, flip.tick + 8) :
  scenario === 'disconnect' ? Math.min(inputCount, Math.max(1, disconnectAt + 1 - LOCKSTEP_DELAY)) : inputCount;
if (usedInputs > inputCount) throw Error('Requested local input workload exceeds the script');
if (flip && flip.tick >= usedInputs) throw Error('Input flip tick is outside the selected workload');
const sourceTicks = usedInputs + LOCKSTEP_DELAY;
if (sourceTicks > 216000) throw Error('Source tick workload exceeds the native bound');
if (disconnectAt !== null && disconnectAt >= sourceTicks)
  throw Error('Disconnect source tick must precede the bounded run end');
const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const scriptHash = sha256(scriptBytes);
const deadline = Date.now() + timeoutMs;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const childDirectory = role => path.join(output, role);

const pairResults = {
  schema: 'melee-web-local-lockstep-a2-run-v1', scenario, seed,
  scope: probe ? 'CSS network-wait and duplicate-contribution probe' :
    scenario === 'positive' ? 'full original-route functional lockstep' : `bounded ${scenario} control`,
  exclusions: ['live timing', 'performance', 'pixels', 'PCM equivalence', 'retail equivalence', 'two-machine Internet acceptance'],
  peer_owner: values['peer-owner'],
  input_delay: LOCKSTEP_DELAY,
  neutral_prefix: {source_ticks: LOCKSTEP_DELAY, player_ports: 'neutral PADStatus', unowned_ports: 'no-controller'},
  script: {name: path.basename(values.script), sha256: scriptHash, frame_count: inputCount,
    input_ticks_used: usedInputs, source_ticks: sourceTicks},
  transport_attempt: describeLockstepTransportAttempt(values['relay-url']),
  peers: [], outcome: 'fail', first_error: null, relay_closed: false, started_at: new Date().toISOString(),
};
let instances = null, relay = null, peers = null, disconnectHandled = false, intentionalRelayClose = false;
let disconnectTask = null;
const peerSummaries = {alpha: null, beta: null};
const checksumFiles = {};
const instanceRows = {};
const routeBoundaryState = {alpha: {sawMatch: false}, beta: {sawMatch: false}};
const waitObservations = [];
const scheduled = new Map();
const closeNotes = [];
const transportErrors = [];
const callbackErrors = [];
const routeCaptureTasks = new Map();
const routeCaptureErrors = [];
let stopRouteCaptureWatchers = false;
const transportCallbackQueue = createTransportCallbackQueue(({role, kind, error}) => {
  const row = {role, kind, message: String(error?.stack || error?.message || error)};
  callbackErrors.push(row);
  transportErrors.push({role, message: `${kind} callback failed: ${row.message}`});
});

async function checkedHealth(role) {
  const instance = instances[role];
  const status = await instance.status();
  const native = await instance.native();
  if (native.error) throw Error(`${role} runtime error: ${native.error}`);
  const fatal = firstFatalBrowserError(instance.errors);
  if (fatal) throw Error(`${role} browser error: ${JSON.stringify(fatal)}`);
  if (!status.active) throw Error(`${role} native network session became inactive`);
  if (status.terminal.kind && !['flip', 'disconnect'].includes(scenario))
    throw Error(`${role} native terminal: ${JSON.stringify(status.terminal)}`);
  // A2 network_wait never borrows A1's controlled owner-timing pause resume.
  return {status, native};
}

async function waitForStart() {
  for (;;) {
    if (Date.now() > deadline) throw Error('Start identity barrier exceeded the run deadline');
    const rows = await Promise.all(['alpha', 'beta'].map(checkedHealth));
    if (rows.some(({status}) => status.start.capture_failed || status.terminal.kind))
      throw Error(`Prepared start identity capture failed: ${JSON.stringify(rows.map(row => row.status))}`);
    if (rows.every(({status}) => status.start.recorded === 1 && status.start.required === 1))
      return rows.map(row => row.status.start);
    await sleep(pollMs);
  }
}

function localSample(role, tick) {
  const port = role === 'alpha' ? 0 : 1;
  return Buffer.from(scriptFrames.subarray(tick * NET_FRAME_BYTES + port * 11,
    tick * NET_FRAME_BYTES + (port + 1) * 11));
}

function nativeSample(role, tick) {
  const sample = localSample(role, tick);
  const port = role === 'alpha' ? 0 : 1;
  if (flip && flip.port === port && flip.tick === tick) sample[flip.byte] ^= 1 << flip.bit;
  return sample;
}

function routeBoundaryPath(role, boundary) {
  const filename = boundary.name === 'css-return' ? 'final.png' : `route-${boundary.name}.png`;
  return path.join(childDirectory(role), filename);
}

function recordMissedBoundary(role, boundary, reason, {phase = null, cursor = null} = {}) {
  const row = instanceRows[role];
  if (!row.route_boundary_misses.some(item => item.name === boundary.name)) {
    row.route_boundary_misses.push({name: boundary.name, label: boundary.label,
      expected_phase: boundary.phase, reason, observed_phase: phase, observed_cursor: cursor,
      observed_at: new Date().toISOString()});
  }
}

async function captureRouteBoundary(role, boundary, priorSignatures) {
  const row = instanceRows[role];
  if (row.route_boundary_captures.some(item => item.name === boundary.name) ||
      row.route_boundary_misses.some(item => item.name === boundary.name)) return;
  const driverDiagnostics = await instances[role].driver.diagnostics();
  const readiness = readyRenderEvent(driverDiagnostics, boundary.phase, priorSignatures);
  const nativeBefore = await instances[role].native();
  const statusBefore = await instances[role].status();
  if (!readiness || nativeBefore.phase !== boundary.phase || statusBefore.blocker === 'terminal') return false;
  const cursor = statusBefore.cursor;
  const screenshotPath = routeBoundaryPath(role, boundary);
  await instances[role].screenshot(screenshotPath);
  const screenshot = await fs.readFile(screenshotPath);
  if (screenshot.length < 8 || !screenshot.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])))
    throw Error(`${role} ${boundary.name} route-boundary screenshot is not a PNG`);
  const nativeAfter = await instances[role].native();
  const statusAfter = await instances[role].status();
  const graphics = await instances[role].graphics();
  row.route_boundary_captures.push({name: boundary.name, label: boundary.label,
    phase: boundary.phase, source_cursor_sampled_before_screenshot: cursor,
    last_consumed_tick: cursor > 0 ? cursor - 1 : null,
    source_cursor_after_screenshot: statusAfter.cursor,
    source_cursor_stable_during_screenshot: statusAfter.cursor === cursor,
    screenshot: path.relative(output, screenshotPath),
    screenshot_bytes: screenshot.length, screenshot_sha256: sha256(screenshot),
    screenshot_phase_stable: nativeAfter.phase === boundary.phase,
    phase_after_screenshot: nativeAfter.phase,
    render_readiness: readiness,
    browser_driver: driverDiagnostics,
    gpu: graphics,
    gpu_observed: graphics.cross_origin_isolated === true && graphics.webgpu_api === true &&
      graphics.webgpu_adapter === true,
    captured_at: new Date().toISOString()});
  return true;
}

async function watchRouteBoundary(role, boundary, {includeCurrentRender = false} = {}) {
  let priorSignatures = new Set();
  if (!includeCurrentRender) {
    const initial = await instances[role].driver.diagnostics();
    priorSignatures = renderEventSignatures(initial.log);
  }
  for (;;) {
    const [status, native, diagnostics] = await Promise.all([
      instances[role].status(), instances[role].native(), instances[role].driver.diagnostics(),
    ]);
    if (native.phase !== boundary.phase) {
      recordMissedBoundary(role, boundary, 'route left the observed phase before a ready draw was captured',
        {phase: native.phase, cursor: status.cursor});
      return;
    }
    const readiness = readyRenderEvent(diagnostics, boundary.phase, priorSignatures);
    if (readiness) {
      if (await captureRouteBoundary(role, boundary, priorSignatures)) return;
    }
    if (stopRouteCaptureWatchers || status.cursor >= sourceTicks) {
      recordMissedBoundary(role, boundary, 'route ended before a ready current-phase draw was captured',
        {phase: native.phase, cursor: status.cursor});
      return;
    }
    await sleep(Math.min(pollMs, 50));
  }
}

function scheduleRouteBoundary(role, boundary, options = {}) {
  const row = instanceRows[role], key = `${role}:${boundary.name}`;
  if (row.route_boundary_captures.some(item => item.name === boundary.name) ||
      row.route_boundary_misses.some(item => item.name === boundary.name) || routeCaptureTasks.has(key)) return;
  const task = watchRouteBoundary(role, boundary, options).catch(error => {
    routeCaptureErrors.push({role, boundary: boundary.name, message: String(error?.stack || error?.message || error)});
  });
  routeCaptureTasks.set(key, task);
}

async function settleRouteBoundaryWatchers() {
  await Promise.all([...routeCaptureTasks.values()]);
  if (routeCaptureErrors.length)
    throw Error(`Route-boundary readiness observer failed: ${JSON.stringify(routeCaptureErrors[0])}`);
}

function captureObservedRouteBoundary(role, phase) {
  const state = routeBoundaryState[role];
  if (phase === 7 || phase === 8) state.sawMatch = true;
  const boundary = phase === 3 ? POSITIVE_ROUTE_BOUNDARIES[1] :
    phase === 7 ? POSITIVE_ROUTE_BOUNDARIES[2] :
    phase === 8 ? POSITIVE_ROUTE_BOUNDARIES[3] :
    phase === 1 && state.sawMatch ? POSITIVE_ROUTE_BOUNDARIES[4] : null;
  if (boundary) scheduleRouteBoundary(role, boundary);
}

async function finishRouteBoundaryEvidence(role, phase, cursor) {
  const row = instanceRows[role];
  for (const boundary of POSITIVE_ROUTE_BOUNDARIES) {
    if (row.route_boundary_captures.some(item => item.name === boundary.name) ||
        row.route_boundary_misses.some(item => item.name === boundary.name)) continue;
    recordMissedBoundary(role, boundary, `route ended before a ready draw in phase ${boundary.phase} was captured`,
      {phase, cursor});
  }
  const captured = new Set(row.route_boundary_captures.map(item => item.name));
  const missed = new Set(row.route_boundary_misses.map(item => item.name));
  const capturesComplete = captured.size === POSITIVE_ROUTE_BOUNDARIES.length && missed.size === 0;
  const renderReady = capturesComplete && row.route_boundary_captures.every(item =>
    item.render_readiness?.draw_calls > 0 && item.render_readiness?.source_draws > 0 &&
    item.render_readiness?.draw_suppressed === 0);
  const gpuComplete = capturesComplete && row.route_boundary_captures.every(item => item.gpu_observed);
  const phaseStable = capturesComplete && row.route_boundary_captures.every(item => item.screenshot_phase_stable);
  row.route_boundary_report = {
    expected: POSITIVE_ROUTE_BOUNDARIES.map(({name, phase: expectedPhase, label}) =>
      ({name, phase: expectedPhase, label})),
    captured: row.route_boundary_captures.map(({name, phase: capturedPhase,
      source_cursor_sampled_before_screenshot, last_consumed_tick,
      source_cursor_after_screenshot, source_cursor_stable_during_screenshot, screenshot, render_readiness,
      screenshot_bytes, screenshot_sha256, screenshot_phase_stable, gpu_observed}) =>
      ({name, phase: capturedPhase, source_cursor_sampled_before_screenshot, last_consumed_tick,
        source_cursor_after_screenshot, source_cursor_stable_during_screenshot, screenshot,
        render_readiness, screenshot_bytes, screenshot_sha256,
        screenshot_phase_stable, gpu_observed})),
    missed: row.route_boundary_misses,
    captures_complete: capturesComplete,
    ready_draw_complete: renderReady,
    gpu_diagnostics_complete: gpuComplete,
    phase_stable_through_screenshots: phaseStable,
    complete: capturesComplete && renderReady && gpuComplete && phaseStable,
    pixel_equivalence_claim: false,
  };
}

async function refreshBrowserPeers() {
  if (browserOwned && peers) {
    await Promise.all(['alpha', 'beta'].map(role => peers[role].refresh()));
    if (peers.alpha.terminal?.kind === 'disconnect' || peers.beta.terminal?.kind === 'disconnect') disconnectHandled = true;
  }
}

async function drainChecksums(role, peer) {
  const instance = instances[role], result = instanceRows[role];
  for (;;) {
    const drained = browserOwned ? await peer.drain() : await instance.drain(512);
    for (let index = 0; index < drained.count; ++index) {
      const record = drained.bytes.subarray(index * NET_RECORD_BYTES, (index + 1) * NET_RECORD_BYTES);
      const tick = record.readUInt32LE(0);
      if (tick !== result.records) throw Error(`${role} checksum cursor jumped: expected ${result.records}, observed ${tick}`);
      await checksumFiles[role].write(record);
      result.records++;
      const parsed = parseNetChecksum(record);
      result.scene_runs.push({tick: parsed.tick, scene: parsed.scene});
      if (!browserOwned && !peer.terminal) await peer.addChecksum(record);
    }
    if (drained.count < 512) break;
  }
}

function createAgreement(identity, start) {
  return {
    protocol: 'melee-web-local-lockstep-a2-v1',
    seed, source_ticks: sourceTicks, input_ticks: usedInputs,
    input_delay: LOCKSTEP_DELAY,
    neutral_prefix: {ticks: LOCKSTEP_DELAY, ports_0_1: 'zero-PADStatus', ports_2_3: 'PAD_ERR_NO_CONTROLLER'},
    pad_encoding: 'MWNI-v1-port-records-11-byte',
    port_ownership: {0: 'alpha', 1: 'beta', 2: 'no-controller', 3: 'no-controller'},
    input_recipe: {sha256: scriptHash, frame_count: inputCount, input_ticks_used: usedInputs},
    runtime_wasm_sha256: identity.wasm,
    disc: identity.disc,
    native_start: start,
  };
}

async function publishAllInputs(alpha, beta) {
  for (let first = 0; first < usedInputs; first += 32) {
    const end = Math.min(first + 32, usedInputs);
    const left = [], right = [];
    for (let tick = first; tick < end; ++tick) {
      left.push([tick, localSample('alpha', tick)]);
      right.push([tick, localSample('beta', tick)]);
    }
    const leftOverrides = left.filter(([tick]) => flip?.port === 0 && flip.tick === tick)
      .map(([tick]) => [tick, nativeSample('alpha', tick)]);
    const rightOverrides = right.filter(([tick]) => flip?.port === 1 && flip.tick === tick)
      .map(([tick]) => [tick, nativeSample('beta', tick)]);
    await alpha.addLocalInputs(left, {nativeOverrides: leftOverrides});
    await beta.addLocalInputs(right, {nativeOverrides: rightOverrides});
    const ackEnd = end - 1;
    const started = Date.now();
    for (;;) {
      await refreshBrowserPeers();
      if (alpha.remoteAckInput >= ackEnd && beta.remoteAckInput >= ackEnd) break;
      if (Date.now() > deadline || Date.now() - started > stallMs)
        throw Error(`Loopback input acknowledgement stalled before tick ${ackEnd}`);
      await sleep(2);
    }
    if (browserOwned) { await drainChecksums('alpha', alpha); await drainChecksums('beta', beta); }
  }
}

async function publishDisconnectPrefix(alpha, beta) {
  const count = Math.min(usedInputs, Math.max(0, disconnectAt - LOCKSTEP_DELAY));
  for (let first = 0; first < count; first += 32) {
    const end = Math.min(first + 32, count);
    const left = [], right = [];
    for (let tick = first; tick < end; ++tick) {
      left.push([tick, localSample('alpha', tick)]);
      right.push([tick, localSample('beta', tick)]);
    }
    await alpha.addLocalInputs(left); await beta.addLocalInputs(right);
    const ackEnd = end - 1, started = Date.now();
    for (;;) {
      await refreshBrowserPeers();
      if (alpha.remoteAckInput >= ackEnd && beta.remoteAckInput >= ackEnd) break;
      if (Date.now() > deadline || Date.now() - started > stallMs)
        throw Error(`Loopback input acknowledgement stalled before disconnect tick ${ackEnd}`);
      await sleep(2);
    }
    if (browserOwned) { await drainChecksums('alpha', alpha); await drainChecksums('beta', beta); }
  }
  pairResults.disconnect_missing_input_tick = count;
}

async function publishProbeInputs(alpha, beta) {
  // Publish tick2 before tick1 so the real relay exercises bounded reordering;
  // role beta retains its own tick1 locally and withholds only its packet.
  await alpha.addLocalInput(0, localSample('alpha', 0));
  await beta.addLocalInput(0, localSample('beta', 0));
  await alpha.addLocalInput(1, localSample('alpha', 1));
  await alpha.addLocalInput(2, localSample('alpha', 2));
  await beta.addLocalInput(2, localSample('beta', 2));
  await beta.addLocalInput(1, localSample('beta', 1), {deferSend: true});
  await alpha.addLocalInputs([[3, localSample('alpha', 3)], [4, localSample('alpha', 4)], [5, localSample('alpha', 5)]]);
  // Input 1 was intentionally not sent by beta; its eventual packet is the
  // delayed/duplicated CSS contribution that releases alpha's exact wait tick.
}

async function pollRun() {
  let lastProgress = Date.now();
  const lastCursors = {alpha: -1, beta: -1};
  let disconnectInjected = false;
  polling: while (Date.now() <= deadline) {
    if (transportErrors.length)
      throw Error(`Loopback receive callback failed: ${JSON.stringify(transportErrors[0])}`);
    await refreshBrowserPeers();
    if (peers.alpha.terminal || peers.beta.terminal) break;
    const rows = {};
    for (const role of ['alpha', 'beta']) {
      const {status, native} = await checkedHealth(role);
      rows[role] = status;
      if (scenario === 'positive') captureObservedRouteBoundary(role, native.phase);
      await drainChecksums(role, peers[role]);
      await refreshBrowserPeers();
      // Checksum delivery can end either peer while the browser drain awaits.
      if (peers.alpha.terminal || peers.beta.terminal) break polling;
      await peers[role].setNativeProgress(status.cursor);
      if (status.wait_episodes > instanceRows[role].last_wait_episodes) {
        instanceRows[role].last_wait_episodes = status.wait_episodes;
        waitObservations.push({role, scope: probe && status.wait_start_tick < LOCKSTEP_DELAY + 1
          ? 'startup-publication' : probe && role === 'alpha' && status.wait_start_tick === LOCKSTEP_DELAY + 1
          ? 'withheld-input1' : 'network-wait', wait_episodes: status.wait_episodes,
          wait_start_tick: status.wait_start_tick, wait_last_tick: status.wait_last_tick,
          cursor: status.cursor, pushed: status.pushed, blocker: status.blocker,
          wait_callbacks: status.wait_callbacks, wait_resume_count: status.wait_resume_count,
          observed_at_ms: Date.now()});
      }
      if (status.cursor !== lastCursors[role]) { lastCursors[role] = status.cursor; lastProgress = Date.now(); }
    }
    if (!probe && scenario === 'disconnect' && !disconnectInjected &&
        Math.min(rows.alpha.cursor, rows.beta.cursor) >= disconnectAt) {
      disconnectInjected = true;
      instanceRows.injected_disconnect = {role: 'beta', source_tick: disconnectAt, at_ms: Date.now()};
      if (browserOwned) await relay.beta.close(false);
      else relay.beta.close();
    }
    // The start handshake confirms source time before asynchronous input0
    // delivery. Preserve those startup wait counters; select the actual
    // withheld-input1 boundary from the current native state.
    const delayedSourceTick = LOCKSTEP_DELAY + 1;
    if (probe && !instanceRows.probe_released && rows.alpha.cursor === delayedSourceTick &&
        rows.alpha.blocker === 'network_wait') {
      const wait = {...rows.alpha};
      if (wait.cursor !== delayedSourceTick || wait.pushed !== delayedSourceTick ||
          wait.wait_start_tick !== delayedSourceTick || wait.wait_last_tick !== delayedSourceTick)
        throw Error(`CSS input wait was not held at one unconsumed source tick: ${JSON.stringify(wait)}`);
      if (!wait.network_wait.active || wait.wait_episodes !== wait.wait_resume_count + 1)
        throw Error(`CSS input wait counters do not describe one active episode: ${JSON.stringify(wait)}`);
      instanceRows.alpha.target_wait = wait;
      instanceRows.alpha.startup_waits = {episodes: wait.wait_episodes - 1,
        resumes: wait.wait_resume_count, observed: waitObservations.filter(row =>
          row.role === 'alpha' && row.wait_start_tick < delayedSourceTick)};
      const before = rows.alpha.cursor;
      await sleep(120);
      const held = await instances.alpha.status();
      if (held.cursor !== before || held.pushed !== before || held.blocker !== 'network_wait' ||
          !held.network_wait.active || held.wait_episodes !== wait.wait_episodes ||
          held.wait_resume_count !== wait.wait_resume_count ||
          held.wait_start_tick !== delayedSourceTick || held.wait_last_tick !== delayedSourceTick)
        throw Error(`Network wait consumed a source tick or changed blocker while remote input was held: ${JSON.stringify(held)}`);
      instanceRows.alpha.wait_hold = {before_cursor: before, after_cursor: held.cursor,
        wait_callbacks_before: wait.wait_callbacks, wait_callbacks_after: held.wait_callbacks,
        wait_resume_count_before: held.wait_resume_count, blocker: held.blocker, held_ms: 120};
      await peers.beta.addLocalInputs([[1, localSample('beta', 1)],
        [3, localSample('beta', 3)], [4, localSample('beta', 4)], [5, localSample('beta', 5)]], {repeat: true});
      instanceRows.probe_released = true;
    }
    await refreshBrowserPeers();
    if (scenario === 'disconnect' && disconnectInjected &&
        (peers.alpha.terminal?.kind === 'disconnect' || peers.beta.terminal?.kind === 'disconnect')) break;
    if ((scenario === 'flip') && (peers.alpha.terminal?.kind === 'desync' || peers.beta.terminal?.kind === 'desync')) break;
    if (!probe && scenario !== 'disconnect' && scenario !== 'flip' &&
        rows.alpha.cursor >= sourceTicks && rows.beta.cursor >= sourceTicks &&
        instanceRows.alpha.records >= sourceTicks && instanceRows.beta.records >= sourceTicks) {
      await Promise.all([peers.alpha.setNativeProgress(sourceTicks, {flushFinal: true}),
        peers.beta.setNativeProgress(sourceTicks, {flushFinal: true})]);
      await sleep(50);
      await drainChecksums('alpha', peers.alpha); await drainChecksums('beta', peers.beta);
      break;
    }
    if (scenario === 'probe' && instanceRows.probe_released &&
        rows.alpha.cursor >= sourceTicks && rows.beta.cursor >= sourceTicks &&
        instanceRows.alpha.records >= sourceTicks && instanceRows.beta.records >= sourceTicks) {
      await Promise.all([peers.alpha.setNativeProgress(sourceTicks, {flushFinal: true}),
        peers.beta.setNativeProgress(sourceTicks, {flushFinal: true})]);
      await sleep(50);
      await drainChecksums('alpha', peers.alpha); await drainChecksums('beta', peers.beta);
      break;
    }
    if (Date.now() - lastProgress > stallMs)
      throw Error(`No source progress for ${stallMs} ms: ${JSON.stringify(rows)}`);
    await sleep(pollMs);
  }
  if (Date.now() > deadline) throw Error('Lockstep run exceeded its wall-time bound');
}

async function waitForTerminalPair(kind) {
  const terminalDeadline = Math.min(deadline, Date.now() + stallMs);
  while (Date.now() <= terminalDeadline) {
    await refreshBrowserPeers();
    const [alpha, beta] = await Promise.all(['alpha', 'beta'].map(role => instances[role].status()));
    if (peers.alpha.terminal?.kind === (kind === TERMINAL.desync ? 'desync' : 'disconnect') &&
        peers.beta.terminal?.kind === (kind === TERMINAL.desync ? 'desync' : 'disconnect') &&
        alpha.terminal.kind === kind && beta.terminal.kind === kind)
      return {alpha, beta};
    await sleep(pollMs);
  }
  throw Error(`Both native peers did not reach terminal kind ${kind} within the bounded notification window`);
}

async function run() {
  const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(values.playwright);
  pairResults.browser = path.basename(browserPath);
  pairResults.playwright = playwrightPath;
  await Promise.all(['alpha', 'beta'].map(role => fs.mkdir(childDirectory(role))));
  if (!browserOwned) relay = await openLockstepPeerPair({relayUrl: values['relay-url'],
    onEndpointError(role, error) {
      const row = {role, message: String(error?.stack || error?.message || error)};
      transportErrors.push(row);
      if (!peers?.[role]) return Promise.resolve();
      return transportCallbackQueue.track(role, 'endpoint-error', () =>
        peers[role].fail('protocol', {reason: `transport receive failed: ${row.message}`}));
    },
    onDisconnect(role, reason) {
      if (intentionalRelayClose || !peers) return Promise.resolve();
      if (disconnectTask) return disconnectTask;
      disconnectHandled = true;
      disconnectTask = transportCallbackQueue.track(role, 'disconnect-terminal', async () => {
        const results = await Promise.allSettled(['alpha', 'beta'].map(name =>
          peers[name].disconnect(`${role}: ${reason}`)));
        const failures = results.flatMap((result, index) => result.status === 'rejected'
          ? [{role: ['alpha', 'beta'][index], reason: result.reason}] : []);
        if (failures.length) throw new AggregateError(failures.map(row => row.reason),
          `disconnect terminal handling failed for ${failures.map(row => row.role).join(', ')}`);
        relay?.alpha.destroy();
        relay?.beta.destroy();
      });
      return disconnectTask;
    },
  });
  if (!browserOwned) pairResults.transport = describeLockstepTransport(relay, {relayUrl: values['relay-url']});
  instances = {};
  instanceRows.alpha = {role: 'alpha', local_port: 0, remote_port: 1, records: 0, timing_resumes: [], scene_runs: [], last_wait_episodes: 0,
    route_boundary_captures: [], route_boundary_misses: []};
  instanceRows.beta = {role: 'beta', local_port: 1, remote_port: 0, records: 0, timing_resumes: [], scene_runs: [], last_wait_episodes: 0,
    route_boundary_captures: [], route_boundary_misses: []};
  const openTimeout = Math.min(180000, deadline - Date.now());
  if (openTimeout <= 0) throw Error('No run deadline remains for browser startup');
  const peerModuleHashes = browserOwned ? Object.fromEntries(await Promise.all([
    'net_lockstep_browser_peer.mjs', 'net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs',
  ].map(async name => [name, sha256(await fs.readFile(new URL(name, import.meta.url)))]))) : null;
  const opened = await Promise.allSettled(['alpha', 'beta'].map(role => openNetInstance({
    chromium, launchOptions, url: values.url, disc: values.disc,
    userDataDir: path.join(childDirectory(role), 'profile'), label: role,
    timeoutMs: openTimeout, deadline, peerModuleHashes,
  })));
  // Transfer every successful launch before reporting a sibling failure so
  // the shared finalizer still owns its source session and browser context.
  for (const [index, row] of opened.entries())
    if (row.status === 'fulfilled') instances[['alpha', 'beta'][index]] = row.value;
  const failed = opened.find(row => row.status === 'rejected');
  if (failed) throw failed.reason;
  pairResults.browser_version = instances.alpha.browserVersion;
  pairResults.user_agents = {alpha: instances.alpha.userAgent, beta: instances.beta.userAgent};
  checksumFiles.alpha = await fs.open(path.join(childDirectory('alpha'), 'checksums.bin'), 'wx');
  checksumFiles.beta = await fs.open(path.join(childDirectory('beta'), 'checksums.bin'), 'wx');
  await Promise.all(['alpha', 'beta'].map(role => instances[role].importDisc()));
  await Promise.all(['alpha', 'beta'].map(role => instances[role].beginLockstep(seed, sourceTicks)));
  const startRows = await waitForStart();
  for (const role of ['alpha', 'beta']) {
    instanceRows[role].source_accounting_start = await instances[role].installSourceAccounting();
  }
  // Freeze the initial browser load-response set before peerIdentity performs
  // its separate cache-bypassing fetch of the served Wasm artifact.
  const loadedWasm = await Promise.all(['alpha', 'beta'].map(role => instances[role].freezeLoadedWasmIdentity()));
  const identities = await Promise.all(['alpha', 'beta'].map(role => instances[role].peerIdentity()));
  for (const [index, role] of ['alpha', 'beta'].entries()) {
    if (loadedWasm[index].sha256 !== identities[index].wasm)
      throw Error(`${role} browser-loaded Wasm response differs from the start-handshake fresh-fetch identity`);
  }
  const agreements = {
    alpha: createAgreement(identities[0], startRows[0]),
    beta: createAgreement(identities[1], startRows[1]),
  };
  pairResults.identity = {
    wasm_sha256_equal: identities[0].wasm === identities[1].wasm,
    loaded_wasm_response_sha256_equal: loadedWasm[0].sha256 === loadedWasm[1].sha256,
    wasm_sha256_source: 'peerIdentity fresh fetch compared with frozen initial browser load-response body SHA-256',
    disc_identity_equal: JSON.stringify(identities[0].disc) === JSON.stringify(identities[1].disc),
    native_start_equal: JSON.stringify(startRows[0]) === JSON.stringify(startRows[1]),
    alpha: {wasm_sha256: identities[0].wasm, disc: identities[0].disc, native_start: startRows[0]},
    beta: {wasm_sha256: identities[1].wasm, disc: identities[1].disc, native_start: startRows[1]},
    browser_loaded_wasm_response: {
      source: 'response body observed before navigation and frozen before peerIdentity fresh fetch',
      alpha: loadedWasm[0], beta: loadedWasm[1],
      compared_to_handshake_fresh_fetch: true,
      fresh_fetch_byte_length: 'not exposed by peerIdentity',
    },
  };
  peers = {};
  if (browserOwned) {
    const roomId = createRoomId();
    await Promise.all(['alpha', 'beta'].map(async role => {
      const initial = await instances[role].createBrowserPeer({role, sourceTicks, inputTicks: usedInputs,
        relayUrl: values['relay-url'], roomId, agreement: agreements[role], timeoutMs: Math.min(stallMs, deadline - Date.now())});
      peers[role] = browserPeerFacade(instances[role], initial);
    }));
    pairResults.browser_peer_modules = {expected: peerModuleHashes, responses: {
      alpha: await instances.alpha.freezePeerModuleIdentity(), beta: await instances.beta.freezePeerModuleIdentity(),
    }};
    relay = {alpha: peers.alpha, beta: peers.beta, transport: peers.alpha.transport,
      async close() {
        await Promise.all(['alpha', 'beta'].map(role => peers[role].armClose()));
        const rows = await Promise.allSettled(['alpha', 'beta'].map(role => peers[role].close(true)));
        const errors = rows.filter(row => row.status === 'rejected').map(row => row.reason);
        if (errors.length) throw new AggregateError(errors, 'Browser peer pair close failed');
      }};
    pairResults.transport = describeLockstepTransport(relay, {relayUrl: values['relay-url']});
  }
  if (!browserOwned) for (const role of ['alpha', 'beta']) {
    peers[role] = new LockstepPeer({role, sourceTicks, inputTicks: usedInputs,
      pushFrame: (tick, frames) => instances[role].pushIndexed(tick, frames),
      onReady: async () => {
        if (!await instances[role].confirmStart()) throw Error(`${role} native rejected peer start identity confirmation`);
      },
      onTerminal: async terminal => {
        const kind = TERMINAL[terminal.kind] ?? TERMINAL.protocol;
        const tick = Number.isInteger(terminal.tick) ? terminal.tick : 0;
        const channel = Number.isInteger(terminal.channel) ? terminal.channel : 0;
        await instances[role].terminate(kind, tick, channel);
      },
    });
  }
  if (!browserOwned) for (const role of ['alpha', 'beta']) {
    const endpoint = relay[role];
    endpoint.onMessage(text => peers[role].receive(text));
    peers[role].attach(text => endpoint.send(text));
  }
  if (browserOwned) await Promise.all([peers.alpha.start(agreements.alpha), peers.beta.start(agreements.beta)]);
  else { await peers.alpha.start(agreements.alpha); await peers.beta.start(agreements.beta); }
  const startDeadline = Math.min(deadline, Date.now() + stallMs);
  while ((!peers.alpha.ready || !peers.beta.ready) && Date.now() < startDeadline &&
      !peers.alpha.terminal && !peers.beta.terminal) {
    await refreshBrowserPeers();
    await sleep(pollMs);
  }
  await refreshBrowserPeers();
  if (peers.alpha.terminal || peers.beta.terminal)
    throw Error(`Start identity handshake failed: ${JSON.stringify([peers.alpha.terminal, peers.beta.terminal])}`);
  if (!peers.alpha.ready || !peers.beta.ready) throw Error('A2 start identity handshake timed out');
  pairResults.identity.handshake_confirmed_before_tick0 = true;
  pairResults.identity.peer_agreement_sha256 = peers.alpha.agreementHash;
  if (scenario === 'positive') {
    const initial = await Promise.all(['alpha', 'beta'].map(async role => ({
      role, status: await instances[role].status(), native: await instances[role].native(),
    })));
    await Promise.all(initial.map(async ({role, status, native}) => {
      if (native.phase === POSITIVE_ROUTE_BOUNDARIES[0].phase) {
        scheduleRouteBoundary(role, POSITIVE_ROUTE_BOUNDARIES[0], {includeCurrentRender: true});
      } else {
        recordMissedBoundary(role, POSITIVE_ROUTE_BOUNDARIES[0],
          'original CSS start was not current before input publication',
          {phase: native.phase, cursor: status.cursor});
      }
    }));
  }
  if (scenario === 'probe') {
    await publishProbeInputs(peers.alpha, peers.beta);
  } else if (scenario === 'disconnect') {
    await publishDisconnectPrefix(peers.alpha, peers.beta);
  } else {
    await publishAllInputs(peers.alpha, peers.beta);
  }
  recordAvailableTransportMetrics(pairResults.transport, relay);
  await pollRun();
  if (scenario === 'flip' || scenario === 'disconnect') {
    const expectedKind = scenario === 'flip' ? TERMINAL.desync : TERMINAL.disconnect;
    const before = await waitForTerminalPair(expectedKind);
    const holdStarted = Date.now();
    await sleep(120);
    const after = {
      alpha: await instances.alpha.status(), beta: await instances.beta.status(),
    };
    const expectedTick = scenario === 'flip' ? flip.tick + LOCKSTEP_DELAY : undefined;
    const expectedChannel = scenario === 'flip' ? 1 : undefined;
    pairResults.terminal_hold = verifyTerminalHold(before, after, expectedKind,
      {expectedTick, expectedChannel});
    pairResults.terminal_hold.window_observed_ms = Date.now() - holdStarted;
    await drainChecksums('alpha', peers.alpha); await drainChecksums('beta', peers.beta);
  }
  if (scenario === 'positive') {
    for (const role of ['alpha', 'beta']) {
      const [status, native] = await Promise.all([instances[role].status(), instances[role].native()]);
      captureObservedRouteBoundary(role, native.phase);
      instanceRows[role].route_completion_sample = {phase: native.phase, cursor: status.cursor};
    }
  }
  stopRouteCaptureWatchers = true;
  await settleRouteBoundaryWatchers();
  for (const role of ['alpha', 'beta']) {
    const capture = await instances[role].readSourceAccounting({freeze: true});
    const bytes = Buffer.from(JSON.stringify(capture, null, 2) + '\n');
    await fs.writeFile(path.join(childDirectory(role), 'source-accounting.json'), bytes);
    instanceRows[role].source_accounting_artifact = {name: 'source-accounting.json',
      bytes: bytes.length, sha256: sha256(bytes)};
    instanceRows[role].source_accounting = verifyNetSourceAccounting(capture,
      scenario === 'positive' || scenario === 'probe' ? sourceTicks : capture.final.cursor);
  }
  pairResults.wait_observations = waitObservations;
  pairResults.transport_errors = transportErrors;
  await refreshBrowserPeers();
  pairResults.endpoint_errors = {alpha: relay.alpha.errors, beta: relay.beta.errors};
  pairResults.peers = ['alpha', 'beta'].map(role => {
    const status = instanceRows[role];
    return {
      ...status,
      protocol: peers[role].summary(),
      ...(browserOwned ? {native_checksum_ownership: peers[role].checksumOwnership} : {}),
    };
  });
  if (browserOwned && (probe || scenario === 'positive')) {
    for (const role of ['alpha', 'beta']) {
      const ownership = peers[role].checksumOwnership;
      if (ownership.active_native_records_submitted_before_export !== sourceTicks ||
          ownership.post_terminal_native_evidence_records !== 0)
        throw Error(`${role} active native checksum ownership differs: ${JSON.stringify(ownership)}`);
    }
  }
  if (probe) {
    const alphaWait = instanceRows.alpha.target_wait;
    if (!alphaWait || !instanceRows.alpha.wait_hold || !instanceRows.probe_released)
      throw Error('Reduced probe did not observe and release the missing remote CSS input tick');
    for (const role of ['alpha', 'beta']) {
      if (instanceRows[role].records !== sourceTicks || instanceRows[role].scene_runs.length !== sourceTicks)
        throw Error(`${role} did not consume exactly ${sourceTicks} checksummed source ticks`);
      if (instanceRows[role].scene_runs.some(row => row.scene !== 1))
        throw Error(`${role} left original CSS during the reduced CSS probe`);
    }
    if (instanceRows.alpha.wait_hold.before_cursor !== instanceRows.alpha.wait_hold.after_cursor)
      throw Error('Held remote input advanced the source cursor');
    const alphaFinal = await instances.alpha.status();
    if (alphaFinal.wait_episodes !== alphaWait.wait_episodes ||
        alphaFinal.wait_resume_count !== alphaWait.wait_resume_count + 1 || alphaFinal.cursor !== sourceTicks)
      throw Error(`Remote-input wait did not finish with exactly one additional resume and no extra wait episodes or source ticks: ${JSON.stringify(alphaFinal)}`);
    instanceRows.alpha.final_status = alphaFinal;
    const bytesA = await fs.readFile(path.join(childDirectory('alpha'), 'checksums.bin'));
    const bytesB = await fs.readFile(path.join(childDirectory('beta'), 'checksums.bin'));
    if (!bytesA.equals(bytesB)) throw Error('CSS probe per-consumed-tick checksum streams differ');
    pairResults.checksums = {records_each: sourceTicks, streams_identical: true, sha256: sha256(bytesA)};
    if (peers.alpha.inputDuplicates < 1 || peers.alpha.outOfOrderInputs < 1)
      throw Error('Reduced probe did not exercise duplicate and out-of-order remote input');
    for (const role of ['alpha', 'beta']) {
      const filename = path.join(childDirectory(role), 'accounted-css.png');
      await instances[role].screenshot(filename);
      const bytes = await fs.readFile(filename);
      const graphics = await instances[role].graphics();
      const [native, status] = await Promise.all([instances[role].native(), instances[role].status()]);
      if (!bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) ||
          graphics.cross_origin_isolated !== true || graphics.webgpu_adapter !== true ||
          native.phase !== 1 || status.cursor !== sourceTicks || status.blocker !== 'complete')
        throw Error(`${role} reduced accounting probe did not retain its final rendered CSS boundary`);
      instanceRows[role].accounted_css = {source_cursor: status.cursor, phase: native.phase,
        screenshot: 'accounted-css.png', bytes: bytes.length, sha256: sha256(bytes),
        gpu: graphics, source_steps_and_draws: instanceRows[role].source_accounting,
        scope: 'CSS at the completed prefix; source counters exclude preparation-only draws'};
    }
    pairResults.route = {scope: 'CSS-only prefix', status: 'not-full-route', scene: 'CSS'};
    pairResults.outcome = 'complete';
  } else if (scenario === 'positive') {
    for (const role of ['alpha', 'beta']) {
      if (instanceRows[role].records !== sourceTicks)
        throw Error(`${role} checksum count ${instanceRows[role].records} did not equal ${sourceTicks}`);
      instanceRows[role].final_status = await instances[role].status();
      instanceRows[role].final_native = await instances[role].native();
      const observed = await instances[role].observe();
      instanceRows[role].match_observation = observed.match;
      const scenes = collapseConsecutiveScenes(instanceRows[role].scene_runs.map(row => row.scene));
      instanceRows[role].route = validateFullRoute(scenes, observed.match);
      await finishRouteBoundaryEvidence(role, instanceRows[role].final_native.phase,
        instanceRows[role].final_status.cursor);
      instanceRows[role].graphics = instanceRows[role].route_boundary_captures
        .find(item => item.name === 'css-return')?.gpu ?? null;
    }
    pairResults.route = {scope: classifyRoute(sourceTicks, sourceTicks), expected_full_scene_order: expectedFullSceneOrder(),
      alpha: instanceRows.alpha.route, beta: instanceRows.beta.route,
      boundary_capture_contract: {expected: POSITIVE_ROUTE_BOUNDARIES.map(({name, phase, label}) => ({name, phase, label})),
        source: 'existing browser-driver diagnostics, net-session WebGPU diagnostics, and headless Chrome viewport screenshots',
        pixel_equivalence_claim: false,
        alpha: instanceRows.alpha.route_boundary_report, beta: instanceRows.beta.route_boundary_report}};
    const bytesA = await fs.readFile(path.join(childDirectory('alpha'), 'checksums.bin'));
    const bytesB = await fs.readFile(path.join(childDirectory('beta'), 'checksums.bin'));
    if (!bytesA.equals(bytesB)) throw Error('Two native per-consumed-tick checksum streams differ');
    pairResults.checksums = {records_each: sourceTicks, streams_identical: true, sha256: sha256(bytesA)};
    pairResults.outcome = 'complete';
  } else if (scenario === 'flip') {
    const mismatch = peers.alpha.checksumMismatches[0] || peers.beta.checksumMismatches[0];
    const expectedTick = flip.tick + LOCKSTEP_DELAY;
    if (!mismatch || mismatch.tick !== expectedTick || mismatch.channel !== 1 ||
        peers.alpha.terminal.kind !== 'desync' || peers.beta.terminal.kind !== 'desync')
      throw Error(`Changed input did not stop at its first delayed checksum tick ${expectedTick}: ${JSON.stringify(mismatch)}`);
    const rawPrefix = verifyFirstChecksumMismatch(
      await fs.readFile(path.join(childDirectory('alpha'), 'checksums.bin')),
      await fs.readFile(path.join(childDirectory('beta'), 'checksums.bin')),
      expectedTick, 1);
    pairResults.negative_control = {changed_local_port: flip.port, input_tick: flip.tick,
      source_tick: expectedTick, byte: flip.byte, bit: flip.bit, detected_at_first_mismatch: true,
      channel: mismatch.channel, native_checksum_prefix: rawPrefix};
    pairResults.outcome = 'expected-desync';
  } else {
    if (!disconnectHandled || peers.alpha.terminal?.kind !== 'disconnect' || peers.beta.terminal?.kind !== 'disconnect')
      throw Error('Transport disconnect did not produce an explicit bounded terminal');
    pairResults.negative_control = {disconnect_at_source_tick: disconnectAt,
      terminals: {alpha: peers.alpha.terminal, beta: peers.beta.terminal}, explicit: true};
    pairResults.outcome = 'expected-disconnect';
  }
}

try {
  await run();
} catch (error) {
  stopRouteCaptureWatchers = true;
  await Promise.allSettled([...routeCaptureTasks.values()]);
  pairResults.first_error = String(error.stack || error.message || error);
  pairResults.outcome = 'fail';
  if (peers) pairResults.peers = ['alpha', 'beta'].map(role => peers[role]?.summary() ?? null);
  pairResults.wait_observations = waitObservations;
  for (const role of ['alpha', 'beta']) {
    if (!instances?.[role]) continue;
    try { instanceRows[role].failure_status = await instances[role].status(); } catch {}
    try { instanceRows[role].failure_native = await instances[role].native(); } catch {}
    try { await instances[role].screenshot(path.join(childDirectory(role), 'failure.png')); } catch (captureError) {
      instanceRows[role].failure_capture_error = String(captureError.message || captureError);
    }
  }
} finally {
  intentionalRelayClose = true;
  if (browserOwned && instances) {
    for (const [role, instance] of Object.entries(instances)) {
      try { await instance.freezePeerModuleIdentity(); }
      catch (error) { closeNotes.push(`${role} final module identity: ${String(error.message || error)}`); }
    }
  }
  if (browserOwned && relay) {
    try { await relay.close(); pairResults.relay_closed = true; }
    catch (error) { closeNotes.push(`browser peer close: ${String(error.stack || error)}`); }
  } else if (browserOwned && instances) {
    // Partial setup still owns page endpoints, even if no pair facade was built.
    await Promise.allSettled(Object.values(instances).map(instance => instance.armPeerClose()));
    for (const instance of Object.values(instances)) {
      try { await instance.closePeer(true); }
      catch (error) { closeNotes.push(`partial browser peer close: ${String(error.message || error)}`); }
    }
  }
  await transportCallbackQueue.drain();
  for (const role of ['alpha', 'beta']) {
    const instance = instances?.[role];
    if (instance) {
      if (instanceRows[role].source_accounting_start && !instanceRows[role].source_accounting_artifact) {
        try {
          const capture = await instance.readSourceAccounting({freeze: true});
          const bytes = Buffer.from(JSON.stringify(capture, null, 2) + '\n');
          await fs.writeFile(path.join(childDirectory(role), 'source-accounting.json'), bytes);
          instanceRows[role].source_accounting_artifact = {name: 'source-accounting.json',
            bytes: bytes.length, sha256: sha256(bytes), incomplete: true};
        } catch (error) { instanceRows[role].source_accounting_error = String(error.message || error); }
      }
      try { instanceRows[role].timing_pause_diagnostics = await instance.timingPauseDiagnostics(); } catch {}
      try { await instance.unload(); instanceRows[role].unloaded = true; }
      catch (error) {
        instanceRows[role].unloaded = false;
        instanceRows[role].unload_error = String(error.message || error);
        closeNotes.push(`${role} unload: ${String(error.message || error)}`);
      }
      try {
        const closed = await instance.close();
        if (closed !== true) closeNotes.push(`${role} browser close did not confirm completion`);
      } catch (error) { closeNotes.push(`${role} browser close: ${String(error.message || error)}`); }
      if (browserOwned) {
        try { instanceRows[role].final_peer_module_responses = await instance.finishPeerModuleIdentity(); }
        catch (error) { closeNotes.push(`${role} closed-browser module identity: ${String(error.message || error)}`); }
      }
      instanceRows[role].browser_closed = instance.closed;
      instanceRows[role].browser_diagnostics = instance.errors;
      instanceRows[role].page_errors = instance.errors.filter(row => row.kind === 'pageerror' || row.kind === 'console');
      instanceRows[role].final_status ??= await instance.status().catch(() => null);
      instanceRows[role].final_native ??= await instance.native().catch(() => null);
    }
    if (checksumFiles[role]) {
      await checksumFiles[role].close().catch(error => closeNotes.push(`${role} checksum file close: ${String(error.message || error)}`));
      const file = path.join(childDirectory(role), 'checksums.bin');
      try { instanceRows[role].checksums_sha256 = sha256(await fs.readFile(file)); } catch {}
    }
    if (instanceRows[role]) {
      pairResults[`peer_${role}`] = instanceRows[role];
      try { await fs.writeFile(path.join(childDirectory(role), 'instance.json'), JSON.stringify(instanceRows[role], null, 2) + '\n'); }
      catch (error) { closeNotes.push(`${role} instance report write: ${String(error.message || error)}`); }
    }
  }
  if (relay && !browserOwned) {
    recordAvailableTransportMetrics(pairResults.transport, relay);
    try {
      await relay.close();
      pairResults.relay_closed = true;
    } catch (error) {
      pairResults.relay_closed = false;
      closeNotes.push(`relay close: ${String(error.message || error)}`);
    }
  }
  // TCP close events may admit callbacks without awaiting them; join once more
  // after transport closure before freezing the result.
  await transportCallbackQueue.drain();
  if (transportErrors.length || callbackErrors.length || closeNotes.length) {
    pairResults.outcome = 'fail';
    pairResults.first_error ??= transportErrors[0]?.message || callbackErrors[0]?.message || closeNotes[0] ||
      'transport callback or cleanup failed';
  }
  pairResults.transport_errors = transportErrors;
  pairResults.callback_errors = callbackErrors;
  pairResults.cleanup_notes = closeNotes;
  pairResults.finished_at = new Date().toISOString();
  await fs.writeFile(path.join(output, 'run.json'), JSON.stringify(pairResults, null, 2) + '\n');
}
console.log(JSON.stringify({scenario: pairResults.scenario, outcome: pairResults.outcome,
  waits: pairResults.wait_observations?.length ?? 0, peers: pairResults.peers?.map(row => row?.role),
  first_error: pairResults.first_error}));
if (!['complete', 'expected-desync', 'expected-disconnect'].includes(pairResults.outcome)) process.exitCode = 1;
