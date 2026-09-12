import {mountPrototypePlayer} from './prototype-runtime-adapter.mjs';
import {resolvePrototypeContent} from './prototype-content.mjs';
import {keyboardRows} from './prototype-keyboard-layouts.mjs';

const $ = id => document.getElementById(id);
const development = document.documentElement.dataset.environment === 'development';
let player, lastStatus, selected = false, restarting = false, keyboardApplied = false;
let controllerTimer, controllerKey = '', currentError = '', fatalError = false;
let appliedKeyboardLayout = 'two';
$('development-link').hidden = !development;
document.title = `Melee · ${development ? 'Development' : 'Staging'} prototype`;
$('player').setAttribute('aria-label', document.title);
$('development-link').onclick = () => { location.href = new URL('./runtime.html', import.meta.url).href; };

function showError(error, fatal = false) {
  currentError = error.message || String(error);
  fatalError = fatal;
  $('error').textContent = currentError;
  $('retry').hidden = !fatal;
  $('status').hidden = false;
  $('status').disabled = false;
  $('status').textContent = 'Error';
  $('status').title = currentError;
  if (!$('error-dialog').open) $('error-dialog').showModal();
}
function clearError() {
  currentError = ''; fatalError = false;
  $('error-dialog').close();
  $('error').textContent = '';
}
function renderStatus(state) {
  lastStatus = state;
  const busy = ['booting', 'importing', 'preparing', 'pausing', 'unloading'].includes(state.state);
  const loadingScene = state.scene === 'preparing';
  $('choose-disc').disabled = !state.canImport;
  $('start-game').disabled = !state.canLaunch;
  $('pause-game').disabled = !state.canPause;
  $('pause-game').textContent = /^Paused/.test(state.message) ? 'Resume' : 'Pause';
  $('end-session').disabled = restarting || busy || !selected;
  $('keyboard-layout').disabled = state.state !== 'available';
  if (!keyboardApplied && state.state === 'available' && player) {
    keyboardApplied = true;
    applyKeyboard();
  }
  $('progress').hidden = !(busy && state.state !== 'booting') && !loadingScene;
  if (state.progress) { $('progress').max = state.progress.total; $('progress').value = state.progress.complete; }
  else $('progress').removeAttribute('value');
  const paused = /^Paused/.test(state.message);
  $('status').hidden = !busy && !loadingScene && !currentError && !paused;
  $('status').disabled = !currentError && !paused;
  $('status').textContent = currentError ? 'Error' : paused ? 'Paused' : state.state === 'importing' ? 'Reading…' : state.state === 'unloading' ? 'Ejecting…' : 'Loading…';
  $('status').title = currentError || state.message;
  if (state.state === 'error' && state.message !== currentError) showError(state.message, true);
}

async function startPlayer() {
  renderStatus({state: 'booting', scene: 'idle', message: 'Starting player…'});
  try {
    // Validate the generated inventory against the shared roster. The native
    // CSS/SSS are the only visible catalog and the only selection authority.
    const response = await fetch(new URL('./prototype-content.json', import.meta.url));
    if (!response.ok) throw Error('Content inventory missing. Rebuild the prototype preview.');
    resolvePrototypeContent(await response.json());
    if (!window.isSecureContext) throw Error('Use HTTPS or a loopback HTTP server.');
    if (!navigator.gpu) throw Error('WebGPU unavailable. Use a desktop browser with WebGPU enabled.');
    if (!window.crossOriginIsolated) throw Error('Server isolation headers missing. Use scripts/serve.py.');
    player = mountPrototypePlayer({root: $('runtime-host'), onStatus: renderStatus});
  } catch (error) {
    renderStatus({state: 'error', scene: 'idle', message: error.message});
  }
}
$('choose-disc').onclick = () => { if (lastStatus?.canImport) $('disc-file').click(); };
$('disc-file').onchange = async () => {
  const file = $('disc-file').files[0];
  if (!file) return;
  clearError(); selected = true;
  try { await player.importDisc(file); $('start-game').focus(); }
  catch (error) { showError(error, lastStatus?.state === 'error'); }
  finally {
    $('disc-file').value = '';
    if (lastStatus) renderStatus(lastStatus);
  }
};
$('start-game').onclick = async () => {
  clearError();
  try { await player.openCharacterSelect(); }
  catch (error) { showError(error, lastStatus?.state === 'error'); }
};
$('pause-game').onclick = () => { clearError(); player?.pauseOrResume().catch(showError); };
async function restart() {
  if (restarting) return;
  restarting = true; $('retry').disabled = true; $('end-session').disabled = true;
  try { await player?.dispose(); }
  catch (error) { showError(error, true); }
  player = null; selected = false; keyboardApplied = false; clearError();
  restarting = false; $('retry').disabled = false;
  await startPlayer();
}
$('retry').onclick = $('end-session').onclick = restart;
$('status').onclick = () => {
  if (currentError) showError(currentError, fatalError);
  else if (lastStatus?.message) showError(lastStatus.message);
};
$('error-close').onclick = () => $('error-dialog').close();

const dialog = $('controls-dialog');
function detectControllers() {
  let pads;
  try { pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(pad => pad?.connected) : []; }
  catch { pads = []; }
  const descriptions = pads.map(pad => `${pad.index + 1}: ${pad.id}`);
  const key = JSON.stringify(descriptions);
  if (key === controllerKey) return;
  controllerKey = key;
  $('controllers').replaceChildren(...(descriptions.length ? descriptions : ['No controllers detected']).map(text => {
    const li = document.createElement('li'); li.textContent = text; return li;
  }));
}
$('controls-open').onclick = () => {
  dialog.showModal(); detectControllers();
  controllerTimer = setInterval(detectControllers, 1000);
};
$('controls-close').onclick = () => dialog.close();
dialog.addEventListener('close', () => { clearInterval(controllerTimer); player?.focus(); });
window.addEventListener('gamepadconnected', () => { if (dialog.open) detectControllers(); });
window.addEventListener('gamepaddisconnected', () => { if (dialog.open) detectControllers(); });
const keyboardPreferenceKey = 'melee-web-keyboard-v1';
try {
  const saved = JSON.parse(localStorage.getItem(keyboardPreferenceKey));
  if (saved && ['two', 'boxx'].includes(saved.layout)) {
    $('keyboard-layout').value = saved.layout;
    if (typeof saved.one === 'boolean') $('keyboard-one').checked = saved.one;
    if (typeof saved.two === 'boolean') $('keyboard-two').checked = saved.two;
  }
} catch { /* Controls work when local storage is unavailable. */ }
function renderKeyboard() {
  const boxx = $('keyboard-layout').value === 'boxx';
  $('keyboard-two-option').hidden = boxx;
  $('keyboard-note').hidden = !boxx;
  $('keyboard-one-label').textContent = boxx ? 'Keyboard' : 'P1 keyboard';
  const header = document.createElement('thead');
  const body = document.createElement('tbody');
  const rows = [boxx ? ['', 'P1'] : ['', 'P1', 'P2'], ...keyboardRows($('keyboard-layout').value, $('keyboard-two').checked)];
  rows.forEach((values, index) => {
    const row = document.createElement('tr');
    values.forEach((text, column) => {
      const cell = document.createElement(index === 0 || column === 0 ? 'th' : 'td');
      if (index === 0) cell.scope = 'col';
      else if (column === 0) cell.scope = 'row';
      cell.textContent = text; row.append(cell);
    });
    (index === 0 ? header : body).append(row);
  });
  $('keyboard-bindings').replaceChildren(header, body);
}
function applyKeyboard() {
  if (!player || !keyboardApplied) return;
  try {
    player.setKeyboardLayout($('keyboard-layout').value);
    player.setKeyboard(0, $('keyboard-one').checked);
    player.setKeyboard(1, $('keyboard-two').checked);
    appliedKeyboardLayout = $('keyboard-layout').value;
  } catch (error) {
    $('keyboard-layout').value = appliedKeyboardLayout;
    renderKeyboard(); showError(error);
  }
}
function changeKeyboard() {
  renderKeyboard(); applyKeyboard();
  try {
    localStorage.setItem(keyboardPreferenceKey, JSON.stringify({layout: $('keyboard-layout').value,
      one: $('keyboard-one').checked, two: $('keyboard-two').checked}));
  } catch { /* Session-only preference if storage is blocked. */ }
}
for (const id of ['keyboard-layout', 'keyboard-one', 'keyboard-two']) $(id).onchange = changeKeyboard;
renderKeyboard();

const fullscreenAvailable = !!document.fullscreenEnabled && typeof $('player').requestFullscreen === 'function';
$('fullscreen').disabled = !fullscreenAvailable;
$('fullscreen').title = fullscreenAvailable ? '' : 'Fullscreen unavailable in this browser';
$('fullscreen').onclick = async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await $('player').requestFullscreen();
    player?.focus();
  } catch { showError('Fullscreen was declined by the browser.'); }
};
document.addEventListener('fullscreenchange', () => {
  $('fullscreen').textContent = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen';
});
void startPlayer();
