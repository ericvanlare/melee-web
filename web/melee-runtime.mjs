/** One player owner per document. Native source ticks remain owned by the compiled player. */
import {loadNativeGameDisc} from './runtime-assets.mjs';
import {createControllerManager} from './controller-input.mjs';

let documentClaimed = false;
const SCENES = {1: 'css', 2: 'preparing', 3: 'sss', 4: 'preparing', 5: 'preparing', 6: 'unloaded', 7: 'match', 8: 'results', 9: 'prize'};
const IMPORT_BATCH_MAX_FILES = 8;
const IMPORT_BATCH_MAX_BYTES = 8 * 1024 * 1024;
const IMPORT_BATCH_MAX_MS = 8;

export async function mountMeleeRuntime({canvas, onState = () => {}, onError = () => {},
  onEvent = () => {}, onLog = () => {}, onOwner, configureModule,
  readDisc = loadNativeGameDisc, openDisc, createAudio,
  loaderUrl = new URL('./gameplay_public.js', import.meta.url), startupTimeout = 60000} = {}) {
  if (!canvas || canvas.id !== 'canvas') throw Error('The player requires its own #canvas.');
  if (documentClaimed) throw Error('Reload the page to start a fresh player.');
  if (!globalThis.isSecureContext) throw Error('The player requires HTTPS or a local server.');
  if (!navigator.gpu) throw Error('WebGPU is unavailable. Try a desktop browser with WebGPU enabled.');
  if (!globalThis.crossOriginIsolated) throw Error('The player requires cross-origin isolation headers.');
  documentClaimed = true;
  const assetBase = new URL('.', loaderUrl);
  let ready = false, fatal = false, destroyed = false, bundle = false, prepared = false, hasLocalData = false;
  let startupCacheReady = false;
  let startupTimeoutHandle = null;
  let busy = '', message = '', progress = null, inputDirty = true, lastState = '';
  let loading = Object.freeze({phase: 'boot', message: 'Starting player…', complete: 0, total: 0});
  let preparationLabel = '', preparationKeepsAudio = false;
  let discSession = null, assetTransfer = null;
  let keyboard = [true, true], layout = 'two';
  const commands = [], listeners = [];
  const audio = createAudio?.({assetBase, onEvent: data => emit('audio', data),
    onError: error => { message = error.message; onError(error); publish(); }, onFatal: stop});
  const emit = (name, data) => onEvent(name, data);
  let resolveStartup, rejectStartup;
  const startup = new Promise((resolve, reject) => { resolveStartup = resolve; rejectStartup = reject; });
  const Module = {
    canvas,
    meleeControllers: createControllerManager(),
    locateFile: name => new URL(name, assetBase).href,
    print: text => onLog(String(text), false),
    printErr: text => onLog(String(text), true),
    onAbort: error => stop(error),
    onRuntimeInitialized() { ready = true; publish(); resolveStartup(); },
  };
  const status = () => ready ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : 'Starting WebGPU…';
  const check = result => { if (!result) throw Error(status()); return result; };
  const numericProgress = (value, fallback = 0) => Number.isFinite(Number(value)) ? Math.max(0, Math.floor(Number(value))) : fallback;
  function readNativeCacheIdle() {
    const idle = Module._melee_web_native_menu_cache_idle;
    if (typeof idle !== 'function') throw Error('The native renderer-cache readiness service is unavailable. Reload to recover.');
    const state = idle();
    if (![1, 0, -1].includes(state)) throw Error(`Invalid native cache idle state: ${state}`);
    return state;
  }
  function clearStartupTimeout() {
    if (startupTimeoutHandle !== null) {
      clearTimeout(startupTimeoutHandle);
      startupTimeoutHandle = null;
    }
  }
  function refreshStartupCacheReadiness() {
    if (!ready || startupCacheReady || fatal || destroyed) return;
    let state;
    try { state = readNativeCacheIdle(); }
    catch (error) { stop(error); return; }
    // Native -1 means the optional renderer cache failed; it is still safe to
    // import the disc, and unloadAndSave preserves that failure explicitly.
    if (state !== 0) {
      startupCacheReady = true;
      clearStartupTimeout();
    }
  }
  function setLoading(phase, text, complete, total) {
    const normalizedTotal = Math.max(0, numericProgress(total));
    const normalizedComplete = Math.min(normalizedTotal, numericProgress(complete));
    loading = Object.freeze({phase, message: text, complete: normalizedComplete, total: normalizedTotal});
  }
  function refreshCatalogLoading() {
    if (fatal || destroyed) { loading = null; return; }
    if (!ready) return;
    if (['disc', 'handoff', 'native'].includes(loading?.phase)) return;
    const preparation = Module.pipelinePreparation;
    if (!startupCacheReady) {
      if (preparation && typeof preparation === 'object' && preparation.ready !== true) {
        const selected = numericProgress(preparation.selected);
        const pending = numericProgress(preparation.pending);
        const total = pending > 0 ? Math.max(selected, pending) : 0;
        const complete = total > 0 ? Math.max(0, total - pending) : 0;
        setLoading('catalog', 'Preparing graphics…', complete, total);
      } else if (loading?.phase === 'boot' || loading?.phase === 'catalog') {
        setLoading('catalog', 'Preparing graphics…', 0, 0);
      }
      return;
    }
    if (!preparation || typeof preparation !== 'object') {
      if (loading?.phase === 'boot' || loading?.phase === 'catalog') loading = null;
      return;
    }
    if (preparation.ready) {
      if (loading?.phase === 'boot' || loading?.phase === 'catalog') loading = null;
      return;
    }
    const selected = numericProgress(preparation.selected);
    const pending = numericProgress(preparation.pending);
    const total = pending > 0 ? Math.max(selected, pending) : 0;
    // `pending` is the remaining count in the native status. Keep an active
    // catalog operation visibly incomplete until native reports ready.
    const complete = total > 0 ? Math.max(0, total - pending) : 0;
    setLoading('catalog', 'Preparing graphics…', complete, total);
  }
  function snapshot() {
    refreshCatalogLoading();
    const phase = ready && !fatal && !destroyed ? Module._melee_web_native_menu_phase() : 0;
    const running = ready && !fatal && !destroyed && !!Module._melee_web_native_menu_running();
    const scene = SCENES[phase] || 'idle';
    const active = ['css', 'sss', 'match', 'results', 'prize'].includes(scene);
    const paused = active && !running && !preparationLabel && !busy;
    const state = destroyed ? 'destroyed' : fatal ? 'error' : !ready ? 'booting' : busy ||
      (preparationLabel ? 'preparing' : paused ? 'paused' : active ? scene : prepared ? 'prepared' : 'idle');
    return Object.freeze({version: 1, state, scene, phase, running, paused, audio: audio ? 'enabled' : 'disabled',
      message: message || preparationLabel || status(), progress, loading,
      ready, bundle, busy: !!busy, requiresReload: destroyed || fatal,
      canImport: ready && startupCacheReady && !fatal && !destroyed && !busy && !preparationLabel,
      canStart: ready && startupCacheReady && bundle && !active && !fatal && !destroyed && !busy && !preparationLabel,
      canPause: active && !fatal && !destroyed && !busy && !preparationLabel,
      canUnload: ready && (bundle || hasLocalData) && !fatal && !destroyed && !busy,
    });
  }
  function publish() {
    const state = snapshot(), key = JSON.stringify(state);
    if (key !== lastState) { lastState = key; onState(state); }
    return state;
  }
  function stop(error) {
    if (fatal || destroyed) return;
    fatal = true; message = String(error?.message || error || 'Player stopped. Reload to recover.');
    clearStartupTimeout();
    discSession?.close(); discSession = null;
    preparationLabel = ''; preparationKeepsAudio = false; loading = null;
    syncAudio();
    for (const c of commands.splice(0)) c.reject(Error(message));
    audio?.fail(Error(message));
    rejectStartup(Error(message)); publish(); onError(Error(message)); emit('fatal', message);
  }
  const boundary = run => fatal || destroyed ? Promise.reject(Error('Reload after the player stopped.')) :
    new Promise((resolve, reject) => commands.push({run, resolve, reject}));
  async function operation(name, run, requireStartupCache = false) {
    if (!ready || fatal || destroyed) throw Error('The player is unavailable. Reload to recover.');
    if (requireStartupCache && !startupCacheReady) throw Error('Graphics are still preparing. Wait for startup preparation to finish.');
    if (busy) throw Error('Wait for the current player operation to finish.');
    busy = name; message = ''; progress = null; publish();
    let timeout;
    try {
      return await Promise.race([run(), new Promise((_, reject) => {
        timeout = setTimeout(() => { const error = Error('The player stopped responding. Reload to recover.'); stop(error); reject(error); }, 60000);
      })]);
    } catch (error) { callbacks.menuPreparationFailed(error.message || String(error)); throw error; }
    finally { clearTimeout(timeout); busy = ''; progress = null; publish(); }
  }
  const waitForAudioAck = () => audio?.waitForAck() || Promise.resolve();
  const prepareAudio = async () => { await audio?.prepare(); };
  const pauseAudioForPreparation = async () => { await audio?.pause(); };
  function syncAudio() {
    const running = ready && !fatal && !destroyed && !!Module._melee_web_native_menu_running();
    const enabled = ready && !fatal && !destroyed && (running || preparationKeepsAudio) && !document.hidden;
    if (running) preparationKeepsAudio = false;
    audio?.setEnabled(enabled);
  }
  const callbacks = {
    menuAudio(pcm) {
      if (!audio) throw Error('Audio output is disabled in this public alpha.');
      audio.write(pcm);
    },
    // These two callbacks MUST remain synchronous at their native boundaries.
    menuAudioReadyForPreparation() { return preparationKeepsAudio || !audio || audio.readyForPreparation(); },
    menuServiceCommands() {
      if (fatal || destroyed) return;
      for (const c of commands.splice(0)) { try { c.resolve(c.run()); } catch (error) { c.reject(error); } }
      if (inputDirty) {
        inputDirty = false;
        Module._melee_web_input_set_keyboard(keyboard[0] ? 1 : 0);
        Module._melee_web_input_set_keyboard_port(1, keyboard[1] && layout !== 'boxx' ? 1 : 0);
        Module._melee_web_input_set_activity(document.hasFocus() && document.activeElement === canvas ? 1 : 0,
          document.hidden ? 0 : 1);
      }
    },
    menuPreparation(label, keepAudio = false) { preparationLabel = label || 'Preparing original scene'; preparationKeepsAudio = !!keepAudio; message = ''; setLoading('native', 'Preparing game data…', 0, 0); emit('preparation', {label: preparationLabel, keepAudio}); publish(); },
    menuPreparationDone() { preparationLabel = ''; message = ''; if (loading?.phase === 'native') { loading = null; refreshCatalogLoading(); } emit('preparationDone'); publish(); },
    menuPreparationCanceled() { preparationLabel = ''; preparationKeepsAudio = false; message = ''; if (loading?.phase === 'native') loading = null; emit('preparationCanceled'); publish(); },
    menuPreparationFailed(error) { preparationLabel = ''; preparationKeepsAudio = false; message = error || 'Native preparation failed'; if (loading?.phase === 'native') loading = null; emit('preparationFailed', message); publish(); },
    menuAssetsRequested(generation) {
      // Native only requests after closing the outgoing owners. Keep its
      // Constructing gate stopped until the complete scope commits.
      if (assetTransfer) { stop(Error('A native asset transfer is already active.')); return; }
      prepared = false;
      assetTransfer = operation('preparing', () => transferScope(generation))
        .catch(stop).finally(() => { assetTransfer = null; });
    },
    menuAssetScopeReleased(receipt) { emit('assetScopeReleased', receipt); },
    menuRenderCacheSettled() { Module.markRuntimeCacheDirty?.(); emit('cacheSettled'); },
    menuFrame(wasRunning) {
      if (!ready || fatal || destroyed) return;
      // Emscripten's onRuntimeInitialized precedes main/Aurora initialization.
      // Only a real native frame can establish renderer-cache readiness.
      refreshStartupCacheReadiness();
      if (fatal || destroyed) return;
      syncAudio(); publish(); emit('frame', wasRunning);
    },
  };
  for (const [name, callback] of Object.entries(callbacks)) window[name] = callback;
  function listen(type, listener) { window.addEventListener(type, listener, true); listeners.push([type, listener]); }
  for (const type of ['focus', 'blur', 'visibilitychange', 'focusin', 'focusout']) listen(type, () => { inputDirty = true; emit('focus'); });
  listen('error', event => stop(event.error || event.message));
  listen('unhandledrejection', event => stop(event.reason));
  const focus = () => { if (!fatal && !destroyed) { canvas.focus(); inputDirty = true; } };
  async function unloadAndSave() {
    const unloaded = await boundary(() => { syncAudio(); return Module._melee_web_native_menu_unload(); });
    if (!unloaded) return false;
    prepared = false; callbacks.menuPreparationCanceled(); await pauseAudioForPreparation();
    const deadline = performance.now() + 30000;
    let cacheState = 0;
    while ((cacheState = await boundary(readNativeCacheIdle)) === 0) {
      if (performance.now() > deadline) throw Error('Pending renderer work did not drain. Reload to recover.');
    }
    if (cacheState !== 1) {
      emit('cacheWriteFailed', 'Optional render cache was not saved: native cache writes failed. Reload to retry storage.');
      return true;
    }
    if (Module.runtimeCacheState?.dirty) await Module.saveRuntimeCache();
    return true;
  }
  function putNow(name, bytes, generation = 0) {
    const encoded = new TextEncoder().encode(name + '\0');
    const np = Module._malloc(encoded.length), bp = Module._malloc(bytes.length);
    try {
      if (!np || !bp) throw Error('Allocation failed.');
      Module.HEAPU8.set(encoded, np); Module.HEAPU8.set(bytes, bp);
      check(generation ? Module._melee_web_native_asset_file(generation, np, bp, bytes.length) :
        Module._melee_web_native_menu_file(np, bp, bytes.length));
      hasLocalData = true;
    } finally { Module._free(np); Module._free(bp); }
  }
  async function put(name, bytes) {
    await boundary(() => putNow(name, bytes));
  }
  async function putBatches(entries, generation = 0) {
    let complete = 0;
    const total = entries.length;
    while (complete < total) {
      const offset = complete;
      const processed = await boundary(() => {
        const started = performance.now();
        let count = 0, bytes = 0;
        while (offset + count < total) {
          const [name, data] = entries[offset + count];
          const size = numericProgress(data?.byteLength ?? data?.length);
          if (count && (count >= IMPORT_BATCH_MAX_FILES ||
              bytes + size > IMPORT_BATCH_MAX_BYTES || performance.now() - started >= IMPORT_BATCH_MAX_MS)) break;
          putNow(name, data, generation);
          ++count; bytes += size;
        }
        return count;
      });
      if (!processed) throw Error('Unable to transfer local game data.');
      complete += processed;
      // Keep this operation visibly active until native preparation starts;
      // the final batch transitions directly so 100% cannot linger.
      if (complete < total) { setLoading('handoff', 'Preparing game data…', complete, total); publish(); }
    }
  }
  function reportDiscRead(p) {
    progress = p.phase === 'complete' ? null : Object.freeze({complete: p.complete, total: p.total});
    message = `Reading local data ${p.complete}/${p.total}`;
    if (p.phase === 'complete') setLoading('handoff', 'Preparing game data…', 0, p.total);
    else setLoading('disc', 'Reading game data…', p.complete, p.total);
    publish();
  }
  async function transferScope(generation) {
    if (!discSession) throw Error('The local disc session is unavailable.');
    try {
      await pauseAudioForPreparation();
      const names = await boundary(() => {
        const count = check(Module._melee_web_native_asset_count(generation));
        return Array.from({length: count}, (_, index) =>
          Module.UTF8ToString(check(Module._melee_web_native_asset_name(generation, index))));
      });
      const files = await discSession.readScope(names, reportDiscRead);
      const entries = Array.from(files);
      setLoading('handoff', 'Preparing game data…', 0, entries.length); publish();
      await putBatches(entries, generation);
      await boundary(() => check(Module._melee_web_native_asset_commit(generation)));
      emit('assetScopeCommitted', {generation, files: entries.length,
        bytes: entries.reduce((sum, [, data]) => sum + data.byteLength, 0)});
      setLoading('native', 'Preparing game data…', 0, 0);
    } catch (error) {
      if (!fatal && !destroyed) await boundary(() => Module._melee_web_native_asset_abort(generation));
      throw error;
    }
  }
  async function prepareNativeResources() {
    callbacks.menuPreparation('Preparing native menu resources');
    try {
      await pauseAudioForPreparation();
      if (discSession && !prepared) {
        const generation = await boundary(() => check(Module._melee_web_native_asset_begin()));
        await transferScope(generation);
      }
      await new Promise(resolve => setTimeout(resolve, 0));
      await boundary(() => check(Module._melee_web_native_menu_prepare()));
      prepared = true; callbacks.menuPreparationDone();
    } finally {
      if (loading?.phase === 'native') { loading = null; refreshCatalogLoading(); }
    }
  }
  const handle = Object.freeze({
    controllers: Module.meleeControllers,
    version: 1, getState: snapshot, focus,
    importDisc(file) {
      return operation('importing', async () => {
        bundle = false;
        try {
          if (!await unloadAndSave()) throw Error(status());
          discSession?.close(); discSession = null;
          setLoading('disc', 'Reading game data…', 0, 1); publish();
          if (openDisc) {
            const opened = await openDisc(file);
            if (fatal || destroyed) {
              opened.close();
              throw Error('The player stopped while opening the local disc.');
            }
            discSession = opened;
          }
          else {
            const files = await readDisc(file, reportDiscRead);
            const entries = Array.from(files);
            progress = null;
            setLoading('handoff', 'Preparing game data…', 0, entries.length); publish();
            await putBatches(entries);
          }
          await prepareNativeResources(); bundle = true; message = 'Local game data loaded.';
          loading = null;
        } finally {
          if (['disc', 'handoff'].includes(loading?.phase)) { loading = null; refreshCatalogLoading(); }
        }
      }, true);
    },
    prepare() { return operation('preparing', async () => { if (!bundle) throw Error('Select a disc first.'); await prepareNativeResources(); }); },
    start() {
      if (!snapshot().canStart) return Promise.reject(Error('Prepare a valid local disc first.'));
      return operation('preparing', async () => {
        // Create the audio context while still handling the user's Play click;
        // native preparation may yield long enough to lose activation.
        await prepareAudio(); await prepareNativeResources();
        await boundary(() => check(Module._melee_web_native_menu_launch())); prepared = false; focus(); syncAudio();
      });
    },
    pause() { if (!snapshot().canPause) return Promise.reject(Error('No active scene to pause.')); return operation('pausing', async () => { await boundary(() => Module._melee_web_native_menu_pause(1)); focus(); syncAudio(); }); },
    resume() { if (!snapshot().canPause) return Promise.reject(Error('No active scene to resume.')); return operation('resuming', async () => { await prepareAudio(); await boundary(() => Module._melee_web_native_menu_pause(0)); focus(); syncAudio(); }); },
    setKeyboard(slot, enabled) { if (![0, 1].includes(slot)) throw Error('Unknown keyboard port.'); keyboard[slot] = !!enabled; inputDirty = true; },
    setKeyboardLayout(value) {
      if (!['two', 'boxx'].includes(value)) return Promise.reject(Error('Unknown keyboard layout.'));
      return boundary(() => { check(Module._melee_web_input_set_keyboard_layout(value === 'boxx' ? 1 : 0)); layout = value; inputDirty = true; });
    },
    unload() { return operation('unloading', async () => { check(await unloadAndSave()); }); },
    async destroy() {
      if (destroyed) return Object.freeze({requiresReload: true});
      try {
        if (!fatal) await handle.unload();
      } catch (error) {
        stop(error);
        throw error;
      } finally {
        clearStartupTimeout();
        destroyed = true; syncAudio();
        discSession?.close(); discSession = null;
        try { await audio?.destroy(); }
        finally {
          for (const [type, listener] of listeners) window.removeEventListener(type, listener, true);
          publish();
        }
      }
      // The global Emscripten heap and main loop live until this document retires.
      return Object.freeze({requiresReload: true});
    },
  });
  // The development entry may attach tools before loading; these are never part of the public handle.
  onOwner?.({Module, boundary, handle, status, check, put, prepareAudio, pauseAudioForPreparation,
    syncAudio, unloadAndSave, prepareNativeResources, waitForAudioAck, stop, callbacks});
  configureModule?.(Module);
  // Native seed loading needs this directory even when optional browser
  // persistence is disabled or unavailable. Keep that prerequisite in the
  // shared owner, ahead of any entry-specific cache mount/populate callback.
  const configuredPreRun = Module.preRun;
  Module.preRun = [() => {
    Module.FS.mkdirTree('/melee-render-cache');
    const callbacks = typeof configuredPreRun === 'function' ? [configuredPreRun] : configuredPreRun || [];
    for (const callback of callbacks) callback(Module);
  }];
  window.Module = Module;
  publish();
  const loader = document.createElement('script'); loader.src = String(loaderUrl);
  loader.onerror = () => stop(Error('The player files could not load. Reload to retry.'));
  startupTimeoutHandle = setTimeout(() => stop(Error(ready && !startupCacheReady ?
    'Renderer preparation timed out. Reload to recover.' : 'Player startup timed out.')), startupTimeout);
  document.head.append(loader);
  try { await startup; return handle; }
  finally {
    if (startupCacheReady || fatal || destroyed) {
      clearStartupTimeout();
    }
  }
}
