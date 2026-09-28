#!/usr/bin/env node
/** Rendered source Results reducer for the historical Sheik-winner boundary.
 * This is a synthetic standings/held-input discriminator, not a natural match
 * replay, retail comparison, or foreground-timing test. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['build-dir', 'out', 'playwright'].map(name => [name, {type: 'string'}]))});
if (!values['build-dir'] || !values.out)
  throw Error('Use --build-dir BUILD_DIRECTORY --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const root = path.resolve(import.meta.dirname, '..');
const build = path.resolve(values['build-dir']);
const output = path.resolve(values.out);
const stem = 'gameplay_results_scene_trace_rendered';
const artifacts = Object.fromEntries(['js', 'wasm', 'data'].map(ext =>
  [ext, path.join(build, `${stem}.${ext}`)]));
for (const [kind, filename] of Object.entries(artifacts)) {
  const stat = await fs.stat(filename).catch(() => null);
  if (!stat?.isFile() || stat.size === 0)
    throw Error(`Rendered Results ${kind} artifact missing: ${filename}`);
}
await fs.mkdir(output, {recursive: false});
const sha256 = async filename => createHash('sha256').update(await fs.readFile(filename)).digest('hex');
const git = (...args) => execFileSync('git', args, {cwd: root, encoding: 'utf8'}).trim();
const report = {
  schema: 'melee-web-results-draw-reducer-v1',
  scope: 'headless rendered Chrome; original Results scene with synthetic Zelda-origin Sheik winner standings; source-tick P1 Start pulses at 180/360/600; disconnected CPU statistics pages; every Results source tick and draw; no natural match, retail comparison, foreground timing, or performance claim',
  browser_mode: 'headless installed Chrome through scripts/browser_tools.mjs',
  provenance: {
    commit: git('rev-parse', 'HEAD'),
    tracked_diff_sha256: createHash('sha256').update(
      execFileSync('git', ['diff', '--binary', 'HEAD'], {cwd: root})).digest('hex'),
    assets: Object.fromEntries(await Promise.all(Object.entries(artifacts).map(
      async ([kind, filename]) => [kind, {bytes: (await fs.stat(filename)).size, sha256: await sha256(filename)}]))),
  },
  console: [], page_errors: [], failed_requests: [], responses: [],
  finished_requests: [], bad_responses: [], source_diagnostics: [],
  source_lines: [], screenshots: {},
};

const html = `<!doctype html><meta charset="utf-8">
<meta http-equiv="Cross-Origin-Opener-Policy" content="same-origin">
<meta http-equiv="Cross-Origin-Embedder-Policy" content="require-corp">
<title>Results draw reducer</title><link rel="icon" href="data:,">
<style>html,body{margin:0;background:#171717}canvas{width:960px;height:720px;display:block}</style>
<canvas id="canvas" width="640" height="480"></canvas>
<script>
window.resultsTraceLogs=[];
const relay=stream=>(...values)=>{
  const line=values.map(value=>String(value)).join(' ');
  window.resultsTraceLogs.push({stream,line});
  console.log('[source '+stream+'] '+line);
};
var Module={
  arguments:['--lineup-b-zelda-sheik-stock-p1-statistics-host-draw',
    '/assets/native-menus','/assets/repro-results-v1','/assets/next-gate'],
  canvas:document.getElementById('canvas'),
  print:relay('stdout'),printErr:relay('stderr'),
  preRun:[()=>FS.mkdir('/melee-results-cache')],
  onRuntimeInitialized(){window.resultsRuntimeReady=true;}
};
</script><script src="/${stem}.js"></script>`;

const mime = {'.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream'};
const server = http.createServer(async (request, response) => {
  response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  response.setHeader('Cache-Control', 'no-store');
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.pathname === '/') {
    response.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'});
    response.end(html);
    return;
  }
  const match = url.pathname.match(/^\/gameplay_results_scene_trace_rendered\.(js|wasm|data)$/);
  if (!match) { response.writeHead(404); response.end(); return; }
  const filename = artifacts[match[1]];
  response.writeHead(200, {'Content-Type': mime[path.extname(filename)]});
  const stream = (await import('node:fs')).createReadStream(filename);
  stream.on('error', error => { report.failed_requests.push({url: request.url, message: error.message}); response.destroy(error); });
  stream.pipe(response);
});

const {chromium, browser: installedBrowser} = await loadBrowserTools(values.playwright);
let browser;
try {
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  report.url = `http://127.0.0.1:${address.port}/`;
  browser = await chromium.launch(browserLaunchOptions(installedBrowser));
  report.browser = browser.version();
  const page = await browser.newPage({viewport: {width: 1000, height: 800}, deviceScaleFactor: 1});
  page.setDefaultTimeout(180000);
  page.on('pageerror', error => report.page_errors.push(error.stack || error.message));
  page.on('crash', () => report.page_errors.push('Headless Chrome page crashed'));
  page.on('console', message => {
    if (message.type() === 'error') report.console.push(message.text());
  });
  page.on('requestfailed', request => report.failed_requests.push({url: request.url(), error: request.failure()?.errorText}));
  page.on('requestfinished', request => report.finished_requests.push({url: request.url()}));
  page.on('response', response => {
    report.responses.push({url: response.url(), status: response.status()});
    if (response.status() >= 400) report.bad_responses.push({url: response.url(), status: response.status()});
  });
  const started = Date.now();
  await page.goto(report.url, {waitUntil: 'load', timeout: 180000});
  report.gpu = await page.evaluate(async () => {
    const adapter = await navigator.gpu?.requestAdapter();
    return {available: Boolean(adapter), isolated: crossOriginIsolated,
      vendor: adapter?.info?.vendor || null, architecture: adapter?.info?.architecture || null,
      fallback: adapter?.info?.isFallbackAdapter ?? null};
  });
  assert(report.gpu.available, 'Chrome WebGPU adapter must be available');
  await page.waitForFunction(() => window.resultsDone !== undefined, {timeout: 180000});
  report.elapsed_ms = Date.now() - started;
  report.result_code = await page.evaluate(() => window.resultsDone);
  report.source_frame = await page.evaluate(() => window.resultsFrame ?? null);
  report.source_lines = await page.evaluate(() => window.resultsTraceLogs);
  report.source_diagnostics = report.source_lines.filter(row =>
    row.stream === 'stderr' && /\[(?:warn|error|critical)\]/i.test(row.line));
  report.data_resource_timing = await page.evaluate(() => performance.getEntriesByType('resource')
    .filter(entry => entry.name.endsWith('gameplay_results_scene_trace_rendered.data'))
    .map(entry => ({name: entry.name, duration: entry.duration, transferSize: entry.transferSize,
      decodedBodySize: entry.decodedBodySize, responseEnd: entry.responseEnd})));
  const dataBytes = report.provenance.assets.data.bytes;
  report.preload_transfer = {
    expected_bytes: dataBytes,
    response_statuses: report.responses.filter(row => row.url.endsWith(`${stem}.data`)).map(row => row.status),
    completed_resource_timing: report.data_resource_timing.some(entry =>
      entry.decodedBodySize === dataBytes && entry.transferSize >= dataBytes && entry.responseEnd > 0),
  };
  report.classified_request_cancellations = report.failed_requests.filter(request =>
    request.url.endsWith(`${stem}.data`) && request.error === 'net::ERR_ABORTED' &&
    report.preload_transfer.response_statuses.includes(200) &&
    report.preload_transfer.completed_resource_timing);
  report.unclassified_failed_requests = report.failed_requests.filter(request =>
    !report.classified_request_cancellations.includes(request));
  report.screenshots.page = path.join(output, 'results-page.png');
  report.screenshots.canvas = path.join(output, 'results-canvas.png');
  await page.screenshot({path: report.screenshots.page});
  await page.locator('#canvas').screenshot({path: report.screenshots.canvas});
  const combined = report.source_lines.map(row => row.line).join('\n');
  assert.equal(report.result_code, 0, `Results reducer returned ${report.result_code}`);
  assert.match(combined, /draw_scope=rendered-GPU/);
  assert.match(combined, /p1-statistics auto-page slot=2 from=0 to=1 source_frame=\d+/);
  assert.match(combined, /p1-statistics auto-page slot=3 from=0 to=1 source_frame=\d+/);
  assert.match(combined, /p1-statistics before-confirm source_frame=600 auto_pages=1,1/);
  assert.match(combined, /p1-statistics coverage frames=(\d+) trigger_edges=3 releases=3 held_ticks=30 .*source_draw_api_calls=(\d+)/);
  const coverage = combined.match(/p1-statistics coverage frames=(\d+) trigger_edges=3 releases=3 held_ticks=30 .*source_draw_api_calls=(\d+)/);
  assert.equal(Number(coverage[1]), Number(coverage[2]), 'Every source Results tick must draw');
  assert.match(combined, /host OnExit\+commit tick=\d+ initial_pool=(0x[0-9a-f]+) source_pool=\1/);
  assert.match(combined, /all four participant demo owners constructed and closed/);
  assert.equal(report.page_errors.length, 0, 'No page exceptions or Chrome crashes');
  assert.equal(report.console.length, 0, 'No browser/WebGPU console errors');
  assert.equal(report.unclassified_failed_requests.length, 0,
    `No unclassified failed browser requests: ${JSON.stringify(report.failed_requests)}`);
  assert.equal(report.bad_responses.length, 0, 'All browser resources must load');
  console.log(JSON.stringify({result_code: report.result_code, source_frame: report.source_frame,
    elapsed_ms: report.elapsed_ms, gpu: report.gpu,
    classified_request_cancellations: report.classified_request_cancellations,
    screenshot: report.screenshots.canvas}));
} catch (error) {
  report.failure = {message: error.message, stack: error.stack};
  throw error;
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
}
