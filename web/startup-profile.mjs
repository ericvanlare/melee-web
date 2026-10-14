/** Bounded, local-only startup timing for the public player. */
export const STARTUP_PROFILE_SCHEMA = 'webmelee-startup-profile-v1';

const MAX_TIMELINE_EVENTS = 32;
const MAX_RESOURCE_ROWS = 8;
const MAX_LONGTASK_ROWS = 24;
const MAX_GRAPHICS_SAMPLES = 64;
const GRAPHICS_SAMPLE_INTERVAL_MS = 1000;
const ASSET_NAMES = new Set([
  'gameplay_public.js', 'gameplay_public.wasm', 'gameplay_public.data',
  'gameplay_audio_preview.js', 'gameplay_audio_preview.wasm', 'gameplay_audio_preview.data',
]);
const EVENT_NAMES = new Set([
  'mount_started', 'environment_ready', 'adapter_requested', 'adapter_ready',
  'loader_inserted', 'loader_loaded', 'runtime_initialized', 'first_native_frame',
  'native_cache_state', 'pipeline_status', 'pipeline_ready', 'graphics_startup_status',
  'graphics_startup_terminal', 'graphics_ready',
  'startup_failed', 'runtime_destroyed',
]);
const PIPELINE_FIELDS = Object.freeze(['ready', 'state', 'selected', 'pending', 'unexpected_count',
  'deferred_count', 'error_count', 'binding_sha256', 'renderer_init_ms', 'union_submit_ms',
  'union_ready_ms', 'bootstrap_total_ms']);
const GRAPHICS_STARTUP_FIELDS = Object.freeze(['renderer_init_ms', 'queued_pipelines', 'created_pipelines',
  'cache_idle', 'first_native_callback_ms', 'elapsed_ms', 'terminal']);
const FAILURE_KINDS = new Set([
  'missing_canvas', 'document_owned', 'insecure_context', 'webgpu_unavailable',
  'isolation_unavailable', 'adapter_api_unavailable', 'adapter_unavailable',
  'adapter_failure', 'startup_timeout', 'loader_failure', 'runtime_failure',
  'module_setup_failure',
]);
const PHASE_NAMES = new Set([
  'environment', 'adapter', 'module_setup', 'module_load', 'native_startup',
  'readiness', 'cache_idle', 'pipeline_catalog', 'graphics_ready', 'failed', 'destroyed',
]);

const finite = value => typeof value === 'number' && Number.isFinite(value) ? value : null;
const integer = value => finite(value) !== null && Number.isSafeInteger(value) ? value : null;
const nonNegative = value => finite(value) !== null && value >= 0 ? value : null;
const nonNegativeInteger = value => integer(value) !== null && value >= 0 ? value : null;
const round = value => value === null ? null : Math.round(value * 1000) / 1000;
function hasMethod(target, key) {
  try { return typeof target?.[key] === 'function'; }
  catch { return false; }
}

function identitySnapshot(value) {
  const source = value && typeof value === 'object' ? value : {};
  const commit = typeof source.source_commit === 'string' && /^[a-f0-9]{40}$/.test(source.source_commit) ? source.source_commit : null;
  const runtime = typeof source.runtime_hash === 'string' && /^[a-f0-9]{16}$/.test(source.runtime_hash) ? source.runtime_hash : null;
  const profile = typeof source.build_profile === 'string' && /^[a-z0-9][a-z0-9._-]{0,31}$/.test(source.build_profile) ? source.build_profile : null;
  return Object.freeze({schema_version: source.schema_version === 1 ? 1 : null,
    source_commit: commit, runtime_hash: runtime, build_profile: profile,
    available: commit !== null && runtime !== null && profile !== null});
}

function pipelineSnapshot(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const ready = typeof value.ready === 'boolean' ? value.ready : null;
  const digest = typeof value.binding_sha256 === 'string' && /^[a-f0-9]{64}$/.test(value.binding_sha256) ?
    value.binding_sha256 : null;
  return Object.freeze({
    ready,
    state: integer(value.state),
    selected: nonNegativeInteger(value.selected),
    pending: nonNegativeInteger(value.pending),
    unexpected_count: nonNegativeInteger(value.unexpected_count),
    deferred_count: nonNegativeInteger(value.deferred_count),
    error_count: nonNegativeInteger(value.error_count),
    binding_sha256: digest,
    renderer_init_ms: nonNegative(value.renderer_init_ms),
    union_submit_ms: nonNegative(value.union_submit_ms),
    union_ready_ms: ready === true ? nonNegative(value.union_ready_ms) : null,
    bootstrap_total_ms: ready === true ? nonNegative(value.bootstrap_total_ms) : null,
  });
}

function pipelineField(value, key) {
  if (key === 'ready') return typeof value.ready === 'boolean' ? value.ready : null;
  if (key === 'state') return integer(value.state);
  if (key === 'selected' || key === 'pending' || key === 'unexpected_count' ||
      key === 'deferred_count' || key === 'error_count') return nonNegativeInteger(value[key]);
  if (key === 'binding_sha256') return typeof value.binding_sha256 === 'string' && /^[a-f0-9]{64}$/.test(value.binding_sha256) ?
    value.binding_sha256 : null;
  if ((key === 'union_ready_ms' || key === 'bootstrap_total_ms') && value.ready !== true) return null;
  return nonNegative(value[key]);
}

function graphicsStartupSnapshot(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const cacheIdle = [-1, 0, 1].includes(value.cache_idle) ? value.cache_idle : null;
  return Object.freeze({
    renderer_init_ms: nonNegative(value.renderer_init_ms),
    queued_pipelines: nonNegativeInteger(value.queued_pipelines),
    created_pipelines: nonNegativeInteger(value.created_pipelines),
    cache_idle: cacheIdle,
    first_native_callback_ms: nonNegative(value.first_native_callback_ms),
    elapsed_ms: nonNegative(value.elapsed_ms),
    terminal: typeof value.terminal === 'boolean' ? value.terminal : null,
  });
}

function graphicsStartupField(value, key) {
  if (key === 'queued_pipelines' || key === 'created_pipelines') return nonNegativeInteger(value[key]);
  if (key === 'cache_idle') return [-1, 0, 1].includes(value.cache_idle) ? value.cache_idle : null;
  if (key === 'terminal') return typeof value.terminal === 'boolean' ? value.terminal : null;
  return nonNegative(value[key]);
}

export function createStartupProfile({identity = null, onUpdate = () => {}, root = globalThis,
  autoTick = true, initialNotify = true} = {}) {
  const performanceApi = (() => { try { return root.performance || null; } catch { return null; } })();
  const dateNow = () => { try { return Date.now(); } catch { return 0; } };
  const wallStart = dateNow();
  const now = () => {
    try {
      const value = performanceApi?.now?.();
      if (Number.isFinite(value)) return value;
    } catch {}
    return Math.max(0, dateNow() - wallStart);
  };
  const startedAt = now();
  const navigationStart = (() => {
    try {
      const entries = performanceApi?.getEntriesByType?.('navigation');
      const value = entries?.[0]?.startTime;
      return Number.isFinite(value) ? value : null;
    } catch { return null; }
  })();
  const normalizedIdentity = identitySnapshot(identity);
  let status = 'pending', terminalReason = null, currentPhase = 'environment';
  let currentPhaseStartedAt = startedAt, firstNativeFrame = false;
  let cacheState = null, pipelineAvailable = false, nativePipeline = null;
  let nativeGraphicsStartup = null;
  let droppedTimeline = 0, droppedLongtasks = 0, callbackErrors = 0;
  let longtaskCount = 0, longtaskTotalMs = 0, longtaskMaxMs = null;
  let resourceApiAvailable = false, resourceApiChecked = false, assetBaseUrl = null;
  let performanceMarkAvailable = hasMethod(performanceApi, 'mark');
  let performanceMeasureAvailable = hasMethod(performanceApi, 'measure');
  let longtaskState = 'unavailable', longtaskObserver = null, intervalHandle = null;
  let eventSequence = 0, lastEventName = null;
  let droppedResources = 0, resourceScanTruncated = false;
  let preProfileLongtaskCount = 0, preProfileLongtaskTotalMs = 0, terminalAt = null;
  let droppedGraphicsSamples = 0, lastGraphicsSampleAt = null, finalSnapshot = null;
  const timeline = [], longtasks = [];
  const graphicsSamples = [];

  function finiteField(value) { return nonNegative(value); }
  function setPhase(value) {
    if (!PHASE_NAMES.has(value) || currentPhase === value) return;
    currentPhase = value;
    currentPhaseStartedAt = now();
  }
  function performanceMark(name) {
    const mark = `webmelee-startup:${name}:${++eventSequence}`;
    try { performanceApi?.mark?.(mark); }
    catch { performanceMarkAvailable = false; }
    return mark;
  }
  let lastMark = null;
  function measureSinceLast(mark, name) {
    if (!lastMark) return;
    try {
      if (typeof performanceApi?.measure !== 'function') { performanceMeasureAvailable = false; return; }
      performanceApi.measure(`webmelee-startup:between:${lastEventName || 'start'}:${name}`, lastMark, mark);
    }
    catch { performanceMeasureAvailable = false; }
  }
  function record(name, value = null, phase = null) {
    if (status !== 'pending' || !EVENT_NAMES.has(name)) return;
    const at = now();
    const mark = performanceMark(name);
    measureSinceLast(mark, name);
    lastMark = mark; lastEventName = name;
    if (timeline.length < MAX_TIMELINE_EVENTS) {
      const row = {event: name, at_ms: round(Math.max(0, at - startedAt))};
      if (value !== null) row.value = value;
      timeline.push(Object.freeze(row));
    } else droppedTimeline++;
    if (phase) setPhase(phase);
  }
  function cacheLabel(value) {
    return value === 0 ? 'pending' : value === 1 ? 'idle' : value === -1 ? 'error' : 'unobserved';
  }
  function pipelinePhase() {
    if (status !== 'pending') return;
    if (!firstNativeFrame) return;
    const cacheDone = cacheState === 1 || cacheState === -1;
    if (!cacheDone && nativePipeline?.ready === false) setPhase('readiness');
    else if (!cacheDone) setPhase('cache_idle');
    else if (nativePipeline?.ready === false) setPhase('pipeline_catalog');
    else if (!pipelineAvailable) setPhase('readiness');
  }
  function stopObservers() {
    if (intervalHandle !== null) {
      try { root.clearInterval?.(intervalHandle); } catch {}
      intervalHandle = null;
    }
    flushLongtaskRecords();
    try { longtaskObserver?.disconnect?.(); } catch {}
    longtaskObserver = null;
  }
  function appendLongtasks(entries) {
    if (!entries || status !== 'pending') return;
    for (const entry of entries) {
      try {
        const start = finiteField(entry.startTime), duration = finiteField(entry.duration);
        if (start === null || duration === null) continue;
        const beforeProfile = start < startedAt;
        longtaskCount++;
        longtaskTotalMs += duration;
        longtaskMaxMs = longtaskMaxMs === null ? duration : Math.max(longtaskMaxMs, duration);
        if (beforeProfile) {
          preProfileLongtaskCount++;
          preProfileLongtaskTotalMs += duration;
        }
        if (longtasks.length < MAX_LONGTASK_ROWS)
          longtasks.push(Object.freeze({start_ms: round(start), duration_ms: round(duration), before_profile: beforeProfile}));
        else droppedLongtasks++;
      } catch { /* One malformed optional entry is ignored. */ }
    }
  }
  function flushLongtaskRecords() {
    if (!longtaskObserver || status !== 'pending') return;
    try { appendLongtasks(longtaskObserver.takeRecords?.() || []); }
    catch { /* Optional performance evidence cannot affect player startup. */ }
  }
  function resourceRows() {
    let entries = [];
    try {
      const values = performanceApi?.getEntriesByType?.('resource');
      resourceApiAvailable = Array.isArray(values);
      resourceApiChecked = true;
      if (resourceApiAvailable) {
        resourceScanTruncated = values.length > 512;
        entries = values.slice(-512);
      }
    } catch { resourceApiAvailable = false; resourceApiChecked = true; }
    if (!assetBaseUrl) return [];
    const href = assetBaseUrl.href;
    const rows = [];
    let matched = 0;
    for (const entry of entries) {
      try {
        const parsed = new URL(String(entry.name), href);
        const name = parsed.pathname.slice(parsed.pathname.lastIndexOf('/') + 1);
        if (parsed.origin !== assetBaseUrl.origin || !ASSET_NAMES.has(name) ||
            parsed.pathname !== `${assetBaseUrl.pathname}${name}`) continue;
        matched++;
        if (rows.length >= MAX_RESOURCE_ROWS) continue;
        rows.push(Object.freeze({name,
          start_ms: round(finiteField(entry.startTime)),
          duration_ms: round(finiteField(entry.duration)),
          transfer_size_bytes: integer(entry.transferSize),
          encoded_body_size_bytes: integer(entry.encodedBodySize),
          decoded_body_size_bytes: integer(entry.decodedBodySize),
          initiator_type: ['script', 'fetch', 'xmlhttprequest', 'other'].includes(entry.initiatorType) ? entry.initiatorType : 'other'}));
      } catch { /* Resource timing is optional and never affects startup. */ }
    }
    droppedResources = Math.max(0, matched - rows.length);
    return rows;
  }
  function sampleGraphicsStartup(value, force = false) {
    if (!value) return;
    const at = now();
    if (!force && lastGraphicsSampleAt !== null && at - lastGraphicsSampleAt < GRAPHICS_SAMPLE_INTERVAL_MS) return;
    const row = Object.freeze({at_ms: round(Math.max(0, at - startedAt)),
      elapsed_ms: nonNegative(value.elapsed_ms), queued_pipelines: nonNegativeInteger(value.queued_pipelines),
      created_pipelines: nonNegativeInteger(value.created_pipelines),
      cache_idle: [-1, 0, 1].includes(value.cache_idle) ? value.cache_idle : null,
      terminal: typeof value.terminal === 'boolean' ? value.terminal : null});
    const last = graphicsSamples[graphicsSamples.length - 1];
    if (force && last && last.at_ms === row.at_ms && last.elapsed_ms === row.elapsed_ms &&
        last.queued_pipelines === row.queued_pipelines && last.created_pipelines === row.created_pipelines &&
        last.cache_idle === row.cache_idle && last.terminal === row.terminal) return;
    if (graphicsSamples.length >= MAX_GRAPHICS_SAMPLES) {
      droppedGraphicsSamples++;
      if (!force) return;
      graphicsSamples.pop();
    }
    graphicsSamples.push(row);
    lastGraphicsSampleAt = at;
  }
  function buildSnapshot(at = now()) {
    const resources = resourceRows();
    return Object.freeze({
      schema: STARTUP_PROFILE_SCHEMA,
      status,
      terminal: status !== 'pending',
      terminal_reason: terminalReason,
      phase: currentPhase,
      elapsed_ms: round(Math.max(0, at - startedAt)),
      phase_elapsed_ms: round(Math.max(0, at - currentPhaseStartedAt)),
      navigation_to_profile_start_ms: navigationStart === null ? null : round(Math.max(0, startedAt - navigationStart)),
      navigation_elapsed_ms: navigationStart === null ? null : round(Math.max(0, at - navigationStart)),
      identity: normalizedIdentity,
      timeline: Object.freeze(timeline.slice()),
      dropped_timeline_events: droppedTimeline,
      native_cache_idle: Object.freeze({state: integer(cacheState), status: cacheLabel(cacheState)}),
      native_pipeline: pipelineAvailable ? Object.freeze({...nativePipeline}) : null,
      graphics_startup: nativeGraphicsStartup ? Object.freeze({...nativeGraphicsStartup}) : null,
      graphics_samples: Object.freeze(graphicsSamples.slice()),
      dropped_graphics_samples: droppedGraphicsSamples,
      resources: Object.freeze(resources),
      dropped_resource_rows: droppedResources,
      resource_scan_truncated: resourceScanTruncated,
      longtasks: Object.freeze({status: longtaskState, count: longtaskCount,
        total_ms: round(longtaskTotalMs), max_ms: round(longtaskMaxMs),
        before_profile_count: preProfileLongtaskCount,
        before_profile_total_ms: round(preProfileLongtaskTotalMs),
        dropped_rows: droppedLongtasks, rows: Object.freeze(longtasks.slice())}),
      coverage: Object.freeze({navigation_timing: navigationStart !== null,
        performance_marks: performanceMarkAvailable,
        performance_measures: performanceMeasureAvailable,
        resource_timing: resourceApiChecked && resourceApiAvailable,
        resource_scope: assetBaseUrl ? 'exact_loader_assets' : 'not_configured',
        resource_scan_truncated: resourceScanTruncated,
        longtask_observer: longtaskState === 'available',
        longtask_buffered_before_profile: preProfileLongtaskCount > 0,
        longtask_pre_profile_coverage: longtaskState === 'available' ? 'buffered_entries_may_precede_profile' : 'unavailable',
        identity: normalizedIdentity.available,
        first_native_frame: firstNativeFrame,
        native_cache_idle_observed: cacheState !== null,
        native_pipeline_status: pipelineAvailable ? (nativePipeline?.ready === null ? 'ready_field_unavailable' : 'observed') : 'not_observed',
        pipeline_ready_observed: nativePipeline?.ready === true,
        graphics_startup_status: nativeGraphicsStartup === null ? 'not_observed' : 'observed',
        callback_errors: callbackErrors}),
    });
  }
  function snapshot() { return finalSnapshot || buildSnapshot(); }
  function notify() {
    try { onUpdate(snapshot()); }
    catch {
      callbackErrors = Math.min(Number.MAX_SAFE_INTEGER, callbackErrors + 1);
      if (finalSnapshot && terminalAt !== null) finalSnapshot = buildSnapshot(terminalAt);
    }
  }
  function finish(nextStatus, reason) {
    if (status !== 'pending') return false;
    terminalReason = reason;
    setPhase(nextStatus === 'ready' ? 'graphics_ready' : nextStatus === 'destroyed' ? 'destroyed' : 'failed');
    record(nextStatus === 'ready' ? 'graphics_ready' : nextStatus === 'destroyed' ? 'runtime_destroyed' : 'startup_failed',
      nextStatus === 'failed' ? reason : null, currentPhase);
    sampleGraphicsStartup(nativeGraphicsStartup, true);
    stopObservers();
    status = nextStatus;
    terminalAt = now();
    finalSnapshot = buildSnapshot(terminalAt);
    notify();
    return true;
  }
  function beginLongtaskObserver() {
    const Observer = (() => { try { return root.PerformanceObserver; } catch { return null; } })();
    if (typeof Observer !== 'function') return;
    try {
      longtaskObserver = new Observer(list => {
        try { appendLongtasks(list.getEntries()); }
        catch { callbackErrors = Math.min(Number.MAX_SAFE_INTEGER, callbackErrors + 1); }
      });
      longtaskObserver.observe({type: 'longtask', buffered: true});
      longtaskState = 'available';
    } catch { longtaskState = 'unavailable'; longtaskObserver = null; }
  }

  record('mount_started', null, 'environment');
  beginLongtaskObserver();
  if (autoTick) {
    try {
      intervalHandle = root.setInterval?.(() => { if (status === 'pending') notify(); }, 1000) ?? null;
    } catch { intervalHandle = null; }
  }
  if (initialNotify) notify();

  return Object.freeze({
    snapshot,
    environmentReady() { record('environment_ready', null, 'adapter'); notify(); },
    setAssetBase(value) {
      if (status !== 'pending') return;
      try {
        const parsed = value instanceof URL ? new URL(value.href) : new URL(String(value), root.location?.href);
        if (parsed.protocol !== 'https:' && parsed.protocol !== 'http:') return;
        if (!parsed.pathname.endsWith('/')) parsed.pathname = `${parsed.pathname}/`;
        parsed.search = ''; parsed.hash = '';
        assetBaseUrl = parsed;
      } catch { assetBaseUrl = null; }
      notify();
    },
    adapterRequested() { record('adapter_requested', null, 'adapter'); notify(); },
    adapterReady() { record('adapter_ready', null, 'module_setup'); notify(); },
    loaderInserted() { record('loader_inserted', null, 'module_load'); notify(); },
    loaderLoaded() { record('loader_loaded'); notify(); },
    runtimeInitialized() { record('runtime_initialized', null, 'native_startup'); notify(); },
    nativeFrame() {
      if (firstNativeFrame) return;
      firstNativeFrame = true;
      record('first_native_frame', null, 'readiness');
      pipelinePhase();
      notify();
    },
    observeCacheIdle(value) {
      if (status !== 'pending' || ![-1, 0, 1].includes(value) || cacheState === value) return;
      cacheState = value;
      record('native_cache_state', value);
      pipelinePhase();
      notify();
    },
    observeGraphicsStartup(value) {
      if (status !== 'pending') return;
      const prior = nativeGraphicsStartup;
      const first = prior === null;
      if (!value || typeof value !== 'object' || Array.isArray(value)) return;
      const priorTerminal = prior?.terminal;
      let changed = first;
      for (const key of GRAPHICS_STARTUP_FIELDS) {
        if (graphicsStartupField(value, key) !== prior?.[key]) { changed = true; break; }
      }
      if (!changed) return;
      const next = graphicsStartupSnapshot(value);
      if (first) nativeGraphicsStartup = {...next};
      else Object.assign(nativeGraphicsStartup, next);
      sampleGraphicsStartup(nativeGraphicsStartup);
      if (first) record('graphics_startup_status');
      if (next.terminal === true && priorTerminal !== true) record('graphics_startup_terminal');
      pipelinePhase();
      if (first || next.terminal === true && priorTerminal !== true) notify();
    },
    observePipeline(value) {
      if (status !== 'pending') return;
      const first = !pipelineAvailable;
      if (!value || typeof value !== 'object' || Array.isArray(value)) return;
      let changed = first;
      for (const key of PIPELINE_FIELDS) {
        if (pipelineField(value, key) !== nativePipeline?.[key]) { changed = true; break; }
      }
      if (!changed) return;
      const next = pipelineSnapshot(value);
      const priorReady = nativePipeline?.ready;
      pipelineAvailable = true;
      if (first) nativePipeline = {...next};
      else Object.assign(nativePipeline, next);
      if (first) record('pipeline_status');
      if (next.ready === true && priorReady !== true) record('pipeline_ready');
      pipelinePhase();
      if (first || next.ready === true && priorReady !== true || next.error_count > 0 && changed) notify();
    },
    observeGraphicsReady(ready) {
      if (ready === true) finish('ready', 'graphics_ready');
    },
    fail(kind = 'runtime_failure') {
      finish('failed', FAILURE_KINDS.has(kind) ? kind : 'runtime_failure');
    },
    destroy() {
      if (status === 'pending') finish('destroyed', 'destroyed');
    },
  });
}
