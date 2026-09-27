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
  ['url', 'disc', 'out', 'playwright'].map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));
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
  savedLanguage: 0,
  name: '8260826082618261',
};
const {chromium, browser: browserConfig} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false}));
const report = {schema: 'webmelee-save-startup-preferences-v1', browser: browser.version(),
  browserMode: 'headless', discSha256: await readHash(values.disc), profile: 'production audio-player',
  syntheticFixture: true, checks: []};
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
  page.on('pageerror', error => console.error(`PAGEERROR ${error.message}`));
  const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 180000});
  await page.goto(values.url);
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
  await driver.selectDisc(path.resolve(values.disc));
  await driver.waitForPublicCss();
  await page.screenshot({path: path.join(output, `${scenario}-css.png`), fullPage: true});
  return {page, driver};
};

let freshContext, customContext;
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
    syntheticProfile[0x448] = expectedCustom.itemFrequency;
    syntheticProfile.set(Buffer.from(expectedCustom.itemMask, 'hex'), 0x450);
    syntheticProfile.set(expectedCustom.rumble, 0x458);
    syntheticProfile[0x45e] = expectedCustom.savedLanguage;
    syntheticProfile.set(Buffer.from(expectedCustom.name, 'hex'), 0x1790 + 0x198);
    const view = new DataView(syntheticProfile.buffer, syntheticProfile.byteOffset);
    view.setInt32(0x1e8, 2, false);
    view.setInt32(0x1ec, 16, false);
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
    await custom.page.locator('#settings-close').click();
    await custom.page.locator('#settings-dialog').waitFor({state: 'hidden'});

    /* Use ordinary supported menu/gameplay input so the source changes
     * PowerTime while the actual production autosaver is sampling. */
    await custom.page.waitForTimeout(1200);
    await custom.driver.pressChord(['7']);
    await custom.driver.waitForPhase(3);
    await custom.page.waitForTimeout(700);
    await custom.driver.pressChord(['4'], {holdMs: 75, releaseMs: 100});
    await custom.driver.pressChord([']'], {holdMs: 45, releaseMs: 100});
    await custom.driver.pressChord(['m']);
    await custom.driver.waitForPhase(7);
    await custom.page.waitForTimeout(6500);
    const live = await exportFromSettings(custom.page, output, 'custom-preferences-after-gameplay-autosave.gci');
    assert.equal(live.fields.itemFrequency, expectedCustom.itemFrequency);
    assert.equal(live.fields.itemMask, expectedCustom.itemMask);
    assert.deepEqual(live.fields.rumble, expectedCustom.rumble);
    assert.equal(live.fields.savedLanguage, expectedCustom.savedLanguage);
    assert.equal(live.fields.name, expectedCustom.name);
    assert(live.fields.powerTime > 16,
      `Ordinary gameplay did not advance source PowerTime: ${live.fields.powerTime}`);
    const storedAfterGameplay = await storeProfile(custom.page);
    assert(storedAfterGameplay.profile?.revision >= 2,
      'A changed live source snapshot must complete an autosave transaction after import.');
    assert.equal(Buffer.from(storedAfterGameplay.profile.data.slice(0x1ec, 0x1f0)).readInt32BE(0), live.fields.powerTime,
    'The completed autosave must contain the changed source PowerTime.');
    report.customPreferences = {confirmation, importStatus: await custom.page.locator('#save-status').textContent(),
      initialProgress: {powerCount: 2, powerTime: 16}, afterGameplay: live.fields,
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
    assert.equal(reloaded.fields.powerTime, live.fields.powerTime,
      'The imported progress change must survive browser reload and source relaunch.');
    report.afterReload = {mode: beforeRelaunch.mode.mode, revision: beforeRelaunch.profile.revision,
      fields: reloaded.fields, exportSha256: reloaded.sha256};
    report.checks.push('browser reload restores Personal and relaunch exports changed progress');
    await custom.page.screenshot({path: path.join(output, 'custom-preferences-after-browser-reload.png'), fullPage: true});
  });

  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({result: 'pass', report}, null, 2));
} catch (error) {
  report.failure = {message: error?.message || String(error), stack: error?.stack || null};
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  throw error;
} finally {
  await freshContext?.close(); await customContext?.close(); await browser.close();
}
