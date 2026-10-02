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
import vm from 'node:vm';
import {performance} from 'node:perf_hooks';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const IDENTITY_META_ID = 'runtime-diagnostic-identity';
const STARTUP_TIMEOUT_MS = 120000;
const STARTUP_ONLY_TIMEOUT_MS = 5000;
const NATIVE_HOOK_TIMEOUT_MS = 5000;
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
function safeHarnessErrorName(error) {
  return ['Error', 'EvalError', 'RangeError', 'ReferenceError', 'SyntaxError', 'TypeError', 'URIError']
    .includes(error?.name) ? error.name : 'Error';
}
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
  native_sample_count: 0, native_source_steps: 0, trace_sequence: 0,
  native_hooks: {activity: false, unload: false, cache: false}, native_hook_module: null,
  native_hook_timer: 0, native_hook_failure: null,
  startup_timeout_ms: new URL(location.href).searchParams.has('startup-only') ? 5000 : 120000,
  start_readiness: {checks: 0, result: null, reason: null}, last_error: null,
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
  const beforeSamples = fixture.native_sample_count;
  fixture.synthetic_raf_invocations = Number(fixture.synthetic_raf_invocations || 0) + 1;
  entry.callback.call(globalThis, timestamp);
  if (fixture.native_sample_count > beforeSamples) {
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
    type, at: performance.now(), sequence: ++fixture.trace_sequence,
    hidden: document.hidden, visibility: document.visibilityState,
  }), true);
}

const number = value => Number.isFinite(Number(value)) ? Number(value) : null;
function safeFailureKind(error) {
  const message = String(error?.message || error || '').toLowerCase();
  if (/prepare a valid local disc|valid local disc/.test(message)) return 'start_prerequisite_unavailable';
  if (/graphics.*prepar|prepar.*graphics/.test(message)) return 'graphics_preparation_failed';
  if (/stopped responding/.test(message)) return 'runtime_operation_timeout';
  if (/startup timed out/.test(message)) return 'runtime_startup_timeout';
  if (/webgpu|adapter/.test(message)) return 'graphics_adapter_unavailable';
  if (/source movie|disc session|disc range|local game data/.test(message)) return 'disc_preparation_failed';
  return 'runtime_start_failed';
}
function recordSample(args, callbackMs) {
  const sourceSteps = number(args[16]);
  fixture.native_sample_count++;
  if (sourceSteps !== null) fixture.native_source_steps += Math.max(0, sourceSteps);
  const row = {sample_index: fixture.native_sample_count, timestamp: number(args[0]),
    source_frame: number(args[1]), scene: number(args[2]),
    debt_ticks: number(args[8]), total_ms: number(args[11]), source_steps: number(args[16]),
    source_draws: number(args[17]), running: args[18] === true || args[18] === 1,
    callback_ms: callbackMs, at: performance.now(), sequence: ++fixture.trace_sequence};
  if (fixture.samples.length >= 128) fixture.samples.shift();
  fixture.samples.push(row);
}
function recordIncident(args) {
  if (fixture.incidents.length >= 32) return;
  fixture.incidents.push({reason_code: number(args[0]), value: number(args[1]), threshold: number(args[2]),
    source_frame: number(args[3]), scene: number(args[4]), clock_owner: number(args[5]),
    sequence: ++fixture.trace_sequence});
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
function nativeHooksReady() {
  return fixture.native_hooks.activity === true && fixture.native_hooks.unload === true;
}
function nativeHooksComplete(module = fixture.owner?.Module) {
  return nativeHooksReady() && (fixture.native_hooks.cache === true || typeof module?.saveRuntimeCache !== 'function');
}
function installNativeHooks() {
  const module = fixture.owner?.Module;
  if (!module) return false;
  if (fixture.native_hook_module !== module) {
    fixture.native_hook_module = module;
    fixture.native_hooks = {activity: false, unload: false, cache: false};
  }
  if (!fixture.native_hooks.activity && typeof module._melee_web_input_set_activity === 'function') {
    const activity = module._melee_web_input_set_activity;
    module._melee_web_input_set_activity = (focused, visible) => {
      fixture.input_activity.push({focused: Number(focused) ? 1 : 0, visible: Number(visible) ? 1 : 0,
        at: performance.now(), sequence: ++fixture.trace_sequence});
      return activity.call(module, focused, visible);
    };
    fixture.native_hooks.activity = true;
  }
  if (!fixture.native_hooks.unload && typeof module._melee_web_native_menu_unload === 'function') {
    const unload = module._melee_web_native_menu_unload;
    module._melee_web_native_menu_unload = (...args) => {
      fixture.unload_calls++;
      return unload.apply(module, args);
    };
    fixture.native_hooks.unload = true;
  }
  if (!fixture.native_hooks.cache && typeof module.saveRuntimeCache === 'function') {
    const save = module.saveRuntimeCache;
    module.saveRuntimeCache = (...args) => { fixture.cache_save_calls++; return save.apply(module, args); };
    fixture.native_hooks.cache = true;
  }
  const complete = nativeHooksComplete(module);
  if (complete && fixture.native_hook_timer) {
    clearInterval(fixture.native_hook_timer);
    fixture.native_hook_timer = 0;
  }
  return complete;
}
function startNativeHookPolling() {
  if (fixture.native_hook_timer) return;
  fixture.native_hook_timer = setInterval(() => { installNativeHooks(); }, 25);
}
fixture.waitForNativeHooks = (timeoutMs = __NATIVE_HOOK_TIMEOUT_MS__) => {
  installNativeHooks();
  if (nativeHooksComplete()) return Promise.resolve(true);
  return new Promise(resolve => {
    const deadline = performance.now() + timeoutMs;
    const check = () => {
      if (installNativeHooks()) { resolve(true); return; }
      if (performance.now() >= deadline) {
        fixture.native_hook_failure = 'native_hooks_unavailable';
        resolve(false); return;
      }
      setTimeout(check, 25);
    };
    check();
  });
};
fixture.waitForCanStart = (timeoutMs = fixture.startup_timeout_ms) => {
  fixture.start_readiness = {checks: 0, result: null, reason: null};
  return new Promise(resolve => {
    const deadline = performance.now() + timeoutMs;
    const check = () => {
      fixture.start_readiness.checks++;
      const state = fixture.player?.getState?.();
      if (state?.canStart === true) {
        fixture.start_readiness.result = 'ready';
        resolve(true); return;
      }
      if (state?.state === 'error') {
        fixture.start_readiness.result = 'unavailable';
        fixture.start_readiness.reason = fixture.last_error || 'start_prerequisite_unavailable';
        resolve(false); return;
      }
      if (performance.now() >= deadline) {
        fixture.start_readiness.result = 'timeout';
        fixture.start_readiness.reason = 'start_prerequisite_timeout';
        resolve(false); return;
      }
      setTimeout(check, 25);
    };
    check();
  });
};
function exposeOwner(owner) {
  fixture.owner = owner;
  startNativeHookPolling();
  installNativeHooks();
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
}
function exposeState(next) {
  if (fixture.states.length < 128) fixture.states.push({at: performance.now(), scene: next.scene,
    phase: next.phase, running: !!next.running, requiresReload: !!next.requiresReload});
}
function setSyntheticActivity(focused, visible) {
  installNativeHooks();
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
  fixture.synthetic_events.push({kind, at: performance.now(), sequence: ++fixture.trace_sequence, hidden: fixture.synthetic_hidden,
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
fixture.syntheticHiddenHold = async ({emitVisibility = true} = {}) => {
  if (typeof originalRequestAnimationFrame !== 'function')
    return {available: false, unrun_reason: 'synthetic_request_animation_frame_unavailable'};
  if (!fixture.synthetic_native_callback_identified)
    return {available: false, unrun_reason: 'synthetic_main_loop_callback_unavailable'};
  if (!syntheticVisibilityOverride())
    return {available: false, unrun_reason: 'synthetic_visibility_override_unavailable'};
  const beforeSamples = fixture.native_sample_count;
  const beforeSourceSteps = fixture.native_source_steps;
  const startedAt = performance.now();
  fixture.synthetic_hidden = emitVisibility;
  fixture.synthetic_callback_hold = true;
  if (emitVisibility && !dispatchSyntheticVisibility('synthetic_hidden_event')) {
    fixture.synthetic_hidden = false;
    fixture.synthetic_callback_hold = false;
    return {available: false, unrun_reason: 'synthetic_visibility_event_unavailable'};
  }
  await new Promise(resolve => setTimeout(resolve, __HIDDEN_DWELL_MS__));
  const duringSamples = fixture.native_sample_count;
  const duringSourceSteps = fixture.native_source_steps;
  const hiddenAt = performance.now();
  fixture.synthetic_hidden = false;
  const visibleEvent = emitVisibility ? dispatchSyntheticVisibility('synthetic_visible_event') : true;
  const released = releaseHeldNativeRaf();
  const visibleAt = performance.now();
  const result = {available: visibleEvent && released, started_at: startedAt, hidden_at: hiddenAt,
    visible_at: visibleAt, before_native_sample_count: beforeSamples, during_native_sample_count: duringSamples,
    retained_sample_count: fixture.samples.length, before_source_steps: beforeSourceSteps,
    after_native_sample_count: fixture.native_sample_count, during_source_steps: duringSourceSteps,
    after_source_steps: fixture.native_source_steps,
    held_callbacks: fixture.synthetic_held_callbacks, released_callbacks: fixture.synthetic_released_callbacks,
    hidden_sample_delta: duringSamples - beforeSamples, hidden_source_step_delta: duringSourceSteps - beforeSourceSteps,
    visibility_emitted: emitVisibility};
  fixture.synthetic_hold_result = result;
  return result;
};
fixture.hasPostHoldNativeSample = () => fixture.native_sample_count >
  Number(fixture.synthetic_hold_result?.before_native_sample_count || 0);
fixture.snapshot = () => {
  installNativeHooks();
  return {
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
  native_sample_count: fixture.native_sample_count, native_source_steps: fixture.native_source_steps,
  native_hooks: fixture.native_hooks, native_hook_failure: fixture.native_hook_failure,
    startup_timeout_ms: fixture.startup_timeout_ms, start_readiness: fixture.start_readiness,
    last_error: fixture.last_error,
    synthetic_hold_result: fixture.synthetic_hold_result,
    report: fixture.owner?.diagnostics?.exportReports?.() || null,
  };
};
let mountMeleeRuntime;
try {
  ({mountMeleeRuntime} = await import("__RUNTIME_MODULE__"));
  fixture.player = await mountMeleeRuntime({canvas: document.getElementById('canvas'),
    diagnosticIdentity: __IDENTITY__, recordDiagnostics: true, onOwner: exposeOwner, onState: exposeState,
    onEvent: (name, data) => { if (name === 'audio') safeAudio(data); },
    onError: error => { fixture.errors++; fixture.last_error = safeFailureKind(error); }});
  fixture.ready = true; fixture.load = {state: 'ready'};
} catch (error) { fixture.load = {state: 'error', reason: safeFailureKind(error)}; fixture.last_error = safeFailureKind(error); fixture.errors++; }
const input = document.getElementById('disc-file');
document.getElementById('choose-disc').onclick = () => input.click();
input.onchange = async () => {
  const file = input.files?.[0]; if (!file || !fixture.player) return;
  fixture.load = {state: 'configuring'};
  try {
    await fixture.player.configureSaveProfile('everything');
    fixture.load = {state: 'importing'}; await fixture.player.importDisc(file);
    if (!await fixture.waitForNativeHooks()) {
      fixture.last_error = fixture.native_hook_failure || 'native_hooks_unavailable';
      fixture.load = {state: 'error', phase: 'native_hooks', reason: fixture.last_error};
      return;
    }
    if (!await fixture.waitForCanStart()) {
      fixture.last_error = fixture.start_readiness.reason || 'start_prerequisite_unavailable';
      fixture.load = {state: 'error', phase: 'start_readiness', reason: fixture.last_error};
      return;
    }
    fixture.load = {state: 'starting'}; await fixture.player.start(); fixture.load = {state: 'started'};
  } catch (error) {
    fixture.load = {state: 'error', phase: 'import_or_start', reason: safeFailureKind(error)};
    fixture.last_error = safeFailureKind(error); fixture.errors++;
  }
};
`;
  const script = source.replace('__RUNTIME_MODULE__', moduleUrl)
    .replace('__IDENTITY__', identityJson).replace('__IDENTITY__', identityJson)
    .replace('__NATIVE_HOOK_TIMEOUT_MS__', String(NATIVE_HOOK_TIMEOUT_MS))
    .replace('__HIDDEN_DWELL_MS__', String(HIDDEN_DWELL_MS))
    .replace('__LIFECYCLE_EVENTS__', JSON.stringify(LIFECYCLE_EVENTS));
  const unresolved = script.match(/__[A-Z][A-Z0-9_]+__/g) || [];
  requireValue(unresolved.length === 0, 'fixture_unresolved_placeholder');
  return '<!doctype html><html><head><meta charset="utf-8"><title>Runtime lifecycle fixture</title></head>' +
    '<body><button id="choose-disc" type="button">Choose local disc</button>' +
    '<input id="disc-file" type="file" accept=".iso,.gcm,.ciso" hidden>' +
    '<canvas id="canvas" width="640" height="480" tabindex="0"></canvas>' +
    '<script type="module">' + script + '</script></body></html>';
}

async function runFixturePreflight() {
  const identity = {schema_version: 1, source_commit: '0'.repeat(40),
    runtime_hash: '0'.repeat(16), build_profile: 'audio-player'};
  const html = fixtureMarkup('runtime/mock.mjs', identity);
  const script = html.match(/<script type="module">([\s\S]*)<\/script>/)?.[1];
  requireValue(script, 'fixture_preflight_script');
  const importLine = '  ({mountMeleeRuntime} = await import("/runtime/mock.mjs"));';
  const mockImport = `  mountMeleeRuntime = async ({onOwner, onState}) => {
    const calls = {configure: 0, import: 0, start: 0, file_name: null};
    globalThis.__fixturePreflightCalls = calls;
    const state = {scene: 'idle', phase: 0, running: false, canStart: true,
      ready: true, requiresReload: false, canPause: false, canUnload: true,
      bundle: true, state: 'prepared'};
    const Module = {
      _melee_web_input_set_activity() {},
      _melee_web_native_menu_unload() {},
      saveRuntimeCache() {},
      _melee_web_native_menu_phase: () => 0,
      _melee_web_native_menu_running: () => 0,
      _melee_web_input_message: () => 0,
      UTF8ToString: () => '',
    };
    onOwner({Module, callbacks: {}});
    onState({scene: 'idle', phase: 0, running: false, canStart: true,
      ready: true, requiresReload: false, canPause: false, canUnload: true,
      bundle: true, state: 'prepared'});
    return {
      getState: () => state,
      configureSaveProfile: async () => { calls.configure++; },
      importDisc: async file => { calls.import++; calls.file_name = file?.name || null; },
      start: async () => { calls.start++; state.running = true; state.scene = 'css'; state.phase = 1; },
      unload: async () => {},
    };
  };`;
  const executable = script.replace(importLine, mockImport);
  requireValue(executable !== script, 'fixture_preflight_import');
  const elements = new Map([
    ['choose-disc', {onclick: null, click() {}}],
    ['disc-file', {files: [], onchange: null}],
    ['canvas', {id: 'canvas'}],
  ]);
  const document = {
    hidden: false, visibilityState: 'visible',
    addEventListener() {}, dispatchEvent() {},
    getElementById(id) { return elements.get(id) || null; },
  };
  const context = {
    URL, document, location: {href: 'http://127.0.0.1/__runtime-lifecycle-fixture/'},
    performance, setTimeout, clearTimeout, setInterval, clearInterval,
    Map, WeakSet, Promise, Number, Object, Array, JSON, console,
    Event: class Event { constructor(type) { this.type = type; } },
    addEventListener() {}, dispatchEvent() {},
  };
  context.globalThis = context;
  context.window = context;
  const vmContext = vm.createContext(context);
  await vm.runInContext(`(async () => {\n${executable}\n})()`, vmContext, {timeout: 2000});
  const fixture = vmContext.__runtimeLifecycleFixture;
  requireValue(fixture, 'fixture_preflight_missing_global');
  requireValue(await fixture.waitForNativeHooks(), 'fixture_preflight_hooks');
  elements.get('disc-file').files = [{name: 'mock.gci'}];
  await elements.get('disc-file').onchange();
  const calls = vmContext.__fixturePreflightCalls;
  requireValue(calls && calls.configure === 1 && calls.import === 1 && calls.start === 1,
    'fixture_preflight_operations');
  requireValue(calls.file_name === 'mock.gci', 'fixture_preflight_file');
  requireValue(fixture.load?.state === 'started', 'fixture_preflight_started');
  requireValue(fixture.start_readiness?.result === 'ready' && fixture.start_readiness.checks === 1,
    'fixture_preflight_readiness');
  const preflightSampleCount = fixture.native_sample_count;
  fixture.synthetic_hold_result = {before_native_sample_count: preflightSampleCount};
  requireValue(fixture.hasPostHoldNativeSample() === false, 'fixture_preflight_post_hold_false');
  fixture.native_sample_count++;
  requireValue(fixture.hasPostHoldNativeSample() === true, 'fixture_preflight_post_hold_true');
  const preflightPage = {waitForFunction: async predicate =>
    requireValue(vm.runInContext(`(${String(predicate)})()`, vmContext) === true,
      'fixture_preflight_predicate_result')};
  await waitFor(preflightPage, () => globalThis.__runtimeLifecycleFixture.hasPostHoldNativeSample(),
    25, 'fixture_preflight_predicate_timeout');
  fixture.owner.Module._melee_web_input_set_activity(1, 1);
  fixture.owner.Module._melee_web_native_menu_unload();
  const snapshot = fixture.snapshot();
  requireValue(snapshot.native_hooks.activity && snapshot.native_hooks.unload && snapshot.native_hooks.cache,
    'fixture_preflight_hook_flags');
  requireValue(snapshot.input_activity.length === 1 && snapshot.unload_calls === 1,
    'fixture_preflight_wrappers');
  const requestedPlans = [
    {name: 'synthetic_hidden_hold', flags: {syntheticHiddenHold: true},
      modes: ['synthetic-hidden-hold', 'manual-pause-hidden'], manual: 'synthetic-hidden-hold'},
    {name: 'synthetic_recovery', flags: {expectLifecycleRecovery: true},
      modes: ['synthetic-hidden-hold-recovery', 'manual-pause-hidden'], manual: 'synthetic-hidden-hold'},
    {name: 'synthetic_foreground', flags: {foregroundCallbackHold: true},
      modes: ['foreground-callback-hold', 'manual-pause-foreground'], manual: 'synthetic-visible-hold'},
    {name: 'genuine', flags: {}, modes: ['frozen', 'manual-pause'], manual: 'genuine-cdp'},
    {name: 'capability_only', flags: {capabilityOnly: true}, modes: [], manual: null},
  ];
  const modePlanProtocols = requestedPlans.map(expected => {
    const plan = lifecycleModePlan(expected.flags);
    requireValue(JSON.stringify(plan.modes) === JSON.stringify(expected.modes),
      'fixture_preflight_mode_plan_' + expected.name);
    const manualMode = plan.modes.find(mode => mode.startsWith('manual-pause'));
    requireValue((manualMode ? lifecycleCaseProtocol(manualMode) : null) === expected.manual,
      'fixture_preflight_manual_protocol_' + expected.name);
    return {name: expected.name, modes: plan.modes, manual_protocol: expected.manual};
  });
  return {result: 'pass', native_hooks: snapshot.native_hooks,
    input_activity: snapshot.input_activity.length, unload_calls: snapshot.unload_calls,
    configure_calls: calls.configure, import_calls: calls.import, start_calls: calls.start,
    file_name: calls.file_name, load_state: fixture.load.state,
    post_hold_predicate: true, predicate_execution: true,
    start_readiness: snapshot.start_readiness, mode_plan_protocols: modePlanProtocols};
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
function capabilityOnlyFixtureMarkup() {
  return '<!doctype html><meta charset="utf-8"><title>Lifecycle capability blank page</title>';
}
function lifecycleCaseProtocol(mode) {
  if (mode === 'manual-pause' || mode === 'frozen') return 'genuine-cdp';
  if (mode === 'manual-pause-hidden') return 'synthetic-hidden-hold';
  if (mode === 'manual-pause-foreground') return 'synthetic-visible-hold';
  return mode;
}
function lifecycleModePlan(flags = {}) {
  const capabilityOnly = flags.capabilityOnly === true;
  const startupOnly = flags.startupOnly === true;
  const syntheticFlags = [flags.synthetic === true, flags.syntheticHiddenHold === true,
    flags.expectLifecycleRecovery === true, flags.foregroundCallbackHold === true].filter(Boolean);
  requireValue(!(capabilityOnly && (startupOnly || syntheticFlags.length > 0)), 'capability_mode_conflict');
  requireValue(!(startupOnly && syntheticFlags.length > 0), 'lifecycle_mode_conflict');
  requireValue(syntheticFlags.length <= 1, 'synthetic_mode_conflict');
  if (capabilityOnly) return {capabilityOnly: true, syntheticMode: false, modes: [], protocols: []};
  if (startupOnly) return {capabilityOnly: false, syntheticMode: true,
    modes: ['startup-only'], protocols: ['startup-only']};
  const modes = flags.expectLifecycleRecovery ? ['synthetic-hidden-hold-recovery', 'manual-pause-hidden'] :
    flags.syntheticHiddenHold ? ['synthetic-hidden-hold', 'manual-pause-hidden'] :
    flags.foregroundCallbackHold ? ['foreground-callback-hold', 'manual-pause-foreground'] :
    flags.synthetic ? ['synthetic'] : ['frozen', 'manual-pause'];
  return {capabilityOnly: false, syntheticMode: syntheticFlags.length > 0,
    modes, protocols: modes.map(lifecycleCaseProtocol)};
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
  } catch (error) {
    if (error instanceof HarnessFailure) throw error;
    throw new HarnessFailure(code);
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
function lifecyclePhase(state) {
  return {load: state?.load?.state || null, scene: state?.state?.scene || null,
    phase: Number.isInteger(state?.state?.phase) ? state.state.phase : null,
    native_phase: Number.isFinite(Number(state?.native?.phase)) ? Number(state.native.phase) : null,
    running: typeof state?.native?.running === 'boolean' ? state.native.running : null};
}
async function startSession(page, disc, timeout = STARTUP_TIMEOUT_MS) {
  const deadline = Date.now() + timeout;
  const remaining = () => Math.max(1, deadline - Date.now());
  await waitFor(page, () => globalThis.__runtimeLifecycleFixture?.ready === true ||
    globalThis.__runtimeLifecycleFixture?.load?.state === 'error', remaining(), 'fixture_startup_timeout');
  const initial = await fixtureState(page);
  requireValue(initial?.load?.state === 'ready', 'fixture_load_failed');
  await waitFor(page, () => {
    const state = globalThis.__runtimeLifecycleFixture?.player?.getState?.();
    return globalThis.__runtimeLifecycleFixture?.load?.state === 'error' ||
      state?.canImport === true;
  }, remaining(), 'can_import_timeout');
  const chooser = page.waitForEvent('filechooser', {timeout: remaining()});
  await page.locator('#choose-disc').click({timeout: remaining()});
  await (await chooser).setFiles(disc, {timeout: remaining()});
  await waitFor(page, () => {
    const fixture = globalThis.__runtimeLifecycleFixture, state = fixture?.player?.getState?.();
    return fixture?.load?.state === 'error' || state?.scene === 'css' && state?.phase === 1 && state?.running === true;
  }, remaining(), 'scene_startup_timeout');
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
async function runLifecycleCase(browser, fixtureUrl, disc, out, mode,
  caseProtocol = lifecycleCaseProtocol(mode)) {
  const page = await browser.newPage({viewport: {width: 1280, height: 960}});
  const startupOnly = mode === 'startup-only';
  const synthetic = mode === 'synthetic';
  const syntheticHold = mode === 'synthetic-hidden-hold';
  const recoveryExpected = mode === 'synthetic-hidden-hold-recovery';
  const foregroundHold = mode === 'foreground-callback-hold';
  const manualPauseHidden = mode === 'manual-pause-hidden' && caseProtocol === 'synthetic-hidden-hold';
  const manualPauseForeground = mode === 'manual-pause-foreground' && caseProtocol === 'synthetic-visible-hold';
  const hiddenHold = syntheticHold || recoveryExpected;
  const manualPause = (mode === 'manual-pause' && caseProtocol === 'genuine-cdp') ||
    manualPauseHidden || manualPauseForeground;
  const manualPauseSynthetic = manualPauseHidden || manualPauseForeground;
  const syntheticMode = startupOnly || synthetic || hiddenHold || foregroundHold || manualPauseSynthetic;
  const result = {mode, protocol: caseProtocol, dwell_ms: HIDDEN_DWELL_MS, result: 'fail', cdp: [], failure_kind: null,
    startup_timeout_ms: startupOnly ? STARTUP_ONLY_TIMEOUT_MS : STARTUP_TIMEOUT_MS,
    scope: startupOnly ? 'Bounded startup/readiness probe through CSS with native hook installation; no lifecycle or gameplay claim.' :
      recoveryExpected ? 'Controlled Emscripten requestAnimationFrame hold with synthetic document visibility and the expected lifecycle recovery policy; no genuine lifecycle or user-root-cause claim.' :
      syntheticHold ? 'Controlled Emscripten requestAnimationFrame hold with synthetic document visibility getters; no genuine lifecycle or user-root-cause claim.' :
      foregroundHold ? 'Controlled Emscripten requestAnimationFrame hold while document visibility remains visible; no browser lifecycle or user-root-cause claim.' :
      synthetic ? 'Synthetic JS/native input handoff and missed-callback checkpoint only; no browser lifecycle or user-root-cause claim.' :
      manualPause ? (manualPauseHidden ? 'Explicit manual-pause control with the same synthetic hidden callback hold; no browser lifecycle or user-root-cause claim.' :
        manualPauseForeground ? 'Explicit manual-pause control with a visible-page callback hold; no browser lifecycle or user-root-cause claim.' :
        'Explicit manual-pause control through the genuine frozen/active lifecycle protocol; no browser lifecycle or user-root-cause claim.') :
      'Real lifecycle mode is entered only after the installed-Chrome capability probe observes genuine lifecycle transitions.',
    experiment: {
      hypothesis: startupOnly ? 'The audited package reaches running CSS within the five-second readiness bound and installs the native input/unload hooks before start.' :
        recoveryExpected ? 'The fixed lifecycle policy resets both native clocks at the first visible service boundary after a 350 ms synthetic hidden hold, preserving running state and neutralizing then restoring input.' :
        syntheticHold ? 'Holding the identified native main-loop callback for 350 ms yields no native sample during the hold; the first visible callback observes visible input and records bounded simulation debt.' :
        foregroundHold ? 'Holding the native main-loop callback for 350 ms while visibility stays visible preserves the original debt guard and pauses on reason 1.' :
        synthetic ? 'A controlled hidden checkpoint can hand off input ownership and return on an explicit visible checkpoint.' :
        manualPause ? (manualPauseHidden ?
          'An explicit manual pause remains stopped across the same 350 ms synthetic hidden callback hold and resumes only after an explicit resume.' : manualPauseForeground ?
          'An explicit manual pause remains stopped across the same 350 ms visible-page callback hold and resumes only after an explicit resume.' :
          'An explicit manual pause remains stopped across a genuine frozen/active lifecycle control and resumes only after an explicit resume.') :
        'Installed Chrome can expose a genuine lifecycle transition through the supported protocol path.',
      pass_criteria: startupOnly ? ['CSS phase 1 is running', 'native activity and unload hooks are installed',
        'no implicit save or unload'] :
        recoveryExpected ? ['native main-loop rAF callback is identified by an actual sample',
        'zero native samples and source steps during the 350 ms synthetic hold',
        'first visible callback remains running with no unexpected incident',
        'neutral input activity 0,0 precedes actual visible activity before the first post-hold sample',
        'no implicit save or unload', 'one active 32 kHz audio context remains owned'] :
        syntheticHold ? ['native main-loop rAF callback is identified by an actual sample',
        'zero native samples and source steps during the 350 ms synthetic hold',
        'first post-hold sample follows visible event and records reason 1 simulation debt above threshold 8',
        'no automatic resume or implicit save/unload', 'one active 32 kHz audio context remains owned'] :
        foregroundHold ? ['native main-loop rAF callback is identified by an actual sample',
          'document visibility remains visible while zero native samples and source steps occur during the hold',
          'first post-hold sample records reason 1 simulation debt above threshold 8 and running becomes false',
          'no automatic resume or implicit save/unload'] :
        synthetic ? ['hidden and visible input activity are both observed', 'native remains running',
          'no unexpected incident, implicit save, or implicit unload', 'one active 32 kHz audio context remains owned'] :
        manualPause ? (manualPauseHidden ? ['manual pause remains stopped during the 350 ms synthetic hidden callback hold',
          'zero native samples and source steps during the hold', 'no automatic resume or unexpected incident',
          'one active 32 kHz audio context remains owned'] : manualPauseForeground ? ['manual pause remains stopped during the 350 ms visible-page callback hold',
          'zero native samples and source steps during the hold', 'no automatic resume or unexpected incident',
          'one active 32 kHz audio context remains owned'] :
          ['manual pause remains stopped across the genuine frozen/active lifecycle control',
            'no automatic resume or unexpected incident', 'one active 32 kHz audio context remains owned']) :
        ['genuine lifecycle events are observed', 'no unexpected incident or implicit save/unload'],
      fail_criteria: ['unexpected incident', 'manual pause resumes without explicit intent', 'bounded report validation fails'],
      stopping_rule: 'Stop at the first unexpected pause, failure, or missing required receipt; retain the structured receipt and screenshot.',
    }};
  try {
    await installAudioTrace(page);
    const startupTimeout = startupOnly ? STARTUP_ONLY_TIMEOUT_MS : STARTUP_TIMEOUT_MS;
    const response = await page.goto(fixtureUrl, {timeout: startupTimeout});
    requireValue(response?.status() === 200, 'fixture_http_status');
    result.gpu = await page.evaluate(async () => {
      let adapter = null; try { adapter = await navigator.gpu?.requestAdapter(); } catch {}
      return {cross_origin_isolated: crossOriginIsolated === true, webgpu: !!navigator.gpu, adapter: !!adapter,
        max_texture_dimension_2d: Number(adapter?.limits?.maxTextureDimension2D || 0) || null};
    });
    requireValue(result.gpu.cross_origin_isolated && result.gpu.webgpu && result.gpu.adapter, 'gpu_precondition');
    await startSession(page, disc, startupTimeout);
    const hooksReady = await page.evaluate(timeout => globalThis.__runtimeLifecycleFixture?.waitForNativeHooks?.(timeout),
      NATIVE_HOOK_TIMEOUT_MS);
    requireValue(hooksReady === true, 'native_hooks_unavailable');
    const before = await fixtureState(page);
    result.before = {native: before.native, input_activity: before.input_activity.length,
      audio_contexts: before.audio_owner?.contexts?.length || 0,
      unload_calls: before.unload_calls, cache_save_calls: before.cache_save_calls};
    if (startupOnly) {
      result.startup_readiness = {native_hooks: before.native_hooks, native: before.native,
        input_activity: before.input_activity.length};
    } else if (hiddenHold || foregroundHold) {
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
      const hold = await page.evaluate(emitVisibility =>
        globalThis.__runtimeLifecycleFixture.syntheticHiddenHold({emitVisibility}), !foregroundHold);
      result.synthetic_hold = hold;
      if (!hold?.available) {
        result.result = 'unrun';
        result.unrun_reason = hold?.unrun_reason || 'synthetic_main_loop_boundary_unavailable';
        result.state = await fixtureState(page);
        await page.screenshot({path: path.join(out, 'lifecycle-synthetic-hidden-hold-unrun.png'), fullPage: true});
        return result;
      }
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.hasPostHoldNativeSample(),
        SCENE_TIMEOUT_MS, 'synthetic_visible_native_sample_missing');
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
    } else if (manualPause) {
      await page.evaluate(() => {
        globalThis.__runtimeLifecycleFixture.manual_intent.push('manual_pause');
        return globalThis.__runtimeLifecycleFixture.player.pause();
      });
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.player.getState()?.running === false,
        SCENE_TIMEOUT_MS, 'manual_pause_not_stopped');
      if (manualPauseHidden || manualPauseForeground) {
        const hold = await page.evaluate(emitVisibility =>
          globalThis.__runtimeLifecycleFixture.syntheticHiddenHold({emitVisibility}), manualPauseHidden);
        result.manual_hidden_hold = hold;
        requireValue(hold?.available === true && hold.visibility_emitted === manualPauseHidden,
          hold?.unrun_reason || 'manual_hold_protocol_unavailable');
        await waitFor(page, () => globalThis.__runtimeLifecycleFixture.hasPostHoldNativeSample(),
          SCENE_TIMEOUT_MS, 'manual_visible_native_sample_missing');
      } else {
        const cdp = await page.context().newCDPSession(page);
        await cdp.send('Page.setWebLifecycleState', {state: 'frozen'});
        result.cdp.push('frozen');
        await new Promise(resolve => setTimeout(resolve, HIDDEN_DWELL_MS));
        await cdp.send('Page.setWebLifecycleState', {state: 'active'});
        result.cdp.push('active');
        await waitFor(page, () => globalThis.__runtimeLifecycleFixture.browser_events.some(event =>
          event.type === 'resume' || event.type === 'pageshow'), SCENE_TIMEOUT_MS, 'manual_lifecycle_resume_event_missing');
      }
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
    const controlledDebt = hiddenHold && !recoveryExpected ?
      afterLifecycle.incidents.filter(incident => incident.reason_code === 1) : [];
    const unexpected = afterLifecycle.incidents.filter(incident =>
      UNEXPECTED_REASONS.has(incident.reason_code) && !(hiddenHold && !recoveryExpected && incident.reason_code === 1));
    const safeReport = validateDiagnosticReport(afterLifecycle.report);
    result.after_lifecycle = {native: afterLifecycle.native, report: safeReport,
      browser_events: afterLifecycle.browser_events.slice(-12), input_activity: afterLifecycle.input_activity.slice(-12),
      audio: afterLifecycle.audio.slice(-12), audio_owner: afterLifecycle.audio_owner,
      incidents: afterLifecycle.incidents, samples: afterLifecycle.samples.slice(-8),
      native_sample_count: afterLifecycle.native_sample_count, native_source_steps: afterLifecycle.native_source_steps,
      native_hooks: afterLifecycle.native_hooks,
      start_readiness: afterLifecycle.start_readiness, last_error: afterLifecycle.last_error,
      manual_intent: afterLifecycle.manual_intent, synthetic_events: afterLifecycle.synthetic_events,
      synthetic_hold_result: afterLifecycle.synthetic_hold_result,
      unload_calls: afterLifecycle.unload_calls, cache_save_calls: afterLifecycle.cache_save_calls};
    result.candidate_reproduced = hiddenHold && !recoveryExpected ? controlledDebt.length > 0 :
      foregroundHold ? unexpected.some(incident => incident.reason_code === 1) :
      !syntheticMode && mode === 'frozen' && unexpected.length > 0;
    if (startupOnly) {
      requireValue(afterLifecycle.state?.scene === 'css' && afterLifecycle.state?.phase === 1 &&
        afterLifecycle.native.running === true, 'startup_css_not_running');
      requireValue(afterLifecycle.native_hooks?.activity === true && afterLifecycle.native_hooks?.unload === true,
        'startup_native_hooks_missing');
    } else if (manualPause) {
      requireValue(afterLifecycle.native.running === false, 'manual_pause_auto_resumed');
      requireValue(!unexpected.length, 'manual_pause_unexpected_incident');
      requireValue(afterLifecycle.manual_intent.includes('manual_pause'), 'manual_pause_intent_missing');
      if (manualPauseSynthetic) requireValue(result.manual_hidden_hold?.hidden_sample_delta === 0 &&
        result.manual_hidden_hold?.hidden_source_step_delta === 0,
        manualPauseHidden ? 'manual_hidden_native_progress' : 'manual_visible_native_progress');
      requireValue(!afterLifecycle.manual_intent.includes('manual_resume'), 'manual_pause_implicit_resume');
      await page.evaluate(() => {
        globalThis.__runtimeLifecycleFixture.manual_intent.push('manual_resume');
        return globalThis.__runtimeLifecycleFixture.player.resume();
      });
      await waitFor(page, () => globalThis.__runtimeLifecycleFixture.player.getState()?.running === true,
        SCENE_TIMEOUT_MS, 'manual_resume_failed');
      result.manual_recovery = 'explicit_resume_only';
      result.manual_intent = (await fixtureState(page)).manual_intent;
    } else if (recoveryExpected) {
      const hold = afterLifecycle.synthetic_hold_result;
      requireValue(hold?.available === true && hold.visibility_emitted === true, 'synthetic_recovery_hold_unavailable');
      requireValue(hold.hidden_sample_delta === 0 && hold.hidden_source_step_delta === 0,
        'synthetic_recovery_hidden_native_progress');
      requireValue(hold.held_callbacks > 0 && hold.released_callbacks > 0, 'synthetic_recovery_callback_gap_unobserved');
      requireValue(!unexpected.length && afterLifecycle.native.running === true,
        'synthetic_recovery_not_running');
      const hiddenEvent = afterLifecycle.synthetic_events.find(event => event.kind === 'synthetic_hidden_event');
      const visibleEvent = afterLifecycle.synthetic_events.find(event => event.kind === 'synthetic_visible_event');
      const postVisibleSample = afterLifecycle.samples.find(sample => Number(sample.sample_index) >
        Number(hold.before_native_sample_count ?? Infinity) && Number(sample.sequence) > Number(visibleEvent?.sequence ?? Infinity));
      const postVisibleInput = afterLifecycle.input_activity.filter(item =>
        Number(item.sequence) > Number(visibleEvent?.sequence ?? Infinity) &&
        Number(item.sequence) < Number(postVisibleSample?.sequence ?? Infinity));
      const neutralIndex = postVisibleInput.findIndex(item => item.focused === 0 && item.visible === 0);
      const visibleIndex = postVisibleInput.findIndex((item, index) => index > neutralIndex && item.visible === 1);
      requireValue(hiddenEvent && visibleEvent && postVisibleSample && neutralIndex >= 0 && visibleIndex > neutralIndex,
        'synthetic_recovery_input_handoff');
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
      const browserHidden = afterLifecycle.browser_events.find(event => event.type === 'visibilitychange' && event.hidden === true &&
        Number(event.sequence) > Number(hiddenEvent?.sequence ?? Infinity));
      const browserVisible = afterLifecycle.browser_events.find(event => event.type === 'visibilitychange' && event.hidden === false &&
        Number(event.sequence) > Number(visibleEvent?.sequence ?? Infinity));
      const postVisibleSample = afterLifecycle.samples.find(sample => Number(sample.sample_index) >
        Number(hold?.before_native_sample_count ?? Infinity) && Number(sample.sequence) > Number(visibleEvent?.sequence ?? Infinity));
      const postVisibleInput = afterLifecycle.input_activity.find(item => item.visible === 1 &&
        Number(item.sequence) > Number(visibleEvent?.sequence ?? Infinity));
      requireValue(hiddenEvent && visibleEvent && browserHidden && browserVisible &&
        hiddenEvent.sequence < browserHidden.sequence && browserHidden.sequence < visibleEvent.sequence &&
        visibleEvent.sequence < browserVisible.sequence && browserVisible.sequence < postVisibleSample?.sequence &&
        postVisibleSample && postVisibleInput,
        'synthetic_hidden_visible_sample_order');
    } else if (foregroundHold) {
      const hold = afterLifecycle.synthetic_hold_result;
      requireValue(hold?.available === true && hold.visibility_emitted === false,
        'foreground_hold_unavailable');
      requireValue(hold.hidden_sample_delta === 0 && hold.hidden_source_step_delta === 0,
        'foreground_hold_native_progress');
      requireValue(unexpected.some(incident => incident.reason_code === 1 && incident.value > 8 && incident.threshold === 8),
        'foreground_hold_debt_missing');
      requireValue(afterLifecycle.native.running === false, 'foreground_hold_not_paused');
      requireValue(!afterLifecycle.manual_intent.includes('manual_resume'), 'foreground_hold_auto_resume');
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
    requireValue(stable.unload_calls === before.unload_calls &&
      stable.cache_save_calls === before.cache_save_calls, 'lifecycle_saved_implicitly');
    requireValue((stable.audio_owner?.contexts || []).length === 1, 'audio_owner_count');
    const activeContexts = (stable.audio_owner?.contexts || []).filter(context => !context.closed);
    requireValue(activeContexts.length === 1 && activeContexts[0].sample_rate === 32000, 'audio_owner_closed');
    result.input_visibility = {observed_hidden: stable.input_activity.some(item => item.visible === 0),
      observed_visible: stable.input_activity.some(item => item.visible === 1), total: stable.input_activity.length};
    if (!startupOnly) requireValue(result.input_visibility.observed_visible, 'input_visible_not_restored');
    if (synthetic || recoveryExpected || manualPauseHidden)
      requireValue(result.input_visibility.observed_hidden, 'synthetic_hidden_not_observed');
    await page.evaluate(async () => { await globalThis.__runtimeLifecycleFixture.player.unload(); });
    const afterUnload = await fixtureState(page);
    requireValue(afterUnload.unload_calls === before.unload_calls + 1, 'explicit_unload_not_observed');
    result.explicit_unload = {unload_calls: afterUnload.unload_calls, cache_save_calls: afterUnload.cache_save_calls,
      unload_delta: afterUnload.unload_calls - before.unload_calls,
      cache_save_delta: afterUnload.cache_save_calls - before.cache_save_calls};
    await page.screenshot({path: path.join(out, 'lifecycle-' + mode + '.png'), fullPage: true});
    result.result = 'pass';
  } catch (error) {
    result.failure_kind = error?.code || 'browser_error';
    result.state = await fixtureState(page).catch(() => null);
    result.failure_phase = lifecyclePhase(result.state);
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
  'synthetic-hidden-hold': {type: 'boolean'}, 'startup-only': {type: 'boolean'},
  'expect-lifecycle-recovery': {type: 'boolean'}, 'foreground-callback-hold': {type: 'boolean'},
  'capability-only': {type: 'boolean'},
  'fixture-preflight': {type: 'boolean'}, help: {type: 'boolean'},
}});
if (values.help) {
  console.log('Usage: node tests/runtime_lifecycle_incident_browser_test.mjs --site AUDITED_AUDIO_PLAYER --out FRESH_EVIDENCE_DIR [--manifest MANIFEST --disc OWNED_ISO] [--playwright PLAYWRIGHT_DIR] [--synthetic | --synthetic-hidden-hold | --expect-lifecycle-recovery | --foreground-callback-hold | --startup-only | --capability-only] [--fixture-preflight]');
  process.exit(0);
}
if (values['fixture-preflight']) {
  try { console.log(JSON.stringify(await runFixturePreflight())); process.exit(0); }
  catch (error) {
    console.log(JSON.stringify({result: 'fail', failure_kind: 'fixture_preflight_failed',
      error_name: safeHarnessErrorName(error)}));
    process.exit(1);
  }
}
for (const name of ['site', 'out']) requireValue(values[name], name + '_required');
if (!values['capability-only']) {
  for (const name of ['manifest', 'disc']) requireValue(values[name], name + '_required');
}
const modePlan = lifecycleModePlan({capabilityOnly: values['capability-only'], startupOnly: values['startup-only'],
  synthetic: values.synthetic, syntheticHiddenHold: values['synthetic-hidden-hold'],
  expectLifecycleRecovery: values['expect-lifecycle-recovery'], foregroundCallbackHold: values['foreground-callback-hold']});
const syntheticMode = modePlan.syntheticMode;
const report = {schema: 'melee-web-runtime-lifecycle-browser-v1',
  scope: values['capability-only'] ?
    'Bounded installed-Chrome lifecycle capability probe on a blank page. It does not import the game or claim a lifecycle cause.' :
    values['startup-only'] ?
    'Bounded five-second startup/readiness probe through CSS with native hook installation. It does not represent lifecycle, sustained gameplay or a user root cause.' :
    values['expect-lifecycle-recovery'] ?
    'Bounded synthetic hidden callback hold with an expected lifecycle recovery policy; no genuine lifecycle or user-root-cause claim.' :
    values['synthetic-hidden-hold'] ?
    'Bounded synthetic Emscripten requestAnimationFrame main-loop hold with synthetic document visibility getters, manual intent, audio ownership and explicit unload/save boundaries. It does not represent genuine browser lifecycle behavior or a user root cause.' :
    values['foreground-callback-hold'] ?
    'Bounded synthetic visible-page requestAnimationFrame main-loop hold preserving the native debt guard; no browser lifecycle or user-root-cause claim.' :
    values.synthetic ?
    'Bounded synthetic JS/native input handoff and missed-callback checkpoint with manual intent, audio ownership and explicit unload/save boundaries. It does not represent browser background/freeze behavior or a user root cause.' :
    'Bounded installed-Chrome lifecycle capability preflight followed by frozen/active detection with manual pause, source input activity, Web Audio ownership and explicit unload/save boundaries. No foreground, physical-input, audible-output, pixel, PCM-equivalence or sustained-gameplay claim.',
  result: 'fail', checks: [], started_at: new Date().toISOString(),
  bounds: {startup_timeout_ms: STARTUP_TIMEOUT_MS, scene_timeout_ms: SCENE_TIMEOUT_MS,
    startup_only_timeout_ms: STARTUP_ONLY_TIMEOUT_MS, capability_timeout_ms: CAPABILITY_TIMEOUT_MS,
    hidden_dwell_ms: HIDDEN_DWELL_MS}};
let browser, context, server;
try {
  const site = path.resolve(values.site), out = path.resolve(values.out);
  await fs.mkdir(path.dirname(out), {recursive: true}); await fs.mkdir(out, {recursive: false});
  let fixtureHtml = capabilityOnlyFixtureMarkup();
  if (!values['capability-only']) {
    const manifest = JSON.parse(await fs.readFile(path.resolve(values.manifest), 'utf8'));
    const packageInfo = await verifyPackage(site, manifest);
    fixtureHtml = fixtureMarkup(packageInfo.runtimeModule, packageInfo.identity);
    report.identity = packageInfo.identity;
  }
  server = await createServer(site, fixtureHtml, capabilityMarkup());
  report.origin = server.origin;
  const loaded = await loadBrowserTools(values.playwright);
  browser = await loaded.chromium.launch(browserLaunchOptions(loaded.browser, {
    headed: false, audible: false, timeout: values['capability-only'] ? CAPABILITY_TIMEOUT_MS :
      values['startup-only'] ? STARTUP_ONLY_TIMEOUT_MS : STARTUP_TIMEOUT_MS,
  }));
  report.browser = browser.version(); report.browser_mode = 'headless'; report.audio_output = 'muted_by_shared_policy';
  context = await browser.newContext();
  const fixtureUrl = server.origin + '/__runtime-lifecycle-fixture/' + (values['startup-only'] ? '?startup-only=1' : '');
  let skipGame = false;
  if (!syntheticMode) {
    const capability = await runCapabilityProbe(browser, server.origin + '/__runtime-lifecycle-capability/', out);
    report.capability_probe = capability;
    if (values['capability-only']) {
      report.result = capability.result;
      report.game_imported = false;
      if (capability.result !== 'pass') report.unrun_reason = capability.unrun_reason || 'genuine_lifecycle_events_unavailable';
      report.checks.push(capability.result === 'pass' ? 'genuine lifecycle capability observed on blank page' :
        'genuine lifecycle capability unavailable on blank page');
      skipGame = true;
    } else if (capability.failure_kind || !capability.real_lifecycle_supported) {
      report.result = 'unrun'; report.unrun_reason = capability.unrun_reason || 'genuine_lifecycle_events_unavailable';
      report.game_imported = false; skipGame = true;
      report.checks.push('lifecycle capability unavailable; game import skipped');
    }
  }
  if (!skipGame) {
    for (const [index, mode] of modePlan.modes.entries()) {
      const caseProtocol = modePlan.protocols[index];
      const item = await runLifecycleCase(browser, fixtureUrl, path.resolve(values.disc), out, mode, caseProtocol);
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
