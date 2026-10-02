#!/usr/bin/env node
/* Original VS Rules/Items -> real match -> Results -> CSS route capture.
 * The browser uses ordinary B0XX keyboard events and source scene callbacks;
 * direct native reads are restricted to read-only route observations. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';

const options = Object.fromEntries(['url', 'disc', 'out', 'playwright']
  .map(name => [name, {type: 'string'}]));
options['menu-only'] = {type: 'boolean', default: false};
options['rules-items-only'] = {type: 'boolean', default: false};
const {values} = parseArgs({options, strict: true});
const menuOnly = values['menu-only'];
const rulesItemsOnly = values['rules-items-only'];
for (const name of ['url', 'disc', 'out'])
  if (!values[name]) throw Error('Use --url DEVELOPMENT_RUNTIME_URL --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR]');
const output = path.resolve(values.out);
const discPath = path.resolve(values.disc);
const expectedDiscSha256 = 'b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c';
const redactDiscPath = value => String(value ?? '').split(discPath).join('[owned disc path]');
try { await fs.access(output); throw Error(`Refusing to overwrite existing evidence: ${output}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await fs.mkdir(output, {recursive: false});

const sha256File = file => new Promise((resolve, reject) => {
  const hash = createHash('sha256');
  const stream = createReadStream(file);
  stream.on('error', reject);
  stream.on('data', bytes => hash.update(bytes));
  stream.on('end', () => resolve(hash.digest('hex')));
});
let browser, context, page, driver, browserPath, playwrightPath;
const report = {
  schema: 'melee-web-vs-rules-item-menu-browser-v1',
  mode: rulesItemsOnly ? 'source-rules-items-entry-reproducer'
    : menuOnly ? 'source-menu-boundary-reproducer' : 'source-rules-items-match-route',
  scope: rulesItemsOnly
    ? 'Headless rendered original CSS -> Main -> VS -> Rules -> Items, ending after the source Items input lock clears. This reduced route reproduces Rules-row navigation and original Items entry before another longer capture.'
    : menuOnly
    ? 'Headless rendered original Main -> VS -> Rules -> Items B -> Rules B -> VS -> Rules re-entry and Main exit. This reduced route exists to validate the source menu-focus boundary before the longer match capture.'
    : 'Headless rendered original-source VS Rules and Items route, changed stock/item setup, source-selected match, No Contest Results return to CSS, and clean Eject/reimport. Keyboard PAD delivery only; retail/Dolphin, physical input, and performance equivalence are separate unrun claims.',
  url: values.url,
  browser: {executable: null, version: null, playwright: null},
  viewport: {width: 1280, height: 960},
  discIdentity: 'USA Rev. 2 CISO',
  discSha256: null,
  inputConfiguration: {playerOne: 'B0XX keyboard PAD', playerTwo: 'off', keyMap: {
    confirm: 'm', back: 'o', start: '7', left: '2', down: '3', right: '4', up: ']'}},
  saveMode: 'Everything unlocked (native runtime default, configured_save_mode = MELEE_WEB_SAVE_MODE_EVERYTHING; isolated Playwright context). No Personal profile is loaded; Personal autosave is not exercised by this route.',
  source: {
    modeScene: 'Original GM_MENU=1 / GS_MENU=1 owns Main/VS/Rules/Items; MenuKind Main=0, VS=2, Rules=13, Items=16',
    vsStates: 'GM_VS=2: GS_CSS=8 -> GS_SSS=9 -> GS_VS=2 -> GS_RESULTS=5',
    observerSceneIds: 'Development host scene observer: CSS=1, SSS=2, Title=3, Main=4',
    mainRootInput: 'mn_8022DB10: SEL_MAIN_VS changes the original menu kind from Main to VS and resets focus to 0',
    vsRulesInput: 'mn_8022D594: SEL_VS_RULES calls mn_80231714 to enter original Rules',
    rulesThink: 'fn_8022F538',
    rulesBack: 'fn_8022F538 B in GM_MENU calls mn_80229894(2, 3, 3) to return to VS Rules focus',
    rulesToItems: 'fn_8022F538 selection 5 calls mnItemSw_802358C0',
    itemThink: 'fn_80233E10',
    itemInputGate: 'Observer reads mnItemSw_804D6BEC so the harness waits for the original transition lock before navigation inputs',
    itemCancelCommit: 'fn_80233E10 B -> mnItemSw_CommitItems -> lbCardGame_UpdatePowerTime -> mn_8023164C',
    rulesMatchHandoff: 'fn_8022F538 Start in GM_MENU commits GameRules then calls mn_80229860(GM_VS)',
    sourceModeHandoff: 'gm_Mode_Menu_States[0].on_exit consumes pending GM_VS; host snapshots the committed source globals after this callback',
    cssToMain: 'Original mn_8022F218 detects PAD_LR_START; B0XX q+9+7 follows CSS parent route to GM_MENU',
    matchNoContest: 'Original PAD_LR_START+A+Start; B0XX q+9+m+7 follows Results',
    assets: ['MnMaAll.usd:MenMainConRl_Top/MenMainConIs_Top', 'SdMenu.usd:SIS_MenuData', 'SdToy.dat:SIS_ToyData'],
    menuInput: 'source PAD up/down/left/right/A/B/Start; no direct selection writes',
  },
  retailReference: {
    status: 'not_run',
    blocker: 'A cold-DOL Rules/Items-to-Results source route driver is implemented and statically checked, but no verified operator setup receipt has been located and the original route has not yet been captured.',
  },
  evidenceClaims: {
    renderedBrowser: {status: 'pending', scope: 'screenshots from headless installed Chrome'},
    sourceStateAndNavigation: {status: 'pending', scope: 'read-only original source menu/rule/match state plus PAD-routed transitions'},
    retailSourceComparison: {status: 'not_run', reason: 'no prepared retail/Dolphin capture environment'},
    visualEquivalence: {status: 'not_run', reason: 'browser screenshots are retained without retail comparison'},
    audioEquivalence: {status: 'not_run', reason: 'headless speaker output is muted by shared browser policy; source audio processing remains enabled'},
    physicalInput: {status: 'not_run', reason: 'browser PAD keyboard routing only'},
    performance: {status: 'not_run', reason: 'functional route capture is not a performance campaign'},
  },
  checks: [], screenshots: {}, input: [], timingPauses: [], timingPauseRecovery: [],
  sourceObservations: [], matchObservations: [],
  lifecycleObservations: [], errors: [],
};
const serializeReport = () => JSON.stringify(report, (_key, value) =>
  typeof value === 'string' ? redactDiscPath(value) : value, 2) + '\n';
try {
  const loaded = await loadBrowserTools(values.playwright);
  browserPath = loaded.browserPath;
  playwrightPath = loaded.playwrightPath;
  browser = await loaded.chromium.launch(browserLaunchOptions(loaded.browser, {headed: false}));
  context = await browser.newContext({viewport: {width: 1280, height: 960}});
  page = await context.newPage();
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: 90000,
    deadline: Date.now() + 15 * 60 * 1000});
  report.browser = {executable: path.basename(browserPath), version: browser.version(), playwright: playwrightPath};
  report.discSha256 = await sha256File(discPath);
  assert.equal(report.discSha256, expectedDiscSha256,
    'Browser route requires the owned USA Rev. 2 source image identity');
} catch (error) {
  report.result = 'fail';
  report.evidenceClaims.renderedBrowser.status = 'failed';
  report.evidenceClaims.sourceStateAndNavigation.status = 'not_started';
  report.failure = {message: redactDiscPath(error.message), stack: redactDiscPath(error.stack)};
  try { driver?.dispose(); } catch {}
  try { await context?.close(); } catch {}
  try { await browser?.close(); } catch {}
  await fs.writeFile(path.join(output, 'report.json'), serializeReport());
  throw Error(redactDiscPath(error.message));
}
const MAIN_MENU_KIND = 0;
const VS_MENU_KIND = 2;
const RULES_MENU_KIND = 13;
const ITEMS_MENU_KIND = 16;
page.on('pageerror', error => report.errors.push({kind: 'pageerror', message: error.message}));
page.on('console', message => { if (message.type() === 'error') report.errors.push({kind: 'console', message: message.text()}); });
page.on('response', response => { if (response.status() >= 400) report.errors.push({kind: 'http', status: response.status(), url: response.url()}); });

const current = () => page.evaluate(() => ({
  message: Module?._melee_web_native_menu_message ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
  phase: Module?._melee_web_native_menu_phase?.() ?? null,
  running: Module?._melee_web_native_menu_running?.() ?? null,
  status: document.querySelector('#status')?.textContent || '',
  error: document.querySelector('#status')?.dataset.runtimeError || null,
}));
const observeSource = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_source_observe())));
const observeMatch = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_match_observe())));
const observeLifecycle = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_memory())));
const ensureNoError = async label => {
  const state = await current();
  if (state.error) throw Error(`${label}: ${state.error}`);
  return state;
};
const resumeTimingPause = async label => {
  const state = await current();
  if (!state.message?.startsWith('Paused after a timing disruption')) return false;
  const pause = {label, phase: state.phase, message: state.message,
    observedAt: new Date().toISOString()};
  report.timingPauses.push(pause);
  const control = await page.evaluate(() => {
    const button = document.querySelector('#pause');
    return {found: Boolean(button), enabled: Boolean(button && !button.disabled),
      text: button?.textContent?.trim() ?? null};
  });
  const recovery = {label, control, attemptedAt: new Date().toISOString(), status: 'not_started'};
  report.timingPauseRecovery.push(recovery);
  if (!control.enabled) {
    recovery.status = 'unavailable';
    throw Error(`${label}: observed timing pause; development runtime #pause control is missing or disabled`);
  }
  try {
    await page.locator('#pause').click({timeout: 1500});
    await page.waitForFunction(() => Module._melee_web_native_menu_running() &&
      !Module.UTF8ToString(Module._melee_web_native_menu_message())
        .startsWith('Paused after a timing disruption'), null, {timeout: 15000});
    recovery.status = 'resumed';
    recovery.completedAt = new Date().toISOString();
    report.checks.push(`${label}: recovered the observed timing pause through the development runtime control`);
  } catch (error) {
    recovery.status = 'failed';
    recovery.error = error.message;
    throw error;
  }
  return true;
};
const shot = async name => {
  const file = path.join(output, `${name}.png`);
  await page.locator('#canvas').screenshot({path: file});
  report.screenshots[name] = {path: file, sha256: await sha256File(file), state: await current()};
};
const verifyTeardown = async label => {
  const state = await observeLifecycle();
  assert.equal(state.source_session_owned, false);
  assert.equal(state.source_objects, 0);
  assert.equal(state.source_processes, 0);
  assert.equal(state.menu_present, false);
  assert.equal(state.match_present, false);
  assert.equal(state.results_present, false);
  assert.equal(state.prize_present, false);
  assert.equal(state.scoped_assets, false);
  assert.equal(state.asset_files, 0);
  assert.equal(state.asset_bytes, 0);
  assert.equal(state.cached_archives, 0);
  assert.equal(state.cached_audio_banks, 0);
  report.lifecycleObservations.push({label, ...state});
  report.checks.push(`${label}: source owners, scoped assets and audio banks cleared`);
  return state;
};
const press = async (key, timing = {}) => {
  await resumeTimingPause(`before ${key}`);
  await ensureNoError(`before ${key}`);
  report.input.push(key);
  await driver.pressChord([key], {holdMs: 120, releaseMs: 220, ...timing});
  await ensureNoError(`after ${key}`);
  await resumeTimingPause(`after ${key}`);
};
const chord = async keys => {
  await resumeTimingPause(`before ${keys.join('+')}`);
  await ensureNoError(`before ${keys.join('+')}`);
  report.input.push(keys);
  await driver.pressChord(keys, {holdMs: 120, releaseMs: 250});
  await ensureNoError(`after ${keys.join('+')}`);
  await resumeTimingPause(`after ${keys.join('+')}`);
};
const waitMessage = async (message, label) => {
  const deadline = Date.now() + 90000;
  let state;
  while (Date.now() < deadline) {
    state = await current();
    if (state.error) throw Error(`${label || message}: ${state.error}`);
    if (state.running && state.message === message) break;
    await resumeTimingPause(label || message);
    await page.waitForTimeout(50);
  }
  assert(state?.running && state.message === message,
    `Timed out waiting for ${label || message}: ${JSON.stringify(state)}`);
  state = await ensureNoError(label || message);
  report.checks.push(`${label || message}: ${state.message}, phase ${state.phase}`);
  return state;
};
const waitPhase = async (phases, label, timeoutMs = 90000) => {
  const expected = Array.isArray(phases) ? phases : [phases];
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const state = await current();
    if (state.error) throw Error(`${label}: ${state.error}`);
    if (state.running && expected.includes(state.phase)) return state;
    if (await resumeTimingPause(label)) continue;
    await page.waitForTimeout(50);
  }
  throw Error(`Timed out waiting for ${label}; current state: ${JSON.stringify(await current())}`);
};
const waitMenu = async (menuKind, hovered, label) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.scene === 4 &&
        observation.source.menu_kind === menuKind &&
        observation.source.hovered_selection === hovered) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.scene === 4 &&
    observation.source.menu_kind === menuKind &&
    observation.source.hovered_selection === hovered,
    `Timed out waiting for original menu ${menuKind} selection ${hovered}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  await ensureNoError(label);
  return observation;
};
const waitItemInputReady = async label => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
        !observation.source.item_input_locked) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
    !observation.source.item_input_locked,
    `Timed out waiting for original Items input lock: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  return observation;
};
const moveMenuCursor = async (menuKind, total, target) => {
  for (let attempt = 0; attempt < total * 2; attempt++) {
    const state = await observeSource();
    assert.equal(state.source?.valid, true, 'Source menu observer must be live before navigation');
    assert.equal(state.source.menu_kind, menuKind, 'Original menu kind changed during cursor navigation');
    if (state.source.hovered_selection === target) return state;
    const down = (target - state.source.hovered_selection + total) % total;
    const up = (state.source.hovered_selection - target + total) % total;
    await press(down <= up ? '3' : ']');
  }
  throw Error(`Original menu cursor did not reach ${target} in menu ${menuKind}`);
};
const enterVsRules = async label => {
  let active = await observeSource();
  assert.equal(active.source?.valid, true, 'Source Main owner must be active before VS navigation');
  assert.equal(active.source.scene, 4, 'Original Main scene owns the VS menu route');
  if (active.source.menu_kind === MAIN_MENU_KIND) {
    await moveMenuCursor(MAIN_MENU_KIND, 5, 1); // SEL_MAIN_VS.
    active = await waitMenu(MAIN_MENU_KIND, 1, `${label}: Main VS selection`);
    await press('m');
    active = await waitMenu(VS_MENU_KIND, 0, `${label}: open VS selection`);
  } else {
    assert.equal(active.source.menu_kind, VS_MENU_KIND,
      'Rules can be entered only from the original Main or VS selection');
  }
  report.sourceObservations.push({label: `${label}: VS submenu`, ...active});
  await moveMenuCursor(VS_MENU_KIND, 5, 3); // SEL_VS_RULES.
  await shot(`${label}-vs-rules-choice`);
  await press('m');
  const rules = await waitMenu(RULES_MENU_KIND, 0, `${label}: open original VS Rules`);
  await shot(`${label}-rules`);
  return rules;
};
let failure;
let nativeSessionActive = false;
try {
  const response = await page.goto(values.url, {timeout: 30000});
  assert.equal(response?.status(), 200);
  await driver.waitForImport();
  await page.locator('#controls-open').click();
  await page.locator('#player-one-source').selectOption('keyboard');
  await page.locator('#player-two-source').selectOption('off');
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.locator('#controls-close').click();
  report.checks.push('fresh isolated browser context has no user profile; native source runtime defaults to Everything unlocked; B0XX P1 and no physical controller');

  await driver.selectDisc(discPath);
  await driver.waitForStart();
  nativeSessionActive = true;
  await driver.launch();
  await waitMessage('Original character select', 'initial CSS');
  await shot('00-initial-css');

  await chord(['q', '9', '7']);
  await waitMessage('Original main menu', 'CSS -> source Main');
  await waitMenu(MAIN_MENU_KIND, 0, 'source Main root');
  await shot('01-main-root');
  route: {
  let rules = await enterVsRules('02-first');

  if (rulesItemsOnly) {
    await moveMenuCursor(RULES_MENU_KIND, 7, 5);
    await press('m');
    await waitMenu(ITEMS_MENU_KIND, 0, 'Rules selection 5 opens original Items');
    await waitItemInputReady('reduced Items entry input lock released');
    await shot('03-items-reduced-route');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after reduced Rules/Items entry');
    report.checks.push('Reduced original Rules selection-to-Items entry route exits through Eject');
    break route;
  }

  // Enter and back out of the original item settings screen, then leave and
  // re-enter Rules using its own source B routes before committing the edit.
  await moveMenuCursor(RULES_MENU_KIND, 7, 5);
  await press('m');
  await waitMenu(ITEMS_MENU_KIND, 0, 'first original Items entry');
  await waitItemInputReady('first Items transition lock released');
  await shot('03-items-first-entry');
  await press('o');
  await waitMenu(RULES_MENU_KIND, 5, 'Items B commits and returns to VS Rules');
  await press('o');
  const vsAgain = await page.waitForFunction(menuKind => {
    const observation = JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_source_observe()));
    return observation.source?.valid && observation.source.scene === 4 &&
      observation.source.menu_kind === menuKind && observation.source.hovered_selection === 3;
  }, VS_MENU_KIND, {timeout: 15000});
  await vsAgain.dispose();
  report.sourceObservations.push({label: 'VS Rules B returns to original VS selection', ...(await observeSource())});
  rules = await enterVsRules('04-reentry');
  report.checks.push('original item/settings screen exits through B; VS Rules re-enters through original source callbacks');

  if (menuOnly) {
    await press('o');
    await waitMenu(VS_MENU_KIND, 3, 'Rules B returns to VS selection in reduced route');
    await press('o');
    await waitMenu(MAIN_MENU_KIND, 1, 'VS B returns to Main in reduced route');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after reduced source-menu route');
    break route;
  }

  await moveMenuCursor(RULES_MENU_KIND, 7, 5);
  await press('m');
  await waitMenu(ITEMS_MENU_KIND, 0, 'second original Items entry');
  await waitItemInputReady('source Items animation lock released');
  await shot('05-items-edit');
  const itemsBefore = (await observeSource()).source.items;
  await press('m');
  await page.waitForFunction(({before, menuKind}) => {
    const observation = JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_source_observe()));
    return observation.source?.valid && observation.source.scene === 4 &&
      observation.source.menu_kind === menuKind && observation.source.items.mask_hex !== before;
  }, {before: itemsBefore.mask_hex, menuKind: ITEMS_MENU_KIND}, {timeout: 15000});
  const itemsAfterToggle = await observeSource();
  report.sourceObservations.push({label: 'source A toggles the selected item mask bit', ...itemsAfterToggle});
  assert.notEqual(itemsAfterToggle.source.items.mask_hex, itemsBefore.mask_hex,
    'Original A must change a real source item preference');
  const toggledMaskBits = BigInt(`0x${itemsAfterToggle.source.items.mask_hex}`) ^
    BigInt(`0x${itemsBefore.mask_hex}`);
  assert.equal(toggledMaskBits & (toggledMaskBits - 1n), 0n,
    'Original A on one item row must change exactly one item-mask bit');
  await waitItemInputReady('source Items accepts navigation after the item toggle');

  await press('2'); // Source mnItemSw maps D-left at item row 0 to frequency row 31.
  await waitItemInputReady('source Items accepts frequency navigation');
  let frequency = await observeSource();
  assert.equal(frequency.source.menu_kind, ITEMS_MENU_KIND);
  assert.equal(frequency.source.hovered_selection, 31);
  // Source stores menu frequency as the selected x21 value minus one. Use
  // original right inputs until x21=0, the source None value, to avoid item
  // object services outside this route's ownership boundary.
  for (let attempt = 0; frequency.source.confirmed_selection !== 0 && attempt < 5; attempt++) {
    await press(']'); // Original Items Up decrements x21 toward source None (zero).
    await waitItemInputReady('source frequency animation lock released');
    frequency = await observeSource();
    assert.equal(frequency.source.hovered_selection, 31);
  }
  assert.equal(frequency.source.confirmed_selection, 0,
    'Original item-frequency selector must reach its source None option');
  await shot('06-items-frequency-none');
  await press('o');
  rules = await waitMenu(RULES_MENU_KIND, 5, 'Items B commits preferences and returns to Rules');
  assert.equal(rules.source.items.frequency, -1, 'Original Items B must commit item frequency None (-1)');
  assert.notEqual(rules.source.items.mask_hex, itemsBefore.mask_hex,
    'Original Items B must retain the changed item mask');
  await shot('07-rules-after-items');

  // Rules layout in Stock mode uses selection 1 for the stock value; source
  // left changes 4 -> 3 and the original Start commits it into GameRules.
  await moveMenuCursor(RULES_MENU_KIND, 7, 1);
  rules = await observeSource();
  assert.equal(rules.source.confirmed_selection, 4,
    'Fresh Everything profile source stock setting is four before edit');
  await press('2');
  const stockEdited = await observeSource();
  assert.equal(stockEdited.source.confirmed_selection, 3,
    'Original left input must change the selected stock setting to three');
  report.sourceObservations.push({label: 'source Rules cursor changed stock value to three', ...stockEdited});
  await shot('08-rules-three-stock');
  await press('7');
  await waitMessage('Original character select', 'Rules Start -> checked GM_VS -> CSS');
  const cssSetup = await observeSource();
  assert.equal(cssSetup.source.valid, true);
  assert.equal(cssSetup.source.scene, 1);
  assert.equal(cssSetup.source.rules.stock_count, 3,
    'Main -> GM_VS handoff must retain source-committed three-stock rules through CSS');
  assert.equal(cssSetup.source.items.frequency, -1);
  assert.equal(cssSetup.source.items.mask_hex, rules.source.items.mask_hex);
  report.sourceObservations.push({label: 'CSS after original VS rules handoff', ...cssSetup});
  await shot('09-css-with-retained-settings');

  await press('7');
  await waitPhase(3, 'original SSS');
  await shot('10-original-sss');
  let stage = 1;
  for (let sample = 0; sample < 600; sample++) {
    stage = await page.evaluate(() => Module._melee_web_native_menu_drive_stage(32));
    if (stage === 2) break;
    if (stage !== 1) throw Error(`Original SSS source stage driver returned ${stage}`);
  }
  assert.equal(stage, 2, 'Original SSS cursor must reach Final Destination using PAD input');
  report.checks.push('SSS source geometry driver reaches Final Destination; source stage choice is not assigned directly');
  await shot('11-sss-final-destination');
  await press('m');
  await waitPhase(7, 'live VS match');
  const matchDeadline = Date.now() + 90000;
  let matchBeforeNoContest;
  while (Date.now() < matchDeadline) {
    matchBeforeNoContest = await observeMatch();
    if (matchBeforeNoContest.ready && matchBeforeNoContest.frame >= 180) break;
    const state = await current();
    if (state.message?.startsWith('Paused after a timing disruption'))
      await waitPhase(7, 'live VS match', Math.min(15000, matchDeadline - Date.now()));
    await page.waitForTimeout(100);
  }
  assert(matchBeforeNoContest?.ready && matchBeforeNoContest.frame >= 180,
    `Live match did not reach 180 frames: ${JSON.stringify(matchBeforeNoContest)}`);
  report.matchObservations.push({label: 'live source match after 180 gameplay frames', ...matchBeforeNoContest});
  assert.deepEqual(matchBeforeNoContest.rules.player_stocks, [3, 3]);
  assert.equal(matchBeforeNoContest.rules.item_frequency, -1);
  assert.equal(matchBeforeNoContest.rules.match_kind, 1);
  assert.equal(matchBeforeNoContest.rules.stage, 0x20,
    'Source-selected SSS Final Destination must reach the match as St_Kind_Last');
  const sourceStart = await observeSource();
  assert.equal(sourceStart.start.valid, true, 'Closed SSS must expose the raw source StartMeleeData');
  assert.equal(sourceStart.start.item_frequency, matchBeforeNoContest.rules.item_frequency);
  assert.equal(sourceStart.start.item_mask_hex, matchBeforeNoContest.rules.item_mask_hex);
  assert.equal(sourceStart.start.stage, 0x20,
    'Closed SSS must commit original St_Kind_Last into StartMeleeData');
  assert.deepEqual(sourceStart.start.player_stocks, [3, 3]);
  report.sourceObservations.push({label: 'raw SSS StartMeleeData after original OnExit', ...sourceStart});
  await shot('12-live-three-stock-match');

  await chord(['q', '9', 'm', '7']);
  await waitPhase(8, 'original Results');
  await page.waitForTimeout(4500); // Match the established source Results presentation boundary.
  const resultObservation = await observeMatch();
  report.matchObservations.push({label: 'terminal No Contest payload used by Results', ...resultObservation});
  await shot('13-original-results');
  for (let confirmation = 0; confirmation < 8; confirmation++) {
    const state = await current();
    if (state.phase !== 8) break;
    await press('7', {releaseMs: 1380});
  }
  await waitPhase([1, 9], 'Results/CSS transition');
  if ((await current()).phase === 9) {
    for (let confirmation = 0; confirmation < 60 && (await current()).phase === 9; confirmation++)
      await press('7', {releaseMs: 380});
  }
  await waitPhase(1, 'CSS after Results');
  const cssAfterResults = await observeSource();
  assert.equal(cssAfterResults.source.rules.stock_count, 3,
    'Results -> CSS must retain the source-committed stock count');
  assert.equal(cssAfterResults.source.items.frequency, -1);
  assert.equal(cssAfterResults.source.items.mask_hex, cssSetup.source.items.mask_hex);
  report.sourceObservations.push({label: 'CSS after original Results return', ...cssAfterResults});
  await shot('14-css-after-results-settings-retained');

  await chord(['q', '9', '7']);
  await waitMessage('Original main menu', 'CSS -> original Main after Results');
  await waitMenu(MAIN_MENU_KIND, 0, 'Main root after Results');
  rules = await enterVsRules('15-retained');
  assert.equal(rules.source.rules.stock_count, 3,
    'Results/CSS/Main/VS navigation must retain the committed stock count');
  assert.equal(rules.source.items.frequency, -1);
  assert.equal(rules.source.items.mask_hex, cssSetup.source.items.mask_hex);
  report.checks.push('Original Rules re-entry after Results retains source-committed stock and item settings');
  await shot('16-rules-retained-after-results');
  await press('o');
  await waitMenu(VS_MENU_KIND, 3, 'Rules B returns to VS selection after Results');
  await press('o');
  await waitMenu(MAIN_MENU_KIND, 1, 'VS B returns to Main selection after Results');

  // Same document, fresh native owners after Eject/reimport catches stale menu,
  // source mode, audio, or PAD ownership after a completed match route.
  await driver.unload();
  nativeSessionActive = false;
  await verifyTeardown('Eject after Results/Main');
  await driver.selectDisc(discPath);
  await driver.waitForStart();
  nativeSessionActive = true;
  await driver.launch();
  await waitPhase(1, 'CSS after Eject/reimport');
  report.checks.push('Eject tears down Results/match/menu/audio owners; same isolated browser context reimports to original CSS');
  await shot('15-clean-reimport-css');
  await driver.unload();
  nativeSessionActive = false;
  await verifyTeardown('Final Eject after clean reimport');
  report.checks.push('second source owner unload returns the browser to idle import state');
  }
  if (report.errors.length) throw Error(`Browser emitted errors: ${JSON.stringify(report.errors)}`);
  report.evidenceClaims.renderedBrowser.status = 'passed';
  report.evidenceClaims.sourceStateAndNavigation.status = 'passed';
  report.result = 'pass';
} catch (error) {
  failure = error;
  report.result = 'fail';
  report.evidenceClaims.renderedBrowser.status = browser ? 'partial' : 'failed';
  report.evidenceClaims.sourceStateAndNavigation.status = nativeSessionActive ? 'partial' : 'not_started';
  report.failure = {message: redactDiscPath(error.message), stack: redactDiscPath(error.stack), diagnostics: error.diagnostics || null,
    state: await current().catch(cause => ({error: cause.message})),
    source: await observeSource().catch(cause => ({error: cause.message})),
    match: await observeMatch().catch(cause => ({error: cause.message}))};
  try {
    const file = path.join(output, 'failure.png');
    await page.locator('#canvas').screenshot({path: file});
    report.failureScreenshot = {path: file, sha256: await sha256File(file)};
  } catch {}
} finally {
  if (failure && nativeSessionActive) {
    try {
      await driver.unload();
      nativeSessionActive = false;
      const state = await observeLifecycle();
      const failures = [];
      for (const field of ['source_session_owned', 'menu_present', 'match_present',
        'results_present', 'prize_present', 'scoped_assets']) {
        if (state[field] !== false) failures.push(`${field} remained ${state[field]}`);
      }
      for (const field of ['source_objects', 'source_processes', 'asset_files',
        'asset_bytes', 'cached_archives', 'cached_audio_banks']) {
        if (state[field] !== 0) failures.push(`${field} remained ${state[field]}`);
      }
      report.failureCleanup = {status: failures.length ? 'failed' : 'passed', failures, ...state};
    } catch (error) {
      report.failureCleanup = {status: 'failed', message: redactDiscPath(error.message)};
    }
  }
  report.diagnostics = await driver.diagnostics();
  await fs.writeFile(path.join(output, 'report.json'), serializeReport());
  driver.dispose();
  await context.close();
  await browser.close();
}
console.log(JSON.stringify({result: report.result, checks: report.checks,
  sourceObservations: report.sourceObservations.length,
  matchObservations: report.matchObservations.length,
  failure: report.failure?.message, report: path.join(output, 'report.json')}));
if (failure) process.exitCode = 1;
