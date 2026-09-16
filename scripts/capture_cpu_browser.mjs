#!/usr/bin/env node
/** Visible CPU match capture through the existing development runtime UI.
 * Retains partial traces and page failures. No CPU decisions are supplied.
 * Timing is reported by the host but is not admitted by this state run.
 */
import fs from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
import {createHash} from 'node:crypto';
import {installCpuBrowserModuleCapture} from './cpu_browser_module_capture.mjs';
const {values: options} = parseArgs({options: {
  ...Object.fromEntries(['url','disc','recipe','out','playwright'].map(name => [name, {type:'string'}])),
  timeout: {type:'string', default:'900000'},
}});
if (!options.url || !options.disc || !options.recipe || !options.out)
  throw Error('Use --url http://127.0.0.1:PORT/runtime.html --disc PATH --recipe PATH --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const url = new URL(options.url);
if (!['http:', 'https:'].includes(url.protocol) || !url.pathname.endsWith('/runtime.html'))
  throw Error('A real HTTP development runtime.html URL is required');
const timeout = Number(options.timeout);
if (!Number.isInteger(timeout) || timeout < 1000 || timeout > 3600000) throw Error('Invalid timeout');
await fs.mkdir(options.out, {recursive:false});
async function fileHash(filename) {
  const hash = createHash('sha256');
  for await (const bytes of createReadStream(filename)) hash.update(bytes);
  return hash.digest('hex');
}
await fs.writeFile(path.join(options.out, 'capture-config.json'), JSON.stringify({
  disc_image_sha256: await fileHash(options.disc),
  disc_image_bytes: (await fs.stat(options.disc)).size,
  recipe_sha256: await fileHash(options.recipe),
  url: options.url, visible_browser: true, mode: 'state_capture',
}, null, 2) + '\n');
const {chromium} = options.playwright
  ? await import(pathToFileURL(path.join(path.resolve(options.playwright), 'index.mjs')).href)
  : await import('playwright');
const browser = await chromium.launch({channel:'chrome', headless:false, chromiumSandbox:true});
await fs.writeFile(path.join(options.out, 'browser-identity.json'), JSON.stringify({
  browser: 'chrome', version: await browser.version(), visible_browser: true,
}, null, 2) + '\n');
const page = await browser.newPage({viewport:{width:1280,height:960}, deviceScaleFactor:1});
const errors = [], requests = [];
const servedArtifacts = {}, servedArtifactsAfter = {};
let complete = false;
const write = (name, value) => fs.writeFile(path.join(options.out, name), value);
page.on('pageerror', error => errors.push({kind:'page', message:error.stack || error.message}));
page.on('console', message => {
  if (message.type() === 'error') errors.push({kind:'console', message:message.text()});
});
page.on('response', response => {
  if (response.status() >= 400) errors.push({kind:'http', status:response.status(), url:response.url()});
});
page.on('request', request => {
  if (request.method() !== 'GET') requests.push({method:request.method(), url:request.url()});
});
async function readArtifacts(expected, output) {
  for (const [name, hash] of Object.entries(expected)) {
    if (path.basename(name) !== name) throw Error('Invalid runtime artifact name');
    // Read and hash the actual HTTP body inside the browser. CDP's response
    // body cache can evict module/duplicate-fetch bodies before Playwright
    // retrieves them; that transport failure is not a changed runtime.
    output[name] = await page.evaluate(async name => {
      const response = await fetch('./' + name, {cache:'no-store'});
      if (!response.ok) throw Error('Missing frozen artifact: ' + name);
      const digest = await crypto.subtle.digest('SHA-256', await response.arrayBuffer());
      return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
    }, name);
    if (output[name] !== hash) throw Error('Frozen HTTP artifact differs: ' + name);
  }
}
try {
  // Install before runtime.html loads. melee-runtime.mjs publishes Module just
  // before appending the generated Emscripten loader, so this observes the
  // callbacks at the boundary where the loader captures them.
  await page.addInitScript(installCpuBrowserModuleCapture);
  await page.addInitScript(() => {
    window.cpuObservationRows = [];
    window.cpuPreparationRows = [];
    window.meleeCpuObservation = text => {
      if (window.cpuObservationRows.length >= 72004) throw Error('CPU observation row bound exceeded');
      window.cpuObservationRows.push(text);
    };
    window.meleeCpuPreparationObservation = text => {
      if (window.cpuPreparationRows.length >= 14400) throw Error('CPU preparation row bound exceeded');
      window.cpuPreparationRows.push(text);
    };
  });
  const response = await page.goto(options.url);
  if (response.status() !== 200 || response.headers()['cross-origin-embedder-policy'] !== 'require-corp')
    throw Error('Runtime did not load over the isolated HTTP server');
  // prepare_prototype.py freezes the shared runtime and its imported modules.
  // Bind even modules that this particular replay never imports. These GETs
  // happen before disc preparation and outside the observed match interval.
  const inventory = await page.evaluate(async () => {
    const response = await fetch('./prototype-build.json');
    if (!response.ok) throw Error('Use a frozen prepare_prototype.py output');
    return response.json();
  });
  if (inventory.schema !== 'melee-web-prototype-preview-v1' || !inventory.runtime_sha256)
    throw Error('Missing frozen shared-runtime inventory');
  await readArtifacts(inventory.runtime_sha256, servedArtifacts);
  await page.locator('#disc:not([disabled])').waitFor({timeout:60000});
  await page.locator('#disc').setInputFiles(options.disc);
  await page.locator('#launch:not([disabled])').waitFor({timeout:60000});
  await page.locator('summary').filter({hasText:'Diagnostics'}).click();
  await page.locator('#retail-replay-mode').selectOption('state');
  await page.locator('#retail-replay-file').setInputFiles(options.recipe);
  await page.locator('#retail-replay-start:not([disabled])').click();
  // Clicking diagnostics scrolls below the canvas. Keep source presentation
  // visible; full-page screenshots can resize the live WebGPU surface.
  await page.locator('#canvas').scrollIntoViewIfNeeded();
  await page.waitForFunction(() => window.cpuObservationRows.length >= 360 ||
    document.querySelector('#retail-replay-downloads a'), null, {timeout});
  // Live diagnostics above the canvas can grow after match entry. Recheck its
  // placement once those rows exist, without changing the framebuffer size.
  await page.locator('#canvas').scrollIntoViewIfNeeded();
  const presentation = await page.locator('#canvas').evaluate(canvas => {
    const rect = canvas.getBoundingClientRect();
    return {x:rect.x, y:rect.y, width:rect.width, height:rect.height,
      buffer_width:canvas.width, buffer_height:canvas.height,
      viewport_width:innerWidth, viewport_height:innerHeight, document_hidden:document.hidden};
  });
  await write('playing-presentation.json', JSON.stringify(presentation, null, 2) + '\n');
  if (presentation.document_hidden || presentation.x < 0 || presentation.y < 0 ||
      presentation.x + presentation.width > presentation.viewport_width ||
      presentation.y + presentation.height > presentation.viewport_height ||
      !presentation.buffer_width || !presentation.buffer_height)
    throw Error('The source canvas must remain visible with a nonempty framebuffer');
  await page.screenshot({path:path.join(options.out, 'playing.png'), fullPage:false});
  await write('playing-observation.json', await page.evaluate(() =>
    window.cpuObservationRows.at(-1) || 'null'));
  await page.waitForFunction(() => document.querySelector('#retail-replay-downloads a'), null, {timeout});
  const exports = await page.locator('#retail-replay-downloads a').evaluateAll(async links =>
    Promise.all(links.map(async link => ({name:link.download, text:await (await fetch(link.href)).text()}))));
  for (const exported of exports) await write(path.basename(exported.name), exported.text);
  const report = JSON.parse(await page.locator('#retail-replay-report').textContent());
  await write('ui-report.json', JSON.stringify(report, null, 2) + '\n');
  await page.screenshot({path:path.join(options.out, 'completed.png'), fullPage:false});
  if (!report.complete || !report.pass || !report.source_match?.complete)
    throw Error('The full browser match did not complete successfully');
  await readArtifacts(inventory.runtime_sha256, servedArtifactsAfter);
  if (errors.length || requests.length) throw Error('Browser errors or unexpected non-GET requests were recorded');
  complete = true;
} catch (error) {
  process.exitCode = 1;
  await write('failure.txt', (error.stack || String(error)) + '\n');
  if (!page.isClosed()) {
    await write('page.txt', await page.locator('body').innerText().catch(() => 'Page unavailable'));
    await page.screenshot({path:path.join(options.out, 'failure.png'), fullPage:false}).catch(() => {});
  }
} finally {
  if (!page.isClosed()) {
    const evidence = await page.evaluate(() => ({
      cpu: window.cpuObservationRows || [],
      preparation: window.cpuPreparationRows || [],
      module: window.__meleeCpuModuleOutputCapture ? {
        version: window.__meleeCpuModuleOutputCapture.version,
        core_limit: window.__meleeCpuModuleOutputCapture.core_limit,
        timer_limit: window.__meleeCpuModuleOutputCapture.timer_limit,
        core_overflow: window.__meleeCpuModuleOutputCapture.core_overflow,
        timer_overflow: window.__meleeCpuModuleOutputCapture.timer_overflow,
        capture_errors: window.__meleeCpuModuleOutputCapture.capture_errors,
        wrapped_modules: window.__meleeCpuModuleOutputCapture.wrapped_modules,
        core: window.__meleeCpuModuleOutputCapture.core,
        timer: window.__meleeCpuModuleOutputCapture.timer,
      } : null,
    })).catch(() => ({cpu:[],preparation:[],module:null}));
    await write('cpu-observation.jsonl', evidence.cpu.join('\n') + (evidence.cpu.length ? '\n' : ''));
    await write('preparation-observation.jsonl', evidence.preparation.join('\n') + (evidence.preparation.length ? '\n' : ''));
    await write('module-output-capture.json', JSON.stringify(evidence.module, null, 2) + '\n');
    if (!evidence.module?.wrapped_modules || evidence.module.core_overflow ||
        evidence.module.timer_overflow || evidence.module.capture_errors) {
      complete = false;
      process.exitCode = 1;
      await write('module-output-failure.txt', 'Module output observation was unavailable or lost rows; this capture is not accepted.\n');
    }
    if (!complete) {
      const core = evidence.module?.core || [];
      const timer = evidence.module?.timer || [];
      await write('partial-port.jsonl', core.join('\n') + (core.length ? '\n' : ''));
      await write('partial-timer.jsonl', timer.join('\n') + (timer.length ? '\n' : ''));
    }
  }
  await write('served-artifacts.json', JSON.stringify(servedArtifacts, null, 2) + '\n');
  await write('served-artifacts-after.json', JSON.stringify(servedArtifactsAfter, null, 2) + '\n');
  await write('browser-errors.json', JSON.stringify({errors, requests}, null, 2) + '\n');
  await browser.close();
}
