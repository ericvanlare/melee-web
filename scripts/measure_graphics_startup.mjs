#!/usr/bin/env node
/**
 * Headless startup timing probe for a packaged public player.
 *
 * This is diagnostic only. It selects a real local disc while the graphics
 * panel is visible, records Aurora/WebGPU calls and ordinary browser callback
 * timing, then stops at playable CSS. It does not advance gameplay.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from './browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from './browser_tools.mjs';

const {values} = parseArgs({options: {
  url: {type: 'string'}, disc: {type: 'string'}, out: {type: 'string'},
  manifest: {type: 'string'}, playwright: {type: 'string'},
  condition: {type: 'string'}, runs: {type: 'string', default: '3'},
  'boundary-only': {type: 'boolean', default: false},
  'async-mode': {type: 'string'}, 'async-inflight': {type: 'string'},
  'async-test-stale-completion': {type: 'boolean', default: false},
  'async-test-retire-owner': {type: 'boolean', default: false},
  'async-fail-first': {type: 'boolean', default: false},
  'expect-async-failure': {type: 'boolean', default: false},
}});
if (!values.url || (!values.disc && !values['boundary-only'] && !values['expect-async-failure']) || !values.out || !values.manifest ||
    !['cold', 'warm'].includes(values.condition)) {
  throw Error('Use --url ORIGIN [--disc OWNED_DISC] --out DIR --manifest PACKAGE_MANIFEST --condition cold|warm [--runs 3] [--boundary-only] [--playwright DIR]');
}
const runCount = Number(values.runs);
assert(Number.isInteger(runCount) && runCount >= 1 && runCount <= 10, '--runs must be between 1 and 10');
const startupTimeoutMs = values['async-mode'] === 'all' ? 240000 : 90000;
if (values['async-mode'] !== undefined) {
  assert(['one', 'all'].includes(values['async-mode']), '--async-mode must be one|all');
  assert(['1', '2'].includes(values['async-inflight']), '--async-inflight must be 1|2');
}
if (values['expect-async-failure']) {
  assert(values['async-mode'] === 'one' && values['async-fail-first'],
    '--expect-async-failure requires --async-mode one --async-fail-first');
}
if (values['async-test-retire-owner']) {
  assert(values['async-mode'] === 'all' && values['async-inflight'] === '1',
    '--async-test-retire-owner requires --async-mode all --async-inflight 1');
}
const discPath = values.disc ? path.resolve(values.disc) : null;
if (discPath) await fs.access(discPath);
const manifestPath = path.resolve(values.manifest);
const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
const isAudioPackage = manifest.schema === 'melee-web-audio-player-package-v1' && manifest.profile === 'audio-player';
const isSilentPlayerPackage = manifest.schema === 'melee-web-public-release-v1' && manifest.profile === 'player' &&
  manifest.runtime?.identity?.audio_policy?.mode === 'disabled';
assert(isAudioPackage || isSilentPlayerPackage, 'Manifest must identify a reviewed audio or silent public player package');
const runtimeIdentity = isSilentPlayerPackage ? manifest.runtime.identity : null;
await fs.mkdir(values.out, {recursive: true});
const packageFiles = await fs.readdir(path.resolve(values.out));
assert(packageFiles.length === 0, '--out must be a new or empty directory');

const browserPath = values.playwright || process.env.MELEE_PLAYWRIGHT_DIR ||
  path.resolve('work/deploy-tools/node_modules/playwright');
const {chromium, browser: launchOptions} = await loadBrowserTools(browserPath);
const origin = new URL(values.url).origin;
const report = {
  schema: 'webmelee-graphics-startup-measurement-v1',
  scope: values['boundary-only'] ?
    'Headless bounded reproducer: no disc import; isolate UI graphics-ready boundary and Aurora pipeline preparation.' :
    'Headless startup diagnostic: real local disc selected while Preparing graphics is visible, then stop at playable CSS. No source gameplay ticks are requested by the harness.',
  condition: values.condition,
  cache_definition: values.condition === 'cold' ?
    'One fresh headless Chrome process and ephemeral context per sample; no prior origin HTTP/IDB state in the run context. Browser/OS graphics-driver disk caches are not cleared or controlled.' :
    'One headless Chrome process and one browser context for all samples, preceded by one unscored real-disc startup. Each sample navigates to a fresh document; context HTTP and process GPU caches are retained. The public player has no IDBFS renderer-cache persistence; its bundled seed is constant.',
  build_identity: {
    schema: manifest.schema, profile: manifest.profile,
    source_sha: manifest.source_sha ?? null,
    runtime_hash: manifest.runtime_hash ?? manifest.runtime?.hash,
    identity_sha256: manifest.identity_sha256 ?? manifest.runtime?.identity_sha256,
    prepared_gameplay_pinned_commit: runtimeIdentity?.source_inputs?.prepared_gameplay?.pinned_commit ?? null,
    manifest_sha256: await sha256(await fs.readFile(manifestPath)),
  },
  machine: {
    platform: process.platform, arch: process.arch, os_release: os.release(),
    cpu_model: os.cpus()[0]?.model || null,
  },
  browser_mode: 'headless installed Chrome through scripts/browser_tools.mjs',
  instrumentation_overhead: values['async-mode'] ?
    'Candidate adds one Promise fulfillment/rejection observer per async pipeline, C++ submit/complete timestamps and compact native identity events, plus 250 ms JS/Wasm heap samples. The WebGPU wrapper observer adds one performance.now timestamp at submit and one at settlement; GPU memory is not browser-observable.' :
    'The existing page observer records requestAnimationFrame/setTimeout timing, WebGPU call duration, UI boundaries and long tasks. It does not wait on the GPU or add pipeline compilation calls.',
  async_experiment_policy: values['async-mode'] ? {
    mode: values['async-mode'], maxInFlight: Number(values['async-inflight']),
    failFirst: values['async-fail-first'],
    testStaleCompletion: values['async-test-stale-completion'],
    testRetireOwner: values['async-test-retire-owner'],
    scheduling: 'Submit at most the configured number from a zero-delay browser task. Each completion schedules the next pump as a separate task; the existing frame worker keeps its synchronous batch cap and never consumes an async-pending entry.',
  } : null,
  attempts: [],
  failures: [],
};

function sha256(data) {
  return import('node:crypto').then(({createHash}) => createHash('sha256').update(data).digest('hex'));
}
function probeInstall() {
  const p = window.__graphicsStartupProbe = {
    startedAt: performance.now(),
    frameSeq: 0, callbackSeq: 0, lastFrameTimestamp: null,
    activeCallback: null, callbacks: [], api: [], nativeBatches: [], lastNativeApiIndex: 0,
    asyncPipelineRequests: [], asyncPipelineInFlight: 0, asyncPipelinePeakInFlight: 0,
    memorySamples: [],
    markers: [], ui: [], longTasks: [], frameGaps: [], frameGapEvents: [], rawFrameTimestampGaps: [],
    schedulerCounts: {raf: 0, timeout: 0, interval: 0},
    errors: [], hookErrors: [], orphanNativeFrames: 0,
    cacheIdle: null, cacheIdleTransitions: [], pipelinePreparation: null,
    graphicsPanelSeen: false, graphicsReadyAt: null, cssReadyAt: null,
    runtimeHooksInstalled: false, wrappedGlobals: [],
  };
  const now = () => performance.now();
  const mark = (name, data = {}) => p.markers.push({name, at: now(), ...data});
  const nodeState = node => ({
    text: node?.textContent?.trim().replace(/\s+/g, ' ').slice(0, 180) || '',
    hidden: !!node?.hidden, disabled: !!node?.disabled, open: !!node?.open,
  });
  const getNode = id => document.getElementById(id);
  const rememberUi = (id, target) => {
    const node = getNode(id);
    if (!node) return;
    const state = nodeState(node);
    p.ui.push({id, at: now(), ...state});
    if (id === 'loading-label' && !getNode('loading-panel')?.hidden && /Preparing graphics/i.test(state.text)) {
      p.graphicsPanelSeen = true;
      mark('graphics-panel-visible', {label: state.text});
    }
    if (id === 'loading-panel' && p.graphicsPanelSeen && state.hidden && p.graphicsReadyAt === null) {
      p.graphicsReadyAt = now();
      mark('graphics-ui-ready', {source: 'loading-panel-hidden'});
    }
    if (id === 'disc-selection-status') {
      if (/^Checking local disc\b/i.test(state.text)) mark('disc-validation-start', {status: state.text});
      if (/^Disc validated; (waiting for graphics|preparing)\b/i.test(state.text))
        mark('disc-validation-ready', {status: state.text});
      if (/^Disc validated; preparing\b/i.test(state.text) || /^Preparing local data for\b/i.test(state.text)) {
        if (p.graphicsReadyAt === null) {
          p.graphicsReadyAt = now();
          mark('graphics-ui-ready', {source: 'owner-can-import-status', status: state.text});
        }
        if (!p.markers.some(event => event.name === 'disc-import-start-visible'))
          mark('disc-import-start-visible', {status: state.text});
      }
      if (/^Disc ready\b/i.test(state.text)) mark('disc-preparation-ready', {status: state.text});
      if (/^Playing\b/i.test(state.text)) mark('disc-playing', {status: state.text});
    }
    if (id === 'pause-game' && !state.disabled && p.cssReadyAt === null) {
      p.cssReadyAt = now();
      mark('css-playable-ui', {status: state.text});
    }
    if (id === 'error-dialog' && state.open) mark('error-dialog-open', {status: getNode('error')?.textContent || ''});
  };
  const watchedIds = new Set(['loading-panel', 'loading-label', 'disc-selection-status',
    'choose-disc', 'pause-game', 'start-game', 'error-dialog', 'status']);
  const observer = new MutationObserver(records => {
    for (const record of records) {
      let element = record.target.nodeType === Node.ELEMENT_NODE ? record.target : record.target.parentElement;
      while (element && !watchedIds.has(element.id)) element = element.parentElement;
      if (element) rememberUi(element.id, record.target);
    }
  });
  observer.observe(document, {subtree: true, childList: true, attributes: true, characterData: true});
  document.addEventListener('change', event => {
    if (event.target?.id === 'disc-file') mark('disc-file-selected');
  }, true);
  window.addEventListener('error', event => p.errors.push({kind: 'error', at: now(),
    message: String(event.message || event.error || 'error'), stack: String(event.error?.stack || '').slice(0, 1000)}));
  window.addEventListener('unhandledrejection', event => p.errors.push({kind: 'unhandledrejection', at: now(),
    message: String(event.reason?.message || event.reason), stack: String(event.reason?.stack || '').slice(0, 1000)}));
  try {
    new PerformanceObserver(list => {
      for (const entry of list.getEntries()) p.longTasks.push({start: entry.startTime, duration: entry.duration});
    }).observe({type: 'longtask', buffered: true});
  } catch (error) { p.hookErrors.push(`longtask observer: ${error.message}`); }
  window.setInterval(() => {
    p.memorySamples.push({at: now(), jsHeapUsedBytes: Number(performance.memory?.usedJSHeapSize) || null,
      wasmHeapBytes: Number(window.Module?.HEAPU8?.byteLength) || null});
  }, 250);

  function beginScheduledCallback(kind, scheduledAt, delayMs, frameTimestamp = null) {
    const actualAt = now();
    let frame = null;
    if (kind === 'raf') {
      if (p.lastFrameTimestamp !== frameTimestamp) {
        if (p.lastFrameTimestamp !== null) {
          p.rawFrameTimestampGaps.push(frameTimestamp - p.lastFrameTimestamp);
          const gap = actualAt - p.lastFrameActualAt;
          p.frameGaps.push(gap);
          p.frameGapEvents.push({startAt: p.lastFrameActualAt, endAt: actualAt,
            timestampDeltaMs: frameTimestamp - p.lastFrameTimestamp, durationMs: gap});
        }
        p.lastFrameTimestamp = frameTimestamp;
        p.lastFrameActualAt = actualAt;
        p.frameSeq++;
      }
      frame = p.frameSeq;
    }
    const entry = {seq: ++p.callbackSeq, kind, frame, frameTimestamp,
      scheduledAt, scheduledDelayMs: delayMs, actualAt,
      scheduleDelay: Math.max(0, actualAt - scheduledAt), callbackEndAt: null,
      nativeFrames: 0, pipelineCount: 0, shaderCount: 0,
      asyncPipelineCount: 0, pipelineActiveMs: 0, shaderActiveMs: 0,
      apiWrapperMs: 0, apiFirstAt: null, apiLastAt: null};
    p.schedulerCounts[kind]++;
    const previous = p.activeCallback;
    p.activeCallback = entry;
    return {entry, previous};
  }
  function endScheduledCallback(scope) {
    const {entry, previous} = scope;
    entry.callbackEndAt = now();
    p.activeCallback = previous;
    if (entry.nativeFrames || entry.pipelineCount || entry.shaderCount || entry.asyncPipelineCount)
      p.callbacks.push(entry);
  }
  const frameCallbacks = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = function(callback) {
    const scheduledAt = now();
    return frameCallbacks(timestamp => {
      const scope = beginScheduledCallback('raf', scheduledAt, null, timestamp);
      try { return callback.call(window, timestamp); }
      finally { endScheduledCallback(scope); }
    });
  };
  const rawSetTimeout = window.setTimeout.bind(window);
  window.setTimeout = function(callback, delay, ...args) {
    if (typeof callback !== 'function') return rawSetTimeout(callback, delay, ...args);
    const scheduledAt = now();
    const delayMs = Number(delay) || 0;
    return rawSetTimeout(function(...callbackArgs) {
      const scope = beginScheduledCallback('timeout', scheduledAt, delayMs);
      try { return Reflect.apply(callback, this, callbackArgs); }
      finally { endScheduledCallback(scope); }
    }, delay, ...args);
  };
  const rawSetInterval = window.setInterval.bind(window);
  window.setInterval = function(callback, delay, ...args) {
    if (typeof callback !== 'function') return rawSetInterval(callback, delay, ...args);
    let nextScheduledAt = now();
    const delayMs = Number(delay) || 0;
    return rawSetInterval(function(...callbackArgs) {
      const scheduledAt = nextScheduledAt;
      const scope = beginScheduledCallback('interval', scheduledAt, delayMs);
      nextScheduledAt += Math.max(1, delayMs);
      try { return Reflect.apply(callback, this, callbackArgs); }
      finally { endScheduledCallback(scope); }
    }, delay, ...args);
  };

  if (!window.GPUDevice?.prototype) p.hookErrors.push('GPUDevice.prototype unavailable at document start');
  else {
    for (const name of ['createRenderPipeline', 'createRenderPipelineAsync', 'createShaderModule']) {
      const original = GPUDevice.prototype[name];
      if (typeof original !== 'function') continue;
      try {
        Object.defineProperty(GPUDevice.prototype, name, {configurable: true, writable: true, value: function(...args) {
          const enteredAt = now();
          const startAt = now();
          let result;
          try { result = Reflect.apply(original, this, args); }
          catch (error) {
          const endAt = now();
          const event = {name, at: startAt, durationMs: endAt - startAt, threw: String(error?.message || error)};
            addApiEvent(event, enteredAt, endAt);
            throw error;
          }
          const endAt = now();
          const apiEvent = {name, at: startAt, durationMs: endAt - startAt};
          addApiEvent(apiEvent, enteredAt, endAt);
          if (name === 'createRenderPipelineAsync' && result && typeof result.then === 'function') {
            const asyncRequest = {at: startAt, submittedAt: endAt, label: String(args[0]?.label || ''),
              completionAt: null, submissionToCompletionMs: null, status: 'pending', error: null};
            p.asyncPipelineRequests.push(asyncRequest);
            p.asyncPipelineInFlight++;
            p.asyncPipelinePeakInFlight = Math.max(p.asyncPipelinePeakInFlight, p.asyncPipelineInFlight);
            result.then(() => {
              asyncRequest.completionAt = now();
              asyncRequest.submissionToCompletionMs = asyncRequest.completionAt - asyncRequest.submittedAt;
              asyncRequest.status = 'success';
              p.asyncPipelineInFlight--;
            }, error => {
              asyncRequest.completionAt = now();
              asyncRequest.submissionToCompletionMs = asyncRequest.completionAt - asyncRequest.submittedAt;
              asyncRequest.status = 'error';
              asyncRequest.error = String(error?.message || error);
              p.asyncPipelineInFlight--;
            });
          }
          return result;
        }});
      } catch (error) { p.hookErrors.push(`${name} wrapper: ${error.message}`); }
    }
  }
  function addApiEvent(event, enteredAt, endAt) {
    const callback = p.activeCallback;
    Object.assign(event, {callbackSeq: callback?.seq ?? null, callbackKind: callback?.kind ?? null,
      frame: callback?.frame ?? null,
      wrapperMs: 0});
    p.api.push(event);
    if (callback) {
      callback.apiFirstAt ??= event.at;
      callback.apiLastAt = event.at + event.durationMs;
      if (event.name === 'createRenderPipeline') { callback.pipelineCount++; callback.pipelineActiveMs += event.durationMs; }
      if (event.name === 'createRenderPipelineAsync') callback.asyncPipelineCount++;
      if (event.name === 'createShaderModule') { callback.shaderCount++; callback.shaderActiveMs += event.durationMs; }
    }
    event.wrapperMs = Math.max(0, now() - endAt);
    if (callback) callback.apiWrapperMs += event.wrapperMs;
  }
  const globalSlots = new Map();
  const wrapGlobal = (name, hook) => {
    if (globalSlots.has(name)) return;
    const desc = Object.getOwnPropertyDescriptor(window, name);
    if (desc && !desc.configurable) { p.hookErrors.push(`${name} is not configurable`); return; }
    let current = desc?.value;
    Object.defineProperty(window, name, {
      configurable: true, enumerable: true,
      get() { return current; },
      set(value) {
        if (typeof value !== 'function') { current = value; return; }
        const original = value;
        current = function(...args) {
          const callback = p.activeCallback;
          if (name === 'menuFrame' && callback) callback.nativeFrames++;
          else if (name === 'menuFrame') p.orphanNativeFrames++;
          if (name === 'menuFrame') installModuleHooks();
          if (hook) return hook.call(this, original, args, callback);
          return Reflect.apply(original, this, args);
        };
        p.wrappedGlobals.push(name);
      },
    });
    if (typeof current === 'function') window[name] = current;
    globalSlots.set(name, true);
  };
  const wrapModuleMethod = (module, name, hook) => {
    const original = module?.[name];
    if (typeof original !== 'function' || original.__graphicsStartupWrapped) return false;
    const wrapped = function(...args) { return hook.call(this, original, args); };
    Object.defineProperty(wrapped, '__graphicsStartupWrapped', {value: true});
    module[name] = wrapped;
    return true;
  };
  function installModuleHooks() {
    if (!p.firstNativeFrameAt) {
      p.firstNativeFrameAt = now();
      mark('native-loop-first-frame');
    }
    if (p.runtimeHooksInstalled) return;
    const module = window.Module;
    if (!module || typeof module._melee_web_native_menu_cache_idle !== 'function') return;
    p.runtimeHooksInstalled = true;
    wrapModuleMethod(module, '_melee_web_native_menu_cache_idle', function(original, args) {
      const value = Reflect.apply(original, this, args);
      if (value !== p.cacheIdle) {
        p.cacheIdleTransitions.push({at: now(), value});
        p.cacheIdle = value;
        if (value === 1) mark('native-cache-idle', {value});
        if (value === -1) mark('native-cache-error', {value});
      }
      return value;
    });
    wrapModuleMethod(module, '_melee_web_native_menu_launch', function(original, args) {
      mark('native-launch-call');
      return Reflect.apply(original, this, args);
    });
    for (const name of ['_melee_web_native_asset_begin', '_melee_web_native_asset_commit',
      '_melee_web_native_asset_abort']) {
      wrapModuleMethod(module, name, function(original, args) {
        mark(name.slice('_melee_web_native_'.length).replaceAll('_', '-'));
        return Reflect.apply(original, this, args);
      });
    }
    for (const name of ['_melee_web_native_asset_file', '_melee_web_native_menu_file']) {
      wrapModuleMethod(module, name, function(original, args) {
        p.assetFileCalls = (p.assetFileCalls || 0) + 1;
        p.assetBytes = (p.assetBytes || 0) + Number(args.at(-1) || 0);
        return Reflect.apply(original, this, args);
      });
    }
  }
  for (const name of ['menuFrame', 'menuAssetsRequested', 'menuPreparation',
    'menuPreparationDone', 'menuPreparationFailed', 'menuPreparationCanceled', 'menuAssetScopeReleased']) {
    wrapGlobal(name, (original, args, callback) => {
      if (name === 'menuAssetsRequested') mark('native-assets-requested', {generation: args[0]});
      if (name === 'menuPreparation') mark('native-preparation-start', {label: String(args[0] || '')});
      if (name === 'menuPreparationDone') mark('native-preparation-done');
      if (name === 'menuPreparationFailed') mark('native-preparation-failed', {message: String(args[0] || '')});
      if (name === 'menuPreparationCanceled') mark('native-preparation-canceled');
      const nativeStartAt = name === 'menuFrame' ? now() : null;
      const result = Reflect.apply(original, window, args);
      if (name === 'menuFrame' && callback) {
        const module = window.Module;
        const prep = module?.pipelinePreparation;
        if (prep && typeof prep === 'object') {
          const ready = prep.ready === true;
          if (p.pipelinePreparation !== ready) {
            p.pipelinePreparation = ready;
            mark(ready ? 'pipeline-preparation-ready' : 'pipeline-preparation-pending', {
              selected: Number(prep.selected) || 0, pending: Number(prep.pending) || 0,
              error_count: Number(prep.error_count) || 0,
            });
          }
        }
      }
      if (name === 'menuFrame') {
        const batch = {nativeFrame: p.nativeBatches.length + 1,
          startAt: nativeStartAt, endAt: now(), callbackSeq: callback?.seq ?? null,
          frame: callback?.frame ?? null, pipelineCount: 0, asyncPipelineCount: 0,
          shaderCount: 0, activeApiMs: 0, wrapperMs: 0};
        for (const event of p.api.slice(p.lastNativeApiIndex)) {
          if (event.callbackKind === 'timeout') continue;
          event.nativeFrame = batch.nativeFrame;
          batch.activeApiMs += event.durationMs;
          batch.wrapperMs += event.wrapperMs || 0;
          if (event.name === 'createRenderPipeline') batch.pipelineCount++;
          if (event.name === 'createRenderPipelineAsync') batch.asyncPipelineCount++;
          if (event.name === 'createShaderModule') batch.shaderCount++;
        }
        p.lastNativeApiIndex = p.api.length;
        p.nativeBatches.push(batch);
      }
      return result;
    });
  }
  p.installModuleHooks = installModuleHooks;
  p.snapshot = () => ({
    startedAt: p.startedAt, frameCount: p.frameSeq, callbackCount: p.callbackSeq,
    callbacks: p.callbacks, api: p.api, markers: p.markers, ui: p.ui,
    longTasks: p.longTasks, frameGaps: p.frameGaps, rawFrameTimestampGaps: p.rawFrameTimestampGaps,
    schedulerCounts: p.schedulerCounts, orphanNativeFrames: p.orphanNativeFrames,
    errors: p.errors, hookErrors: p.hookErrors, nativeBatches: p.nativeBatches,
    frameGapEvents: p.frameGapEvents,
    cacheIdleTransitions: p.cacheIdleTransitions, pipelinePreparation: p.pipelinePreparation,
    asyncPipelineRequests: p.asyncPipelineRequests,
    asyncPipelineInFlight: p.asyncPipelineInFlight, asyncPipelinePeakInFlight: p.asyncPipelinePeakInFlight,
    memorySamples: p.memorySamples,
    asyncTestResults: window.__meleeWebAsyncPipelineTestResults || null,
    graphicsReadyAt: p.graphicsReadyAt, cssReadyAt: p.cssReadyAt,
    firstNativeFrameAt: p.firstNativeFrameAt ?? null,
    assetFileCalls: p.assetFileCalls || 0, assetBytes: p.assetBytes || 0,
    wrappedGlobals: [...new Set(p.wrappedGlobals)], runtimeHooksInstalled: p.runtimeHooksInstalled,
  });
}

function summary(values) {
  const sorted = [...values].filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return {count: 0, min: null, median: null, p95: null, max: null};
  const quantile = q => sorted[Math.min(sorted.length - 1, Math.ceil(q * sorted.length) - 1)];
  return {count: sorted.length, min: sorted[0], median: quantile(0.5), p95: quantile(0.95), max: sorted.at(-1)};
}
function markerAt(probe, name) { return probe.markers.find(event => event.name === name)?.at ?? null; }
function timedEvents(probe) {
  const graphicsReady = probe.graphicsReadyAt;
  const cssReady = probe.cssReadyAt;
  const callsBeforeGraphics = probe.api.filter(event => graphicsReady !== null && event.at <= graphicsReady);
  const callsBeforeCss = probe.api.filter(event => cssReady !== null && event.at <= cssReady);
  const groupCalls = calls => {
    const groups = new Map();
    for (const event of calls) {
      const key = event.nativeFrame === undefined ?
        (event.callbackSeq === null ? 'pre-frame' : `callback-${event.callbackSeq}`) :
        `native-${event.nativeFrame}`;
      let batch = groups.get(key);
      if (!batch) {
        const nativeFrame = event.nativeFrame === undefined ? null : probe.nativeBatches.find(item => item.nativeFrame === event.nativeFrame);
        const callback = probe.callbacks.find(item => item.seq === (nativeFrame?.callbackSeq ?? event.callbackSeq));
        batch = {nativeFrame: event.nativeFrame ?? null,
          kind: nativeFrame ? 'native-frame' : callback?.kind ?? 'pre-frame',
          callbackSeq: nativeFrame?.callbackSeq ?? event.callbackSeq, frame: nativeFrame?.frame ?? event.frame,
          scheduledAt: callback?.scheduledAt ?? null, actualAt: callback?.actualAt ?? null,
          callbackEndAt: callback?.callbackEndAt ?? null,
          callbackGapMs: callback?.actualAt != null && callback?.scheduledAt != null ? callback.actualAt - callback.scheduledAt : null,
          firstAt: event.at, lastAt: event.at + event.durationMs,
          pipelineCount: 0, asyncPipelineCount: 0, shaderCount: 0, apiActiveMs: 0, wrapperMs: 0};
        groups.set(key, batch);
      }
      batch.firstAt = Math.min(batch.firstAt, event.at);
      batch.lastAt = Math.max(batch.lastAt, event.at + event.durationMs);
      batch.apiActiveMs += event.durationMs;
      batch.wrapperMs += event.wrapperMs || 0;
      if (event.name === 'createRenderPipeline') batch.pipelineCount++;
      if (event.name === 'createRenderPipelineAsync') batch.asyncPipelineCount++;
      if (event.name === 'createShaderModule') batch.shaderCount++;
    }
    return [...groups.values()].sort((a, b) => a.firstAt - b.firstAt).map((item, index, all) => ({
      ...item,
      waitSincePreviousMs: index ? Math.max(0, item.firstAt - all[index - 1].lastAt) : 0,
    }));
  };
  const graphicsBatches = groupCalls(callsBeforeGraphics);
  const cssBatches = groupCalls(callsBeforeCss);
  const nativeCallbacks = probe.nativeBatches.filter(item => graphicsReady !== null && item.endAt <= graphicsReady);
  const timerBatches = graphicsBatches.filter(batch => batch.kind === 'timeout');
  const renderedBatches = graphicsBatches.filter(batch => batch.nativeFrame !== null);
  const pipelineCalls = callsBeforeGraphics.filter(event => event.name === 'createRenderPipeline');
  const shaderCalls = callsBeforeGraphics.filter(event => event.name === 'createShaderModule');
  const asyncCalls = callsBeforeGraphics.filter(event => event.name === 'createRenderPipelineAsync');
  const asyncRequests = (probe.asyncPipelineRequests || []).filter(event =>
    graphicsReady === null || event.at <= graphicsReady);
  const asyncCompleted = asyncRequests.filter(event => event.status !== 'pending' &&
    (graphicsReady === null || event.completionAt <= graphicsReady));
  const asyncTestEvents = probe.asyncTestResults?.events || [];
  const successfulAsyncEvents = asyncTestEvents.filter(event => event.event === 1);
  const failedAsyncEvents = asyncTestEvents.filter(event => event.event === 2);
  const stateTransitionMatches = successfulAsyncEvents.every(event =>
    event.pendingBeforeSettlement === true && event.readyBeforeSettlement === false &&
    event.pendingAfterSettlement === false && event.readyAfterSettlement === true) &&
    failedAsyncEvents.every(event => event.pendingBeforeSettlement === true &&
      event.readyBeforeSettlement === false && event.pendingAfterSettlement === false &&
      event.readyAfterSettlement === false);
  const readyLookups = probe.asyncTestResults?.readyLookups || [];
  const lookupChecks = probe.asyncTestResults?.lookupChecks || [];
  const successfulHashLows = new Set(successfulAsyncEvents.map(event => event.hashLow));
  const completedPipelineLookups = readyLookups.filter(lookup => lookup.readyAtLookup === true &&
    lookup.pendingAtLookup === false && successfulHashLows.has(lookup.hashLow));
  const completedPipelineLookupChecks = lookupChecks.filter(check => check.lookupReady === true &&
    check.ready === true && check.pending === false && check.ownerInitialized === true &&
    successfulAsyncEvents.some(event => event.type === check.type && event.hashHigh === 0 &&
      event.hashLow === check.hashLow));
  const submittedIdentity = event => `${event.type}:${event.hashHigh}:${event.hashLow}`;
  const asyncSubmittedIds = asyncTestEvents.filter(event => event.event === 0).map(submittedIdentity);
  const asyncSettledIds = asyncTestEvents.filter(event => event.event === 1 || event.event === 2).map(submittedIdentity);
  const identityCounts = ids => ids.reduce((counts, id) => counts.set(id, (counts.get(id) || 0) + 1), new Map());
  const submittedCounts = identityCounts(asyncSubmittedIds);
  const settledCounts = identityCounts(asyncSettledIds);
  const asyncIdentitySettlementsMatch = submittedCounts.size === settledCounts.size &&
    [...submittedCounts].every(([id, count]) => settledCounts.get(id) === count);
  const firstApi = graphicsBatches[0]?.firstAt ?? null;
  const lastApi = graphicsBatches.at(-1)?.lastAt ?? null;
  return {
    graphicsReadyAtMs: graphicsReady,
    cssPlayableAtMs: cssReady,
    timeToGraphicsReadyMs: graphicsReady,
    timeToPlayableCssMs: cssReady,
    graphicsReadyToCssMs: graphicsReady !== null && cssReady !== null ? cssReady - graphicsReady : null,
    initialLoading: {
      nativeLoopFirstFrameAtMs: probe.firstNativeFrameAt ?? null,
      graphicsPanelFirstVisibleAtMs: markerAt(probe, 'graphics-panel-visible'),
      graphicsReadySource: probe.markers.find(event => event.name === 'graphics-ui-ready')?.source || null,
    },
    preparation: {
      pipelineCount: pipelineCalls.length + asyncCalls.length,
      synchronousPipelineCount: pipelineCalls.length,
      shaderModuleCount: shaderCalls.length,
      asyncPipelineCount: asyncCalls.length,
      asyncPipelineCompletion: {
        requestCount: asyncRequests.length,
        completedBeforeGraphicsReady: asyncCompleted.length,
        pendingAtGraphicsReady: asyncRequests.filter(event => event.status === 'pending' ||
          (graphicsReady !== null && event.completionAt > graphicsReady)).length,
        activeInFlightMax: probe.asyncPipelinePeakInFlight || 0,
        measuredSubmissionToCompletionMs: summary(asyncCompleted.map(event => event.submissionToCompletionMs)),
        submissionToCompletionSamplesMs: asyncCompleted.map(event => event.submissionToCompletionMs),
        statuses: asyncRequests.reduce((counts, event) => {
          counts[event.status] = (counts[event.status] || 0) + 1;
          return counts;
        }, {}),
      },
      asyncOwnership: {
        selected: probe.asyncTestResults?.selected || 0,
        submitted: asyncTestEvents.filter(event => event.event === 0).length,
        completed: asyncTestEvents.filter(event => event.event === 1).length,
        failed: asyncTestEvents.filter(event => event.event === 2).length,
        peakInFlight: probe.asyncTestResults?.peakInFlight || 0,
        pendingPipelineLookupCount: probe.asyncTestResults?.pendingPipelineLookupCount || 0,
        readyLookupCount: readyLookups.length,
        completedPipelineLookupCount: completedPipelineLookups.length,
        lookupCheckCount: lookupChecks.length,
        completedPipelineLookupCheckCount: completedPipelineLookupChecks.length,
        completionStateTransitionsValid: stateTransitionMatches,
        identitiesSettleExactlyOnce: asyncIdentitySettlementsMatch,
        duplicateSubmissionIdentities: submittedCounts.size !== asyncSubmittedIds.length,
      },
      activeCreateRenderPipelineMs: pipelineCalls.reduce((sum, event) => sum + event.durationMs, 0),
      activeAsyncSubmissionMs: asyncCalls.reduce((sum, event) => sum + event.durationMs, 0),
      activeCreateShaderModuleMs: shaderCalls.reduce((sum, event) => sum + event.durationMs, 0),
      activePipelineApiWallMs: [...pipelineCalls, ...shaderCalls, ...asyncCalls].reduce((sum, event) => sum + event.durationMs, 0),
      apiWrapperOverheadMs: [...pipelineCalls, ...shaderCalls, ...asyncCalls].reduce((sum, event) => sum + event.wrapperMs, 0),
      firstApiAtMs: firstApi,
      lastApiAtMs: lastApi,
      pipelineCreateSpanMs: firstApi !== null && lastApi !== null ? lastApi - firstApi : null,
      waitBetweenNonemptyBatchesMs: graphicsBatches.slice(1).reduce((sum, batch) => sum + batch.waitSincePreviousMs, 0),
      batchCount: graphicsBatches.length,
      nativePreparationCallbacks: nativeCallbacks.length,
      renderedBatchCount: renderedBatches.length,
      timerBatchCount: timerBatches.length,
      pipelineCallsPerBatch: summary(graphicsBatches.map(batch => batch.pipelineCount)),
      pipelineCallsPerTimerBatch: summary(timerBatches.map(batch => batch.pipelineCount)),
      shaderCallsPerBatch: summary(graphicsBatches.map(batch => batch.shaderCount)),
      scheduledToActualMs: summary(graphicsBatches.filter(batch => batch.callbackGapMs !== null).map(batch => batch.callbackGapMs)),
      timerScheduledToActualMs: summary(timerBatches.filter(batch => batch.callbackGapMs !== null).map(batch => batch.callbackGapMs)),
      batchSchedule: graphicsBatches,
      batchScheduleToGraphicsReadyRemainderMs: graphicsReady !== null && lastApi !== null ? graphicsReady - lastApi : null,
      throughCss: {
        pipelineCount: callsBeforeCss.filter(event => event.name === 'createRenderPipeline' ||
          event.name === 'createRenderPipelineAsync').length,
        shaderModuleCount: callsBeforeCss.filter(event => event.name === 'createShaderModule').length,
        asyncPipelineCount: callsBeforeCss.filter(event => event.name === 'createRenderPipelineAsync').length,
        batchCount: cssBatches.length,
      },
    },
    responsiveness: {
      animationCallbackGapMs: summary(probe.frameGapEvents.filter(item => graphicsReady === null || item.endAt <= graphicsReady).map(item => item.durationMs)),
      gapsOver16_67ms: probe.frameGapEvents.filter(item => (graphicsReady === null || item.endAt <= graphicsReady) && item.durationMs > 1000 / 60).length,
      gapsOver35ms: probe.frameGapEvents.filter(item => (graphicsReady === null || item.endAt <= graphicsReady) && item.durationMs > 35).length,
      longTaskMs: summary(probe.longTasks.filter(item => graphicsReady === null || item.start <= graphicsReady).map(item => item.duration)),
      longTasksDuringGraphics: probe.longTasks.filter(item => graphicsReady === null || item.start <= graphicsReady),
      longTasksAfterGraphics: graphicsReady === null ? [] : probe.longTasks.filter(item => item.start > graphicsReady),
    },
    startupStages: {
      discSelectedAtMs: markerAt(probe, 'disc-file-selected'),
      discValidationStartedAtMs: markerAt(probe, 'disc-validation-start'),
    discValidationReadyAtMs: markerAt(probe, 'disc-validation-ready'),
      importVisibleAtMs: markerAt(probe, 'disc-import-start-visible'),
      nativeAssetRequestAtMs: markerAt(probe, 'native-assets-requested'),
      assetBeginAtMs: markerAt(probe, 'asset-begin'),
      assetCommitAtMs: markerAt(probe, 'asset-commit'),
      nativePreparationStartAtMs: markerAt(probe, 'native-preparation-start'),
      nativePreparationDoneAtMs: markerAt(probe, 'native-preparation-done'),
      nativeLaunchAtMs: markerAt(probe, 'native-launch-call'),
      nativeCacheIdleAtMs: markerAt(probe, 'native-cache-idle'),
      pipelinePreparationReadyAtMs: markerAt(probe, 'pipeline-preparation-ready'),
      assetFileCalls: probe.assetFileCalls,
      assetBytes: probe.assetBytes,
    },
  };
}

async function runAttempt(browser, context, index, warmup = false) {
  const page = await context.newPage();
  const errors = [];
  const failures = [];
  page.on('pageerror', error => errors.push({kind: 'pageerror', message: error.message,
    stack: String(error.stack || '').slice(0, 1000)}));
  page.on('console', message => { if (message.type() === 'error') errors.push({kind: 'console', message: message.text()}); });
  page.on('requestfailed', request => failures.push({url: request.url(), error: request.failure()?.errorText || ''}));
  if (values['async-mode']) {
    await page.addInitScript(({mode, maxInFlight, failFirst, retireOwnerOnFirstCompletion}) => {
      window.__meleeWebAsyncPipelineTestPolicy = {mode, maxInFlight, failFirst, retireOwnerOnFirstCompletion};
      if (retireOwnerOnFirstCompletion) window.__meleeWebAsyncPipelineTestRetireArmed = false;
    }, {mode: values['async-mode'], maxInFlight: Number(values['async-inflight']),
      failFirst: values['async-fail-first'], retireOwnerOnFirstCompletion: values['async-test-retire-owner']});
  }
  await page.addInitScript(probeInstall);
  const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: startupTimeoutMs});
  const startedAt = Date.now();
  let attempt = {index, warmup, status: 'running', screenshot: null};
  try {
    const response = await page.goto(values.url, {waitUntil: 'load'});
    assert.equal(response.status(), 200);
    const headers = response.headers();
    assert.equal(headers['cross-origin-opener-policy'], 'same-origin');
    assert.equal(headers['cross-origin-embedder-policy'], 'require-corp');
    assert.match(headers['content-security-policy'] || '', /'wasm-unsafe-eval'/);
    if (values['expect-async-failure']) {
      await page.waitForFunction(() => document.querySelector('#error-dialog')?.open === true,
        null, {timeout: 60000});
      const failurePage = await page.evaluate(() => ({
        error: document.querySelector('#error')?.textContent || '',
        launchCalls: window.__graphicsStartupProbe.markers.filter(event => event.name === 'native-launch-call').length,
        asyncTestResults: window.__meleeWebAsyncPipelineTestResults || null,
        probe: window.__graphicsStartupProbe.snapshot(),
      }));
      attempt.browser = browser.version();
      attempt.expectedFailure = failurePage;
      const events = failurePage.asyncTestResults?.events || [];
      assert.match(failurePage.error, /pipeline preparation failed/i,
        'Async pipeline failure was not visible to the startup owner');
      assert.equal(failurePage.launchCalls, 0, 'Native launch occurred after required pipeline failure');
      assert.equal(events.filter(event => event.event === 0).length, 1, 'Expected one actual async submission');
      assert.equal(events.filter(event => event.event === 2).length, 1, 'Expected one visible WebGPU rejection');
      assert.equal(events.filter(event => event.event === 1).length, 0, 'Rejected pipeline was counted as ready');
      assert(events.find(event => event.event === 2)?.error, 'WebGPU failure message was lost');
      attempt.status = 'pass';
      attempt.wallDurationMs = Date.now() - startedAt;
      attempt.screenshot = path.join(values.out, `attempt-${index}-expected-failure.png`);
      await page.screenshot({path: attempt.screenshot, fullPage: true});
      await page.close();
      return attempt;
    }
    await page.locator('#loading-panel').waitFor({state: 'visible', timeout: 30000});
    await page.waitForFunction(() => {
      const panel = document.querySelector('#loading-panel');
      const label = document.querySelector('#loading-label')?.textContent || '';
      const choose = document.querySelector('#choose-disc');
      return panel && !panel.hidden && /Preparing graphics/i.test(label) && choose && !choose.disabled;
    }, null, {timeout: startupTimeoutMs});
    const earlyBoundary = await page.evaluate(() => ({
      at: performance.now(), label: document.querySelector('#loading-label')?.textContent || '',
      hidden: document.querySelector('#loading-panel')?.hidden ?? null,
      filenameStatus: document.querySelector('#disc-selection-status')?.textContent || '',
    }));
    assert.equal(earlyBoundary.hidden, false, 'Disc selection must begin while the graphics panel is visible');
    await page.evaluate(() => window.__graphicsStartupProbe.installModuleHooks());
    attempt.earlyBoundary = earlyBoundary;
    if (values['boundary-only']) {
      await page.waitForFunction(() => document.querySelector('#loading-panel')?.hidden === true,
        null, {timeout: startupTimeoutMs});
    } else {
      await driver.selectDisc(discPath);
      await page.waitForFunction(() => /\.ciso\b/i.test(document.querySelector('#disc-selection-status')?.textContent || ''),
        null, {timeout: 30000});
      if (values['async-test-retire-owner']) {
        await page.evaluate(() => { window.__meleeWebAsyncPipelineTestRetireArmed = true; });
      }
      await page.waitForFunction(() => {
        const status = document.querySelector('#disc-selection-status')?.textContent || '';
        return /Disc validated; waiting for graphics|Disc validated; preparing|Preparing local data for/i.test(status) ||
          document.querySelector('#error-dialog[open]');
      }, null, {timeout: 60000});
      const validatedStatus = await page.locator('#disc-selection-status').innerText();
      assert.match(validatedStatus, /Disc validated|Preparing local data/,
        `Owned disc validation failed: ${validatedStatus}`);
      attempt.discValidationStatusAtCompletion = validatedStatus;
      if (values['async-test-retire-owner']) {
        await page.waitForFunction(() => Boolean(
          window.__meleeWebAsyncPipelineTestResults?.retiredOwnerCheck), null, {timeout: 60000});
        const retirementPage = await page.evaluate(() => ({
          asyncTestResults: window.__meleeWebAsyncPipelineTestResults || null,
          probe: window.__graphicsStartupProbe.snapshot(),
          launchCalls: window.__graphicsStartupProbe.markers.filter(event => event.name === 'native-launch-call').length,
        }));
        const result = retirementPage.asyncTestResults;
        const check = result?.retiredOwnerCheck;
        const trigger = result?.retirementTriggered;
        assert.equal(result?.events?.filter(event => event.event === 0).length, 1,
          'Retirement must follow one actual game-pipeline submission');
        assert.equal(result?.events?.filter(event => event.event === 1).length, 0,
          'A retired pipeline completion was counted ready');
        assert.equal(trigger?.actualWebGPUCompletion, true,
          'Retirement did not retain a successfully completed real WebGPU pipeline');
        assert.equal(check.pipelineType, trigger.type, 'Retired completion changed shader type identity');
        assert.equal(check.pipelineHashLow, trigger.hashLow, 'Retired completion changed pipeline identity');
        assert.equal(check.oldGeneration, trigger.generation, 'Retirement changed the original cache generation');
        assert(check.oldGeneration < check.newGeneration, 'Cache owner was not reinitialized before completion publication');
        assert.equal(check.pendingAtCompletion, true, 'The real pipeline was not pending at its completion boundary');
        assert.equal(check.readyAtCompletion, false, 'A pending pipeline was already present in the ready map');
        assert.equal(check.beforeReadyIdentity, check.afterReadyIdentity,
          'The retired completion changed the exact ready-map state in the replacement owner');
        assert.equal(check.beforePendingIdentity, check.afterPendingIdentity,
          'The retired completion changed the exact pending-map state in the replacement owner');
        assert.equal(check.settled, true, 'The retired completion did not settle');
        assert.equal(check.initialized, true, 'The replacement cache owner did not remain initialized');
        assert.equal(check.unchanged, true, 'The retired completion changed the replacement cache owner');
        assert.equal(retirementPage.launchCalls, 0, 'The deliberately retired startup owner launched gameplay');
        attempt.browser = browser.version();
        attempt.page = retirementPage;
        attempt.metrics = timedEvents(retirementPage.probe);
        attempt.asyncRetirement = check;
        attempt.errors = [...errors, ...failures.map(failure => ({kind: 'requestfailed', ...failure}))];
        assert.equal(attempt.errors.length, 0, 'Browser emitted an error during deferred cache retirement');
        attempt.wallDurationMs = Date.now() - startedAt;
        attempt.status = 'pass';
        attempt.screenshot = path.join(values.out, `attempt-${index}-retired-owner.png`);
        await page.screenshot({path: attempt.screenshot, fullPage: true});
        await page.close();
        return attempt;
      }
      await driver.waitForPublicCss();
    }
    if (values['async-mode'] && !values['async-test-retire-owner']) {
      const lookupCheck = await page.evaluate(() => {
        const results = window.__meleeWebAsyncPipelineTestResults;
        const completed = results?.events?.find(event => event.event === 1);
        const lookup = Module._melee_web_async_pipeline_test_lookup_completed;
        if (!completed || typeof lookup !== 'function') return null;
        return {type: completed.type, hashLow: completed.hashLow, result: lookup(completed.type, completed.hashLow)};
      });
      assert.equal(lookupCheck?.result, 1,
        'Aurora did not retrieve the exact completed pipeline as ready and not pending');
    }
    const pageMetrics = await page.evaluate(() => {
      const probe = window.__graphicsStartupProbe.snapshot();
      const resources = performance.getEntriesByType('resource').map(entry => ({
        path: new URL(entry.name).pathname,
        startTime: entry.startTime, durationMs: entry.duration,
        responseEnd: entry.responseEnd, transferSize: entry.transferSize,
        encodedBodySize: entry.encodedBodySize, decodedBodySize: entry.decodedBodySize,
      }));
      const module = globalThis.Module;
      let phase = null, running = null;
      try { phase = module._melee_web_native_menu_phase(); running = module._melee_web_native_menu_running(); } catch {}
      const memorySamples = probe.memorySamples;
      const maxMetric = key => Math.max(0, ...memorySamples.map(sample => Number(sample[key]) || 0));
      return {probe, resources, memory: {samples: memorySamples.length,
        peakJsHeapUsedBytes: maxMetric('jsHeapUsedBytes') || null,
        peakWasmHeapBytes: maxMetric('wasmHeapBytes') || null,
        gpuMemoryObservable: false}, asyncTestResults: window.__meleeWebAsyncPipelineTestResults || null,
        final: {phase, running,
        loadingHidden: document.querySelector('#loading-panel')?.hidden ?? null,
        discStatus: document.querySelector('#disc-selection-status')?.textContent || '',
        launchCalls: probe.markers.filter(event => event.name === 'native-launch-call').length}};
    });
    attempt.browser = browser.version();
    attempt.page = pageMetrics;
    attempt.metrics = timedEvents(pageMetrics.probe);
    attempt.errors = [...errors, ...failures.map(failure => ({kind: 'requestfailed', ...failure}))];
    attempt.wallDurationMs = Date.now() - startedAt;
    if (!values['boundary-only']) {
      assert.equal(pageMetrics.final.phase, 1, 'Original CSS was not entered');
      assert.equal(pageMetrics.final.running, 1, 'CSS is not playable');
      assert.equal(pageMetrics.final.launchCalls, 1, 'Automatic launch did not invoke CSS exactly once');
    }
    if (values['async-mode'] && !values['async-fail-first']) {
      const asyncPrep = attempt.metrics.preparation;
      assert.equal(asyncPrep.asyncPipelineCompletion.pendingAtGraphicsReady, 0,
        'Graphics readiness opened before every async WebGPU promise completed');
      if (values['async-test-retire-owner']) {
        assert.fail('Retirement test should have returned at its dedicated owner-lifecycle boundary');
      } else {
        assert.equal(asyncPrep.asyncOwnership.identitiesSettleExactlyOnce, true,
          'An async submission did not settle exactly once to its original cache identity');
        assert.equal(asyncPrep.asyncOwnership.duplicateSubmissionIdentities, false,
          'The same exact cache identity was submitted more than once');
        assert(asyncPrep.asyncOwnership.peakInFlight <= Number(values['async-inflight']),
          'The native async creation cap was exceeded');
        assert.equal(asyncPrep.asyncOwnership.pendingPipelineLookupCount, 0,
          'Renderer requested a pending pipeline before readiness; drawing would have been skipped');
        assert.equal(asyncPrep.asyncOwnership.completionStateTransitionsValid, true,
          'Async completion did not atomically transition pending to ready (or clear failure state)');
        assert(asyncPrep.asyncOwnership.completedPipelineLookupCount > 0,
          'No completed async pipeline was subsequently looked up through Aurora');
        assert(asyncPrep.asyncOwnership.completedPipelineLookupCheckCount > 0,
          'The actual Aurora lookup did not report the completed cache identity as ready and not pending');
        assert.equal(asyncPrep.asyncOwnership.submitted,
          asyncPrep.asyncOwnership.completed + asyncPrep.asyncOwnership.failed,
          'A native async submission did not settle before the graphics barrier');
        assert(asyncPrep.asyncOwnership.selected >= 1,
          'The test-only policy did not select any actual game pipeline descriptor');
        assert.equal(asyncPrep.asyncOwnership.failed, 0, 'Unexpected async pipeline failure');
      }
    }
    if (values['async-test-stale-completion']) {
      assert(values['async-mode'], '--async-test-stale-completion requires --async-mode');
      const staleCheck = await page.evaluate(() => {
        const check = Module._melee_web_async_pipeline_test_reject_stale_generation;
        return typeof check === 'function' ? check() : null;
      });
      assert.equal(staleCheck, 1,
        'A completion from a retired pipeline-cache generation changed the active owner');
      attempt.metrics.preparation.asyncOwnership.staleRetiredGenerationRejected = true;
    }
    assert.equal(attempt.errors.length, 0, 'Browser emitted startup errors');
    assert(attempt.metrics.graphicsReadyAtMs !== null, 'No separate full graphics-ready UI boundary was observed');
    if (!values['boundary-only']) {
      const selected = markerAt(pageMetrics.probe, 'disc-file-selected');
      assert(selected !== null && selected < attempt.metrics.graphicsReadyAtMs,
        'The real disc was not selected before full graphics readiness');
      assert(attempt.metrics.cssPlayableAtMs !== null && attempt.metrics.cssPlayableAtMs >= attempt.metrics.graphicsReadyAtMs,
        'CSS did not become playable after graphics readiness');
    }
    attempt.status = 'pass';
    if (!warmup) {
      attempt.screenshot = path.join(values.out, `attempt-${index}-css.png`);
      await page.screenshot({path: attempt.screenshot, fullPage: true});
    }
    await page.close();
    return attempt;
  } catch (error) {
    attempt.status = 'fail';
    attempt.failure = {message: error.message, stack: error.stack};
    attempt.errors = errors;
    attempt.driverDiagnostics = error.diagnostics || null;
    attempt.pageSnapshot = await page.evaluate(() => ({
      title: document.title,
      status: document.querySelector('#status')?.textContent || '',
      statusTitle: document.querySelector('#status')?.title || '',
      loadingLabel: document.querySelector('#loading-label')?.textContent || '',
      loadingHidden: document.querySelector('#loading-panel')?.hidden ?? null,
      discStatus: document.querySelector('#disc-selection-status')?.textContent || '',
      error: document.querySelector('#error')?.textContent || '',
      probe: window.__graphicsStartupProbe?.snapshot?.() || null,
    })).catch(() => null);
    attempt.wallDurationMs = Date.now() - startedAt;
    try { attempt.screenshot = path.join(values.out, `attempt-${index}-failure.png`); await page.screenshot({path: attempt.screenshot, fullPage: true}); } catch {}
    await page.close().catch(() => {});
    return attempt;
  }
}

const isWarm = values.condition === 'warm';
if (isWarm) {
  const browser = await chromium.launch(browserLaunchOptions(launchOptions));
  try {
    const context = await browser.newContext({viewport: {width: 1280, height: 960}});
    const warmup = await runAttempt(browser, context, 'warmup', true);
    report.warmup = warmup;
    if (warmup.status !== 'pass') {
      report.failures.push({attempt: 'warmup', failure: warmup.failure});
    } else {
      for (let index = 1; index <= runCount; index++) {
        const attempt = await runAttempt(browser, context, index);
        report.attempts.push(attempt);
        if (attempt.status !== 'pass') report.failures.push({attempt: index, failure: attempt.failure});
        await fs.writeFile(path.join(values.out, `attempt-${index}.json`), JSON.stringify(attempt, null, 2));
      }
    }
    await context.close();
  } finally { await browser.close(); }
} else {
  for (let index = 1; index <= runCount; index++) {
    const browser = await chromium.launch(browserLaunchOptions(launchOptions));
    try {
      const context = await browser.newContext({viewport: {width: 1280, height: 960}});
      const attempt = await runAttempt(browser, context, index);
      report.attempts.push(attempt);
      if (attempt.status !== 'pass') report.failures.push({attempt: index, failure: attempt.failure});
      await fs.writeFile(path.join(values.out, `attempt-${index}.json`), JSON.stringify(attempt, null, 2));
      await context.close();
    } finally { await browser.close(); }
  }
}
const reduce = field => summary(report.attempts.map(attempt => attempt.metrics?.[field]).filter(Number.isFinite));
report.summary = {
  timeToGraphicsReadyMs: reduce('timeToGraphicsReadyMs'),
  timeToPlayableCssMs: reduce('timeToPlayableCssMs'),
  graphicsReadyToCssMs: reduce('graphicsReadyToCssMs'),
  pipelineBatchCount: summary(report.attempts.map(attempt => attempt.metrics?.preparation.batchCount).filter(Number.isFinite)),
  pipelineCount: summary(report.attempts.map(attempt => attempt.metrics?.preparation.pipelineCount).filter(Number.isFinite)),
  shaderModuleCount: summary(report.attempts.map(attempt => attempt.metrics?.preparation.shaderModuleCount).filter(Number.isFinite)),
  waitBetweenNonemptyBatchesMs: summary(report.attempts.map(attempt => attempt.metrics?.preparation.waitBetweenNonemptyBatchesMs).filter(Number.isFinite)),
  activePipelineApiWallMs: summary(report.attempts.map(attempt => attempt.metrics?.preparation.activePipelineApiWallMs).filter(Number.isFinite)),
  apiWrapperOverheadMs: summary(report.attempts.map(attempt => attempt.metrics?.preparation.apiWrapperOverheadMs).filter(Number.isFinite)),
  pipelineCreateSpanMs: summary(report.attempts.map(attempt => attempt.metrics?.preparation.pipelineCreateSpanMs).filter(Number.isFinite)),
  animationGapMaxMs: summary(report.attempts.map(attempt => attempt.metrics?.responsiveness.animationCallbackGapMs.max).filter(Number.isFinite)),
  longTaskCount: summary(report.attempts.map(attempt => attempt.metrics?.responsiveness.longTaskMs.count).filter(Number.isFinite)),
  asyncPipelineCompletionMs: summary(report.attempts.flatMap(attempt =>
    attempt.metrics?.preparation.asyncPipelineCompletion.submissionToCompletionSamplesMs || [])),
  asyncPeakInFlight: summary(report.attempts.map(attempt => attempt.metrics?.preparation.asyncOwnership.peakInFlight).filter(Number.isFinite)),
  peakJsHeapUsedBytes: summary(report.attempts.map(attempt => attempt.page?.memory?.peakJsHeapUsedBytes).filter(Number.isFinite)),
  peakWasmHeapBytes: summary(report.attempts.map(attempt => attempt.page?.memory?.peakWasmHeapBytes).filter(Number.isFinite)),
};
report.result = report.failures.length === 0 && report.attempts.length === runCount ? 'pass' : 'fail';
await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({result: report.result, condition: report.condition, browser: report.attempts[0]?.browser || report.warmup?.browser,
  build_identity: report.build_identity, summary: report.summary, failures: report.failures}, null, 2));
if (report.result !== 'pass') process.exitCode = 1;
