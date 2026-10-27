// Actual comparators and launch wrapper; native/DOM leaves explicitly modeled.
// The Python RNG tool owns semantic event comparison, not these modeled rows.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {parseArgs} from 'node:util';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {derivePairedFull,PairedFullComparator,installPairedFullLaunch,firstFireOriginalValues,FIRST_FIRE_STATE_KEYS,validateFirstFireCompletion} from '../scripts/stadium_paired_v11_full.mjs';
import {typedOriginalValues} from './stadium_matched_setup_cf_consumer.mjs';
const projectionPath=process.env.MELEE_PAIRED_FULL_PROJECTION;
const endpointsPath=process.env.MELEE_PAIRED_FULL_ENDPOINTS;
if((projectionPath!==undefined)!==(endpointsPath!==undefined)||[projectionPath,endpointsPath].some(x=>x!==undefined&&!x))throw Error('Paired full retained fixtures require both nonempty paths');
function original(){assert.ok(projectionPath&&endpointsPath,'actual retained projection/endpoints required');const p=JSON.parse(fs.readFileSync(projectionPath)),ep=JSON.parse(fs.readFileSync(endpointsPath));return {p,ep,e:derivePairedFull(p,p.recipe_sha256,p.actual_frame_count,ep)};}
test('actual synchronous launch wrapper arms exactly once before forwarding',()=>{
 const harness=fs.readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
 const observerStart=harness.indexOf('function installPairedFullObserver('),observerEnd=harness.indexOf('\n}',observerStart);assert.ok(observerStart>=0&&observerEnd>observerStart);
 const runtime=fs.readFileSync(new URL('../web/runtime-development.mjs',import.meta.url),'utf8');
 const assignment='window.__meleeWebStadiumTextTailProbe=stadiumTextTailProbe;';
 const runtimeStart=runtime.indexOf('const STADIUM_TEXT_TAIL_MAX_RECORDS='),runtimeEnd=runtime.indexOf(assignment,runtimeStart)+assignment.length;assert.ok(runtimeStart>=0&&runtimeEnd>runtimeStart);
 const page={window:{__meleeWebStadiumTextTailProbe:{observe(){throw Error('stale init observer');}}},TextEncoder};
 vm.createContext(page);vm.runInContext(runtime.slice(runtimeStart,runtimeEnd),page); // Actual page assignment replaces the obsolete init observer.
 const probe=page.window.__meleeWebStadiumTextTailProbe;probe.enable();probe.observe('C1_SIS_TEXT_EVENT before');
 vm.runInContext(harness.slice(observerStart,observerEnd+2),page);page.installPairedFullObserver({maximumRows:2});
 probe.observe('C1_SIS_TEXT_EVENT after');const native='{"record":"stadium_paired_v11","order":0}';
 probe.observe(native);probe.observe(native.replace(':0',':1'));
 assert.equal(probe.totalRecords,2);assert.equal(probe.records.length,2);assert.equal(page.window.__pairedFullRows.length,2);assert.equal(page.window.__pairedFullRows[0],native);
 assert.throws(()=>probe.observe(native),/exceeds recipe bound/);assert.throws(()=>page.installPairedFullObserver({maximumRows:2}),/already owned/);
 const missing={window:{}};vm.createContext(missing);vm.runInContext(harness.slice(observerStart,observerEnd+2),missing);assert.throws(()=>missing.installPairedFullObserver({maximumRows:2}),/unavailable/);
 const calls=[],m={stadiumMatchedSetupNativeIdle:()=>true,_melee_web_native_menu_stadium_paired_v11_full_pair_arm:()=>{calls.push('arm');return 1;},_melee_web_native_menu_launch:()=>{calls.push('launch');return 17;}};
 const ctx={Module:m};vm.runInNewContext('('+installPairedFullLaunch.toString()+')()',ctx);assert.equal(m._melee_web_native_menu_launch(),17);assert.deepEqual(calls,['arm','launch']);assert.throws(()=>m._melee_web_native_menu_launch(),/one-use/);
 for(const mode of ['busy','refuse']){let launched=0;const bad={stadiumMatchedSetupNativeIdle:()=>mode!=='busy',_melee_web_native_menu_stadium_paired_v11_full_pair_arm:()=>0,_melee_web_native_menu_launch:()=>launched++,_melee_web_native_menu_message:()=>1,UTF8ToString:()=> 'actual refusal'};vm.runInNewContext('('+installPairedFullLaunch.toString()+')()',{Module:bad});assert.throws(()=>bad._melee_web_native_menu_launch(),/settled|actual refusal/);assert.equal(launched,0);}
});
test('actual retained projection and eight original endpoints require exact roles',{skip:!projectionPath||!endpointsPath},()=>{
 const {p,ep}=original();for(const mutate of [x=>x.recipe_sha256='0'.repeat(64),x=>x.matches.reverse(),x=>x.matches[0].actions[0].action_index++,x=>x.matches[0].setup.declared_setup.players[0].player_type=1]){const q=structuredClone(p);mutate(q);assert.throws(()=>derivePairedFull(q,p.recipe_sha256,p.actual_frame_count,ep));}
 const bad=structuredClone(ep);bad.native.original_rng_scene_boundaries.samples[2].phase='return';assert.throws(()=>derivePairedFull(p,p.recipe_sha256,p.actual_frame_count,bad));
});
function modeledTrace(e,mutate=()=>{},mutateProbe=()=>{}){
 const c=new PairedFullComparator(e);let order=0;
 function emit(i,kind,payload,coords={}){const row={record:'stadium_paired_v11',schema:'melee-web-stadium-paired-v11-row-v1',order:order++,kind,match_index:i,world_generation:3+4*i,input_cursor:0,diagnostic_action_index:c.action,local_action_index:c.matches[i].actions,source_tick:0,draw_ordinal:0,payload,...coords};mutate(row);c.accept(row);}
 for(const [i,item]of e.matches.entries()){
  const r=item.reference,g=3+4*i,x=r.constructor.state_projection;

  function endpoint(n){emit(i,'scene_rng',{schema:'melee-web-stadium-paired-scene-rng-point-v1',phase:['partitioned_entry_before','partitioned_entry_return','original_vs_exit_before','original_vs_exit_return'][n],source_boundary_ordinal:n,world_generation:g,seed_pointer_hex:'0x12340000',seed_hex:item.endpoints[n].value.toString(16).padStart(8,'0'),original_callback_dispatch:n>=2});}
  endpoint(0);endpoint(1);
  emit(i,'setup',{schema:'melee-web-stadium-paired-v11-setup-v1',match_index:i,input_cursor:r.actions[0].frame_index,seed_hex:r.setup.rng.value_hex,world_generation:g,world_ticks:0,source_frame:0,constructor:{scope:'shared-constructor-aggregate-only',entry_count:x.constructor_entry_count,parameter_roles:{min_parameter:'parameter_x0',max_parameter:'parameter_x4'},min_parameter:x.parameter_x0,max_parameter:x.parameter_x4,random_bound:x.random_bound,random_result:x.random_result,seed_before:x.seed_before_hex,seed_after:x.seed_after_hex,randi_call_count:1}});
  let closed=false;
  for(const a of r.actions){
   const range=a.selected_rng_event_ordinals,all=a.rng_event_ordinals,count=range&&all&&range[0]===all[0]&&range[1]===all[1]?range[1]-range[0]:0;
   const zero='0000000000000000';const draws=Array.from({length:count},()=>({kind:'HSD_Rand',seed_after:'00000000',owner:{status:'outside_proc',gobj:zero,proc:zero,callback:zero,callback_symbol:null,classifier:0,p_link:0,s_link:0,fighter:null}}));
   const probe={schema:'melee-web-rng-draw-probe',version:3,source_cursor:c.action,overflowed:false,draws};
   mutateProbe(probe,a,r,i);c.acceptProbe(JSON.stringify(probe));
   const coords={source_tick:a.source_tick,draw_ordinal:a.draw_ordinal,input_cursor:a.frame_index??0};
   emit(i,'action_begin',{action:a.kind,seed_before_hex:'00000000',source_frame_before:0},coords);
   emit(i,'action_end',{action:a.kind,input_cursor:a.frame_index??0,seed_after_hex:'00000000',source_frame_after:0},coords);
   if(a.kind==='tick'&&a.source_tick===r.window.go_tick)emit(i,'go',{generation:g,world_ticks:a.source_tick,sequence:3,source_branch:0,map2_gate:Number(r.go.state.map2.gate_mask_80),display_mode:r.go.state.display.mode_s16,source_seed_valid:1,source_seed_hex:r.go.rng.value_hex});
   const state=item.states.get(a.source_tick);
   if(a.kind==='tick'&&state&&!closed){const t=state.tick.source_tick;emit(i,'state',{ordinal:c.matches[i].state,selected:state.selected,typed22:{...typedOriginalValues(state.state,t),schema:'melee-web-stadium-per-tick-common-fields-native-v3',world_generation:g,source_tick:t,world_ticks:t+1,seed_hex:state.rng.value_hex},pad_queue:{schema:'melee-web-stadium-hsd-pad-queue-observation',version:2,source_generation:g,source_tick_before:t,source_tick_after:t+1,qnum_before:1,qread_before:0,qcount_before:1,qcount_after_hsd:0,tick_complete:true,queue_slot_status_hex:state.pads}});}
   if(a.kind==='draw'&&!closed&&a.source_tick===r.window.close.source_tick&&a.draw_ordinal===r.window.close.draw_ordinal){emit(i,'window_closed',{state_samples:3501,natural_tail_ticks:r.window.natural_tail_ticks,close_source_tick:a.source_tick,draw_ordinal:a.draw_ordinal});closed=true;}
  }
  emit(i,'terminal',{});endpoint(2);endpoint(3);emit(i,'retired',{native_world_absent:true,parent_seed_restored:true,handoff_seed_hex:'00000000',restored_parent_seed_hex:'00000000'});emit(i,'results_request',{typed_results_requested:true});emit(i,'results',{typed_results_entered:true,world_generation:g+1,entry_seed_hex:'00000000',seed_hex:'00000000'});emit(i,'css',{original_css_entered:true});
 }
 emit(1,'complete',{whole_session_input_complete:true,C3:false});return c.finish({schema:'melee-web-stadium-paired-v11-full-pair-v1',full_pair:true,whole_session_complete:true,failed:false,diagnostic_actions:e.total,lifetimes:e.matches.map((x,i)=>({match_index:i,world_generation:3+4*i,actions:x.reference.actions.length,ticks:x.reference.actions.filter(a=>a.kind==='tick').length,draws:x.reference.actions.filter(a=>a.kind==='draw').length,state_samples:3501,natural_tail_ticks:x.reference.window.natural_tail_ticks,window_closed:true,retired:true,results_requested:true,results_entered:true,css_returned:true,scene_rng_count:4}))});
}
test('actual two-window state/action comparator accepts modeled native leaves over all retained coordinates',{skip:!projectionPath||!endpointsPath},()=>{const {e}=original();const result=modeledTrace(e);assert.equal(result.pass,true);assert.equal(result.actions,e.total);assert.equal(result.lifetimes.length,2);assert.equal(result.RNG_semantics_requires_separate_python_comparison,true);});
test('source bracket distinguishes empty closing guards from fully observed empty actions',{skip:!projectionPath||!endpointsPath},()=>{
 const {e}=original();const zero='0000000000000000';
 const event={kind:'HSD_Rand',seed_after:'00000000',owner:{status:'outside_proc',gobj:zero,proc:zero,callback:zero,callback_symbol:null,classifier:0,p_link:0,s_link:0,fighter:null}};
 let guards=0;
 const result=modeledTrace(e,()=>{},(probe,a,r)=>{
  const range=a.selected_rng_event_ordinals,all=a.rng_event_ordinals;
  if(a.through_sequence>r.window.target_source_tick.sequence&&range&&all&&range[0]===range[1]&&range[0]===all[0]&&range[1]===all[1]){probe.draws.push(structuredClone(event));guards++;}
 });
 assert.ok(guards>0);assert.equal(result.pass,true);assert.equal(result.lifetimes.reduce((n,m)=>n+m.unpairedNativeEvents,0),guards);
 let changed=false;
 assert.throws(()=>modeledTrace(e,()=>{},(probe,a,r)=>{if(!changed&&a.after_sequence>=r.states[0].tick.sequence&&a.through_sequence<=r.window.target_source_tick.sequence&&probe.draws.length===0){probe.draws.push(structuredClone(event));changed=true;}}),/selected action RNG count/);
 assert.equal(changed,true);
});
test('actual comparator stops altered Setup, endpoint, GO, PAD and owner-generation fields',{skip:!projectionPath||!endpointsPath},()=>{const {e}=original();for(const target of ['setup','scene_rng','go','state','action_begin']){let changed=false;assert.throws(()=>modeledTrace(e,row=>{if(changed||row.kind!==target)return;changed=true;if(target==='setup')row.payload.seed_hex='00000000';else if(target==='scene_rng')row.payload.seed_hex='00000000';else if(target==='go')row.payload.source_seed_hex='00000000';else if(target==='state')row.payload.pad_queue.queue_slot_status_hex=['ff'.repeat(11),...row.payload.pad_queue.queue_slot_status_hex.slice(1)];else row.world_generation++;}));assert.equal(changed,true);}});
test('actual full harness phase waits for ordinary completion retirement without a second unload',async()=>{
 const source=fs.readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
 const start=source.indexOf('async function pairedFullPhase(){'),end=source.indexOf('\n}',start);assert.ok(start>=0&&end>start);
 const clicks=[],writes=[];let drains=0,observations=0;
 const ctx={Date,Promise,setTimeout,replayTimeoutMs:1000,replayPollMs:1,output:'/owned',pairedRegistration:false,
  fs:{writeFile:async p=>writes.push(p)},path:{join:(...p)=>p.join('/')},installPairedFullLaunch:()=>{},installPairedFullObserver:()=>{},write:async()=>{},
  page:{evaluate:async()=>true,locator:label=>({click:async()=>clicks.push(label)})},
  report:{paired_v11_full_pair:{}},pageErrors:[],drainPairedFull:async()=>drains++,
  readPairedFirstGoStatus:async()=>++observations===1?{busy:false,status:{schema:'melee-web-stadium-paired-v11-full-pair-v1',whole_session_complete:true,failed:false}}:{busy:false,status:{native_retired:true},phase:0,running:0,runtime:{replay_report:{complete:true,pass:true,failures:[]}}},
  pairedFullExpected:{total:14394},
  pairedFullComparison:{finish:()=>({pass:true})},runPairedFullRngComparison:()=>({status:'PASS_SELECTED_RNG_COMPARISON'})};
 vm.createContext(ctx);vm.runInContext(source.slice(start,end+2),ctx);await ctx.pairedFullPhase();
 assert.deepEqual(clicks,['#retail-replay-start']);assert.equal(drains,2);assert.equal(writes.length,2);assert.equal(ctx.report.paired_v11_full_pair.component_pass,true);assert.equal(ctx.report.rng_draw_probe.complete,true);assert.equal(ctx.report.rng_draw_probe.captured_action_count,14394);
 assert.deepEqual(ctx.report.browser_report,{complete:true,pass:true,failures:[]});
 const bad={...ctx,report:{paired_v11_full_pair:{}},readPairedFirstGoStatus:async()=>({busy:true,runtime:{terminal_error:'actual native refusal'}})};vm.createContext(bad);vm.runInContext(source.slice(start,end+2),bad);await assert.rejects(bad.pairedFullPhase(),/actual native refusal/);assert.notEqual(bad.report.paired_v11_full_pair.component_pass,true);
 let semanticReads=0,semanticRetired=false;
 const semanticFailure={...ctx,report:{paired_v11_full_pair:{},rng_draw_probe:{complete:false}},
  runPairedFullRngComparison:()=>{assert.equal(semanticRetired,true,'RNG comparison follows checked native retirement');throw Error('actual semantic RNG mismatch');},
  readPairedFirstGoStatus:async()=>{
   if(++semanticReads===1)return {busy:false,status:{schema:'melee-web-stadium-paired-v11-full-pair-v1',whole_session_complete:true,failed:false}};
   semanticRetired=true;
   return {busy:false,status:{native_retired:true},phase:0,running:0,runtime:{replay_report:{complete:true,pass:true,failures:[]}}};
  }};
 vm.createContext(semanticFailure);vm.runInContext(source.slice(start,end+2),semanticFailure);await assert.rejects(semanticFailure.pairedFullPhase(),/actual semantic RNG mismatch/);assert.equal(semanticRetired,true);assert.equal(semanticFailure.report.rng_draw_probe.complete,true);assert.equal(semanticFailure.report.rng_draw_probe.captured_action_count,14394);assert.notEqual(semanticFailure.report.paired_v11_full_pair.component_pass,true);assert.equal(semanticFailure.report.paired_v11_full_pair.rng_comparison,undefined);

});

test('actual complete CLI prelude admits only the bounded full artifact route',async()=>{
 const source=fs.readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
 const start=source.indexOf('const {values, tokens} = parseArgs('),end=source.indexOf('await fs.mkdir(output, {recursive: false});',start);assert.ok(start>=0&&end>start);
 const prelude=source.slice(start,end).replaceAll('import.meta.url','sourceUrl');
 const base=['--url','http://127.0.0.1:8787/runtime.html','--disc','/disc','--recipe','/recipe','--out','/new',
 '--runtime-incidents','--artifact-root','/artifacts','--runtime-data','/data','--phase-timeout','150000','--replay-timeout','150000',
 '--stadium-paired-v11-full-pair-projection','/projection','--stadium-paired-v11-full-pair-projection-sha256','a'.repeat(64),
 '--stadium-paired-v11-full-pair-endpoints','/endpoints','--stadium-paired-v11-full-pair-endpoints-sha256','b'.repeat(64)];
 async function run(args){const ctx={URL,path,fileURLToPath,sourceUrl:new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url).href,
 parseArgs:o=>parseArgs({...o,args}),resolveCaptureMode:mode=>({mode,diagnostic:false}),incidentRecordingUrl:s=>new URL(s),
 parseRngDrawProbe:()=>null,parseHitTransitionProbe:()=>null,NATURAL_PAUSE_PROTOCOL:{},STOPPED_SCENE_PAIR_PROTOCOL:{},OBSERVED_B_REFERENCE:{id:'observed'},LONG_B_REFERENCE:{id:'long'}};
 vm.createContext(ctx);return vm.runInContext('(async()=>{'+prelude+';return {pairedFull,inputPaths,artifactRoot};})()',ctx);}
 // Execute the actual generic guard and following full-mode action admission.
 const guardStart=source.indexOf('  if (rngDrawProbe && rngDrawProbe.selected.some(cursor => cursor >= report.recipe_header.frames))');
 const guardEnd=source.indexOf('  if (captureCpuObservations',guardStart);
 assert.ok(guardStart>=0&&guardEnd>guardStart);
 const headerGuard=source.slice(guardStart,guardEnd);
 const actionSelection={request:{kind:'range',first:0,last:14393},selected:Array.from({length:14394},(_,i)=>i)};
 const full={pairedFull:true,pairedFullExpected:{total:14394,selection:actionSelection},rngDrawProbe:null,report:{recipe_header:{frames:8154}}};vm.createContext(full);vm.runInContext(headerGuard,full);
 assert.equal(full.rngDrawProbe.selected.length,14394);assert.equal(full.report.rng_draw_probe.requested_action_count,14394);assert.equal(full.report.rng_draw_probe.complete,false);assert.equal(full.report.rng_draw_probe.scope,'paired-v11-global-diagnostic-action-index');
 for(const cursor of [8153,8154]){const ordinary={pairedFull:false,rngDrawProbe:{selected:[cursor]},report:{recipe_header:{frames:8154}}};vm.createContext(ordinary);if(cursor===8153)vm.runInContext(headerGuard,ordinary);else assert.throws(()=>vm.runInContext(headerGuard,ordinary),/inside the source recipe frame count/);}
 const admitted=await run(base);assert.equal(admitted.pairedFull,true);assert.equal(admitted.artifactRoot,'/artifacts');assert.ok(admitted.inputPaths.includes('/projection')&&admitted.inputPaths.includes('/endpoints'));
 await run([...base,'--stadium-paired-v11-full-pair-registration']);
 await assert.rejects(run([...base.filter((x,i)=>x!=='--stadium-paired-v11-full-pair-projection'&&base[i-1]!=='--stadium-paired-v11-full-pair-projection'),'--stadium-paired-v11-full-pair-registration']),/Paired full requires/);
 for(const flag of ['--artifact-root','--runtime-data','--stadium-paired-v11-full-pair-endpoints']){const args=[...base],i=args.indexOf(flag);args.splice(i,2);await assert.rejects(run(args),/Paired full requires/);}
 for(const flag of ['--phase-timeout','--replay-timeout']){const args=[...base];args[args.indexOf(flag)+1]='150001';await assert.rejects(run(args),/Paired full requires/);}
 for(const more of [['--expected-frame0','/frame0'],['--stadium-paired-v11-first-go-projection','/first'],['--stadium-later-sss-exit']])await assert.rejects(run([...base,...more]),/Paired full requires/);
});

test('first-fire launch uses only explicit bounded request callback and settled operands',()=>{
 const calls=[],m={stadiumMatchedSetupNativeIdle:()=>true,
  _melee_web_native_menu_stadium_paired_v11_first_fire_pair_arm:(...args)=>{calls.push(args);return 1;},
  _melee_web_native_menu_launch:()=>19};
 vm.runInNewContext('('+installPairedFullLaunch.toString()+')({firstFire:true,requestSourceTick:3853,callbackSourceTick:3864,lastSelectedSourceTick:4470})',{Module:m});
 assert.equal(m._melee_web_native_menu_launch(),19);assert.deepEqual(calls,[[3853,3864,4470]]);
 for(const args of [{requestSourceTick:3853,callbackSourceTick:3853,lastSelectedSourceTick:4470},
 {requestSourceTick:3853,callbackSourceTick:3864,lastSelectedSourceTick:0x100000000}])
  assert.throws(()=>installPairedFullLaunch({firstFire:true,...args}),/coordinates invalid/);
});
test('first-fire common field adapter checks actual pending bit without promoting old relations',()=>{
 // Explicit modeled source leaf, not a captured World or parity result.
 const fields={display:{flags_s16:0,focused_player_s16:0,mode_s16:1,packed_flags_raw_hex:'00',previous_flags_s16:0,previous_mode_s16:0,selection_counter_s16:0,timer_initialization_observed:false,timer_raw_hex:'00000000',timer_representation:'unpaired',xF0_observed:false},
 map2:{countdown_s32:0,form_s16:3,gate_mask_40:true,gate_mask_80:false,gate_raw_hex:'40',map5_root_pointer_equals_fixed_map5_slot_value:false,phase_s16:1,previous_form_s16:-1,prior_form_s16:-1},
 draw_ordinal:0,guest_pointer_policy:'unpaired',label:'modeled',map_root_allocation_lifetime_proved:false,map_root_object_type_observed:false,map_root_roles:[0,1,2,3].map(fixed_slot_map_id=>({fixed_slot_map_id,root_pointer_range_valid:true,ground_pointer_range_valid:true})),sequence:1,source_tick:0,title:{}};
 const values=firstFireOriginalValues(fields,0);assert.deepEqual(Object.keys(values),FIRST_FIRE_STATE_KEYS);assert.equal(values.map2_pending,true);
 assert.equal(Object.keys(values).length,14);assert.equal('map5_root_relation' in values,false);
 const initial=structuredClone(fields);initial.map2.form_s16=5;
 initial.map2.map5_root_pointer_equals_fixed_map5_slot_value=true;
 initial.map_root_roles[3].fixed_slot_map_id=5;assert.equal(firstFireOriginalValues(initial,0).map2_form,5);
 const pending=structuredClone(initial);pending.map2.form_s16=3;
 assert.equal(firstFireOriginalValues(pending,0).map2_form,3); // Selected form precedes source root replacement.
 for(const mutate of [x=>x.map_root_roles[3].fixed_slot_map_id=4,
  x=>x.map_root_roles[0].fixed_slot_map_id=1,
  x=>x.map_root_roles[3].ground_pointer_range_valid=false,
  x=>x.map2.map5_root_pointer_equals_fixed_map5_slot_value=true,
  x=>x.map2.form_s16=5]){const bad=structuredClone(fields);mutate(bad);assert.throws(()=>firstFireOriginalValues(bad,0));}

 for(const change of [x=>x.map2.gate_mask_40=false,x=>delete x.map2.gate_mask_40]){const bad=structuredClone(fields);change(bad);assert.throws(()=>firstFireOriginalValues(bad,0));}
});

test('first-fire sealed completion validates actual cursor receipt independently of graph',()=>{
 const f={request:{source_tick:3853},callback:{source_tick:3864},settled:{source_tick:4470}};
 const c={schema:'melee-web-stadium-first-fire-recorded-completion-v1',armed:1,failed:0,completed:1,request_seen:1,local_request_id:7,actual_request_cursor:3853,actual_callback_cursor:3864,last_preconsume_cursor:4490};
 assert.equal(validateFirstFireCompletion(c,f),true);
 for(const change of [x=>x.armed=0,x=>x.failed=1,x=>x.completed=0,x=>x.request_seen=0,x=>x.local_request_id=0,x=>x.actual_request_cursor++,x=>x.actual_callback_cursor--,x=>x.last_preconsume_cursor=4469]){const bad={...c};change(bad);assert.throws(()=>validateFirstFireCompletion(bad,f));}
});
