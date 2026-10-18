import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import {installCurrentSssExitReplayArm} from '../scripts/stadium_current_sss_exit_observation.mjs';
import {validateStock4SssExitExpected, validateStock4SssExitStop,
  compareStock4PreparedStartProjection, compareStock4PreparedStartReceipt,
  compareStock4PreparedStartObservation,
  validateStock4SssExitTerminal, validateStock4SssExitFinalization,
  validateStock4PreparedStartFinalization} from '../scripts/stadium_stock4_sss_exit_observation.mjs';

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
    assert.throws(()=>installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:2}),/profile/);
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
  assert.throws(()=>historical.observePreparedStart(),/explicit stock4\/None/);
  historical.dispose();
  Module._melee_web_native_menu_stadium_current_sss_prepared_start_observe=()=>1;
  const candidate=installCurrentSssExitReplayArm({recipeBytes:bytes,observationProfile:1,preparedStartProbe:true});
  assert.equal(Module._melee_web_native_menu_replay.call(receiver,16,328,1),1);
  assert.deepEqual(candidate.observePreparedStart(),{busy:false,value:{captured:true}});
  candidate.dispose();
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
  const receipt={schema:'melee-web-stadium-prepared-start-before-assets-v1',
    scope:'source_prepared_start_after_sss_leave_before_assets',phase:'prepared_entry',observation_profile:1,
    captured:true,source_scene:0,host_entered:false,waiting_for_audio_retained_until_unload:true,
    replay_cursor:270,local_order_rows:543,callback_source_steps:0,callback_source_draws:0,
    asset_requests_after_leave_boundary:0,world_generation:1,random_seed_hex:'00000000',start,
    menu_draw_reference:'UNKNOWN',runtime_admission:false,Entry:false,C3:false};
  assert.equal(compareStock4PreparedStartReceipt(receipt,rng,declared,sourceStartHex).scope,
    'prepared_start_boundary_common_fields_only');
  for(const mutate of [x=>x.random_seed_hex='12345678',x=>x.local_order_rows++,
    x=>x.asset_requests_after_leave_boundary++,x=>x.Entry=true]) {
    const changed=structuredClone(receipt);mutate(changed);
    assert.throws(()=>compareStock4PreparedStartReceipt(changed,rng,declared,sourceStartHex));
  }
  assert.throws(()=>compareStock4PreparedStartObservation(receipt,e),
    /retained original Entry\/RNG identity/);
});
test('prepared-start finalization is a separate opt-in control from the historical SSS stop',()=>{
  const e=expected(), report={current_sss_exit:{prepared_start_comparison:{pass:true,
    scope:'prepared_start_common_fields_against_original_entry_profile'}}};
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
