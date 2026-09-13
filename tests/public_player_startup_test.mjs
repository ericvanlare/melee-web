/** Execute the real public player shell around a controlled runtime boundary. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const ROOT = new URL('../', import.meta.url);
const SHELL_URL = new URL('web/player/player-shell.mjs', ROOT);

class FakeElement {
  constructor(tagName = 'div', id = '') {
    this.tagName = tagName;
    this.id = id;
    this.children = [];
    this.listeners = new Map();
    this.disabled = false;
    this.hidden = false;
    this.open = false;
    this.checked = id !== 'disc-ack';
    this.value = id === 'keyboard-layout' ? 'two' : '';
    this.textContent = '';
    this.title = '';
    this.files = [];
  }

  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  removeAttribute(name) { if (name === 'value') delete this.value; }
  addEventListener(name, listener) {
    const listeners = this.listeners.get(name) || [];
    listeners.push(listener);
    this.listeners.set(name, listeners);
  }
  showModal() { this.open = true; }
  close() {
    this.open = false;
    for (const listener of this.listeners.get('close') || []) listener();
  }
  click() { return this.onclick?.(); }
  focus() { globalThis.document.activeElement = this; }
  requestFullscreen() { return Promise.resolve(); }
}

function makeDocument() {
  const ids = [
    'canvas', 'player', 'choose-disc', 'disc-file', 'start-game', 'pause-game',
    'controls-open', 'controls-close', 'controls-dialog', 'keyboard-layout',
    'keyboard-one', 'keyboard-two', 'keyboard-two-option', 'keyboard-one-label',
    'keyboard-bindings', 'controllers', 'fullscreen', 'end-session', 'status',
    'progress', 'disc-dialog', 'disc-ack', 'disc-cancel', 'disc-continue', 'error-dialog',
    'error', 'retry', 'error-close',
  ];
  const elements = new Map(ids.map(id => [id, new FakeElement('div', id)]));
  elements.get('canvas').focus = () => { document.activeElement = elements.get('canvas'); };
  elements.get('player').requestFullscreen = () => Promise.resolve();
  elements.get('keyboard-one').checked = true;
  elements.get('keyboard-two').checked = true;
  elements.get('keyboard-layout').disabled = true;

  return {
    hidden: false,
    activeElement: elements.get('canvas'),
    fullscreenEnabled: false,
    fullscreenElement: null,
    getElementById(id) {
      const element = elements.get(id);
      if (!element) throw Error(`Missing test element #${id}`);
      return element;
    },
    createElement(tagName) { return new FakeElement(tagName); },
    addEventListener() {},
    hasFocus: () => true,
    exitFullscreen: async () => {},
    elements,
  };
}

function installGlobals(document) {
  const previous = new Map();
  const values = {
    document,
    window: globalThis,
    navigator: {getGamepads: () => []},
    localStorage: {getItem: () => null, setItem() {}, removeItem() {}},
    location: {reload() {}},
    isSecureContext: true,
    crossOriginIsolated: true,
  };
  for (const [name, value] of Object.entries(values)) {
    previous.set(name, Object.getOwnPropertyDescriptor(globalThis, name));
    Object.defineProperty(globalThis, name, {configurable: true, writable: true, value});
  }
  return () => {
    for (const [name, descriptor] of previous) {
      if (descriptor) Object.defineProperty(globalThis, name, descriptor);
      else delete globalThis[name];
    }
  };
}

async function importShellWithMocks(scenario) {
  const source = await fs.readFile(SHELL_URL, 'utf8');
  const imports = [
    "import {mountMeleeRuntime} from '../melee-runtime.mjs';",
    "import {keyboardRows} from '../prototype-keyboard-layouts.mjs';",
  ].join('\n');
  const replacement = [
    'const mountMeleeRuntime = globalThis.testMountMeleeRuntime;',
    'const keyboardRows = globalThis.testKeyboardRows;',
  ].join('\n');
  assert.notEqual(source.indexOf(imports), -1, 'shell imports must remain source-substitutable');
  const substituted = source.replace(imports, replacement);
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-public-shell-'));
  const shell = path.join(directory, 'player-shell.mjs');
  await fs.writeFile(shell, substituted);
  try {
    await import(`${pathToFileURL(shell).href}?case=${scenario.name}`);
  } finally {
    await fs.rm(directory, {recursive: true, force: true});
  }
}

async function runScenario({name, failMkdir}) {
  const document = makeDocument();
  const restore = installGlobals(document);
  const trace = [];
  const keyboardCalls = [];
  let module;
  let nativeMainCalled = false;
  let seedOpen = false;
  let audioCreated = 0;

  globalThis.testKeyboardRows = (layout, second) => {
    keyboardCalls.push([layout, second]);
    return [['Action', 'Key']];
  };
  globalThis.testMountMeleeRuntime = async options => {
    trace.push('mount');
    assert.equal(options.canvas, document.getElementById('canvas'));
    assert.equal(options.createAudio, undefined, 'public shell must not create an audio runtime');
    if (options.createAudio) audioCreated++;
    assert.equal(typeof options.configureModule, 'function', 'public shell must configure the runtime module');

    const directories = new Set();
    const fsCalls = [];
    const fakeFs = {
      mkdirTree(directoryPath) {
        fsCalls.push(['mkdirTree', directoryPath]);
        trace.push('fs:mkdirTree');
        if (failMkdir) throw Error('cache directory denied');
        directories.add(directoryPath);
      },
      mount(...args) { fsCalls.push(['mount', ...args]); trace.push('fs:mount'); },
      syncfs(...args) { fsCalls.push(['syncfs', ...args]); trace.push('fs:syncfs'); },
    };
    // The loader installs FS after configureModule; only preRun may use it.
    module = {};
    options.configureModule(module);
    assert.deepEqual(fsCalls, [], 'FS must not be touched while configureModule only installs preRun');
    assert.ok(Array.isArray(module.preRun), 'configureModule must install a preRun callback');
    trace.push('preRun');

    module.FS = fakeFs;
    for (const callback of module.preRun) callback(module);
    assert.deepEqual(fsCalls, [['mkdirTree', '/melee-render-cache']],
      'only the volatile cache directory may be created');
    if (failMkdir) throw Error('unreachable: mkdir failure should abort preRun');

    // Aurora opens its writable database in native main, after loader preRun.
    // Data-package callback ordering is outside this shell-level test.
    trace.push('seed-open');
    assert.ok(directories.has('/melee-render-cache'));
    seedOpen = true;
    trace.push('native-main');
    nativeMainCalled = true;
    options.onState({ready: true, requiresReload: false, busy: false, state: 'idle', paused: false,
      canImport: true, canStart: false, canPause: false, canUnload: false, progress: null, message: 'Ready'});
    return {
      setKeyboardLayout: async layout => { trace.push(['layout', layout]); },
      setKeyboard(slot, enabled) { trace.push(['keyboard', slot, enabled]); },
    };
  };

  try {
    await importShellWithMocks({name});
    return {
      document,
      fsCalls: trace.filter(item => typeof item === 'string' && item.startsWith('fs:')),
      trace,
      keyboardCalls,
      module,
      nativeMainCalled,
      seedOpen,
      audioCreated,
    };
  } finally {
    delete globalThis.testMountMeleeRuntime;
    delete globalThis.testKeyboardRows;
    restore();
  }
}

const success = await runScenario({name: 'success', failMkdir: false});
assert.deepEqual(success.fsCalls, ['fs:mkdirTree']);
assert.deepEqual(success.trace.slice(0, 5), ['mount', 'preRun', 'fs:mkdirTree', 'seed-open', 'native-main']);
assert.equal(success.nativeMainCalled, true);
assert.equal(success.seedOpen, true);
assert.equal(success.module.IDBFS, undefined);
assert.equal(success.module.runtimeCacheState, undefined);
assert.equal(success.module.saveRuntimeCache, undefined);
assert.equal(success.module.markRuntimeCacheDirty, undefined);
assert.equal(success.audioCreated, 0);
assert.ok(success.keyboardCalls.length >= 2, 'shell must use the imported keyboard table');

const failed = await runScenario({name: 'mkdir-failure', failMkdir: true});
const failedDocument = failed.document;
assert.equal(failed.nativeMainCalled, false, 'mkdir failure must prevent an unseeded native start');
assert.equal(failed.seedOpen, false, 'seed open must not run after cache-directory failure');
assert.equal(failedDocument.getElementById('error-dialog').open, true);
assert.equal(failedDocument.getElementById('retry').hidden, false);
assert.equal(failedDocument.getElementById('error').textContent, 'cache directory denied');
assert.deepEqual(failed.fsCalls, ['fs:mkdirTree']);
assert.equal(failed.audioCreated, 0);
console.log('Public player shell startup: volatile cache directory ordering, no persistence/audio, and mkdir failure propagation pass.');
