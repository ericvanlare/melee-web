/**
 * TEMPORARY same-origin DOM adapter. See docs/PROTOTYPE.md for its removal gate.
 * Only this module knows runtime.html's DOM. Its one native configuration call
 * selects a keyboard layout. It never replaces host callbacks, samples input,
 * injects PAD samples, or implements a second game loop.
 */
export function mountPrototypePlayer({root, onStatus = () => {}, onSceneChange = () => {}}) {
  const frame = document.createElement('iframe');
  frame.title = 'Original Melee character select, stage select and game';
  frame.allow = 'autoplay; fullscreen; gamepad';
  frame.src = new URL('./runtime.html', import.meta.url).href;
  let doc, timer, watchdog, disposed = false, busy = false, started = false;
  let lastKey = '', lastScene = '', fault = '', operation = '', keyboardLayout = 'two';
  const $ = id => doc?.getElementById(id);
  const emit = state => {
    const key = JSON.stringify(state);
    if (key !== lastKey) { lastKey = key; onStatus(Object.freeze(state)); }
    if (state.scene !== lastScene) { lastScene = state.scene; onSceneChange(state.scene); }
  };
  // These phases are the existing #status data-phase values, not simulation state.
  const scenes = {1: 'character-select', 2: 'preparing', 3: 'stage-select', 4: 'preparing', 5: 'preparing', 6: 'closed', 7: 'match'};
  function snapshot() {
    const message = $('status')?.textContent.trim() || 'Starting the player…';
    const phase = $('status')?.dataset.phase;
    const scene = scenes[phase] || 'idle';
    const reading = message.match(/^Reading local data (\d+)\/(\d+)$/);
    const fatal = fault || (/^Stopped:/.test(message) ? message : '');
    return {
      state: fatal ? 'error' : busy ? operation : !started ? 'booting' : 'available',
      message: fatal || message, scene,
      progress: reading ? {complete: Number(reading[1]), total: Number(reading[2])} : null,
      canImport: started && !busy && !fatal && !$('disc').disabled,
      canLaunch: started && !busy && !fatal && !$('launch').disabled,
      canPause: started && !busy && !fatal && !$('pause').disabled,
      canUnload: started && !busy && !fatal && !$('unload').disabled,
    };
  }
  function report() {
    if (disposed) return;
    if (!started && $('disc') && !$('disc').disabled) { started = true; clearTimeout(watchdog); }
    emit(snapshot());
  }
  function fail(message) { fault = message; clearTimeout(watchdog); report(); }
  frame.addEventListener('load', () => {
    if (disposed) return;
    try {
      doc = frame.contentDocument;
      for (const id of ['canvas', 'disc', 'launch', 'pause', 'unload', 'status', 'keyboard', 'keyboard2']) {
        if (!$(id)) throw Error(`Player adapter is incompatible: missing ${id}. Rebuild the preview with a matching runtime.`);
      }
      const style = doc.createElement('style');
      style.textContent = `html,body{margin:0!important;padding:0!important;width:100%!important;height:100%!important;max-width:none!important;background:#000!important;overflow:hidden!important}body{display:flex!important;align-items:center;justify-content:center}body>*:not(canvas){display:none!important}canvas{display:block!important;width:min(100vw,calc(100vh * 4 / 3))!important;height:auto!important;max-height:100vh;aspect-ratio:4/3;object-fit:contain;outline-offset:-3px!important}canvas:focus-visible{outline:2px solid #aaa!important}`;
      doc.head.append(style);
      frame.dataset.attached = '';
      report();
      clearInterval(timer);
      timer = setInterval(report, 250); // Display updates only; never drives source ticks.
    } catch (error) { fail(error.message); }
  });
  frame.addEventListener('error', () => fail('The player could not load. Restart the player or check the preview server.'));
  watchdog = setTimeout(() => fail('Player startup timed out. Check WebGPU support and the compiled game files, then restart.'), 60000);
  root.replaceChildren(frame);
  report();

  async function command(kind, fn) {
    if (disposed || busy || fault) throw Error('The player is not ready for another action.');
    busy = true; operation = kind; report();
    let timeout;
    try {
      await Promise.race([Promise.resolve().then(fn), new Promise((_, reject) => {
        timeout = setTimeout(() => reject(Error('The player did not finish this action. Restart it to recover.')), 60000);
      })]);
    } catch (error) { fail(error.message); throw error; }
    finally { clearTimeout(timeout); busy = false; operation = ''; report(); }
  }
  const focus = () => { if (!disposed) { frame.contentWindow?.focus(); $('canvas')?.focus(); } };
  return Object.freeze({
    async importDisc(file) {
      if (!snapshot().canImport) throw Error('Wait for the player before selecting a disc.');
      await command('importing', async () => {
        // Use the existing file-selection handler, including its hash validation,
        // bounded reader, native upload, preparation, error and teardown paths.
        // Construct the File in the child's realm: the existing disc reader
        // checks ArrayBuffer identity there. Blob parts retain bounded lazy reads.
        const child = frame.contentWindow;
        const transfer = new child.DataTransfer();
        transfer.items.add(new child.File([file], file.name, {type: file.type, lastModified: file.lastModified}));
        $('disc').files = transfer.files;
        await $('disc').onchange(new Event('change'));
      });
      if (!snapshot().canLaunch) throw Error($('status').textContent || 'Disc preparation failed. Choose a valid disc and try again.');
    },
    async openCharacterSelect() {
      if (!snapshot().canLaunch) throw Error('Prepare a valid local disc first.');
      await command('preparing', () => $('launch').onclick());
      if (!snapshot().canPause) throw Error($('status').textContent || 'Character select could not open.');
      focus();
    },
    async pauseOrResume() {
      if (!snapshot().canPause) throw Error('There is no active scene to pause.');
      // The legacy handler does not return its queued promise. Serialize the
      // click until its displayed pause state changes; never queue another toggle.
      const wasPaused = /^Paused/.test($('status').textContent);
      let observer;
      try {
        await command('pausing', () => new Promise((resolve, reject) => {
          observer = new MutationObserver(() => {
            const message = $('status').textContent;
            if (/^Stopped:/.test(message)) reject(Error(message));
            else if (/^Paused/.test(message) !== wasPaused) resolve();
          });
          observer.observe($('status'), {childList: true, characterData: true, subtree: true});
          $('pause').click();
        }));
      } finally { observer?.disconnect(); }
    },
    setKeyboardLayout(layout) {
      if (!['two', 'boxx'].includes(layout)) throw Error('Unknown keyboard layout.');
      if (!started || disposed) throw Error('Wait for the player before changing controls.');
      const sharedSettings = frame.contentWindow?.meleeControllerSettings;
      if (sharedSettings?.setLayout) {
        // Keep the hidden runtime settings view in sync with the prototype
        // host. Iframe overrides are explicitly session-only.
        void sharedSettings.setLayout(layout, {persist: false}).catch(error => fail(error.message));
        keyboardLayout = layout;
        return;
      }
      // Configuration only, on the same main thread as the existing input owner.
      // No frame scheduling or diagnostic PAD injection is involved.
      const setLayout = frame.contentWindow?.Module?._melee_web_input_set_keyboard_layout;
      if (!setLayout) {
        if (layout !== 'two') throw Error('Rebuild the game runtime to enable B0XX controls.');
      } else if (setLayout(layout === 'boxx' ? 1 : 0) !== 1) throw Error('Keyboard layout could not be applied.');
      keyboardLayout = layout;
    },
    setKeyboard(slot, enabled) {
      const input = $(slot === 0 ? 'keyboard' : 'keyboard2');
      if (!input || disposed) return;
      input.checked = !!enabled && (slot === 0 || keyboardLayout !== 'boxx'); input.dispatchEvent(new Event('change'));
    },
    focus,
    async dispose() {
      if (disposed) return;
      // Unload performs the original audio acknowledgement and cache-idle save.
      // Removing the document afterward retires its heap, imported data and graph.
      try {
        if (!busy && !fault && started) {
          await command('unloading', () => $('unload').onclick());
        }
      } finally {
        disposed = true; clearTimeout(watchdog); clearInterval(timer);
        frame.remove(); doc = null;
      }
    },
  });
}
