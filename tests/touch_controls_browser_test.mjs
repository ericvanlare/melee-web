#!/usr/bin/env node
/** Browser-coordinate touch controls and PAD boundary against an owned disc. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'playwright', 'out', 'surface'].map(name => [name, {type: 'string'}]))});
values.surface ||= 'development';
if (!values.url || !values.disc || !values.out || !['development', 'public'].includes(values.surface))
  throw Error('Use --url PLAYER_URL --disc OWNED_DISC --out NEW_DIRECTORY [--surface development|public] [--playwright PACKAGE_DIR]');
await fs.mkdir(path.dirname(values.out), {recursive: true});
await fs.mkdir(values.out);

const {chromium, browser: installedBrowser} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(installedBrowser));
const context = await browser.newContext({viewport: {width: 390, height: 844}, deviceScaleFactor: 1,
  isMobile: true, hasTouch: true});
const page = await context.newPage();
const cdp = await context.newCDPSession(page);
await cdp.send('Emulation.setTouchEmulationEnabled', {enabled: true, maxTouchPoints: 10});
await page.addInitScript(() => {
  window.__meleeTouchPointerTrace = [];
  window.__meleeTouchSyntheticInput = {mouseDown: 0, keyDown: 0};
  document.addEventListener('pointerdown', event => {
    const element = event.target?.closest?.('[data-touch-button], [data-touch-stick]');
    window.__meleeTouchPointerTrace.push({pointerId: event.pointerId, pointerType: event.pointerType,
      type: 'pointerdown', action: element?.dataset?.touchButton || element?.dataset?.touchStick || null,
      target: event.target?.id || event.target?.className || event.target?.tagName || null,
      x: event.clientX, y: event.clientY});
  }, true);
  document.addEventListener('pointermove', event => {
    const element = event.target?.closest?.('[data-touch-button], [data-touch-stick]');
    window.__meleeTouchPointerTrace.push({type: 'pointermove', pointerId: event.pointerId,
      pointerType: event.pointerType, action: element?.dataset?.touchButton || element?.dataset?.touchStick || null,
      x: event.clientX, y: event.clientY});
  }, true);
  document.addEventListener('touchstart', event => {
    const element = event.target?.closest?.('[data-touch-button], [data-touch-stick]');
    window.__meleeTouchPointerTrace.push({type: 'touchstart', action:
      element?.dataset?.touchButton || element?.dataset?.touchStick || null,
      target: event.target?.id || event.target?.className || event.target?.tagName || null,
      touches: [...event.changedTouches].map(touch => ({id: touch.identifier, x: touch.clientX, y: touch.clientY}))});
  }, true);
  for (const type of ['pointerup', 'pointercancel', 'gotpointercapture', 'lostpointercapture'])
    document.addEventListener(type, event => window.__meleeTouchPointerTrace.push({type,
      pointerId: event.pointerId, pointerType: event.pointerType,
      action: event.target?.dataset?.touchButton || event.target?.dataset?.touchStick || null}), true);
  document.addEventListener('mousedown', event => {
    if (event.target?.closest?.('#touch-controls')) window.__meleeTouchSyntheticInput.mouseDown++;
  }, true);
  document.addEventListener('keydown', event => {
    if (event.target?.closest?.('#touch-controls')) window.__meleeTouchSyntheticInput.keyDown++;
  }, true);
});
const driver = createBrowserDriver(page, {surface: values.surface, timeoutMs: 120000});
const errors = [];
page.on('pageerror', error => errors.push(error.message));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });

const source = port => page.getByLabel(`Player ${port} input source`, {exact: true});
const waitSource = mode => page.waitForFunction(mode => Module.meleeControllers.getPortSource(0) === mode, mode);
const pad = () => page.evaluate(() => {
  const samples = new Int32Array(32);
  Module.meleeControllers.writeSamples(samples, 0);
  return [...samples.slice(0, 16)];
});
const emptyPlayerPad = sample => assert.deepEqual(sample.slice(1, 8), [0, 0, 0, 0, 0, 0, 0],
  'all held buttons, axes, and analog triggers return to neutral');
const bitFor = Object.freeze({A: 0x100, B: 0x200, X: 0x400, Y: 0x800, L: 0x40, R: 0x20,
  Z: 0x10, Start: 0x1000, Up: 0x8, Down: 0x4, Left: 0x1, Right: 0x2});

let activeTouches = new Map();
let nextTouchId = 10000;
const nextFrame = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(resolve)));
function point(id, position, state) {
  return {id, x: position.x, y: position.y, radiusX: 1, radiusY: 1, force: 1, state};
}
async function dispatch(type, points = []) {
  await cdp.send('Input.dispatchTouchEvent', {type, touchPoints: points});
}
async function touchStart(id, x, y) {
  const existed = activeTouches.has(id);
  activeTouches.set(id, {x, y});
  await dispatch('touchStart', [...activeTouches].map(([touchId, position]) =>
    point(touchId, position, touchId === id && !existed ? 'touchPressed' : 'touchStationary')));
  await nextFrame();
}
async function touchMove(id, x, y) {
  assert(activeTouches.has(id), `touch ${id} must already be active`);
  activeTouches.set(id, {x, y});
  await dispatch('touchMove', [...activeTouches].map(([touchId, position]) =>
    point(touchId, position, touchId === id ? 'touchMoved' : 'touchStationary')));
  await nextFrame();
}
async function touchEnd(id) {
  assert(activeTouches.has(id), `touch ${id} must already be active`);
  const released = activeTouches.get(id);
  activeTouches.delete(id);
  // CDP touchEnd applies to the listed IDs. Dispatch only the finger being
  // lifted so the other active contacts remain down independently.
  await dispatch('touchEnd', [point(id, released, 'touchReleased')]);
  await nextFrame();
}
async function touchEndAll() {
  const points = [...activeTouches].map(([id, position]) => point(id, position, 'touchReleased'));
  activeTouches.clear();
  await dispatch('touchEnd', points);
  await nextFrame();
}
async function touchCancelAll() {
  activeTouches.clear();
  await dispatch('touchCancel');
  await nextFrame();
}
async function rects() {
  return page.evaluate(() => {
    const canvas = document.querySelector('#canvas'), overlay = document.querySelector('#touch-controls');
    const json = element => {
      const r = element.getBoundingClientRect();
      return {left: r.left, top: r.top, right: r.right, bottom: r.bottom, width: r.width, height: r.height};
    };
    return {canvas: json(canvas), overlay: json(overlay), toolbar: json(document.querySelector('#toolbar')),
      controls: [...document.querySelectorAll('#touch-controls [data-touch-button], #touch-controls [data-touch-stick]')].map(element => {
        const r = json(element);
        return {kind: element.dataset.touchButton ? 'button' : 'stick', action: element.dataset.touchButton || element.dataset.touchStick,
          selector: element.dataset.touchButton ? `[data-touch-button="${element.dataset.touchButton}"]` : `[data-touch-stick="${element.dataset.touchStick}"]`,
          ...r, x: r.left + r.width / 2, y: r.top + r.height / 2};
      }), safe: ['left', 'top', 'right', 'bottom'].map(side =>
        getComputedStyle(overlay).getPropertyValue(`--touch-safe-${side}`).trim()),
      fullscreenEnabled: document.fullscreenEnabled === true};
  });
}
async function setExpanded(enabled) {
  await page.evaluate(enabled => {
    const player = document.querySelector('#player');
    player.classList.toggle('player-expanded', enabled);
    player.classList.remove('toolbar-actions-open');
    document.documentElement.classList.toggle('player-expanded', enabled);
    document.body.classList.toggle('player-expanded', enabled);
    const more = document.querySelector('#toolbar-more-toggle');
    more.setAttribute('aria-expanded', 'false');
    more.textContent = 'More controls';
  }, enabled);
  await page.waitForFunction(() => {
    const canvas = document.querySelector('#canvas').getBoundingClientRect();
    const overlay = document.querySelector('#touch-controls').getBoundingClientRect();
    return Math.abs(canvas.left - overlay.left) < 1 && Math.abs(canvas.top - overlay.top) < 1 &&
      Math.abs(canvas.width - overlay.width) < 1 && Math.abs(canvas.height - overlay.height) < 1;
  });
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
}
async function controlStateAt(control) {
  return page.evaluate(({selector, x, y}) => {
    const element = document.querySelector(`#touch-controls ${selector}`);
    const hit = document.elementFromPoint(x, y);
    return {reachable: element === hit || element.contains(hit), hit: hit?.className || hit?.tagName || null};
  }, control);
}
async function refreshControl(control) {
  return page.locator(`#touch-controls ${control.selector}`).evaluate((element, previous) => {
    const r = element.getBoundingClientRect();
    return {...previous, left: r.left, top: r.top, right: r.right, bottom: r.bottom,
      width: r.width, height: r.height, x: r.left + r.width / 2, y: r.top + r.height / 2};
  }, control);
}
async function assertPointerCapture(control, action, traceStart, position) {
  await page.waitForFunction(({action, traceStart}) => window.__meleeTouchPointerTrace
    .slice(traceStart).some(record => record.action === action && record.pointerType === 'touch'),
  {action, traceStart}, {timeout: 1500}).catch(() => {});
  const pointer = await page.evaluate(({action, traceStart}) => window.__meleeTouchPointerTrace
    .slice(traceStart).reverse().find(record => record.action === action && record.pointerType === 'touch') || null,
  {action, traceStart});
  if (!pointer) {
    const events = await page.evaluate(() => window.__meleeTouchPointerTrace.slice(-12));
    const context = await page.evaluate(position => {
      const r = document.querySelector(`#touch-controls ${position.selector}`).getBoundingClientRect();
      const hit = document.elementFromPoint(position.x, position.y);
      return {point: position, rect: {x: r.x, y: r.y, width: r.width, height: r.height},
        hit: hit?.id || hit?.className || hit?.tagName || null, scroll: [scrollX, scrollY],
        viewport: {width: visualViewport.width, height: visualViewport.height, scale: visualViewport.scale,
          offsetLeft: visualViewport.offsetLeft, offsetTop: visualViewport.offsetTop}};
    }, {...position, selector: control.selector});
    assert(pointer, `${action} receives a new browser touch pointer event; trace=${JSON.stringify({events, context})}`);
  }
  const selector = `#touch-controls ${control.selector}`;
  await page.waitForFunction(({selector, id}) => document.querySelector(selector)?.hasPointerCapture(id),
    {selector, id: pointer.pointerId}, {timeout: 1500}).catch(() => {});
  const captured = await page.locator(selector).evaluate((element, id) => element.hasPointerCapture(id), pointer.pointerId);
  assert(captured, `${action} captures its active browser touch pointer; trace=${JSON.stringify(
    await page.evaluate(() => window.__meleeTouchPointerTrace.slice(-8)))}`);
}
async function testButton(control, touchId) {
  control = await refreshControl(control);
  const hit = await controlStateAt(control);
  assert(hit.reachable, `${control.action} live center is browser-hit-test reachable; got ${hit.hit}`);
  const traceStart = await page.evaluate(() => window.__meleeTouchPointerTrace.length);
  await touchStart(touchId, control.x, control.y);
  await assertPointerCapture(control, control.action, traceStart, {x: control.x, y: control.y});
  let sample = await pad();
  assert.equal(sample[0], 1, `${control.action} has an active P1 PAD sample`);
  assert(sample[1] & bitFor[control.action], `${control.action} center produces the original PAD bit`);
  if (control.action === 'L') assert.equal(sample[6], 255, 'L sets full analog trigger pressure');
  if (control.action === 'R') assert.equal(sample[7], 255, 'R sets full analog trigger pressure');
  await touchEnd(touchId);
  sample = await pad();
  assert.equal(sample[1] & bitFor[control.action], 0, `${control.action} releases independently`);
  if (control.action === 'L') assert.equal(sample[6], 0, 'L release clears analog pressure');
  if (control.action === 'R') assert.equal(sample[7], 0, 'R release clears analog pressure');
}
async function testStick(control, touchId) {
  control = await refreshControl(control);
  const hit = await controlStateAt(control);
  assert(hit.reachable, `${control.action} live center is browser-hit-test reachable; got ${hit.hit}`);
  const traceStart = await page.evaluate(() => window.__meleeTouchPointerTrace.length);
  await touchStart(touchId, control.x, control.y);
  await assertPointerCapture(control, control.action, traceStart, {x: control.x, y: control.y});
  const radius = Math.min(control.width, control.height) * 0.42;
  const direction = control.action === 'main' ? 1 : -1;
  await touchMove(touchId, control.x + direction * radius * 0.32, control.y + radius * 0.27);
  let sample = await pad();
  const index = control.action === 'main' ? 2 : 4;
  assert.notEqual(sample[index], 0, `${control.action} coordinate movement reaches its PAD axis`);
  assert.notEqual(sample[index + 1], 0, `${control.action} diagonal coordinate movement reaches its second PAD axis`);
  await touchEnd(touchId);
  sample = await pad();
  assert.deepEqual(sample.slice(index, index + 2), [0, 0], `${control.action} release returns both axes to neutral`);
}
async function assertLayout(size, mode, captures = false) {
  await page.setViewportSize({width: size[0], height: size[1]});
  await setExpanded(mode === 'expanded');
  const geometry = await rects();
  assert(Math.abs(geometry.canvas.width / geometry.canvas.height - 4 / 3) < 0.01,
    `${size.join('x')} ${mode} canvas remains 4:3`);
  assert(Math.abs(geometry.overlay.width - geometry.canvas.width) < 1 &&
    Math.abs(geometry.overlay.height - geometry.canvas.height) < 1, `${size.join('x')} ${mode} overlay follows the fitted canvas`);
  if (mode === 'expanded')
    assert(geometry.canvas.top >= geometry.toolbar.bottom - 1, `${size.join('x')} expanded mode reserves toolbar space above the game`);
  for (const control of geometry.controls) {
    if (control.kind === 'button') await testButton(control, nextTouchId++);
    else await testStick(control, nextTouchId++);
  }
  if (mode === 'expanded') {
    // Exercise the real toolbar toggle as well as the page's collapsed
    // expanded state. The secondary row reserves additional game space.
    await page.locator('#toolbar-more-toggle').click();
    await page.waitForFunction(() => {
      if (!document.querySelector('#player').classList.contains('toolbar-actions-open')) return false;
      const canvas = document.querySelector('#canvas').getBoundingClientRect();
      const overlay = document.querySelector('#touch-controls').getBoundingClientRect();
      return Math.abs(canvas.left - overlay.left) < 1 && Math.abs(canvas.top - overlay.top) < 1 &&
        Math.abs(canvas.width - overlay.width) < 1 && Math.abs(canvas.height - overlay.height) < 1;
    });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    const menu = await rects();
    assert(menu.canvas.top >= menu.toolbar.bottom - 1,
      `${size.join('x')} expanded toolbar menu reserves its full height above gameplay`);
    assert(Math.abs(menu.canvas.width / menu.canvas.height - 4 / 3) < 0.01,
      `${size.join('x')} expanded toolbar menu preserves 4:3 rendering`);
    assert(Math.abs(menu.overlay.left - menu.canvas.left) < 1 && Math.abs(menu.overlay.top - menu.canvas.top) < 1 &&
      Math.abs(menu.overlay.width - menu.canvas.width) < 1 &&
      Math.abs(menu.overlay.height - menu.canvas.height) < 1,
    `${size.join('x')} expanded toolbar menu keeps touch overlay fitted to the game`);
    for (const id of ['fullscreen', 'controls-open', 'toolbar-more-toggle']) {
      const reachable = await page.locator(`#${id}`).evaluate(element => {
        const r = element.getBoundingClientRect();
        const hit = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return !element.disabled && r.width > 0 && r.height > 0 && (hit === element || element.contains(hit));
      });
      assert(reachable, `${id} remains visible and reachable in the expanded toolbar menu`);
    }
    for (const control of menu.controls) {
      const hit = await controlStateAt(control);
      assert(hit.reachable, `${size.join('x')} expanded menu leaves ${control.action} reachable; got ${hit.hit}`);
      if (control.kind === 'button') await testButton(control, nextTouchId++);
      else await testStick(control, nextTouchId++);
    }
    report.layouts.push({viewport: size, mode: 'expanded-more', canvas: menu.canvas,
      overlay: menu.overlay, target_count: menu.controls.length, toolbar_actions_reachable: true});
    await page.locator('#toolbar-more-toggle').click();
  }
  if (captures) {
    const directory = path.join(values.out, `${size[0]}x${size[1]}-${mode}`);
    await fs.mkdir(directory, {recursive: true});
    await page.screenshot({path: path.join(directory, 'overlay.png'), fullPage: true});
    report.screenshots.push(`${size[0]}x${size[1]}-${mode}/overlay.png`);
  }
  report.layouts.push({viewport: size, mode, canvas: geometry.canvas, overlay: geometry.overlay,
    target_count: geometry.controls.length, safe_area_css_px: geometry.safe});
  return geometry;
}

const report = {schema: 'webmelee-touch-controls-coordinate-browser-v2', browser: browser.version(),
  browser_mode: 'headless installed Chrome with mobile viewport/touch emulation', surface: values.surface,
  url: values.url, disc: path.basename(values.disc), screenshots: [], layouts: [],
  scope: 'Owned-disc original CSS on the requested player surface; coordinate-based Chrome DevTools touch dispatch through browser hit testing, pointer capture and the shared PAD writer. No physical phone, iOS fullscreen, timing or retail-equivalence claim.'};

try {
  await page.goto(values.url);
  report.gpu = await page.evaluate(async () => {
    const adapter = await navigator.gpu?.requestAdapter();
    return {isolated: crossOriginIsolated, available: !!adapter,
      vendor: adapter?.info?.vendor || null, architecture: adapter?.info?.architecture || null,
      fallback: adapter?.info?.isFallbackAdapter ?? null};
  });
  assert(report.gpu.isolated && report.gpu.available, 'The actual player needs an isolated WebGPU adapter');
  await driver.waitForImport();
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
  if (values.surface === 'development') {
    await driver.waitForStart();
    await driver.launch();
  } else {
    const state = await driver.waitForPublicCss();
    if (state === 'audio-recovery-required') await driver.recoverAudioActivation();
  }
  await page.waitForFunction(() => Module._melee_web_native_menu_phase() === 1 &&
    Module._melee_web_native_menu_running() === 1, null, {timeout: 120000});
  let initial = await rects();
  assert(Math.abs(initial.canvas.width / initial.canvas.height - 4 / 3) < 0.01, 'owned disc CSS is displayed at 4:3');
  assert(Math.abs(initial.overlay.width - initial.canvas.width) < 1 &&
    Math.abs(initial.overlay.height - initial.canvas.height) < 1, 'overlay matches the actual packaged/development canvas');
  await page.screenshot({path: path.join(values.out, 'touch-overlay-portrait.png'), fullPage: true});
  report.screenshots.push('touch-overlay-portrait.png');
  await page.locator('#controls-open').click();
  assert.equal(await source(1).inputValue(), 'touch');
  assert.equal(await opacity.inputValue(), '0.65');
  await page.screenshot({path: path.join(values.out, 'touch-settings-portrait.png'), fullPage: true});
  report.screenshots.push('touch-settings-portrait.png');
  await page.locator('#controls-close').click();

  // Every rendered control is tapped at its center using browser coordinate
  // dispatch. This exercises real hit testing instead of dispatching to the
  // selected node and proves each intended location produces PAD state.
  for (const [size, mode] of [
    [[320, 568], 'normal'], [[320, 568], 'expanded'],
    [[390, 844], 'normal'], [[390, 844], 'expanded'],
    [[667, 375], 'normal'], [[667, 375], 'expanded'],
    [[844, 390], 'normal'], [[844, 390], 'expanded'],
  ]) await assertLayout(size, mode, size[0] === 320 && mode === 'normal' || size[0] === 844 && mode === 'expanded');

  // Safari safe-area env values are 0 in headless Chrome. Apply a representative
  // inset at the real overlay root to exercise its edge anchors and retest every
  // hit target for a layout that Chrome itself cannot supply on this machine.
  await page.setViewportSize({width: 320, height: 568});
  await setExpanded(false);
  await page.evaluate(() => {
    const overlay = document.querySelector('#touch-controls');
    for (const side of ['left', 'top', 'right', 'bottom']) overlay.style.setProperty(`--touch-safe-${side}`, '12px');
  });
  let safeGeometry = await rects();
  for (const control of safeGeometry.controls) {
    const hit = await controlStateAt(control);
    assert(hit.reachable, `12px emulated safe-area inset keeps ${control.action} center reachable; got ${hit.hit}`);
  }
  report.safe_area_fixture = {insets_css_px: 12, targets_reachable: safeGeometry.controls.length,
    limitation: 'Emulated CSS custom properties in desktop Chrome; physical cutout/browser safe-area behavior remains device-only.'};
  await page.evaluate(() => {
    const overlay = document.querySelector('#touch-controls');
    for (const side of ['left', 'top', 'right', 'bottom']) overlay.style.removeProperty(`--touch-safe-${side}`);
  });

  // Both sticks receive unclamped off-axis movement outside their visible rim.
  for (const [name, id, sign, axisIndex] of [['main', 701, 1, 2], ['cstick', 702, -1, 4]]) {
    const control = (await rects()).controls.find(row => row.action === name);
    const hit = await controlStateAt(control);
    assert(hit.reachable, `${name} center is reachable before off-axis drag`);
    await touchStart(id, control.x, control.y);
    const radius = Math.min(control.width, control.height) * 0.42;
    await touchMove(id, control.x + sign * radius * 2, control.y + radius * 0.5);
    const sample = await pad();
    assert(Math.abs(sample[axisIndex]) >= 120, `${name} clamps magnitude to the PAD range beyond the rim`);
    assert(Math.abs(Math.abs(sample[axisIndex] / sample[axisIndex + 1]) - 4) < 0.15,
      `${name} preserves a 4:1 off-axis direction beyond the rim`);
    await touchEnd(id);
    assert.deepEqual((await pad()).slice(axisIndex, axisIndex + 2), [0, 0], `${name} release outside the rendered rim clears both axes`);
  }

  // Independent browser touch IDs can move both sticks while A attacks, X
  // jumps, and L shields. Removing one finger must not release its neighbors.
  const combined = (await rects()).controls;
  const combinedTraceStart = await page.evaluate(() => window.__meleeTouchPointerTrace.length);
  const at = action => combined.find(row => row.action === action);
  await touchStart(801, at('main').x, at('main').y);
  await touchStart(802, at('cstick').x, at('cstick').y);
  await touchStart(803, at('A').x, at('A').y);
  await touchStart(804, at('X').x, at('X').y);
  await touchStart(805, at('L').x, at('L').y);
  await touchMove(801, at('main').x + 24, at('main').y + 20);
  await touchMove(802, at('cstick').x - 22, at('cstick').y + 18);
  let sample = await pad();
  for (const action of ['A', 'X', 'L']) assert(sample[1] & bitFor[action], `${action} remains held concurrently`);
  assert(sample[2] > 0 && sample[3] < 0 && sample[4] < 0 && sample[5] < 0,
    `main and C-stick move independently at the same time as face and shoulder input; axes=${sample.slice(2, 6)}; trace=${JSON.stringify(
      await page.evaluate(start => window.__meleeTouchPointerTrace.slice(start), combinedTraceStart))}`);
  assert.deepEqual(sample.slice(6, 8), [255, 0], 'L maintains full analog shield pressure');
  await touchEnd(803);
  sample = await pad();
  assert.equal(sample[1] & bitFor.A, 0,
    `releasing the A finger independently clears only A; buttons=${sample[1].toString(16)}; trace=${JSON.stringify(await page.evaluate(start => window.__meleeTouchPointerTrace.slice(start), combinedTraceStart))}`);
  assert(sample[1] & bitFor.X && sample[1] & bitFor.L,
    `X jump and L shield remain held after A release; buttons=${sample[1].toString(16)}; trace=${JSON.stringify(await page.evaluate(start => window.__meleeTouchPointerTrace.slice(start), combinedTraceStart))}`);
  assert.notEqual(sample[2], 0); assert.notEqual(sample[4], 0);
  await touchEnd(805);
  sample = await pad();
  assert.equal(sample[1] & bitFor.L, 0, 'releasing L clears its digital shield bit');
  assert.deepEqual(sample.slice(6, 8), [0, 0], 'independent L release clears trigger pressure');
  assert(sample[1] & bitFor.X, 'the independent jump finger remains held');
  await touchEnd(802);
  sample = await pad();
  assert.deepEqual(sample.slice(4, 6), [0, 0], 'C-stick finger release clears only its axes');
  assert.notEqual(sample[2], 0); assert(sample[1] & bitFor.X);
  await touchCancelAll();
  emptyPlayerPad(await pad());

  // Browser pointercancel, focus loss, visibility loss, geometry changes,
  // Controls opening, source changes, and teardown all clear held contacts.
  const normal = await rects();
  const aButton = normal.controls.find(row => row.action === 'A');
  await touchStart(901, aButton.x, aButton.y);
  await touchCancelAll();
  emptyPlayerPad(await pad());
  await touchStart(902, aButton.x, aButton.y);
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  emptyPlayerPad(await pad());
  await touchEndAll();
  await touchStart(903, aButton.x, aButton.y);
  await page.evaluate(() => {
    const original = Object.getOwnPropertyDescriptor(document, 'hidden');
    Object.defineProperty(document, 'hidden', {configurable: true, value: true});
    document.dispatchEvent(new Event('visibilitychange'));
    if (original) Object.defineProperty(document, 'hidden', original); else delete document.hidden;
  });
  emptyPlayerPad(await pad());
  await touchEndAll();

  await touchStart(904, aButton.x, aButton.y);
  await page.setViewportSize({width: 667, height: 375});
  await page.evaluate(() => window.dispatchEvent(new Event('orientationchange')));
  await page.waitForFunction(() => getComputedStyle(document.querySelector('#touch-controls')).width !== '0px');
  emptyPlayerPad(await pad());
  await touchEndAll();
  await assertLayout([667, 375], 'normal');

  const resized = (await rects()).controls.find(row => row.action === 'A');
  await touchStart(905, resized.x, resized.y);
  await page.locator('#controls-open').click();
  emptyPlayerPad(await pad());
  assert.equal(await page.locator('#controls-dialog').evaluate(element => element.open), true,
    'Controls opens above the game and touch overlay');
  await page.locator('#controls-close').click();
  await touchEndAll();

  await touchStart(906, resized.x, resized.y);
  // Keep Controls closed while a touch is held so opening the dialog cannot
  // itself clear the pointer before the source-change release path runs.
  await source(1).evaluate(select => {
    select.value = 'keyboard';
    select.dispatchEvent(new Event('change', {bubbles: true}));
  });
  await waitSource('keyboard');
  emptyPlayerPad(await pad());
  assert(await page.locator('#touch-controls').isHidden(), 'switching away from touch hides the overlay');
  await touchEndAll();
  await page.locator('#controls-open').click();
  await source(1).selectOption('touch');
  await waitSource('touch');
  await page.locator('#controls-close').click();
  const beforeTeardown = (await rects()).controls.find(row => row.action === 'A');
  await touchStart(907, beforeTeardown.x, beforeTeardown.y);
  if (values.surface === 'development') {
    await page.evaluate(() => window.meleeControllerSettings.destroy());
    emptyPlayerPad(await pad());
    assert(await page.locator('#touch-controls').isHidden(), 'settings teardown removes the overlay');
    await touchEndAll();
  } else {
    await page.evaluate(() => {
      window.addEventListener('beforeunload', () => {
        const samples = new Int32Array(32);
        Module.meleeControllers.writeSamples(samples, 0);
        sessionStorage.setItem('__touchPadBeforeEject', JSON.stringify([...samples.slice(0, 8)]));
      }, {once: true});
      const eject = document.querySelector('#end-session');
      if (eject.disabled) throw Error('Eject became disabled before touch cleanup could be checked');
      eject.click();
    });
    await page.waitForFunction(() => {
      if (sessionStorage.getItem('__touchPadBeforeEject')) return true;
      const manager = Module?.meleeControllers;
      if (!manager) return false;
      const samples = new Int32Array(32);
      manager.writeSamples(samples, 0);
      return samples.slice(1, 8).every(value => value === 0);
    }, null, {timeout: 10000});
    const ejectedPad = await page.evaluate(() => {
      const saved = sessionStorage.getItem('__touchPadBeforeEject');
      if (saved) return JSON.parse(saved);
      const samples = new Int32Array(32);
      Module.meleeControllers.writeSamples(samples, 0);
      return [...samples.slice(0, 8)];
    });
    assert.deepEqual(ejectedPad.slice(1), [0, 0, 0, 0, 0, 0, 0], 'Eject releases all touch controls before teardown');
    await touchCancelAll();
  }

  // Rebind after the cleanup test and prove source/opacity persistence.
  await page.reload();
  await driver.waitForImport();
  await page.locator('#controls-open').click();
  assert.equal(await source(1).inputValue(), 'touch', 'touch source persists through reload');
  assert.equal(await page.locator('#touch-opacity').inputValue(), '0.65', 'opacity persists through reload');
  await page.locator('#controls-close').click();

  // Native fullscreen uses the real button's user activation and the overlay
  // stays within its element. Unsupported/rejected cases are covered in the
  // shell tests; Chrome may legitimately omit the native path in a headless run.
  await page.setViewportSize({width: 844, height: 390});
  const nativeAvailable = await page.evaluate(() => document.fullscreenEnabled === true &&
    typeof document.querySelector('#player').requestFullscreen === 'function');
  if (nativeAvailable) {
    await page.locator('#fullscreen').click();
    await page.waitForFunction(() => !!document.fullscreenElement ||
      document.querySelector('#player').classList.contains('player-expanded') ||
      /declined fullscreen/i.test(document.querySelector('#fullscreen-status').textContent));
    if (await page.evaluate(() => !!document.fullscreenElement)) {
      await page.waitForFunction(() => {
        const c = document.querySelector('#canvas').getBoundingClientRect();
        const o = document.querySelector('#touch-controls').getBoundingClientRect();
        return Math.abs(c.width - o.width) < 1 && Math.abs(c.height - o.height) < 1;
      });
      const full = await rects();
      assert(Math.abs(full.canvas.width / full.canvas.height - 4 / 3) < 0.01, 'native fullscreen retains 4:3 rendering');
      for (const control of full.controls) {
        const hit = await controlStateAt(control);
        assert(hit.reachable, `native fullscreen leaves ${control.action} reachable; got ${hit.hit}`);
        if (control.kind === 'button') await testButton(control, nextTouchId++);
        else await testStick(control, nextTouchId++);
      }
      await page.locator('#fullscreen').click();
      await page.waitForFunction(() => !document.fullscreenElement);
      report.native_fullscreen = 'supported by headless installed Chrome; entered, exercised overlay, and exited from direct button gestures';
    } else report.native_fullscreen = 'request rejected by headless browser; application displayed its page-expansion fallback';
  } else report.native_fullscreen = 'unsupported in this headless Chrome configuration; native behavior remains device/browser dependent';

  const synthetic = await page.evaluate(() => window.__meleeTouchSyntheticInput);
  assert.deepEqual(synthetic, {mouseDown: 0, keyDown: 0}, 'touch controls emit no duplicate synthetic mouse or keyboard gameplay input');
  assert.deepEqual(errors, []);
  report.result = 'pass';
  report.game = values.surface === 'development'
    ? 'Owned disc reached original CSS in the development runtime'
    : 'Owned disc reached original CSS in the production audio-player package';
  report.input = 'CDP coordinate touch through browser hit testing and pointer capture; every rendered button/stick sampled through shared PAD writer; dual-stick plus A/X/L, independent release, off-axis radial clamp, cancel/blur/visibility/resize/Controls/source-switch/teardown cleanup; 4:1 direction preserved for both sticks.';
  report.synthetic_mouse_keyboard_events = synthetic;
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(`Touch controls: ${values.surface} owned-disc CSS, coordinate hit testing, multi-touch PAD input, cleanup, persistence and responsive 4:3 layouts pass.`);
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
