// Actual comparators and launch wrapper; native/DOM leaves explicitly modeled.
// The Python RNG tool owns semantic event comparison, not these modeled rows.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {parseArgs} from 'node:util';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {derivePairedFull,PairedFullComparator,installPairedFullLaunch,firstFireOriginalValues,FIRST_FIRE_STATE_KEYS,validateFirstFireCompletion,firstWaterOriginalValues,FIRST_WATER_STATE_KEYS,validateFirstWaterCompletion,validateFirstWaterLocalGraph,validateFirstWaterCollisionQuery} from '../scripts/stadium_paired_v11_full.mjs';
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

// Explicit modeled source leaves. No original Water recording is manufactured.
function modeledWaterFields(form=9,active=form){return {
 display:{flags_s16:0,focused_player_s16:0,mode_s16:1,packed_flags_raw_hex:'00',previous_flags_s16:0,previous_mode_s16:0,selection_counter_s16:0,timer_initialization_observed:false,timer_raw_hex:'00000000',timer_representation:'unpaired',xF0_observed:false},
 map2:{countdown_s32:0,form_s16:form,gate_mask_40:false,gate_mask_80:false,gate_raw_hex:'00',map5_root_pointer_equals_fixed_map5_slot_value:active===5,phase_s16:0,previous_form_s16:5,prior_form_s16:-1},
 draw_ordinal:0,guest_pointer_policy:'unpaired',label:'modeled-only',map_root_allocation_lifetime_proved:false,map_root_object_type_observed:false,map_root_roles:[0,1,2,active].map(fixed_slot_map_id=>({fixed_slot_map_id,root_pointer_range_valid:true,ground_pointer_range_valid:true})),sequence:1,source_tick:0,title:{}};}
test('first-water launch preserves distinct arm and rejects mixed selectors',()=>{
 const calls=[],m={stadiumMatchedSetupNativeIdle:()=>true,
 _melee_web_native_menu_stadium_paired_v11_first_water_pair_arm:(...args)=>{calls.push(args);return 1;},_melee_web_native_menu_launch:()=>23};
 vm.runInNewContext('('+installPairedFullLaunch.toString()+')({firstWater:true,requestSourceTick:100,callbackSourceTick:111,lastSelectedSourceTick:200})',{Module:m});
 assert.equal(m._melee_web_native_menu_launch(),23);assert.deepEqual(calls,[[100,111,200]]);assert.equal(m.__pairedFullLaunch.waterCollisionProbe,false);
 const queryModule={stadiumMatchedSetupNativeIdle:()=>true,
  _melee_web_native_menu_stadium_paired_v11_first_water_pair_arm:(...args)=>{calls.push(args);return 1;},_melee_web_native_menu_launch:()=>24};
 vm.runInNewContext('('+installPairedFullLaunch.toString()+')({firstWater:true,waterCollisionProbe:true,requestSourceTick:100,callbackSourceTick:111,lastSelectedSourceTick:200})',{Module:queryModule});
 assert.equal(queryModule._melee_web_native_menu_launch(),24);assert.equal(queryModule.__pairedFullLaunch.waterCollisionProbe,true);
 assert.throws(()=>installPairedFullLaunch({firstWater:true,firstFire:true}),/explicit/);
 assert.throws(()=>installPairedFullLaunch({firstWater:true,waterCollisionProbe:'true'}),/explicit/);
 assert.throws(()=>installPairedFullLaunch({firstFire:true,waterCollisionProbe:true}),/explicit/);
 for(const args of [{requestSourceTick:100,callbackSourceTick:100,lastSelectedSourceTick:200},{requestSourceTick:100,callbackSourceTick:111,lastSelectedSourceTick:0x100000000}])assert.throws(()=>installPairedFullLaunch({firstWater:true,...args}),/coordinates invalid/);
});
test('first-water actual role adapter never relabels Fire or stable owners',()=>{
 const fields=modeledWaterFields();assert.deepEqual(Object.keys(firstWaterOriginalValues(fields,0)),FIRST_WATER_STATE_KEYS);
 assert.equal(firstWaterOriginalValues(modeledWaterFields(5,5),0).map2_form,5);
 assert.equal(firstWaterOriginalValues(modeledWaterFields(9,5),0).map2_form,9);
 assert.throws(()=>firstFireOriginalValues(fields,0));
 assert.throws(()=>firstWaterOriginalValues(modeledWaterFields(3),0));
 for(const mutate of [x=>x.map_root_roles[0].fixed_slot_map_id=1,x=>x.map_root_roles[3].fixed_slot_map_id=7,x=>x.map2.map5_root_pointer_equals_fixed_map5_slot_value=true,x=>x.map2.gate_mask_40=true]){const bad=structuredClone(fields);mutate(bad);assert.throws(()=>firstWaterOriginalValues(bad,0));}
});
test('first-water eleven event graph and separate completion refuse missing reordered or Fire receipts',()=>{
 const events=[[1,9],[1,7],[1,8],[2,5],[3,5],[2,9],[2,7],[3,7],[2,8],[3,8],[3,9]].map(([kind,map_id])=>({kind,map_id}));
 const g={schema:'melee-web-stadium-first-water-native-graph-v1',selected_form:9,world_generation:7,armed:2,failed:false,scalar_scope:'last_live_validated_sample',scalar_sample_valid:true,scalar_world_ticks:200,event_count:11,events,phase:0,form:9,buffer_uses:2};
 validateFirstWaterLocalGraph(g,7,true);
 const live={...g,armed:1,event_count:5,events:events.slice(0,5)};validateFirstWaterLocalGraph(live,7);
 for(const mutate of [x=>x.selected_form=3,x=>x.form=3,x=>x.event_count=5,x=>x.events[6].map_id=8,x=>x.buffer_uses=0,x=>x.world_generation=8]){const bad=structuredClone(g);mutate(bad);assert.throws(()=>validateFirstWaterLocalGraph(bad,7,true));}
 const f={request:{source_tick:100},callback:{source_tick:111},settled:{source_tick:200}},c={schema:'melee-web-stadium-first-water-recorded-completion-v1',armed:1,failed:0,completed:1,request_seen:1,local_request_id:17,actual_request_cursor:100,actual_callback_cursor:111,last_preconsume_cursor:200};
 validateFirstWaterCompletion(c,f);
 for(const mutate of [x=>x.schema='melee-web-stadium-first-fire-recorded-completion-v1',x=>x.failed=1,x=>x.completed=0,x=>x.actual_request_cursor++,x=>x.actual_callback_cursor--,x=>x.last_preconsume_cursor--]){const bad=structuredClone(c);mutate(bad);assert.throws(()=>validateFirstWaterCompletion(bad,f));}
});
function waterCollisionQueryFixture(generation=7,sourceTick=20){
 const bits=value=>{const view=new DataView(new ArrayBuffer(4));view.setFloat32(0,value,true);return '0x'+view.getUint32(0,true).toString(16).padStart(8,'0');};
 const bits3=values=>values.map(bits);
 const state={world_generation:generation,world_ticks:sourceTick+1,source_frame:12,random_seed:1234,journal_armed:1,selected_form:9,journal_failed:0,event_count:5,map2_gate:0,countdown:0,phase:0,form:9,history_e0:0,history_e2:0,pending:0,display_mode:0,display_e6:0,display_e8:0,display_previous_mode:0,display_selected_player:0,display_f2:0,buffer_uses:2,display_flags:0,scalar_sample_valid:1};
 const readiness={vertices:8,lines:4,joints:3,floor_islands:1,ceiling_islands:0,empty_lines:0,storage_owned:1,original_indices_initialized:1,stage_joint_bindings_ready:1,stage_callbacks_ready:1};
 const floor=(line,x)=>({line,x,x_bits:bits(x),y:0,y_bits:bits(0),displacement_y:0,displacement_y_bits:bits(0),normal:[0,1,0],normal_bits:bits3([0,1,0]),material_flags:0});
 return {schema:'melee-web-stadium-first-water-collision-query-native-v1',world_generation:generation,source_tick:sourceTick,
  state_before:{...state},state_after:{...state},readiness_before:{...readiness},readiness_after:{...readiness},
  line_index:2,chain:{min_x:-2,min_x_bits:bits(-2),max_x:3,max_x_bits:bits(3)},line:{v0:[0,-0,0],v0_bits:bits3([0,-0,0]),v1:[1,0,0],v1_bits:bits3([1,0,0]),normal:[0,1,0],normal_bits:bits3([0,1,0]),kind:1,material_flags:0,runtime_flags:0x10001,previous:1,next:3,joint:0,pruned_hi_flags:0,has_normal:1},
  queries:{hit:floor(2,0.5),left_miss:floor(-1,-3),right_miss:floor(-1,4)}};
}
test('first-water collision query is one scoped post-window component with exact pointer-free hit/miss data',()=>{
 const q=waterCollisionQueryFixture();assert.equal(validateFirstWaterCollisionQuery(q,7,20),true);
 assert.ok(Object.is(q.line.v0[1],-0));assert.equal(q.line.v0_bits[1],'0x80000000');
 for(const mutate of [x=>x.source_tick++,x=>x.world_generation++,x=>x.readiness_before.stage_callbacks_ready=0,
  x=>x.state_after.random_seed++,x=>x.readiness_after.lines++,x=>x.line.joint=1,x=>x.line.runtime_flags=1,x=>x.queries.hit.line=-1,
  x=>x.queries.left_miss.line=0,x=>x.queries.right_miss.x=x.chain.max_x,
  x=>x.chain.min_x+=Number.EPSILON,
  x=>x.line.v0_bits[1]='0x00000000',
  x=>x.pointer='0x1234']){const bad=structuredClone(q);mutate(bad);assert.throws(()=>validateFirstWaterCollisionQuery(bad,7,20));}
 const e={firstWater:true,waterCollisionProbe:true,projection:{first_water:{settled:{source_tick:20}}},total:3,
  matches:[{reference:{actions:[],states:[]}},{reference:{actions:[{}],states:[{}]}}]};
 const c=new PairedFullComparator(e);c.matches[0].css=true;c.matches[1].setup=true;c.matches[1].generation=7;c.matches[1].go=true;c.matches[1].state=1;c.matches[1].actions=3;c.matches[1].lastActionKind='tick';c.matches[1].lastActionSourceTick=20;c.action=3;
 const row={record:'stadium_paired_v11',schema:'melee-web-stadium-paired-v11-row-v1',order:0,kind:'water_collision_query',match_index:1,world_generation:7,input_cursor:44,diagnostic_action_index:3,local_action_index:3,source_tick:21,draw_ordinal:0,payload:q};
 c.accept(row);assert.equal(c.matches[1].waterQuery,true);
 const duplicate={...row,order:1};assert.throws(()=>c.accept(duplicate),/Water collision query/);
 const unrequested=new PairedFullComparator({...e,waterCollisionProbe:false});unrequested.matches[0].css=true;unrequested.matches[1].setup=true;unrequested.matches[1].generation=7;unrequested.matches[1].go=true;unrequested.matches[1].state=1;unrequested.matches[1].actions=3;unrequested.matches[1].lastActionKind='tick';unrequested.matches[1].lastActionSourceTick=20;unrequested.action=3;
 assert.throws(()=>unrequested.accept(row),/explicitly requested/);
 const wrongProfile=new PairedFullComparator({...e,firstWater:false});wrongProfile.matches[0].css=true;wrongProfile.matches[1].setup=true;wrongProfile.matches[1].generation=7;wrongProfile.matches[1].go=true;wrongProfile.matches[1].state=1;wrongProfile.matches[1].actions=3;wrongProfile.matches[1].lastActionKind='tick';wrongProfile.matches[1].lastActionSourceTick=20;wrongProfile.action=3;
 assert.throws(()=>wrongProfile.accept(row),/Water collision query/);
});
function modeledWaterProjection(){
 const sha='1'.repeat(64),request={kind:'request_enter',source_tick:3},callback={kind:'callback_enter',source_tick:4},poll={kind:'poll_return',result:1,source_tick:4};
 const settled={selected_form:9,phase:0,active_map_id:9,root_replacement_observed:true,source_tick:5};
 const points={observed:true,sample_count:8,samples:Array.from({length:8},(_,i)=>({lifetime:Math.floor(i/4)+1,function:i%4<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit',phase:i%2?'return':'enter',value:17}))};
 const setup={declared_setup:{stage:3,players:[0,1].map(()=>({character_kind:8,player_type:0,stocks:4}))}};
 const actions=Array.from({length:4},(_,i)=>({action_index:i,kind:'tick',source_tick:i+2,draw_ordinal:0,frame_index:i,after_sequence:2*i+1,through_sequence:2*i+2}));
 const p={schema:'melee-web-stadium-first-water-pair-projection-v1',profile:'selected_b_first_water_recorded_callback_v1',source_schedule_verified:true,recipe_sha256:sha,actual_frame_count:6,claims:{per_owner_rng_comparison:false,C4_complete:false},original_route:'whole3',runtime_view:'first_two_complete_lifetimes',source_files:{'native-primary.mwro':{sha256:sha}},first_water:{selected_form:9,request,callback,successful_poll:poll,settled},matches:[{match_index:0,actions:actions.slice(0,1),states:[],window:{state_samples:0,natural_tail_ticks:0},setup,go:null,constructor:null},{match_index:1,actions,states:actions.map(a=>({selected:true,tick:{source_tick:a.source_tick},state:modeledWaterFields()})),window:{state_samples:4,natural_tail_ticks:0,go_tick:2,last_selected_source_tick:5},setup,go:{},constructor:{}}]};
 const report={result:'pass',scope:'original-stadium-whole3-first-form-latency-replay-v1',cleanup_errors:[],native:{primary_sha256:sha,original_rng_scene_boundaries:points},stadium_selected_request_timing:{schema:'melee-web-stadium-selected-request-source-timing-summary-v1',profile:'first_selected_stage_request_v1',identity:{selected_form:9},drive_model_observed:false,C4_complete:false,rows:[request,callback,poll]},stadium_whole_first_form_latency_replay:{schema:'melee-web-stadium-first-form-latency-replay-comparison-v1',comparison_result:'PASS_EXACT_INPUT_AND_FIRST_FORM_SAMPLE',input_terminal_checks:'PASS',summary_equal:true,actual_summary:{first_form_latency:{settled_phase0:settled}},sequence_correspondence:{original_rows:2,actual_rows:3,timing_rows:1,original_to_actual:[[0,0],[1,2]],timing_actual_sequences:[1],raw_rows_modified:false}}};
 return {p,report,sha};
}
test('first-water exact replay metadata admission rejects pending terminal and rewritten source coordinates',()=>{
 const {p,report,sha}=modeledWaterProjection();assert.equal(derivePairedFull(p,sha,6,report).firstWater,true);
 for(const mutate of [x=>x.stadium_whole_first_form_latency_replay.summary_equal=false,x=>x.stadium_whole_first_form_latency_replay.input_terminal_checks='PENDING',x=>x.stadium_whole_first_form_latency_replay.sequence_correspondence.raw_rows_modified=true,x=>x.stadium_selected_request_timing.identity.selected_form=3,x=>x.scope='original-stadium-whole3-first-form-latency-record-v2']){const bad=structuredClone(report);mutate(bad);assert.throws(()=>derivePairedFull(p,sha,6,bad));}
});
