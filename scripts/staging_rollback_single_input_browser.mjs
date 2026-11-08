#!/usr/bin/env node
/*
 * First #375 menu-only control: one real local keyboard input drives original
 * CSS -> SSS -> ready Mario/FD/four-stock match. It intentionally stops before
 * any online agreement, rollback owner, gameplay input or result claim.
 */
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createReadStream} from 'node:fs';
import {createBrowserDriver} from './browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from './browser_tools.mjs';

const {values} = parseArgs({options: {
  disc: {type: 'string'},
  'served-root': {type: 'string'},
  output: {type: 'string'},
  playwright: {type: 'string'},
  'transition-only': {type: 'boolean', default: false},
  'timeout-ms': {type: 'string', default: '180000'},
}});
const root = values['served-root'] ? path.resolve(values['served-root']) : null;
const output = values.output ? path.resolve(values.output) : null;
const disc = values.disc || process.env.MELEE_DISC_IMAGE || null;
const timeoutMs = Number(values['timeout-ms']);
const transitionOnly = values['transition-only'];
if (!disc || !output || !root || !Number.isInteger(timeoutMs) || timeoutMs < 10000 || timeoutMs > 600000) {
  throw Error('Usage: node scripts/staging_rollback_single_input_browser.mjs --disc <local-iso> --served-root <immutable-served-build-dir> --output <new-external-dir> [--playwright <installed-package-dir>] [--timeout-ms 180000]');
}

const mediaTypes = new Map([
  ['.css', 'text/css; charset=utf-8'], ['.data', 'application/octet-stream'],
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'], ['.mjs', 'text/javascript; charset=utf-8'],
  ['.wasm', 'application/wasm'], ['.woff', 'font/woff'], ['.woff2', 'font/woff2'],
]);

async function hashFile(filename) {
  const hash = crypto.createHash('sha256');
  for await (const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest('hex');
}

async function makeStaticServer(directory) {
  const realRoot = await fs.realpath(directory);
  const server = http.createServer(async (request, response) => {
    try {
      const url = new URL(request.url || '/', 'http://127.0.0.1');
      const decoded = decodeURIComponent(url.pathname);
      const relative = decoded === '/' ? '/runtime.html' : decoded;
      const filename = path.resolve(realRoot, `.${relative}`);
      if (!filename.startsWith(`${realRoot}${path.sep}`)) throw Error('Static path escaped served root');
      const resolved = await fs.realpath(filename);
      if (!resolved.startsWith(`${realRoot}${path.sep}`)) throw Error('Static symlink escaped served root');
      const stat = await fs.stat(resolved);
      if (!stat.isFile()) throw Error('Static path is not a regular file');
      response.writeHead(200, {
        'Content-Type': mediaTypes.get(path.extname(resolved)) || 'application/octet-stream',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
        'Cache-Control': 'no-store',
      });
      createReadStream(resolved).pipe(response);
    } catch (error) {
      response.writeHead(error?.code === 'ENOENT' ? 404 : 400,
        {'Content-Type': 'text/plain; charset=utf-8'});
      response.end(String(error?.message || error));
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return {server, url: `http://127.0.0.1:${server.address().port}/runtime.html`};
}

const stageCursorMoved = ({x, y}) => {
  const sample = globalThis.menuObserveStage?.(32);
  return !!sample && Array.isArray(sample.geometry) &&
    (Math.abs(sample.geometry[0] - x) > 0.1 || Math.abs(sample.geometry[1] - y) > 0.1);
};

const report = {
  schema: 'melee-web-staging-rollback-single-input-menu-control-v1',
  scope: transitionOnly ?
    'one-page original CSS -> SSS source transition only; no peer, rollback, gameplay, Results, cadence, or human-input acceptance' :
    'one-page original CSS -> SSS -> ready match only; no peer, rollback, gameplay, Results, cadence, or human-input acceptance',
  input: {source: 'P1 Playwright keyboard events; raw PAD poll receipts plus source HSD_PadCopyStatus on menu buttons',
    keys: [], inputPollReceipts: [], sourcePadReceipts: [], directPadApiCalls: 0,
    p2KeyEvents: null},
  source: {initialCss: null, twoHumanCss: null, sss: null, firstReadyMatch: null},
  checks: [], cleanup: {browserClosed: false, serverClosed: false},
  result: 'not_started',
};

let server = null, browser = null, page = null, driver = null, launched = false;
let failure = null;
const ownedPagesErrors = [];
const deadline = Date.now() + timeoutMs;
const remaining = () => {
  const value = deadline - Date.now();
  if (value <= 0) throw Error('Single-input original-menu control exceeded its wall bound');
  return value;
};
const cssSetup = () => page.evaluate(() => globalThis.menuObserveCssSetup?.() ?? null);
const stageSetup = () => page.evaluate(() => globalThis.menuObserveStage?.(32) ?? null);
const inputSnapshot = () => page.evaluate(() => {
  const pointer = Module?._melee_web_input_message?.();
  return pointer ? JSON.parse(Module.UTF8ToString(pointer)) : null;
});
const nativeState = () => page.evaluate(() => ({
  phase: Module?._melee_web_native_menu_phase?.() ?? null,
  running: Module?._melee_web_native_menu_running?.() ?? null,
  error: document.querySelector('#status')?.dataset.runtimeError ||
    (document.querySelector('#error-dialog[open]') ?
      document.querySelector('#error')?.textContent || 'Application error' : null),
  input: Module?._melee_web_input_message?.()
    ? JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())) : null,
  menuMemory: Module?._melee_web_native_menu_memory?.()
    ? JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_memory())) : null,
}));

async function tap(key, label, holdMs = 160, releaseMs = 150) {
  remaining();
  report.input.keys.push({key, label, port: 0});
  const before = await inputSnapshot();
  const expectedButton = key === 'j' ? 0x0100 : key === 'Enter' ? 0x1000 : 0;
  const observed = await driver.pressChord([key], {
    holdMs,
    ...(expectedButton ? {observeBeforeRelease: {
      predicate: sourceButtonObserved,
      arg: {buttonMask: expectedButton, afterSample: before?.last_non_neutral?.sample || 0},
      timeoutMs: 500,
  }} : {}),
    releaseMs,
  });
  const state = await nativeState();
  if (state.error) throw Error(`${label}: ${state.error}`);
  if (expectedButton) {
    assert(observed?.sourcePad0 && observed.sourcePad0.err === 0 &&
      (observed.sourcePad0.button & expectedButton) === expectedButton,
    `${label}: original source did not expose the expected consumed P1 button: ${JSON.stringify(observed)}`);
    report.input.sourcePadReceipts.push({key, label, port: 0,
      source: 'HSD_PadCopyStatus[0] copied by the original menu source', ...observed});
  }
  const receipt = await waitForInputPollReceipt(before?.last_non_neutral?.sample || 0, key, label);
  report.input.inputPollReceipts.push(receipt);
}

async function nudge(direction, before, predicate, label) {
  const arg = {x: before.geometry[0], y: before.geometry[1]};
  report.input.keys.push({key: direction, label, port: 0});
  const inputBefore = await inputSnapshot();
  await driver.pressChord([direction], {
    holdMs: 0, releaseMs: 35,
    observeBeforeRelease: {predicate, arg, timeoutMs: 500},
  });
  const state = await nativeState();
  if (state.error) throw Error(`${label}: ${state.error}`);
  report.input.inputPollReceipts.push(await waitForInputPollReceipt(
    inputBefore?.last_non_neutral?.sample || 0, direction, label));
}

async function waitForInputPollReceipt(afterSample, key, label) {
  const expected = key === 'j' ? {button: 0x0100} : key === 'Enter' ? {button: 0x1000} :
    key === 'a' ? {stickX: -1} : key === 'd' ? {stickX: 1} :
    key === 's' ? {stickY: -1} : key === 'w' ? {stickY: 1} : null;
  assert(expected, `${label}: no source keyboard expectation for ${key}`);
  const result = await page.waitForFunction(({afterSample, expected}) => {
    try {
      const pointer = Module?._melee_web_input_message?.();
      if (!pointer) return false;
      const snapshot = JSON.parse(Module.UTF8ToString(pointer));
      const row = snapshot.last_non_neutral;
      if (!row || row.sample <= afterSample || row.port !== 0 || row.source !== 'keyboard' || row.err !== 0)
        return false;
      if (expected.button !== undefined && (row.buttons & expected.button) !== expected.button) return false;
      if (expected.stickX !== undefined && Math.sign(row.stick[0]) !== expected.stickX) return false;
      if (expected.stickY !== undefined && Math.sign(row.stick[1]) !== expected.stickY) return false;
      return {sample: row.sample, port: row.port, source: row.source, err: row.err,
        buttons: row.buttons, stick: row.stick, cstick: row.cstick,
        triggers: row.triggers, clamped: row.clamped};
    } catch { return false; }
  }, {afterSample, expected}, {timeout: remaining(), polling: 15});
  const receipt = await result.jsonValue();
  await result.dispose();
  return {label, key, ...receipt};
}

function sourceButtonObserved({buttonMask, afterSample}) {
  try {
    const inputPointer = Module?._melee_web_input_message?.();
    if (!inputPointer) return false;
    const input = JSON.parse(Module.UTF8ToString(inputPointer));
    const inputPad = input.last_non_neutral;
    if (!inputPad || inputPad.sample <= afterSample || inputPad.port !== 0 ||
        inputPad.source !== 'keyboard' || inputPad.err !== 0 ||
        (inputPad.buttons & buttonMask) !== buttonMask) return false;
    const pointer = Module?._melee_web_native_menu_memory?.();
    if (!pointer) return false;
    const memory = JSON.parse(Module.UTF8ToString(pointer));
    const sourcePad0 = memory.source_pad0;
    if (sourcePad0?.err !== 0 || (sourcePad0.button & buttonMask) !== buttonMask) return false;
    return {phase: memory.menu_phase, inputPoll: inputPad, sourcePad0};
  } catch { return false; }
}

async function waitForCss(predicate, label, timeout = 15000) {
  const stopAt = Math.min(deadline, Date.now() + timeout);
  let last = null;
  while (Date.now() < stopAt) {
    last = await cssSetup();
    if (last && predicate(last)) return last;
    const state = await nativeState();
    if (state.error) throw Error(`${label}: ${state.error}`);
    await page.waitForTimeout(35);
  }
  throw Error(`${label} did not reach its source state: ${JSON.stringify(last)}`);
}

async function waitForPhase(phase, label, timeoutMs = remaining()) {
  const result = await page.waitForFunction(({phase}) => {
    const error = document.querySelector('#status')?.dataset.runtimeError ||
      (document.querySelector('#error-dialog[open]') ? document.querySelector('#error')?.textContent : null);
    if (error) return {error};
    const module = globalThis.Module;
    return module?._melee_web_native_menu_phase?.() === phase &&
      module._melee_web_native_menu_running?.() ? {ready: true} : false;
  }, {phase}, {timeout: Math.min(remaining(), timeoutMs), polling: 20}).catch(async error => {
    const diagnostics = {state: null, css: null};
    try { diagnostics.state = await nativeState(); } catch (stateError) {
      diagnostics.state = {readError: stateError.message || String(stateError)};
    }
    try { diagnostics.css = await cssSetup(); } catch (cssError) {
      diagnostics.css = {readError: cssError.message || String(cssError)};
    }
    try {
      diagnostics.screenshot = path.join(output, 'original-css-transition-failure.png');
      await page.screenshot({path: diagnostics.screenshot});
    } catch (screenshotError) {
      diagnostics.screenshotError = screenshotError.message || String(screenshotError);
    }
    const failure = Error(`${label}: ${error.message}; observed=${JSON.stringify(diagnostics.state)}`);
    failure.diagnostics = diagnostics;
    throw failure;
  });
  const state = await result.jsonValue();
  await result.dispose();
  if (state?.error) throw Error(`${label}: ${state.error}`);
}

async function configureSingleLocalInput() {
  await page.locator('#controls-open').click({timeout: remaining()});
  await page.locator('#keyboard-layout').selectOption('two', {timeout: remaining()});
  await page.locator('#player-one-source').selectOption('keyboard', {timeout: remaining()});
  // Keep P2 present as the authored second Human slot, but send no P2 key event.
  await page.locator('#player-two-source').selectOption('keyboard', {timeout: remaining()});
  await page.locator('#controls-close').click({timeout: remaining()});
  await page.waitForFunction(() => {
    try {
      const input = JSON.parse(Module.UTF8ToString(Module._melee_web_input_message()));
      return input.keyboard_requested_mask === 3 && input.keyboard_active_mask === 3 &&
        input.keyboard_layout === 0 && input.physical_mask === 0;
    } catch { return false; }
  }, null, {timeout: remaining(), polling: 20});
  const css = await cssSetup();
  assert(css && css.doors?.length === 40 && css.geometry?.length === 48,
    'Original CSS read-only source observation unavailable after Controls setup');
  report.checks.push('P1 and P2 keyboard sources are available; every menu key event below is P1-only');
  return css;
}

async function verifyInitialTwoHumanCss() {
  let setup = await waitForCss(value => value.doors?.length === 40, 'initial original CSS');
  const initial = [0, 1, 2, 3].map(port => ({
    kind: setup.doors[port * 10], slotType: setup.doors[port * 10 + 4],
    character: setup.doors[port * 10 + 3], rawSlot: setup.doors[port * 10 + 6],
  }));
  assert.deepEqual(initial.map(row => [row.kind, row.slotType]),
    [[0, 0], [0, 0], [3, 3], [3, 3]],
    `Expected source CSS P1/P2 Human/two empty doors: ${JSON.stringify(initial)}`);
  assert.deepEqual(initial.slice(0, 2).map(row => row.character), [8, 8],
    'Original CSS default active doors must both be Mario');
  report.source.initialCss = {doors: initial, cursor: setup.geometry.slice(0, 2),
    sourceObserver: {cursors: setup.cursors, doors: setup.doors, geometry: setup.geometry}};

  assert.deepEqual(initial.slice(0, 2).map(row => row.rawSlot), [0, 0],
    'The source selection must retain authored zero slot fields');
  report.source.twoHumanCss = {doors: initial, cursor: setup.geometry.slice(0, 2),
    sourceObserver: {cursors: setup.cursors, doors: setup.doors, geometry: setup.geometry}};
  report.checks.push('Original CSS already presents two Human Mario doors; subsequent menu input uses P1 only');
}

async function settleOriginalCssStart() {
  const before = {native: await nativeState(), css: await cssSetup()};
  assert(before.css?.doors?.length === 40 && before.css?.cursors?.length === 16,
    'Original CSS readiness snapshot is unavailable before P1 Start');
  // The observer does not expose the original CSS debounce counter. Use a finite
  // UI-only settling window, then retain both source snapshots; no game tick
  // or frame count is inferred from elapsed wall time.
  const settleMs = 5000;
  await page.waitForTimeout(settleMs);
  const after = {native: await nativeState(), css: await cssSetup()};
  assert(after.css?.doors?.length === 40 && after.css?.cursors?.length === 16,
    'Original CSS readiness snapshot was lost during the bounded UI settle interval');
  report.source.startReadiness = {policy: 'finite original-menu UI settle; no game tick is inferred',
    settleMs, before, after};
  await page.screenshot({path: path.join(output, 'original-css-start-ready.png')});
}

async function enterSssAndSelectFd() {
  if (transitionOnly) await settleOriginalCssStart();
  await tap('Enter', 'P1 Start: original CSS to SSS');
  await waitForPhase(3, 'original CSS→SSS', transitionOnly ? 5000 : remaining());
  report.source.cssStartTransition = {
    after: {native: await nativeState(), css: await cssSetup()},
    input: report.input.inputPollReceipts.at(-1) ?? null,
    sourcePad: report.input.sourcePadReceipts.at(-1) ?? null};
  let observation = await stageSetup();
  assert(observation?.ids?.length === 4 && observation?.geometry?.length === 6,
    'Original SSS source stage observer is unavailable');
  report.source.sss = {stage: observation.ids[1], cursor: observation.geometry.slice(0, 2)};
  await page.screenshot({path: path.join(output, 'original-sss-before-selection.png')});
  if (transitionOnly) return;
  for (let step = 0; step < 360; step++) {
    observation = await stageSetup();
    assert(observation?.ids?.length === 4 && observation?.geometry?.length === 6,
      'Original SSS source stage observer was lost');
    const [selected, stageKind] = observation.ids;
    const [x, y, targetX, targetY] = observation.geometry;
    const dx = targetX - x, dy = targetY - y;
    if (stageKind === 32 && Math.abs(dx) <= 1.125 && Math.abs(dy) <= 1.125) break;
    const key = Math.abs(dx) > 1.125 ? (dx > 0 ? 'd' : 'a') :
      Math.abs(dy) > 1.125 ? (dy > 0 ? 'w' : 's') : (dx >= 0 ? 'd' : 'a');
    await nudge(key, observation, stageCursorMoved, `P1 original SSS cursor step ${step + 1}`);
  }
  observation = await stageSetup();
  assert.equal(observation?.ids?.[1], 32,
    `Original P1 keyboard input did not select Final Destination: ${JSON.stringify(observation)}`);
  report.source.sss = {stage: observation.ids[1], cursor: observation.geometry.slice(0, 2), target: observation.geometry.slice(2, 4)};
  await tap('j', 'P1 A: original SSS confirms Final Destination');
  await waitForPhase(7, 'original SSS→match construction');
}

async function waitForReadyMatch() {
  const result = await page.waitForFunction(() => {
    try {
      const pointer = Module?._melee_web_native_menu_match_observe?.();
      if (!pointer) return false;
      const observation = JSON.parse(Module.UTF8ToString(pointer));
      return observation.ready === true ? observation : false;
    } catch { return false; }
  }, null, {timeout: remaining(), polling: 20});
  try { return await result.jsonValue(); }
  finally { await result.dispose(); }
}

async function main() {
  await fs.mkdir(output, {recursive: false});
  const discStat = await fs.stat(disc);
  assert(discStat.isFile(), 'Disc input must be a local regular file');
  const discSha256 = await hashFile(disc);
  const servedManifest = await Promise.all(['gameplay_menu_browser.js', 'gameplay_menu_browser.wasm',
    'runtime-development.mjs'].map(async name => ({name, sha256: await hashFile(path.join(root, name))})));
  report.producer = {kind: 'retained immutable #373 served build', files: servedManifest};
  report.input.disc = {name: path.basename(disc), bytes: discStat.size, sha256: discSha256};
  const started = await makeStaticServer(root);
  server = started.server;
  report.server = {url: started.url, servedRootLabel: path.basename(root)};
  const {chromium, browser: launchConfig, browserPath, playwrightPath} = await loadBrowserTools(values.playwright);
  browser = await chromium.launch(browserLaunchOptions(launchConfig, {headed: false}));
  report.browser = {executable: path.basename(browserPath), version: browser.version(), playwright: path.basename(playwrightPath)};
  page = await browser.newPage({viewport: {width: 1280, height: 960}});
  page.on('pageerror', error => ownedPagesErrors.push({kind: 'pageerror', message: error.message}));
  page.on('console', message => { if (message.type() === 'error') ownedPagesErrors.push({kind: 'console', message: message.text()}); });
  page.on('response', response => { if (response.status() >= 400) ownedPagesErrors.push({kind: 'http', status: response.status(), url: response.url()}); });
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: remaining(), deadline});

  await page.goto(started.url, {waitUntil: 'load', timeout: remaining()});
  await driver.selectDisc(disc);
  await driver.waitForStart();
  await driver.launch(1);
  launched = true;
  await configureSingleLocalInput();

  // Count only direct calls through the diagnostic PAD-writing export. Natural
  // source polling of actual keyboard events remains the existing PAD path.
  await page.evaluate(() => {
    const names = ['_melee_web_native_menu_pad_sample', '_melee_web_native_menu_pad_sample_full'];
    globalThis.__singleInputDirectPadCalls = 0;
    for (const name of names) {
      const original = Module[name];
      if (typeof original !== 'function') throw Error(`Private diagnostic PAD export ${name} is unavailable`);
      Module[name] = (...args) => { globalThis.__singleInputDirectPadCalls++; return original(...args); };
    }
  });

  await verifyInitialTwoHumanCss();
  await page.screenshot({path: path.join(output, 'two-human-css.png')});
  if (transitionOnly) {
    await enterSssAndSelectFd();
    const state = await nativeState();
    assert.equal(state.phase, 3, 'Original CSS Start must reach source SSS');
    report.source.sss = await stageSetup();
    const directPadCalls = await page.evaluate(() => globalThis.__singleInputDirectPadCalls);
    assert.equal(directPadCalls, 0, 'CSS-to-SSS reducer must not call diagnostic PAD-writing exports');
    report.input.directPadApiCalls = directPadCalls;
    report.input.p2KeyEvents = report.input.keys.filter(event => event.port === 1).length;
    assert.equal(report.input.keys.length, 1, 'CSS-to-SSS reducer must send exactly one P1 keyboard event');
    assert.equal(report.input.p2KeyEvents, 0, 'CSS-to-SSS reducer must send no P2 keyboard events');
    assert.equal(report.input.sourcePadReceipts.length, 1,
      'Original CSS Start must have a literal source HSD_PadCopyStatus receipt');
    assert.equal(report.input.inputPollReceipts.length, 1,
      'P1 Start must have a raw keyboard PAD poll receipt');
    assert.deepEqual(ownedPagesErrors, [], `Browser reported errors: ${JSON.stringify(ownedPagesErrors)}`);
    report.checks.push('One actual P1 keyboard Start reached the original SSS scene after bounded CSS readiness settling');
    await page.screenshot({path: path.join(output, 'original-sss-transition-only.png')});
    report.source.offerObservation = 'Not collected by this CSS-to-SSS reducer.';
    report.result = 'PASS';
    return;
  }
  await enterSssAndSelectFd();
  const firstReady = await waitForReadyMatch();
  const rules = firstReady?.rules;
  assert.equal(firstReady?.ready, true, 'Native source match must reach its first ready observation');
  assert(Number.isInteger(firstReady.frame) && firstReady.frame >= 0 && firstReady.frame <= 30,
    `First ready observation must remain within the short prefix: ${JSON.stringify(firstReady)}`);
  assert.equal(rules?.stage, 32, 'The source match must retain the selected Final Destination stage kind');
  assert.deepEqual(rules?.player_source_characters, [8, 8], 'The source match must retain two Mario selections');
  assert.deepEqual(rules?.player_slot_types, [0, 0], 'The source match must retain two Human slots');
  assert.deepEqual(rules?.player_source_stocks, [4, 4], 'The source match must retain four stocks per player');
  assert.deepEqual(rules?.resolved_controller_ports, [0, 1], 'Source player order must resolve to ports zero and one');
  assert.equal(rules?.is_teams, 0, 'The source match must remain non-team VS');
  report.source.firstReadyMatch = firstReady;
  report.checks.push('Native source match observation proves FD, two Human Mario slots, four stocks each, and resolved source ports 0/1');

  const directPadCalls = await page.evaluate(() => globalThis.__singleInputDirectPadCalls);
  assert.equal(directPadCalls, 0, 'The control must not call the diagnostic PAD-writing export');
  report.input.directPadApiCalls = directPadCalls;
  report.input.p2KeyEvents = report.input.keys.filter(event => event.port === 1).length;
  assert.equal(report.input.p2KeyEvents, 0,
    'This menu control must use P1 keyboard events only');
  assert(report.input.sourcePadReceipts.length >= 2,
    'Original menu button input must have literal source HSD_PadCopyStatus receipts');
  assert.equal(report.input.inputPollReceipts.length, report.input.keys.length,
    'Every recorded P1 keyboard event must have a raw PAD poll receipt');
  report.checks.push('Original source HSD_PadCopyStatus and the keyboard poll receipt both expose the pressed P1 A/Start buttons');
  assert.deepEqual(ownedPagesErrors, [], `Browser reported errors: ${JSON.stringify(ownedPagesErrors)}`);
  await page.screenshot({path: path.join(output, 'first-ready-match.png')});
  report.source.offerObservation = 'Not collected: this bounded control records the first ready match, not a native-held selection offer.';
  report.result = 'PASS';
}

try {
  await main();
} catch (error) {
  failure = error;
  report.result = 'FAIL';
  report.failure = {message: error.message || String(error), step: error.step || null,
    diagnostics: error.diagnostics || null, pageErrors: ownedPagesErrors};
} finally {
  if (launched && page && browser) {
    try { await driver?.unload(); report.cleanup.unloaded = true; }
    catch (error) { report.cleanup.unloadError = error.message || String(error); }
  }
  if (driver) {
    try { driver.dispose(); report.cleanup.driverDisposed = true; }
    catch (error) { report.cleanup.driverError = error.message || String(error); }
  }
  if (browser) {
    try { await browser.close(); report.cleanup.browserClosed = true; }
    catch (error) { report.cleanup.browserError = error.message || String(error); }
  }
  if (server) {
    try { await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve())); report.cleanup.serverClosed = true; }
    catch (error) { report.cleanup.serverError = error.message || String(error); }
  }
  const cleanupFailures = Object.entries(report.cleanup)
    .filter(([key, value]) => /Error$/.test(key) && value)
    .map(([key, value]) => `${key}: ${value}`);
  if (cleanupFailures.length) {
    report.result = 'FAIL';
    report.failure = report.failure || {message: cleanupFailures.join('; '), step: 'cleanup'};
    report.failure.cleanupErrors = cleanupFailures;
    failure = failure || Error(cleanupFailures.join('; '));
  }
  if (output) {
    try { await fs.writeFile(path.join(output, 'result.json'), `${JSON.stringify(report, null, 2)}\n`, {flag: 'wx'}); }
    catch (error) {
      report.result = 'FAIL';
      report.resultWriteError = error.message || String(error);
      report.failure = report.failure || {message: report.resultWriteError, step: 'result-write'};
      failure = failure || Error(`Could not retain result.json: ${report.resultWriteError}`);
    }
  }
}
if (failure) throw failure;
console.log(JSON.stringify({result: report.result, checks: report.checks, output}));
