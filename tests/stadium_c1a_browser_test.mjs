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
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createCssSssTransitionCapture, CSS_SSS_TRANSITION_LIMITS,
  cssReadinessEvidence, cssSssOutcome, hasCssMenuReadiness} from './stadium_c1a_css_sss_reducer.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {values} = parseArgs({options: Object.fromEntries(
  ['build', 'disc', 'disc-sha256', 'out', 'playwright', 'preflight', 'source-revision',
    'first-css-context', 'first-css-consumed-pad', 'first-css-return-expected',
    'first-css-tick-expected', 'first-css-draw-expected',
    'first-css-postdraw-input', 'first-css-postdraw-expected',
    'first-sss-pair-expected', 'first-sss-tick-expected', 'first-sss-tick-input']
    .map(name => [name, {type: 'string'}]).concat([
      ['css-sss-reducer', {type: 'boolean', default: false}],
      ['first-css-browser-draw', {type: 'boolean', default: false}],
      ['first-css-browser-stream', {type: 'boolean', default: false}],
      ['first-css-browser-final-draw', {type: 'boolean', default: false}],
      ['first-sss-constructor-pair', {type: 'boolean', default: false}],
      ['first-sss-consumed-pad-tick', {type: 'boolean', default: false}],
    ])), strict: true});
const cssSssReducer = values['css-sss-reducer'];
const firstSssConsumedPadTick = values['first-sss-consumed-pad-tick'];
const firstSssConstructorPair = values['first-sss-constructor-pair'] || firstSssConsumedPadTick;
const firstCssBrowserFinalDraw = values['first-css-browser-final-draw'] || firstSssConstructorPair;
const firstCssBrowserStream = values['first-css-browser-stream'] || firstCssBrowserFinalDraw;
const firstCssBrowserDraw = values['first-css-browser-draw'] || firstCssBrowserStream;
if ((cssSssReducer && firstCssBrowserDraw) ||
    (values['first-css-browser-draw'] && firstCssBrowserStream) ||
    (values['first-css-browser-stream'] && values['first-css-browser-final-draw']) ||
    (firstSssConstructorPair && !values['first-sss-pair-expected']) ||
    (!firstSssConstructorPair && values['first-sss-pair-expected']) ||
    (firstSssConsumedPadTick && (!values['first-sss-tick-expected'] ||
      !values['first-sss-tick-input'])) ||
    (!firstSssConsumedPadTick && (values['first-sss-tick-expected'] ||
      values['first-sss-tick-input'])))
  throw Error('Choose one private C1a diagnostic route');
const LIMITS = Object.freeze({
  serverStartMs: 10000,
  browserLaunchMs: 30000,
  pageStartMs: 15000,
  discImportMs: 60000,
  sourceTransitionMs: CSS_SSS_TRANSITION_LIMITS.transitionMs,
  stageDriveMs: 30000,
  manifestHandoffMs: 20000,
  unloadMs: 15000,
  cleanupMs: 5000,
  cleanupTotalMs: cssSssReducer ? 5000 : 15000,
  captureWorkMs: cssSssReducer ? 95000 : 150000,
  stageDriveFrames: 180,
});
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
    route_mode: firstSssConsumedPadTick ? 'first-sss-consumed-pad-tick' :
      firstSssConstructorPair ? 'first-sss-constructor-pair' :
      firstCssBrowserFinalDraw ? 'first-css-browser-final-pending-draw' :
      firstCssBrowserStream ? 'first-css-browser-postdraw-stream' :
      firstCssBrowserDraw ? 'first-css-browser-draw' :
      values['css-sss-reducer'] ? 'css-sss-reducer' : 'full-c1a',
    armed_before_css_entry: false,
    controls: null,
    input_recipe: null,
    original_css_phase: null,
    original_sss_phase: null,
    raw_pad_stage_drive_frames: 0,
    source_selection: null,
    css_sss_reducer: values['css-sss-reducer'] ? createCssSssTransitionCapture(
      'Original CSS -> original SSS only; one ordinary B0XX Start chord after observed source and input readiness. No SSS selection, stage, match, or source archive request.') : null,
    css_sss_transition: null,
    first_sss_constructor_pair: null,
    first_sss_consumed_tick: null,
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
  source_request_scope: firstSssConsumedPadTick
    ? 'Original CSS final draw, original SSS constructor pair, and one consumed SSS PAD/scheduler-end comparison. The run stops before any SSS draw or further input; no whole-session acceptance.'
    : firstSssConstructorPair
    ? 'Original SSS constructor entry1579/return1603 pair after the approved first-CSS boundary. Stops before any SSS host tick/draw; no SSS selection, VS/GO equality or whole-session acceptance.'
    : firstCssBrowserStream
    ? 'CSS-only source-consumed input/tick/draw comparison; terminal transition snapshot and final original draw remain unpaired. No SSS admission, VS/GO equality or whole-session acceptance.'
    : 'C1a stops after exact local-disc preparation. It does not instrument or prove a runtime source archive file-service request; full C1 remains open.',
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

let firstCssBrowserDrawInputs = null;
let firstSssConstructorPairInputs = null;
let firstSssConsumedPadTickInputs = null;
async function loadFirstCssBrowserDrawInputs() {
  if (!firstCssBrowserDraw) return null;
  const specs = {
    context_bundle: values['first-css-context'],
    consumed_pad_bundle: values['first-css-consumed-pad'],
    return_expected: values['first-css-return-expected'],
    tick_expected: values['first-css-tick-expected'],
    draw_expected: values['first-css-draw-expected'],
  };
  const frozen = preflight.first_css_browser_draw?.inputs;
  assert.ok(frozen && typeof frozen === 'object',
    'First-CSS browser-draw preflight omitted exact external input identities');
  const bytes = {};
  for (const [name, fileValue] of Object.entries(specs)) {
    assert.ok(fileValue, `First-CSS browser-draw requires --${name.replaceAll('_', '-')}`);
    const file = path.resolve(fileValue);
    const binding = frozen[name];
    assert.ok(binding && typeof binding === 'object', `Preflight omitted ${name}`);
    assert.equal(path.resolve(binding.path), file, `Preflight path differs for ${name}`);
    const data = await fs.readFile(file);
    assert.equal(data.byteLength, binding.bytes, `Preflight byte count differs for ${name}`);
    assert.equal(digest(data), binding.sha256, `Preflight hash differs for ${name}`);
    bytes[name] = data;
  }
  const contextExpected = JSON.parse(bytes.return_expected.toString('utf8'));
  const tickExpected = JSON.parse(bytes.tick_expected.toString('utf8'));
  const drawExpected = JSON.parse(bytes.draw_expected.toString('utf8'));
  assert.equal(contextExpected.schema, 'melee-web-stadium-first-css-context-diagnostic');
  assert.equal(tickExpected.schema, 'melee-web-stadium-first-css-consumed-tick-diagnostic');
  assert.equal(drawExpected.schema, 'melee-web-stadium-first-css-first-draw-diagnostic');
  assert.equal(contextExpected.provenance.stream_bytes, 4397889);
  assert.equal(contextExpected.provenance.stream_sha256,
    '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778');
  assert.equal(bytes.context_bundle.byteLength, contextExpected.input_bundle.bytes);
  assert.equal(digest(bytes.context_bundle), contextExpected.input_bundle.sha256);
  assert.equal(bytes.context_bundle.subarray(0, 8).toString('ascii'), 'STC1INPT');
  assert.equal(bytes.context_bundle.readUInt32BE(8), 1);
  assert.equal(bytes.context_bundle.subarray(12, 44).toString('hex'),
    contextExpected.provenance.stream_sha256);
  assert.equal(bytes.context_bundle.readUInt32BE(44), 704);
  assert.equal(bytes.context_bundle.readUInt32BE(48), 833);
  assert.equal(bytes.consumed_pad_bundle.byteLength, tickExpected.input_bundle.bytes);
  assert.equal(digest(bytes.consumed_pad_bundle), tickExpected.input_bundle.sha256);
  assert.equal(bytes.consumed_pad_bundle.subarray(0, 8).toString('ascii'), 'STC1PAD1');
  assert.equal(bytes.consumed_pad_bundle.subarray(12, 44).toString('hex'),
    tickExpected.provenance.stream_sha256);
  assert.equal(tickExpected.provenance.stream_sha256,
    contextExpected.provenance.stream_sha256);
  assert.equal(bytes.consumed_pad_bundle.readUInt32BE(44), 834);
  assert.equal(bytes.consumed_pad_bundle.readUInt32BE(48), 835);
  assert.equal(tickExpected.expected_post_tick.source_tick_sequence, 835);
  assert.equal(drawExpected.provenance.stream_sha256,
    contextExpected.provenance.stream_sha256);
  assert.equal(drawExpected.provenance.source_tick_sequence, 835);
  assert.equal(drawExpected.provenance.draw_enter_sequence, 836);
  assert.equal(drawExpected.provenance.draw_return_sequence, 837);
  assert.equal(contextExpected.expected_return.scene_kind, 8);
  assert.deepEqual(drawExpected.comparison_fields, [
    'source_tick', 'draw_ordinal', 'pad_state_hex', 'random_seed_hex',
    'scene_frame', 'scene_kind', 'scene_routing_getters']);
  return {bytes, contextExpected, tickExpected, drawExpected,
    bundle_hashes: Object.fromEntries(Object.entries(bytes).map(([name, data]) =>
      [name, {bytes: data.byteLength, sha256: digest(data)}]))};
}

let firstCssBrowserStreamInputs = null;
async function loadFirstCssBrowserStreamInputs() {
  if (!firstCssBrowserStream) return null;
  const frozen = preflight.first_css_browser_stream;
  assert.ok(frozen?.inputs && frozen.comparator,
    'CSS stream preflight omitted exact input and host-comparator identities');
  const bytes = {};
  for (const [name, option] of Object.entries({postdraw_input: 'first-css-postdraw-input',
    postdraw_expected: 'first-css-postdraw-expected'})) {
    assert.ok(values[option], `CSS stream requires --${option}`);
    const file = path.resolve(values[option]);
    const binding = frozen.inputs[name];
    assert.ok(binding, `CSS stream preflight omitted ${name}`);
    assert.equal(path.resolve(binding.path), file);
    const data = await fs.readFile(file);
    assert.equal(data.byteLength, binding.bytes, `CSS stream byte count differs for ${name}`);
    assert.equal(digest(data), binding.sha256, `CSS stream hash differs for ${name}`);
    bytes[name] = data;
  }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  assert.equal(path.resolve(frozen.comparator.path), helperPath);
  const helperBytes = await fs.readFile(helperPath);
  assert.equal(helperBytes.byteLength, frozen.comparator.bytes);
  assert.equal(digest(helperBytes), frozen.comparator.sha256);
  const expected = JSON.parse(bytes.postdraw_expected.toString('utf8'));
  assert.equal(expected.schema, 'melee-web-stadium-first-css-postdraw-stream-diagnostic');
  assert.equal(expected.version, 1);
  assert.equal(expected.provenance.stream_bytes, 4397889);
  assert.equal(expected.provenance.stream_sha256,
    firstCssBrowserDrawInputs.contextExpected.provenance.stream_sha256);
  assert.equal(expected.provenance.baseline_draw_return_sequence, 837);
  assert.equal(expected.provenance.source_batch_count, 148);
  assert.equal(expected.provenance.first_consumed_pad_sequence, 839);
  assert.equal(expected.provenance.last_consumed_pad_sequence, 1574);
  assert.equal(expected.provenance.first_draw_return_sequence, 842);
  assert.equal(expected.provenance.last_draw_return_sequence, 1577);
  assert.equal(expected.provenance.stop_before_sss_admission, true);
  assert.equal(expected.whole_session_equivalent, false);
  assert.equal(expected.source_admission, false);
  assert.deepEqual(expected.unpaired_routing_fields, ['pending_mode', 'next_state_id']);
  assert.deepEqual(expected.excluded_source_tags, [2, 36, 37]);
  assert.deepEqual(expected.draw_comparison_fields,
    firstCssBrowserDrawInputs.drawExpected.comparison_fields);
  const input = bytes.postdraw_input;
  assert.equal(input.byteLength, 6564);
  assert.equal(input.byteLength, expected.input_bundle.bytes);
  assert.equal(digest(input), expected.input_bundle.sha256);
  assert.equal(input.subarray(0, 8).toString('ascii'), 'STC1PSTR');
  assert.equal(input.readUInt32BE(8), 1);
  assert.equal(input.subarray(12, 44).toString('hex'), expected.provenance.stream_sha256);
  assert.equal(input.readUInt32BE(44), 839);
  assert.equal(input.readUInt32BE(48), 148);
  assert.equal(expected.input_bundle.sample_count, 148);
  assert.equal(expected.input_bundle.contains_expected_post_tick_state, false);
  assert.equal(expected.input_bundle.contains_expected_draw_state, false);
  // Validate the actual pure comparator's expected-boundary contract before
  // server/browser startup; this does not execute source or apply expected data.
  const helper = await import(pathToFileURL(helperPath).href);
  helper.createFirstCssStreamComparator(expected.expected_pairs);
  assert.equal(input.subarray(52).toString('hex'), expected.expected_pairs
    .flatMap(pair => pair.input_port_status_hex).join(''),
  'CSS stream input payload differs from the bound original consumed statuses');
  return {bytes, expected, helperSource: helperBytes.toString('utf8'),
    bundle_hashes: Object.fromEntries(Object.entries(bytes).map(([name, data]) =>
      [name, {bytes: data.byteLength, sha256: digest(data)}])),
    comparator: {path: helperPath, bytes: helperBytes.byteLength, sha256: digest(helperBytes)}};
}

async function loadFirstSssConstructorPairInputs() {
  if (!firstSssConstructorPair) return null;
  const file = path.resolve(values['first-sss-pair-expected']);
  const frozen = preflight.first_sss_constructor_pair?.expected_json;
  assert.ok(frozen && typeof frozen === 'object',
    'SSS-pair preflight omitted the exact retained original expected JSON identity');
  assert.equal(path.resolve(frozen.path), file,
    'SSS-pair expected JSON path differs from the frozen preflight');
  assert.equal(path.basename(file), frozen.basename,
    'SSS-pair expected JSON basename differs from the frozen preflight');
  const bytes = await fs.readFile(file);
  assert.equal(bytes.byteLength, frozen.bytes,
    'SSS-pair expected JSON byte count differs from the frozen preflight');
  assert.equal(digest(bytes), frozen.sha256,
    'SSS-pair expected JSON hash differs from the frozen preflight');
  let expected;
  try { expected = JSON.parse(bytes.toString('utf8')); }
  catch (error) { throw Error(`SSS-pair expected JSON is malformed: ${errorText(error)}`); }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  const helper = await import(pathToFileURL(helperPath).href);
  helper.createFirstSssConstructorPairComparator(expected);
  assert.equal(expected.schema, 'melee-web-stadium-first-sss-constructor-pair-diagnostic');
  assert.equal(expected.provenance.observer_sha256,
    firstCssBrowserDrawInputs.contextExpected.provenance.stream_sha256);
  assert.equal(expected.source_entry.argument, '0x80480668');
  assert.equal(expected.source_return.argument, '0x00000000');
  return {bytes, expected, expected_identity: {path: file, basename: path.basename(file),
    bytes: bytes.byteLength, sha256: digest(bytes)}};
}

async function loadFirstSssConsumedPadTickInputs() {
  if (!firstSssConsumedPadTick) return null;
  const specs = {
    expected_json: values['first-sss-tick-expected'],
    input_bundle: values['first-sss-tick-input'],
  };
  const frozen = preflight.first_sss_consumed_tick;
  assert.ok(frozen && typeof frozen === 'object',
    'SSS tick preflight omitted its exact expected/input identities');
  const bytes = {};
  const identities = {};
  for (const [name, fileValue] of Object.entries(specs)) {
    const file = path.resolve(fileValue);
    const identity = frozen[name];
    assert.ok(identity && typeof identity === 'object',
      `SSS tick preflight omitted ${name} identity`);
    assert.equal(path.resolve(identity.path), file,
      `SSS tick ${name} path differs from the frozen preflight`);
    assert.equal(path.basename(file), identity.basename,
      `SSS tick ${name} basename differs from the frozen preflight`);
    bytes[name] = await fs.readFile(file);
    assert.equal(bytes[name].byteLength, identity.bytes,
      `SSS tick ${name} byte count differs from the frozen preflight`);
    assert.equal(digest(bytes[name]), identity.sha256,
      `SSS tick ${name} hash differs from the frozen preflight`);
    identities[name] = {path: file, basename: path.basename(file),
      bytes: bytes[name].byteLength, sha256: digest(bytes[name])};
  }
  let expected;
  try { expected = JSON.parse(bytes.expected_json.toString('utf8')); }
  catch (error) { throw Error(`SSS tick expected JSON is malformed: ${errorText(error)}`); }
  const helperPath = path.join(ROOT, 'tests/stadium_first_css_stream_compare.mjs');
  const helper = await import(pathToFileURL(helperPath).href);
  helper.validateFirstSssConsumedTickExpectedSource(expected);
  const input = bytes.input_bundle;
  assert.equal(input.byteLength, expected.input_bundle.bytes);
  assert.equal(digest(input), expected.input_bundle.sha256);
  assert.equal(input.subarray(0, 8).toString('ascii'), 'STC1SSS1');
  assert.equal(input.readUInt32BE(8), 1);
  assert.equal(input.subarray(12, 44).toString('hex'), expected.provenance.observer_sha256);
  assert.equal(input.readUInt32BE(44), 1604);
  assert.equal(input.readUInt32BE(48), 1608);
  assert.equal(input.subarray(52).toString('hex'),
    expected.source_scheduler_end.expected.consumed_pad_status_hex.join(''),
    'SSS tick bundle differs from the exact four original PAD statuses');
  assert.deepEqual(expected.input_bundle.port_status_hex,
    expected.source_scheduler_end.expected.consumed_pad_status_hex);
  assert.equal(expected.input_bundle.contains_expected_state, false,
    'SSS tick input must contain only consumed PAD statuses and source bindings');
  return {bytes, expected, identities};
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

async function observeCssInputFrame(label, operationDeadline = deadline) {
  const operationTimeout = Math.min(5000, remaining(), operationDeadline - Date.now());
  if (operationTimeout <= 0) throw Error(`${label} deadline exhausted`);
  return timeout(page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => {
    const module = globalThis.Module;
    const phase = module?._melee_web_native_menu_phase?.() ?? null;
    const pointer = module?._melee_web_native_menu_message?.();
    const diagnosticsPointer = module?._melee_web_native_menu_diagnostics?.();
    let css = null, observerError = null;
    if (phase === 1) {
      if (typeof module?._melee_web_css_observe_port !== 'function') {
        observerError = 'CSS source observer export is absent';
      } else {
        const ids = module._malloc(14 * 4), geometry = module._malloc(8 * 4);
        if (!ids || !geometry) {
          if (ids) module._free(ids);
          if (geometry) module._free(geometry);
          observerError = 'CSS source observer allocation failed';
        } else {
          try {
            if (module._melee_web_css_observe_port(0, 8, ids, geometry)) {
              css = {ids: Array.from(module.HEAP32.subarray(ids >> 2, (ids >> 2) + 14)),
                geometry: Array.from(module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))};
            }
          } catch (error) { observerError = String(error?.message || error); }
          finally { module._free(ids); module._free(geometry); }
        }
      }
    }
    let input = null;
    try {
      if (typeof module?._melee_web_input_message === 'function')
        input = JSON.parse(module.UTF8ToString(module._melee_web_input_message()));
      else observerError ||= 'Input state export is absent';
    } catch (error) { observerError ||= `Input snapshot failed: ${String(error?.message || error)}`; }
    resolve({at_ms: performance.now(), phase,
      running: module?._melee_web_native_menu_running?.() ?? null,
      message: pointer ? module.UTF8ToString(pointer) : null,
      diagnostics: diagnosticsPointer ? module.UTF8ToString(diagnosticsPointer) : null,
      runtime_error: document.querySelector('#status')?.dataset.runtimeError || null,
      hidden: document.hidden, focused: document.hasFocus(), css, input, observer_error: observerError});
  }))), operationTimeout, label);
}

function assertFirstCssBrowserEntry(observation, expected) {
  assert.ok(observation?.entry, 'First-CSS browser entry was not captured');
  assert.equal(observation.source_stream_sha256, expected.provenance.stream_sha256);
  const actual = observation.entry;
  const wanted = expected.expected_return;
  assert.equal(actual.source_scene, 1, 'Native first-CSS return is not the original CSS scene');
  assert.equal(actual.scene_kind, wanted.scene_kind);
  assert.equal(actual.random_seed_hex, wanted.random_seed_hex);
  assert.equal(actual.pad_state_hex, wanted.pad_state_hex);
  assert.equal(actual.ko_counts_hex, wanted.ko_counts_hex);
  const actualCss = structuredClone(actual.css);
  const wantedCss = structuredClone(wanted.css);
  assert.ok(actualCss?.vs?.start?.rules &&
    Object.prototype.hasOwnProperty.call(actualCss.vs.start.rules, 'pad_x5C'),
  'Native first-CSS typed rules omitted the explicit ABI-padding field');
  delete actualCss.vs.start.rules.pad_x5C;
  for (const field of ['on_unpause_override', 'on_pause_override',
    'check_for_pauser_override', 'on_match_start', 'on_frame_start',
    'on_frame_end', 'on_match_end', 'x54_pointer', 'x58_pointer']) {
    assert.equal(actualCss.vs.start.rules[field], 'null',
      `Returned source pointer field is not null: ${field}`);
    actualCss.vs.start.rules[field] = null;
  }
  assert.deepEqual(actualCss, wantedCss,
    'Full typed first-CSS return differs from the captured original CSS return');
  assert.deepEqual(actual.abi_padding_excluded, ['pad_x5C']);
}

function assertFirstCssBrowserTick(observation, expected) {
  const actual = observation?.tick;
  const wanted = expected.expected_post_tick;
  assert.ok(actual, 'The one native first-CSS tick was not captured');
  for (const key of ['source_stream_sha256', 'consumed_pad_sequence', 'source_tick_sequence',
    'source_tick_value', 'source_draw_ordinal', 'original_source_frame',
    'phase_relation', 'source_scene_kind', 'host_source_scene', 'host_menu_phase',
    'native_post_host_tick_frame', 'random_seed_hex', 'pad_state_hex'])
    assert.equal(actual[key], wanted[key], `First-CSS tick differs at ${key}`);
  assert.deepEqual(actual.scene_routing_getters, wanted.scene_routing_getters,
    'First-CSS tick scene-routing getters differ');
  assert.equal(actual.host_tick_calls, 1);
  assert.equal(actual.host_tick_result, 1);
  assert.equal(actual.world_generation_stable, true);
  assert.equal(actual.source_scene_stable, true);
  assert.equal(actual.seed_owner_stable, true);
  assert.deepEqual(actual.routing_raw_fields_excluded, ['pending_mode', 'next_state_id']);
}

function assertFirstCssBrowserDraw(observation, expected) {
  const actual = observation?.draw;
  const wanted = expected.expected_draw_return;
  assert.ok(actual, 'The original first-CSS host draw return was not captured');
  for (const key of expected.comparison_fields)
    assert.deepEqual(actual[key], wanted[key], `First-CSS browser draw differs at ${key}`);
  assert.equal(actual.boundary, 'browser_host_draw_return');
  assert.equal(actual.observation_phase,
    'after source host draw, before Aurora end-frame');
  assert.equal(actual.host_draw_calls, 1);
  assert.equal(actual.aurora_begin_calls, 1);
  assert.equal(actual.source_scene, 1);
  assert.equal(actual.menu_phase, 1);
  assert.equal(actual.scene_kind, 8);
  assert.equal(actual.world_generation_stable, true);
  assert.equal(actual.scene_owner_stable, true);
  assert.equal(actual.seed_owner_stable, true);
  assert.equal(actual.world_generation, observation.tick.world_generation);
}

async function readFirstCssBrowserDrawObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const observe = module?._melee_web_native_menu_stadium_first_css_draw_observe;
    if (typeof observe !== 'function') throw Error('First-CSS browser-draw observer export is absent');
    const pointer = observe();
    return pointer ? {...JSON.parse(module.UTF8ToString(pointer)),
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null;
  });
}

async function readFirstCssBrowserStreamObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstCssStreamRead?.() ?? null};
  });
}

function assertFirstCssBrowserStreamTerminal(evidence) {
  const observation = evidence?.native;
  const native = observation?.postdraw_stream;
  const host = evidence?.host;
  assert.ok(native && host, 'CSS stream terminal evidence is absent');
  assert.equal(native.failed, false, native.error || 'Native CSS stream failed');
  assert.equal(native.terminal, true);
  assert.equal(native.error, null);
  assert.equal(observation.native_phase, 1, 'CSS-only stream left its CSS owner');
  assert.equal(observation.running, 0, 'Terminal CSS stream source clock is still running');
  assert.equal(host.hook_error, null);
  assert.equal(host.status.failed, false);
  assert.equal(host.status.terminal, true);
  assert.equal(host.status.pending_draw, false);
  assert.equal(native.input_source_stream_sha256,
    firstCssBrowserStreamInputs.expected.provenance.stream_sha256);
  assert.equal(native.consumed_inputs, 148);
  assert.equal(native.host_tick_calls, 148);
  assert.equal(native.executed_host_ticks, 149);
  assert.equal(native.current_input_index, 147);
  assert.equal(native.current_input_ordinal, 148);
  assert.equal(native.current_consumed_pad_sequence, 1574);
  if (native.outcome === 'stop_after_last_input_request') {
    assert.equal(native.status, 'terminal');
    assert.equal(native.complete, false);
    assert.equal(native.tick_result, 3);
    assert.equal(native.frame_end_returned, false,
      'Terminal transition unexpectedly started a source presentation');
    assert.equal(native.input_index, 147);
    assert.equal(native.matched_ticks, 147);
    assert.equal(native.matched_draws, 147);
    assert.equal(native.host_draw_calls, 147);
    assert.equal(native.aurora_begin_calls, 147);
    assert.equal(native.aurora_end_calls, 147);
    assert.equal(host.status.outcome, 'stop_after_last_input_request');
    assert.equal(host.status.next_index, 147);
    assert.equal(host.rows.length, 295);
    const last = host.rows.at(-1);
    assert.equal(last.phase, 'transition');
    assert.equal(last.approved, false, 'Terminal transition was incorrectly compared/approved');
    assert.deepEqual(last.actual, native.transition_snapshot);
    assert.deepEqual(host.status.terminal_observation, native.transition_snapshot);
    assert.equal(last.actual.consumed_pad_sequence, 1574);
    assert.equal(last.actual.executed_host_ticks, 149);
    assert.equal(last.actual.stream_input_ordinal, 148);
    assert.equal(last.actual.terminal_transition, true);
  } else {
    assert.equal(native.outcome, 'bounded_pair_cap');
    assert.equal(native.status, 'bounded-pair-cap');
    assert.equal(native.complete, true);
    assert.equal(native.tick_result, 1);
    assert.equal(native.frame_end_returned, true);
    assert.equal(native.input_index, 148);
    assert.equal(native.matched_ticks, 148);
    assert.equal(native.matched_draws, 148);
    assert.equal(native.host_draw_calls, 148);
    assert.equal(native.aurora_begin_calls, 148);
    assert.equal(native.aurora_end_calls, 148);
    assert.equal(host.status.outcome, 'bounded_pair_cap');
    assert.equal(host.status.next_index, 148);
    assert.equal(host.rows.length, 296);
    assert.equal(host.rows.at(-1).phase, 'draw');
    assert.equal(host.rows.at(-1).approved, true);
  }
  // Every paired boundary was synchronously approved in original input order;
  // only the explicitly unpaired final transition is a permitted false result.
  for (let index = 0; index < host.status.next_index; index++) {
    assert.equal(host.rows[index * 2].phase, 'tick');
    assert.equal(host.rows[index * 2 + 1].phase, 'draw');
    assert.equal(host.rows[index * 2].approved, true);
    assert.equal(host.rows[index * 2 + 1].approved, true);
  }
}

async function runFirstCssBrowserPostdrawStream(baseline) {
  // This call is reached only after every existing full typed entry/tick/draw
  // assertion and one-shot lifecycle assertion has succeeded in Node.
  assert.equal(baseline.state, 'complete');
  assert.equal(baseline.host_tick_calls, 1);
  assert.equal(baseline.host_draw_calls, 1);
  assert.equal(baseline.running, 0);
  report.scenario.first_css_postdraw_inputs = firstCssBrowserStreamInputs.bundle_hashes;
  report.scenario.first_css_stream_comparator = firstCssBrowserStreamInputs.comparator;
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedPairs}) => {
    if (globalThis.__meleeWebStadiumFirstCssStreamCompare ||
        globalThis.__meleeWebStadiumFirstCssStreamRead)
      throw Error('CSS stream host comparator was already registered');
    const {createFirstCssStreamComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstCssStreamComparator(expectedPairs);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstCssStreamRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstCssStreamCompare = (phase, actualJson) => {
      if (rows.length >= 297) {
        hookError = 'CSS stream callback count exceeded the bound';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      // No runtime exports or guest writes are called by this synchronous hook.
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      rows.push({phase, actual, approved});
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
        Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
      expectedPairs: firstCssBrowserStreamInputs.expected.expected_pairs}),
  Math.min(5000, remaining()), 'Register host-only synchronous CSS stream comparison');
  const arm = await timeout(page.evaluate(inputBytes => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_css_postdraw_stream_arm;
    if (typeof armFunction !== 'function' ||
        typeof globalThis.__meleeWebStadiumFirstCssStreamCompare !== 'function')
      throw Error('Private CSS stream arm or host comparator is absent');
    const pointer = module._malloc(inputBytes.length);
    if (!pointer) throw Error('CSS stream input allocation failed');
    try {
      // Only fixed input-only PAD bytes enter WASM; expected rows remain above.
      module.HEAPU8.set(inputBytes, pointer);
      const result = armFunction(pointer, inputBytes.length);
      if (result !== 1) {
        const message = module._melee_web_native_menu_message();
        throw Error(message ? module.UTF8ToString(message) : 'CSS stream arm refused');
      }
      return result;
    } finally { module._free(pointer); }
  }, [...firstCssBrowserStreamInputs.bytes.postdraw_input]),
  Math.min(5000, remaining()), 'Arm input-only post-draw CSS stream');
  assert.equal(arm, 1);
  const armed = await timeout(readFirstCssBrowserStreamObservation(),
    Math.min(5000, remaining()), 'Armed CSS stream evidence before kick');
  report.scenario.first_css_stream_armed = armed;
  await persistReport();
  assert.equal(armed.native?.postdraw_stream?.status, 'armed');
  assert.equal(armed.native.running, 0);
  assert.equal(armed.host.rows.length, 0);
  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_css_postdraw_stream_kick;
    if (typeof kickFunction !== 'function') throw Error('CSS stream kick export is absent');
    const result = kickFunction();
    if (result !== 1) {
      const message = module._melee_web_native_menu_message();
      throw Error(message ? module.UTF8ToString(message) : 'CSS stream kick refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release bounded source-consumed CSS stream');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const stream = JSON.parse(module.UTF8ToString(pointer)).postdraw_stream;
    return stream?.terminal === true || stream?.complete === true || stream?.failed === true;
  }, null, {timeout: Math.min(60000, remaining())}),
  Math.min(60000, remaining()), 'CSS stream first mismatch, transition or bounded cap');
  const evidence = await timeout(readFirstCssBrowserStreamObservation(),
    Math.min(5000, remaining()), 'Terminal CSS stream native and host evidence');
  report.scenario.first_css_browser_stream = evidence;
  await persistReport();
  assertFirstCssBrowserStreamTerminal(evidence);
  report.scenario.css_stream_claim = evidence.native.postdraw_stream.outcome ===
    'stop_after_last_input_request'
    ? '147 additional tick/draw pairs compared; last additional input consumed and native transition retained unpaired; original final draw1577 unpaired. No SSS admission or transition-state equivalence.'
    : '148 additional tick/draw pairs compared; bounded pair cap reached without native transition. No SSS admission or transition equivalence.';
  return evidence.native;
}

async function readFirstCssBrowserFinalDrawObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstCssFinalDrawRead?.() ?? null};
  });
}

async function readFirstSssConstructorPairObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssPairRead?.() ?? null};
  });
}

async function readFirstSssConsumedTickObservation() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {native: observation ? {...observation,
      native_phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running()} : null,
      host: globalThis.__meleeWebStadiumFirstSssTickRead?.() ?? null};
  });
}

async function readFirstSssPairPostUnloadState() {
  return page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {phase: module?._melee_web_native_menu_phase?.() ?? null,
      running: module?._melee_web_native_menu_running?.() ?? null,
      observation: pointer ? module.UTF8ToString(pointer) : null,
      first_sss_constructor_pair: observation?.first_sss_constructor_pair ?? null,
      first_sss_consumed_tick: observation?.first_sss_consumed_tick ?? null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') &&
        !document.querySelector('#disc').disabled};
  });
}

function assertFirstCssBrowserFinalDraw(evidence, stoppedStream,
    {expectedNativePhase = 1} = {}) {
  const observation = evidence?.native;
  const final = observation?.final_pending_css_draw;
  const host = evidence?.host;
  assert.ok(final && host, 'Final pending-CSS draw evidence is absent');
  assert.deepEqual(observation.postdraw_stream, stoppedStream,
    'Separate final draw changed the retained terminal stream');
  assert.equal(observation.native_phase, expectedNativePhase);
  assert.equal(observation.running, 0);
  for (const key of ['armed', 'kicked', 'attempted', 'captured', 'compared',
    'complete', 'frame_end_returned']) assert.equal(final[key], true, key);
  assert.equal(final.failed, false, final.error || 'Final CSS draw failed');
  assert.equal(final.error, null);
  assert.equal(final.input_ordinal, 148);
  assert.equal(final.consumed_pad_sequence, 1574);
  assert.equal(final.terminal_tick_sequence, 1575);
  assert.equal(final.draw_enter_sequence, 1576);
  assert.equal(final.draw_return_sequence, 1577);
  assert.equal(final.source_steps, 0);
  assert.equal(final.source_draws, 0);
  assert.equal(final.source_pending, false);
  assert.equal(final.host_draw_calls, 1);
  assert.equal(final.aurora_begin_calls, 1);
  assert.equal(final.aurora_end_calls, 1);
  assert.equal(host.hook_error, null);
  assert.deepEqual(host.status, {attempted: true, approved: true, failed: false});
  assert.equal(host.rows.length, 1);
  assert.equal(host.rows[0].phase, 'final_draw');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.rows[0].actual, final.actual_draw);
  const wanted = firstCssBrowserStreamInputs.expected.expected_pairs[147].expected_draw_return;
  for (const key of firstCssBrowserDrawInputs.drawExpected.comparison_fields)
    assert.deepEqual(final.actual_draw[key], wanted[key], `Final CSS draw differs at ${key}`);
  assert.equal(final.actual_draw.world_generation, stoppedStream.transition_snapshot.world_generation);
  for (const key of ['world_generation_stable', 'scene_owner_stable', 'seed_owner_stable'])
    assert.equal(final.actual_draw[key], true, key);
  assert.equal(final.actual_draw.source_scene, 1);
  assert.equal(final.actual_draw.menu_phase, 1);
  assert.equal(final.actual_draw.scene_kind, 8);
}

async function runFirstCssBrowserFinalPendingDraw() {
  // Preserve and assert the completed stream before authorizing any final draw.
  const stopped = report.scenario.first_css_browser_stream;
  assertFirstCssBrowserStreamTerminal(stopped);
  assert.equal(stopped.native.postdraw_stream.outcome, 'stop_after_last_input_request',
    'Final pending draw requires the retained result3 boundary, not the pair cap');
  const stoppedStream = structuredClone(stopped.native.postdraw_stream);
  await persistReport();
  await timeout(page.evaluate(async ({moduleUrl, expectedPair, terminal}) => {
    if (globalThis.__meleeWebStadiumFirstCssFinalDrawCompare ||
        globalThis.__meleeWebStadiumFirstCssFinalDrawRead)
      throw Error('Final CSS draw host comparator was already registered');
    const {createFirstCssFinalDrawComparator, requestSynchronousApproval} = await import(moduleUrl);
    const comparator = createFirstCssFinalDrawComparator(expectedPair, terminal);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstCssFinalDrawRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstCssFinalDrawCompare = (phase, actualJson) => {
      if (rows.length >= 1) { hookError = 'Final CSS draw callback repeated'; return false; }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      rows.push({phase, actual, approved});
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedPair: firstCssBrowserStreamInputs.expected.expected_pairs[147],
    terminal: stoppedStream.transition_snapshot}), Math.min(5000, remaining()),
  'Register separate host-only final CSS draw comparison');
  // No input or expected state is allocated/copied into WASM here.
  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_css_final_draw_arm;
    if (typeof fn !== 'function') throw Error('Final CSS draw arm export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Final CSS draw arm refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Arm one checked final pending-CSS draw');
  assert.equal(arm, 1);
  report.scenario.first_css_final_draw_armed = await timeout(
    readFirstCssBrowserFinalDrawObservation(), Math.min(5000, remaining()),
    'Final CSS draw evidence before kick');
  await persistReport();
  assert.deepEqual(report.scenario.first_css_final_draw_armed.native.postdraw_stream, stoppedStream);
  assert.equal(report.scenario.first_css_final_draw_armed.native.running, 0);
  assert.equal(report.scenario.first_css_final_draw_armed.native.final_pending_css_draw.armed, true);
  assert.equal(report.scenario.first_css_final_draw_armed.host.rows.length, 0);
  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_css_final_draw_kick;
    if (typeof fn !== 'function') throw Error('Final CSS draw kick export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Final CSS draw kick refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release one source draw without another tick');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const final = JSON.parse(module.UTF8ToString(pointer)).final_pending_css_draw;
    return final?.complete === true || final?.failed === true;
  }, null, {timeout: Math.min(30000, remaining())}), Math.min(30000, remaining()),
  'Final CSS draw comparison and paired Aurora end');
  const evidence = await timeout(readFirstCssBrowserFinalDrawObservation(),
    Math.min(5000, remaining()), 'Retain final CSS draw actual comparison before cleanup');
  report.scenario.first_css_final_pending_draw = evidence;
  await persistReport();
  assertFirstCssBrowserFinalDraw(evidence, stoppedStream);
  report.scenario.css_final_draw_claim =
    'Original final CSS DrawReturn1577 seven fields compared after147 additional pairs; terminal scheduler snapshot and raw pending/next remain unpaired. No SSS admission.';
  return evidence.native;
}

function assertFirstSssPostCaptureState(evidence, baseline) {
  const native = evidence?.native;
  const pair = native?.first_sss_constructor_pair;
  assert.ok(pair, 'Native SSS constructor-pair observation is absent');
  assert.equal(pair.captured, true, 'SSS constructor notes were not captured');
  assert.equal(pair.host_entered, true, 'SSS host-enter did not complete');
  assert.equal(pair.sss_host_tick_calls, 0, 'SSS host tick ran before terminal capture');
  assert.equal(pair.sss_host_draw_calls, 0, 'SSS host draw ran before terminal capture');
  assert.equal(native.running, 0, 'Host time continued after SSS constructor capture');
  assert.deepEqual(native.entry, baseline.entry,
    'Original typed CSS entry changed during SSS capture');
  assert.deepEqual(native.tick, baseline.tick,
    'Original typed CSS tick changed during SSS capture');
  assert.deepEqual(native.draw, baseline.draw,
    'Original typed CSS draw changed during SSS capture');
  assert.deepEqual(native.postdraw_stream, baseline.postdraw_stream,
    'Original CSS postdraw stream changed during SSS capture');
  assert.deepEqual(native.final_pending_css_draw, baseline.final_pending_css_draw,
    'Original final CSS draw changed during SSS capture');
}

async function runFirstSssConstructorPair() {
  const finalCss = report.scenario.first_css_final_pending_draw;
  const stoppedStream = report.scenario.first_css_browser_stream;
  assertFirstCssBrowserFinalDraw(finalCss, stoppedStream.native.postdraw_stream);
  assert.equal(stoppedStream.native.postdraw_stream.outcome,
    'stop_after_last_input_request');
  assert.equal(finalCss.native.native_phase, 1,
    'SSS pair requires the ordinary pending CSS leave to remain entered');
  assert.equal(finalCss.native.final_pending_css_draw.frame_end_returned, true);
  assert.equal(finalCss.native.final_pending_css_draw.complete, true);
  assert.equal(finalCss.native.final_pending_css_draw.compared, true);
  report.scenario.first_sss_constructor_pair = {
    expected_json: firstSssConstructorPairInputs.expected_identity,
    arm_snapshot: null,
    kick_result: null,
    observation: null,
    claim: null,
  };
  await persistReport();

  await timeout(page.evaluate(async ({moduleUrl, expectedPair}) => {
    if (globalThis.__meleeWebStadiumFirstSssPairCompare ||
        globalThis.__meleeWebStadiumFirstSssPairRead)
      throw Error('SSS pair host comparator was already registered');
    const {createFirstSssConstructorPairComparator, requestSynchronousApproval} =
      await import(moduleUrl);
    const comparator = createFirstSssConstructorPairComparator(expectedPair);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssPairRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssPairCompare = (phase, actualJson) => {
      if (rows.length >= 1) {
        hookError = 'SSS pair comparison callback repeated';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      // Retain the complete actual pair before invoking the strict compare.
      const row = {phase, actual, approved: null};
      rows.push(row);
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedPair: firstSssConstructorPairInputs.expected}),
  Math.min(5000, remaining()), 'Register host-only original SSS constructor-pair comparison');

  const arm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_pair_arm;
    if (typeof fn !== 'function') throw Error('Original SSS pair arm export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Original SSS pair arm refused');
    }
    return result;
  }), Math.min(5000, remaining()), 'Arm checked pending CSS-to-SSS pair route');
  assert.equal(arm, 1);
  const beforeKick = await timeout(readFirstSssConstructorPairObservation(),
    Math.min(5000, remaining()), 'Retain armed SSS constructor-pair state');
  report.scenario.first_sss_constructor_pair.arm_snapshot = beforeKick;
  await persistReport();
  assert.equal(beforeKick.native?.final_pending_css_draw?.complete, true);
  assert.equal(beforeKick.native?.final_pending_css_draw?.failed, false);
  assert.equal(beforeKick.native?.first_sss_constructor_pair?.armed, true);
  assert.equal(beforeKick.native?.first_sss_constructor_pair?.kicked, false);
  assert.deepEqual(beforeKick.native?.postdraw_stream, stoppedStream.native.postdraw_stream,
    'SSS arm changed the approved CSS source stream');
  assert.deepEqual(beforeKick.native?.final_pending_css_draw,
    finalCss.native.final_pending_css_draw,
    'SSS arm changed the approved final CSS draw observation');
  assert.equal(beforeKick.host?.rows?.length, 0);

  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const fn = module?._melee_web_native_menu_stadium_first_sss_pair_kick;
    if (typeof fn !== 'function') throw Error('Original SSS pair kick export is absent');
    const result = fn();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'Original SSS pair kick refused');
    }
    return result;
  }), Math.min(5000, remaining()),
  'Complete pending CSS leave/rebuild and enter SSS without another input or tick');
  report.scenario.first_sss_constructor_pair.kick_result = kick;
  await persistReport();
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const pair = JSON.parse(module.UTF8ToString(pointer)).first_sss_constructor_pair;
    return pair?.complete === true || pair?.failed === true;
  }, null, {timeout: Math.min(LIMITS.sourceTransitionMs, remaining())}),
  Math.min(LIMITS.sourceTransitionMs, remaining()),
  'Original SSS host-enter constructor pair and first mismatch');

  const evidence = await timeout(readFirstSssConstructorPairObservation(),
    Math.min(5000, remaining()), 'Retain both original SSS constructor notes before cleanup');
  report.scenario.first_sss_constructor_pair.observation = evidence;
  report.scenario.first_sss_constructor_pair.claim =
    'SSS constructor observation attempt retained; capture, stop guarantees and semantic comparison have not yet passed their checks. No equivalence or acceptance claim.';
  await persistReport();

  const postcaptureCssBaseline = await timeout(readFirstCssBrowserFinalDrawObservation(),
    Math.min(5000, remaining()), 'Retain the current CSS baseline after SSS capture');
  report.scenario.first_sss_constructor_pair.postcapture_css_baseline = postcaptureCssBaseline;
  await persistReport();

  const nativePair = evidence.native?.first_sss_constructor_pair;
  assertFirstSssPostCaptureState(evidence, {
    entry: report.scenario.first_css_browser_draw.entry,
    tick: report.scenario.first_css_browser_draw.tick,
    draw: report.scenario.first_css_browser_draw.draw,
    postdraw_stream: stoppedStream.native.postdraw_stream,
    final_pending_css_draw: finalCss.native.final_pending_css_draw,
  });
  assertFirstCssBrowserEntry(evidence.native, firstCssBrowserDrawInputs.contextExpected);
  assertFirstCssBrowserTick(evidence.native, firstCssBrowserDrawInputs.tickExpected);
  assertFirstCssBrowserDraw(evidence.native, firstCssBrowserDrawInputs.drawExpected);
  assertFirstCssBrowserFinalDraw(postcaptureCssBaseline,
    stoppedStream.native.postdraw_stream, {expectedNativePhase: 3});
  const host = evidence.host;
  assert.ok(nativePair, 'Native observation omitted the first_sss_constructor_pair sibling');
  assert.equal(nativePair.failed, false, nativePair.error || 'Native SSS pair failed');
  assert.equal(nativePair.status, 'complete');
  for (const key of ['armed', 'kicked', 'attempted', 'captured', 'compared', 'complete'])
    assert.equal(nativePair[key], true, key);
  assert.equal(nativePair.error, null);
  assert.equal(nativePair.host_entered, true);
  assert.equal(nativePair.sss_host_tick_calls, 0);
  assert.equal(nativePair.sss_host_draw_calls, 0);
  assert.equal(evidence.native.running, 0,
    'Host time continued after paired SSS constructor capture');
  assert.equal(host?.hook_error, null);
  assert.equal(host?.status?.attempted, true);
  assert.equal(host?.status?.approved, true);
  assert.equal(host?.status?.failed, false);
  assert.equal(host?.status?.first_mismatch, null);
  assert.equal(host?.status?.error, null);
  assert.equal(host?.rows?.length, 1);
  assert.equal(host.rows[0].phase, 'sss_pair');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.status.actual_pair, host.rows[0].actual);
  assert.deepEqual(host.rows[0].actual.entry, nativePair.entry);
  assert.deepEqual(host.rows[0].actual.returned, nativePair.returned);
  assert.equal(nativePair.entry.host_entered, false);
  assert.equal(nativePair.returned.host_entered, false);
  assert.equal(nativePair.entry.session_ticks, nativePair.returned.session_ticks);
  report.scenario.first_sss_constructor_pair.claim =
    'Original SSS OnEnter entry1579/return1603 approved with both passive notes retained; zero SSS host ticks/draws, before SSS input or progression. No whole-session acceptance.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-constructor-pair');
  return evidence.native;
}

function assertFirstSssConsumedTickStopState(evidence, cssBaseline, constructorPair) {
  const native = evidence?.native;
  const tick = native?.first_sss_consumed_tick;
  assert.ok(tick, 'Native SSS consumed-tick observation is absent');
  assert.equal(tick.attempted, true, 'The one authorized SSS host tick was not attempted');
  assert.equal(tick.host_tick_calls, 1, 'The authorized SSS host tick count differs');
  assert.equal(tick.host_draw_calls, 0, 'An SSS host draw ran after consumed-tick capture');
  assert.equal(native.running, 0, 'Host source time continued after the one-shot SSS tick');
  assert.equal(tick.post_host_frame_captured, true,
    'The actual post-host frame was not captured after the scheduler sample');
  assert.equal(tick.post_host_frame, 1,
    'The one host tick did not finish at source frame one');
  assert.deepEqual(native.first_sss_constructor_pair, constructorPair,
    'The approved constructor pair or its zero-tick/draw counters changed during the tick');
  assert.deepEqual(native.entry, cssBaseline.entry,
    'Original typed CSS entry changed during SSS tick capture');
  assert.deepEqual(native.tick, cssBaseline.tick,
    'Original typed CSS tick changed during SSS tick capture');
  assert.deepEqual(native.draw, cssBaseline.draw,
    'Original typed CSS draw changed during SSS tick capture');
  assert.deepEqual(native.postdraw_stream, cssBaseline.postdraw_stream,
    'Original CSS postdraw stream changed during SSS tick capture');
  assert.deepEqual(native.final_pending_css_draw, cssBaseline.final_pending_css_draw,
    'Original final CSS draw changed during SSS tick capture');
}

function retainFirstSssConsumedTickEvidence(scenario, evidence, kickResult) {
  scenario.kick_result = kickResult;
  scenario.observation = evidence;
  scenario.claim = 'One-shot SSS scheduler-end sample attempt retained; comparison and protocol gates are pending.';
}

function assertFirstSssConsumedTickCompared(evidence, kickResult) {
  const nativeTick = evidence?.native?.first_sss_consumed_tick;
  const host = evidence?.host;
  assert.equal(kickResult?.result, 1,
    kickResult?.message || 'SSS consumed-tick kick failed');
  assert.equal(nativeTick?.captured, true, 'Scheduler-end SSS state was not captured');
  assert.equal(nativeTick?.host_draw_calls, 0);
  assert.equal(nativeTick?.clock_post_succeeded, true,
    'The one SSS host tick did not complete its ordinary menu clock post');
  assert.equal(nativeTick?.tick_result, 1, 'The ordinary SSS host tick did not return success');
  assert.equal(nativeTick?.transition_requested, false,
    'The single retained PAD input requested a scene transition');
  assert.equal(nativeTick?.post_host_frame_captured, true);
  assert.equal(nativeTick?.post_host_frame, 1);
  assert.equal(nativeTick?.failed, false, nativeTick?.error || 'SSS consumed-tick host failed');
  assert.equal(nativeTick?.compared, true);
  assert.equal(nativeTick?.complete, true);
  assert.equal(host?.hook_error, null);
  assert.equal(host?.status?.attempted, true);
  assert.equal(host?.status?.approved, true);
  assert.equal(host?.status?.failed, false);
  assert.equal(host?.status?.first_mismatch, null);
  assert.equal(host?.rows?.length, 1);
  assert.equal(host.rows[0].phase, 'scheduler_end');
  assert.equal(host.rows[0].approved, true);
  assert.deepEqual(host.status.actual, host.rows[0].actual);
}

async function runFirstSssConsumedPadTick(constructorNative) {
  assert.ok(constructorNative?.first_sss_constructor_pair,
    'First SSS consumed tick requires the retained constructor-pair observation');
  const constructorPair = constructorNative.first_sss_constructor_pair;
  assert.equal(constructorPair.complete, true);
  assert.equal(constructorPair.compared, true);
  assert.equal(constructorPair.failed, false);
  assert.equal(constructorPair.host_entered, true);
  assert.equal(constructorPair.sss_host_tick_calls, 0);
  assert.equal(constructorPair.sss_host_draw_calls, 0);
  const cssBaseline = report.scenario.first_sss_constructor_pair.postcapture_css_baseline.native;
  assert.ok(cssBaseline, 'First SSS tick requires the retained postconstructor CSS baseline');
  report.scenario.first_sss_consumed_tick = {
    expected_json: firstSssConsumedPadTickInputs.identities.expected_json,
    input_bundle: firstSssConsumedPadTickInputs.identities.input_bundle,
    arm_snapshot: null,
    kick_result: null,
    observation: null,
    claim: 'One-shot SSS scheduler-end sample attempt retained; comparison and protocol gates are pending.',
  };
  await persistReport();

  await timeout(page.evaluate(async ({moduleUrl, expectedTick, pair}) => {
    if (globalThis.__meleeWebStadiumFirstSssTickCompare ||
        globalThis.__meleeWebStadiumFirstSssTickRead)
      throw Error('SSS consumed-tick comparator was already registered');
    const {createFirstSssConsumedPadTickComparator, requestSynchronousApproval} =
      await import(moduleUrl);
    const comparator = createFirstSssConsumedPadTickComparator(expectedTick, pair);
    const rows = [];
    let hookError = null;
    globalThis.__meleeWebStadiumFirstSssTickRead = () => ({
      status: comparator.status(), rows, hook_error: hookError});
    globalThis.__meleeWebStadiumFirstSssTickCompare = (phase, actualJson) => {
      if (rows.length >= 1) {
        hookError = 'SSS consumed-tick comparison callback repeated';
        return false;
      }
      let actual;
      try { actual = JSON.parse(actualJson); }
      catch { actual = {malformed_json: String(actualJson).slice(0, 6000)}; }
      const row = {phase, actual, approved: null};
      rows.push(row);
      const approved = requestSynchronousApproval(comparator.compare, phase, actualJson);
      row.approved = approved === true;
      return approved === true;
    };
  }, {moduleUrl: 'data:text/javascript;base64,' +
      Buffer.from(firstCssBrowserStreamInputs.helperSource).toString('base64'),
    expectedTick: firstSssConsumedPadTickInputs.expected, pair: constructorPair}),
  Math.min(5000, remaining()), 'Register exact original SSS frame-zero comparison');

  const arm = await timeout(page.evaluate(bundle => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_sss_tick_arm;
    if (typeof armFunction !== 'function') throw Error('SSS consumed-tick arm export is absent');
    if (module._melee_web_native_menu_running() !== 0)
      throw Error('SSS consumed-tick arm requires the stopped constructor-only host');
    const pointer = module._malloc(bundle.length);
    if (!pointer) throw Error('SSS consumed-tick input allocation failed');
    try {
      module.HEAPU8.set(bundle, pointer);
      const result = armFunction(pointer, bundle.length);
      const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
      return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
    } finally { module._free(pointer); }
  }, Array.from(firstSssConsumedPadTickInputs.bytes.input_bundle)),
  Math.min(5000, remaining()), 'Arm one exact consumed SSS PAD input bundle');
  const armSnapshot = await timeout(readFirstSssConsumedTickObservation(),
    Math.min(5000, remaining()), 'Retain armed SSS consumed-tick state');
  report.scenario.first_sss_consumed_tick.arm_snapshot = armSnapshot;
  await persistReport();
  assert.equal(arm.result, 1, arm.message || 'SSS consumed-tick arm refused');
  assert.equal(armSnapshot.native?.first_sss_consumed_tick?.armed, true);
  assert.equal(armSnapshot.native?.first_sss_consumed_tick?.kicked, false);
  assert.deepEqual(armSnapshot.native?.first_sss_constructor_pair, constructorPair,
    'SSS tick arm changed the approved constructor pair');
  assert.deepEqual(armSnapshot.native?.entry, cssBaseline.entry,
    'SSS tick arm changed the original typed CSS entry');
  assert.deepEqual(armSnapshot.native?.tick, cssBaseline.tick,
    'SSS tick arm changed the original typed CSS tick');
  assert.deepEqual(armSnapshot.native?.draw, cssBaseline.draw,
    'SSS tick arm changed the original typed CSS draw');
  assert.deepEqual(armSnapshot.native?.postdraw_stream, cssBaseline.postdraw_stream,
    'SSS tick arm changed the original CSS stream');
  assert.deepEqual(armSnapshot.native?.final_pending_css_draw, cssBaseline.final_pending_css_draw,
    'SSS tick arm changed the approved final CSS draw');

  const kickResult = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_sss_tick_kick;
    if (typeof kickFunction !== 'function') throw Error('SSS consumed-tick kick export is absent');
    const result = kickFunction();
    const messagePointer = result === 1 ? 0 : module._melee_web_native_menu_message?.();
    return {result, message: messagePointer ? module.UTF8ToString(messagePointer) : null};
  }), Math.min(10000, remaining()), 'Run exactly one ordinary SSS host tick and stop before draw');
  report.scenario.first_sss_consumed_tick.kick_result = kickResult;
  await persistReport();

  const evidence = await timeout(readFirstSssConsumedTickObservation(),
    Math.min(5000, remaining()), 'Retain scheduler-end sample before success checks');
  retainFirstSssConsumedTickEvidence(report.scenario.first_sss_consumed_tick, evidence, kickResult);
  await persistReport();

  // Keep stop, counter and old-boundary checks independent from a source-state
  // mismatch. A mismatch remains a failing comparison after these assertions.
  assertFirstSssConsumedTickStopState(evidence, cssBaseline, constructorPair);
  assertFirstSssConsumedTickCompared(evidence, kickResult);
  report.scenario.first_sss_consumed_tick.claim =
    'Original SSS consumed PAD row1604 and frame-zero scheduler-end row1608 matched; exactly one normal host tick completed to frame1 with no draw or transition. This is one bounded boundary comparison, not whole-session acceptance.';
  await persistReport();
  await saveScreenshot('stadium-first-sss-consumed-pad-tick');
  return evidence.native;
}

async function runFirstCssBrowserDraw({baseUrl, artifacts, before}) {
  const prearm = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const memory = module?._melee_web_native_menu_memory;
    if (typeof memory !== 'function')
      throw Error('First-CSS preparation ownership observer is absent');
    const pointer = memory();
    if (!pointer) throw Error('First-CSS preparation ownership observation is absent');
    return {phase: module._melee_web_native_menu_phase(),
      running: module._melee_web_native_menu_running(),
      memory: JSON.parse(module.UTF8ToString(pointer))};
  }), Math.min(5000, remaining()), 'Prepared first-CSS imported-disc ownership');
  report.scenario.first_css_prearm = prearm;
  await persistReport();
  assert.equal(prearm.phase, 0);
  assert.equal(prearm.running, 0);
  assert.equal(prearm.memory.scoped_assets, true,
    'First-CSS diagnostic requires the canonical imported-disc asset scope');
  assert.equal(prearm.memory.source_session_owned, true);
  assert.equal(prearm.memory.menu_present, true);
  assert.equal(prearm.memory.menu_host_entered, false);
  assert.equal(prearm.memory.menu_phase, 0);
  assert.equal(prearm.memory.staged_asset_files, 0);
  assert.equal(prearm.memory.asset_generation, 0);
  assert.ok(prearm.memory.asset_files > 0,
    'First-CSS diagnostic requires the committed menu assets');
  const arm = await timeout(page.evaluate(({contextBytes, consumedBytes}) => {
    const module = globalThis.Module;
    const armFunction = module?._melee_web_native_menu_stadium_first_css_draw_arm;
    const observe = module?._melee_web_native_menu_stadium_first_css_draw_observe;
    if (typeof armFunction !== 'function' || typeof observe !== 'function')
      throw Error('Private first-CSS browser-draw exports are absent');
    if (module._melee_web_native_menu_phase() !== 0 || module._melee_web_native_menu_running() !== 0)
      throw Error('First-CSS bundles must be applied to the fresh, unentered VS owner');
    const context = module._malloc(contextBytes.length);
    const consumed = module._malloc(consumedBytes.length);
    if (!context || !consumed) {
      if (context) module._free(context);
      if (consumed) module._free(consumed);
      throw Error('First-CSS input bundle allocation failed');
    }
    try {
      module.HEAPU8.set(contextBytes, context);
      module.HEAPU8.set(consumedBytes, consumed);
      const result = armFunction(context, contextBytes.length, consumed, consumedBytes.length);
      if (result !== 1) {
        const pointer = module._melee_web_native_menu_message();
        throw Error(pointer ? module.UTF8ToString(pointer) : 'First-CSS source context arm was rejected');
      }
      return {result, phase: module._melee_web_native_menu_phase(),
        running: module._melee_web_native_menu_running()};
    } finally {
      module._free(context);
      module._free(consumed);
    }
  }, {contextBytes: [...firstCssBrowserDrawInputs.bytes.context_bundle],
      consumedBytes: [...firstCssBrowserDrawInputs.bytes.consumed_pad_bundle]}),
  Math.min(10000, remaining()), 'Arm retained first-CSS browser draw inputs');
  assert.equal(arm.result, 1);
  assert.equal(arm.phase, 0);
  assert.equal(arm.running, 0);
  report.scenario.armed_before_css_entry = true;
  report.scenario.first_css_input_bundles = firstCssBrowserDrawInputs.bundle_hashes;

  await timeout(driver.launch(1), Math.min(LIMITS.sourceTransitionMs, remaining()),
    'Original CSS launch with source time held before the retained tick');
  report.scenario.original_css_phase = await sourceSnapshot('Original first-CSS phase before retained tick');
  assert.equal(report.scenario.original_css_phase.phase, 1);
  assert.equal(report.scenario.original_css_phase.running, 1);
  let observation = await timeout(readFirstCssBrowserDrawObservation(),
    Math.min(5000, remaining()), 'Immediate first-CSS browser entry observation');
  assert.equal(observation?.state, 'entered',
    'First-CSS browser entry was not captured before a tick');
  assert.equal(observation.native_phase, 1);
  assert.equal(observation.running, 1);
  assertFirstCssBrowserEntry(observation, firstCssBrowserDrawInputs.contextExpected);
  assert.equal(observation.source_steps, 0);
  assert.equal(observation.source_draws, 0);
  assert.equal(observation.host_tick_calls, 0);
  assert.equal(observation.host_draw_calls, 0);
  report.scenario.first_css_browser_draw = observation;
  await persistReport();

  const kick = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const kickFunction = module?._melee_web_native_menu_stadium_first_css_draw_kick;
    if (typeof kickFunction !== 'function') throw Error('First-CSS one-shot kick export is absent');
    const result = kickFunction();
    if (result !== 1) {
      const pointer = module._melee_web_native_menu_message();
      throw Error(pointer ? module.UTF8ToString(pointer) : 'First-CSS one-shot kick was rejected');
    }
    return result;
  }), Math.min(5000, remaining()), 'Release one retained CSS source tick');
  assert.equal(kick, 1);
  await timeout(page.waitForFunction(() => {
    const module = globalThis.Module;
    const pointer = module?._melee_web_native_menu_stadium_first_css_draw_observe?.();
    if (!pointer) return false;
    const state = JSON.parse(module.UTF8ToString(pointer)).state;
    return state === 'complete' || state === 'failed';
  }, null, {timeout: Math.min(60000, remaining())}),
  Math.min(60000, remaining()), 'One native CSS tick and one browser source draw');
  observation = await timeout(readFirstCssBrowserDrawObservation(),
    Math.min(5000, remaining()), 'Final first-CSS browser draw observation');
  report.scenario.first_css_browser_draw = observation;
  await persistReport();
  assert.equal(observation.state, 'complete',
    `First-CSS browser draw stopped at ${observation.state}: ${observation.error || 'no detail'}`);
  assertFirstCssBrowserEntry(observation, firstCssBrowserDrawInputs.contextExpected);
  assertFirstCssBrowserTick(observation, firstCssBrowserDrawInputs.tickExpected);
  assertFirstCssBrowserDraw(observation, firstCssBrowserDrawInputs.drawExpected);
  assert.ok(Number.isSafeInteger(observation.source_callbacks_before_kick) &&
      observation.source_callbacks_before_kick >= 0,
    'First-CSS pre-kick callback count is not a nonnegative integer');
  assert.equal(observation.source_steps, 1);
  assert.equal(observation.source_draws, 1);
  assert.equal(observation.host_tick_calls, 1);
  assert.equal(observation.host_draw_calls, 1);
  assert.equal(observation.aurora_begin_calls, 1);
  assert.equal(observation.aurora_end_calls, 1);
  assert.equal(observation.frame_end_returned, true);
  assert.equal(observation.preparation_source_callbacks, 0);
  assert.equal(observation.native_phase, 1);
  assert.equal(observation.running, 0,
    'Completed one-shot CSS draw did not stop the native source clock');
  if (firstCssBrowserStream) {
    await saveScreenshot('stadium-first-css-browser-draw');
    observation = await runFirstCssBrowserPostdrawStream(observation);
    if (firstCssBrowserFinalDraw) {
      observation = await runFirstCssBrowserFinalPendingDraw();
      if (firstSssConstructorPair) {
        observation = await runFirstSssConstructorPair();
        if (firstSssConsumedPadTick)
          observation = await runFirstSssConsumedPadTick(observation);
      }
    }
  }
  report.scenario.final_phase = observation.native_phase;
  report.scenario.final_running = observation.running;
  report.scenario.native_observation = observation;
  report.scenario.native_message = report.scenario.original_css_phase.message;
  report.scenario.native_diagnostics = report.scenario.original_css_phase.diagnostics;
  report.scenario.gpu = await timeout(page.evaluate(async () => {
    let adapter = null, failure = null;
    try { adapter = await navigator.gpu?.requestAdapter(); }
    catch (error) { failure = String(error); }
    const info = adapter?.info ? {vendor: adapter.info.vendor || null,
      architecture: adapter.info.architecture || null, device: adapter.info.device || null,
      description: adapter.info.description || null} : null;
    return {cross_origin_isolated: crossOriginIsolated === true, webgpu_api: !!navigator.gpu,
      adapter_available: !!adapter, adapter_info: info, failure,
      canvas: [Module.canvas?.width ?? null, Module.canvas?.height ?? null]};
  }), Math.min(10000, remaining()), 'First-CSS browser WebGPU snapshot');
  assert.equal(report.scenario.gpu.cross_origin_isolated, true);
  assert.equal(report.scenario.gpu.adapter_available, true);
  await saveScreenshot(firstSssConstructorPair ? 'stadium-first-sss-constructor-pair-final' :
    firstCssBrowserFinalDraw ? 'stadium-first-css-browser-final-pending-draw' :
    firstCssBrowserStream ? 'stadium-first-css-browser-postdraw-stream' : 'stadium-first-css-browser-draw');
  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(externalRequests, []);

  report.cleanup.unload_attempted = true;
  unloadAttempted = true;
  await timeout(driver.unload(), LIMITS.unloadMs, 'First-CSS diagnostic native menu unload');
  report.cleanup.unload_completed = true;
  const afterUnload = await timeout(page.evaluate(() => {
    const module = globalThis.Module;
    const pointer = module._melee_web_native_menu_stadium_first_css_draw_observe?.();
    const observation = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
    return {phase: module._melee_web_native_menu_phase(), running: module._melee_web_native_menu_running(),
      observation: pointer ? module.UTF8ToString(pointer) : null,
      first_sss_constructor_pair: observation?.first_sss_constructor_pair ?? null,
      first_sss_consumed_tick: observation?.first_sss_consumed_tick ?? null,
      error: document.querySelector('#status')?.dataset.runtimeError || null,
      import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled};
  }), Math.min(5000, remaining()), 'First-CSS post-unload snapshot');
  assert.equal(afterUnload.phase, 0);
  assert.equal(afterUnload.running, 0);
  assert.equal(afterUnload.observation, null);
  assert.equal(afterUnload.error, null);
  assert.equal(afterUnload.import_enabled, true);
  if (firstSssConstructorPair)
    assert.equal(afterUnload.first_sss_constructor_pair, null,
      'SSS pair diagnostic ownership survived teardown');
  if (firstSssConsumedPadTick)
    assert.equal(afterUnload.first_sss_consumed_tick, null,
      'SSS consumed-tick diagnostic ownership survived teardown');
  report.cleanup.native_after_unload = afterUnload;
  report.cleanup.observation_cleared = true;
  await saveScreenshot('stadium-first-css-browser-draw-after-unload');
  report.profile.after = summarizeStorage(await timeout(context.storageState(),
    Math.min(5000, remaining()), 'First-CSS post-run browser profile snapshot'));
  const after = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(after, artifacts, 'After first-CSS run');
  report.local_http_artifacts.after = after;
  report.local_http_artifacts.unchanged = JSON.stringify(before) === JSON.stringify(after);
  assert.equal(report.local_http_artifacts.unchanged, true);
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(consoleErrors, []);
  report.result = 'pass';
}

async function runCssSssTransition({startKey, capture, routeName}) {
  const samples = capture.samples;
  await timeout(page.locator('#canvas').focus({timeout: Math.min(5000, remaining())}),
    Math.min(5000, remaining()), `${routeName} canvas focus before CSS readiness observation`);
  capture.canvas_focused_before_observation = true;
  for (let frame = 0; frame < capture.max_readiness_frames; frame++) {
    const sample = await observeCssInputFrame(`${routeName} CSS readiness RAF sample ${frame}`);
    samples.push(sample);
    if (sample.observer_error || sample.runtime_error) {
      capture.outcome = {accepted: false, failure_boundary: sample.runtime_error
        ? 'native-runtime-error' : 'css-or-input-observer-error', sample_index: frame,
        detail: sample.runtime_error || sample.observer_error};
      await persistReport();
      throw Error(`${capture.outcome.failure_boundary}: ${capture.outcome.detail}`);
    }
    const progress = cssReadinessEvidence(samples);
    if (hasCssMenuReadiness(sample) && progress.input_progressed && progress.source_callback_progressed) {
      capture.readiness = {...progress, ready_sample: frame,
        source_start_cooldown: sample.css.ids[5], source_pending_scene: sample.css.ids[7],
        source_start_ready: sample.css.ids[8], source_callback_count: sample.css.ids[10],
        input_sample_count: sample.input.samples, p1: sample.input.pads[0]};
      break;
    }
    if (frame % 10 === 9) await persistReport();
  }
  const lastReadySample = samples.at(-1);
  const readiness = cssReadinessEvidence(samples);
  capture.readiness ||= {...readiness, source_start_cooldown: lastReadySample?.css?.ids?.[5] ?? null,
    source_pending_scene: lastReadySample?.css?.ids?.[7] ?? null,
    source_start_ready: lastReadySample?.css?.ids?.[8] ?? null,
    source_callback_count: lastReadySample?.css?.ids?.[10] ?? null,
    input_sample_count: lastReadySample?.input?.samples ?? null,
    p1: lastReadySample?.input?.pads?.[0] ?? null};
  if (!readiness.readiness_observed || !readiness.input_progressed || !readiness.source_callback_progressed ||
      !hasCssMenuReadiness(lastReadySample)) {
    capture.outcome = {accepted: false, failure_boundary: !readiness.readiness_observed
      ? 'css-readiness-not-observed' : !readiness.input_progressed || !readiness.source_callback_progressed
      ? 'css-source-or-input-progress-not-observed' : 'css-readiness-not-current-at-start-gate'};
    await persistReport();
    throw Error(`${routeName} stopped before sending Start: ${capture.outcome.failure_boundary}`);
  }

  const baselineSamples = lastReadySample.input.samples;
  if (lastReadySample.input.pads[0].buttons !== 0) {
    capture.outcome = {accepted: false, failure_boundary: 'p1-not-neutral-before-start',
      buttons: lastReadySample.input.pads[0].buttons};
    await persistReport();
    throw Error(`${routeName} stopped because P1 raw PAD was not neutral before Start`);
  }
  capture.input_before_start = lastReadySample.input;
  capture.start_key = startKey;
  const postStartSampleStart = samples.length;
  await persistReport();
  const transitionStartedAt = Date.now();
  const transitionDeadline = Math.min(deadline, transitionStartedAt + LIMITS.sourceTransitionMs);
  try {
    const chordTimeout = Math.min(transitionDeadline - Date.now(), remaining());
    if (chordTimeout <= 0) throw Error('Source transition deadline expired before the Start chord');
    await timeout(driver.pressChord([startKey], {holdMs: 120, releaseMs: 150}), chordTimeout,
      `${routeName} single ordinary B0XX CSS-to-SSS Start chord`);
  } catch (error) {
    capture.transition = {started_at_epoch_ms: transitionStartedAt,
      elapsed_ms: Date.now() - transitionStartedAt, sample_count: 0,
      sample_cap: capture.max_post_start_frames, sample_cap_hz: capture.post_start_sample_cap_hz,
      deadline_ms: LIMITS.sourceTransitionMs,
      stop: 'start-chord-error', observation_error: errorText(error)};
    capture.outcome = {accepted: false, failure_boundary: 'start-chord-delivery-error',
      detail: errorText(error)};
    await persistReport();
    throw error;
  }
  let finalSample = null;
  let transitionStop = 'sample-cap';
  let transitionError = null;
  for (let frame = 0; frame < capture.max_post_start_frames; frame++) {
    if (Date.now() >= transitionDeadline) { transitionStop = 'source-transition-deadline'; break; }
    try {
      finalSample = await observeCssInputFrame(`${routeName} post-Start RAF sample ${frame}`,
        transitionDeadline);
    } catch (error) {
      transitionStop = Date.now() >= transitionDeadline ? 'source-transition-deadline' : 'observation-error';
      transitionError = errorText(error);
      break;
    }
    samples.push(finalSample);
    if (finalSample.observer_error || finalSample.runtime_error) {
      transitionStop = finalSample.runtime_error ? 'native-runtime-error' : 'css-or-input-observer-error';
      transitionError = finalSample.runtime_error || finalSample.observer_error;
      break;
    }
    if (frame % 60 === 59) await persistReport();
    if (finalSample.phase === 3 && finalSample.running === 1) {
      transitionStop = 'sss-phase-3-running';
      break;
    }
  }
  capture.transition = {started_at_epoch_ms: transitionStartedAt,
    elapsed_ms: Date.now() - transitionStartedAt,
    sample_count: samples.length - postStartSampleStart,
    sample_cap: capture.max_post_start_frames, sample_cap_hz: capture.post_start_sample_cap_hz,
    deadline_ms: LIMITS.sourceTransitionMs,
    stop: transitionStop, observation_error: transitionError};
  capture.input_after_start = finalSample?.input ?? null;
  const lastSourceSample = [...samples].reverse().find(sample => sample.css?.ids?.length === 14);
  capture.last_source_start = lastSourceSample ? {
    trigger: lastSourceSample.css.ids[11], ready_at_trigger: lastSourceSample.css.ids[12],
    pending_at_trigger: lastSourceSample.css.ids[13], callback_count: lastSourceSample.css.ids[10],
    phase: lastSourceSample.phase, running: lastSourceSample.running,
  } : null;
  capture.outcome = cssSssOutcome(samples, finalSample, baselineSamples);
  if (transitionError) {
    capture.outcome.failure_boundary = finalSample?.runtime_error ? 'native-runtime-error' :
      finalSample?.observer_error ? 'css-or-input-observer-error' : 'css-to-sss-observation-error';
    capture.outcome.detail = transitionError;
    capture.outcome.accepted = false;
  }
  report.scenario.final_phase = finalSample?.phase ?? null;
  report.scenario.final_running = finalSample?.running ?? null;
  report.scenario.native_message = finalSample?.message ?? null;
  report.scenario.native_diagnostics = finalSample?.diagnostics ?? null;
  await persistReport();
  return {finalSample, baselineSamples, outcome: capture.outcome};
}

async function runCssSssReducer({startKey, baseUrl, artifacts, before}) {
  const reducer = report.scenario.css_sss_reducer;
  const transition = await runCssSssTransition({startKey, capture: reducer, routeName: 'CSS-to-SSS reducer'});
  await saveScreenshot(transition.finalSample?.phase === 3
    ? 'stadium-c1a-sss-after-start' : 'stadium-c1a-css-after-start');
  if (!transition.outcome.accepted)
    throw Error(`CSS-to-SSS reducer failed at ${transition.outcome.failure_boundary}`);

  report.scenario.page_errors = pageErrors;
  report.scenario.console_errors = consoleErrors;
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  assert.deepEqual(pageErrors, [], 'Browser page raised JavaScript errors during CSS-to-SSS');
  assert.deepEqual(externalRequests, [], 'CSS-to-SSS reducer attempted a non-loopback HTTP request');

  report.cleanup.unload_attempted = true;
  unloadAttempted = true;
  await timeout(driver.unload(), LIMITS.unloadMs, 'Recoverable CSS-to-SSS reducer unload');
  report.cleanup.unload_completed = true;
  const afterUnload = await timeout(page.evaluate(() => ({
    phase: Module._melee_web_native_menu_phase(), running: Module._melee_web_native_menu_running(),
    error: document.querySelector('#status')?.dataset.runtimeError || null,
    import_enabled: !!document.querySelector('#disc') && !document.querySelector('#disc').disabled,
  })), Math.min(5000, remaining()), 'CSS-to-SSS reducer post-unload snapshot');
  assert.equal(afterUnload.phase, 0);
  assert.equal(afterUnload.running, 0);
  assert.equal(afterUnload.error, null);
  assert.equal(afterUnload.import_enabled, true);
  report.cleanup.native_after_unload = afterUnload;
  report.cleanup.observation_cleared = true;
  await saveScreenshot('stadium-c1a-css-sss-after-unload');
  report.profile.after = summarizeStorage(await timeout(context.storageState(),
    Math.min(5000, remaining()), 'Post-reducer browser profile snapshot'));
  const after = await buildArtifactInventory(baseUrl, artifacts);
  assertProducerArtifacts(after, artifacts, 'Reducer after-run');
  report.local_http_artifacts.after = after;
  report.local_http_artifacts.unchanged = JSON.stringify(before) === JSON.stringify(after);
  assert.equal(report.local_http_artifacts.unchanged, true,
    'One or more of the 40 served artifacts changed during the CSS-to-SSS reducer');
  report.scenario.http_requests = [...requestRows, ...responseRows];
  report.scenario.external_http_requests = externalRequests;
  report.result = 'pass';
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
  if (firstSssConsumedPadTick)
    assert.equal(preflight.route_mode, 'first-sss-consumed-pad-tick',
      'SSS consumed-tick command requires its exact frozen preflight route');
  else if (firstSssConstructorPair)
    assert.equal(preflight.route_mode, 'first-sss-constructor-pair',
      'SSS constructor-pair command requires its exact frozen preflight route');
  else if (firstCssBrowserDraw)
    assert.equal(preflight.route_mode, firstCssBrowserStream
      ? (firstCssBrowserFinalDraw ? 'first-css-browser-final-pending-draw' :
        'first-css-browser-postdraw-stream') : 'first-css-browser-draw',
      'First-CSS diagnostic command requires its exact frozen preflight route');
  else if (values['css-sss-reducer'])
    assert.equal(preflight.route_mode, 'css-sss-reducer', 'Reducer command requires a frozen CSS-to-SSS preflight');
  else if (preflight.route_mode)
    assert.equal(preflight.route_mode, 'full-c1a', 'Full C1a command differs from its frozen preflight route');
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
  if (values['css-sss-reducer']) {
    const helperPath = path.join(ROOT, 'tests/stadium_c1a_css_sss_reducer.mjs');
    assert.equal(path.resolve(preflight.reducer_helper?.path || ''), helperPath,
      'Reducer command omitted or changed its acceptance helper path');
    assert.equal(await hashFile(helperPath), preflight.reducer_helper.sha256,
      'Reducer acceptance helper bytes differ from the frozen preflight');
  }
  firstCssBrowserDrawInputs = await loadFirstCssBrowserDrawInputs();
  firstCssBrowserStreamInputs = await loadFirstCssBrowserStreamInputs();
  firstSssConstructorPairInputs = await loadFirstSssConstructorPairInputs();
  firstSssConsumedPadTickInputs = await loadFirstSssConsumedPadTickInputs();
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
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_css_draw_arm'), false,
    'Ordinary default runtime unexpectedly exports the private first-CSS diagnostic gate');

  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_css_postdraw_stream_arm'), false,
    'Ordinary default runtime unexpectedly exports the private CSS stream gate');
  assert.equal(defaultJs.includes('_melee_web_native_menu_stadium_first_sss_tick_arm'), false,
    'Ordinary default runtime unexpectedly exports the private SSS tick diagnostic gate');

  const artifacts = JSON.parse(await fs.readFile(path.join(ROOT, 'tools/browser_build_artifacts.json'), 'utf8'));
  assert.equal(artifacts.length, 40, 'The checked-in browser artifact inventory changed; review the preflight');
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
  const readControls = () => timeout(page.evaluate(() => ({
    layout: document.querySelector('#keyboard-layout')?.value ?? null,
    sources: ['#player-one-source', '#player-two-source'].map(selector =>
      document.querySelector(selector)?.value ?? null),
    key_bindings: [...document.querySelectorAll('#keyboard-bindings tbody tr')].map(row =>
      [...row.cells].map(cell => cell.textContent.trim())),
  })), Math.min(5000, remaining()), 'Runtime Controls snapshot');
  await page.locator('#controls-open').click();
  const controlsBefore = await readControls();
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.getByLabel('Player 1 input source', {exact: true}).selectOption('keyboard');
  await page.getByLabel('Player 2 input source', {exact: true}).selectOption('off');
  const controlsForRecipe = await readControls();
  assert.deepEqual({layout: controlsForRecipe.layout, sources: controlsForRecipe.sources},
    {layout: 'boxx', sources: ['keyboard', 'off']},
    'C1a must use the established single-player B0XX keyboard Controls recipe');
  const startKey = controlsForRecipe.key_bindings.find(([action]) => action === 'Start')?.[1];
  const attackKey = controlsForRecipe.key_bindings.find(([action]) => action === 'Attack')?.[1];
  assert.equal(startKey, '7', 'The live B0XX Controls table must map source PAD Start to 7');
  assert.equal(attackKey, 'M', 'The live B0XX Controls table must map source PAD A to M');
  report.scenario.controls = {initial: controlsBefore, configured: controlsForRecipe};
  report.scenario.input_recipe = {
    layout: 'boxx', player1: 'keyboard', player2: 'off',
    css_to_sss_start_key: startKey, sss_a_confirm_key: attackKey,
  };
  await page.locator('#controls-close').click();
  await driver.selectDisc(values.disc);
  await timeout(driver.waitForStart(), Math.min(LIMITS.discImportMs, remaining()), 'Disc import and native prep');
  if (firstCssBrowserDraw) {
    await runFirstCssBrowserDraw({baseUrl, artifacts, before});
    return;
  }

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
  if (values['css-sss-reducer']) {
    await runCssSssReducer({startKey, baseUrl, artifacts, before});
    return;
  }

  report.scenario.css_sss_transition = createCssSssTransitionCapture(
    'Original CSS -> original SSS, then continue the existing raw-PAD Stadium tile selection and C1a preparation route.');
  const cssSssTransition = await runCssSssTransition({startKey,
    capture: report.scenario.css_sss_transition, routeName: 'Full C1a route'});
  await saveScreenshot(cssSssTransition.finalSample?.phase === 3
    ? 'stadium-c1a-sss-after-start' : 'stadium-c1a-css-after-start');
  assert.equal(cssSssTransition.outcome.accepted, true,
    `Full C1a CSS-to-SSS transition failed at ${cssSssTransition.outcome.failure_boundary}`);
  report.scenario.original_sss_phase = {
    phase: cssSssTransition.finalSample.phase,
    running: cssSssTransition.finalSample.running,
    message: cssSssTransition.finalSample.message,
    diagnostics: cssSssTransition.finalSample.diagnostics,
    error: cssSssTransition.finalSample.runtime_error,
  };
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
  await saveScreenshot('stadium-c1a-sss-stadium-selected');
  await timeout(driver.pressChord([attackKey.toLowerCase()], {holdMs: 120, releaseMs: 150}),
    Math.min(LIMITS.sourceTransitionMs, remaining()), 'Raw-PAD B0XX SSS A confirmation');
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
    'One or more of the 40 served build artifacts changed during the browser attempt');
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
    if (firstCssBrowserFinalDraw) {
      try { report.scenario.first_css_final_draw_failure = await timeout(
        readFirstCssBrowserFinalDrawObservation(), Math.min(2000, cleanupRemaining()),
        'Final CSS draw failure evidence before unload'); }
      catch (error) { cleanupFailures.push({step: 'final-draw-failure-snapshot', error: errorText(error)}); }
    }
    if (firstCssBrowserStream) {
      try { report.scenario.first_css_stream_failure = await timeout(
        readFirstCssBrowserStreamObservation(), Math.min(2000, cleanupRemaining()),
        'CSS stream failure evidence before unload'); }
      catch (error) { cleanupFailures.push({step: 'stream-failure-snapshot', error: errorText(error)}); }
    }
    if (firstSssConstructorPair) {
      try {
        report.scenario.first_sss_pair_failure_snapshot =
          await timeout(readFirstSssConstructorPairObservation(),
            Math.min(2000, cleanupRemaining()),
            'SSS pair actual entry/return and first-mismatch evidence before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-pair-failure-snapshot', error: errorText(error)});
      }
    }
    if (firstSssConsumedPadTick) {
      try {
        report.scenario.first_sss_tick_failure_snapshot =
          await timeout(readFirstSssConsumedTickObservation(),
            Math.min(2000, cleanupRemaining()),
            'SSS tick scheduler sample and first-mismatch evidence before unload');
        await persistReport();
      } catch (error) {
        cleanupFailures.push({step: 'sss-tick-failure-snapshot', error: errorText(error)});
      }
    }
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
  if (firstSssConstructorPair && report.cleanup.unload_completed &&
      !report.cleanup.native_after_unload && page && !page.isClosed()) {
    try {
      const afterUnload = await timeout(readFirstSssPairPostUnloadState(),
        Math.min(3000, cleanupRemaining()), 'SSS-pair failure-path post-unload ownership check');
      report.cleanup.native_after_unload = afterUnload;
      assert.equal(afterUnload.phase, 0);
      assert.equal(afterUnload.running, 0);
      assert.equal(afterUnload.observation, null);
      assert.equal(afterUnload.first_sss_constructor_pair, null);
      if (firstSssConsumedPadTick)
        assert.equal(afterUnload.first_sss_consumed_tick, null);
      assert.equal(afterUnload.error, null);
      assert.equal(afterUnload.import_enabled, true);
      report.cleanup.observation_cleared = true;
      await persistReport();
    } catch (error) {
      cleanupFailures.push({step: 'sss-pair-post-unload-check', error: errorText(error)});
    }
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
