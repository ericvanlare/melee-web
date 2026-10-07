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
const toolbarActions = playerHtml.match(/<div id="toolbar-actions">([\s\S]*?)<\/div>/)?.[1];
assert.ok(toolbarActions, 'The player keeps one toolbar action group');
const toolbarButtons = [...toolbarActions.matchAll(/<button\b[^>]*\bid="([^"]+)"/g)].map(match => match[1]);
const primaryActionOrder = ['choose-disc', 'start-game', 'pause-game', 'fullscreen', 'end-session'];
assert.deepEqual(toolbarButtons.filter(id => primaryActionOrder.includes(id)), primaryActionOrder,
  'Disc, Play, Pause, Fullscreen and Eject keep their familiar order');
assert.equal(toolbarButtons.indexOf('controls-open'), 0,
  'Controls stays in its established leading position');
assert.equal(toolbarButtons.indexOf('settings-open'), 1,
  'Settings stays beside Controls before the game actions');
assert.equal(toolbarButtons.indexOf('choose-disc'), 2,
  'Disc starts the primary game-action group after Controls and Settings');
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
    const classes = new Set();
    this.classList = {
      contains: name => classes.has(name),
      add: name => classes.add(name),
      remove: name => classes.delete(name),
      toggle: (name, force) => {
        const next = force === undefined ? !classes.has(name) : !!force;
        if (next) classes.add(name); else classes.delete(name);
        return next;
      },
    };
  }

  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  removeAttribute(name) { if (name === 'value') delete this.value; }
  setAttribute(name, value) { this.attributes ||= new Map(); this.attributes.set(name, String(value)); }
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
  click() {
    this.clickCount++;
    globalThis.testClickTrace?.push(this.id);
    const document = globalThis.document;
    if (this.id === 'fullscreen') document.inFullscreenGesture = true;
    try { return this.onclick?.(); }
    finally { if (this.id === 'fullscreen') document.inFullscreenGesture = false; }
  }
  dispatchEvent(event) {
    for (const listener of this.listeners.get(event.type) || []) listener(event);
  }
  focus() { globalThis.document.activeElement = this; }
  requestFullscreen() { return Promise.resolve(); }
}

function makeDocument(fullscreen = 'unsupported') {
  const ids = [
  'canvas', 'player', 'choose-disc', 'disc-file', 'start-game', 'pause-game',
  'controls-open', 'controls-close', 'controls-dialog', 'keyboard-layout',
    'player-one-source', 'player-two-source', 'player-one-source-status',
    'player-two-source-status', 'boxx-source-note', 'keyboard-bindings-details',
    'keyboard-bindings', 'controller-advanced', 'controllers', 'idle-hint', 'fullscreen', 'end-session', 'status',
  'progress', 'disc-dialog', 'disc-cancel', 'disc-choose-file', 'error-dialog',
    'error', 'retry', 'error-close',
    'loading-panel', 'loading-label', 'loading-progress', 'loading-detail',
    'disc-selection-status',
  ];
  const elements = new Map([...new Set([...markupIds, ...ids])]
    .map(id => [id, new FakeElement('div', id)]));
  const listeners = new Map();
  const document = {
    hidden: false,
    documentElement: new FakeElement('html'),
    body: new FakeElement('body'),
    activeElement: elements.get('canvas'),
    fullscreenEnabled: fullscreen !== 'unsupported',
    fullscreenElement: null,
    inFullscreenGesture: false,
    getElementById(id) {
      const element = elements.get(id);
      if (!element) throw Error(`Missing test element #${id}`);
      return element;
    },
    createElement(tagName) { return new FakeElement(tagName); },
    addEventListener(name, listener) {
      const rows = listeners.get(name) || [];
      rows.push(listener); listeners.set(name, rows);
    },
    dispatch(name) { for (const listener of listeners.get(name) || []) listener(); },
    hasFocus: () => true,
    exitFullscreen: async () => {
      document.fullscreenElement = null;
      document.dispatch('fullscreenchange');
    },
    elements,
  };
  const player = elements.get('player');
  elements.get('canvas').focus = () => { document.activeElement = elements.get('canvas'); };
  player.requestCalls = 0;
  player.requestWasGesture = false;
  player.requestFullscreen = () => {
    player.requestCalls++;
    player.requestWasGesture = document.inFullscreenGesture;
    if (fullscreen === 'rejected') return Promise.reject(Error('gesture rejected'));
    document.fullscreenElement = player;
    document.dispatch('fullscreenchange');
    return Promise.resolve();
  };
  elements.get('player-one-source').value = 'auto';
  elements.get('player-two-source').value = 'auto';
  elements.get('keyboard-layout').disabled = true;
  return document;
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
    "import {mountSaveProfileSettings} from '../save-profile-settings.mjs';",
    "import {mountDiagnosticsSettings} from '../diagnostics-settings.mjs';",
  ].join('\n');
  const replacement = [
    'const mountMeleeRuntime = globalThis.testMountMeleeRuntime;',
    'const mountControllerSettings = globalThis.testMountControllerSettings;',
    'const mountSaveProfileSettings = globalThis.testMountSaveProfileSettings;',
    'const mountDiagnosticsSettings = globalThis.testMountDiagnosticsSettings;',
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

async function runScenario({name, failStartup = false, behavior = {}, fullscreen = 'unsupported', exerciseFullscreen = false}) {
  const document = makeDocument(fullscreen);
  const restore = installGlobals(document);
  const trace = [];
  globalThis.testClickTrace = trace;
  let stateCallback;
  let errorCallback;
  let settingsOptions;
  let saveSettingsOptions;
  let diagnosticsSettingsOptions;
  let nativeMainCalled = false;
  let audioCreated = 0;
  const idle = {ready: true, requiresReload: false, busy: false, state: 'idle', paused: false,
    canSelectDisc: true, canImport: true, graphicsReady: true, canStart: false, canPause: false, canUnload: false, audio: behavior.audio ? 'enabled' : 'disabled',
    progress: null, loading: null, message: 'Ready'};
  const graphicsPending = { ...idle, graphicsReady: false, canImport: false,
    loading: {phase: 'catalog', message: 'Preparing graphics…', complete: 0, total: 0} };
  let mockState = behavior.graphicsPending ? graphicsPending : idle;
  let player;

  globalThis.testMountControllerSettings = options => {
    settingsOptions = options;
    trace.push('settings');
    return {
      setState: next => trace.push(['settings-state', next?.state]),
      clearTouchInputs: () => trace.push('clear-touch-inputs'),
      bindPlayer: async runtime => { trace.push(['settings-bind', runtime]); },
    };
  };
  globalThis.testMountSaveProfileSettings = options => {
    saveSettingsOptions = options;
    trace.push('save-settings');
    return {
      get blocked() { return !!behavior.saveBlocked; },
      get busy() { return false; },
      setState: next => trace.push(['save-settings-state', next?.state]),
      bindPlayer: async runtime => { trace.push(['save-settings-bind', runtime]); },
      flushBeforeTeardown: async () => { trace.push('save-flush'); },
    };
  };
  globalThis.testMountDiagnosticsSettings = options => {
    diagnosticsSettingsOptions = options;
    trace.push('diagnostics-settings');
    return {
      setState: next => trace.push(['diagnostics-state', next?.state]),
      bindPlayer: async runtime => { trace.push(['diagnostics-bind', runtime]); },
    };
  };
  const testOpenNativeGameDiscSession = async file => {
    trace.push(['validate', file.name]);
    if (behavior.validateDisc) await behavior.validateDisc(file);
    if (file.invalid) throw Error('Invalid local disc');
    return {
      close() { trace.push(['close-session', file.name]); },
      async readScope() { return new Map(); },
    };
  };
  globalThis.testOpenNativeGameDiscSession = testOpenNativeGameDiscSession;
  globalThis.testMountMeleeRuntime = async options => {
    trace.push('mount');
    stateCallback = options.onState;
    errorCallback = options.onError;
    assert.equal(options.canvas, document.getElementById('canvas'));
    assert.equal(options.openDisc, undefined,
      'public shell delegates disc opening to the runtime profile adapter');
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
    options.onState(mockState);
    assert.equal(document.getElementById('loading-panel').hidden, !mockState.loading,
      'Graphics preparation remains visible until ready');
    assert.equal(document.getElementById('idle-hint').hidden, !!mockState.loading,
      'The idle instruction yields to active startup preparation');
    player = {
      controllers: {
        inspect: () => [], sample: () => [],
      },
      setKeyboardLayout: async layout => { trace.push(['layout', layout]); },
      setKeyboard(slot, enabled) { trace.push(['keyboard', slot, enabled]); },
      getState: () => mockState,
      focus() { trace.push('focus'); },
      activateAudio() { trace.push('audio-activate'); return behavior.activateAudio?.() ?? Promise.resolve(); },
      openDiscSession(file) {
        trace.push(['open-session-through-runtime', file.name]);
        return testOpenNativeGameDiscSession(file);
      },
      importDisc(file, options = {}) {
        trace.push(['import', file.name, options.preopenedSession]);
        const complete = () => {
          mockState = {...mockState, state: 'prepared', canImport: true,
            canStart: mockState.graphicsReady, canUnload: true, loading: null};
          stateCallback(mockState);
        };
        const imported = behavior.importDisc ? behavior.importDisc(file, options) : Promise.resolve();
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
    options.onOwner?.({handle: player});
    return player;
  };

  try {
    await importShellWithMocks({name});
    if (exerciseFullscreen) {
      const button = document.getElementById('fullscreen');
      const playerElement = document.getElementById('player');
      if (fullscreen === 'unsupported') {
        assert.equal(button.hidden, true, 'Unsupported native fullscreen hides its action');
        assert.equal(button.onclick, undefined, 'Unsupported fullscreen does not install a fallback action');
        assert.equal(markupIds.has('fullscreen-status'), false, 'No persistent fullscreen explanation is rendered');
      } else if (fullscreen === 'supported') {
        assert.equal(button.hidden, false);
        button.click();
        assert.equal(playerElement.requestCalls, 1);
        assert.equal(playerElement.requestWasGesture, true,
          'requestFullscreen runs synchronously within the button activation');
        assert.equal(button.textContent, 'Exit fullscreen');
        button.click();
        assert.equal(document.fullscreenElement, null);
        assert.equal(button.textContent, 'Fullscreen');
      } else {
        assert.equal(button.hidden, false);
        button.click();
        await new Promise(resolve => setTimeout(resolve, 0));
        assert.equal(playerElement.requestCalls, 1);
        assert.equal(button.textContent, 'Fullscreen', 'a rejected request leaves its native action truthful');
        assert.equal(playerElement.classList.contains('player-expanded'), false);
        assert.equal(markupIds.has('fullscreen-status'), false, 'A rejected request adds no persistent explanation');
      }
    }
    return {
      document,
      trace,
      settingsOptions,
      saveSettingsOptions,
      diagnosticsSettingsOptions,
      stateCallback,
      getMockState: () => mockState,
      failRuntime(error) {
        mockState = {...mockState, state: 'error', ready: false, requiresReload: true,
          canImport: false, canStart: false, loading: null, message: error.message};
        stateCallback(mockState);
        errorCallback(error);
      },
      nativeMainCalled,
      audioCreated,
      player,
    };
  } finally {
    delete globalThis.testMountMeleeRuntime;
    delete globalThis.testMountControllerSettings;
    delete globalThis.testMountSaveProfileSettings;
    delete globalThis.testMountDiagnosticsSettings;
    delete globalThis.testOpenNativeGameDiscSession;
    delete globalThis.testClickTrace;
    restore();
  }
}

const success = await runScenario({name: 'success', failStartup: false});
assert.deepEqual(success.trace.slice(0, 4), ['settings', 'save-settings', 'diagnostics-settings', 'mount']);
assert.equal(success.trace[4], 'native-main');
assert.equal(success.nativeMainCalled, true);
assert.equal(success.audioCreated, 0);
assert.equal(success.diagnosticsSettingsOptions, undefined,
  'Public diagnostics settings use the isolated DOM owner without exposing runtime options');
assert.deepEqual(success.settingsOptions.initialSources, ['auto', 'auto', 'off', 'off'],
  'public settings preserve two-player defaults while allowing explicit extra-port choices');
assert.equal(success.settingsOptions.openButton, success.document.getElementById('controls-open'));
assert.ok(success.trace.some(row => Array.isArray(row) && row[0] === 'settings-bind'), 'shell binds settings after native startup');
assert.ok(success.trace.some(row => Array.isArray(row) && row[0] === 'save-settings-bind'), 'save settings bind before a disc can be imported');
assert.ok(success.trace.some(row => Array.isArray(row) && row[0] === 'diagnostics-bind'), 'diagnostics settings bind after native startup');
assert.equal(success.document.getElementById('settings-open').disabled, false, 'Settings unlocks after save mode initialization');
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
  const input = scenario.document.getElementById('disc-file');
  const before = input.clickCount;
  scenario.document.getElementById('choose-disc').click();
  assert.equal(scenario.document.getElementById('disc-dialog').open, true,
    'Disc opens the disclosure before the file picker');
  scenario.document.getElementById('disc-choose-file').click();
  assert.equal(input.clickCount, before + 1, 'Choose file opens the picker with no checkbox gate');
  input.files = [file];
  await input.onchange();
  await new Promise(resolve => setTimeout(resolve, 0));
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
  assert.equal(flow.trace.filter(row => Array.isArray(row) && row[0] === 'validate').length, 1,
    'The selected File is validated once before native import');
  assert.equal(flow.trace.filter(row => Array.isArray(row) && row[0] === 'open-session-through-runtime').length, 1,
    'Profile-specific preopening goes through the runtime handle');
  assert.equal(flow.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 1);
  assert.ok(flow.trace.find(row => Array.isArray(row) && row[0] === 'import')[2],
    'The validated session is adopted instead of reopening the selected File');
  assert(flow.trace.indexOf('audio-activate') < flow.trace.indexOf('disc-file'),
    'Audio activation starts during the Choose file gesture before opening the native picker');
  assert(flow.document.getElementById('start-game').disabled,
    'The normal first launch does not require the Play button');
  assert.equal(flow.document.getElementById('disc-selection-status').hidden, true,
    'The temporary disc acknowledgement clears after play starts');
  assert.equal(flow.document.getElementById('disc-selection-status').textContent, '');
  const importCount = flow.trace.filter(row => Array.isArray(row) && row[0] === 'import').length;
  assert.equal(flow.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, importCount);
} finally { delete globalThis.testClickTrace; restoreFlow(); }

const earlySelection = await runScenario({name: 'selection-before-native-readiness', behavior: {graphicsPending: true}});
const restoreEarlySelection = installGlobals(earlySelection.document);
try {
  await selectThroughShell(earlySelection, {name: 'early.iso'});
  const status = earlySelection.document.getElementById('disc-selection-status');
  assert.equal(status.hidden, false, 'The selected File remains acknowledged while graphics are preparing');
  assert.match(status.textContent, /early\.iso/);
  assert.match(status.textContent, /waiting for graphics/i);
  assert.equal(earlySelection.trace.filter(row => Array.isArray(row) && row[0] === 'validate').length, 1,
    'Disc validation may run before native import readiness');
  assert.equal(earlySelection.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 0,
    'A prevalidated File is retained without importing into the unready native owner');
  assert.equal(earlySelection.trace.filter(row => row === 'start').length, 0);

  const graphicsStillPending = {...earlySelection.getMockState(), canImport: true, graphicsReady: false, canStart: false};
  earlySelection.stateCallback(graphicsStillPending);
  await new Promise(resolve => setTimeout(resolve, 0));
  const importRow = earlySelection.trace.find(row => Array.isArray(row) && row[0] === 'import');
  assert.ok(importRow, 'Native import begins when its own readiness gate opens, before graphics finish');
  assert.ok(importRow[2], 'Early native import adopts the already validated session');
  assert.equal(status.hidden, false, 'The filename stays acknowledged while native data prepares');
  assert.match(status.textContent, /Preparing early\.iso/);
  assert.equal(earlySelection.trace.filter(row => row === 'start').length, 0,
    'Import completion waits at the graphics readiness barrier');

  earlySelection.document.getElementById('choose-disc').click();
  earlySelection.document.getElementById('disc-choose-file').click();
  earlySelection.document.getElementById('disc-file').dispatchEvent({type: 'cancel'});
  assert.equal(status.hidden, true, 'Cancelling the file picker clears its temporary acknowledgement');
  assert.equal(status.textContent, '');
  earlySelection.stateCallback({...earlySelection.getMockState(), canImport: true, graphicsReady: true, canStart: true});
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(earlySelection.trace.filter(row => row === 'start').length, 1,
    'Late graphics readiness releases exactly one automatic launch');
  assert.equal(status.hidden, true, 'The acknowledgement clears when the delayed game starts');
  assert.equal(status.textContent, '');
} finally { delete globalThis.testClickTrace; restoreEarlySelection(); }

const invalidRecovery = await runScenario({name: 'invalid-file-recovery'});
const restoreInvalidRecovery = installGlobals(invalidRecovery.document);
try {
  await selectThroughShell(invalidRecovery, {name: 'bad.rvz', invalid: true});
  assert.equal(invalidRecovery.document.getElementById('error-dialog').open, true);
  assert.equal(invalidRecovery.document.getElementById('disc-selection-status').hidden, true,
    'Actionable validation errors do not occupy the toolbar');
  assert.notEqual(invalidRecovery.document.getElementById('error').textContent, '',
    'The error dialog retains the actionable validation detail');
  assert.equal(invalidRecovery.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 0);
  await selectThroughShell(invalidRecovery, {name: 'recovered.iso'});
  assert.equal(invalidRecovery.document.getElementById('error-dialog').open, false,
    'Choosing another File clears the previous validation error');
  assert.equal(invalidRecovery.trace.filter(row => row === 'start').length, 1);
} finally { delete globalThis.testClickTrace; restoreInvalidRecovery(); }

let finishValidationDuringFailure;
let validationStarted = false;
const initializationFailure = await runScenario({name: 'initialization-failure-during-validation', behavior: {
  graphicsPending: true,
  validateDisc: () => new Promise(resolve => {
    validationStarted = true;
    finishValidationDuringFailure = resolve;
  }),
}});
const restoreInitializationFailure = installGlobals(initializationFailure.document);
try {
  const input = initializationFailure.document.getElementById('disc-file');
  input.files = [{name: 'pending-valid-disc.iso'}];
  const selectionValidation = input.onchange();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(validationStarted, true);
  initializationFailure.failRuntime(Error('Graphics initialization failed'));
  finishValidationDuringFailure();
  await selectionValidation;
  const status = initializationFailure.document.getElementById('disc-selection-status');
  assert.equal(status.hidden, true, 'A stopped selection does not leave a filename in the toolbar');
  assert.equal(status.textContent, '');
  assert.equal(initializationFailure.document.getElementById('error').textContent,
    'Graphics initialization failed');
  assert.equal(initializationFailure.document.getElementById('retry').hidden, false,
    'The initialization failure keeps the reload recovery action visible');
  assert.equal(initializationFailure.trace.filter(row => Array.isArray(row) && row[0] === 'close-session').length, 1,
    'A session finishing after the runtime failure closes exactly once');
  assert.equal(initializationFailure.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 0);
  assert.equal(initializationFailure.trace.filter(row => row === 'start').length, 0);
} finally { restoreInitializationFailure(); }

let finishFirstValidation;
const replacement = await runScenario({name: 'replacement-during-validation', behavior: {
  validateDisc: file => file.name === 'first.iso' ? new Promise(resolve => { finishFirstValidation = resolve; }) : Promise.resolve(),
}});
const restoreReplacement = installGlobals(replacement.document);
try {
  globalThis.testClickTrace = replacement.trace;
  const input = replacement.document.getElementById('disc-file');
  input.files = [{name: 'first.iso'}];
  const first = input.onchange();
  input.files = [{name: 'replacement.iso'}];
  const second = input.onchange();
  await second;
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(replacement.trace.filter(row => row === 'start').length, 1,
    'A replacement selection can finish while older file validation is pending');
  finishFirstValidation();
  await first;
  assert.ok(replacement.trace.some(row => Array.isArray(row) && row[0] === 'close-session' && row[1] === 'first.iso'),
    'A stale validation result closes its newly opened file session');
  assert.equal(replacement.trace.filter(row => Array.isArray(row) && row[0] === 'import').length, 1,
    'Only the current selection reaches native import');
} finally { delete globalThis.testClickTrace; restoreReplacement(); }

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
  assert.equal(recovery.document.getElementById('status').hidden, true,
    'Dismissing a recoverable error clears the stale toolbar alert');
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
  assert(ejectRace.trace.indexOf('clear-touch-inputs') >= 0 &&
    ejectRace.trace.indexOf('clear-touch-inputs') < ejectRace.trace.indexOf('destroy'),
  'Eject clears all held touch input before waiting for native teardown');
  assert(ejectRace.trace.indexOf('save-flush') < ejectRace.trace.indexOf('destroy'),
    'Eject commits Personal progress before retiring the native owner');
  assert.equal(ejectRace.document.getElementById('disc-selection-status').hidden, true,
    'Eject clears the temporary disc acknowledgement');
  assert.equal(ejectRace.document.getElementById('disc-selection-status').textContent, '');
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
await runScenario({name: 'fullscreen-unsupported', fullscreen: 'unsupported', exerciseFullscreen: true});
await runScenario({name: 'fullscreen-supported', fullscreen: 'supported', exerciseFullscreen: true});
await runScenario({name: 'fullscreen-rejected', fullscreen: 'rejected', exerciseFullscreen: true});
console.log('Public player shell: profile-owned startup, pre-readiness disc selection, readiness-gated import/autoplay (exactly once), invalid retry, replacement, cancellation, audio recovery, stale-selection guards and native fullscreen supported, unsupported and rejection cases pass.');
