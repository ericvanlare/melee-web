#!/usr/bin/env node
/** Actual release-graph UI/network smoke. Owned-disc checks are optional and never performance admission. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
const {values} = parseArgs({options: Object.fromEntries(['url', 'playwright', 'disc', 'out'].map(name => [name, {type: 'string'}]))});
if (!values.url || !values.playwright || !values.out) throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out LOCAL_DIR [--disc OWNED_DISC]');
const {chromium} = await import(pathToFileURL(path.join(path.resolve(values.playwright), 'index.mjs')).href);
await fs.mkdir(values.out, {recursive: true});
const browser = await chromium.launch({channel: 'chrome', headless: false, chromiumSandbox: true});
const context = await browser.newContext({viewport: {width: 1280, height: 960}});
const page = await context.newPage(), origin = new URL(values.url).origin;
const requests = [], errors = [], violations = [], sockets = [], audioEvents = [];
const report = {schema: 'webmelee-public-player-browser-v1', browser: browser.version(), checks: [],
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
const ready = () => page.locator('#choose-disc:not([disabled])').waitFor({timeout: 90000});
const shot = name => page.screenshot({path: path.join(values.out, name + '.png'), fullPage: true});
const press = async key => { await page.keyboard.down(key); await page.waitForTimeout(120); await page.keyboard.up(key); await page.waitForTimeout(150); };
const phase = value => page.waitForFunction(value => Module._melee_web_native_menu_phase() === value &&
  Module._melee_web_native_menu_running() && !document.querySelector('#pause-game').disabled, value, {timeout: 90000});
async function collectViolations() { violations.push(...await page.evaluate(() => window.releaseCspViolations)); }
async function selectDisc(file) {
  await page.locator('#choose-disc').click();
  assert(await page.locator('#disc-continue').isDisabled());
  await page.locator('#disc-ack').check();
  const chooser = page.waitForEvent('filechooser');
  await page.locator('#disc-continue').click();
  await (await chooser).setFiles(file);
}
try {
  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  assert.equal(response.headers()['cross-origin-opener-policy'], 'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'], 'require-corp');
  assert.match(response.headers()['content-security-policy'], /'wasm-unsafe-eval'/);
  await ready();
  await check('isolated WebGPU/Wasm startup and direct original-style player', async () => {
    await page.waitForFunction(() => Module._melee_web_native_menu_cache_idle() !== 0, null, {timeout: 30000});
    assert.equal(await page.evaluate(() => Module._melee_web_native_menu_cache_idle()), 1,
      'The public renderer must open its volatile cache before consuming the bundled pipeline seed');
    const selective = await page.evaluate(() => Module.pipelinePreparation || null);
    if (selective) {
      assert.equal(selective.policy, 'catalog');
      assert.equal(selective.selected, 508);
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
    assert(await page.locator('#end-session').isDisabled());
    assert.match(await page.locator('#edition').innerText(), /no audio/);
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_replay_begin), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module._melee_web_native_menu_diagnostics), 'undefined');
    assert.equal(await page.evaluate(() => typeof window.menuObservePlayer), 'undefined');
    assert.equal(await page.evaluate(() => typeof Module.runtimeCacheState), 'undefined');
    await shot('desktop');
  });
  await check('controls, focus and preferences survive a fresh document', async () => {
    await page.locator('#controls-open').click();
    await page.locator('#keyboard-layout').selectOption('boxx');
    assert(await page.locator('#boxx-source-note').isVisible());
    assert(await page.locator('#player-two-source option[value="keyboard"]').isDisabled());
    assert.match(await page.locator('#player-two-source-status').innerText(),/B0XX keyboard is Player 1 only/);
    await page.locator('#controls-close').click();
    await page.waitForFunction(() => document.activeElement.id === 'canvas');
    await collectViolations(); await page.reload(); await ready();
    assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
  });
  await check('acknowledgement, invalid-disc errors and selectable retry', async () => {
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
      await page.waitForFunction(() => !!document.fullscreenElement);
      await page.locator('#fullscreen').click();
      await page.waitForFunction(() => !document.fullscreenElement);
    } else assert.match(await page.locator('#fullscreen').getAttribute('title'), /unavailable/);
  });
  if (values.disc) {
    await check('owned-disc import, native preparation and original CSS', async () => {
      await selectDisc(values.disc);
      await page.locator('#start-game:not([disabled])').waitFor({timeout: 90000});
      assert(await page.locator('#error-dialog').isHidden());
      await page.locator('#start-game').click(); await phase(1);
      await page.waitForFunction(() => document.activeElement.id === 'canvas');
      assert(await page.locator('#pause-game').isEnabled());
    });
    await check('pause/resume acknowledges the shared native owner', async () => {
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => document.querySelector('#pause-game').textContent === 'Resume' && !document.querySelector('#pause-game').disabled);
      assert.equal(await page.evaluate(() => Module._melee_web_native_menu_running()), 0);
      await page.locator('#pause-game').click();
      await page.waitForFunction(() => Module._melee_web_native_menu_running());
      await page.locator('#pause-game:not([disabled])').waitFor();
    });
    await check('ordinary B0XX keyboard enters original SSS and cancels back to CSS', async () => {
      await page.waitForTimeout(1200); await press('7'); await phase(3);
      await page.waitForTimeout(700);
      await press('o'); await phase(1);
    });
    await check('Eject retires the document; a second silent import can launch', async () => {
      await page.evaluate(() => { window.releaseOldDocumentMarker = true; });
      await collectViolations(); await page.locator('#end-session').click(); await page.waitForFunction(() => !window.releaseOldDocumentMarker); await ready();
      assert(await page.locator('#start-game').isDisabled());
      assert.equal(await page.locator('#keyboard-layout').inputValue(), 'boxx');
      await selectDisc(values.disc); await page.locator('#start-game:not([disabled])').waitFor({timeout: 90000});
      await page.locator('#start-game').click(); await phase(1);
      await page.locator('#end-session').click(); await ready();
      assert(await page.locator('#start-game').isDisabled());
    });
    assert.deepEqual(audioEvents, [], 'The audio-disabled public profile must never create a Web Audio context');
    report.audio = 'Audio explicitly disabled. No Web Audio contexts were created during import, menus, pause/resume or second launch. No audio fidelity claim.';
  } else report.disc = 'Not supplied; native import, menus and audio not exercised.';
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
    assert.deepEqual(storage, {local: ['melee-prototype-keyboard-v1'], session: [], indexed: [], caches: [], workers: 0});
    assert.equal((await context.cookies()).length, 0); report.storage = storage;
    await collectViolations();
    assert.deepEqual(violations, []); assert.deepEqual(errors, []); assert.deepEqual(sockets, []);
    for (const request of requests) {
      const url = new URL(request.url);
      assert.equal(url.origin, origin); assert.equal(request.method, 'GET'); assert.equal(request.body, null);
      assert.equal(url.search, '');
      assert.doesNotMatch(url.pathname, /dsp-coefficients|runtime-audio|audio-worklet|audio-ring/);
      assert(['/', '/terms', '/privacy', '/copyright', '/notices'].includes(url.pathname) ||
        /^\/runtime\/[a-f0-9]+\/[a-z0-9/_.-]+$/i.test(url.pathname) || /^\/assets\/site\.[a-f0-9]+\.css$/.test(url.pathname), url.pathname);
    }
    report.requests = requests.map(({url, method}) => ({path: new URL(url).pathname, method}));
  });
  report.result = 'pass';
} catch (error) {
  report.result = 'fail'; report.failure = error.message;
  report.state = await page.evaluate(() => ({status: document.querySelector('#status')?.textContent,
    error: document.querySelector('#error')?.textContent, native: window.Module?._melee_web_native_menu_message ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null})).catch(() => null);
  await shot('failure').catch(() => {}); throw error;
} finally {
  report.errors = errors; report.csp = violations; report.audioEvents = audioEvents;
  await fs.writeFile(path.join(values.out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks.length, browser: report.browser}));
