// Actual comparator over retained original rows; native leaves are modeled, not browser evidence.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {derivePairedFull,PairedFullComparator,installPairedFullLaunch} from '../scripts/stadium_paired_v11_full.mjs';
import {typedOriginalValues} from './stadium_matched_setup_cf_consumer.mjs';
const projectionPath=process.env.MELEE_STOCKOUT_PAIR_PROJECTION;
const endpointsPath=process.env.MELEE_STOCKOUT_PAIR_REPORT;
if((projectionPath!==undefined)!==(endpointsPath!==undefined)||[projectionPath,endpointsPath].some(x=>x!==undefined&&!x))throw Error('Both stockout fixture paths must be nonempty');
function original(){const p=JSON.parse(fs.readFileSync(projectionPath)),ep=JSON.parse(fs.readFileSync(endpointsPath));return {p,ep,e:derivePairedFull(p,p.recipe_sha256,p.actual_frame_count,ep)};}
test('stockout launch selects the explicit arm and still forwards once',()=>{
 const calls=[],Module={stadiumMatchedSetupNativeIdle:()=>true,_melee_web_native_menu_stadium_paired_v11_stockout_pair_arm:()=>{calls.push('stockout');return 1;},_melee_web_native_menu_stadium_paired_v11_full_pair_arm:()=>{throw Error('wrong profile');},_melee_web_native_menu_launch:()=>{calls.push('launch');return 23;}};
 vm.runInNewContext('('+installPairedFullLaunch.toString()+')({stockout:true})',{Module});
 assert.equal(Module._melee_web_native_menu_launch(),23);assert.deepEqual(calls,['stockout','launch']);assert.throws(()=>Module._melee_web_native_menu_launch(),/one-use/);
 assert.throws(()=>vm.runInNewContext('('+installPairedFullLaunch.toString()+')({stockout:1})',{}),/explicit/);
 assert.throws(()=>derivePairedFull({schema:'melee-web-stadium-stockout-pair-projection-v1'},'0'.repeat(64),1,{}),/identity/);
});
function modeledTrace(e,mutate=()=>{},mutateProbe=()=>{}){
 const c=new PairedFullComparator(e);let order=0;
 function emit(i,kind,payload,coords={}){const row={record:'stadium_paired_v11',schema:'melee-web-stadium-paired-v11-row-v1',order:order++,kind,match_index:i,world_generation:3+4*i,input_cursor:0,diagnostic_action_index:c.action,local_action_index:c.matches[i].actions,source_tick:0,draw_ordinal:0,payload,...coords};mutate(row);c.accept(row);}
 for(const [i,item]of e.matches.entries()){
  const r=item.reference,g=3+4*i,x=r.constructor?.state_projection;

  function endpoint(n){emit(i,'scene_rng',{schema:'melee-web-stadium-paired-scene-rng-point-v1',phase:['partitioned_entry_before','partitioned_entry_return','original_vs_exit_before','original_vs_exit_return'][n],source_boundary_ordinal:n,world_generation:g,seed_pointer_hex:'0x12340000',seed_hex:item.endpoints[n].value.toString(16).padStart(8,'0'),original_callback_dispatch:n>=2});}
  endpoint(0);endpoint(1);
  emit(i,'setup',{schema:'melee-web-stadium-paired-v11-setup-v1',match_index:i,input_cursor:r.actions[0].frame_index,seed_hex:r.setup.rng.value_hex,world_generation:g,world_ticks:0,source_frame:0,constructor:x?{scope:'shared-constructor-aggregate-only',entry_count:x.constructor_entry_count,parameter_roles:{min_parameter:'parameter_x0',max_parameter:'parameter_x4'},min_parameter:x.parameter_x0,max_parameter:x.parameter_x4,random_bound:x.random_bound,random_result:x.random_result,seed_before:x.seed_before_hex,seed_after:x.seed_after_hex,randi_call_count:1}:null});
  let closed=false;
  for(const a of r.actions){
   const draws=[]; // No original per-owner RNG was observed in this producer.
   const probe={schema:'melee-web-rng-draw-probe',version:3,source_cursor:c.action,overflowed:false,draws};
   mutateProbe(probe,a,r,i);c.acceptProbe(JSON.stringify(probe));
   const coords={source_tick:a.source_tick,draw_ordinal:a.draw_ordinal,input_cursor:a.frame_index??0};
   emit(i,'action_begin',{action:a.kind,seed_before_hex:'00000000',source_frame_before:0},coords);
   emit(i,'action_end',{action:a.kind,input_cursor:a.frame_index??0,seed_after_hex:'00000000',source_frame_after:0},coords);
   if(a.kind==='tick'&&i===1&&a.source_tick===r.window.go_tick)emit(i,'go',{generation:g,world_ticks:a.source_tick,sequence:3,source_branch:0,map2_gate:Number(r.go.state.map2.gate_mask_80),display_mode:r.go.state.display.mode_s16,source_seed_valid:1,source_seed_hex:r.go.rng.value_hex});
   const state=item.states.get(a.source_tick);
   if(a.kind==='tick'&&state&&!closed){const t=state.tick.source_tick;emit(i,'state',{ordinal:c.matches[i].state,selected:state.selected,typed22:{...typedOriginalValues(state.state,t),schema:'melee-web-stadium-per-tick-common-fields-native-v3',world_generation:g,source_tick:t,world_ticks:t+1,seed_hex:state.rng.value_hex},pad_queue:{schema:'melee-web-stadium-hsd-pad-queue-observation',version:2,source_generation:g,source_tick_before:t,source_tick_after:t+1,qnum_before:1,qread_before:0,qcount_before:1,qcount_after_hsd:0,tick_complete:true,queue_slot_status_hex:[...state.pads]},players:state.players.map(p=>({...p,fighter_present:true}))});}

  }
  emit(i,'window_closed',{close_policy:'declared_match_actions_complete',state_samples:r.states.length,source_ticks:r.actions.filter(a=>a.kind==='tick').length,source_draws:r.actions.filter(a=>a.kind==='draw').length,natural_tail_ticks:0});
  emit(i,'terminal',{});endpoint(2);endpoint(3);emit(i,'published_result',{outcome:i===0?7:2,winner_count:i===0?0:1,winners:i===0?[]:[1]});emit(i,'retired',{native_world_absent:true,parent_seed_restored:true,handoff_seed_hex:'00000000',restored_parent_seed_hex:'00000000'});emit(i,'results_request',{typed_results_requested:true});emit(i,'results',{typed_results_entered:true,world_generation:g+1,entry_seed_hex:'00000000',seed_hex:'00000000'});emit(i,'css',{original_css_entered:true});
 }
 emit(1,'complete',{whole_session_input_complete:true,C3:false});return c.finish({schema:'melee-web-stadium-paired-v11-full-pair-v1',full_pair:true,profile:'selected_b_natural_stockout_v1',per_owner_rng_comparison:false,whole_session_complete:true,failed:false,diagnostic_actions:e.total,lifetimes:e.matches.map((x,i)=>({match_index:i,world_generation:3+4*i,actions:x.reference.actions.length,ticks:x.reference.actions.filter(a=>a.kind==='tick').length,draws:x.reference.actions.filter(a=>a.kind==='draw').length,published_result:true,stock_losses:i===0?0:4,mode9_observed:i===1,state_samples:x.reference.states.length,natural_tail_ticks:x.reference.window.natural_tail_ticks,window_closed:true,retired:true,results_requested:true,results_entered:true,css_returned:true,scene_rng_count:4}))});
}

test('stockout same-source action and state comparison has explicitly unpaired native RNG',{skip:!projectionPath},()=>{
 const {e}=original(),result=modeledTrace(e);
 assert.equal(result.pass,true);assert.equal(result.actions,3630);assert.equal(result.per_owner_rng_comparison,false);assert.equal(result.RNG_semantics_requires_separate_python_comparison,false);assert.equal(result.C3,false);assert.deepEqual(result.lifetimes.map(x=>x.state),[0,1537]);
});
test('stockout comparator rejects changed gameplay, missing fighters, wrong winner and reordered publication',{skip:!projectionPath},()=>{
 const {e}=original();
 for(const target of ['stocks','motion_id','ground_or_air','fighter_present','pad','mode9','winner','retire']){
  let changed=false;
  assert.throws(()=>modeledTrace(e,row=>{
   if(changed||row.match_index!==1)return;
   if(row.kind==='state'&&target!=='winner'&&target!=='retire'){
    if(target==='mode9'&&row.payload.typed22.display_mode!==9)return;
    changed=true;
    if(target==='pad')row.payload.pad_queue.queue_slot_status_hex[0]='ff'.repeat(12);
    else if(target==='mode9')row.payload.typed22.display_mode=8;
    else if(target==='fighter_present')row.payload.players[0].fighter_present=false;
    else row.payload.players[0][target]++;
   }else if(row.kind==='published_result'&&(target==='winner'||target==='retire')){
    changed=true;
    if(target==='winner')row.payload.winners=[0];
    else {row.kind='retired';row.payload={native_world_absent:true,parent_seed_restored:true,handoff_seed_hex:'00000000',restored_parent_seed_hex:'00000000'};}
   }
  }),undefined,target);
  assert.equal(changed,true,target);
 }
});
test('stockout projection rejects invented A state and invalid source stock sequence',{skip:!projectionPath},()=>{
 const {p,ep}=original();
 for(const mutate of [q=>q.matches[0].go={},q=>q.matches[1].states[0].players[1].stocks=3,q=>q.matches[1].states.at(-1).players[0].stocks=1]){
  const q=structuredClone(p);mutate(q);assert.throws(()=>derivePairedFull(q,p.recipe_sha256,p.actual_frame_count,ep));
 }
});
