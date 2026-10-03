#!/usr/bin/env node
/** Exercise fixed game-surface ownership during real browser resize and startup. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: {
  ...Object.fromEntries(['url', 'playwright', 'out', 'manifest'].map(name => [name, {type: 'string'}])),
  headed: {type: 'boolean', default: false},
}});
if (!values.url || !values.playwright || !values.out)
  throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out NEW_DIRECTORY [--headed]');
await fs.mkdir(values.out, {recursive: false});
const manifest = values.manifest ? JSON.parse(await fs.readFile(values.manifest, 'utf8')) : null;
const identity = manifest ? {source_sha: manifest.source_sha, runtime_hash: manifest.runtime_hash,
  identity_sha256: manifest.identity_sha256, profile: manifest.profile} : null;
const {chromium, browser: launchOptions} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(launchOptions, {headed: values.headed}));
const page = await browser.newPage({viewport: {width: 1100, height: 800}});
const errors = [], observations = [], startupObservations = [];
page.on('pageerror', error => errors.push(error.stack || error.message));
const snapshot = () => page.evaluate(() => ({
  viewport: [innerWidth, innerHeight],
  framebuffer: [Module.canvas.width, Module.canvas.height],
  pipeline: Module.pipelinePreparation ? {...Module.pipelinePreparation} : null,
  error: document.querySelector('#error-dialog[open]')?.textContent || null,
}));
const frame = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
try {
  await page.goto(values.url, {waitUntil: 'commit'});

  // Disc selection can open before graphics are ready. Use the graphics
  // panel and configured canvas, rather than the import button, as the gate.
  await page.waitForFunction(() => {
    const canvas = globalThis.Module?.canvas;
    return canvas?.width === 640 && canvas?.height === 480 &&
      document.querySelector('#loading-panel')?.hidden === false;
  }, null, {timeout: 90000});
  const startupBoundary = await page.evaluate(() => ({
    graphicsPreparing: document.querySelector('#loading-panel')?.hidden === false,
    moduleCanvasReady: Boolean(globalThis.Module?.canvas?.width && globalThis.Module?.canvas?.height),
  }));
  assert.equal(startupBoundary.graphicsPreparing, true, 'Startup resize must occur before graphics readiness');
  assert.equal(startupBoundary.moduleCanvasReady, true, 'Startup resize requires the real module canvas');
  await page.setViewportSize({width: 320, height: 700});
  await frame();
  const startupObserved = await snapshot();
  startupObservations.push({...startupObserved, boundary: 'startup'});
  observations.push(startupObserved);
  assert.equal(startupObserved.error, null, 'Native presentation must survive startup resize');
  assert.deepEqual(errors, []);

  // Continue only after the final startup readiness gate has opened.
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
  observations.push(await snapshot());
  for (let round = 0; round < 3; round++) {
    for (const [width, height] of [[320, 700], [1280, 960], [640, 480], [768, 844]]) {
      await page.setViewportSize({width, height});
      await frame();
      const observed = await snapshot();
      observations.push(observed);
      assert.equal(observed.error, null, 'Native presentation must survive resize');
      assert.deepEqual(errors, []);
    }
  }
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
  await page.locator('#controls-open').click();
  await page.setViewportSize({width: 320, height: 700});
  await frame();
  await page.locator('#controls-close').click();
  observations.push(await snapshot());
  assert.deepEqual(errors, []);
  assert.equal(observations.at(-1).error, null);
  for (const observed of observations)
    assert.deepEqual(observed.framebuffer, [640, 480], 'CSS resizing must preserve configured game pixels');
  await page.screenshot({path: path.join(values.out, 'narrow.png')});
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify({
    result: 'pass', browser: browser.version(), browser_mode: values.headed ? 'headed' : 'headless',
    identity, startupObservations, observations, errors,
    scope: 'Real WebGPU fixed backing pixels during CSS resize, startup and open controls; no disc or gameplay claim',
  }, null, 2));
} catch (error) {
  await fs.writeFile(path.join(values.out, 'failure.json'), JSON.stringify({
    identity, error: String(error), errors, startupObservations, observations, final: await snapshot().catch(() => null),
  }, null, 2));
  await page.screenshot({path: path.join(values.out, 'failure.png')});
  throw error;
} finally {
  await browser.close();
}
