#!/usr/bin/env node
/**
 * Hosted, authorized-disc audio preview check. This is browser/lifecycle
 * evidence only: it does not replay a long match or make a timing claim.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({
  options: {
    ...Object.fromEntries(['url', 'playwright', 'disc', 'out', 'manifest'].map(name => [name, {type: 'string'}])),
    headed: {type: 'boolean', default: false},
    'select-after-graphics': {type: 'boolean', default: false},
  },
});
if (!values.url || !values.disc || !values.out) {
  throw Error('Use --url ORIGIN --disc AUTHORIZED_DISC --out LOCAL_DIR [--playwright PACKAGE_DIR] [--headed]');
}

const {chromium, browser: launchOptions} = await loadBrowserTools(values.playwright);
const packageManifest = values.manifest ? JSON.parse(await fs.readFile(values.manifest, 'utf8')) : null;
await fs.mkdir(values.out, {recursive: true});
const browser = await chromium.launch(browserLaunchOptions(launchOptions, {headed: values.headed}));
const context = await browser.newContext({viewport: {width: 1280, height: 960}});
const page = await context.newPage();
const origin = new URL(values.url).origin;
const requests = [], errors = [], requestFailures = [], pendingPackageAborts = [],
  verifiedPackageAborts = [], expectedNavigationAborts = [], violations = [], sockets = [], audioEvents = [];
const startedAt = Date.now();
let ejectReloadInProgress = false;
let assetTraceBeforeTitleEject = null;
const report = {
  schema: 'webmelee-audio-preview-browser-v1',
  browser: browser.version(),
  browser_mode: values.headed ? 'headed' : 'headless',
  build_identity: packageManifest ? {
    schema: packageManifest.schema, profile: packageManifest.profile,
    source_sha: packageManifest.source_sha, runtime_hash: packageManifest.runtime_hash,
    identity_sha256: packageManifest.identity_sha256,
  } : null,
  scope: 'Authorized local disc through original CSS, SSS, supported Mario/Final Destination match, Results return, and Title idle into the source Opening VS asset boundary. If source RNG selects an unadmitted fighter or stage, the exact asset-scope failure remains explicit and recovers through Eject/reimport. Web Audio lifecycle and PCM transport only; no complete attract-cycle, audible-quality, equivalence or performance claim.',
  checks: [],
  audio: {phases: {}, cdp: []},
  assets: {transactions: [], legacyCalls: 0},
  request_failures: requestFailures,
  verified_package_abort_events: verifiedPackageAborts,
  expected_navigation_aborts: expectedNavigationAborts,
};

// Install before the module graph runs. The trace observes the public Web
// Audio boundary (context, worklet connection and PCM messages); it does not
// add a native export or alter the game's input/simulation path.
await page.addInitScript(() => {
  const trace = {contexts: [], worklets: [], nodes: new WeakMap(), ports: new WeakMap(), installErrors: []};
  const rememberError = error => trace.installErrors.push(String(error?.message || error));
  const nonzeroPcm = (record, pcm) => {
    record.pcmMessages++;
    if (!ArrayBuffer.isView(pcm) || pcm.length === 0) return;
    record.pcmFrames += Math.floor(pcm.length / 2);
    let nonzero = 0;
    for (const value of pcm) {
      if (Number.isFinite(value) && Math.abs(value) > 1e-8) nonzero++;
    }
    if (nonzero) {
      record.nonzeroPcmMessages++;
      record.nonzeroPcmSamples += nonzero;
    }
  };
  const snapshot = () => ({
    contexts: trace.contexts.map(record => ({
      sampleRate: record.sampleRate,
      state: record.context.state,
      states: [...record.states],
      resumes: record.resumes,
      closed: record.closed,
    })),
    worklets: trace.worklets.map(record => ({
      name: record.name,
      sampleRate: record.sampleRate,
      connected: record.connected,
      destinationConnected: record.destinationConnected,
      pcmMessages: record.pcmMessages,
      nonzeroPcmMessages: record.nonzeroPcmMessages,
      nonzeroPcmSamples: record.nonzeroPcmSamples,
      pcmFrames: record.pcmFrames,
    })),
    installErrors: [...trace.installErrors],
  });
  window.audioPreviewTrace = {snapshot};

  const NativeAudioContext = window.AudioContext;
  if (NativeAudioContext) {
    const AudioContextProxy = function(...args) {
      const audioContext = new NativeAudioContext(...args);
      const record = {context: audioContext, sampleRate: audioContext.sampleRate, states: [audioContext.state], resumes: 0, closed: false};
      trace.contexts.push(record);
      for (const methodName of ['resume', 'suspend', 'close']) {
        const method = audioContext[methodName];
        if (typeof method !== 'function') continue;
        try {
          audioContext[methodName] = async (...methodArgs) => {
            const result = await method.apply(audioContext, methodArgs);
            record.states.push(audioContext.state);
            if (methodName === 'resume') record.resumes++;
            if (methodName === 'close') record.closed = true;
            return result;
          };
        } catch (error) { rememberError(error); }
      }
      return audioContext;
    };
    AudioContextProxy.prototype = NativeAudioContext.prototype;
    try {
      Object.defineProperty(window, 'AudioContext', {value: AudioContextProxy, configurable: true, writable: true});
    } catch (error) { rememberError(error); }
  }

  const NativeAudioNode = window.AudioNode;
  const nativeConnect = NativeAudioNode?.prototype?.connect;
  if (nativeConnect) {
    try {
      Object.defineProperty(NativeAudioNode.prototype, 'connect', {
        configurable: true,
        writable: true,
        value(destination, ...args) {
          const record = trace.nodes.get(this);
          if (record) {
            record.connected = true;
            record.destinationConnected ||= destination === record.context.destination;
          }
          return nativeConnect.call(this, destination, ...args);
        },
      });
    } catch (error) { rememberError(error); }
  }

  const NativeMessagePort = window.MessagePort;
  const nativePostMessage = NativeMessagePort?.prototype?.postMessage;
  if (nativePostMessage) {
    try {
      Object.defineProperty(NativeMessagePort.prototype, 'postMessage', {
        configurable: true,
        writable: true,
        value(data, ...args) {
          const record = trace.ports.get(this);
          if (record && data?.type === 'pcm') nonzeroPcm(record, data.pcm);
          return nativePostMessage.call(this, data, ...args);
        },
      });
    } catch (error) { rememberError(error); }
  }

  const NativeAudioWorkletNode = window.AudioWorkletNode;
  if (NativeAudioWorkletNode) {
    const AudioWorkletNodeProxy = function(audioContext, name, options) {
      const node = new NativeAudioWorkletNode(audioContext, name, options);
      const record = {
        context: audioContext,
        name,
        sampleRate: audioContext.sampleRate,
        connected: false,
        destinationConnected: false,
        pcmMessages: 0,
        nonzeroPcmMessages: 0,
        nonzeroPcmSamples: 0,
        pcmFrames: 0,
      };
      trace.worklets.push(record);
      trace.nodes.set(node, record);
      trace.ports.set(node.port, record);
      return node;
    };
    AudioWorkletNodeProxy.prototype = NativeAudioWorkletNode.prototype;
    try {
      Object.defineProperty(window, 'AudioWorkletNode', {value: AudioWorkletNodeProxy, configurable: true, writable: true});
    } catch (error) { rememberError(error); }
  }
});

page.on('request', request => requests.push({url: request.url(), method: request.method(), body: request.postData()}));
page.on('requestfailed', async request => {
  const failure = request.failure()?.errorText || '';
  let response = null;
  try { response = await request.response(); } catch {}
  const detail = {
    url: request.url(), method: request.method(), resourceType: request.resourceType(), failure,
    navigationRequest: request.isNavigationRequest(),
    frameUrl: (() => { try { return request.frame()?.url() || null; } catch { return null; } })(),
    responseStatus: response?.status() ?? null,
    elapsedMs: Date.now() - startedAt, ejectReloadInProgress,
  };
  requestFailures.push(detail);
  if (ejectReloadInProgress && failure === 'net::ERR_ABORTED' && new URL(request.url()).origin === origin) {
    expectedNavigationAborts.push(detail);
  } else if (failure === 'net::ERR_ABORTED' && request.resourceType() === 'fetch' &&
      new URL(request.url()).pathname.endsWith('/gameplay_audio_preview.data') && response?.status() === 200) {
    pendingPackageAborts.push(detail);
  } else {
    errors.push(`request failed: ${request.method()} ${request.url()} ${failure}`);
  }
});
page.on('pageerror', error => errors.push(error.message));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
page.on('response', response => { if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`); });
page.on('websocket', socket => sockets.push(socket.url()));
page.on('framenavigated', frame => { if (frame === page.mainFrame()) report.navigations = (report.navigations || 0) + 1; });
await page.addInitScript(() => {
  window.audioPreviewCspViolations = [];
  document.addEventListener('securitypolicyviolation', event => {
    window.audioPreviewCspViolations.push({directive: event.violatedDirective, blocked: event.blockedURI});
  });
});

const cdp = await context.newCDPSession(page);
try {
  await cdp.send('WebAudio.enable');
  for (const event of ['contextCreated', 'contextChanged', 'contextWillBeDestroyed']) {
    cdp.on(`WebAudio.${event}`, data => audioEvents.push({event, data}));
  }
} catch (error) {
  report.audio.cdpError = error.message;
}

const check = async (name, run) => { await run(); report.checks.push(name); console.log(name); };
const screenshot = name => page.screenshot({path: path.join(values.out, `${name}.png`), fullPage: true});
const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 90000});
const selectDisc = driver.selectDisc;
const press = key => driver.pressChord([key]);
const phase = driver.waitForPhase;
const trace = () => page.evaluate(() => window.audioPreviewTrace?.snapshot() || null);
const assetTrace = () => page.evaluate(() => window.audioPreviewAssetTrace?.snapshot() || null);
const nativeMenuState = () => page.evaluate(() => ({
  message: typeof Module?._melee_web_native_menu_message === 'function'
    ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
  running: typeof Module?._melee_web_native_menu_running === 'function'
    ? Module._melee_web_native_menu_running() : 0,
  runtimeError: document.querySelector('#status')?.dataset.runtimeError || null,
}));
const waitForNativeScene = async scene => {
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const state = await nativeMenuState();
    if (state.runtimeError) throw Error(`Runtime error while waiting for ${scene}: ${state.runtimeError}`);
    if (state.running && state.message === scene) return state;
    await page.waitForTimeout(50);
  }
  throw Error(`Timed out waiting for ${scene}: ${JSON.stringify(await nativeMenuState())}`);
};
const installAssetTrace = async () => page.evaluate(() => {
  const module = globalThis.Module;
  const names = ['_melee_web_native_asset_begin', '_melee_web_native_asset_count',
    '_melee_web_native_asset_name', '_melee_web_native_asset_file',
    '_melee_web_native_asset_commit', '_melee_web_native_asset_abort'];
  if (!module || names.some(name => typeof module[name] !== 'function'))
    throw Error('Audio preview does not expose the native scoped asset API.');
  const begin = module._melee_web_native_asset_begin;
  const count = module._melee_web_native_asset_count;
  const name = module._melee_web_native_asset_name;
  const file = module._melee_web_native_asset_file;
  const commit = module._melee_web_native_asset_commit;
  const abort = module._melee_web_native_asset_abort;
  const launch = module._melee_web_native_menu_launch;
  if (typeof launch !== 'function') throw Error('Audio preview does not expose the original menu launch boundary.');
  let launchCalls = 0;
  module._melee_web_native_menu_launch = function(...args) {
    launchCalls++;
    return launch.apply(this, args);
  };
  const events = [];
  const legacy = module._melee_web_native_menu_file;
  const recordScope = generation => {
    const total = generation ? count.call(module, generation) : 0;
    const expected = [];
    for (let index = 0; index < total; ++index) {
      const pointer = name.call(module, generation, index);
      if (!pointer) throw Error(`Native scoped asset name ${index} is unavailable.`);
      expected.push(module.UTF8ToString(pointer));
    }
    return expected;
  };
  const assetsRequested = globalThis.menuAssetsRequested;
  if (typeof assetsRequested !== 'function') throw Error('Audio preview does not expose its scoped asset request callback.');
  const recordRequest = (event, generation) => {
    events.push({event, generation, names: recordScope(generation)});
  };
  module._melee_web_native_asset_begin = function(...args) {
    const generation = begin.apply(this, args);
    recordRequest('begin', generation);
    return generation;
  };
  module._melee_web_native_asset_file = function(generation, namePointer, dataPointer, size) {
    const logicalName = module.UTF8ToString(namePointer);
    const result = file.apply(this, arguments);
    events.push({event: 'file', generation, name: logicalName, size, result});
    return result;
  };
  module._melee_web_native_asset_commit = function(generation, ...args) {
    const result = commit.apply(this, [generation, ...args]);
    events.push({event: 'commit', generation, result});
    return result;
  };
  module._melee_web_native_asset_abort = function(generation, ...args) {
    const result = abort.apply(this, [generation, ...args]);
    events.push({event: 'abort', generation, result});
    return result;
  };
  if (typeof legacy === 'function') {
    module._melee_web_native_menu_file = function(...args) {
      events.push({event: 'legacy-file'});
      return legacy.apply(this, args);
    };
  }
  globalThis.menuAssetsRequested = function(generation, ...args) {
    recordRequest('request', generation);
    return assetsRequested.apply(this, [generation, ...args]);
  };
  globalThis.audioPreviewAssetTrace = {
    snapshot: () => ({legacyAvailable: typeof legacy === 'function', launchCalls,
      events: events.map(event => ({...event}))}),
  };
});
const total = (snapshot, field) => (snapshot?.worklets || []).reduce((sum, worklet) => sum + Number(worklet[field] || 0), 0);
const observeAudio = async (name, before) => {
  const baseline = total(before, 'nonzeroPcmMessages');
  await page.waitForFunction(({baseline}) => {
    const current = globalThis.audioPreviewTrace?.snapshot?.();
    if (!current) return false;
    return current.contexts.some(context => context.sampleRate === 32000 && context.state === 'running') &&
      current.worklets.some(worklet => worklet.name === 'melee-audio-output' &&
        worklet.sampleRate === 32000 &&
        worklet.connected && worklet.destinationConnected &&
        worklet.nonzeroPcmMessages > baseline);
  }, {baseline}, {timeout: 30000});
  const current = await trace();
  assert(current, `${name}: Web Audio trace is unavailable`);
  assert(current.contexts.some(context => context.sampleRate === 32000 && context.state === 'running'),
    `${name}: no running 32000 Hz AudioContext`);
  assert(current.worklets.some(worklet => worklet.name === 'melee-audio-output' &&
    worklet.sampleRate === 32000 &&
    worklet.connected && worklet.destinationConnected && worklet.nonzeroPcmMessages > baseline),
  `${name}: nonzero PCM did not reach a connected audio worklet`);
  report.audio.phases[name] = current;
  return current;
};
const collectViolations = async () => {
  violations.push(...await page.evaluate(() => window.audioPreviewCspViolations || []));
};
const activeAudioContextIds = () => {
  const contexts = new Map();
  for (const row of audioEvents) {
    if (row.event === 'contextCreated' || row.event === 'contextChanged') {
      const context = row.data.context;
      contexts.set(context.contextId, context.contextState);
    } else if (row.event === 'contextWillBeDestroyed') {
      contexts.delete(row.data.contextId);
    }
  }
  return [...contexts].filter(([, state]) => state !== 'closed').map(([id]) => id);
};

try {
  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  assert.equal(response.headers()['cross-origin-opener-policy'], 'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'], 'require-corp');
  await driver.waitForImport();

  await check('preview startup and reviewed native artifact names', async () => {
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#end-session').isDisabled());
    assert.equal(await page.locator('iframe,h1,header,footer,article').count(), 0);
    assert.equal(await page.evaluate(() => {
      const module = globalThis.Module;
      return typeof module?.['_melee_web_native_menu_diagnostics'];
    }), 'undefined',
      'The browser check must not depend on diagnostic native exports');
    await page.waitForFunction(() => !document.querySelector('#keyboard-layout')?.disabled,
      null, {timeout: 90000});
    // Match the public player's ordinary recipe: configure B0XX through the
    // visible controls before using its Start key on the original CSS.
    await page.locator('#controls-open').click();
    await page.locator('#keyboard-layout').selectOption('boxx');
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.activeElement?.id === 'canvas');
    const boundary = await page.waitForFunction(({afterGraphics}) => {
      const panel = document.querySelector('#loading-panel');
      const label = document.querySelector('#loading-label')?.textContent || '';
      const choose = document.querySelector('#choose-disc');
      const module = globalThis.Module;
      const ownerReady = typeof module?._melee_web_native_asset_begin === 'function' &&
        typeof module?._melee_web_native_menu_launch === 'function' &&
        typeof globalThis.menuAssetsRequested === 'function';
      const graphicsReady = !!panel?.hidden;
      const graphicsPreparing = !panel?.hidden && /Preparing graphics/i.test(label);
      if (!ownerReady || (afterGraphics ? !graphicsReady : (!graphicsPreparing || !choose || choose.disabled))) return false;
      return {loadingPanelHidden: graphicsReady, loadingLabel: label,
        canSelectDisc: !!choose && !choose.disabled,
        cacheIdle: typeof module._melee_web_native_menu_cache_idle === 'function' ?
          module._melee_web_native_menu_cache_idle() : null};
    }, {afterGraphics: values['select-after-graphics']}, {timeout: 90000});
    report.selection_boundary = await boundary.jsonValue();
    await boundary.dispose();
    report.selection_boundary.when = values['select-after-graphics'] ? 'after-graphics-ready' : 'while-graphics-preparing';
    await installAssetTrace();
    await screenshot(values['select-after-graphics'] ? 'ready-before-selection' : 'graphics-preparing-before-selection');
  });

  if (!values['select-after-graphics']) {
    await check('replace an owned early selection before native import', async () => {
      assert.equal(report.selection_boundary.loadingPanelHidden, false);
      assert.equal(report.selection_boundary.cacheIdle, 0,
        'The native filesystem/cache import prerequisite is still closed');
      await selectDisc(values.disc);
      const filename = path.basename(values.disc);
      await page.waitForFunction(filename => {
        const status = document.querySelector('#disc-selection-status');
        return status && !status.hidden && status.textContent.includes(filename);
      }, filename, {timeout: 30000});
      const selected = await page.evaluate(() => ({
        label: document.querySelector('#loading-label')?.textContent || '',
        loadingPanelHidden: document.querySelector('#loading-panel')?.hidden ?? null,
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
      }));
      assert.equal(selected.loadingPanelHidden, false);
      assert.equal(selected.phase, 0);
      assert.equal(selected.running, 0);
      await selectDisc({name: 'replacement-before-import.rvz', mimeType: 'application/octet-stream', buffer: Buffer.from('invalid')});
      await page.locator('#error-dialog[open]').waitFor();
      await page.waitForTimeout(300);
      const errorDetail = await page.locator('#error').innerText();
      assert.match(errorDetail, /RVZ is not supported/,
        'The dialog keeps the actionable format error after the temporary filename clears');
      assert(await page.locator('#disc-selection-status').isHidden(),
        'Rejected selections do not persist their filename in the toolbar');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_phase()), 0,
        'The replaced session never entered native import or launch');
      assert.equal((await assetTrace()).events.length, 0,
        'No native asset scope begins for a selection replaced before the import gate');
      report.replacement_before_import = {selected, error_detail: errorDetail,
        current_status: '', native_asset_events: 0};
      await page.locator('#error-close').click();
    });
  }

  await check('authorized-disc import and original CSS emits nonzero PCM', async () => {
    await selectDisc(values.disc);
    const filename = path.basename(values.disc);
    await page.waitForFunction(filename => {
      const status = document.querySelector('#disc-selection-status');
      return status && !status.hidden && status.textContent.includes(filename);
    }, filename, {timeout: 30000});
    report.disc_selection = await page.evaluate(() => ({
      status: document.querySelector('#disc-selection-status')?.textContent || '',
      loadingPanelHidden: document.querySelector('#loading-panel')?.hidden ?? null,
      loadingLabel: document.querySelector('#loading-label')?.textContent || '',
    }));
    if (!values['select-after-graphics']) {
      assert.equal(report.selection_boundary.loadingPanelHidden, false,
        'The owned disc was selected while graphics preparation was visible');
      assert.equal(report.selection_boundary.canSelectDisc, true);
    } else {
      assert.equal(report.selection_boundary.loadingPanelHidden, true,
        'The late-selection variant starts only after graphics are ready');
    }
    const cssEntry = await driver.waitForPublicCss();
    if (cssEntry === 'audio-recovery-required') {
      report.audio_activation_recovery = 'The player showed its specific suspended-audio message; the test used the separate Play gesture only for that recovery.';
      await driver.recoverAudioActivation();
      report.css_entry = 'Audio activation recovery';
    } else {
      report.audio_activation_recovery = 'Automatic public launch entered CSS without a Play click.';
      report.css_entry = 'Automatic public launch';
    }
    assert(await page.locator('#error-dialog').isHidden());
    assert(await page.locator('#loading-panel').isHidden(), 'Loading feedback must retire before interactive CSS');
    const afterCssAssets = await assetTrace();
    assert.equal(afterCssAssets.launchCalls, 1,
      'Successful disc and graphics readiness must invoke the original CSS launch exactly once');
    report.launchesAtCss = afterCssAssets.launchCalls;
    const preload = await page.evaluate(() => {
      let seedBytes = 0;
      try { seedBytes = Module.FS.stat('/initial_pipeline_cache.db').size; } catch {}
      const resources = performance.getEntriesByType('resource')
        .filter(entry => entry.name.endsWith('/gameplay_audio_preview.data'))
        .map(entry => ({durationMs: entry.duration, transferSize: entry.transferSize,
          encodedBodySize: entry.encodedBodySize, decodedBodySize: entry.decodedBodySize,
          responseStart: entry.responseStart, responseEnd: entry.responseEnd}));
      return {seedBytes, resources};
    });
    const packageBytes = packageManifest?.files?.find(file => file.path.endsWith('/gameplay_audio_preview.data'))?.size;
    assert(Number.isInteger(packageBytes) && packageBytes > 0,
      'Build manifest must bind the audio preload size');
    assert.equal(preload.seedBytes, packageBytes,
      'The runtime filesystem must contain the complete, manifest-sized pipeline cache package');
    assert(preload.resources.some(entry => entry.decodedBodySize === packageBytes &&
      entry.responseEnd >= entry.responseStart && entry.responseEnd > 0),
    'Browser resource timing must confirm the full pipeline cache package arrived');
    for (const detail of pendingPackageAborts) {
      verifiedPackageAborts.push({
        ...detail,
        verification: 'HTTP 200; full manifest-sized response recorded by PerformanceResourceTiming and present in /initial_pipeline_cache.db',
      });
    }
    assert.equal(pendingPackageAborts.length, verifiedPackageAborts.length);
    report.pipeline_cache_preload = {...preload, manifestBytes: packageBytes};
    await page.waitForFunction(() => document.activeElement?.id === 'canvas');
    const before = await trace();
    await observeAudio('css', before);
    await screenshot('css');
  });

  await check('pause and resume preserve the audio owner', async () => {
    await page.locator('#pause-game').click();
    await page.waitForFunction(() => document.querySelector('#pause-game').textContent === 'Resume' &&
      !document.querySelector('#pause-game').disabled);
    await page.locator('#pause-game').click();
    await page.waitForFunction(() => document.querySelector('#pause-game').textContent === 'Pause' &&
      !document.querySelector('#pause-game').disabled);
    const before = await trace();
    await observeAudio('css-resume', before);
  });

  await check('ordinary B0XX input enters original SSS with audio', async () => {
    await page.waitForTimeout(1200);
    await press('7');
    await phase(3);
    const before = await trace();
    await observeAudio('sss', before);
    await screenshot('sss');
  });

  await check('ordinary B0XX A confirms the supported Mario/Final Destination match', async () => {
    await page.waitForTimeout(1000);
    // The original cursor starts below the stage tiles. This bounded keyboard
    // recipe was visually checked against the displayed Final Destination
    // highlight; no native selection or PAD state is written by the harness.
    await driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
    await driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
    await screenshot('stage-target');
    await press('m');
    await phase(7);
    const before = await trace();
    await observeAudio('match', before);
    await screenshot('match');
  });

  await check('ordinary B0XX pause and No Contest return through original CSS with audio', async () => {
    const before = await trace();
    // Phase 7 begins the source Ready countdown; let that bounded entry state
    // finish before sending Start so the original match can accept the chord.
    await page.waitForTimeout(5000);
    // Source pause keeps the native loop running, so the outer player toolbar
    // still says Pause. Allow its input lockout to settle before the chord.
    await press('7');
    await page.waitForTimeout(700);
    await screenshot('source-pause');
    await driver.pressChord(['q', '9', 'm', '7'], {holdMs: 250, releaseMs: 200});
    await phase(8);
    await page.waitForTimeout(4500);
    const resultsAudio = await trace();
    report.audio.results_transition = resultsAudio;
    await screenshot('results');
    // No Contest enters the original Results route. Confirm its panels with
    // ordinary Start press/release edges, matching the bounded public return
    // recipe; a single LRAS chord only reaches Results.
    for (let confirmation = 0; confirmation < 8; confirmation++) {
      if (await page.evaluate(() => Module._melee_web_native_menu_phase()) !== 8) break;
      await phase(8);
      await driver.pressChord(['7'], {holdMs: 120, releaseMs: 1380});
    }
    assert.notEqual(await page.evaluate(() => Module._melee_web_native_menu_phase()), 8,
      'Original Results did not finish its bounded Start confirmation sequence');
    const beforeCssReturnAudio = await trace();
    const returnBoundary = await page.waitForFunction(() => {
      const error = document.querySelector('#status')?.dataset.runtimeError;
      if (error) return {error};
      const currentPhase = Module._melee_web_native_menu_phase();
      return (currentPhase === 1 || currentPhase === 9) && Module._melee_web_native_menu_running() ?
        {phase: currentPhase} : false;
    }, null, {timeout: 60000});
    const returnState = await returnBoundary.jsonValue(); await returnBoundary.dispose();
    if (returnState.error) throw Error(returnState.error);
    if (returnState.phase === 9) {
      for (let confirmation = 0; confirmation < 120; confirmation++) {
        await phase(9);
        await driver.pressChord(['7'], {holdMs: 120, releaseMs: 380});
        const next = await page.evaluate(() => ({phase: Module._melee_web_native_menu_phase(),
          error: document.querySelector('#status')?.dataset.runtimeError}));
        if (next.error) throw Error(next.error);
        if (next.phase !== 9) break;
      }
    }
    await phase(1);
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    await observeAudio('css-after-no-contest', beforeCssReturnAudio);
    await screenshot('css-after-no-contest');
  });

  await check('second ordinary B0XX CSS to SSS transition emits audio', async () => {
    await page.waitForTimeout(1200);
    await press('7');
    await phase(3);
    const before = await trace();
    await observeAudio('sss-after-no-contest', before);
    await screenshot('sss-after-no-contest');
  });

  await check('second ordinary B0XX entry reaches Mario/Final Destination with audio', async () => {
    await page.waitForTimeout(1000);
    // Repeat the checked stage cursor recipe after the fresh SSS asset scope.
    await driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
    await driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
    await screenshot('stage-target-after-no-contest');
    await press('m');
    await phase(7);
    const before = await trace();
    await observeAudio('match-after-no-contest', before);
    await screenshot('match-after-no-contest');
  });

  await check('Title timeout follows the source Opening state and Eject recovers to CSS', async () => {
    // The preceding check leaves the second match running. Return through the
    // original Results confirmations before entering Main and Title so this
    // probe exercises the same CSS-first public route as a player session.
    await page.waitForTimeout(5000);
    await press('7');
    await page.waitForTimeout(700);
    await driver.pressChord(['q', '9', 'm', '7'], {holdMs: 250, releaseMs: 200});
    await phase(8);
    await page.waitForTimeout(4500);
    await screenshot('results-after-second-match');
    for (let confirmation = 0; confirmation < 8; confirmation++) {
      if (await page.evaluate(() => Module._melee_web_native_menu_phase()) !== 8) break;
      await phase(8);
      await driver.pressChord(['7'], {holdMs: 120, releaseMs: 1380});
    }
    assert.notEqual(await page.evaluate(() => Module._melee_web_native_menu_phase()), 8,
      'Second original Results route did not finish its bounded Start confirmation sequence');
    const returnState = await page.waitForFunction(() => {
      const error = document.querySelector('#status')?.dataset.runtimeError;
      if (error) return {error};
      const currentPhase = Module._melee_web_native_menu_phase();
      return (currentPhase === 1 || currentPhase === 9) && Module._melee_web_native_menu_running() ?
        {phase: currentPhase} : false;
    }, null, {timeout: 60000});
    const returned = await returnState.jsonValue(); await returnState.dispose();
    if (returned.error) throw Error(returned.error);
    if (returned.phase === 9) {
      for (let confirmation = 0; confirmation < 120; confirmation++) {
        await phase(9);
        await driver.pressChord(['7'], {holdMs: 120, releaseMs: 380});
        const next = await page.evaluate(() => ({phase: Module._melee_web_native_menu_phase(),
          error: document.querySelector('#status')?.dataset.runtimeError}));
        if (next.error) throw Error(next.error);
        if (next.phase !== 9) break;
      }
    }
    await phase(1);
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    await screenshot('css-before-title-idle');

    await driver.pressChord(['q', '9', '7']);
    await waitForNativeScene('Original main menu');
    await page.waitForTimeout(700);
    await press('o');
    await waitForNativeScene('Original title');
    await screenshot('title-before-idle-opening');
    const titleAudioBefore = await trace();
    await observeAudio('title-before-idle-opening', titleAudioBefore);

    await page.waitForFunction(() => {
      const dialog = document.querySelector('#error-dialog');
      const text = document.querySelector('#error')?.textContent || '';
      return dialog?.open && text.includes('GM_OPENING_MV state 1') &&
        text.includes('four-player VS demo') && text.includes('Eject to recover');
    }, null, {timeout: 45000});
    const routeError = await page.locator('#error').innerText();
    assert.match(routeError, /Original Title idle reached source GM_OPENING_MV state 1/);
    assert.match(routeError, /four-player VS demo/);
    assert.match(routeError, /source-selected asset scope is unsupported/);
    assert.match(routeError, /Opening demo source selection has unadmitted/);
    assert.match(routeError, /(not admitted|exceeds|absent from the generated registry)/);
    const transitionState = await nativeMenuState();
    report.title_idle_handoff = {
      source_target: 'GM_OPENING_MV state 1',
      browser_boundary: 'source-selected four-player VS asset scope explicitly rejected',
      asset_error: routeError,
      native_running_after_error: transitionState.running,
      error: routeError,
      full_cycle_supported: false,
    };
    await screenshot('title-idle-opening-unsupported');

    const contextIds = activeAudioContextIds();
    assetTraceBeforeTitleEject = await assetTrace();
    assert(assetTraceBeforeTitleEject,
      'Scoped asset trace must be retained before the Title Eject reload');
    const eventOffset = audioEvents.length;
    const navigationCount = report.navigations || 0;
    await page.locator('#error-close').click();
    ejectReloadInProgress = true;
    try {
      await driver.unload();
      assert((report.navigations || 0) > navigationCount,
        'Eject after the Opening owner boundary must reload the player document');
      await driver.waitForImport();
      await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
    } finally {
      ejectReloadInProgress = false;
    }
    const teardownEvents = audioEvents.slice(eventOffset);
    assert(contextIds.every(id => teardownEvents.some(row =>
      (row.event === 'contextChanged' && row.data.context.contextId === id &&
        row.data.context.contextState === 'closed') ||
      (row.event === 'contextWillBeDestroyed' && row.data.contextId === id))),
    'Eject after Title idle must close every prior AudioContext');
    const freshTrace = await trace();
    assert(freshTrace && freshTrace.contexts.length === 0,
      'Eject after Title idle must retire prior Web Audio owners');

    await selectDisc(values.disc);
    const cssEntry = await driver.waitForPublicCss();
    if (cssEntry === 'audio-recovery-required') {
      await driver.recoverAudioActivation();
      report.title_idle_handoff.audio_activation_recovery = true;
    }
    assert.equal(await page.evaluate(() => Module._melee_web_native_menu_phase()), 1,
      'Disc reimport after Title idle must start directly in original CSS');
    const beforeCss = await trace();
    await observeAudio('css-after-title-idle-reimport', beforeCss);
    await screenshot('css-after-title-idle-reimport');
    report.title_idle_handoff.eject_reimport = 'CSS-first; prior audio contexts closed';
  });

  await check('scoped audio asset generations are complete and never use legacy file upload', async () => {
    // Title Eject intentionally reloads the document. Keep the initial
    // document's completed source-scope history before that navigation clears
    // its in-memory instrumentation object.
    const observed = assetTraceBeforeTitleEject || await assetTrace();
    assert(observed, 'Scoped asset trace is unavailable');
    const begins = observed.events.filter(event => event.event === 'begin' || event.event === 'request');
    const commits = observed.events.filter(event => event.event === 'commit');
    const aborts = observed.events.filter(event => event.event === 'abort');
    assert(begins.length >= 4, `Expected initial menu, match, return-menu and second match scopes; saw ${begins.length}`);
    assert.equal(new Set(begins.map(begin => begin.generation)).size, begins.length,
      'Scoped asset generations must be unique');
    assert.equal(aborts.length, 0, 'Audio preview must not abort a scoped asset transfer');
    assert.equal(commits.length, begins.length, 'Every observed scoped asset generation must commit');
    for (const begin of begins) {
      assert(begin.generation > 0, 'Scoped asset generation must be positive');
      assert(begin.names.includes('dsp_coef.bin'), 'Every audio scope must include generated DSP coefficients');
      assert(begin.names.includes('sislib_font.bin'), 'Every audio scope must include generated font bytes');
      const files = observed.events.filter(event => event.event === 'file' && event.generation === begin.generation);
      assert(files.every(file => file.result), `Generation ${begin.generation} rejected a transferred file`);
      assert.deepEqual(new Set(files.map(file => file.name)), new Set(begin.names),
        `Generation ${begin.generation} did not transfer its complete expected name set`);
      assert.equal(files.length, begin.names.length,
        `Generation ${begin.generation} transferred a duplicate or missing file`);
      assert(observed.events.some(event => event.event === 'commit' && event.generation === begin.generation && event.result),
        `Generation ${begin.generation} did not report a successful commit`);
    }
    assert.equal(observed.events.filter(event => event.event === 'legacy-file').length, 0,
      'Audio preview must use scoped transfers instead of _melee_web_native_menu_file');
    report.assets = {
      transactions: begins.map(begin => ({generation: begin.generation, names: begin.names})),
      legacyCalls: observed.events.filter(event => event.event === 'legacy-file').length,
      legacyAvailable: observed.legacyAvailable,
    };
  });

  await check('Eject closes Web Audio and reloads the player document', async () => {
    const contextIds = activeAudioContextIds();
    assert(contextIds.length > 0, 'The session must have an observed AudioContext');
    const eventCount = audioEvents.length;
    const navigationCount = report.navigations || 0;
    await collectViolations();
    ejectReloadInProgress = true;
    try {
      await driver.unload();
      assert((report.navigations || 0) > navigationCount, 'Eject must reload the player document');
      await driver.waitForImport();
      await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
    } finally {
      ejectReloadInProgress = false;
    }
    const fresh = await trace();
    assert(fresh && fresh.contexts.length === 0, 'Reloaded player must not retain the old AudioContext');
    // Closing a context releases its audio resources. Chrome may retain the
    // closed object until collection without emitting contextWillBeDestroyed.
    const teardownEvents = audioEvents.slice(eventCount);
    assert(contextIds.every(id => teardownEvents.some(row =>
      (row.event === 'contextChanged' && row.data.context.contextId === id &&
        row.data.context.contextState === 'closed') ||
      (row.event === 'contextWillBeDestroyed' && row.data.contextId === id))),
    'Eject must close every prior AudioContext');
    assert(await page.locator('#start-game').isDisabled());
    report.audio.afterEject = fresh;
  });

  await check('same-origin GET-only session has no page, CSP, socket or upload failures', async () => {
    await collectViolations();
    const artifactNames = new Set(requests.map(request => path.posix.basename(new URL(request.url).pathname)));
    for (const artifact of ['gameplay_audio_preview.js', 'gameplay_audio_preview.wasm', 'gameplay_audio_preview.data']) {
      assert(artifactNames.has(artifact), `Preview artifact was not requested: ${artifact}`);
    }
    assert.equal(artifactNames.has('gameplay_public.js'), false, 'Audio preview must not load the silent public runtime');
    for (const request of requests) {
      const url = new URL(request.url);
      assert.equal(url.origin, origin, `off-origin request: ${request.url}`);
      assert.equal(request.method, 'GET', `non-GET request: ${request.method} ${request.url}`);
      assert.equal(request.body, null, `upload body on ${request.url}`);
    }
    assert.deepEqual(sockets, []);
    assert.deepEqual(violations, []);
    assert.deepEqual(errors, []);
    assert.equal(requestFailures.length,
      verifiedPackageAborts.length + expectedNavigationAborts.length,
      'Every Chrome request-aborted event must have a verified complete package transfer or be caused by Eject reload');
    report.requests = requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
    report.audio.cdp = audioEvents;
  });
  report.result = 'pass';
} catch (error) {
  report.result = 'fail';
  report.failure = error.message;
  report.state = await page.evaluate(() => ({
    url: location.href,
    status: document.querySelector('#status')?.textContent || null,
    error: document.querySelector('#error')?.textContent || null,
    phase: typeof globalThis.Module?._melee_web_native_menu_phase === 'function' ?
      Module._melee_web_native_menu_phase() : null,
    running: typeof globalThis.Module?._melee_web_native_menu_running === 'function' ?
      Module._melee_web_native_menu_running() : null,
    audio: window.audioPreviewTrace?.snapshot?.() || null,
  })).catch(() => null);
  await screenshot('failure').catch(() => {});
  throw error;
} finally {
  await collectViolations().catch(() => {});
  report.errors = errors;
  report.request_failures = requestFailures;
  report.expected_navigation_aborts = expectedNavigationAborts;
  report.csp = violations;
  report.audio.cdp = audioEvents;
  report.requests = report.requests || requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  driver.dispose();
  await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
