/**
 * One headless Chrome process that runs a development networked-session
 * instance (Track A1). Transport-free: the caller pushes agreed PAD frames and
 * drains per-tick checksum records through the _melee_web_net_* exports. It
 * reuses the shared browser tools and driver, adds no source input except the
 * agreed frames, and never resumes anything except an instrumented timing pause
 * (recorded in `timingResumes`).
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import {browserLaunchOptions} from './browser_tools.mjs';
import {createBrowserDriver} from './browser_driver.mjs';
import {attachWasmResponseIdentityObserver} from './net_lockstep_observers.mjs';

export const NET_FRAME_BYTES = 44;
export const NET_RECORD_BYTES = 64;

export function firstFatalBrowserError(errors) {
  return errors.find(error => error.kind !== 'requestfailed') ?? null;
}

// Installed once per page. Every call reads Module.HEAPU8 fresh because the
// heap can grow between callbacks.
const PAGE_HELPERS = () => {
  const toBase64 = bytes => {
    let text = '';
    for (let i = 0; i < bytes.length; i += 0x8000)
      text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(text);
  };
  const scratch = {ptr: 0, bytes: 0};
  const buffer = bytes => {
    if (scratch.bytes < bytes) {
      if (scratch.ptr) Module._free(scratch.ptr);
      scratch.ptr = Module._malloc(bytes);
      if (!scratch.ptr) throw Error('Networked scratch allocation failed');
      scratch.bytes = bytes;
    }
    return scratch.ptr;
  };
  window.__net = {
    push(base64) {
      const text = atob(base64);
      const ptr = buffer(text.length);
      const heap = Module.HEAPU8;
      for (let i = 0; i < text.length; ++i) heap[ptr + i] = text.charCodeAt(i);
      return Module._melee_web_net_push(ptr, text.length / 44);
    },
    pushIndexed(firstTick, base64) {
      const text = atob(base64);
      const ptr = buffer(text.length);
      const heap = Module.HEAPU8;
      for (let i = 0; i < text.length; ++i) heap[ptr + i] = text.charCodeAt(i);
      return Module._melee_web_net_push_indexed(firstTick >>> 0, ptr, text.length / 44);
    },
    confirmStart() { return Module._melee_web_net_confirm_start(); },
    terminate(kind, tick, channel) {
      Module._melee_web_net_terminate(kind >>> 0, tick >>> 0, channel >>> 0);
    },
    drain(max) {
      const ptr = buffer(max * 64);
      const count = Module._melee_web_net_checksum_drain(ptr, max);
      return {count, data: count ? toBase64(Module.HEAPU8.subarray(ptr, ptr + count * 64)) : ''};
    },
    status() { return JSON.parse(Module.UTF8ToString(Module._melee_web_net_status())); },
    native() {
      return {
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
        error: document.querySelector('#status')?.dataset.runtimeError || null,
        status: document.querySelector('#status')?.textContent?.slice(-300) || null,
      };
    },
    observe() {
      const out = {css: null, sss: null};
      if (Module._melee_web_native_menu_phase() === 1) {
        const ids = Module._malloc(56), geometry = Module._malloc(32);
        try {
          out.css = [];
          for (let port = 0; port < 4; ++port) {
            if (!Module._melee_web_css_observe_port(port, 8, ids, geometry)) { out.css.push(null); continue; }
            out.css.push({ids: Array.from(Module.HEAP32.subarray(ids >> 2, (ids >> 2) + 14)),
              geometry: Array.from(Module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))});
          }
        } finally { Module._free(ids); Module._free(geometry); }
        out.setup = window.menuObserveCssSetup?.() ?? null;
      }
      if (Module._melee_web_native_menu_match_observe) {
        const match = Module.UTF8ToString(Module._melee_web_native_menu_match_observe());
        out.match = JSON.parse(match);
      }
      return out;
    },
    resumeTimingPause() {
      if (typeof window.meleeNetCanResumeTimingPause !== 'function' ||
          !window.meleeNetCanResumeTimingPause()) return false;
      return window.meleeNetResumeTimingPause();
    },
  };
};

export async function openNetInstance({chromium, launchOptions, url, disc, userDataDir, label,
  throttle = 1, arenaFill = -1, timeoutMs = 120000, deadline = Infinity}) {
  await fs.mkdir(path.resolve(userDataDir), {recursive: true});
  const context = await chromium.launchPersistentContext(path.resolve(userDataDir), {
    ...browserLaunchOptions(launchOptions, {timeout: timeoutMs}),
    viewport: {width: 900, height: 700}, deviceScaleFactor: 1,
  });
  let page, driver, instance, wasmResponses, closed = false, closeComplete = false;
  const close = async () => {
    if (closed) return closeComplete;
    closed = true;
    try { driver?.dispose(); } catch {}
    try { await wasmResponses?.detach(); } catch {}
    let browser;
    try { browser = context.browser(); } catch {}
    try { await context.close(); closeComplete = true; }
    catch { try { if (browser) { await browser.close(); closeComplete = true; } } catch {} }
    if (instance) instance.closed = closeComplete;
    return closeComplete;
  };
  const remaining = () => {
    const ms = Math.min(timeoutMs, deadline - Date.now());
    if (ms <= 0) throw Error('Network determinism wall-time bound exhausted');
    return Math.max(1, Math.floor(ms));
  };
  const bounded = async operation => {
    const ms = remaining();
    if (page) {
      page.setDefaultTimeout(ms);
      page.setDefaultNavigationTimeout(ms);
    }
    let timer;
    try {
      return await Promise.race([
        Promise.resolve().then(operation),
        new Promise((_, reject) => { timer = setTimeout(() => reject(Error('Network determinism wall-time bound exhausted')), ms); }),
      ]);
    } finally { clearTimeout(timer); }
  };

  try {
    page = context.pages()[0] || await bounded(() => context.newPage());
    const errors = [];
    const noteError = error => { if (errors.length < 32) errors.push(error); };
    const wasmCdp = await bounded(() => context.newCDPSession(page));
    wasmResponses = await bounded(() => attachWasmResponseIdentityObserver(wasmCdp, {
      expectedUrl: new URL('gameplay_menu_browser.wasm', url).href,
    }));
    page.on('pageerror', error => noteError({kind: 'pageerror', message: String(error.stack || error.message)}));
    page.on('console', message => { if (message.type() === 'error') noteError({kind: 'console', message: message.text()}); });
    page.on('response', response => {
      if (response.status() >= 400) noteError({kind: 'http', status: response.status(), url: response.url()});
    });
    page.on('requestfailed', request => noteError({kind: 'requestfailed', method: request.method(),
      url: request.url(), failure: request.failure()?.errorText || null}));
    page.on('request', request => { if (request.method() !== 'GET') noteError({kind: 'unexpected-request', method: request.method(), url: request.url()}); });
    await bounded(() => page.addInitScript(() => {
      window.__meleeNativeRuntimeReady = false;
      const module = globalThis.Module || {};
      module.onRuntimeInitialized = () => { window.__meleeNativeRuntimeReady = true; };
      globalThis.Module = module;
    }));
    instance = {label, page, context, errors, timingResumes: [], throttle, arenaFill, closed: false, close};
    driver = createBrowserDriver(page, {surface: 'development', timeoutMs, deadline});
    instance.driver = driver;
    instance.freezeLoadedWasmIdentity = () => bounded(() => wasmResponses.freeze());
    const response = await bounded(() => page.goto(url, {waitUntil: 'domcontentloaded'}));
    if (response?.status() !== 200) throw Error(`runtime.html returned HTTP ${response?.status()}`);
    const headers = response.headers();
    if (headers['cross-origin-opener-policy'] !== 'same-origin' || headers['cross-origin-embedder-policy'] !== 'require-corp')
      throw Error('Runtime did not load over COOP/COEP HTTP isolation');
    if (!await bounded(() => page.evaluate(() => crossOriginIsolated))) throw Error('Browser page is not cross-origin isolated');
    await driver.waitForImport();
    await bounded(() => page.evaluate(PAGE_HELPERS));
    if (throttle !== 1) {
      instance.cdp = await bounded(() => context.newCDPSession(page));
      await bounded(() => instance.cdp.send('Emulation.setCPUThrottlingRate', {rate: throttle}));
    }
    // The arena pattern is a pre-session diagnostic; it must precede disc import.
    if (arenaFill >= 0) {
      const accepted = await bounded(() => page.evaluate(pattern => Module._melee_web_net_arena_fill(pattern), arenaFill));
      if (!accepted) throw Error('The session arena pattern was rejected');
    }
    instance.userAgent = await bounded(() => page.evaluate(() => navigator.userAgent));
    instance.browserVersion = context.browser()?.version() ?? null;
    instance.importDisc = async () => { await driver.selectDisc(disc); await driver.waitForStart(); };
    instance.begin = (seed, maxFrames) => bounded(() => page.evaluate(([s, m]) => window.meleeNetBegin(s, m), [seed >>> 0, maxFrames]));
    instance.beginLockstep = (seed, maxFrames) => bounded(() => page.evaluate(([s, m]) => window.meleeNetBeginLockstep(s, m), [seed >>> 0, maxFrames]));
    instance.peerIdentity = () => bounded(() => page.evaluate(() => window.meleeNetPeerIdentity()));
    instance.push = async frames => {
      if (frames.length % NET_FRAME_BYTES) throw Error('Networked frames must be 44-byte multiples');
      const ok = await bounded(() => page.evaluate(base64 => window.__net.push(base64), Buffer.from(frames).toString('base64')));
      if (!ok) throw Error('The native queue rejected an agreed frame chunk');
    };
    instance.pushIndexed = async (firstTick, frames) => {
      if (frames.length % NET_FRAME_BYTES) throw Error('Lockstep frames must be 44-byte multiples');
      const ok = await bounded(() => page.evaluate(([tick, base64]) => window.__net.pushIndexed(tick, base64),
        [firstTick, Buffer.from(frames).toString('base64')]));
      if (!ok) throw Error(`The native queue rejected indexed frames beginning at ${firstTick}`);
    };
    instance.confirmStart = () => bounded(() => page.evaluate(() => window.__net.confirmStart()));
    instance.terminate = (kind, tick, channel = 0) => bounded(() => page.evaluate(([k, t, c]) => {
      window.__net.terminate(k, t, c); return window.__net.status();
    }, [kind, tick, channel]));
    instance.drain = async (max = 1024) => {
      const result = await bounded(() => page.evaluate(count => window.__net.drain(count), max));
      return {count: result.count, bytes: Buffer.from(result.data, 'base64')};
    };
    instance.status = () => bounded(() => page.evaluate(() => window.__net.status()));
    instance.native = () => bounded(() => page.evaluate(() => window.__net.native()));
    instance.observe = () => bounded(() => page.evaluate(() => window.__net.observe()));
    instance.graphics = () => bounded(() => page.evaluate(async () => {
      const canvas = document.querySelector('canvas');
      const adapter = globalThis.navigator.gpu ? await navigator.gpu.requestAdapter().catch(() => null) : null;
      let webgl = null;
      const context = canvas?.getContext('webgl2') || canvas?.getContext('webgl');
      if (context) {
        const debug = context.getExtension('WEBGL_debug_renderer_info');
        webgl = {
          version: context.getParameter(context.VERSION),
          renderer: context.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : context.RENDERER),
        };
      }
      return {cross_origin_isolated: crossOriginIsolated, webgpu_api: !!navigator.gpu,
        webgpu_adapter: !!adapter,
        canvas: canvas ? {width: canvas.width, height: canvas.height} : null, webgl};
    }));
    instance.screenshot = file => bounded(() => page.screenshot({path: file, fullPage: false}));
    instance.maybeResume = async cursor => {
      const resumed = await bounded(() => page.evaluate(() => window.__net.resumeTimingPause()));
      if (resumed) instance.timingResumes.push({cursor, at_ms: Date.now()});
      return resumed;
    };
    instance.timingPauseDiagnostics = () => bounded(() => page.evaluate(() =>
      window.meleeNetTimingPauseDiagnostics?.() ?? null));
    instance.unload = () => driver.unload();
    return instance;
  } catch (error) {
    if (error && typeof error === 'object') {
      error.browserErrors = instance?.errors ? [...instance.errors] : [];
      error.startupDiagnostics = error.diagnostics ?? null;
    }
    const browserClosed = await close();
    if (error && typeof error === 'object') error.browserClosed = browserClosed;
    throw error;
  }
}
