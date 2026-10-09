import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {callbackSteps,validateFinalSdCapture,checkDeclaredPair,checkNeutralTimeout,checkMatchObservation,runBoundedSdDeparture,SD_BROWSER_LIMITS,checkFreshOrdinaryEntry,runSubsequentOrdinaryEntry} from './vs_sudden_death_browser_route.mjs';
const counter={status:'installed',invalid_phase_steps:0,invalid_preparation_count:0,
  unknown_reason_count:0,reason_counts:Array(10).fill(0),phase_source_steps:Array(16).fill(0)};
function pair(sd=false){return {leg:sd?'sudden_death':'vs',observed_player_source_slots:[0,1],
  prior_vs_source_frames:3600,prior_vs_terminal:{outcome:1,winners:[0,1]},
  rules:{stage:32,match_kind:1,is_teams:0,item_frequency:-1,timer_enabled:sd?0:1,
    time_limit:60,is_stock:sd?0:1,is_vs:sd?0:1,source_sudden_death_flag:sd?1:0,
    player_stocks:sd?[1,1]:[4,4]},
  players:[0,1].map(i=>({source_player_index:i,source_port:i,source_character:8,fighter:0,
    human:true,slot_type:0,source_stocks:sd?1:4,stocks:sd?1:4,
    source_initial_damage:sd?300:0,damage_percent:sd?300:0,source_color:i}))};}

function freshPair(frame=1) {
  const match=pair();delete match.prior_vs_terminal;
  Object.assign(match,{prior_vs_source_frames:0,ready:true,paused:false,ending:false,
    complete:false,outcome:0,frame});
  match.players.forEach(player=>player.source_slot=0);
  return match;
}
const freshState={phase:7,running:1,pause_present:true,pause_disabled:false,
  error:null,diagnostics:'Completed matches: 1 · raw PAD: none'};
test('fresh ordinary entry rejects retained SD, wrong identities/rules and non-ready owners',()=>{
  const prior=freshPair();checkFreshOrdinaryEntry(freshState,freshPair(),prior);
  for(const mutate of [m=>m.leg='sudden_death',m=>m.terminal={outcome:2,winners:[1]},
    m=>m.prior_vs_terminal={outcome:1,winners:[0,1]},m=>m.prior_vs_source_frames=3600,
    m=>m.players[1].source_port=0,m=>m.players[0].source_slot=1,
    m=>m.players[1].source_color=0,m=>m.rules.is_vs=0,m=>m.rules.time_limit=480,
    m=>m.players[0].stocks=1,m=>m.complete=true,m=>m.ending=true,m=>m.outcome=2]){
    const bad=freshPair();mutate(bad);assert.throws(()=>checkFreshOrdinaryEntry(freshState,bad,prior));
  }
  for(const state of [{...freshState,pause_disabled:true},{...freshState,running:0},
    {...freshState,phase:14},{...freshState,error:'fault'},
    {...freshState,diagnostics:'Completed matches: 2 · raw PAD: none'}])
    assert.throws(()=>checkFreshOrdinaryEntry(state,freshPair(),prior));
});

function subsequentFixture({badSelection=false,badDrain=false,noProgress=false,stepJump=false}={}) {
  const calls=[];let phase=1,frame=0,steps=0;
  const selection={source:{valid:true,scene:2,rules:{stock_count:4,stock_time_limit:1}},
    start:{valid:true,stage:badSelection?31:32,match_kind:1,item_frequency:-1,
      player_stocks:[4,4],players:[0,1].map(i=>({character_kind:8,slot_type:0,stocks:4,color:i}))
        .concat([{slot_type:3},{slot_type:3}])}};
  const report={suddenDeath:{},sourceObservations:[]};
  return {calls,report,page:{evaluate:async()=>{calls.push('drive-FD');return 2;},
      waitForTimeout:async()=>{}},press:async key=>{calls.push(key);phase=key==='Enter'?3:7;},
    current:async()=>({...freshState,phase,message:phase===1?'Original character select':'source menu'}),
    observeSource:async()=>selection,observeMatch:async()=>freshPair(noProgress?1:(frame+=6)),
    checked:async()=>{steps+=stepJump&&phase===7?601:4;return steps;},
    waitForNoQueuedPad:async()=>{calls.push('drain');if(badDrain)throw Error('PAD release rejected');},
    shot:async label=>calls.push(label),prior:freshPair()};
}
test('subsequent tail uses original CSS/SSS inputs and a bounded advancing fresh VS prefix',async()=>{
  const fixture=subsequentFixture();await runSubsequentOrdinaryEntry(fixture);
  assert.deepEqual(fixture.calls,['Enter','drain','drive-FD','drain','sd-subsequent-sss-fd','j','sd-subsequent-ordinary-vs']);
  const observations=fixture.report.suddenDeath.subsequentEntry.observations;
  assert.equal(observations.length,2);assert.equal(observations[1].match.frame-observations[0].match.frame,12);
  assert(observations[1].steps>observations[0].steps);
});
test('subsequent tail stops on source selection, PAD release, progress and callback cap failures',async()=>{
  for(const options of [{badSelection:true},{badDrain:true},{noProgress:true},{stepJump:true}]){
    const fixture=subsequentFixture(options);
    await assert.rejects(runSubsequentOrdinaryEntry(fixture));
    assert(!fixture.calls.includes('sd-subsequent-ordinary-vs'));
  }
});
test('actual pair oracle separates original SD payload/live damage, slots and preserved flags',()=>{
  const prior=pair();checkDeclaredPair(prior,false);checkDeclaredPair(pair(true),true,prior);
  for(const mutate of [p=>p.players[1].damage_percent=0,p=>p.players[1].source_initial_damage=0,
    p=>p.players[1].source_port=0,p=>p.rules.is_vs=1,p=>p.prior_vs_terminal.winners=[1],
    p=>p.players[1].source_color=0]){
    const bad=pair(true);mutate(bad);assert.throws(()=>checkDeclaredPair(bad,true,prior));
  }
});
test('counter workload caps reject unavailable, invalid and unsafe observations',()=>{
  const good={...counter,phase_source_steps:[1,2]};assert.equal(callbackSteps(good),3);
  for(const bad of [{...good,status:'unavailable'},{...good,invalid_phase_steps:1},
    {...good,phase_source_steps:[-1]},{...good,phase_source_steps:[Number.MAX_SAFE_INTEGER,1]}])
    assert.throws(()=>callbackSteps(bad));
});
function departure(states){
  let index=0,pulses=0;
  const report={suddenDeath:{departurePulses:[]}};
  return {report,get pulses(){return pulses;},args:{report,
    driver:{pressChord:async(keys,timing)=>{assert.deepEqual(keys,['d']);
      assert.equal(timing.holdMs,250);assert.equal(timing.releaseMs,25);pulses++;index++;}},
    checked:async()=>index*10,current:async()=>states[Math.min(index,states.length-1)].state,
    observeMatch:async()=>states[Math.min(index,states.length-1)].match,
    record:async()=>states[Math.min(index,states.length-1)]}};
}
test('actual departure stops at source ending/phase transition and never sends another pulse',async()=>{
  for(const end of [{state:{phase:14},match:{ending:true}}, {state:{phase:8},match:{}}]){
    const control=departure([{state:{phase:14},match:{ending:false}},end]);
    await runBoundedSdDeparture(control.args);assert.equal(control.pulses,1);
  }
  const already=departure([{state:{phase:14},match:{complete:true}}]);
  await runBoundedSdDeparture(already.args);assert.equal(already.pulses,0);
});
test('actual departure has no retry after input rejection or capped neutral route',async()=>{
  const control=departure([{state:{phase:14},match:{}}]);
  await assert.rejects(runBoundedSdDeparture(control.args),/pulse cap/);
  assert.equal(control.pulses,SD_BROWSER_LIMITS.departurePulses);
  const rejected=departure([{state:{phase:14},match:{}}]);
  rejected.args.driver.pressChord=async()=>{throw Error('owned input rejected');};
  await assert.rejects(runBoundedSdDeparture(rejected.args),/owned input rejected/);
  assert.equal(rejected.report.suddenDeath.departurePulses.length,1);
});
test('actual harness independent close attempts survive diagnostic/resource failures',async()=>{
  const source=fs.readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url),'utf8');
  const body=source.slice(source.indexOf('const closeOnce ='),source.indexOf('const checkInterruption ='));
  const calls=[];
  const scope={driver:{dispose(){calls.push('driver');throw Error('dispose');}},
    context:{close:async()=>{calls.push('context');throw Error('context');}},
    browser:{close:async()=>{calls.push('browser');}},report:{},
    cleanupPromises:new Map(),cleanupResults:new Map()};
  vm.runInNewContext(body+'\nglobalThis.closeOwned=closeOwnedResources;',scope);
  await scope.closeOwned();await scope.closeOwned();
  assert.deepEqual(calls,['driver','context','browser']);
  assert.equal(scope.report.cleanup.driver.status,'failed');
  assert.equal(scope.report.cleanup.context.status,'failed');
  assert.equal(scope.report.cleanup.browser.status,'closed');
});
test('SD CLI rejects conflicting route/missing producer before browser loading or output',()=>{
  const harness=new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url).pathname;
  for(const args of [['--sudden-death-route'],['--sudden-death-route','--menu-only']]){
    const child=spawnSync(process.execPath,[harness,...args],{encoding:'utf8'});
    assert.equal(child.status,1);
    assert.match(child.stderr,/frozen --runtime-wasm-sha256|incompatible/);
    assert.doesNotMatch(child.stderr,/Chrome|Playwright|Missing --/);
  }
});

test('natural timeout rejects observer errors, early Results, lost stocks/damage and pause immediately',()=>{
  const good=pair();good.paused=false;good.outcome=0;
  checkNeutralTimeout({phase:7},good);checkNeutralTimeout({phase:14},{});
  for(const mutate of [p=>p.observer_error=true,p=>p.paused=true,p=>p.outcome=2,p=>p.players[0].stocks=3,p=>p.players[0].damage_percent=1]){
    const bad=structuredClone(good);mutate(bad);assert.throws(()=>checkNeutralTimeout({phase:7},bad));
  }
  assert.throws(()=>checkNeutralTimeout({phase:8},good));
  assert.throws(()=>checkMatchObservation({observer_error:true}));
  for(const reason of [1,2,3,4,5,6,8,9]){
    const c=structuredClone(counter);c.reason_counts[reason]=1;assert.throws(()=>callbackSteps(c));
  }
  const prep=structuredClone(counter);prep.reason_counts[7]=5;callbackSteps(prep);
});

test('actual harness finally preserves primary failure and closes after capture/diagnostic errors',async()=>{
  const source=fs.readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url),'utf8');
  const start=source.lastIndexOf('} finally {')+'} finally {'.length;
  const body=source.slice(start,source.indexOf('\n}\nconsole.log',start));
  for(const primary of [null,Error('original route failure')]){
    const calls=[];const scope={failure:primary,nativeSessionActive:false,suddenDeathRoute:true,
      report:{result:primary?'fail':'pass'},page:{},redactDiscPath:String,reportWriteError:null,
      driver:{diagnostics:async()=>{throw Error('diagnostic failure');}},
      readRuntimeDiagnosticsCapture:async()=>{throw Error('capture unavailable');},validateFinalSdCapture,
      persistReport:async()=>{calls.push('report');},
      closeOwnedResources:async()=>{calls.push('close');scope.report.cleanup={browser:{status:'failed'}};},
      process:{off(){}},onSigint(){},onSigterm(){}};
    vm.runInNewContext('globalThis.finish=async()=>{'+body+';return failure;};',scope);
    const returned=await scope.finish();
    assert.equal(scope.report.result,'fail');assert.equal(scope.report.diagnostics_error,'diagnostic failure');
    assert.equal(scope.report.callbackCapture.status,'unavailable');
    assert.deepEqual(calls,['report','close','report']);
    if(primary)assert.equal(returned,primary);else assert.match(returned.message,/capture unavailable/);
  }
});

test('final cumulative capture requires all route phases and rejects missing or late incidents',()=>{
  const good=structuredClone(counter);for(const phase of [7,14,8])good.phase_source_steps[phase]=1;
  assert.equal(validateFinalSdCapture(good),3);
  assert.throws(()=>validateFinalSdCapture({status:'unavailable'}));
  for(const phase of [7,14,8]){
    const absent=structuredClone(good);absent.phase_source_steps[phase]=0;
    assert.throws(()=>validateFinalSdCapture(absent),/no observed phase/);
  }
  const late=structuredClone(good);late.reason_counts[8]=1;
  assert.throws(()=>validateFinalSdCapture(late),/Runtime incident/);
});
test('actual finally retains late failing capture and still closes without replacing primary',async()=>{
  const source=fs.readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs',import.meta.url),'utf8');
  const start=source.lastIndexOf('} finally {')+'} finally {'.length;
  const body=source.slice(start,source.indexOf('\n}\nconsole.log',start));
  for(const primary of [null,Error('primary source failure')]){
    const capture=structuredClone(counter);for(const phase of [7,14,8])capture.phase_source_steps[phase]=1;
    capture.reason_counts[3]=1;let closed=0;
    const scope={failure:primary,nativeSessionActive:false,suddenDeathRoute:true,
      report:{result:'pass'},page:{},redactDiscPath:String,reportWriteError:null,
      driver:{diagnostics:async()=>({})},validateFinalSdCapture,
      readRuntimeDiagnosticsCapture:async()=>capture,persistReport:async()=>{},
      closeOwnedResources:async()=>{closed++;scope.report.cleanup={browser:{status:'closed'}};},
      process:{off(){}},onSigint(){},onSigterm(){}};
    vm.runInNewContext('globalThis.finish=async()=>{'+body+';return failure;};',scope);
    const failure=await scope.finish();assert.equal(closed,1);assert.equal(scope.report.result,'fail');
    assert.equal(scope.report.callbackCapture,capture);assert.match(scope.report.callback_capture_error,/Runtime incident/);
    if(primary)assert.equal(failure,primary);else assert.match(failure.message,/Runtime incident/);
  }
});

test('actual captured deferred SD phase5 admits only completed neutral published tie',()=>{
  const fixture=JSON.parse(fs.readFileSync(new URL('./fixtures/sd_deferred_continuation_observation.json',import.meta.url),'utf8'));
  checkNeutralTimeout(fixture.state,fixture.match);
  for(const change of [f=>f.state.running=1,f=>f.state.message='Preparing original next scene...',
    f=>f.state.error='asset loading failed',f=>f.match.observer_error=true,
    f=>f.match.complete=false,f=>f.match.ending=false,f=>f.match.ready=true,
    f=>f.match.frame=3599,f=>f.match.prior_vs_source_frames=0,
    f=>f.match.terminal.winners=[1],f=>f.match.terminal.outcome=2,
    f=>delete f.match.terminal,f=>f.match.outcome=0,
    f=>f.match.players[0].stocks=3,f=>f.match.players[1].damage_percent=1]){
    const bad=structuredClone(fixture);change(bad);
    assert.throws(()=>checkNeutralTimeout(bad.state,bad.match));
  }
  assert.throws(()=>checkNeutralTimeout({phase:5,running:0,message:'Preparing original match continuation...'},{}));
});

// These are actual retained row fields, not a synthetic P2/phase-4 completion.
test('actual v5 271-row P1-only prefix cannot admit next-scene preparation',async()=>{
  const {readFile}=await import('node:fs/promises');
  const {confirmTwoHumanResults,resultsPadTraceFailures}=await import('./vs_rules_results_confirmation_driver.mjs');
  const fixture=JSON.parse(await readFile(new URL('./fixtures/sd_results_v5_observed_prefix.json',import.meta.url),'utf8'));
  const actual=fixture.trace;
  assert.equal(actual.retained,271);assert.deepEqual(resultsPadTraceFailures(actual),[]);
  assert.deepEqual(actual.samples.at(-1).results_state_after_tick.players.map(p=>p.confirmed),[1,0,1,1]);
  let actions=0;const keys=[];
  const prefix=()=>{const length=[201,231,271,271][actions];return {...actual,
    attempts:length,retained:length,samples:actual.samples.slice(0,length)};};
  await assert.rejects(confirmTwoHumanResults({deadlineAt:Date.now()+45000,
    observeTrace:async()=>prefix(),
    observeHost:async()=>actions===3?{phase:5,running:0,message:'Preparing original next scene...',status:'Preparing original next scene... · 56 ms · audio paused',pause_present:true,pause_disabled:true,error:null}:{phase:8,running:1,message:'Original Results',status:'Original Results',pause_present:true,pause_disabled:false,error:null},
    press:async key=>{keys.push(key);++actions;},wait:async()=>{},
  }),/P2 statistics confirmation.*(?:exactly one|one consumed|Start)/);
  assert.deepEqual(keys,['Enter','Enter','End']);
});
