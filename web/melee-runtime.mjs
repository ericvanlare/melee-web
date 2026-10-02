/** One player owner per document. Native source ticks remain owned by the compiled player. */
import {loadNativeGameDisc, openNativeGameDiscSession} from './runtime-assets.mjs';
import {createControllerManager} from './controller-input.mjs';
import {createRuntimeDiagnostics} from './runtime-diagnostics.mjs';
import {createDiagnosticsDelivery} from './runtime-diagnostics-delivery.mjs';
import {DIAGNOSTICS_PREFERENCE_KEY, readDiagnosticsPreference, writeDiagnosticsPreference} from './diagnostics-settings.mjs';

let documentOwner = null;
function releaseDocumentReservation(reservation) {
  reservation.active = false;
  if (documentOwner === reservation && !reservation.initialized) documentOwner = null;
}
const SCENES = {
  1: 'css', 2: 'preparing', 3: 'sss', 4: 'preparing', 5: 'preparing',
  6: 'unloaded', 7: 'match', 8: 'results', 9: 'prize', 10: 'title',
  11: 'main', 12: 'opening', 13: 'opening-vs',
};
const IMPORT_BATCH_MAX_FILES = 8;
const IMPORT_BATCH_MAX_BYTES = 8 * 1024 * 1024;
const IMPORT_BATCH_MAX_MS = 8;
const SOURCE_STREAM_FILES = Object.freeze(['MvOpen.mth', 'MvHowto.mth', 'MvOmake15.mth']);

export async function mountMeleeRuntime({canvas, onState = () => {}, onError = () => {},
  onEvent = () => {}, onLog = () => {}, onOwner, configureModule,
  readDisc = loadNativeGameDisc, openDisc = openNativeGameDiscSession, createAudio,
  diagnosticIdentity = null, recordDiagnostics = true,
  loaderUrl = new URL('./gameplay_public.js', import.meta.url), startupTimeout = 60000} = {}) {
  if (!canvas || canvas.id !== 'canvas') throw Error('The player requires its own #canvas.');
  if (documentOwner) throw Error('Reload the page to start a fresh player.');
  if (!globalThis.isSecureContext) throw Error('The player requires HTTPS or a local server.');
  if (!navigator.gpu) throw Error('WebGPU is unavailable in this browser or device. The player cannot start here.');
  if (!globalThis.crossOriginIsolated) throw Error('The player requires cross-origin isolation headers.');
  if (typeof navigator.gpu.requestAdapter !== 'function')
    throw Error('This browser exposes WebGPU without the required adapter API. The player cannot start here.');
  // Claim the document before yielding. Every later startup side effect belongs
  // to this one attempt, even while the browser's adapter promise is pending.
  const reservation = {active: true, initialized: false, expired: false};
  documentOwner = reservation;
  const startupTimeoutMs = Number.isFinite(Number(startupTimeout)) ? Math.max(0, Number(startupTimeout)) : 60000;
  const startupDeadlineAt = Date.now() + startupTimeoutMs;
  let startupTimeoutHandle = null;
  let rejectAdapterDeadline;
  let failStartup = null;
  const adapterDeadline = new Promise((_, reject) => { rejectAdapterDeadline = reject; });
  const expireStartup = () => {
    reservation.expired = true;
    const error = reservation.initialized && ready && (!startupCacheReady || !graphicsPreparationReady()) ?
      Error('Renderer preparation timed out. Reload to recover.') : Error('Player startup timed out.');
    if (reservation.initialized) failStartup?.(error);
    else {
      releaseDocumentReservation(reservation);
      rejectAdapterDeadline(error);
    }
  };
  startupTimeoutHandle = setTimeout(expireStartup, startupTimeoutMs);
  // A present WebGPU object does not guarantee that this browser can provide
  // an adapter (for example, when WebGPU is disabled or no usable GPU exists).
  // Check before creating audio or loading the large native module.
  try {
    const adapter = await Promise.race([Promise.resolve(navigator.gpu.requestAdapter()), adapterDeadline]);
    if (!reservation.active || reservation.expired || documentOwner !== reservation)
      throw Error('Player startup timed out.');
    if (!adapter)
      throw Error('No WebGPU adapter is available. The player cannot start on this browser or device.');
  } catch (error) {
    // Null, rejected, and timed-out adapter preflights are retryable. Release
    // only this uninitialized reservation; later/initialized owners stay held.
    releaseDocumentReservation(reservation);
    clearTimeout(startupTimeoutHandle);
    startupTimeoutHandle = null;
    throw error;
  }
  // Preflight succeeded. Re-arm the same absolute deadline after synchronous
  // owner setup so setup exceptions cannot leave an orphaned timer.
  clearTimeout(startupTimeoutHandle);
  startupTimeoutHandle = null;
  // From here onward setup can have externally visible effects. Keep ownership
  // for the document lifetime, including if audio or native setup later fails.
  reservation.initialized = true;
  const assetBase = new URL('.', loaderUrl);
  let ready = false, fatal = false, destroyed = false, bundle = false, prepared = false, hasLocalData = false;
  let startupCacheReady = false;
  let busy = '', message = '', progress = null, inputDirty = true, lastState = '';
  // The browser may withhold every native callback between hidden and visible.
  // Retain that boundary until native can reset its wall clocks and input once.
  let lifecycleSuspended = false;
  let loading = Object.freeze({phase: 'boot', message: 'Starting player…', complete: 0, total: 0});
  let preparationLabel = '', preparationKeepsAudio = false;
  let discSession = null, assetTransfer = null;
  const sourceReadResults = new Map();
  const openedDiscSessions = new WeakSet();
  let keyboard = [true, true], layout = 'two';
  const commands = [], listeners = [];
  let diagnostics = null, diagnosticActive = true, longtaskObserver = null, diagnosticPreparationAt = null;
  let diagnosticDelivery = null, diagnosticDeliveryTimer = null, diagnosticRetainedLoaded = false;
  let diagnosticGeneration = 0;
  let automaticDiagnostics = readDiagnosticsPreference();
  function diagnosticDeliveryBlocked() {
    // Fatal owners cannot consume another native handoff. Once visible, their
    // sanitized failure may be delivered despite abandoned preparation state.
    return document.hidden || destroyed || (!fatal &&
      (lifecycleSuspended || !!busy || !!preparationLabel || !!loading));
  }
  function cancelDiagnosticDelivery() {
    diagnosticGeneration++;
    if (diagnosticDeliveryTimer !== null) clearTimeout(diagnosticDeliveryTimer);
    diagnosticDeliveryTimer = null;
  }
  function scheduleDiagnosticDelivery() {
    if (!diagnostics || !diagnosticDelivery || diagnosticActive || destroyed ||
        diagnosticDeliveryBlocked() || !automaticDiagnostics || diagnosticDeliveryTimer !== null ||
        !diagnosticDelivery.getStatus().eligible) return;
    const generation = diagnosticGeneration;
    // Complete the bounded post-event window away from the native callback.
    diagnosticDeliveryTimer = setTimeout(async () => {
      diagnosticDeliveryTimer = null;
      const eligible = () => !diagnosticActive && !diagnosticDeliveryBlocked() &&
        automaticDiagnostics && generation === diagnosticGeneration;
      if (!eligible()) return;
      try {
        if (!diagnosticRetainedLoaded) {
          const retained = await diagnostics.loadRetained();
          if (!eligible()) return;
          diagnosticRetainedLoaded = true;
          for (const record of retained.records || []) {
            diagnosticDelivery.enqueue({...record, incidents: [record.incident]});
          }
        }
        if (!eligible()) return;
        diagnosticDelivery.enqueue(diagnostics.exportReports());
        await diagnosticDelivery.flushWhenInactive();
      } catch { /* Delivery is optional and isolated from gameplay and saves. */ }
    }, 1100);
  }
  const diagnosticLifecycle = (type, detail) => { try { diagnostics?.lifecycle(type, detail); } catch {} };
  const diagnosticActivity = active => {
    if (active !== diagnosticActive) {
      diagnosticActive = active;
      try { void diagnostics?.setActive(active); } catch {}
    }
    try {
      const blocked = diagnosticDeliveryBlocked();
      diagnosticDelivery?.setActive(active || blocked);
      if (active || blocked) cancelDiagnosticDelivery();
      else scheduleDiagnosticDelivery();
    } catch {}
  };
  const diagnosticAudio = data => {
    try { diagnostics?.audio({timestamp: performance.now(), queue_depth: data.queued,
      underruns: data.underruns, overflows: data.overflows,
      clock_seconds: data.audio_clock_seconds, context_state: data.context_state,
      enabled: data.enabled}); } catch {}
  };
  let longtaskAvailable = false;
  try {
    if (recordDiagnostics && typeof PerformanceObserver === 'function' &&
        PerformanceObserver.supportedEntryTypes?.includes('longtask')) {
      longtaskObserver = new PerformanceObserver(list => {
        for (const entry of list.getEntries()) {
          try { diagnostics?.longtask({timestamp: entry.startTime, duration_ms: entry.duration}); } catch {}
        }
      });
      longtaskObserver.observe({type: 'longtask'});
      longtaskAvailable = true;
    }
  } catch { longtaskObserver?.disconnect(); longtaskObserver = null; }
  try {
    const packaged = diagnosticIdentity || JSON.parse(
      document.getElementById?.('runtime-diagnostic-identity')?.content || 'null');
    if (recordDiagnostics) diagnostics = createRuntimeDiagnostics({identity: packaged,
      audioAvailable: !!createAudio, longtaskAvailable});
  } catch { /* Optional diagnostics must never stop the player or touch saves. */ }
  if (!diagnostics) { longtaskObserver?.disconnect(); longtaskObserver = null; }
  if (diagnostics) {
    try { diagnosticDelivery = createDiagnosticsDelivery({optOut: !automaticDiagnostics}); }
    catch { /* Local retention remains useful when delivery is unavailable. */ }
  }
  const audio = createAudio?.({assetBase, onEvent: data => emit('audio', data),
    onError: error => { message = error.message; onError(error); publish(); }, onFatal: stop});
  const emit = (name, data) => { if (name === 'audio') diagnosticAudio(data); onEvent(name, data); };
  let resolveStartup, rejectStartup;
  const startup = new Promise((resolve, reject) => { resolveStartup = resolve; rejectStartup = reject; });
  const Module = {
    canvas,
    meleeControllers: createControllerManager(),
    locateFile: name => new URL(name, assetBase).href,
    print: text => onLog(String(text), false),
    printErr: text => onLog(String(text), true),
    onAbort: error => stop(error),
    onRuntimeInitialized() {
      if (fatal || destroyed || reservation.expired) return;
      ready = true; publish(); resolveStartup();
    },
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
    }
  }
  function graphicsPreparationReady() {
    const preparation = Module.pipelinePreparation;
    return !preparation || typeof preparation !== 'object' || preparation.ready === true;
  }
  function setLoading(phase, text, complete, total) {
    const normalizedTotal = Math.max(0, numericProgress(total));
    const normalizedComplete = Math.min(normalizedTotal, numericProgress(complete));
    loading = Object.freeze({phase, message: text, complete: normalizedComplete, total: normalizedTotal});
  }
  function refreshCatalogLoading() {
    if (fatal || destroyed) { loading = null; return; }
    if (!ready) return;
    if (startupCacheReady && graphicsPreparationReady()) clearStartupTimeout();
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
    const active = ['css', 'sss', 'title', 'main', 'opening', 'opening-vs',
      'match', 'results', 'prize'].includes(scene);
    const graphicsReady = ready && startupCacheReady && graphicsPreparationReady();
    const paused = active && !running && !preparationLabel && !busy;
    const state = destroyed ? 'destroyed' : fatal ? 'error' : !ready ? 'booting' : busy ||
      (preparationLabel ? 'preparing' : paused ? 'paused' : active ? scene : prepared ? 'prepared' : 'idle');
    return Object.freeze({version: 1, state, scene, phase, running, paused, audio: audio ? 'enabled' : 'disabled',
      message: message || preparationLabel || status(), progress, loading,
      ready, bundle, busy: !!busy, requiresReload: destroyed || fatal,
      graphicsReady,
      canSelectDisc: !fatal && !destroyed && !busy && !preparationLabel && (!ready || !active),
      canImport: ready && startupCacheReady && !fatal && !destroyed && !busy && !preparationLabel,
      canStart: graphicsReady && bundle && !active && !fatal && !destroyed && !busy && !preparationLabel,
      canPause: active && !fatal && !destroyed && !busy && !preparationLabel,
      canUnload: ready && (bundle || hasLocalData) && !fatal && !destroyed && !busy,
    });
  }
  function publish() {
    const state = snapshot(), key = JSON.stringify(state);
    diagnosticActivity(!fatal && !destroyed && !document.hidden &&
      (state.running || state.busy || !!state.loading || !!preparationLabel));
    if (key !== lastState) { lastState = key; onState(state); }
    return state;
  }
  function stop(error) {
    if (fatal || destroyed) return;
    try { diagnostics?.trigger(4, null, null, null, null, 0); } catch {}
    diagnosticActivity(false);
    fatal = true; message = String(error?.message || error || 'Player stopped. Reload to recover.');
    clearStartupTimeout();
    discSession?.close(); discSession = null;
    preparationLabel = ''; preparationKeepsAudio = false; loading = null;
    syncAudio();
    for (const c of commands.splice(0)) c.reject(Error(message));
    audio?.fail(Error(message));
    rejectStartup(Error(message)); publish(); onError(Error(message)); emit('fatal', message);
  }
  failStartup = stop;
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
    menuDiagnosticSample: diagnostics?.observeNative || (() => {}),
    menuDiagnosticIncident: diagnostics?.trigger || (() => {}),
    menuAudio(pcm) {
      if (!audio) throw Error('Audio output is disabled in this public alpha.');
      audio.write(pcm);
    },
    // These two callbacks MUST remain synchronous at their native boundaries.
    menuAudioReadyForPreparation() { return preparationKeepsAudio || !audio || audio.readyForPreparation(); },
    menuStartSourceRead(request, name, offset, size) {
      if (!discSession || typeof discSession.readFile !== 'function' ||
          !Number.isInteger(request) || typeof name !== 'string' || !name ||
          !Number.isSafeInteger(offset) || offset < 0 ||
          !Number.isInteger(size) || size <= 0 || size > 16 * 1024 * 1024 ||
          sourceReadResults.has(request)) return false;
      const session = discSession;
      const result = {state: 0, bytes: null};
      sourceReadResults.set(request, result);
      Promise.resolve().then(() => session.readFile(name, offset, size)).then(bytes => {
        if (sourceReadResults.get(request) !== result) return;
        if (!(bytes instanceof Uint8Array) || bytes.byteLength !== size) {
          result.state = -1;
          emit('sourceReadError', {request, name, message: 'Disc range returned an invalid byte count.'});
          return;
        }
        result.bytes = bytes;
        result.state = 1;
      }, error => {
        if (sourceReadResults.get(request) !== result) return;
        result.state = -1;
        emit('sourceReadError', {request, name,
          message: String(error?.message || error || 'Disc range read failed.')});
      });
      return true;
    },
    menuSourceReadStatus(request) { return sourceReadResults.get(request)?.state ?? -1; },
    menuSourceReadTake(request) {
      const result = sourceReadResults.get(request);
      if (!result || result.state !== 1 || !result.bytes?.byteLength) return 0;
      const pointer = Module._malloc(result.bytes.byteLength);
      if (!pointer) {
        result.state = -1;
        result.bytes = null;
        return 0;
      }
      Module.HEAPU8.set(result.bytes, pointer);
      sourceReadResults.delete(request);
      return pointer;
    },
    menuSourceReadDiscard(request) { sourceReadResults.delete(request); },
    menuServiceCommands() {
      if (fatal || destroyed) return 0;
      for (const c of commands.splice(0)) { try { c.resolve(c.run()); } catch (error) { c.reject(error); } }
      const suspended = lifecycleSuspended;
      lifecycleSuspended = false;
      if (suspended) {
        Module._melee_web_input_set_activity(0, 0);
        inputDirty = true;
      }
      if (inputDirty) {
        inputDirty = false;
        Module._melee_web_input_set_keyboard(keyboard[0] ? 1 : 0);
        Module._melee_web_input_set_keyboard_port(1, keyboard[1] && layout !== 'boxx' ? 1 : 0);
        Module._melee_web_input_set_activity(document.hasFocus() && document.activeElement === canvas ? 1 : 0,
          document.hidden ? 0 : 1);
      }
      return suspended ? 1 : 0;
    },
    menuPreparation(label, keepAudio = false) { diagnosticPreparationAt = performance.now(); diagnosticLifecycle('preparation', {timestamp: diagnosticPreparationAt}); preparationLabel = label || 'Preparing original scene'; preparationKeepsAudio = !!keepAudio; message = ''; setLoading('native', 'Preparing game data…', 0, 0); emit('preparation', {label: preparationLabel, keepAudio}); publish(); },
    menuPreparationDone() { const at = performance.now(); diagnosticLifecycle('preparation_done', {timestamp: at, duration_ms: diagnosticPreparationAt === null ? null : at - diagnosticPreparationAt}); diagnosticPreparationAt = null; preparationLabel = ''; message = ''; if (loading?.phase === 'native') { loading = null; refreshCatalogLoading(); } emit('preparationDone'); publish(); },
    menuPreparationCanceled() { preparationLabel = ''; preparationKeepsAudio = false; message = ''; if (loading?.phase === 'native') loading = null; emit('preparationCanceled'); publish(); },
    menuPreparationFailed(error) { preparationLabel = ''; preparationKeepsAudio = false; message = error || 'Native preparation failed'; if (loading?.phase === 'native') loading = null; emit('preparationFailed', message); publish(); onError(Error(message)); },
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
  listen('visibilitychange', () => {
    diagnosticLifecycle(document.hidden ? 'visibility_hidden' : 'visibility_visible');
    if (document.hidden) {
      lifecycleSuspended = true;
      // A paused document may already be inactive. Force the delivery adapter
      // back to its active state until native consumes the sticky handoff.
      diagnosticActivity(diagnosticActive);
    } else if (fatal) {
      diagnosticActivity(false);
    }
  });
  for (const type of ['pagehide', 'pageshow', 'freeze', 'resume']) listen(type, () => {
    if (type === 'pagehide' || type === 'freeze') {
      lifecycleSuspended = true;
      diagnosticActivity(diagnosticActive);
    }
    diagnosticLifecycle(type);
  });
  listen('offline', () => { try { diagnosticDelivery?.setOnline(false); } catch {} });
  listen('online', () => { try { diagnosticDelivery?.setOnline(true); } catch {} });
  listen('storage', event => {
    if (event.key === DIAGNOSTICS_PREFERENCE_KEY || event.key === null)
      setAutomaticDiagnostics(readDiagnosticsPreference(), false);
  });
  listen('error', event => stop(event.error || event.message));
  listen('unhandledrejection', event => stop(event.reason));
  const focus = () => { if (!fatal && !destroyed) { canvas.focus(); inputDirty = true; } };
  function saveProfileBytes(cardData) {
    if (cardData == null) return new Uint8Array();
    if (!(cardData instanceof Uint8Array) || cardData.byteLength !== 0x1790 + 7 * 0x1F2C)
      throw Error('Save profile must contain the exact original GameCube card data.');
    return cardData;
  }
  async function configureSaveProfile(mode, cardData = null) {
    if (!['everything', 'personal'].includes(mode)) throw Error('Unknown save mode.');
    const bytes = saveProfileBytes(cardData);
    if (mode === 'everything' && bytes.byteLength) throw Error('Everything unlocked does not use a personal save.');
    if (!ready || fatal || destroyed) throw Error('The player is unavailable. Reload to recover.');
    return operation('saving', async () => {
      let ptr = 0;
      try {
        if (bytes.length) {
          ptr = Module._malloc(bytes.length);
          if (!ptr) throw Error('Unable to allocate save profile transfer memory.');
          Module.HEAPU8.set(bytes, ptr);
        }
        await boundary(() => check(Module._melee_web_native_menu_set_save_profile(
          mode === 'everything' ? 0 : 1, ptr, bytes.length)));
      } finally { if (ptr) Module._free(ptr); }
    });
  }
  async function snapshotSaveProfile({baseline = false} = {}) {
    if (!ready || fatal || destroyed) throw Error('The player is unavailable. Reload to recover.');
    const length = 0x1790 + 7 * 0x1F2C;
    const ptr = Module._malloc(length);
    if (!ptr) throw Error('Unable to allocate save snapshot memory.');
    try {
      await boundary(() => check(baseline ?
        Module._melee_web_native_menu_snapshot_unlocked_baseline(ptr, length) :
        Module._melee_web_native_menu_snapshot_save_profile(ptr, length, 0)));
      return new Uint8Array(Module.HEAPU8.slice(ptr, ptr + length));
    } finally { Module._free(ptr); }
  }
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
    const diagnosticStarted = performance.now();
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
      const bytes = entries.reduce((sum, [, data]) => sum + data.byteLength, 0);
      diagnosticLifecycle('asset_preparation', {timestamp: performance.now(),
        duration_ms: performance.now() - diagnosticStarted, files: entries.length, bytes});
      emit('assetScopeCommitted', {generation, files: entries.length, bytes});
      setLoading('native', 'Preparing game data…', 0, 0);
    } catch (error) {
      if (!fatal && !destroyed) await boundary(() => Module._melee_web_native_asset_abort(generation));
      throw error;
    }
  }
  async function configureSourceFileStreams(session) {
    if (typeof session.fileInfo !== 'function' || typeof session.readFile !== 'function') {
      throw Error('The selected disc session cannot provide bounded original movie reads.');
    }
    const metadata = SOURCE_STREAM_FILES.map(name => {
      const info = session.fileInfo(name);
      if (!info || info.name !== name || !Number.isSafeInteger(info.size) ||
          info.size <= 0 || info.size > 0xffffffff) {
        throw Error(`The validated disc is missing a supported source movie: ${name}`);
      }
      return {name, size: info.size};
    });
    await boundary(() => {
      for (const {name, size} of metadata) {
        const encoded = new TextEncoder().encode(name + '\0');
        const pointer = Module._malloc(encoded.length);
        try {
          if (!pointer) throw Error('Unable to allocate source movie name.');
          Module.HEAPU8.set(encoded, pointer);
          check(Module._melee_web_native_source_file_external_set(pointer, size));
        } finally { Module._free(pointer); }
      }
    });
  }
  async function clearSourceFileStreams() {
    if (!ready || fatal || destroyed) return;
    await boundary(() => check(Module._melee_web_native_source_files_external_clear()));
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
    getDiagnosticsSettings() {
      let eligible = false;
      try { eligible = diagnosticDelivery?.getStatus().eligible === true; } catch {}
      return Object.freeze({eligible, automatic: eligible && automaticDiagnostics});
    },
    setAutomaticDiagnostics,
    async exportDiagnostics() {
      // Settings keeps this explicit serialization action off active gameplay.
      if (diagnosticActive) return null;
      try {
        await diagnostics?.persist();
        return diagnostics ? Object.freeze({current: diagnostics.exportReports(), retained: await diagnostics.exportRetained()}) : null;
      } catch { return null; }
    },
    activateAudio() { return prepareAudio(); },
    async openDiscSession(file) {
      if (typeof openDisc !== 'function') throw Error('This player has no local disc session loader.');
      const session = await openDisc(file);
      if (!session || typeof session !== 'object' || typeof session.close !== 'function' ||
          typeof session.readScope !== 'function') {
        session?.close?.();
        throw Error('The configured disc loader returned an invalid session.');
      }
      if (fatal || destroyed) {
        session.close();
        throw Error('The player stopped while opening the local disc.');
      }
      openedDiscSessions.add(session);
      return session;
    },
    importDisc(file, {preopenedSession = null} = {}) {
      if (preopenedSession && (!openedDiscSessions.has(preopenedSession) ||
          typeof preopenedSession.close !== 'function' || typeof preopenedSession.readScope !== 'function')) {
        return Promise.reject(Error('The selected disc session was not opened by this player. Choose the disc again.'));
      }
      let adoptedSession = false;
      const work = operation('importing', async () => {
        bundle = false;
        try {
          if (!await unloadAndSave()) throw Error(status());
          if (discSession) {
            await clearSourceFileStreams();
            discSession.close(); discSession = null;
          }
          setLoading('disc', 'Reading game data…', 0, 1); publish();
          if (openDisc) {
            const opened = preopenedSession || await handle.openDiscSession(file);
            if (fatal || destroyed) {
              opened.close();
              throw Error('The player stopped while opening the local disc.');
            }
            discSession = opened;
            adoptedSession = !!preopenedSession;
            await configureSourceFileStreams(discSession);
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
      return work.catch(error => {
        if (!adoptedSession) preopenedSession?.close();
        throw error;
      });
    },
    prepare() { return operation('preparing', async () => { if (!bundle) throw Error('Select a disc first.'); await prepareNativeResources(); }); },
    start({isCurrent = () => true} = {}) {
      if (typeof isCurrent !== 'function') return Promise.reject(Error('Invalid player start guard.'));
      if (!snapshot().canStart) return Promise.reject(Error('Prepare a valid local disc first.'));
      return operation('preparing', async () => {
        // The shell primes audio on Choose file; a recovery Play click supplies
        // its own gesture. Resume before native preparation can yield.
        await prepareAudio(); await prepareNativeResources();
        if (!isCurrent()) throw Error('Disc selection changed before launch.');
        await boundary(() => check(Module._melee_web_native_menu_launch())); prepared = false; focus(); syncAudio();
      });
    },
    pause() { if (!snapshot().canPause) return Promise.reject(Error('No active scene to pause.')); return operation('pausing', async () => { await boundary(() => Module._melee_web_native_menu_pause(1)); focus(); syncAudio(); }); },
    resume() { if (!snapshot().canPause) return Promise.reject(Error('No active scene to resume.')); return operation('resuming', async () => { await prepareAudio(); await boundary(() => Module._melee_web_native_menu_pause(0)); focus(); syncAudio(); }); },
    configureSaveProfile,
    snapshotSaveProfile,
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
        if (!fatal && discSession) {
          try { await clearSourceFileStreams(); }
          catch (error) { onLog(`Source movie catalog cleanup failed: ${error.message}`, true); }
        }
        destroyed = true; syncAudio();
        diagnosticLifecycle('scene_exit'); diagnosticActivity(false);
        cancelDiagnosticDelivery(); diagnosticDelivery?.dispose();
        longtaskObserver?.disconnect();
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
  function setAutomaticDiagnostics(enabled, persistPreference = true) {
    automaticDiagnostics = enabled === true;
    if (persistPreference) writeDiagnosticsPreference(automaticDiagnostics);
    cancelDiagnosticDelivery();
    try { diagnosticDelivery?.setOptOut(!automaticDiagnostics); } catch {}
    try { if (automaticDiagnostics) scheduleDiagnosticDelivery(); } catch {}
    return automaticDiagnostics;
  }
  // The development entry may attach tools before loading; these are never part of the public handle.
  onOwner?.({Module, boundary, handle, status, check, put, prepareAudio, pauseAudioForPreparation,
    syncAudio, unloadAndSave, prepareNativeResources, waitForAudioAck, stop, callbacks, diagnostics});
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
  if (!fatal && !destroyed && !(ready && startupCacheReady && graphicsPreparationReady()))
    startupTimeoutHandle = setTimeout(expireStartup, Math.max(0, startupDeadlineAt - Date.now()));
  document.head.append(loader);
  try { await startup; return handle; }
  finally {
    if (startupCacheReady || fatal || destroyed) {
      clearStartupTimeout();
    }
  }
}
