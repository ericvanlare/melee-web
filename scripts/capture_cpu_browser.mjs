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
  await page.waitForFunction(() => window.cpuObservationRows.length >= 360 ||
    document.querySelector('#retail-replay-downloads a'), null, {timeout});
  await page.screenshot({path:path.join(options.out, 'playing.png'), fullPage:true});
  await write('playing-observation.json', await page.evaluate(() =>
    window.cpuObservationRows.at(-1) || 'null'));
  await page.waitForFunction(() => document.querySelector('#retail-replay-downloads a'), null, {timeout});
  const exports = await page.locator('#retail-replay-downloads a').evaluateAll(async links =>
    Promise.all(links.map(async link => ({name:link.download, text:await (await fetch(link.href)).text()}))));
  for (const exported of exports) await write(path.basename(exported.name), exported.text);
  const report = JSON.parse(await page.locator('#retail-replay-report').textContent());
  await write('ui-report.json', JSON.stringify(report, null, 2) + '\n');
  await page.screenshot({path:path.join(options.out, 'completed.png'), fullPage:true});
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
    await page.screenshot({path:path.join(options.out, 'failure.png'), fullPage:true}).catch(() => {});
  }
} finally {
  if (!page.isClosed()) {
    const evidence = await page.evaluate(() => ({
      cpu: window.cpuObservationRows || [],
      preparation: window.cpuPreparationRows || [],
      core: typeof retailRun !== 'undefined' ? retailRun?.rows || [] : [],
      timer: typeof retailRun !== 'undefined' ? retailRun?.timerRows || [] : [],
    })).catch(() => ({cpu:[],preparation:[],core:[],timer:[]}));
    await write('cpu-observation.jsonl', evidence.cpu.join('\n') + (evidence.cpu.length ? '\n' : ''));
    await write('preparation-observation.jsonl', evidence.preparation.join('\n') + (evidence.preparation.length ? '\n' : ''));
    if (!complete) {
      await write('partial-port.jsonl', evidence.core.join('\n') + (evidence.core.length ? '\n' : ''));
      await write('partial-timer.jsonl', evidence.timer.join('\n') + (evidence.timer.length ? '\n' : ''));
    }
  }
  await write('served-artifacts.json', JSON.stringify(servedArtifacts, null, 2) + '\n');
  await write('served-artifacts-after.json', JSON.stringify(servedArtifactsAfter, null, 2) + '\n');
  await write('browser-errors.json', JSON.stringify({errors, requests}, null, 2) + '\n');
  await browser.close();
}
