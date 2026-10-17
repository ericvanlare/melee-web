import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {defaultFirstDrawCssFailures,defaultFirstDrawMatchFailures} from './vs_rules_competitive_profile_helpers.mjs';
import {
  buildOneFrameRecorderBinding,
  frameEffectMustSkipOrdinaryNativeCleanup,
  observeOneNormalFrame,
  validateFrameEffectMode,
  validateServedRuntimeBinding,
  verifyServedRuntimeAssets,
} from './one_normal_frame_observation_adapter.mjs';

import * as driver from './match_frame_effect_driver.mjs';
import {schema2Fixture} from './match_frame_effect_schema2_fixture.mjs';
import * as recorder from './runtime_callback_recorder.mjs';

const originalModule = globalThis.Module;
const originalSample = globalThis.menuDiagnosticSample;
const originalIncident = globalThis.menuDiagnosticIncident;
const originalCapture = globalThis.__meleeWebRuntimeIncidentCampaignCapture;

test.afterEach(() => {
  if (originalModule === undefined) delete globalThis.Module;
  else globalThis.Module = originalModule;
  if (originalSample === undefined) delete globalThis.menuDiagnosticSample;
  else globalThis.menuDiagnosticSample = originalSample;
  if (originalIncident === undefined) delete globalThis.menuDiagnosticIncident;
  else globalThis.menuDiagnosticIncident = originalIncident;
  if (originalCapture === undefined) delete globalThis.__meleeWebRuntimeIncidentCampaignCapture;
  else globalThis.__meleeWebRuntimeIncidentCampaignCapture = originalCapture;
});

const producerFiles = () => Object.fromEntries([
  ['gameplay_menu_browser.js', new TextEncoder().encode('fixture javascript')],
  ['gameplay_menu_browser.wasm', new Uint8Array([0, 97, 115, 109])],
  ['gameplay_menu_browser.data', new Uint8Array([1, 2, 3, 4])],
].map(([name, bytes]) => [name, {
  bytes: bytes.byteLength,
  sha256: createHash('sha256').update(bytes).digest('hex'),
}]));

const validProducerBinding = () => ({
  schema: 'melee-web-normal-frame-served-runtime-v1',
  source: 'a'.repeat(40),
  source_patch_sha256: 'b'.repeat(64),
  profile: 'runtime-match-frame-effect Release',
  files: {
    ...producerFiles(),
    'other-served-file.mjs': {bytes: 5, sha256: 'c'.repeat(64)},
  },
});

const identityArgs = () => ({
  outputName: 'adapter-control',
  discSha256: 'a'.repeat(64),
  producerBinding: validProducerBinding(),
});
const validMatch = () => ({ready: true, paused: false, ending: false, complete: false, frame: 200});

function rendererEvent(kind, flags = 0) {
  return {kind, frame_index: 31, op_index: 0, item_index: 0, flags,
    value0: 0, value1: 0, value2: 0, value3: 0, handle_a: '0', handle_b: '0'};
}

function validNativeStatus({fenced = true, retired = false} = {}) {
  const receipt = schema2Fixture();
  receipt.native_retired = retired;
  Object.assign(receipt.renderer, {state: fenced ? 4 : 3, fence_count: fenced ? 1 : 0,
    fence_success: fenced ? 1 : 0, target_frame_index: 31, frame_index: 31});
  for (const row of receipt.events) row.frame_index = 31;
  for (const row of receipt.draws) row.frame_index = 31;
  return receipt;
}

test('legacy match-start prefix stays default and observer mode has an explicit paired opt-in', () => {
  assert.equal(validateFrameEffectMode(), false);
  const defaults={defaultFirstDrawOnly:true,firstDrawObservationOnly:true,producerBindingPath:'/frozen/default.json'};
  assert.equal(validateFrameEffectMode(defaults),true);
  for(const delta of [{competitiveMatchStartOnly:true},{firstDrawObservationOnly:false},
    {frameEffectObservationOnly:true},{otherRouteSelected:true},{producerBindingPath:null}])
    assert.throws(()=>validateFrameEffectMode({...defaults,...delta}),/default first-draw/);

  assert.equal(buildOneFrameRecorderBinding(identityArgs()).identity.scenario,
    'one-normal-frame-effect-observation-v1');
  assert.equal(buildOneFrameRecorderBinding({...identityArgs(), observationScope: 'first_draw'}).identity.scenario,
    'one-first-draw-effect-observation-v1');
  assert.throws(() => buildOneFrameRecorderBinding({...identityArgs(), observationScope: 'arbitrary'}),
    /Unsupported.*scope/);
  assert.equal(validateFrameEffectMode({competitiveMatchStartOnly: true}), false);
  assert.throws(() => validateFrameEffectMode({frameEffectObservationOnly: true,
    producerBindingPath: '/frozen/packet.json'}), /requires --competitive-match-start-only/);
  assert.throws(() => validateFrameEffectMode({competitiveMatchStartOnly: true,
    producerBindingPath: '/frozen/packet.json'}), /required only with/);
  assert.equal(validateFrameEffectMode({competitiveMatchStartOnly: true,
    frameEffectObservationOnly: true, producerBindingPath: '/frozen/packet.json'}), true);
  assert.equal(validateFrameEffectMode({competitiveMatchStartOnly: true,
    firstDrawObservationOnly: true, producerBindingPath: '/frozen/packet.json'}), true);
  assert.throws(() => validateFrameEffectMode({competitiveMatchStartOnly: true,
    frameEffectObservationOnly: true, firstDrawObservationOnly: true,
    producerBindingPath: '/frozen/packet.json'}), /mutually exclusive/);
  assert.throws(() => validateFrameEffectMode({firstDrawObservationOnly: true,
    producerBindingPath: '/frozen/packet.json'}), /requires --competitive-match-start-only/);
  assert.throws(() => validateFrameEffectMode({competitiveMatchStartOnly: true,
    firstDrawObservationOnly: true}), /required only with/);
  assert.throws(() => validateFrameEffectMode({competitiveMatchStartOnly: true,
    firstDrawObservationOnly: true, otherRouteSelected: true,
    producerBindingPath: '/frozen/packet.json'}), /exclusive competitive match-start/);
});

// Scoped identity is selected before installing the runtime recorder; arbitrary
// identity/scope injection remains unsupported.
test('served producer packet requires exact schema, private profile, and typed executable identities', () => {
  const valid = validProducerBinding();
  assert.equal(validateServedRuntimeBinding(valid).source, 'a'.repeat(40));
  for (const mutate of [
    packet => { packet.schema = 'unknown'; },
    packet => { packet.source = 'not-a-commit'; },
    packet => { packet.source_patch_sha256 = 'A'.repeat(64); },
    packet => { packet.profile = 'Release'; },
    packet => { packet.files['gameplay_menu_browser.wasm'].bytes = 0; },
    packet => { packet.files['gameplay_menu_browser.data'].sha256 = 'bad'; },
    packet => { delete packet.files['gameplay_menu_browser.js']; },
  ]) {
    const invalid = validProducerBinding();
    mutate(invalid);
    assert.throws(() => validateServedRuntimeBinding(invalid));
  }
});

test('served JS, Wasm, and data are hashed against the producer packet before launch', async () => {
  const binding = validProducerBinding();
  const payloads = new Map([
    ['gameplay_menu_browser.js', new TextEncoder().encode('fixture javascript')],
    ['gameplay_menu_browser.wasm', new Uint8Array([0, 97, 115, 109])],
    ['gameplay_menu_browser.data', new Uint8Array([1, 2, 3, 4])],
  ]);
  const requests = [];
  const fetchImpl = async url => {
    const name = new URL(url).pathname.split('/').at(-1);
    requests.push(name);
    const bytes = payloads.get(name);
    return {status: 200, arrayBuffer: async () => bytes.slice().buffer};
  };
  const receipt = await verifyServedRuntimeAssets({binding, baseUrl: 'http://127.0.0.1:4173/', fetchImpl});
  assert.equal(receipt.status, 'passed');
  assert.deepEqual(requests, ['gameplay_menu_browser.js', 'gameplay_menu_browser.wasm', 'gameplay_menu_browser.data']);
  assert.equal(Object.keys(receipt.files).length, 3);
  await assert.rejects(verifyServedRuntimeAssets({binding, baseUrl: 'http://127.0.0.1:4173/',
    fetchImpl: async url => {
      const name = new URL(url).pathname.split('/').at(-1);
      const bytes = name === 'gameplay_menu_browser.wasm' ? new Uint8Array([9]) : payloads.get(name);
      return {status: 200, arrayBuffer: async () => bytes.slice().buffer};
    }}), /differs from its frozen producer binding/);
});

function setupPage({fencePendingOnce = false, callbackReturned = true,
  armResponseLost = false, retirementResponseLost = false, armResult = 1,
  waitFails = false, fenceNever = false, nativeIdle = true,
  statusTransform = value => value} = {}) {
  const calls = [];
  let statusReads = 0;
  globalThis.Module = {
    matchFrameEffectNativeIdle: () => nativeIdle,
    matchFrameEffectCallbackReturned: callbackReturned,
    UTF8ToString: value => value,
    _melee_web_native_menu_match_observe: () => {
      calls.push('match'); return JSON.stringify({ready: true, frame: 200});
    },
    _melee_web_native_menu_frame_effect_arm: frame => {
      calls.push(['arm', frame]); return armResult;
    },
    _melee_web_native_menu_frame_effect_status: () => {
      calls.push('native_status'); statusReads++;
      const fenced = !fenceNever && (!fencePendingOnce || statusReads > 1);
      return JSON.stringify(statusTransform(validNativeStatus({fenced, retired: globalThis.__retired === true})));
    },
    ccall: async (name, resultType, argTypes, args, options) => {
      calls.push(['retire', name, options?.async]);
      globalThis.__retired = true;
      return 1;
    },
  };
  delete globalThis.__retired;
  const page = {
    async evaluate(fn, arg) {
      if (fn === driver.armNativeFrameEffect) {
        calls.push('arm-evaluate');
        const result = await fn(arg);
        if (armResponseLost) throw Error('synthetic Playwright response loss after arm dispatch');
        return result;
      }
      if (fn === driver.readNativeFrameEffect) calls.push('read-effect');
      if (fn === driver.retireNativeFrameEffect) {
        calls.push('retire-evaluate');
        const result = await fn();
        if (retirementResponseLost) throw Error('synthetic Playwright response loss after retirement');
        return result;
      }
      return await fn(arg);
    },
    async waitForFunction(predicate, arg, options) {
      calls.push(['wait-callback', options?.timeout]);
      if (waitFails || predicate() !== true) throw Error('synthetic callback return timeout');
      return {jsonValue: async () => true, dispose: async () => {}};
    },
  };
  return {page, calls};
}

async function installRecorder(page, args = identityArgs()) {
  const binding = buildOneFrameRecorderBinding(args);
  await recorder.installRuntimeDiagnosticsCapture(binding.identity, binding.identityScope, page);
  // The actual route recorder samples every callback before thinning. This
  // record witnesses a gameplay-phase source step without inventing H1 rows.
  globalThis.menuDiagnosticSample(1, 200, 7, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, true);
  return binding;
}

function args(page, extras = {}) {
  return {
    page, normalizedMatch: validMatch(), normalizedMatchFailures: [],
    ...identityArgs(), driver, readRuntimeCapture: recorder.readRuntimeDiagnosticsCapture,
    callbackTimeoutMs: 50, fenceTimeoutMs: 100, fencePollMs: 1, ...extras,
  };
}

test('actual one-frame driver and callback recorder pass one normal fenced callback and checked retirement', async () => {
  const {page, calls} = setupPage({fencePendingOnce: true});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.status, 'pass');
  assert.equal(result.native_retired, true);
  assert.equal(result.renderer_status.renderer.first_blocker, 7,
    'the first blocker is retained even when the normal frame/fence completed');
  assert.equal(result.renderer_status.renderer.fence_count, 1);
  assert.deepEqual(result.runtime_callback_capture_failures, []);
  assert(calls.filter(call => call === 'native_status').length >= 3,
    'the adapter polls the real status helper until the normal fence is visible');
  assert(frameEffectMustSkipOrdinaryNativeCleanup(result),
    'the already-retired native owner must not be unloaded again');
});

test('lost arm response marks state uncertain and forbids all native reads/retirement', async () => {
  const {page, calls} = setupPage({armResponseLost: true, callbackReturned: false});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.callback_state, 'uncertain');
  assert.equal(result.cleanup_policy, 'browser_owner_only');
  assert(result.failures.includes('arm_evaluation_response_lost_or_failed'));
  assert.equal(calls.filter(call => call === 'native_status').length, 0);
  assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
  assert(result.runtime_callback_capture,
    'retain the JS-only callback ring without querying native state while the outcome is uncertain');
  assert.equal(result.runtime_callback_capture_failures.length, 0);
});

test('callback-return timeout never falls through to status getter or ordinary cleanup', async () => {
  const {page, calls} = setupPage({callbackReturned: false, waitFails: true});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.cleanup_policy, 'browser_owner_only');
  assert.equal(result.callback_state, 'uncertain');
  assert.equal(calls.filter(call => call === 'native_status').length, 0);
  assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
  assert(result.runtime_callback_capture,
    'retain already-published H1 rows after the callback-return deadline');
});


test('normal-fence timeout preserves the last pending status and forbids retirement/fallback', async () => {
  const {page, calls} = setupPage({fenceNever: true});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page, {fenceTimeoutMs: 20, fencePollMs: 1}));
  assert.equal(result.status, 'failed');
  assert.equal(result.callback_state, 'fence_pending');
  assert.equal(result.cleanup_policy, 'browser_owner_only');
  assert.equal(result.renderer_status.renderer.fence_count, 0);
  assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
});

test('strict receipt validator rejects malformed frame rows and duplicate submit while retaining evidence', async () => {
  const {page: badFramePage} = setupPage({statusTransform: status => {
    status.events[1].frame_index += 1;
    return status;
  }});
  await installRecorder(badFramePage);
  const badFrame = await observeOneNormalFrame(args(badFramePage));
  assert.equal(badFrame.status, 'failed');
  assert(badFrame.observation_failures.includes('event_rows'));
  assert.equal(badFrame.renderer_status.events[1].frame_index, 32,
    'the original malformed event row remains in the observation');

  const {page: duplicateSubmitPage} = setupPage({statusTransform: status => {
    status.events.splice(1, 0, rendererEvent(17));
    status.renderer.event_count = status.events.length;
    return status;
  }});
  await installRecorder(duplicateSubmitPage);
  const duplicateSubmit = await observeOneNormalFrame(args(duplicateSubmitPage));
  assert.equal(duplicateSubmit.status, 'failed');
  assert(duplicateSubmit.observation_failures.includes('submit_fence_events'));
});

test('armed source frame is bound to the receipt and pending native continuation requires browser-owner-only cleanup', async () => {
  const {page: mismatchedPage} = setupPage({statusTransform: status => {
    status.requested_source_frame = 201;
    status.source_frame_before = 201;
    status.source_frame_after = 202;
    return status;
  }});
  await installRecorder(mismatchedPage);
  const mismatch = await observeOneNormalFrame(args(mismatchedPage));
  assert.equal(mismatch.status, 'failed');
  assert(mismatch.observation_failures.includes('armed_source_frame_mismatch'));
  assert.equal(mismatch.requested_source_frame, 200);

  const {page: pendingPage, calls} = setupPage({nativeIdle: false});
  await installRecorder(pendingPage);
  const pending = await observeOneNormalFrame(args(pendingPage));
  assert.equal(pending.arm_attempt, 'not_dispatched');
  assert.equal(pending.cleanup_policy, 'browser_owner_only');
  assert(frameEffectMustSkipOrdinaryNativeCleanup(pending));
  assert.equal(calls.filter(call => call === 'native_status').length, 0);
  assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
  assert(pending.runtime_callback_capture,
    'the safe JS-only callback ring is retained even when the native continuation is pending');
});

test('a retained H1 runtime incident fails the observation but the confirmed fenced owner still retires', async () => {
  const {page, calls} = setupPage();
  await installRecorder(page);
  globalThis.menuDiagnosticIncident(1, 3, 2, 200, 7, 1);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.status, 'failed');
  assert(result.runtime_callback_capture_failures.includes('runtime_non_preparation_incident'));
  assert.equal(result.runtime_callback_capture.incidents.length, 1);
  assert.equal(result.native_retired, true);
  assert(calls.some(call => Array.isArray(call) && call[0] === 'retire'));
});

test('failed competitive normalization is rejected before any page/native operation', async () => {
  const {page, calls} = setupPage();
  const result = await observeOneNormalFrame(args(page, {normalizedMatchFailures: ['stage mismatch']}));
  assert.equal(result.status, 'refused');
  assert(result.failures.includes('competitive_normalization_gate_failed'));
  assert.deepEqual(calls, []);
});

test('lost checked-retirement response remains browser-owner-only with no fallback query', async () => {
  const {page, calls} = setupPage({retirementResponseLost: true, fencePendingOnce: true});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.status, 'failed');
  assert.equal(result.cleanup_policy, 'browser_owner_only');
  assert.equal(result.callback_state, 'uncertain');
  assert(result.failures.includes('retirement_response_lost_or_failed'));
  assert.equal(calls.filter(call => call === 'native_status').length, 3,
    'two fence reads and the retirement helper final status are the only status reads');
});

test('known pre-arm refusal permits the existing ordinary failure cleanup path', async () => {
  const {page} = setupPage({armResult: 0});
  await installRecorder(page);
  const result = await observeOneNormalFrame(args(page));
  assert.equal(result.arm_attempt, 'returned_refusal');
  assert.equal(result.cleanup_policy, 'ordinary_owner_cleanup_allowed');
  assert.equal(frameEffectMustSkipOrdinaryNativeCleanup(result), false);
});

test('missing producer identity is refused before native interaction', async () => {
  const {page, calls} = setupPage();
  const result = await observeOneNormalFrame(args(page, {producerBinding: null}));
  assert.equal(result.status, 'refused');
  assert(result.failures.includes('frozen_producer_binding_missing'));
  assert.deepEqual(calls, []);
});

for (const [phase, helper, expectedFailure] of [
  ['arm', driver.armNativeFrameEffect, 'arm_evaluation_response_lost_or_failed'],
  ['fence', driver.readNativeFrameEffect, 'native_status_response_lost_or_failed'],
  ['retirement', driver.retireNativeFrameEffect, 'retirement_response_lost_or_failed'],
]) {
  test(`never-settling ${phase} transport respects its bound and never reenters native code`, async () => {
    const {page, calls} = setupPage();
    await installRecorder(page);
    const evaluate = page.evaluate.bind(page);
    let lostResponses = 0;
    page.evaluate = (fn, arg) => {
      if (fn === helper) {
        lostResponses++;
        return new Promise(() => {});
      }
      return evaluate(fn, arg);
    };
    const start = Date.now();
    const result = await observeOneNormalFrame(args(page, {
      armTimeoutMs: 20, callbackTimeoutMs: 20, fenceTimeoutMs: 20, retirementTimeoutMs: 20,
    }));
    assert(Date.now() - start < 1000, 'bounded transport must not wait for the overall supervisor');
    assert.equal(lostResponses, 1);
    assert.equal(result.status, 'failed');
    assert.equal(result.callback_state, 'uncertain');
    assert.equal(result.cleanup_policy, 'browser_owner_only');
    assert(result.failures.includes(expectedFailure));
    assert(result.runtime_callback_capture, 'retain JS-only H1 ring before owned browser closure');
    assert.equal(calls.filter(call => call === 'native_status').length, phase === 'retirement' ? 1 : 0);
    assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
  });
}

const defaultCss = () => ({source:{valid:true,scene:1,
  rules:{mode:1,stock_count:4,time_limit:2,stock_time_limit:0,handicap:0,damage_ratio:10,friendly_fire:0,pause:1},
  items:{frequency:-1,mask_hex:'ffffffffffffffff'},css_setup:{valid:true,is_teams:0,player_teams:[0,0]}},
  selection:{valid:true,scene:1,provenance:'raw_css_vs_start',match_kind:0,timer_enabled:0,time_limit_seconds:0,
    friendly_fire:0,disable_pausing:0,damage_ratio_bits:'3f800000',item_frequency:2,
    item_mask_hex:'ffffffffffffffff',player_stocks:[0,0]}});
const defaultMatch = () => ({leg:'vs',ready:true,paused:false,ending:false,complete:false,outcome:0,
  frame:180,rng:123,observed_player_source_slots:[0,1],rules:{match_kind:1,stage:32,timer_enabled:0,
    time_limit:0,disable_pausing:0,friendly_fire:0,damage_ratio_bits:'3f800000',item_frequency:-1,
    item_mask_hex:'ffffffffffffffff',is_stock:1,is_vs:1,source_sudden_death_flag:0,is_teams:0,
    player_teams:[0,0],player_stocks:[4,4],player_source_slots:[0,0],resolved_controller_ports:[0,1],
    player_slot_types:[0,0],player_source_stocks:[4,4],player_source_characters:[8,8],
    player_attack_ratio_bits:['3f800000','3f800000'],player_defense_ratio_bits:['3f800000','3f800000']},
  players:[0,1].map(i=>({fighter:0,source_player_index:i,source_slot:0,source_port:i,source_character:8,
    slot_type:0,human:true,stocks:4,source_stocks:4,source_initial_damage:0,damage_percent:0}))});
test('separate default oracle strictly joins actual raw CSS and normalized source fields',()=>{
  const css=defaultCss(),match=defaultMatch();
  const original=JSON.stringify({css,match});
  assert.deepEqual(defaultFirstDrawCssFailures(css),[]);
  assert.deepEqual(defaultFirstDrawMatchFailures(match,css),[]);
  assert.equal(JSON.stringify({css,match}),original,'Oracle must not rewrite observations');
  for(const [field,value] of [['stock_time_limit',8],['friendly_fire',1],['pause',0],['stock_count',3]]){
    const wrong=structuredClone(css);wrong.source.rules[field]=value;
    assert(defaultFirstDrawMatchFailures(match,wrong).length,field);
  }
  for(const [field,value] of [['time_limit_seconds',120],['item_frequency',-1],['provenance','normalized']]){
    const wrong=structuredClone(css);wrong.selection[field]=value;
    assert(defaultFirstDrawMatchFailures(match,wrong).length,field);
  }
  for(const [field,value] of [['timer_enabled',1],['time_limit',480],['friendly_fire',1],['disable_pausing',1],
    ['item_frequency',2],['item_mask_hex','fffffff80000000f'],['stage',3],['damage_ratio_bits','3f7fffff'],
    ['player_slot_types',[0,1]],['resolved_controller_ports',[0,2]],['player_attack_ratio_bits',['3f800000','3f7fffff']]]){
    const wrong=structuredClone(match);wrong.rules[field]=value;
    assert(defaultFirstDrawMatchFailures(wrong,css).length,field);
  }
  for(const [field,value] of [['rng',undefined],['frame',241],['ready',false],['paused',true],['outcome',1]]){
    const wrong=structuredClone(match);wrong[field]=value;
    assert(defaultFirstDrawMatchFailures(wrong,css).length,field);
  }
  for(const [field,value] of [['source_port',2],['human',false],['fighter',1],['source_initial_damage',1]]){
    const wrong=structuredClone(match);wrong.players[1][field]=value;
    assert(defaultFirstDrawMatchFailures(wrong,css).length,field);
  }
  assert(defaultFirstDrawMatchFailures({...match,players:[]},css).length);
  assert(defaultFirstDrawMatchFailures(match,null).length);
});

test('actual CLI and route bodies isolate the default context and preserve source PAD recording',async()=>{
  const source=readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url),'utf8');
  const prefix=source.slice(source.indexOf('const options ='),source.indexOf('let frameEffectProducerBinding = null;'));
  const args=['--url','http://localhost/runtime.html?__depth_no_work_v1=1',
    '--disc','owned.ciso','--out','fresh','--default-first-draw-only','--first-draw-observation-only',
    '--first-draw-components-only','--first-draw-selected-texture-interval-only',
    '--frame-effect-producer-binding','frozen.json'];
  const cli=argv=>vm.runInNewContext(prefix,{assert,URL,path,validateFrameEffectMode,
    parseArgs:options=>parseArgs({...options,args:argv})});
  cli(args);
  for(const foreign of ['menu-only','rules-items-only','css-sss-only','stage-only','no-contest-only',
    'team-battle','team-setup-only','sparse-mario-route','sparse-mario-css-only','sparse-mario-prefix',
    'competitive-profile-only','competitive-match-start-only','competitive-timeout-route','sudden-death-route'])
    assert.throws(()=>cli([...args,'--'+foreign]),/exclusive|requires|incompatible|frozen/);
  assert.throws(()=>cli(args.filter(x=>x!=='--first-draw-components-only')),/requires/);
  assert.throws(()=>cli(args.filter(x=>x!=='--first-draw-observation-only')),/require/);
  const bypass=source.slice(source.indexOf('  let rules;'),source.indexOf('  if (competitiveProfileOnly || matchStartRoute) {'));
  const calls=[];
  const runBypass=new Function('defaultFirstDrawOnly','chord','waitMessage','waitMenu','shot','enterVsRules',
    'MAIN_MENU_KIND','return (async()=>{'+bypass+'})()');
  const record=async name=>calls.push(name);
  await runBypass(true,()=>{throw Error('Main chord forbidden');},record,record,record,record,0);
  assert.deepEqual(calls,[]);
  await runBypass(false,()=>record('chord'),()=>record('message'),()=>record('menu'),()=>record('shot'),()=>record('rules'),0);
  assert.deepEqual(calls,['chord','message','menu','shot','rules']);
  const body=source.slice(source.indexOf('const sourcePadSample ='),source.indexOf('const sourcePadTap ='));
  const report={sourcePadSamples:[]},delivered=[],drains=[];
  const make=new Function('page','assert','report','ensureNoError','waitForNoQueuedPad',body+'return sourcePadSample;');
  const sample=make({evaluate:async(fn,arg)=>{delivered.push(arg);return 1;}},assert,report,async()=>{},async(...arg)=>drains.push(arg));
  await sample(0x100,80,-80,'actual recorded sample',1,2);
  assert.deepEqual(delivered,[[1,0x100,80,-80,2]]);
  assert.deepEqual(report.sourcePadSamples,[{port:1,buttons:0x100,stickX:80,stickY:-80,duration:2,label:'actual recorded sample'}]);
  assert.equal(drains.length,1);
  const rejected=make({evaluate:async()=>0},assert,report,async()=>{},async()=>{throw Error('Rejected PAD must not drain');});
  await assert.rejects(rejected(0,0,0,'rejected'),/Source PAD rejected/);
  assert.equal(report.sourcePadSamples.length,1);
});
