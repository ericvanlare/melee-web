/** Lifecycle unit evidence with a controlled native boundary, not gameplay validation. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createRuntimeAudio} from '../web/runtime-audio.mjs';
const withAudio = !process.argv.includes('--silent');
const cacheUnavailable = process.argv.includes('--cache-unavailable');
const failMkdir = process.argv.includes('--mkdir-failure');
const startupCacheDelay = process.argv.includes('--startup-cache-delay');
const startupCacheError = process.argv.includes('--startup-cache-error');
const startupCacheTimeout = process.argv.includes('--startup-cache-timeout');
const invalidCacheService = process.argv.includes('--invalid-cache-service');
const missingCacheService = process.argv.includes('--missing-cache-service');
const adapterRace = process.argv.includes('--adapter-race');
const adapterRetry = process.argv.includes('--adapter-retry');
const adapterTimeoutLate = process.argv.includes('--adapter-timeout-late');
const adapterDeadlineSpan = process.argv.includes('--adapter-deadline-span');
const lifecycleHandoff = process.argv.includes('--lifecycle-handoff');
const diagnosticsRetentionCheckpoint = process.argv.includes('--diagnostics-retention-checkpoint');
const diagnosticsRetentionDestroy = process.argv.includes('--diagnostics-retention-destroy');
const diagnosticsRetentionDenied = process.argv.includes('--diagnostics-retention-denied');
const diagnosticsRetentionStalled = process.argv.includes('--diagnostics-retention-stalled');
const diagnosticsRetentionQuota = process.argv.includes('--diagnostics-retention-quota');
const diagnosticsRetentionResumeCancel = process.argv.includes('--diagnostics-retention-resume-cancel');
const diagnosticsRetentionFollowup = process.argv.includes('--diagnostics-retention-followup');
const diagnosticsRetentionOrdinaryPauseDestroy = process.argv.includes('--diagnostics-retention-ordinary-pause-destroy');
const diagnosticsRetentionHiddenFatal = process.argv.includes('--diagnostics-retention-hidden-fatal');
const diagnosticsRetentionEmptyThenDestroy = process.argv.includes('--diagnostics-retention-empty-destroy');
const diagnosticsRetentionFailedThenDestroy = process.argv.includes('--diagnostics-retention-failed-destroy');
const diagnosticsRetentionMode = diagnosticsRetentionCheckpoint || diagnosticsRetentionDestroy ||
  diagnosticsRetentionDenied || diagnosticsRetentionStalled || diagnosticsRetentionQuota ||
  diagnosticsRetentionResumeCancel || diagnosticsRetentionFollowup || diagnosticsRetentionOrdinaryPauseDestroy ||
  diagnosticsRetentionHiddenFatal ||
  diagnosticsRetentionEmptyThenDestroy ||
  diagnosticsRetentionFailedThenDestroy;
const diagnosticsKnownHost = process.argv.includes('--diagnostics-known-host') || diagnosticsRetentionMode;
const diagnosticIdentity = {schema_version: 1, source_commit: 'a'.repeat(40), runtime_hash: 'b'.repeat(16), build_profile: 'player'};
const diagnosticFetches = [];
const diagnosticStorageRecords = [];
let diagnosticStorageMergeCalls = 0;
let releaseDiagnosticStorageFirst = null;
let diagnosticPreference = 'on';
if (diagnosticsKnownHost) {
  globalThis.location = {origin: 'https://webmelee.gg'};
  globalThis.fetch = async (url, options) => {
    diagnosticFetches.push({url, method: options?.method, body: options?.body});
    return {status: 201};
  };
  globalThis.indexedDB = {open() { throw Object.assign(new Error('denied'), {name: 'NotAllowedError'}); }};
  if (diagnosticsRetentionMode && !diagnosticsRetentionDenied) {
    globalThis.testDiagnosticsStorage = {
      load() { return diagnosticStorageRecords.slice(); },
      merge(records) {
        ++diagnosticStorageMergeCalls;
        if (diagnosticsRetentionStalled) return new Promise(() => {});
        if (diagnosticsRetentionQuota) {
          return Promise.reject(Object.assign(new Error('quota'), {name: 'QuotaExceededError'}));
        }
        if ((diagnosticsRetentionFollowup || diagnosticsRetentionOrdinaryPauseDestroy || diagnosticsRetentionHiddenFatal) &&
            diagnosticStorageMergeCalls === 1) {
          return new Promise(resolve => {
            releaseDiagnosticStorageFirst = () => {
              diagnosticStorageRecords.splice(0, diagnosticStorageRecords.length, ...records);
              resolve({records, bytes: records.length, evictedCount: 0, malformedCount: 0});
            };
          });
        }
        if (diagnosticsRetentionFailedThenDestroy && diagnosticStorageMergeCalls === 1)
          return Promise.reject(Object.assign(new Error('quota'), {name: 'QuotaExceededError'}));
        diagnosticStorageRecords.splice(0, diagnosticStorageRecords.length, ...records);
        return {records, bytes: records.length, evictedCount: 0, malformedCount: 0};
      },
    };
  }
  globalThis.localStorage = {
    getItem() { return diagnosticPreference; },
    setItem(_key, value) { diagnosticPreference = value; },
  };
}
if (cacheUnavailable) await import('../web/runtime-cache.js');
const original = await fs.readFile(new URL('../web/melee-runtime.mjs', import.meta.url), 'utf8');
let source = original.replace(
  "import {loadNativeGameDisc, openNativeGameDiscSession} from './runtime-assets.mjs';",
  'const loadNativeGameDisc = globalThis.testDiscReader; const openNativeGameDiscSession = globalThis.testOpenNativeGameDiscSession;');
if (diagnosticsRetentionMode && !diagnosticsRetentionDenied) {
  source = source.replace(
    'createRuntimeDiagnostics({identity:',
    'createRuntimeDiagnostics({storage: globalThis.testDiagnosticsStorage, identity:');
  assert.match(source, /createRuntimeDiagnostics\(\{storage: globalThis\.testDiagnosticsStorage,/,
    'Retention fixture must install the controlled local storage adapter');
}
let phase = 0, running = false, nextPointer = 16,
  cacheWaits = startupCacheDelay ? 2 : startupCacheTimeout ? Number.MAX_SAFE_INTEGER : 0, audioClosed = false;
let rendererStarted = false, cacheIdleCalls = 0;
let failedFile = null, serviceBatch = 0;
const calls = [], states = [], listeners = new Map();
let adapterRequests = 0, adapterImplementation = () => process.argv.includes('--no-webgpu-adapter') ? null : ({limits: {}});
let resolveDeferredAdapter;
const deferredAdapter = new Promise(resolve => { resolveDeferredAdapter = resolve; });
Object.defineProperty(globalThis, 'navigator', {value: {gpu: {
  requestAdapter: () => { ++adapterRequests; return adapterImplementation(); },
}}, configurable: true});
globalThis.window = globalThis;
globalThis.isSecureContext = true;
globalThis.crossOriginIsolated = true;
globalThis.addEventListener = (name, fn) => { const rows = listeners.get(name) || []; rows.push(fn); listeners.set(name, rows); };
globalThis.removeEventListener = (name, fn) => listeners.set(name, (listeners.get(name) || []).filter(row => row !== fn));
const canvas = {id: 'canvas', focus() { document.activeElement = this; }};
globalThis.document = {hidden: false, activeElement: canvas, hasFocus: () => true,
  createElement: () => ({}), head: {append(loader) { calls.push(['loader', loader.src]); }}};
globalThis.AudioContext = class {
  sampleRate = 32000;
  state = 'suspended';
  audioWorklet = {addModule: async url => { calls.push(['worklet', url]); }};
  destination = {};
  async resume() { calls.push(['audioResume']); this.state = 'running'; }
  async close() { audioClosed = true; }
};
globalThis.AudioWorkletNode = class {
  port = {postMessage: data => {
    calls.push(['audioMessage', data.type, data.enabled]);
    if (data.type === 'state') queueMicrotask(() => this.port.onmessage({data: {type: 'state-ack', enabled: data.enabled}}));
  }};
  connect() {}
  disconnect() {}
};
globalThis.testDiscReader = async (file, report) => {
  calls.push(['readDisc', file.name]);
  if (file.invalid) throw Error('Invalid local disc');
  const entries = file.batch ? Array.from({length: 19}, (_, index) =>
    [`local-${index}.dat`, new Uint8Array([index, index + 1, index + 2])]) :
    file.largeBatch ? Array.from({length: 3}, (_, index) =>
      [`large-${index}.dat`, new Uint8Array(5 * 1024 * 1024).fill(index + 1)]) :
    file.failPut ? [['before-failure.dat', new Uint8Array([7, 8])], ['failure.dat', new Uint8Array([9, 10])],
      ['after-failure.dat', new Uint8Array([11, 12])]] :
    [['local.dat', new Uint8Array([1, 2, 3])]];
  const total = entries.length + 1;
  report({phase: 'validate', complete: 0, total});
  for (let index = 0; index < entries.length; index++) report({phase: 'read', file: entries[index][0], complete: index, total});
  report({phase: 'complete', complete: total, total});
  return new Map(entries);
};
const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-runtime-owner-'));
const sourcePath = path.join(temporary, 'runtime.mjs');
await fs.copyFile(new URL('../web/controller-input.mjs', import.meta.url), path.join(temporary, 'controller-input.mjs'));
for (const module of ['runtime-diagnostics.mjs', 'runtime-diagnostics-delivery.mjs', 'diagnostics-settings.mjs', 'diagnostics-schema.mjs']) {
  await fs.copyFile(new URL(`../web/${module}`, import.meta.url), path.join(temporary, module));
}
await fs.writeFile(sourcePath, source);
const {mountMeleeRuntime} = await import(pathToFileURL(sourcePath));
await fs.rm(temporary, {recursive: true});
if (adapterRace || adapterRetry || adapterTimeoutLate || adapterDeadlineSpan) {
  let createdAudio = 0, owners = 0, configurations = 0, assignedModule = null, moduleAssignments = 0;
  Object.defineProperty(globalThis, 'Module', {configurable: true,
    get: () => assignedModule,
    set: value => { ++moduleAssignments; assignedModule = value; }});
  const options = (startupTimeout = 1000) => ({canvas, openDisc: null, startupTimeout,
    loaderUrl: new URL('http://localhost/runtime/version/gameplay_public.js'),
    createAudio: () => { ++createdAudio; return {setEnabled() {}, fail() {}, destroy() {}}; },
    onOwner: () => { ++owners; }, configureModule: () => { ++configurations; }});
  const effects = () => ({audio: createdAudio, owners, configurations, modules: moduleAssignments,
    loaders: calls.filter(row => row[0] === 'loader').length});
  const assertEffects = expected => assert.deepEqual(effects(), expected,
    'Only the current document reservation may construct or publish runtime owners');
  const readyModule = () => {
    assert.ok(assignedModule, 'The accepted attempt assigns its Emscripten module');
    Object.assign(assignedModule, {
      UTF8ToString: value => value,
      _melee_web_native_menu_phase: () => 0,
      _melee_web_native_menu_running: () => 0,
      _melee_web_native_menu_message: () => 'Ready',
      _melee_web_native_menu_cache_idle: () => 1,
    });
    assignedModule.onRuntimeInitialized();
  };

  if (adapterRace) {
    adapterImplementation = () => deferredAdapter;
    const first = mountMeleeRuntime(options());
    await assert.rejects(mountMeleeRuntime(options()), /Reload the page/,
      'A second mount is rejected while the first adapter request owns the document');
    assertEffects({audio: 0, owners: 0, configurations: 0, modules: 0, loaders: 0});
    resolveDeferredAdapter({limits: {}});
    await new Promise(resolve => setImmediate(resolve));
    assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
    readyModule();
    assert.equal((await first).getState().ready, true);
    console.log('Shared runtime owner: deferred concurrent adapter requests reserve one document owner before audio, Module, or loader setup.');
    process.exit(0);
  }

  if (adapterRetry) {
    adapterImplementation = () => adapterRequests === 1 ? null : adapterRequests === 2 ?
      Promise.reject(Error('controlled adapter rejection')) : ({limits: {}});
    await assert.rejects(mountMeleeRuntime(options()), /No WebGPU adapter is available/);
    assertEffects({audio: 0, owners: 0, configurations: 0, modules: 0, loaders: 0});
    await assert.rejects(mountMeleeRuntime(options()), /controlled adapter rejection/);
    assertEffects({audio: 0, owners: 0, configurations: 0, modules: 0, loaders: 0});
    const retry = mountMeleeRuntime(options());
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(adapterRequests, 3, 'Null and rejected probes each permit a fresh preflight attempt');
    assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
    readyModule();
    assert.equal((await retry).getState().ready, true);
    console.log('Shared runtime owner: null and rejected adapter preflight releases only its reservation and allows retry.');
    process.exit(0);
  }

  const nativeSetTimeout = globalThis.setTimeout;
  const nativeClearTimeout = globalThis.clearTimeout;
  const nativeNow = Date.now;
  const timers = [];
  let clockNow = nativeNow();
  globalThis.setTimeout = (callback, delay) => { const timer = {callback, delay, cleared: false}; timers.push(timer); return timers.length; };
  globalThis.clearTimeout = id => { if (timers[id - 1]) timers[id - 1].cleared = true; };
  try {
    if (adapterDeadlineSpan) Date.now = () => clockNow;
    adapterImplementation = () => deferredAdapter;
    const first = mountMeleeRuntime(options(60000));
    assert.equal(timers.length, 1, 'The startup deadline is armed before awaiting the adapter');
    if (adapterTimeoutLate) {
      timers[0].callback();
      await assert.rejects(first, /Player startup timed out/);
      assertEffects({audio: 0, owners: 0, configurations: 0, modules: 0, loaders: 0});
      adapterImplementation = () => ({limits: {}});
      const retry = mountMeleeRuntime(options(60000));
      await new Promise(resolve => setImmediate(resolve));
      assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
      resolveDeferredAdapter({limits: {late: true}});
      await new Promise(resolve => setImmediate(resolve));
      assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
      readyModule();
      assert.equal((await retry).getState().ready, true);
      console.log('Shared runtime owner: timed-out adapter attempts release safely; late adapter results cannot start a runtime.');
    } else {
      resolveDeferredAdapter({limits: {}});
      clockNow += 10000;
      await new Promise(resolve => setImmediate(resolve));
      assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
      assert.equal(timers.length, 2, 'The same deadline is re-armed for native startup after preflight');
      assert.equal(timers[0].cleared, true, 'Adapter success clears its preflight timer');
      assert.equal(timers[1].delay, 50000, 'Native startup receives only the time remaining after preflight');
      timers[1].callback();
      await assert.rejects(first, /Player startup timed out/);
      assertEffects({audio: 1, owners: 1, configurations: 1, modules: 1, loaders: 1});
      await assert.rejects(mountMeleeRuntime(options()), /Reload the page/,
        'An initialized runtime keeps ownership after startup timeout');
      console.log('Shared runtime owner: one bounded startup deadline covers adapter preflight and native startup.');
    }
  } finally {
    globalThis.setTimeout = nativeSetTimeout;
    globalThis.clearTimeout = nativeClearTimeout;
    Date.now = nativeNow;
  }
  process.exit(0);
}
let owner;
const mounted = mountMeleeRuntime({canvas, openDisc: null, createAudio: withAudio ? options => {
  calls.push(['createAudio']); return createRuntimeAudio(options);
} : undefined, loaderUrl: new URL('http://localhost/runtime/version/gameplay_public.js'),
  diagnosticIdentity: diagnosticsKnownHost ? diagnosticIdentity : undefined,
  configureModule: cacheUnavailable ? module => {
    module.preRun = () => {
      assert.ok(directories.has('/melee-render-cache'), 'Required setup precedes entry callbacks');
      calls.push(['entryPreRun']);
    };
    globalThis.installRuntimeCache(module, event => calls.push(['cacheReport', event]));
  } : undefined,
  onState: state => states.push(state), onOwner: context => { owner = context; },
  startupTimeout: startupCacheTimeout ? 10 : undefined});
if (process.argv.includes('--no-webgpu-adapter')) {
  await assert.rejects(mounted, /No WebGPU adapter is available/);
  assert.equal(calls.some(row => row[0] === 'loader'), false,
    'No adapter is detected before downloading the native player');
  assert.equal(calls.some(row => row[0] === 'createAudio'), false,
    'No adapter is detected before creating the audio owner');
  console.log('Shared runtime owner: missing WebGPU adapter stops before native download or audio setup.');
  process.exit(0);
}
await new Promise(resolve => setImmediate(resolve));
assert.equal(states.at(-1).state, 'booting');
assert.deepEqual(states[0].loading, {phase: 'boot', message: 'Starting player…', complete: 0, total: 0});
assert.equal(states.at(-1).canSelectDisc, true,
  'File selection is available while the native module is starting');
const directories = new Set();
Module.FS = {
  mkdirTree(directory) {
    calls.push(['mkdirTree', directory]);
    if (failMkdir) throw Error('cache directory denied');
    directories.add(directory);
  },
  mount() { assert.fail('Unavailable or disabled persistence must not mount storage'); },
  syncfs() { assert.fail('Unavailable or disabled persistence must not synchronize storage'); },
};
assert.equal(directories.size, 0, 'The loader installs FS after module configuration');
if (failMkdir) {
  assert.throws(() => Module.preRun.forEach(callback => callback(Module)), /cache directory denied/);
  Module.onAbort('cache directory denied');
  await assert.rejects(mounted, /cache directory denied/);
  assert.equal(states.at(-1).canImport, false);
  console.log('Shared runtime owner: required directory failure prevents native initialization.');
  process.exit(0);
}
for (const callback of Module.preRun) callback(Module);
assert.ok(directories.has('/melee-render-cache'), 'Both entries create the required directory before native initialization');
assert.deepEqual(calls.filter(row => row[0] === 'mkdirTree'), [['mkdirTree', '/melee-render-cache']]);
if (cacheUnavailable) {
  assert.equal(Module.runtimeCacheState.state, 'unavailable');
  assert.ok(calls.some(row => row[0] === 'entryPreRun'));
} else {
  assert.equal(Module.runtimeCacheState, undefined, 'Public startup does not install persistence');
}
Object.assign(Module, {
  HEAPU8: new Uint8Array(24 * 1024 * 1024), UTF8ToString: x => x,
  _malloc: size => { const p = nextPointer; nextPointer += size; return p; }, _free(pointer) { calls.push(['free', pointer]); },
  _melee_web_native_menu_phase: () => phase,
  _melee_web_native_menu_running: () => +running,
  _melee_web_native_menu_message: () => running ? 'Running original scene' : 'Ready',
  _melee_web_native_menu_file(np, bp, length) {
    let end = np; while (Module.HEAPU8[end]) end++;
    const name = new TextDecoder().decode(Module.HEAPU8.slice(np, end));
    calls.push(['put', name, length, Module.HEAPU8[bp], Module.HEAPU8[bp + length - 1], serviceBatch]);
    return name === failedFile ? 0 : 1;
  },
  _melee_web_native_menu_prepare() {
    assert.equal(window.menuAudioReadyForPreparation(), true, 'Native preparation must wait for audio acknowledgement');
    calls.push(['prepare']); return 1;
  },
  _melee_web_native_menu_launch() { calls.push(['launch']); phase = 1; running = true; return 1; },
  _melee_web_native_menu_unload() { calls.push(['unload']); phase = 0; running = false; return 1; },
  _melee_web_native_menu_pause(value) { calls.push(['pause', value]); running = !value; /* native API is void */ },
  _melee_web_native_menu_cache_idle: () => {
    ++cacheIdleCalls;
    if (!rendererStarted) return 1;
    return invalidCacheService ? 2 : startupCacheError ? -1 : cacheWaits-- <= 0 ? 1 : 0;
  },
  _melee_web_input_set_keyboard(value) { calls.push(['keyboard', value]); },
  _melee_web_input_set_keyboard_port(port, value) { calls.push(['keyboardPort', port, value]); },
  _melee_web_input_set_activity(focused, visible) { calls.push(['activity', focused, visible]); },
  _melee_web_input_set_keyboard_layout(value) { calls.push(['layout', value]); return 1; },
});
if (missingCacheService) delete Module._melee_web_native_menu_cache_idle;
if (startupCacheDelay) assert.equal(Module._melee_web_native_menu_cache_idle(), 1,
  'The pre-main cache status is not used as startup readiness');
Module.onRuntimeInitialized();
let player = await mounted;
assert.equal(player.getState().ready, true);
await assert.rejects(player.snapshotSaveProfile({baseline: true}), /Select a disc/);
assert.equal(player.getState().requiresReload, false, 'Unavailable baseline export must not enter native code or poison the player');
assert.equal(player.getState().canSelectDisc, true,
  'Disc selection stays available before the first renderer-cache readiness frame');
const cacheCallsBeforeMainFrame = cacheIdleCalls;
assert.equal(cacheCallsBeforeMainFrame, startupCacheDelay ? 1 : 0,
  'Runtime initialization must not poll cache readiness before the main frame');
assert.equal(player.getState().canImport, false,
  'Import remains disabled until the first post-main frame reports cache readiness');
rendererStarted = true;
window.menuFrame(false);
if (startupCacheTimeout) {
  assert.equal(player.getState().canImport, false);
  await new Promise(resolve => setTimeout(resolve, 25));
  assert.equal(player.getState().requiresReload, true, 'Pending startup cache work must fail at the startup deadline');
  assert.match(player.getState().message, /Renderer preparation timed out/);
  console.log('Shared runtime owner: pending startup cache work fails at the bounded readiness deadline.');
  process.exit(0);
} else if (startupCacheDelay) {
  assert.equal(states.at(-1).canImport, false, 'Initial import stays disabled while native cache work is pending');
  assert.equal(states.at(-1).canSelectDisc, true, 'A local File may still be selected while native cache work is pending');
  assert.equal(states.at(-1).graphicsReady, false);
  assert.equal(states.at(-1).loading?.phase, 'catalog');
  await assert.rejects(player.importDisc({name: 'too-early.iso'}), /still preparing/);
  for (let i = 0; !player.getState().canImport && i < 20; i++) {
    window.menuFrame(false);
    await new Promise(resolve => setTimeout(resolve, 1));
  }
  assert.equal(player.getState().canImport, true, 'Startup cache readiness must settle through native frame callbacks');
  phase = 1; running = true;
  cacheWaits = 2;
  window.menuFrame(true);
  assert.equal(player.getState().scene, 'css');
  assert.equal(player.getState().canImport, true, 'Active-scene cache work does not revoke import eligibility');
  phase = 0; running = false;
  window.menuFrame(false);
} else if (missingCacheService) {
  assert.equal(player.getState().requiresReload, true);
  assert.match(player.getState().message, /renderer-cache readiness service is unavailable/);
  assert.equal(states.at(-1).canImport, false);
  console.log('Shared runtime owner: missing required cache readiness service fails explicitly.');
  process.exit(0);
} else if (invalidCacheService) {
  assert.equal(player.getState().requiresReload, true);
  assert.match(player.getState().message, /Invalid native cache idle state/);
  assert.equal(states.at(-1).canImport, false);
  console.log('Shared runtime owner: invalid cache readiness state fails explicitly.');
  process.exit(0);
}
assert.equal(player.getState().canImport, true);
if (diagnosticsKnownHost) {
  const wait = delay => new Promise(resolve => setTimeout(resolve, delay));
  async function pumpBoundary(promise) {
    let settled = false, failure, value;
    promise.then(result => { value = result; settled = true; }, error => { failure = error; settled = true; });
    for (let i = 0; !settled && i < 300; i++) {
      window.menuServiceCommands();
      await wait(1);
    }
    assert.equal(settled, true, 'Owner operation must finish through controlled native boundaries');
    if (failure) throw failure;
    return value;
  }
  const sample = [100, 12, 1, 2, 3, 4, 5, 6, 9, 1, 2, 3, 4, 5, 6, 7, 8, 1];
  phase = 1; running = true; window.menuFrame(true);
  owner.callbacks.menuDiagnosticSample(...sample);
  let incidentId = null;
  if (!diagnosticsRetentionEmptyThenDestroy) {
    incidentId = owner.callbacks.menuDiagnosticIncident(1, 9, 8, 12, 1, 1);
    assert.match(incidentId, /^incident-[0-9]+$/);
  }
  const activeReport = owner.diagnostics.exportReports();
  assert.deepEqual(activeReport.identity, diagnosticIdentity, 'Known HTTPS host keeps caller-provided safe identity');
  assert.equal(activeReport.native.callback_count, 1, 'Native scalar callback crosses the owner boundary');
  assert.equal(activeReport.incidents.length, diagnosticsRetentionEmptyThenDestroy ? 0 : 1,
    'Structured incident trigger crosses the owner boundary');
  if (diagnosticsRetentionMode) {
    const dispatch = type => { for (const listener of listeners.get(type) || []) listener(); };
    phase = 1; running = false; window.menuFrame(false);
    const drainMicrotasks = async count => {
      for (let i = 0; i < count; i++) await Promise.resolve();
    };
    const destroyBeforeDeferredPersistence = async () => {
      let destroySettled = false, destroyFailure = null, destroyedBeforePersist = null;
      const destroyStartedAt = Date.now();
      const destroyPromise = player.destroy().then(value => {
        destroyedBeforePersist = value;
        destroySettled = true;
      }, error => {
        destroyFailure = error;
        destroySettled = true;
      });
      // Drain only native command/microtask boundaries. Do not wait for a
      // timer: the regression observes the owner before deferred persistence.
      for (let i = 0; !destroySettled && i < 64; i++) {
        window.menuServiceCommands();
        await Promise.resolve();
      }
      await destroyPromise;
      if (destroyFailure) throw destroyFailure;
      assert.equal(destroySettled, true, 'Orderly destroy must finish through controlled native boundaries');
      assert.equal(destroyedBeforePersist.requiresReload, true);
      return Date.now() - destroyStartedAt;
    };
    if (diagnosticsRetentionResumeCancel) {
      document.hidden = true;
      dispatch('visibilitychange');
      document.hidden = false;
      dispatch('visibilitychange');
      dispatch('pageshow');
      dispatch('resume');
      await drainMicrotasks(8);
      assert.equal(diagnosticStorageMergeCalls, 0,
        'Hide followed by visible/resume before the checkpoint microtask must not serialize');
      phase = 1; running = true; window.menuFrame(true);
      owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
      await drainMicrotasks(8);
      assert.equal(diagnosticStorageMergeCalls, 0,
        'Active gameplay after a cancelled checkpoint must not serialize diagnostics');
      assert.equal(diagnosticFetches.length, 0, 'Cancelled lifecycle checkpoint never starts delivery');
      console.log('Shared runtime owner: lifecycle checkpoint cancels before visible gameplay and stays off the active path.');
      process.exit(0);
    }
    if (diagnosticsRetentionFollowup) {
      document.hidden = true;
      dispatch('visibilitychange');
      await drainMicrotasks(6);
      assert.equal(diagnosticStorageMergeCalls, 1, 'The first suspended checkpoint must reach storage once');
      assert.equal(typeof releaseDiagnosticStorageFirst, 'function');
      document.hidden = false;
      dispatch('visibilitychange');
      dispatch('pageshow');
      dispatch('resume');
      phase = 1; running = true; window.menuFrame(true);
      owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
      phase = 1; running = false; window.menuFrame(false);
      document.hidden = true;
      dispatch('visibilitychange');
      releaseDiagnosticStorageFirst();
      await drainMicrotasks(24);
      assert.ok(diagnosticStorageMergeCalls >= 2,
        'A later incident after Resume/re-hide gets a fresh storage write');
      assert.equal(diagnosticStorageRecords.length, 2,
        'The follow-up write contains both bounded incidents');
      assert.equal(diagnosticFetches.length, 0, 'Suspended follow-up remains local');
      console.log('Shared runtime owner: delayed lifecycle checkpoint follows a later incident across Resume/re-hide.');
      process.exit(0);
    }
    if (diagnosticsRetentionOrdinaryPauseDestroy) {
      // Let the normal setActive(false) timer begin its older snapshot, then
      // add an incident and destroy.  Destroy must await that write and force
      // a fresh checkpoint instead of reusing the old snapshot.
      await new Promise(resolve => setTimeout(resolve, 0));
      for (let i = 0; diagnosticStorageMergeCalls === 0 && i < 20; i++)
        await new Promise(resolve => setTimeout(resolve, 0));
      assert.equal(diagnosticStorageMergeCalls, 1,
        'ordinary inactive persistence reaches storage before destroy');
      assert.equal(typeof releaseDiagnosticStorageFirst, 'function');
      owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
      let destroySettled = false;
      const destroyStartedAt = Date.now();
      const destroyPromise = player.destroy().then(() => { destroySettled = true; });
      for (let i = 0; !destroySettled && i < 64; i++) {
        window.menuServiceCommands();
        await Promise.resolve();
      }
      assert.equal(destroySettled, false,
        'destroy waits for the already pending ordinary persistence');
      releaseDiagnosticStorageFirst();
      for (let i = 0; !destroySettled && i < 64; i++) {
        window.menuServiceCommands();
        await Promise.resolve();
      }
      await destroyPromise;
      const elapsed = Date.now() - destroyStartedAt;
      assert.ok(elapsed < 1000);
      assert.equal(diagnosticStorageMergeCalls, 2,
        'destroy forces a fresh write after the ordinary persistence settles');
      assert.equal(diagnosticStorageRecords.length, 2,
        'fresh destroy snapshot contains the later incident');
      console.log('Shared runtime owner: destroy refreshes a delayed ordinary inactive snapshot.');
      process.exit(0);
    }
    if (diagnosticsRetentionHiddenFatal) {
      document.hidden = true;
      dispatch('visibilitychange');
      owner.stop(Error('private-user-path-must-not-be-reported'));
      await drainMicrotasks(8);
      assert.equal(diagnosticStorageMergeCalls, 1,
        'hidden fatal incident starts one bounded local checkpoint');
      assert.equal(typeof releaseDiagnosticStorageFirst, 'function');
      releaseDiagnosticStorageFirst();
      await drainMicrotasks(24);
      assert.ok(diagnosticStorageRecords.some(record => record.incident.reason === 'runtime_failure'),
        'fatal incident reaches the hidden checkpoint through the owner wrapper');
      assert.equal(diagnosticFetches.length, 0, 'hidden fatal checkpoint remains local');
      console.log('Shared runtime owner: hidden fatal incident is included in the local checkpoint.');
      process.exit(0);
    }
    if (diagnosticsRetentionEmptyThenDestroy) {
      document.hidden = true;
      dispatch('visibilitychange');
      await drainMicrotasks(8);
      assert.equal(diagnosticStorageMergeCalls, 0, 'Empty checkpoint performs no storage write');
      document.hidden = false;
      dispatch('visibilitychange');
      dispatch('pageshow');
      dispatch('resume');
      phase = 1; running = true; window.menuFrame(true);
      owner.callbacks.menuDiagnosticIncident(1, 9, 8, 12, 1, 1);
      const elapsed = await destroyBeforeDeferredPersistence();
      assert.ok(elapsed < 1000);
      assert.equal(diagnosticStorageRecords.length, 1,
        'A fresh destroy snapshot is not suppressed by an earlier empty checkpoint');
      console.log('Shared runtime owner: empty lifecycle checkpoint does not suppress later destroy persistence.');
      process.exit(0);
    }
    if (diagnosticsRetentionFailedThenDestroy) {
      document.hidden = true;
      dispatch('visibilitychange');
      await drainMicrotasks(8);
      const failedReport = owner.diagnostics.exportReports();
      assert.equal(failedReport.flags.persistence_failed, true,
        'The first lifecycle checkpoint failure is observable');
      document.hidden = false;
      dispatch('visibilitychange');
      dispatch('pageshow');
      dispatch('resume');
      phase = 1; running = true; window.menuFrame(true);
      owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
      const elapsed = await destroyBeforeDeferredPersistence();
      assert.ok(elapsed < 1000);
      assert.equal(diagnosticStorageMergeCalls, 2,
        'Destroy retries a fresh snapshot after a completed checkpoint failure');
      assert.equal(diagnosticStorageRecords.length, 2);
      console.log('Shared runtime owner: failed lifecycle checkpoint does not suppress fresh destroy persistence.');
      process.exit(0);
    }
    if (diagnosticsRetentionCheckpoint || diagnosticsRetentionDenied) {
      // Keep the owner in the same task: the old 0 ms persistence callback has
      // not run yet. Both lifecycle signals must share one local checkpoint.
      document.hidden = true;
      dispatch('visibilitychange');
      dispatch('pagehide');
      dispatch('freeze');
      await drainMicrotasks(8);
      assert.equal(diagnosticFetches.length, 0,
        'Lifecycle checkpoints never start network delivery while hidden');
      if (diagnosticsRetentionDenied) {
        const deniedReport = owner.diagnostics.exportReports();
        assert.equal(deniedReport.flags.storage_denied, true,
          'Denied lifecycle checkpoint is represented without throwing onto the owner');
        console.log('Shared runtime owner: denied lifecycle checkpoint remains local and isolated.');
        process.exit(0);
      }
      assert.equal(diagnosticStorageRecords.length, 1,
        'Pagehide/freeze must persist one incident before deferred persistence runs');
      assert.equal(diagnosticStorageRecords[0].incident.reason, 'simulation_debt');
      console.log('Shared runtime owner: pagehide/freeze checkpoint persists before destroy.');
      process.exit(0);
    }
    const destroyElapsed = await destroyBeforeDeferredPersistence();
    assert.ok(destroyElapsed < 1000,
      'Orderly destroy must remain bounded when local diagnostics storage stalls');
    if (diagnosticsRetentionStalled) {
      assert.equal(diagnosticStorageRecords.length, 0, 'A stalled storage adapter cannot fake a completed checkpoint');
      console.log('Shared runtime owner: stalled destroy checkpoint times out without trapping teardown.');
    } else if (diagnosticsRetentionQuota) {
      const quotaReport = owner.diagnostics.exportReports();
      assert.equal(quotaReport.flags.persistence_failed, true,
        'Quota persistence failure is represented without trapping teardown');
      assert.equal(quotaReport.flags.quota_exceeded, true);
      console.log('Shared runtime owner: quota destroy checkpoint fails explicitly without trapping teardown.');
    } else {
      assert.equal(diagnosticStorageRecords.length, 1,
        'Orderly destroy must persist one incident before disposing delivery');
      assert.equal(diagnosticStorageRecords[0].incident.reason, 'simulation_debt');
      console.log('Shared runtime owner: orderly destroy persists before disposing delivery.');
    }
    process.exit(0);
  }
  await owner.prepareAudio();
  assert.ok(calls.some(row => row[0] === 'audioResume'), 'Audio ownership remains on the runtime owner');
  window.menuPreparation('Controlled preparation', true);
  await wait(40);
  assert.equal(diagnosticFetches.length, 0, 'Active and preparing states never POST diagnostics');
  window.menuPreparationDone();
  phase = 1; running = false; window.menuFrame(false);
  await wait(1200);
  assert.equal(diagnosticFetches.length, 1, 'Inactive integration sends only after the bounded 1,100 ms task');
  assert.equal(diagnosticFetches[0].method, 'POST');
  assert.match(JSON.parse(diagnosticFetches[0].body).incident_id, /^session-[a-z0-9]+:incident-[0-9]+$/,
    'Delivery uses the stable session-bound native incident id');

  if (process.argv.includes('--diagnostics-fatal')) {
    const beforeFatal = diagnosticFetches.length;
    phase = 1; running = true; window.menuFrame(true);
    const dispatch = type => { for (const listener of listeners.get(type) || []) listener(); };
    document.hidden = true; dispatch('visibilitychange');
    owner.stop(Error('private-user-path-must-not-be-reported'));
    await wait(1200);
    assert.equal(diagnosticFetches.length, beforeFatal, 'Hidden fatal incidents remain local');
    document.hidden = false; dispatch('visibilitychange');
    assert.equal(player.getState().requiresReload, true);
    await wait(1200);
    assert.equal(diagnosticFetches.length, beforeFatal + 1,
      'A fatal stopped owner must deliver its sanitized failure without another native frame');
    const report = JSON.parse(diagnosticFetches.at(-1).body);
    assert.equal(report.incident.reason, 'runtime_failure');
    assert.ok(!diagnosticFetches.at(-1).body.includes('private-user-path'));
    console.log('Shared runtime owner: fatal failure delivers sanitized diagnostics while stopped.');
    process.exit(0);
  }

  if (lifecycleHandoff) {
    const dispatch = type => { for (const listener of listeners.get(type) || []) listener(); };
    const beforeLifecycleDelivery = diagnosticFetches.length;
    // Queue a fresh incident while the native scene is paused. The first
    // inactive frame arms the delivery task, which the hidden edge must cancel
    // until the native service boundary consumes the sticky handoff.
    owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
    phase = 1; running = false; window.menuFrame(false);
    assert.equal(player.getState().paused, true, 'The queued incident leaves manual native pause intent intact');
    document.hidden = true; dispatch('visibilitychange');
    document.hidden = false; dispatch('visibilitychange');
    await wait(1200);
    assert.equal(diagnosticFetches.length, beforeLifecycleDelivery,
      'Hidden then visible does not deliver while the native lifecycle handoff is pending');
    assert.equal(window.menuServiceCommands(), 1,
      'The first lifecycle service boundary consumes the handoff exactly once');
    assert.equal(window.menuServiceCommands(), 0,
      'A lifecycle handoff is not replayed without another hidden edge');
    assert.equal(player.getState().paused, true, 'Lifecycle delivery gating does not resume the manually paused scene');
    window.menuFrame(false);
    await wait(1200);
    assert.equal(diagnosticFetches.length, beforeLifecycleDelivery + 1,
      'Deferred delivery becomes eligible only after native handoff and a subsequent frame');
  }

  const beforeImmediateResume = diagnosticFetches.length;
  phase = 1; running = true; window.menuFrame(true);
  owner.callbacks.menuDiagnosticIncident(2, 61, 60, 13, 1, 2);
  phase = 1; running = false; window.menuFrame(false);
  await wait(40);
  phase = 1; running = true; window.menuFrame(true);
  await wait(1200);
  const deliveredBeforeOptOut = diagnosticFetches.length;
  assert.equal(diagnosticFetches.length, beforeImmediateResume, 'Immediate resume cancels deferred delivery');

  owner.callbacks.menuDiagnosticIncident(4, null, null, -1, 1, 0);
  player.setAutomaticDiagnostics(false);
  await wait(40);
  assert.deepEqual(player.getDiagnosticsSettings(), {eligible: true, automatic: false},
    'Opt-out clears future work while known-host eligibility remains available');
  phase = 1; running = false; window.menuFrame(false);
  await wait(1200);
  assert.equal(diagnosticFetches.length, deliveredBeforeOptOut, 'Opt-out prevents queued diagnostics from posting');
  diagnosticPreference = 'off';
  for (const listener of listeners.get('storage') || []) listener({key: 'melee-web-automatic-diagnostics-v1'});
  assert.deepEqual(player.getDiagnosticsSettings(), {eligible: true, automatic: false},
    'Cross-tab storage opt-out is honored by the owner');

  const exported = await player.exportDiagnostics();
  assert.ok(exported && exported.current && exported.retained,
    'Inactive manual export includes current and retained sections despite denied storage');
  assert.ok(exported.current.incidents.length <= 4);
  assert.ok(Array.isArray(exported.retained.records) && exported.retained.records.length <= 4);
  assert.equal(exported.current.flags.storage_denied, true, 'Denied storage is represented as an explicit flag');
  const beforeUnload = calls.filter(row => row[0] === 'unload').length;
  for (let i = 0; !window.menuAudioReadyForPreparation() && i < 20; i++) await wait(10);
  assert.equal(window.menuAudioReadyForPreparation(), true, 'Audio ownership acknowledges inactive preparation before save/unload');
  await pumpBoundary(owner.unloadAndSave());
  assert.equal(calls.filter(row => row[0] === 'unload').length, beforeUnload + 1,
    'Save/unload ownership remains on the native owner boundary');
  document.hidden = true;
  for (const listener of listeners.get('visibilitychange') || []) listener();
  window.menuServiceCommands();
  assert.deepEqual(calls.filter(row => row[0] === 'activity').at(-1), ['activity', 1, 0]);
  document.hidden = false;
  for (const listener of listeners.get('visibilitychange') || []) listener();
  window.menuServiceCommands();
  assert.deepEqual(calls.filter(row => row[0] === 'activity').at(-1), ['activity', 1, 1]);
  console.log('Shared runtime owner: known-host diagnostics identity, scalar incident wiring, inactive delivery delay/cancel, opt-out storage event, denied persistence, audio/input/save ownership pass.');
  process.exit(0);
}
await assert.rejects(player.openDiscSession({name: 'unsupported.iso'}), /no local disc session loader/,
  'the public shell can request validation only through a configured profile adapter');
await assert.rejects(player.importDisc({name: 'forged.iso'}, {preopenedSession: {
  close() {}, async *streamScope() {}, readScope: async () => new Map(),
}}), /not opened by this player/,
  'a structurally plausible session cannot bypass the configured profile loader');
if (startupCacheError) assert.equal(player.getState().canImport, true, 'Native cache error remains optional for import eligibility');
Module.pipelinePreparation = {ready: false, selected: 4, pending: 4};
window.menuFrame(false);
assert.deepEqual(player.getState().loading, {phase: 'catalog', message: 'Preparing graphics…', complete: 0, total: 4});
assert.equal(player.getState().canImport, true, 'Catalog preparation does not block disc selection');
assert.equal(player.getState().canSelectDisc, true);
assert.equal(player.getState().graphicsReady, false, 'Pending pipeline preparation remains an explicit start barrier');
assert.equal(player.getState().canStart, false);
Module.pipelinePreparation.pending = 2;
window.menuFrame(false);
assert.equal(player.getState().loading.complete, 2);
Module.pipelinePreparation.pending = 0;
window.menuFrame(false);
assert.deepEqual(player.getState().loading, {phase: 'catalog', message: 'Preparing graphics…', complete: 0, total: 0});
Module.pipelinePreparation.ready = true;
window.menuFrame(false);
assert.equal(player.getState().loading, null, 'Ready catalog clears startup loading');
assert.equal(player.getState().graphicsReady, true);
assert.equal(player.Module, undefined, 'Native module is not on the public handle');
await assert.rejects(mountMeleeRuntime({canvas}), /Reload the page/);
async function pump(promise) {
  let settled = false, failure, value;
  promise.then(v => { value = v; settled = true; }, e => { failure = e; settled = true; });
  for (let i = 0; !settled && i < 300; i++) {
    window.menuServiceCommands(); window.menuFrame(running);
    await new Promise(resolve => setTimeout(resolve, 1));
  }
  assert.equal(settled, true, 'Operation must finish through controlled native boundaries');
  if (failure) throw failure;
  return value;
}
const nativeServiceCommands = window.menuServiceCommands;
window.menuServiceCommands = () => { ++serviceBatch; return nativeServiceCommands(); };

if (lifecycleHandoff) {
  function dispatch(type) {
    for (const listener of listeners.get(type) || []) listener();
  }
  function activityRowsSince(index) {
    return calls.slice(index).filter(row => row[0] === 'activity').map(row => row.slice(1));
  }
  function assertHandoff(events, expectedVisible, label) {
    const start = calls.length;
    for (const event of events) dispatch(event);
    assert.equal(window.menuServiceCommands(), 1, `${label} is consumed exactly at the native command boundary`);
    assert.deepEqual(activityRowsSince(start).slice(-2), [[0, 0], [1, expectedVisible]],
      `${label} neutralizes native activity before publishing current input`);
    const after = calls.length;
    assert.equal(window.menuServiceCommands(), 0, `${label} handoff is not replayed without another lifecycle event`);
    assert.deepEqual(activityRowsSince(after), [], `${label} does not emit a second clock handoff`);
  }

  phase = 1; running = true; window.menuFrame(true);
  window.menuServiceCommands();
  let baseline = calls.length;
  assert.equal(window.menuServiceCommands(), 0, 'ordinary foreground command service has no clock handoff');
  assert.deepEqual(activityRowsSince(baseline), [], 'ordinary foreground service does not neutralize activity');

  document.hidden = true;
  assertHandoff(['visibilitychange'], 0, 'visibility hidden');

  // A visible callback is allowed to arrive before the native boundary runs.
  // The hidden edge remains sticky until that boundary consumes it; observing
  // the visible edge must not erase the neutralization request.
  const hiddenVisibleStart = calls.length;
  document.hidden = true;
  dispatch('visibilitychange');
  document.hidden = false;
  dispatch('visibilitychange');
  assert.equal(window.menuServiceCommands(), 1,
    'hidden then visible before service still consumes one clock handoff');
  assert.deepEqual(activityRowsSince(hiddenVisibleStart).slice(-2), [[0, 0], [1, 1]],
    'hidden then visible before service neutralizes before current activity');
  const hiddenVisibleAfter = calls.length;
  assert.equal(window.menuServiceCommands(), 0,
    'hidden then visible handoff is consumed only once');
  assert.deepEqual(activityRowsSince(hiddenVisibleAfter), [],
    'hidden then visible does not emit another handoff');

  document.hidden = false;
  assertHandoff(['pagehide', 'pageshow'], 1, 'pagehide/pageshow');
  assertHandoff(['freeze', 'resume'], 1, 'freeze/resume');

  // Foreground focus changes update input activity but do not reset either
  // fixed-tick clock. The native handoff return remains zero.
  document.activeElement = null;
  baseline = calls.length;
  dispatch('blur');
  assert.equal(window.menuServiceCommands(), 0, 'foreground blur does not request a clock handoff');
  assert.deepEqual(activityRowsSince(baseline), [[0, 1]], 'foreground blur publishes current activity only');
  player.focus();
  baseline = calls.length;
  dispatch('focus');
  assert.equal(window.menuServiceCommands(), 0, 'foreground focus does not request a clock handoff');
  assert.deepEqual(activityRowsSince(baseline), [[1, 1]], 'foreground focus publishes current activity only');

  const pauseBoundaryStart = calls.length;
  const audioResumesBeforePause = calls.filter(row => row[0] === 'audioResume').length;
  await pump(player.pause());
  assert.equal(player.getState().paused, true, 'manual pause remains an explicit external state');
  const pauseBoundaryCalls = calls.slice(pauseBoundaryStart);
  assert.equal(pauseBoundaryCalls.some(row => row[0] === 'pause' && row[1] === 1), true,
    'manual pause reaches the native pause boundary in lifecycle mode');
  assert.equal(pauseBoundaryCalls.some(row => row[0] === 'pause' && row[1] === 0), false,
    'manual pause does not implicitly resume the native scene in lifecycle mode');
  assert.equal(pauseBoundaryCalls.some(row => ['unload', 'saveProfile', 'snapshot'].includes(row[0])), false,
    'manual pause does not unload or save the native owner in lifecycle mode');
  assert.equal(calls.filter(row => row[0] === 'audioResume').length, audioResumesBeforePause,
    'manual pause does not resume Web Audio in lifecycle mode');
  await pump(player.resume());
  assert.equal(player.getState().running, true, 'manual resume restores running state in lifecycle mode');

  console.log('Shared runtime owner: hidden/page lifecycle handoff neutralizes once before current activity; foreground focus alone does not reset clocks.');
  process.exit(0);
}
const queuedLayout = player.setKeyboardLayout('boxx');
assert.equal(calls.some(row => row[0] === 'layout'), false);
window.menuServiceCommands();
assert.deepEqual(calls.find(row => row[0] === 'layout'), ['layout', 1], 'Commands drain synchronously before native stepping resumes');
await queuedLayout;
assert.deepEqual(calls.filter(row => row[0] === 'activity').at(-1), ['activity', 1, 1]);
document.hidden = true;
for (const listener of listeners.get('visibilitychange')) listener();
window.menuServiceCommands();
assert.deepEqual(calls.at(-1), ['activity', 1, 0], 'Visibility is a separate native input argument');
document.hidden = false; document.activeElement = null;
for (const listener of listeners.get('focusout')) listener();
window.menuServiceCommands();
assert.deepEqual(calls.at(-1), ['activity', 0, 1], 'Dialog focus disables input without hiding the document');
player.focus(); window.menuServiceCommands();
assert.deepEqual(calls.at(-1), ['activity', 1, 1]);
const ownerPutBegin = calls.length;
await pump(owner.put('owner.dat', new Uint8Array([4, 5, 6])));
assert.deepEqual(calls.slice(ownerPutBegin).filter(row => row[0] === 'put').map(row => row.slice(0, 5)),
  [['put', 'owner.dat', 3, 4, 6]], 'onOwner.put keeps the single-file ownership helper');
await assert.rejects(pump(player.importDisc({name: 'invalid.iso', invalid: true})), /Invalid local disc/);
assert.equal(player.getState().canImport, true, 'Invalid disc allows a new selection');
assert.equal(player.getState().canStart, false);
assert.equal(player.getState().loading, null, 'Invalid disc clears transient loading feedback');
const begin = calls.length;
await pump(player.importDisc({name: 'owned.iso'}));
const importCalls = calls.slice(begin).filter(row => ['unload', 'readDisc', 'put', 'prepare'].includes(row[0])).map(row => row[0]);
assert.deepEqual(importCalls, ['unload', 'readDisc', 'put', 'prepare']);
assert.equal(player.getState().canStart, true);
Module.pipelinePreparation.ready = false;
window.menuFrame(false);
assert.equal(player.getState().canImport, true, 'Graphics work does not revoke the native import gate');
assert.equal(player.getState().canSelectDisc, true, 'A prepared or pending disc can be replaced while graphics are preparing');
assert.equal(player.getState().graphicsReady, false);
assert.equal(player.getState().canStart, false, 'A prepared disc cannot launch before graphics readiness');
Module.pipelinePreparation.ready = true;
window.menuFrame(false);
assert.equal(player.getState().canStart, true, 'Graphics completion releases the start barrier');
const ownedLoading = states.filter(state => state.loading).map(state => [state.loading.phase, state.loading.message]);
assert.ok(ownedLoading.some(([phase]) => phase === 'disc'));
assert.ok(ownedLoading.some(([phase]) => phase === 'handoff'));
assert.ok(ownedLoading.some(([phase]) => phase === 'native'));
assert.ok(states.every(state => !state.loading || state.loading.complete < state.loading.total || state.loading.total === 0),
  'Active loading progress never reports a lingering 100% transfer');
await assert.rejects(pump(player.importDisc({name: 'invalid-replacement.iso', invalid: true})), /Invalid local disc/);
assert.equal(player.getState().canStart, false);
assert.equal(player.getState().canUnload, true, 'A failed replacement still permits Eject of previously imported bytes');
assert.equal(player.getState().loading, null, 'Invalid replacement clears transient loading feedback');
await pump(player.importDisc({name: 'owned.iso'}));
const batchStart = calls.length;
const batchServiceStart = serviceBatch;
await pump(player.importDisc({name: 'batch.iso', batch: true}));
const batchPuts = calls.slice(batchStart).filter(row => row[0] === 'put');
assert.equal(batchPuts.length, 19);
assert.deepEqual(batchPuts.map(row => row[1]), Array.from({length: 19}, (_, index) => `local-${index}.dat`),
  'Batched import keeps native file order');
const batchGroups = new Map();
for (const row of batchPuts) batchGroups.set(row[5], (batchGroups.get(row[5]) || 0) + 1);
assert.ok(batchGroups.size > 1, 'Import uses multiple native boundaries for a larger bundle');
assert.ok([...batchGroups.values()].every(count => count <= 8), 'File-count cap bounds each import boundary');
assert.ok(batchPuts.every(row => row[2] === 3 && row[3] >= 0 && row[4] >= 2));
assert.ok(serviceBatch > batchServiceStart);
const batchFrees = calls.slice(batchStart).filter(row => row[0] === 'free');
assert.equal(batchFrees.length, batchPuts.length * 2, 'Every batched allocation is freed');
const largeStart = calls.length;
await pump(player.importDisc({name: 'large.iso', largeBatch: true}));
const largePuts = calls.slice(largeStart).filter(row => row[0] === 'put');
assert.equal(largePuts.length, 3);
assert.equal(new Set(largePuts.map(row => row[5])).size, 3, 'Byte cap yields a separate batch for each oversized file');
failedFile = 'failure.dat';
const failedStart = calls.length;
await assert.rejects(pump(player.importDisc({name: 'failed.iso', failPut: true})), /Ready/);
const failedPuts = calls.slice(failedStart).filter(row => row[0] === 'put');
assert.deepEqual(failedPuts.map(row => row[1]), ['before-failure.dat', 'failure.dat']);
assert.equal(calls.slice(failedStart).filter(row => row[0] === 'free').length, failedPuts.length * 2,
  'A native transfer error frees the current batch allocations');
failedFile = null;
await pump(player.importDisc({name: 'owned.iso'}));
const staleLaunchStart = calls.length;
await assert.rejects(pump(player.start({isCurrent: () => false})), /Disc selection changed before launch/);
assert.equal(calls.slice(staleLaunchStart).some(row => row[0] === 'launch'), false,
  'A stale selection is rejected after preparation and before native launch');
const startCalls = calls.length;
const starting = player.start();
if (withAudio) {
  assert.ok(calls.slice(startCalls).some(row => row[0] === 'audioResume'),
    'Starting must initiate Web Audio resume before yielding to native preparation');
}
await pump(starting);
if (withAudio) {
  const beforeLaunch = calls.slice(startCalls);
  assert.ok(beforeLaunch.findIndex(row => row[0] === 'audioResume') < beforeLaunch.findIndex(row => row[0] === 'prepare'));
}
assert.equal(player.getState().scene, 'css');
assert.equal(player.getState().canPause, true);
if (withAudio) assert.ok(calls.findIndex(row => row[0] === 'audioResume') < calls.findIndex(row => row[0] === 'launch'));
else {
  assert.equal(calls.some(row => row[0] === 'audioResume'), false);
  assert.equal(player.getState().audio, 'disabled');
  assert.throws(() => window.menuAudio(new Float32Array(2)), /Audio output is disabled/);
}
const pauseBoundaryStart = calls.length;
const audioResumesBeforePause = calls.filter(row => row[0] === 'audioResume').length;
await pump(player.pause()); assert.equal(player.getState().paused, true);
const pauseBoundaryCalls = calls.slice(pauseBoundaryStart);
assert.equal(pauseBoundaryCalls.some(row => row[0] === 'pause' && row[1] === 1), true,
  'Manual pause reaches the native pause boundary');
assert.equal(pauseBoundaryCalls.some(row => row[0] === 'pause' && row[1] === 0), false,
  'Manual pause does not implicitly resume the native scene');
assert.equal(pauseBoundaryCalls.some(row => ['unload', 'saveProfile', 'snapshot'].includes(row[0])), false,
  'Manual pause does not unload or save the native owner');
assert.equal(calls.filter(row => row[0] === 'audioResume').length, audioResumesBeforePause,
  'Manual pause does not resume Web Audio');
await pump(player.resume()); assert.equal(player.getState().running, true);
for (const [menuPhase, menuScene] of [[10, 'title'], [11, 'main']]) {
  phase = menuPhase; running = true; window.menuFrame(true);
  assert.equal(player.getState().scene, menuScene);
  assert.equal(player.getState().canPause, true,
    `The original ${menuScene} scene remains recoverable after a source-clock timing pause`);
  await pump(player.pause()); assert.equal(player.getState().paused, true);
  await pump(player.resume()); assert.equal(player.getState().running, true);
}
phase = 8; running = true; window.menuFrame(true);
assert.equal(player.getState().scene, 'results');
assert.equal(player.getState().canPause, true, 'Results remains an active pausable scene');
assert.equal(player.getState().canStart, false, 'Results cannot start a second route');
await pump(player.pause()); assert.equal(player.getState().paused, true);
await pump(player.resume()); assert.equal(player.getState().running, true);
phase = 9; running = true; window.menuFrame(true);
assert.equal(player.getState().scene, 'prize');
assert.equal(player.getState().canPause, true, 'Prize remains an active pausable scene');
assert.equal(player.getState().canStart, false, 'Prize cannot start a second route');
await pump(player.pause()); assert.equal(player.getState().paused, true);
await pump(player.resume()); assert.equal(player.getState().running, true);
phase = 1; running = true; window.menuFrame(true);
window.menuPreparation('Next source scene', true);
assert.equal(window.menuAudioReadyForPreparation(), true, 'Same-owner transitions preserve continuous audio');
assert.equal(player.getState().canPause, false);
window.menuPreparationDone();
cacheWaits = 2;
await pump(player.unload());
assert.equal(player.getState().running, false);
assert.equal(player.getState().canStart, true, 'Normal unload retains the prepared disc for another native launch');
await pump(player.start());
const destroyed = await pump(player.destroy());
assert.equal(destroyed.requiresReload, true);
assert.equal(audioClosed, withAudio);
assert.equal(player.getState().state, 'destroyed');
assert.equal(player.getState().canImport, false);
await assert.rejects(player.start(), /valid local disc/);
assert.equal(calls.filter(row => row[0] === 'loader').length, 1);
assert.equal(calls.filter(row => row[0] === 'worklet').length, withAudio ? 1 : 0);
console.log('Shared runtime owner: boot, command order, disc retry, audio gates, void pause API, repeat launch and reload-only destruction pass.');
