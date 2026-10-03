#!/usr/bin/env node
/* Exercise the retail Main > Settings > Display route in the actual public player. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {parseMeleeGCI} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: {
  ...Object.fromEntries(['url', 'disc', 'out', 'playwright', 'manifest']
    .map(name => [name, {type: 'string'}])),
  'stop-after-export': {type: 'boolean', default: false},
  'stop-after-sss': {type: 'boolean', default: false},
  'stop-after-match': {type: 'boolean', default: false},
}, strict: true});
if ([values['stop-after-export'], values['stop-after-sss'], values['stop-after-match']]
  .filter(Boolean).length > 1)
  throw Error('Probe flags are mutually exclusive');
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));
const report = {schema: 'melee-web-main-settings-display-browser-v1',
  scope: values['stop-after-export'] ? 'display-export-and-teardown-probe' :
    values['stop-after-sss'] ? 'display-css-sss-cancel-probe' :
    values['stop-after-match'] ? 'display-css-sss-match-entry-probe' : 'full-display-route',
  url: values.url,
  disc: path.basename(values.disc), discSha256: await readHash(values.disc),
  reference: {sourceCommit: 'b43912cc78606f96c9569f5d6229bc9d7e265ea5',
    mainDolSha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
    route: 'Title > Main > Settings > Display > Settings > Main > VS > CSS > SSS > Mario/Final Destination > No Contest Results > CSS',
    sourceCallbacks: ['mnMain_Scene_OnEnter', 'mnMain_Scene_OnFrame',
      'mn_8022DB10', 'mn_8022D104', 'mnDeflicker_8024A6C4',
      'mnDeflicker_8024A168', 'mnStageSel_Scene_OnFrame',
      'gmMainLib_8015F588', 'gmMainLib_8015F4F4'],
    mainSelections: {versus: 1, settings: 3}, settingsBackRestoresSelection: 3,
    assets: ['MnMaAll.usd', 'SdMenu.usd', 'SdToy.dat', 'LbMcGame.usd',
      'NtMemAc.usd', 'LbMcSnap.usd', 'GmEvent.dat', 'LbAd.dat'],
    menuIds: [0, 4, 21, 4, 21, 4, 0],
    deflickerByte: {offset: '0x45D', before: 1, after: 0}},
  input: ['B0XX L+R+Start from CSS', 'B to Title', 'Start to Main',
    'Down x3 to Settings', 'A to Settings', 'Down x2 to Display', 'A to Display',
    'A toggles deflicker off', 'B to Settings', 'A re-enters Display', 'B to Settings',
    'B to Main', 'Up x2 to Versus', 'A to Versus', 'A to Melee/CSS',
    'Start confirms Ready-to-Fight; repeat Start if CSS remains active; Start to SSS',
    'Right then Up to Final Destination; wait for the source 90-frame cursor transition',
    'Start confirms the stage selection and starts the match',
    'Start then L+R+A+Start enters No Contest Results', 'Start confirmations return to CSS'],
  browserMode: 'headless installed Chrome; isolated Playwright context; public runtime player profile',
  saveMode: 'Everything unlocked default; fresh Personal profile created for this context',
  audio: 'Public runtime build is the silent player profile. Retail capture used No Audio Output. No audio fidelity claim.',
  checks: [], screenshots: {}, cleanup: null};

let chromium, browserConfig, browser, context, page, driver;
let nativeUnloadCaptured = false, nativeUnloaded = false;
const errors = [];
const shot = async name => {
  const file = path.join(output, `${name}.png`);
  await page.screenshot({path: file, fullPage: true});
  report.screenshots[name] = {file: path.basename(file), sha256: await readHash(file)};
};
const readNative = () => page.evaluate(() => ({
  message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
  phase: Module._melee_web_native_menu_phase(),
  running: Module._melee_web_native_menu_running(),
}));
const nativeDisplaySnapshot = () => page.evaluate(() => {
  const length = 0x1790 + 7 * 0x1f2c;
  const pointer = Module._malloc(length);
  if (!pointer) throw Error('Unable to allocate native save snapshot memory.');
  try {
    if (!Module._melee_web_native_menu_snapshot_save_profile(pointer, length, 0))
      throw Error(Module.UTF8ToString(Module._melee_web_native_menu_message()));
    return {length, deflicker: Module.HEAPU8[pointer + 0x45D]};
  } finally { Module._free(pointer); }
});
const waitForScene = async message => {
  await page.waitForFunction(expected => {
    const module = globalThis.Module;
    return module?._melee_web_native_menu_running?.() === 1 &&
      module.UTF8ToString(module._melee_web_native_menu_message()) === expected;
  }, message, {timeout: 90000});
  return readNative();
};
const gpuDiagnostics = () => page.evaluate(async () => {
  if (!navigator.gpu) return {available: false, reason: 'navigator.gpu-unavailable'};
  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) return {available: false, reason: 'no-adapter'};
    let info = {};
    try { info = await adapter.requestAdapterInfo(); }
    catch (error) { info = {error: String(error)}; }
    return {available: true, info, features: [...adapter.features].sort(),
      limits: {maxTextureDimension2D: adapter.limits.maxTextureDimension2D,
        maxBindGroups: adapter.limits.maxBindGroups,
        maxBufferSize: adapter.limits.maxBufferSize}};
  } catch (error) {
    return {available: false, reason: String(error)};
  }
});
const waitForPhase = async expected => {
  const result = await page.waitForFunction(value => {
    const runtimeError = document.querySelector('#status')?.dataset.runtimeError;
    if (runtimeError) return {error: runtimeError};
    if (document.querySelector('#error-dialog[open]'))
      return {error: document.querySelector('#error')?.textContent || 'Application error'};
    return globalThis.Module?._melee_web_native_menu_running?.() === 1 &&
      globalThis.Module?._melee_web_native_menu_phase?.() === value ? {phase: value} : false;
  }, expected, {timeout: 90000});
  const value = await result.jsonValue();
  await result.dispose();
  if (value.error) throw Error(value.error);
};
const storeSummary = () => page.evaluate(async () => {
  const url = performance.getEntriesByType('resource')
    .find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
  if (!url) throw Error('Save-profile store module was not loaded.');
  const {SaveProfileStore} = await import(url);
  const store = await SaveProfileStore.open();
  try {
    const mode = await store.getMode();
    const profile = await store.getProfile();
    return {mode: mode.mode, revision: profile?.revision ?? null,
      generation: profile?.generation ?? null,
      deflicker: profile ? profile.data[0x45D] : null};
  } finally { store.close(); }
});
const waitForSaved = async predicate => {
  const deadline = Date.now() + 15000;
  let state;
  while (Date.now() < deadline) {
    state = await storeSummary();
    if (predicate(state)) return state;
    await page.waitForTimeout(100);
  }
  throw Error(`Timed out waiting for Personal save state: ${JSON.stringify(state)}`);
};
const enterSssFromCss = async () => {
  await page.waitForTimeout(1200);
  await press('7');
  await page.waitForTimeout(300);
  const firstStartPhase = await page.evaluate(() => Module._melee_web_native_menu_phase());
  report.cssSssStart = {phaseAfterFirstStart: firstStartPhase,
    repeatedStart: firstStartPhase === 1};
  // With the default P1 and CPU selections, the first Start confirms Ready to
  // Fight while retail CSS remains live. The established ordinary-input route
  // sends Start again only when CSS is still in phase 1.
  if (firstStartPhase === 1) await press('7');
  await waitForPhase(3);
};
const selectFinalDestination = async () => {
  await page.waitForTimeout(1000);
  await driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
  await driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
  // The retail cursor transition advances for 0x5A frames before Start can
  // commit a newly selected stage. SSS commits on Start or A+Start; A alone
  // is ignored by mnStageSel_80259C28.
  await page.waitForTimeout(1600);
  await shot('stage-final-destination');
  await press('7');
};
const press = key => driver.pressChord([key]);
const pressDown = async count => { for (let i = 0; i < count; i++) await press('3'); };
const pressUp = async count => { for (let i = 0; i < count; i++) await press(']'); };
const check = async (name, run) => { await run(); report.checks.push(name); console.log(name); };

try {
  ({chromium, browser: browserConfig} = await loadBrowserTools(values.playwright));
  browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false}));
  report.browser = browser.version();
  if (values.manifest) {
    const manifest = JSON.parse(await fs.readFile(values.manifest, 'utf8'));
    report.build = {profile: manifest.profile, runtimeHash: manifest.runtime?.hash,
      identitySha256: manifest.runtime?.identity_sha256};
  }
  context = await browser.newContext({viewport: {width: 1280, height: 960}, acceptDownloads: true});
  page = await context.newPage();
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 180000});

  await check('fresh Personal profile and CSS entry', async () => {
    const response = await page.goto(values.url);
    assert.equal(response.status(), 200);
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
    report.gpu = await gpuDiagnostics();
    assert.equal(await page.locator('#settings-dialog[open]').count(), 0,
      'Settings is closed until its original toolbar control opens it');
    await driver.selectDisc(path.resolve(values.disc));
    await driver.waitForPublicCss();
    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
    const before = await storeSummary();
    assert.deepEqual(before, {mode: 'everything', revision: null, generation: null, deflicker: null},
      'The isolated browser context begins with Everything unlocked and no Personal save.');
    await page.locator('#save-mode').selectOption('personal');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').innerText(), /next launch|restart/i);
    await page.locator('#save-confirm-accept').click();
    await page.waitForFunction(() => /source session restarted/.test(
      document.querySelector('#save-status')?.textContent || ''), null, {timeout: 90000});
    await driver.waitForPublicCss();
    const first = await waitForSaved(state => state.mode === 'personal' && state.revision === 1);
    assert.equal(first.deflicker, 1, 'Fresh source defaults enable Display deflicker.');
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
    await shot('css-fresh-personal');
  });

  await check('ordinary B0XX input on original title and Main scenes', async () => {
    await page.locator('#controls-open').click();
    await page.locator('#controls-dialog[open]').waitFor();
    await page.locator('#player-one-source').selectOption('keyboard');
    await page.locator('#player-two-source').selectOption('off');
    await page.locator('#keyboard-layout').selectOption('boxx');
    await page.locator('#controls-close').click();
    await page.locator('#controls-dialog').waitFor({state: 'hidden'});
    await page.waitForFunction(() => document.activeElement?.id === 'canvas', null,
      {timeout: 10000});
    await page.waitForTimeout(450);

    await driver.pressChord(['q', '9', '7']);
    const fromCss = await waitForScene('Original main menu');
    assert.deepEqual(fromCss, {message: 'Original main menu', phase: 11, running: 1});
    await page.waitForTimeout(700);
    await shot('main-from-css');
    await press('o');
    const title = await waitForScene('Original title');
    assert.deepEqual(title, {message: 'Original title', phase: 10, running: 1});
    await page.waitForTimeout(500);
    await shot('title');
    await press('7');
    const main = await waitForScene('Original main menu');
    assert.deepEqual(main, {message: 'Original main menu', phase: 11, running: 1});
    await page.waitForTimeout(700);
    await shot('main-from-title');
  });

  await check('retail Main Settings Display route toggles and autosaves source SaveData', async () => {
    // Retail starts from the Main root's first choice; three ordinary Down
    // presses reach Settings. Display is authored at Settings selection 2.
    await pressDown(3);
    await press('m');
    await page.waitForTimeout(300);
    await shot('settings');
    await pressDown(2);
    await page.waitForTimeout(300);
    await shot('settings-display-selected');
    await press('m');
    // The retail Display root gates input until its initial JObj animation
    // reaches frame 19. The first capture showed the scene still animating at
    // 700 ms, so let that source-owned gate settle before sending A.
    await page.waitForTimeout(1500);
    await shot('display-deflicker-on-settled');
    await press('m');
    await page.waitForTimeout(300);
    await page.waitForTimeout(400);
    await shot('display-deflicker-off');
    const sourceSnapshot = await nativeDisplaySnapshot();
    assert.equal(sourceSnapshot.deflicker, 0,
      'A native source snapshot during Display must contain the changed SaveData byte.');
    const sourceBytesPath = path.join(output, 'native-display-save-data-byte.bin');
    await fs.writeFile(sourceBytesPath, Buffer.from([sourceSnapshot.deflicker]));
    report.sourceSnapshot = {sha256: await readHash(sourceBytesPath),
      length: sourceSnapshot.length, deflicker: sourceSnapshot.deflicker, offset: '0x45D'};
    await press('o');
    const saved = await waitForSaved(state => state.mode === 'personal' &&
      state.revision >= 2 && state.deflicker === 0);
    assert.equal(saved.deflicker, 0);

    // The Settings cursor remains on Display when the original callback
    // returns. Re-enter without changing it to cover repeated scene ownership.
    await page.waitForTimeout(250);
    await press('m');
    await page.waitForTimeout(700);
    await press('o');
    await page.waitForTimeout(250);
    await shot('display-reentered-off');
    assert.equal((await nativeDisplaySnapshot()).deflicker, 0);
    await press('o');
    const root = await readNative();
    assert.deepEqual(root, {message: 'Original main menu', phase: 11, running: 1});
    report.displayState = {before: 1, after: saved.deflicker,
      personalRevision: saved.revision, saveDataOffset: '0x45D'};
  });

  await check('Settings exit reaches original CSS and exports the source-written setting', async () => {
    await pressUp(2);
    await press('m');
    await page.waitForTimeout(300);
    await shot('versus-mode-selection');
    await press('m');
    await driver.waitForPublicCss();
    const css = await readNative();
    assert.deepEqual(css, {message: 'Original character select', phase: 1, running: 1});
    report.cssBoundary = {source: await nativeDisplaySnapshot(), personal: await storeSummary()};
    assert.equal(report.cssBoundary.source.deflicker, 0,
      'Source SaveData must retain deflicker off after returning to CSS.');
    assert.equal(report.cssBoundary.personal.deflicker, 0,
      'Personal progress must retain deflicker off after returning to CSS.');
    await shot('css-returned');

    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    report.exportPreflight = {source: await nativeDisplaySnapshot(), personal: await storeSummary()};
    const [download] = await Promise.all([
      page.waitForEvent('download'), page.locator('#export-save').click(),
    ]);
    const exportedPath = path.join(output, 'personal-display-deflicker-off.gci');
    await download.saveAs(exportedPath);
    const exported = parseMeleeGCI(new Uint8Array(await fs.readFile(exportedPath)));
    report.exportSha256 = await readHash(exportedPath);
    report.exportedDeflicker = exported[0x45D];
    report.personalAfterCss = await storeSummary();
    assert.equal(exported[0x45D], 0,
      `The public player exports the source-written Display preference; boundary=${JSON.stringify({
        css: report.cssBoundary, preflight: report.exportPreflight,
        personalAfterCss: report.personalAfterCss, exported: report.exportedDeflicker})}`);
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
  });

  if (values['stop-after-export']) {
    report.result = 'probe-pass';
  } else if (values['stop-after-sss']) {
    await check('retail Start input enters SSS and B cancels back to CSS', async () => {
      await enterSssFromCss();
      // mnStageSel_Scene_OnEnter sets the original 20-frame input gate.
      await page.waitForTimeout(700);
      await shot('sss-entered');
      await press('o');
      await waitForPhase(1);
      await shot('css-after-sss-cancel');
    });
    report.result = 'sss-probe-pass';
  } else if (values['stop-after-match']) {
    await check('retail SSS selection transition commits Final Destination and enters the match', async () => {
      await enterSssFromCss();
      await selectFinalDestination();
      await waitForPhase(7);
      await shot('match');
    });
    report.result = 'match-probe-pass';
  } else await check('the saved Display preference survives a supported match and Results return to CSS', async () => {
    await enterSssFromCss();
    await selectFinalDestination();
    await waitForPhase(7);
    await page.waitForTimeout(5000);
    await shot('match');
    await press('7');
    await page.waitForTimeout(700);
    await driver.pressChord(['q', '9', 'm', '7'], {holdMs: 250, releaseMs: 200});
    await waitForPhase(8);
    await page.waitForTimeout(4500);
    await shot('results');
    for (let confirmation = 0; confirmation < 8; confirmation++) {
      if (await page.evaluate(() => Module._melee_web_native_menu_phase()) !== 8) break;
      await waitForPhase(8);
      await driver.pressChord(['7'], {holdMs: 120, releaseMs: 1380});
    }
    const boundary = await page.waitForFunction(() => {
      const error = document.querySelector('#status')?.dataset.runtimeError;
      if (error) return {error};
      const phase = Module._melee_web_native_menu_phase();
      return (phase === 1 || phase === 9) && Module._melee_web_native_menu_running() ? {phase} : false;
    }, null, {timeout: 60000});
    const boundaryState = await boundary.jsonValue();
    await boundary.dispose();
    if (boundaryState.error) throw Error(boundaryState.error);
    for (let confirmation = 0; confirmation < 120; confirmation++) {
      const current = await page.evaluate(() => Module._melee_web_native_menu_phase());
      if (current !== 9) break;
      await waitForPhase(9);
      await driver.pressChord(['7'], {holdMs: 120, releaseMs: 380});
    }
    await waitForPhase(1);
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    assert.equal((await storeSummary()).deflicker, 0);
    report.displayState.afterResultsCss = (await storeSummary()).deflicker;
    await shot('css-after-results');
  });

  await check('source owner unload leaves the Personal Display setting committed', async () => {
    await page.evaluate(() => {
      const unload = Module._melee_web_native_menu_unload.bind(Module);
      Module._melee_web_native_menu_unload = (...args) => {
        const result = unload(...args);
        window.name = JSON.stringify({result,
          message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
          phase: Module._melee_web_native_menu_phase(),
          running: Module._melee_web_native_menu_running()});
        return result;
      };
    });
    nativeUnloadCaptured = true;
    await driver.unload();
    const teardown = await page.evaluate(() => JSON.parse(window.name || 'null'));
    assert.deepEqual(teardown, {result: 1, message: 'Native menus unloaded.', phase: 0, running: 0});
    nativeUnloaded = true;
    await page.evaluate(() => { window.name = ''; });
    await page.locator('#settings-open').waitFor({state: 'visible', timeout: 90000});
    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    assert.equal(await page.locator('#save-mode').inputValue(), 'personal');
    const retained = await storeSummary();
    assert.equal(retained.deflicker, 0);
    assert(retained.revision >= 2);
    report.retainedAfterNativeUnload = retained;
    await shot('personal-save-after-reload');
  });

  assert.deepEqual(errors, [], 'The rendered route must not emit browser errors.');
  if (!['probe-pass', 'sss-probe-pass', 'match-probe-pass'].includes(report.result)) report.result = 'pass';
} catch (error) {
  report.result = 'fail';
  report.failure = error?.stack || String(error);
  if (page) {
    report.failureState = await page.evaluate(() => ({
      status: document.querySelector('#status')?.textContent || '',
      error: document.querySelector('#error')?.textContent || '',
      native: globalThis.Module?._melee_web_native_menu_message
        ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
      phase: globalThis.Module?._melee_web_native_menu_phase?.() ?? null,
      running: globalThis.Module?._melee_web_native_menu_running?.() ?? null,
    })).catch(() => null);
    await shot('failure').catch(() => {});
  }
} finally {
  report.errors = errors;
  if (page && driver && !nativeUnloaded) {
    try {
      if (await page.locator('#settings-dialog[open]').count()) {
        await page.locator('#settings-close').click({timeout: 5000});
        await page.locator('#settings-dialog').waitFor({state: 'hidden', timeout: 5000});
      }
      if (!nativeUnloadCaptured) {
        await page.evaluate(() => {
          const unload = Module._melee_web_native_menu_unload.bind(Module);
          Module._melee_web_native_menu_unload = (...args) => {
            const result = unload(...args);
            window.name = JSON.stringify({result,
              message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
              phase: Module._melee_web_native_menu_phase(),
              running: Module._melee_web_native_menu_running()});
            return result;
          };
        });
        nativeUnloadCaptured = true;
      }
      await driver.unload();
      report.cleanup = await page.evaluate(() => JSON.parse(window.name || 'null'));
    } catch (error) {
      report.cleanup = {error: String(error)};
    }
  } else if (nativeUnloaded) {
    report.cleanup = {result: 1, message: 'Native menus unloaded.', phase: 0, running: 0};
  }
  driver?.dispose();
  try { await context?.close(); }
  catch (error) { report.contextCleanupError = String(error); }
  try { await browser?.close(); }
  catch (error) { report.browserCleanupError = String(error); }
  if (!report.cleanup) report.cleanup = {state: 'not-started'};
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}

if (!['pass', 'probe-pass', 'sss-probe-pass', 'match-probe-pass'].includes(report.result))
  throw Error(`Main Settings Display route failed; see ${path.join(output, 'report.json')}`);
console.log(JSON.stringify({result: report.result, scope: report.scope, checks: report.checks,
  display: report.displayState, exportSha256: report.exportSha256, cleanup: report.cleanup}, null, 2));
