import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {keyboardRows} from '../prototype-keyboard-layouts.mjs';
import {mountControllerPanel} from '../controller-panel.mjs';

const $ = id => document.getElementById(id);
let player, state, currentError = '', requiresReload = false, hasStarted = false;
const preferenceKey = 'melee-prototype-keyboard-v1';
const busyStates = ['booting', 'importing', 'preparing', 'pausing', 'resuming', 'unloading'];
const SOURCE_MODES = Object.freeze(['auto', 'keyboard', 'controller', 'off']);
const sourceModes = ['auto', 'auto'];

function validSource(mode) { return SOURCE_MODES.includes(mode) ? mode : 'auto'; }

function sourceSelect(port) { return $(`player-${port ? 'two' : 'one'}-source`); }
function sourceStatus(port) { return $(`player-${port ? 'two' : 'one'}-source-status`); }

function showError(error, fatal = false) {
  currentError = error?.message || String(error);
  requiresReload = fatal || !!state?.requiresReload;
  $('error').textContent = currentError;
  $('retry').hidden = !requiresReload;
  if (!$('error-dialog').open) $('error-dialog').showModal();
  renderStatus(state);
}
function clearError() { currentError = ''; $('error').textContent = ''; $('error-dialog').close(); }
function renderStatus(next) {
  if (!next) return;
  state = next;
  if (next.running) hasStarted = true;
  $('choose-disc').disabled = !next.canImport;
  $('start-game').disabled = !next.canStart;
  $('pause-game').disabled = !next.canPause;
  $('pause-game').textContent = next.paused ? 'Resume' : 'Pause';
  $('end-session').disabled = !next.canUnload;
  $('keyboard-layout').disabled = !next.ready || next.requiresReload || next.busy;
  for (let port = 0; port < 2; port++) sourceSelect(port).disabled = !next.ready || next.requiresReload || next.busy;
  $('idle-hint').hidden = hasStarted || !next.ready || next.busy || next.requiresReload;
  const busy = busyStates.includes(next.state);
  $('progress').hidden = !busy || next.state === 'booting';
  if (next.progress) { $('progress').max = next.progress.total; $('progress').value = next.progress.complete; }
  else $('progress').removeAttribute('value');
  $('status').hidden = !busy && !next.paused && !currentError && next.state !== 'error';
  $('status').disabled = !currentError && !next.paused && next.state !== 'error';
  $('status').textContent = currentError || next.state === 'error' ? 'Error' : next.paused ? 'Paused' : next.state === 'importing' ? 'Reading…' : next.state === 'unloading' ? 'Ejecting…' : 'Loading…';
  $('status').title = currentError || next.message;
  renderControllerNotice();
}
$('choose-disc').onclick = () => { $('disc-ack').checked = false; $('disc-continue').disabled = true; $('disc-dialog').showModal(); };
$('disc-ack').onchange = () => { $('disc-continue').disabled = !$('disc-ack').checked; };
$('disc-cancel').onclick = () => { $('disc-dialog').close(); player?.focus(); };
$('disc-continue').onclick = () => {
  if (!$('disc-ack').checked || !state?.canImport) return;
  $('disc-dialog').close(); $('disc-file').click();
};
$('disc-file').onchange = async () => {
  const file = $('disc-file').files[0];
  if (!file) return;
  clearError();
  try { await player.importDisc(file); $('start-game').focus(); }
  catch (error) { showError(error); }
  finally { $('disc-file').value = ''; }
};
$('start-game').onclick = async () => { clearError(); try { await player.start(); } catch (error) { showError(error); } };
$('pause-game').onclick = async () => { clearError(); try { await (state.paused ? player.resume() : player.pause()); } catch (error) { showError(error); } };
$('end-session').onclick = async () => {
  $('end-session').disabled = true;
  try { await player.destroy(); location.reload(); }
  catch (error) { showError(error, true); }
};
$('retry').onclick = () => location.reload();
$('status').onclick = () => {
  if (currentError || state?.state === 'error') showError(currentError || state.message, state?.requiresReload);
  else if (state?.paused) showError(state.message);
};
$('error-close').onclick = () => { $('error-dialog').close(); player?.focus(); };

try {
  const saved = JSON.parse(localStorage.getItem(preferenceKey));
  if (saved && ['two', 'boxx'].includes(saved.layout)) {
    $('keyboard-layout').value = saved.layout;
  }
  if (Array.isArray(saved?.sources)) {
    for (let port = 0; port < 2; port++) sourceModes[port] = validSource(saved.sources[port]);
  } else {
    // Older preferences represented keyboard availability with one/two. Keep
    // those profiles playable while letting Auto choose a physical device.
    if (typeof saved?.one === 'boolean') sourceModes[0] = saved.one ? 'auto' : 'controller';
    if (typeof saved?.two === 'boolean') sourceModes[1] = saved.two ? 'auto' : 'controller';
  }
} catch { /* Controls remain available without saved preferences. */ }

function renderKeyboard() {
  const layout = $('keyboard-layout').value, second = layout !== 'boxx';
  const option = sourceSelect(1)?.querySelector?.('option[value="keyboard"]');
  if (option) option.disabled = layout === 'boxx';
  $('boxx-source-note').hidden = layout !== 'boxx';
  const head = document.createElement('thead'), body = document.createElement('tbody');
  const rows = [layout === 'boxx' ? ['Action', 'Key'] : ['Action', 'P1', ...(second ? ['P2'] : [])], ...keyboardRows(layout, second)];
  rows.forEach((values, index) => {
    const row = document.createElement('tr');
    values.slice(0, layout === 'boxx' || !second ? 2 : 3).forEach(value => { const cell = document.createElement(index ? 'td' : 'th'); cell.textContent = value; row.append(cell); });
    (index ? body : head).append(row);
  });
  $('keyboard-bindings').replaceChildren(head, body);
}

function persistPreferences() {
  try {
    localStorage.setItem(preferenceKey, JSON.stringify({layout: $('keyboard-layout').value, sources: sourceModes.slice()}));
  } catch { /* Session-only controls when storage is unavailable. */ }
}

function sourceMode(port) {
  const manager = player?.controllers;
  try {
    const mode = manager?.getPortSource?.(port);
    if (SOURCE_MODES.includes(mode)) return mode;
  } catch { /* Use the shell preference while a controller manager is unavailable. */ }
  return sourceModes[port];
}

function applyPortSources() {
  if (!player) return;
  if ($('keyboard-layout').value === 'boxx' && sourceModes[1] === 'keyboard') sourceModes[1] = 'auto';
  for (let port = 0; port < 2; port++) {
    const mode = validSource(sourceModes[port]);
    sourceModes[port] = mode;
    player.controllers?.setPortSource?.(port, mode);
    player.setKeyboard?.(port, mode === 'auto' || mode === 'keyboard');
  }
  // The public player has two ports. Keep the manager's developer-only ports
  // disabled so a third connected device cannot be routed invisibly.
  for (let port = 2; port < 4; port++) player.controllers?.setPortSource?.(port, 'off');
  renderControllerNotice(true);
}

async function applyKeyboard() {
  renderKeyboard();
  if (!player) return;
  try {
    await player.setKeyboardLayout($('keyboard-layout').value);
    applyPortSources();
  } catch (error) { showError(error); }
}

function inspectControllers() {
  const manager = player?.controllers;
  if (!manager) return [];
  try {
    const rows = manager.sample?.() ?? manager.inspect?.() ?? [];
    return Array.isArray(rows) ? rows : [];
  } catch { return []; }
}

function readyControllerForPort(rows, port) {
  return rows.find(row => row?.port === port && row?.status === 'ready' &&
    (typeof row.enabled === 'boolean' ? row.enabled : true)) || null;
}

function controllerName(row) {
  return String(row?.id || row?.profile || 'Connected controller');
}

function describeSource(mode, row, port) {
  if (mode === 'off') return 'Off';
  if (mode === 'keyboard') return 'Keyboard';
  if (mode === 'controller') return row ? `Controller only · ${controllerName(row)}` : 'Controller only · waiting for a ready controller';
  if (!row && port === 1 && $('keyboard-layout').value === 'boxx') return 'No controller · B0XX keyboard is Player 1 only';
  return row ? `Controller · ${controllerName(row)}` : 'Keyboard · no ready controller connected';
}

function renderSourceSettings(rows = []) {
  for (let port = 0; port < 2; port++) {
    const select = sourceSelect(port), status = sourceStatus(port);
    if (!select || !status) continue;
    const mode = validSource(sourceMode(port));
    sourceModes[port] = mode;
    select.value = mode;
    status.textContent = describeSource(mode, readyControllerForPort(rows, port), port);
  }
}

function renderControllerNotice(force = false) {
  const manager = player?.controllers;
  if (!manager) {
    renderSourceSettings([]);
    return;
  }
  const now = Date.now();
  if (!force && now - lastControllerInspection < 900) {
    renderSourceSettings(lastControllerRows);
    return;
  }
  const controllers = inspectControllers();
  lastControllerRows = controllers;
  lastControllerInspection = now;
  renderSourceSettings(controllers);
  const needsSetup = controllers.some(row => row?.status === 'needs-setup');
  $('controls-open').textContent = manager.error ? 'Controls · unavailable' : needsSetup ? 'Controls · setup needed' : 'Controls';
}

let controllerPanel, lastControllerRows = [], lastControllerInspection = 0;
function unmountControllers() {
  controllerPanel?.();
  controllerPanel = null;
  player?.controllers?.setTesting?.(false);
}

function renderControllers() {
  const details = $('controller-advanced');
  if (!player || !details?.open || controllerPanel) return;
  try {
    controllerPanel = mountControllerPanel($('controllers'), player.controllers);
    player.controllers.setTesting?.(true);
  } catch (error) { showError(error); }
}

for (let port = 0; port < 2; port++) sourceSelect(port).onchange = () => {
  const select = sourceSelect(port);
  let mode = validSource(select.value);
  if (port === 1 && $('keyboard-layout').value === 'boxx' && mode === 'keyboard') mode = 'auto';
  sourceModes[port] = mode;
  select.value = mode;
  persistPreferences();
  try { applyPortSources(); } catch (error) { showError(error); }
};
$('keyboard-layout').onchange = () => {
  renderKeyboard();
  if ($('keyboard-layout').value === 'boxx' && sourceModes[1] === 'keyboard') sourceModes[1] = 'auto';
  persistPreferences();
  void applyKeyboard();
};
$('controls-open').onclick = () => { renderKeyboard(); renderControllerNotice(true); $('controls-dialog').showModal(); };
$('controls-close').onclick = () => $('controls-dialog').close();
$('controller-advanced').addEventListener('toggle', () => {
  if ($('controller-advanced').open) renderControllers();
  else unmountControllers();
});
$('controls-dialog').addEventListener('close', () => {
  $('controller-advanced').open = false;
  unmountControllers();
  player?.focus();
});
const fullscreenAvailable = !!document.fullscreenEnabled && typeof $('player').requestFullscreen === 'function';
$('fullscreen').disabled = !fullscreenAvailable;
$('fullscreen').title = fullscreenAvailable ? '' : 'Fullscreen unavailable in this browser';
$('fullscreen').onclick = async () => {
  try { if (document.fullscreenElement) await document.exitFullscreen(); else await $('player').requestFullscreen(); player?.focus(); }
  catch { showError('Fullscreen was declined by the browser.'); }
};
document.addEventListener('fullscreenchange', () => { $('fullscreen').textContent = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen'; });
renderKeyboard();
try {
  player = await mountMeleeRuntime({
    canvas: $('canvas'), onState: renderStatus, onError: error => showError(error),
  });
  await applyKeyboard();
  if ($('controls-dialog').open && $('controller-advanced').open) renderControllers();
  renderControllerNotice(true);
  const controllerNoticeTimer = setInterval(() => renderControllerNotice(true), 1000);
  window.addEventListener('pagehide', () => clearInterval(controllerNoticeTimer), {once: true});
} catch (error) { showError(error, true); }
