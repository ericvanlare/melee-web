// Actual serialized driver functions, synthetic native endpoints; no GPU proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {armNativeFrameEffectFirstDraw,readFirstDrawComponents,armFirstDrawComponents,firstDrawComponentFailures} from './match_frame_effect_first_draw_driver.mjs';
function nativeReceipt(){
 const before={generation:'1',registration_sequence:'3',callback_sequence:'3',stale_callback_count:'0',queue_handle:'9',last_begin_frame_id:'4',last_end_frame_id:'4',frame_index:1,active_frame_count:2,active_staging_count:2,free_frame_count:2,free_staging_count:2,worker_idle:1,process_events_queued:0};
 const slots=Array.from({length:4},(_,i)=>Object.fromEntries([['slot',i<2?i:0],['frame_index',0],['encoder_present',0],...['frame_id','render_pass_count','texture_copy_count','encoder_task_count','op_count','texture_upload_count','after_submit_callback_count','verts_bytes','uniforms_bytes','indices_bytes','storage_bytes','texture_upload_bytes'].map(k=>[k,'0'])]));
 const owners=Array.from({length:4},()=>Object.fromEntries([['state',0],...['future_id','registration_sequence','callback_sequence','captured_generation','queue_handle','encoder_handle'].map(k=>[k,'0'])]));
 return {schema:'melee-web-native-first-draw-components-v1',copied:true,component_ready:true,refusal:'',composite_eligible:false,frame_slot_capacity:4,staging_slot_capacity:4,facts:{schema_version:1,owner_book_present:1,before,after:{...before},packets:{schema_version:1,active_slot_count:2,recorder:{active:0,in_offscreen:0,suspended_efb_pass:0},slots},owners}};
}
function fixture(){
 let phase='READY',idle=true,returned=true,failure=null,seq=3;const calls=[];const words=enabled=>{const a=Array(35).fill(0);a[0]=1;a[1]=enabled;a[4]=3;for(let i=0;i<3;++i)a[8+i*9]=i;return a;};
 const status=()=>({schema:'melee-web-private-depth-no-work-v1',phase,sequence:seq,initBegins:1,initEnds:1,firstFailure:failure,bridgeFailure:false,snapshots:{preMain:words(0),initialized:words(1),arm:phase==='ACTIVE'?words(1):null},componentOnly:true,gameplayAdmission:false,remainingGates:['native-submission','LOAD-content','device-host-futures','native-refs','restore']});
 const module={matchFrameEffectNativeIdle:()=>idle,matchFrameEffectCallbackReturned:true,UTF8ToString:x=>x,
  __meleeWebDepthNoWorkV1:{status},_melee_web_native_menu_match_observe:()=>JSON.stringify({ready:true,frame:200}),
  _melee_web_native_menu_frame_effect_arm_first_draw:()=>{calls.push('native-arm');return 1;},_melee_web_native_menu_frame_effect_status:()=>'',
  _melee_web_native_menu_frame_effect_components:()=>{calls.push('native-components');return JSON.stringify(nativeReceipt());},
  _aurora_browser_depth_no_work_arm_component:()=>{calls.push('depth-arm');phase='ACTIVE';++seq;return 1;}};
 const context=vm.createContext({Module:module});const arm=vm.runInContext('('+armNativeFrameEffectFirstDraw.toString()+')',context),read=vm.runInContext('('+readFirstDrawComponents.toString()+')',context),armDepth=vm.runInContext('('+armFirstDrawComponents.toString()+')',context);
 return {module,context,calls,arm:()=>arm({componentEligibility:true}),read,armDepth,dirty:()=>{failure={code:'DEPTH_FORBIDDEN_READ'};phase='TERMINAL';},busy:()=>{idle=false;},replace:()=>{context.Module={...module};}};
}
function observe(f){
 const read=f.read();const failures=firstDrawComponentFailures(JSON.parse(JSON.stringify(read)),{stage:'read'});
 if(failures.length)return {read,failures};
 const armed=f.armDepth({expectedDepthSequence:read.depth_before.sequence});
 if(armed.status!=='armed')return armed;
 return {...read,schema:'melee-web-first-draw-components-v1',status:'observed',depth_after:armed.depth_after};
}
test('actual component driver retains same Module, reads before depth arm, and never admits composite',()=>{
 const f=fixture();assert.equal(f.arm().armed,true);const value=observe(f);assert.deepEqual(firstDrawComponentFailures(JSON.parse(JSON.stringify(value))),[]);
 assert.deepEqual(f.calls,['native-arm','native-components','depth-arm']);assert.equal(value.composite_eligible,false);
 assert.throws(()=>vm.runInContext('Object.defineProperty(globalThis,"__meleeWebFirstDrawComponentModuleV1",{value:{}})',f.context));
});
for(const [name,mutate,reason] of [
 ['Module replacement',f=>f.replace(),'same_module_or_returned_idle_missing'],
 ['native busy',f=>f.busy(),'same_module_or_returned_idle_missing'],
 ['callback pending',f=>{f.module.matchFrameEffectCallbackReturned=false;},'same_module_or_returned_idle_missing'],
 ['depth work',f=>f.dirty(),'depth_history_not_clean'],
 ['missing export',f=>{delete f.module._melee_web_native_menu_frame_effect_components;},'private_exports_missing'],
 ['native refusal',f=>{f.module._melee_web_native_menu_frame_effect_components=()=>JSON.stringify({copied:false,component_ready:false});},'native_component_not_ready'],
 ['native drift',f=>{f.module._melee_web_native_menu_frame_effect_components=()=>{f.replace();return JSON.stringify(nativeReceipt());};},'module_or_idle_changed'],
 ])test(name+' refuses before further native use',()=>{const f=fixture();assert.equal(f.arm().armed,true);mutate(f);const value=f.read();assert.equal(value.status,'refused');assert.equal(value.reason,reason);assert.notEqual(f.calls.at(-1),'depth-arm');});
test('premain refusal and duplicate sentinel cannot arm again',()=>{const f=fixture();f.dirty();assert.equal(f.arm().armed,false);assert.deepEqual(f.calls,[]);const g=fixture();assert.equal(g.arm().armed,true);assert.equal(g.arm().armed,false);assert.deepEqual(g.calls,['native-arm']);});
test('strict component decoder refuses malformed and widened native rows',()=>{
 const f=fixture();f.arm();const original=JSON.parse(JSON.stringify(observe(f)));
 for(const mutate of [v=>v.native.facts.owners[0]=null,v=>v.native.facts.packets.slots[0]=null,v=>v.native.facts.before.generation=1,v=>v.native.frame_slot_capacity=5,v=>v.native.extra=0,v=>v.depth_after.phase='READY',v=>v.composite_eligible=true,v=>v.native.facts.owners[0].state=1,v=>v.native.facts.packets.slots[0].verts_bytes='4',v=>v.native.facts.before.free_frame_count=0]){
  const v=structuredClone(original);mutate(v);assert(firstDrawComponentFailures(v).length);
 }
});

test('actual separate depth arm rechecks Module, idle, depth sequence and native return',()=>{
 for(const mutate of [f=>f.replace(),f=>f.busy(),f=>f.dirty(),f=>{f.module._aurora_browser_depth_no_work_arm_component=()=>0;}]){
  const f=fixture();f.arm();const read=f.read();assert.deepEqual(firstDrawComponentFailures(JSON.parse(JSON.stringify(read)),{stage:'read'}),[]);
  mutate(f);const value=f.armDepth({expectedDepthSequence:read.depth_before.sequence});assert.equal(value.status,'refused');
 }
});
test('actual serialized malformed claimed-ready read never reaches depth arm',()=>{
 const f=fixture();f.arm();f.module._melee_web_native_menu_frame_effect_components=()=>JSON.stringify({copied:true,component_ready:true});
 const read=f.read();assert.equal(read.status,'read');assert(firstDrawComponentFailures(JSON.parse(JSON.stringify(read)),{stage:'read'}).length);
 assert.equal(f.calls.includes('depth-arm'),false);
});
