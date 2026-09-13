import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {keyboardRows} from '../prototype-keyboard-layouts.mjs';

const $ = id => document.getElementById(id);
let player, state, currentError = '', requiresReload = false;
const preferenceKey = 'melee-prototype-keyboard-v1';
const busyStates = ['booting', 'importing', 'preparing', 'pausing', 'resuming', 'unloading'];
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
  $('choose-disc').disabled = !next.canImport;
  $('start-game').disabled = !next.canStart;
  $('pause-game').disabled = !next.canPause;
  $('pause-game').textContent = next.paused ? 'Resume' : 'Pause';
  $('end-session').disabled = !next.canUnload;
  $('keyboard-layout').disabled = !next.ready || next.requiresReload || next.busy;
  const busy = busyStates.includes(next.state);
  $('progress').hidden = !busy || next.state === 'booting';
  if (next.progress) { $('progress').max = next.progress.total; $('progress').value = next.progress.complete; }
  else $('progress').removeAttribute('value');
  $('status').hidden = !busy && !next.paused && !currentError && next.state !== 'error';
  $('status').disabled = !currentError && !next.paused && next.state !== 'error';
  $('status').textContent = currentError || next.state === 'error' ? 'Error' : next.paused ? 'Paused' : next.state === 'importing' ? 'Reading…' : next.state === 'unloading' ? 'Ejecting…' : 'Loading…';
  $('status').title = currentError || next.message;
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
    if (typeof saved.one === 'boolean') $('keyboard-one').checked = saved.one;
    if (typeof saved.two === 'boolean') $('keyboard-two').checked = saved.two;
  }
} catch { /* Controls remain available without saved preferences. */ }
function renderKeyboard() {
  const layout = $('keyboard-layout').value, second = $('keyboard-two').checked;
  $('keyboard-two-option').hidden = layout === 'boxx';
  $('keyboard-one-label').textContent = layout === 'boxx' ? 'Keyboard' : 'P1 keyboard';
  const head = document.createElement('thead'), body = document.createElement('tbody');
  const rows = [layout === 'boxx' ? ['Action', 'Key'] : ['Action', 'P1', ...(second ? ['P2'] : [])], ...keyboardRows(layout, second)];
  rows.forEach((values, index) => {
    const row = document.createElement('tr');
    values.slice(0, layout === 'boxx' || !second ? 2 : 3).forEach(value => { const cell = document.createElement(index ? 'td' : 'th'); cell.textContent = value; row.append(cell); });
    (index ? body : head).append(row);
  });
  $('keyboard-bindings').replaceChildren(head, body);
}
async function applyKeyboard() {
  renderKeyboard();
  if (!player) return;
  try {
    await player.setKeyboardLayout($('keyboard-layout').value);
    player.setKeyboard(0, $('keyboard-one').checked); player.setKeyboard(1, $('keyboard-two').checked);
  } catch (error) { showError(error); }
}
for (const id of ['keyboard-layout', 'keyboard-one', 'keyboard-two']) $(id).onchange = () => {
  void applyKeyboard();
  try { localStorage.setItem(preferenceKey, JSON.stringify({layout: $('keyboard-layout').value, one: $('keyboard-one').checked, two: $('keyboard-two').checked})); }
  catch { /* Session-only controls when storage is unavailable. */ }
};
let controllerTimer;
function renderControllers() {
  const rows = Array.from(navigator.getGamepads?.() || []).filter(Boolean).map(pad => {
    const row = document.createElement('li'); row.textContent = `Port ${pad.index + 1}: ${pad.id}`; return row;
  });
  if (!rows.length) { const row = document.createElement('li'); row.textContent = 'No controller detected. Press a controller button to connect.'; rows.push(row); }
  $('controllers').replaceChildren(...rows);
}
$('controls-open').onclick = () => { renderKeyboard(); renderControllers(); $('controls-dialog').showModal(); controllerTimer = setInterval(renderControllers, 1000); };
$('controls-close').onclick = () => $('controls-dialog').close();
$('controls-dialog').addEventListener('close', () => { clearInterval(controllerTimer); player?.focus(); });
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
  player = await mountMeleeRuntime({canvas: $('canvas'), onState: renderStatus, onError: error => showError(error)});
  await applyKeyboard();
} catch (error) { showError(error, true); }
