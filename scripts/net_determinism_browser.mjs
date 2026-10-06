#!/usr/bin/env node
/*
 * Headless two-instance determinism run for the networked-session seam
 * (Track A1). Each --instance is a separate installed-Chrome process with its
 * own user-data directory. Every instance receives the same agreed per-tick
 * script (optionally with one flipped input bit or a different seed as a
 * negative control), runs the original CSS -> SSS -> match -> Results -> CSS
 * route through the development player, and exports its 64-byte per-tick
 * checksum records. Compare the streams with tools/net_checksum_compare.py.
 *
 * Scope: functional determinism only. Pacing is deliberately uncontrolled (and
 * can be perturbed by CPU throttling); no timing, performance, pixel, PCM or
 * retail-equivalence claim. Instrumented timing pauses are auto-resumed and
 * every resume is reported.
 */
import fs from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {loadBrowserTools} from './browser_tools.mjs';
import {classifyRoute, expectedFullSceneOrder, validateFullRoute} from './net_determinism_contract.mjs';
import {NET_FRAME_BYTES, NET_RECORD_BYTES, openNetInstance} from './net_session_instance.mjs';

const SCRIPT_HEADER_BYTES = 16;
const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, script: {type: 'string'}, out: {type: 'string'},
  playwright: {type: 'string'}, seed: {type: 'string'},
  instance: {type: 'string', multiple: true},
  lookahead: {type: 'string', default: '240'},
  'timeout-ms': {type: 'string', default: '3600000'},
  'stall-ms': {type: 'string', default: '120000'},
  'poll-ms': {type: 'string', default: '100'},
  'stop-after-ticks': {type: 'string'},
  'skip-unload': {type: 'boolean', default: false},
}});

const integer = (name, minimum, maximum) => {
  const value = Number(values[name]);
  if (!Number.isInteger(value) || value < minimum || value > maximum)
    throw Error(`--${name} must be an integer between ${minimum} and ${maximum}`);
  return value;
};
const uint32 = (text, name) => {
  if (typeof text !== 'string' || !/^(?:0[xX][0-9a-fA-F]+|[0-9]+)$/.test(text))
    throw Error(`${name} must be an unsigned 32-bit integer`);
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)
    throw Error(`${name} must be an unsigned 32-bit integer`);
  return value;
};
if (!values.url || !values.disc || !values.script || !values.out || !values.seed || !values.instance?.length)
  throw Error('Use --url runtime.html --disc PATH --script NAME.mwni --seed U32 --out NEW_DIR --instance "label=a,profile=DIR[,throttle=4][,flip=TICK:BYTE:BIT][,seed=U32][,arena-fill=0..255]" (repeatable)');
const url = new URL(values.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html'))
  throw Error('A real HTTP development runtime.html URL is required');
const lookahead = integer('lookahead', 1, 20000);
const timeoutMs = integer('timeout-ms', 1000, 21600000);
const stallMs = integer('stall-ms', 1000, 3600000);
const pollMs = integer('poll-ms', 10, 2000);
const seed = uint32(values.seed, '--seed');
const stopAfter = values['stop-after-ticks'] ? integer('stop-after-ticks', 1, 216000) : null;

function parseInstance(text) {
  const spec = {throttle: 1, seed, flip: null, arenaFill: -1};
  for (const part of text.split(',')) {
    const at = part.indexOf('=');
    if (at < 1) throw Error(`Invalid --instance field: ${part}`);
    const key = part.slice(0, at), value = part.slice(at + 1);
    if (key === 'label') spec.label = value;
    else if (key === 'profile') spec.profile = path.resolve(value);
    else if (key === 'throttle') spec.throttle = Number(value);
    else if (key === 'seed') spec.seed = uint32(value, '--instance seed');
    else if (key === 'arena-fill') spec.arenaFill = Number(value);
    else if (key === 'flip') {
      const fields = value.split(':');
      const [tick, byte, bit] = fields.map(Number);
      if (fields.length !== 3 || ![tick, byte, bit].every(Number.isInteger) || tick < 0 || byte < 0 || byte >= NET_FRAME_BYTES || bit < 0 || bit > 7)
        throw Error('flip must be TICK:BYTE:BIT with BYTE below 44 and BIT below 8');
      spec.flip = {tick, byte, bit};
    } else throw Error(`Unknown --instance field: ${key}`);
  }
  if (!/^[a-z0-9-]+$/.test(spec.label || '') || !spec.profile) throw Error('--instance needs label=[a-z0-9-]+ and profile=DIR');
  if (!Number.isInteger(spec.throttle) || spec.throttle < 1 || spec.throttle > 20) throw Error('throttle must be an integer 1..20');
  if (!Number.isInteger(spec.arenaFill) || spec.arenaFill < -1 || spec.arenaFill > 255) throw Error('arena-fill must be 0..255');
  return spec;
}
const specs = values.instance.map(parseInstance);
if (new Set(specs.map(spec => spec.label)).size !== specs.length) throw Error('Instance labels must be distinct');
if (new Set(specs.map(spec => spec.profile)).size !== specs.length) throw Error('Each instance needs its own user-data directory');

const scriptBytes = await fs.readFile(values.script);
if (scriptBytes.length < SCRIPT_HEADER_BYTES || scriptBytes.subarray(0, 4).toString() !== 'MWNI' || scriptBytes.readUInt32BE(4) !== 1)
  throw Error('Script is not an MWNI v1 file');
const frameCount = scriptBytes.readUInt32BE(8);
if (scriptBytes.length !== SCRIPT_HEADER_BYTES + frameCount * NET_FRAME_BYTES) throw Error('Script length disagrees with its frame count');
const baseFrames = scriptBytes.subarray(SCRIPT_HEADER_BYTES);
const total = stopAfter ? Math.min(stopAfter, frameCount) : frameCount;
for (const spec of specs)
  if (spec.flip && spec.flip.tick >= total)
    throw Error(`flip tick ${spec.flip.tick} is outside the ${total}-tick workload`);
const routeScope = classifyRoute(total, frameCount);
const fullRoute = routeScope === 'full';
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

const output = path.resolve(values.out);
await fs.mkdir(output, {recursive: false});
const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(values.playwright);

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function runInstance(spec) {
  const directory = path.join(output, spec.label);
  await fs.mkdir(directory);
  const frames = Buffer.from(baseFrames);
  if (spec.flip) frames[spec.flip.tick * NET_FRAME_BYTES + spec.flip.byte] ^= 1 << spec.flip.bit;
  const result = {
    label: spec.label, profile_kind: null, throttle: spec.throttle, seed: spec.seed,
    flip: spec.flip, arena_fill: spec.arenaFill, frames_pushed_sha256: sha256(frames.subarray(0, total * NET_FRAME_BYTES)),
    total_ticks: total, outcome: 'fail', first_error: null, records: 0, scene_runs: [], timing_resumes: [],
    started_at: new Date().toISOString(),
  };
  const checksums = await fs.open(path.join(directory, 'checksums.bin'), 'wx');
  let instance, lastScene = null, lastProgress = Date.now(), lastCursor = -1;
  const deadline = Date.now() + timeoutMs;
  try {
    result.profile_kind = (await fs.readdir(spec.profile).catch(() => [])).length ? 'warm-existing' : 'cold-empty';
    const openTimeoutMs = Math.min(180000, deadline - Date.now());
    if (openTimeoutMs <= 0) throw Error(`Instance exceeded ${timeoutMs} ms during browser startup`);
    instance = await openNetInstance({chromium, launchOptions, url: values.url, disc: values.disc,
      userDataDir: spec.profile, label: spec.label, throttle: spec.throttle, arenaFill: spec.arenaFill,
      timeoutMs: openTimeoutMs, deadline});
    result.browser_version = instance.browserVersion;
    result.user_agent = instance.userAgent;
    result.browser_executable = path.basename(browserPath);
    await instance.importDisc();
    await instance.begin(spec.seed, total);
    let pushed = 0;
    for (;;) {
      if (Date.now() > deadline) throw Error(`Instance exceeded ${timeoutMs} ms`);
      const status = await instance.status();
      if (!status.active) throw Error('The networked session is no longer active');
      // Keep a bounded lookahead of agreed frames in front of the native cursor.
      while (pushed < total && pushed - status.cursor < lookahead) {
        const count = Math.min(lookahead, total - pushed);
        await instance.push(frames.subarray(pushed * NET_FRAME_BYTES, (pushed + count) * NET_FRAME_BYTES));
        pushed += count;
      }
      for (;;) {
        const drained = await instance.drain(1024);
        for (let i = 0; i < drained.count; ++i) {
          const scene = drained.bytes.readUInt32LE(i * NET_RECORD_BYTES + 4);
          const tick = drained.bytes.readUInt32LE(i * NET_RECORD_BYTES);
          if (scene !== lastScene) { result.scene_runs.push({scene, first_tick: tick}); lastScene = scene; }
        }
        if (drained.count) { await checksums.write(drained.bytes); result.records += drained.count; }
        if (drained.count < 1024) break;
      }
      if (status.cursor !== lastCursor) { lastCursor = status.cursor; lastProgress = Date.now(); }
      const native = await instance.native();
      if (native.error) throw Error(`Runtime error: ${native.error}`);
      if (instance.errors.length) throw Error(`Page error: ${JSON.stringify(instance.errors[0])}`);
      if (await instance.maybeResume(status.cursor)) lastProgress = Date.now();
      if (status.cursor >= total && result.records >= total) {
        result.final_status = status;
        result.final_native = native;
        break;
      }
      if (Date.now() - lastProgress > stallMs)
        throw Error(`No source progress for ${stallMs} ms at cursor ${status.cursor}: ${JSON.stringify(native)}`);
      await sleep(pollMs);
    }
    if (fullRoute) {
      const observed = await instance.observe();
      const scenes = result.scene_runs.map(row => row.scene);
      result.route = validateFullRoute(scenes, observed.match);
      result.match_observation = observed.match;
    } else {
      result.route = {scope: 'prefix-only', status: 'not-checked',
        observed_scenes: result.scene_runs.map(row => row.scene)};
    }
    result.graphics = await instance.graphics();
    result.timing_pause_diagnostics = await instance.timingPauseDiagnostics();
    result.screenshot = path.join(spec.label, 'final.png');
    await instance.screenshot(path.join(directory, 'final.png'));
    result.wait_episodes = result.final_status.wait_episodes;
    if (!values['skip-unload']) {
      // Unload also persists the optional render cache into the profile, which is
      // how a profile becomes "warm" for a later instance.
      try { await instance.unload(); result.unloaded = true; }
      catch (error) { result.unloaded = false; result.unload_error = String(error.message || error); }
    }
    result.outcome = 'complete';
  } catch (error) {
    result.first_error = String(error.stack || error.message || error);
    if (Array.isArray(error.browserErrors)) result.page_errors = error.browserErrors;
    if (error.startupDiagnostics) result.failure_startup = error.startupDiagnostics;
    if (typeof error.browserClosed === 'boolean') result.browser_closed = error.browserClosed;
    try { if (instance) { result.failure_status = await instance.status(); result.failure_native = await instance.native(); } } catch {}
    try { if (instance) result.failure_timing_pause = await instance.timingPauseDiagnostics(); } catch {}
    try {
      if (instance) {
        result.failure_graphics = await instance.graphics();
        result.failure_screenshot = path.join(spec.label, 'failure.png');
        await instance.screenshot(path.join(directory, 'failure.png'));
      }
    } catch (captureError) { result.failure_capture_error = String(captureError.message || captureError); }
  } finally {
    result.finished_at = new Date().toISOString();
    result.page_errors = instance?.errors ?? result.page_errors ?? [];
    if (instance) result.timing_resumes = instance.timingResumes;
    await checksums.close();
    if (instance) result.start_record = (result.final_status ?? result.failure_status)?.start ?? null;
    if (instance) result.arena = (result.final_status ?? result.failure_status)?.arena ?? null;
    await instance?.close();
    if (instance) result.browser_closed = instance.closed;
    else if (result.browser_closed === undefined) result.browser_closed = null;
    await fs.writeFile(path.join(directory, 'instance.json'), JSON.stringify(result, null, 2) + '\n');
  }
  return result;
}

const results = await Promise.all(specs.map(runInstance));
const report = {
  schema: 'melee-web-net-determinism-run-v1',
  scope: 'Functional two-instance determinism workload; no timing, performance, pixels, PCM, transport or retail-equivalence claim.',
  url: values.url, browser: path.basename(browserPath), playwright: playwrightPath,
  script: {path: path.basename(values.script), sha256: sha256(scriptBytes), frames: frameCount, ticks_pushed: total},
  route: {scope: routeScope,
    expected_full_scene_order: fullRoute ? expectedFullSceneOrder() : null,
    full_route_selection_checked: fullRoute},
  seed, lookahead, poll_ms: pollMs, timing_pauses_auto_resumed: true,
  instances: results.map(row => ({label: row.label, outcome: row.outcome, records: row.records, throttle: row.throttle,
    profile_kind: row.profile_kind, route: row.route ?? {scope: fullRoute ? 'full' : 'prefix-only', status: 'failed'},
    timing_resumes: row.timing_resumes.length, first_error: row.first_error})),
};
await fs.writeFile(path.join(output, 'run.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report.instances));
if (results.some(row => row.outcome !== 'complete')) process.exitCode = 1;
