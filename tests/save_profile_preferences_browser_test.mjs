#!/usr/bin/env node
/* Production-player regression for source preferences replaced at CSS startup.
 * The custom GCI is explicitly synthetic: it starts from this production
 * player's Everything export, adds a recognizable name and preference values,
 * and is re-encoded with the project codec. It is not Dolphin-written proof. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createMeleeGCI, parseMeleeGCI} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'out', 'playwright', 'seed-gci'].map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['url', 'disc', 'out', 'seed-gci'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --seed-gci GAME-WRITTEN-WEB-EXPORT --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));
const readU16 = (profile, offset) => new DataView(profile.buffer, profile.byteOffset)
  .getUint16(offset, false);
const fields = profile => ({
  characters: Buffer.from(profile.subarray(0, 2)).toString('hex'),
  stages: Buffer.from(profile.subarray(2, 4)).toString('hex'),
  features: profile[4],
  powerCount: new DataView(profile.buffer, profile.byteOffset).getInt32(0x1e8, false),
  powerTime: new DataView(profile.buffer, profile.byteOffset).getInt32(0x1ec, false),
  itemFrequency: profile[0x448],
  itemMask: Buffer.from(profile.subarray(0x450, 0x458)).toString('hex'),
  rumble: [...profile.subarray(0x458, 0x45c)],
  savedLanguage: profile[0x45e],
  name: Buffer.from(profile.subarray(0x1790 + 0x198, 0x1790 + 0x1a0)).toString('hex'),
});
const expectedCustom = {
  itemFrequency: 1,
  itemMask: '0000000000000001',
  rumble: [0, 1, 0, 1],
  savedLanguage: 1,
  name: '8260826082618261',
};
const trophyLedgerDelta = (before, after) => {
  const changedFlags = [];
  // Source gmm_x1868 declares trophy_flags[TY_TROPHY_COUNT] at 0x46C and
  // padding at 0x6B6, so the authored table extent is 0x24A bytes.
  for (let offset = 0x46c; offset < 0x6b6; offset += 2) {
    const oldValue = readU16(before, offset);
    const newValue = readU16(after, offset);
    if (oldValue !== newValue)
      changedFlags.push({index: (offset - 0x46c) / 2, before: oldValue, after: newValue});
  }
  return {categoryFlags: {before: readU16(before, 0x46a), after: readU16(after, 0x46a)},
    changedFlags};
};
const seedBytes = new Uint8Array(await fs.readFile(values['seed-gci']));
const seedProfile = parseMeleeGCI(seedBytes);
const seedFields = fields(seedProfile);
assert.equal(seedFields.name, expectedCustom.name,
  'The retained WebMelee export from the accepted Dolphin round trip must contain name AABB.');
assert.equal(seedFields.powerCount, 3);
assert.equal(seedFields.powerTime, 28);
const {chromium, browser: browserConfig} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false}));
const report = {schema: 'webmelee-save-startup-preferences-v1', browser: browser.version(),
  browserMode: 'headless', discSha256: await readHash(values.disc), profile: 'production audio-player',
  syntheticFixture: true, sourceProgress: {seedGciSha256: hash(seedBytes),
    powerCount: seedFields.powerCount, powerTime: seedFields.powerTime,
    provenance: 'Existing WebMelee export from the accepted Dolphin round-trip receipt; the custom-preference regression GCI is synthetic and re-encoded.'},
  checks: []};
const check = async (name, run) => { await run(); report.checks.push(name); console.log(name); };
const storeProfile = page => page.evaluate(async () => {
  const url = performance.getEntriesByType('resource')
    .find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
  if (!url) throw Error('Save-profile store module was not loaded.');
  const {SaveProfileStore} = await import(url);
  const store = await SaveProfileStore.open();
  try {
    const profile = await store.getProfile();
    return {mode: await store.getMode(), profile: profile && {...profile, data: Array.from(profile.data)}};
  }
  finally { store.close(); }
});
const exportFromSettings = async (page, folder, filename) => {
  if (!await page.locator('#settings-dialog[open]').count()) {
    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
  }
  const [download] = await Promise.all([
    page.waitForEvent('download'), page.locator('#export-save').click(),
  ]);
  const file = path.join(folder, filename);
  await download.saveAs(file);
  const profile = parseMeleeGCI(new Uint8Array(await fs.readFile(file)));
  return {file, profile, fields: fields(profile), sha256: await readHash(file)};
};
const openPlayer = async (context, scenario) => {
  const page = await context.newPage();
  diagnosticPage = page;
  page.on('pageerror', error => console.error(`PAGEERROR ${error.message}`));
  const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 180000});
  await page.goto(values.url);
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
  await driver.selectDisc(path.resolve(values.disc));
  await driver.waitForPublicCss();
  await page.screenshot({path: path.join(output, `${scenario}-css.png`), fullPage: true});
  return {page, driver};
};

let freshContext, customContext, diagnosticPage;
try {
  freshContext = await browser.newContext({viewport: {width: 1280, height: 960}, acceptDownloads: true});
  const fresh = await openPlayer(freshContext, 'fresh-personal');
  await check('fresh Personal first commit uses source defaults', async () => {
    await fresh.page.locator('#settings-open').click();
    assert.equal(await fresh.page.locator('#save-mode').inputValue(), 'everything');
    const before = await storeProfile(fresh.page);
    assert.equal(before.profile, null, 'This browser context has no previously committed Personal profile.');
    await fresh.page.locator('#save-mode').selectOption('personal');
    await fresh.page.locator('#save-confirm-dialog[open]').waitFor();
    await fresh.page.locator('#save-confirm-accept').click();
    await fresh.page.waitForFunction(() => /source session restarted/.test(
      document.querySelector('#save-status')?.textContent || ''), null, {timeout: 90000});
    await fresh.driver.waitForPublicCss();
    await fresh.page.waitForFunction(async () => {
      const url = performance.getEntriesByType('resource')
        .find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
      const {SaveProfileStore} = await import(url);
      const store = await SaveProfileStore.open();
      try { return (await store.getProfile())?.revision === 1; } finally { store.close(); }
    }, null, {timeout: 15000});
    const saved = await exportFromSettings(fresh.page, output, 'fresh-personal-first-save.gci');
    assert.equal(saved.fields.itemFrequency, 2);
    assert.equal(saved.fields.itemMask, 'ffffffffffffffff');
    assert.deepEqual(saved.fields.rumble, [1, 1, 1, 1]);
    assert.equal(saved.fields.savedLanguage, 1);
    report.freshPersonal = {initialProfileAbsent: true, revision: 1,
      preferences: {itemFrequency: saved.fields.itemFrequency, itemMask: saved.fields.itemMask,
        rumble: saved.fields.rumble, savedLanguage: saved.fields.savedLanguage},
      exportSha256: saved.sha256};
    await fresh.page.screenshot({path: path.join(output, 'fresh-personal-first-save.png'), fullPage: true});
  });
  await freshContext.close(); freshContext = null;

  customContext = await browser.newContext({viewport: {width: 1280, height: 960}, acceptDownloads: true});
  const custom = await openPlayer(customContext, 'custom-preferences');
  await check('synthetic custom preferences survive production import, autosave, export, reload and relaunch', async () => {
    await custom.page.locator('#settings-open').click();
    assert.equal(await custom.page.locator('#save-mode').inputValue(), 'everything');
    const baselineExport = await exportFromSettings(custom.page, output, 'everything-baseline-for-synthetic-fixture.gci');
    assert.equal(baselineExport.fields.characters, '07ff');
    assert.equal(baselineExport.fields.stages, '07ff');
    assert.equal(baselineExport.fields.features, 0x0f);
    const syntheticProfile = new Uint8Array(baselineExport.profile);
    /* Reuse a source-valid saved name row and genuine source progress from the
     * accepted round-trip export, then alter only the preferences under test. */
    syntheticProfile.set(seedProfile.subarray(0x1790, 0x1790 + 0x1A4), 0x1790);
    const progressView = new DataView(syntheticProfile.buffer, syntheticProfile.byteOffset);
    progressView.setInt32(0x1e8, seedFields.powerCount, false);
    progressView.setInt32(0x1ec, seedFields.powerTime, false);
    syntheticProfile[0x448] = expectedCustom.itemFrequency;
    syntheticProfile.set(Buffer.from(expectedCustom.itemMask, 'hex'), 0x450);
    syntheticProfile.set(expectedCustom.rumble, 0x458);
    syntheticProfile[0x45e] = expectedCustom.savedLanguage;
    syntheticProfile.set(Buffer.from(expectedCustom.name, 'hex'), 0x1790 + 0x198);
    const syntheticBytes = createMeleeGCI(syntheticProfile,
      new Date('2026-09-27T12:00:00.000Z')).bytes;
    const syntheticPath = path.join(output, 'synthetic-custom-preferences.gci');
    await fs.writeFile(syntheticPath, syntheticBytes);
    report.syntheticFixtureSha256 = hash(syntheticBytes);
    report.syntheticFixtureFields = fields(syntheticProfile);
    assert.equal(fields(syntheticProfile).itemFrequency, expectedCustom.itemFrequency);
    assert.equal(fields(syntheticProfile).itemMask, expectedCustom.itemMask);
    assert.deepEqual(fields(syntheticProfile).rumble, expectedCustom.rumble);
    assert.equal(fields(syntheticProfile).name, expectedCustom.name);

    await custom.page.locator('#save-file').setInputFiles(syntheticPath);
    await custom.page.locator('#save-confirm-dialog[open]').waitFor();
    const confirmation = await custom.page.locator('#save-confirm-dialog').innerText();
    assert.match(confirmation, /Personal progress/);
    assert.match(confirmation, /restart/i);
    await custom.page.locator('#save-confirm-accept').click();
    await custom.page.waitForFunction(() => document.querySelector('#save-mode')?.value === 'personal' &&
      /Save loaded/.test(document.querySelector('#save-status')?.textContent || ''), null, {timeout: 30000});
    await custom.driver.waitForPublicCss();
    const importStatus = await custom.page.locator('#save-status').textContent();
    assert.match(importStatus || '', /Save loaded/i);
    await custom.page.locator('#settings-close').click();
    await custom.page.locator('#settings-dialog').waitFor({state: 'hidden'});
    await custom.page.locator('#controls-open').click();
    await custom.page.locator('#player-one-source').selectOption('keyboard');
    await custom.page.locator('#player-two-source').selectOption('off');
    await custom.page.locator('#keyboard-layout').selectOption('boxx');
    await custom.page.locator('#controls-close').click();
    await custom.page.locator('#canvas').focus();
    await custom.page.waitForFunction(() => document.activeElement?.id === 'canvas');
    await custom.page.evaluate(() => {
      const snapshot = Module._melee_web_native_menu_snapshot_save_profile.bind(Module);
      window.nativeSaveProfileSnapshotCalls = 0;
      Module._melee_web_native_menu_snapshot_save_profile = (...args) => {
        window.nativeSaveProfileSnapshotCalls++;
        return snapshot(...args);
      };
    });

    /* Use ordinary supported menu/gameplay input while the actual production
     * autosaver samples live source state. */
    await custom.page.waitForTimeout(1200);
    await custom.driver.pressChord(['7']);
    await custom.driver.waitForPhase(3);
    await custom.page.waitForTimeout(700);
    await custom.driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
    await custom.driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
    await custom.driver.pressChord(['m']);
    await custom.driver.waitForPhase(7);
    await custom.page.waitForTimeout(6500);
    const beforeExport = await storeProfile(custom.page);
    assert(beforeExport.profile?.revision >= 2,
      'A changed live source snapshot must commit after the imported revision.');
    const live = await exportFromSettings(custom.page, output, 'custom-preferences-after-gameplay-autosave.gci');
    assert.equal(live.fields.itemFrequency, expectedCustom.itemFrequency);
    assert.equal(live.fields.itemMask, expectedCustom.itemMask);
    assert.deepEqual(live.fields.rumble, expectedCustom.rumble);
    assert.equal(live.fields.savedLanguage, expectedCustom.savedLanguage);
    assert.equal(live.fields.name, expectedCustom.name);
    assert.equal(live.fields.powerCount, seedFields.powerCount,
      'Source PowerCount progress from the prior game-written profile must survive Personal launch.');
    assert.equal(live.fields.powerTime, seedFields.powerTime,
      'Source PowerTime progress from the prior game-written profile must survive Personal launch.');
    const sourceLedger = trophyLedgerDelta(syntheticProfile, live.profile);
    assert(sourceLedger.changedFlags.length > 0,
      'The supported original route must retain its live source trophy-ledger update.');
    const nativeSnapshots = await custom.page.evaluate(() => window.nativeSaveProfileSnapshotCalls);
    assert(nativeSnapshots >= 3,
      `Expected repeated production autosave snapshots while gameplay was active, observed ${nativeSnapshots}.`);
    const storedAfterGameplay = await storeProfile(custom.page);
    assert.equal(storedAfterGameplay.profile?.revision, beforeExport.profile.revision,
      'Forced export flush must not rewrite an identical committed snapshot.');
    assert(Buffer.from(storedAfterGameplay.profile.data).equals(Buffer.from(live.profile)),
      'The completed production snapshot/flush must match the live source export, including non-preference progress.');
    report.customPreferences = {confirmation, keyboard: 'Player 1 keyboard / B0XX; Player 2 off',
      importStatus,
      seedProgress: {powerCount: seedFields.powerCount, powerTime: seedFields.powerTime},
      afterGameplay: live.fields, sourceLedgerDelta: {
        categoryFlags: sourceLedger.categoryFlags,
        changedFlagCount: sourceLedger.changedFlags.length,
        changedFlags: sourceLedger.changedFlags.map(flag => ({index: flag.index,
          before: `0x${flag.before.toString(16).padStart(4, '0')}`,
          after: `0x${flag.after.toString(16).padStart(4, '0')}`})),
      }, nativeAutosaveSnapshots: nativeSnapshots,
      revisionBeforeForcedFlush: beforeExport.profile.revision,
      committedRevision: storedAfterGameplay.profile.revision, exportSha256: live.sha256};
    await custom.page.screenshot({path: path.join(output, 'custom-preferences-after-gameplay.png'), fullPage: true});

    await custom.page.reload({waitUntil: 'domcontentloaded', timeout: 60000});
    await custom.page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
    const beforeRelaunch = await storeProfile(custom.page);
    assert.equal(beforeRelaunch.mode.mode, 'personal');
    assert.equal(beforeRelaunch.profile.revision, storedAfterGameplay.profile.revision);
    await custom.driver.selectDisc(path.resolve(values.disc));
    await custom.driver.waitForPublicCss();
    await custom.page.waitForTimeout(1800);
    const reloaded = await exportFromSettings(custom.page, output, 'custom-preferences-after-browser-reload.gci');
    assert.equal(reloaded.fields.itemFrequency, expectedCustom.itemFrequency);
    assert.equal(reloaded.fields.itemMask, expectedCustom.itemMask);
    assert.deepEqual(reloaded.fields.rumble, expectedCustom.rumble);
    assert.equal(reloaded.fields.savedLanguage, expectedCustom.savedLanguage);
    assert.equal(reloaded.fields.name, expectedCustom.name);
    assert.equal(reloaded.fields.powerCount, live.fields.powerCount);
    assert.equal(reloaded.fields.powerTime, live.fields.powerTime,
      'The existing game-written progress must survive browser reload and source relaunch.');
    assert(Buffer.from(reloaded.profile).equals(Buffer.from(live.profile)),
      'The full committed source profile, including live trophy-ledger changes, must survive reload and relaunch.');
    report.afterReload = {mode: beforeRelaunch.mode.mode, revision: beforeRelaunch.profile.revision,
      fields: reloaded.fields, exportSha256: reloaded.sha256};
    report.checks.push('browser reload restores Personal and relaunch exports existing game-written progress');
    await custom.page.screenshot({path: path.join(output, 'custom-preferences-after-browser-reload.png'), fullPage: true});
  });

  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({result: 'pass', report}, null, 2));
} catch (error) {
  report.failure = {message: error?.message || String(error), step: error?.step || null,
    diagnostics: error?.diagnostics || null, stack: error?.stack || null};
  await diagnosticPage?.screenshot({path: path.join(output, 'failed-browser-boundary.png'), fullPage: true})
    .catch(() => {});
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  throw error;
} finally {
  await freshContext?.close(); await customContext?.close(); await browser.close();
}
