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
if (cacheUnavailable) await import('../web/runtime-cache.js');
const original = await fs.readFile(new URL('../web/melee-runtime.mjs', import.meta.url), 'utf8');
const source = original.replace("import {loadNativeGameDisc} from './runtime-assets.mjs';", 'const loadNativeGameDisc = globalThis.testDiscReader;');
let phase = 0, running = false, nextPointer = 16, cacheWaits = 0, audioClosed = false;
let failedFile = null, serviceBatch = 0;
const calls = [], states = [], listeners = new Map();
Object.defineProperty(globalThis, 'navigator', {value: {gpu: {}}, configurable: true});
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
  audioWorklet = {addModule: async url => { calls.push(['worklet', url]); }};
  destination = {};
  async resume() { calls.push(['audioResume']); }
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
await fs.writeFile(sourcePath, source);
const {mountMeleeRuntime} = await import(pathToFileURL(sourcePath));
await fs.rm(temporary, {recursive: true});
let owner;
const mounted = mountMeleeRuntime({canvas, createAudio: withAudio ? createRuntimeAudio : undefined, loaderUrl: new URL('http://localhost/runtime/version/gameplay_public.js'),
  configureModule: cacheUnavailable ? module => {
    module.preRun = () => {
      assert.ok(directories.has('/melee-render-cache'), 'Required setup precedes entry callbacks');
      calls.push(['entryPreRun']);
    };
    globalThis.installRuntimeCache(module, event => calls.push(['cacheReport', event]));
  } : undefined,
  onState: state => states.push(state), onOwner: context => { owner = context; }});
assert.equal(states.at(-1).state, 'booting');
assert.deepEqual(states[0].loading, {phase: 'boot', message: 'Starting player…', complete: 0, total: 0});
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
  _melee_web_native_menu_cache_idle: () => cacheWaits-- <= 0 ? 1 : 0,
  _melee_web_input_set_keyboard(value) { calls.push(['keyboard', value]); },
  _melee_web_input_set_keyboard_port(port, value) { calls.push(['keyboardPort', port, value]); },
  _melee_web_input_set_activity(focused, visible) { calls.push(['activity', focused, visible]); },
  _melee_web_input_set_keyboard_layout(value) { calls.push(['layout', value]); return 1; },
});
Module.onRuntimeInitialized();
const player = await mounted;
assert.equal(player.getState().canImport, true);
Module.pipelinePreparation = {ready: false, selected: 4, pending: 4};
window.menuFrame(false);
assert.deepEqual(player.getState().loading, {phase: 'catalog', message: 'Preparing graphics…', complete: 0, total: 4});
assert.equal(player.getState().canImport, true, 'Catalog preparation does not block disc selection');
Module.pipelinePreparation.pending = 2;
window.menuFrame(false);
assert.equal(player.getState().loading.complete, 2);
Module.pipelinePreparation.pending = 0;
window.menuFrame(false);
assert.deepEqual(player.getState().loading, {phase: 'catalog', message: 'Preparing graphics…', complete: 0, total: 0});
Module.pipelinePreparation.ready = true;
window.menuFrame(false);
assert.equal(player.getState().loading, null, 'Ready catalog clears startup loading');
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
window.menuServiceCommands = () => { ++serviceBatch; nativeServiceCommands(); };
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
await pump(player.start());
assert.equal(player.getState().scene, 'css');
assert.equal(player.getState().canPause, true);
if (withAudio) assert.ok(calls.findIndex(row => row[0] === 'audioResume') < calls.findIndex(row => row[0] === 'launch'));
else {
  assert.equal(calls.some(row => row[0] === 'audioResume'), false);
  assert.equal(player.getState().audio, 'disabled');
  assert.throws(() => window.menuAudio(new Float32Array(2)), /Audio output is disabled/);
}
await pump(player.pause()); assert.equal(player.getState().paused, true);
await pump(player.resume()); assert.equal(player.getState().running, true);
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
