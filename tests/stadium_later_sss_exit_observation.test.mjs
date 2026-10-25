import test from 'node:test';
import {LATER_REFERENCE_CONTEXTS, OBSERVED_B_REFERENCE, OBSERVED_B_SCHEMA} from '../scripts/stadium_later_reference_contexts.mjs';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {validateLaterReferenceRecipeInputs, validateLaterSssExitExpected, validateLaterSssExitTrace, compareLaterSssExitObservation,
  validateLaterSssExitTerminal, validateLaterSssExitFinalization, compareLaterPreparedStartObservation, compareLaterPreparedSetupObservation} from '../scripts/stadium_later_sss_exit_observation.mjs';
import {validateStock4SssExitExpected, compareStock4SssExitObservation, validateStock4SssExitTrace,
  validateStock4SssExitTerminal, validateStock4SssExitFinalization} from '../scripts/stadium_stock4_sss_exit_observation.mjs';
import {installCurrentSssExitReplayArm, continuePreparedStartInPage, validateCurrentSssExitExpected, compareCurrentSssExitObservation,
  validateCurrentSssExitTrace, validateCurrentSssExitTerminal, validateCurrentSssExitFinalization} from '../scripts/stadium_current_sss_exit_observation.mjs';

// Synthetic mechanism controls. Reuse the existing public literal solely for
// exact hash admission; these shapes are not an original/native game result.
const retainedStart = readFileSync(new URL('./stadium_first_css_stream_compare.mjs', import.meta.url), 'utf8')
  .match(/const FRESH132_FULL_A_START_HEX = '([0-9a-f]+)'/)[1];
function expected() {
  const digest = createHash('sha256').update(Buffer.from(retainedStart, 'hex')).digest('hex');
  const declared = {match_kind:1,is_stock:true,stage:3,item_frequency:-1,time_limit_seconds:0,
    timer_enabled:false,timer_counts_up:false,disable_pausing:false,is_teams:false,
    players:[1,0].map((costume,i)=>({port:i+1,character_kind:8,costume,stocks:4,player_type:0}))};
  const rng = value_hex => ({pointer_slot_address:0x804d5f94,pointer_hex:'804d5f90',value_address:0x804d5f90,value_hex});
  const endpoint = (sequence,boundary,match_index,seed) => ({sequence,boundary,match_index,source_tick:133,
    draw_ordinal:0,pc:1,start_melee_hex:retainedStart,start_melee_sha256:digest,
    declared_setup:structuredClone(declared),rng:rng(seed)});
  const preceding_spans = [[1,0,144],[2,145,269],[3,270,461],[4,462,674],[1,675,806],[2,807,939]]
    .map(([scene,first_frame,last_frame])=>({scene,first_frame,last_frame}));
  const literal_inputs = Array.from({length:940},(_,index)=>({index,
    scene:preceding_spans.find(s=>index>=s.first_frame&&index<=s.last_frame).scene,
    pads:[0,1,2,3].map(port=>(port+index%200).toString(16).padStart(2,'0').repeat(11))}));
  return {schema:'melee-web-stock4-none-later-sss-exit-host-expectation-v1',observation_profile:2,
    menu_draw_reference:'UNKNOWN',Entry:false,C3:false,expected_replay_stop:'Manual unload stopped the replay',
    selected_occurrence:1,notification_cursor:940,menu_session_ticks:535,original_B_constructor_GO:'UNOBSERVED',
    recipe:{version:8,frames:2006,bytes:328,sha256:'a'.repeat(64)},primary_sha256:'b'.repeat(64),
    preceding_spans,literal_inputs,
    source:{sequence:4234,source_tick:133,draw_ordinal:192,boundary:'sss_exit',pc:0x8025bbd0,
      match_index:1,raw_start_melee_hex:'00'.repeat(240),rng:rng('7fc17cf1')},
    prepared_profile:{scope:'original_prepared_Entry_Setup_profile_not_SSS_payload',source_primary_sha256:'b'.repeat(64),
      entry:endpoint(4235,'entry',1,'7fc17cf1'),setup:endpoint(4313,'setup',1,'25d557a7')},
    preceding_profile:{match_index:0,entry:endpoint(1640,'entry',0,'e2624ac3'),setup:endpoint(1718,'setup',0,'083a62a9')},
    expected:{phase:'sss_exit',scene_kind:9,scene_frame:133,random_seed_hex:'7fc17cf1',pad_state_hex:'00'.repeat(822),
      sss:{header:{start_game:1}}}};
}
function trace(e) {
  const rows=[{record:'header',schema:'melee-web-port-session-diagnostic',version:1,
    frames_requested:2006,draw_state:'not_captured',comparison:'not_run'}];
  for(const row of e.literal_inputs){
    if(row.index===270)rows.push({record:'session_match_enter_complete',rng:0x083a62a9,pad_state_hex:'00'.repeat(822)});
    rows.push({record:'session_frame',index:row.index,scene:row.scene,supplied_inputs:row.pads,
      rng:0,pad_state_hex:'00'.repeat(822)});
    rows.push({record:'current_sss_exit_order',order:row.index,kind:'tick_return',replay_cursor:row.index,
      source_steps:1,source_draws:0,menu_draw_reference:'UNKNOWN'});
  }
  return rows;
}
const text = rows => rows.map(row=>JSON.stringify(row)).join('\n');
test('explicit occurrence1 accepts global prefix and rejects legacy admission',()=>{
  const e=expected();assert.equal(validateLaterSssExitExpected(e),e);
  assert.throws(()=>validateStock4SssExitExpected(e));
  for(const mutate of [v=>v.selected_occurrence=0,v=>v.notification_cursor=270,
    v=>v.preceding_spans[2].first_frame++,v=>v.literal_inputs[270].scene=2,
    v=>v.literal_inputs[675].pads.pop(),v=>v.source.match_index=0,
    v=>v.source.rng.pointer_hex='804d5f94',v=>v.menu_session_ticks=940,
    v=>v.prepared_profile.entry.rng.value_hex='e2624ac3']){
    const bad=structuredClone(e);mutate(bad);assert.throws(()=>validateLaterSssExitExpected(bad));
  }
});
test('actual trace API requires ordered entry plus literal four-port prefix',()=>{
  const e=expected(), rows=trace(e), a={notification_coordinates:{order:940}};
  assert.equal(validateLaterSssExitTrace(text(rows),e,a).literal_supplied_inputs,940);
  for(const mutate of [r=>r.splice(r.findIndex(x=>x.record==='session_match_enter_complete'),1),
    r=>{const i=r.findIndex(x=>x.record==='session_match_enter_complete');r.push(...r.splice(i,1));},
    r=>r.find(x=>x.record==='session_match_enter_complete').rng=0,
    r=>r.find(x=>x.record==='session_frame'&&x.index===270).supplied_inputs[3]='ff'.repeat(11),
    r=>r.find(x=>x.record==='session_frame'&&x.index===675).index=0,
    r=>r.find(x=>x.record==='current_sss_exit_order'&&x.replay_cursor===939).order=0]){
    const bad=structuredClone(rows);mutate(bad);assert.throws(()=>validateLaterSssExitTrace(text(bad),e,a));
  }
});
test('actual page wrapper binds explicit profile2 and keeps one-use/owner guards',()=>{
  // Endpoint doubles test only actual page-function call and ownership mechanics.
  const make=()=>{
    const recipe=Array(328).fill(37),heap=new Uint8Array(1024);heap.set(recipe,16);
    const parsed=[],armed=[],receiver={};
    const original=function(...args){parsed.push({args,receiver:this});return 1;};
    const module={HEAPU8:heap,UTF8ToString:()=>'',stadiumMatchedSetupNativeIdle:()=>true,
      _melee_web_native_menu_replay:original,_melee_web_native_menu_stadium_current_sss_exit_observe:()=>1,
      _melee_web_native_menu_stadium_current_sss_exit_arm_profile:profile=>{armed.push(profile);return 1;}};
    return {recipe,heap,parsed,armed,receiver,original,module};
  };
  try{
    const f=make();globalThis.Module=f.module;
    const scope=installCurrentSssExitReplayArm({recipeBytes:f.recipe,observationProfile:2});
    assert.equal(f.module._melee_web_native_menu_replay.call(f.receiver,16,328,1),1);
    assert.deepEqual(f.parsed,[{args:[16,328,1],receiver:f.receiver}]);assert.deepEqual(f.armed,[2]);
    assert.equal(scope.state().armed,true);
    assert.throws(()=>f.module._melee_web_native_menu_replay(16,328,1),/ownership\/reuse/);
    assert.equal(f.parsed.length,1);assert.equal(scope.dispose(),true);assert.equal(f.module._melee_web_native_menu_replay,f.original);
    assert.throws(()=>installCurrentSssExitReplayArm({recipeBytes:f.recipe,observationProfile:2,preparedStartProbe:true}));
    assert.throws(()=>installCurrentSssExitReplayArm({recipeBytes:f.recipe,observationProfile:3}),/profile/);
    // Every post-parser refusal consumes the one parser attempt; native arm is never called.
    for(const item of [
      {name:'missing explicit parser mode',args:[16,328]},
      {name:'wrong explicit parser mode',args:[16,328,0]},
      {name:'changed actual recipe bytes',args:[16,328,1],mutate:x=>{x.heap[17]^=1;}},
      {name:'lost returned idle owner',args:[16,328,1],mutate:x=>{x.module.stadiumMatchedSetupNativeIdle=()=>false;}}
    ]){
      const x=make();globalThis.Module=x.module;
      // Idle is captured before installation so this case exercises returned-idle refusal, not callback replacement.
      if(item.name==='lost returned idle owner'){
        let idle=true;x.module.stadiumMatchedSetupNativeIdle=()=>idle;
        const owner=installCurrentSssExitReplayArm({recipeBytes:x.recipe,observationProfile:2});idle=false;
        assert.throws(()=>x.module._melee_web_native_menu_replay(...item.args),/foreign recipe|idle owner/);
        assert.equal(x.parsed.length,1);assert.deepEqual(x.armed,[]);assert.equal(owner.state().armed,false);owner.dispose();continue;
      }
      const owner=installCurrentSssExitReplayArm({recipeBytes:x.recipe,observationProfile:2});item.mutate?.(x);
      assert.throws(()=>x.module._melee_web_native_menu_replay(...item.args),/foreign recipe|idle owner/);
      assert.equal(x.parsed.length,1);assert.deepEqual(x.armed,[]);assert.equal(owner.state().armed,false);
      assert.throws(()=>x.module._melee_web_native_menu_replay(16,328,1),/ownership\/reuse/);assert.equal(x.parsed.length,1);owner.dispose();
    }
    for(const mutate of [x=>{globalThis.Module={...x.module};},
      x=>{x.module._melee_web_native_menu_stadium_current_sss_exit_arm_profile=()=>1;},
      x=>{x.module._melee_web_native_menu_stadium_current_sss_exit_observe=()=>1;}]){
      const x=make();globalThis.Module=x.module;const owner=installCurrentSssExitReplayArm({recipeBytes:x.recipe,observationProfile:2});mutate(x);
      assert.throws(()=>x.module._melee_web_native_menu_replay(16,328,1),/ownership\/reuse/);
      assert.equal(x.parsed.length,0);assert.deepEqual(x.armed,[]);
      const retainedModule=globalThis.Module;
      const retainedReplay=Object.getOwnPropertyDescriptor(x.module,'_melee_web_native_menu_replay');
      const retainedArm=x.module._melee_web_native_menu_stadium_current_sss_exit_arm_profile;
      const retainedObserve=x.module._melee_web_native_menu_stadium_current_sss_exit_observe;
      assert.throws(()=>owner.dispose(),/disposal refuses foreign replacement/);
      assert.equal(globalThis.Module,retainedModule);
      assert.deepEqual(Object.getOwnPropertyDescriptor(x.module,'_melee_web_native_menu_replay'),retainedReplay);
      assert.equal(x.module._melee_web_native_menu_stadium_current_sss_exit_arm_profile,retainedArm);
      assert.equal(x.module._melee_web_native_menu_stadium_current_sss_exit_observe,retainedObserve);
      assert.equal(owner.state().disposed,false);
    }
  }finally{delete globalThis.Module;}
});

// Synthetic endpoint values exercise actual validators and runner seams only.
function laterEndpoint(e,unloaded=false) {
  e.expected.scene_routing_getters={current_game_mode:2,previous_game_mode:2,current_scene_index:9,previous_scene_index:8};
  const rules={match_kind:0,hud_layout:0,timer_enabled:false,timer_counts_up:false,friendly_fire:false,
    is_stock:false,single_button:false,disable_pausing:false,is_vs:true,is_teams:0,item_frequency:2,
    stage_kind:3,time_limit:0,item_mask:'ffffffffffffffff',damage_ratio_bits:'3f800000',game_speed_bits:'3f800000'};
  const player={ckind:8,slot_type:0,stocks:0,color:0,slot:0,spawn:0,spawn_direction:0,sub_color:0,
    handicap:0,team:0,nametag:0,flags_c:0,flags_d:0,cpu_kind:0,cpu_level:0,damage_10:0,damage_12:0,hp:0,
    attack_ratio_bits:'3f800000',defense_ratio_bits:'3f800000',model_scale_bits:'3f800000'};
  e.expected.sss.vs={start:{rules,players:Array.from({length:4},()=>({...player}))}};
  const nativeRules=Object.fromEntries(Object.entries(rules).map(([k,v])=>[
    ({hud_layout:'x0_3',item_frequency:'xB'})[k]||k,typeof v==='boolean'?Number(v):v]));
  return {scope:'passive_sss_onexit_return_only',armed:true,notified:true,failed:false,error:null,
    notification_received:true,observation_state:2,menu_draw_reference:'UNKNOWN',runtime_admission:false,Entry:false,C3:false,
    observation_profile:2,selected_occurrence:1,preceding_lifecycle_stage:4,original_B_constructor_GO:'UNOBSERVED',
    notification_coordinates:{replay_cursor:940,callback_source_steps:0,callback_source_draws:0,order:940,
      enclosing_host_tick_status:'not_in_host_tick',last_host_tick_call_returned:true},
    terminal:{complete:true,host_call_returned:true,cursor:940,callback_source_steps:0,callback_source_draws:0,
      asset_requests_after_leave_boundary:0,asset_requests_at_terminal:0,
      waiting_for_audio_retained_until_unload:true,native_unloaded:unloaded},
    native_sss_bytes_scope:'native_ABI_only_not_original_raw_equivalence',native_sss_bytes_hex:'00',
    note:{...structuredClone(e.expected),host_entered:true,session_phase:3,source_scene:2,session_ticks:535,
      world_generation:3,audio_generation:1,
      owners:Object.fromEntries(['host','session','world','audio','vs_mode','scene_info','payload','seed'].map(k=>[k,true])),
      sss:{header:{start_game:1},vs:{start:{rules:nativeRules,
        players:[...Array.from({length:4},()=>({...player,xB:0})),{},{}]}}}}};
}
function laterReplayReport(e) {
  return {pass:false,complete:false,recipe_sha256:e.recipe.sha256,frames:2006,mode:'state_capture',
    scheduling_equivalence:'not_evaluated',instrumented_timing_resumes:0,
    metrics:{sourceFrames:940,sourceSteps:940,sourceDraws:939}, // Synthetic count, no original draw claim.
    source_match:{complete:false,outcome:null,winner:null},final_scene:null,
    failures:['whole-session final CSS was not entered','Manual unload stopped the replay',
      'incomplete input timeline','source tick/draw count mismatch']};
}
function laterFinalReport(e) {
  return {snapshots:[{phase:1,running:1}],phases:[{name:'current-sss-exit-component',result:'pass'}],
    cleanup:{browser_close_returned:true},original_replay_capture_result:'fail',
    finalization_failures:['Whole-session replay did not complete successfully','Browser replay report is incomplete or failed'],
    current_sss_exit:{comparison:{pass:true},unload_requested:true,
      observation:laterEndpoint(e),unloaded_observation:laterEndpoint(e,true)},
    browser_report:laterReplayReport(e),final_snapshot:{phase:0,running:0}};
}
test('later terminal and finalization retain exact incomplete global940 stop and forbid B work',()=>{
  const e=expected(),r=laterFinalReport(e);
  assert.equal(compareLaterSssExitObservation(r.current_sss_exit.observation,e).pass,true);
  assert.equal(validateLaterSssExitTerminal(r.browser_report,r.final_snapshot,e,r.current_sss_exit.unloaded_observation).pass,true);
  assert.equal(validateLaterSssExitFinalization(r,e).C3,false);
  assert.equal(validateLaterSssExitTrace(text(trace(e)),e,r.current_sss_exit.observation).literal_supplied_inputs,940);
  for(const mutate of [x=>x.browser_report.metrics.sourceFrames=535,x=>x.browser_report.metrics.sourceSteps=939,
    x=>x.browser_report.metrics.sourceDraws=-1,x=>x.browser_report.recipe_sha256='0'.repeat(64),
    x=>x.browser_report.complete=true,x=>x.browser_report.pass=true,x=>x.browser_report.frames=940,
    x=>x.browser_report.failures.push('teardown incomplete'),x=>x.browser_report.final_scene=1,
    x=>x.browser_report.source_match.complete=true,x=>x.final_snapshot.running=1,
    x=>x.current_sss_exit.unloaded_observation.terminal.native_unloaded=false,
    x=>x.current_sss_exit.unloaded_observation.terminal.asset_requests_after_leave_boundary=1,
    x=>x.current_sss_exit.unloaded_observation.terminal.asset_requests_at_terminal=1,
    x=>x.current_sss_exit.unloaded_observation.prepared_to_setup={attempted:true},
    x=>x.current_sss_exit.prepared_setup_begin={result:1},x=>x.current_sss_exit.unload_requested=false,
    x=>x.current_sss_exit.unloaded_observation.note.random_seed_hex='00000000',
    x=>x.current_sss_exit.comparison.pass=false,x=>x.cleanup.browser_close_returned=false,
    x=>x.first_error={kind:'runtime'},x=>x.snapshots[0].runtime_error='later failure',
    x=>x.snapshots=Array(2048).fill({}),x=>x.phases[0].result='fail']){
    const bad=structuredClone(r);mutate(bad);assert.throws(()=>validateLaterSssExitFinalization(bad,e));
  }
});
const runnerText=readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
function runnerSlice(first,last) {
  assert.equal(runnerText.split(first).length,2);assert.equal(runnerText.split(last).length,2);
  return runnerText.slice(runnerText.indexOf(first),runnerText.indexOf(last));
}
async function runnerAdmission(e,args,overrides={}) {
  const report={inputs:{recipe:{sha256:e.recipe.sha256}},requested_entity_prefix:false};
  e.source_binding={files:{operand:{path:'modeled-source-input',sha256:'c'.repeat(64)}}};
  const context={path,JSON,OBSERVED_B_REFERENCE,OBSERVED_B_SCHEMA,validateLaterReferenceRecipeInputs,parseArgs:options=>parseArgs({...options,args}),
    validateLaterSssExitExpected,compareLaterSssExitObservation,validateLaterSssExitTrace,
    validateLaterSssExitTerminal,validateLaterSssExitFinalization,compareLaterPreparedStartObservation,compareLaterPreparedSetupObservation,
    validateStock4SssExitExpected,compareStock4SssExitObservation,validateStock4SssExitTerminal,
    validateStock4SssExitTrace,validateStock4SssExitFinalization,
    validateCurrentSssExitExpected,compareCurrentSssExitObservation,validateCurrentSssExitTerminal,
    validateCurrentSssExitTrace,validateCurrentSssExitFinalization,
    diagnostic:false,firstReplayCallbackProbe:false,captureCpuObservations:false,rngDrawProbe:false,
    hitTransitionProbe:false,stopAfter:false,resumeTimingPauses:false,runtimeIncidentRecording:false,
    phaseTimeoutMs:150000,replayTimeoutMs:150000,expectedFrame0Path:null,
    fs:{readFile:async()=>JSON.stringify(e)},statInput:async()=>({sha256:'d'.repeat(64)}),
    digest:async()=> 'c'.repeat(64),browserArtifactInventory:async()=>({equal:true}),
    recipeBytes:new Uint8Array(e.recipe.bytes),report,...overrides};
  const source=runnerSlice('const {values, tokens} = parseArgs','function integer(')+
    runnerSlice('const currentSssExitPath =','const runtimeDataUrl =')+
    runnerSlice('let currentSssExpected,','let browser;')+
    runnerSlice('  if (currentSssExit) {\n    report.inputs.current_sss_expected','  const cpuObservationRowLimit =')+
    'return {currentSssExpected,currentSssPolicy,currentSssLater,currentSssStock4,currentSssStopsBeforeAssets,report};';
  return vm.runInNewContext('(async()=>{'+source+'})()',context);
}
const laterArgs=['--stadium-later-sss-exit','--stadium-current-sss-exit-expected','modeled-expected.json',
  '--stadium-current-sss-exit-expected-sha256','d'.repeat(64),'--artifact-root','modeled-artifacts','--runtime-data','modeled-data'];
test('actual runner CLI and admission select later policy only under explicit finite opt-in',async()=>{
  const admitted=await runnerAdmission(expected(),laterArgs);
  assert.equal(admitted.currentSssLater,true);assert.equal(admitted.currentSssStock4,false);
  assert.equal(admitted.currentSssStopsBeforeAssets,true);
  assert.equal(admitted.currentSssPolicy.compare,compareLaterSssExitObservation);
  assert.equal(admitted.currentSssPolicy.terminal,validateLaterSssExitTerminal);
  assert.equal(admitted.currentSssPolicy.trace,validateLaterSssExitTrace);
  assert.equal(admitted.currentSssPolicy.finalization,validateLaterSssExitFinalization);
  for(const [e,args,extra] of [
    [expected(),laterArgs.slice(1),{}],
    [expected(),['--stadium-later-sss-exit'],{}],
    [expected(),[...laterArgs,'--stadium-current-sss-prepared-start'],{}],
    [expected(),laterArgs,{phaseTimeoutMs:150001}],
    [expected(),laterArgs,{replayTimeoutMs:150001}],
    [expected(),laterArgs,{digest:async()=> '0'.repeat(64)}],
    [expected(),laterArgs,{statInput:async()=>({sha256:'0'.repeat(64)})}],
    [expected(),laterArgs,{browserArtifactInventory:async()=>({equal:false})}],
  ])await assert.rejects(()=>runnerAdmission(e,args,extra));
  for(const mutate of [e=>e.schema='melee-web-stock4-none-sss-exit-host-expectation-v1',
    e=>e.observation_profile=1,e=>e.notification_cursor=270,e=>e.literal_inputs[675].index=0,
    e=>e.literal_inputs[939].pads.pop(),e=>e.original_B_constructor_GO='observed']){
    const bad=expected();mutate(bad);await assert.rejects(()=>runnerAdmission(bad,laterArgs));
  }
});
test('actual runner loop waits for returned later stop then unloads once and checks actual policy',async()=>{
 for(const preparedOnly of [false,true]){
  const e=expected(),before=laterEndpoint(e),after=laterEndpoint(e,true),browserReport=laterReplayReport(e);
  const prepared=preparedLaterReceipt(e);
  const admitted=await runnerAdmission(e,preparedOnly?[...laterArgs,'--stadium-later-sss-prepared-start']:laterArgs);let unloaded=false,observations=0,starts=0,unloads=0,preparedCalls=0;
  const report=admitted.report,writes=[];
  const handle={evaluate:async fn=>fn({observePreparedStart:()=>{++preparedCalls;return {busy:false,value:prepared};},state:()=>({owned:true,failure:null}),observe:()=>{
    ++observations;return {busy:false,value:unloaded?after:observations===1?
      {...before,terminal:{...before.terminal,complete:false}}:before};}})};
  const page={evaluateHandle:async(fn,options)=>{
    assert.equal(fn,installCurrentSssExitReplayArm);assert.equal(options.observationProfile,2);
    assert.equal(options.preparedStartProbe,preparedOnly);return handle;},locator:selector=>({click:async()=>{
      if(selector==='#retail-replay-start')++starts;else{assert.equal(selector,'#unload');++unloads;unloaded=true;}}})};
  const context={...admitted,currentSssExit:true,currentSssPreparedStart:false,laterSssPreparedStart:preparedOnly,
    laterSssPreparedSetup:false,preparedSetupRequested:false,
    currentSssRecipeBytes:Array(328).fill(0),installCurrentSssExitReplayArm,page,
    observePageOperation:async(label,fn)=>fn(),phase:async(label,fn)=>fn(),
    replayTimeoutMs:1000,replayPollMs:0,setTimeout,Date,Promise,
    write:async(name,value)=>writes.push({name,value}),pageErrors:[],report,
    snapshot:async()=>({phase:0,running:0,replay_downloads:unloaded?['retail-browser-report.json']:[],replay_report:browserReport})};
  const code=runnerSlice('  if (currentSssExit) {\n    currentSssArm =','  } else if (firstReplayCallbackProbe) await firstReplayCallbackPhase();')+'  }';
  await vm.runInNewContext('(async()=>{let currentSssArm;'+code+'})()',context);
  assert.equal(starts,1);assert.equal(unloads,1);assert.equal(observations,3);
  assert.equal(report.current_sss_exit.comparison.pass,true);assert.equal(report.current_sss_exit.terminal.pass,true);
  assert.equal(report.current_sss_exit.runtime_replay,'FAIL_INCOMPLETE_EXPLICIT_SSS_STOP');
  assert.equal(writes.filter(x=>x.name==='current-sss-exit-unloaded-observation.json').length,1);
  assert.equal(preparedCalls,Number(preparedOnly));
  if(preparedOnly){assert.equal(report.current_sss_exit.prepared_start_comparison.pass,true);assert.equal(report.current_sss_exit.prepared_setup_begin,undefined);}
 }
});

test('actual runner admission keeps legacy first stock4 and TIME policies without later opt-in',async()=>{
  const first=expected();first.schema='melee-web-stock4-none-sss-exit-host-expectation-v1';first.observation_profile=1;
  first.notification_cursor=270;first.literal_inputs=first.literal_inputs.slice(0,270);
  first.source.match_index=0;first.source.sequence=1639;
  first.prepared_profile.entry=first.preceding_profile.entry;first.prepared_profile.setup=first.preceding_profile.setup;
  const legacyArgs=laterArgs.slice(1),a=await runnerAdmission(first,legacyArgs);
  assert.equal(a.currentSssLater,false);assert.equal(a.currentSssStock4,true);
  assert.equal(a.currentSssPolicy.compare,compareStock4SssExitObservation);
  assert.equal(a.currentSssPolicy.terminal,validateStock4SssExitTerminal);
  assert.equal(a.currentSssStopsBeforeAssets,true);
  const time=structuredClone(first);time.schema='melee-web-current-sss-exit-host-expectation-v1';
  time.recipe.frames=2013;
  time.expected_runtime_refusal='Original VS entry produced an unsupported Stadium diagnostic payload';
  const b=await runnerAdmission(time,legacyArgs);
  assert.equal(b.currentSssLater,false);assert.equal(b.currentSssStock4,false);
  assert.equal(b.currentSssPolicy.compare,compareCurrentSssExitObservation);
  assert.equal(b.currentSssPolicy.terminal,validateCurrentSssExitTerminal);
  assert.equal(b.currentSssStopsBeforeAssets,false);
});
test('actual runner finalizer classifies only later component pass after strict trace and owned cleanup',async()=>{
  const e=expected(),base=laterFinalReport(e),admitted=await runnerAdmission(e,laterArgs);
  base.current_sss_exit.terminal=validateLaterSssExitTerminal(base.browser_report,base.final_snapshot,e,
    base.current_sss_exit.unloaded_observation);
  assert.equal(base.current_sss_exit.terminal.pass,true);
  const finalizer=runnerSlice('    if (currentSssExit) {\n      report.original_replay_capture_result',
    "\n  }\n  await write('report.json', report);\n  if (report.failure) await write('failure.txt'");
  const outcome=runnerText.slice(runnerText.lastIndexOf('\nif (diagnostic) {'));
  for(const mutate of [null,r=>r.current_sss_exit.terminal.pass=false,
    r=>r.close_error='close failure',r=>r.current_sss_exit.wrapper_restored=false,
    r=>r.browser_errors.push('page error'),r=>r.unexpected_requests.push('foreign request'),
    r=>r.current_sss_exit.observation.note.session_ticks=940]){
    const report=structuredClone(base);report.result='fail';report.current_sss_exit.wrapper_restored=true;
    report.browser_errors=[];report.unexpected_requests=[];report.artifact_inventory_pre={equal:true};mutate?.(report);
    const process={exitCode:0},messages=[];
    const context={currentSssExit:true,currentSssLater:true,currentSssStock4:false,currentSssPreparedStart:false,laterSssPreparedStart:false,laterSssPreparedSetup:false,
      currentSssPolicy:admitted.currentSssPolicy,currentSssExpected:e,currentSssTrace:text(trace(e)),report,
      browserArtifactInventory:async()=>({equal:true}),diagnostic:false,process,console:{log:s=>messages.push(s)}};
    await vm.runInNewContext('(async()=>{'+finalizer+'})()',context);
    vm.runInNewContext(outcome,context);
    if(!mutate){assert.equal(report.result,'later_sss_component_pass_replay_stopped');assert.equal(process.exitCode,0);
      assert.equal(report.current_sss_exit.trace_comparison.literal_supplied_inputs,940);
      assert.equal(report.browser_report.complete,false);assert.match(messages[0],/no B Entry.*C3/);}
    else{assert.equal(report.result,'fail');assert.equal(process.exitCode,1);}
  }
});


// Synthetic receipt controls: source/native observation and ownership are separate C++ controls.
function preparedLaterReceipt(e){
 const a=e.prepared_profile.entry.declared_setup;
 Object.assign(a,{item_mask_hex:'ffffffffffffffff',damage_ratio_bits:'3f800000',game_speed_bits:'3f800000'});
 a.players.forEach(p=>p.rumble_enabled=true);
 e.prepared_profile.setup.declared_setup=structuredClone(a);
 return {schema:'melee-web-stadium-prepared-start-before-assets-v2',
 scope:'source_prepared_start_after_sss_leave_before_assets',phase:'prepared_entry',observation_profile:2,
 captured:true,source_scene:0,host_entered:false,waiting_for_audio_retained_until_unload:true,
 replay_cursor:940,local_order_rows:1887,callback_source_steps:0,callback_source_draws:0,
 asset_requests_after_leave_boundary:0,world_generation:6,hsd_pending_loads_at_return:0,
 bank_transport_active_at_return:true,bank_transport_configured_at_return:true,bank_transport_busy_at_return:false,
 random_seed_hex:'7fc17cf1',start:{rules:{match_kind:1,is_stock:1,is_vs:1,stage_kind:3,xB:-1,
 time_limit:0,timer_enabled:0,timer_counts_up:0,disable_pausing:0,is_teams:0,item_mask:'ffffffffffffffff',
 damage_ratio_bits:'3f800000',game_speed_bits:'3f800000',friendly_fire:0,timer_shows_hours:0,x14:0},
 players:[1,0].map(color=>({slot:0,slot_type:0,ckind:8,color,stocks:4,flags_c:192})).concat(Array.from({length:4},()=>({slot_type:3})))},
 menu_draw_reference:'UNKNOWN',runtime_admission:false,Entry:false,C3:false};
}
test('later prepared Start uses shared strict projection and exact940 pre-assets identity',()=>{
 const e=expected(),a=preparedLaterReceipt(e);
 assert.equal(compareLaterPreparedStartObservation(a,e).pass,true);
 for(const mutate of [x=>x.observation_profile=1,x=>x.replay_cursor=270,x=>x.local_order_rows=543,
 x=>x.asset_requests_after_leave_boundary=1,x=>x.random_seed_hex='e2624ac3',x=>x.bank_transport_busy_at_return=true,
 x=>x.start.rules.is_stock=0,x=>x.start.players[0].stocks=0,x=>x.start.rules.stage_kind=32,
 x=>x.start.players[2].slot_type=0,x=>x.callback_source_steps=1,x=>x.world_generation=0]){
 const bad=structuredClone(a);mutate(bad);assert.throws(()=>compareLaterPreparedStartObservation(bad,e));}
 const r=laterFinalReport(e);r.later_sss_prepared_start=true;
 r.current_sss_exit.prepared_start_comparison=compareLaterPreparedStartObservation(a,e);
 r.current_sss_exit.prepared_start_observation=a;
 assert.equal(validateLaterSssExitFinalization(r,e).pass,true);
 r.current_sss_exit.prepared_setup_begin={result:1};assert.throws(()=>validateLaterSssExitFinalization(r,e));
});
test('actual page profile2 prepared observation never exposes Setup continuation',()=>{
 const recipe=Array(328).fill(37),heap=new Uint8Array(1024);heap.set(recipe,16);
 let preparedCalls=0,setupCalls=0;
 const mod={HEAPU8:heap,UTF8ToString:()=>JSON.stringify({captured:true}),stadiumMatchedSetupNativeIdle:()=>true,
 _melee_web_native_menu_replay:()=>1,_melee_web_native_menu_stadium_current_sss_exit_arm_profile:()=>1,
 _melee_web_native_menu_stadium_current_sss_exit_observe:()=>1,
 _melee_web_native_menu_stadium_current_sss_prepared_start_observe:()=>{++preparedCalls;return 1;},
 _melee_web_native_menu_stadium_current_sss_prepared_setup_begin:()=>{++setupCalls;return 1;}};
 try{globalThis.Module=mod;
 const owner=installCurrentSssExitReplayArm({recipeBytes:recipe,observationProfile:2,preparedStartProbe:true});
 assert.equal(mod._melee_web_native_menu_replay(16,328,1),1);
 assert.equal(owner.observePreparedStart().value.captured,true);
 assert.throws(()=>owner.continuePreparedStart({captured:true}),/one-use owner/);
 assert.equal(setupCalls,0);assert.equal(preparedCalls,1);
 const kept=mod._melee_web_native_menu_stadium_current_sss_prepared_start_observe;
 mod._melee_web_native_menu_stadium_current_sss_prepared_start_observe=()=>1;
 assert.throws(()=>owner.observePreparedStart(),/retained Module/);
 mod._melee_web_native_menu_stadium_current_sss_prepared_start_observe=kept;owner.dispose();
 }finally{delete globalThis.Module;}
});
test('actual CLI opt-in selects profile2 observation without Setup mode',async()=>{
 const e=expected(),a=await runnerAdmission(e,[...laterArgs,'--stadium-later-sss-prepared-start']);
 assert.equal(a.currentSssPolicy.preparedStart,compareLaterPreparedStartObservation);
 assert.equal(a.report.later_sss_prepared_start,true);
 await assert.rejects(runnerAdmission(e,laterArgs.slice(1).concat('--stadium-later-sss-prepared-start')));
 await assert.rejects(runnerAdmission(e,[...laterArgs,'--stadium-current-sss-prepared-start','--stadium-later-sss-prepared-start']));
});

// Synthetic zero-action Setup receipt; constructor/RNG source execution is not modeled here.
function laterSetupReceipt(e) {
 const prepared=preparedLaterReceipt(e),base=laterEndpoint(e);
 base.notification_coordinates.order=1887;base.note.world_generation=6;
 Object.assign(base.terminal,{asset_requests_after_leave_boundary:1,asset_requests_at_terminal:0,
   waiting_for_audio_retained_until_unload:false,waiting_for_audio_at_terminal:true,waiting_for_audio_current:false});
 base.prepared_to_setup={attempted:true,approved:true,consumed:true,failed:false,error:null,
   asset_requests_at_handoff:0,asset_requests_current_after_handoff:1,waiting_for_audio_at_handoff:true,
   waiting_for_audio_current:false,waiting_for_audio_retired:true,setup:{
   handoff_origin:'current_sss_prepared_later_940',attempted:true,requested:true,import_committed:true,
   menu_retired:true,parent_seed_owner_value_restored:true,complete:true,captured:true,failed:false,
   construction_complete:true,source_frames:0,host_tick_calls:0,host_draw_calls:0,requested_generation:4,
   committed_generation:4,prior_world_generation:6,match_world_generation:7,
   current_owners:{host:true,host_entered:false,match:true,cache:true,source_world_exists:true,running:false,
     pending:false,first_use_draw_pending:false,pending_scope_generation:0,asset_generation:4,active_files:2},
   actual:{phase:'vs_setup',random_seed_hex:e.prepared_profile.setup.rng.value_hex,start:prepared.start},
   stadium_setup_state:{schema:'melee-web-stadium-zero-tick-setup-state-v1',world_generation:7,
     world_ticks:0,source_frame:0,map_id:2,constructor_callback_count:1,source_parameter_roots_agree:true,
     ground_source_heap_handle:0,ground_requested_bytes:256,ground_allocation_generation:8,
     object_source_heap_handle:0,object_requested_bytes:128,object_allocation_generation:9}}};
 return base;
}
test('later Setup checks retained Setup seed distinctly from Entry and refuses changed zero-action owners',()=>{
 const e=expected(),a=laterSetupReceipt(e);
 assert.equal(e.prepared_profile.entry.rng.value_hex,'7fc17cf1');
 assert.equal(e.prepared_profile.setup.rng.value_hex,'25d557a7');
 assert.equal(compareLaterPreparedSetupObservation(a,e).pass,true);
 assert.throws(()=>compareStock4SssExitObservation(a,e));
 for(const mutate of [x=>x.observation_profile=1,x=>x.selected_occurrence=0,x=>x.terminal.cursor=270,
   x=>x.prepared_to_setup.setup.handoff_origin='current_sss_prepared_stock4_270',
   x=>x.prepared_to_setup.setup.actual.random_seed_hex='7fc17cf1',
   x=>x.prepared_to_setup.setup.actual.start.rules.is_stock=0,
   x=>x.prepared_to_setup.setup.source_frames=1,x=>x.prepared_to_setup.setup.host_tick_calls=1,
   x=>x.prepared_to_setup.setup.parent_seed_owner_value_restored=false,
   x=>x.prepared_to_setup.setup.committed_generation++,x=>x.prepared_to_setup.setup.match_world_generation=6,
   x=>x.prepared_to_setup.setup.stadium_setup_state.constructor_callback_count=2]){
   const bad=structuredClone(a);mutate(bad);assert.throws(()=>compareLaterPreparedSetupObservation(bad,e));}
 const r=laterFinalReport(e),prepared=preparedLaterReceipt(e),done=laterSetupReceipt(e);
 r.current_sss_exit.observation.note.world_generation=6;r.current_sss_exit.observation.notification_coordinates.order=1887;
 r.later_sss_prepared_start=r.later_sss_prepared_setup=true;
 Object.assign(r.current_sss_exit,{prepared_start_observation:prepared,
   prepared_start_comparison:compareLaterPreparedStartObservation(prepared,e),
   prepared_setup_begin:{result:1,accepted_receipt_rechecked:true},prepared_setup_observation:done,
   prepared_setup_comparison:compareLaterPreparedSetupObservation(done,e)});
 Object.assign(r.current_sss_exit.observation.terminal,{asset_requests_at_terminal:0,
   waiting_for_audio_at_terminal:true,waiting_for_audio_current:true});
 r.current_sss_exit.unloaded_observation=structuredClone(done);
 r.current_sss_exit.unloaded_observation.terminal.native_unloaded=true;
 assert.equal(validateLaterSssExitFinalization(r,e).pass,true);
 for(const mutate of [x=>delete x.later_sss_prepared_setup,x=>x.current_sss_exit.prepared_setup_begin.result=0,
   x=>x.current_sss_exit.prepared_setup_begin.accepted_receipt_rechecked=false,
   x=>x.current_sss_exit.unloaded_observation.prepared_to_setup.consumed=false,
   x=>x.current_sss_exit.unloaded_observation.terminal.native_unloaded=false]){
   const bad=structuredClone(r);mutate(bad);assert.throws(()=>validateLaterSssExitFinalization(bad,e));}
});
test('actual page later Setup capability is explicit immutable and one-use',()=>{
 const recipe=Array(328).fill(37),heap=new Uint8Array(1024);heap.set(recipe,16);
 for(const fault of [null,'changed-receipt','getter','setup','module','duplicate-approval','reentrant']){
  const receipt={captured:true,profile:2},originalReceipt=structuredClone(receipt);let calls=0,owner;
  const mod={HEAPU8:heap,UTF8ToString:()=>JSON.stringify(receipt),stadiumMatchedSetupNativeIdle:()=>true,
    _melee_web_native_menu_replay:()=>1,_melee_web_native_menu_stadium_current_sss_exit_arm_profile:()=>1,
    _melee_web_native_menu_stadium_current_sss_exit_observe:()=>1,
    _melee_web_native_menu_stadium_current_sss_prepared_start_observe:()=>1,
    _melee_web_native_menu_stadium_current_sss_prepared_setup_begin:()=>{++calls;
      if(fault==='reentrant')assert.throws(()=>owner.continuePreparedStart(originalReceipt),/one-use owner/);
      const approve=globalThis.__meleeWebStadiumCurrentSssPreparedStartApproved;
      assert.equal(approve(),true);if(fault==='duplicate-approval')return approve()?1:0;return 1;}};
  try{globalThis.Module=mod;
   owner=installCurrentSssExitReplayArm({recipeBytes:recipe,observationProfile:2,preparedStartProbe:true,laterPreparedSetup:true});
   assert.equal(mod._melee_web_native_menu_replay(16,328,1),1);
   if(fault==='changed-receipt')receipt.captured=false;
   if(fault==='getter')mod._melee_web_native_menu_stadium_current_sss_prepared_start_observe=()=>1;
   if(fault==='setup')mod._melee_web_native_menu_stadium_current_sss_prepared_setup_begin=()=>1;
   if(fault==='module')globalThis.Module={...mod};
   if(fault&&fault!=='reentrant')assert.throws(()=>owner.continuePreparedStart(originalReceipt));
   else assert.equal(owner.continuePreparedStart(originalReceipt).accepted_receipt_rechecked,true);
   assert.equal(calls,(!fault||['reentrant','duplicate-approval'].includes(fault))?1:0);
   assert.throws(()=>owner.continuePreparedStart(originalReceipt));
   assert.equal(Object.hasOwn(globalThis,'__meleeWebStadiumCurrentSssPreparedStartApproved'),false);
  }finally{delete globalThis.Module;delete globalThis.__meleeWebStadiumCurrentSssPreparedStartApproved;}
 }
 assert.throws(()=>installCurrentSssExitReplayArm({recipeBytes:recipe,observationProfile:1,preparedStartProbe:true,laterPreparedSetup:true}));
});
test('actual CLI requires later Setup plus prepared-Start and keeps readonly mode separate',async()=>{
 const args=[...laterArgs,'--stadium-later-sss-prepared-start','--stadium-later-sss-prepared-setup'];
 const a=await runnerAdmission(expected(),args);
 assert.equal(a.currentSssPolicy.preparedSetup,compareLaterPreparedSetupObservation);
 assert.equal(a.report.later_sss_prepared_setup,true);
 const readonly=await runnerAdmission(expected(),[...laterArgs,'--stadium-later-sss-prepared-start']);
 assert.equal(readonly.currentSssPolicy.preparedSetup,undefined);
 for(const bad of [[...laterArgs,'--stadium-later-sss-prepared-setup'],
   args.filter(x=>x!=='--stadium-later-sss-exit'),[...args,'--stadium-current-sss-prepared-start']])
   await assert.rejects(()=>runnerAdmission(expected(),bad));
});

test('actual runner Setup branch compares prepared receipt before one handoff then bounded completion and unload',async()=>{
 const e=expected(),prepared=preparedLaterReceipt(e),before=laterEndpoint(e),done=laterSetupReceipt(e);
 before.note.world_generation=6;before.notification_coordinates.order=1887;
 Object.assign(before.terminal,{asset_requests_at_terminal:0,waiting_for_audio_at_terminal:true,waiting_for_audio_current:true});
 const after=structuredClone(done);after.terminal.native_unloaded=true;
 const admitted=await runnerAdmission(e,[...laterArgs,'--stadium-later-sss-prepared-start','--stadium-later-sss-prepared-setup']);
 let unloaded=false,continued=false,observations=0,starts=0,unloads=0,continuations=0;
 const report=admitted.report,writes=[];
 const scope={state:()=>({owned:true,failure:null}),observePreparedStart:()=>({busy:false,value:prepared}),
   continuePreparedStart:receipt=>{assert.deepEqual(receipt,prepared);assert.equal(report.current_sss_exit.prepared_start_comparison.pass,true);
     assert.equal(continued,false);continued=true;++continuations;return {result:1,accepted_receipt_rechecked:true};},
   observe:()=>{++observations;return {busy:false,value:unloaded?after:continued?done:before};}};
 const handle={evaluate:async(fn,arg)=>fn(scope,arg)};
 const page={evaluateHandle:async(fn,options)=>{assert.equal(options.observationProfile,2);assert.equal(options.laterPreparedSetup,true);return handle;},
   locator:selector=>({click:async()=>{if(selector==='#retail-replay-start')++starts;else{assert.equal(selector,'#unload');++unloads;unloaded=true;}}})};
 const context={...admitted,currentSssExit:true,currentSssPreparedStart:false,laterSssPreparedStart:true,laterSssPreparedSetup:true,preparedSetupRequested:true,
   currentSssRecipeBytes:Array(328).fill(0),installCurrentSssExitReplayArm,page,
   // Exact production callback, passed through modeled evaluateHandle; no native approval success claim.
   continuePreparedStartInPage,
   observePageOperation:async(label,fn)=>fn(),phase:async(label,fn)=>fn(),replayTimeoutMs:1000,replayPollMs:0,setTimeout,Date,Promise,
   write:async(name,value)=>writes.push({name,value}),pageErrors:[],report,
   snapshot:async()=>({phase:0,running:0,replay_downloads:unloaded?['retail-browser-report.json']:[],replay_report:laterReplayReport(e)})};
 const code=runnerSlice('  if (currentSssExit) {\n    currentSssArm =','  } else if (firstReplayCallbackProbe) await firstReplayCallbackPhase();')+'  }';
 await vm.runInNewContext('(async()=>{let currentSssArm;'+code+'})()',context);
 assert.equal(starts,1);assert.equal(unloads,1);assert.equal(continuations,1);assert.equal(observations,3);
 assert.equal(report.current_sss_exit.prepared_setup_comparison.pass,true);assert.equal(report.current_sss_exit.terminal.pass,true);
 assert.equal(report.current_sss_exit.runtime_replay,'FAIL_INCOMPLETE_EXPLICIT_SSS_STOP');
 assert.equal(writes.filter(x=>x.name==='current-sss-prepared-to-setup-observation.json').length,1);
});

// Synthetic new-context mechanism fixtures keep original/native parity unclaimed.
function observedExpected(){
  const e=expected(),d=OBSERVED_B_REFERENCE;
  Object.assign(e,{schema:OBSERVED_B_SCHEMA,observation_profile:d.profile,
    notification_cursor:d.first[6],menu_session_ticks:d.menu_ticks,
    original_B_constructor_GO:d.original_B_constructor_GO,
    reference_context:structuredClone(d),reference_order_contract:d.notification_order_contract,
    recipe:{version:8,frames:d.frames,bytes:d.recipe_bytes,sha256:d.recipe_sha256}});
  e.preceding_spans=d.first.slice(0,6).map((first,i)=>({scene:LATER_REFERENCE_CONTEXTS.scenes[i],first_frame:first,last_frame:d.last[i]}));
  e.literal_inputs=Array.from({length:d.first[6]},(_,index)=>({index,
    scene:e.preceding_spans.find(s=>index>=s.first_frame&&index<=s.last_frame).scene,
    pads:[0,1,2,3].map(port=>(port+index%200).toString(16).padStart(2,'0').repeat(11))}));
  Object.assign(e.source,{sequence:d.sss_sequence,source_tick:d.sss_tick,draw_ordinal:d.sss_draw});
  e.source.rng.value_hex=d.entry_seed;e.expected.random_seed_hex=d.entry_seed;e.expected.scene_frame=d.sss_tick;
  for(const [name,sequence,seed] of [['entry',d.entry_sequence,d.entry_seed],['setup',d.setup_sequence,d.setup_seed]]){
    const row=e.prepared_profile[name];Object.assign(row,{sequence,source_tick:d.sss_tick});row.rng.value_hex=seed;
  }
  Object.assign(e.preceding_profile.entry,{sequence:d.a_entry_sequence,source_tick:d.a_tick});
  Object.assign(e.preceding_profile.setup,{sequence:d.a_setup_sequence,source_tick:d.a_tick});
  return e;
}
function observedTrace(e){
  const rows=[{record:'header',schema:'melee-web-port-session-diagnostic',version:1,
    frames_requested:e.recipe.frames,draw_state:'not_captured',comparison:'not_run'}];let order=0;
  for(const row of e.literal_inputs){
    if(row.index===OBSERVED_B_REFERENCE.first[2])rows.push({record:'session_match_enter_complete',rng:0x083a62a9,pad_state_hex:'00'.repeat(822)});
    rows.push({record:'session_frame',index:row.index,scene:row.scene,supplied_inputs:row.pads,rng:0,pad_state_hex:'00'.repeat(822)});
    rows.push({record:'current_sss_exit_order',order:order++,kind:'tick_return',replay_cursor:row.index,
      source_steps:1,source_draws:0,menu_draw_reference:'UNKNOWN'});
    // A real emitted zero-step draw is legal: finish accounting occurs later.
    if(row.index===7)rows.push({record:'current_sss_exit_order',order:order++,kind:'draw_aurora_return',
      replay_cursor:row.index+1,source_steps:0,source_draws:0,menu_draw_reference:'UNKNOWN'});
  }
  return {rows,actual:{reference_descriptor:OBSERVED_B_REFERENCE.id,notification_coordinates:{order},notification_order_count:order+1}};
}
test('new reference identity refuses mixed or mutated contexts and preserves old admission',()=>{
  const e=observedExpected();assert.equal(validateLaterSssExitExpected(e),e);
  assert.equal(validateLaterSssExitExpected(expected()).observation_profile,2);
  for(const mutate of [v=>v.reference_context.first[6]=940,v=>v.reference_context.id='foreign',
    v=>v.observation_profile=2,v=>v.recipe.sha256='0'.repeat(64),v=>v.recipe.bytes--,
    v=>v.prepared_profile.setup.rng.value_hex='25d557a7',v=>v.source.sequence=4234,
    v=>v.preceding_profile.entry.source_tick=125,v=>v.literal_inputs[43].pads.pop()]){
    const bad=structuredClone(e);mutate(bad);assert.throws(()=>validateLaterSssExitExpected(bad));
  }
});
test('new emitted trace brackets every frame and accepts actual zero-step draw shape',()=>{
  const e=observedExpected(),{rows,actual}=observedTrace(e);
  assert.equal(validateLaterSssExitTrace(text(rows),e,actual).literal_supplied_inputs,943);
  for(const mutate of [r=>{const i=r.findIndex(x=>x.record==='session_frame'&&x.index===1);const next=r.splice(i,1)[0];
      r.splice(r.findIndex(x=>x.record==='current_sss_exit_order'&&x.kind==='tick_return'&&x.replay_cursor===0),0,next);},
    r=>r.splice(r.findIndex(x=>x.record==='current_sss_exit_order'&&x.replay_cursor===10),1),
    r=>{const i=r.findIndex(x=>x.record==='session_frame'&&x.index===10);r.splice(i,0,structuredClone(r[i]));},
    r=>{const i=r.findIndex(x=>x.record==='current_sss_exit_order'&&x.replay_cursor===10);[r[i],r[i+2]]=[r[i+2],r[i]];},
    r=>r.find(x=>x.record==='session_frame'&&x.index===43).supplied_inputs[0]='ff'.repeat(11)]){
    const bad=structuredClone(rows);mutate(bad);assert.throws(()=>validateLaterSssExitTrace(text(bad),e,actual));
  }
  for(const mutate of [a=>a.notification_order_count++,a=>a.reference_descriptor='foreign',a=>a.notification_coordinates.order--]){
    const bad=structuredClone(actual);mutate(bad);assert.throws(()=>validateLaterSssExitTrace(text(rows),e,bad));
  }
});
const gate1Fixture=process.env.MELEE_LATER_B_GATE1_FIXTURE;
test('retained exact2013 recipe passes actual layout, PAD and CLI admission; corrupt transports refuse',
  {skip:!gate1Fixture},async()=>{
    const e=JSON.parse(readFileSync(path.join(gate1Fixture,'expected.json'),'utf8'));
    const raw=readFileSync(path.join(gate1Fixture,'recipe.mwrc'));
    assert.equal(validateLaterReferenceRecipeInputs(raw,e),true);
    const args=[...laterArgs,'--stadium-later-sss-prepared-start','--stadium-later-sss-prepared-setup',
      '--stadium-later-sss-reference',OBSERVED_B_REFERENCE.id];
    const admitted=await runnerAdmission(structuredClone(e),args,{recipeBytes:raw});
    assert.equal(admitted.currentSssExpected.observation_profile,3);
    assert.equal(admitted.currentSssPolicy.compare,compareLaterSssExitObservation);
    const trailer=raw.length-(2+12*OBSERVED_B_REFERENCE.first.length);
    for(const [mutate,reason] of [
      [b=>b.writeUInt32BE(7,4),/v8 header/],
      [b=>b.writeUInt32BE(2006,12),/v8 header/],
      [b=>b.writeUInt32BE(0,24),/v8 header/],
      [b=>b.writeUInt16BE(11,trailer),/span trailer/],
      [b=>b[trailer+3]=1,/span flags/],
      [b=>b.writeUInt32BE(1,trailer+6),/span flags/],
      [b=>b.writeUInt32BE(2013,raw.length-4),/span flags/],
      [b=>b[100]^=1,/raw recipe identity/]]){
      const bad=Buffer.from(raw);mutate(bad);
      assert.throws(()=>validateLaterReferenceRecipeInputs(bad,e),reason);
      await assert.rejects(()=>runnerAdmission(structuredClone(e),args,{recipeBytes:bad}));
    }
    for(const rawBad of [raw.subarray(0,-1),Buffer.concat([raw,Buffer.from([0])])])
      assert.throws(()=>validateLaterReferenceRecipeInputs(rawBad,e),/transport size/);
    const bad=structuredClone(e);bad.literal_inputs[43].pads[0]='ff'.repeat(11);
    assert.throws(()=>validateLaterReferenceRecipeInputs(raw,bad),/literal four-port prefix/);
    await assert.rejects(()=>runnerAdmission(structuredClone(e),laterArgs,{recipeBytes:raw}));
    await assert.rejects(()=>runnerAdmission(expected(),args,{recipeBytes:raw}));
    const foreign=structuredClone(e);foreign.reference_context.first[6]=940;
    await assert.rejects(()=>runnerAdmission(foreign,args,{recipeBytes:raw}));
  });

test('new prepared receipts bind captured parent order and distinct zero-action Setup identity',()=>{
 const e=observedExpected(),prepared=preparedLaterReceipt(e),parent=laterEndpoint(e);
 const {actual}=observedTrace(e),d=OBSERVED_B_REFERENCE;
 Object.assign(parent,{observation_profile:3,reference_descriptor:d.id,notification_order_count:actual.notification_order_count,
   original_B_constructor_GO:d.original_B_constructor_GO});
 parent.notification_coordinates={...parent.notification_coordinates,replay_cursor:943,order:actual.notification_coordinates.order};
 parent.terminal.cursor=943;parent.note.session_ticks=d.menu_ticks;
 Object.assign(prepared,{observation_profile:3,replay_cursor:943,local_order_rows:parent.notification_coordinates.order,
   random_seed_hex:d.entry_seed});
 assert.equal(compareLaterPreparedStartObservation(prepared,e,parent).pass,true);
 assert.throws(()=>compareLaterPreparedStartObservation(prepared,e),/notification owner/);
 for(const mutate of [v=>v.local_order_rows++,v=>v.observation_profile=2,v=>v.replay_cursor=940,
   v=>v.random_seed_hex='7fc17cf1',v=>v.callback_source_steps=1]){
  const bad=structuredClone(prepared);mutate(bad);assert.throws(()=>compareLaterPreparedStartObservation(bad,e,parent));
 }
 const setup=laterSetupReceipt(e);
 Object.assign(setup,{observation_profile:3,reference_descriptor:d.id,notification_order_count:actual.notification_order_count,
   original_B_constructor_GO:d.original_B_constructor_GO});
 Object.assign(setup.notification_coordinates,{replay_cursor:943,order:parent.notification_coordinates.order});
 setup.terminal.cursor=943;setup.note.session_ticks=d.menu_ticks;
 setup.prepared_to_setup.setup.handoff_origin=d.handoff_origin;
 assert.equal(compareLaterPreparedSetupObservation(setup,e).pass,true);
 for(const mutate of [v=>v.prepared_to_setup.setup.handoff_origin='current_sss_prepared_later_940',
   v=>v.prepared_to_setup.setup.actual.random_seed_hex=d.entry_seed,
   v=>v.prepared_to_setup.setup.source_frames=1,v=>v.notification_order_count++,
   v=>v.prepared_to_setup.setup.parent_seed_owner_value_restored=false]){
  const bad=structuredClone(setup);mutate(bad);assert.throws(()=>compareLaterPreparedSetupObservation(bad,e));
 }
});
