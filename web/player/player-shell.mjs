import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {mountControllerSettings} from '../controller-settings.mjs';
import {mountSaveProfileSettings} from '../save-profile-settings.mjs';
import {mountDiagnosticsSettings} from '../diagnostics-settings.mjs';

const $ = id => document.getElementById(id);
let player, state, settings, saveSettings, diagnosticSettings, saveSettingsBinding = null;
let saveSettingsReady = false, currentError = '', requiresReload = false, hasStarted = false;
let discSelectionGeneration = 0, selectedDiscReady = false, selectedDiscFile = null;
let selectedDiscSession = null, selectedDiscValidated = false, importingSelection = null, importedSelection = null;
let selectedDiscMessage = '', validatingSelection = null;
let audioActivation = Promise.resolve(), audioActivationError = null;
const busyStates = ['booting', 'importing', 'preparing', 'pausing', 'resuming', 'unloading', 'saving'];
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
      selectedDiscMessage = '';
      renderDiscSelection();
      if (!currentError) showError(state?.message || 'Player stopped while validating the selected disc.', true);
      return;
    }
    selectedDiscSession = session;
    selectedDiscValidated = true;
    selectedDiscMessage = state?.canImport ? 'Preparing' : 'Waiting for graphics';
    renderStatus(state);
  } catch (error) {
    if (selection === discSelectionGeneration) {
      if (state?.requiresReload) {
        selectedDiscMessage = '';
        renderStatus(state);
        showError(error, true);
      } else {
        selectedDiscMessage = '';
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
      !selectedDiscSession || state.requiresReload || !saveSettingsReady || saveSettings?.busy) return;
  if (saveSettings?.blocked) {
    selectedDiscMessage = 'Save settings required';
    renderDiscSelection();
    return;
  }
  const selection = discSelectionGeneration;
  if (importingSelection === selection || importedSelection === selection) return;
  importingSelection = selection;
  importedSelection = selection;
  const file = selectedDiscFile, session = selectedDiscSession;
  selectedDiscSession = null;
  selectedDiscMessage = 'Preparing';
  renderDiscSelection();
  void importAndStartSelectedDisc(selection, file, session);
}

async function importAndStartSelectedDisc(selection, file, session) {
  try {
    await player.importDisc(file, {preopenedSession: session});
    if (selection !== discSelectionGeneration) return;
    if (!saveSettingsReady || saveSettings?.blocked || saveSettings?.busy)
      throw Error('Save settings are not ready. Resolve the save status in Settings before starting.');
    if (!await waitForStartReadiness(selection)) return;
    await audioActivation;
    if (selection !== discSelectionGeneration) return;
    if (!player.getState().canStart && !await waitForStartReadiness(selection)) return;
    selectedDiscReady = true;
    renderStatus(player.getState());
    if (player.getState().audio === 'enabled' && audioActivationError) {
      showError(audioActivationError);
      return;
    }
    await player.start({isCurrent: () => selection === discSelectionGeneration && selectedDiscReady});
    if (selection === discSelectionGeneration) {
      selectedDiscMessage = '';
      renderDiscSelection();
      player.focus();
    }
  } catch (error) {
    if (selection === discSelectionGeneration) {
      selectedDiscMessage = '';
      renderDiscSelection();
      showError(error);
    }
  } finally {
    if (importingSelection === selection) importingSelection = null;
  }
}

function showError(error, fatal = false) {
  selectedDiscMessage = '';
  renderDiscSelection();
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
  const saveBusy = !!saveSettings?.busy;
  $('choose-disc').disabled = !next.canSelectDisc;
  $('start-game').disabled = !selectedDiscReady || !next.canStart || !saveSettingsReady ||
    !!saveSettings?.blocked || saveBusy;
  $('pause-game').disabled = !next.canPause || saveBusy;
  $('pause-game').textContent = next.paused ? 'Resume' : 'Pause';
  $('end-session').disabled = !next.canUnload || saveBusy;
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
  $('status').textContent = currentError || next.state === 'error' ? 'Error' : next.paused ? 'Paused' :
    next.state === 'importing' ? 'Reading…' : next.state === 'unloading' ? 'Ejecting…' :
      next.state === 'saving' ? 'Saving…' : 'Loading…';
  $('status').title = currentError || next.message;
  if (next.requiresReload) {
    if (selectedDiscSession) selectedDiscSession.close();
    selectedDiscSession = null;
    selectedDiscValidated = false;
    selectedDiscReady = false;
    selectedDiscMessage = '';
    ++discSelectionGeneration;
  }
  renderDiscSelection();
  settings?.setState(next);
  saveSettings?.setState(next);
  diagnosticSettings?.setState(next);
  wakeStartReadinessWaiters();
  maybeImportSelectedDisc();
}

function bindSaveSettings(runtime) {
  if (saveSettingsBinding) return saveSettingsBinding;
  saveSettingsBinding = saveSettings.bindPlayer(runtime).then(() => {
    saveSettingsReady = true;
    $('settings-open').disabled = false;
    renderStatus(runtime.getState());
  }).catch(error => {
    showError(error, true);
    throw error;
  });
  return saveSettingsBinding;
}

$('choose-disc').onclick = () => $('disc-dialog').showModal();
function clearSelectedDiscAcknowledgement() {
  selectedDiscMessage = '';
  renderDiscSelection();
}
$('disc-cancel').onclick = () => { clearSelectedDiscAcknowledgement(); $('disc-dialog').close(); player?.focus(); };
$('disc-dialog').addEventListener('cancel', clearSelectedDiscAcknowledgement);
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
$('disc-file').addEventListener('cancel', () => {
  clearSelectedDiscAcknowledgement();
  $('choose-disc').focus();
});
$('start-game').onclick = async () => {
  if (!selectedDiscReady || !state?.canStart || !saveSettingsReady || saveSettings?.blocked || saveSettings?.busy) return;
  const selection = discSelectionGeneration;
  clearError();
  try {
    await player.start({isCurrent: () => selection === discSelectionGeneration && selectedDiscReady});
    if (selection === discSelectionGeneration) clearSelectedDiscAcknowledgement();
  } catch (error) {
    if (selection === discSelectionGeneration) {
      clearSelectedDiscAcknowledgement();
      showError(error);
    }
  }
};
$('pause-game').onclick = async () => { clearError(); try { await (state.paused ? player.resume() : player.pause()); } catch (error) { showError(error); } };
$('end-session').onclick = async () => {
  // Release virtual PAD state before profile teardown, which may be delayed
  // while its native owner stops. Eject must never leave a held touch behind.
  settings?.clearTouchInputs?.();
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
  let teardownStarted = false;
  try {
    await saveSettings.flushBeforeTeardown();
    teardownStarted = true;
    await player.destroy();
    settings?.destroy();
    location.reload();
  } catch (error) {
    showError(error, teardownStarted);
    $('end-session').disabled = !state?.canUnload;
  }
};
$('retry').onclick = () => location.reload();
$('status').onclick = () => {
  if (currentError || state?.state === 'error') showError(currentError || state.message, state?.requiresReload);
  else if (state?.paused) showError(state.message);
};
$('error-close').onclick = () => { clearError(); renderStatus(state); player?.focus(); };
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
saveSettings = mountSaveProfileSettings({onError: error => showError(error), onBusy: () => renderStatus(state)});
diagnosticSettings = mountDiagnosticsSettings();
$('settings-open').disabled = true;

const fullscreenButton = $('fullscreen'), playerElement = $('player');
const nativeFullscreenAvailable = document.fullscreenEnabled === true &&
  typeof playerElement.requestFullscreen === 'function' && typeof document.exitFullscreen === 'function';
fullscreenButton.hidden = !nativeFullscreenAvailable;
function renderFullscreenControl() {
  if (nativeFullscreenAvailable)
    fullscreenButton.textContent = document.fullscreenElement === playerElement ? 'Exit fullscreen' : 'Fullscreen';
}
renderFullscreenControl();
if (nativeFullscreenAvailable) {
  fullscreenButton.onclick = () => {
    if (document.fullscreenElement === playerElement) {
      try { Promise.resolve(document.exitFullscreen()).catch(() => {}); }
      catch {}
      return;
    }
    // Keep requestFullscreen synchronous in this trusted button activation.
    // A rejected request leaves the supported control in its ordinary state.
    try { Promise.resolve(playerElement.requestFullscreen()).then(() => player?.focus(), () => {}); }
    catch {}
  };
  document.addEventListener('fullscreenchange', renderFullscreenControl);
  playerElement.addEventListener('fullscreenerror', renderFullscreenControl);
}

try {
  player = await mountMeleeRuntime({
    canvas: $('canvas'), onState: renderStatus, onError: error => showError(error),
    onOwner: owner => {
      player = owner.handle;
      diagnosticSettings.bindPlayer(player);
      renderStatus(player.getState());
      // Save storage is bound after the native startup boundary becomes usable.
      // A prevalidated disc can wait here without entering an unconfigured session.
      if (selectedDiscFile && !selectedDiscValidated)
        void validateSelectedDisc(discSelectionGeneration, selectedDiscFile);
    },
  });
  await bindSaveSettings(player);
  await settings.bindPlayer(player);
} catch (error) { showError(error, true); }
