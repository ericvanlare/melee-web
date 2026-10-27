/** One player owner per document. Native source ticks remain owned by the compiled player. */
import {loadNativeGameDisc, openNativeGameDiscSession} from './runtime-assets.mjs';
import {createControllerManager} from './controller-input.mjs';
import {createRuntimeDiagnostics} from './runtime-diagnostics.mjs';
import {createDiagnosticsDelivery} from './runtime-diagnostics-delivery.mjs';
import {createStartupProfile} from './startup-profile.mjs';
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
const DIAGNOSTIC_DESTROY_PERSIST_TIMEOUT_MS = 250;

export async function mountMeleeRuntime({canvas, onState = () => {}, onError = () => {},
  onEvent = () => {}, onLog = () => {}, onOwner, configureModule,
  readDisc = loadNativeGameDisc, openDisc = openNativeGameDiscSession, createAudio,
  diagnosticIdentity = null, onStartupProfile = () => {}, recordDiagnostics = true, stagingDiagnosticsOrigin = null,
  loaderUrl = new URL('./gameplay_public.js', import.meta.url), startupTimeout = 60000} = {}) {
  let packagedIdentity = diagnosticIdentity;
  if (!packagedIdentity) {
    try { packagedIdentity = JSON.parse(document.getElementById?.('runtime-diagnostic-identity')?.content || 'null'); }
    catch { packagedIdentity = null; }
  }
  const startupProfile = createStartupProfile({identity: packagedIdentity, onUpdate: onStartupProfile,
    initialNotify: false});
  const profileCall = (method, ...args) => {
    try { return startupProfile[method](...args); }
    catch { return undefined; }
  };
  let startupProfilingPending = true;
  const failProfile = kind => { profileCall('fail', kind); startupProfilingPending = false; };
  const failPreflight = (kind, message) => { failProfile(kind); throw Error(message); };
  try {
    if (!canvas || canvas.id !== 'canvas') failPreflight('missing_canvas', 'The player requires its own #canvas.');
    if (documentOwner) failPreflight('document_owned', 'Reload the page to start a fresh player.');
    if (!globalThis.isSecureContext) failPreflight('insecure_context', 'The player requires HTTPS or a local server.');
    if (!navigator.gpu) failPreflight('webgpu_unavailable', 'WebGPU is unavailable in this browser or device. The player cannot start here.');
    if (!globalThis.crossOriginIsolated) failPreflight('isolation_unavailable', 'The player requires cross-origin isolation headers.');
    if (typeof navigator.gpu.requestAdapter !== 'function')
      failPreflight('adapter_api_unavailable', 'This browser exposes WebGPU without the required adapter API. The player cannot start here.');
  } catch (error) {
    failProfile('module_setup_failure');
    throw error;
  }
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
    failProfile('startup_timeout');
    if (reservation.initialized) failStartup?.(error);
    else {
      releaseDocumentReservation(reservation);
      rejectAdapterDeadline(error);
    }
  };
  try { startupTimeoutHandle = setTimeout(expireStartup, startupTimeoutMs); }
  catch (error) { failProfile('module_setup_failure'); throw error; }
  // A present WebGPU object does not guarantee that this browser can provide
  // an adapter (for example, when WebGPU is disabled or no usable GPU exists).
  // Check before creating audio or loading the large native module.
  profileCall('environmentReady');
  profileCall('adapterRequested');
  try {
    const adapter = await Promise.race([Promise.resolve(navigator.gpu.requestAdapter()), adapterDeadline]);
    if (!reservation.active || reservation.expired || documentOwner !== reservation)
      failPreflight('startup_timeout', 'Player startup timed out.');
    if (!adapter)
      failPreflight('adapter_unavailable', 'No WebGPU adapter is available. The player cannot start on this browser or device.');
  } catch (error) {
    failProfile(reservation.expired ? 'startup_timeout' : 'adapter_failure');
    // Null, rejected, and timed-out adapter preflights are retryable. Release
    // only this uninitialized reservation; later/initialized owners stay held.
    releaseDocumentReservation(reservation);
    clearTimeout(startupTimeoutHandle);
    startupTimeoutHandle = null;
    throw error;
  }
  profileCall('adapterReady');
  // Preflight succeeded. Re-arm the same absolute deadline after synchronous
  // owner setup so setup exceptions cannot leave an orphaned timer.
  clearTimeout(startupTimeoutHandle);
  startupTimeoutHandle = null;
  // From here onward setup can have externally visible effects. Keep ownership
  // for the document lifetime, including if audio or native setup later fails.
  reservation.initialized = true;
  let assetBase;
  try {
    assetBase = new URL('.', loaderUrl);
    profileCall('setAssetBase', assetBase);
  } catch (error) {
    failProfile('module_setup_failure');
    clearTimeout(startupTimeoutHandle);
    throw error;
  }
  let ready = false, fatal = false, destroyed = false, bundle = false, prepared = false, hasLocalData = false;
  let startupCacheReady = false;
  let busy = '', message = '', progress = null, inputDirty = true, lastState = '';
  // The browser may withhold every native callback between hidden and visible.
  // Retain that boundary until native can reset its wall clocks and input once.
  let lifecycleSuspended = false;
  let loading = Object.freeze({phase: 'boot', message: 'Starting player…', complete: 0, total: 0});
  let preparationLabel = '', preparationKeepsAudio = false;
  let discSession = null, assetTransfer = null;
  let networkSessionClose = null, networkCloseOperation = null, networkCloseMode = null;
  let discRetirementOperation = null, destroyOperation = null, unloadOperation = null;
  const networkCleanupErrors = [];
  const sourceReadResults = new Map();
  const openedDiscSessions = new WeakSet();
  let keyboard = [true, true], layout = 'two';
  const commands = [], listeners = [];
  let terminalDrawDiscardRetirementConsumed = false;
  let terminalMatchedSetupCfRetirementObserved = false;
  let terminalMatchedSetupCfRetirementConsumed = false;
  let stadiumRecreationCloseSettled = false;
  let diagnostics = null, diagnosticActive = true, longtaskObserver = null, diagnosticPreparationAt = null;
  let diagnosticDelivery = null, diagnosticDeliveryTimer = null, diagnosticRetainedLoaded = false;
  let diagnosticMatureDeliveryPromise = null;
  let diagnosticGeneration = 0;
  let diagnosticCheckpointPromise = null, diagnosticCheckpointSuspended = false;
  let diagnosticCheckpointRequested = false, diagnosticCheckpointDestroyWindow = false;
  let diagnosticCheckpointClosed = false;
  let automaticDiagnostics = readDiagnosticsPreference();
  let stagingDiagnosticsEnabled = false;
  try {
    const url = new URL(globalThis.location.href);
    const localHost = ['localhost', '127.0.0.1', '::1', '[::1]'].includes(url.hostname);
    const hostedStaging = typeof stagingDiagnosticsOrigin === 'string' &&
      url.protocol === 'https:' && url.origin === stagingDiagnosticsOrigin;
    stagingDiagnosticsEnabled = recordDiagnostics && (localHost || hostedStaging) &&
      url.searchParams.get('melee-web-staging-diagnostics') === '1';
  } catch { /* Staging diagnostics remain disabled unless explicitly selected. */ }
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
  function requestDiagnosticCheckpoint() {
    if (laterBReplayTransfer.suspended()) return Promise.resolve({persisted: false, reason: 'private-replay-transfer'});
    diagnosticCheckpointRequested = true;
    if (!diagnostics) {
      diagnosticCheckpointRequested = false;
      return Promise.resolve({persisted: false, reason: 'unavailable'});
    }
    if (diagnosticCheckpointPromise) return diagnosticCheckpointPromise;
    const eligible = () => !diagnosticCheckpointClosed &&
      (diagnosticCheckpointSuspended || diagnosticCheckpointDestroyWindow);
    // One job owns all lifecycle persistence.  A request received while its
    // recorder write is pending becomes one fresh iteration after that write;
    // it never serializes from the native callback or uses a stale snapshot.
    let promise;
    promise = Promise.resolve().then(async () => {
      try {
        let result = {persisted: false, reason: 'cancelled'};
        while (diagnosticCheckpointRequested && eligible()) {
          diagnosticCheckpointRequested = false;
          try { result = await diagnostics.checkpoint({eligible}); }
          catch { result = {persisted: false, reason: 'failed'}; }
        }
        return result;
      } catch {
        return {persisted: false, reason: 'failed'};
      } finally {
        if (diagnosticCheckpointPromise === promise) diagnosticCheckpointPromise = null;
      }
    });
    diagnosticCheckpointPromise = promise;
    return promise;
  }
  function beginDiagnosticCheckpoint() {
    diagnosticCheckpointSuspended = true;
    try { void requestDiagnosticCheckpoint(); } catch {}
  }
  function resetDiagnosticCheckpoint() {
    diagnosticCheckpointSuspended = false;
    diagnosticCheckpointRequested = false;
  }
  function awaitDiagnosticCheckpoint(timeoutMs = DIAGNOSTIC_DESTROY_PERSIST_TIMEOUT_MS) {
    diagnosticCheckpointDestroyWindow = true;
    const job = requestDiagnosticCheckpoint();
    let timeout;
    const deadline = new Promise(resolve => {
      timeout = setTimeout(() => resolve({persisted: false, reason: 'timeout'}), timeoutMs);
    });
    return Promise.race([job, deadline]).finally(() => {
      clearTimeout(timeout);
      diagnosticCheckpointDestroyWindow = false;
      diagnosticCheckpointClosed = true;
      diagnosticCheckpointRequested = false;
    });
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
        const report = await loadRetainedForDelivery(eligible);
        if (!report || !eligible()) return;
        diagnosticDelivery.enqueue(report);
        await diagnosticDelivery.flushWhenInactive();
      } catch { /* Delivery is optional and isolated from gameplay and saves. */ }
    }, 1100);
  }
  async function loadRetainedForDelivery(eligible) {
    if (diagnosticRetainedLoaded) {
      if (!eligible()) return null;
      return diagnostics.exportReports();
    }
    const retained = await diagnostics.loadRetained();
    if (!eligible()) return null;
    const report = diagnostics.exportReports();
    const current = new Map((report.incidents || []).map(incident => [incident?.id, incident]));
    for (const record of retained.records || []) {
      if (!eligible()) return null;
      const currentIncident = record.session_id === report.session_id ? current.get(record.incident?.id) : null;
      if (currentIncident) continue;
      diagnosticDelivery.enqueue({...record, incidents: [record.incident]});
    }
    // A young current incident may reach storage after this read. Keep the
    // next inactive read eligible even when older retained records exist, so
    // ring eviction does not orphan its already persisted snapshot.
    diagnosticRetainedLoaded = !(report.incidents || []).some(incident => !incident.closed);
    return report;
  }
  function scheduleMatureDiagnosticDelivery() {
    if (!diagnostics || !diagnosticDelivery || diagnosticActive || destroyed ||
        diagnosticDeliveryBlocked() || !automaticDiagnostics || diagnosticMatureDeliveryPromise !== null ||
        !diagnosticDelivery.getStatus().eligible) return;
    const generation = diagnosticGeneration;
    const eligible = () => !diagnosticActive && !diagnosticDeliveryBlocked() &&
      automaticDiagnostics && generation === diagnosticGeneration;
    let promise;
    promise = Promise.resolve().then(async () => {
      if (!eligible()) return;
      const report = await loadRetainedForDelivery(eligible);
      if (!report) return;
      if (!eligible()) return;
      for (const incident of report.incidents || []) {
        if (!eligible()) return;
        if (!incident.closed) continue;
        diagnosticDelivery.enqueue({...report, incidents: [incident], incident});
      }
      if (eligible()) await diagnosticDelivery.flushWhenInactive();
    }).catch(() => {}).finally(() => {
      if (diagnosticMatureDeliveryPromise === promise) {
        diagnosticMatureDeliveryPromise = null;
        if (generation !== diagnosticGeneration && !diagnosticActive && !destroyed &&
            automaticDiagnostics && !diagnosticDeliveryBlocked()) scheduleMatureDiagnosticDelivery();
      }
    });
    diagnosticMatureDeliveryPromise = promise;
  }
  const diagnosticLifecycle = (type, detail) => { try { diagnostics?.lifecycle(type, detail); } catch {} };
  const diagnosticActivity = active => {
    const becameInactive = !active && diagnosticActive;
    if (active !== diagnosticActive) {
      diagnosticActive = active;
      try { void diagnostics?.setActive(active); } catch {}
    }
    try {
      const blocked = diagnosticDeliveryBlocked();
      diagnosticDelivery?.setActive(active || blocked);
      if (active || blocked) cancelDiagnosticDelivery();
      else {
        scheduleDiagnosticDelivery();
        if (becameInactive) scheduleMatureDiagnosticDelivery();
      }
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
    if (recordDiagnostics) diagnostics = createRuntimeDiagnostics({identity: packagedIdentity,
      audioAvailable: !!createAudio, longtaskAvailable, stagingDiagnosticsEnabled});
  } catch { /* Optional diagnostics must never stop the player or touch saves. */ }
  if (!diagnostics) { longtaskObserver?.disconnect(); longtaskObserver = null; }
  if (diagnostics) {
    try { diagnosticDelivery = createDiagnosticsDelivery({optOut: !automaticDiagnostics}); }
    catch { /* Local retention remains useful when delivery is unavailable. */ }
  }
  let audio = null;
  try {
    audio = createAudio?.({assetBase, onEvent: data => emit('audio', data),
      onError: error => { message = error.message; onError(error); publish(); }, onFatal: stop});
  } catch (error) { failProfile('module_setup_failure'); throw error; }
  const emit = (name, data) => { if (name === 'audio') diagnosticAudio(data); onEvent(name, data); };
  let resolveStartup, rejectStartup;
  const startup = new Promise((resolve, reject) => { resolveStartup = resolve; rejectStartup = reject; });
  let controllerManager;
  try { controllerManager = createControllerManager(); }
  catch (error) { failProfile('module_setup_failure'); throw error; }
  const Module = {
    canvas,
    meleeControllers: controllerManager,
    locateFile: name => new URL(name, assetBase).href,
    print: text => onLog(String(text), false),
    printErr: text => onLog(String(text), true),
    onAbort: error => stop(error),
    onRuntimeInitialized() {
      if (fatal || destroyed || reservation.expired) return;
      profileCall('runtimeInitialized');
      ready = true; publish(); resolveStartup();
    },
  };
  // Private Gate2 shell ownership. Native transfer/abort producers are separate
  // prerequisites: reserving a token is NOT evidence that native moved a parent.
  // BEGIN LATER_B_REPLAY_TRANSFER_OWNER
  let saveProfileSnapshotsActive = 0;
  function createLaterBReplayTransferOwner() {
    let active = null;
    const cacheExports = new Set();
    const copy = value => JSON.parse(JSON.stringify(value));
    const freeze = value => { if (value && typeof value === 'object') { for (const item of Object.values(value)) freeze(item); Object.freeze(value); } return value; };
    const profiles = Object.freeze({
      'original-b-2013-943-v1': Object.freeze({recipe_sha256:'5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755',cursor:943,setup_seed_hex:'4b25c79b'}),
      'original-b-5422-951-long-v1': Object.freeze({recipe_sha256:'e669f831538b90e39241a569b6c38884300bfe246b252cacda994cbc45c10bbf',cursor:951,setup_seed_hex:'25d557a7'}),
    });
    const keys = ['reference_id', 'recipe_sha256', 'cursor', 'notification_order', 'world_generation', 'setup_seed_hex'];
    const owned = token => {
      if (!active || active.token !== token || active.consumed) throw Error('Later B replay transfer token is stale, repeated or foreign');
      return active;
    };
    const same = (value, expected) => value && keys.every(key => value[key] === expected[key]);
    const proof = value => active && value?.schema === 'melee-web-stadium-later-b-parent-transfer-v1' &&
      value.token === active.token.id && same(value, active.request) &&
      value.parent_identity_checked === true;
    const api = {
      suspended: () => active !== null,
      beginCacheExport() {
        if (active) throw Error('Private replay continuation forbids cache export');
        const token = Object.freeze({}); cacheExports.add(token); return token;
      },
      endCacheExport(token) {
        if (!cacheExports.delete(token)) throw Error('Cache export transaction token is foreign or repeated');
      },
      checkReservation(request) {
        const profile = request && Object.hasOwn(profiles, request.reference_id) ? profiles[request.reference_id] : null;
        if (cacheExports.size || saveProfileSnapshotsActive || active || !ready || fatal || destroyed || busy || assetTransfer || networkSessionClose || commands.length ||
            (loading && loading.phase !== 'native') ||
            Module.stadiumMatchedSetupNativeIdle?.() !== true ||
            !request || Object.keys(request).sort().join(',') !== [...keys].sort().join(',') ||
            !profile || request.recipe_sha256 !== profile.recipe_sha256 ||
            request.cursor !== profile.cursor || request.setup_seed_hex !== profile.setup_seed_hex ||
            !Number.isSafeInteger(request.notification_order) || request.notification_order < 1 ||
            !Number.isSafeInteger(request.world_generation) || request.world_generation < 1)
          throw Error('Later B replay transfer requires an idle exact new-reference Setup reservation');
      },
      reserve(request) {
        api.checkReservation(request);
        const before = snapshot();
        active = {token: Object.freeze({id: crypto.randomUUID()}), request: freeze(copy(request)),
          before, phase: 'reserved', transferred: false, terminal: null, consumed: false, failures: []};
        return active.token;
      },
      request(token) { return owned(token).request; },
      state() { return active ? {phase: active.phase, transferred: active.transferred,
        consumed: active.consumed, failures: [...active.failures]} : null; },
      snapshot() {
        return active && Object.freeze({...active.before, loading, state: 'diagnostic-transfer', running: false,
          message: 'Private replay continuation owns native access.', canImport: false, canStart: false,
          canPause: false, canUnload: false, canSelectDisc: false});
      },
      pending(token, kind) {
        const item = owned(token);
        const permitted = item.phase === 'reserved' ? ['arm'] : item.phase === 'rejected' ? ['abort'] :
          item.phase === 'transferred' ? ['action', 'retire', 'abort'] : [];
        if (!permitted.includes(kind)) throw Error('Later B replay transfer command is repeated, pending or unsafe');
        item.command = kind; item.phase = 'pending';
      },
      settled(token) {
        const item = owned(token);
        if (item.phase !== 'pending' || Module.stadiumMatchedSetupNativeIdle?.() !== true)
          throw Error('Later B replay transfer requires actual settled Asyncify');
        item.phase = item.terminal ? 'terminal' : item.transferred ? 'transferred' : 'rejected';
      },
      fail(reason) {
        if (!active || active.consumed) return;
        active.phase = 'uncertain'; active.failures.push(String(reason || 'Player stopped during transfer'));
      },
      uncertain(token, reason) {
        const item = owned(token); item.phase = 'uncertain';
        item.failures.push(String(reason || 'Native command settlement is unknown'));
      },
      transferred(receipt) {
        if (!active || active.phase !== 'pending' || active.command !== 'arm' || active.transferred || !proof(receipt) ||
            receipt.stage !== 'after-parent-move' || receipt.parent_archived !== true ||
            receipt.source_ticks !== 0 || receipt.source_draws !== 0) return false;
        active.transferred = true; return true;
      },
      acceptsTerminal(kind, receipt) {
        if (!active || active.phase !== 'pending' || active.terminal || !proof(receipt?.later_b_transfer) ||
            receipt.phase_after_close !== 0 || receipt.running_after_close !== 0 ||
            receipt.later_b_transfer.parent_close_checked !== true || receipt.error !== null) return false;
        if (kind === 'retired') {
          if (active.command !== 'retire' || !active.transferred || receipt.native_retired !== true || receipt.terminal !== true) return false;
        } else if (kind === 'known-unarmed-abort') {
          if (active.command !== 'abort' || receipt.schema !== 'melee-web-stadium-later-b-unarmed-abort-v1' ||
              receipt.native_closed !== true || receipt.attempt_consumed !== true || receipt.armed !== false ||
              receipt.source_ticks !== 0 || receipt.source_draws !== 0 || receipt.action_cursor !== 0) return false;
        } else return false;
        return true;
      },
      terminal(kind, receipt) {
        if (!api.acceptsTerminal(kind, receipt)) return false;
        active.terminal = freeze({kind, receipt: copy(receipt)}); return true;
      },
      consume(token) {
        const item = owned(token);
        if (item.phase !== 'terminal' || !item.terminal || Module.stadiumMatchedSetupNativeIdle?.() !== true)
          throw Error('Later B replay transfer has no acknowledged settled native close');
        item.consumed = true; return item.terminal;
      },
    };
    return Object.freeze(api);
  }
  // END LATER_B_REPLAY_TRANSFER_OWNER
  const laterBReplayTransfer = createLaterBReplayTransferOwner();
  const status = () => laterBReplayTransfer.suspended() ? 'Private replay continuation owns native access.' : terminalMatchedSetupCfRetirementObserved ? 'Native diagnostic owner retired.' : ready ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : 'Starting WebGPU…';
  const check = result => { if (!result) throw Error(status()); return result; };
  const numericProgress = (value, fallback = 0) => Number.isFinite(Number(value)) ? Math.max(0, Math.floor(Number(value))) : fallback;
  function readNativeCacheIdle() {
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation forbids ordinary cache reads');
    const idle = Module._melee_web_native_menu_cache_idle;
    if (typeof idle !== 'function') throw Error('The native renderer-cache readiness service is unavailable. Reload to recover.');
    const state = idle();
    if (![1, 0, -1].includes(state)) throw Error(`Invalid native cache idle state: ${state}`);
    if (startupProfilingPending) {
      profileCall('observeCacheIdle', state);
      try { startupProfile.observeGraphicsStartup(Module.graphicsStartup); } catch {}
    }
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
    if (laterBReplayTransfer.suspended()) return laterBReplayTransfer.snapshot();
    refreshCatalogLoading();
    const phase = ready && !fatal && !destroyed && !terminalMatchedSetupCfRetirementObserved
      ? Module._melee_web_native_menu_phase() : 0;
    const running = ready && !fatal && !destroyed && !terminalMatchedSetupCfRetirementObserved &&
      !!Module._melee_web_native_menu_running();
    const scene = SCENES[phase] || 'idle';
    const active = ['css', 'sss', 'title', 'main', 'opening', 'opening-vs',
      'match', 'results', 'prize'].includes(scene);
    const graphicsReady = ready && startupCacheReady && graphicsPreparationReady();
    if (startupProfilingPending) {
      try { startupProfile.observePipeline(Module.pipelinePreparation); } catch {}
      try { startupProfile.observeGraphicsStartup(Module.graphicsStartup); } catch {}
      profileCall('observeGraphicsReady', graphicsReady);
      if (graphicsReady) startupProfilingPending = false;
    }
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
  function recordNetworkCleanupError(error, label = 'Network session cleanup failed') {
    if (!networkCleanupErrors.includes(error)) networkCleanupErrors.push(error);
    try { onLog(`${label}: ${String(error?.message || error)}`, true); } catch {}
  }
  function attachNetworkSession(close) {
    if (typeof close !== 'function') throw Error('Network session close owner must be a function');
    if (fatal || destroyed) throw Error('Cannot attach a network session to a stopped player');
    if (networkSessionClose) throw Error('This player already owns a network session');
    networkSessionClose = close;
  }
  function closeNetworkSession(mode = 'normal') {
    if (!networkSessionClose) return Promise.resolve();
    if (!['normal', 'fatal'].includes(mode)) return Promise.reject(Error('Network session close mode must be normal or fatal'));
    if (!networkCloseOperation) {
      networkCloseMode = mode;
      try { networkCloseOperation = Promise.resolve(networkSessionClose({mode})); }
      catch (error) { networkCloseOperation = Promise.reject(error); }
      networkCloseOperation.catch(error => recordNetworkCleanupError(error));
      return networkCloseOperation;
    }
    if (mode === 'fatal' && networkCloseMode !== 'fatal') {
      networkCloseMode = 'fatal';
      let escalation;
      try { escalation = Promise.resolve(networkSessionClose({mode})); }
      catch (error) { escalation = Promise.reject(error); }
      const previous = networkCloseOperation;
      networkCloseOperation = Promise.allSettled([previous, escalation]).then(results => {
        const failures = results.filter(result => result.status === 'rejected').map(result => result.reason);
        if (failures.length) throw new AggregateError([...new Set(failures)], 'Network session fatal cleanup failed');
      });
      networkCloseOperation.catch(error => recordNetworkCleanupError(error));
    }
    return networkCloseOperation;
  }
  function retireDiscSession() {
    if (discRetirementOperation) return discRetirementOperation;
    const session = discSession;
    discSession = null;
    if (!session) return Promise.resolve();
    try {
      discRetirementOperation = Promise.resolve(session.close()).catch(error => {
        recordNetworkCleanupError(error, 'Disc session cleanup failed');
      });
    } catch (error) {
      recordNetworkCleanupError(error, 'Disc session cleanup failed');
      discRetirementOperation = Promise.resolve();
    }
    return discRetirementOperation;
  }
  function retireDiscAfterNetwork(closeOperation) {
    if (!networkSessionClose) { void retireDiscSession(); return; }
    Promise.resolve(closeOperation).catch(() => {}).then(retireDiscSession).catch(error => {
      recordNetworkCleanupError(error, 'Disc session cleanup failed');
    });
  }
  function stop(error, startupFailureKind = 'runtime_failure') {
    if (fatal || destroyed) return;
    failProfile(startupFailureKind);
    laterBReplayTransfer.fail(error?.message || String(error));
    if (!laterBReplayTransfer.suspended()) { try { diagnosticIncident(4, null, null, null, null, 0); } catch {} }
    diagnosticActivity(false);
    fatal = true; message = String(error?.message || error || 'Player stopped. Reload to recover.');
    clearStartupTimeout();
    const networkClosing = closeNetworkSession('fatal');
    if (!laterBReplayTransfer.suspended()) retireDiscAfterNetwork(networkClosing);
    preparationLabel = ''; preparationKeepsAudio = false; loading = null;
    syncAudio();
    for (const c of commands.splice(0)) c.reject(Error(message));
    audio?.fail(Error(message));
    rejectStartup(Error(message)); publish(); onError(Error(message)); emit('fatal', message);
  }
  failStartup = stop;
  const boundary = run => laterBReplayTransfer.suspended() ? Promise.reject(Error('Private replay continuation owns native access.')) : fatal || destroyed ? Promise.reject(Error('Reload after the player stopped.')) :
    new Promise((resolve, reject) => commands.push({run, resolve, reject}));
  async function operation(name, run, requireStartupCache = false) {
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation owns native access.');
    if (!ready || fatal || destroyed) throw Error('The player is unavailable. Reload to recover.');
    if (requireStartupCache && !startupCacheReady) throw Error('Graphics are still preparing. Wait for startup preparation to finish.');
    if (busy) throw Error('Wait for the current player operation to finish.');
    busy = name; message = ''; progress = null; publish();
    let timeout;
    try {
      return await Promise.race([run(), new Promise((_, reject) => {
        timeout = setTimeout(() => { const error = Error('The player stopped responding. Reload to recover.'); stop(error); reject(error); }, 60000);
      })]);
    } catch (error) {
      if (!fatal && !destroyed) callbacks.menuPreparationFailed(error.message || String(error));
      throw error;
    }
    finally { clearTimeout(timeout); busy = ''; progress = null; publish(); }
  }
  const waitForAudioAck = () => audio?.waitForAck() || Promise.resolve();
  const prepareAudio = async () => { await audio?.prepare(); };
  const waitForAudioRender = async () => {
    if (audio && typeof audio.waitForRender !== 'function')
      throw Error('The audio output cannot confirm startup readiness. Reload to recover.');
    await audio?.waitForRender();
  };
  const pauseAudioForPreparation = async () => { await audio?.pause(); };
  function syncAudio() {
    if (laterBReplayTransfer.suspended()) return;
    const running = ready && !fatal && !destroyed && !!Module._melee_web_native_menu_running();
    const enabled = ready && !fatal && !destroyed && (running || preparationKeepsAudio) && !document.hidden;
    if (running) preparationKeepsAudio = false;
    audio?.setEnabled(enabled);
  }
  function diagnosticIncident(...args) {
    if (laterBReplayTransfer.suspended()) return false;
    const result = diagnostics?.trigger(...args);
    // An earlier idle read cannot cover an incident created afterward. Its
    // persisted snapshot may outlive its slot in the bounded current ring.
    if (result) diagnosticRetainedLoaded = false;
    const reason = Number(args[0]);
    if ([1, 2, 3, 4, 8].includes(reason) && diagnosticCheckpointSuspended) {
      try { void requestDiagnosticCheckpoint(); } catch {}
    }
    return result;
  }
  const callbacks = {
    menuDiagnosticSample: diagnostics?.observeNative || (() => {}),
    menuDiagnosticIncident: diagnosticIncident,
    menuStagingDiagnosticsEnabled() { return stagingDiagnosticsEnabled; },
    menuDiagnosticStagingSample(timestamp, activeSlots, occupiedSlots, registrations,
      callbacks, peakQueueCompletionCallbackMs, lastCompletedSourceFrame, force) {
      if (!stagingDiagnosticsEnabled) return false;
      return diagnostics?.observeStaging(timestamp, activeSlots, occupiedSlots, registrations,
        callbacks, peakQueueCompletionCallbackMs, lastCompletedSourceFrame, force === true || force === 1) || false;
    },
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
    menuOwnerStopped() { return fatal || destroyed; },
    menuServiceCommands() {
      // Native consumes this terminal signal before input, clocks, preparation
      // or source work. Never re-enter native teardown from an audio/error event.
      if (fatal || destroyed || laterBReplayTransfer.state()?.phase === 'uncertain') return -1;
      if (laterBReplayTransfer.suspended()) return 0;
      for (const c of commands.splice(0)) {
        if (fatal || destroyed) { c.reject(Error(message || 'Player stopped.')); continue; }
        try {
          const result = c.run();
          if (fatal || destroyed) c.reject(Error(message || 'Player stopped.'));
          else c.resolve(result);
        } catch (error) { c.reject(error); }
      }
      if (fatal || destroyed) return -1;
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
    // An in-flight native transaction may finish after a terminal audio error.
    // Its late preparation notifications must preserve that original failure.
    menuPreparation(label, keepAudio = false) { if (fatal || destroyed) return; diagnosticPreparationAt = performance.now(); diagnosticLifecycle('preparation', {timestamp: diagnosticPreparationAt}); preparationLabel = label || 'Preparing original scene'; preparationKeepsAudio = !!keepAudio; message = ''; setLoading('native', 'Preparing game data…', 0, 0); emit('preparation', {label: preparationLabel, keepAudio}); publish(); },
    menuPreparationDone() { if (fatal || destroyed) return; const at = performance.now(); diagnosticLifecycle('preparation_done', {timestamp: at, duration_ms: diagnosticPreparationAt === null ? null : at - diagnosticPreparationAt}); diagnosticPreparationAt = null; preparationLabel = ''; message = ''; if (loading?.phase === 'native') { loading = null; refreshCatalogLoading(); } emit('preparationDone'); publish(); },
    menuPreparationCanceled() { if (fatal || destroyed) return; preparationLabel = ''; preparationKeepsAudio = false; message = ''; if (loading?.phase === 'native') loading = null; emit('preparationCanceled'); publish(); },
    menuPreparationFailed(error) { if (fatal || destroyed) return; preparationLabel = ''; preparationKeepsAudio = false; message = error || 'Native preparation failed'; if (loading?.phase === 'native') loading = null; emit('preparationFailed', message); publish(); onError(Error(message)); },
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
    // This is a private native retirement event, never a synthetic frame.
    menuMatchDrawDiscardRetired() {
      if (laterBReplayTransfer.suspended()) return false;
      if (!ready || fatal || destroyed || terminalDrawDiscardRetirementConsumed ||
          typeof Module._melee_web_native_menu_match_draw_discard_status !== 'function') return false;
      let terminal;
      try { terminal = JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_draw_discard_status())); }
      catch { return false; }
      if (terminal?.native_retired !== true || Module._melee_web_native_menu_phase() !== 0 ||
          Module._melee_web_native_menu_running() !== 0) return false;
      terminalDrawDiscardRetirementConsumed = true;
      diagnosticLifecycle('scene_exit');
      publish(); // Derive diagnostic activity from the actual inactive owner.
      return diagnosticActive === false;
    },
    // The explicit-step Stadium path supplies its immutable C/F receipt from
    // native retirement. This callback deliberately reads no native exports:
    // the awaited ccall owns teardown and the host checks Asyncify idle after it settles.
    menuMatchedSetupCfRetired(receipt) {
      if (laterBReplayTransfer.suspended() && !laterBReplayTransfer.acceptsTerminal('retired', receipt)) return false;
      if (!ready || fatal || destroyed || terminalMatchedSetupCfRetirementConsumed ||
          receipt?.schema !== 'melee-web-stadium-matched-setup-cf-retirement-v1' ||
          receipt.native_retired !== true || receipt.terminal !== true || receipt.error !== null ||
          receipt.phase_after_close !== 0 || receipt.running_after_close !== 0) return false;
      const state = receipt.matched_setup_cf;
      if (!state || state.queued === true ||
          (state.complete === true && state.error !== null) ||
          (state.failed === true && typeof state.error !== 'string') ||
          !(state.complete === true || state.failed === true ||
            (state.awaiting_approval === true && state.last_action?.complete === true)) ||
          !['complete', 'failed', 'awaiting-approval'].includes(state.state)) return false;
      const firstRetirementObservation = !terminalMatchedSetupCfRetirementObserved;
      terminalMatchedSetupCfRetirementObserved = true;
      if (firstRetirementObservation) diagnosticLifecycle('scene_exit');
      // Native retirement closes the source owner, but it does not run the
      // ordinary unload callback that cancels a preparation label. Do not
      // consume host retirement while a host operation or unrelated loading
      // phase still owns the player; a later retry can settle it after unwind.
      const hasActiveOwner = !!busy || !!assetTransfer || commands.length !== 0;
      if (hasActiveOwner || (loading && loading.phase !== 'native')) {
        publish();
        return false;
      }
      const canCancelStaleNativePreparation = !!preparationLabel || loading?.phase === 'native';
      if (canCancelStaleNativePreparation) callbacks.menuPreparationCanceled();
      else publish();
      if (diagnosticActive !== false) return false;
      if (laterBReplayTransfer.suspended() && !laterBReplayTransfer.terminal('retired', receipt)) return false;
      terminalMatchedSetupCfRetirementConsumed = true;
      return true;
    },
    // These callbacks require a separately reviewed native producer. No public
    // caller receipt is accepted by the shell finalizer as native-close proof.
    menuLaterBReplayParentTransferred(receipt) { return laterBReplayTransfer.transferred(receipt); },
    menuLaterBReplayUnarmedAborted(receipt) { return laterBReplayTransfer.terminal('known-unarmed-abort', receipt); },
    // Private nonterminal canary boundary. Read native state only after the
    // generated Asyncify idle predicate; never consume terminal retirement or
    // hide B's native state. Ordinary unload/terminal paths stay unchanged.
    menuStadiumRecreationClosed(receipt) {
      if (laterBReplayTransfer.suspended()) return false;
      if (!ready || fatal || destroyed || stadiumRecreationCloseSettled ||
          terminalMatchedSetupCfRetirementObserved || terminalMatchedSetupCfRetirementConsumed ||
          busy || assetTransfer || commands.length || (loading && loading.phase !== 'native')) return false;
      try {
        if (Module.stadiumMatchedSetupNativeIdle?.() !== true ||
            typeof Module._melee_web_native_menu_stadium_recreation_status !== 'function' ||
            typeof Module._melee_web_native_menu_phase !== 'function' ||
            typeof Module._melee_web_native_menu_running !== 'function' ||
            typeof Module.UTF8ToString !== 'function') return false;
        const state = receipt?.matched_setup_cf;
        const prefixComplete = state?.schema === 'melee-web-stadium-source-action-native-v2' &&
          state.scope === 'proper-prefix-next-draw-tick-draw' && state.action_count === 174 && state.cursor === 174;
        const fullPairComplete = state?.schema === 'melee-web-stadium-source-action-native-v5' &&
          state.scope === 'original-full7167-common-fields-seed-observation-only' &&
          state.action_count === 7167 && state.cursor === 7167 &&
          state.common_field_count === 15 && state.seed_value_count === 1 &&
          state.source_ticks === 3585 && state.source_draws === 3582;
        const typed22Complete = receipt?.schema === 'melee-web-stadium-recreation-boundary-v2' &&
          state?.schema === 'melee-web-stadium-source-action-native-v7' &&
          state.scope === 'fresh132-full-pair-7168-typed22-seed-observation-only' &&
          state.common_field_count === 22 && state.seed_value_count === 1 &&
          state.action_count === 7168 && state.cursor === 7168 &&
          state.source_ticks === 3585 && state.source_draws === 3583;
        if ((!typed22Complete && receipt?.schema !== 'melee-web-stadium-recreation-boundary-v1') ||
            receipt.attempted !== true || receipt.closed !== true || receipt.restarted !== false || receipt.error !== null ||
            receipt.match_to_host_restored !== true || receipt.host_to_parent_restored !== true ||
            !Number.isSafeInteger(receipt.prior_world_generation) || receipt.prior_world_generation <= 0 ||
            ['match_seed', 'host_seed', 'parent_seed'].some(key => !Number.isInteger(receipt[key]) || receipt[key] < 0 || receipt[key] > 0xffffffff) ||
            (!prefixComplete && !fullPairComplete && !typed22Complete) ||
            (receipt.schema === 'melee-web-stadium-recreation-boundary-v2' && !typed22Complete) ||
            state.state !== 'complete' || state.complete !== true || state.failed !== false || state.error !== null ||
            state.queued !== false || state.awaiting_approval !== false) return false;
        const pointer = Module._melee_web_native_menu_stadium_recreation_status();
        if (!pointer || JSON.stringify(JSON.parse(Module.UTF8ToString(pointer))) !== JSON.stringify(receipt) ||
            Module._melee_web_native_menu_phase() !== 0 || Module._melee_web_native_menu_running() !== 0) return false;
      } catch { return false; }
      if (preparationLabel || loading?.phase === 'native') callbacks.menuPreparationCanceled();
      else publish();
      if (diagnosticActive !== false) return false;
      stadiumRecreationCloseSettled = true;
      emit('stadiumRecreationClosed', receipt);
      return true;
    },
    menuFrame(wasRunning) {
      if (laterBReplayTransfer.suspended()) { emit('frame', false); return; }
      if (!ready || fatal || destroyed) return;
      if (startupProfilingPending) profileCall('nativeFrame');
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
      beginDiagnosticCheckpoint();
    } else {
      resetDiagnosticCheckpoint();
      if (fatal) {
        diagnosticActivity(false);
      }
    }
  });
  for (const type of ['pagehide', 'pageshow', 'freeze', 'resume']) listen(type, () => {
    if (type === 'pagehide' || type === 'freeze') {
      lifecycleSuspended = true;
      diagnosticActivity(diagnosticActive);
      beginDiagnosticCheckpoint();
    } else {
      resetDiagnosticCheckpoint();
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
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation forbids ordinary save/unload access');
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
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation forbids ordinary save/unload access');
    if (baseline && !prepared && snapshot().phase === 0) throw Error('Select a disc before exporting Everything unlocked.');
    if (!ready || fatal || destroyed) throw Error('The player is unavailable. Reload to recover.');
    saveProfileSnapshotsActive++;
    try {
    const length = 0x1790 + 7 * 0x1F2C;
    const ptr = Module._malloc(length);
    if (!ptr) throw Error('Unable to allocate save snapshot memory.');
    try {
      await boundary(() => check(baseline ?
        Module._melee_web_native_menu_snapshot_unlocked_baseline(ptr, length) :
        Module._melee_web_native_menu_snapshot_save_profile(ptr, length, 0)));
      return new Uint8Array(Module.HEAPU8.slice(ptr, ptr + length));
    } finally { Module._free(ptr); }
    } finally { saveProfileSnapshotsActive--; }
  }
  async function unloadAndSave() {
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation forbids ordinary unload/cache flush');
    await closeNetworkSession('normal');
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
  async function putBatches(entries, generation = 0, reportProgress = true) {
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
      if (reportProgress && complete < total) { setLoading('handoff', 'Preparing game data…', complete, total); publish(); }
    }
  }
  function reportDiscRead(p) {
    if (fatal || destroyed) return;
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
      const stadiumC1a = typeof Module._melee_web_native_menu_stadium_c1a_asset_scope === 'function' &&
        await boundary(() => Module._melee_web_native_menu_stadium_c1a_asset_scope(generation) === 1);
      const streamScope = stadiumC1a
        ? discSession.streamStadiumC1aScope
        : discSession.streamScope;
      if (typeof streamScope !== 'function')
        throw Error(stadiumC1a
          ? 'The armed Stadium C1a import requires its private disc mapping.'
          : 'The local disc session cannot stream the requested asset scope.');
      let batch = [], batchBytes = 0, files = 0, bytes = 0;
      for await (const [name, data] of streamScope.call(discSession, names, reportDiscRead)) {
        // The next file may exceed the existing batch budget on its own. Keep
        // at most the bounded pending batch plus this one complete file alive.
        if (batch.length && (batch.length >= IMPORT_BATCH_MAX_FILES ||
            batchBytes + data.byteLength > IMPORT_BATCH_MAX_BYTES)) {
          await putBatches(batch, generation, false);
          batch = []; batchBytes = 0;
        }
        batch.push([name, data]); batchBytes += data.byteLength;
        ++files; bytes += data.byteLength;
      }
      setLoading('handoff', 'Preparing game data…', 0, files); publish();
      await putBatches(batch, generation, false);
      batch = [];
      await boundary(() => check(Module._melee_web_native_asset_commit(generation)));
      diagnosticLifecycle('asset_preparation', {timestamp: performance.now(),
        duration_ms: performance.now() - diagnosticStarted, files, bytes});
      emit('assetScopeCommitted', {generation, files, bytes});
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
    version: 1, getState: snapshot, getStartupProfile: () => profileCall('snapshot'), focus,
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
          typeof session.streamScope !== 'function') {
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
    async discIdentity() {
      if (!discSession || typeof discSession.identity !== 'function')
        throw Error('A validated local disc identity is unavailable.');
      return discSession.identity();
    },
    importDisc(file, {preopenedSession = null} = {}) {
      if (preopenedSession && (!openedDiscSessions.has(preopenedSession) ||
          typeof preopenedSession.close !== 'function' || typeof preopenedSession.streamScope !== 'function')) {
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
        // A running AudioContext and a port state acknowledgement can precede
        // its first output callback. Start source time only after the renderer
        // processes a quantum following this preparation.
        await waitForAudioRender();
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
    unload() {
    if (laterBReplayTransfer.suspended()) throw Error('Private replay continuation forbids ordinary save/unload access');
      if (unloadOperation) return unloadOperation;
      unloadOperation = operation('unloading', async () => { check(await unloadAndSave()); })
        .finally(() => { unloadOperation = null; });
      return unloadOperation;
    },
    destroy() {
      if (laterBReplayTransfer.suspended()) return Promise.reject(Error('Private replay continuation requires owned OS cleanup'));
      if (destroyOperation) return destroyOperation;
      profileCall('destroy');
      startupProfilingPending = false;
      destroyOperation = (async () => {
        let operationError = null;
        try {
          if (!fatal) await handle.unload();
          else await closeNetworkSession('fatal');
        } catch (error) {
          if (!fatal) { operationError = error; stop(error); }
          else recordNetworkCleanupError(error);
        } finally {
          clearStartupTimeout();
          if (!fatal && discSession) {
            try { await clearSourceFileStreams(); }
            catch (error) { onLog(`Source movie catalog cleanup failed: ${error.message}`, true); }
          }
          if (fatal) {
            try { await closeNetworkSession('fatal'); } catch {}
          }
          await retireDiscSession();
          destroyed = true; syncAudio();
          diagnosticLifecycle('scene_exit'); diagnosticActivity(false);
          try { await awaitDiagnosticCheckpoint(); } catch {}
          cancelDiagnosticDelivery(); diagnosticDelivery?.dispose();
          longtaskObserver?.disconnect();
          try { await audio?.destroy(); }
          finally {
            for (const [type, listener] of listeners) window.removeEventListener(type, listener, true);
            publish();
          }
        }
        if (operationError) throw operationError;
        // Native consumes the terminal owner state at its next safe boundary.
        // The global Emscripten heap remains until this document retires.
        return Object.freeze({requiresReload: true});
      })();
      destroyOperation.catch(() => {});
      return destroyOperation;
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
  try {
    onOwner?.({Module, boundary, handle, status, check, put, prepareAudio, pauseAudioForPreparation,
      laterBReplayTransfer, syncAudio, unloadAndSave, prepareNativeResources, waitForAudioAck, waitForAudioRender, stop, callbacks,
      attachNetworkSession, getNetworkCleanupErrors: () => Object.freeze(networkCleanupErrors.slice()), diagnostics});
    configureModule?.(Module);
  } catch (error) { failProfile('module_setup_failure'); throw error; }
  // Native seed loading needs this directory even when optional browser
  // persistence is disabled or unavailable. Keep that prerequisite in the
  // shared owner, ahead of any entry-specific cache mount/populate callback.
  const configuredPreRun = Module.preRun;
  Module.preRun = [() => {
    Module.FS.mkdirTree('/melee-render-cache');
    const callbacks = typeof configuredPreRun === 'function' ? [configuredPreRun] : configuredPreRun || [];
    for (const callback of callbacks) callback(Module);
  }];
  try {
    window.Module = Module;
    publish();
    const loader = document.createElement('script'); loader.src = String(loaderUrl);
    loader.onload = () => profileCall('loaderLoaded');
    loader.onerror = () => stop(Error('The player files could not load. Reload to retry.'), 'loader_failure');
    if (!fatal && !destroyed && !(ready && startupCacheReady && graphicsPreparationReady()))
      startupTimeoutHandle = setTimeout(expireStartup, Math.max(0, startupDeadlineAt - Date.now()));
    document.head.append(loader);
    profileCall('loaderInserted');
  } catch (error) {
    failProfile('loader_failure');
    clearStartupTimeout();
    throw error;
  }
  try { await startup; return handle; }
  finally {
    if (startupCacheReady || fatal || destroyed) {
      clearStartupTimeout();
    }
  }
}
