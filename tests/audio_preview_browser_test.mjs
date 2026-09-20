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
import {loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({
  options: Object.fromEntries(['url', 'playwright', 'disc', 'out'].map(name => [name, {type: 'string'}])),
});
if (!values.url || !values.disc || !values.out) {
  throw Error('Use --url ORIGIN --disc AUTHORIZED_DISC --out LOCAL_DIR [--playwright PACKAGE_DIR]');
}

const {chromium, browser: launchOptions} = await loadBrowserTools(values.playwright);
await fs.mkdir(values.out, {recursive: true});
const browser = await chromium.launch({...launchOptions, headless: false, chromiumSandbox: true});
const context = await browser.newContext({viewport: {width: 1280, height: 960}});
const page = await context.newPage();
const origin = new URL(values.url).origin;
const requests = [], errors = [], violations = [], sockets = [], audioEvents = [];
const report = {
  schema: 'webmelee-audio-preview-browser-v1',
  browser: browser.version(),
  scope: 'Authorized local disc through original CSS, SSS and supported Mario/Final Destination match; Web Audio lifecycle and PCM transport only. No long replay or performance claim.',
  checks: [],
  audio: {phases: {}, cdp: []},
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
page.on('requestfailed', request => errors.push(`request failed: ${request.method()} ${request.url()} ${request.failure()?.errorText || ''}`));
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

try {
  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  assert.equal(response.headers()['cross-origin-opener-policy'], 'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'], 'require-corp');
  await driver.waitForImport();

  await check('headed preview startup and reviewed native artifact names', async () => {
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#end-session').isDisabled());
    assert.equal(await page.locator('iframe,h1,header,footer,article').count(), 0);
    assert.equal(await page.evaluate(() => {
      const module = globalThis.Module;
      return typeof module?.['_melee_web_native_menu_diagnostics'];
    }), 'undefined',
      'The browser check must not depend on diagnostic native exports');
    // Match the public player's ordinary recipe: configure B0XX through the
    // visible controls before using its Start key on the original CSS.
    await page.locator('#controls-open').click();
    await page.locator('#keyboard-layout').selectOption('boxx');
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.activeElement?.id === 'canvas');
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    await screenshot('ready');
  });

  await check('authorized-disc import and original CSS emits nonzero PCM', async () => {
    await selectDisc(values.disc);
    await driver.waitForStart();
    assert(await page.locator('#error-dialog').isHidden());
    await driver.launch();
    assert(await page.locator('#loading-panel').isHidden(), 'Loading feedback must retire before interactive CSS');
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
    await page.locator('#pause-game:not([disabled])').waitFor();
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
    await page.waitForTimeout(700);
    await press('m');
    await phase(7);
    const before = await trace();
    await observeAudio('match', before);
    await screenshot('match');
  });

  await check('Eject destroys Web Audio and reloads the player document', async () => {
    const contextDestroyEvents = audioEvents.filter(row => row.event === 'contextWillBeDestroyed').length;
    const navigationCount = report.navigations || 0;
    await collectViolations();
    await driver.unload();
    assert((report.navigations || 0) > navigationCount, 'Eject must reload the player document');
    await driver.waitForImport();
    const fresh = await trace();
    assert(fresh && fresh.contexts.length === 0, 'Reloaded player must not retain the old AudioContext');
    assert(audioEvents.filter(row => row.event === 'contextWillBeDestroyed').length > contextDestroyEvents,
      'Eject must destroy the prior AudioContext');
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
    audio: window.audioPreviewTrace?.snapshot?.() || null,
  })).catch(() => null);
  await screenshot('failure').catch(() => {});
  throw error;
} finally {
  await collectViolations().catch(() => {});
  report.errors = errors;
  report.csp = violations;
  report.audio.cdp = audioEvents;
  report.requests = report.requests || requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  driver.dispose();
  await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
