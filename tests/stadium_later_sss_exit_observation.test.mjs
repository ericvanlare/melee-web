import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {validateLaterSssExitExpected, validateLaterSssExitTrace, compareLaterSssExitObservation,
  validateLaterSssExitTerminal, validateLaterSssExitFinalization} from '../scripts/stadium_later_sss_exit_observation.mjs';
import {validateStock4SssExitExpected, compareStock4SssExitObservation, validateStock4SssExitTrace,
  validateStock4SssExitTerminal, validateStock4SssExitFinalization} from '../scripts/stadium_stock4_sss_exit_observation.mjs';
import {installCurrentSssExitReplayArm, validateCurrentSssExitExpected, compareCurrentSssExitObservation,
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
  const context={path,JSON,parseArgs:options=>parseArgs({...options,args}),
    validateLaterSssExitExpected,compareLaterSssExitObservation,validateLaterSssExitTrace,
    validateLaterSssExitTerminal,validateLaterSssExitFinalization,
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
  const e=expected(),before=laterEndpoint(e),after=laterEndpoint(e,true),browserReport=laterReplayReport(e);
  const admitted=await runnerAdmission(e,laterArgs);let unloaded=false,observations=0,starts=0,unloads=0;
  const report=admitted.report,writes=[];
  const handle={evaluate:async fn=>fn({state:()=>({owned:true,failure:null}),observe:()=>{
    ++observations;return {busy:false,value:unloaded?after:observations===1?
      {...before,terminal:{...before.terminal,complete:false}}:before};}})};
  const page={evaluateHandle:async(fn,options)=>{
    assert.equal(fn,installCurrentSssExitReplayArm);assert.equal(options.observationProfile,2);
    assert.equal(options.preparedStartProbe,false);return handle;},locator:selector=>({click:async()=>{
      if(selector==='#retail-replay-start')++starts;else{assert.equal(selector,'#unload');++unloads;unloaded=true;}}})};
  const context={...admitted,currentSssExit:true,currentSssPreparedStart:false,
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
    const context={currentSssExit:true,currentSssLater:true,currentSssStock4:false,currentSssPreparedStart:false,
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
