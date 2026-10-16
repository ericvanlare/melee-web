// Actual serialized driver functions, synthetic native endpoints; no GPU proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {armNativeFrameEffectFirstDraw,readFirstDrawComponents,armFirstDrawComponents,firstDrawComponentFailures,startFirstDrawSelectedTextureInterval,endFirstDrawSelectedTextureInterval} from './match_frame_effect_first_draw_driver.mjs';
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

function intervalFixture(){
 const f=fixture();f.arm();const components=observe(f);let phase='INSTALLED',endFence=false;
 const cap=Object.freeze({});
 f.module._melee_web_native_menu_frame_effect_issue_selected_texture=()=>{f.calls.push('selected-issue');phase='ISSUED';return 1;};
 const identity={nativeKeys:[4,8,12,9],textureSerial:[635,0],viewSerial:[636,0],deviceEpoch:[1,0]};
 const status=()=>{const component=!['INSTALLED','ISSUED'].includes(phase);return {identity,schema:component?2:1,scope:component?'selected-texture-readonly-component-only':phase==='ISSUED'?'native-issued-unarmed-component-only':'fixture-component-only',
  phase,epoch:1,attempts:0,dispatches:0,fenceComplete:component,firstFailure:null,gameplayAdmission:false,
  ...(component?{endFenceComplete:endFence,commandEpoch:1,commandDispatches:0,submitChecks:0}:{})};};
 f.module.__meleeWebTextureReadonlyGuardV1={capability:()=>phase==='INSTALLED'?null:cap,status,
  async armSelectedComponent(){f.calls.push('start-fence');phase='COMPONENT_ACTIVE';return status();},
  async endSelectedComponent(){f.calls.push('end-fence');endFence=true;phase='COMPONENT_ENDED';return status();},
  close(){f.calls.push('selected-close');phase='CLOSED';return status();}};
 const invoke=(fn,arg)=>Promise.resolve(vm.runInContext('('+fn.toString()+')('+JSON.stringify(arg??null)+')',f.context));
 return {...f,components,invoke};
}
test('actual selected interval transport keeps strict native/depth checks across separate genuine-fence services',async()=>{
 const f=intervalFixture(),started=await startFirstDrawSelectedTextureInterval(f);assert.equal(started.status,'active');
 const ended=await endFirstDrawSelectedTextureInterval({...f,started});assert.equal(ended.status,'closed');assert.equal(ended.composite_eligible,false);
 assert.deepEqual(f.calls.filter(x=>['selected-issue','start-fence','end-fence','selected-close'].includes(x)),['selected-issue','start-fence','end-fence','selected-close']);
});
test('actual selected interval refuses malformed claimed-ready pre-start facts before issuer',async()=>{
 const f=intervalFixture();f.module._melee_web_native_menu_frame_effect_components=()=>JSON.stringify({copied:true,component_ready:true});
 const result=await startFirstDrawSelectedTextureInterval(f);assert.equal(result.status,'refused');assert(!f.calls.includes('selected-issue'));
});
test('actual selected interval refuses malformed claimed-ready end facts before close or native retire',async()=>{
 const f=intervalFixture(),started=await startFirstDrawSelectedTextureInterval(f);assert.equal(started.status,'active');
 const original=f.module.__meleeWebTextureReadonlyGuardV1.endSelectedComponent;
 f.module.__meleeWebTextureReadonlyGuardV1.endSelectedComponent=async()=>{const value=await original();f.module._melee_web_native_menu_frame_effect_components=()=>JSON.stringify({copied:true,component_ready:true});return value;};
 const result=await endFirstDrawSelectedTextureInterval({...f,started});assert.equal(result.status,'refused');assert(!f.calls.includes('selected-close'));assert(!f.calls.includes('retire'));
});
test('actual selected interval rechecks same Module after initial fence',async()=>{
 const f=intervalFixture(),original=f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent;
 f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent=async()=>{const value=await original();f.replace();return value;};
 const result=await startFirstDrawSelectedTextureInterval(f);assert.equal(result.status,'refused');assert(!f.calls.includes('selected-close'));
});
test('actual selected interval refuses current-owner native refusal after initial fence',async()=>{
 const f=intervalFixture(),original=f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent;
 f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent=async()=>{const value=await original();f.module._melee_web_native_menu_frame_effect_components=()=>JSON.stringify({copied:false,component_ready:false,refusal:'selected_texture_current_owner_mismatch'});return value;};
 const result=await startFirstDrawSelectedTextureInterval(f);assert.equal(result.status,'refused');assert(!f.calls.includes('selected-close'));
});
test('actual selected interval refuses otherwise-valid native stamp drift before end fence',async()=>{
 const f=intervalFixture(),started=await startFirstDrawSelectedTextureInterval(f);assert.equal(started.status,'active');
 f.module._melee_web_native_menu_frame_effect_components=()=>{const v=nativeReceipt();v.facts.before.registration_sequence=v.facts.after.registration_sequence='4';v.facts.before.callback_sequence=v.facts.after.callback_sequence='4';return JSON.stringify(v);};
 const result=await endFirstDrawSelectedTextureInterval({...f,started});assert.equal(result.status,'refused');assert(result.failures.includes('selected_texture_native_boundary_drift'));assert(!f.calls.includes('end-fence'));assert(!f.calls.includes('selected-close'));
});

test('selected transport refuses incomplete schema2 guard status before end or close',async()=>{
 const f=intervalFixture(),original=f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent;
 f.module.__meleeWebTextureReadonlyGuardV1.armSelectedComponent=async()=>{const value=await original();delete value.identity;return value;};
 const result=await startFirstDrawSelectedTextureInterval(f);assert.equal(result.status,'refused');assert(result.failures.includes('selected_texture_guard_status'));assert(!f.calls.includes('selected-close'));
});
test('selected transport refuses typed guard identity drift after end fence before close',async()=>{
 const f=intervalFixture(),started=await startFirstDrawSelectedTextureInterval(f);assert.equal(started.status,'active');
 const original=f.module.__meleeWebTextureReadonlyGuardV1.endSelectedComponent;
 f.module.__meleeWebTextureReadonlyGuardV1.endSelectedComponent=async()=>{const value=await original();value.identity={...value.identity,viewSerial:[637,0]};return value;};
 const result=await endFirstDrawSelectedTextureInterval({...f,started});assert.equal(result.status,'refused');assert(result.failures.includes('selected_texture_guard_identity_drift'));assert(!f.calls.includes('selected-close'));
});

test('selected transport refuses historical fixture label on unarmed native issue',async()=>{
 const f=intervalFixture(),original=f.module.__meleeWebTextureReadonlyGuardV1.status;
 f.module.__meleeWebTextureReadonlyGuardV1.status=()=>{const value=original();if(value.phase==='ISSUED')value.scope='fixture-component-only';return value;};
 const result=await startFirstDrawSelectedTextureInterval(f);assert.equal(result.status,'refused');assert(result.failures.includes('selected_texture_issue_refused'));assert(!f.calls.includes('start-fence'));assert(!f.calls.includes('selected-close'));assert(!f.calls.includes('retire'));
});

// Actual serialized page helper, synthetic returned-zero native endpoint.
// Raw refusal copying is diagnostic only and never admits a selected interval.
function issueRefusalFixture(reason='selected_texture_submitted_owner_mismatch'){
 const f=intervalFixture();
 f.module._melee_web_native_menu_frame_effect_issue_selected_texture=()=>{f.calls.push('selected-issue');return 0;};
 f.module._melee_web_native_menu_frame_effect_status=()=>{f.calls.push('native-refusal-status');return JSON.stringify({terminal:true,native_retired:false,error:reason});};
 return f;
}
for(const reason of ['selected_texture_submitted_owner_mismatch','selected_texture_current_owner_mismatch'])
 test('native issue refusal diagnostic preserves exact sticky reason '+reason,async()=>{
  const f=issueRefusalFixture(reason),value=await startFirstDrawSelectedTextureInterval(f);
  assert.equal(value.status,'refused');assert.deepEqual(value.failures,['selected_texture_issue_refused']);
  assert.equal(value.issue.issued,0);assert.equal(value.issue.guard.phase,'INSTALLED');
  assert.equal(value.issue.native_refusal.status,'copied_raw');assert.equal(value.issue.native_refusal.receipt.error,reason);
  assert.equal(f.calls.filter(x=>x==='selected-issue').length,1);assert.equal(f.calls.filter(x=>x==='native-refusal-status').length,1);
  assert(!f.calls.some(x=>['start-fence','end-fence','selected-close','retire'].includes(x)));
 });
for(const [name,change] of [
 ['Module replacement',f=>f.replace()],
 ['native busy',f=>f.busy()],
 ['callback not returned',f=>{f.module.matchFrameEffectCallbackReturned=false;}],
])test('native issue refusal diagnostic forbids getter after '+name,async()=>{
 const f=issueRefusalFixture();f.module._melee_web_native_menu_frame_effect_issue_selected_texture=()=>{f.calls.push('selected-issue');change(f);return 0;};
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'refused');
 assert.equal(value.issue.native_refusal.status,'unavailable');assert.equal(value.issue.native_refusal.reason,'same_module_or_returned_idle_missing');
 assert(!f.calls.includes('native-refusal-status'));assert(!f.calls.includes('start-fence'));assert(!f.calls.includes('retire'));
});
test('native issue refusal diagnostic performs no followup after a completed page response is lost',async()=>{
 const f=issueRefusalFixture(),invoke=f.invoke,operations=[];f.invoke=async(fn,arg)=>{
  operations.push(fn.name);const value=await invoke(fn,arg);
  if(fn.name==='issueSelectedTextureBoundary')throw Error('synthetic lost completed page response');
  return value;
 };
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'refused');
 assert.deepEqual(operations,['readSelectedTextureBoundary','issueSelectedTextureBoundary']);
 // Inline copying happened before response loss; there is no subsequent native/page call.
 assert.equal(f.calls.filter(x=>x==='selected-issue').length,1);assert.equal(f.calls.filter(x=>x==='native-refusal-status').length,1);
 assert(!f.calls.includes('start-fence'));assert(!f.calls.includes('retire'));
});
test('native issue refusal diagnostic does not follow a throwing native issuer',async()=>{
 const f=issueRefusalFixture();f.module._melee_web_native_menu_frame_effect_issue_selected_texture=()=>{f.calls.push('selected-issue');throw Error('issuer failed');};
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'refused');assert(!f.calls.includes('native-refusal-status'));assert(!f.calls.includes('start-fence'));
});
test('native issue refusal diagnostic preserves positive dispatch and does not read refusal status',async()=>{
 const f=intervalFixture();f.module._melee_web_native_menu_frame_effect_status=()=>{f.calls.push('native-refusal-status');throw Error('positive path must not read this');};
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'active');
 assert.equal(value.issue.issued,1);assert.equal(Object.hasOwn(value.issue,'native_refusal'),false);
 assert.equal(f.calls.filter(x=>x==='selected-issue').length,1);assert(!f.calls.includes('native-refusal-status'));
});
for(const [name,read] of [
 ['malformed JSON',()=>'{'],['missing error',()=>JSON.stringify({terminal:true,native_retired:false})],
 ['getter throw',()=>{throw Error('getter failed');}],
])test('native issue refusal diagnostic remains refusal on '+name,async()=>{
 const f=issueRefusalFixture();f.module._melee_web_native_menu_frame_effect_status=()=>{f.calls.push('native-refusal-status');return read();};
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'refused');assert.deepEqual(value.failures,['selected_texture_issue_refused']);
 assert.equal(value.issue.native_refusal.status,'unavailable');assert(!f.calls.includes('start-fence'));assert(!f.calls.includes('retire'));
});
test('native issue refusal diagnostic discards raw receipt if Module changes inside getter',async()=>{
 const f=issueRefusalFixture();f.module._melee_web_native_menu_frame_effect_status=()=>{f.calls.push('native-refusal-status');f.replace();return JSON.stringify({terminal:true,native_retired:false,error:'selected_texture_current_owner_mismatch'});};
 const value=await startFirstDrawSelectedTextureInterval(f);assert.equal(value.status,'refused');
 assert.equal(value.issue.native_refusal.reason,'same_module_or_returned_idle_changed');assert(!Object.hasOwn(value.issue.native_refusal,'receipt'));assert(!f.calls.includes('start-fence'));
});
