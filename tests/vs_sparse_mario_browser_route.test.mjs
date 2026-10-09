import test from 'node:test';
import assert from 'node:assert/strict';
import {checkSparseLoss,checkSparseTerminal,checkSparseCss,checkSparseMatch,validateFinalSparseCapture,installSparseVirtualController,readSparsePadVectors,checkSparsePadVectors} from './vs_sparse_mario_browser_route.mjs';
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


test('actual serialized active PAD reader never performs a lifecycle heap audit',()=>{
 const original=globalThis.Module;
 const input={active:1,error:'',pads:[0,-1,0,-1].map(err=>({err}))};
 let copied=[0,-1,0,-1],matchReads=0;
 globalThis.Module={UTF8ToString:x=>x,
  _melee_web_input_message:()=>JSON.stringify(input),
  _melee_web_native_menu_match_observe:()=>{matchReads++;return JSON.stringify({source_pad_errors:copied});},
  _melee_web_native_menu_memory:()=>{throw new Error('Unexpected lifecycle heap audit');}};
 try{
  checkSparsePadVectors(readSparsePadVectors({activeMatch:true}));
  assert.equal(matchReads,1);
  for(const wrong of [[0,0,-1,-1],[0,-1,-1,-1],[0,-1,0,0],undefined]){
   copied=wrong;assert.throws(()=>checkSparsePadVectors(readSparsePadVectors({activeMatch:true})));
  }
  copied=[0,-1,0,-1];input.pads[1].err=0;
  assert.throws(()=>checkSparsePadVectors(readSparsePadVectors({activeMatch:true})));
 }finally{globalThis.Module=original;}
});


import {configureSparseControllerSettings} from './sparse_controller_settings_driver.mjs';
function controlsPage({selectorError=null,closeError=null}={}){
 let open=false,advanced=false,port=0;const sources=['auto','auto','auto','auto'],events=[];
 const rows=()=>[{port,active:!advanced,output:{buttons:0,stick:[0,0],cstick:[0,0],triggers:[0,0]}}];
 return {events,locateOpen:()=>open,manager:{inspect:rows,getPortSource:p=>sources[p]},
  locator(selector){return {
   async click(){events.push(selector);if(selector==='#controls-open')open=true;
    if(selector==='#controller-advanced > summary')advanced=true;
    if(selector==='#controls-close'){if(closeError)throw closeError;open=advanced=false;}},
   async selectOption(value){const names=['one','two','three','four'];const i=names.findIndex(n=>selector===`#player-${n}-source`);if(i>=0)sources[i]=value;},
   async waitFor(){if(!advanced)throw Error('Advanced not mounted');},
   async evaluate(fn){return fn({open:selector==='#controls-dialog'?open:advanced});},
   async count(){return advanced?1:0;}
  };},
  getByLabel(_label,options){assert.equal(options.exact,true);return {async selectOption(value){assert(advanced);if(selectorError)throw selectorError;port=Number(value);}};},
  async evaluate(){return rows();},
  async waitForFunction(fn){events.push('active-wait');assert(!open&&!advanced);assert(fn());}
 };
}
test('actual Controls helper mounts assignment then unmounts before active readiness',async()=>{
 const previous=globalThis.Module,p=controlsPage(),retention={};globalThis.Module={meleeControllers:p.manager};
 try{await configureSparseControllerSettings({page:p,retention});assert.equal(retention.advancedRows[0].active,false);
  assert.equal(retention.activeRows[0].active,true);assert.equal(retention.dialogClosed,true);
  assert(p.events.indexOf('#controls-close')<p.events.indexOf('active-wait'));
 }finally{globalThis.Module=previous;}
});
test('actual Controls helper closes on selector failure and preserves primary over cleanup error',async()=>{
 const primary=Error('deliberate selector failure'),secondary=Error('deliberate close failure');
 for(const closeError of [null,secondary]){
  const p=controlsPage({selectorError:primary,closeError}),retention={};
  await assert.rejects(configureSparseControllerSettings({page:p,retention}),error=>error===primary);
  assert(p.events.includes('#controls-close'));assert(!p.events.includes('active-wait'));
  if(closeError)assert.equal(retention.closeError,String(secondary));else assert.equal(p.locateOpen(),false);
 }
});


import fs from 'node:fs';
import {sparseP3FighterDecision} from './vs_sparse_mario_browser_route.mjs';
const actualCursorRows=JSON.parse(fs.readFileSync(new URL('./fixtures/sparse-p3-actual-cursor-geometry.json',import.meta.url)));
function syntheticP3Owner(){
 const setup={doors:Array(40).fill(0),cursors:Array(16).fill(0)};
 setup.cursors[8]=2;setup.doors[22]=25;setup.doors[23]=26;
 return setup;
}
test('eight captured geometry rows require no-A fresh board entry with synthetic checked owner context',()=>{
 assert.equal(actualCursorRows.observations.length,8);
 for(const row of actualCursorRows.observations){
  const decision=sparseP3FighterDecision(row,syntheticP3Owner());
  assert.deepEqual(decision,{kind:'fresh-board-entry',button:0,x:0,y:80});
 }
 const o=actualCursorRows.observations.at(-1),[x,y,mx,my]=o.geometry;
 assert((x-(mx-3.8))**2+(y-(my+2.6))**2<9,'Captured prior distance-only policy would send A');
});
test('source P3 decision rejects foreign ownership and unsupported slot/type, and gates selected pickup by board',()=>{
 const o=structuredClone(actualCursorRows.observations.at(-1));
 for(const change of [s=>s.doors[20]=1,s=>s.doors[24]=3,s=>s.doors[26]=3,
  s=>s.cursors[8]=1,s=>s.cursors[9]=1,s=>s.cursors[9]=3,s=>{s.cursors[1]=1;s.cursors[2]=2;}]){
  const setup=syntheticP3Owner();change(setup);assert.throws(()=>sparseP3FighterDecision(o,setup));
 }
 const setup=syntheticP3Owner();setup.doors[22]=1;setup.doors[23]=8;o.ids[2]=8;
 assert.equal(sparseP3FighterDecision(o,setup).button,0,'Distance-only A below source board must be rejected');
 o.geometry[1]=.3;assert.equal(sparseP3FighterDecision(o,setup).button,1,'Actual original board and distance prerequisites admit pickup');
 o.ids[1]=1;assert.throws(()=>sparseP3FighterDecision(o,setup));
 o.ids[1]=2;setup.cursors[9]=1;setup.cursors[10]=2;
 assert.equal(sparseP3FighterDecision(o,setup).kind,'owned-puck-placement');
});
test('CSS-only final diagnostic gate requires CSS steps without inventing ordinary match coverage',()=>{
 const capture={status:'installed',invalid_phase_steps:0,invalid_preparation_count:0,unknown_reason_count:0,
  reason_counts:Array(10).fill(0),phase_source_steps:Array(15).fill(0),dropped_incidents:0};
 capture.phase_source_steps[1]=10;
 assert.equal(validateFinalSparseCapture(capture,{cssOnly:true}),10);
 assert.throws(()=>validateFinalSparseCapture(capture));capture.phase_source_steps[1]=0;
 assert.throws(()=>validateFinalSparseCapture(capture,{cssOnly:true}));
});

test('live sparse stock observation admits one loss at a time and canonical source2 winner only',()=>{
 let before=pair();
 for(let stocks=3;stocks>=0;stocks--){const after=structuredClone(before);after.frame++;after.players[0].stocks=stocks;
   assert.equal(checkSparseLoss(before,after),true);before=after;}
 before.terminal={outcome:2,winners:[2]};checkSparseTerminal(before);
 for(const terminal of [{outcome:2,winners:[1]},{outcome:1,winners:[2]},{outcome:2,winners:[0,2]}]){
   const wrong=structuredClone(before);wrong.terminal=terminal;assert.throws(()=>checkSparseTerminal(wrong));}
 const skipped=pair();skipped.frame++;skipped.players[0].stocks=2;assert.throws(()=>checkSparseLoss(pair(),skipped));
 const foreign=pair();foreign.frame++;foreign.players[1].stocks=3;assert.throws(()=>checkSparseLoss(pair(),foreign));
 const identity=pair();identity.frame++;identity.players[1].source_port=1;assert.throws(()=>checkSparseLoss(pair(),identity));
});
