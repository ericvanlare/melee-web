import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {parseMeleeGCI} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'gci', 'out', 'playwright'].map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['url', 'disc', 'gci', 'out'])
  if (!values[name]) throw Error(`Use --url ORIGIN --disc OWNED_DISC --gci GAME_WRITTEN_GCI --out NEW_DIRECTORY [--playwright PACKAGE_DIR]`);

const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readHash = async file => hash(await fs.readFile(file));

function fields(profile) {
  const names = [];
  for (let bank = 0; bank < 7; bank++) for (let record = 0; record < 19; record++) {
    const start = 0x1790 + bank * 0x1f2c + record * 0x1a4 + 0x198;
    const bytes = profile.subarray(start, start + 8);
    const end = bytes.indexOf(0) < 0 ? bytes.length : bytes.indexOf(0);
    if (end) {
      const raw = bytes.subarray(0, end);
      const text = new TextDecoder('shift_jis').decode(raw);
      names.push({bank, record, hex: Buffer.from(raw).toString('hex'), text, normalized: text.normalize('NFKC')});
    }
  }
  return {
    unlockedCharacters: Buffer.from(profile.subarray(0, 2)).toString('hex'),
    unlockedStages: Buffer.from(profile.subarray(2, 4)).toString('hex'),
    unlockedFeatures: profile[4],
    powerCount: new DataView(profile.buffer, profile.byteOffset).getInt32(0x1e8, false),
    powerTime: new DataView(profile.buffer, profile.byteOffset).getInt32(0x1ec, false),
    itemFrequency: profile[0x448],
    controllerRumble: [...profile.subarray(0x458, 0x45c)],
    names,
  };
}

const inputBytes = new Uint8Array(await fs.readFile(values.gci));
const inputProfile = parseMeleeGCI(inputBytes);
const inputFields = fields(inputProfile);
assert(inputFields.names.some(item => item.bank === 0 && item.record === 0 && item.normalized === 'AABB'),
  'The game-written GCI must contain the identifiable AABB profile name.');
assert.equal(inputFields.unlockedCharacters, '07ff');
assert.equal(inputFields.unlockedStages, '07ff');
assert.equal(inputFields.unlockedFeatures, 0x0f);
assert.deepEqual(inputFields.controllerRumble, [0, 1, 1, 1], 'The GCI must contain the Dolphin-made rumble change.');

const {chromium, browser: browserConfig} = await loadBrowserTools(values.playwright);
const browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false}));
let context;
try {
  context = await browser.newContext({viewport: {width: 1280, height: 960}, acceptDownloads: true});
  const page = await context.newPage();
  page.on('pageerror', error => console.error(`PAGEERROR ${error.message}`));
  const driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 180000});
  await page.goto(values.url);
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 60000});
  console.log('public-player document loaded');
  await driver.selectDisc(path.resolve(values.disc));
  await driver.waitForPublicCss();
  await page.locator('#settings-open').click();
  const defaultMode = await page.locator('#save-mode').inputValue();
  assert.equal(defaultMode, 'everything');
  await page.locator('#save-file').setInputFiles(path.resolve(values.gci));
  await page.locator('#save-confirm-dialog[open]').waitFor();
  const confirmationText = await page.locator('#save-confirm-dialog').innerText();
  assert.match(confirmationText, /becomes your Personal progress save/);
  assert.match(confirmationText, /game will restart now/);
  await page.locator('#save-confirm-accept').click();
  await page.waitForFunction(() => document.querySelector('#save-mode')?.value === 'personal' &&
    /Save loaded/.test(document.querySelector('#save-status')?.textContent || ''), null, {timeout: 30000});
  await driver.waitForPublicCss();
  const importStatus = await page.locator('#save-status').textContent();
  console.log('confirmed the game-written GCI import and observed the restarted player');
  await page.screenshot({path: path.join(output, 'css-after-game-written-import.png'), fullPage: true});

  const exportFromSettings = async (filename) => {
    if (!await page.locator('#settings-dialog[open]').count()) {
      await page.locator('#settings-open').click();
      await page.locator('#settings-dialog[open]').waitFor();
    }
    const [download] = await Promise.all([
      page.waitForEvent('download'),
      page.locator('#export-save').click(),
    ]);
    const file = path.join(output, filename);
    await download.saveAs(file);
    return {file, profile: parseMeleeGCI(new Uint8Array(await fs.readFile(file))), sha256: await readHash(file)};
  };
  const first = await exportFromSettings('personal-after-import.gci');
  const firstFields = fields(first.profile);
  assert.deepEqual(first.profile, inputProfile, 'The live Personal export must preserve the entire game-written profile.');

  console.log('reloading the browser document to check the committed Personal profile');
  await page.reload({waitUntil: 'domcontentloaded', timeout: 60000});
  await page.locator('#settings-open').waitFor({state: 'visible', timeout: 60000});
  await page.locator('#settings-open').click();
  await page.waitForFunction(() => document.querySelector('#save-mode')?.value === 'personal', null, {timeout: 30000});
  const reloadedMode = await page.locator('#save-mode').inputValue();
  assert.equal(reloadedMode, 'personal');
  const reloaded = await exportFromSettings('personal-after-browser-reload.gci');
  const reloadedFields = fields(reloaded.profile);
  assert.deepEqual(reloaded.profile, inputProfile, 'The reloaded Personal export must preserve the entire imported profile.');

  const report = {
    url: values.url,
    browser: browser.version(),
    disc: path.basename(values.disc),
    discSha256: await readHash(values.disc),
    inputGciSha256: hash(inputBytes),
    inputFields,
    defaultMode,
    confirmationText,
    importStatus,
    firstWebExportSha256: first.sha256,
    firstWebFields: firstFields,
    reloadedMode,
    reloadedExportSha256: reloaded.sha256,
    reloadedFields,
    checks: {
      inputHasCompletedProgressAndAABB: true,
      importWasConfirmedAndRestarted: /game restarted/.test(importStatus),
      firstWebExportMatchesImportedProfile: true,
      personalStateSurvivedBrowserReload: true,
    },
    screenshot: path.join(output, 'css-after-game-written-import.png'),
  };
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({result: 'pass', report}, null, 2));
} finally {
  await context?.close();
  await browser.close();
}
