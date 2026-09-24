/**
 * Controlled owner evidence for the native scoped-asset protocol. This tests
 * the real web/melee-runtime.mjs owner with a native boundary stub; it does
 * not claim source simulation or renderer correctness.
 */
import assert from 'node:assert/strict';
import {mountMeleeRuntime} from '../web/melee-runtime.mjs';

const mode = process.argv[2] || '--lifecycle';
const calls = [];
const states = [];
const listeners = new Map();
let phase = 0;
let running = false;
let nextPointer = 64;
let serviceBatch = 0;
let generation = 0;
let nextGeneration = 0;
let pendingNames = [];
let committed = false;
let commitFailure = mode === '--commit-fail';
let readFailure = mode === '--read-fail';
let putFailure = mode === '--put-fail';
let putError = mode === '--put-fail';
let unloadCalls = 0;
const unloadFailure = mode === '--destroy-unload-fail';
let lateOpenStarted = false;
let lateOpenResolve = null;
let holdRead = false;
let readStarted = false;
let releaseRead = null;
let readCount = 0;
let discClosed = false;

Object.defineProperty(globalThis, 'navigator', {value: {gpu: {}}, configurable: true});
globalThis.window = globalThis;
globalThis.isSecureContext = true;
globalThis.crossOriginIsolated = true;
globalThis.addEventListener = (name, fn) => {
  const rows = listeners.get(name) || [];
  rows.push(fn);
  listeners.set(name, rows);
};
globalThis.removeEventListener = (name, fn) => {
  listeners.set(name, (listeners.get(name) || []).filter(row => row !== fn));
};
const canvas = {id: 'canvas', focus() { document.activeElement = this; }};
globalThis.document = {
  hidden: false,
  activeElement: canvas,
  hasFocus: () => true,
  createElement: () => ({}),
  head: {append(loader) { calls.push(['loader', loader.src]); }},
};

const scopeNames = () => Array.from({length: 19}, (_, index) =>
  `scope-${String(index).padStart(2, '0')}.dat`);

function setRequestedScope(value) {
  generation = value;
  nextGeneration = Math.max(nextGeneration, value);
  pendingNames = scopeNames();
  committed = false;
  calls.push(['asset-request', value]);
}

async function openDisc(file) {
  calls.push(['openDisc', file.name]);
  if (mode === '--late-open-stop' || mode === '--late-open-destroy') {
    lateOpenStarted = true;
    await new Promise(resolve => { lateOpenResolve = resolve; });
  }
  return {
    async readScope(names) {
      ++readCount;
      readStarted = true;
      calls.push(['readScope', [...names]]);
      assert.deepEqual(names, pendingNames, 'browser reads the native descriptor exactly');
      if (readFailure) {
        readFailure = false;
        throw Error('source read failed');
      }
      if (holdRead) await new Promise(resolve => { releaseRead = resolve; });
      return new Map(names.map((name, index) => [name, new Uint8Array([index + 1])]));
    },
    close() {
      if (!discClosed) {
        discClosed = true;
        calls.push(['closeDisc']);
      }
    },
  };
}

function nativeName(pointer) {
  let end = pointer;
  while (Module.HEAPU8[end]) ++end;
  return new TextDecoder().decode(Module.HEAPU8.slice(pointer, end));
}

function installModule(Module) {
  Object.assign(Module, {
    HEAPU8: new Uint8Array(24 * 1024 * 1024),
    UTF8ToString: value => value,
    _malloc(size) { const pointer = nextPointer; nextPointer += size; return pointer; },
    _free(pointer) { calls.push(['free', pointer]); },
    _melee_web_native_menu_phase: () => phase,
    _melee_web_native_menu_running: () => +running,
    _melee_web_native_menu_message: () => commitFailure ? 'asset commit failed' :
      putError ? 'asset put failed' : unloadFailure ? 'native unload failed' : 'Ready',
    _melee_web_native_asset_begin() {
      setRequestedScope(++nextGeneration);
      calls.push(['asset-begin', generation]);
      return generation;
    },
    _melee_web_native_asset_count(value) {
      calls.push(['asset-count', value]);
      return value === generation ? pendingNames.length : 0;
    },
    _melee_web_native_asset_name(value, index) {
      return value === generation ? pendingNames[index] || null : null;
    },
    _melee_web_native_asset_file(value, namePointer, bytesPointer, size) {
      const name = nativeName(namePointer);
      assert.equal(value, generation);
      assert.equal(pendingNames.includes(name), true);
      assert.equal(size, 1);
      calls.push(['asset-file', value, name, serviceBatch,
        Module.HEAPU8[bytesPointer], Module.HEAPU8[bytesPointer + size - 1]]);
      if (putFailure && name === 'scope-03.dat') {
        putFailure = false;
        return 0;
      }
      return 1;
    },
    _melee_web_native_asset_commit(value) {
      assert.equal(value, generation);
      calls.push(['asset-commit', value]);
      if (commitFailure) return 0;
      committed = true;
      generation = 0;
      return 1;
    },
    _melee_web_native_asset_abort(value) {
      calls.push(['asset-abort', value]);
      assert.notEqual(value, 0);
      generation = 0;
      pendingNames = [];
      return 1;
    },
    _melee_web_native_menu_prepare() {
      calls.push(['prepare']);
      assert.equal(committed, true, 'menu preparation follows a committed scope');
      return 1;
    },
    _melee_web_native_menu_launch() {
      calls.push(['launch']);
      phase = 1;
      running = true;
      return 1;
    },
    _melee_web_native_menu_unload() {
      ++unloadCalls;
      calls.push(['unload']);
      phase = 0;
      running = false;
      if (unloadFailure && unloadCalls >= 2) return 0;
      return 1;
    },
    _melee_web_native_menu_pause(value) {
      calls.push(['pause', value]);
      running = !value;
    },
    _melee_web_native_menu_cache_idle: () => 1,
    _melee_web_input_set_keyboard: () => {},
    _melee_web_input_set_keyboard_port: () => {},
    _melee_web_input_set_activity: () => {},
    _melee_web_input_set_keyboard_layout: () => 1,
  });
}

function createTestAudio() {
  return {
    async prepare() { calls.push(['audio-prepare']); },
    async pause() { calls.push(['audio-pause']); },
    readyForPreparation: () => true,
    waitForAck: () => Promise.resolve(),
    setEnabled(value) { calls.push(['audio-enabled', !!value]); },
    fail(error) { calls.push(['audio-fail', String(error?.message || error)]); },
    write() {},
    async destroy() { calls.push(['audio-destroy']); },
  };
}

let owner;
const mounted = mountMeleeRuntime({
  canvas,
  openDisc,
  createAudio: mode === '--destroy-unload-fail' ? createTestAudio : undefined,
  loaderUrl: new URL('http://localhost/runtime/version/gameplay_public.js'),
  onState: state => states.push(state),
  onOwner: context => {
    owner = context;
    installModule(context.Module);
  },
});
owner.Module.onRuntimeInitialized();
// The native cache service is sampled from the first post-main frame. Keep
// this controlled owner on the same boundary as the real browser loop.
window.menuFrame(false);
const player = await mounted;

const nativeServiceCommands = window.menuServiceCommands;
window.menuServiceCommands = () => {
  ++serviceBatch;
  nativeServiceCommands();
};

const delay = () => new Promise(resolve => setTimeout(resolve, 0));
async function pumpUntil(predicate, message) {
  for (let index = 0; index < 400; ++index) {
    window.menuServiceCommands();
    await Promise.resolve();
    if (predicate()) return;
    await delay();
  }
  assert.fail(`${message}; recent=${JSON.stringify(calls.slice(-16))}`);
}

async function settle(promise, message = 'operation did not settle') {
  let done = false;
  let value;
  let failure;
  Promise.resolve(promise).then(result => { value = result; done = true; },
    error => { failure = error; done = true; });
  await pumpUntil(() => done, message);
  if (failure) throw failure;
  return value;
}

function assetFilesSince(index) {
  return calls.slice(index).filter(row => row[0] === 'asset-file');
}

async function lifecycle() {
  generation = 0;
  await settle(player.importDisc({name: 'scoped.iso'}), 'initial scoped import did not settle');
  const initialFiles = assetFilesSince(0);
  assert.equal(initialFiles.length, 19);
  const batches = new Map();
  for (const row of initialFiles) batches.set(row[3], (batches.get(row[3]) || 0) + 1);
  assert.ok(batches.size >= 3, 'bounded transfer uses multiple service boundaries');
  assert.ok([...batches.values()].every(count => count <= 8));
  assert.deepEqual(calls.filter(row => ['asset-commit', 'prepare'].includes(row[0])).map(row => row[0]),
    ['asset-commit', 'prepare']);

  await settle(player.start(), 'initial launch did not settle');
  running = false;
  holdRead = true;
  readStarted = false;
  releaseRead = null;
  window.menuPreparation('Scene assets', false);
  setRequestedScope(2);
  const beforeScene = calls.length;
  window.menuAssetsRequested(2);
  await pumpUntil(() => readStarted, 'scene scope read did not begin');
  assert.equal(player.getState().busy, true);
  assert.equal(calls.slice(beforeScene).some(row => row[0] === 'asset-commit'), false,
    'scene remains uncommitted while the asynchronous read is held');
  assert.equal(calls.slice(beforeScene).some(row => row[0] === 'prepare'), false);
  releaseRead();
  await pumpUntil(() => !player.getState().busy, 'scene scope transfer did not finish');
  holdRead = false;
  assert.equal(calls.slice(beforeScene).some(row => row[0] === 'asset-commit'), true);
  window.menuPreparationDone();

  await settle(player.unload(), 'unload did not settle');
  const readsBeforeRestart = readCount;
  await settle(player.start(), 'restart did not settle');
  assert.equal(readCount, readsBeforeRestart + 1, 'restart rereads the retained disc session');
  assert.equal(calls.filter(row => row[0] === 'asset-begin').length, 2);
  assert.equal(calls.filter(row => row[0] === 'asset-commit').length, 3,
    'initial, scene, and restart scopes each commit');
  await settle(player.destroy(), 'destroy did not settle');
  assert.equal(discClosed, true, 'destroy closes the retained disc File/session');
  console.log('Scoped runtime owner: initial commit, bounded batches, paused async scene handoff, restart reread and final File close pass.');
}

async function duplicateRequest() {
  await settle(player.importDisc({name: 'scoped.iso'}), 'setup import did not settle');
  holdRead = true;
  readStarted = false;
  releaseRead = null;
  running = false;
  setRequestedScope(2);
  window.menuAssetsRequested(2);
  await pumpUntil(() => readStarted, 'first concurrent request did not begin');
  window.menuAssetsRequested(3);
  assert.equal(player.getState().state, 'error');
  assert.match(player.getState().message, /already active|Reload/);
  releaseRead();
  await delay();
  console.log('Scoped runtime owner: duplicate concurrent request fails closed.');
}

async function commitFails() {
  await assert.rejects(settle(player.importDisc({name: 'bad-commit.iso'}), 'failed commit did not settle'),
    /asset commit failed|Ready/);
  const names = calls.map(row => row[0]);
  assert.ok(names.includes('asset-file'));
  assert.ok(names.includes('asset-commit'));
  assert.ok(names.includes('asset-abort'));
  assert.equal(names.includes('prepare'), false, 'failed commit never prepares native menu');
  await settle(player.destroy(), 'destroy after failed commit did not settle');
  console.log('Scoped runtime owner: commit failure aborts staged bytes before preparation.');
}

async function transferFails(kind) {
  const label = kind === 'read' ? 'source read failed' : 'asset put failed';
  const before = calls.length;
  await assert.rejects(settle(player.importDisc({name: `${kind}-failure.iso`}),
    `${kind} failure did not settle`), new RegExp(label));
  const failed = calls.slice(before).map(row => row[0]);
  assert.ok(failed.includes('asset-abort'));
  assert.equal(failed.includes('asset-commit'), false);
  assert.equal(failed.includes('prepare'), false);
  assert.equal(calls.filter(row => row[0] === 'asset-begin').length, 1);

  // The one-shot failure is cleared by the controlled boundary. A retry must
  // begin a fresh generation and reach the ordinary commit/prepare path.
  putError = false;
  await settle(player.importDisc({name: `${kind}-retry.iso`}), `${kind} retry did not settle`);
  const begins = calls.filter(row => row[0] === 'asset-begin').map(row => row[1]);
  assert.deepEqual(begins, [1, 2], 'retry does not reuse an aborted generation');
  assert.equal(calls.filter(row => row[0] === 'prepare').length, 1);
  await settle(player.destroy(), `${kind} failure destroy did not settle`);
  console.log(`Scoped runtime owner: ${kind} failure preserves the original error, aborts, and retries cleanly.`);
}

async function lateOpen(action) {
  const importPromise = player.importDisc({name: `late-${action}.iso`});
  await pumpUntil(() => lateOpenStarted && lateOpenResolve !== null,
    'late open did not reach its controlled pending boundary');
  assert.equal(calls.some(row => row[0] === 'asset-begin'), false);
  assert.equal(calls.some(row => row[0] === 'prepare'), false);
  if (action === 'stop') {
    owner.stop(Error('forced fatal while opening disc'));
    assert.equal(player.getState().state, 'error');
  } else {
    await assert.rejects(player.destroy(), /current player operation/);
    assert.equal(player.getState().state, 'destroyed');
  }
  lateOpenResolve();
  await assert.rejects(settle(importPromise, 'late open did not reject after fatal boundary'),
    /stopped while opening/);
  assert.equal(calls.filter(row => row[0] === 'closeDisc').length, 1,
    'late native session is closed exactly once');
  assert.equal(calls.some(row => row[0] === 'asset-begin'), false);
  assert.equal(calls.some(row => row[0] === 'prepare'), false);
  console.log(`Scoped runtime owner: late open after ${action} closes the session without native preparation.`);
}

async function destroyUnloadFails() {
  await settle(player.importDisc({name: 'unload-failure.iso'}),
    'unload-failure setup import did not settle');
  await assert.rejects(settle(player.destroy(), 'destroy after unload failure did not settle'),
    /native unload failed/);
  assert.equal(player.getState().state, 'destroyed');
  assert.equal(calls.filter(row => row[0] === 'closeDisc').length, 1,
    'destroy closes the retained disc session after unload failure');
  assert.equal(calls.filter(row => row[0] === 'audio-destroy').length, 1,
    'destroy closes audio after unload failure');
  assert.equal(calls.filter(row => row[0] === 'audio-fail').length, 1,
    'unload failure is reported through the audio fatal boundary');
  console.log('Scoped runtime owner: failed native unload preserves the error and still closes File/audio.');
}

if (mode === '--lifecycle') await lifecycle();
else if (mode === '--duplicate') await duplicateRequest();
else if (mode === '--commit-fail') await commitFails();
else if (mode === '--read-fail') await transferFails('read');
else if (mode === '--put-fail') await transferFails('put');
else if (mode === '--late-open-stop') await lateOpen('stop');
else if (mode === '--late-open-destroy') await lateOpen('destroy');
else if (mode === '--destroy-unload-fail') await destroyUnloadFails();
else throw Error(`Unknown scoped-owner mode: ${mode}`);
