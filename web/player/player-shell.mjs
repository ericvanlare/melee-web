import {mountMeleeRuntime} from '../melee-runtime.mjs';
import {mountControllerSettings} from '../controller-settings.mjs';

const $ = id => document.getElementById(id);
let player, state, settings, currentError = '', requiresReload = false, hasStarted = false;
const busyStates = ['booting', 'importing', 'preparing', 'pausing', 'resuming', 'unloading'];

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
  $('choose-disc').disabled = !next.canImport;
  $('start-game').disabled = !next.canStart;
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
  settings?.setState(next);
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

settings = mountControllerSettings({
  container: $('controls-dialog'),
  disableExtraPorts: true,
  openButton: $('controls-open'),
  focus: () => player?.focus(),
  onError: error => showError(error),
});

const fullscreenAvailable = !!document.fullscreenEnabled && typeof $('player').requestFullscreen === 'function';
$('fullscreen').disabled = !fullscreenAvailable;
$('fullscreen').title = fullscreenAvailable ? '' : 'Fullscreen unavailable in this browser';
$('fullscreen').onclick = async () => {
  try { if (document.fullscreenElement) await document.exitFullscreen(); else await $('player').requestFullscreen(); player?.focus(); }
  catch { showError('Fullscreen was declined by the browser.'); }
};
document.addEventListener('fullscreenchange', () => { $('fullscreen').textContent = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen'; });

try {
  player = await mountMeleeRuntime({
    canvas: $('canvas'), onState: renderStatus, onError: error => showError(error),
  });
  await settings.bindPlayer(player);
} catch (error) { showError(error, true); }
