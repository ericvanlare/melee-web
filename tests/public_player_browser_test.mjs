#!/usr/bin/env node
/** Actual release-graph UI/network smoke. Owned-disc checks are optional and never performance admission. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {installBrowserAudioTrace} from './browser_audio_trace.mjs';
const {values} = parseArgs({options: {
  ...Object.fromEntries(['url', 'playwright', 'disc', 'out', 'manifest'].map(name => [name, {type: 'string'}])),
  audio: {type: 'boolean', default: false},
  headed: {type: 'boolean', default: false},
}});
if (!values.url || !values.out) throw Error('Use --url ORIGIN --out LOCAL_DIR [--playwright PACKAGE_DIR] [--disc OWNED_DISC] [--audio] [--headed]');
const {chromium,browser:launchOptions} = await loadBrowserTools(values.playwright);
const packageManifest = values.manifest ? JSON.parse(await fs.readFile(values.manifest, 'utf8')) : null;
await fs.mkdir(values.out, {recursive: true});
const browser = await chromium.launch(browserLaunchOptions(launchOptions, {headed: values.headed}));
const context = await browser.newContext({viewport: {width: 1280, height: 960}});
const page = await context.newPage(), origin = new URL(values.url).origin;
const requests = [], errors = [], violations = [], sockets = [], audioEvents = [];
const activeDocumentAudioContexts = new Set();
const report = {schema: 'webmelee-public-player-browser-v1', browser: browser.version(), browser_mode: values.headed ? 'headed' : 'headless', checks: [],
  profile: values.audio ? 'audio-player' : 'player',
  build_identity: packageManifest ? {
    schema: packageManifest.schema, profile: packageManifest.profile,
    source_sha: packageManifest.source_sha, runtime_hash: packageManifest.runtime_hash,
    identity_sha256: packageManifest.identity_sha256,
  } : null,
  scope: values.audio ? 'Production audio-enabled public entry, ordinary keyboard UI, original menu route, supported match/results lifecycle, Web Audio initialization and nonzero PCM transport. No retail pixel/PCM equivalence, audible-quality, physical-controller or foreground-timing claim.' : 'Production entry, ordinary keyboard UI, lifecycle and application network smoke. No retail comparison, physical-controller, PCM or performance claim.'};
page.on('request', request => requests.push({url: request.url(), method: request.method(), body: request.postData()}));
page.on('pageerror', error => errors.push(error.message));
page.on('websocket', socket => sockets.push(socket.url()));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
await page.addInitScript(() => {
  window.releaseCspViolations = [];
  document.addEventListener('securitypolicyviolation', event => window.releaseCspViolations.push({directive: event.violatedDirective, blocked: event.blockedURI}));
});
if (values.audio) await page.addInitScript(installBrowserAudioTrace);
const cdp = await context.newCDPSession(page);
await cdp.send('WebAudio.enable');
page.on('framenavigated', frame => {
  if (frame === page.mainFrame()) activeDocumentAudioContexts.clear();
});
for (const event of ['contextCreated', 'contextChanged', 'contextWillBeDestroyed']) cdp.on('WebAudio.' + event, data => {
  audioEvents.push({event, data});
  if (event === 'contextCreated') activeDocumentAudioContexts.add(data.context.contextId);
  if (event === 'contextWillBeDestroyed') activeDocumentAudioContexts.delete(data.contextId);
  if (event === 'contextChanged' && data.context.contextState === 'closed')
    activeDocumentAudioContexts.delete(data.context.contextId);
});
const check = async (name, run) => { await run(); report.checks.push(name); console.log(name); };
const driver = createBrowserDriver(page, {surface:'public', timeoutMs:90000});
const ready = driver.waitForImport;
const shot = name => page.screenshot({path: path.join(values.out, name + '.png'), fullPage: true});
const press = key => driver.pressChord([key]);
const phase = driver.waitForPhase;
const audioTrace = () => page.evaluate(() => window.audioPreviewTrace?.snapshot() || null);
const pcmMessages = snapshot => (snapshot?.worklets || []).reduce((sum, worklet) => sum + worklet.nonzeroPcmMessages, 0);
const observePcm = async (name, before) => {
  if (!values.audio) return null;
  const baseline = pcmMessages(before);
  await page.waitForFunction(({baseline}) => {
    const current = window.audioPreviewTrace?.snapshot?.();
    return current?.contexts.some(context => context.sampleRate === 32000 && context.state === 'running') &&
      current.worklets.some(worklet => worklet.name === 'melee-audio-output' &&
        worklet.sampleRate === 32000 && worklet.connected && worklet.destinationConnected &&
        worklet.nonzeroPcmMessages > baseline);
  }, {baseline}, {timeout: 30000});
  const current = await audioTrace();
  assert(current, `${name}: Web Audio observer was not installed`);
  assert(current.worklets.some(worklet => worklet.name === 'melee-audio-output' &&
    worklet.sampleRate === 32000 && worklet.connected && worklet.destinationConnected &&
    worklet.nonzeroPcmMessages > baseline), `${name}: no new nonzero PCM reached the connected worklet`);
  report.audio_phases ||= {};
  report.audio_phases[name] = current;
  return current;
};
async function readNativeMenuState() {
  return page.evaluate(() => ({
    message: typeof Module?._melee_web_native_menu_message === 'function'
      ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
    running: typeof Module?._melee_web_native_menu_running === 'function'
      ? Module._melee_web_native_menu_running() : 0,
    status: document.querySelector('#status')?.textContent || '',
    runtimeError: document.querySelector('#status')?.dataset.runtimeError || null,
  }));
}
const isTimingPaused = state =>
  state.message?.startsWith('Paused after a timing disruption') ||
  state.status.startsWith('Paused after a timing disruption');
async function resumeAfterTimingPause(label) {
  const state = await readNativeMenuState();
  if (!isTimingPaused(state)) return false;
  report.timing_pause_recoveries ||= [];
  report.timing_pause_recoveries.push({label, message: state.message, running: state.running, status: state.status});
  await page.waitForFunction(() => {
    const button = document.querySelector('#pause-game');
    return button && !button.disabled;
  }, null, {timeout: 10000});
  await page.locator('#pause-game').click();
  await page.waitForFunction(() => Module._melee_web_native_menu_running() &&
    !Module.UTF8ToString(Module._melee_web_native_menu_message())
      .startsWith('Paused after a timing disruption') &&
    !String(document.querySelector('#status')?.textContent || '')
      .startsWith('Paused after a timing disruption'),
  null, {timeout: 15000});
  return true;
}
async function waitForNativeScene(scene) {
  const deadline = Date.now() + 90000;
  while (Date.now() < deadline) {
    const state = await readNativeMenuState();
    if (state.runtimeError) throw Error(`Runtime error while waiting for ${scene}: ${state.runtimeError}`);
    if (isTimingPaused(state)) {
      await resumeAfterTimingPause(`waiting-for-${scene}`);
      continue;
    }
    if (state.running && state.message === scene) return state;
    await page.waitForTimeout(50);
  }
  throw Error(`Timed out waiting for ${scene}: ${JSON.stringify(await readNativeMenuState())}`);
}
const captureUnload = async () => page.evaluate(() => {
  const nativeUnload = Module._melee_web_native_menu_unload.bind(Module);
  Module._melee_web_native_menu_unload = (...args) => {
    const result = nativeUnload(...args);
    window.name = JSON.stringify({result,
      message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
      phase: Module._melee_web_native_menu_phase(),
      running: Module._melee_web_native_menu_running()});
    return result;
  };
});
const assertUnloadCompleted = async () => {
  const result = await page.evaluate(() => JSON.parse(window.name || 'null'));
  assert.deepEqual(result, {result: 1, message: 'Native menus unloaded.', phase: 0, running: 0},
    'Eject must complete native teardown before the document reloads');
  await page.evaluate(() => { window.name = ''; });
  return result;
};
async function collectViolations() { violations.push(...await page.evaluate(() => window.releaseCspViolations)); }
const selectDisc = driver.selectDisc;
async function armLaunchObserver() {
  await page.waitForFunction(() => typeof globalThis.Module?._melee_web_native_menu_launch === 'function',
    null, {timeout: 30000});
  await page.evaluate(() => {
    const nativeLaunch = Module._melee_web_native_menu_launch.bind(Module);
    window.nativeLaunchCalls = 0;
    Module._melee_web_native_menu_launch = (...args) => {
      window.nativeLaunchCalls++;
      return nativeLaunch(...args);
    };
  });
}
async function armAudioActivationObserver() {
  if (!values.audio) return;
  await page.evaluate(() => {
    if (window.webMeleeAudioActivationObserved) return;
    window.webMeleeAudioActivationObserved = true;
    const NativeAudioContext = window.AudioContext;
    window.audioActivation = [];
    window.AudioContext = class extends NativeAudioContext {
      constructor(...args) {
        super(...args);
        const nativeResume = this.resume.bind(this), context = this;
        this.resume = () => {
          const record = {gesture: navigator.userActivation?.isActive === true, before: context.state};
          window.audioActivation.push(record);
          return nativeResume().then(() => { record.after = context.state; });
        };
      }
    };
  });
}
async function waitForCssOrAudioRecovery() {
  await page.waitForFunction(() => {
    const dialog = document.querySelector('#error-dialog');
    return !!dialog?.open || (Module._melee_web_native_menu_phase() === 1 && Module._melee_web_native_menu_running());
  }, null, {timeout: 90000});
  const recovery = await page.locator('#error-dialog').isVisible();
  if (recovery) {
    const failure = await page.locator('#error').innerText();
    assert(values.audio && /browser kept game audio suspended.*choose Play to enable audio/i.test(failure),
      `Unexpected first-start failure: ${failure}`);
    assert(await page.locator('#start-game').isEnabled(), 'A browser that blocks activation exposes Play recovery');
    assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 0,
      'Blocked audio must stop before native launch');
    await page.locator('#error-close').click();
    await page.locator('#start-game').click();
  }
  await phase(1);
  assert(await page.locator('#error-dialog').isHidden());
  return recovery;
}

try {
  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  assert.equal(response.headers()['cross-origin-opener-policy'], 'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'], 'require-corp');
  assert.match(response.headers()['content-security-policy'], /'wasm-unsafe-eval'/);
  await page.locator('#loading-panel').waitFor({state: 'visible', timeout: 30000});
  await check('disc selection during graphics preparation', async () => {
    assert(await page.locator('#choose-disc').isEnabled(), 'Selection stays available while startup is busy');
    if (values.disc) {
      await armAudioActivationObserver();
      await selectDisc(values.disc);
      await page.waitForFunction(() => {
        const status = document.querySelector('#disc-selection-status');
        return !status.hidden && /checking|waiting for graphics|preparing/i.test(status.textContent);
      }, null, {timeout: 15000});
      assert(await page.locator('#loading-panel').isVisible(), 'The valid disc is acknowledged while graphics are preparing');
      const acknowledgement = await page.locator('#disc-selection-status').innerText();
      assert(acknowledgement.includes(path.basename(values.disc)), 'The temporary acknowledgement identifies the selected file');
      await armLaunchObserver();
      const audioRecovery = await waitForCssOrAudioRecovery();
      if (audioRecovery) report.early_audio_activation_recovery = 'Browser required a separate Play gesture after graphics preparation.';
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'A valid disc selected during graphics preparation auto-launches exactly once');
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_phase()), 1);
      assert(await page.locator('#loading-panel').isHidden());
      assert(await page.locator('#disc-selection-status').isHidden(), 'The filename clears as CSS starts');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
      await shot('css-after-early-selection');
      report.early_disc_validation = `A valid file (${path.basename(values.disc)}) was selected while graphics preparation was visible; its acknowledgement cleared at CSS and native launch ran once.`;
      await driver.unload();
      assert(await page.locator('#disc-selection-status').isHidden(), 'Eject and reload leave no disc acknowledgement');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
    } else {
      await selectDisc({name: 'early-invalid.rvz', mimeType: 'application/octet-stream', buffer: Buffer.from('invalid')});
      await page.locator('#error-dialog[open]').waitFor();
      assert.match(await page.locator('#error').innerText(), /RVZ is not supported/);
      assert(await page.locator('#disc-selection-status').isHidden(), 'Validation errors do not occupy the toolbar');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
      report.early_disc_validation = 'A file was selected and rejected while the full graphics loading panel was still visible; no import or launch occurred.';
      await page.reload();
    }
  });
  await ready();
  await check('isolated WebGPU/Wasm startup and direct original-style player', async () => {
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    assert.equal(await page.evaluate(() => Module._melee_web_native_menu_cache_idle()), 1,
      'Full graphics readiness includes the native volatile cache being idle');
    const state = await page.evaluate(() => Module._melee_web_native_menu_cache_idle());
    assert.equal(state, 1,
      'The public renderer must open its volatile cache before consuming the bundled pipeline seed');
    const selective = await page.evaluate(() => Module.pipelinePreparation || null);
    if (selective) {
      assert.equal(selective.policy, 'catalog');
      assert.equal(selective.selected, 626);
      assert.equal(selective.binding_sha256, '122eaece4fce109e9f2c958de8b0bb7315ccb670dab349eb2090da1f2fd589a6');
      assert.equal(selective.unexpected_count, 0);
      assert(await page.evaluate(() => Module.FS.stat('/initial_pipeline_cache.db').size > 0));
      assert.deepEqual(await page.evaluate(() => Module.FS.readdir('/melee-render-cache').filter(name => !['.', '..'].includes(name))), [],
        'Selective preparation retains descriptors in memory without a writable cache or IDBFS');
    } else {
      assert(await page.evaluate(() => Module.FS.stat('/melee-render-cache/pipeline_cache.db').size > 0),
        'The ordinary bundled seed needs a writable document-local SQLite database');
    }
    assert.equal(await page.evaluate(() => crossOriginIsolated && !!navigator.gpu), true);
    assert.equal(await page.locator('canvas').count(), 1);
    assert.equal(await page.locator('iframe,h1,header,footer,article').count(), 0);
    assert(await page.locator('#start-game').isDisabled());
    assert.equal(await page.locator('#disc-ack').count(), 0,
      'Disc selection must not have an acknowledgement checkbox gate');
    assert(await page.locator('#end-session').isDisabled());
    assert.equal(await page.locator('#brand').innerText(), 'WEBMELEE.GG');
    assert.equal(await page.locator('#edition').innerText(), 'alpha');
    assert.equal(await page.locator('#edition em').evaluate(node => getComputedStyle(node).fontStyle), 'italic');
    const actionButtons = await page.locator('#toolbar-actions > button').evaluateAll(nodes => nodes.map(node => node.id));
    const primaryActionOrder = ['choose-disc', 'start-game', 'pause-game', 'fullscreen', 'end-session'];
    assert.deepEqual(actionButtons.filter(id => primaryActionOrder.includes(id)), primaryActionOrder,
      'The compact toolbar keeps Disc, Play, Pause, Fullscreen and Eject in order');
    assert.equal(actionButtons.indexOf('controls-open'), 0,
      'Controls stays in its established leading position');
    if (values.audio) {
      assert.equal(await page.locator('#audio-note,#audio-info,#audio-details').count(), 0);
    } else assert.equal(await page.locator('#audio-note').textContent(), 'no audio ⓘlicensing issue, need to remove about 50 lines of Dolphin audio code still');
    assert.deepEqual(await page.locator('#toolbar > *').evaluateAll(nodes => nodes.map(node => node.id)),
      ['toolbar-brand', 'toolbar-actions', 'toolbar-meta']);
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_replay_begin), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_diagnostics), 'undefined');
    assert.equal(await page.evaluate(() => typeof window.menuObservePlayer), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module.runtimeCacheState), 'undefined');
    await shot('desktop');
  });
  await check('controls, focus and preferences survive a fresh document', async () => {
    await page.locator('#controls-open').click();
    // This smoke drives the original menus with the keyboard, regardless of
    // physical devices attached to the host running the browser.
    await page.locator('#player-one-source').selectOption('keyboard');
    await page.locator('#player-two-source').selectOption('off');
    await page.locator('#keyboard-layout').selectOption('boxx');
    assert(await page.locator('#boxx-source-note').isVisible());
    assert(await page.locator('#player-two-source option[value="keyboard"]').isDisabled());
    await page.waitForFunction(() => document.querySelector('#player-two-source-status').textContent === 'Off');
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.activeElement.id === 'canvas');
    await collectViolations(); await page.reload(); await ready();
    assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
  });
  await check('disclosure before file selection, invalid-disc errors and selectable retry', async () => {
    await armAudioActivationObserver();
    await page.locator('#choose-disc').click();
    assert(await page.locator('#disc-dialog').isVisible());
    assert.equal(await page.locator('#disc-ack').count(), 0);
    const disclosure = (await page.locator('#disc-dialog').textContent()).replace(/\s+/g, ' ');
    assert.match(disclosure, /choosing a game file, you acknowledge the disclosures in the linked.*and agree to the Terms of Use/i);
    for (const [href, text] of [['/terms', 'Terms of Use'], ['/privacy', 'Privacy Notice'],
      ['/notices', 'About & legal'], ['/copyright', 'Copyright & contact']]) {
      assert.equal(await page.locator(`#disc-dialog a[href="${href}"]`).innerText(), text);
    }
    await page.locator('#disc-cancel').click();
    await selectDisc({name: 'unsupported.rvz', mimeType: 'application/octet-stream', buffer: Buffer.from('invalid')});
    await page.locator('#error-dialog[open]').waitFor();
    assert.match(await page.locator('#error').innerText(), /RVZ is not supported/);
    assert(await page.locator('#choose-disc').isEnabled());
    await page.locator('#error-close').click();
    await selectDisc({name: 'invalid.iso', mimeType: 'application/octet-stream', buffer: Buffer.alloc(2048)});
    await page.waitForFunction(() => document.querySelector('#error-dialog').open && /expected Super Smash Bros/.test(document.querySelector('#error').textContent));
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#choose-disc').isEnabled());
    await page.locator('#error-close').click();
    if (values.audio) {
      const activation = await page.evaluate(() => window.audioActivation);
      assert(activation.some(record => record.gesture), 'AudioContext.resume is initiated from the Choose file gesture');
      report.audio_activation_before_import = activation;
      report.audio_activation_before_import_state = activation.some(record => record.after === 'running') ?
        'running' : 'the browser kept the context suspended; a valid-disc attempt will expose recovery';
    }
  });
  await check('narrow layouts and fullscreen', async () => {
    await collectViolations(); await page.reload(); await ready();
    for (const width of [320, 390, 768]) {
      await page.setViewportSize({width, height: 844});
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `overflow at ${width}`);
      if (width === 390) await shot('mobile');
    }
    await page.setViewportSize({width: 844, height: 390});
    const landscapeGeometry = await page.evaluate(() => {
      const rect = selector => {
        const {top, bottom, width, height} = document.querySelector(selector).getBoundingClientRect();
        return {top, bottom, width, height};
      };
      return {runtime: rect('#runtime-host'), toolbar: rect('#toolbar'), canvas: rect('#canvas'),
        pageWidth: document.documentElement.scrollWidth};
    });
    assert(landscapeGeometry.pageWidth <= 844, 'landscape toolbar does not overflow the viewport');
    assert(landscapeGeometry.runtime.bottom <= landscapeGeometry.toolbar.top + 1,
      'landscape toolbar stays below the game area');
    assert(Math.abs(landscapeGeometry.canvas.width / landscapeGeometry.canvas.height - 4 / 3) < 0.01,
      'landscape keeps the original 4:3 canvas');
    await shot('landscape-ready');
    await page.setViewportSize({width: 1280, height: 960});
    const fullscreenAvailable = await page.evaluate(() => document.fullscreenEnabled === true &&
      typeof document.querySelector('#player').requestFullscreen === 'function' &&
      typeof document.exitFullscreen === 'function');
    assert(fullscreenAvailable, 'Installed headless Chrome supports native fullscreen for the supported-path check');
    assert.equal(await page.locator('#fullscreen').isVisible(), fullscreenAvailable,
      'Fullscreen is visible only when the native API is supported');
    assert.equal(await page.locator('#fullscreen-status').count(), 0,
      'The page has no persistent fullscreen explanation');
    assert.equal(await page.getByRole('button', {name: 'Expand player'}).count(), 0,
      'The page has no expansion fallback');
    assert.doesNotMatch(await page.locator('body').innerText(), /browser controls remain visible|fullscreen unavailable/i);
    if (fullscreenAvailable) {
      await page.locator('#fullscreen').click();
      await page.waitForFunction(() => document.fullscreenElement === document.querySelector('#player'));
      await page.waitForFunction(() => document.querySelector('#fullscreen').textContent === 'Exit fullscreen');
      assert.equal(await page.locator('#fullscreen').innerText(), 'Exit fullscreen');
      await page.locator('#fullscreen').click();
      await page.waitForFunction(() => !document.fullscreenElement &&
        document.querySelector('#fullscreen').textContent === 'Fullscreen');
      assert.equal(await page.locator('#fullscreen').innerText(), 'Fullscreen');
    }
    report.fullscreen_supported = {available: true, entered: true, exited: true};
    await page.addInitScript(() => Object.defineProperty(document, 'fullscreenEnabled', {configurable: true, value: false}));
    await page.reload(); await ready();
    const unsupportedFullscreen = await page.evaluate(() => {
      const button = document.querySelector('#fullscreen'), rect = button.getBoundingClientRect();
      const primary = new Set(['controls-open', 'choose-disc', 'start-game', 'pause-game', 'end-session']);
      return {hidden: button.hidden, display: getComputedStyle(button).display, width: rect.width, height: rect.height,
        actions: [...document.querySelectorAll('#toolbar-actions > button')]
          .filter(node => primary.has(node.id) && !node.hidden && getComputedStyle(node).display !== 'none')
          .map(node => node.id)};
    });
    assert(unsupportedFullscreen.hidden && unsupportedFullscreen.display === 'none' &&
      unsupportedFullscreen.width === 0 && unsupportedFullscreen.height === 0,
    'Unsupported native fullscreen leaves no toolbar slot');
    assert.deepEqual(unsupportedFullscreen.actions, ['controls-open', 'choose-disc', 'start-game', 'pause-game', 'end-session'],
      'The remaining controls flow together without a fullscreen gap');
    assert.equal(await page.locator('#fullscreen-status').count(), 0);
    assert.doesNotMatch(await page.locator('body').innerText(), /browser controls remain visible|fullscreen unavailable/i);
    await shot('fullscreen-unsupported');
    report.fullscreen_unsupported = {simulated: true, button_hidden: unsupportedFullscreen.hidden,
      display: unsupportedFullscreen.display, width: unsupportedFullscreen.width, height: unsupportedFullscreen.height};
    await page.removeAllInitScripts();
    await page.addInitScript(() => {
      window.releaseCspViolations = [];
      document.addEventListener('securitypolicyviolation', event => window.releaseCspViolations.push({directive: event.violatedDirective, blocked: event.blockedURI}));
    });
    await page.reload(); await ready();
    assert(await page.locator('#fullscreen').isVisible(), 'The simulated unsupported state does not leak into the gameplay capture');
    await page.locator('#controls-open').click();
    await page.locator('#player-one-source').selectOption('touch');
    await page.locator('#controls-close').click();
    await page.locator('#touch-controls:not([hidden])').waitFor();
    for (const [name, width, height] of [['touch-portrait-ready', 390, 844], ['touch-landscape-ready', 844, 390]]) {
      await page.setViewportSize({width, height});
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const touchLayout = await page.evaluate(() => {
        const rect = selector => {
          const {left, top, right, bottom, width, height} = document.querySelector(selector).getBoundingClientRect();
          return {left, top, right, bottom, width, height};
        };
        const controls = [...document.querySelectorAll('#touch-controls [data-touch-button], #touch-controls [data-touch-stick]')];
        return {runtime: rect('#runtime-host'), toolbar: rect('#toolbar'), canvas: rect('#canvas'), overlay: rect('#touch-controls'),
          controls: controls.map(element => {
            const r = element.getBoundingClientRect();
            const hit = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
            return {visible: r.width > 0 && r.height > 0, reachable: hit === element || element.contains(hit)};
          }), pageWidth: document.documentElement.scrollWidth};
      });
      assert(touchLayout.pageWidth <= width, `${name} toolbar does not overflow the viewport`);
      assert(touchLayout.runtime.bottom <= touchLayout.toolbar.top + 1, `${name} toolbar stays below the game area`);
      assert(Math.abs(touchLayout.canvas.width / touchLayout.canvas.height - 4 / 3) < 0.01,
        `${name} keeps the original 4:3 canvas`);
      assert(Math.abs(touchLayout.overlay.width - touchLayout.canvas.width) < 1 &&
        Math.abs(touchLayout.overlay.height - touchLayout.canvas.height) < 1,
      `${name} touch overlay follows the canvas`);
      assert(touchLayout.controls.length > 0 && touchLayout.controls.every(control => control.visible && control.reachable),
        `${name} touch controls remain visible and hit-test reachable`);
      await shot(name);
    }
    await page.locator('#controls-open').click();
    await page.locator('#player-one-source').selectOption('keyboard');
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.querySelector('#touch-controls').hidden);
    await page.setViewportSize({width: 1280, height: 960});
  });
  if (values.disc) {
    await check('owned-disc import, native preparation and original CSS', async () => {
      await armAudioActivationObserver();
      await armLaunchObserver();
      const audioActivationStart = values.audio ? await page.evaluate(() => window.audioActivation.length) : 0;
      await selectDisc(values.disc);
      const audioRecovery = await waitForCssOrAudioRecovery();
      if (audioRecovery) report.audio_activation_recovery = 'Browser required a separate Play gesture; first attempt did not launch or retry.';
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'A prepared disc must call native launch exactly once; the normal path does not click Play');
      assert(await page.locator('#loading-panel').isHidden(), 'Loading feedback must retire before interactive CSS');
      await page.waitForFunction(() => document.activeElement.id === 'canvas');
      assert(await page.locator('#pause-game').isEnabled());
      assert(await page.locator('#start-game').isDisabled(), 'Play is not a required first-launch step');
      assert(await page.locator('#disc-selection-status').isHidden(),
        'The selected filename clears after play starts');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
      assert.doesNotMatch(await page.locator('#toolbar-meta').innerText(), /Playing\s+.+/,
        'The toolbar has no post-start filename/status narration');
      await shot('css-after-import');
      await page.locator('#controls-open').click();
      await page.locator('#player-one-source').selectOption('touch');
      await page.locator('#controls-close').click();
      await page.locator('#touch-controls:not([hidden])').waitFor();
      for (const [name, width, height] of [['portrait-gameplay', 390, 844], ['landscape-gameplay', 844, 390]]) {
        await page.setViewportSize({width, height});
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        const geometry = await page.evaluate(() => {
          const rect = selector => {
            const {left, top, right, bottom, width, height} = document.querySelector(selector).getBoundingClientRect();
            return {left, top, right, bottom, width, height};
          };
          const targets = [...document.querySelectorAll('#touch-controls [data-touch-button], #touch-controls [data-touch-stick]')];
          const actionButtons = [...document.querySelectorAll('#toolbar-actions > button')]
            .filter(button => !button.hidden && getComputedStyle(button).display !== 'none');
          const buttonRects = actionButtons.map(button => {
            const {left, top, right, bottom} = button.getBoundingClientRect();
            return {left, top, right, bottom};
          });
          const buttonsOverlap = buttonRects.some((a, index) => buttonRects.slice(index + 1).some(b =>
            a.left < b.right - 1 && a.right > b.left + 1 && a.top < b.bottom - 1 && a.bottom > b.top + 1));
          return {toolbar: rect('#toolbar'), runtime: rect('#runtime-host'), canvas: rect('#canvas'), overlay: rect('#touch-controls'),
            buttonsOverlap,
            targets: targets.map(element => {
              const r = element.getBoundingClientRect();
              const hit = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
              return {visible: r.width > 0 && r.height > 0, reachable: hit === element || element.contains(hit)};
            }),
            pageWidth: document.documentElement.scrollWidth};
        });
        assert(geometry.pageWidth <= width, `${name} toolbar does not overflow the viewport`);
        assert(!geometry.buttonsOverlap, `${name} toolbar buttons wrap without overlap`);
        assert(geometry.runtime.bottom <= geometry.toolbar.top + 1, `${name} toolbar stays below the game area`);
        assert(Math.abs(geometry.canvas.width / geometry.canvas.height - 4 / 3) < 0.01,
          `${name} keeps the original 4:3 game presentation`);
        assert(geometry.overlay.left >= geometry.runtime.left && geometry.overlay.right <= geometry.runtime.right &&
          geometry.overlay.top >= geometry.runtime.top && geometry.overlay.bottom <= geometry.runtime.bottom,
        `${name} touch controls stay inside the game area`);
        assert(geometry.targets.length > 0 && geometry.targets.every(target => target.visible && target.reachable),
          `${name} touch controls remain visible and hit-test reachable during CSS`);
        await shot(name);
      }
      await page.locator('#controls-open').click();
      await page.locator('#player-one-source').selectOption('keyboard');
      await page.locator('#controls-close').click();
      await page.waitForFunction(() => document.querySelector('#touch-controls').hidden);
      await page.setViewportSize({width: 1280, height: 960});
      report.css_after_import = await page.evaluate(() => ({
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        launch_calls: window.nativeLaunchCalls,
        loading_hidden: document.querySelector('#loading-panel').hidden,
        error_open: document.querySelector('#error-dialog').open,
      }));
      if (values.audio) {
        const activation = await page.evaluate(start => window.audioActivation.slice(start), audioActivationStart);
        assert(activation.some(record => record.gesture), 'AudioContext.resume must be initiated by the Choose file user gesture');
        assert(activation.some(record => record.after === 'running'), 'The browser audio context must be running after preparation');
        report.audio_activation = activation;
        if (!audioRecovery) report.audio_activation_recovery = 'Headless Chrome resumed audio from Choose file; no Play gesture was used.';
        await observePcm('css-first-entry', null);
      }
    });
    await check('pause/resume acknowledges the shared native owner', async () => {
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => document.querySelector('#pause-game').textContent === 'Resume' && !document.querySelector('#pause-game').disabled);
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_running()), 0);
      assert(await page.locator('#disc-selection-status').isHidden(), 'Pause does not restore the disc filename');
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => Module._melee_web_native_menu_running());
      await page.locator('#pause-game:not([disabled])').waitFor();
      assert(await page.locator('#disc-selection-status').isHidden(), 'Resume does not restore the disc filename');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
    });
    await check('ordinary B0XX keyboard enters original SSS and cancels back to CSS', async () => {
      await page.waitForTimeout(1200); await press('7'); await phase(3);
      await page.waitForTimeout(700);
      await press('o'); await phase(1);
    });

    const closeObservedContexts = async (contextIds, fromEvent) => {
      assert(contextIds.length > 0, 'The production audio session must create an AudioContext');
      const closed = () => {
        const closingEvents = audioEvents.slice(fromEvent);
        return contextIds.every(id => closingEvents.some(row =>
          (row.event === 'contextChanged' && row.data.context.contextId === id &&
            row.data.context.contextState === 'closed') ||
          (row.event === 'contextWillBeDestroyed' && row.data.contextId === id)));
      };
      const deadline = Date.now() + 5000;
      while (!closed() && Date.now() < deadline)
        await new Promise(resolve => setTimeout(resolve, 50));
      assert(closed(), 'Eject must close every prior AudioContext before the new document is used');
    };
    const activeAudioContextIds = () => [...activeDocumentAudioContexts];
    const ejectAndReimport = async label => {
      const contextIds = activeAudioContextIds();
      const eventOffset = audioEvents.length;
      await captureUnload();
      await collectViolations();
      try { await driver.unload(); }
      catch (error) {
        const attempted = await page.evaluate(() => {
          try { return JSON.parse(window.name || 'null'); } catch { return null; }
        }).catch(() => null);
        throw Error(`${label} Eject failed before reload: ${JSON.stringify(attempted)}; ${error.message}`);
      }
      assert.deepEqual(await assertUnloadCompleted(),
        {result: 1, message: 'Native menus unloaded.', phase: 0, running: 0});
      if (values.audio) await closeObservedContexts(contextIds, eventOffset);
      assert.equal(await page.evaluate(() => typeof window.nativeLaunchCalls), 'undefined');
      assert(await page.locator('#start-game').isDisabled());
      assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
      await armAudioActivationObserver();
      await armLaunchObserver();
      await selectDisc(values.disc);
      if (await waitForCssOrAudioRecovery())
        report[`${label}_audio_activation_recovery`] = 'A separate Play gesture was required after Eject/reimport.';
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'A fresh document launches its selected disc once');
      assert(await page.locator('#disc-selection-status').isHidden(), 'Reimport clears its filename after start');
      assert.equal(await page.locator('#disc-selection-status').textContent(), '');
      await shot('css-after-reselection');
      report.css_after_reselection = await page.evaluate(() => ({
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        launch_calls: window.nativeLaunchCalls,
        loading_hidden: document.querySelector('#loading-panel').hidden,
      }));
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_phase()), 1,
        'Owned-disc reimport must start directly in original CSS');
      assert(await page.locator('#loading-panel').isHidden());
      if (values.audio) await observePcm(`${label}-css-after-reimport`, null);
      await shot(`${label}-css-after-reimport`);
      report.eject_reimport ||= [];
      report.eject_reimport.push({screen: label, native_unload: 'success before reload', css_first_start: true,
        prior_audio_contexts_closed: contextIds.length});
    };

    await check('Eject from Main completes native cleanup before disc reimport', async () => {
      await waitForNativeScene('Original character select');
      await page.waitForTimeout(500);
      await driver.pressChord(['q', '9', '7']);
      await waitForNativeScene('Original main menu');
      if (values.audio) await observePcm('main-before-eject', await audioTrace());
      await shot('main-before-eject');
      await ejectAndReimport('main');
    });

    await check('Eject from Title completes native cleanup before disc reimport', async () => {
      await page.waitForTimeout(500);
      await driver.pressChord(['q', '9', '7']);
      await waitForNativeScene('Original main menu');
      await page.waitForTimeout(900);
      await press('o');
      await waitForNativeScene('Original title');
      if (values.audio) await observePcm('title-before-eject', await audioTrace());
      await shot('title-before-eject');
      await ejectAndReimport('title');
    });

    await check('retail CSS to Main to Title to Main to Versus to CSS route repeats twice', async () => {
      for (let cycle = 1; cycle <= 2; cycle++) {
        await waitForNativeScene('Original character select');
        await page.waitForTimeout(450);
        await driver.pressChord(['q', '9', '7']);
        await waitForNativeScene('Original main menu');
        await page.waitForTimeout(900);
        if (values.audio) await observePcm(`route-${cycle}-main`, await audioTrace());
        await shot(`route-${cycle}-main`);
        await press('o');
        await waitForNativeScene('Original title');
        await page.waitForTimeout(500);
        if (values.audio) await observePcm(`route-${cycle}-title`, await audioTrace());
        await shot(`route-${cycle}-title`);
        await press('7');
        await waitForNativeScene('Original main menu');
        await page.waitForTimeout(700);
        if (values.audio) await observePcm(`route-${cycle}-main-after-title`, await audioTrace());
        await shot(`route-${cycle}-main-after-title`);
        await press('3');
        await page.waitForTimeout(250);
        await shot(`route-${cycle}-versus-choice`);
        await press('m');
        await page.waitForTimeout(750);
        let routeState = await readNativeMenuState();
        if (isTimingPaused(routeState)) {
          await resumeAfterTimingPause(`route-${cycle}-versus-selection`);
          routeState = await readNativeMenuState();
        }
        let scene = routeState.message;
        if (scene === 'Original main menu') {
          await shot(`route-${cycle}-versus-menu`);
          await press('m');
          await waitForNativeScene('Original character select');
        } else assert.equal(scene, 'Original character select',
          `Main's original Versus selection returned unsupported screen ${scene}`);
        if (values.audio) await observePcm(`route-${cycle}-css-return`, await audioTrace());
        await shot(`route-${cycle}-css-return`);
        const route = {cycle, sequence: ['Original character select', 'Original main menu',
          'Original title', 'Original main menu', 'Original Versus selection',
          'Original character select'], final_phase: await page.evaluate(() => Module._melee_web_native_menu_phase())};
        assert.equal(route.final_phase, 1);
        report.menu_routes ||= [];
        report.menu_routes.push(route);
      }
    });

    await check('CSS to SSS to supported Mario/Final Destination match to Results to CSS after menu route', async () => {
      await page.waitForTimeout(900);
      await press('7');
      await phase(3);
      if (values.audio) await observePcm('routed-sss', await audioTrace());
      await shot('routed-sss');
      await page.waitForTimeout(750);
      await driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
      await driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
      await shot('routed-final-destination');
      await press('m');
      await phase(7);
      if (values.audio) await observePcm('routed-match', await audioTrace());
      await shot('routed-match');
      await page.waitForTimeout(5000);
      await press('7');
      await page.waitForTimeout(700);
      await driver.pressChord(['q', '9', 'm', '7'], {holdMs: 250, releaseMs: 200});
      await phase(8);
      if (values.audio) await observePcm('routed-results', await audioTrace());
      await shot('routed-results');
      for (let confirmation = 0; confirmation < 8; confirmation++) {
        if (await page.evaluate(() => Module._melee_web_native_menu_phase()) !== 8) break;
        await phase(8);
        await driver.pressChord(['7'], {holdMs: 120, releaseMs: 1380});
      }
      let currentPhase = await page.evaluate(() => Module._melee_web_native_menu_phase());
      for (let confirmation = 0; currentPhase === 9 && confirmation < 120; confirmation++) {
        await phase(9);
        await driver.pressChord(['7'], {holdMs: 120, releaseMs: 380});
        currentPhase = await page.evaluate(() => Module._melee_web_native_menu_phase());
      }
      await phase(1);
      await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
      if (values.audio) await observePcm('routed-css-after-results', await audioTrace());
      await shot('routed-css-after-results');
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_phase()), 1);
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_running()), 1);
    });

    await check('CSS Eject still retires the original session', async () => {
      await page.evaluate(() => { window.releaseOldDocumentMarker = true; });
      await captureUnload();
      await collectViolations();
      await driver.unload();
      await assertUnloadCompleted();
      assert.equal(await page.evaluate(() => !!window.releaseOldDocumentMarker), false);
      assert(await page.locator('#start-game').isDisabled());
      report.css_eject = 'Native unload returned success, phase/running were zero before reload.';
    });
    if (values.audio) {
      const created = audioEvents.filter(row => row.event === 'contextCreated');
      assert(created.length >= 4,
        'The lifecycle creates audio contexts for invalid-selection recovery and public sessions');
      assert(created.every(row => row.data.context.sampleRate === 32000));
      report.audio = 'The early-invalid, invalid-recovery and two playable documents each create one 32 kHz context; actual activation state is recorded above. PCM and match transitions are checked by the separate audio lifecycle test; no fidelity claim.';
    } else {
      assert.deepEqual(audioEvents, [], 'The audio-disabled public profile must never create a Web Audio context');
      report.audio = 'Audio explicitly disabled. No Web Audio contexts were created during import, menus, pause/resume or second launch. No audio fidelity claim.';
    }
  } else {
    report.disc = 'Not supplied; native import, menus and successful auto-launch not exercised.';
    if (values.audio) {
      const created = audioEvents.filter(row => row.event === 'contextCreated');
      assert(created.length >= 1, 'Choosing a file must construct the audio context in the real browser');
      assert.equal(created[0].data.context.sampleRate, 32000);
      report.audio = 'The real browser created one 32 kHz Web Audio context from the Choose file path; activation state is recorded. No owned disc was supplied, so CSS launch, audible output and PCM were not exercised.';
    } else report.audio = 'Audio disabled in this public profile; no owned disc supplied.';
  }
  await check('legal pages use their readable document stylesheet and serve full notices', async () => {
    await collectViolations();
    for (const route of ['/terms', '/privacy', '/copyright', '/notices']) {
      const response = await page.goto(origin + route, {waitUntil: 'networkidle'});
      assert.equal(response.status(), 200);
      assert.match(await page.locator('link[rel="stylesheet"]').getAttribute('href'), /^\/assets\/site\.[a-f0-9]+\.css$/);
      assert.equal(await page.evaluate(() => getComputedStyle(document.body).backgroundColor), 'rgb(255, 255, 255)');
      assert(await page.locator('h1').isVisible()); await collectViolations();
    }
    await shot('legal');
    await page.setViewportSize({width: 390, height: 844});
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await shot('legal-mobile');
    const notices = await page.request.get(origin + '/licenses/runtime-third-party.txt');
    assert.equal(notices.status(), 200); assert.match(await notices.text(), /Permission is hereby granted/);
  });
  await check('keyboard-only session persists its preferences; no application upload or background connections', async () => {
    const storage = await page.evaluate(async () => ({local: Object.keys(localStorage), session: Object.keys(sessionStorage),
      indexed: await indexedDB.databases(), caches: await caches.keys(), workers: (await navigator.serviceWorker.getRegistrations()).length}));
    assert.deepEqual(storage, {local: ['melee-prototype-keyboard-v1'], session: [], indexed: [], caches: [], workers: 0});
    assert.equal((await context.cookies()).length, 0); report.storage = storage;
    await collectViolations();
    assert.deepEqual(violations, []); assert.deepEqual(errors, []); assert.deepEqual(sockets, []);
    for (const request of requests) {
      const url = new URL(request.url);
      assert.equal(url.origin, origin); assert.equal(request.method, 'GET'); assert.equal(request.body, null);
      assert.equal(url.search, '');
      if (!values.audio) assert.doesNotMatch(url.pathname, /dsp-coefficients|runtime-audio|audio-worklet|audio-ring/);
      assert(['/', '/terms', '/privacy', '/copyright', '/notices'].includes(url.pathname) ||
        /^\/runtime\/[a-f0-9]+\/[a-z0-9/_.-]+$/i.test(url.pathname) || /^\/assets\/site\.[a-f0-9]+\.css$/.test(url.pathname), url.pathname);
    }
    report.requests = requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  });
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = error.message;
  if (error.diagnostics) report.driverFailure = {step:error.step, ...error.diagnostics};
  report.state = await page.evaluate(() => ({status: document.querySelector('#status')?.textContent,
    error: document.querySelector('#error')?.textContent,
    disc: document.querySelector('#disc-selection-status')?.textContent,
    loading: {hidden: document.querySelector('#loading-panel')?.hidden,
      label: document.querySelector('#loading-label')?.textContent,
      detail: document.querySelector('#loading-detail')?.textContent},
    start_disabled: document.querySelector('#start-game')?.disabled ?? null,
    pause_disabled: document.querySelector('#pause-game')?.disabled ?? null,
    native_launch_calls: window.nativeLaunchCalls ?? null,
    audio_activation: window.audioActivation ?? null,
    native: window.Module?._melee_web_native_menu_message ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
    native_phase: window.Module?._melee_web_native_menu_phase?.() ?? null,
    native_running: window.Module?._melee_web_native_menu_running?.() ?? null,
    native_cache_idle: window.Module?._melee_web_native_menu_cache_idle?.() ?? null,
    pipeline_preparation: window.Module?.pipelinePreparation || null,
    audio: window.audioPreviewTrace?.snapshot?.() || null})).catch(() => null);
  await shot('failure').catch(() => {}); throw error;
} finally {
  report.errors = errors; report.csp = violations; report.audioEvents = audioEvents;
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  driver.dispose(); await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
