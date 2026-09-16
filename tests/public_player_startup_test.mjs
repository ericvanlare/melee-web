/** Execute the real public player shell around a controlled runtime boundary. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const ROOT = new URL('../', import.meta.url);
const SHELL_URL = new URL('web/player/player-shell.mjs', ROOT);
const shellSource = await fs.readFile(SHELL_URL, 'utf8');
const playerHtml = await fs.readFile(new URL('web/player/index.html', ROOT), 'utf8');
const markupIds = new Set([...playerHtml.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]));
// Exercise the real markup contract before mocks can hide a removed element.
for (const [, id] of shellSource.matchAll(/\$\('([^']+)'\)/g)) {
  assert(markupIds.has(id), `Public shell requires missing HTML element #${id}`);
}

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
    'loading-panel', 'loading-label', 'loading-progress', 'loading-detail',
  ];
  const elements = new Map([...new Set([...markupIds, ...ids])]
    .map(id => [id, new FakeElement('div', id)]));
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
    "import {mountControllerSettings} from '../controller-settings.mjs';",
  ].join('\n');
  const replacement = [
    'const mountMeleeRuntime = globalThis.testMountMeleeRuntime;',
    'const mountControllerSettings = globalThis.testMountControllerSettings;',
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
  let stateCallback;
  let settingsOptions;
  let nativeMainCalled = false;
  let audioCreated = 0;

  globalThis.testMountControllerSettings = options => {
    settingsOptions = options;
    trace.push('settings');
    return {
      setState: next => trace.push(['settings-state', next?.state]),
      bindPlayer: async runtime => { trace.push(['settings-bind', runtime]); },
    };
  };
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
    const idle = {ready: true, requiresReload: false, busy: false, state: 'idle', paused: false,
      canImport: true, canStart: false, canPause: false, canUnload: false, progress: null, loading: null, message: 'Ready'};
    options.onState({...idle, state: 'booting', ready: false, canImport: false,
      loading: {phase: 'engine', message: 'Starting player…', complete: null, total: null}});
    assert.equal(document.getElementById('loading-panel').hidden, false);
    assert.equal(document.getElementById('idle-hint').hidden, true, 'Loading owns the current status');
    assert.equal(document.getElementById('loading-label').textContent, 'Starting player…');
    assert.equal(document.getElementById('loading-progress').value, undefined);
    options.onState({...idle,
      loading: {phase: 'graphics', message: 'Preparing graphics…', complete: 72, total: 120}});
    assert.equal(document.getElementById('loading-detail').textContent, '60% complete');
    assert.equal(document.getElementById('choose-disc').disabled, false, 'Graphics feedback must not block disc selection');
    options.onState({...idle,
      loading: {phase: 'graphics', message: 'Preparing graphics…', complete: 507, total: 508}});
    assert.equal(document.getElementById('loading-detail').textContent, '99% complete', 'Pending work must not round to 100%');
    options.onState({...idle, state: 'error', loading: {message: 'Preparing graphics…'}});
    assert.equal(document.getElementById('loading-panel').hidden, true, 'Errors replace loading feedback');
    options.onState(idle);
    assert.equal(document.getElementById('loading-panel').hidden, true, 'Ready player has no loading overlay');
    assert.equal(document.getElementById('idle-hint').hidden, false, 'Ready player retains the disc instruction');
    return {
      controllers: {
        inspect: () => [], sample: () => [],
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
      settingsOptions,
      stateCallback,
      nativeMainCalled,
      audioCreated,
    };
  } finally {
    delete globalThis.testMountMeleeRuntime;
    delete globalThis.testMountControllerSettings;
    restore();
  }
}

const success = await runScenario({name: 'success', failStartup: false});
assert.deepEqual(success.trace.slice(0, 3), ['settings', 'mount', 'native-main']);
assert.equal(success.nativeMainCalled, true);
assert.equal(success.audioCreated, 0);
assert.equal(success.settingsOptions.disableExtraPorts, true, 'public settings keep developer-only ports disabled');
assert.equal(success.settingsOptions.openButton, success.document.getElementById('controls-open'));
assert.ok(success.trace.some(row => Array.isArray(row) && row[0] === 'settings-bind'), 'shell binds settings after native startup');
const restoreSuccess = installGlobals(success.document);
try {
  success.stateCallback({ready: true, running: true, requiresReload: false, busy: false, state: 'css', paused: false,
    canImport: true, canStart: false, canPause: true, canUnload: true, progress: null, message: 'Running'});
  assert.equal(success.document.getElementById('idle-hint').hidden, true, 'Idle hint disappears once native play starts');
  assert.ok(success.trace.some(row => Array.isArray(row) && row[0] === 'settings-state' && row[1] === 'css'),
    'native state is forwarded to the shared settings component');
} finally { restoreSuccess(); }

const failed = await runScenario({name: 'mkdir-failure', failStartup: true});
const failedDocument = failed.document;
assert.equal(failed.nativeMainCalled, false, 'mkdir failure must prevent an unseeded native start');
assert.equal(failedDocument.getElementById('error-dialog').open, true);
assert.equal(failedDocument.getElementById('retry').hidden, false);
assert.equal(failedDocument.getElementById('error').textContent, 'cache directory denied');
assert.equal(failed.audioCreated, 0);
console.log('Public player shell startup: shared owner startup, no audio, and startup failure propagation pass.');
