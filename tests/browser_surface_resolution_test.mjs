#!/usr/bin/env node
/** Real WebGPU surface sizing. No disc, source gameplay or timing admission. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'out', 'playwright', 'manifest'].map(name => [name, {type: 'string'}]),
)});
if (!values.url || !values.out) throw Error('Use --url PUBLIC_PLAYER --out FRESH_DIRECTORY [--playwright PACKAGE_DIR] [--manifest PACKAGE_MANIFEST]');
await fs.mkdir(values.out, {recursive: false});
const report = {schema: 'melee-web-browser-surface-resolution-v1',
  scope: 'Installed headless Chrome, real WebGPU public-player startup at DPR 1/2/3 and CSS viewport resize. No disc/gameplay, physical device, OS fullscreen or performance claim.',
  cases: [], errors: [], cleanup: {}};
if (values.manifest) {
  const manifest = JSON.parse(await fs.readFile(values.manifest, 'utf8'));
  report.identity = {source_sha: manifest.source_sha, runtime_hash: manifest.runtime_hash,
    identity_sha256: manifest.identity_sha256, profile: manifest.profile};
}
let browser, context, page, watchdog;
const started = Date.now(), deadline = started + 120_000;
try {
  const tools = await loadBrowserTools(values.playwright);
  browser = await tools.chromium.launch(browserLaunchOptions(tools.browser, {timeout: 30_000}));
  watchdog = setTimeout(() => browser.close().catch(() => {}), Math.max(1, deadline - Date.now()));
  report.browser = browser.version();
  for (const dpr of [1, 2, 3]) {
    context = await browser.newContext({viewport: {width: 390, height: 844}, deviceScaleFactor: dpr,
      isMobile: true, hasTouch: true});
    page = await context.newPage();
    page.on('pageerror', error => report.errors.push({dpr, kind: 'pageerror', message: error.message}));
    page.on('crash', () => report.errors.push({dpr, kind: 'crash'}));
    page.on('console', message => {
      if (message.type() === 'error') report.errors.push({dpr, kind: 'console', message: message.text()});
    });
    const response = await page.goto(values.url, {waitUntil: 'domcontentloaded', timeout: 30_000});
    assert.equal(response.status(), 200);
    await page.waitForFunction(() => document.querySelector('#loading-panel')?.hidden === true &&
      typeof globalThis.Module?._melee_web_native_menu_cache_idle === 'function' &&
      Module._melee_web_native_menu_cache_idle() === 1, null,
    {timeout: Math.min(30_000, Math.max(1, deadline - Date.now() - 15_000))});
    for (const viewport of [{width: 390, height: 844}, {width: 844, height: 390}, {width: 1280, height: 960}]) {
      await page.setViewportSize(viewport);
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const state = await page.evaluate(() => {
        const canvas = document.querySelector('#canvas'), rect = canvas.getBoundingClientRect();
        return {dpr: devicePixelRatio, canvas: [canvas.width, canvas.height],
          css: [rect.width, rect.height], phase: Module._melee_web_native_menu_phase(),
          running: Module._melee_web_native_menu_running(), isolated: crossOriginIsolated,
          webgpu: !!navigator.gpu, loading_hidden: document.querySelector('#loading-panel').hidden};
      });
      report.cases.push({dpr, viewport, state});
      assert.deepEqual(state.canvas, [640, 480], `Configured pixel surface changed at DPR ${dpr}, ${JSON.stringify(viewport)}`);
      assert.equal(state.dpr, dpr);
      assert(state.css.every(value => value > 0));
      assert(Math.abs(state.css[0] / state.css[1] - 4 / 3) < 0.01, 'CSS presentation lost its original aspect');
      assert.equal(state.phase, 0);
      assert.equal(state.running, 0);
      assert.equal(state.isolated, true);
      assert.equal(state.webgpu, true);
      assert.equal(state.loading_hidden, true);
      assert.equal(report.errors.length, 0, JSON.stringify(report.errors));
    }
    await page.screenshot({path: path.join(values.out, `dpr-${dpr}.png`), timeout: 2000});
    await context.close(); context = null; page = null;
  }
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = error.message;
  await page?.screenshot({path: path.join(values.out, 'failure.png'), timeout: 2000}).catch(() => {});
} finally {
  await context?.close().catch(() => {});
  await browser?.close().catch(() => {});
  clearTimeout(watchdog);
  report.cleanup.browser_closed = !browser?.isConnected();
  report.elapsed_ms = Date.now() - started;
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}
console.log(JSON.stringify({result: report.result, cases: report.cases.length, failure: report.failure, out: values.out}));
if (report.result !== 'pass') process.exitCode = 1;
