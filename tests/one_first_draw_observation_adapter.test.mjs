import vm from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {buildOneFrameRecorderBinding,frameEffectMustSkipOrdinaryNativeCleanup} from './one_normal_frame_observation_adapter.mjs';
import {observeOneFirstDraw,firstDrawDriver as driver,firstDrawComponentsDriver} from './one_first_draw_observation_adapter.mjs';
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
  receipt.schema_version=3;receipt.scope='first_draw_in_normal_callback_observation_only';
  receipt.planned_steps=receipt.callback_steps=receipt.callback_draws=receipt.tick_returns=receipt.draw_returns=2;
  receipt.source_frame_after=receipt.source_frame_before+2;
  receipt.selected_source_frame=receipt.source_frame_before+1;receipt.unobserved_draws=1;
  receipt.selected_depth_owners={registered:0,completed:0,pending:0,ignored_other_issuer:2,finalized:retired};
  receipt.native_retired = retired;
  Object.assign(receipt.renderer, {state: fenced ? 4 : 3, fence_count: fenced ? 1 : 0,
    fence_success: fenced ? 1 : 0, target_frame_index: 31, frame_index: 31});
  for (const row of receipt.events) row.frame_index = 31;
  for (const row of receipt.draws) row.frame_index = 31;
  return receipt;
}

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
    _melee_web_native_menu_frame_effect_arm_first_draw: frame => {
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
  const binding = buildOneFrameRecorderBinding({...args, observationScope: 'first_draw'});
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

test('explicit first draw reuses bounded real driver/recorder, retaining two total draws and one selected renderer frame', async () => {
  const {page, calls} = setupPage({fencePendingOnce: true});
  await installRecorder(page);
  const result = await observeOneFirstDraw(args(page));
  assert.equal(result.status, 'pass');
  assert.equal(result.scope, 'first_draw_in_normal_callback_observation_only');
  assert.equal(result.runtime_callback_identity.scenario, 'one-first-draw-effect-observation-v1');
  assert.equal(result.runtime_callback_capture.identity.scenario, 'one-first-draw-effect-observation-v1');
  assert.equal(result.renderer_status.callback_draws, 2);
  assert.equal(result.renderer_status.unobserved_draws, 1);
  assert.equal(result.later_draws_observed, false);
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
  const result = await observeOneFirstDraw(args(page));
  assert.equal(result.callback_state, 'uncertain');
  assert.equal(result.cleanup_policy, 'browser_owner_only');
  assert(result.failures.includes('arm_evaluation_response_lost_or_failed'));
  assert.equal(calls.filter(call => call === 'native_status').length, 0);
  assert.equal(calls.some(call => Array.isArray(call) && call[0] === 'retire'), false);
  assert(result.runtime_callback_capture,
    'retain the JS-only callback ring without querying native state while the outcome is uncertain');
  assert.equal(result.runtime_callback_capture_failures.length, 0);
});


// Adapter transport controls with declared synthetic endpoints. Actual serialized
// component driver behavior is independently exercised in first_draw_components.test.mjs.
for(const mode of ['positive','multiple-ticks','response-lost'])test('component transport '+mode+' remains before retirement',async()=>{
 const one=value=>{for(const k of ['planned_steps','callback_steps','callback_draws','tick_returns','draw_returns'])value[k]=1;
  value.source_frame_after=value.source_frame_before+1;value.unobserved_draws=0;return value;};
 const {page,calls}=setupPage({statusTransform:mode==='multiple-ticks'?v=>v:one});
 const binding=validProducerBinding();binding.profile='runtime-match-frame-effect Release --first-draw-components-diagnostic';
 await installRecorder(page,{...identityArgs(),producerBinding:binding});
 const synthetic={...driver,
  armNativeFrameEffect:()=>({armed:true,match:{frame:200}}),
  readFrameComponents:()=>{calls.push('component-read');if(mode==='response-lost')throw Error('synthetic response loss');return {component_only:true,depth_before:{sequence:3}};},
  armFrameComponents:()=>{calls.push('component-arm');return {schema:'melee-web-first-draw-components-arm-v1',status:'armed',same_module:true,returned_idle:true,composite_eligible:false,depth_after:{}};},
  componentObservationFailures:(v,options)=>{assert.equal(v.component_only,true);calls.push(options?.stage==='read'?'component-validate-read':'component-validate');return [];}};
 const result=await observeOneFirstDraw(args(page,{driver:synthetic,producerBinding:binding}));
 if(mode==='positive'){
  assert.equal(result.status,'pass');assert.equal(result.native_retired,true);
  assert(calls.indexOf('component-read')>=0);assert(calls.indexOf('component-validate-read')<calls.indexOf('component-arm'));assert(calls.indexOf('component-validate')<calls.indexOf('retire-evaluate'));
 }else{
  assert.equal(result.status,'failed');assert.equal(result.native_retired,false);assert.equal(result.cleanup_policy,'browser_owner_only');
  assert.equal(calls.includes('retire-evaluate'),false);
  if(mode==='multiple-ticks'){assert(result.failures.includes('component_requires_valid_exact_one_tick_draw'));assert.equal(calls.includes('component-read'),false);}
  else assert(result.failures.includes('component_response_lost_or_failed'));
 }
});

test('actual serialized claimed-ready malformed native read refuses before depth-arm and retirement',async()=>{
 const one=value=>{for(const k of ['planned_steps','callback_steps','callback_draws','tick_returns','draw_returns'])value[k]=1;value.source_frame_after=value.source_frame_before+1;value.unobserved_draws=0;return value;};
 const {page,calls}=setupPage({statusTransform:one});
 const binding=validProducerBinding();binding.profile='runtime-match-frame-effect Release --first-draw-components-diagnostic';
 await installRecorder(page,{...identityArgs(),producerBinding:binding});
 const module=globalThis.Module;module.__meleeWebDepthNoWorkV1={status:()=>({phase:'READY',firstFailure:null,bridgeFailure:false})};
 module._melee_web_native_menu_frame_effect_components=()=>{calls.push('actual-component-getter');return JSON.stringify({copied:true,component_ready:true});};
 module._aurora_browser_depth_no_work_arm_component=()=>{calls.push('actual-depth-arm');return 1;};
 const context=vm.createContext({Module:module});const originalEvaluate=page.evaluate.bind(page);
 page.evaluate=async(fn,arg)=>[firstDrawComponentsDriver.armNativeFrameEffect,firstDrawComponentsDriver.readFrameComponents,firstDrawComponentsDriver.armFrameComponents].includes(fn)
  ?vm.runInContext('('+fn.toString()+')',context)(arg):originalEvaluate(fn,arg);
 const result=await observeOneFirstDraw(args(page,{driver:firstDrawComponentsDriver,producerBinding:binding}));
 assert.equal(result.status,'failed');assert(result.component_failures.includes('native_component_receipt'));
 assert.equal(calls.filter(v=>v==='actual-component-getter').length,1);assert.equal(calls.includes('actual-depth-arm'),false);
 assert.equal(calls.includes('retire-evaluate'),false);assert.equal(result.native_retired,false);assert.equal(result.cleanup_policy,'browser_owner_only');
});
