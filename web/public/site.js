'use strict';
// This shell has no imports, network APIs, file readers or persistent storage.
const player = document.getElementById('player');
const button = document.getElementById('fullscreen');
const status = document.getElementById('fullscreen-status');
if (player && button && status) {
  let mode = typeof player.requestFullscreen === 'function' &&
    typeof document.exitFullscreen === 'function' && document.fullscreenEnabled !== false ? 'native' : 'expand';
  button.hidden = false;
  button.disabled = false;
  function render() {
    button.textContent = document.fullscreenElement === player ? 'Exit fullscreen' :
      player.classList.contains('player-expanded') ? 'Shrink player' : mode === 'native' ? 'Fullscreen' : 'Expand player';
    button.title = mode === 'native' ? 'Enter or exit browser fullscreen' :
      'Expand the page content. Browser controls remain visible.';
  }
  function rejected(action) {
    if (action === 'enter') mode = 'expand';
    status.textContent = action === 'enter'
      ? 'The browser declined fullscreen. Expand player makes the game area larger within this page; browser controls remain visible.'
      : 'The browser could not exit fullscreen. Use its fullscreen exit gesture or key.';
    render();
  }
  if (mode === 'expand') {
    status.textContent = 'Native fullscreen is unavailable here. Expand player uses this page; browser controls remain visible.';
  }
  render();
  button.addEventListener('click', () => {
    if (document.fullscreenElement) {
      try { Promise.resolve(document.exitFullscreen()).catch(() => rejected('exit')); }
      catch { rejected('exit'); }
      return;
    }
    if (mode === 'native') {
      // Keep the native request in the direct button gesture required by browsers.
      try { Promise.resolve(player.requestFullscreen()).catch(() => rejected('enter')); }
      catch { rejected('enter'); }
      return;
    }
    const expanded = player.classList.toggle('player-expanded');
    document.documentElement.classList.toggle('player-expanded', expanded);
    document.body.classList.toggle('player-expanded', expanded);
    status.textContent = expanded ? 'Expanded within this page. Browser controls remain visible.' : 'Expanded player closed.';
    render();
  });
  document.addEventListener('fullscreenchange', () => {
    if (document.fullscreenElement === player) status.textContent = '';
    render();
  });
  player.addEventListener('fullscreenerror', () => rejected('enter'));
}
