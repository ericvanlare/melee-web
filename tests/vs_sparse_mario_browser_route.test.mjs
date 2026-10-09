import test from 'node:test';
import assert from 'node:assert/strict';
import {checkSparseCss,checkSparseMatch,validateFinalSparseCapture,installSparseVirtualController,readSparsePadVectors,checkSparsePadVectors} from './vs_sparse_mario_browser_route.mjs';
import {observeRuntimeOwner} from './runtime_owner_observation.mjs';
function pair(){return {leg:'vs',ready:true,complete:false,ending:false,paused:false,outcome:0,frame:180,
 observed_player_source_slots:[0,2],rules:{stage:32,match_kind:1,is_teams:0,timer_enabled:0,is_stock:1,is_vs:1,source_sudden_death_flag:0,item_frequency:-1,player_stocks:[4,4]},
 players:[0,2].map(port=>({source_player_index:port,source_port:port,source_slot:0,slot_type:0,human:true,source_character:8,fighter:0,source_stocks:4,stocks:4,source_initial_damage:0,damage_percent:0,source_color:port/2}))};}
const state={phase:7,running:1,pause_present:true,pause_disabled:false};
test('declared original sparse rows require empty1/3 and zero raw encoding',()=>{
 const setup={doors:Array(40).fill(0)};
 for(let port=0;port<4;port++){setup.doors[port*10]=setup.doors[port*10+4]=[0,3,0,3][port];setup.doors[port*10+3]=8;}
 checkSparseCss(setup);
 for(const index of [10,14,20,24,26,30]){const bad=structuredClone(setup);bad.doors[index]=99;assert.throws(()=>checkSparseCss(bad));}
});
test('actual coherent serialized reader retains the failed source pair before admission',async()=>{
 const match=pair();match.players[1].source_port=1;
 const module={_melee_web_native_menu_message:()=> 'match',UTF8ToString:x=>x,
  _melee_web_native_menu_phase:()=>7,_melee_web_native_menu_running:()=>1,
  _melee_web_native_menu_match_observe:()=>JSON.stringify(match)};
 const oldModule=globalThis.Module,oldDoc=globalThis.document;
 globalThis.Module=module;globalThis.document={querySelector:s=>s==='#pause'?{disabled:false}:null};
 try{let evaluations=0;const page={evaluate:async(fn,arg)=>{evaluations++;return fn(arg);}},retention={};
  const o=await observeRuntimeOwner(page,retention,'sparse failure');assert.equal(evaluations,1);
  assert.throws(()=>checkSparseMatch(o.state,o.match));assert.deepEqual(retention.latestOwnerObservation.match,match);
 }finally{globalThis.Module=oldModule;globalThis.document=oldDoc;}
});
test('ordinary sparse admission rejects compact renumbering, SD, timer, wrong roster and stock change',()=>{
 checkSparseMatch(state,pair());
 for(const mutate of [m=>m.observed_player_source_slots=[0,1],m=>m.players[1].source_player_index=1,
  m=>m.players[1].source_port=1,m=>m.players[1].source_slot=3,m=>m.players[1].slot_type=1,
  m=>m.players[1].source_character=9,m=>m.players[1].stocks=3,m=>m.players[1].damage_percent=1,
  m=>m.rules.timer_enabled=1,m=>m.rules.source_sudden_death_flag=1,m=>m.leg='sudden_death',
  m=>m.observer_error=true,m=>m.complete=true,m=>m.frame=0]){
  const m=pair();mutate(m);assert.throws(()=>checkSparseMatch(state,m));}
 assert.throws(()=>checkSparseMatch({...state,running:0},pair()));
 const m=pair();m.frame=179;assert.throws(()=>checkSparseMatch(state,m,pair()));
});
test('authored virtual controller is a standard neutral live browser device',async()=>{
 let result;await installSparseVirtualController({addInitScript:async(fn,pad)=>{result=pad;assert.equal(typeof fn,'function');}});
 assert.equal(result.connected,true);assert.equal(result.mapping,'standard');assert(result.buttons.every(b=>!b.pressed&&b.value===0));assert.deepEqual(result.axes,[0,0,0,0]);
});
test('missing diagnostics, non-preparation incident or absent match phase fail final receipt',()=>{
 const capture={status:'installed',invalid_phase_steps:0,invalid_preparation_count:0,unknown_reason_count:0,dropped_incidents:0,reason_counts:Array(10).fill(0),phase_source_steps:Array(15).fill(0)};capture.phase_source_steps[7]=180;
 assert.equal(validateFinalSparseCapture(capture),180);
 assert.throws(()=>validateFinalSparseCapture(null));
 const incident=structuredClone(capture);incident.reason_counts[4]=1;assert.throws(()=>validateFinalSparseCapture(incident));
 capture.phase_source_steps[7]=0;assert.throws(()=>validateFinalSparseCapture(capture));
});

test('actual serialized PAD reader requires distinct raw and original copied sparse profiles',()=>{
 const original=globalThis.Module;
 const input={active:1,error:'',pads:[0,-1,0,-1].map(err=>({err}))};
 let copied=[0,-1,0,-1];
 globalThis.Module={UTF8ToString:x=>x,_melee_web_input_message:()=>JSON.stringify(input),
  _melee_web_native_menu_memory:()=>JSON.stringify({source_pad_errors:copied,source_pad0:{err:0}})};
 try{const sample=readSparsePadVectors();checkSparsePadVectors(sample);
  for(const wrong of [[0,0,-1,-1],[0,-1,-1,-1],[0,-1,0,0],undefined]){
   copied=wrong;assert.throws(()=>checkSparsePadVectors(readSparsePadVectors()));}
  copied=[0,-1,0,-1];input.pads[1].err=0;
  assert.throws(()=>checkSparsePadVectors(readSparsePadVectors()));
 }finally{globalThis.Module=original;}
});
