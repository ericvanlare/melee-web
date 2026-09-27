import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {mountControllerSettings} from '../controller-settings.mjs';

const $ = id => document.getElementById(id);
let player, state, settings, currentError = '', requiresReload = false, hasStarted = false;
let discSelectionGeneration = 0, selectedDiscReady = false, selectedDiscFile = null;
let selectedDiscSession = null, selectedDiscValidated = false, importingSelection = null, importedSelection = null;
let selectedDiscMessage = '', validatingSelection = null;
let audioActivation = Promise.resolve(), audioActivationError = null;
const busyStates = ['booting', 'importing', 'preparing', 'pausing', 'resuming', 'unloading'];
const startReadinessWaiters = new Set();

function waitForStartReadiness(selection) {
  return new Promise(resolve => {
    const wake = () => {
      if (selection !== discSelectionGeneration || state?.requiresReload || state?.state === 'error') {
        startReadinessWaiters.delete(wake);
        resolve(false);
      } else if (state?.canStart) {
        startReadinessWaiters.delete(wake);
        resolve(true);
      }
    };
    startReadinessWaiters.add(wake);
    wake();
  });
}
function wakeStartReadinessWaiters() {
  for (const wake of [...startReadinessWaiters]) wake();
}

function renderDiscSelection() {
  const status = $('disc-selection-status');
  status.hidden = !selectedDiscFile || !selectedDiscMessage;
  status.textContent = selectedDiscFile && selectedDiscMessage ? `${selectedDiscMessage} ${selectedDiscFile.name}` : '';
}

async function validateSelectedDisc(selection, file) {
  if (!player || typeof player.openDiscSession !== 'function' ||
      selection !== discSelectionGeneration || validatingSelection === selection || state?.requiresReload) return;
  validatingSelection = selection;
  try {
    const session = await player.openDiscSession(file);
    if (selection !== discSelectionGeneration) {
      session.close();
      return;
    }
    if (state?.requiresReload) {
      session.close();
      selectedDiscMessage = 'Validation stopped';
      renderDiscSelection();
      if (!currentError) showError(state?.message || 'Player stopped while validating the selected disc.', true);
      return;
    }
    selectedDiscSession = session;
    selectedDiscValidated = true;
    selectedDiscMessage = state?.canImport ? 'Disc validated; preparing' : 'Disc validated; waiting for graphics';
    renderStatus(state);
  } catch (error) {
    if (selection === discSelectionGeneration) {
      if (state?.requiresReload) {
        selectedDiscMessage = 'Validation stopped';
        renderStatus(state);
        showError(error, true);
      } else {
        selectedDiscMessage = 'Invalid disc';
        renderDiscSelection();
        showError(error);
      }
    }
  } finally {
    if (validatingSelection === selection) validatingSelection = null;
  }
}

function maybeImportSelectedDisc() {
  if (!player || !state?.canImport || !selectedDiscValidated || !selectedDiscFile ||
      !selectedDiscSession || state.requiresReload) return;
  const selection = discSelectionGeneration;
  if (importingSelection === selection || importedSelection === selection) return;
  importingSelection = selection;
  importedSelection = selection;
  const file = selectedDiscFile, session = selectedDiscSession;
  selectedDiscSession = null;
  selectedDiscMessage = 'Preparing local data for';
  renderDiscSelection();
  void importAndStartSelectedDisc(selection, file, session);
}

async function importAndStartSelectedDisc(selection, file, session) {
  try {
    await player.importDisc(file, {preopenedSession: session});
    if (selection !== discSelectionGeneration) return;
    if (!await waitForStartReadiness(selection)) return;
    await audioActivation;
    if (selection !== discSelectionGeneration) return;
    if (!player.getState().canStart && !await waitForStartReadiness(selection)) return;
    selectedDiscReady = true;
    selectedDiscMessage = 'Disc ready';
    renderStatus(player.getState());
    if (player.getState().audio === 'enabled' && audioActivationError) {
      showError(audioActivationError);
      return;
    }
    await player.start({isCurrent: () => selection === discSelectionGeneration && selectedDiscReady});
    if (selection === discSelectionGeneration) {
      selectedDiscMessage = 'Playing';
      renderDiscSelection();
      player.focus();
    }
  } catch (error) {
    if (selection === discSelectionGeneration) {
      selectedDiscMessage = 'Could not prepare';
      renderDiscSelection();
      showError(error);
    }
  } finally {
    if (importingSelection === selection) importingSelection = null;
  }
}

function showError(error, fatal = false) {
  currentError = error?.message || String(error);
  requiresReload = fatal || !!state?.requiresReload;
  $('error').textContent = currentError;
  $('retry').hidden = !requiresReload;
  if (!$('error-dialog').open) $('error-dialog').showModal();
  renderStatus(state);
}
function clearError() {
  currentError = '';
  $('error').textContent = '';
  $('error-dialog').close();
}
function renderStatus(next) {
  if (!next) return;
  state = next;
  if (next.running) hasStarted = true;
  $('choose-disc').disabled = !next.canSelectDisc;
  $('start-game').disabled = !selectedDiscReady || !next.canStart;
  $('pause-game').disabled = !next.canPause;
  $('pause-game').textContent = next.paused ? 'Resume' : 'Pause';
  $('end-session').disabled = !next.canUnload;
  const busy = busyStates.includes(next.state);
  const loading = !currentError && next.state !== 'error' && !next.paused ? next.loading : null;
  $('loading-panel').hidden = !loading;
  $('idle-hint').hidden = hasStarted || !next.ready || next.busy || next.requiresReload || !!loading;
  if (loading) {
    $('loading-label').textContent = loading.message;
    const measured = Number.isFinite(loading.total) && loading.total > 0 && Number.isFinite(loading.complete);
    if (measured) {
      $('loading-progress').max = loading.total;
      $('loading-progress').value = Math.max(0, Math.min(loading.complete, loading.total));
      $('loading-detail').textContent = `${Math.floor($('loading-progress').value / loading.total * 100)}% complete`;
    } else {
      $('loading-progress').removeAttribute('value');
      $('loading-detail').textContent = 'First use can take longer.';
    }
  }
  $('progress').hidden = !busy || next.state === 'booting';
  if (next.progress) { $('progress').max = next.progress.total; $('progress').value = next.progress.complete; }
  else $('progress').removeAttribute('value');
  $('status').hidden = !busy && !next.paused && !currentError && next.state !== 'error';
  $('status').disabled = !currentError && !next.paused && next.state !== 'error';
  $('status').textContent = currentError || next.state === 'error' ? 'Error' : next.paused ? 'Paused' : next.state === 'importing' ? 'Reading…' : next.state === 'unloading' ? 'Ejecting…' : 'Loading…';
  $('status').title = currentError || next.message;
  if (next.requiresReload) {
    if (selectedDiscSession) selectedDiscSession.close();
    selectedDiscSession = null;
    selectedDiscValidated = false;
    selectedDiscReady = false;
    if (selectedDiscFile)
      selectedDiscMessage = validatingSelection !== null ? 'Validation stopped' : 'Preparation stopped';
    ++discSelectionGeneration;
  }
  renderDiscSelection();
  settings?.setState(next);
  wakeStartReadinessWaiters();
  maybeImportSelectedDisc();
}

$('choose-disc').onclick = () => $('disc-dialog').showModal();
$('disc-cancel').onclick = () => { $('disc-dialog').close(); player?.focus(); };
$('disc-choose-file').onclick = () => {
  if (!state?.canSelectDisc) return;
  // Start/resume Web Audio directly from this user gesture, before import and
  // native preparation can outlive the browser's transient activation window.
  audioActivationError = null;
  try {
    audioActivation = Promise.resolve(player?.activateAudio()).then(() => {}, error => { audioActivationError = error; });
  } catch (error) {
    audioActivationError = error;
    audioActivation = Promise.resolve();
  }
  $('disc-dialog').close(); $('disc-file').click();
};
$('disc-file').onchange = async () => {
  const file = $('disc-file').files[0];
  if (!file) return;
  const selection = ++discSelectionGeneration;
  selectedDiscSession?.close();
  selectedDiscSession = null;
  selectedDiscFile = file;
  selectedDiscValidated = false;
  selectedDiscReady = false;
  importingSelection = null;
  importedSelection = null;
  selectedDiscMessage = 'Checking local disc';
  clearError();
  renderStatus(state);
  await validateSelectedDisc(selection, file);
  $('disc-file').value = '';
};
$('disc-file').addEventListener('cancel', () => $('choose-disc').focus());
$('start-game').onclick = async () => {
  if (!selectedDiscReady || !state?.canStart) return;
  const selection = discSelectionGeneration;
  clearError();
  try { await player.start({isCurrent: () => selection === discSelectionGeneration && selectedDiscReady}); }
  catch (error) { if (selection === discSelectionGeneration) showError(error); }
};
$('pause-game').onclick = async () => { clearError(); try { await (state.paused ? player.resume() : player.pause()); } catch (error) { showError(error); } };
$('end-session').onclick = async () => {
  ++discSelectionGeneration;
  selectedDiscSession?.close();
  selectedDiscSession = null;
  selectedDiscFile = null;
  selectedDiscValidated = false;
  selectedDiscReady = false;
  selectedDiscMessage = '';
  renderDiscSelection();
  renderStatus(state);
  $('end-session').disabled = true;
  try { await player.destroy(); settings?.destroy(); location.reload(); }
  catch (error) { showError(error, true); }
};
$('retry').onclick = () => location.reload();
$('status').onclick = () => {
  if (currentError || state?.state === 'error') showError(currentError || state.message, state?.requiresReload);
  else if (state?.paused) showError(state.message);
};
$('error-close').onclick = () => { $('error-dialog').close(); player?.focus(); };
const audioInfo = $('audio-info'), audioDetails = $('audio-details');
function setAudioDetails(open) {
  audioDetails.classList.toggle('audio-details-open', open);
  audioDetails.setAttribute('aria-hidden', String(!open));
  audioInfo.setAttribute('aria-expanded', String(open));
}
if (audioInfo && audioDetails) {
  audioInfo.onclick = event => { event.stopPropagation(); setAudioDetails(!audioDetails.classList.contains('audio-details-open')); };
  audioInfo.onblur = () => setAudioDetails(false);
  audioInfo.onkeydown = event => { if (event.key === 'Escape') { setAudioDetails(false); audioInfo.blur(); } };
}

settings = mountControllerSettings({
  container: $('controls-dialog'),
  disableExtraPorts: true,
  openButton: $('controls-open'),
  focus: () => player?.focus(),
  onError: error => showError(error),
});

const fullscreenButton = $('fullscreen'), fullscreenStatus = $('fullscreen-status'), playerElement = $('player');
let fullscreenMode = typeof playerElement.requestFullscreen === 'function' &&
  typeof document.exitFullscreen === 'function' && document.fullscreenEnabled !== false ? 'native' : 'expand';
function renderFullscreenControl() {
  const native = document.fullscreenElement === playerElement;
  const expanded = playerElement.classList.contains('player-expanded');
  fullscreenButton.textContent = native ? 'Exit fullscreen' : expanded ? 'Shrink player' :
    fullscreenMode === 'native' ? 'Fullscreen' : 'Expand player';
  fullscreenButton.title = fullscreenMode === 'native' ? 'Enter or exit browser fullscreen' :
    'Expand the game within this page. Browser controls remain visible.';
}
function fullscreenRejected(action) {
  if (action === 'enter') fullscreenMode = 'expand';
  fullscreenStatus.textContent = action === 'enter'
    ? 'The browser declined fullscreen. Expand player enlarges the game within this page; browser controls remain visible.'
    : 'The browser could not exit fullscreen. Use its fullscreen exit gesture or key.';
  renderFullscreenControl();
}
fullscreenButton.disabled = false;
if (fullscreenMode === 'expand') {
  fullscreenStatus.textContent = 'Native fullscreen is unavailable here. Expand player enlarges the game within this page; browser controls remain visible.';
}
renderFullscreenControl();
fullscreenButton.onclick = () => {
  if (document.fullscreenElement) {
    try { Promise.resolve(document.exitFullscreen()).catch(() => fullscreenRejected('exit')); }
    catch { fullscreenRejected('exit'); }
    return;
  }
  if (fullscreenMode === 'native') {
    // requestFullscreen must be called synchronously from this trusted button
    // activation; do not await player work or another browser prompt first.
    try { Promise.resolve(playerElement.requestFullscreen()).then(() => player?.focus()).catch(() => fullscreenRejected('enter')); }
    catch { fullscreenRejected('enter'); }
    return;
  }
  const expanded = playerElement.classList.toggle('player-expanded');
  document.documentElement.classList.toggle('player-expanded', expanded);
  document.body.classList.toggle('player-expanded', expanded);
  fullscreenStatus.textContent = expanded
    ? 'Expanded within this page. Browser controls remain visible.'
    : 'Expanded player closed.';
  renderFullscreenControl();
  player?.focus();
};
document.addEventListener('fullscreenchange', () => {
  if (document.fullscreenElement === playerElement) fullscreenStatus.textContent = '';
  renderFullscreenControl();
});
playerElement.addEventListener('fullscreenerror', () => fullscreenRejected('enter'));

try {
  player = await mountMeleeRuntime({
    canvas: $('canvas'), onState: renderStatus, onError: error => showError(error),
    onOwner: owner => {
      player = owner.handle;
      renderStatus(player.getState());
      if (selectedDiscFile && !selectedDiscValidated)
        void validateSelectedDisc(discSelectionGeneration, selectedDiscFile);
    },
  });
  await settings.bindPlayer(player);
} catch (error) { showError(error, true); }
