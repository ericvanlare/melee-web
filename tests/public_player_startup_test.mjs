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
  querySelector() { return null; }
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
    'player-one-source', 'player-two-source', 'player-one-source-status',
    'player-two-source-status', 'boxx-source-note', 'keyboard-bindings-details',
    'keyboard-bindings', 'controller-advanced', 'controllers', 'idle-hint', 'fullscreen', 'end-session', 'status',
    'progress', 'disc-dialog', 'disc-ack', 'disc-cancel', 'disc-continue', 'error-dialog',
    'error', 'retry', 'error-close',
  ];
  const elements = new Map(ids.map(id => [id, new FakeElement('div', id)]));
  elements.get('canvas').focus = () => { document.activeElement = elements.get('canvas'); };
  elements.get('player').requestFullscreen = () => Promise.resolve();
  elements.get('player-one-source').value = 'auto';
  elements.get('player-two-source').value = 'auto';
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
    setInterval: () => 0,
    addEventListener: () => {},
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
    "import {mountControllerPanel} from '../controller-panel.mjs';",
  ].join('\n');
  const replacement = [
    'const mountMeleeRuntime = globalThis.testMountMeleeRuntime;',
    'const keyboardRows = globalThis.testKeyboardRows;',
    'const mountControllerPanel = globalThis.testMountControllerPanel;',
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

async function runScenario({name, failStartup}) {
  const document = makeDocument();
  const restore = installGlobals(document);
  const trace = [];
  const keyboardCalls = [];
  const sourceCalls = [];
  const panelCalls = [];
  const modes = ['auto', 'auto', 'off', 'off'];
  let stateCallback;
  let nativeMainCalled = false;
  let audioCreated = 0;

  globalThis.testKeyboardRows = (layout, second) => {
    keyboardCalls.push([layout, second]);
    return [['Action', 'Key']];
  };
  globalThis.testMountControllerPanel = () => { panelCalls.push('mount'); return () => panelCalls.push('cleanup'); };
  globalThis.testMountMeleeRuntime = async options => {
    trace.push('mount');
    stateCallback = options.onState;
    assert.equal(options.canvas, document.getElementById('canvas'));
    assert.equal(options.createAudio, undefined, 'public shell must not create an audio runtime');
    if (options.createAudio) audioCreated++;
    assert.equal(options.configureModule, undefined, 'required filesystem setup belongs to the shared owner');
    if (failStartup) throw Error('cache directory denied');
    trace.push('native-main');
    nativeMainCalled = true;
    options.onState({ready: true, requiresReload: false, busy: false, state: 'idle', paused: false,
      canImport: true, canStart: false, canPause: false, canUnload: false, progress: null, message: 'Ready'});
    return {
      controllers: {
        inspect: () => [], sample: () => [],
        getPortSource: port => modes[port],
        setPortSource: (port, mode) => { sourceCalls.push([port, mode]); modes[port] = mode; },
        setTesting(value) { trace.push(['testing', value]); },
      },
      setKeyboardLayout: async layout => { trace.push(['layout', layout]); },
      setKeyboard(slot, enabled) { trace.push(['keyboard', slot, enabled]); },
    };
  };

  try {
    await importShellWithMocks({name});
    return {
      document,
      trace,
      keyboardCalls,
      sourceCalls,
      panelCalls,
      stateCallback,
      nativeMainCalled,
      audioCreated,
    };
  } finally {
    delete globalThis.testMountMeleeRuntime;
    delete globalThis.testKeyboardRows;
    delete globalThis.testMountControllerPanel;
    restore();
  }
}

const success = await runScenario({name: 'success', failStartup: false});
assert.deepEqual(success.trace.slice(0, 2), ['mount', 'native-main']);
assert.equal(success.nativeMainCalled, true);
assert.equal(success.audioCreated, 0);
assert.ok(success.keyboardCalls.length >= 2, 'shell must use the imported keyboard table');
const restoreSuccess = installGlobals(success.document);
try {
  assert.deepEqual(success.sourceCalls.slice(0, 4), [[0, 'auto'], [1, 'auto'], [2, 'off'], [3, 'off']],
    'public startup must configure both visible ports and keep developer-only ports off');
  assert.deepEqual(success.trace.filter(row => Array.isArray(row) && row[0] === 'keyboard').slice(-2),
    [['keyboard', 0, true], ['keyboard', 1, true]], 'Auto keeps keyboard fallback enabled for both players');
  const p1 = success.document.getElementById('player-one-source');
  p1.value = 'keyboard'; p1.onchange();
  assert.deepEqual(success.sourceCalls.slice(-4), [[0, 'keyboard'], [1, 'auto'], [2, 'off'], [3, 'off']], 'P1 source changes reach the manager');
  assert.deepEqual(success.trace.filter(row => Array.isArray(row) && row[0] === 'keyboard').at(-2), ['keyboard', 0, true],
    'Keyboard source enables the native keyboard fallback');
  const advanced = success.document.getElementById('controller-advanced');
  advanced.open = true; advanced.listeners.get('toggle')[0]();
  assert.deepEqual(success.panelCalls, ['mount'], 'Advanced controller UI mounts only after expansion');
  advanced.open = false; advanced.listeners.get('toggle')[0]();
  assert.deepEqual(success.panelCalls, ['mount', 'cleanup'], 'Collapsing advanced controls releases the panel');
  success.stateCallback({ready: true, running: true, requiresReload: false, busy: false, state: 'css', paused: false,
    canImport: true, canStart: false, canPause: true, canUnload: true, progress: null, message: 'Running'});
  assert.equal(success.document.getElementById('idle-hint').hidden, true, 'Idle hint disappears once native play starts');
} finally { restoreSuccess(); }

const failed = await runScenario({name: 'mkdir-failure', failStartup: true});
const failedDocument = failed.document;
assert.equal(failed.nativeMainCalled, false, 'mkdir failure must prevent an unseeded native start');
assert.equal(failedDocument.getElementById('error-dialog').open, true);
assert.equal(failedDocument.getElementById('retry').hidden, false);
assert.equal(failedDocument.getElementById('error').textContent, 'cache directory denied');
assert.equal(failed.audioCreated, 0);
console.log('Public player shell startup: shared owner startup, no audio, and startup failure propagation pass.');
