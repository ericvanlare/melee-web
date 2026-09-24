#!/usr/bin/env node
/** Exercise acquired-surface bounds during real browser resize and startup. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: {
  ...Object.fromEntries(['url', 'playwright', 'out'].map(name => [name, {type: 'string'}])),
  headed: {type: 'boolean', default: false},
}});
if (!values.url || !values.playwright || !values.out)
  throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out NEW_DIRECTORY [--headed]');
await fs.mkdir(values.out, {recursive: false});
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

  // Observe startup as soon as the real module canvas is published while
  // Import is still gated, then resize before waiting for final readiness.
  await page.waitForFunction(() => {
    const canvas = globalThis.Module?.canvas;
    const importControl = document.querySelector('#choose-disc');
    return canvas?.width > 0 && canvas?.height > 0 && importControl?.disabled === true;
  }, null, {timeout: 90000});
  const startupBoundary = await page.evaluate(() => ({
    importDisabled: document.querySelector('#choose-disc')?.disabled === true,
    moduleCanvasReady: Boolean(globalThis.Module?.canvas?.width && globalThis.Module?.canvas?.height),
  }));
  assert.equal(startupBoundary.importDisabled, true, 'Startup resize must occur while Import is gated');
  assert.equal(startupBoundary.moduleCanvasReady, true, 'Startup resize requires the real module canvas');
  await page.setViewportSize({width: 320, height: 700});
  await frame();
  const startupObserved = await snapshot();
  startupObservations.push({...startupObserved, boundary: 'startup'});
  observations.push(startupObserved);
  assert.equal(startupObserved.error, null, 'Native presentation must survive startup resize');
  assert.deepEqual(errors, []);

  // Continue only after the final startup readiness gate has opened.
  await page.locator('#choose-disc:not([disabled])').waitFor({timeout: 90000});
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
  assert(new Set(observations.map(value => value.framebuffer.join('x'))).size > 1,
    'Exercise actual framebuffer resizing, not only CSS scaling');
  await page.screenshot({path: path.join(values.out, 'narrow.png')});
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify({
    result: 'pass', browser: browser.version(), browser_mode: values.headed ? 'headed' : 'headless',
    startupObservations, observations, errors,
    scope: 'Real WebGPU presentation resizing during startup and with controls open; no disc or gameplay claim',
  }, null, 2));
} catch (error) {
  await fs.writeFile(path.join(values.out, 'failure.json'), JSON.stringify({
    error: String(error), errors, startupObservations, observations, final: await snapshot().catch(() => null),
  }, null, 2));
  await page.screenshot({path: path.join(values.out, 'failure.png')});
  throw error;
} finally {
  await browser.close();
}
