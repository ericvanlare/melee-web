#!/usr/bin/env node
/* Small rendered reproducer for entering the retail Main > Settings > Rumble scene. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values} = parseArgs({options: Object.fromEntries(
  ['url', 'disc', 'out', 'playwright'].map(name => [name, {type: 'string'}])), strict: true});
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url ORIGIN --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
try { await fs.access(output); throw Error(`Refusing to overwrite existing output directory: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: true});

const report = {schema: 'melee-web-main-rumble-entry-repro-v1', url: values.url,
  browserMode: 'headless installed Chrome; isolated Playwright context',
  disc: path.basename(values.disc), checks: [], errors: [], console: [],
  instrumentation: 'Intercepted packaged melee-runtime.mjs to retain Emscripten printErr and onAbort stack in browser console.',
  cleanup: 'Playwright context and browser closed; native owner unload is unavailable after the observed player stop.'};
let chromium, browserConfig, browser, context, page, driver;
const save = async () => fs.writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
const within = (promise, label, milliseconds=12000) => Promise.race([
  promise, new Promise((_, reject) => setTimeout(() => reject(Error(`${label} exceeded ${milliseconds}ms`)), milliseconds)),
]);
const screenshot = async name => {
  const file = path.join(output, `${name}.png`);
  await page.screenshot({path: file, fullPage: true, timeout: 10000});
  report.screenshots ??= {};
  report.screenshots[name] = path.basename(file);
  return file;
};
const native = () => within(page.evaluate(() => ({
  message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
  phase: Module._melee_web_native_menu_phase(),
  running: Module._melee_web_native_menu_running(),
})), 'Native route observer');
const press = key => driver.pressChord([key]);
const waitScene = async expected => page.waitForFunction(message => {
  const module = globalThis.Module;
  return module?._melee_web_native_menu_running?.() === 1 &&
    module.UTF8ToString(module._melee_web_native_menu_message()) === message;
}, expected, {timeout: 90000});

try {
  ({chromium, browser: browserConfig} = await loadBrowserTools(values.playwright));
  browser = await chromium.launch(browserLaunchOptions(browserConfig, {headed: false}));
  report.browser = browser.version();
  context = await browser.newContext({viewport: {width: 1280, height: 960}});
  page = await context.newPage();
  page.setDefaultTimeout(12000);
  page.setDefaultNavigationTimeout(12000);
  page.on('pageerror', error => report.errors.push({message: error.message, stack: error.stack}));
  page.on('console', message => report.console.push({type: message.type(), text: message.text(), location: message.location()}));
  await page.route('**/melee-runtime.mjs', async route => {
    const response = await route.fetch();
    let source = await response.text();
    const printErr = 'printErr: text => onLog(String(text), true),';
    const onAbort = 'onAbort: error => stop(error),';
    assert(source.includes(printErr), 'Packaged runtime diagnostic hook was not found.');
    assert(source.includes(onAbort), 'Packaged runtime abort hook was not found.');
    source = source.replace(printErr,
      "printErr: text => { console.error('[rumble-repro Emscripten stderr]', String(text)); onLog(String(text), true); },");
    source = source.replace(onAbort,
      "onAbort: error => { console.error('[rumble-repro Emscripten abort]', String(error), new Error('abort stack').stack); stop(error); },");
    await route.fulfill({response, body: source});
  });
  driver = createBrowserDriver(page, {surface: 'public', timeoutMs: 180000});

  const response = await page.goto(values.url);
  assert.equal(response.status(), 200);
  await page.locator('#loading-panel').waitFor({state: 'hidden', timeout: 90000});
  await driver.selectDisc(path.resolve(values.disc));
  await driver.waitForPublicCss();
  report.checks.push('Rendered public player entered original CSS with an isolated Everything unlocked profile.');
  await page.locator('#controls-open').click();
  await page.locator('#controls-dialog[open]').waitFor();
  await page.locator('#player-one-source').selectOption('keyboard');
  await page.locator('#player-two-source').selectOption('off');
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.locator('#controls-close').click();
  await page.locator('#controls-dialog').waitFor({state: 'hidden'});
  await page.waitForFunction(() => document.activeElement?.id === 'canvas', null, {timeout: 10000});
  await page.waitForTimeout(450);
  await driver.pressChord(['q', '9', '7']);
  await waitScene('Original main menu');
  report.checks.push('B0XX input reached original Main from CSS.');
  await page.waitForTimeout(700);
  await press('o');
  await waitScene('Original title');
  await page.waitForTimeout(500);
  await press('7');
  await waitScene('Original main menu');
  await page.waitForTimeout(700);
  await press('3'); await press('3'); await press('3');
  await press('m');
  await page.waitForTimeout(500);
  report.beforeRumbleEntry = await native();
  await screenshot('settings-before-rumble-entry');
  assert.deepEqual(report.beforeRumbleEntry, {message: 'Original main menu', phase: 11, running: 1});
  await press('m');
  await page.waitForTimeout(1200);
  report.afterRumbleEntry = await native().catch(error => ({readError: String(error)}));
  report.uiError = await page.locator('#error').innerText().catch(() => '');
  await screenshot('rumble-entry-result');
  assert.deepEqual(report.afterRumbleEntry, {message: 'Original main menu', phase: 11, running: 1});
  assert.equal(report.uiError, '', 'Original Rumble must remain running after the intro begins.');

  const checkpoint = async name => {
    await page.waitForTimeout(300);
    const state = await native();
    const file = await screenshot(name);
    report.reentrySteps.push({name, state,
      screenshotSha256: await fs.readFile(file).then(bytes => createHash('sha256').update(bytes).digest('hex'))});
    await save();
    assert.deepEqual(state, {message: 'Original main menu', phase: 11, running: 1});
  };
  report.reentrySteps = [];
  await press('m');
  await checkpoint('rumble-controller-1-off');
  await press('o');
  await checkpoint('rumble-back-to-settings');
  await press('o');
  await checkpoint('rumble-back-to-main');
  await press('m');
  await checkpoint('settings-reentered');
  await press('m');
  await checkpoint('rumble-reentered');
  report.result = 'pass';
} catch (error) {
  report.result = 'fail';
  report.failure = error?.stack || String(error);
  if (page) {
    report.nativeAtFailure = await native().catch(() => null);
    report.uiError = await page.locator('#error').innerText().catch(() => '');
    await screenshot('failure').catch(() => {});
  }
} finally {
  driver?.dispose();
  await context?.close().catch(error => { report.contextCloseError = String(error); });
  await browser?.close().catch(error => { report.browserCloseError = String(error); });
  await save();
}
console.log(JSON.stringify(report, null, 2));
if (report.result !== 'pass') process.exitCode = 1;
