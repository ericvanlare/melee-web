'use strict';
// This shell has no imports, network APIs, file readers or persistent storage.
const player = document.getElementById('player');
const button = document.getElementById('fullscreen');
const status = document.getElementById('fullscreen-status');
if (player && button && document.fullscreenEnabled && player.requestFullscreen) {
  button.hidden = false;
  button.addEventListener('click', async () => {
    status.textContent = '';
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await player.requestFullscreen();
    } catch {
      status.textContent = 'Fullscreen is unavailable here. You can keep using this page in the browser.';
    }
  });
  document.addEventListener('fullscreenchange', () => {
    button.textContent = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen';
  });
}
