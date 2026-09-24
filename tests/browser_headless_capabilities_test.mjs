/**
 * Bounded, synthetic headless-browser capability probe.
 *
 * This is agent-utility evidence only. It exercises browser primitives through
 * a real HTTP server and does not load the Melee runtime or make a gameplay,
 * timing, or pixel-equivalence claim.
 */
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({
  options: {
    out: {type: 'string'},
    playwright: {type: 'string'},
    timeout: {type: 'string', default: '15000'},
  },
});
if (!values.out) {
  throw Error('Use --out NEW_DIRECTORY [--playwright PACKAGE_DIR] [--timeout MILLISECONDS]');
}
const timeoutMs = Number(values.timeout);
if (!Number.isInteger(timeoutMs) || timeoutMs < 1000 || timeoutMs > 60000) {
  throw Error('--timeout must be an integer between 1000 and 60000 milliseconds');
}
const overallWatchdogMs = Math.min(120000, Math.max(30000, timeoutMs * 4));

const output = path.resolve(values.out);
await fs.mkdir(path.dirname(output), {recursive: true});
await fs.mkdir(output);

const uploadText = 'headless upload fixture / browser utility';
const downloadBytes = Buffer.from('headless download fixture\0\x01\x02\n', 'utf8');
const fixture = () => `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="icon" href="data:,">
  <title>Headless browser capability fixture</title>
  <style>
    body { font: 16px system-ui, sans-serif; margin: 24px; }
    canvas { border: 1px solid #888; display: block; margin: 12px 0; }
    #status { min-height: 1.5em; }
  </style>
</head>
<body>
  <h1>Headless browser capability fixture</h1>
  <p id="status">Loading fixture…</p>
  <canvas id="webgpu-canvas" width="4" height="4"></canvas>
  <label>Keyboard target <input id="typing-target" autocomplete="off"></label>
  <button id="focus-target" type="button">Mouse focus target</button>
  <button id="file-button" type="button">Choose fixture file</button>
  <input id="file-input" type="file" hidden>
  <a id="download-link" href="/download.bin" download="headless-download.bin">Download fixture</a>
  <pre id="network-result">pending</pre>
  <pre id="interaction-result">pending</pre>
  <script>
    window.__headlessCapability = {
      keys: [], pointer: [], upload: null, network: null,
    };
    const state = window.__headlessCapability;
    const focusTarget = document.querySelector('#focus-target');
    const fileButton = document.querySelector('#file-button');
    const fileInput = document.querySelector('#file-input');
    const status = document.querySelector('#status');
    document.addEventListener('keydown', event => state.keys.push('down:' + event.key));
    document.addEventListener('keyup', event => state.keys.push('up:' + event.key));
    focusTarget.addEventListener('pointerdown', () => state.pointer.push('pointerdown'));
    focusTarget.addEventListener('click', () => state.pointer.push('click'));
    fileButton.addEventListener('click', () => fileInput.click());
    fileInput.addEventListener('change', async () => {
      const file = fileInput.files[0];
      if (!file) return;
      state.upload = {name: file.name, type: file.type, size: file.size, text: await file.text()};
      status.textContent = 'Upload received';
    });
    fetch('/network.json').then(response => {
      if (!response.ok) throw Error('network fixture status ' + response.status);
      return response.json();
    }).then(value => {
      state.network = value;
      document.querySelector('#network-result').textContent = JSON.stringify(value);
      status.textContent = 'Network fixture loaded';
    }).catch(error => { status.textContent = 'Network fixture failed: ' + error.message; });
    console.info('headless-capability-console-marker');
    document.querySelector('#interaction-result').textContent =
      'fixture-dom-ready:' + document.querySelector('h1').textContent;
  </script>
</body>
</html>`;

const server = http.createServer((request, response) => {
  const requestUrl = new URL(request.url, 'http://127.0.0.1');
  const headers = {
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Resource-Policy': 'same-origin',
    'Cache-Control': 'no-store',
  };
  if (requestUrl.pathname === '/') {
    response.writeHead(200, {...headers, 'Content-Type': 'text/html; charset=utf-8'});
    response.end(fixture());
  } else if (requestUrl.pathname === '/network.json') {
    response.writeHead(200, {...headers, 'Content-Type': 'application/json'});
    response.end(JSON.stringify({networkProbe: 'same-origin-json', value: 17}));
  } else if (requestUrl.pathname === '/download.bin') {
    response.writeHead(200, {
      ...headers,
      'Content-Type': 'application/octet-stream',
      'Content-Disposition': 'attachment; filename="headless-download.bin"',
      'Content-Length': downloadBytes.length,
    });
    response.end(downloadBytes);
  } else {
    response.writeHead(404, {...headers, 'Content-Type': 'text/plain'});
    response.end('not found');
  }
});

const checks = [];
const pass = name => checks.push(name);
const report = {
  schema: 'melee-web-headless-browser-capabilities-v1',
  scope: 'Synthetic browser capabilities only; no runtime, disc, gameplay, timing, or equivalence claim',
  result: 'fail',
  checks,
  timeoutMs,
  overallWatchdogMs,
};
let browser;
let page;
let watchdogTimer;
let watchdogError;
const consoleMessages = [];
const pageErrors = [];
const requests = [];
const responses = [];
const failedRequests = [];

async function retainFailureArtifact(error) {
  report.failure = String(error?.stack || error);
  if (page) {
    try {
      await page.screenshot({path: path.join(output, 'failure.png'), fullPage: true});
    } catch {}
    try {
      await fs.writeFile(path.join(output, 'failure-page.txt'), await page.locator('body').innerText());
    } catch {}
  }
  await fs.writeFile(path.join(output, 'failure.txt'), report.failure + '\n');
}

function expectedPixel(format, color) {
  const rgb = color.slice(0, 3).map(channel => Math.round(channel * 255));
  return format.startsWith('bgra') ? [rgb[2], rgb[1], rgb[0], 255] : [...rgb, 255];
}

function assertPixel(actual, expected, label) {
  assert.equal(actual.length, 4, `${label} has four readback channels`);
  for (let index = 0; index < expected.length; index++) {
    assert.ok(Math.abs(actual[index] - expected[index]) <= 2,
      `${label}[${index}] expected ${expected[index]}, got ${actual[index]}`);
  }
}

async function inspectChromeGpuPage() {
  const gpuPage = await browser.newPage();
  try {
    await gpuPage.goto('chrome://gpu', {waitUntil: 'domcontentloaded', timeout: timeoutMs});
    await gpuPage.waitForFunction(() => Boolean(
      document.querySelector('info-view')?.shadowRoot?.querySelector('#content tr')),
    null, {timeout: timeoutMs});
    return await gpuPage.evaluate(() => {
      const root = document.querySelector('info-view')?.shadowRoot;
      const rows = [...(root?.querySelectorAll('tr') || [])].map(row => {
        const cells = [...row.querySelectorAll('td')].map(cell => cell.textContent.trim()
          .replace(/\s*:\s*$/, ''));
        return cells.length === 2 ? cells : null;
      }).filter(Boolean);
      const table = Object.fromEntries(rows);
      const features = [...(root?.querySelectorAll('li') || [])]
        .map(item => item.textContent.trim());
      const commandLine = table['Command Line'] || '';
      const hasFlag = flag => commandLine.split(/\s+/).includes(flag);
      return {
        available: true,
        webgpuFeature: features.find(value => value.includes('WebGPU:')) || null,
        skiaBackend: table['Skia Backend'] || null,
        displayType: table['Display type'] || null,
        gpu0: table.GPU0 || null,
        effectiveFlags: {
          headless: hasFlag('--headless'),
          muteAudio: hasFlag('--mute-audio'),
          backgroundTimerThrottlingDisabled: hasFlag('--disable-background-timer-throttling'),
          rendererBackgroundingDisabled: hasFlag('--disable-renderer-backgrounding'),
          backgroundOccludedWindowsDisabled: hasFlag('--disable-backgrounding-occluded-windows'),
          unsafeSwiftShaderEnabled: hasFlag('--enable-unsafe-swiftshader'),
        },
      };
    });
  } catch (error) {
    return {available: false, error: error.message};
  } finally {
    await gpuPage.close();
  }
}

try {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const {chromium, browser: launchOptions, browserPath, playwrightPath} =
    await loadBrowserTools(values.playwright);
  report.playwrightPath = playwrightPath;
  report.browserExecutable = path.basename(browserPath);
  // Keep this explicit: the probe must never open a visible browser window.
  const browserLaunch = {
    ...launchOptions,
    headless: true,
    chromiumSandbox: true,
    timeout: timeoutMs,
    // Do not enable unsafe SwiftShader fallback. Record the adapter actually used.
    ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
  };
  browser = await chromium.launch(browserLaunch);
  report.browserVersion = browser.version();
  report.watchdog = {timeoutMs: overallWatchdogMs, fired: false};
  watchdogTimer = setTimeout(() => {
    watchdogError = Error(`Overall browser capability watchdog expired after ${overallWatchdogMs} ms`);
    report.watchdog = {timeoutMs: overallWatchdogMs, fired: true, error: watchdogError.message};
    void browser?.close().catch(() => {});
  }, overallWatchdogMs);
  report.launch = {
    requested: {
      executable: path.basename(browserPath),
      headless: true,
      chromiumSandbox: true,
      args: browserLaunch.args || [],
      ignoreDefaultArgs: browserLaunch.ignoreDefaultArgs || [],
      channel: browserLaunch.channel || null,
      proxy: browserLaunch.proxy ? 'configured' : null,
    },
    context: {viewport: {width: 900, height: 700}},
    effectiveDefaults: 'Chrome/Playwright defaults; effective flags are recorded from chrome://gpu when available',
  };
  page = await browser.newPage({viewport: {width: 900, height: 700}});
  page.setDefaultTimeout(timeoutMs);
  page.setDefaultNavigationTimeout(timeoutMs);
  page.on('console', message => consoleMessages.push({type: message.type(), text: message.text()}));
  page.on('pageerror', error => pageErrors.push({message: error.message, stack: error.stack || null}));
  page.on('request', request => requests.push({method: request.method(), path: new URL(request.url()).pathname}));
  page.on('response', response => responses.push({status: response.status(), path: new URL(response.url()).pathname}));
  page.on('requestfailed', request => failedRequests.push({
    method: request.method(), path: new URL(request.url()).pathname, failure: request.failure()?.errorText || null,
  }));

  const response = await page.goto(origin + '/', {waitUntil: 'networkidle'});
  assert.equal(response?.status(), 200);
  const responseHeaders = response.headers();
  assert.equal(responseHeaders['cross-origin-opener-policy'], 'same-origin');
  assert.equal(responseHeaders['cross-origin-embedder-policy'], 'require-corp');
  assert.equal(await page.evaluate(() => crossOriginIsolated), true);
  pass('real HTTP server publishes COOP/COEP and page is cross-origin isolated');

  await page.locator('h1').waitFor();
  await page.waitForFunction(() => window.__headlessCapability.network?.networkProbe === 'same-origin-json');
  const domInspection = await page.evaluate(() => ({
    title: document.title,
    heading: document.querySelector('h1')?.textContent,
    canvas: {width: document.querySelector('#webgpu-canvas').width, height: document.querySelector('#webgpu-canvas').height},
    secureContext: window.isSecureContext,
    isolated: window.crossOriginIsolated,
    network: window.__headlessCapability.network,
  }));
  assert.equal(domInspection.heading, 'Headless browser capability fixture');
  assert.deepEqual(domInspection.network, {networkProbe: 'same-origin-json', value: 17});
  assert.equal(domInspection.isolated, true);
  assert.equal(domInspection.secureContext, true);
  assert(requests.some(item => item.path === '/network.json' && item.method === 'GET'));
  assert(responses.some(item => item.path === '/network.json' && item.status === 200));
  pass('DOM/evaluate inspection and same-origin network request are observable');

  const webgpu = await page.evaluate(async () => {
    if (!navigator.gpu) return {available: false, reason: 'navigator.gpu unavailable'};
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) return {available: false, reason: 'requestAdapter returned null'};
    const adapterInfo = adapter.info || (typeof adapter.requestAdapterInfo === 'function'
      ? await adapter.requestAdapterInfo().catch(() => null) : null);
    const info = adapterInfo ? Object.fromEntries(
      ['vendor', 'architecture', 'device', 'description', 'isFallbackAdapter',
        'subgroupMinSize', 'subgroupMaxSize']
        .filter(key => adapterInfo[key] !== undefined).map(key => [key, adapterInfo[key]])) : null;
    const isFallbackAdapter = typeof adapterInfo?.isFallbackAdapter === 'boolean'
      ? adapterInfo.isFallbackAdapter
      : typeof adapter.isFallbackAdapter === 'boolean' ? adapter.isFallbackAdapter : null;
    const device = await adapter.requestDevice();
    const canvas = document.querySelector('#webgpu-canvas');
    const context = canvas.getContext('webgpu');
    const format = navigator.gpu.getPreferredCanvasFormat();
    if (!context || !format) return {available: false, reason: 'WebGPU canvas context unavailable', info};
    const usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC;
    const renderAndRead = async (width, height, color) => {
      canvas.width = width;
      canvas.height = height;
      context.configure({device, format, usage, alphaMode: 'opaque'});
      const texture = context.getCurrentTexture();
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginRenderPass({colorAttachments: [{
        view: texture.createView(),
        clearValue: {r: color[0], g: color[1], b: color[2], a: color[3]},
        loadOp: 'clear', storeOp: 'store',
      }]});
      pass.end();
      const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
      const buffer = device.createBuffer({
        size: bytesPerRow * height,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      });
      encoder.copyTextureToBuffer({texture}, {buffer, bytesPerRow, rowsPerImage: height},
        {width, height, depthOrArrayLayers: 1});
      device.queue.submit([encoder.finish()]);
      await device.queue.onSubmittedWorkDone();
      await buffer.mapAsync(GPUMapMode.READ);
      const bytes = new Uint8Array(buffer.getMappedRange()).slice();
      buffer.unmap();
      buffer.destroy();
      return {
        width, height, bytesPerRow,
        firstPixel: Array.from(bytes.slice(0, 4)),
        lastPixel: Array.from(bytes.slice(bytesPerRow * (height - 1), bytesPerRow * (height - 1) + 4)),
      };
    };
    const first = await renderAndRead(4, 4, [1, 0, 0, 1]);
    const resized = await renderAndRead(7, 5, [0, 0, 1, 1]);
    return {
      available: true,
      format,
      info,
      isFallbackAdapter,
      features: [...adapter.features].sort(),
      limits: {
        maxTextureDimension2D: adapter.limits.maxTextureDimension2D,
        maxBufferSize: adapter.limits.maxBufferSize,
      },
      first, resized,
    };
  });
  assert.equal(webgpu.available, true, webgpu.reason || 'WebGPU unavailable');
  assert.match(webgpu.format, /^(?:bgra|rgba)8unorm$/);
  assert.equal(webgpu.first.width, 4);
  assert.equal(webgpu.first.height, 4);
  assert.equal(webgpu.resized.width, 7);
  assert.equal(webgpu.resized.height, 5);
  assertPixel(webgpu.first.firstPixel, expectedPixel(webgpu.format, [1, 0, 0, 1]), 'initial framebuffer');
  assertPixel(webgpu.first.lastPixel, expectedPixel(webgpu.format, [1, 0, 0, 1]), 'initial framebuffer last pixel');
  assertPixel(webgpu.resized.firstPixel, expectedPixel(webgpu.format, [0, 0, 1, 1]), 'resized framebuffer');
  assertPixel(webgpu.resized.lastPixel, expectedPixel(webgpu.format, [0, 0, 1, 1]), 'resized framebuffer last pixel');
  report.webgpu = {
    adapterInfo: webgpu.info,
    isFallbackAdapter: webgpu.isFallbackAdapter,
    backendIdentity: {
      source: 'GPUAdapter.info/isFallbackAdapter plus chrome://gpu when available',
      reported: webgpu.info,
      isFallbackAdapter: webgpu.isFallbackAdapter,
      backendFieldExposed: Object.prototype.hasOwnProperty.call(webgpu.info || {}, 'backend'),
    },
    format: webgpu.format,
    features: webgpu.features,
    limits: webgpu.limits,
    framebuffers: [webgpu.first, webgpu.resized],
  };
  report.webgpu.browserGpuInternals = await inspectChromeGpuPage();
  report.webgpu.backendIdentity.chromeGpuPage = report.webgpu.browserGpuInternals;
  report.launch.effectiveFlags = report.webgpu.browserGpuInternals.effectiveFlags || null;
  pass('real WebGPU render, framebuffer resize, and GPU readback values validate');

  await page.locator('#typing-target').click();
  await page.keyboard.type('agent');
  const focusBox = await page.locator('#focus-target').boundingBox();
  assert(focusBox);
  await page.mouse.click(focusBox.x + focusBox.width / 2, focusBox.y + focusBox.height / 2);
  const interaction = await page.evaluate(() => ({
    value: document.querySelector('#typing-target').value,
    activeElement: document.activeElement?.id,
    keys: window.__headlessCapability.keys,
    pointer: window.__headlessCapability.pointer,
  }));
  assert.equal(interaction.value, 'agent');
  assert.equal(interaction.activeElement, 'focus-target');
  assert(interaction.keys.includes('down:a') && interaction.keys.includes('up:t'));
  assert.deepEqual(interaction.pointer.slice(-2), ['pointerdown', 'click']);
  pass('ordinary keyboard text entry, mouse click, and DOM focus are observable');

  const chooserPromise = page.waitForEvent('filechooser');
  await page.locator('#file-button').click();
  const chooser = await chooserPromise;
  await chooser.setFiles({name: 'headless-upload.txt', mimeType: 'text/plain', buffer: Buffer.from(uploadText)});
  await page.waitForFunction(expected => window.__headlessCapability.upload?.text === expected, uploadText);
  const upload = await page.evaluate(() => window.__headlessCapability.upload);
  assert.deepEqual(upload, {name: 'headless-upload.txt', type: 'text/plain', size: Buffer.byteLength(uploadText), text: uploadText});
  pass('Playwright filechooser upload reaches the page and file bytes validate');

  const downloadPromise = page.waitForEvent('download');
  await page.locator('#download-link').click();
  const download = await downloadPromise;
  const downloadPath = path.join(output, download.suggestedFilename());
  await download.saveAs(downloadPath);
  assert.deepEqual(await fs.readFile(downloadPath), downloadBytes);
  report.download = {path: path.relative(process.cwd(), downloadPath), bytes: downloadBytes.length};
  pass('Playwright download saves and byte contents validate');

  const expectedPageError = 'headless-capability-pageerror-marker';
  const pageErrorPromise = page.waitForEvent('pageerror', {
    predicate: error => error.message === expectedPageError,
    timeout: timeoutMs,
  });
  await page.evaluate(message => {
    setTimeout(() => { throw Error(message); }, 0);
  }, expectedPageError);
  const observedPageError = await pageErrorPromise;
  assert.equal(observedPageError.message, expectedPageError);
  assert(consoleMessages.some(item => item.text.includes('headless-capability-console-marker')));
  assert.deepEqual(pageErrors.map(item => item.message), [expectedPageError]);
  assert.deepEqual(consoleMessages.filter(item => item.type === 'error'), []);
  assert.deepEqual(failedRequests, []);
  pass('console and pageerror diagnostics are captured');

  const screenshotPath = path.join(output, 'capabilities.png');
  const screenshot = await page.screenshot({path: screenshotPath, fullPage: true});
  assert(screenshot.length > 100);
  const pngHeader = (await fs.readFile(screenshotPath)).subarray(0, 8);
  assert.deepEqual([...pngHeader], [137, 80, 78, 71, 13, 10, 26, 10]);
  report.screenshot = path.relative(process.cwd(), screenshotPath);
  pass('inspectable screenshot artifact retained');

  report.console = consoleMessages;
  report.pageErrors = pageErrors;
  report.requests = requests;
  report.responses = responses;
  report.failedRequests = failedRequests;
  report.result = 'pass';
} catch (error) {
  const failure = watchdogError || error;
  await retainFailureArtifact(failure);
  throw failure;
} finally {
  clearTimeout(watchdogTimer);
  report.console = consoleMessages;
  report.pageErrors = pageErrors;
  report.requests = requests;
  report.responses = responses;
  report.failedRequests = failedRequests;
  const cleanupErrors = [];
  try { await browser?.close(); }
  catch (error) { cleanupErrors.push('browser: ' + String(error)); }
  await new Promise(resolve => server.close(error => {
    if (error) cleanupErrors.push('server: ' + String(error));
    resolve();
  }));
  report.cleanup = {browserClosed: !browser?.isConnected(), serverClosed: !server.listening, errors: cleanupErrors};
  if (cleanupErrors.length) report.result = 'fail';
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  if (cleanupErrors.length) throw Error('Capability probe cleanup failed: ' + cleanupErrors.join('; '));
}

console.log(JSON.stringify({
  result: report.result,
  browser: report.browserVersion,
  checks: report.checks.length,
  screenshot: report.screenshot,
  webgpu: {
    format: report.webgpu?.format,
    isFallbackAdapter: report.webgpu?.isFallbackAdapter,
    adapterInfo: report.webgpu?.adapterInfo,
  },
}));
