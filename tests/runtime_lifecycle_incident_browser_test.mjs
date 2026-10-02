#!/usr/bin/env node
/**
 * Bounded lifecycle detector.
 * It uses an audited audio-player package over real HTTP and retains only
 * structured scalar evidence. Unexpected incidents fail the frozen case and
 * are never automatically resumed. Real lifecycle claims are gated by a
 * separate installed-Chrome capability probe. Synthetic mode is an explicit
 * JS/native owner-handoff experiment and is never evidence of a browser cause.
 */
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const IDENTITY_META_ID = 'runtime-diagnostic-identity';
const STARTUP_TIMEOUT_MS = 120000;
const SCENE_TIMEOUT_MS = 60000;
const HIDDEN_DWELL_MS = 350;
const CAPABILITY_TIMEOUT_MS = 5000;
const LIFECYCLE_EVENTS = ['visibilitychange', 'pagehide', 'pageshow', 'freeze', 'resume'];
const UNEXPECTED_REASONS = new Set([1, 2, 3, 4, 8]);
const SAFE_REPORT_KEYS = new Set([
  'schema', 'version', 'session_id', 'identity', 'environment', 'client', 'active',
  'capabilities', 'native', 'audio', 'lifecycle', 'limits', 'flags', 'incidents',
]);

class HarnessFailure extends Error {
  constructor(code) { super(code); this.code = code; }
}
function requireValue(value, code) { if (!value) throw new HarnessFailure(code); }
function validateDiagnosticReport(report) {
  requireValue(report && typeof report === 'object' && !Array.isArray(report), 'diagnostic_report_missing');
  requireValue(report.schema === 'melee-web-runtime-diagnostics' && report.version === 1,
    'diagnostic_report_identity');
  requireValue(Object.keys(report).every(key => SAFE_REPORT_KEYS.has(key)), 'diagnostic_report_unknown_field');
  requireValue(Array.isArray(report.incidents), 'diagnostic_report_incidents');
  const encoded = JSON.stringify(report);
  requireValue(Buffer.byteLength(encoded) <= Number(report.limits?.max_report_bytes || 0),
    'diagnostic_report_bounds');
  requireValue(!/(?:exception|stack|private\.invalid|secret\.gci|file:|data:)/i.test(encoded),
    'diagnostic_report_private_text');
  return report;
}
function parseIdentity(value, label) {
  requireValue(value && typeof value === 'object' && !Array.isArray(value), label + '_object');
  requireValue(JSON.stringify(Object.keys(value).sort()) === JSON.stringify([
    'build_profile', 'runtime_hash', 'schema_version', 'source_commit',
  ]), label + '_fields');
  requireValue(value.schema_version === 1, label + '_schema');
  requireValue(typeof value.source_commit === 'string' && /^[0-9a-f]{40}$/.test(value.source_commit), label + '_commit');
  requireValue(typeof value.runtime_hash === 'string' && /^[0-9a-f]{16}$/.test(value.runtime_hash), label + '_runtime_hash');
  requireValue(['audio-player', 'audio-preview', 'player'].includes(value.build_profile), label + '_profile');
  return {schema_version: value.schema_version, source_commit: value.source_commit,
    runtime_hash: value.runtime_hash, build_profile: value.build_profile};
}
function decodeHtml(value) {
  return value.replace(/&quot;/g, '"').replace(/&#x27;/g, "'")
    .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
}
function parseDiagnosticMeta(html) {
  const matches = [];
  for (const tag of html.match(/<meta\b[^>]*>/gi) || []) {
    const id = tag.match(/\bid\s*=\s*(["'])(.*?)\1/i)?.[2];
    if (id !== IDENTITY_META_ID) continue;
    const content = tag.match(/\bcontent\s*=\s*(["'])(.*?)\1/i)?.[2];
    requireValue(content !== undefined, 'identity_meta_content');
    matches.push(content);
  }
  requireValue(matches.length === 1, 'identity_meta_count');
  try { return parseIdentity(JSON.parse(decodeHtml(matches[0])), 'packaged_identity'); }
  catch (error) { if (error instanceof HarnessFailure) throw error; throw new HarnessFailure('identity_meta_json'); }
}
function digest(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
async function listFiles(root, prefix = '') {
  const result = [];
  for (const entry of await fs.readdir(root, {withFileTypes: true})) {
    const relative = prefix ? prefix + '/' + entry.name : entry.name;
    const local = path.join(root, entry.name);
    if (entry.isSymbolicLink()) throw new HarnessFailure('package_symlink');
    if (entry.isDirectory()) result.push(...await listFiles(local, relative));
    else if (entry.isFile()) result.push(relative);
    else throw new HarnessFailure('package_nonfile');
  }
  return result.sort();
}
async function verifyPackage(site, manifest) {
  requireValue(manifest && typeof manifest === 'object', 'manifest_object');
  const identity = parseIdentity({schema_version: 1, source_commit: manifest.source_sha,
    runtime_hash: manifest.runtime_hash, build_profile: manifest.profile}, 'manifest_identity');
  requireValue(manifest.profile === 'audio-player' && Array.isArray(manifest.files), 'manifest_profile_or_files');
  const records = new Map();
  for (const record of manifest.files) {
    requireValue(record && typeof record === 'object', 'manifest_record');
    requireValue(typeof record.path === 'string' && !record.path.startsWith('/') &&
      !record.path.includes('\\') && path.posix.normalize(record.path) === record.path &&
      !record.path.split('/').includes('..'), 'manifest_path');
    requireValue(!records.has(record.path), 'manifest_duplicate');
    requireValue(Number.isInteger(record.size) && record.size >= 0 &&
      typeof record.sha256 === 'string' && /^[0-9a-f]{64}$/.test(record.sha256), 'manifest_digest');
    records.set(record.path, record);
  }
  const actual = await listFiles(site);
  requireValue(actual.length === records.size, 'package_inventory_count');
  for (const relative of actual) {
    const record = records.get(relative);
    requireValue(record, 'package_inventory_extra');
    const bytes = await fs.readFile(path.join(site, relative));
    requireValue(bytes.byteLength === record.size && digest(bytes) === record.sha256, 'package_bytes');
  }
  const prefix = 'runtime/' + identity.runtime_hash + '/';
  const runtimeModule = prefix + 'audio-preview-runtime.mjs';
  requireValue(records.has(runtimeModule) && records.has(prefix + 'runtime-diagnostics.mjs'), 'package_runtime_modules');
  requireValue(JSON.stringify(parseDiagnosticMeta(await fs.readFile(path.join(site, 'index.html'), 'utf8'))) ===
    JSON.stringify(identity), 'package_html_identity');
  return {identity, runtimeModule};
}

function fixtureMarkup(runtimeModule, identity) {
const moduleUrl = '/' + runtimeModule;
const identityJson = JSON.stringify(identity);
  const source = String.raw`
const fixture = globalThis.__runtimeLifecycleFixture = {
  ready: false, load: {state: 'booting'}, errors: 0, states: [], samples: [],
  incidents: [], browser_events: [], input_activity: [], audio: [], owner: null, player: null,
  unload_calls: 0, cache_save_calls: 0, manual_intent: [], synthetic_events: [],
  synthetic_missed_callbacks: false, frame_callbacks: 0, dropped_frame_callbacks: 0,
  synthetic_hidden: false, synthetic_visibility_override: false, synthetic_callback_hold: false,
  synthetic_native_callback_identified: false, synthetic_held_callbacks: 0,
  synthetic_released_callbacks: 0, synthetic_hold_result: null,
};
// The compiled Emscripten main loop schedules its runner through the browser's
// requestAnimationFrame. Install this bounded shim before importing the runtime
// so the experiment can identify that callback by an actual native sample.
const originalRequestAnimationFrame = globalThis.requestAnimationFrame?.bind(globalThis);
const originalCancelAnimationFrame = globalThis.cancelAnimationFrame?.bind(globalThis);
const rafEntries = new Map();
const heldRafEntries = [];
const nativeRafCallbacks = new WeakSet();
let nextRafId = 1;
function invokeRafEntry(entry, timestamp) {
  if (entry.cancelled) return;
  if (fixture.synthetic_callback_hold && nativeRafCallbacks.has(entry.callback)) {
    rafEntries.delete(entry.id);
    fixture.synthetic_held_callbacks++;
    heldRafEntries.push(entry);
    return;
  }
  rafEntries.delete(entry.id);
  const beforeSamples = fixture.samples.length;
  fixture.synthetic_raf_invocations = Number(fixture.synthetic_raf_invocations || 0) + 1;
  entry.callback.call(globalThis, timestamp);
  if (fixture.samples.length > beforeSamples) {
    nativeRafCallbacks.add(entry.callback);
    fixture.synthetic_native_callback_identified = true;
  }
}
function scheduleRaf(callback) {
  if (typeof originalRequestAnimationFrame !== 'function' || typeof callback !== 'function') return 0;
  const entry = {id: nextRafId++, callback, cancelled: false, nativeHandle: 0};
  rafEntries.set(entry.id, entry);
  entry.nativeHandle = originalRequestAnimationFrame(timestamp => invokeRafEntry(entry, timestamp));
  return entry.id;
}
if (typeof originalRequestAnimationFrame === 'function') {
  globalThis.requestAnimationFrame = scheduleRaf;
  globalThis.cancelAnimationFrame = id => {
    const entry = rafEntries.get(id);
    if (entry) { entry.cancelled = true; rafEntries.delete(id); }
    else if (typeof originalCancelAnimationFrame === 'function') originalCancelAnimationFrame(id);
  };
}
for (const type of __LIFECYCLE_EVENTS__) {
  const target = type === 'visibilitychange' ? document : window;
  target.addEventListener(type, () => fixture.browser_events.push({
    type, at: performance.now(), hidden: document.hidden, visibility: document.visibilityState,
  }), true);
}

const number = value => Number.isFinite(Number(value)) ? Number(value) : null;
function recordSample(args, callbackMs) {
  if (fixture.samples.length >= 128) return;
  fixture.samples.push({timestamp: number(args[0]), source_frame: number(args[1]), scene: number(args[2]),
    debt_ticks: number(args[8]), total_ms: number(args[11]), source_steps: number(args[16]),
    source_draws: number(args[17]), running: args[18] === true || args[18] === 1,
    callback_ms: callbackMs, at: performance.now()});
}
function recordIncident(args) {
  if (fixture.incidents.length >= 32) return;
  fixture.incidents.push({reason_code: number(args[0]), value: number(args[1]), threshold: number(args[2]),
    source_frame: number(args[3]), scene: number(args[4]), clock_owner: number(args[5])});
}
function safeAudio(data) {
  if (!data || typeof data !== 'object') return;
  const states = ['running', 'suspended', 'closed', 'interrupted', 'unknown'];
  const type = ['context-state', 'state-ack', null].includes(data.type) ? data.type : 'other';
  fixture.audio.push({type, queued: number(data.queued),
    underruns: number(data.underruns), overflows: number(data.overflows),
    context_state: states.includes(data.context_state) ? data.context_state : 'unknown',
    clock_seconds: number(data.audio_clock_seconds), enabled: data.enabled === true});
  if (fixture.audio.length > 128) fixture.audio.shift();
}
function inputSnapshot() {
  const module = fixture.owner?.Module;
  try {
    const pointer = module?._melee_web_input_message?.();
    const input = pointer && module.UTF8ToString ? JSON.parse(module.UTF8ToString(pointer)) : null;
    if (!input) return null;
    return {ready: !!input.ready, focused: !!input.focused, visible: !!input.visible,
      active: !!input.active, samples: number(input.samples), keyboard_active: !!input.keyboard_active};
  } catch { return null; }
}
function nativeState() {
  const module = fixture.owner?.Module;
  return {phase: Number(module?._melee_web_native_menu_phase?.() ?? -1),
    running: !!module?._melee_web_native_menu_running?.(), input: inputSnapshot()};
}
function exposeOwner(owner) {
  fixture.owner = owner;
  const sample = globalThis.menuDiagnosticSample;
  globalThis.menuDiagnosticSample = (...args) => {
    const started = performance.now(); let result;
    try { result = sample?.(...args); } finally { recordSample(args, Math.max(0, performance.now() - started)); }
    return result;
  };
  owner.callbacks.menuDiagnosticSample = globalThis.menuDiagnosticSample;
  const incident = globalThis.menuDiagnosticIncident;
  globalThis.menuDiagnosticIncident = (...args) => { recordIncident(args); return incident?.(...args); };
  owner.callbacks.menuDiagnosticIncident = globalThis.menuDiagnosticIncident;
  const frame = globalThis.menuFrame;
  if (typeof frame === 'function') {
    globalThis.menuFrame = (...args) => {
      fixture.frame_callbacks++;
      if (fixture.synthetic_missed_callbacks) { fixture.dropped_frame_callbacks++; return; }
      return frame(...args);
    };
    owner.callbacks.menuFrame = globalThis.menuFrame;
  }
  const module = owner.Module;
  const activity = module?._melee_web_input_set_activity;
  if (typeof activity === 'function') module._melee_web_input_set_activity = (focused, visible) => {
    fixture.input_activity.push({focused: Number(focused) ? 1 : 0, visible: Number(visible) ? 1 : 0, at: performance.now()});
    return activity.call(module, focused, visible);
  };
  const unload = module?._melee_web_native_menu_unload;
  if (typeof unload === 'function') module._melee_web_native_menu_unload = (...args) => {
    fixture.unload_calls++; return unload.apply(module, args);
  };
  if (typeof module?.saveRuntimeCache === 'function') {
    const save = module.saveRuntimeCache;
    module.saveRuntimeCache = (...args) => { fixture.cache_save_calls++; return save.apply(module, args); };
  }
}
function exposeState(next) {
  if (fixture.states.length < 128) fixture.states.push({at: performance.now(), scene: next.scene,
    phase: next.phase, running: !!next.running, requiresReload: !!next.requiresReload});
}
function setSyntheticActivity(focused, visible) {
  const module = fixture.owner?.Module;
  const setter = module?._melee_web_input_set_activity;
  if (typeof setter !== 'function') return false;
  setter(focused, visible);
  return true;
}
function syntheticVisibilityOverride() {
  if (fixture.synthetic_visibility_override) return true;
  try {
    Object.defineProperty(document, 'hidden', {configurable: true, get: () => fixture.synthetic_hidden});
    Object.defineProperty(document, 'visibilityState', {configurable: true,
      get: () => fixture.synthetic_hidden ? 'hidden' : 'visible'});
    fixture.synthetic_visibility_override = true;
    return true;
  } catch { return false; }
}
function dispatchSyntheticVisibility(kind) {
  fixture.synthetic_events.push({kind, at: performance.now(), hidden: fixture.synthetic_hidden,
    visibility: document.visibilityState});
  try { document.dispatchEvent(new Event('visibilitychange')); } catch { return false; }
  return true;
}
function releaseHeldNativeRaf() {
  if (!fixture.synthetic_native_callback_identified) return false;
  fixture.synthetic_callback_hold = false;
  const held = heldRafEntries.splice(0, heldRafEntries.length);
  for (const entry of held) {
    if (entry.cancelled) continue;
    fixture.synthetic_released_callbacks++;
    scheduleRaf(entry.callback);
  }
  return held.length > 0;
}
fixture.syntheticSuspend = () => {
  fixture.manual_intent.push('synthetic_suspend');
  fixture.synthetic_events.push({kind: 'synthetic_hidden_checkpoint', at: performance.now()});
  fixture.synthetic_missed_callbacks = true;
  return setSyntheticActivity(1, 0);
};
fixture.syntheticResume = () => {
  fixture.manual_intent.push('synthetic_resume');
  fixture.synthetic_events.push({kind: 'synthetic_visible_checkpoint', at: performance.now()});
  fixture.synthetic_missed_callbacks = false;
  const changed = setSyntheticActivity(1, 1);
  try { fixture.owner?.callbacks?.menuServiceCommands?.(); } catch {}
  return changed;
};
fixture.syntheticHiddenHold = async () => {
  if (typeof originalRequestAnimationFrame !== 'function')
    return {available: false, unrun_reason: 'synthetic_request_animation_frame_unavailable'};
  if (!fixture.synthetic_native_callback_identified)
    return {available: false, unrun_reason: 'synthetic_main_loop_callback_unavailable'};
  if (!syntheticVisibilityOverride())
    return {available: false, unrun_reason: 'synthetic_visibility_override_unavailable'};
  const beforeSamples = fixture.samples.length;
  const beforeSourceSteps = fixture.samples.reduce((sum, sample) => sum + (sample.source_steps || 0), 0);
  const startedAt = performance.now();
  fixture.synthetic_hidden = true;
  fixture.synthetic_callback_hold = true;
  if (!dispatchSyntheticVisibility('synthetic_hidden_event')) {
    fixture.synthetic_hidden = false;
    fixture.synthetic_callback_hold = false;
    return {available: false, unrun_reason: 'synthetic_visibility_event_unavailable'};
  }
  await new Promise(resolve => setTimeout(resolve, __HIDDEN_DWELL_MS__));
  const duringSamples = fixture.samples.length;
  const duringSourceSteps = fixture.samples.reduce((sum, sample) => sum + (sample.source_steps || 0), 0);
  const hiddenAt = performance.now();
  fixture.synthetic_hidden = false;
  const visibleEvent = dispatchSyntheticVisibility('synthetic_visible_event');
  const released = releaseHeldNativeRaf();
  const visibleAt = performance.now();
  const result = {available: visibleEvent && released, started_at: startedAt, hidden_at: hiddenAt,
    visible_at: visibleAt, before_samples: beforeSamples, during_samples: duringSamples,
    after_samples: fixture.samples.length, before_source_steps: beforeSourceSteps,
    during_source_steps: duringSourceSteps, after_source_steps: fixture.samples.reduce((sum, sample) => sum + (sample.source_steps || 0), 0),
    held_callbacks: fixture.synthetic_held_callbacks, released_callbacks: fixture.synthetic_released_callbacks,
    hidden_sample_delta: duringSamples - beforeSamples, hidden_source_step_delta: duringSourceSteps - beforeSourceSteps};
  fixture.synthetic_hold_result = result;
  return result;
};
fixture.snapshot = () => ({
  load: fixture.load, state: (() => { const state = fixture.player?.getState?.(); return state ? {
    ready: !!state.ready, scene: state.scene, phase: Number.isInteger(state.phase) ? state.phase : null,
    running: !!state.running, requiresReload: !!state.requiresReload, canPause: !!state.canPause,
    canUnload: !!state.canUnload, canStart: !!state.canStart, bundle: !!state.bundle,
  } : null; })(), browser_events: fixture.browser_events,
  input_activity: fixture.input_activity, incidents: fixture.incidents, samples: fixture.samples,
  audio: fixture.audio, native: nativeState(), unload_calls: fixture.unload_calls,
  cache_save_calls: fixture.cache_save_calls, audio_owner: globalThis.__runtimeLifecycleAudio?.snapshot?.() || null,
  manual_intent: fixture.manual_intent, synthetic_events: fixture.synthetic_events,
  synthetic_missed_callbacks: fixture.synthetic_missed_callbacks,
  frame_callbacks: fixture.frame_callbacks, dropped_frame_callbacks: fixture.dropped_frame_callbacks,
  synthetic_hidden: fixture.synthetic_hidden, synthetic_visibility_override: fixture.synthetic_visibility_override,
  synthetic_callback_hold: fixture.synthetic_callback_hold,
  synthetic_native_callback_identified: fixture.synthetic_native_callback_identified,
  synthetic_held_callbacks: fixture.synthetic_held_callbacks, synthetic_released_callbacks: fixture.synthetic_released_callbacks,
  synthetic_hold_result: fixture.synthetic_hold_result,
  report: fixture.owner?.diagnostics?.exportReports?.() || null,
});
let mountMeleeRuntime;
try {
  ({mountMeleeRuntime} = await import("__RUNTIME_MODULE__"));
  fixture.player = await mountMeleeRuntime({canvas: document.getElementById('canvas'),
    diagnosticIdentity: __IDENTITY__, recordDiagnostics: true, onOwner: exposeOwner, onState: exposeState,
    onEvent: (name, data) => { if (name === 'audio') safeAudio(data); }, onError: () => { fixture.errors++; }});
  fixture.ready = true; fixture.load = {state: 'ready'};
} catch { fixture.load = {state: 'error'}; fixture.errors++; }
const input = document.getElementById('disc-file');
document.getElementById('choose-disc').onclick = () => input.click();
input.onchange = async () => {
  const file = input.files?.[0]; if (!file || !fixture.player) return;
  fixture.load = {state: 'configuring'};
  try {
    await fixture.player.configureSaveProfile('everything');
    fixture.load = {state: 'importing'}; await fixture.player.importDisc(file);
    fixture.load = {state: 'starting'}; await fixture.player.start(); fixture.load = {state: 'started'};
  } catch { fixture.load = {state: 'error'}; fixture.errors++; }
};
`;
  const script = source.replace('__RUNTIME_MODULE__', moduleUrl)
    .replace('__IDENTITY__', identityJson).replace('__IDENTITY__', identityJson)
    .replace('__HIDDEN_DWELL_MS__', String(HIDDEN_DWELL_MS))
    .replace('__LIFECYCLE_EVENTS__', JSON.stringify(LIFECYCLE_EVENTS));
  return '<!doctype html><html><head><meta charset="utf-8"><title>Runtime lifecycle fixture</title></head>' +
    '<body><button id="choose-disc" type="button">Choose local disc</button>' +
    '<input id="disc-file" type="file" accept=".iso,.gcm,.ciso" hidden>' +
    '<canvas id="canvas" width="640" height="480" tabindex="0"></canvas>' +
    '<script type="module">' + script + '</script></body></html>';
}

function capabilityMarkup() {
  return '<!doctype html><meta charset="utf-8"><title>Lifecycle capability probe</title>' +
    '<script>' + String.raw`
const events = [];
for (const type of ${JSON.stringify(LIFECYCLE_EVENTS)}) {
  const target = type === 'visibilitychange' ? document : window;
  target.addEventListener(type, () => events.push({type, at: performance.now(),
    hidden: document.hidden, visibility: document.visibilityState}), true);
}
globalThis.__runtimeLifecycleCapability = {
  snapshot: () => ({events: events.slice(-32), hidden: document.hidden,
    visibility: document.visibilityState}),
};
` + '</script>';
}

const MIME_TYPES = {
  '.css': 'text/css; charset=utf-8', '.data': 'application/octet-stream', '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8', '.wasm': 'application/wasm',
};
async function createServer(site, fixtureHtml, capabilityHtml) {
  const requests = [];
  const server = http.createServer(async (request, response) => {
    const parsed = new URL(request.url || '/', 'http://127.0.0.1');
    requests.push({method: request.method || 'GET', path: parsed.pathname});
    const headers = {'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'same-origin', 'Cache-Control': 'no-store'};
    if (parsed.pathname === '/__runtime-lifecycle-fixture/') {
      if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405, headers); response.end(); return; }
      response.writeHead(200, {...headers, 'Content-Type': 'text/html; charset=utf-8'});
      response.end(request.method === 'HEAD' ? undefined : fixtureHtml); return;
    }
    if (parsed.pathname === '/__runtime-lifecycle-capability/') {
      if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405, headers); response.end(); return; }
      response.writeHead(200, {...headers, 'Content-Type': 'text/html; charset=utf-8'});
      response.end(request.method === 'HEAD' ? undefined : capabilityHtml); return;
    }
    if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405, headers); response.end(); return; }
    let relative;
    try { relative = decodeURIComponent(parsed.pathname).replace(/^\/+/, '') || 'index.html'; }
    catch { response.writeHead(400, headers); response.end(); return; }
    const root = path.resolve(site), local = path.resolve(root, relative);
    if (!local.startsWith(root + path.sep) && local !== root) { response.writeHead(404, headers); response.end(); return; }
    let bytes;
    try { const stat = await fs.lstat(local); if (!stat.isFile() || stat.isSymbolicLink()) throw Error(); bytes = await fs.readFile(local); }
    catch { response.writeHead(404, headers); response.end(); return; }
    const cache = relative.startsWith('runtime/') ? 'public, max-age=31536000, immutable' : 'no-store';
    response.writeHead(200, {...headers, 'Content-Type': MIME_TYPES[path.extname(local).toLowerCase()] || 'application/octet-stream',
      'Content-Length': bytes.byteLength, 'Cache-Control': cache});
    response.end(request.method === 'HEAD' ? undefined : bytes);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const address = server.address();
  requireValue(address && typeof address === 'object', 'server_address');
  return {server, requests, origin: 'http://127.0.0.1:' + address.port,
    close: () => new Promise(resolve => server.close(() => resolve()))};
}
async function waitFor(page, predicate, timeout, code) {
  let timer;
  const timeoutTask = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new HarnessFailure(code)), timeout + 1000);
  });
  try {
    await Promise.race([page.waitForFunction(predicate, null, {timeout}), timeoutTask]);
  } finally {
    clearTimeout(timer);
  }
}
async function installAudioTrace(page) {
  await page.addInitScript(() => {
    const trace = {contexts: [], snapshot: () => ({contexts: trace.contexts.map(row => ({
      sample_rate: row.sample_rate, states: [...row.states], closed: row.closed, resumes: row.resumes,
    }))})};
    globalThis.__runtimeLifecycleAudio = trace;
    const NativeAudioContext = globalThis.AudioContext;
    if (NativeAudioContext) {
      const AudioContextProxy = function(...args) {
        const context = new NativeAudioContext(...args);
        const row = {context, sample_rate: context.sampleRate, states: [context.state], closed: false, resumes: 0};
        trace.contexts.push(row);
        for (const name of ['resume', 'suspend', 'close']) {
          const method = context[name]; if (typeof method !== 'function') continue;
          try { context[name] = async (...values) => {
            const result = await method.apply(context, values); row.states.push(context.state);
            if (name === 'resume') row.resumes++; if (name === 'close') row.closed = true; return result;
          }; } catch {}
        }
        return context;
      };
      AudioContextProxy.prototype = NativeAudioContext.prototype;
      try { Object.defineProperty(globalThis, 'AudioContext', {value: AudioContextProxy, configurable: true, writable: true}); } catch {}
    }
  });
}
async function fixtureState(page) { return page.evaluate(() => globalThis.__runtimeLifecycleFixture?.snapshot?.() || null); }
async function startSession(page, disc) {
  await waitFor(page, () => globalThis.__runtimeLifecycleFixture?.ready === true ||
    globalThis.__runtimeLifecycleFixture?.load?.state === 'error', STARTUP_TIMEOUT_MS, 'fixture_startup_timeout');
  const initial = await fixtureState(page);
  requireValue(initial?.load?.state === 'ready', 'fixture_load_failed');
  await waitFor(page, () => {
    const state = globalThis.__runtimeLifecycleFixture?.player?.getState?.();
    return globalThis.__runtimeLifecycleFixture?.load?.state === 'error' ||
      state?.canImport === true;
  }, STARTUP_TIMEOUT_MS, 'can_import_timeout');
  const chooser = page.waitForEvent('filechooser', {timeout: STARTUP_TIMEOUT_MS});
  await page.locator('#choose-disc').click({timeout: STARTUP_TIMEOUT_MS});
  await (await chooser).setFiles(disc, {timeout: STARTUP_TIMEOUT_MS});
  await waitFor(page, () => {
    const fixture = globalThis.__runtimeLifecycleFixture, state = fixture?.player?.getState?.();
    return fixture?.load?.state === 'error' || state?.graphicsReady === true && state?.canStart === true;
  }, STARTUP_TIMEOUT_MS, 'graphics_ready_timeout');
  await waitFor(page, () => {
    const fixture = globalThis.__runtimeLifecycleFixture, state = fixture?.player?.getState?.();
    return fixture?.load?.state === 'error' || state?.scene === 'css' && state?.phase === 1 && state?.running === true;
  }, STARTUP_TIMEOUT_MS, 'scene_startup_timeout');
  const state = await fixtureState(page);
  requireValue(state?.load?.state === 'started' && state?.native?.phase === 1 && state?.native?.running, 'scene_not_running');
}
async function runCapabilityProbe(browser, capabilityUrl, out) {
  const page = await browser.newPage({viewport: {width: 800, height: 600}});
  const result = {result: 'unrun', game_imported: false, commands: [], failure_kind: null,
    genuine_visibility_pair: false, genuine_freeze_pair: false, real_lifecycle_supported: false};
  try {
    const response = await page.goto(capabilityUrl, {timeout: CAPABILITY_TIMEOUT_MS});
    requireValue(response?.status() === 200, 'capability_http_status');
    result.gpu = await page.evaluate(async () => {
      let adapter = null; try { adapter = await navigator.gpu?.requestAdapter(); } catch {}
      return {cross_origin_isolated: crossOriginIsolated === true, webgpu: !!navigator.gpu,
        adapter: !!adapter, max_texture_dimension_2d: Number(adapter?.limits?.maxTextureDimension2D || 0) || null};
    });
    const cdp = await page.context().newCDPSession(page);
    const marker = await page.evaluate(() => performance.now());
    for (const state of ['frozen', 'active']) {
      try {
        await cdp.send('Page.setWebLifecycleState', {state});
        result.commands.push({state, ok: true});
      } catch {
        result.commands.push({state, ok: false});
      }
      await new Promise(resolve => setTimeout(resolve, 250));
    }
    const snapshot = await page.evaluate(() => globalThis.__runtimeLifecycleCapability?.snapshot?.() || null);
    requireValue(snapshot && Array.isArray(snapshot.events), 'capability_snapshot_missing');
    const events = snapshot.events.filter(event => Number(event.at) >= Number(marker) - 1);
    const has = type => events.some(event => event.type === type);
    result.events = events.slice(-16);
    result.document = {hidden: snapshot.hidden === true, visibility: snapshot.visibility};
    result.genuine_visibility_pair = events.some(event => event.type === 'visibilitychange' && event.hidden === true) &&
      events.some(event => event.type === 'visibilitychange' && event.hidden === false);
    result.genuine_freeze_pair = has('freeze') && has('resume');
    result.real_lifecycle_supported = result.genuine_visibility_pair || result.genuine_freeze_pair;
    result.result = result.real_lifecycle_supported ? 'pass' : 'unrun';
    result.unrun_reason = result.real_lifecycle_supported ? null : 'genuine_lifecycle_events_unavailable';
    if (!result.real_lifecycle_supported)
      await page.screenshot({path: path.join(out, 'lifecycle-capability-unavailable.png'), fullPage: true});
  } catch (error) {
    result.result = 'unrun';
    result.failure_kind = error?.code || 'capability_probe_failed';
    result.unrun_reason = 'lifecycle_capability_probe_failed';
    await page.screenshot({path: path.join(out, 'lifecycle-capability-failure.png'), fullPage: true}).catch(() => {});
  } finally {
    await page.close().catch(() => {});
  }
  return result;
}
async function runLifecycleCase(browser, fixtureUrl, disc, out, mode) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  const synthetic = mode === 'synthetic';
  const syntheticHold = mode === 'synthetic-hidden-hold';
  const syntheticMode = synthetic || syntheticHold;
  const result = {mode, dwell_ms: HIDDEN_DWELL_MS, result: 'fail', cdp: [], failure_kind: null,
    scope: syntheticHold ? 'Controlled Emscripten requestAnimationFrame hold with synthetic document visibility getters; no genuine lifecycle or user-root-cause claim.' :
      synthetic ? 'Synthetic JS/native input handoff and missed-callback checkpoint only; no browser lifecycle or user-root-cause claim.' :
      'Real lifecycle mode is entered only after the installed-Chrome capability probe observes genuine lifecycle transitions.',
    experiment: {
      hypothesis: syntheticHold ? 'Holding the identified native main-loop callback for 350 ms yields no native sample during the hold; the first visible callback observes visible input and records bounded simulation debt.' :
        synthetic ? 'A controlled hidden checkpoint can hand off input ownership and return on an explicit visible checkpoint.' :
        'Installed Chrome can expose a genuine lifecycle transition through the supported protocol path.',
      pass_criteria: syntheticHold ? ['native main-loop rAF callback is identified by an actual sample',
        'zero native samples and source steps during the 350 ms synthetic hold',
        'first post-hold sample follows visible event and records reason 1 simulation debt above threshold 8',
        'no automatic resume or implicit save/unload', 'one active 32 kHz audio context remains owned'] :
        synthetic ? ['hidden and visible input activity are both observed', 'native remains running',
          'no unexpected incident, implicit save, or implicit unload', 'one active 32 kHz audio context remains owned'] :
        ['genuine lifecycle events are observed', 'no unexpected incident or implicit save/unload'],
      fail_criteria: ['unexpected incident', 'manual pause resumes without explicit intent', 'bounded report validation fails'],
      stopping_rule: 'Stop at the first unexpected pause, failure, or missing required receipt; retain the structured receipt and screenshot.',
    }};
  try {
    await installAudioTrace(page);
    const response = await page.goto(fixtureUrl, {timeout: STARTUP_TIMEOUT_MS});
    requireValue(response?.status() === 200, 'fixture_http_status');
    result.gpu = await page.evaluate(async () => {
      let adapter = null; try { adapter = await navigator.gpu?.requestAdapter(); } catch {}
      return {cross_origin_isolated: crossOriginIsolated === true, webgpu: !!navigator.gpu, adapter: !!adapter,
        max_texture_dimension_2d: Number(adapter?.limits?.maxTextureDimension2D || 0) || null};
    });
    requireValue(result.gpu.cross_origin_isolated && result.gpu.webgpu && result.gpu.adapter, 'gpu_precondition');
    await startSession(page, disc);
    const before = await fixtureState(page);
    result.before = {native: before.native, input_activity: before.input_activity.length,
      audio_contexts: before.audio_owner?.contexts?.length || 0};
    if (syntheticHold) {
      try {
        await waitFor(page, () => globalThis.__runtimeLifecycleFixture.synthetic_native_callback_identified === true,
          5000, 'synthetic_main_loop_callback_unavailable');
      } catch {
        result.result = 'unrun';
        result.unrun_reason = 'synthetic_main_loop_callback_unavailable';
        result.state = await fixtureState(page);
        await page.screenshot({path: path.join(out, 'lifecycle-synthetic-hidden-hold-unrun.png'), fullPage: true});
        return result;
      }
      const hold = await page.evaluate(() => globalThis.__runtimeLifecycleFixture.syntheticHiddenHold());
      result.synthetic_hold = hold;
      if (!hold?.available) {
        result.result = 'unrun';
        result.unrun_reason = hold?.unrun_reason || 'synthetic_main_loop_boundary_unavailable';
        result.state = await fixtureState(page);
        await page.screenshot({path: path.join(out, 'lifecycle-synthetic-hidden-hold-unrun.png'), fullPage: true});
        return result;
      }
      await waitFor(page, () => {
        const fixture = globalThis.__runtimeLifecycleFixture;
        return fixture.samples.length > Number(fixture.synthetic_hold_result?.before_samples || 0);
      }, SCENE_TIMEOUT_MS, 'synthetic_visible_native_sample_missing');
    } else if (synthetic) {
      const suspended = await page.evaluate(() => globalThis.__runtimeLifecycleFixture.syntheticSuspend());
      requireValue(suspended === true, 'synthetic_hidden_activity_unavailable');
      await new Promise(resolve => setTimeout(resolve, HIDDEN_DWELL_MS));
      const during = await fixtureState(page);
      requireValue(during.synthetic_missed_callbacks === true, 'synthetic_window_not_recorded');
      requireValue(during.dropped_frame_callbacks > 0, 'synthetic_callback_gap_unobserved');
      const resumed = await page.evaluate(() => globalThis.__runtimeLifecycleFixture.syntheticResume());
      requireValue(resumed === true, 'synthetic_visible_activity_unavailable');
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.input_activity.some(item => item.visible === 1),
        SCENE_TIMEOUT_MS, 'synthetic_visible_activity_missing');
    } else if (mode === 'manual-pause') {
      await page.evaluate(() => {
        globalThis.__runtimeLifecycleFixture.manual_intent.push('manual_pause');
        return globalThis.__runtimeLifecycleFixture.player.pause();
      });
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.player.getState()?.running === false,
        SCENE_TIMEOUT_MS, 'manual_pause_not_stopped');
    } else {
      const cdp = await page.context().newCDPSession(page);
      await cdp.send('Page.setWebLifecycleState', {state: 'frozen'});
      result.cdp.push('frozen');
      await new Promise(resolve => setTimeout(resolve, HIDDEN_DWELL_MS));
      await cdp.send('Page.setWebLifecycleState', {state: 'active'});
      result.cdp.push('active');
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.browser_events.some(event =>
        event.type === 'resume' || event.type === 'pageshow'), SCENE_TIMEOUT_MS, 'lifecycle_resume_event_missing');
    }
    const afterLifecycle = await fixtureState(page);
    const controlledDebt = syntheticHold ? afterLifecycle.incidents.filter(incident => incident.reason_code === 1) : [];
    const unexpected = afterLifecycle.incidents.filter(incident =>
      UNEXPECTED_REASONS.has(incident.reason_code) && !(syntheticHold && incident.reason_code === 1));
    const safeReport = validateDiagnosticReport(afterLifecycle.report);
    result.after_lifecycle = {native: afterLifecycle.native, report: safeReport,
      browser_events: afterLifecycle.browser_events.slice(-12), input_activity: afterLifecycle.input_activity.slice(-12),
      audio: afterLifecycle.audio.slice(-12), audio_owner: afterLifecycle.audio_owner,
      incidents: afterLifecycle.incidents, samples: afterLifecycle.samples.slice(-8),
      manual_intent: afterLifecycle.manual_intent, synthetic_events: afterLifecycle.synthetic_events,
      synthetic_hold_result: afterLifecycle.synthetic_hold_result,
      unload_calls: afterLifecycle.unload_calls, cache_save_calls: afterLifecycle.cache_save_calls};
    result.candidate_reproduced = syntheticHold ? controlledDebt.length > 0 : !syntheticMode && mode === 'frozen' && unexpected.length > 0;
    if (mode === 'manual-pause') {
      requireValue(afterLifecycle.native.running === false, 'manual_pause_auto_resumed');
      requireValue(!unexpected.length, 'manual_pause_unexpected_incident');
      requireValue(afterLifecycle.manual_intent.includes('manual_pause'), 'manual_pause_intent_missing');
      await page.evaluate(() => {
        globalThis.__runtimeLifecycleFixture.manual_intent.push('manual_resume');
        return globalThis.__runtimeLifecycleFixture.player.resume();
      });
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.player.getState()?.running === true,
        SCENE_TIMEOUT_MS, 'manual_resume_failed');
      result.manual_recovery = 'explicit_resume_only';
      result.manual_intent = (await fixtureState(page)).manual_intent;
    } else if (syntheticHold) {
      const hold = afterLifecycle.synthetic_hold_result;
      requireValue(hold?.available === true, 'synthetic_hold_unavailable');
      requireValue(hold.hidden_sample_delta === 0 && hold.hidden_source_step_delta === 0,
        'synthetic_hidden_native_progress');
      requireValue(hold.held_callbacks > 0 && hold.released_callbacks > 0, 'synthetic_native_callback_gap_unobserved');
      requireValue(controlledDebt.some(incident => incident.value > 8 && incident.threshold === 8),
        'synthetic_simulation_debt_missing');
      requireValue(afterLifecycle.native.running === false, 'synthetic_debt_not_paused');
      requireValue(!afterLifecycle.manual_intent.includes('synthetic_resume') &&
        !afterLifecycle.manual_intent.includes('manual_resume'), 'synthetic_hold_auto_resume');
      requireValue(afterLifecycle.synthetic_events.some(event => event.kind === 'synthetic_hidden_event'),
        'synthetic_hidden_event_missing');
      requireValue(afterLifecycle.synthetic_events.some(event => event.kind === 'synthetic_visible_event'),
        'synthetic_visible_event_missing');
      const hiddenEvent = afterLifecycle.synthetic_events.find(event => event.kind === 'synthetic_hidden_event');
      const visibleEvent = afterLifecycle.synthetic_events.find(event => event.kind === 'synthetic_visible_event');
      const browserHidden = afterLifecycle.browser_events.find(event => event.type === 'visibilitychange' && event.hidden === true);
      const browserVisible = afterLifecycle.browser_events.find(event => event.type === 'visibilitychange' && event.hidden === false &&
        Number(event.at) >= Number(visibleEvent?.at || Infinity));
      const postVisibleSample = afterLifecycle.samples.find(sample => Number(sample.at) >= Number(visibleEvent?.at || Infinity));
      const postVisibleInput = afterLifecycle.input_activity.find(item => item.visible === 1 &&
        Number(item.at) >= Number(visibleEvent?.at || Infinity));
      requireValue(hiddenEvent && visibleEvent && browserHidden && browserVisible && hiddenEvent.at < visibleEvent.at &&
        browserHidden.at <= visibleEvent.at && browserVisible.at >= visibleEvent.at && postVisibleSample && postVisibleInput,
        'synthetic_hidden_visible_sample_order');
    } else if (synthetic) {
      requireValue(!unexpected.length, 'synthetic_unexpected_incident');
      requireValue(afterLifecycle.native.running === true, 'synthetic_auto_pause');
      requireValue(afterLifecycle.synthetic_events.some(event => event.kind === 'synthetic_hidden_checkpoint'),
        'synthetic_hidden_checkpoint_missing');
      requireValue(afterLifecycle.synthetic_events.some(event => event.kind === 'synthetic_visible_checkpoint'),
        'synthetic_visible_checkpoint_missing');
    } else {
      requireValue(!unexpected.length, 'real_lifecycle_incident');
      requireValue(afterLifecycle.native.running === true, 'lifecycle_auto_pause');
    }
    const stable = await fixtureState(page);
    requireValue(stable.unload_calls === 0 && stable.cache_save_calls === 0, 'lifecycle_saved_implicitly');
    requireValue((stable.audio_owner?.contexts || []).length === 1, 'audio_owner_count');
    const activeContexts = (stable.audio_owner?.contexts || []).filter(context => !context.closed);
    requireValue(activeContexts.length === 1 && activeContexts[0].sample_rate === 32000, 'audio_owner_closed');
    result.input_visibility = {observed_hidden: stable.input_activity.some(item => item.visible === 0),
      observed_visible: stable.input_activity.some(item => item.visible === 1), total: stable.input_activity.length};
    requireValue(result.input_visibility.observed_visible, 'input_visible_not_restored');
    if (synthetic) requireValue(result.input_visibility.observed_hidden, 'synthetic_hidden_not_observed');
    await page.evaluate(async () => { await globalThis.__runtimeLifecycleFixture.player.unload(); });
    const afterUnload = await fixtureState(page);
    requireValue(afterUnload.unload_calls === 1, 'explicit_unload_not_observed');
    result.explicit_unload = {unload_calls: afterUnload.unload_calls, cache_save_calls: afterUnload.cache_save_calls};
    await page.screenshot({path: path.join(out, 'lifecycle-' + mode + '.png'), fullPage: true});
    result.result = 'pass';
  } catch (error) {
    result.failure_kind = error?.code || 'browser_error';
    result.state = await fixtureState(page).catch(() => null);
    await page.screenshot({path: path.join(out, 'lifecycle-' + mode + '-failure.png'), fullPage: true}).catch(() => {});
  } finally {
    try { await page.evaluate(async () => { try { await globalThis.__runtimeLifecycleFixture?.player?.unload?.(); } catch {} }); } catch {}
    await page.close().catch(() => {});
  }
  return result;
}

const {values} = parseArgs({options: {
  site: {type: 'string'}, manifest: {type: 'string'}, disc: {type: 'string'},
  playwright: {type: 'string'}, out: {type: 'string'}, synthetic: {type: 'boolean'},
  'synthetic-hidden-hold': {type: 'boolean'}, help: {type: 'boolean'},
}});
if (values.help) {
  console.log('Usage: node tests/runtime_lifecycle_incident_browser_test.mjs --site AUDITED_AUDIO_PLAYER --manifest MANIFEST --disc OWNED_ISO --out FRESH_EVIDENCE_DIR [--playwright PLAYWRIGHT_DIR] [--synthetic | --synthetic-hidden-hold]');
  process.exit(0);
}
for (const name of ['site', 'manifest', 'disc', 'out']) requireValue(values[name], name + '_required');
requireValue(!(values.synthetic && values['synthetic-hidden-hold']), 'synthetic_mode_conflict');
const syntheticMode = values.synthetic === true || values['synthetic-hidden-hold'] === true;
const report = {schema: 'melee-web-runtime-lifecycle-browser-v1',
  scope: values['synthetic-hidden-hold'] ?
    'Bounded synthetic Emscripten requestAnimationFrame main-loop hold with synthetic document visibility getters, manual intent, audio ownership and explicit unload/save boundaries. It does not represent genuine browser lifecycle behavior or a user root cause.' :
    values.synthetic ?
    'Bounded synthetic JS/native input handoff and missed-callback checkpoint with manual intent, audio ownership and explicit unload/save boundaries. It does not represent browser background/freeze behavior or a user root cause.' :
    'Bounded installed-Chrome lifecycle capability preflight followed by frozen/active detection with manual pause, source input activity, Web Audio ownership and explicit unload/save boundaries. No foreground, physical-input, audible-output, pixel, PCM-equivalence or sustained-gameplay claim.',
  result: 'fail', checks: [], started_at: new Date().toISOString(),
  bounds: {startup_timeout_ms: STARTUP_TIMEOUT_MS, scene_timeout_ms: SCENE_TIMEOUT_MS,
    capability_timeout_ms: CAPABILITY_TIMEOUT_MS, hidden_dwell_ms: HIDDEN_DWELL_MS}};
let browser, context, server;
try {
  const site = path.resolve(values.site), out = path.resolve(values.out);
  await fs.mkdir(path.dirname(out), {recursive: true}); await fs.mkdir(out, {recursive: false});
  const manifest = JSON.parse(await fs.readFile(path.resolve(values.manifest), 'utf8'));
  const packageInfo = await verifyPackage(site, manifest); report.identity = packageInfo.identity;
  server = await createServer(site, fixtureMarkup(packageInfo.runtimeModule, packageInfo.identity), capabilityMarkup());
  report.origin = server.origin;
  const loaded = await loadBrowserTools(values.playwright);
  browser = await loaded.chromium.launch(browserLaunchOptions(loaded.browser, {
    headed: false, audible: false, timeout: STARTUP_TIMEOUT_MS,
  }));
  report.browser = browser.version(); report.browser_mode = 'headless'; report.audio_output = 'muted_by_shared_policy';
  context = await browser.newContext(); const fixtureUrl = server.origin + '/__runtime-lifecycle-fixture/';
  let skipGame = false;
  if (!syntheticMode) {
    const capability = await runCapabilityProbe(browser, server.origin + '/__runtime-lifecycle-capability/', out);
    report.capability_probe = capability;
    if (capability.failure_kind || !capability.real_lifecycle_supported) {
      report.result = 'unrun'; report.unrun_reason = capability.unrun_reason || 'genuine_lifecycle_events_unavailable';
      report.game_imported = false; skipGame = true;
      report.checks.push('lifecycle capability unavailable; game import skipped');
    }
  }
  if (!skipGame) {
    const modes = values['synthetic-hidden-hold'] ? ['synthetic-hidden-hold', 'manual-pause'] :
      values.synthetic ? ['synthetic'] : ['frozen', 'manual-pause'];
    for (const mode of modes) {
      const item = await runLifecycleCase(browser, fixtureUrl, path.resolve(values.disc), out, mode);
      report[mode.replaceAll('-', '_')] = item;
      if (item.result === 'unrun') {
        report.result = 'unrun'; report.unrun_reason = item.unrun_reason || 'synthetic_main_loop_boundary_unavailable';
        report.checks.push(mode + ' lifecycle case unavailable: ' + report.unrun_reason);
        continue;
      }
      if (item.result !== 'pass') throw new HarnessFailure(mode + '_' + (item.failure_kind || 'failed'));
      report.checks.push(mode + ' lifecycle case passed');
    }
    report.game_imported = true;
  }
  const forbidden = server.requests.filter(request => (request.method !== 'GET' && request.method !== 'HEAD') ||
    /(?:upload|evidence|manifest|__melee_evidence)/i.test(request.path));
  requireValue(forbidden.length === 0, 'forbidden_network_request');
  report.network = {request_count: server.requests.length, application_uploads: 0};
  if (!skipGame && report.result !== 'unrun') report.result = 'pass';
} catch (error) {
  report.failure_kind = error?.code || 'harness_error'; process.exitCode = 1;
} finally {
  await context?.close().catch(() => {}); await browser?.close().catch(() => {}); await server?.close().catch(() => {});
  report.finished_at = new Date().toISOString();
  if (values.out) {
    try { await fs.writeFile(path.join(path.resolve(values.out), 'report.json'),
      JSON.stringify(report, null, 2) + '\n', {flag: 'wx'}); }
    catch { report.write_failure = true; process.exitCode = 1; }
  }
}
console.log(JSON.stringify({result: report.result, checks: report.checks,
  ...(report.failure_kind ? {failure_kind: report.failure_kind} : {}),
  report: values.out ? path.join(path.resolve(values.out), 'report.json') : null}));
