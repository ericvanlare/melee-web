import assert from 'node:assert/strict';
import {observeOneNormalFrame,validateServedRuntimeBinding} from './one_normal_frame_observation_adapter.mjs';
import {firstDrawSelectedTextureIntervalDriver,firstDrawHostOwnersIntervalDriver,firstDrawHostOwnerFailures,observeSelectedTextureIntervalWithHostRead} from './one_first_draw_observation_adapter.mjs';
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
