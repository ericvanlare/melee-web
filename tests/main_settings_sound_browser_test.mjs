#!/usr/bin/env node
/* Exercise the original Main > Settings > Sound route in the rendered player. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {parseMeleeGCI} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: {...Object.fromEntries(
  ['url', 'disc', 'out', 'playwright', 'manifest'].map(name => [name, {type: 'string'}])),
  'probe-export': {type: 'boolean'}}, strict: true});
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));
const report = {schema: 'melee-web-main-settings-sound-browser-v1', url: values.url,
  disc: path.basename(values.disc), discSha256: await readHash(values.disc),
  reference: {routeEvidence: 'docs/evidence/main-settings-sound-route-v1.json',
    sourceCommit: 'b43912cc78606f96c9569f5d6229bc9d7e265ea5',
    mainDolSha256: 'dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646',
    route: 'CSS > parent Main > Title > Main > Settings (MenuKind 4) > Sound (MenuKind 20) > Settings > Main > CSS',
    sourceCallbacks: ['gm_Scene_Title_OnFrame', 'mnMain_Scene_OnEnter',
      'mnMain_Scene_OnFrame', 'mn_8022DB10', 'mn_8022D104',
      'mnSound_8024A09C', 'mnSound_802492CC', 'gmMainLib_8015ED74',
      'gmMainLib_8015ED80'],
    mainSelections: {versus: 1, settings: 3, sound: 1}, settingsBackRestoresSelection: 3,
    assets: ['MnMaAll.usd', 'SdMenu.usd', 'SdToy.dat', 'LbMcGame.usd',
      'NtMemAc.usd', 'LbMcSnap.usd', 'GmEvent.dat', 'LbAd.dat'],
    menuIds: [0, 4, 20, 4, 0], soundBalanceByte: {offset: '0x45C', before: 0, after: 251}},
  input: ['B0XX L+R+Start from CSS', 'B to Title', 'Start to Main',
    'Down x3 to Settings', 'A to Settings', 'Down once to Sound', 'A to Sound',
    'B cancels back to Settings without changing the source balance',
    'A re-enters the retained Sound selection; Down selects the Sound/Music balance row',
    'Left changes source balance from 0 to -5',
    'B returns to Settings, A re-enters Sound, B returns through Settings to Main',
    'Up x2 to Versus, A to Versus, A to Melee/CSS'],
  browserMode: 'headless installed Chrome; isolated Playwright context; public runtime player profile',
  probeExportOnly: !!values['probe-export'],
  saveMode: 'Everything unlocked default; fresh Personal profile created for this context',
  audio: 'Chrome host output muted through browserLaunchOptions; source audio processing remains enabled. No PCM or audio-fidelity claim.',
  checks: [], screenshots: {}, cleanup: null};

let chromium, browserConfig, browser, context, page, driver;
let nativeUnloadCaptured = false;
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
const nativeSoundSnapshot = () => page.evaluate(() => {
  const length = 0x1790 + 7 * 0x1f2c;
  const pointer = Module._malloc(length);
  if (!pointer) throw Error('Unable to allocate native save snapshot memory.');
  try {
    if (!Module._melee_web_native_menu_snapshot_save_profile(pointer, length, 0))
      throw Error(Module.UTF8ToString(Module._melee_web_native_menu_message()));
    return {length, soundBalance: Module.HEAPU8[pointer + 0x45c]};
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
      soundBalance: profile ? profile.data[0x45c] : null};
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
    assert.deepEqual(before, {mode: 'everything', revision: null, generation: null, soundBalance: null},
      'The isolated browser context begins with Everything unlocked and no Personal save.');
    await page.locator('#save-mode').selectOption('personal');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').innerText(), /next launch|restart/i);
    await page.locator('#save-confirm-accept').click();
    await page.waitForFunction(() => /source session restarted/.test(
      document.querySelector('#save-status')?.textContent || ''), null, {timeout: 90000});
    await driver.waitForPublicCss();
    const first = await waitForSaved(state => state.mode === 'personal' && state.revision === 1);
    assert.equal(first.soundBalance, 0, 'Fresh source defaults center the saved Sound balance.');
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

  await check('original Main Settings Sound route changes and autosaves its SaveData balance', async () => {
    // Three ordinary Down presses select Settings; Sound is its second row.
    await pressDown(3);
    await press('m');
    await page.waitForTimeout(300);
    await shot('settings');
    await pressDown(1);
    await page.waitForTimeout(300);
    await shot('settings-sound-selected');
    await press('m');
    await page.waitForTimeout(700);
    await shot('sound-balance-center');
    if (!values['probe-export']) {
      await press('o');
      await page.waitForTimeout(500);
      await shot('settings-after-sound-cancel');
      assert.equal((await nativeSoundSnapshot()).soundBalance, 0,
        'Backing out of Sound before editing must leave source SaveData unchanged.');
      await press('m');
      await page.waitForTimeout(700);
      await shot('sound-reentered-center');
      assert.equal((await nativeSoundSnapshot()).soundBalance, 0,
        'Re-entering the retained Sound selection must preserve the centered source balance.');
    }
    await pressDown(1);
    await press('2');
    await page.waitForTimeout(300);
    await shot('sound-balance-minus-five');
    const sourceSnapshot = await nativeSoundSnapshot();
    assert.equal(sourceSnapshot.soundBalance, 251,
      'The source Sound callback must decrement SaveData sound_balance from 0 to -5.');
    const sourceBytesPath = path.join(output, 'native-sound-balance-save-data-byte.bin');
    await fs.writeFile(sourceBytesPath, Buffer.from([sourceSnapshot.soundBalance]));
    report.sourceSnapshot = {sha256: await readHash(sourceBytesPath),
      length: sourceSnapshot.length, soundBalance: sourceSnapshot.soundBalance, offset: '0x45C'};
    await press('o');
    const saved = await waitForSaved(state => state.mode === 'personal' &&
      state.revision >= 2 && state.soundBalance === 251);
    assert.equal(saved.soundBalance, 251);

    // Re-enter through the retained Sound selection, then Back through both
    // original menu kinds. This checks the original callback boundary twice.
    await press('m');
    await page.waitForTimeout(700);
    await shot('sound-reentered-minus-five');
    assert.equal((await nativeSoundSnapshot()).soundBalance, 251);
    assert.equal((await storeSummary()).soundBalance, 251);
    await press('o');
    await press('o');
    const root = await readNative();
    assert.deepEqual(root, {message: 'Original main menu', phase: 11, running: 1});
    report.soundState = {before: 0, after: saved.soundBalance,
      personalRevision: saved.revision, saveDataOffset: '0x45C'};
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
    assert.equal((await storeSummary()).soundBalance, 251);
    await shot('css-returned');
    const nativeAfterCss = await nativeSoundSnapshot();
    report.nativeAfterCss = {soundBalance: nativeAfterCss.soundBalance, offset: '0x45C'};

    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    const [download] = await Promise.all([
      page.waitForEvent('download'), page.locator('#export-save').click(),
    ]);
    const exportedPath = path.join(output, 'personal-sound-balance-minus-five.gci');
    await download.saveAs(exportedPath);
    const exported = parseMeleeGCI(new Uint8Array(await fs.readFile(exportedPath)));
    assert.equal(exported[0x45c], nativeAfterCss.soundBalance,
      'GCI export must serialize the live source SaveData value after returning to CSS.');
    assert.equal(exported[0x45c], 251,
      'Dolphin-compatible GCI export must preserve source-written sound_balance at 0x45C.');
    report.exportSha256 = await readHash(exportedPath);
    report.exportedSoundBalance = exported[0x45c];
    report.personalAfterCss = await storeSummary();
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
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
  if (page && driver) {
    try {
      if (await page.locator('#settings-dialog[open]').count())
        await page.locator('#settings-close').click();
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
  }
  driver?.dispose();
  try { await context?.close(); }
  catch (error) { report.contextCleanupError = String(error); }
  try { await browser?.close(); }
  catch (error) { report.browserCleanupError = String(error); }
  if (!report.cleanup) report.cleanup = {state: 'not-started'};
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
}

if (report.result !== 'pass') throw Error(`Main Settings Sound route failed; see ${path.join(output, 'report.json')}`);
console.log(JSON.stringify({result: report.result, checks: report.checks, sound: report.soundState,
  exportSha256: report.exportSha256, cleanup: report.cleanup}, null, 2));
