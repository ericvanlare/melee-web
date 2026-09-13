/** Lifecycle unit evidence with a controlled native boundary, not gameplay validation. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const original = await fs.readFile(new URL('../web/melee-runtime.mjs', import.meta.url), 'utf8');
const source = original.replace("import {loadNativeGameDisc} from './runtime-assets.mjs';", 'const loadNativeGameDisc = globalThis.testDiscReader;');
let phase = 0, running = false, nextPointer = 16, cacheWaits = 0, audioClosed = false;
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
  report({complete: 0, total: 1});
  return new Map([['local.dat', new Uint8Array([1, 2, 3])]]);
};
const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-runtime-owner-'));
const sourcePath = path.join(temporary, 'runtime.mjs');
await fs.writeFile(sourcePath, source);
const {mountMeleeRuntime} = await import(pathToFileURL(sourcePath));
await fs.rm(temporary, {recursive: true});
let owner;
const mounted = mountMeleeRuntime({canvas, loaderUrl: new URL('http://localhost/runtime/version/gameplay_public.js'),
  onState: state => states.push(state), onOwner: context => { owner = context; }});
assert.equal(states.at(-1).state, 'booting');
Object.assign(Module, {
  HEAPU8: new Uint8Array(1024), UTF8ToString: x => x,
  _malloc: size => { const p = nextPointer; nextPointer += size; return p; }, _free() {},
  _melee_web_native_menu_phase: () => phase,
  _melee_web_native_menu_running: () => +running,
  _melee_web_native_menu_message: () => running ? 'Running original scene' : 'Ready',
  _melee_web_native_menu_file() { calls.push(['put']); return 1; },
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
await assert.rejects(pump(player.importDisc({name: 'invalid.iso', invalid: true})), /Invalid local disc/);
assert.equal(player.getState().canImport, true, 'Invalid disc allows a new selection');
assert.equal(player.getState().canStart, false);
const begin = calls.length;
await pump(player.importDisc({name: 'owned.iso'}));
const importCalls = calls.slice(begin).filter(row => ['unload', 'readDisc', 'put', 'prepare'].includes(row[0])).map(row => row[0]);
assert.deepEqual(importCalls, ['unload', 'readDisc', 'put', 'prepare']);
assert.equal(player.getState().canStart, true);
await assert.rejects(pump(player.importDisc({name: 'invalid-replacement.iso', invalid: true})), /Invalid local disc/);
assert.equal(player.getState().canStart, false);
assert.equal(player.getState().canUnload, true, 'A failed replacement still permits Eject of previously imported bytes');
await pump(player.importDisc({name: 'owned.iso'}));
await pump(player.start());
assert.equal(player.getState().scene, 'css');
assert.equal(player.getState().canPause, true);
assert.ok(calls.findIndex(row => row[0] === 'audioResume') < calls.findIndex(row => row[0] === 'launch'));
await pump(player.pause()); assert.equal(player.getState().paused, true);
await pump(player.resume()); assert.equal(player.getState().running, true);
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
assert.equal(audioClosed, true);
assert.equal(player.getState().state, 'destroyed');
assert.equal(player.getState().canImport, false);
await assert.rejects(player.start(), /valid local disc/);
assert.equal(calls.filter(row => row[0] === 'loader').length, 1);
assert.equal(calls.filter(row => row[0] === 'worklet').length, 1);
console.log('Shared runtime owner: boot, command order, disc retry, audio gates, void pause API, repeat launch and reload-only destruction pass.');
