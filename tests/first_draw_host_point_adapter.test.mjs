import vm from 'node:vm';
import * as firstDrawFunctions from './match_frame_effect_first_draw_driver.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {observeOneNormalFrame,validateServedRuntimeBinding} from './one_normal_frame_observation_adapter.mjs';
import {firstDrawDriver,firstDrawComponentsDriver,firstDrawSelectedTextureIntervalDriver,firstDrawHostOwnersIntervalDriver,firstDrawHostOwnerFailures,observeSelectedTextureIntervalWithHostRead,readFirstDrawColorConversions,observeHostIntervalThenColorConversions,firstDrawHostOwnersColorConversionsIntervalDriver} from './one_first_draw_observation_adapter.mjs';
const oldProfile='runtime-match-frame-effect Release --first-draw-components-diagnostic';
const joined=oldProfile+' --first-pair-audio-file-diagnostic';
const producer=profile=>({schema:'melee-web-normal-frame-served-runtime-v1',source:'a'.repeat(40),source_patch_sha256:'b'.repeat(64),profile,files:Object.fromEntries(['gameplay_menu_browser.js','gameplay_menu_browser.wasm','gameplay_menu_browser.data'].map(n=>[n,{bytes:1,sha256:'c'.repeat(64)}]))});
validateServedRuntimeBinding(producer(joined));validateServedRuntimeBinding(producer(oldProfile));
let dispatches=0;
const opts=(profile,driver)=>({page:{evaluate:()=>{dispatches++;throw Error('transport sentinel');},waitForFunction:()=>{}},normalizedMatch:{ready:true,paused:false,ending:false,complete:false,frame:180},normalizedMatchFailures:[],producerBinding:producer(profile),outputName:'synthetic-profile-check',discSha256:'d'.repeat(64),driver,readRuntimeCapture:()=>null,observationScope:'first_draw'});
for(const [profile,driver] of [[joined,firstDrawSelectedTextureIntervalDriver],[oldProfile,firstDrawHostOwnersIntervalDriver],[joined,{...firstDrawHostOwnersIntervalDriver,hostOwnerPointRead:true}]]){dispatches=0;const v=await observeOneNormalFrame(opts(profile,driver));assert.deepEqual(v.failures,['host_owner_point_profile_driver_mismatch']);assert.equal(dispatches,0);assert.equal(v.arm_attempt,'not_dispatched');}
for(const [profile,driver] of [[joined,firstDrawHostOwnersIntervalDriver],[oldProfile,firstDrawSelectedTextureIntervalDriver]]){dispatches=0;const v=await observeOneNormalFrame(opts(profile,driver));assert.equal(dispatches,1);assert.ok(v.failures.includes('arm_evaluation_response_lost_or_failed'));}
const facts=()=>({generation:'0',unresolvedReads:0,resultCount:0,discSession:true,assetTransfer:false,preparing:false});
const valid=()=>({schema:'melee-web-first-draw-host-owners-v1',status:'observed',before:facts(),after:facts(),native_quiescent:1,same_module:true,returned_idle:true,point_observation_only:true,composite_eligible:false});
assert.deepEqual(firstDrawHostOwnerFailures(valid()),[]);
for(const v of [null,{}, {...valid(),composite_eligible:true},{...valid(),point_observation_only:false},{...valid(),after:{...facts(),generation:'1'}},{...valid(),extra:0}])assert.ok(firstDrawHostOwnerFailures(v).length);
for(const response of [null,{status:'refused',reason:'native_file_owners_not_quiescent'}]){let calls=0;const v=await observeSelectedTextureIntervalWithHostRead({invoke:async()=>{calls++;return response;},components:{}});assert.equal(calls,1);assert.equal(v.status,'refused');assert.equal(v.started,null);assert.deepEqual(v.transport,[]);assert.deepEqual(v.host_transport.response,response);}
let calls=0;const lost=await observeSelectedTextureIntervalWithHostRead({invoke:async()=>{calls++;throw Error('lost');},components:{}});assert.equal(calls,1);assert.equal(lost.host_transport.error,'lost');assert.equal(lost.started,null);assert.deepEqual(lost.transport,[]);
console.log('Host point profile pairing and strict refusal controls PASS; accepted profile only reaches synthetic transport sentinel.');

// Execute the actual CLI call-site selector; no browser/server or route module import.
const routeSource=fs.readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url),'utf8');
const selection=routeSource.match(/driver: (firstDrawSelectedTextureIntervalOnly[^\n]+),\n\s*readRuntimeCapture:/g);assert.equal(selection?.length,1);
const expression=selection[0].slice('driver: '.length,selection[0].lastIndexOf(',\n'));
const route=(profile,interval=true,conversion=false)=>Function('firstDrawColorConversionFactsOnly','firstDrawHostOwnersColorConversionsIntervalDriver','firstDrawSelectedTextureIntervalOnly','firstDrawComponentsOnly','firstDrawObservationOnly','frameEffectProducerBinding','firstDrawHostOwnersIntervalDriver','firstDrawSelectedTextureIntervalDriver','firstDrawComponentsDriver','firstDrawDriver','matchFrameEffectDriver','return ('+expression+');')(conversion,firstDrawHostOwnersColorConversionsIntervalDriver,interval,true,true,{profile},firstDrawHostOwnersIntervalDriver,firstDrawSelectedTextureIntervalDriver,firstDrawComponentsDriver,firstDrawDriver,{});
assert.equal(route(joined,true,true),firstDrawHostOwnersColorConversionsIntervalDriver);assert.equal(route(joined),firstDrawHostOwnersIntervalDriver);assert.equal(route(oldProfile),firstDrawSelectedTextureIntervalDriver);assert.equal(route(joined,false),firstDrawComponentsDriver);
dispatches=0;const disconnected=await observeOneNormalFrame(opts(joined,route(joined,false)));assert.deepEqual(disconnected.failures,['host_owner_point_profile_driver_mismatch']);assert.equal(dispatches,0);
console.log('Actual CLI profile-to-driver selection PASS; joined without explicit interval refuses before dispatch.');

// Actual serialized conversion reader, synthetic native endpoint only.
const companion=()=>({schema:'melee-web-color-conversion-facts-v1',packet_frame_id:'4',internal_gpu_creation_ownership_qualified:false,packet_content_qualified:false,restore_eligible:false,composite_eligible:false,overflow:false,facts:[]});
function conversionReaderFixture(){
 const calls=[];const module={matchFrameEffectNativeIdle:()=>true,matchFrameEffectCallbackReturned:true,
  _melee_web_native_menu_running:()=>0,UTF8ToString:x=>x,
  _melee_web_native_menu_frame_effect_color_conversions:()=>{calls.push('conversion-getter');return JSON.stringify(companion());}};
 const context=vm.createContext({Module:module,__meleeWebFirstDrawComponentModuleV1:module});
 const read=vm.runInContext('('+readFirstDrawColorConversions.toString()+')',context);
 return {calls,module,context,read};
}
{
 const f=conversionReaderFixture(),v=f.read();assert.equal(v.status,'observed');assert.equal(v.raw,JSON.stringify(companion()));assert.equal(v.diagnostic_wasm_serialization,true);assert.equal(v.composite_eligible,false);assert.equal(f.calls.length,1);
}
for(const edit of [f=>{f.module.matchFrameEffectNativeIdle=()=>false;},f=>{f.module.matchFrameEffectCallbackReturned=false;},f=>{f.module._melee_web_native_menu_running=()=>1;},f=>{f.context.__meleeWebFirstDrawComponentModuleV1={};},f=>{delete f.module._melee_web_native_menu_frame_effect_color_conversions;}]){
 const f=conversionReaderFixture();edit(f);assert.equal(f.read().status,'refused');assert.equal(f.calls.length,0);
}
{
 const f=conversionReaderFixture();f.module._melee_web_native_menu_frame_effect_color_conversions=()=>{f.calls.push('conversion-getter');f.context.Module={};return '{}';};assert.equal(f.read().reason,'module_or_reader_changed');assert.equal(f.calls.length,1);
}
// Reuse the existing actual-helper test fixture source; this remains synthetic
// native/host services and never supplies purported captured conversion facts.
const componentFixtureSource=fs.readFileSync(new URL('./first_draw_components.test.mjs',import.meta.url),'utf8');
const bodies=['nativeReceipt','fixture','observe','intervalFixture'].map(name=>{const start=componentFixtureSource.indexOf('function '+name+'(');assert(start>=0);const end=componentFixtureSource.indexOf('\n}',start);assert(end>start);return componentFixtureSource.slice(start,end+2);}).join('\n');
const helperNames=['armNativeFrameEffectFirstDraw','readFirstDrawComponents','armFirstDrawComponents','firstDrawComponentFailures','startFirstDrawSelectedTextureInterval','endFirstDrawSelectedTextureInterval'];
const actualIntervalFixture=Function('vm',...helperNames,bodies+'\nreturn intervalFixture;')(vm,...helperNames.map(k=>firstDrawFunctions[k]));
function joinedFixture(){
 const f=actualIntervalFixture();f.module._melee_web_native_menu_running=()=>0;f.module._melee_web_native_menu_first_pair_file_owners_quiescent=()=>1;
 f.context.window={menuFirstPairHostReadSnapshot:facts,__meleeWebFirstPairFileGuardArmed:false};
 f.module._melee_web_native_menu_frame_effect_color_conversions=()=>{assert.equal(f.calls.at(-1),'selected-close');f.calls.push('conversion-getter');return JSON.stringify(companion());};return f;
}
{
 const f=joinedFixture();const v=await observeHostIntervalThenColorConversions({...f,packet:null});assert.equal(v.transport.length,8);assert.equal(v.started.status,'active');assert.equal(v.ended.status,'closed');assert.equal(f.calls.at(-1),'conversion-getter');assert.equal(v.color_conversion_point.after_closed_interval,true);assert.equal(v.color_conversion_point.diagnostic_wasm_serialization,true);assert.equal(v.color_conversion_point.transport.response.raw,JSON.stringify(companion()));assert.equal(v.status,'refused');assert.ok(v.failures.length);assert.equal(v.color_conversion_point.dependencies.restoreEligible,false);
}
{
 const f=joinedFixture();f.module._melee_web_native_menu_frame_effect_issue_selected_texture=()=>0;const v=await observeHostIntervalThenColorConversions({...f,packet:null});assert.equal(v.status,'refused');assert.equal(f.calls.includes('conversion-getter'),false);
}
{
 const f=joinedFixture();const invoke=async(fn,arg)=>{if(fn.name==='readFirstDrawColorConversions')throw Error('lost conversion response');return f.invoke(fn,arg);};const v=await observeHostIntervalThenColorConversions({...f,invoke,packet:null});assert.equal(v.status,'refused');assert.equal(v.color_conversion_point.transport.error,'lost conversion response');assert.equal(f.calls.includes('conversion-getter'),false);
}
const colorGate=routeSource.split('\n').find(line=>line.startsWith('if(firstDrawColorConversionFactsOnly&&frameEffectProducerBinding?.profile'));assert(colorGate);
const gate=Function('firstDrawColorConversionFactsOnly','frameEffectProducerBinding',colorGate);assert.throws(()=>gate(true,{profile:oldProfile}),/joined private profile/);gate(true,{profile:joined});gate(false,{profile:oldProfile});
console.log('Conversion getter readiness, after-closed order, loss/refusal, exact CLI selection and diagnostic-only scope PASS.');


// Top-level native refusal categories only; lower HPS/SSM/source cause unknown.
function hostReasonFixture(value=0,code=12){
 const calls=[];const module={matchFrameEffectNativeIdle:()=>true,matchFrameEffectCallbackReturned:true,
  _melee_web_native_menu_running:()=>0,
  _melee_web_native_menu_first_pair_file_owners_quiescent:()=>{calls.push('native');return value;},
  _melee_web_native_menu_first_pair_file_owners_reason:()=>{calls.push('reason');return code;}};
 const window={__meleeWebFirstPairFileGuardArmed:false,menuFirstPairHostReadSnapshot:()=>{calls.push('host');return facts();}};
 const context=vm.createContext({Module:module,window,__meleeWebFirstDrawComponentModuleV1:module});
 return {calls,module,context,read:vm.runInContext('('+firstDrawFunctions.readFirstDrawHostOwners.toString()+')',context)};
}
{
 const f=hostReasonFixture(),v=f.read();assert.equal(v.reason,'native_file_owners_not_quiescent');
 assert.equal(v.native_quiescent,0);assert.equal(v.native_refusal.status,'observed');
 assert.equal(v.native_refusal.code,12);assert.equal(v.native_refusal.category,'hps_not_quiescent');
 assert.equal(v.composite_eligible,false);assert.deepEqual(f.calls,['host','native','host','reason']);
}
for(const code of Array.from({length:15},(_,i)=>i+2)){
 const f=hostReasonFixture(0,code),v=f.read();assert.equal(v.native_refusal.status,'observed');assert.equal(v.native_refusal.code,code);assert.equal(typeof v.native_refusal.category,'string');
}
for(const code of [0,1,17,-1,NaN,undefined,{},'12']){
 const f=hostReasonFixture(0,code);
 f.module._melee_web_native_menu_first_pair_file_owners_reason=()=>{f.calls.push('reason');return code;};
 const v=f.read();assert.equal(v.native_refusal.status,'invalid');assert.equal(v.native_refusal.category,null);assert.equal(v.status,'refused');assert.equal(v.composite_eligible,false);
}
{
 const f=hostReasonFixture(1),v=f.read();assert.equal(v.status,'observed');assert.equal(Object.hasOwn(v,'native_refusal'),false);assert.deepEqual(f.calls,['host','native','host']);
}
{
 const f=hostReasonFixture();delete f.module._melee_web_native_menu_first_pair_file_owners_reason;
 const v=f.read();assert.equal(v.reason,'native_file_owners_not_quiescent');assert.equal(v.native_refusal.status,'unavailable');assert.deepEqual(f.calls,['host','native','host']);
}
{
 const f=hostReasonFixture();f.module._melee_web_native_menu_first_pair_file_owners_reason=()=>{f.calls.push('reason');f.context.Module={};return 12;};
 assert.equal(f.read().reason,'module_reader_or_arm_changed');assert.deepEqual(f.calls,['host','native','host','reason']);
}
{
 const f=hostReasonFixture();f.module._melee_web_native_menu_first_pair_file_owners_quiescent=()=>{f.calls.push('native');f.module._melee_web_native_menu_first_pair_file_owners_reason=()=>13;return 0;};
 assert.equal(f.read().reason,'module_reader_or_arm_changed');assert.deepEqual(f.calls,['host','native']);
}
console.log('Actual serialized native top-category refusal, same-call order, missing/invalid/stale getter and READY no-read controls PASS.');

{
 const f=hostReasonFixture();f.module._melee_web_native_menu_first_pair_file_owners_reason=()=>{f.calls.push('reason');throw Error('lost category');};
 const v=f.read();assert.equal(v.reason,'native_file_owners_not_quiescent');assert.equal(v.native_refusal.status,'read_failed');assert.equal(v.native_quiescent,0);assert.deepEqual(f.calls,['host','native','host','reason']);
}
{
 const f=hostReasonFixture(2),v=f.read();assert.equal(v.reason,'native_file_owners_not_quiescent');assert.equal(v.native_refusal.status,'unavailable');assert.deepEqual(f.calls,['host','native','host']);
}
