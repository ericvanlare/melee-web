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
import {
  ITEM_ROW_TO_PREFERENCE_BIT,
  competitiveMatchStartFailures,
  competitiveProfileFailures,
  deriveAllOffItemMasks,
} from './vs_rules_competitive_profile_helpers.mjs';

const options = Object.fromEntries(['url', 'disc', 'out', 'playwright']
  .map(name => [name, {type: 'string'}]));
options['menu-only'] = {type: 'boolean', default: false};
options['rules-items-only'] = {type: 'boolean', default: false};
options['css-sss-only'] = {type: 'boolean', default: false};
options['stage-only'] = {type: 'boolean', default: false};
options['no-contest-only'] = {type: 'boolean', default: false};
options['team-battle'] = {type: 'boolean', default: false};
options['team-setup-only'] = {type: 'boolean', default: false};
options['competitive-profile-only'] = {type: 'boolean', default: false};
options['competitive-match-start-only'] = {type: 'boolean', default: false};
const {values} = parseArgs({options, strict: true});
const menuOnly = values['menu-only'];
const rulesItemsOnly = values['rules-items-only'];
const cssSssOnly = values['css-sss-only'];
const stageOnly = values['stage-only'];
const noContestOnly = values['no-contest-only'];
const competitiveProfileOnly = values['competitive-profile-only'];
const competitiveMatchStartOnly = values['competitive-match-start-only'];
const teamSetupOnly = values['team-setup-only'];
const teamBattle = values['team-battle'] || teamSetupOnly;
if (teamSetupOnly && values['team-battle'])
  throw Error('--team-setup-only and --team-battle select different route lengths');
if (teamBattle && (menuOnly || rulesItemsOnly || cssSssOnly || stageOnly || noContestOnly ||
    competitiveProfileOnly || competitiveMatchStartOnly))
  throw Error('--team-battle runs the full Rules/Items/match route and cannot be combined with a reduced-route flag');
if (competitiveProfileOnly &&
    (menuOnly || rulesItemsOnly || cssSssOnly || stageOnly || noContestOnly || teamSetupOnly ||
      competitiveMatchStartOnly))
  throw Error('--competitive-profile-only is a dedicated source Rules/Items-to-CSS route');
if (competitiveMatchStartOnly &&
    (menuOnly || rulesItemsOnly || cssSssOnly || stageOnly || noContestOnly || teamSetupOnly ||
      competitiveProfileOnly))
  throw Error('--competitive-match-start-only runs the competitive profile through a short normalized match prefix');
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
let failure;
let interruptionSignal = null;
const cleanupPromises = new Map();
const cleanupResults = new Map();
let reportWriteError = null;
const report = {
  schema: 'melee-web-vs-rules-item-menu-browser-v1',
  mode: competitiveMatchStartOnly ? 'source-competitive-normalized-match-start-prefix'
    : competitiveProfileOnly ? 'source-competitive-rules-profile-preflight'
    : noContestOnly ? 'source-no-contest-results-reproducer'
    : teamSetupOnly ? 'source-team-setup-cancel-reentry-reproducer'
    : teamBattle ? 'source-two-player-team-battle-results-route'
    : stageOnly ? 'source-sss-stage-driver-reproducer'
    : rulesItemsOnly ? 'source-rules-items-entry-reproducer'
    : cssSssOnly ? 'source-css-to-sss-cooldown-reproducer'
    : menuOnly ? 'source-menu-boundary-reproducer' : 'source-rules-items-match-route',
  scope: competitiveMatchStartOnly
    ? 'Headless original CSS -> Main/VS/Rules/Items/Rules Plus -> CSS; enable P2 through Controls and require the original CSS CPU/empty/Human transitions before Start; select Final Destination through original SSS, check normalized two-human Mario stock settings once at the first available observation in the bounded 180–240 source-frame window (about a 3-second prefix), then Eject. No 8-minute match, timeout, Results, retail comparison, or full-route acceptance claim.'
    : competitiveProfileOnly
    ? 'Headless original CSS -> Main/VS/Rules/Items/Rules Plus -> CSS; B0XX P1 inputs set and verify the exact GameRules profile and preserve raw CSS StartMeleeData for provenance. CSS data is not treated as normalized before SSS. No match, timeout, Results, retail comparison, or acceptance claim.'
    : noContestOnly
    ? 'Headless rendered original CSS -> SSS -> Final Destination -> match; P1 Start opens the original source pause, then the held LRAS+Start No Contest chord enters Results and Eject verifies teardown.'
    : teamSetupOnly
    ? 'Headless rendered original CSS -> Main/VS/Rules/Items -> CSS; original CSS Teams toggle and P2 team-color input configure the two existing players, SSS B cancellation returns to CSS with Rules retained, SSS re-entry and a second B cancellation return to CSS, then Eject verifies teardown.'
    : teamBattle
    ? 'Headless rendered original CSS -> Main/VS/Rules/Items -> CSS; original CSS Teams toggle and team-color input configure two opposing players, SSS B cancellation returns to CSS, SSS re-entry starts a real team match, and original Results returns to CSS before Eject/reimport.'
    : stageOnly
    ? 'Headless rendered original CSS -> Main -> VS Rules/Items changes -> CSS -> SSS; the PAD-fed source stage driver reaches Final Destination, then B returns to CSS and Eject verifies teardown. No match is launched.'
    : rulesItemsOnly
    ? 'Headless rendered original CSS -> Main -> VS -> Rules -> Items, ending after the source Items input lock clears. This reduced route reproduces Rules-row navigation and original Items entry before another longer capture.'
    : cssSssOnly
    ? 'Headless rendered original CSS → Main → VS Rules/Items changes → CSS → SSS after the source-authored CSS Start cooldown → SSS B to CSS → CSS L/R/Start to Main and Eject. No match is launched.'
    : menuOnly
    ? 'Headless rendered original Main -> VS -> Rules -> Items re-entry, A item toggle, frequency selector round-trip and save, B return to Rules, then Rules B -> VS -> Main exit. This reduced route validates transient and saved item settings before the longer match capture.'
    : 'Headless rendered original-source VS Rules, Items, and Rules Plus route, changed stock/item/timer setup, source-selected match, No Contest Results return to CSS, retained settings on re-entry, and clean Eject/reimport. Keyboard PAD delivery only; retail/Dolphin, physical input, and performance equivalence are separate unrun claims.',
  url: values.url,
  browser: {executable: null, version: null, playwright: null},
  viewport: {width: 1280, height: 960},
  discIdentity: 'USA Rev. 2 CISO',
  discSha256: null,
  inputConfiguration: {playerOne: 'B0XX keyboard PAD', playerTwo: 'off', teamBattle,
    teamSetup: teamBattle ? 'P1 source stick moves to the original CSS Teams control and P2 team-color swatch; P1 A activates each' : null,
    keyMap: {confirm: 'm', back: 'o', start: '7', left: '2', down: '3', right: '4', up: ']'}},
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
    rulesToRulesPlus: 'fn_8022F538 selection 6 calls mn_802339FC; mn_802339FC sets MENU_KIND_RULES_EXTRA=15 and enters the source Rules Plus GObj',
    rulesPlusInput: 'fn_8023201C uses D-pad left/right on option 0, saves stock_time_limit in minutes, B commits and returns through mn_8023164C, and Start commits before mn_80229860(GM_VS)',
    rulesPlusProfileRows: 'Rules Plus rows 0/1/2 are stock timer minutes, friendly_fire, and pause; the source GameRules observation reports the committed values, while normalized StartMeleeData fields are produced later by gm_80167BC8 on original VS entry after SSS',
    rulesPlusMatchHandoff: 'gm_80167BC8 converts nonzero stock_time_limit minutes to StartMeleeRules.time_limit seconds and enables the match timer',
    itemInput: 'fn_80233E10: MenuInput_Back commits the item table and returns to Rules; MenuInput_A toggles the selected item; MenuInput_Start commits and returns to VS',
    itemThink: 'fn_80234C24: animates Items entry/exit, observes cursor/value changes, and commits the source item settings after confirmed changes',
    itemInputGate: 'Observer reads mnItemSw_804D6BEC so the harness waits for the original transition lock before navigation inputs',
    itemCancelCommit: 'fn_80233E10 MenuInput_Back -> mnItemSw_CommitItems -> lbCardGame_UpdatePowerTime -> mn_8023164C',
    itemSaveEffect: 'mnItemSw_CommitItems writes 31 authored item flags through MnItemSwTable.item_order (mnItemSw_803ED438) to gmMainLib_8015CC58()->item_mask and x21 - 1 to item_freq; the observer reports the live row-to-preference bit',
    selectionPayload: 'melee_web_menu_host_selection_state copies raw session-owned CSS VsModeData.start; gm_80167BC8 converts GameRules later when original VS enters gameplay after SSS, so this preflight records CSS provenance without treating raw StartMeleeData as normalized settings',
    rulesMatchHandoff: 'fn_8022F538 Start in GM_MENU commits GameRules then calls mn_80229860(GM_VS)',
    sourceModeHandoff: 'gmmenumode.c:onExit passes MenuExitData.pending_mode to gm_SetPendingGameMode and gm_SetNewGameModePending; host snapshots source globals after this callback',
    cssToMain: 'mnCharSel_Scene_OnFrame checks mn_8022F218 for PAD_LR_START; B0XX q+9+7 follows the original CSS parent route to GM_MENU',
    teamsToggle: 'mnCharSel_CursorThink toggles StartMeleeData.rules.is_teams through PAD A when the original CSS cursor is at x<-25.5 and y>22; cycleTeam assigns one of the three authored team colors to the selected CSS door',
    matchNoContest: 'Original PAD_LR_START+A+Start; B0XX q+9+m+7 follows Results',
    routeMenuAssets: ['MnSlChr.usd and SdSlChr.usd:SIS_SelCharData (non-Japanese saved language; Japanese uses .dat)', 'MnSlMap.usd (non-Japanese saved language; Japanese uses .dat)', 'MnMaAll.usd:MenMainConRl_Top/MenMainCursorRl_Top/MenMainNmRl_Top/MenMainConIs_Top/MenMainCursorIs_Top'],
    textArchives: ['SdMenu.usd:SIS_MenuData (saved US language)', 'SdMenu.dat:SIS_MenuData (other saved languages)', 'SdToy.dat:SIS_ToyData'],
    mainResources: ['mnMain_Scene_OnEnter: GmEvent.dat:sqEventInitDataLevelTbl via gm_801BA8FC', 'mnMain_Scene_OnEnter: LbAd.dat:lbAudioLoadData via lbAudioAx_8002392C'],
    mainAudioEntry: 'mnMain_Scene_OnEnter selects the configured HPS through lbAudioAx_80023F28(gmMainLib_8015ECB0()); menu sfxBack/sfxForward/sfxMove use lbAudioAx_80024030',
    menuInput: 'source PAD up/down/left/right/A/B/Start; no direct selection writes',
  },
  retailReference: {
    status: 'not_run',
    scope: 'This browser runner does not launch Dolphin. The cold-DOL Rules/Items route is captured separately, with its own source-state and screenshot receipt.',
  },
  evidenceClaims: {
    renderedBrowser: {status: 'pending', scope: 'screenshots from headless installed Chrome'},
    sourceStateAndNavigation: {status: 'pending', scope: 'read-only original source menu/rule/match state plus PAD-routed transitions'},
    retailSourceComparison: {status: 'not_run', reason: 'This browser runner does not compare its port observations against the separate retail route capture.'},
    visualEquivalence: {status: 'not_run', reason: 'browser screenshots are retained without retail comparison'},
    audioEquivalence: {status: 'not_run', reason: 'headless speaker output is muted by shared browser policy; source audio processing remains enabled'},
    physicalInput: {status: 'not_run', reason: 'browser PAD keyboard routing only'},
    performance: {status: 'not_run', reason: 'functional route capture is not a performance campaign'},
  },
  competitiveProfile: competitiveProfileOnly || competitiveMatchStartOnly ? {
    requested: {mode: 'stock', stocks: 4, timer_minutes: 8, item_frequency: -1,
      all_31_item_switches_off: true, pause_enabled: false, friendly_fire: true,
      damage_ratio_menu_value: 10, handicap: 0},
    raw_css_selection_stage: 'CSS-owned VsModeData.start before SSS and gm_80167BC8 conversion',
    source_item_row_preference_bits: ITEM_ROW_TO_PREFERENCE_BIT,
    initial_css_selection: null,
    item_rows_after_original_inputs: [],
    derived_masks: null,
    final_css_selection: null,
  } : null,
  competitiveMatchStart: competitiveMatchStartOnly ? {
    requested: {players: ['Mario', 'Mario'], css_port_identities: [1, 2],
      raw_match_payload_slot_fields: [0, 0], resolved_controller_ports: [0, 1],
      stage: 'Final Destination', normalized_stock_timer_seconds: 480,
      source_frame_observation_window: [180, 240]},
    controls_change: null,
    css_roster: [],
    normalized_match: null,
    last_observation_before_eject: null,
  } : null,
  checks: [], screenshots: {}, input: [], sourcePadSamples: [], timingPauses: [], timingPauseRecovery: [],
  sourceObservations: [], cssObservations: [], matchObservations: [],
  lifecycleObservations: [], errors: [],
};
const serializeReport = () => JSON.stringify(report, (_key, value) =>
  typeof value === 'string' ? redactDiscPath(value) : value, 2) + '\n';
const closeOnce = (name, resource, close) => {
  if (!resource) return null;
  if (!cleanupPromises.has(name)) {
    cleanupPromises.set(name, Promise.resolve().then(close).then(() => {
      cleanupResults.set(name, {status: 'closed'});
    }, error => {
      cleanupResults.set(name, {status: 'failed', error: String(error?.message || error)});
    }));
  }
  return cleanupPromises.get(name);
};
const closeOwnedResources = async () => {
  if (driver) {
    if (!cleanupPromises.has('driver')) {
      cleanupPromises.set('driver', Promise.resolve().then(() => driver.dispose()).then(() => {
        cleanupResults.set('driver', {status: 'disposed'});
      }, error => {
        cleanupResults.set('driver', {status: 'failed', error: String(error?.message || error)});
      }));
    }
  }
  closeOnce('context', context, () => context.close());
  closeOnce('browser', browser, () => browser.close());
  await Promise.all([...cleanupPromises.values()]);
  report.cleanup = Object.fromEntries(cleanupResults);
  return report.cleanup;
};
const checkInterruption = () => {
  if (!interruptionSignal) return;
  const error = new Error(`Browser profile preflight interrupted by ${interruptionSignal}`);
  error.code = 'PROFILE_PREFLIGHT_INTERRUPTED';
  throw error;
};
const onOwnedInterrupt = signal => {
  if (interruptionSignal) return;
  interruptionSignal = signal;
  const error = new Error(`Browser profile preflight interrupted by ${signal}`);
  error.code = 'PROFILE_PREFLIGHT_INTERRUPTED';
  failure = failure || error;
  report.result = 'interrupted';
  report.interruption = {signal, receivedAt: new Date().toISOString(), cleanup: 'started'};
  report.failure = report.failure || {message: error.message, code: error.code};
  // Closing the owned Browser forces an in-flight Playwright operation to
  // reject into the normal catch/finally path; the signal itself never exits
  // Node before that cleanup can run.
  void closeOwnedResources().then(cleanup => {
    if (report.interruption) {
      report.interruption.cleanup = Object.values(cleanup).every(item =>
        item.status === 'closed' || item.status === 'disposed') ? 'completed' : 'partial';
    }
  }).catch(error => {
    if (report.interruption) report.interruption.cleanup = `failed: ${error.message}`;
  });
};
const onSigint = () => onOwnedInterrupt('SIGINT');
const onSigterm = () => onOwnedInterrupt('SIGTERM');
process.on('SIGINT', onSigint);
process.on('SIGTERM', onSigterm);
const persistReport = async () => {
  try {
    await fs.writeFile(path.join(output, 'report.json'), serializeReport());
  } catch (error) {
    reportWriteError = error;
    console.error(`Could not persist browser profile report: ${error.message}`);
  }
};
try {
  const loaded = await loadBrowserTools(values.playwright);
  checkInterruption();
  browserPath = loaded.browserPath;
  playwrightPath = loaded.playwrightPath;
  browser = await loaded.chromium.launch(browserLaunchOptions(loaded.browser, {headed: false}));
  checkInterruption();
  context = await browser.newContext({viewport: {width: 1280, height: 960}});
  checkInterruption();
  page = await context.newPage();
  checkInterruption();
  driver = createBrowserDriver(page, {surface: 'development', timeoutMs: 90000,
    deadline: Date.now() + 15 * 60 * 1000});
  checkInterruption();
  report.browser = {executable: path.basename(browserPath), version: browser.version(), playwright: playwrightPath};
  report.discSha256 = await sha256File(discPath);
  assert.equal(report.discSha256, expectedDiscSha256,
    'Browser route requires the owned USA Rev. 2 source image identity');
} catch (error) {
  failure = error;
  report.result = interruptionSignal ? 'interrupted' : 'fail';
  report.evidenceClaims.renderedBrowser.status = 'failed';
  report.evidenceClaims.sourceStateAndNavigation.status = 'not_started';
  report.failure = {message: redactDiscPath(error.message), stack: redactDiscPath(error.stack)};
  try { await closeOwnedResources(); } catch (cleanupError) {
    report.cleanup_error = cleanupError.message;
  }
  await persistReport();
  process.off('SIGINT', onSigint);
  process.off('SIGTERM', onSigterm);
  throw Error(redactDiscPath(error.message));
}
const MAIN_MENU_KIND = 0;
const VS_MENU_KIND = 2;
const RULES_MENU_KIND = 13;
const ITEMS_MENU_KIND = 16;
const RULES_PLUS_MENU_KIND = 15;
page.on('pageerror', error => report.errors.push({kind: 'pageerror', message: error.message}));
page.on('console', message => { if (message.type() === 'error') report.errors.push({kind: 'console', message: message.text()}); });
page.on('response', response => { if (response.status() >= 400) report.errors.push({kind: 'http', status: response.status(), url: response.url()}); });

const current = () => page.evaluate(() => ({
  message: Module?._melee_web_native_menu_message ? Module.UTF8ToString(Module._melee_web_native_menu_message()) : null,
  phase: Module?._melee_web_native_menu_phase?.() ?? null,
  running: Module?._melee_web_native_menu_running?.() ?? null,
  status: document.querySelector('#status')?.textContent || '',
  error: document.querySelector('#status')?.dataset.runtimeError || null,
  diagnostics: Module?._melee_web_native_menu_diagnostics
    ? Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()) : '',
}));
const observeSource = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_source_observe())));
const waitCssTeams = async (isTeams, teams, label) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    const css = observation.source?.css_setup;
    if (observation.source?.valid && observation.source.scene === 1 &&
        css?.valid && css.is_teams === isTeams &&
        JSON.stringify(css.player_teams) === JSON.stringify(teams)) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  const css = observation?.source?.css_setup;
  assert(observation?.source?.valid && observation.source.scene === 1 && css?.valid &&
    css.is_teams === isTeams && JSON.stringify(css.player_teams) === JSON.stringify(teams),
    `Timed out waiting for original CSS Teams state ${isTeams}/${teams}: ${JSON.stringify(observation)}`);
  report.cssObservations.push({label, ...observation});
  return observation;
};
const observeCssSetup = () => page.evaluate(() => window.menuObserveCssSetup?.() ?? null);
const observeMatch = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_match_observe())));
const waitForMatchSourceFrames = async (target, maximum) => {
  const deadline = Date.now() + 30000;
  let observation;
  while (Date.now() < deadline) {
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(resolve)));
    await resumeTimingPause(`competitive match source frame ${target}`);
    observation = await observeMatch();
    if (observation?.observer_error)
      throw Error(`Match observer failed while waiting for source frame ${target}: ${JSON.stringify(observation)}`);
    if (observation?.ready && observation.frame >= target) {
      if (observation.frame > maximum)
        throw Error(`First ready match observation passed frame ${maximum}: ${JSON.stringify(observation)}`);
      return observation;
    }
    await ensureNoError(`competitive match source frame ${target}`);
  }
  throw Error(`Competitive match did not reach source frame ${target}: ${JSON.stringify(observation)}`);
};
const observeLifecycle = () => page.evaluate(() => JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_memory())));
const ensureNoError = async label => {
  const state = await current();
  if (state.error) throw Error(`${label}: ${state.error}`);
  return state;
};
const waitForNoQueuedPad = async label => {
  const deadline = Date.now() + 3000;
  let state;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    state = await current();
    if (state.error) throw Error(`${label}: ${state.error}`);
    if (state.diagnostics.includes('raw PAD: none')) return state;
    await page.waitForTimeout(20);
  }
  throw Error(`${label}: raw PAD did not drain: ${JSON.stringify(state)}`);
};
const resumeTimingPause = async label => {
  const state = await current();
  if (!state.message?.startsWith('Paused after a timing disruption')) return false;
  const pause = {label, phase: state.phase, message: state.message,
    observedAt: new Date().toISOString()};
  report.timingPauses.push(pause);
  if (competitiveProfileOnly || competitiveMatchStartOnly) {
    pause.status = 'failed_preflight';
    throw Error(`${label}: competitive profile preflight stops on a runtime timing disruption: ${state.message}`);
  }
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
const sourcePadSample = async (buttons, stickX, stickY, label, port = 0) => {
  await ensureNoError(`before source PAD ${label}`);
  const accepted = await page.evaluate(args => window.menuDiagnosticPad(...args),
    [port, buttons, stickX, stickY, 1]);
  assert.equal(accepted, 1, `Source PAD rejected ${label}`);
  report.sourcePadSamples.push({port, buttons, stickX, stickY, duration: 1, label});
  await waitForNoQueuedPad(`source PAD ${label} drains`);
};
const sourcePadTap = async (button, label, port = 0) => {
  await sourcePadSample(button, 0, 0, label, port);
  await page.evaluate(args => window.menuDiagnosticPad(...args), [port, 0, 0, 0, 2]);
  report.sourcePadSamples.push({port, buttons: 0, stickX: 0, stickY: 0,
    duration: 2, label: `${label}:release`});
  await waitForNoQueuedPad(`source PAD ${label} release drains`);
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
const waitRulesPlusTimer = async (expected, label) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.scene === 4 &&
        observation.source.menu_kind === RULES_PLUS_MENU_KIND &&
        observation.source.hovered_selection === 0 &&
        observation.source.confirmed_selection === expected &&
        observation.source.rules.stock_time_limit === expected) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.scene === 4 &&
    observation.source.menu_kind === RULES_PLUS_MENU_KIND &&
    observation.source.hovered_selection === 0 &&
    observation.source.confirmed_selection === expected &&
    observation.source.rules.stock_time_limit === expected,
    `Timed out waiting for source Rules Plus stock timer ${expected}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  return observation;
};
const waitRulesPlusValue = async (row, expected, label, sourceField = null) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    const source = observation.source;
    if (source?.valid && source.scene === 4 && source.menu_kind === RULES_PLUS_MENU_KIND &&
        source.hovered_selection === row && source.confirmed_selection === expected &&
        (sourceField === null || source.rules[sourceField] === expected)) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  const source = observation?.source;
  assert(source?.valid && source.scene === 4 && source.menu_kind === RULES_PLUS_MENU_KIND &&
    source.hovered_selection === row && source.confirmed_selection === expected &&
    (sourceField === null || source.rules[sourceField] === expected),
    `Timed out waiting for Rules Plus row ${row} value ${expected}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
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
const waitItemConfirmed = async (expected, label, row = 0) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
        observation.source.hovered_selection === row &&
        observation.source.confirmed_selection === expected) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
    observation.source.hovered_selection === row &&
    observation.source.confirmed_selection === expected,
    `Timed out waiting for original item row confirmation ${expected}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  return observation;
};
const waitLiveItemState = async (expected, label, row = 0) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
        observation.items_menu?.valid && observation.items_menu.cursor === row &&
        observation.items_menu.selected_item_enabled === expected) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
    observation.items_menu?.valid && observation.items_menu.cursor === row &&
    observation.items_menu.selected_item_enabled === expected,
    `Timed out waiting for source MnItemSwData row value ${expected}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  return observation;
};
const waitItemsCursor = async (expected, label) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
        observation.source.hovered_selection === expected && observation.items_menu?.valid &&
        observation.items_menu.cursor === expected) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
    observation.source.hovered_selection === expected && observation.items_menu?.valid &&
    observation.items_menu.cursor === expected,
    `Timed out waiting for source Items cursor ${expected}: ${JSON.stringify(observation)}`);
  report.sourceObservations.push({label, ...observation});
  return observation;
};
const moveItemsCursor = async target => {
  const current = await observeSource();
  assert.equal(current.source?.valid, true, 'Source Items owner must be live before row navigation');
  assert.equal(current.source.menu_kind, ITEMS_MENU_KIND, 'Original Items menu must own row navigation');
  const start = current.items_menu?.cursor;
  assert(Number.isInteger(start) && start >= 0 && start < 31 && target >= 0 && target < 31,
    `Items row navigation requires source item rows 0..30: ${JSON.stringify({start, target})}`);
  if (start === target) return current;

  const nextByInput = cursor => {
    const up = cursor === 0 ? 31 : cursor === 16 ? 32 : cursor === 31 ? 15 : cursor === 32 ? 30 : cursor - 1;
    const down = cursor === 15 ? 31 : cursor === 30 ? 32 : cursor === 31 ? 0 : cursor === 32 ? 16 : cursor + 1;
    const left = cursor >= 16 && cursor < 31 ? cursor - 16 : cursor;
    const right = cursor < 16 ? Math.min(cursor + 16, 30) : cursor;
    return [[']', up], ['3', down], ['2', left], ['4', right]];
  };
  const previous = new Map([[start, null]]);
  const queue = [start];
  for (let offset = 0; offset < queue.length && !previous.has(target); offset++) {
    const cursor = queue[offset];
    for (const [key, next] of nextByInput(cursor)) {
      if (next >= 31 || next === cursor || previous.has(next)) continue;
      previous.set(next, {cursor, key});
      queue.push(next);
    }
  }
  assert(previous.has(target), `Original Items directional graph cannot reach row ${target} from ${start}`);
  const steps = [];
  for (let cursor = target; cursor !== start;) {
    const step = previous.get(cursor);
    steps.push({key: step.key, next: cursor});
    cursor = step.cursor;
  }
  steps.reverse();
  let observation = current;
  for (const [index, step] of steps.entries()) {
    await press(step.key);
    observation = await waitItemsCursor(step.next,
      `source Items navigation ${index + 1}/${steps.length} reaches row ${step.next}`);
  }
  return observation;
};
const waitItemFrequency = async (expected, label) => {
  const deadline = Date.now() + 15000;
  let observation;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    observation = await observeSource();
    if (observation.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
        observation.source.hovered_selection === 31 && observation.items_menu?.valid &&
        observation.items_menu.cursor === 31 && observation.items_menu.frequency_selector === expected &&
        observation.source.confirmed_selection === expected) break;
    await ensureNoError(label);
    await page.waitForTimeout(50);
  }
  assert(observation?.source?.valid && observation.source.menu_kind === ITEMS_MENU_KIND &&
    observation.source.hovered_selection === 31 && observation.items_menu?.valid &&
    observation.items_menu.cursor === 31 && observation.items_menu.frequency_selector === expected &&
    observation.source.confirmed_selection === expected,
    `Timed out waiting for source item frequency selector ${expected}: ${JSON.stringify(observation)}`);
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
const runCompetitiveProfilePreflight = async initial => {
  assert.equal(initial.source?.valid, true, 'Initial source CSS owner must be observable');
  assert.equal(initial.source.scene, 1, 'Profile baseline must come from original CSS');
  assert.equal(initial.selection?.valid, true, 'CSS-owned StartMeleeData selection must be observable');
  assert.equal(initial.selection.scene, 1, 'Profile baseline must use the CSS selection-state API');
  assert.equal(initial.selection.provenance, 'raw_css_vs_start',
    'Profile baseline must retain the raw CSS payload before SSS conversion');
  const initialPreferenceMaskHex = initial.source.items.mask_hex;
  report.competitiveProfile.initial_css_selection = {
    rules: initial.source.rules,
    items: initial.source.items,
    selection: initial.selection,
  };

  await moveMenuCursor(RULES_MENU_KIND, 7, 5);
  await press('m');
  await waitMenu(ITEMS_MENU_KIND, 0, 'competitive profile opens original Items');
  await waitItemInputReady('competitive profile Items transition lock released');
  await shot('03-competitive-items-entry');

  const itemRows = [];
  for (let cursor = 0; cursor < ITEM_ROW_TO_PREFERENCE_BIT.length; cursor++) {
    const row = await moveItemsCursor(cursor);
    const item = row.items_menu;
    assert.equal(item?.valid, true, `Original Items row ${cursor} must expose its live source value`);
    assert.equal(item.cursor, cursor);
    const preferenceBit = item.selected_item_preference_bit;
    assert(Number.isInteger(preferenceBit) && preferenceBit >= 0 && preferenceBit < 32,
      `Original Items row ${cursor} has no live preference-bit mapping: ${JSON.stringify(item)}`);
    assert(item.selected_item_enabled === 0 || item.selected_item_enabled === 1,
      `Original Items row ${cursor} has a non-binary source switch value: ${JSON.stringify(item)}`);
    const initialEnabled = item.selected_item_enabled;
    let finalItem = row;
    if (initialEnabled !== 0) {
      await press('m');
      await waitItemConfirmed(0, `source Items A confirms row ${cursor} off`, cursor);
      finalItem = await waitLiveItemState(0, `source Items applies row ${cursor} off`, cursor);
      finalItem = await waitItemInputReady(`source Items accepts the next profile input after row ${cursor}`);
    }
    assert.equal(finalItem.items_menu.selected_item_preference_bit, preferenceBit,
      `Original Items row ${cursor} must keep the same source preference mapping after A`);
    assert.equal(finalItem.items_menu.selected_item_enabled, 0,
      `Original Items row ${cursor} must be off before the menu commits`);
    itemRows.push({cursor, preference_bit: preferenceBit, enabled: 0, initial_enabled: initialEnabled});
  }
  report.competitiveProfile.item_rows_after_original_inputs = itemRows;

  await moveItemsCursor(0);
  await press(']');
  let frequency = await waitItemsCursor(31, 'source Items Up enters the original frequency selector');
  let frequencyValue = frequency.items_menu.frequency_selector;
  assert(Number.isInteger(frequencyValue) && frequencyValue >= 0 && frequencyValue <= 5,
    `Original Items frequency selector is outside its source range: ${JSON.stringify(frequency.items_menu)}`);
  while (frequencyValue > 0) {
    await press('4');
    frequencyValue--;
    frequency = await waitItemFrequency(frequencyValue,
      `source Items frequency selector moves to None (${frequencyValue})`);
  }
  assert.equal(frequency.items_menu.frequency_selector, 0,
    'Original Items frequency selector must be None before B commits the profile');
  await shot('04-competitive-items-none');
  await press('o');
  let rules = await waitMenu(RULES_MENU_KIND, 5, 'original Items B commits all switches and returns to Rules');
  const masks = deriveAllOffItemMasks({
    initialPreferenceMaskHex,
    rows: itemRows,
  });
  assert.equal(rules.source.items.frequency, -1,
    'Original Items B must commit None as the source -1 frequency');
  assert.equal(rules.source.items.mask_hex, masks.preferenceMaskHex,
    'Original Items B must save the exact preferences cleared by the 31 observed source rows');
  report.competitiveProfile.derived_masks = masks;
  report.sourceObservations.push({label: 'all 31 original item switches committed off', ...rules});

  const stock = await moveMenuCursor(RULES_MENU_KIND, 7, 1);
  assert.equal(stock.source.confirmed_selection, 4,
    'Original VS Rules selection must retain the four-stock setting');
  assert.equal(stock.source.rules.mode, 1, 'Original VS Rules must remain in stock mode');
  assert.equal(stock.source.rules.stock_count, 4, 'Original VS Rules must store four stocks');
  assert.equal(stock.source.rules.handicap, 0, 'Original VS Rules must leave handicap off');
  assert.equal(stock.source.rules.damage_ratio, 10,
    'Original VS Rules must retain the source 1.0 damage-ratio setting');

  await moveMenuCursor(RULES_MENU_KIND, 7, 6);
  await press('m');
  const timerEntry = await waitRulesPlusTimer(stock.source.rules.stock_time_limit,
    'Rules selection 6 opens original Rules Plus');
  assert.equal(timerEntry.source.previous_menu_kind, RULES_MENU_KIND);
  assert.equal(timerEntry.source.rules.stock_time_limit, 0,
    'Fresh Everything context must enter Rules Plus at its source zero-minute timer');
  for (let minute = 1; minute <= 8; minute++) {
    await press('4');
    await waitRulesPlusTimer(minute,
      `original Rules Plus right input sets timer to ${minute} minute${minute === 1 ? '' : 's'}`);
  }
  const friendlyFireEntry = await moveMenuCursor(RULES_PLUS_MENU_KIND, 6, 1);
  let friendlyFireValue = friendlyFireEntry.source.confirmed_selection;
  assert(friendlyFireValue === 0 || friendlyFireValue === 1,
    `Original Rules Plus friendly-fire value is not binary: ${JSON.stringify(friendlyFireEntry.source)}`);
  if (friendlyFireValue !== 1) {
    assert.equal(friendlyFireValue, 0, 'Friendly fire can be set from its source off value');
    await press('4');
    friendlyFireValue = 1;
  }
  await waitRulesPlusValue(1, friendlyFireValue,
    'original Rules Plus row 1 sets friendly fire on', 'friendly_fire');

  const pauseEntry = await moveMenuCursor(RULES_PLUS_MENU_KIND, 6, 2);
  let pauseValue = pauseEntry.source.confirmed_selection;
  assert(pauseValue === 0 || pauseValue === 1,
    `Original Rules Plus pause value is not binary: ${JSON.stringify(pauseEntry.source)}`);
  if (pauseValue !== 0) {
    assert.equal(pauseValue, 1, 'Pause can be disabled from its source enabled value');
    await press('2');
    pauseValue = 0;
  }
  await waitRulesPlusValue(2, pauseValue, 'original Rules Plus row 2 sets pause off');
  assert.equal(pauseValue, 0);
  await shot('05-competitive-rules-plus-profile');
  await press('7');
  await waitMessage('Original character select', 'Rules Plus Start commits the profile through original GM_VS');
  const finalCss = await observeSource();
  assert.equal(finalCss.source?.valid, true);
  assert.equal(finalCss.source.scene, 1);
  const failures = competitiveProfileFailures({
    source: finalCss.source,
    rawCssSelection: finalCss.selection,
    expectedPreferenceMaskHex: masks.preferenceMaskHex,
  });
  assert.deepEqual(failures, [],
    `CSS-owned competitive profile differs from the requested values: ${JSON.stringify({failures, finalCss})}`);
  report.competitiveProfile.final_css_selection = {
    rules: finalCss.source.rules,
    items: finalCss.source.items,
    selection: finalCss.selection,
  };
  report.sourceObservations.push({label: 'raw CSS-owned StartMeleeData provenance after source Rules commit', ...finalCss});
  await shot('06-competitive-profile-css');
  report.checks.push('Original Rules, Items, and Rules Plus PAD inputs set the exact GameRules profile; all 31 mapped item switches are off, pause is disabled, and raw CSS StartMeleeData provenance is retained without treating it as post-SSS normalized match data. This menu-profile checkpoint ends at CSS; subsequent route evidence is recorded separately.');
};
const moveCssCursor = async (label, isInside, directionFor) => {
  for (let step = 0; step < 240; step++) {
    const setup = await observeCssSetup();
    assert(setup?.cursors?.length === 16 && setup?.doors?.length === 40 &&
      setup?.geometry?.length === 48, 'Live original CSS cursor geometry is unavailable');
    const [x, y] = setup.geometry;
    if (isInside(x, y, setup)) return setup;
    const [stickX, stickY] = directionFor(x, y, setup);
    assert(stickX || stickY, `${label} cursor movement made no progress at (${x}, ${y})`);
    await sourcePadSample(0, stickX, stickY, `${label} source cursor step ${step + 1}`);
  }
  const setup = await observeCssSetup();
  throw Error(`${label} cursor did not reach its authored source bounds: ${JSON.stringify({
    cursor: setup?.geometry?.slice(0, 2), door1: setup?.geometry?.slice(12, 24)})}`);
};
const cssDoor = (setup, port) => ({
  p_kind: setup.doors[port * 10],
  slot_type: setup.doors[port * 10 + 4],
  character: setup.doors[port * 10 + 3],
  slot: setup.doors[port * 10 + 6],
  source_port: setup.doors[port * 10 + 6]
    ? setup.doors[port * 10 + 6] - 1 : port,
});
const waitCssDoor = async (port, predicate, label) => {
  const deadline = Date.now() + 15000;
  let setup;
  while (Date.now() < deadline) {
    await resumeTimingPause(label);
    setup = await observeCssSetup();
    if (setup?.doors?.length === 40 && setup?.geometry?.length === 48) {
      const door = cssDoor(setup, port);
      if (predicate(door)) return {setup, door};
    }
    await ensureNoError(label);
    await page.waitForTimeout(40);
  }
  throw Error(`${label}: source CSS door ${port} did not reach the required state: ${JSON.stringify({
    door: setup?.doors?.length === 40 ? cssDoor(setup, port) : null,
    geometry: setup?.geometry?.slice(port * 12, port * 12 + 12)})}`);
};
const configureCompetitiveSecondHuman = async () => {
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('two');
  await page.locator('#player-two-source').selectOption('keyboard');
  await page.waitForFunction(() => {
    const layout = document.querySelector('#keyboard-layout')?.value;
    const source = document.querySelector('#player-two-source')?.value;
    const status = document.querySelector('#player-two-source-status')?.textContent?.trim();
    return layout === 'two' && source === 'keyboard' && status === 'Keyboard';
  }, null, {timeout: 5000});
  const controlsChange = await page.evaluate(() => ({
    layout: document.querySelector('#keyboard-layout')?.value,
    playerOneSource: document.querySelector('#player-one-source')?.value,
    playerTwoSource: document.querySelector('#player-two-source')?.value,
    playerTwoStatus: document.querySelector('#player-two-source-status')?.textContent?.trim(),
  }));
  assert.deepEqual(controlsChange, {
    layout: 'two', playerOneSource: 'keyboard', playerTwoSource: 'keyboard',
    playerTwoStatus: 'Keyboard',
  }, 'Controls must enable the actual two-player keyboard source before CSS joins P2');
  await page.locator('#controls-close').click();
  report.inputConfiguration.playerTwoAfterProfile = 'keyboard';
  report.inputConfiguration.layoutAfterProfile = 'two';
  report.competitiveMatchStart.controls_change = controlsChange;

  let setup = await observeCssSetup();
  assert(setup?.cursors?.length === 16 && setup?.doors?.length === 40 &&
    setup?.geometry?.length === 48, 'Live CSS source roster is unavailable after Controls change');
  const initialRoster = [0, 1, 2, 3].map(port => cssDoor(setup, port));
  assert.deepEqual(initialRoster.map(door => [door.p_kind, door.slot_type]),
    [[0, 0], [1, 1], [3, 3], [3, 3]],
    'Original CSS must expose P1 Human, P2 CPU, and two empty doors before the source join');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.character), [8, 8],
    'The source CSS roster must already contain Mario on both active doors');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.slot), [0, 0],
    'Original CSS StartMeleeData keeps its authored zero-valued raw slot fields');
  assert.deepEqual(initialRoster.slice(0, 2).map(door => door.source_port), [0, 1],
    'When raw slot fields are zero, source player order resolves the P1/P2 ports');
  report.competitiveMatchStart.css_roster.push({label: 'CSS after P2 keyboard selection', doors: initialRoster});
  await shot('07-css-before-p2-source-join');

  const bounds = setup.geometry.slice(12, 24);
  const left = bounds[4], right = bounds[5];
  assert(Number.isFinite(left) && Number.isFinite(right) && right > left,
    `Original P2 CPU/Human toggle bounds are invalid: ${JSON.stringify(bounds)}`);
  await moveCssCursor('original P2 CPU/Human toggle',
    (x, y, current) => {
      const doorBounds = current.geometry.slice(12, 24);
      return x > doorBounds[4] + 0.2 && x < doorBounds[5] - 0.2 && y > -4.4 && y < 0;
    },
    (x, y, current) => {
      const doorBounds = current.geometry.slice(12, 24);
      const centerX = (doorBounds[4] + doorBounds[5]) / 2;
      return [x < centerX - 0.5 ? 80 : x > centerX + 0.5 ? -80 : 0,
        y < -2.2 ? 80 : y > -2.2 ? -80 : 0];
    });
  await sourcePadTap(0x0100, 'original CSS P2 CPU to empty');
  const empty = await waitCssDoor(1,
    door => door.p_kind === 3 && door.slot_type === 3,
    'original CSS P2 CPU-to-empty source transition');
  report.competitiveMatchStart.css_roster.push({label: 'original CSS P2 empty transition', door: empty.door});
  await sourcePadTap(0x0100, 'original CSS P2 empty to Human');
  const human = await waitCssDoor(1,
    door => door.p_kind === 0 && door.slot_type === 0,
    'original CSS P2 empty-to-Human source transition');
  assert.equal(human.door.character, 8,
    'Original CSS P2 Human transition must retain Mario without selecting another character');
  assert.deepEqual([0, 1, 2, 3].map(port => {
    const door = cssDoor(human.setup, port);
    return [door.p_kind, door.slot_type];
  }), [[0, 0], [0, 0], [3, 3], [3, 3]],
  'Original CSS must retain both Human doors and both empty doors after P2 joins');
  assert.deepEqual([0, 1].map(port => cssDoor(human.setup, port).source_port), [0, 1],
    'The two active source doors must retain their resolved controller ports');
  report.competitiveMatchStart.css_roster.push({label: 'original CSS P2 Human transition', door: human.door});
  await shot('08-css-two-human-mario');
  report.checks.push('Controls enables P2 keyboard; original CSS confirms door 1 CPU -> empty -> Human, raw slot fields remain zero, source-order fallback resolves ports 0/1, and doors 2/3 remain empty');
};
const configureCssTeamBattle = async () => {
  // Follow the live original cursor and door bounds through the existing
  // read-only CSS observer. Only single-tick raw PAD samples feed source input.
  // This keeps the route inside the source Teams control and team-color box.
  let setup = await moveCssCursor('CSS Teams control',
    (x, y) => x < -25.5 && y > 22 && y < 24.6,
    (x, y) => [0, y <= 22 ? 80 : -80]);
  const activeKinds = [0, 1, 2, 3].map(door => setup.doors[door * 10]);
  assert.deepEqual(activeKinds, [0, 1, 3, 3],
    'Teams setup must leave the original two-player roster unchanged');
  await shot('09-team-toggle-control');
  await sourcePadTap(0x0100, 'original CSS Teams toggle A');
  await waitCssTeams(1, [0, 0], 'CSS Teams control enabled with both players on team 0');

  setup = await observeCssSetup();
  const teamBounds = setup.geometry.slice(12, 24);
  const left = teamBounds[6], right = teamBounds[7];
  assert(Number.isFinite(left) && Number.isFinite(right) && right > left,
    `Original P2 team-color bounds are invalid: ${JSON.stringify(teamBounds)}`);
  await moveCssCursor('P2 team-color box',
    (x, y) => x > left + 0.25 && x < right - 0.25 && y > -5.55 && y < -1.05,
    (x, y) => [x <= left + 0.25 ? 80 : x >= right - 0.25 ? -80 : 0,
      y <= -5.55 ? 80 : y >= -1.05 ? -80 : 0]);
  await shot('10-second-door-team-color-control');
  await sourcePadTap(0x0100, 'original CSS P2 team-color A');
  await waitCssTeams(1, [0, 1], 'CSS P2 changed to the opposing team');
  setup = await observeCssSetup();
  assert.deepEqual([0, 1, 2, 3].map(door => setup.doors[door * 10]), [0, 1, 3, 3],
    'Source team-color selection must preserve the original two-player roster');
  await shot('11-css-team-battle-configured');
  report.checks.push('Original CSS Teams toggle and P2 color control use source predicates, live CSS geometry, and single-tick source PAD samples');
};
let nativeSessionActive = false;
try {
route: {
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
  const initialProfileCss = competitiveProfileOnly || competitiveMatchStartOnly ? await observeSource() : null;
  if (competitiveProfileOnly || competitiveMatchStartOnly)
    report.sourceObservations.push({label: 'initial CSS profile baseline', ...initialProfileCss});

  if (noContestOnly) {
    await page.waitForTimeout(800);
    await press('7');
    await waitPhase(3, 'original SSS for No Contest reproducer');
    await waitForNoQueuedPad('CSS Start sample drains before source SSS stage driver');
    let selectedStage = 1;
    for (let sample = 0; sample < 600; sample++) {
      selectedStage = await page.evaluate(() => Module._melee_web_native_menu_drive_stage(32));
      if (selectedStage === 2) break;
      if (selectedStage !== 1) throw Error(`Original SSS source stage driver returned ${selectedStage}`);
      await waitForNoQueuedPad(`source SSS direction sample ${sample + 1} drains`);
    }
    assert.equal(selectedStage, 2, 'No Contest reproducer must select Final Destination through source PAD');
    await press('m');
    await waitPhase(7, 'live VS match for No Contest reproducer');
    const matchDeadline = Date.now() + 90000;
    let liveMatch;
    while (Date.now() < matchDeadline) {
      await resumeTimingPause('No Contest reproducer live match');
      liveMatch = await observeMatch();
      if (liveMatch.ready && liveMatch.frame >= 180) break;
      await ensureNoError('No Contest reproducer live match');
      await page.waitForTimeout(100);
    }
    assert(liveMatch?.ready && liveMatch.frame >= 180,
      `No Contest reproducer match did not reach 180 frames: ${JSON.stringify(liveMatch)}`);
    report.matchObservations.push({label: 'live match before source pause and No Contest', ...liveMatch});
    await press('7');
    const pauseDeadline = Date.now() + 15000;
    let pausedMatch;
    while (Date.now() < pauseDeadline) {
      await resumeTimingPause('No Contest reproducer source pause');
      pausedMatch = await observeMatch();
      if (pausedMatch.paused) break;
      await ensureNoError('No Contest reproducer source pause');
      await page.waitForTimeout(50);
    }
    assert.equal(pausedMatch?.paused, true,
      `P1 Start did not open the source pause before LRAS: ${JSON.stringify(pausedMatch)}`);
    await page.waitForTimeout(700);
    await chord(['q', '9', 'm', '7'], {holdMs: 120, releaseMs: 150});
    await waitPhase(8, 'original Results after source pause and LRAS No Contest');
    const terminal = await observeMatch();
    report.matchObservations.push({label: 'terminal No Contest result in original Results', ...terminal});
    await shot('02-original-results');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after source No Contest Results reproducer');
    report.checks.push('P1 Start opens source pause; held original LRAS+Start enters Results; Eject clears source owners');
    break route;
  }

  await chord(['q', '9', '7']);
  await waitMessage('Original main menu', 'CSS -> source Main');
  await waitMenu(MAIN_MENU_KIND, 0, 'source Main root');
  await shot('01-main-root');
  let rules = await enterVsRules('02-first');

  if (competitiveProfileOnly || competitiveMatchStartOnly) {
    await runCompetitiveProfilePreflight(initialProfileCss);
    if (competitiveProfileOnly) {
      await driver.unload();
      nativeSessionActive = false;
      await verifyTeardown('Eject after competitive Rules profile preflight');
      report.checks.push('Source profile preflight stops at CSS; no SSS, match, timeout, Results, or full-route acceptance was exercised');
      break route;
    }

    await configureCompetitiveSecondHuman();
    await press('Enter');
    await waitPhase(3, 'original SSS after P2 joins as a source Human');
    report.checks.push('P1 Start on the two-player keyboard layout enters the original SSS after CSS confirmed P2 Human');
    await shot('09-original-sss');
    await waitForNoQueuedPad('CSS Start sample drains before source SSS stage driver');
    let selectedStage = 1;
    for (let sample = 0; sample < 600; sample++) {
      selectedStage = await page.evaluate(() => Module._melee_web_native_menu_drive_stage(32));
      if (selectedStage === 2) break;
      if (selectedStage !== 1)
        throw Error(`Original SSS source stage driver returned ${selectedStage}`);
      await waitForNoQueuedPad(`competitive source SSS direction sample ${sample + 1} drains`);
    }
    assert.equal(selectedStage, 2,
      'Competitive match-start route must select Final Destination through the original SSS PAD driver');
    await press('j');
    await waitPhase(7, 'two-human Mario Final Destination source match', 60000);
    const liveMatch = await waitForMatchSourceFrames(180, 240);
    const sourcePreferenceMaskHex = report.competitiveProfile.derived_masks.preferenceMaskHex;
    const matchFailures = competitiveMatchStartFailures(liveMatch, {sourcePreferenceMaskHex});
    assert.deepEqual(matchFailures, [],
      `Normalized source match differs from the competitive profile: ${JSON.stringify({matchFailures, liveMatch})}`);
    report.competitiveMatchStart.normalized_match = liveMatch;
    report.matchObservations.push({label: 'normalized source match at its first ready observation from frame 180 through 240', ...liveMatch});
    const lastBeforeEject = await observeMatch();
    assert.equal(lastBeforeEject?.ready, true,
      'The match observer must remain available immediately before Eject');
    assert(Number.isInteger(lastBeforeEject.frame),
      `The pre-Eject source frame must be observable: ${JSON.stringify(lastBeforeEject)}`);
    report.competitiveMatchStart.last_observation_before_eject = {
      ready: lastBeforeEject.ready,
      frame: lastBeforeEject.frame,
    };
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after the bounded normalized match-start prefix');
    report.checks.push(`Original two-human Mario match selected Final Destination; normalized settings were checked once at frame ${liveMatch.frame}, the last pre-Eject observation was frame ${report.competitiveMatchStart.last_observation_before_eject.frame}, and Eject ended the roughly 3-second prefix before timeout or Results`);
    break route;
  }

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
    await moveMenuCursor(RULES_MENU_KIND, 7, 5);
    await press('m');
    await waitMenu(ITEMS_MENU_KIND, 0, 'reduced toggle route opens original Items');
    await waitItemInputReady('reduced toggle route Items input lock released');
    const toggleBefore = await observeSource();
    assert.equal(toggleBefore.source.confirmed_selection, 1,
      'Fresh Everything profile must enable the selected source item before A');
    assert.equal(toggleBefore.items_menu?.valid, true,
      'Original Items observer must expose its live MnItemSwData owner');
    assert.equal(toggleBefore.items_menu.cursor, 0);
    assert.equal(toggleBefore.items_menu.selected_item_enabled, 1);
    await press('m');
    await waitItemConfirmed(0, 'source Items A confirms row zero off');
    const toggleAfterA = await waitLiveItemState(0, 'source Items update applies the row-zero toggle');
    assert.equal(toggleAfterA.source.items.mask_hex, toggleBefore.source.items.mask_hex,
      'A changes MnItemSwData only; the source GameRules mask remains unchanged at this point');
    await press(']'); // Source mnItemSw maps Up at item row 0 to frequency row 31.
    const frequencyEntry = await waitItemsCursor(31, 'source Items Up enters the frequency selector');
    assert.equal(frequencyEntry.items_menu.frequency_selector, 0,
      'Fresh Everything profile starts on the original None frequency option');
    await press('2'); // Source mnItemSw Left advances the frequency selector from None to Very High.
    await waitItemFrequency(1, 'source frequency selector moves from None to Very High');
    await press('4'); // Source mnItemSw Right returns the selector to None.
    const frequencySaved = await waitItemFrequency(0, 'source frequency selector returns to None');
    const frequencySavedMaskDelta = BigInt(`0x${frequencySaved.source.items.mask_hex}`) ^
      BigInt(`0x${toggleBefore.source.items.mask_hex}`);
    assert.notEqual(frequencySavedMaskDelta, 0n,
      'Original frequency selection must save the pending A-selected item row');
    assert.equal(frequencySavedMaskDelta & (frequencySavedMaskDelta - 1n), 0n,
      'Original frequency selection must save exactly the one A-selected item-mask bit');
    assert.equal(frequencySavedMaskDelta, 1n << 5n,
      'Source Items cursor zero maps to saved item preference bit 5');
    assert.equal(frequencySaved.source.items.frequency, -1,
      'Original frequency selection must save None as -1');
    report.sourceObservations.push({label: 'source frequency selection saves pending item row', ...frequencySaved});
    await press('o');
    const toggleCommitted = await waitMenu(RULES_MENU_KIND, 5,
      'reduced toggle route Items B returns to Rules');
    const toggleMaskDelta = BigInt(`0x${toggleCommitted.source.items.mask_hex}`) ^
      BigInt(`0x${toggleBefore.source.items.mask_hex}`);
    assert.equal(toggleMaskDelta, frequencySavedMaskDelta,
      'Items B must return the item mask previously saved by the original frequency control');
    assert.equal(toggleMaskDelta & (toggleMaskDelta - 1n), 0n,
      'Rules must retain exactly the one A-selected item-mask bit');
    assert.equal(toggleCommitted.source.items.frequency, -1,
      'Rules must retain the source None frequency as -1');
    report.sourceObservations.push({label: 'source Items B returns saved item preferences to Rules', ...toggleCommitted});
    report.checks.push('source Items A changes MnItemSwData transiently; original frequency controls round-trip None/Very High/None and save the one-bit item-mask change; B returns that mask and None frequency to Rules');
    await shot('05-toggle-committed-mask');
    await press('o');
    await waitMenu(VS_MENU_KIND, 3, 'Rules B returns to VS selection in reduced toggle route');
    await press('o');
    await waitMenu(MAIN_MENU_KIND, 1, 'VS B returns to Main in reduced toggle route');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after reduced item-toggle route');
    break route;
  }

  await moveMenuCursor(RULES_MENU_KIND, 7, 5);
  await press('m');
  await waitMenu(ITEMS_MENU_KIND, 0, 'second original Items entry');
  await waitItemInputReady('source Items animation lock released');
  await shot('05-items-edit');
  const itemsMenuBefore = await observeSource();
  const itemsBefore = itemsMenuBefore.source.items;
  assert.equal(itemsMenuBefore.source.confirmed_selection, 1,
    'Fresh Everything profile must enable the selected source item before A');
  assert.equal(itemsMenuBefore.items_menu?.valid, true,
    'Original Items observer must expose its live MnItemSwData owner');
  assert.equal(itemsMenuBefore.items_menu.cursor, 0);
  assert.equal(itemsMenuBefore.items_menu.selected_item_enabled, 1);
  await press('m');
  await waitItemConfirmed(0, 'source Items A confirms row zero off');
  const itemsAfterToggle = await waitLiveItemState(0, 'source Items update applies the row-zero toggle');
  assert.equal(itemsAfterToggle.source.items.mask_hex, itemsBefore.mask_hex,
    'A changes MnItemSwData only; the source GameRules mask remains unchanged at this point');
  await waitItemInputReady('source Items accepts navigation after the item toggle');

  await press(']'); // Source mnItemSw maps Up at item row 0 to frequency row 31.
  const frequencyEntry = await waitItemsCursor(31, 'source Items Up enters the frequency selector');
  assert.equal(frequencyEntry.items_menu.frequency_selector, 0,
    'Fresh Everything profile starts on the original None frequency option');
  await press('2'); // Source mnItemSw Left advances the frequency selector from None to Very High.
  await waitItemFrequency(1, 'source frequency selector moves from None to Very High');
  await press('4'); // Source mnItemSw Right returns the selector to None.
  const frequencySaved = await waitItemFrequency(0, 'source frequency selector returns to None');
  const frequencySavedMaskDelta = BigInt(`0x${frequencySaved.source.items.mask_hex}`) ^
    BigInt(`0x${itemsBefore.mask_hex}`);
  assert.notEqual(frequencySavedMaskDelta, 0n,
    'Original frequency selection must save the pending A-selected item row');
  assert.equal(frequencySavedMaskDelta & (frequencySavedMaskDelta - 1n), 0n,
    'Original frequency selection must save exactly the one A-selected item-mask bit');
  assert.equal(frequencySavedMaskDelta, 1n << 5n,
    'Source Items cursor zero maps to saved item preference bit 5');
  assert.equal(frequencySaved.source.items.frequency, -1,
    'Original frequency selection must save None as -1');
  report.sourceObservations.push({label: 'source frequency selection saves pending item row', ...frequencySaved});
  await shot('06-items-frequency-none');
  await press('o');
  rules = await waitMenu(RULES_MENU_KIND, 5, 'Items B returns saved preferences to Rules');
  assert.equal(rules.source.items.frequency, -1, 'Original Items B must return source None frequency (-1) to Rules');
  assert.equal(rules.source.items.mask_hex, frequencySaved.source.items.mask_hex,
    'Original Items B must return the mask saved by the frequency selector to Rules');
  const toggledMaskBits = BigInt(`0x${rules.source.items.mask_hex}`) ^
    BigInt(`0x${itemsBefore.mask_hex}`);
  assert.equal(toggledMaskBits, frequencySavedMaskDelta,
    'Rules must retain the one item-mask bit saved by the original frequency selector');
  assert.equal(toggledMaskBits & (toggledMaskBits - 1n), 0n,
    'Original A on one item row must result in exactly one item-mask bit in Rules');
  report.sourceObservations.push({label: 'source Items B returns saved item preferences to Rules', ...rules});
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
  await moveMenuCursor(RULES_MENU_KIND, 7, 6);
  const rulesPlusChoice = await observeSource();
  assert.equal(rulesPlusChoice.source.hovered_selection, 6);
  await press('m');
  const rulesPlusEntry = await waitRulesPlusTimer(0,
    'Rules selection 6 opens original Rules Plus at its retained timer value');
  assert.equal(rulesPlusEntry.source.previous_menu_kind, RULES_MENU_KIND);
  await shot('08a-rules-plus-timer-entry');
  await press('4');
  const timerEdited = await waitRulesPlusTimer(1,
    'original Rules Plus right input selects a one-minute stock timer');
  report.checks.push('Original Rules Plus D-right changes the source timer from zero to one minute');
  await shot('08b-rules-plus-one-minute');
  await press('o');
  const rulesAfterTimerBack = await waitMenu(RULES_MENU_KIND, 6,
    'Rules Plus B commits the timer and returns to Rules');
  assert.equal(rulesAfterTimerBack.source.rules.stock_time_limit, 1,
    'Rules Plus B must commit the one-minute timer into source GameRules');
  report.sourceObservations.push({label: 'Rules after Rules Plus B', ...rulesAfterTimerBack});
  await press('m');
  const rulesPlusReentry = await waitRulesPlusTimer(1,
    'Rules selection 6 re-enters Rules Plus with its committed timer');
  await shot('08c-rules-plus-reentered-one-minute');
  await press('7');
  await waitMessage('Original character select', 'Rules Plus Start -> checked GM_VS -> CSS');
  const cssSetup = await observeSource();
  assert.equal(cssSetup.source.valid, true);
  assert.equal(cssSetup.source.scene, 1);
  assert.equal(cssSetup.source.rules.stock_count, 3,
    'Main -> GM_VS handoff must retain source-committed three-stock rules through CSS');
  assert.equal(cssSetup.source.rules.stock_time_limit, 1,
    'Main -> GM_VS handoff must retain the source-committed one-minute timer through CSS');
  assert.equal(cssSetup.source.items.frequency, -1);
  assert.equal(cssSetup.source.items.mask_hex, rules.source.items.mask_hex);
  report.sourceObservations.push({label: 'CSS after original VS rules handoff', ...cssSetup});
  await shot('09-css-with-retained-settings');

  // mnCharSel enters with its source-authored 30-frame Start cooldown.
  await page.waitForTimeout(800);
  let teamSssReady = false;
  if (teamBattle) {
    await configureCssTeamBattle();
    await page.waitForTimeout(400);
    await press('7');
    await waitPhase(3, 'original SSS after CSS Team Battle setup');
    await shot('12-team-sss-before-cancellation');
    // mnStageSel_Scene_OnFrame discards buttons while its source-authored
    // 30-frame transition lock counts down.
    await page.waitForTimeout(800);
    await press('o');
    await waitPhase(1, 'original CSS after SSS B cancellation in Team Battle');
    await waitCssTeams(1, [0, 1], 'Team settings retained after SSS cancellation');
    const cssAfterTeamCancel = await observeSource();
    assert.equal(cssAfterTeamCancel.source.rules.stock_count, 3,
      'SSS cancellation must retain the Rules stock setting');
    assert.equal(cssAfterTeamCancel.source.items.frequency, -1);
    report.sourceObservations.push({label: 'CSS after original Team Battle SSS cancellation', ...cssAfterTeamCancel});
    await shot('13-css-after-team-sss-cancellation');
    await page.waitForTimeout(500);
    await press('7');
    await waitPhase(3, 'original SSS re-entry after Team Battle cancellation');
    await shot('14-team-sss-re-entry');
    await page.waitForTimeout(800);
    await press('o');
    await waitPhase(1, 'CSS after repeated original SSS cancellation');
    await waitCssTeams(1, [0, 1], 'Team settings retained after repeated SSS cancellation');
    if (teamSetupOnly) {
      const cssAfterRepeatedCancel = await observeSource();
      assert.equal(cssAfterRepeatedCancel.source.rules.stock_count, 3);
      assert.equal(cssAfterRepeatedCancel.source.items.frequency, -1);
      report.sourceObservations.push({label: 'CSS after repeated SSS cancellation', ...cssAfterRepeatedCancel});
      await shot('15-css-after-repeated-cancellation');
      await driver.unload();
      nativeSessionActive = false;
      await verifyTeardown('Eject after repeated Team Battle menu cancellation');
      report.checks.push('Repeated original SSS entry/cancellation returns to CSS with Rules retained and source owners released');
      break route;
    }
    await page.waitForTimeout(500);
    await press('7');
    await waitPhase(3, 'original SSS third entry after Team Battle cancellation');
    await shot('15-team-sss-match-entry');
    teamSssReady = true;
    report.checks.push('Repeated original SSS B cancellations retain opposing CSS Teams; CSS Start re-enters SSS');
  }
  if (cssSssOnly) {
    await press('7');
    await waitPhase(3, 'original SSS after the source CSS cooldown');
    report.checks.push('CSS Start after the original 30-frame source cooldown enters the original SSS');
    await shot('10-original-sss');
    await press('o');
    await waitPhase(1, 'SSS B cancellation to original CSS');
    const cssAfterSss = await observeSource();
    assert.equal(cssAfterSss.source?.valid, true);
    assert.equal(cssAfterSss.source.scene, 1);
    report.sourceObservations.push({label: 'CSS after original SSS B cancellation', ...cssAfterSss});
    await shot('11-css-after-sss-cancel');
    await chord(['q', '9', '7']);
    await waitMessage('Original main menu', 'CSS L/R/Start route to original Main');
    await waitMenu(MAIN_MENU_KIND, 0, 'original Main after SSS/CSS cancellation');
    await shot('12-main-after-sss-cancel');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after CSS/SSS cooldown reproducer');
    report.checks.push('SSS B returns through original CSS; CSS L/R/Start returns through Main; Eject clears owners');
    break route;
  }
  if (!teamSssReady) {
    await press('7');
    await waitPhase(3, 'original SSS');
    await shot('10-original-sss');
  }
  await waitForNoQueuedPad('CSS Start sample drains before source SSS stage driver');
  let stage = 1;
  for (let sample = 0; sample < 600; sample++) {
    stage = await page.evaluate(() => Module._melee_web_native_menu_drive_stage(32));
    if (stage === 2) break;
    if (stage !== 1) throw Error(`Original SSS source stage driver returned ${stage}`);
    await waitForNoQueuedPad(`source SSS direction sample ${sample + 1} drains`);
  }
  assert.equal(stage, 2, 'Original SSS cursor must reach Final Destination using PAD input');
  report.checks.push('SSS source geometry driver reaches Final Destination; source stage choice is not assigned directly');
  await shot(teamBattle ? '15-team-sss-final-destination' : '11-sss-final-destination');
  if (stageOnly) {
    await press('o');
    await waitPhase(1, 'SSS B cancellation after source stage selection');
    report.sourceObservations.push({label: 'CSS after SSS stage-driver reproducer', ...(await observeSource())});
    await shot('12-css-after-sss-cancel');
    await driver.unload();
    nativeSessionActive = false;
    await verifyTeardown('Eject after SSS stage-driver reproducer');
    report.checks.push('Final Destination highlighted through source SSS PAD controls; B returns to CSS; Eject clears owners');
    break route;
  }
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
  assert.equal(matchBeforeNoContest.rules.is_teams, teamBattle ? 1 : 0,
    'The live source StartMeleeData team flag must match the original CSS selection');
  if (teamBattle) {
    assert.equal(matchBeforeNoContest.rules.player_teams.length, 2);
    assert.ok(matchBeforeNoContest.rules.player_teams.every(team => team >= 0 && team <= 2));
    assert.notEqual(matchBeforeNoContest.rules.player_teams[0],
      matchBeforeNoContest.rules.player_teams[1],
      'Original CSS Start requires the active players to belong to opposing teams');
    report.checks.push(`Live source Team Battle has opposing source teams ${matchBeforeNoContest.rules.player_teams.join(' vs ')}`);
  }
  assert.equal(matchBeforeNoContest.rules.item_frequency, -1);
  assert.equal(matchBeforeNoContest.rules.match_kind, 1);
  assert.equal(matchBeforeNoContest.rules.timer_enabled, 1,
    'Live StartMeleeData must enable the timer selected in original Rules Plus');
  assert.equal(matchBeforeNoContest.rules.time_limit, 60,
    'Live StartMeleeData must convert one Rules Plus minute to 60 seconds');
  assert.equal(matchBeforeNoContest.rules.stage, 0x20,
    'Source-selected SSS Final Destination must reach the match as St_Kind_Last');
  const saveMaskDelta = BigInt('0xffffffffffffffff') ^
    BigInt(`0x${rules.source.items.mask_hex}`);
  const matchMaskDelta = BigInt('0xffffffffffffffff') ^
    BigInt(`0x${matchBeforeNoContest.rules.item_mask_hex}`);
  assert.equal(saveMaskDelta & (saveMaskDelta - 1n), 0n,
    'The committed source item mask must differ by exactly the A-selected row bit');
  assert.notEqual(matchMaskDelta, 0n,
    'The real match must receive the original VS menu item mask');
  assert.equal(matchMaskDelta & (matchMaskDelta - 1n), 0n,
    'The real match item setup must change exactly one authored item bit');
  assert.equal(matchMaskDelta, 1n << 18n,
    'Source item preference bit 5 maps to StartMeleeData item-mask bit 18');
  report.checks.push('live StartMeleeData receives three-stock Rules, the one-minute Rules Plus timer, None frequency, custom item mask, and source-selected Final Destination');
  await shot(teamBattle ? '16-live-two-player-team-match' : '12-live-three-stock-match');

  await press('7');
  const resultsPauseDeadline = Date.now() + 15000;
  let resultsPause;
  while (Date.now() < resultsPauseDeadline) {
    await resumeTimingPause('Results route source pause');
    resultsPause = await observeMatch();
    if (resultsPause.paused) break;
    await ensureNoError('Results route source pause');
    await page.waitForTimeout(50);
  }
  assert.equal(resultsPause?.paused, true,
    `P1 Start did not open the source pause before LRAS: ${JSON.stringify(resultsPause)}`);
  report.matchObservations.push({label: 'original source pause before No Contest', ...resultsPause});
  await page.waitForTimeout(700);
  await chord(['q', '9', 'm', '7'], {holdMs: 120, releaseMs: 150});
  await waitPhase(8, 'original Results');
  await page.waitForTimeout(4500); // Match the established source Results presentation boundary.
  const resultObservation = await observeMatch();
  report.matchObservations.push({label: 'terminal No Contest payload used by Results', ...resultObservation});
  await shot(teamBattle ? '17-original-team-results' : '13-original-results');
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
  if (teamBattle)
    await waitCssTeams(1, [0, 1], 'Team settings retained after Results return');
  const cssAfterResults = await observeSource();
  assert.equal(cssAfterResults.source.rules.stock_count, 3,
    'Results -> CSS must retain the source-committed stock count');
  assert.equal(cssAfterResults.source.rules.stock_time_limit, 1,
    'Results -> CSS must retain the source-committed one-minute timer');
  assert.equal(cssAfterResults.source.items.frequency, -1);
  assert.equal(cssAfterResults.source.items.mask_hex, cssSetup.source.items.mask_hex);
  report.sourceObservations.push({label: 'CSS after original Results return', ...cssAfterResults});
  await shot(teamBattle ? '18-css-after-team-results-settings-retained' : '14-css-after-results-settings-retained');

  await chord(['q', '9', '7']);
  await waitMessage('Original main menu', 'CSS -> original Main after Results');
  await waitMenu(MAIN_MENU_KIND, 0, 'Main root after Results');
  rules = await enterVsRules('15-retained');
  assert.equal(rules.source.rules.stock_count, 3,
    'Results/CSS/Main/VS navigation must retain the committed stock count');
  assert.equal(rules.source.rules.stock_time_limit, 1,
    'Results/CSS/Main/VS navigation must retain the committed one-minute timer');
  assert.equal(rules.source.items.frequency, -1);
  assert.equal(rules.source.items.mask_hex, cssSetup.source.items.mask_hex);
  report.checks.push('Original Rules re-entry after Results retains source-committed stock and item settings');
  await shot('16-rules-retained-after-results');
  await moveMenuCursor(RULES_MENU_KIND, 7, 6);
  await press('m');
  const rulesPlusAfterResults = await waitRulesPlusTimer(1,
    'post-Results Rules re-entry retains the source Rules Plus timer');
  report.sourceObservations.push({label: 'Rules Plus re-entry after Results', ...rulesPlusAfterResults});
  await shot('16a-rules-plus-retained-after-results');
  await press('o');
  const rulesAfterResultsBack = await waitMenu(RULES_MENU_KIND, 6,
    'post-Results Rules Plus B returns to original Rules');
  assert.equal(rulesAfterResultsBack.source.rules.stock_time_limit, 1);
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
  report.result = interruptionSignal ? 'interrupted' : 'fail';
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
  try {
    if (driver) report.diagnostics = await driver.diagnostics();
  } catch (error) {
    report.diagnostics = {status: 'unavailable', error: redactDiscPath(error.message)};
  }
  try {
    await closeOwnedResources();
  } catch (error) {
    report.cleanup_error = redactDiscPath(error.message);
  }
  process.off('SIGINT', onSigint);
  process.off('SIGTERM', onSigterm);
  const cleanupFailures = Object.entries(report.cleanup || {})
    .filter(([, value]) => value.status === 'failed');
  if (cleanupFailures.length) {
    failure = failure || new Error(`Owned browser cleanup failed: ${JSON.stringify(cleanupFailures)}`);
    report.result = interruptionSignal ? 'interrupted' : 'fail';
    report.cleanupFailures = cleanupFailures;
  }
  await persistReport();
}
console.log(JSON.stringify({result: report.result, checks: report.checks,
  sourceObservations: report.sourceObservations.length,
  matchObservations: report.matchObservations.length,
  failure: report.failure?.message || (failure ? redactDiscPath(failure.message) : null),
  report: path.join(output, 'report.json')}));
if (failure || reportWriteError) process.exitCode = 1;
