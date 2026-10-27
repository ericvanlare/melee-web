import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import test from 'node:test';
import {randomUUID} from 'node:crypto';
import * as h from './stadium_matched_setup_cf_consumer.mjs';
import * as gate from './stadium_matched_setup_cf_runner_gate.mjs';
import {createStadiumRecreationCanaryContext} from './stadium_recreation_canary_context.mjs';
import {isStadiumC3ReportCheckpointEligible} from './stadium_c3_report_checkpoint.mjs';
// Reuse the reviewed stock4 fixture's actual owner extraction. Only idle,
// snapshot and UUID leaves are modeled here; no native transfer is executed.
function installActualLaterBTransferOwner(vm, source, context) {
 const first='  let saveProfileSnapshotsActive = 0;',last='  // END LATER_B_REPLAY_TRANSFER_OWNER';
 assert.equal(source.split(first).length,2);assert.equal(source.split(last).length,2);
 const begin=source.indexOf(first),end=source.indexOf(last);assert.ok(end>begin);
 context.networkSessionClose=null;context.crypto={randomUUID};context.snapshot=()=>({});
 vm.runInContext(source.slice(begin,end)+'\nconst laterBReplayTransfer = createLaterBReplayTransferOwner();',context);
 assert.equal(vm.runInContext('laterBReplayTransfer.suspended()',context),false);
}
const expected={action_index:168,boundary_role:'C',draw_ordinal:84,raw_identity:{sequence:3007,source_tick:84,draw_ordinal:84},seed_ordinal:0,seed_hex:'bc2cb82f',source_tick:84,values:{map2_countdown:3665,map2_phase:0,map2_form:5,map2_history_e0:-1,map2_history_e2:-1,map2_gate:false,display_mode:11,display_e6:64,display_e8:64,display_previous_mode:10,display_selected_player:99,display_f2:0,display_flags:0}};
const syntheticNative=(row,typed22)=>({schema:`melee-web-stadium-per-tick-common-fields-native-v${typed22?3:2}`,schedule_index:row.action_index,source_tick:row.source_tick,world_ticks:row.source_tick+1,source_frame:0,world_generation:3,...row.values,seed_hex:row.seed_hex});
const typedExpected=structuredClone(expected);for(const key of h.TYPED22_RELATION_KEYS)typedExpected.values[key]=key!=='map2_gate_mask_40';
const compare=(row,actual,typed22)=>h.compareOriginalCommonTickFields({expected:row,actual,actionIndex:row.action_index,sourceFramesAfter:actual.source_frame,admittedWorldGeneration:3,typed22});
test('synthetic declared22 positive and every relation corruption refusal; legacy15 unchanged',()=>{
 const original=compare(expected,syntheticNative(expected,false),false);assert.equal(original.decision,'PASS_EXACT_15_COMMON_FIELDS_AND_SEED');assert.equal(original.compared_field_count,15);
 const actual=syntheticNative(typedExpected,true),result=compare(typedExpected,actual,true);assert.equal(result.decision,'PASS_EXACT_22_COMMON_FIELDS_AND_SEED');assert.equal(result.compared_field_count,22);assert.equal(result.exact_value_count,23);
 for(const key of h.TYPED22_RELATION_KEYS){const bad=structuredClone(actual);bad[key]=!bad[key];assert.equal(compare(typedExpected,bad,true).first_mismatch.path,key);bad[key]=1;assert.match(compare(typedExpected,bad,true).decision,/REFUSE/);}
 assert.match(compare(expected,actual,false).decision,/REFUSE/);assert.match(compare(typedExpected,syntheticNative(expected,false),true).decision,/REFUSE/);
 assert.deepEqual(compare(expected,syntheticNative(expected,false),false),original);
});
test('new refusal summaries and conservative largest declared sample preserve1508/512 limits',()=>{
 const max=syntheticNative(typedExpected,true);for(const key of ['map2_countdown'])max[key]=-2147483648;for(const key of ['map2_phase','map2_form','map2_history_e0','map2_history_e2','display_mode','display_e6','display_e8','display_previous_mode','display_selected_player','display_f2'])max[key]=-32768;
 max.schedule_index=4294967295;max.source_tick=4294967295;max.world_ticks=18446744073709551615n.toString();max.world_generation=18446744073709551615n.toString();max.source_frame=4294967295;max.display_flags=7;for(const key of h.TYPED22_RELATION_KEYS)max[key]=false;
 // String uint64 values add quotes, conservatively exceeding actual native decimal serialization.
 assert.ok(Buffer.byteLength(JSON.stringify(max))<=1508);
 const malformed=structuredClone(max);malformed.seed_hex='x'.repeat(10000);const r=compare(typedExpected,malformed,true);assert.match(r.decision,/REFUSE/);assert.ok(Buffer.byteLength(JSON.stringify(r))<=512);
});
const freshReport=()=>({scenario:{first_sss_vs_setup:{observation:{first_sss_vs_setup:{match_world_generation:3}}},matched_setup_cf:{actions_completed:7168,actions:Array.from({length:7168},(_,i)=>({schedule_index:i})),decision:'PASS_FRESH132_FULL_PAIR_TYPED22_LIFETIME_7168_3501_FIELDS_SEED_C_F_GO_GO2',common_field_count:22,seed_value_count:1,common_field_comparisons_passed:3501,seed_value_comparisons_passed:3501,exact_value_comparisons_passed:80523,last_native_state:{schema:'melee-web-stadium-source-action-native-v7',action_count:7168,source_ticks:3585,source_draws:3583,complete:true,failed:false,cursor:7168}}}});
test('actual context archives22 A/close/restart and distinct B, refusing mutation and oldshape',async()=>{
 const report=freshReport(),context=createStadiumRecreationCanaryContext({reportA:report,checkpointA:{},fullPair:true,fresh132Typed22:true});await context.archiveA(async()=>{});const receipt={schema:'melee-web-stadium-recreation-boundary-v2',attempted:true,closed:true,restarted:false,error:null,match_to_host_restored:true,host_to_parent_restored:true,prior_world_generation:3,matched_setup_cf:report.scenario.matched_setup_cf.last_native_state};await context.archiveClose(receipt,async()=>{});context.acceptRestart({...receipt,restarted:true});assert.equal(context.beginB({scenario:{}},{}).priorWorldGeneration,3);
 for(const change of [r=>r.scenario.matched_setup_cf.last_native_state.source_draws=3582,r=>r.scenario.matched_setup_cf.common_field_count=15]){const bad=freshReport();change(bad);const c=createStadiumRecreationCanaryContext({reportA:bad,checkpointA:{},fullPair:true,fresh132Typed22:true});await assert.rejects(c.archiveA(async()=>{}));}
 const mutation=freshReport(),c=createStadiumRecreationCanaryContext({reportA:mutation,checkpointA:{},fullPair:true,fresh132Typed22:true});await assert.rejects(c.archiveA(async()=>{mutation.scenario.extra=true;}));
});
test('checkpoint exact22 pair eligibility refuses scope/count/field confusion',()=>{
 const q={scenario:{matched_setup_cf:{mode:h.FRESH132_TYPED22_MODE,schedule_actions:7168,common_field_count:22,seed_value_count:1,explicit_step_mode:true}}};assert.equal(isStadiumC3ReportCheckpointEligible(q),true);
 for(const [key,value] of [['mode','fresh132-full-a-v1'],['schedule_actions',7167],['common_field_count',15],['seed_value_count',0],['explicit_step_mode',false]]){const bad=structuredClone(q);bad.scenario.matched_setup_cf[key]=value;assert.equal(isStadiumC3ReportCheckpointEligible(bad),false);}
});
test('retained actual3501 rows derive22 values without changingMWC6; simulated nativevalues only',async t=>{
 const root=process.env.MELEE_STADIUM_FRESH132_CONTROL_RUN_ROOT;if(!root)return t.skip('actual retained original projection not supplied');const p=JSON.parse(await fs.readFile(root+'/fresh132-full-a-capture-candidate-v3/full-a-source-host-projection-v1.json','utf8'));const a=h.hostEvidenceFromFresh132FullAProjection(p),b=h.hostEvidenceFromFresh132FullAProjection(p,{typed22:true});assert.equal(b.mode,h.FRESH132_TYPED22_MODE);assert.deepEqual(h.encodeFresh132FullANativeManifest(a.runtimePayload),h.encodeFresh132FullANativeManifest(b.runtimePayload));
 for(const row of b.commonRowsByAction.values()){const native=syntheticNative(row,true);native.source_frame=row.source_tick-84;const r=compare(row,native,true);assert.equal(r.decision,'PASS_EXACT_22_COMMON_FIELDS_AND_SEED');}assert.equal(b.commonRowsByAction.size,3501);
});

test('actual private identity binds new arm/full-close/full-B wrappers',async()=>{
 const vm=await import('node:vm'),source=await fs.readFile(new URL('../src/stadium_recreation_identity_library.js',import.meta.url),'utf8');
 let library;const memory=new WebAssembly.Memory({initial:1,maximum:2}),Module={HEAPU8:new Uint8Array(memory.buffer)};
 const names=['_main','_melee_web_native_menu_stadium_recreation_status','_melee_web_native_menu_stadium_recreation_close','_melee_web_native_menu_stadium_recreation_restart','_melee_web_native_menu_stadium_recreation_admit_b','_melee_web_native_menu_stadium_fresh132_typed22_explicit_arm','_melee_web_native_menu_stadium_recreation_close_full_pair','_melee_web_native_menu_stadium_recreation_admit_full_b'];
 for(const name of names)Module[name]=()=>{};
 const context=vm.createContext({Module,wasmExports:{native:()=>{}},wasmMemory:memory,WebAssembly,Uint8Array,addToLibrary(v){library=v;}});
 vm.runInContext(source,context);vm.runInContext(library.$MeleeWebStadiumRecreationIdentityV1__postset,context);const api=Module.__meleeWebStadiumRecreationIdentityV1;assert.equal(api.captureFresh132Typed22Pair().captured,true);assert.equal(api.compare().same_instance,true);
 for(const name of names){const original=Module[name];Module[name]=()=>{};assert.equal(api.compare().same_instance,false,name);Module[name]=original;}
 memory.grow(1);Module.HEAPU8=new Uint8Array(memory.buffer);assert.equal(api.compare().same_instance,true);assert.equal(api.compare().memory_buffer_changed,true);
});
test('actual runtime close accepts exact new shape/live receipt and preserves legacy admission',async()=>{
 const vm=await import('node:vm'),source=await fs.readFile(new URL('../web/melee-runtime.mjs',import.meta.url),'utf8');const start=source.indexOf('    menuStadiumRecreationClosed(receipt) {'),end=source.indexOf('    menuFrame(',start);assert.ok(start>0&&end>start);const body=source.slice(start,end);
 const base={schema:'melee-web-stadium-recreation-boundary-v2',attempted:true,closed:true,restarted:false,error:null,match_to_host_restored:true,host_to_parent_restored:true,prior_world_generation:3,match_seed:1,host_seed:2,parent_seed:3,matched_setup_cf:{schema:'melee-web-stadium-source-action-native-v7',scope:'fresh132-full-pair-7168-typed22-seed-observation-only',common_field_count:22,seed_value_count:1,action_count:7168,cursor:7168,source_ticks:3585,source_draws:3583,state:'complete',complete:true,failed:false,error:null,queued:false,awaiting_approval:false}};
 function run(receipt,mutate=()=>{},activeTransfer=false){const ctx={ready:true,fatal:false,destroyed:false,stadiumRecreationCloseSettled:false,terminalMatchedSetupCfRetirementObserved:false,terminalMatchedSetupCfRetirementConsumed:false,busy:false,assetTransfer:false,commands:[],loading:null,preparationLabel:null,diagnosticActive:false,callbacks:{menuPreparationCanceled(){}},publish(){},emit(){},Module:{stadiumMatchedSetupNativeIdle:()=>true,_melee_web_native_menu_stadium_recreation_status:()=>1,_melee_web_native_menu_phase:()=>0,_melee_web_native_menu_running:()=>0,UTF8ToString:()=>JSON.stringify(receipt)}};mutate(ctx);vm.createContext(ctx);installActualLaterBTransferOwner(vm,source,ctx);
 if(activeTransfer){
  // Synthetic reservation invokes the actual owner; this proves callback refusal,
  // not native lifetime success. Traps must remain untouched after reservation.
  vm.runInContext(`laterBReplayTransfer.reserve({reference_id:'original-b-2013-943-v1',
   recipe_sha256:'5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755',
   cursor:943,notification_order:4330,world_generation:7,setup_seed_hex:'4b25c79b'});`,ctx);
  const trap=()=>{throw Error('active transfer callback performed ordinary native/host effects');};
  for(const key of Object.keys(ctx.Module))ctx.Module[key]=trap;
  ctx.callbacks.menuPreparationCanceled=ctx.publish=ctx.emit=trap;
 }
 return vm.runInContext(`({${body}})`,ctx).menuStadiumRecreationClosed(receipt);}

 assert.equal(run(base),true);
 assert.equal(run(base,()=>{},true),false,'actual active transfer refuses before native getters or host effects');
 for(const [key,value] of [['action_count',7167],['source_draws',3582],['common_field_count',15],['queued',true],['awaiting_approval',true]]){const bad=structuredClone(base);bad.matched_setup_cf[key]=value;assert.equal(run(bad),false,key);}
 for(const mutate of [c=>c.busy=true,c=>c.Module.stadiumMatchedSetupNativeIdle=()=>false,c=>c.Module.UTF8ToString=()=>'{}',c=>c.Module._melee_web_native_menu_running=()=>1])assert.equal(run(base,mutate),false);
 const legacy=structuredClone(base);legacy.schema='melee-web-stadium-recreation-boundary-v1';Object.assign(legacy.matched_setup_cf,{schema:'melee-web-stadium-source-action-native-v5',scope:'original-full7167-common-fields-seed-observation-only',common_field_count:15,action_count:7167,cursor:7167,source_draws:3582});assert.equal(run(legacy),true);
 Object.assign(legacy.matched_setup_cf,{schema:'melee-web-stadium-source-action-native-v2',scope:'proper-prefix-next-draw-tick-draw',action_count:174,cursor:174});assert.equal(run(legacy),true);
});


test('typed22 adversarial refusal serialization keeps valid identities and old15 bytes',()=>{
 const row=structuredClone(typedExpected);row.action_index=7166;row.source_tick=3584;row.seed_ordinal=3500;row.boundary_role='IDLE';row.draw_ordinal=3582;row.raw_identity={sequence:Number.MAX_SAFE_INTEGER,source_tick:3584,draw_ordinal:3582};
 for(const actual of [{},null,{schema:'x'.repeat(10000)}, {...syntheticNative(row,true),map2_countdown:'x'.repeat(10000)}]){
  const result=compare(row,actual??{},true);assert.match(result.decision,/REFUSE/);assert.ok(Buffer.byteLength(JSON.stringify(result))<=512);assert.equal(result.original_sequence,Number.MAX_SAFE_INTEGER);assert.equal(result.action_index,7166);assert.equal(result.source_tick,3584);assert.equal(result.draw_ordinal,3582);
 }
 const bad=structuredClone(row);bad.source_tick=Number.MAX_SAFE_INTEGER;bad.draw_ordinal=-1;const result=h.compareOriginalCommonTickFields({expected:bad,actual:{},actionIndex:Number.MAX_SAFE_INTEGER,typed22:true});assert.equal(result.first_mismatch.path,'original.coordinates');assert.equal(result.source_tick,null);assert.equal(result.draw_ordinal,null);assert.equal(result.action_index,null);assert.equal(result.original_sequence,Number.MAX_SAFE_INTEGER);assert.ok(Buffer.byteLength(JSON.stringify(result))<=512);
 const native=syntheticNative(row,true);native.source_frame=0;const good=compare(row,native,true);assert.equal(good.decision,'PASS_EXACT_22_COMMON_FIELDS_AND_SEED');assert.equal(good.original_sequence,Number.MAX_SAFE_INTEGER);assert.ok(Buffer.byteLength(JSON.stringify(good))<=512);
});

test('actual shell and checked core close renew only one newer finite lifetime',async()=>{
 const vm=await import('node:vm'),shell=await fs.readFile(new URL('../web/runtime-development.mjs',import.meta.url),'utf8'),core=await fs.readFile(new URL('../web/melee-runtime.mjs',import.meta.url),'utf8');
 const start=shell.indexOf("// Only the shared owner's fully checked"),end=shell.indexOf('window.meleeNetTimingPauseEligibilitySnapshot=',start),cs=core.indexOf('    menuStadiumRecreationClosed(receipt) {'),ce=core.indexOf('    menuFrame(',cs);assert.ok(start>0&&end>start&&cs>0&&ce>cs);
 // Explicit host/native-observation doubles exercise actual shell/owner guards;
 // no original game execution or valid compiled lifetime is claimed here.
 function fixture(){
  let generation=1,phase=0,running=0,launches=0,stageCalls=0,hold=null,closed=false;
  const state={state:'prepared',busy:false,loading:false,requiresReload:false,ready:true,canStart:true,phase:0,running:false};
  const receipt={schema:'melee-web-stadium-recreation-boundary-v2',attempted:true,closed:true,restarted:false,error:null,match_to_host_restored:true,host_to_parent_restored:true,prior_world_generation:3,match_seed:1,host_seed:2,parent_seed:3,matched_setup_cf:{schema:'melee-web-stadium-source-action-native-v7',scope:'fresh132-full-pair-7168-typed22-seed-observation-only',common_field_count:22,seed_value_count:1,action_count:7168,cursor:7168,source_ticks:3585,source_draws:3583,state:'complete',complete:true,failed:false,error:null,queued:false,awaiting_approval:false}};
  let liveReceipt=receipt;
  let observation={schema:'melee-web-stadium-first-css-finite-observation-v4',mode:'fresh_finite_css_prefix_v4',state:'armed',error:null,native_phase:0,running:false,host_entered:false,armed_owner_stable:true,armed_world_generation:1,host_tick_calls:0,host_draw_calls:0,aurora_begin_calls:0,aurora_end_calls:0,source_steps:0,source_draws:0};
  const Module={stadiumMatchedSetupNativeIdle:()=>true,_melee_web_native_menu_stadium_recreation_status:()=>2,_melee_web_native_menu_phase:()=>phase,_melee_web_native_menu_running:()=>running,_melee_web_native_menu_stadium_first_css_fresh_finite_v4_arm(){},_melee_web_native_menu_stadium_first_css_draw_observe:()=>1,UTF8ToString:p=>JSON.stringify(p===1?observation:liveReceipt),_melee_web_native_menu_launch:()=>{++launches;phase=1;running=1;Object.assign(state,{state:'css',scene:'css',phase:1,running:true,paused:false});Object.assign(observation,{state:'entered',native_phase:1,running:true,host_entered:true});return 1;},_melee_web_native_menu_stadium_first_css_fresh_finite_v4_first_kick:()=>{++stageCalls;return 1;}};
  const owner={handle:{getState:()=>state}},boundary=async fn=>{if(hold)await hold;return fn();};
  const c=vm.createContext({window:{},Module,owner,boundary,check:x=>x,developmentHooks:{},structuredClone,resetTiming(){},prepareAudio:async()=>{},pauseAudioForPreparation:async()=>{},waitForAudioRender:async()=>{},$:()=>({focus(){}}),inputDirty:false,syncAudio(){},ready:true,fatal:false,destroyed:false,stadiumRecreationCloseSettled:false,terminalMatchedSetupCfRetirementObserved:false,terminalMatchedSetupCfRetirementConsumed:false,busy:false,assetTransfer:false,commands:[],loading:null,preparationLabel:null,diagnosticActive:false,callbacks:{menuPreparationCanceled(){}},publish(){}});
  installActualLaterBTransferOwner(vm,core,c);
  c.emit=(name,data)=>c.developmentHooks[name]?.(data);vm.runInContext(shell.slice(start,end),c);const callbacks=vm.runInContext(`({${core.slice(cs,ce)}})`,c);
  return {c,Module,state,receipt,callbacks,get launches(){return launches;},get stageCalls(){return stageCalls;},restart(gen=4){generation=gen;phase=0;running=0;liveReceipt={...receipt,restarted:true};Object.assign(state,{state:'prepared',scene:'css',phase:0,running:false,paused:false});Object.assign(observation,{state:'armed',native_phase:0,running:false,host_entered:false,armed_world_generation:gen});},close(){phase=0;running=0;liveReceipt=receipt;closed=callbacks.menuStadiumRecreationClosed(receipt);return closed;},hold(p){hold=p;},get liveReceipt(){return liveReceipt;},set liveReceipt(v){liveReceipt=v;}};
 }
 const f=fixture();await f.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);vm.runInContext('freshFirstCssKickAttempted=true',f.c);await f.c.window.meleeStadiumFirstCssFreshFiniteStage('first');await assert.rejects(f.c.window.meleeStadiumFirstCssFreshFiniteStage('first'),/already attempted/);await assert.rejects(f.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1),/already attempted/);
 assert.equal(f.close(),true);assert.equal(f.callbacks.menuStadiumRecreationClosed(f.receipt),false);await assert.rejects(f.c.window.meleeStadiumFirstCssFreshFiniteStage('stream_arm'),/no prepared finite-owner lease/);
 f.restart();await f.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4);assert.equal(vm.runInContext('freshFirstCssKickAttempted',f.c),false);await f.c.window.meleeStadiumFirstCssFreshFiniteStage('first');assert.equal(f.launches,2);assert.equal(f.stageCalls,2);await assert.rejects(f.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/);await assert.rejects(f.c.window.meleeStadiumFirstCssFreshFiniteStage('first'),/already attempted/);
 for(const kind of ['old-generation','foreign-restart','not-restarted','owner','module','handle','boundary','check','busy','unsafe-idle','wrong-route','queued-owner-change']){
  const q=fixture();await q.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);assert.equal(q.close(),true);q.restart();
  if(kind==='old-generation')q.restart(3);
  if(kind==='foreign-restart')q.liveReceipt={...q.liveReceipt,parent_seed:9};
  if(kind==='not-restarted')q.liveReceipt=q.receipt;
  if(kind==='owner')q.c.owner={handle:q.c.owner.handle};
  if(kind==='module')q.c.Module={...q.Module};
  if(kind==='handle')q.c.owner.handle={getState:()=>q.state};
  if(kind==='boundary')q.c.boundary=async fn=>fn();
  if(kind==='check')q.c.check=x=>x;
  if(kind==='busy')q.state.busy=true;
  if(kind==='unsafe-idle')q.Module.stadiumMatchedSetupNativeIdle=()=>false;
  let release;if(kind==='queued-owner-change')q.hold(new Promise(r=>release=r));
  const pending=kind==='wrong-route'?q.c.window.meleeStadiumFirstCssFreshRowLaunch(4):q.c.window.meleeStadiumFirstCssFreshFiniteLaunch(kind==='old-generation'?3:4);
  if(release){q.c.owner={handle:q.c.owner.handle};release();}
  await assert.rejects(pending,/owner|generation|restart|prepared|CREATED|capability/i,kind);assert.equal(q.launches,1,kind);await assert.rejects(q.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/,kind+' failure stays consumed');
 }
 const racing=fixture();await racing.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);assert.equal(racing.close(),true);racing.restart();let release;racing.hold(new Promise(r=>release=r));const pending=racing.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4);await assert.rejects(racing.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/);release();await pending;assert.equal(racing.launches,2);
 const legacy=fixture();await legacy.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);legacy.receipt.schema='melee-web-stadium-recreation-boundary-v1';Object.assign(legacy.receipt.matched_setup_cf,{schema:'melee-web-stadium-source-action-native-v5',scope:'original-full7167-common-fields-seed-observation-only',common_field_count:15,action_count:7167,cursor:7167,source_draws:3582});assert.equal(legacy.close(),true);legacy.restart();await assert.rejects(legacy.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/);
 const failed=fixture();failed.state.busy=true;await assert.rejects(failed.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1),/idle prepared/);failed.state.busy=false;await assert.rejects(failed.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1),/already attempted/);
 const noClose=fixture();await noClose.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);noClose.restart();await assert.rejects(noClose.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/);
 const refused=fixture();await refused.c.window.meleeStadiumFirstCssFreshFiniteLaunch(1);refused.c.busy=true;assert.equal(refused.close(),false);refused.c.busy=false;refused.restart();await assert.rejects(refused.c.window.meleeStadiumFirstCssFreshFiniteLaunch(4),/already attempted/);
});
