/* Bounded original-menu natural timeout -> live SD -> original Results control.
 * Observations never write source state. Publication-tagged callback steps are
 * conservative workload caps, not exact per-leg PAD or source-frame counts. */
import assert from 'node:assert/strict';
import {createCssHumanJoinDriver} from './vs_css_two_human_driver.mjs';
import {readRuntimeDiagnosticCounters} from './runtime_callback_recorder.mjs';
import {confirmTwoHumanResults} from './vs_rules_results_confirmation_driver.mjs';
import {returnFromCompetitivePrize} from './vs_rules_timeout_route_helpers.mjs';
import {observeRuntimeOwner} from './runtime_owner_observation.mjs';

export const SD_BROWSER_LIMITS = Object.freeze({vsSteps:4800,vsWallMs:120000,
  readySteps:600,readyWallMs:20000,departureSteps:2400,departureWallMs:45000,
  departurePulses:96,holdMs:250,releaseMs:25});
export const SD_SUBSEQUENT_LIMITS = Object.freeze({sssWallMs:20000,sssSamples:600,
  readyWallMs:20000,readySteps:600,prefixWallMs:5000,prefixSteps:120,prefixFrames:12});
export function callbackSteps(counters) {
  assert.equal(counters.status,'installed','Callback recorder must be installed');
  assert.equal(counters.invalid_phase_steps,0);
  assert.equal(counters.invalid_preparation_count,0);
  assert.equal(counters.unknown_reason_count,0);
  assert.equal(counters.reason_counts.length,10);
  // Runtime stall 1/2/3/8, fault 4, pause/resume 5/6 and hidden 9
  // stop this zero-recovery route. Reason 7 is declared preparation only.
  for(const reason of [0,1,2,3,4,5,6,8,9])
    assert.equal(counters.reason_counts[reason],0,`Runtime incident reason ${reason}`);
  assert(counters.phase_source_steps.every(n=>Number.isSafeInteger(n)&&n>=0));
  const total=counters.phase_source_steps.reduce((sum,n)=>sum+n,0);
  assert(Number.isSafeInteger(total));
  return total;
}
export function validateFinalSdCapture(capture) {
  const total=callbackSteps(capture);
  for(const phase of [7,14,8])
    assert(capture.phase_source_steps[phase]>0,`Final capture has no observed phase ${phase} steps`);
  return total;
}
export function checkMatchObservation(match) {
  assert(!match.observer_error&&!match.observer_error_reason,
    `Match observer failed: ${JSON.stringify(match)}`);
}
export function checkNeutralTimeout(state,match) {
  checkMatchObservation(match);
  assert(!state.error,`Natural tie runtime error: ${state.error}`);
  if(state.phase===5){
    // advance_match publishes/retains the completed VS terminal, closes its
    // owner, then checked typed dispatch requests scoped SD assets. With no
    // match yet, public phase falls through to the host's completed phase5.
    // This admission is only that declared neutral timeout, not any phase5.
    assert.equal(state.running,0,'Deferred SD must stop source ticking');
    assert.equal(state.message,'Preparing original match continuation...');
    checkDeclaredPair(match,false);
    assert.equal(match.frame,3600);assert.equal(match.prior_vs_source_frames,3600);
    assert.equal(match.complete,true);assert.equal(match.ending,true);
    assert.equal(match.ready,false);assert.equal(match.paused,false);
    assert.equal(match.outcome,1);
    assert.deepEqual(match.terminal,{outcome:1,winners:[0,1]});
    return;
  }
  assert(state.phase===7||state.phase===14,'Natural tie unexpectedly left VS/SD');
  if(!match.leg)return; // Only declared deferred construction may be empty.
  if(state.phase===14){assert.equal(match.leg,'sudden_death');return;}
  assert.equal(match.leg,'vs');assert.equal(match.paused,false);
  assert(match.outcome===0||match.outcome===1,'Unexpected non-timeout VS outcome');
  assert.deepEqual(match.players.map(p=>p.stocks),[4,4]);
  assert.deepEqual(match.players.map(p=>p.damage_percent),[0,0]);
}
export function checkDeclaredPair(observation,sd,prior=null) {
  checkMatchObservation(observation);
  assert.equal(observation.leg,sd?'sudden_death':'vs');
  assert.deepEqual(observation.observed_player_source_slots,[0,1]);
  const r=observation.rules;
  assert.equal(r.stage,32);assert.equal(r.match_kind,1);assert.equal(r.is_teams,0);
  assert.equal(r.item_frequency,-1);
  assert.equal(r.timer_enabled,sd?0:1);
  if(!sd)assert.equal(r.time_limit,60);
  assert.equal(r.is_stock,sd?0:1);assert.equal(r.is_vs,sd?0:1);
  assert.equal(r.source_sudden_death_flag,sd?1:0);
  assert.deepEqual(r.player_stocks,sd?[1,1]:[4,4]);
  assert.equal(observation.players.length,2);
  observation.players.forEach((p,i)=>{
    assert.equal(p.source_player_index,i);assert.equal(p.source_port,i);
    assert.equal(p.source_character,8);assert.equal(p.fighter,0);
    assert.equal(p.human,true);assert.equal(p.slot_type,0);
    assert.equal(p.source_stocks,sd?1:4);assert.equal(p.stocks,sd?1:4);
    assert.equal(p.source_initial_damage,sd?300:0);
    assert.equal(p.damage_percent,sd?300:0);
    if(prior)assert.equal(p.source_color,prior.players[i].source_color);
  });
  if(sd){
    assert.equal(observation.prior_vs_source_frames,3600);
    assert.deepEqual(observation.prior_vs_terminal,{outcome:1,winners:[0,1]});
  }
}

export async function runSuddenDeathBrowserRoute(d) {
  const {page,report,driver,press,chord,current,ensureNoError,resumeTimingPause,
    observeSource,observeMatch,observeCssSetup,sourcePadSample,sourcePadTap,
    waitForNoQueuedPad,waitMessage,waitPhase,waitMenu,enterVsRules,moveMenuCursor,
    waitItemInputReady,waitItemsCursor,waitItemFrequency,waitRulesPlusTimer,
    shot,verifyTeardown}=d;
  report.suddenDeath={limits:SD_BROWSER_LIMITS,observations:[],departurePulses:[],
    resultsPadTraceLatest:null,
    stepSemantics:'Sum of source steps at callback publication; transition callbacks may include work from the preceding phase. Source cursors are reported separately.',
    internalFinalSdPadAndSeed:'unobserved at the internal publication boundary',
    comparison:'authored functional route only; no original/reference, physical input, PCM or timing equivalence'};
  report.competitiveMatchStart={controls_change:null,css_roster:[]};
  const counters=()=>readRuntimeDiagnosticCounters(page);
  const checked=async label=>{
    await resumeTimingPause(label);await ensureNoError(label);
    assert.equal(report.errors.length,0,`${label}: browser errors observed`);
    return callbackSteps(await counters());
  };
  const observeOwner=label=>observeRuntimeOwner(page,report.suddenDeath,label);
  const record=async(label,snapshot=null)=>{
    const {state,match}=snapshot??await observeOwner(label),count=await counters();
    checkMatchObservation(match);
    report.suddenDeath.observations.push({label,state,match,counters:count});
    return {state,match};
  };
  const poll=async(label,limitMs,limitSteps,predicate,invariant=()=>{},origin=null)=>{
    const start=origin?.at??Date.now(),steps=origin?.steps??await checked(label);
    while(Date.now()-start<=limitMs){
      const now=await checked(label);
      assert(now-steps<=limitSteps,`${label}: callback step cap exceeded`);
      const snapshot=await observeOwner(label),{state,match}=snapshot;
      checkMatchObservation(match);invariant(state,match);
      if(predicate(state,match))return record(label,snapshot);
      await page.waitForTimeout(50);
    }
    throw Error(`${label}: wall-clock cap exceeded`);
  };
  await chord(['q','9','7']);
  await waitPhase(11,'original Main for SD rules');
  let rules=await enterVsRules('sd-original');
  assert.equal(rules.source.rules.stock_count,4);
  assert.equal(rules.source.rules.stock_time_limit,0);
  await moveMenuCursor(13,7,5);await press('m');
  await waitItemInputReady('SD Items input gate');
  await waitItemsCursor(0,'SD Items initial row');
  await press(']');await waitItemFrequency(0,'SD None frequency');
  await press('o');rules=await waitMenu(13,5,'SD Items commit');
  assert.equal(rules.source.items.frequency,-1);
  await moveMenuCursor(13,7,6);await press('m');
  await waitRulesPlusTimer(0,'SD Rules Plus initial timer');
  await press('4');await waitRulesPlusTimer(1,'SD original one-minute timer');
  await press('7');await waitMessage('Original character select','SD Rules Start to CSS');
  const css=await observeSource();
  assert.equal(css.source.rules.stock_count,4);
  assert.equal(css.source.rules.stock_time_limit,1);
  assert.equal(css.source.items.frequency,-1);
  report.sourceObservations.push({label:'SD original Rules committed to raw CSS',...css});
  await createCssHumanJoinDriver({page,report,shot,observeCssSetup,sourcePadSample,
    sourcePadTap,resumeTimingPause,ensureNoError}).configureSecondHuman();
  // The shared Controls change now uses the verified two-player keyboard map.
  report.suddenDeath.keys={start:'Enter',confirm:'j',p1PositiveStickX:'d'};
  await page.waitForTimeout(800);await press('Enter');
  await waitPhase(3,'SD original SSS');
  await waitForNoQueuedPad('SD CSS Start drained');
  let stage=1;
  for(let sample=0;sample<600;sample++){
    await checked('SD source SSS direction');
    stage=await page.evaluate(()=>Module._melee_web_native_menu_drive_stage(32));
    assert(stage===1||stage===2,'Source stage direction rejected');
    await waitForNoQueuedPad(`SD source SSS direction ${sample} drained`);
    if(stage===2)break;
  }
  assert.equal(stage,2);await shot('sd-sss-fd');
  const vsOrigin={at:Date.now(),steps:await checked('VS total workload begins')};
  await press('j');
  await waitPhase(7,'SD prior VS');
  const prior=(await poll('SD prior VS ready',20000,600,
    (state,match)=>state.phase===7&&match.ready&&match.frame>0)).match;
  checkNeutralTimeout({phase:7},prior);checkDeclaredPair(prior,false);await shot('sd-prior-vs');
  const firstSd=(await poll('natural VS timeout to actual SD',SD_BROWSER_LIMITS.vsWallMs,
    SD_BROWSER_LIMITS.vsSteps,(state,match)=>state.phase===14&&match.leg==='sudden_death',checkNeutralTimeout,vsOrigin)).match;
  checkDeclaredPair(firstSd,true,prior);await shot('sd-original-entry-300');
  const ready=(await poll('SD active source cursor',SD_BROWSER_LIMITS.readyWallMs,
    SD_BROWSER_LIMITS.readySteps,(state,match)=>state.phase===14&&match.ready&&match.frame>0)).match;
  checkDeclaredPair(ready,true,prior);assert.equal(ready.paused,false);assert(ready.frame>0);await shot('sd-active-before-input');
  await runBoundedSdDeparture({driver,report,checked,observeOwner,record});
  await waitPhase(8,'SD typed original Results',30000);
  const terminal=await record('SD canonical terminal at Results');
  assert.deepEqual(terminal.match.terminal,{outcome:2,winners:[1]});
  assert(terminal.match.sd_source_frames>0);
  assert.equal(terminal.match.players[0].stocks,0);assert.equal(terminal.match.players[1].stocks,1);
  await shot('sd-original-results');
  const resultsDeadline=Date.now()+45000;
  const observeResultsSample=async label=>{
    const snapshot=await observeRuntimeOwner(page,report.suddenDeath,label,
      {includeMatch:false,includeResultsTrace:true});
    report.suddenDeath.resultsPadTraceLatest=snapshot.trace;
    await checked(label); // Cumulative health remains strict; no recovery.
    return {host:snapshot.state,trace:snapshot.trace};
  };
  report.suddenDeath.resultsConfirmation=await confirmTwoHumanResults({
    deadlineAt:resultsDeadline,observeSample:observeResultsSample,press,
    wait:milliseconds=>page.waitForTimeout(milliseconds),
  });
  report.suddenDeath.prizeReturn=await returnFromCompetitivePrize({
    deadlineAt:resultsDeadline,observeSample:observeResultsSample,press,
    wait:milliseconds=>page.waitForTimeout(milliseconds),
  });
  const returned=await observeSource();
  assert.equal(returned.source.scene,1);assert.equal(returned.source.rules.stock_count,4);
  assert.equal(returned.source.rules.stock_time_limit,1);
  report.sourceObservations.push({label:'SD original CSS return',...returned});
  await shot('sd-returned-css');
  await runSubsequentOrdinaryEntry({page,report,press,observeOwner,observeSource,
    checked,waitForNoQueuedPad,shot,prior});
  await driver.unload();await verifyTeardown('SD subsequent VS Eject');
  report.checks.push('Original one-minute four-stock two-human Mario/FD timeout -> active 300-damage SD -> P1 live departure -> source P2 winner -> typed original Results -> CSS -> SSS/FD -> fresh ordinary VS prefix -> checked Eject');
}

export function checkFreshOrdinaryEntry(state,match,prior) {
  assert(!state.error,`Subsequent VS runtime error: ${state.error}`);
  assert.equal(state.phase,7);assert.equal(state.running,1);
  assert.equal(state.pause_present,true);assert.equal(state.pause_disabled,false);
  checkDeclaredPair(match,false,prior);
  assert.equal(match.rules.item_mask_hex,prior.rules.item_mask_hex);
  assert.equal(match.ready,true);assert.equal(match.paused,false);
  assert.equal(match.ending,false);assert.equal(match.complete,false);assert.equal(match.outcome,0);
  assert.equal(match.prior_vs_source_frames,0);
  assert.equal(match.terminal,undefined);assert.equal(match.prior_vs_terminal,undefined);
  assert(match.frame>0);
  match.players.forEach((player,i)=>assert.equal(player.source_slot,prior.players[i].source_slot));
  assert.match(state.diagnostics,/Completed matches: 1(?: ·|$)/);
}

export async function runSubsequentOrdinaryEntry({page,report,press,observeOwner,
  observeSource,checked,waitForNoQueuedPad,shot,prior}) {
  const limits=SD_SUBSEQUENT_LIMITS;
  const observations=[];
  report.suddenDeath.subsequentEntry={limits,observations,
    scope:'One fresh ordinary VS entry and short neutral prefix; no second completed match. Callback steps are distinct from active source frames.'};
  const active=(state,phase)=>state.phase===phase&&state.running===1&&
    state.pause_present===true&&state.pause_disabled===false;
  const stageDeadline=Date.now()+limits.sssWallMs;
  const stageCheck=async label=>{
    const steps=await checked(label);
    assert(Date.now()<stageDeadline,`${label}: returned CSS/SSS wall cap`);
    return steps;
  };
  const {state:returned}=await observeOwner('Subsequent CSS Start');await stageCheck('Subsequent CSS Start');
  assert(active(returned,1));assert.equal(returned.message,'Original character select');
  assert.match(returned.diagnostics,/Completed matches: 1(?: ·|$)/);
  await press('Enter');
  let state;
  while(Date.now()<stageDeadline){
    await stageCheck('Subsequent source SSS entry');({state}=await observeOwner('Subsequent source SSS entry'));
    if(active(state,3))break;
    await page.waitForTimeout(50);
  }
  assert(active(state,3),'Subsequent SSS did not become active');
  await waitForNoQueuedPad('Subsequent CSS Start released');
  let stage=1;
  for(let sample=0;sample<limits.sssSamples;sample++){
    await stageCheck('Subsequent source FD selection');
    stage=await page.evaluate(()=>Module._melee_web_native_menu_drive_stage(32));
    assert(stage===1||stage===2,'Subsequent source FD selection rejected');
    await waitForNoQueuedPad(`Subsequent SSS direction ${sample} released`);
    await stageCheck('Subsequent FD PAD drain');
    if(stage===2)break;
  }
  assert.equal(stage,2);
  const selection=await observeSource();
  assert.equal(selection.source.valid,true);assert.equal(selection.source.scene,2);
  assert.equal(selection.source.rules.stock_count,4);
  assert.equal(selection.source.rules.stock_time_limit,1);
  assert.equal(selection.source.items.frequency,-1);
  assert.equal(selection.source.items.mask_hex,'ffffffffffffffff');
  // start.valid is only exposed after original SSS commits and closes. The
  // entered pre-confirmation scene supplies raw rules/items; the ready VS
  // observer below supplies the normalized payload and original identities.
  report.sourceObservations.push({label:'Subsequent original SSS before confirmation',...selection});
  await shot('sd-subsequent-sss-fd');
  const readyStart=Date.now(),readySteps=await checked('Subsequent ordinary VS begins');
  await press('j');
  let first;
  while(Date.now()-readyStart<=limits.readyWallMs){
    const steps=await checked('Subsequent ordinary VS readiness');
    assert(steps-readySteps<=limits.readySteps,'Subsequent VS readiness callback cap');
    const snapshot=await observeOwner('Subsequent ordinary VS readiness'),match=snapshot.match;
    state=snapshot.state;checkMatchObservation(match);
    if(active(state,7)&&match.ready&&match.frame>0){
      checkFreshOrdinaryEntry(state,match,prior);first={state,match,steps};break;
    }
    await page.waitForTimeout(50);
  }
  assert(first,'Subsequent ordinary VS did not become ready');observations.push(first);
  const prefixAt=Date.now();let final;
  while(Date.now()-prefixAt<=limits.prefixWallMs){
    const steps=await checked('Subsequent ordinary neutral prefix');
    assert(steps-first.steps<=limits.prefixSteps,'Subsequent neutral prefix callback cap');
    const snapshot=await observeOwner('Subsequent ordinary neutral prefix'),match=snapshot.match;
    state=snapshot.state;checkFreshOrdinaryEntry(state,match,prior);
    assert(match.frame>=first.match.frame,'Subsequent source cursor regressed');
    final={state,match,steps};
    if(match.frame-first.match.frame>=limits.prefixFrames)break;
    await page.waitForTimeout(50);
  }
  assert(final&&final.match.frame-first.match.frame>=limits.prefixFrames,'Subsequent neutral source prefix did not advance');
  observations.push(final);await shot('sd-subsequent-ordinary-vs');
}

export async function runBoundedSdDeparture({driver,report,checked,observeOwner,record}) {
  const departureStart=Date.now(),departureSteps=await checked('SD departure begins');
  let ending=false;
  for(let pulse=0;pulse<SD_BROWSER_LIMITS.departurePulses;pulse++){
    const steps=await checked('SD before departure pulse');
    assert(steps-departureSteps<=SD_BROWSER_LIMITS.departureSteps,'SD departure step cap');
    assert(Date.now()-departureStart<=SD_BROWSER_LIMITS.departureWallMs,'SD departure wall cap');
    const {state:before,match}=await observeOwner('SD before departure pulse');
    checkMatchObservation(match);
    assert.equal(match.paused??false,false,'SD paused before departure');
    if(before.phase!==14||match.ending||match.complete){ending=true;break;}
    const entry={pulse:pulse+1,keys:['d'],holdMs:250,releaseMs:25,before:{state:before,match}};
    report.suddenDeath.departurePulses.push(entry);
    // Existing driver guarantees keyup in finally, including rejected pulses.
    await driver.pressChord(entry.keys,{holdMs:entry.holdMs,releaseMs:entry.releaseMs});
    const afterSteps=await checked('SD after departure pulse');
    assert(afterSteps-departureSteps<=SD_BROWSER_LIMITS.departureSteps,'SD departure step cap');
    assert(Date.now()-departureStart<=SD_BROWSER_LIMITS.departureWallMs,'SD departure wall cap');
    entry.after=await record(`SD departure pulse ${pulse+1}`);
    checkMatchObservation(entry.after.match);
    if(entry.after.state.phase!==14||entry.after.match.ending||entry.after.match.complete){ending=true;break;}
  }
  assert(ending,'SD departure pulse cap reached without an original ending');
}
