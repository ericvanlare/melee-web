// SPDX-License-Identifier: MIT
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../../scripts/browser_tools.mjs';

const {values} = parseArgs({options: {
  url: {type: 'string'},
  out: {type: 'string'},
  playwright: {type: 'string'},
  'target-file': {type: 'string'},
  'ready-file': {type: 'string'},
  count: {type: 'string'},
  timeout: {type: 'string'},
}});
if (!values.url || !values.out || !values.playwright || !values['target-file'] ||
    !values['ready-file'] || !values.count)
  throw Error('Use --url LOOPBACK_ORIGIN --out NEW_DIRECTORY --playwright PACKAGE_DIR --target-file PATH --ready-file PATH --count N');

const frameCount = Number(values.count);
const timeout = Number(values.timeout ?? '30000');
if (!Number.isInteger(frameCount) || frameCount < 8 || frameCount > 64 ||
    !Number.isInteger(timeout) || timeout < 1 || timeout > 120000)
  throw Error('frame count or timeout is outside the local probe limits');

const out = path.resolve(values.out);
const targetFile = path.resolve(values['target-file']);
const readyFile = path.resolve(values['ready-file']);
await fs.mkdir(out, {recursive: false, mode: 0o700});
const screenshotPath = path.join(out, 'browser-transport.png');
const evidencePath = path.join(out, 'browser-evidence.json');
const errors = [];
const pageErrors = [];
let browser;
let page;
let report = {
  schema: 'melee-web-slippi-browser-transport-v1',
  result: 'incomplete',
  headless: true,
  audible_output: false,
  requested_frame_count: frameCount,
  screenshot: path.basename(screenshotPath),
};

try {
  const {chromium, browser: installedBrowser, browserPath, playwrightPath} =
    await loadBrowserTools(values.playwright);
  browser = await chromium.launch(browserLaunchOptions({
    ...installedBrowser,
    args: [...(installedBrowser.args || []), '--disable-background-networking',
      '--disable-component-update', '--disable-sync', '--no-first-run',
      '--no-default-browser-check'],
  }, {timeout}));
  report.browser = {
    executable: path.basename(browserPath),
    version: await browser.version(),
    playwright: path.basename(playwrightPath),
  };
  const context = await browser.newContext({viewport: {width: 960, height: 640}});
  page = await context.newPage();
  page.on('console', message => {
    if (message.type() === 'error') errors.push(message.text());
  });
  page.on('pageerror', error => pageErrors.push(String(error)));
  const origin = new URL(values.url).origin;
  if (origin !== values.url.replace(/\/$/, '') || !origin.startsWith('http://127.0.0.1:'))
    throw Error('browser transport page must use its exact IPv4 loopback origin');
  const response = await page.goto(values.url, {waitUntil: 'domcontentloaded', timeout});
  if (!response || response.status() !== 200 || await page.title() !== 'Local Slippi transport probe')
    throw Error('loopback browser transport page did not load the expected document');
  report.origin = origin;
  await page.waitForFunction(() => Boolean(window.browserTransport?.state?.sessionId), null,
    {timeout});

  const gpu = await page.evaluate(() => {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
    return gl ? {
      available: true,
      version: gl.getParameter(gl.VERSION),
      vendor: gl.getParameter(gl.VENDOR),
      renderer: gl.getParameter(gl.RENDERER),
    } : {available: false};
  });
  if (!gpu.available)
    throw Error('headless Chrome could not create a WebGL context for the rendered browser check');
  const ready = await page.evaluate(() => ({
    sessionId: window.browserTransport.state.sessionId,
    origin: location.origin,
  }));
  await fs.writeFile(readyFile, `${JSON.stringify({
    schema: 'melee-web-slippi-browser-transport-ready-v1',
    result: 'ready',
    websocket_session_id: ready.sessionId,
    origin: ready.origin,
    gpu,
  }, null, 2)}\n`, {mode: 0o600, flag: 'wx'});

  const targetDeadline = Date.now() + timeout;
  let target;
  while (Date.now() < targetDeadline) {
    try {
      target = JSON.parse(await fs.readFile(targetFile, 'utf8'));
      break;
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error;
      await new Promise(resolve => setTimeout(resolve, 10));
    }
  }
  if (!target) throw Error('runner did not provide a live Slippi PAD frame target in time');
  const firstFrame = target.first_frame;
  if (!Number.isInteger(firstFrame) || firstFrame < 1 ||
      firstFrame + frameCount - 1 > 1_000_000 || target.frame_count !== frameCount)
    throw Error('runner provided an invalid live Slippi PAD frame target');

  const frames = Array.from({length: frameCount}, (_, index) => ({
    frame: firstFrame + index,
    // This is a known eight-byte Slippi sample with the A bit set. The bridge
    // transports it unchanged; it does not decode or synthesize game input.
    padHex: '01007f0000000000',
  }));
  await page.evaluate(input => window.browserTransport.sendFrames(input), frames);
  await page.waitForFunction(expected =>
    window.browserTransport.state.events.some(event =>
      event.event === 'pad_queued' && event.count === expected), frameCount, {timeout});
  await page.waitForFunction(expected => {
    const state = window.browserTransport.state;
    const applied = new Set(state.appliedFrames);
    return expected.every(frame => applied.has(frame));
  }, frames.map(row => row.frame), {timeout});
  await page.waitForFunction(target =>
    window.browserTransport.state.peerFrames.includes(target), firstFrame, {timeout});

  const browserState = await page.evaluate(() => {
    return {
      sessionId: window.browserTransport.state.sessionId,
      events: window.browserTransport.state.events,
      appliedFrames: window.browserTransport.state.appliedFrames,
      peerFrames: window.browserTransport.state.peerFrames,
      status: document.getElementById('status').dataset.state,
    };
  });
  if (errors.length || pageErrors.length)
    throw Error('browser page reported console or runtime errors');
  await page.screenshot({path: screenshotPath, fullPage: true});
  report = {
    ...report,
    result: 'passed',
    first_frame: firstFrame,
    websocket_session_id: browserState.sessionId,
    browser_applied_frames: browserState.appliedFrames,
    peer_frames_received: browserState.peerFrames,
    peer_frame_payloads: browserState.events.filter(event => event.event === 'peer_pad'),
    relay_applied_receipts: browserState.events.filter(event => event.event === 'pad_applied'),
    page_state: browserState.status,
    gpu,
    console_errors: errors,
    page_errors: pageErrors,
  };
} catch (error) {
  report.result = 'failed';
  report.failure = String(error?.stack ?? error);
  report.console_errors = errors;
  report.page_errors = pageErrors;
  if (page) {
    try { await page.screenshot({path: screenshotPath, fullPage: true}); }
    catch { /* Keep the browser failure as the primary evidence. */ }
  }
  await fs.writeFile(evidencePath, `${JSON.stringify(report, null, 2)}\n`, {mode: 0o600});
  throw error;
} finally {
  if (browser) await browser.close();
}

await fs.writeFile(evidencePath, `${JSON.stringify(report, null, 2)}\n`, {mode: 0o600});
console.log(JSON.stringify({result: report.result, applied: report.browser_applied_frames?.length,
  peerFrames: report.peer_frames_received?.length, gpu: report.gpu?.renderer}));
