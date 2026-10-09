/* Renderer-backed sparse original-menu prefix. All changes are browser input;
 * no source state or normalized payload is written by this workload. */
import assert from 'node:assert/strict';
import {configureSparseControllerSettings} from './sparse_controller_settings_driver.mjs';
import {standardPad} from './controller-fixtures.mjs';
import {createCssHumanJoinDriver} from './vs_css_two_human_driver.mjs';
import {observeRuntimeOwner} from './runtime_owner_observation.mjs';
import {readRuntimeDiagnosticCounters} from './runtime_callback_recorder.mjs';
import {callbackSteps} from './vs_sudden_death_browser_route.mjs';
export const SPARSE_PREFIX_LIMITS=Object.freeze({fighterSamples:180,sssSamples:600,
  readyWallMs:20000,readySteps:1200,prefixFrames:180,prefixWallMs:10000,prefixSteps:600});
export async function installSparseVirtualController(page){
  await page.addInitScript(pad=>{
    window.sparseVirtualPad=pad;
    Object.defineProperty(navigator,'getGamepads',{value:()=>[window.sparseVirtualPad]});
  },standardPad(0,'Sparse Mario authored standard Gamepad'));
}
// CSS explicitly audits lifecycle once; active sampling uses the cheap match getter.
// Never infer copied PAD from raw input. Both native reads are synchronous.
export function readSparsePadVectors({activeMatch=false}={}){
  const m=globalThis.Module;
  const input=JSON.parse(m.UTF8ToString(m._melee_web_input_message()));
  const copied=JSON.parse(m.UTF8ToString(activeMatch?
    m._melee_web_native_menu_match_observe():m._melee_web_native_menu_memory()));
  return {input,copied_errors:copied.source_pad_errors};
}
export function checkSparsePadVectors(observation){
  assert(observation.input&&observation.input.active===1&&!observation.input.error);
  assert.equal(observation.input.pads?.length,4);
  assert.deepEqual(observation.input.pads.map(p=>p.err),[0,-1,0,-1]);
  assert.deepEqual(observation.copied_errors,[0,-1,0,-1],
    'Exact original copied PAD errors must be observed, never inferred');
}
export function checkSparseCss(setup){
  assert.equal(setup?.doors?.length,40);
  for(let port=0;port<4;port++){
    const row=setup.doors.slice(port*10,port*10+10),kind=port===0||port===2?0:3;
    assert.equal(row[0],kind);assert.equal(row[4],kind);
    assert.equal(row[6],0,'Original raw source slot must stay zero');
    if(kind===0)assert.equal(row[3],8,'Original active door must select Mario');
  }
}
export function checkSparseMatch(state,match,prior=null){
  assert(!state.error&&!match.observer_error&&!match.observer_error_reason);
  assert.equal(state.phase,7);assert.equal(state.running,1);
  assert.equal(state.pause_present,true);assert.equal(state.pause_disabled,false);
  assert.equal(match.leg,'vs');assert.equal(match.ready,true);
  assert.equal(match.complete,false);assert.equal(match.ending,false);assert.equal(match.paused,false);
  assert.equal(match.outcome,0);assert.deepEqual(match.observed_player_source_slots,[0,2]);
  const r=match.rules;
  assert.equal(r.stage,32);assert.equal(r.match_kind,1);assert.equal(r.is_teams,0);
  assert.equal(r.timer_enabled,0);assert.equal(r.is_stock,1);assert.equal(r.is_vs,1);
  assert.equal(r.source_sudden_death_flag,0);assert.equal(r.item_frequency,-1);
  assert.deepEqual(r.player_stocks,[4,4]);assert.equal(match.players.length,2);
  match.players.forEach((p,i)=>{
    const port=i*2;
    assert.equal(p.source_player_index,port);assert.equal(p.source_port,port);
    assert.equal(p.source_slot,0);assert.equal(p.slot_type,0);assert.equal(p.human,true);
    assert.equal(p.source_character,8);assert.equal(p.fighter,0);
    assert.equal(p.source_stocks,4);assert.equal(p.stocks,4);
    assert.equal(p.source_initial_damage,0);assert.equal(p.damage_percent,0);
    if(prior)assert.equal(p.source_color,prior.players[i].source_color);
  });
  assert(Number.isSafeInteger(match.frame)&&match.frame>0);
  if(prior)assert(match.frame>=prior.frame,'Original source cursor regressed');
}
export function validateFinalSparseCapture(capture){
  const steps=callbackSteps(capture);
  assert.equal(capture.dropped_incidents,0,'Incident retention overflow');
  assert(capture.phase_source_steps[7]>0,'No ordinary match callback steps retained');
  return steps;
}
export async function runSparseMarioBrowserPrefix(d){
  const {page,report,press,chord,observeSource,observeCssSetup,sourcePadSample,sourcePadTap,
    resumeTimingPause,ensureNoError,waitPhase,waitMenu,enterVsRules,moveMenuCursor,
    waitItemInputReady,waitItemsCursor,waitItemFrequency,waitMessage,waitForNoQueuedPad,shot,verifyTeardown}=d;
  report.inputConfiguration={playerOne:'Keyboard P1 with declared original-menu raw diagnostic PAD0 samples',
    playerThree:'Authored standard Browser Gamepad assigned through Controls to original port2',
    playerTwo:'off',playerFour:'off',layout:'two',physicalHardware:'unrun'};
  report.evidenceClaims.physicalInput.reason='Authored virtual Gamepad and keyboard/raw diagnostic inputs; no physical controller capture';
  const route=report.sparseMario={limits:SPARSE_PREFIX_LIMITS,observations:[],css:[],gamepadPulses:[],
    claims:'Virtual live Browser Gamepad input and rendered functional prefix only',
    hudObservation:'Rendered screenshots retain original HUD; numeric HUD values are not exported by this observer'};
  const padVectors=async (label,activeMatch=false)=>{
    const observation=await page.evaluate(readSparsePadVectors,{activeMatch});
    route.latestPadObservation={label,...observation};checkSparsePadVectors(observation);
    route.padObservations??=[];route.padObservations.push({label,...observation});return observation;
  };
  const checked=async label=>{
    await resumeTimingPause(label);await ensureNoError(label);assert.equal(report.errors.length,0);
    return callbackSteps(await readRuntimeDiagnosticCounters(page));
  };
  const sample=async label=>{
    const snapshot=await observeRuntimeOwner(page,route,label);
    // The exact coherent failing pair is retained before any assertion.
    assert(!snapshot.state.error&&!snapshot.match?.observer_error&&!snapshot.match?.observer_error_reason);
    await checked(label);if(snapshot.state.phase===7&&snapshot.state.running===1)await padVectors(label,true);return snapshot;
  };
  await chord(['q','9','7']);await waitPhase(11,'Sparse original Main');
  let rules=await enterVsRules('sparse');
  assert.equal(rules.source.rules.stock_count,4);assert.equal(rules.source.rules.stock_time_limit,0);
  await moveMenuCursor(13,7,5);await press('m');await waitItemInputReady('Sparse original Items');
  await waitItemsCursor(0,'Sparse original Items row0');await press(']');
  await waitItemFrequency(0,'Sparse None');await press('o');await waitMenu(13,5,'Sparse Items commit');
  await press('7');await waitMessage('Original character select','Sparse committed CSS');
  const inputCss=await observeSource();assert.equal(inputCss.source.rules.stock_time_limit,0);
  assert.equal(inputCss.source.items.frequency,-1);report.sourceObservations.push({label:'Sparse committed CSS',...inputCss});
  route.controllerConfiguration={};
  route.controllerAssignment=await configureSparseControllerSettings({page,retention:route.controllerConfiguration});
  const cssDriver=createCssHumanJoinDriver({page,report,shot,observeCssSetup,sourcePadSample,
    sourcePadTap,resumeTimingPause,ensureNoError});
  const toggle=async port=>{
    await cssDriver.moveCssCursor(`Sparse door${port} toggle`,
      (x,y,s)=>{const b=s.geometry.slice(port*12,port*12+12);return x>b[4]+.2&&x<b[5]-.2&&y>-4.4&&y<0;},
      (x,y,s)=>{const b=s.geometry.slice(port*12,port*12+12),center=(b[4]+b[5])/2;
        assert(Number.isFinite(center)&&b[5]>b[4]);return [x<center-.5?80:x>center+.5?-80:0,y<-2.2?80:y>-2.2?-80:0];});
  };
  let setup=await observeCssSetup();route.css.push({label:'before original sparse join',setup});
  assert.equal(cssDriver.cssDoor(setup,0).p_kind,0);
  assert([0,1].includes(cssDriver.cssDoor(setup,1).p_kind));
  assert.equal(cssDriver.cssDoor(setup,2).p_kind,3);assert.equal(cssDriver.cssDoor(setup,3).p_kind,3);
  await toggle(1);
  for(let changes=0;cssDriver.cssDoor(setup,1).p_kind!==3&&changes<2;changes++){
    const before=cssDriver.cssDoor(setup,1).p_kind,expected=before===0?1:3;
    await sourcePadTap(0x100,'Sparse P2 original toggle');
    ({setup}=await cssDriver.waitCssDoor(1,p=>p.p_kind===expected&&p.slot_type===expected,'Sparse P2 toggle observed'));
    route.css.push({label:'P2 original toggle',setup});
  }
  assert.equal(cssDriver.cssDoor(setup,1).p_kind,3);await toggle(2);
  await sourcePadTap(0x100,'Sparse P3 empty to Human');
  ({setup}=await cssDriver.waitCssDoor(2,p=>p.p_kind===0&&p.slot_type===0,'Sparse P3 original Human'));
  // Same source cursor/model/target geometry policy as CPU9 driveFighter,
  // with original port2 Browser Gamepad pulses instead of native PAD injection.
  const pulse=async(button,x,y,label)=>{
    const record={button,x,y,label};route.gamepadPulses.push(record);
    await checked(label);
    try{await page.evaluate(({button,x,y})=>{const p=window.sparseVirtualPad;
      p.buttons[0]={pressed:!!button,value:button?1:0};p.axes[0]=x/80;p.axes[1]=-y/80;},{button,x,y});
      await page.waitForTimeout(35);
    }finally{await page.evaluate(()=>{const p=window.sparseVirtualPad;p.buttons[0]={pressed:false,value:0};p.axes=[0,0,0,0];});}
    await page.waitForTimeout(35);await checked(label+' released');
  };
  let selected=false;
  for(let n=0;n<SPARSE_PREFIX_LIMITS.fighterSamples;n++){
    const o=await page.evaluate(()=>window.menuObserveFighterPort(2,8));assert(o);
    route.css.push({label:'P3 original Mario geometry',observation:o});
    const [port,held]=o.ids,[x,y,mx,my,left,right,top,bottom]=o.geometry;
    assert.equal(port,2);assert(o.geometry.every(Number.isFinite));
    const inside=mx>left&&mx<right&&my<top&&my>bottom;
    setup=await observeCssSetup();
    if(cssDriver.cssDoor(setup,2).character===8&&held<0&&inside){selected=true;break;}
    let tx,ty,ready;
    if(held<0){tx=mx-3.8;ty=my+2.6;ready=(tx-x)**2+(ty-y)**2<9;}
    else{assert.equal(held,2);tx=(left+right)/2-2.7;ty=(top+bottom)/2+2;ready=inside;}
    const axis=(value,target)=>value<target-.62?80:value>target+.62?-80:0;
    await pulse(ready?1:0,ready?0:axis(x,tx),ready?0:axis(y,ty),'P3 original Mario cursor');
  }
  assert(selected,'P3 original Mario selection cap');checkSparseCss(setup);
  await padVectors('Sparse committed CSS raw/copied PAD profile');
  route.css.push({label:'closed sparse CSS selection',setup});await shot('sparse-css-p1-p3');
  await press('Enter');await waitPhase(3,'Sparse original SSS');await waitForNoQueuedPad('Sparse CSS Start released');
  let stage=1;
  for(let n=0;n<SPARSE_PREFIX_LIMITS.sssSamples;n++){
    await checked('Sparse FD driver');stage=await page.evaluate(()=>Module._melee_web_native_menu_drive_stage(32));
    assert(stage===1||stage===2);await waitForNoQueuedPad('Sparse FD PAD released');if(stage===2)break;
  }
  assert.equal(stage,2);const sss=await observeSource();
  assert.equal(sss.source.valid,true);assert.equal(sss.source.scene,2);
  assert.equal(sss.source.rules.stock_count,4);assert.equal(sss.source.rules.stock_time_limit,0);
  assert.equal(sss.source.items.frequency,-1);report.sourceObservations.push({label:'Sparse raw SSS before confirmation',...sss});
  await shot('sparse-original-sss-fd');await press('j');
  const origin=Date.now(),steps=await checked('Sparse ordinary construction');let first;
  while(Date.now()-origin<SPARSE_PREFIX_LIMITS.readyWallMs){
    assert(await checked('Sparse ordinary readiness')-steps<=SPARSE_PREFIX_LIMITS.readySteps);
    const o=await sample('Sparse ordinary readiness');
    if(o.state.phase===7&&o.state.running===1&&o.match.ready&&o.match.frame>0){checkSparseMatch(o.state,o.match);first=o;break;}
    await page.waitForTimeout(50);
  }
  assert(first,'Sparse ordinary ready cap');route.observations.push(first);
  const prefixAt=Date.now(),prefixSteps=await checked('Sparse prefix');let last;
  while(Date.now()-prefixAt<SPARSE_PREFIX_LIMITS.prefixWallMs){
    assert(await checked('Sparse prefix')-prefixSteps<=SPARSE_PREFIX_LIMITS.prefixSteps);
    last=await sample('Sparse ordinary neutral prefix');checkSparseMatch(last.state,last.match,first.match);
    if(last.match.frame-first.match.frame>=SPARSE_PREFIX_LIMITS.prefixFrames)break;
    await page.waitForTimeout(50);
  }
  assert(last&&last.match.frame-first.match.frame>=SPARSE_PREFIX_LIMITS.prefixFrames,'Sparse source prefix cap');
  route.observations.push(last);route.actualSourceFramesAdvanced=last.match.frame-first.match.frame;
  await shot('sparse-ordinary-mario-hud');await page.locator('#unload').click();await verifyTeardown('Sparse prefix Eject');
  report.result='pass';report.checks.push('Rendered original sparse CSS/SSS, normalized source ports0/2, ordinary neutral source prefix and Eject; no ending/Results acceptance');
}
