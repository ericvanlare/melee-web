#!/usr/bin/env node
/** Touch UI and PAD boundary against the real development player and owned disc. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'playwright', 'out'].map(name => [name, {type: 'string'}]))});
if (!values.url || !values.disc || !values.out)
  throw Error('Use --url DEVELOPMENT_RUNTIME_URL --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
await fs.mkdir(path.dirname(values.out), {recursive: true});
await fs.mkdir(values.out);
const {chromium, browser: installedBrowser} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(installedBrowser));
const page = await browser.newPage({viewport: {width: 1280, height: 800}});
const driver = createBrowserDriver(page, {surface: 'development', timeoutMs: 90000});
const errors = [];
page.on('pageerror', error => errors.push(error.message));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
const source = port => page.getByLabel(`Player ${port} input source`, {exact: true});
const waitSource = mode => page.waitForFunction(mode => Module.meleeControllers.getPortSource(0) === mode, mode);
const pad = () => page.evaluate(() => {
  const samples = new Int32Array(32);
  Module.meleeControllers.writeSamples(samples, 0);
  return [...samples];
});
async function pointer(selector, type, pointerId, x, y) {
  await page.locator(selector).evaluate((element, event) => {
    const init = {bubbles: true, cancelable: true, pointerId: event.pointerId,
      pointerType: 'touch', isPrimary: true, button: 0, clientX: event.x, clientY: event.y};
    element.dispatchEvent(new PointerEvent(event.type, init));
  }, {type, pointerId, x, y});
}
async function stickPoint(selector, axisX, axisY) {
  const rect = await page.locator(selector).boundingBox();
  const radius = Math.min(rect.width, rect.height) * 0.42;
  return {rect, x: rect.x + rect.width / 2 + radius * axisX,
    y: rect.y + rect.height / 2 + radius * axisY};
}
const report = {schema: 'webmelee-touch-controls-browser-v1', browser: browser.version(),
  browser_mode: 'headless installed Chrome', url: values.url, disc: path.basename(values.disc),
  screenshots: [], scope: 'Owned-disc original CSS screen, touch settings, emulated portrait/landscape layout and authored multi-pointer input through the shared PAD writer. No phone, iOS fullscreen, timing or retail-equivalence claim.'};

try {
  await page.goto(values.url);
  report.gpu = await page.evaluate(async () => {
    const adapter = await navigator.gpu?.requestAdapter();
    return {isolated: crossOriginIsolated, available: !!adapter,
      vendor: adapter?.info?.vendor || null, architecture: adapter?.info?.architecture || null,
      fallback: adapter?.info?.isFallbackAdapter ?? null};
  });
  assert(report.gpu.isolated && report.gpu.available, 'Actual development game needs an isolated WebGPU adapter');
  await driver.waitForImport();
  await page.setViewportSize({width: 390, height: 844});
  await page.locator('#controls-open').click();
  assert.equal(await source(1).inputValue(), 'auto');
  assert.equal(await source(2).locator('option[value="touch"]').count(), 0, 'P2 has no touch source option');
  assert.equal(await page.locator('#touch-controls').isHidden(), true, 'touch input is disabled by default');
  await source(1).selectOption('touch');
  await waitSource('touch');
  const opacity = page.locator('#touch-opacity');
  await opacity.evaluate(input => { input.value = '0.65'; input.dispatchEvent(new Event('input', {bubbles: true})); });
  assert.equal(await page.locator('#touch-opacity-value').textContent(), '65%');
  const stored = await page.evaluate(() => JSON.parse(localStorage.getItem('melee-prototype-keyboard-v1')));
  assert.equal(stored.sources[0], 'touch');
  assert.equal(stored.touchOpacity, 0.65);
  await page.locator('#controls-close').click();
  await page.locator('#touch-controls:not([hidden])').waitFor();

  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  await driver.launch();
  await page.waitForTimeout(900);
  const gameState = await page.evaluate(() => ({
    phase: Module._melee_web_native_menu_phase(), running: Module._melee_web_native_menu_running(),
    input: Module.meleeControllers.getPortSource(0), gameRect: document.querySelector('#canvas').getBoundingClientRect().toJSON(),
    overlayRect: document.querySelector('#touch-controls').getBoundingClientRect().toJSON(),
    safe: ['left', 'top', 'right', 'bottom'].map(side => getComputedStyle(document.querySelector('#touch-controls')).getPropertyValue(`--touch-safe-${side}`).trim()),
  }));
  assert.equal(gameState.phase, 1, 'owned disc reached original CSS');
  assert.equal(gameState.running, 1);
  assert.equal(gameState.input, 'touch');
  assert(Math.abs(gameState.gameRect.width / gameState.gameRect.height - 4 / 3) < 0.01, 'portrait game remains 4:3');
  assert(Math.abs(gameState.overlayRect.width - gameState.gameRect.width) < 1 &&
    Math.abs(gameState.overlayRect.height - gameState.gameRect.height) < 1, 'touch controls fit the displayed game rectangle');
  assert(gameState.safe.every(value => Number.isFinite(parseFloat(value)) && parseFloat(value) >= 0), 'safe-area offsets are finite and nonnegative');
  await page.screenshot({path: path.join(values.out, 'touch-overlay-portrait.png'), fullPage: true});
  report.screenshots.push('touch-overlay-portrait.png');
  await page.locator('#controls-open').click();
  assert.equal(await source(1).inputValue(), 'touch');
  assert.equal(await opacity.inputValue(), '0.65');
  await page.screenshot({path: path.join(values.out, 'touch-settings-portrait.png'), fullPage: true});
  report.screenshots.push('touch-settings-portrait.png');
  await page.locator('#controls-close').click();

  await page.setViewportSize({width: 1280, height: 720});
  await page.waitForTimeout(80);
  const landscape = await page.evaluate(() => ({
    game: document.querySelector('#canvas').getBoundingClientRect().toJSON(),
    overlay: document.querySelector('#touch-controls').getBoundingClientRect().toJSON(),
  }));
  assert(Math.abs(landscape.game.width / landscape.game.height - 4 / 3) < 0.01, 'landscape game remains 4:3');
  assert(Math.abs(landscape.overlay.width - landscape.game.width) < 1 &&
    Math.abs(landscape.overlay.height - landscape.game.height) < 1, 'overlay follows canvas after orientation-size change');
  await page.screenshot({path: path.join(values.out, 'touch-overlay-landscape.png'), fullPage: true});
  report.screenshots.push('touch-overlay-landscape.png');

  await page.locator('#pause').click();
  await page.waitForFunction(() => Module._melee_web_native_menu_running() === 0);
  // Keep the verified CSS paused while holding one touch through ordinary
  // Eject teardown.
  await pointer('[data-touch-button="A"]', 'pointerdown', 401, 0, 0);
  await driver.unload();
  assert.equal((await pad())[1], 0, 'Eject cleanup leaves virtual buttons neutral');

  // Exercise multi-touch through the controller manager after Eject; the PAD
  // writer remains available for direct boundary assertions.

  // Independent pointer IDs hold multiple buttons and both analog sticks.
  const main = await stickPoint('[data-touch-stick="main"]', 0.68, 0.58);
  const cstick = await stickPoint('[data-touch-stick="cstick"]', -0.68, 0.58);
  await pointer('[data-touch-button="A"]', 'pointerdown', 101, 0, 0);
  await pointer('[data-touch-button="B"]', 'pointerdown', 102, 0, 0);
  await pointer('[data-touch-button="L"]', 'pointerdown', 103, 0, 0);
  await pointer('[data-touch-button="R"]', 'pointerdown', 104, 0, 0);
  await pointer('[data-touch-button="Z"]', 'pointerdown', 105, 0, 0);
  await pointer('[data-touch-stick="main"]', 'pointerdown', 106, main.x, main.y);
  await pointer('[data-touch-stick="cstick"]', 'pointerdown', 107, cstick.x, cstick.y);
  await pointer('[data-touch-button="X"]', 'pointerdown', 108, 0, 0);
  await pointer('[data-touch-button="Y"]', 'pointerdown', 109, 0, 0);
  await pointer('[data-touch-button="Up"]', 'pointerdown', 110, 0, 0);
  await pointer('[data-touch-button="Left"]', 'pointerdown', 111, 0, 0);
  let sample = await pad();
  assert.equal(sample[0], 1);
  for (const bit of [256, 512, 1024, 2048, 64, 32, 16, 8, 1])
    assert(sample[1] & bit, `concurrent attack/shield/jump/dpad button bit ${bit}`);
  assert.deepEqual(sample.slice(6, 8), [255, 255], 'L and R press their digital bits and full analog pressure');
  assert(sample[2] > 0 && sample[3] < 0, 'main stick supports magnitude and screen-down maps to original up-axis');
  assert(sample[4] < 0 && sample[5] < 0, 'C-stick has independent diagonal axes');

  await pointer('[data-touch-button="B"]', 'pointerup', 102, 0, 0);
  await pointer('[data-touch-button="L"]', 'pointercancel', 103, 0, 0);
  sample = await pad();
  assert.equal(sample[1] & 512, 0, 'B release leaves other fingers held');
  assert(sample[1] & 256 && sample[1] & 32 && sample[1] & 16, 'A/R/Z stay held after B and L release');
  assert.deepEqual(sample.slice(6, 8), [0, 255], 'canceling L releases only the left analog trigger');

  // Visibility and focus loss release all owners. Re-arm to test the other
  // cleanup path, then opening Controls and changing the source clears input.
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  sample = await pad();
  assert.deepEqual(sample.slice(1, 8), [0, 0, 0, 0, 0, 0, 0], 'window blur clears all held buttons and axes');
  await pointer('[data-touch-button="A"]', 'pointerdown', 201, 0, 0);
  await pointer('[data-touch-button="L"]', 'pointerdown', 202, 0, 0);
  await pointer('[data-touch-stick="main"]', 'pointerdown', 203, main.x, main.y);
  await page.evaluate(() => {
    const original = Object.getOwnPropertyDescriptor(document, 'hidden');
    Object.defineProperty(document, 'hidden', {configurable: true, value: true});
    document.dispatchEvent(new Event('visibilitychange'));
    if (original) Object.defineProperty(document, 'hidden', original);
    else delete document.hidden;
  });
  sample = await pad();
  assert.deepEqual(sample.slice(1, 8), [0, 0, 0, 0, 0, 0, 0], 'hidden-document transition clears all input');
  await pointer('[data-touch-button="A"]', 'pointerdown', 301, 0, 0);
  await page.evaluate(() => window.meleeControllerSettings.setSource(0, 'keyboard'));
  await waitSource('keyboard');
  assert(await page.locator('#touch-controls').isHidden(), 'source switch hides the overlay');
  sample = await pad();
  assert.deepEqual(sample.slice(0, 8), [0, 0, 0, 0, 0, 0, 0, 0], 'source switch releases touch from P1');
  await page.evaluate(() => window.meleeControllerSettings.setSource(0, 'touch'));
  await waitSource('touch');
  await pointer('[data-touch-button="A"]', 'pointerdown', 302, 0, 0);
  await page.locator('#controls-open').click();
  sample = await pad();
  assert.deepEqual(sample.slice(1, 8), [0, 0, 0, 0, 0, 0, 0], 'opening Controls clears all touch input');
  await source(1).selectOption('keyboard');
  await waitSource('keyboard');
  assert(await page.locator('#touch-controls').isHidden(), 'Controls source selection hides the overlay');
  await source(1).selectOption('touch');
  await waitSource('touch');
  await page.locator('#controls-close').click();
  await pointer('[data-touch-button="A"]', 'pointerdown', 402, 0, 0);
  await page.evaluate(() => window.meleeControllerSettings.destroy());
  assert.equal((await pad())[1], 0, 'settings teardown releases virtual inputs');
  assert(await page.locator('#touch-controls').isHidden(), 'settings teardown removes the overlay');

  await page.reload();
  await driver.waitForImport();
  await page.locator('#controls-open').click();
  assert.equal(await source(1).inputValue(), 'touch', 'touch source persists through reload');
  assert.equal(await page.locator('#touch-opacity').inputValue(), '0.65', 'opacity persists through reload');
  await page.screenshot({path: path.join(values.out, 'touch-settings-persisted.png'), fullPage: true});
  report.screenshots.push('touch-settings-persisted.png');
  assert.deepEqual(errors, []);
  report.result = 'pass';
  report.game = 'Owned disc reached original CSS (phase 1) in the development runtime';
  report.layout = {portrait: [390, 844], landscape: [1280, 720], safeAreaValues: gameState.safe};
  report.input = 'Independent synthetic PointerEvents; A/B/X/Y/L/R/Z/dpad + both sticks; independent up/cancel; blur/visibility/source-switch/Controls/Eject/teardown cleanup; PAD ABI readback.';
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log('Touch controls: owned-disc CSS, multi-pointer PAD mapping, cleanup, persistence and responsive 4:3 layout pass.');
} catch (error) {
  report.result = 'fail'; report.failure = String(error);
  report.diagnostics = await driver.diagnostics();
  await page.screenshot({path: path.join(values.out, 'failure.png'), fullPage: true}).catch(() => {});
  await fs.writeFile(path.join(values.out, 'failure.txt'), `${String(error)}\n${await page.locator('body').innerText().catch(() => '')}`);
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  throw error;
} finally {
  driver.dispose();
  await browser.close();
}
