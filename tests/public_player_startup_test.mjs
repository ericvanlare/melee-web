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
assert(!markupIds.has('disc-ack'), 'The player must not render a mandatory acknowledgement checkbox');
assert(!shellSource.includes('disc-ack'), 'The player must not gate import on hidden acknowledgement state');
for (const href of ['/terms', '/privacy', '/notices', '/copyright']) {
  assert(playerHtml.includes(`href="${href}"`), `The preselection notice must link ${href}`);
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
    this.value = id === 'keyboard-layout' ? 'two' : '';
    this.textContent = '';
    this.title = '';
    this.files = [];
    this.clickCount = 0;
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
  click() { this.clickCount++; globalThis.testClickTrace?.push(this.id); return this.onclick?.(); }
  dispatchEvent(event) {
    for (const listener of this.listeners.get(event.type) || []) listener(event);
  }
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
  'progress', 'disc-dialog', 'disc-cancel', 'disc-choose-file', 'error-dialog',
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
    "import {openNativeGameDiscSession} from '../runtime-assets.mjs';",
  ].join('\n');
  const replacement = [
    'const mountMeleeRuntime = globalThis.testMountMeleeRuntime;',
    'const mountControllerSettings = globalThis.testMountControllerSettings;',
    'const openNativeGameDiscSession = globalThis.testOpenNativeGameDiscSession;',
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

async function runScenario({name, failStartup = false, behavior = {}}) {
  const document = makeDocument();
  const restore = installGlobals(document);
  const trace = [];
  globalThis.testClickTrace = trace;
  let stateCallback;
  let settingsOptions;
  let nativeMainCalled = false;
  let audioCreated = 0;
  const idle = {ready: true, requiresReload: false, busy: false, state: 'idle', paused: false,
    canImport: true, canStart: false, canPause: false, canUnload: false, audio: behavior.audio ? 'enabled' : 'disabled',
    progress: null, loading: null, message: 'Ready'};
  let mockState = idle;
  let player;

  globalThis.testMountControllerSettings = options => {
    settingsOptions = options;
    trace.push('settings');
    return {
      setState: next => trace.push(['settings-state', next?.state]),
      bindPlayer: async runtime => { trace.push(['settings-bind', runtime]); },
    };
  };
  globalThis.testOpenNativeGameDiscSession = async () => {
    throw Error('public startup test must not open a disc during mount');
  };
  globalThis.testMountMeleeRuntime = async options => {
    trace.push('mount');
    stateCallback = options.onState;
    assert.equal(options.canvas, document.getElementById('canvas'));
    assert.equal(options.openDisc, globalThis.testOpenNativeGameDiscSession,
      'public shell must provide the audio-free scoped disc opener');
    assert.equal(options.createAudio, undefined, 'public shell must not create an audio runtime');
    if (options.createAudio) audioCreated++;
    assert.equal(options.configureModule, undefined, 'required filesystem setup belongs to the shared owner');
    if (failStartup) throw Error('cache directory denied');
    trace.push('native-main');
    nativeMainCalled = true;
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
    mockState = idle;
    assert.equal(document.getElementById('loading-panel').hidden, true, 'Ready player has no loading overlay');
    assert.equal(document.getElementById('idle-hint').hidden, false, 'Ready player retains the disc instruction');
    player = {
      controllers: {
        inspect: () => [], sample: () => [],
      },
      setKeyboardLayout: async layout => { trace.push(['layout', layout]); },
      setKeyboard(slot, enabled) { trace.push(['keyboard', slot, enabled]); },
      getState: () => mockState,
      focus() { trace.push('focus'); },
      activateAudio() { trace.push('audio-activate'); return behavior.activateAudio?.() ?? Promise.resolve(); },
      importDisc(file) {
        trace.push(['import', file.name]);
        const complete = () => { mockState = {...idle, canStart: true, canUnload: true}; stateCallback(mockState); };
        const imported = behavior.importDisc ? behavior.importDisc(file) : Promise.resolve();
        return Promise.resolve(imported).then(complete);
      },
      start() {
        trace.push('start');
        if (behavior.start) return behavior.start();
        mockState = {...mockState, state: 'css', canStart: false, canPause: true, running: true};
        stateCallback(mockState);
        return Promise.resolve();
      },
      destroy() { trace.push('destroy'); return Promise.resolve({requiresReload: true}); },
    };
    return player;
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
      player,
    };
  } finally {
    delete globalThis.testMountMeleeRuntime;
    delete globalThis.testMountControllerSettings;
    delete globalThis.testOpenNativeGameDiscSession;
    delete globalThis.testClickTrace;
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

async function selectThroughShell(scenario, file = {name: 'owned.iso'}) {
  globalThis.testClickTrace = scenario.trace;
  scenario.document.getElementById('choose-disc').click();
  assert.equal(scenario.document.getElementById('disc-dialog').open, true,
    'Disc opens the disclosure before the file picker');
  scenario.document.getElementById('disc-choose-file').click();
  const input = scenario.document.getElementById('disc-file');
  assert.equal(input.clickCount, 1, 'Choose file opens the picker with no checkbox gate');
  input.files = [file];
  return input.onchange();
}

const cancellation = await runScenario({name: 'picker-cancellation'});
const restoreCancellation = installGlobals(cancellation.document);
try {
  globalThis.testClickTrace = cancellation.trace;
  cancellation.document.getElementById('choose-disc').click();
  cancellation.document.getElementById('disc-choose-file').click();
  cancellation.document.getElementById('disc-file').dispatchEvent({type: 'cancel'});
  assert.equal(cancellation.document.getElementById('disc-dialog').open, false);
  assert.equal(cancellation.document.activeElement.id, 'choose-disc');
  assert.equal(cancellation.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 0,
    'Cancelling the browser file picker does not import or launch');
  assert.equal(cancellation.trace.filter(row => row === 'start').length, 0);
} finally { delete globalThis.testClickTrace; restoreCancellation(); }

const flow = await runScenario({name: 'automatic-launch'});
const restoreFlow = installGlobals(flow.document);
try {
  await selectThroughShell(flow);
  assert.equal(flow.trace.filter(row => row === 'start').length, 1,
    'A successfully prepared selection automatically starts exactly once');
  assert(flow.trace.indexOf('audio-activate') < flow.trace.indexOf('disc-file'),
    'Audio activation starts during the Choose file gesture before opening the native picker');
  assert(flow.document.getElementById('start-game').disabled,
    'The normal first launch does not require the Play button');
  const importCount = flow.trace.filter(row => Array.isArray(row) && row[0] === 'import').length;
  assert.equal(flow.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, importCount);
} finally { delete globalThis.testClickTrace; restoreFlow(); }

const preparationFailure = await runScenario({name: 'preparation-failure', behavior: {
  importDisc: async () => { throw Error('Native asset preparation failed'); },
}});
const restorePreparationFailure = installGlobals(preparationFailure.document);
try {
  await selectThroughShell(preparationFailure);
  assert.equal(preparationFailure.document.getElementById('error-dialog').open, true);
  assert.equal(preparationFailure.document.getElementById('error').textContent, 'Native asset preparation failed');
  assert.equal(preparationFailure.trace.filter(row => row === 'start').length, 0,
    'A preparation failure never calls launch');
} finally { delete globalThis.testClickTrace; restorePreparationFailure(); }

let recoveryAttempts = 0;
const recovery = await runScenario({name: 'audio-recovery', behavior: {audio: true,
  activateAudio: async () => { throw Error('The browser kept game audio suspended. Close this message, then choose Play to enable audio and start the game.'); },
  start: async () => { recoveryAttempts++; },
}});
const restoreRecovery = installGlobals(recovery.document);
try {
  await selectThroughShell(recovery);
  assert.match(recovery.document.getElementById('error').textContent, /choose Play to enable audio/);
  assert.equal(recovery.trace.filter(row => row === 'start').length, 0,
    'A failed activation does not auto-retry or launch without prepared audio');
  assert.equal(recovery.document.getElementById('start-game').disabled, false,
    'A browser audio gesture failure leaves the explicit recovery action available');
  recovery.document.getElementById('error-close').click();
  await recovery.document.getElementById('start-game').click();
  assert.equal(recoveryAttempts, 1, 'Play starts only after the user requests recovery');
} finally { delete globalThis.testClickTrace; restoreRecovery(); }

let finishEjectedImport;
const ejectRace = await runScenario({name: 'eject-race', behavior: {
  importDisc: () => new Promise(resolve => { finishEjectedImport = resolve; }),
}});
const restoreEjectRace = installGlobals(ejectRace.document);
try {
  const importWork = selectThroughShell(ejectRace);
  await Promise.resolve();
  await ejectRace.document.getElementById('end-session').onclick();
  finishEjectedImport();
  await importWork;
  assert.equal(ejectRace.trace.filter(row => row === 'start').length, 0,
    'An import that finishes after Eject cannot start the ejected disc');
} finally { delete globalThis.testClickTrace; restoreEjectRace(); }

let finishSupersededImport;
const superseded = await runScenario({name: 'superseded-race', behavior: {
  importDisc: file => file.name === 'first.iso' ? new Promise(resolve => { finishSupersededImport = resolve; }) :
    Promise.reject(Error('Replacement import rejected while earlier work is pending')),
}});
const restoreSuperseded = installGlobals(superseded.document);
try {
  globalThis.testClickTrace = superseded.trace;
  const input = superseded.document.getElementById('disc-file');
  input.files = [{name: 'first.iso'}];
  const first = input.onchange();
  await Promise.resolve();
  input.files = [{name: 'second.iso'}];
  const second = input.onchange();
  await second;
  finishSupersededImport();
  await first;
  assert.equal(superseded.trace.filter(row => row === 'start').length, 0,
    'A superseded import completion cannot launch its stale selection');
} finally { delete globalThis.testClickTrace; restoreSuperseded(); }

const failed = await runScenario({name: 'mkdir-failure', failStartup: true});
const failedDocument = failed.document;
assert.equal(failed.nativeMainCalled, false, 'mkdir failure must prevent an unseeded native start');
assert.equal(failedDocument.getElementById('error-dialog').open, true);
assert.equal(failedDocument.getElementById('retry').hidden, false);
assert.equal(failedDocument.getElementById('error').textContent, 'cache directory denied');
assert.equal(failed.audioCreated, 0);
console.log('Public player shell: shared owner startup, acknowledgement-free disclosure, automatic launch, cancellation, preparation failure and stale-selection guards pass.');
