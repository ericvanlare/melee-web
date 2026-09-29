'use strict';
// This shell has no imports, network APIs, file readers or persistent storage.
const player = document.getElementById('player');
const button = document.getElementById('fullscreen');
if (player && button) {
  const nativeFullscreenAvailable = document.fullscreenEnabled === true &&
    typeof player.requestFullscreen === 'function' && typeof document.exitFullscreen === 'function';
  button.hidden = !nativeFullscreenAvailable;
  if (nativeFullscreenAvailable) {
    function render() {
      button.textContent = document.fullscreenElement === player ? 'Exit fullscreen' : 'Fullscreen';
    }
    render();
    button.addEventListener('click', () => {
      if (document.fullscreenElement === player) {
        try { Promise.resolve(document.exitFullscreen()).catch(() => {}); }
        catch {}
        return;
      }
      // Keep the native request in the direct user gesture. Rejection is a
      // no-op: the same supported control remains available for a later gesture.
      try { Promise.resolve(player.requestFullscreen()).catch(() => {}); }
      catch {}
    });
    document.addEventListener('fullscreenchange', render);
    player.addEventListener('fullscreenerror', render);
  }
}
