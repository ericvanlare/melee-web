#!/usr/bin/env node
/** Real public player + owned-disc presentation. Headless is not OS fullscreen evidence. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'playwright', 'out', 'dpr'].map(name => [name, {type: 'string'}]))});
if (!values.url || !values.disc || !values.out)
  throw Error('Use --url PUBLIC_ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR] [--dpr 1|2]');
const dpr = Number(values.dpr || 1);
assert([1, 2].includes(dpr), 'Use DPR 1 or 2');
await fs.mkdir(path.dirname(values.out), {recursive: true});
await fs.mkdir(values.out);
const {chromium, browser: installedBrowser} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(installedBrowser));
const page = await browser.newPage({viewport: {width: 1000, height: 1400}, deviceScaleFactor: dpr});
const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 90000});
const report = {schema: 'webmelee-public-presentation-v1', browser: browser.version(),
  browser_mode: 'headless', dpr, url: values.url, observations: [], errors: [], layoutFailures: [],
  scope: 'Original CSS in the public player: display geometry, WebGPU pixels, resize, DOM fullscreen and pointer coordinates. No OS fullscreen, timing or retail-equivalence claim.'};
page.on('pageerror', error => report.errors.push(error.message));
page.on('console', message => {if (message.type() === 'error') report.errors.push(message.text());});
const frames = () => page.evaluate(() => new Promise(resolve =>
  requestAnimationFrame(() => requestAnimationFrame(() => requestAnimationFrame(resolve)))));

async function observe(name) {
  await frames();
  const observed = await page.evaluate(() => {
    const canvas = document.querySelector('#canvas');
    const rect = selector => document.querySelector(selector).getBoundingClientRect().toJSON();
    return {viewport: [innerWidth, innerHeight], canvas: rect('#canvas'),
      host: rect('#runtime-host'), toolbar: rect('#toolbar'),
      buffer: [canvas.width, canvas.height], dpr: devicePixelRatio,
      scroll: [document.documentElement.scrollWidth, document.documentElement.scrollHeight],
      fullscreen: document.fullscreenElement?.id || null,
      phase: Module._melee_web_native_menu_phase(), running: Module._melee_web_native_menu_running(),
      message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
      error: document.querySelector('#error-dialog[open]')?.textContent || null};
  });
  // Screenshot readback observes the presented frame. WebGPU discards its
  // current texture after presentation, so drawImage(canvas) can read black.
  const screenshot = await page.screenshot({path: path.join(values.out, name + '.png'), scale: 'css'});
  observed.pixels = await page.evaluate(async ({encoded, rect, viewportWidth}) => {
    const bytes = Uint8Array.from(atob(encoded), c => c.charCodeAt(0));
    const bitmap = await createImageBitmap(new Blob([bytes], {type: 'image/png'}));
    const scale = bitmap.width / viewportWidth;
    const probe = document.createElement('canvas');
    probe.width = 64; probe.height = 48;
    const ctx = probe.getContext('2d');
    ctx.drawImage(bitmap, rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale, 0, 0, 64, 48);
    bitmap.close();
    const pixels = ctx.getImageData(0, 0, 64, 48).data;
    let lit = 0;
    const colors = new Set();
    for (let i = 0; i < pixels.length; i += 4) {
      if (pixels[i] + pixels[i + 1] + pixels[i + 2] > 48) lit++;
      colors.add(`${pixels[i]},${pixels[i + 1]},${pixels[i + 2]}`);
    }
    return {lit, colors: colors.size};
  }, {encoded: screenshot.toString('base64'), rect: observed.canvas, viewportWidth: observed.viewport[0]});
  report.observations.push({name, ...observed});
  assert.equal(observed.error, null, 'No player error');
  assert.equal(observed.phase, 1, 'Original character select must remain loaded');
  assert.equal(observed.running, 1, observed.message);
  assert(observed.pixels.lit > 500 && observed.pixels.colors > 100, 'Real game pixels must survive resize');
  assert.deepEqual(report.errors, [], 'No browser or WebGPU validation errors');
  // Retain every size on a failing baseline, then fail the run below.
  try {
    const {canvas: c, host: h, toolbar: t, viewport: [w, height]} = observed;
    assert(c.width > 0 && c.height > 0);
    assert(Math.abs(c.width - c.height * 4 / 3) < 1, 'Game rectangle must be 4:3');
    assert(c.x >= h.x - 1 && c.y >= h.y - 1 && c.right <= h.right + 1 && c.bottom <= h.bottom + 1, 'Complete image inside player area');
    assert(Math.abs(c.x + c.width / 2 - h.x - h.width / 2) < 1 &&
      Math.abs(c.y + c.height / 2 - h.y - h.height / 2) < 1, 'Centered image');
    assert(Math.abs(c.width - h.width) < 1 || Math.abs(c.height - h.height) < 1, 'Largest fitting image');
    assert(c.bottom <= t.top + 1 && t.bottom <= height + 1, 'Toolbar has reserved space');
    assert(observed.scroll[0] <= w && observed.scroll[1] <= height, 'No document scrolling');
    for (const [actual, expected] of [[observed.buffer[0], c.width * dpr], [observed.buffer[1], c.height * dpr]])
      assert(Math.abs(actual - expected) <= 2, 'Backing buffer follows display size and DPR');
  } catch (error) {report.layoutFailures.push({name, message: error.message});}
  console.log(name, observed.buffer, observed.canvas.width, observed.canvas.height);
}

try {
  await page.goto(values.url);
  report.gpu = await page.evaluate(async () => {
    const adapter = await navigator.gpu.requestAdapter();
    return {isolated: crossOriginIsolated, vendor: adapter?.info.vendor,
      architecture: adapter?.info.architecture, fallback: adapter?.info.isFallbackAdapter};
  });
  assert(report.gpu.isolated && report.gpu.vendor, 'Isolated real WebGPU player');
  await driver.waitForImport();
  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  await driver.launch();
  // Allow the authored CSS entry animation to reveal the complete screen.
  await page.waitForTimeout(1500);
  for (const [name, width, height] of [
    ['tall', 1001, 1400], ['wide', 1800, 800], ['desktop', 1280, 960],
    ['narrow-toolbar', 390, 844], ['short', 1280, 480], ['tall-return', 1000, 1400],
  ]) {
    await page.setViewportSize({width, height});
    await observe(name);
  }
  await page.locator('#fullscreen').click();
  await page.waitForFunction(() => document.fullscreenElement?.id === 'player');
  await observe('dom-fullscreen');
  await page.locator('#fullscreen').click();
  await page.waitForFunction(() => !document.fullscreenElement);
  await observe('fullscreen-exit');
  await page.evaluate(() => document.querySelector('#canvas').addEventListener('pointerdown', event => {
    window.presentationPointer = {x: event.offsetX, y: event.offsetY};
  }, {once: true}));
  const box = await page.locator('#canvas').boundingBox();
  await page.mouse.click(box.x + box.width / 4, box.y + box.height / 4);
  const pointer = await page.evaluate(() => window.presentationPointer);
  assert(Math.abs(pointer.x - box.width / 4) < 1 && Math.abs(pointer.y - box.height / 4) < 1,
    'Pointer offsets follow the fitted canvas, excluding bars');
  report.pointer = pointer;
  assert.deepEqual(report.layoutFailures, [], 'All presentation rectangles must fit without distortion');
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = String(error);
  report.diagnostics = await driver.diagnostics();
  await page.screenshot({path: path.join(values.out, 'failure.png')}).catch(() => {});
  throw error;
} finally {
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close();
}
