import {keyboardRows} from './prototype-keyboard-layouts.mjs';
import {mountControllerPanel} from './controller-panel.mjs';

export const SOURCE_MODES = Object.freeze(['auto', 'keyboard', 'controller', 'off']);
export const SETTINGS_KEY = 'melee-prototype-keyboard-v1';

const validSource = mode => SOURCE_MODES.includes(mode) ? mode : 'auto';
const validLayout = layout => ['two', 'boxx'].includes(layout) ? layout : 'two';
const sourceSelect = port => `player-${port ? 'two' : 'one'}-source`;
const sourceStatus = port => `player-${port ? 'two' : 'one'}-source-status`;

function safeStorage(storage) {
  if (storage !== undefined) return storage;
  try { return globalThis.localStorage; } catch { return null; }
}

function readSettings(storage, key, initialSources = ['auto', 'auto']) {
  const fallback = {layout: 'two', sources: initialSources.map(validSource)};
  if (!storage || !key) return fallback;
  try {
    const saved = JSON.parse(storage.getItem(key) || 'null');
    if (!saved || typeof saved !== 'object') return fallback;
    const result = {layout: validLayout(saved.layout), sources: fallback.sources.slice()};
    if (Array.isArray(saved.sources)) {
      for (let port = 0; port < 2; port++) result.sources[port] = validSource(saved.sources[port]);
    } else {
      // Older public preferences stored whether each keyboard was enabled.
      // Preserve their effective source: enabled means Auto, disabled means
      // Controller only.
      if (typeof saved.one === 'boolean') result.sources[0] = saved.one ? 'auto' : 'controller';
      if (typeof saved.two === 'boolean') result.sources[1] = saved.two ? 'auto' : 'controller';
    }
    return result;
  } catch { return fallback; }
}

function saveSettings(storage, key, layout, sources) {
  if (!storage || !key) return;
  try { storage.setItem(key, JSON.stringify({layout, sources: sources.slice(0, 2)})); }
  catch { /* Session-only controls when storage is unavailable. */ }
}

function element(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [name, value] of Object.entries(attrs)) {
    if (name === 'textContent') node.textContent = value;
    else if (name === 'className') node.className = value;
    else if (name === 'value') node.value = value;
    else if (name === 'disabled' || name === 'hidden' || name === 'open') node[name] = !!value;
    else node.setAttribute(name, value);
  }
  node.append(...children.filter(child => child != null));
  return node;
}

function installStyle() {
  if (document.querySelector?.('link[data-melee-controller-settings-style]')) return;
  const link = element('link', {
    'data-melee-controller-settings-style': '', rel: 'stylesheet',
    href: new URL('./controller-settings.css', import.meta.url).href,
  });
  document.head?.append(link);
}

function controllerName(row) {
  return String(row?.id || row?.profile || 'Connected controller');
}

function describeSource(mode, row, port, layout) {
  if (mode === 'off') return 'Off';
  if (mode === 'keyboard') return 'Keyboard';
  if (mode === 'controller') {
    return row ? `Controller only · ${controllerName(row)}` : 'Controller only · waiting for a ready controller';
  }
  if (!row && port === 1 && layout === 'boxx') return 'No controller · B0XX keyboard is Player 1 only';
  return row ? `Controller · ${controllerName(row)}` : 'Keyboard · no ready controller connected';
}

function renderMarkup(container) {
  container.replaceChildren(
    element('div', {className: 'controls-heading'},
      element('div', {},
        element('h2', {textContent: 'Controls'}),
        element('p', {textContent: 'Choose where each player gets input. Auto uses a connected controller when one is ready and falls back to the keyboard.'}),
      ),
      element('button', {id: 'controls-close', type: 'button', textContent: 'Done'}),
    ),
    element('section', {className: 'source-settings', 'aria-labelledby': 'source-settings-title'},
      element('h3', {id: 'source-settings-title', textContent: 'Player input'}),
      sourceRow(0), sourceRow(1),
      element('p', {id: 'boxx-source-note', className: 'source-note', hidden: true,
        textContent: 'B0XX provides keyboard controls for Player 1 only; Player 2 keyboard input is unavailable.'}),
    ),
    element('div', {className: 'keyboard-options'},
      element('label', {for: 'keyboard-layout', textContent: 'Keyboard layout'}),
      element('select', {id: 'keyboard-layout', 'aria-label': 'Keyboard layout', disabled: true},
        element('option', {value: 'two', textContent: '2 players'}),
        element('option', {value: 'boxx', textContent: '1 player · B0XX'}),
      ),
    ),
    element('details', {id: 'keyboard-bindings-details'},
      element('summary', {textContent: 'Keyboard bindings'}),
      element('table', {id: 'keyboard-bindings', 'aria-label': 'Keyboard bindings'}),
    ),
    element('details', {id: 'controller-advanced'},
      element('summary', {textContent: 'Advanced controller test and remapping'}),
      element('div', {id: 'controllers', 'aria-label': 'Controllers'}),
    ),
  );
  function sourceRow(port) {
    return element('div', {className: 'source-row'},
      element('label', {for: sourceSelect(port), textContent: `Player ${port + 1}`}),
      element('select', {'id': sourceSelect(port), 'data-source-port': port,
        'aria-label': `Player ${port + 1} input source`, disabled: true},
        element('option', {value: 'auto', textContent: 'Auto (controller if connected, keyboard otherwise)'}),
        element('option', {value: 'keyboard', textContent: 'Keyboard'}),
        element('option', {value: 'controller', textContent: 'Controller only'}),
        element('option', {value: 'off', textContent: 'Off'}),
      ),
      element('p', {id: sourceStatus(port), className: 'source-status', role: 'status', 'aria-live': 'polite',
        textContent: 'Auto · Keyboard until a ready controller connects'}),
    );
  }
}

/**
 * Mount the shared compact source/layout settings UI.
 *
 * The player owner is deliberately injected after native startup. This keeps
 * this component a page-settings boundary: it does not create a runtime,
 * schedule frames, or touch the low-level controller mapper.
 */
export function mountControllerSettings({container, storage,
  preferenceKey = SETTINGS_KEY, initialSources = ['auto', 'auto'], disableExtraPorts = false,
  legacyKeyboard = [], onError = () => {}, focus = () => {}, openButton = null,
  expose = false} = {}) {
  if (!container || typeof container.replaceChildren !== 'function') throw Error('A controller settings container is required.');
  installStyle();
  renderMarkup(container);
  container.dataset.controllerSettings = '';

  const store = safeStorage(storage);
  const loaded = readSettings(store, preferenceKey, initialSources);
  let layout = loaded.layout;
  const sources = loaded.sources;
  if (layout === 'boxx' && sources[1] === 'keyboard') sources[1] = 'auto';
  let player = null, applySequence = 0, applyPromise = Promise.resolve();
  let controllerPanel = null, lastRows = [], lastInspection = 0;
  const $ = id => container.querySelector(`#${id}`) || document.getElementById(id);
  const dialog = container.matches?.('dialog') ? container : container.closest?.('dialog');
  const sourceElements = [$(sourceSelect(0)), $(sourceSelect(1))];
  const layoutElement = $('keyboard-layout');
  const closeElement = $('controls-close');
  const legacy = legacyKeyboard.filter(Boolean);

  function save() { saveSettings(store, preferenceKey, layout, sources); }
  function manager() { return player?.controllers; }
  function inspectControllers() {
    const current = manager();
    if (!current) return [];
    try {
      const rows = current.sample?.() ?? current.inspect?.() ?? [];
      return Array.isArray(rows) ? rows : [];
    } catch { return []; }
  }
  function readyControllerForPort(rows, port) {
    return rows.find(row => row?.port === port && row?.status === 'ready' &&
      (typeof row.enabled === 'boolean' ? row.enabled : true)) || null;
  }
  function renderKeyboard() {
    const table = $('keyboard-bindings');
    if (!table) return;
    const second = layout !== 'boxx';
    const option = sourceElements[1]?.querySelector?.('option[value="keyboard"]');
    if (option) option.disabled = layout === 'boxx';
    const note = $('boxx-source-note');
    if (note) note.hidden = layout !== 'boxx';
    const head = document.createElement('thead'), body = document.createElement('tbody');
    const rows = [layout === 'boxx' ? ['Action', 'Key'] : ['Action', 'P1', ...(second ? ['P2'] : [])], ...keyboardRows(layout, second)];
    rows.forEach((values, index) => {
      const row = document.createElement('tr');
      values.slice(0, layout === 'boxx' || !second ? 2 : 3).forEach(value => {
        const cell = document.createElement(index ? 'td' : 'th'); cell.textContent = value; row.append(cell);
      });
      (index ? body : head).append(row);
    });
    table.replaceChildren(head, body);
  }
  function renderSources(rows = lastRows) {
    for (let port = 0; port < 2; port++) {
      const select = sourceElements[port], statusElement = $(sourceStatus(port));
      if (!select || !statusElement) continue;
      const mode = validSource(sources[port]);
      select.value = mode;
      statusElement.textContent = describeSource(mode, readyControllerForPort(rows, port), port, layout);
    }
    for (let port = 0; port < legacy.length; port++) legacy[port].checked = sources[port] === 'auto' || sources[port] === 'keyboard';
  }
  function renderNotice(force = false) {
    const current = manager();
    const now = Date.now();
    if (!force && now - lastInspection < 900) { renderSources(); return; }
    lastRows = inspectControllers(); lastInspection = now; renderSources(lastRows);
    if (openButton) {
      const needsSetup = lastRows.some(row => row?.status === 'needs-setup');
      openButton.textContent = current?.error ? 'Controls · unavailable' : needsSetup ? 'Controls · setup needed' : 'Controls';
    }
  }
  function disableControls(disabled) {
    if (layoutElement) layoutElement.disabled = disabled;
    for (const select of sourceElements) if (select) select.disabled = disabled;
  }
  function applySources(ticket = applySequence) {
    const current = player, currentManager = current?.controllers;
    if (!current || ticket !== applySequence) return Promise.resolve();
    for (let port = 0; port < 2; port++) {
      const mode = validSource(sources[port]);
      sources[port] = mode;
      currentManager?.setPortSource?.(port, mode);
      current.setKeyboard?.(port, mode === 'auto' || mode === 'keyboard');
    }
    if (disableExtraPorts) for (let port = 2; port < 4; port++) currentManager?.setPortSource?.(port, 'off');
    renderNotice(true);
    return Promise.resolve();
  }
  function setLayout(value, {persist = true} = {}) {
    const next = validLayout(value);
    if (layout === next && layoutElement?.value === next) return applyPromise;
    layout = next;
    if (layout === 'boxx' && sources[1] === 'keyboard') sources[1] = 'auto';
    if (layoutElement) layoutElement.value = layout;
    renderKeyboard(); renderSources(); if (persist) save();
    const ticket = ++applySequence, current = player;
    if (!current) return Promise.resolve();
    applyPromise = Promise.resolve().then(async () => {
      if (current !== player) return;
      await current.setKeyboardLayout?.(layout);
      if (ticket !== applySequence || current !== player) return;
      await applySources(ticket);
    }).catch(error => { onError(error); throw error; });
    return applyPromise;
  }
  function setSource(port, mode, {persist = true} = {}) {
    if (![0, 1].includes(port)) throw Error('Unknown player port.');
    let next = validSource(mode);
    if (port === 1 && layout === 'boxx' && next === 'keyboard') next = 'auto';
    sources[port] = next;
    const ticket = ++applySequence;
    sourceElements[port].value = next;
    if (persist) save();
    renderSources();
    const current = player;
    if (!current) return Promise.resolve();
    applyPromise = Promise.resolve().then(() => applySources(ticket)).catch(error => { onError(error); throw error; });
    return applyPromise;
  }
  function bindPlayer(nextPlayer) {
    player = nextPlayer || null;
    const ticket = ++applySequence;
    if (!player) { renderNotice(true); return Promise.resolve(); }
    const current = player;
    applyPromise = Promise.resolve().then(async () => {
      if (current !== player) return;
      await current.setKeyboardLayout?.(layout);
      if (ticket !== applySequence) return;
      await applySources(ticket);
      renderNotice(true);
      mountAdvanced();
    }).catch(error => { onError(error); throw error; });
    return applyPromise;
  }
  function setState(next) {
    disableControls(!next?.ready || !!next?.requiresReload || !!next?.busy);
    renderNotice();
  }
  function open() { if (dialog && !dialog.open) dialog.showModal?.(); }
  function close() {
    const details = $('controller-advanced');
    if (details) details.open = false;
    unmountAdvanced();
    if (dialog?.open) dialog.close?.(); else focus();
  }
  function unmountAdvanced() {
    controllerPanel?.(); controllerPanel = null; manager()?.setTesting?.(false);
  }
  function mountAdvanced() {
    const details = $('controller-advanced');
    if (!details?.open || controllerPanel || !manager()) return;
    try {
      controllerPanel = mountControllerPanel($('controllers'), manager());
      manager().setTesting?.(true);
    } catch (error) { onError(error); }
  }

  const cleanup = [];
  const listen = (target, type, listener, options) => {
    if (!target?.addEventListener) return;
    target.addEventListener(type, listener, options);
    cleanup.push(() => target.removeEventListener?.(type, listener, options));
  };
  listen(layoutElement, 'change', () => { void setLayout(layoutElement.value).catch(() => {}); });
  sourceElements.forEach((select, port) => listen(select, 'change', () => {
    void setSource(port, select.value).catch(() => {});
  }));
  legacy.forEach((input, port) => listen(input, 'change', () => {
    void setSource(port, input.checked ? 'auto' : 'controller', {persist: false}).catch(() => {});
  }));
  const advanced = $('controller-advanced');
  listen(advanced, 'toggle', () => {
    if (advanced.open) mountAdvanced(); else unmountAdvanced();
  });
  listen(closeElement, 'click', close);
  const onClose = () => { const details = $('controller-advanced'); if (details) details.open = false; unmountAdvanced(); focus(); };
  listen(dialog, 'close', onClose);
  listen(openButton, 'click', open);
  layoutElement.value = layout;
  renderKeyboard(); renderSources(); setState(null);
  const noticeTimer = setInterval(() => renderNotice(true), 1000);
  const pagehide = () => clearInterval(noticeTimer);
  listen(globalThis, 'pagehide', pagehide, {once: true});

  const api = {
    bindPlayer, setState, setLayout, setSource, open, close,
    inspect: () => inspectControllers(),
    destroy() {
      unmountAdvanced(); clearInterval(noticeTimer);
      ++applySequence; player = null;
      for (const remove of cleanup.splice(0)) remove();
      if (expose && typeof window !== 'undefined' && window.meleeControllerSettings === api) delete window.meleeControllerSettings;
    },
  };
  if (expose && typeof window !== 'undefined') window.meleeControllerSettings = api;
  return api;
}
