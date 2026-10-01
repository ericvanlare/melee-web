#!/usr/bin/env node
/* Regression for rejecting wrong-sized GCI files before reading their bytes. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createMeleeGCI, MELEE_GCI_FILE_BYTES, MELEE_GCI_PROFILE_BYTES} from '../web/gamecube-save.mjs';

const {values} = parseArgs({options: {
  out: {type: 'string'}, playwright: {type: 'string'},
}});
if (!values.out) throw Error('Use --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const root = path.resolve(fileURLToPath(new URL('../', import.meta.url)));
const modules = new Map([
  ['/web/gamecube-save.mjs', 'web/gamecube-save.mjs'],
  ['/web/save-profile-store.mjs', 'web/save-profile-store.mjs'],
  ['/web/save-profile-settings.mjs', 'web/save-profile-settings.mjs'],
]);
const server = http.createServer(async (request, response) => {
  response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  response.setHeader('Cache-Control', 'no-store');
  if (request.url === '/') {
    response.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'});
    response.end('<!doctype html><meta charset="utf-8"><title>Save import regression</title>');
    return;
  }
  const relative = modules.get(new URL(request.url, 'http://127.0.0.1').pathname);
  if (!relative) {
    response.writeHead(404); response.end(); return;
  }
  try {
    const bytes = await fs.readFile(path.join(root, relative));
    response.writeHead(200, {'Content-Type': 'text/javascript; charset=utf-8'});
    response.end(bytes);
  } catch (error) {
    response.writeHead(500); response.end(String(error));
  }
});

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const profile = new Uint8Array(MELEE_GCI_PROFILE_BYTES).fill(0x21);
profile[0x448] = 3;
const validGci = createMeleeGCI(profile, new Date('2026-09-27T12:00:00.000Z')).bytes;
assert.equal(validGci.byteLength, MELEE_GCI_FILE_BYTES);
const report = {
  schema: 'webmelee-save-oversized-import-browser-v1',
  browserMode: 'headless',
  exactGciBytes: MELEE_GCI_FILE_BYTES,
  syntheticValidGciSha256: hash(validGci),
  checks: [],
};
let browser, page;
try {
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const origin = `http://127.0.0.1:${server.address().port}`;
  const {chromium, browser: launchConfig} = await loadBrowserTools(values.playwright);
  browser = await chromium.launch(browserLaunchOptions(launchConfig, {headed: false}));
  report.browser = browser.version();
  report.launch = {headless: true, audioMuted: true};
  const context = await browser.newContext({acceptDownloads: true});
  page = await context.newPage();
  const pageErrors = [];
  page.on('pageerror', error => pageErrors.push(error.message));
  await page.goto(origin);
  await page.setContent(`<!doctype html><body>
    <button id="settings-open">Settings</button>
    <dialog id="settings-dialog"><label for="save-mode">Save mode</label>
      <select id="save-mode"><option value="everything">Everything unlocked</option>
        <option value="personal">Personal progress</option></select>
      <p id="save-mode-description"></p><button id="export-save">Export</button>
      <button id="load-save">Load save file</button><input id="save-file" type="file">
      <p id="save-status" role="status"></p><button id="settings-close">Close</button>
    </dialog>
    <dialog id="save-confirm-dialog"><h2 id="save-confirm-title"></h2>
      <p id="save-confirm-body"></p><button id="save-confirm-cancel">Cancel</button>
      <button id="save-confirm-accept">Accept</button></dialog></body>`);
  await page.evaluate(async () => {
    const {mountSaveProfileSettings} = await import('/web/save-profile-settings.mjs');
    const {SaveProfileStore} = await import('/web/save-profile-store.mjs');
    const commitProfile = SaveProfileStore.prototype.commitProfile;
    const setMode = SaveProfileStore.prototype.setMode;
    window.storageCalls = {profileCommits: 0, standaloneModeWrites: 0};
    SaveProfileStore.prototype.commitProfile = function(...args) {
      window.storageCalls.profileCommits++;
      return commitProfile.apply(this, args);
    };
    SaveProfileStore.prototype.setMode = function(...args) {
      window.storageCalls.standaloneModeWrites++;
      return setMode.apply(this, args);
    };
    window.runtimeActions = {unload: 0, configure: 0, start: 0};
    window.saveController = mountSaveProfileSettings();
    window.saveStore = await SaveProfileStore.open();
    await window.saveController.bindPlayer({
      unload: async () => { window.runtimeActions.unload++; },
      configureSaveProfile: async () => { window.runtimeActions.configure++; },
      start: async () => { window.runtimeActions.start++; },
      snapshotSaveProfile: async () => new Uint8Array(0xF1C4),
    });
    window.saveController.setState({scene: 'css'});
    const arrayBuffer = File.prototype.arrayBuffer;
    window.fileArrayBufferCalls = 0;
    File.prototype.arrayBuffer = function(...args) {
      window.fileArrayBufferCalls++;
      return arrayBuffer.apply(this, args);
    };
  });
  await page.locator('#settings-open').click();
  await page.locator('#settings-dialog[open]').waitFor();
  assert.equal(await page.locator('#save-mode').inputValue(), 'everything');

  const before = await page.evaluate(async () => ({
    mode: await window.saveStore.getMode(),
    profile: await window.saveStore.getProfile(),
    writes: {...window.storageCalls},
    runtime: {...window.runtimeActions},
  }));
  const oversizedBytes = MELEE_GCI_FILE_BYTES + 1;
  await page.locator('#load-save').click();
  await page.locator('#save-file').setInputFiles({name: 'oversized.gci',
    mimeType: 'application/octet-stream', buffer: Buffer.alloc(oversizedBytes)});
  await page.waitForFunction(() => /must be exactly 90176 bytes/.test(
    document.querySelector('#save-status')?.textContent || ''));
  assert.equal(await page.locator('#save-confirm-dialog[open]').count(), 0,
    'Oversized input must not present import confirmation');
  const after = await page.evaluate(async () => ({
    mode: await window.saveStore.getMode(),
    profile: await window.saveStore.getProfile(),
    writes: {...window.storageCalls},
    runtime: {...window.runtimeActions},
    arrayBufferCalls: window.fileArrayBufferCalls,
  }));
  assert.equal(after.arrayBufferCalls, 0, 'File.size is checked before reading file bytes');
  assert.deepEqual(after.mode, before.mode, 'Oversized input must not change the stored mode');
  assert.deepEqual(after.profile, before.profile, 'Oversized input must not change the stored Personal profile');
  assert.deepEqual(after.writes, before.writes, 'Oversized input must not write save storage');
  assert.deepEqual(after.runtime, before.runtime,
    'Oversized input must not unload, configure, or restart the current source session');
  report.oversizedInput = {bytes: oversizedBytes, arrayBufferCalls: after.arrayBufferCalls,
    confirmationPresented: false, storageUnchanged: true, runtimeUnchanged: true,
    message: await page.locator('#save-status').textContent()};
  report.checks.push('oversized GCI rejected before read with no confirmation, storage write, mode change, or restart');
  await page.screenshot({path: path.join(output, 'oversized-rejected.png'), fullPage: true});

  await page.locator('#load-save').click();
  await page.locator('#save-file').setInputFiles({name: 'valid.gci',
    mimeType: 'application/octet-stream', buffer: Buffer.from(validGci)});
  await page.locator('#save-confirm-dialog[open]').waitFor();
  assert.equal(await page.evaluate(() => window.fileArrayBufferCalls), 1,
    'An exact-size GCI must be read and parsed normally');
  assert.match(await page.locator('#save-confirm-body').textContent(), /Personal progress/);
  await page.locator('#save-confirm-accept').click();
  await page.waitForFunction(() => document.querySelector('#save-mode')?.value === 'personal' &&
    /Save loaded/.test(document.querySelector('#save-status')?.textContent || ''));
  const valid = await page.evaluate(async () => {
    const saved = await window.saveStore.getProfile();
    return {mode: await window.saveStore.getMode(), revision: saved.revision,
      data: Array.from(saved.data), writes: {...window.storageCalls}, runtime: {...window.runtimeActions},
      arrayBufferCalls: window.fileArrayBufferCalls};
  });
  assert.deepEqual(valid.data, Array.from(profile), 'Valid GCI bytes must import unchanged');
  assert.equal(valid.mode.mode, 'personal');
  assert.equal(valid.revision, 1);
  assert.deepEqual(valid.writes, {profileCommits: 1, standaloneModeWrites: 0},
    'The valid import commits profile and Personal mode together in its existing atomic transaction');
  assert.equal(valid.runtime.configure, before.runtime.configure + 1);
  assert.equal(valid.runtime.start, before.runtime.start + 1,
    'A valid import still follows the existing source restart path');
  assert.equal(valid.arrayBufferCalls, 1);
  report.validInput = {bytes: MELEE_GCI_FILE_BYTES, importConfirmed: true,
    mode: valid.mode.mode, profileRevision: valid.revision, profileSha256: hash(Uint8Array.from(valid.data)),
    storageWrites: valid.writes, runtimeActions: valid.runtime, arrayBufferCalls: valid.arrayBufferCalls};
  report.checks.push('exact-size valid GCI is parsed, confirmed, committed, and restarts the source session');
  await page.screenshot({path: path.join(output, 'valid-import-confirmed.png'), fullPage: true});
  assert.deepEqual(pageErrors, [], 'Browser page must not emit uncaught errors');
  report.result = 'pass';
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  report.result = 'fail';
  report.failure = {message: error?.message || String(error), stack: error?.stack || null};
  if (page) await page.screenshot({path: path.join(output, 'failed-import-size.png'), fullPage: true}).catch(() => {});
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  throw error;
} finally {
  if (page) await page.evaluate(() => { window.saveController?.close?.(); window.saveStore?.close?.(); }).catch(() => {});
  await browser?.close();
  if (server.listening) await new Promise(resolve => server.close(resolve));
}
