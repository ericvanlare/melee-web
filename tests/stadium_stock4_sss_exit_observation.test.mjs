import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import {installCurrentSssExitReplayArm, continuePreparedStartInPage}
  from '../scripts/stadium_current_sss_exit_observation.mjs';
import {validateStock4SssExitExpected, validateStock4SssExitStop,
  compareStock4PreparedStartProjection, compareStock4PreparedStartReceipt,
  compareStock4PreparedStartObservation, compareStock4PreparedSetupObservation,
  validateStock4SssExitTerminal, validateStock4SssExitFinalization,
  validateStock4PreparedStartFinalization, validateStock4PreparedSetupFinalization}
  from '../scripts/stadium_stock4_sss_exit_observation.mjs';

// Synthetic shapes exercise refusal/classification only; no original result.
function expected() {
  const raw = '00'.repeat(312), digest = createHash('sha256').update(Buffer.from(raw, 'hex')).digest('hex');
  const declared = {match_kind:1,is_stock:true,stage:3,item_frequency:-1,time_limit_seconds:0,
    timer_enabled:false,timer_counts_up:false,disable_pausing:false,is_teams:false,
    item_mask_hex:'ffffffffffffffff',damage_ratio_bits:'3f800000',game_speed_bits:'3f800000',
    players:[1,0].map((costume,i)=>({port:i+1,character_kind:8,costume,stocks:4,player_type:0,rumble_enabled:true}))};
  const endpoint = (sequence,boundary) => ({sequence,boundary,source_tick:125,draw_ordinal:0,pc:1,
    match_index:0,start_melee_hex:raw,start_melee_sha256:digest,
    rng:{pointer_slot_address:0,pointer_hex:'00000000',value_address:0,value_hex:'00000000'},
    declared_setup:structuredClone(declared)});
  return {schema:'melee-web-stock4-none-sss-exit-host-expectation-v1',observation_profile:1,
    menu_draw_reference:'UNKNOWN',Entry:false,C3:false,expected_replay_stop:'Manual unload stopped the replay',
    recipe:{version:8,frames:2006,bytes:328,sha256:'a'.repeat(64)},primary_sha256:'b'.repeat(64),
    notification_cursor:2,literal_inputs:[1,2].map((scene,index)=>({index,scene,pads:Array(4).fill('00'.repeat(11))})),
    prepared_profile:{scope:'original_prepared_Entry_Setup_profile_not_SSS_payload',source_primary_sha256:'b'.repeat(64),
      entry:endpoint(2,'entry'),setup:endpoint(3,'setup')},
    source:{sequence:1,boundary:'sss_exit',pc:0x8025bbd0,match_index:0,raw_start_melee_hex:'00'.repeat(240)},
    expected:{phase:'sss_exit',scene_kind:9,scene_frame:125,random_seed_hex:'00000000',pad_state_hex:'00'.repeat(822),
      sss:{header:{start_game:1},vs:{start:{rules:{match_kind:0,is_stock:false,stage_kind:3,item_frequency:2,time_limit:0,
        timer_enabled:false,timer_counts_up:false,disable_pausing:false}}}}}};
}
function stop(unloaded=false) {
  return {observation_profile:1,notification_coordinates:{enclosing_host_tick_status:'not_in_host_tick',
    last_host_tick_call_returned:true,callback_source_steps:0,callback_source_draws:0},terminal:{complete:true,
    host_call_returned:true,cursor:2,callback_source_steps:0,callback_source_draws:0,asset_requests_after_leave_boundary:0,
    waiting_for_audio_retained_until_unload:true,native_unloaded:unloaded}};
}
function report() {
  return {pass:false,complete:false,frames:2006,mode:'state_capture',scheduling_equivalence:'not_evaluated',
    instrumented_timing_resumes:0,metrics:{sourceFrames:2,sourceSteps:2,sourceDraws:2},
    source_match:{complete:false,outcome:null,winner:null},final_scene:null,
    failures:['whole-session final CSS was not entered','Manual unload stopped the replay',
      'incomplete input timeline','source tick/draw count mismatch']};
}
function preparedSetupObservation(e=expected()) {
  const player=(costume,slot=0,slot_type=0)=>({slot,slot_type,ckind:8,color:costume,stocks:4,flags_c:128});
  const start={rules:{match_kind:1,is_stock:1,stage_kind:3,xB:-1,time_limit:0,timer_enabled:0,
    timer_counts_up:0,disable_pausing:0,is_teams:0,item_mask:'ffffffffffffffff',damage_ratio_bits:'3f800000',
    game_speed_bits:'3f800000',is_vs:1,friendly_fire:0,timer_shows_hours:0,x14:0},
    players:[player(1),player(0),...Array.from({length:4},()=>player(0,0,3))]};
  const generation=12;
  const snapshot={schema:'melee-web-stadium-zero-tick-setup-state-v1',world_generation:generation,
    world_ticks:0,source_frame:0,map_id:2,constructor_callback_count:1,source_parameter_roots_agree:true,
    ground_source_heap_handle:3,ground_requested_bytes:256,ground_allocation_generation:12,
    object_source_heap_handle:3,object_requested_bytes:128,object_allocation_generation:11};
  return {terminal:{complete:true,native_unloaded:false,asset_requests_after_leave_boundary:1,
    asset_requests_at_terminal:0,waiting_for_audio_retained_until_unload:false,
    waiting_for_audio_at_terminal:true,waiting_for_audio_current:false},
    prepared_to_setup:{attempted:true,approved:true,consumed:true,failed:false,error:null,
    asset_requests_at_handoff:0,asset_requests_current_after_handoff:1,
    waiting_for_audio_at_handoff:true,waiting_for_audio_current:false,waiting_for_audio_retired:true,setup:{
    handoff_origin:'current_sss_prepared_stock4_270',attempted:true,requested:true,import_committed:true,
    menu_retired:true,parent_seed_owner_value_restored:true,complete:true,captured:true,failed:false,
    construction_complete:true,source_frames:0,host_tick_calls:0,host_draw_calls:0,requested_generation:4,
    committed_generation:4,prior_world_generation:9,match_world_generation:generation,
    current_owners:{host:true,host_entered:false,match:true,cache:true,source_world_exists:true,running:false,
      pending:false,first_use_draw_pending:false,pending_scope_generation:0,asset_generation:4,active_files:2},
    actual:{phase:'vs_setup',random_seed_hex:e.prepared_profile.setup.rng.value_hex,start},
    stadium_setup_state:snapshot}}};
}
test('synthetic stock4 selector forwards original once and exact profile, refuses foreign profile/arm',()=>{
  for(const armResult of [1,0]) {
    const bytes=Array(328).fill(7), calls=[],receiver={};const heap=new Uint8Array(400);heap.set(bytes,16);
    const original=function(...args){assert.equal(this,receiver);assert.deepEqual(args,[16,328,1]);calls.push('parse');return 1;};
    globalThis.Module={HEAPU8:heap,UTF8ToString:()=> '{}',stadiumMatchedSetupNativeIdle:()=>true,
      _melee_web_native_menu_replay:original,_melee_web_native_menu_stadium_current_sss_exit_arm(){throw Error('legacy called');},
      _melee_web_native_menu_stadium_current_sss_exit_arm_profile(profile){assert.equal(profile,1);calls.push('arm');return armResult;},
      _melee_web_native_menu_stadium_current_sss_exit_observe:()=>1,
      _melee_web_native_menu_stadium_current_sss_prepared_start_observe:()=>1};
    const scope=installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:1});
    const invoke=()=>Module._melee_web_native_menu_replay.call(receiver,16,328,1);
    if(armResult)assert.equal(invoke(),1);else assert.throws(invoke,/arm refused/);
    assert.deepEqual(calls,['parse','arm']);scope.dispose();assert.equal(Module._melee_web_native_menu_replay,original);
    // Profile2 now names the explicit later-SSS diagnostic;3 remains unsupported.
    assert.throws(()=>installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:3}),/profile/);
  }
});
test('prepared Start getter remains opt-in when installing the historical stock4 SSS arm',()=>{
  const bytes=Array(328).fill(9),heap=new Uint8Array(400);heap.set(bytes,16);
  const receiver={},original=function(){return 1;};
  globalThis.Module={HEAPU8:heap,UTF8ToString:()=>'{"captured":true}',stadiumMatchedSetupNativeIdle:()=>true,
    _melee_web_native_menu_replay:original,
    _melee_web_native_menu_stadium_current_sss_exit_arm_profile:()=>1,
    _melee_web_native_menu_stadium_current_sss_exit_observe:()=>1};
  const historical=installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:1});
  assert.equal(Module._melee_web_native_menu_replay.call(receiver,16,328,1),1);
  assert.throws(()=>historical.observePreparedStart(),/explicit checked prepared-Start profile/);
  historical.dispose();
  Module._melee_web_native_menu_stadium_current_sss_prepared_start_observe=()=>1;
  const candidate=installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:1,preparedStartProbe:true});
  assert.equal(Module._melee_web_native_menu_replay.call(receiver,16,328,1),1);
  assert.deepEqual(candidate.observePreparedStart(),{busy:false,value:{captured:true}});
  candidate.dispose();
});
test('prepared Setup approval rechecks the exact retained native receipt and consumes one use',()=>{
  const receipt={schema:'prepared-start-v2',replay_cursor:270,random_seed_hex:'e2624ac3'};
  let beginCalls=0;
  const bytes=Array(328).fill(4),heap=new Uint8Array(400);heap.set(bytes,16);
  const original=function(){return 1;};
  globalThis.Module={HEAPU8:heap,UTF8ToString:()=>JSON.stringify(receipt),stadiumMatchedSetupNativeIdle:()=>true,
    _melee_web_native_menu_replay:original,
    _melee_web_native_menu_stadium_current_sss_exit_arm_profile:()=>1,
    _melee_web_native_menu_stadium_current_sss_exit_observe:()=>1,
    _melee_web_native_menu_stadium_current_sss_prepared_start_observe:()=>1,
    _melee_web_native_menu_stadium_current_sss_prepared_setup_begin(){
      ++beginCalls;assert.equal(globalThis.__meleeWebStadiumCurrentSssPreparedStartApproved(),true);return 1;
    }};
  const scope=installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:1,preparedStartProbe:true});
  assert.equal(Module._melee_web_native_menu_replay(16,bytes.length,1),1);
  assert.deepEqual(scope.continuePreparedStart(receipt),{result:1,accepted_receipt_rechecked:true});
  assert.equal(beginCalls,1);assert.equal(Object.hasOwn(globalThis,
    '__meleeWebStadiumCurrentSssPreparedStartApproved'),false);
  assert.throws(()=>scope.continuePreparedStart(receipt),/one-use owner/);
  scope.dispose();
});
test('the serialized Setup continuation forwards the explicit accepted receipt in a fresh page realm',()=>{
  const realm=vm.createContext({});
  const pageFunction=vm.runInContext(`(${continuePreparedStartInPage.toString()})`,realm);
  const accepted=Object.freeze({schema:'prepared-start-v2',replay_cursor:270,random_seed_hex:'e2624ac3'});
  let received=null;
  const scope={continuePreparedStart(value){received=value;return 'continued';}};
  assert.equal(pageFunction(scope,accepted),'continued');
  assert.strictEqual(received,accepted);
});
test('synthetic prepared-entry compact projection compares only declared common fields',()=>{
  const declared=expected().prepared_profile.entry.declared_setup;
  const sourceStartHex='00'.repeat(312);
  const projection={rules:{match_kind:1,is_stock:1,stage_kind:3,xB:-1,time_limit:0,
    timer_enabled:0,timer_counts_up:0,disable_pausing:0,is_teams:0,item_mask:'ffffffffffffffff',
    damage_ratio_bits:'3f800000',game_speed_bits:'3f800000',is_vs:1,friendly_fire:0,
    timer_shows_hours:0,x14:0},players:[
    {slot:0,slot_type:0,ckind:8,color:1,stocks:4,flags_c:128},
    {slot:0,slot_type:0,ckind:8,color:0,stocks:4,flags_c:128},
    ...Array(4).fill({slot_type:3})]};
  assert.equal(compareStock4PreparedStartProjection(projection,declared,sourceStartHex).scope,
    'declared_source_common_fields_only');
  for(const mutate of [x=>x.rules.xB=2,x=>x.rules.is_vs=0,x=>x.players[1].color=1,
    x=>x.players[2].slot_type=0]) {
    const changed=structuredClone(projection);mutate(changed);
    assert.throws(()=>compareStock4PreparedStartProjection(changed,declared,sourceStartHex));
  }
});
test('prepared-entry receipt requires the stopped boundary and expected RNG; strict wrapper pins source identity',()=>{
  const e=expected(), declared=e.prepared_profile.entry.declared_setup, rng=e.prepared_profile.entry.rng;
  const sourceStartHex='00'.repeat(312);
  const start={rules:{match_kind:1,is_stock:1,stage_kind:3,xB:-1,time_limit:0,
    timer_enabled:0,timer_counts_up:0,disable_pausing:0,is_teams:0,item_mask:'ffffffffffffffff',
    damage_ratio_bits:'3f800000',game_speed_bits:'3f800000',is_vs:1,friendly_fire:0,
    timer_shows_hours:0,x14:0},players:[
    {slot:0,slot_type:0,ckind:8,color:1,stocks:4,flags_c:128},
    {slot:0,slot_type:0,ckind:8,color:0,stocks:4,flags_c:128},...Array(4).fill({slot_type:3})]};
  const receipt={schema:'melee-web-stadium-prepared-start-before-assets-v2',
    scope:'source_prepared_start_after_sss_leave_before_assets',phase:'prepared_entry',observation_profile:1,
    captured:true,source_scene:0,host_entered:false,waiting_for_audio_retained_until_unload:true,
    replay_cursor:270,local_order_rows:543,callback_source_steps:0,callback_source_draws:0,
    asset_requests_after_leave_boundary:0,world_generation:1,hsd_pending_loads_at_return:0,
    bank_transport_active_at_return:true,bank_transport_configured_at_return:true,
    bank_transport_busy_at_return:false,random_seed_hex:'00000000',start,
    menu_draw_reference:'UNKNOWN',runtime_admission:false,Entry:false,C3:false};
  const comparison=compareStock4PreparedStartReceipt(receipt,rng,declared,sourceStartHex);
  assert.equal(comparison.scope,'prepared_start_boundary_common_fields_and_announcer_service_drain');
  assert.equal(comparison.announcer_service_completion.transfer_occurrence,'UNOBSERVED');
  assert.match(comparison.announcer_service_completion.helper_return,/C1A host READY follows helper success/);
  for(const mutate of [x=>x.random_seed_hex='12345678',x=>x.local_order_rows++,
    x=>x.asset_requests_after_leave_boundary++,x=>x.Entry=true,
    x=>x.hsd_pending_loads_at_return=1,x=>x.bank_transport_active_at_return=false,
    x=>x.bank_transport_configured_at_return=false,x=>x.bank_transport_busy_at_return=true,
    x=>delete x.hsd_pending_loads_at_return,x=>delete x.bank_transport_active_at_return,
    x=>delete x.bank_transport_busy_at_return]) {
    const changed=structuredClone(receipt);mutate(changed);
    assert.throws(()=>compareStock4PreparedStartReceipt(changed,rng,declared,sourceStartHex));
  }
  const legacy=structuredClone(receipt);legacy.schema='melee-web-stadium-prepared-start-before-assets-v1';
  assert.throws(()=>compareStock4PreparedStartReceipt(legacy,rng,declared,sourceStartHex));
  assert.throws(()=>compareStock4PreparedStartObservation(receipt,e),
    /retained original Entry\/RNG identity/);
});
test('outer prepared-start comparator forwards the validated announcer completion (wrapper-only control)',()=>{
  const identity={sequence:1640,source_tick:125,draw_ordinal:0,start_melee_sha256:'a'.repeat(64),
    rng:{pointer_slot_address:0x100,pointer_hex:'00000001',value_address:0x104,value_hex:'12345678'}};
  const declaredSetup={fixture:'synthetic-only'},startHex='synthetic-wrapper-control';
  const entry={...identity,boundary:'entry',start_melee_hex:startHex,declared_setup:declaredSetup};
  const expectedProfile={prepared_profile:{entry}},actual={fixture:'actual-shape-passed-to-inner-control'};
  const projection={pass:true,scope:'synthetic-projection'};
  const serviceCompletion={basis:'synthetic-only',transfer_occurrence:'UNOBSERVED'};
  const validatedReceipt={pass:true,projection,announcer_service_completion:serviceCompletion};
  let validatedExpected=null,receiptArguments=null;
  const context={ORIGINAL_ENTRY:identity,requireValue:(condition,message)=>{if(!condition)throw Error(message);},
    validateStock4SssExitExpected:value=>{validatedExpected=value;},
    compareStock4PreparedStartReceipt:(...args)=>{receiptArguments=args;return validatedReceipt;}};
  vm.createContext(context);
  vm.runInContext(`globalThis.invoke=${compareStock4PreparedStartObservation.toString()};`,context);
  const result=context.invoke(actual,expectedProfile);
  assert.equal(validatedExpected,expectedProfile);
  assert.equal(receiptArguments[0],actual);
  assert.equal(receiptArguments[1],entry.rng);
  assert.equal(receiptArguments[2],declaredSetup);
  assert.equal(receiptArguments[3],startHex);
  assert.equal(result.pass,true);
  assert.equal(result.announcer_service_completion,serviceCompletion);
  assert.equal(result.projection,projection);
  assert.equal(result.raw_start_melee_bytes,'not_compared');
  assert.equal(result.runtime_admission,false);
  assert.equal(result.Entry,false);
  assert.equal(result.C3,false);
});
test('prepared-start finalization is a separate opt-in control from the historical SSS stop',()=>{
  const e=expected(), report={current_sss_exit:{prepared_start_comparison:{pass:true,
    scope:'prepared_start_common_fields_and_announcer_service_drain_against_original_entry_profile'}}};
  assert.equal(validateStock4PreparedStartFinalization(report,e).pass,true);
  assert.throws(()=>validateStock4PreparedStartFinalization({current_sss_exit:{}},e),/separately enabled/);
});
test('synthetic stop refuses changed phase/asset/source work and unknown profile',()=>{
  assert.equal(validateStock4SssExitExpected(expected()).recipe.frames,2006);
  const historical=expected();delete historical.prepared_profile.entry.rng;delete historical.prepared_profile.setup.rng;
  assert.equal(validateStock4SssExitExpected(historical),historical);
  assert.equal(validateStock4SssExitStop(stop(),expected()).pass,true);
  for(const change of [s=>s.terminal.complete=false,s=>s.terminal.asset_requests_after_leave_boundary++,
    s=>s.terminal.callback_source_steps++,s=>s.terminal.callback_source_draws++,s=>s.terminal.native_unloaded=true,
    s=>s.notification_coordinates.enclosing_host_tick_status='in_flight_not_returned',s=>s.observation_profile=0]) {
    const s=stop();change(s);assert.throws(()=>validateStock4SssExitStop(s,expected()));
  }
  for(const change of [e=>e.recipe.frames=2013,e=>e.observation_profile=0,e=>e.prepared_profile.entry.declared_setup.item_frequency=2,
    e=>e.literal_inputs.reverse(),e=>e.C3=true]) {
    const e=expected();change(e);assert.throws(()=>validateStock4SssExitExpected(e));
  }
});
test('synthetic raw SSS prefix remains independent from later prepared profile',()=>{
  const e=expected();assert.equal(e.expected.sss.vs.start.rules.is_stock,false);
  assert.equal(e.expected.sss.vs.start.rules.item_frequency,2);
  assert.equal(validateStock4SssExitExpected(e),e);
  const confused=structuredClone(e);
  confused.prepared_profile.entry.declared_setup=structuredClone(e.expected.sss.vs.start.rules);
  confused.prepared_profile.setup.declared_setup=structuredClone(e.expected.sss.vs.start.rules);
  assert.throws(()=>validateStock4SssExitExpected(confused),/prepared/);
  for(const mutate of [x=>x.prepared_profile.entry.start_melee_hex='01'.repeat(312),
    x=>x.prepared_profile.source_primary_sha256='c'.repeat(64),x=>x.prepared_profile.setup.sequence=1]) {
    const x=structuredClone(e);mutate(x);assert.throws(()=>validateStock4SssExitExpected(x));
  }
});
test('synthetic terminal preserves incomplete full replay and rejects later diagnostics/cleanup failure',()=>{
  const snapshot={phase:0,running:0,runtime_error:''};
  assert.equal(validateStock4SssExitTerminal(report(),snapshot,expected(),stop(true)).pass,true);
  const full={browser_report:report(),final_snapshot:snapshot,last_successful_snapshot:snapshot,snapshots:[snapshot],
    cleanup:{browser_close_returned:true},phases:[{name:'current-sss-exit-component',result:'pass'}],
    current_sss_exit:{unload_requested:true,unloaded_observation:stop(true)},original_replay_capture_result:'fail',
    finalization_failures:['Whole-session replay did not complete successfully','Browser replay report is incomplete or failed']};
  assert.equal(validateStock4SssExitFinalization(full,expected()).pass,true);
  for(const change of [r=>r.first_error={kind:'runtime',message:'unexpected'},r=>r.snapshots.push({runtime_error:'later'}),
    r=>r.cleanup.browser_close_returned=false,r=>r.browser_report.pass=true,r=>r.current_sss_exit.unload_requested=false,
    r=>r.snapshots=Array(2048).fill(snapshot)]) {
    const r=structuredClone(full);change(r);assert.throws(()=>validateStock4SssExitFinalization(r,expected()));
  }
});
test('prepared-to-Setup comparator requires completed same-origin zero-tick Setup and the retained endpoint seed',()=>{
  const e=expected(),actual=preparedSetupObservation(e);
  assert.equal(compareStock4PreparedSetupObservation(actual,e).scope,
    'PASS_BOUNDED_PREPARED_ENTRY_TO_SETUP_COMMON_FIELDS_AND_SEED_COMPONENT');
  const indexZeroHeap=structuredClone(actual);
  indexZeroHeap.prepared_to_setup.setup.stadium_setup_state.ground_source_heap_handle=0;
  indexZeroHeap.prepared_to_setup.setup.stadium_setup_state.object_source_heap_handle=0;
  assert.equal(compareStock4PreparedSetupObservation(indexZeroHeap,e).pass,true);
  const faults=[
    x=>x.terminal.asset_requests_after_leave_boundary=0,
    x=>x.terminal.asset_requests_at_terminal=1,
    x=>x.terminal.waiting_for_audio_at_terminal=false,
    x=>x.terminal.waiting_for_audio_current=true,
    x=>x.prepared_to_setup.approved=false,
    x=>x.prepared_to_setup.asset_requests_at_handoff=1,
    x=>x.prepared_to_setup.asset_requests_current_after_handoff=0,
    x=>x.prepared_to_setup.waiting_for_audio_at_handoff=false,
    x=>x.prepared_to_setup.waiting_for_audio_retired=false,
    x=>x.prepared_to_setup.setup.handoff_origin='legacy_entry_124',
    x=>x.prepared_to_setup.setup.complete=false,
    x=>x.prepared_to_setup.setup.current_owners.source_world_exists=false,
    x=>x.prepared_to_setup.setup.actual.phase='sss_exit',
    x=>x.prepared_to_setup.setup.actual.random_seed_hex='e2624ac3',
    x=>x.prepared_to_setup.setup.stadium_setup_state.world_ticks=1,
    x=>x.prepared_to_setup.setup.stadium_setup_state.constructor_callback_count=2,
    x=>x.prepared_to_setup.setup.stadium_setup_state.ground_source_heap_handle=-1,
    x=>x.prepared_to_setup.setup.stadium_setup_state.object_source_heap_handle=-1,
    x=>x.prepared_to_setup.setup.stadium_setup_state.ground_source_heap_handle=1.5,
    x=>x.prepared_to_setup.setup.stadium_setup_state.object_source_heap_handle='0',
    x=>{x.prepared_to_setup.setup.stadium_setup_state.ground_source_heap_handle=1;
      x.prepared_to_setup.setup.stadium_setup_state.object_source_heap_handle=0;},
    x=>x.prepared_to_setup.setup.stadium_setup_state.ground_requested_bytes=0,
    x=>x.prepared_to_setup.setup.stadium_setup_state.object_requested_bytes=0,
    x=>x.prepared_to_setup.setup.stadium_setup_state.ground_allocation_generation=0,
    x=>x.prepared_to_setup.setup.stadium_setup_state.object_allocation_generation=0,
    x=>x.prepared_to_setup.setup.stadium_setup_state.world_generation++,
    x=>x.prepared_to_setup.setup.stadium_setup_state.source_parameter_roots_agree=false,
  ];
  for(const mutate of faults){const bad=structuredClone(actual);mutate(bad);assert.throws(()=>compareStock4PreparedSetupObservation(bad,e));}
  const doubleEncoded=structuredClone(actual);
  doubleEncoded.prepared_to_setup.setup.actual=JSON.stringify(doubleEncoded.prepared_to_setup.setup.actual);
  assert.throws(()=>compareStock4PreparedSetupObservation(doubleEncoded,e),/producer-shaped JSON objects/);
  const final={current_sss_exit:{prepared_setup_comparison:compareStock4PreparedSetupObservation(actual,e)}};
  assert.equal(validateStock4PreparedSetupFinalization(final,e).pass,true);
  assert.throws(()=>validateStock4PreparedSetupFinalization({current_sss_exit:{}},e));
});
test('prepared Setup finalization checks the original live handoff and separate current unload state',()=>{
  const e=expected(),handoff=stop(false),unloaded=preparedSetupObservation(e);
  handoff.terminal.asset_requests_at_terminal=0;
  handoff.terminal.waiting_for_audio_at_terminal=true;
  handoff.terminal.waiting_for_audio_current=true;
  handoff.terminal.native_unloaded=false;
  unloaded.terminal.native_unloaded=true;
  assert.equal(validateStock4SssExitTerminal(report(),{phase:0,running:0,runtime_error:''},e,
    handoff,unloaded).pass,true);
  const full={browser_report:report(),final_snapshot:{phase:0,running:0,runtime_error:''},
    last_successful_snapshot:{phase:0,running:0,runtime_error:''},snapshots:[{phase:0,running:0,runtime_error:''}],
    cleanup:{browser_close_returned:true},phases:[{name:'current-sss-exit-component',result:'pass'}],
    current_sss_exit:{unload_requested:true,observation:handoff,unloaded_observation:unloaded,
      prepared_setup_begin:{result:1,accepted_receipt_rechecked:true},
      prepared_setup_comparison:{pass:true},},original_replay_capture_result:'fail',
    finalization_failures:['Whole-session replay did not complete successfully','Browser replay report is incomplete or failed']};
  assert.equal(validateStock4SssExitFinalization(full,e).pass,true);
  assert.throws(()=>validateStock4SssExitStop(unloaded,e),/ordinary-leave\/pre-assets boundary/);
  const comparisonMissing=structuredClone(full);
  delete comparisonMissing.current_sss_exit.prepared_setup_comparison;
  comparisonMissing.current_sss_exit.prepared_setup_failure='retained comparator refusal';
  assert.equal(validateStock4SssExitFinalization(comparisonMissing,e).pass,true);
  assert.throws(()=>validateStock4PreparedSetupFinalization(comparisonMissing,e),
    /separately checked completed zero-tick Setup comparison/);
  const comparisonFailed=structuredClone(comparisonMissing);
  comparisonFailed.current_sss_exit.prepared_setup_comparison={pass:false};
  assert.throws(()=>validateStock4PreparedSetupFinalization(comparisonFailed,e),
    /separately checked completed zero-tick Setup comparison/);
  for(const mutate of [x=>x.terminal.asset_requests_after_leave_boundary=1,
    x=>x.terminal.waiting_for_audio_retained_until_unload=false]) {
    const changed=structuredClone(handoff);mutate(changed);
    assert.throws(()=>validateStock4SssExitTerminal(report(),{phase:0,running:0,runtime_error:''},e,
      changed,unloaded));
  }
  for(const mutate of [x=>x.terminal.asset_requests_after_leave_boundary=0,
    x=>x.terminal.waiting_for_audio_current=true,
    x=>x.prepared_to_setup.asset_requests_current_after_handoff=0]) {
    const changed=structuredClone(unloaded);mutate(changed);
    assert.throws(()=>validateStock4SssExitTerminal(report(),{phase:0,running:0,runtime_error:''},e,
      handoff,changed));
  }
  for(const mutate of [x=>x.current_sss_exit.prepared_setup_begin.result=0,
    x=>x.current_sss_exit.prepared_setup_begin.accepted_receipt_rechecked=false,
    x=>x.current_sss_exit.unloaded_observation.prepared_to_setup.consumed=false,
    x=>x.current_sss_exit.unloaded_observation.prepared_to_setup.approved=false,
    x=>x.current_sss_exit.unloaded_observation.prepared_to_setup.failed=true,
    x=>x.current_sss_exit.unloaded_observation.prepared_to_setup.error='failed',
    x=>x.current_sss_exit.unloaded_observation.terminal.asset_requests_after_leave_boundary=0,
    x=>x.current_sss_exit.unloaded_observation.terminal.waiting_for_audio_current=true,
    x=>x.current_sss_exit.unloaded_observation.terminal.native_unloaded=false]) {
    const changed=structuredClone(full);mutate(changed);
    assert.throws(()=>validateStock4SssExitFinalization(changed,e));
  }
  const passWithoutConsumedRoute=structuredClone(full);
  passWithoutConsumedRoute.current_sss_exit.unloaded_observation.prepared_to_setup.consumed=false;
  assert.throws(()=>validateStock4SssExitFinalization(passWithoutConsumedRoute,e),
    /consumed route at unload/);
});
test('actual unloadAndSave body cancels preparation and waits for observed cache drain',async()=>{
  const root=process.env.MELEE_STADIUM_STOCK4_BASE_ROOT||new URL('..',import.meta.url).pathname;
  const text=fs.readFileSync(root+'/web/melee-runtime.mjs','utf8');
  const start=text.indexOf('  async function unloadAndSave() {'),end=text.indexOf('  function putNow(',start);
  assert.ok(start>=0&&end>start);const events=[];let reads=0;
  const context={prepared:true,closeNetworkSession:async(reason)=>events.push('network:'+reason),
    boundary:async(fn)=>{events.push('command');return fn();},syncAudio:()=>events.push('audio-sync'),
    Module:{_melee_web_native_menu_unload:()=>{events.push('native-unload');return 1;}},
    callbacks:{menuPreparationCanceled:()=>events.push('preparation-canceled')},pauseAudioForPreparation:async()=>events.push('audio-ack'),
    performance:{now:()=>0},readNativeCacheIdle:()=>{events.push('cache-observe');return reads++?1:0;},emit:()=>{throw Error('unexpected emit');}};
  vm.createContext(context);vm.runInContext(text.slice(start,end)+'\nglobalThis.invoke=unloadAndSave;',context);
  assert.equal(await context.invoke(),true);assert.equal(context.prepared,false);
  assert.deepEqual(events,['network:normal','command','audio-sync','native-unload','preparation-canceled','audio-ack',
    'command','cache-observe','command','cache-observe']);
});
