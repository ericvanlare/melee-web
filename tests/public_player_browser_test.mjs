#!/usr/bin/env node
/** Actual release-graph UI/network smoke. Owned-disc checks are optional and never performance admission. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createMeleeGCI, parseMeleeGCI} from '../web/gamecube-save.mjs';
const {values} = parseArgs({options: {
  ...Object.fromEntries(['url', 'playwright', 'disc', 'out', 'manifest'].map(name => [name, {type: 'string'}])),
  audio: {type: 'boolean', default: false},
  headed: {type: 'boolean', default: false},
}});
if (!values.url || !values.out) throw Error('Use --url ORIGIN --out LOCAL_DIR [--playwright PACKAGE_DIR] [--disc OWNED_DISC] [--audio] [--headed]');
const {chromium,browser:launchOptions} = await loadBrowserTools(values.playwright);
const packageManifest = values.manifest ? JSON.parse(await fs.readFile(values.manifest, 'utf8')) : null;
await fs.mkdir(values.out, {recursive: true});
const browser = await chromium.launch(browserLaunchOptions(launchOptions, {headed: values.headed}));
const context = await browser.newContext({viewport: {width: 1280, height: 960}, acceptDownloads: true});
const page = await context.newPage(), origin = new URL(values.url).origin;
const requests = [], errors = [], violations = [], sockets = [], audioEvents = [];
const report = {schema: 'webmelee-public-player-browser-v1', browser: browser.version(), browser_mode: values.headed ? 'headed' : 'headless', checks: [],
  profile: values.audio ? 'audio-player' : 'player',
  build_identity: packageManifest ? {
    schema: packageManifest.schema, profile: packageManifest.profile,
    source_sha: packageManifest.source_sha, runtime_hash: packageManifest.runtime_hash,
    identity_sha256: packageManifest.identity_sha256,
  } : null,
  scope: 'Production entry, ordinary keyboard UI, lifecycle and application network smoke. No retail comparison, physical-controller, PCM or performance claim.'};
page.on('request', request => requests.push({url: request.url(), method: request.method(), body: request.postData()}));
page.on('pageerror', error => errors.push(error.message));
page.on('websocket', socket => sockets.push(socket.url()));
page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
await page.addInitScript(() => {
  window.releaseCspViolations = [];
  document.addEventListener('securitypolicyviolation', event => window.releaseCspViolations.push({directive: event.violatedDirective, blocked: event.blockedURI}));
});
const cdp = await context.newCDPSession(page);
await cdp.send('WebAudio.enable');
for (const event of ['contextCreated', 'contextChanged', 'contextWillBeDestroyed']) cdp.on('WebAudio.' + event, data => audioEvents.push({event, data}));
const check = async (name, run) => { await run(); report.checks.push(name); console.log(name); };
const driver = createBrowserDriver(page, {surface:'public', timeoutMs:90000});
const ready = driver.waitForImport;
const shot = name => page.screenshot({path: path.join(values.out, name + '.png'), fullPage: true});
const press = key => driver.pressChord([key]);
const phase = driver.waitForPhase;
async function collectViolations() { violations.push(...await page.evaluate(() => window.releaseCspViolations)); }
const selectDisc = driver.selectDisc;
async function armLaunchObserver() {
  await page.evaluate(() => {
    if (window.discFileChanges === undefined) {
      window.discFileChanges = 0;
      document.querySelector('#disc-file').addEventListener('change', () => window.discFileChanges++);
    }
    const nativeLaunch = Module._melee_web_native_menu_launch.bind(Module);
    window.nativeLaunchCalls = 0;
    Module._melee_web_native_menu_launch = (...args) => {
      window.nativeLaunchCalls++;
      return nativeLaunch(...args);
    };
  });
}
async function armAudioActivationObserver() {
  if (!values.audio) return;
  await page.evaluate(() => {
    if (window.webMeleeAudioActivationObserved) return;
    window.webMeleeAudioActivationObserved = true;
    const NativeAudioContext = window.AudioContext;
    window.audioActivation = [];
    window.AudioContext = class extends NativeAudioContext {
      constructor(...args) {
        super(...args);
        const nativeResume = this.resume.bind(this), context = this;
        this.resume = () => {
          const record = {gesture: navigator.userActivation?.isActive === true, before: context.state};
          window.audioActivation.push(record);
          return nativeResume().then(() => { record.after = context.state; });
        };
      }
    };
  });
}
async function waitForCssOrAudioRecovery() {
  await page.waitForFunction(() => {
    const dialog = document.querySelector('#error-dialog');
    return !!dialog?.open || (Module._melee_web_native_menu_phase() === 1 && Module._melee_web_native_menu_running());
  }, null, {timeout: 90000});
  const recovery = await page.locator('#error-dialog').isVisible();
  if (recovery) {
    const failure = await page.locator('#error').innerText();
    assert(values.audio && /browser kept game audio suspended.*choose Play to enable audio/i.test(failure),
      `Unexpected first-start failure: ${failure}`);
    assert(await page.locator('#start-game').isEnabled(), 'A browser that blocks activation exposes Play recovery');
    assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 0,
      'Blocked audio must stop before native launch');
    await page.locator('#error-close').click();
    await page.locator('#start-game').click();
  }
  await phase(1);
  assert(await page.locator('#error-dialog').isHidden());
  return recovery;
}

try {
  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  assert.equal(response.headers()['cross-origin-opener-policy'], 'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'], 'require-corp');
  assert.match(response.headers()['content-security-policy'], /'wasm-unsafe-eval'/);
  await page.locator('#loading-panel').waitFor({state: 'visible', timeout: 30000});
  await check('disc validation is available before graphics readiness', async () => {
    await page.waitForFunction(() => {
      const selection = document.querySelector('#choose-disc');
      const loading = document.querySelector('#loading-panel');
      return selection && !selection.disabled && loading && !loading.hidden;
    }, null, {timeout: 30000});
    assert(await page.locator('#choose-disc').isEnabled(), 'Selection stays available while startup is busy');
    await selectDisc({name: 'early-invalid.rvz', mimeType: 'application/octet-stream', buffer: Buffer.from('invalid')});
    await page.locator('#error-dialog[open]').waitFor();
    assert.match(await page.locator('#error').innerText(), /RVZ is not supported/);
    assert.match(await page.locator('#disc-selection-status').innerText(), /Invalid disc.*early-invalid\.rvz/);
    report.early_disc_validation = 'A file was selected and rejected while the full graphics loading panel was still visible; no import or launch occurred.';
    await page.reload();
  });
  await ready();
  await check('isolated WebGPU/Wasm startup and direct original-style player', async () => {
    await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 30000});
    assert.equal(await page.evaluate(() => Module._melee_web_native_menu_cache_idle()), 1,
      'Full graphics readiness includes the native volatile cache being idle');
    const state = await page.evaluate(() => Module._melee_web_native_menu_cache_idle());
    assert.equal(state, 1,
      'The public renderer must open its volatile cache before consuming the bundled pipeline seed');
    const selective = await page.evaluate(() => Module.pipelinePreparation || null);
    if (selective) {
      assert.equal(selective.policy, 'catalog');
      assert.equal(selective.selected, 626);
      assert.equal(selective.binding_sha256, '122eaece4fce109e9f2c958de8b0bb7315ccb670dab349eb2090da1f2fd589a6');
      assert.equal(selective.unexpected_count, 0);
      assert(await page.evaluate(() => Module.FS.stat('/initial_pipeline_cache.db').size > 0));
      assert.deepEqual(await page.evaluate(() => Module.FS.readdir('/melee-render-cache').filter(name => !['.', '..'].includes(name))), [],
        'Selective preparation retains descriptors in memory without a writable cache or IDBFS');
    } else {
      assert(await page.evaluate(() => Module.FS.stat('/melee-render-cache/pipeline_cache.db').size > 0),
        'The ordinary bundled seed needs a writable document-local SQLite database');
    }
    assert.equal(await page.evaluate(() => crossOriginIsolated && !!navigator.gpu), true);
    assert.equal(await page.locator('canvas').count(), 1);
    assert.equal(await page.locator('iframe,h1,header,footer,article').count(), 0);
    assert(await page.locator('#start-game').isDisabled());
    assert.equal(await page.locator('#disc-ack').count(), 0,
      'Disc selection must not have an acknowledgement checkbox gate');
    assert(await page.locator('#end-session').isDisabled());
    assert.equal(await page.locator('#brand').innerText(), 'WEBMELEE.GG');
    assert.equal(await page.locator('#edition').innerText(), 'alpha');
    assert.equal(await page.locator('#edition em').evaluate(node => getComputedStyle(node).fontStyle), 'italic');
    if (values.audio) {
      assert.equal(await page.locator('#audio-note,#audio-info,#audio-details').count(), 0);
    } else assert.equal(await page.locator('#audio-note').textContent(), 'no audio ⓘlicensing issue, need to remove about 50 lines of Dolphin audio code still');
    assert.deepEqual(await page.locator('#toolbar > *').evaluateAll(nodes => nodes.map(node => node.id)),
      ['toolbar-brand', 'toolbar-actions', 'toolbar-meta']);
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_replay_begin), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_diagnostics), 'undefined');
    assert.equal(await page.evaluate(() => typeof window.menuObservePlayer), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module.runtimeCacheState), 'undefined');
    await shot('desktop');
  });
  await check('save settings export, import, persistence, compare-and-swap and recovery', async () => {
    await page.locator('#settings-open').click();
    await page.locator('#settings-dialog[open]').waitFor();
    assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
    await shot('settings');
    const exported = page.waitForEvent('download');
    await page.locator('#export-save').click();
    const download = await exported;
    const gciPath = path.join(values.out, 'everything-unlocked.gci');
    await download.saveAs(gciPath);
    const baseline = parseMeleeGCI(new Uint8Array(await fs.readFile(gciPath)));
    assert.equal(baseline.byteLength, 0xF1C4);

    await page.locator('#save-mode').selectOption('personal');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').textContent(), /next launch/);
    await shot('mode-confirmation');
    await page.locator('#save-confirm-cancel').click();
    await page.locator('#save-confirm-dialog').waitFor({state: 'hidden'});
    assert.equal(await page.locator('#save-mode').inputValue(), 'everything',
      'Cancel restores the active mode selection');
    const canceledMode = await page.evaluate(async () => {
      const storeUrl = performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
      const {SaveProfileStore} = await import(storeUrl);
      const store = await SaveProfileStore.open();
      try { return await store.getMode(); } finally { store.close(); }
    });
    assert.equal(canceledMode.mode, 'everything', 'Cancel must not persist the proposed mode');

    await page.locator('#save-mode').selectOption('personal');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    await page.locator('#save-confirm-accept').click();
    await page.waitForFunction(() => document.querySelector('#save-mode').value === 'personal');

    await page.locator('#load-save').click();
    await page.locator('#save-file').setInputFiles(gciPath);
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').textContent(), /Personal progress/);
    await shot('load-confirmation');
    await page.locator('#save-confirm-cancel').click();
    await page.locator('#save-confirm-dialog').waitFor({state: 'hidden'});
    const canceledImport = await page.evaluate(async () => {
      const storeUrl = performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
      const {SaveProfileStore} = await import(storeUrl);
      const store = await SaveProfileStore.open();
      try { return {mode: (await store.getMode()).mode, profile: await store.getProfile()}; }
      finally { store.close(); }
    });
    assert.equal(canceledImport.mode, 'personal');
    assert.equal(canceledImport.profile, null, 'Cancel must not store a validated candidate save');
    for (let generation = 0; generation < 2; generation++) {
      await page.locator('#load-save').click();
      await page.locator('#save-file').setInputFiles(gciPath);
      await page.locator('#save-confirm-dialog[open]').waitFor();
      await page.locator('#save-confirm-accept').click();
      await page.waitForFunction(() => document.querySelector('#save-status').textContent.includes('Save loaded'));
    }

    const storageRace = await page.evaluate(async bytes => {
      const storeUrl = performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
      if (!storeUrl) throw Error('Save profile store module was not loaded.');
      const {SaveProfileStore} = await import(storeUrl);
      const [left, right] = await Promise.all([SaveProfileStore.open(), SaveProfileStore.open()]);
      try {
        const revision = await left.getProfileRevision();
        const first = Uint8Array.from(bytes), second = Uint8Array.from(bytes);
        second[32] ^= 1;
        const outcomes = await Promise.allSettled([
          left.commitProfile(first, revision), right.commitProfile(second, revision),
        ]);
        const fulfilled = outcomes.filter(result => result.status === 'fulfilled').length;
        const conflict = outcomes.find(result => result.status === 'rejected')?.reason?.name;
        if (fulfilled !== 1 || conflict !== 'SaveProfileConflictError')
          throw Error(`Expected one stale-writer conflict, got ${fulfilled} commits and ${conflict}.`);
        const tx = left.db.transaction('profiles', 'readwrite');
        const done = new Promise((resolve, reject) => { tx.oncomplete = resolve; tx.onerror = tx.onabort = () => reject(tx.error); });
        const request = tx.objectStore('profiles').get('personal');
        request.onsuccess = () => {
          const record = request.result;
          record.active.data[0] ^= 1;
          tx.objectStore('profiles').put(record, 'personal');
        };
        await done;
        return {fulfilled, conflict};
      } finally { left.close(); right.close(); }
    }, Array.from(baseline));
    assert.deepEqual(storageRace, {fulfilled: 1, conflict: 'SaveProfileConflictError'});

    await page.reload(); await ready();
    await page.locator('#settings-open').click();
    assert.equal(await page.locator('#save-mode').inputValue(), 'personal');
    await page.waitForFunction(() => /previous verified progress copy/.test(document.querySelector('#save-status').textContent));
    const recovered = await page.evaluate(async () => {
      const storeUrl = performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
      const {SaveProfileStore} = await import(storeUrl);
      const store = await SaveProfileStore.open();
      try { const profile = await store.getProfile(); return {recovered: profile.recovered, data: Array.from(profile.data)}; }
      finally { store.close(); }
    });
    assert.equal(recovered.recovered, true);
    assert.deepEqual(Buffer.from(recovered.data), Buffer.from(baseline));
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
  });
  await check('Personal autosave skips identical snapshots and retains verified generations', async () => {
    const waitForSavedState = async (predicate, label) => {
      const deadline = Date.now() + 10000;
      let state;
      while (Date.now() < deadline) {
        state = await savePage.evaluate(async () => ({samples: window.saveSamples, commits: window.saveCommitCalls,
          profile: await window.saveStore.getProfile(), envelope: await window.readSavedEnvelope()}));
        if (predicate(state)) return state;
        await savePage.waitForTimeout(50);
      }
      throw Error(`Timed out waiting for ${label}: ${JSON.stringify(state)}`);
    };
    const [settingsUrl, storeUrl] = await page.evaluate(() => ['save-profile-settings.mjs', 'save-profile-store.mjs']
      .map(name => performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/' + name))?.name));
    assert(settingsUrl && storeUrl);
    const isolated = await browser.newContext({viewport: {width: 800, height: 600}});
    const savePage = await isolated.newPage();
    try {
      await savePage.goto(origin + '/privacy');
      await savePage.setContent(`<!doctype html><body>
        <button id="settings-open">Settings</button>
        <dialog id="settings-dialog"><label for="save-mode">Save mode</label>
          <select id="save-mode"><option value="everything">Everything</option><option value="personal">Personal</option></select>
          <p id="save-mode-description"></p><button id="export-save">Export</button><button id="load-save">Load</button>
          <input id="save-file" type="file"><p id="save-status"></p><button id="settings-close">Close</button>
        </dialog>
        <dialog id="save-confirm-dialog"><h2 id="save-confirm-title"></h2><p id="save-confirm-body"></p>
          <button id="save-confirm-cancel">Cancel</button><button id="save-confirm-accept">Accept</button>
        </dialog></body>`);
      await savePage.evaluate(async ({settingsUrl, storeUrl}) => {
        const {mountSaveProfileSettings} = await import(settingsUrl);
        const {SaveProfileStore, SaveProfileStorageError} = await import(storeUrl);
        const commitProfile = SaveProfileStore.prototype.commitProfile;
        window.failNextSaveCommit = false;
        SaveProfileStore.prototype.commitProfile = function(...args) {
          window.saveCommitCalls = (window.saveCommitCalls || 0) + 1;
          if (window.failNextSaveCommit) {
            window.failNextSaveCommit = false;
            return Promise.reject(new SaveProfileStorageError(
              'Browser save transaction did not commit. Previous committed progress remains available.',
              {cause: new DOMException('Storage quota exceeded.', 'QuotaExceededError')}));
          }
          return commitProfile.apply(this, args);
        };
        window.saveSamples = 0;
        window.saveCommitCalls = 0;
        window.saveByte = 1;
        window.saveController = mountSaveProfileSettings({onError: error => { window.saveFailure = error.message; }});
        window.mountSaveProfileSettings = mountSaveProfileSettings;
        window.saveStore = await SaveProfileStore.open();
        window.readSavedEnvelope = () => new Promise((resolve, reject) => {
          const tx = window.saveStore.db.transaction('profiles', 'readonly');
          const request = tx.objectStore('profiles').get('personal');
          request.onsuccess = () => resolve(request.result || null);
          request.onerror = () => reject(request.error);
        });
        window.corruptCurrentEnvelope = () => new Promise((resolve, reject) => {
          const tx = window.saveStore.db.transaction('profiles', 'readwrite');
          tx.oncomplete = resolve;
          tx.onerror = tx.onabort = () => reject(tx.error);
          const profiles = tx.objectStore('profiles');
          const request = profiles.get('personal');
          request.onsuccess = () => {
            const record = request.result;
            record.active.data[0] ^= 0xFF;
            profiles.put(record, 'personal');
          };
          request.onerror = () => tx.abort();
        });
        await window.saveController.bindPlayer({
          configureSaveProfile: async () => {},
          snapshotSaveProfile: async () => {
            window.saveSamples++;
            return new Uint8Array(0xF1C4).fill(window.saveByte);
          },
        });
      }, {settingsUrl, storeUrl});
      await savePage.locator('#settings-open').click();
      await savePage.locator('#save-mode').selectOption('personal');
      await savePage.locator('#save-confirm-dialog[open]').waitFor();
      await savePage.locator('#save-confirm-accept').click();
      await savePage.waitForFunction(() => document.querySelector('#save-mode').value === 'personal');
      await savePage.locator('#settings-close').click();
      await savePage.evaluate(() => window.saveController.setState({scene: 'css'}));
      const firstState = await waitForSavedState(state => state.samples >= 1 && state.profile?.revision === 1,
        'the initial Personal snapshot');
      const first = firstState.envelope;
      assert(first, 'The initial Personal snapshot must have a committed IndexedDB envelope');
      assert.equal(first.revision, 1, 'The first Personal snapshot must commit');
      assert.equal(first.active.data[0], 1);
      assert.equal(first.previous, null);
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 1);

      const unchangedState = await waitForSavedState(state => state.samples >= 3 && state.profile?.revision === 1,
        'repeated unchanged snapshots');
      const unchanged = unchangedState.envelope;
      assert.equal(unchanged.revision, 1, 'Unchanged samples must not advance the revision');
      assert.equal(unchanged.active.generation, first.active.generation);
      assert.equal(unchanged.previous, null, 'Unchanged samples must not replace recovery history');
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 1);

      await savePage.evaluate(() => { window.saveByte = 2; });
      const secondState = await waitForSavedState(state => state.samples >= 4 && state.profile?.revision === 2 &&
        state.envelope?.active?.data[0] === 2, 'the first changed snapshot');
      const second = secondState.envelope;
      assert.equal(second.revision, 2, 'A changed snapshot commits once');
      assert.equal(second.previous.generation, first.active.generation);
      assert.equal(second.previous.data[0], 1);
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 2);

      const repeatedState = await waitForSavedState(state => state.samples >= 5 && state.profile?.revision === 2,
        'unchanged snapshots after the first change');
      const repeated = repeatedState.envelope;
      assert.equal(repeated.revision, 2, 'Repeated snapshots must preserve the committed revision');
      assert.equal(repeated.active.generation, second.active.generation);
      assert.equal(repeated.previous.generation, first.active.generation,
        'Repeated snapshots must preserve the preceding verified generation');
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 2);

      await savePage.evaluate(() => { window.saveByte = 3; });
      const thirdState = await waitForSavedState(state => state.samples >= 6 && state.profile?.revision === 3 &&
        state.envelope?.active?.data[0] === 3, 'the later distinct snapshot');
      const third = thirdState.envelope;
      assert.equal(third.revision, 3, 'A later distinct snapshot commits once');
      assert.equal(third.previous.generation, second.active.generation);
      assert.equal(third.previous.data[0], 2,
        'The preceding verified generation must remain available after a later change');
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 3);

      await savePage.evaluate(() => { window.failNextSaveCommit = true; window.saveByte = 4; });
      await savePage.waitForFunction(() => /Browser storage is full/.test(document.querySelector('#save-status').textContent),
        null, {timeout: 10000});
      const writeFailure = await savePage.evaluate(async () => ({
        status: document.querySelector('#save-status').textContent,
        record: await window.readSavedEnvelope(),
        commits: window.saveCommitCalls,
      }));
      assert.match(writeFailure.status, /Free space in this browser profile, then reload/,
        'Quota failures must explain how to recover');
      assert.equal(writeFailure.record.revision, 3);
      assert.equal(writeFailure.record.active.generation, third.active.generation,
        'A failed write must preserve the last committed active generation');
      assert.equal(writeFailure.record.active.data[0], 3);
      assert.equal(writeFailure.record.previous.generation, second.active.generation,
        'A failed write must preserve the verified recovery generation');
      assert.equal(writeFailure.commits, 4);

      const beforeFlushSamples = await savePage.evaluate(() => window.saveSamples);
      await savePage.evaluate(async () => {
        await window.saveController.close();
        window.saveController = window.mountSaveProfileSettings({onError: error => { window.saveFailure = error.message; }});
        await window.saveController.bindPlayer({
          configureSaveProfile: async () => {},
          snapshotSaveProfile: async () => {
            window.saveSamples++;
            return new Uint8Array(0xF1C4).fill(window.saveByte = 3);
          },
        });
        window.saveController.setState({scene: 'css'});
      });
      await waitForSavedState(state => state.samples > beforeFlushSamples && state.profile?.revision === 3,
        'an identical snapshot after reopening Personal progress');
      await savePage.evaluate(() => window.saveController.flushBeforeTeardown());
      const flushed = await savePage.evaluate(() => window.readSavedEnvelope());
      assert.equal(flushed.revision, 3, 'Forced flush must not rewrite an identical committed snapshot');
      assert.equal(flushed.active.generation, third.active.generation);
      assert.equal(flushed.previous.generation, second.active.generation);
      assert.equal(await savePage.evaluate(() => window.saveCommitCalls), 4);

      await savePage.evaluate(() => window.corruptCurrentEnvelope());
      const recovered = await savePage.evaluate(async () => {
        const profile = await window.saveStore.getProfile();
        return {revision: profile.revision, recovered: profile.recovered,
          firstByte: profile.data[0], generation: profile.generation};
      });
      assert.deepEqual(recovered, {revision: 3, recovered: true, firstByte: 2,
        generation: second.active.generation});
      assert.match(await savePage.evaluate(() => window.saveFailure), /Browser storage is full/,
        'The storage error callback receives the actionable failure');
    } finally {
      await savePage.evaluate(() => window.saveController?.close?.()).catch(() => {});
      await isolated.close();
    }
  });
  await check('Personal imports retain source settings the browser runtime does not expose', async () => {
    const settingsPage = await browser.newPage({viewport: {width: 800, height: 600}, acceptDownloads: true});
    const profile = new Uint8Array(0x1790 + 7 * 0x1F2C);
    profile.fill(0x21);
    profile[0x448] = 2; // gmm_x1CB0.item_freq
    profile.set([0, 1, 1, 1], 0x458); // gmm_x1CB0.rumble_enabled[4]
    const gci = createMeleeGCI(profile, new Date('2026-09-27T12:00:00Z')).bytes;
    try {
      await settingsPage.goto(origin + '/privacy');
      await settingsPage.setContent(`<!doctype html><body>
        <button id="settings-open">Settings</button>
        <dialog id="settings-dialog"><label for="save-mode">Save mode</label>
          <select id="save-mode"><option value="everything">Everything</option><option value="personal">Personal</option></select>
          <p id="save-mode-description"></p><button id="export-save">Export</button><button id="load-save">Load</button>
          <input id="save-file" type="file"><p id="save-status"></p><button id="settings-close">Close</button>
        </dialog>
        <dialog id="save-confirm-dialog"><h2 id="save-confirm-title"></h2><p id="save-confirm-body"></p>
          <button id="save-confirm-cancel">Cancel</button><button id="save-confirm-accept">Accept</button>
        </dialog></body>`);
      const [settingsUrl, storeUrl] = await page.evaluate(() => ['save-profile-settings.mjs', 'save-profile-store.mjs']
        .map(name => performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/' + name))?.name));
      await settingsPage.evaluate(async ({settingsUrl, storeUrl, profile}) => {
        const {mountSaveProfileSettings} = await import(settingsUrl);
        const {SaveProfileStore} = await import(storeUrl);
        const originalCommit = SaveProfileStore.prototype.commitProfile;
        window.profileCommitCount = 0;
        SaveProfileStore.prototype.commitProfile = function(...args) {
          window.profileCommitCount++;
          return originalCommit.apply(this, args);
        };
        const imported = new Uint8Array(profile);
        window.runtimeSnapshot = new Uint8Array(imported);
        // Model the observed source bootstrap defaults. The browser route does
        // not include Melee's Item Switch or Rumble Settings screens.
        window.runtimeSnapshot[0x448] = 0xff;
        window.runtimeSnapshot.set([1, 1, 1, 1], 0x458);
        window.snapshotCount = 0;
        window.saveController = mountSaveProfileSettings();
        window.saveStore = await SaveProfileStore.open();
        window.readProfileRecord = () => new Promise((resolve, reject) => {
          const tx = window.saveStore.db.transaction('profiles', 'readonly');
          const request = tx.objectStore('profiles').get('personal');
          request.onsuccess = () => resolve(request.result || null);
          request.onerror = () => reject(request.error);
        });
        await window.saveController.bindPlayer({
          unload: async () => {}, configureSaveProfile: async () => {}, start: async () => {},
          snapshotSaveProfile: async () => { window.snapshotCount++; return new Uint8Array(window.runtimeSnapshot); },
        });
        window.saveController.setState({scene: 'css'});
      }, {settingsUrl, storeUrl, profile: Array.from(profile)});
      await settingsPage.locator('#settings-open').click();
      await settingsPage.locator('#settings-dialog[open]').waitFor();
      await settingsPage.locator('#save-file').setInputFiles({name: 'melee-save.gci', mimeType: 'application/octet-stream', buffer: Buffer.from(gci)});
      await settingsPage.locator('#save-confirm-dialog[open]').waitFor();
      await settingsPage.locator('#save-confirm-accept').click();
      await settingsPage.waitForFunction(() => document.querySelector('#save-mode').value === 'personal' &&
        /Save loaded/.test(document.querySelector('#save-status').textContent));
      await settingsPage.waitForFunction(() => window.snapshotCount >= 3, null, {timeout: 10000});
      const stored = await settingsPage.evaluate(async () => ({record: await window.readProfileRecord(), samples: window.snapshotCount,
        commits: window.profileCommitCount}));
      assert.equal(stored.record.revision, 1, 'Identical normalized snapshots do not replace the import generation');
      assert.equal(stored.record.previous, null);
      assert.equal(stored.record.active.data[0x448], 2, 'Autosave retains the imported item-frequency value');
      assert.equal(stored.record.active.data[0x458], 0, 'Autosave retains imported Controller 1 Rumble off');
      assert.deepEqual([...stored.record.active.data.slice(0x458, 0x45c)], [0, 1, 1, 1]);
      assert(stored.samples >= 3);
      assert.equal(stored.commits, 1, 'The import is the only committed generation');

      const [download] = await Promise.all([settingsPage.waitForEvent('download'), settingsPage.locator('#export-save').click()]);
      const exportedPath = path.join(values.out, 'imported-source-settings.gci');
      await download.saveAs(exportedPath);
      const exported = parseMeleeGCI(new Uint8Array(await fs.readFile(exportedPath)));
      assert.equal(exported[0x448], 2);
      assert.deepEqual([...exported.slice(0x458, 0x45c)], [0, 1, 1, 1]);
      await settingsPage.evaluate(() => window.saveController.flushBeforeTeardown());
      const flushed = await settingsPage.evaluate(() => window.readProfileRecord());
      assert.equal(flushed.revision, 1, 'Forced flush does not rewrite a profile equal to its source-preserved snapshot');
      assert.equal(flushed.active.data[0x448], 2);
      assert.equal(flushed.active.data[0x458], 0);
    } finally {
      await settingsPage.evaluate(() => { window.saveController?.close?.(); window.saveStore?.close?.(); }).catch(() => {});
      await settingsPage.close();
    }
  });
  await check('controls, focus and preferences survive a fresh document', async () => {
    await page.locator('#controls-open').click();
    // This smoke drives the original menus with the keyboard, regardless of
    // physical devices attached to the host running the browser.
    await page.locator('#player-one-source').selectOption('keyboard');
    await page.locator('#player-two-source').selectOption('off');
    await page.locator('#keyboard-layout').selectOption('boxx');
    assert(await page.locator('#boxx-source-note').isVisible());
    assert(await page.locator('#player-two-source option[value="keyboard"]').isDisabled());
    await page.waitForFunction(() => document.querySelector('#player-two-source-status').textContent === 'Off');
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.activeElement.id === 'canvas');
    await collectViolations(); await page.reload(); await ready();
    assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
    await page.locator('#settings-open').click();
    assert.equal(await page.locator('#save-mode').inputValue(), 'personal');
    await page.locator('#save-mode').selectOption('everything');
    await page.locator('#save-confirm-dialog[open]').waitFor();
    assert.match(await page.locator('#save-confirm-body').textContent(), /next launch/);
    await page.locator('#save-confirm-accept').click();
    await page.waitForFunction(() => document.querySelector('#save-mode').value === 'everything');
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
    await page.reload(); await ready();
    await page.locator('#settings-open').click();
    assert.equal(await page.locator('#save-mode').inputValue(), 'everything',
      'Everything unlocked remains the default selected mode across a fresh document');
    await page.locator('#settings-close').click();
    await page.locator('#settings-dialog').waitFor({state: 'hidden'});
  });
  await check('disclosure before file selection, invalid-disc errors and selectable retry', async () => {
    await armAudioActivationObserver();
    await page.locator('#choose-disc').click();
    assert(await page.locator('#disc-dialog').isVisible());
    assert.equal(await page.locator('#disc-ack').count(), 0);
    const disclosure = (await page.locator('#disc-dialog').textContent()).replace(/\s+/g, ' ');
    assert.match(disclosure, /choosing a game file, you acknowledge the disclosures in the linked.*and agree to the Terms of Use/i);
    for (const [href, text] of [['/terms', 'Terms of Use'], ['/privacy', 'Privacy Notice'],
      ['/notices', 'About & legal'], ['/copyright', 'Copyright & contact']]) {
      assert.equal(await page.locator(`#disc-dialog a[href="${href}"]`).innerText(), text);
    }
    await page.locator('#disc-cancel').click();
    await selectDisc({name: 'unsupported.rvz', mimeType: 'application/octet-stream', buffer: Buffer.from('invalid')});
    await page.locator('#error-dialog[open]').waitFor();
    assert.match(await page.locator('#error').innerText(), /RVZ is not supported/);
    assert(await page.locator('#choose-disc').isEnabled());
    await page.locator('#error-close').click();
    await selectDisc({name: 'invalid.iso', mimeType: 'application/octet-stream', buffer: Buffer.alloc(2048)});
    await page.waitForFunction(() => document.querySelector('#error-dialog').open && /expected Super Smash Bros/.test(document.querySelector('#error').textContent));
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#choose-disc').isEnabled());
    await page.locator('#error-close').click();
    if (values.audio) {
      const activation = await page.evaluate(() => window.audioActivation);
      assert(activation.some(record => record.gesture), 'AudioContext.resume is initiated from the Choose file gesture');
      report.audio_activation_before_import = activation;
      report.audio_activation_before_import_state = activation.some(record => record.after === 'running') ?
        'running' : 'the browser kept the context suspended; a valid-disc attempt will expose recovery';
    }
  });
  await check('narrow layouts and fullscreen', async () => {
    await collectViolations(); await page.reload(); await ready();
    for (const width of [320, 390, 768]) {
      await page.setViewportSize({width, height: 844});
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `overflow at ${width}`);
      if (width === 390) await shot('mobile');
    }
    await page.setViewportSize({width: 1280, height: 960});
    if (await page.locator('#fullscreen').isEnabled()) {
      await page.locator('#fullscreen').click();
      await page.waitForFunction(() => !!document.fullscreenElement ||
        document.querySelector('#player').classList.contains('player-expanded') ||
        /declined fullscreen/i.test(document.querySelector('#fullscreen-status').textContent));
      if (await page.evaluate(() => !!document.fullscreenElement)) {
        await page.locator('#fullscreen').click();
        await page.waitForFunction(() => !document.fullscreenElement);
      } else {
        if (!(await page.locator('#player').evaluate(element => element.classList.contains('player-expanded'))))
          await page.locator('#fullscreen').click();
        assert.match(await page.locator('#fullscreen-status').innerText(), /browser controls remain visible/i);
        await page.locator('#fullscreen').click();
        assert.equal(await page.locator('#player').evaluate(element => element.classList.contains('player-expanded')), false);
      }
    } else throw Error('Fullscreen fallback control must remain enabled');
  });
  if (values.disc) {
    await check('owned-disc import, native preparation and original CSS', async () => {
      await armAudioActivationObserver();
      await armLaunchObserver();
      const audioActivationStart = values.audio ? await page.evaluate(() => window.audioActivation.length) : 0;
      await selectDisc(values.disc);
      const audioRecovery = await waitForCssOrAudioRecovery();
      if (audioRecovery) report.audio_activation_recovery = 'Browser required a separate Play gesture; first attempt did not launch or retry.';
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'A prepared disc must call native launch exactly once; the normal path does not click Play');
      assert(await page.locator('#loading-panel').isHidden(), 'Loading feedback must retire before interactive CSS');
      await page.waitForFunction(() => document.activeElement.id === 'canvas');
      assert(await page.locator('#pause-game').isEnabled());
      assert(await page.locator('#start-game').isDisabled(), 'Play is not a required first-launch step');
      await shot('css-after-import');
      report.css_after_import = await page.evaluate(() => ({
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        launch_calls: window.nativeLaunchCalls,
        loading_hidden: document.querySelector('#loading-panel').hidden,
        error_open: document.querySelector('#error-dialog').open,
      }));
      if (values.audio) {
        const activation = await page.evaluate(start => window.audioActivation.slice(start), audioActivationStart);
        assert(activation.some(record => record.gesture), 'AudioContext.resume must be initiated by the Choose file user gesture');
        assert(activation.some(record => record.after === 'running'), 'The browser audio context must be running after preparation');
        report.audio_activation = activation;
        if (!audioRecovery) report.audio_activation_recovery = 'Headless Chrome resumed audio from Choose file; no Play gesture was used.';
      }
    });
    await check('pause/resume acknowledges the shared native owner', async () => {
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => document.querySelector('#pause-game').textContent === 'Resume' && !document.querySelector('#pause-game').disabled);
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_running()), 0);
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => Module._melee_web_native_menu_running());
      await page.locator('#pause-game:not([disabled])').waitFor();
    });
    await check('confirmed and canceled save-mode changes restart with the retained disc', async () => {
      assert.equal(await page.evaluate(() => window.discFileChanges), 1);
      await page.locator('#settings-open').click();
      assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
      await page.locator('#save-mode').selectOption('personal');
      await page.locator('#save-confirm-dialog[open]').waitFor();
      assert.match(await page.locator('#save-confirm-body').textContent(), /restart the game/i);
      assert.match(await page.locator('#save-confirm-body').textContent(), /Personal progress is kept/);
      await shot('loaded-mode-confirmation');
      await page.locator('#save-confirm-cancel').click();
      await page.locator('#save-confirm-dialog').waitFor({state: 'hidden'});
      assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'Cancel must leave the running source session untouched');

      await page.locator('#save-mode').selectOption('personal');
      await page.locator('#save-confirm-dialog[open]').waitFor();
      await page.locator('#save-confirm-accept').click();
      await page.waitForFunction(() => /source session restarted/.test(document.querySelector('#save-status').textContent),
        null, {timeout: 90000});
      await phase(1);
      assert.equal(await page.locator('#save-mode').inputValue(), 'personal');
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 2,
        'Confirm must create exactly one fresh native launch');
      assert.equal(await page.evaluate(() => window.discFileChanges), 1,
        'Restart must reuse the selected disc without opening the file picker');
      await shot('css-after-personal-restart');

      await page.locator('#save-mode').selectOption('everything');
      await page.locator('#save-confirm-dialog[open]').waitFor();
      await page.locator('#save-confirm-accept').click();
      await page.waitForFunction(() => /source session restarted/.test(document.querySelector('#save-status').textContent),
        null, {timeout: 90000});
      await phase(1);
      await page.locator('#status').waitFor({state: 'hidden', timeout: 90000});
      assert.equal(await page.locator('#save-mode').inputValue(), 'everything');
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 3);
      assert.equal(await page.evaluate(() => window.discFileChanges), 1,
        'Returning to Personal must also retain the selected disc');
      const retainedPersonal = await page.evaluate(async () => {
        const storeUrl = performance.getEntriesByType('resource').find(entry => entry.name.endsWith('/save-profile-store.mjs'))?.name;
        const {SaveProfileStore} = await import(storeUrl);
        const store = await SaveProfileStore.open();
        try { return await store.getProfile(); } finally { store.close(); }
      });
      assert(retainedPersonal?.data?.byteLength > 0, 'Everything must preserve the separate Personal profile');
      await shot('css-after-everything-restart');
      report.mode_restarts = {native_launches: 3, disc_file_changes: 1, default_mode: 'everything',
        personal_profile_retained: true, cancel_restarted: false};
      await page.locator('#settings-close').click();
      await page.locator('#settings-dialog').waitFor({state: 'hidden'});
    });
    await check('ordinary B0XX keyboard enters original SSS and cancels back to CSS', async () => {
      await page.waitForTimeout(1200); await press('7');
      await page.waitForTimeout(300);
      if (await page.evaluate(() => Module._melee_web_native_menu_phase()) === 1) await press('7');
      await phase(3);
      await page.waitForTimeout(700);
      await press('o'); await phase(1);
    });
    await check('Eject retires the document; a second import can launch', async () => {
      await page.evaluate(() => { window.releaseOldDocumentMarker = true; });
      await collectViolations(); await driver.unload();
      assert.equal(await page.evaluate(() => !!window.releaseOldDocumentMarker), false);
      assert(await page.locator('#start-game').isDisabled());
      assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
      await armAudioActivationObserver();
      await armLaunchObserver();
      await selectDisc(values.disc);
      if (await waitForCssOrAudioRecovery()) {
        report.second_audio_activation_recovery = 'A separate Play gesture was required after reload.';
      }
      assert.equal(await page.evaluate(() => window.nativeLaunchCalls), 1,
        'A fresh document launches its selected disc once');
      await shot('css-after-reselection');
      report.css_after_reselection = await page.evaluate(() => ({
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        launch_calls: window.nativeLaunchCalls,
        loading_hidden: document.querySelector('#loading-panel').hidden,
      }));
      await driver.unload();
      assert(await page.locator('#start-game').isDisabled());
    });
    if (values.audio) {
      const created = audioEvents.filter(row => row.event === 'contextCreated');
      assert.equal(created.length, 4,
        'The early-invalid, invalid-recovery and two playable documents create one audio context each');
      assert(created.every(row => row.data.context.sampleRate === 32000));
      report.audio = 'The early-invalid, invalid-recovery and two playable documents each create one 32 kHz context; actual activation state is recorded above. PCM and match transitions are checked by the separate audio lifecycle test; no fidelity claim.';
    } else {
      assert.deepEqual(audioEvents, [], 'The audio-disabled public profile must never create a Web Audio context');
      report.audio = 'Audio explicitly disabled. No Web Audio contexts were created during import, menus, pause/resume or second launch. No audio fidelity claim.';
    }
  } else {
    report.disc = 'Not supplied; native import, menus and successful auto-launch not exercised.';
    if (values.audio) {
      const created = audioEvents.filter(row => row.event === 'contextCreated');
      assert(created.length >= 1, 'Choosing a file must construct the audio context in the real browser');
      assert.equal(created[0].data.context.sampleRate, 32000);
      report.audio = 'The real browser created one 32 kHz Web Audio context from the Choose file path; activation state is recorded. No owned disc was supplied, so CSS launch, audible output and PCM were not exercised.';
    } else report.audio = 'Audio disabled in this public profile; no owned disc supplied.';
  }
  await check('legal pages use their readable document stylesheet and serve full notices', async () => {
    await collectViolations();
    for (const route of ['/terms', '/privacy', '/copyright', '/notices']) {
      const response = await page.goto(origin + route, {waitUntil: 'networkidle'});
      assert.equal(response.status(), 200);
      assert.match(await page.locator('link[rel="stylesheet"]').getAttribute('href'), /^\/assets\/site\.[a-f0-9]+\.css$/);
      assert.equal(await page.evaluate(() => getComputedStyle(document.body).backgroundColor), 'rgb(255, 255, 255)');
      assert(await page.locator('h1').isVisible()); await collectViolations();
    }
    await shot('legal');
    await page.setViewportSize({width: 390, height: 844});
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await shot('legal-mobile');
    const notices = await page.request.get(origin + '/licenses/runtime-third-party.txt');
    assert.equal(notices.status(), 200); assert.match(await notices.text(), /Permission is hereby granted/);
  });
  await check('keyboard-only session persists its preferences; no application upload or background connections', async () => {
    const storage = await page.evaluate(async () => ({local: Object.keys(localStorage), session: Object.keys(sessionStorage),
      indexed: await indexedDB.databases(), caches: await caches.keys(), workers: (await navigator.serviceWorker.getRegistrations()).length}));
    assert.deepEqual(storage, {local: ['melee-prototype-keyboard-v1'], session: [],
      indexed: [{name: 'webmelee-save-profiles-v1', version: 1}], caches: [], workers: 0});
    assert.equal((await context.cookies()).length, 0); report.storage = storage;
    await collectViolations();
    assert.deepEqual(violations, []); assert.deepEqual(errors, []); assert.deepEqual(sockets, []);
    for (const request of requests) {
      const url = new URL(request.url);
      assert.equal(url.origin, origin); assert.equal(request.method, 'GET'); assert.equal(request.body, null);
      assert.equal(url.search, '');
      if (!values.audio) assert.doesNotMatch(url.pathname, /dsp-coefficients|runtime-audio|audio-worklet|audio-ring/);
      assert(['/', '/terms', '/privacy', '/copyright', '/notices'].includes(url.pathname) ||
        /^\/runtime\/[a-f0-9]+\/[a-z0-9/_.-]+$/i.test(url.pathname) || /^\/assets\/site\.[a-f0-9]+\.css$/.test(url.pathname), url.pathname);
    }
    report.requests = requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  });
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = error.message;
  if (error.diagnostics) report.driverFailure = {step:error.step, ...error.diagnostics};
  report.state = await page.evaluate(() => ({status: document.querySelector('#status')?.textContent,
    error: document.querySelector('#error')?.textContent, native: window.Module?._melee_web_native_menu_message ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null})).catch(() => null);
  await shot('failure').catch(() => {}); throw error;
} finally {
  report.errors = errors; report.csp = violations; report.audioEvents = audioEvents;
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  driver.dispose(); await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
