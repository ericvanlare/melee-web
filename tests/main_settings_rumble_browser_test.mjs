#!/usr/bin/env node
/* Exercise the retail Main > Settings > Rumble route in the actual public player. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {parseMeleeGCI} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'out', 'playwright', 'manifest'].map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));
const report = {schema: 'melee-web-main-settings-rumble-browser-v1', url: values.url,
  disc: path.basename(values.disc), discSha256: await readHash(values.disc),
  reference: {receipt: 'docs/evidence/save-profile-dolphin-roundtrip-v1.json',
    sourceCommit: 'b43912cc78606f96c9569f5d6229bc9d7e265ea5',
    mainDolSha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
    dolphinRunSha256: '087e212bc1537f0bb79c9e9375311f75d773aaa5cb36cf4bb32a864da21c185b',
    route: 'Title > Main > Settings > Rumble > Settings > Main',
    sourceCallbacks: ['gm_Scene_Title_OnFrame', 'mnMain_Scene_OnEnter',
      'mnMain_Scene_OnFrame', 'mn_8022DB10', 'mn_8022D104',
      'mnVibration_Init', 'mnVibration_HandleInput', 'gmMainLib_SetRumbleEnabled'],
    mainSelections: {versus: 1, settings: 3}, settingsBackRestoresSelection: 3,
    assets: ['MnMaAll.usd', 'SdMenu.usd', 'SdToy.dat', 'LbMcGame.usd',
      'NtMemAc.usd', 'LbMcSnap.usd', 'GmEvent.dat', 'LbAd.dat'],
    menuIds: [0, 4, 19, 4, 0], rumbleByte: {offset: '0x458', before: 1, after: 0}},
  input: ['B0XX L+R+Start from CSS', 'B to Title', 'Start to Main',
    'Down x3 to Settings', 'A to Settings', 'A to Rumble', 'A toggles Controller 1 off',
    'B to Settings', 'B to Main', 'A to Settings', 'A to Rumble',
    'B to Settings', 'B to Main', 'Up x2 to Versus', 'A to Versus', 'A to Melee/CSS'],
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
const nativeRumbleSnapshot = () => page.evaluate(() => {
  const length = 0x1790 + 7 * 0x1f2c;
  const pointer = Module._malloc(length);
  if (!pointer) throw Error('Unable to allocate native save snapshot memory.');
  try {
    if (!Module._melee_web_native_menu_snapshot_save_profile(pointer, length, 0))
      throw Error(Module.UTF8ToString(Module._melee_web_native_menu_message()));
    return {length, rumble: [...Module.HEAPU8.subarray(pointer + 0x458, pointer + 0x45c)]};
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
      rumble: profile ? [...profile.data.subarray(0x458, 0x45c)] : null};
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
    assert.equal(await page.locator('#settings-dialog[open]').count(), 0,
      'Settings is closed until its original toolbar control opens it');
    await driver.selectDisc(path.resolve(values.disc));
    await driver.waitForPublicCss();
    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
    const before = await storeSummary();
    assert.deepEqual(before, {mode: 'everything', revision: null, generation: null, rumble: null},
      'The isolated browser context begins with Everything unlocked and no Personal save.');
    await page.locator('#save-mode').selectOption('personal');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').innerText(), /next launch|restart/i);
    await page.locator('#save-confirm-accept').click();
    await page.waitForFunction(() => /source session restarted/.test(
      document.querySelector('#save-status')?.textContent || ''), null, {timeout: 90000});
    await driver.waitForPublicCss();
    const first = await waitForSaved(state => state.mode === 'personal' && state.revision === 1);
    assert.deepEqual(first.rumble, [1, 1, 1, 1], 'Fresh source defaults enable all four controller rumble slots.');
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

  await check('retail Main Settings Rumble route toggles and autosaves source SaveData', async () => {
    // Retail starts from the Main root's first choice; three ordinary Down
    // presses reach Settings. Its initial child choice is Rumble.
    await pressDown(3);
    await press('m');
    await page.waitForTimeout(300);
    await shot('settings');
    await press('m');
    // mnVibration's intro animation and input gate run for more than twenty
    // source frames before its first accepted A press.
    await page.waitForTimeout(700);
    await shot('rumble-before');
    await press('m');
    await page.waitForTimeout(300);
    await shot('rumble-controller-1-off');
    // Observe the same native snapshot API used by the production export
    // without opening the browser toolbar over the live original menu.
    const sourceSnapshot = await nativeRumbleSnapshot();
    assert.deepEqual(sourceSnapshot.rumble, [0, 1, 1, 1],
      'A native source snapshot during Rumble must contain the changed SaveData bytes.');
    const sourceBytesPath = path.join(output, 'native-rumble-save-data-bytes.bin');
    await fs.writeFile(sourceBytesPath, Buffer.from(sourceSnapshot.rumble));
    report.sourceSnapshot = {sha256: await readHash(sourceBytesPath),
      length: sourceSnapshot.length, rumble: sourceSnapshot.rumble, offset: '0x458'};
    await press('o');
    await press('o');
    const saved = await waitForSaved(state => state.mode === 'personal' &&
      state.revision >= 2 && state.rumble?.[0] === 0);
    assert.deepEqual(saved.rumble, [0, 1, 1, 1]);

    // Back out through both original child menus and re-enter Rumble without
    // changing it. This covers the repeated source callback path and value.
    await press('m');
    await page.waitForTimeout(250);
    await shot('settings-reentered');
    await press('m');
    await page.waitForTimeout(250);
    await shot('rumble-reentered-off');
    assert.deepEqual((await storeSummary()).rumble, [0, 1, 1, 1]);
    await press('o');
    await press('o');
    const root = await readNative();
    assert.deepEqual(root, {message: 'Original main menu', phase: 11, running: 1});
    report.rumbleState = {before: [1, 1, 1, 1], after: saved.rumble,
      personalRevision: saved.revision, saveDataOffset: '0x458'};
  });

  await check('Settings exit returns through original VS selection to CSS', async () => {
    await pressUp(2);
    await press('m');
    await page.waitForTimeout(300);
    await shot('versus-mode-selection');
    await press('m');
    await driver.waitForPublicCss();
    const css = await readNative();
    assert.deepEqual(css, {message: 'Original character select', phase: 1, running: 1});
    assert.deepEqual((await storeSummary()).rumble, [0, 1, 1, 1]);
    await shot('css-returned');

    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    const [download] = await Promise.all([
      page.waitForEvent('download'), page.locator('#export-save').click(),
    ]);
    const exportedPath = path.join(output, 'personal-rumble-off.gci');
    await download.saveAs(exportedPath);
    const exported = parseMeleeGCI(new Uint8Array(await fs.readFile(exportedPath)));
    assert.deepEqual([...exported.subarray(0x458, 0x45c)], [0, 1, 1, 1],
      'The public player exports the source-written controller preference.');
    report.exportSha256 = await readHash(exportedPath);
    report.exportedRumble = [...exported.subarray(0x458, 0x45c)];
    report.personalAfterCss = await storeSummary();
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
  });

  await check('source owner unload completes after the full menu route', async () => {
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
    assert.deepEqual(retained.rumble, [0, 1, 1, 1]);
    assert(retained.revision >= 2);
    report.retainedAfterDocumentReload = retained;
    await shot('personal-save-after-reload');
  });

  assert.deepEqual(errors, [], 'The rendered route must not emit browser errors.');
  report.result = 'pass';
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

if (report.result !== 'pass') throw Error(`Main Settings Rumble route failed; see ${path.join(output, 'report.json')}`);
console.log(JSON.stringify({result: report.result, checks: report.checks, rumble: report.rumbleState,
  exportSha256: report.exportSha256, cleanup: report.cleanup}, null, 2));
