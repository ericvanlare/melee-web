#!/usr/bin/env node
/** Headless rendered four-CPU9 source-menu match/Results/CSS loop fixture.
 * Inputs are bounded raw PAD samples consumed by the live original CSS/SSS;
 * fighter setup and match state are read-only source observations. This does
 * not compare against retail, physical controllers, timing, pixels or PCM.
 * Results defaults to historical keyboard pulses; keyboard-gated preserves
 * that key path while source-tick-gating the CPU page transition. Source-tick
 * remains a separate controlled PAD path, not a keyboard/reference claim. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {readResultsEntryPacket,bindResultsEntryPacket} from './results_entry_packet.mjs';
import {queueResultsP1StartAtCurrentSource} from './results_source_pad_input.mjs';
import {assertResultsCpuPagesAfterInitialP1Keyboard,buildResultsPadTraceRecord,
  findConsumedResultsStartKeyboardAttempt,summarizeResultsPadTrace}
  from './results_source_pad_trace.mjs';

const {values}=parseArgs({options:{...Object.fromEntries(
  ['url','disc','out','lineup','playwright','build-dir','results-input'].map(name=>[name,{type:'string'}])),
  matches:{type:'string'},'setup-only':{type:'boolean'}}});
if(!values.url||!values.disc||!values.out||!['A','B'].includes(values.lineup))
  throw Error('Use --url http://127.0.0.1:PORT/runtime.html --disc OWNED_CISO --out NEW_DIRECTORY --lineup A|B [--matches 1|2] [--setup-only] [--playwright PACKAGE_DIR] [--build-dir BUILT_RUNTIME_DIR] [--results-input keyboard|keyboard-gated|source-tick]');
const resultsInputMode=values['results-input']||'keyboard';
if(!['keyboard','keyboard-gated','source-tick'].includes(resultsInputMode))
  throw Error('--results-input must be keyboard, keyboard-gated, or source-tick');
const matchCount=Number(values.matches||2);
if(![1,2].includes(matchCount))throw Error('--matches must be 1 or 2');
const url=new URL(values.url);
if(!['http:','https:'].includes(url.protocol)||!url.pathname.endsWith('/runtime.html'))
  throw Error('A real HTTP development runtime.html URL is required');
const output=path.resolve(values.out);
await fs.mkdir(output,{recursive:false});
const sha256=async filename=>{
  const hash=createHash('sha256');
  for await(const bytes of (await import('node:fs')).createReadStream(filename))hash.update(bytes);
  return hash.digest('hex');
};
const repository=path.resolve(import.meta.dirname,'..');
const buildDirectory=values['build-dir']?path.resolve(values['build-dir']):null;
let localWasmIdentity=null;
if(buildDirectory){
  const wasmPath=path.join(buildDirectory,'gameplay_menu_browser.wasm');
  const wasmStat=await fs.stat(wasmPath);
  if(!wasmStat.isFile())throw Error('--build-dir must contain gameplay_menu_browser.wasm');
  localWasmIdentity={path:path.relative(repository,wasmPath),bytes:wasmStat.size,
    sha256:await sha256(wasmPath)};
}
function sourceProvenance(){
  const git=(...args)=>execFileSync('git',args,{cwd:repository,encoding:'utf8'}).trim();
  return {commit:git('rev-parse','HEAD'),tree:git('rev-parse','HEAD^{tree}'),
    status:git('status','--porcelain=v1'),
    tracked_diff_sha256:createHash('sha256').update(
      execFileSync('git',['diff','--binary','HEAD'],{cwd:repository})).digest('hex')};
}
const lineup=values.lineup==='A'?
  [{name:'Game & Watch',kind:3,position:[7.1,2.5]},
   {name:'Kirby',kind:4,position:[0.1,9.5]},
   {name:'Ice Climbers',kind:14,position:[-6.9,9.5]},
   {name:'Fox',kind:2,position:[-20.9,9.5]}]:
  [{name:'Samus',kind:16,position:[7.1,9.5]},
   {name:'Yoshi',kind:17,position:[7.1,16.5]},
   {name:'Zelda',kind:18,position:[9.1,9.5]},
   {name:'Falco',kind:20,position:[-32.2,9.5]}];
const continuationScope=matchCount===1?'natural Results→CSS only':
  'natural Results→CSS→second match';
const report={schema:'melee-web-cpu9-lineup-browser-v1',result:'fail',
  scope:`Headless Chrome rendered gameplay; live source CSS/SSS controller input, four CPU9 players, four stocks, Final Destination; Results continuation input=${resultsInputMode}; ${continuationScope}. No retail comparison, pixels, PCM, foreground timing, physical-controller or performance claim.`,
  results_input_mode:resultsInputMode,
  lineup:values.lineup,players:lineup.map(({name,kind})=>({name,kind,cpu:9,stocks:4})),
  matches:[],screenshots:[],source_progress:[],pad_sample_count:0,page_errors:[],phases:[],controller_inputs:[],
  results_input_events:[]};
report.source_timing_disruptions=[];
report.native_command_errors=[];
report.results_entry_packets=[];
report.results_entry_packet_reads=[];
report.results_source_pad_traces=[];
report.results_page_transition_checks=[];
report.provenance={source_start:sourceProvenance(),
  harness_sha256:await sha256(new URL(import.meta.url)),
  browser_driver_helper_sha256:await sha256(new URL('../scripts/browser_driver.mjs',import.meta.url)),
  browser_tools_helper_sha256:await sha256(new URL('../scripts/browser_tools.mjs',import.meta.url)),
  results_entry_helper_sha256:await sha256(new URL('./results_entry_packet.mjs',import.meta.url)),
  results_source_pad_trace_helper_sha256:await sha256(new URL('./results_source_pad_trace.mjs',import.meta.url)),
  results_source_pad_input_helper_sha256:await sha256(new URL('./results_source_pad_input.mjs',import.meta.url)),
  local_artifacts:localWasmIdentity?[localWasmIdentity]:[],
  served_artifacts:[]};
const artifactReads=[];
let browser,page,driver,activeMatchIndex=null;
// Called only on Results entry or failure, never by the polling diagnostics.
// Read independently of other observers so an unrelated observer failure does
// not hide the last retained entry. Older frozen builds remain explicitly absent.
async function retainResultsEntry(reason){
  try{
    const read=await readResultsEntryPacket(page);
    report.results_entry_packet_reads.push({reason,status:read.status});
    if(read.status!=='captured')return;
    await Promise.all(artifactReads);
    let bound;
    try{bound=bindResultsEntryPacket(read.packet,report.provenance.served_artifacts);}
    catch(error){
      // Preserve the packet even if response identity collection failed. Never
      // label missing/conflicting served-byte evidence as a bound build.
      bound={build_binding:{status:'unbound',error:error.message},packet:read.packet};
    }
    const bytes=JSON.stringify(bound,null,2)+'\n';
    const digest=createHash('sha256').update(bytes).digest('hex');
    let retained=report.results_entry_packets.find(row=>row.sha256===digest);
    if(!retained){
      const filename=`results-entry-${read.packet.match_index}-${digest}.json`;
      await fs.writeFile(path.join(output,filename),bytes,{flag:'wx'});
      retained={match_index:read.packet.match_index,sha256:digest,file:filename,
        build_binding:bound.build_binding,reasons:[]};
      report.results_entry_packets.push(retained);
    }
    retained.reasons.push(reason);
  }catch(error){
    report.results_entry_packet_reads.push({reason,status:'capture-error',error:error.message});
  }
}
// Record source progress at ordinary P1 Enter key events without supplying or
// changing input. The cursor is the last observed Results step at dispatch;
// the exact consumed PAD sample may be the following source tick, so this is
// an event bracket, not a reconstructed historical tick schedule.
async function retainResultsInputEvents(){
  try{
    report.results_input_events=await page.evaluate(()=>
      window.__meleeWebResultsInputEvents?.slice()||[]);
  }catch(error){
    report.results_input_event_read={status:'capture-error',error:error.message};
  }
}
async function readResultsSourcePadTrace(){
  return page.evaluate(()=>{
    if(typeof Module._melee_web_native_menu_results_pad_trace!=='function')
      throw Error('Results raw-PAD trace getter is unavailable in the served development build');
    return JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_results_pad_trace()));
  });
}
async function retainResultsSourcePadTrace(match,reason){
  try{
    const trace=await readResultsSourcePadTrace();
    const retained=buildResultsPadTraceRecord(match,reason,trace);
    report.results_source_pad_traces.push(retained);
    return retained;
  }catch(error){
    report.results_source_pad_traces.push({match,reason,status:'capture-error',error:error.message});
    return null;
  }
}
async function installResultsInputObserver(){
  await page.evaluate(()=>{
    const rows=[];
    window.__meleeWebResultsInputEvents=rows;
    const record=(kind,event)=>{
      if(event.key!=='Enter')return;
      const phase=Module._melee_web_native_menu_phase();
      if(phase!==8&&phase!==9)return;
      const diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
      const cursor=diagnostics.match(/(?:Results|Prize) source frame: (\d+)/);
      rows.push({kind,key:event.key,repeat:!!event.repeat,isTrusted:!!event.isTrusted,
        eventTimeMs:event.timeStamp,nativeSourceSteps:Number(window.nativeSourceSteps),
        phase,resultsSourceFrameAtEvent:cursor?Number(cursor[1]):null,
        diagnostics});
    };
    window.addEventListener('keydown',event=>record('keydown',event),true);
    window.addEventListener('keyup',event=>record('keyup',event),true);
  });
}
const buttonA=0x0100,buttonStart=0x1000;
const phase=()=>page.evaluate(()=>Module._melee_web_native_menu_phase());
const diagnostic=()=>page.evaluate(()=>{
  const phase=Module._melee_web_native_menu_phase();
  const match=JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_observe()));
  const readyMatch=phase===7&&match.ready===true;
  return {
    phase,running:Module._melee_web_native_menu_running(),
    status:document.querySelector('#status')?.textContent||'',
    error:document.querySelector('#status')?.dataset.runtimeError||null,
    diagnostics:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),
    runtimeLog:document.querySelector('#log')?.textContent||'',
    nativeCommandError:window.__meleeWebUnsupportedCommand||null,
    assetFatal:window.__meleeWebFighterAssetFatal||null,
    memory:JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_memory())),
    css:window.menuObserveCssSetup?.()||null,match,
    fighterParts:window.__fighterPartsLast||null,
    fighterPartsStep:window.__fighterPartsStep||null,
    fighterPartsOwner:window.__fighterPartsOwner||null,
    kirbyCopyVisibility:window.__kirbyCopyVisibility||null,
    kirbyHatLoad:window.__meleeWebKirbyHatLoad||null,
    p0:readyMatch?window.menuObservePlayer?.(0)||null:null,
    p1:readyMatch?window.menuObservePlayer?.(1)||null:null,
    p2:readyMatch?window.menuObservePlayer?.(2)||null:null,
    p3:readyMatch?window.menuObservePlayer?.(3)||null:null,
    sss:window.menuObserveStage?.(32)||null,
  };
});
async function screenshot(name){
  const file=path.join(output,`${name}.png`);
  await page.screenshot({path:file,fullPage:false});report.screenshots.push(file);
  const canvas=path.join(output,`${name}-canvas.png`);
  await page.locator('#canvas').screenshot({path:canvas});report.screenshots.push(canvas);
}
async function writeProgress(label){
  const state=await diagnostic();
  report.source_progress.push({label,phase:state.phase,status:state.status,
    error:state.error,p0:state.p0,p1:state.p1,p2:state.p2,p3:state.p3,match:state.match,
    css:state.css,memory:state.memory,fighterParts:state.fighterParts,
    fighterPartsStep:state.fighterPartsStep,fighterPartsOwner:state.fighterPartsOwner,
    kirbyHatLoad:state.kirbyHatLoad,assetFatal:state.assetFatal});
  await fs.writeFile(path.join(output,'progress.json'),JSON.stringify({
    label,phase:state.phase,status:state.status,error:state.error,
    p0:state.p0,p1:state.p1,p2:state.p2,p3:state.p3,match:state.match,
    assetFatal:state.assetFatal,at:new Date().toISOString()},null,2)+'\n');
  if(state.error)throw Error(state.error);
  return state;
}
async function waitFor(label,predicate,timeoutMs=30000){
  const deadline=Date.now()+timeoutMs;
  while(Date.now()<deadline){
    const state=await diagnostic();
    if(state.error)throw Error(`${label}: ${state.error}`);
    if(predicate(state))return state;
    await page.waitForTimeout(50);
  }
  const state=await diagnostic();
  throw Error(`${label} timed out after ${timeoutMs} ms: ${JSON.stringify({phase:state.phase,status:state.status,p0:state.p0,p1:state.p1,match:state.match})}`);
}
async function pad(port,buttons=0,stickX=0,stickY=0,duration=1,label='input'){
  assert(port===0||port===1,'the live diagnostic PAD route supports only P1/P2');
  assert(Number.isInteger(duration)&&duration>=1&&duration<=120);
  await page.evaluate(({port,buttons,stickX,stickY,duration})=>
    window.menuDiagnosticPad(port,buttons,stickX,stickY,duration),
    {port,buttons,stickX,stickY,duration});
  report.pad_sample_count++;
  if(report.controller_inputs.length<4000)
    report.controller_inputs.push({port,buttons,stickX,stickY,duration,label});
  // Let the source consume the queued sample and resume ordinary neutral PAD.
  await page.waitForTimeout(Math.max(40,duration*18));
}
async function tap(port,button,label,duration=1){
  await pad(port,button,0,0,duration,label);
  await pad(port,0,0,0,2,`${label}:release`);
}
async function driveFighter(port,door,fighter){
  for(let step=0;step<480;step++){
    const observation=await page.evaluate(({port,kind})=>
      window.menuObserveFighterPort(port,kind),{port,kind:fighter.kind});
    assert(observation,`Source CSS geometry unavailable for ${fighter.name} on P${port+1}`);
    const [cursorPort,heldDoor,selectedKind]=observation.ids;
    const [cursorX,cursorY,observedModelX,observedModelY,left,right,top,bottom]=observation.geometry;
    const doorGeometry=row((await css()).geometry,door,12);
    // P1/P2 models are paired with their cursor. A CPU puck remains owned by
    // its source door while P1's cursor carries it, so read that door's model.
    const modelX=door===port?observedModelX:doorGeometry[2];
    const modelY=door===port?observedModelY:doorGeometry[3];
    assert.equal(cursorPort,port,`${fighter.name}: wrong source cursor port`);
    const inside=modelX>left&&modelX<right&&modelY<top&&modelY>bottom;
    const setup=await css(),doorState=row(setup.doors,door,10);
    if(doorState[3]===fighter.kind&&heldDoor<0&&inside)return;
    let targetX,targetY,ready=false;
    if(heldDoor<0){
      targetX=modelX-3.8;targetY=modelY+2.6;
      ready=(targetX-cursorX)**2+(targetY-cursorY)**2<9;
    }else{
      assert.equal(heldDoor,door,`${fighter.name}: cursor is holding CSS door ${heldDoor}, expected ${door}`);
      targetX=(left+right)/2-2.7;targetY=(top+bottom)/2+2.0;
      ready=inside;
    }
    if(ready){await tap(port,buttonA,`source-place-${fighter.name}-door-${door}`);continue;}
    const axis=(current,target)=>current<target-0.62?80:current>target+0.62?-80:0;
    const stickX=axis(cursorX,targetX),stickY=axis(cursorY,targetY);
    if(!stickX&&!stickY)await pad(port,0,0,0,1,`source-settle-${fighter.name}-door-${door}`);
    else await pad(port,0,stickX,stickY,1,`source-drive-${fighter.name}-door-${door}`);
  }
  const final=await css();
  throw Error(`Source CSS did not select ${fighter.name} on door ${door}: ${JSON.stringify(row(final.doors,door,10))}`);
}
async function css(){
  const state=await diagnostic();
  assert.equal(state.phase,1,`CSS required; phase=${state.phase} status=${state.status}`);
  assert(state.css&&state.css.cursors?.length===16&&state.css.doors?.length===40&&
    state.css.geometry?.length===48,'Live source CSS observation unavailable');
  return state.css;
}
function row(array,port,width){return array.slice(port*width,(port+1)*width);}
async function move(port,targetX,targetY,label){
  const deadline=Date.now()+15000;
  for(let step=0;step<160&&Date.now()<deadline;step++){
    const setup=await css(),g=row(setup.geometry,port,12),x=g[0],y=g[1];
    const dx=targetX-x,dy=targetY-y;
    if(Math.abs(dx)<0.6&&Math.abs(dy)<0.6){
      await pad(port,0,0,0,3,`${label}:settle`);
      const settled=row((await css()).geometry,port,12);
      if(Math.abs(targetX-settled[0])<0.8&&Math.abs(targetY-settled[1])<0.8)return;
      continue;
    }
    const axis=d=>Math.abs(d)<0.5?0:Math.sign(d)*(Math.abs(d)>5?70:35);
    const magnitude=Math.max(Math.abs(dx),Math.abs(dy));
    const duration=Math.max(1,Math.min(8,Math.ceil(magnitude/1.5)));
    await pad(port,0,axis(dx),axis(dy),duration,label);
  }
  const setup=await css(),g=row(setup.geometry,port,12);
  throw Error(`${label}: source cursor failed to settle at ${targetX},${targetY}; observed ${g[0]},${g[1]}`);
}
async function chooseHuman(port,fighter){
  await driveFighter(port,port,fighter);
  const setup=await waitFor(`${fighter.name} CSS selection`,s=>{
    const doors=row(s.css?.doors||[],port,10);
    return doors.length===10&&doors[3]===fighter.kind;
  },15000);
  report.phases.push(`source CSS confirmed ${fighter.name} on P${port+1}`);
  return setup.css;
}
async function setCpuDoor(door){
  let setup=await css(),d=row(setup.doors,door,10);
  if(d[0]===1)return;
  const g=row(setup.geometry,door,12),x=(g[4]+g[5])/2;
  await move(0,x,-2.2,`toggle-CPU-door-${door}`);
  for(let attempt=0;attempt<4;attempt++){
    await tap(0,buttonA,`set-CPU-door-${door}`);
    setup=await waitFor(`CPU mode on door ${door}`,s=>row(s.css?.doors||[],door,10)[0]===1,4000).catch(()=>null);
    if(setup)return;
  }
  throw Error(`Source CSS did not change door ${door} to CPU mode`);
}
async function selectCpuCharacter(door,fighter){
  let setup=await css(),g=row(setup.geometry,door,12);
  await move(0,g[2]-3.8,g[3]+2.6,`pick-up-CPU-${door}`);
  await tap(0,buttonA,`attach-CPU-${door}`);
  await waitFor(`CPU ${door} puck attached`,s=>row(s.css?.cursors||[],0,4)[1]===1&&
    row(s.css?.cursors||[],0,4)[2]===door,5000);
  await driveFighter(0,door,fighter);
  await waitFor(`CPU ${door} selected ${fighter.name}`,
    s=>row(s.css?.doors||[],door,10)[3]===fighter.kind,15000);
}
async function setCpuLevel9(door){
  let setup=await css(),g=row(setup.geometry,door,12);
  if(row(setup.doors,door,10)[5]===9)return;
  await move(0,g[8],g[9],`grab-CPU-slider-${door}`);
  await tap(0,buttonA,`grab-CPU-slider-${door}`);
  await waitFor(`CPU slider ${door} grabbed`,s=>row(s.css?.cursors||[],0,4)[2]===door+4,5000);
  for(let sample=0;sample<24;sample++){
    setup=await css();
    if(row(setup.doors,door,10)[5]===9)break;
    await pad(0,0,80,0,8,`raise-CPU-${door}-level`);
  }
  await pad(0,0,0,0,2,`CPU-${door}-slider-neutral`);
  setup=await css();
  assert.equal(row(setup.doors,door,10)[5],9,`CPU ${door} level must be source-confirmed 9`);
  await tap(0,buttonA,`release-CPU-slider-${door}`);
  await waitFor(`CPU slider ${door} released`,s=>row(s.css?.cursors||[],0,4)[1]!==1,5000);
}
async function verifyRoster(expected,label){
  const setup=await css();
  const players=Array.from({length:4},(_,door)=>{
    const d=row(setup.doors,door,10);
    return {door,slot_type:d[4],character:d[3],cpu_level:d[5],slot:d[6],kind:d[0]};
  });
  for(let door=0;door<4;door++){
    assert.equal(players[door].kind,1,`${label}: door ${door} is not CPU`);
    assert.equal(players[door].character,expected[door].kind,`${label}: door ${door} fighter identity`);
    assert.equal(players[door].slot_type,1,`${label}: door ${door} slot type`);
    assert.equal(players[door].cpu_level,9,`${label}: door ${door} CPU level`);
  }
  report.phases.push({label,players});
  return players;
}
async function configureRoster(expected){
  await waitFor('four source CSS cursor/model pairs',s=>s.phase===1&&s.css&&
    s.css.cursors.length===16&&s.css.doors.length===40&&s.css.geometry.length===48,30000);
  await chooseHuman(0,expected[0]);
  await chooseHuman(1,expected[1]);
  for(let door=0;door<4;door++)await setCpuDoor(door);
  for(const door of [2,3])await selectCpuCharacter(door,expected[door]);
  for(let door=0;door<4;door++)await setCpuLevel9(door);
  return verifyRoster(expected,'four-CPU9 Final Destination roster before SSS');
}
async function chooseFinalDestination(){
  await tap(0,buttonStart,'CSS-to-SSS Start',8);
  const rawStart=await waitFor('original SSS entry after raw PAD Start',s=>s.phase===3,2000)
    .catch(()=>null);
  if(!rawStart){
    report.phases.push('raw P1 Start did not transition CSS; retrying with focused browser Enter→source PAD');
    await driver.pressChord(['Enter'],{holdMs:160,releaseMs:150});
  }
  await waitFor('original SSS entry',s=>s.phase===3,30000);
  await screenshot(`match-${report.matches.length+1}-sss`);
  for(let attempt=0;attempt<360;attempt++){
    const state=await diagnostic();
    if(state.error)throw Error(`SSS source error: ${state.error}`);
    if(state.phase!==3)throw Error(`SSS exited before stage confirmation; phase=${state.phase}`);
    const result=await page.evaluate(()=>Module._melee_web_native_menu_drive_stage(32));
    if(result===0)throw Error('Source SSS stage-driver failed: '+await page.evaluate(()=>Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())));
    if(result===2)break;
    await page.waitForTimeout(45);
  }
  const stage=await page.evaluate(()=>window.menuObserveStage(32));
  assert(stage&&stage.ids[1]===32,'Final Destination was not highlighted by the live source SSS');
  report.phases.push({label:'source SSS highlighted Final Destination',stage});
  await screenshot(`match-${report.matches.length+1}-fd-highlighted`);
  await tap(0,buttonA,'confirm-Final-Destination');
  await waitFor('original four-player match entry',s=>s.phase===7,60000);
}
async function runMatch(matchIndex,expected){
  activeMatchIndex=matchIndex;
  await chooseFinalDestination();
  const entry=await writeProgress(`match-${matchIndex}-entry`);
  await screenshot(`match-${matchIndex}-entry`);
  const checkpoints=[600,2400,6000,10000,14000,18000,24000];
  let next=0,deadline=Date.now()+12*60*1000,stalledSince=0,terminalTransitionRecorded=false;
  const lastKindBySlot=new Map();
  while(Date.now()<deadline){
    const state=await diagnostic();
    if(state.nativeCommandError&&report.native_command_errors.length===0)
      report.native_command_errors.push(state.nativeCommandError);
    if(state.error)throw Error(`Browser match ${matchIndex} runtime error: ${state.error}`);
    for(const [slot,player] of [[2,state.p2],[3,state.p3]]){
      if(!player)continue;
      const previous=lastKindBySlot.get(slot);
      if(previous!==player.fighterKind){
        report.form_transitions??=[];
        report.form_transitions.push({match:matchIndex,slot,from:previous??null,
          to:player.fighterKind,frame:player.frame});
        lastKindBySlot.set(slot,player.fighterKind);
      }
    }
    if(state.phase===8||state.phase===9)break;
    if(state.match?.complete===true&&(state.phase===5||state.phase===6)){
      if(!terminalTransitionRecorded){
        report.phases.push({label:`match ${matchIndex} terminal; waiting for authored Results transition`,
          phase:state.phase,frame:state.match.frame,outcome:state.match.outcome,
          terminal:state.match.terminal,memory:state.memory});
        terminalTransitionRecorded=true;
      }
      await page.waitForTimeout(100);
      continue;
    }
    assert.equal(state.phase,7,`Match ${matchIndex} left the source Match scene unexpectedly`);
    const frame=state.p0?.frame||0;
    if(state.status.startsWith('Paused after a timing disruption')){
      if(!stalledSince){
        stalledSince=Date.now();
        report.source_timing_disruptions.push({match:matchIndex,frame,action:'pause-observed'});
      }else if(Date.now()-stalledSince>500){
        await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
        report.source_timing_disruptions.push({match:matchIndex,frame,action:'resumed-at-same-source-frame'});
        stalledSince=0;
      }
      await page.waitForTimeout(50);
      continue;
    }
    stalledSince=0;
    if(next<checkpoints.length&&frame>=checkpoints[next]){
      const label=`match-${matchIndex}-source-frame-${frame}`;
      await writeProgress(label);
      if(next===0||next===2||next===4)await screenshot(label);
      next++;
    }
    if((report.source_progress.length===0||
       Date.now()-Date.parse(report.source_progress.at(-1).wall_time||0)>5000)&&frame%300<40){
      const progress=await writeProgress(`match-${matchIndex}-progress-${frame}`);
      report.source_progress.at(-1).wall_time=new Date().toISOString();
      if(progress.error)throw Error(progress.error);
    }
    await page.waitForTimeout(250);
  }
  let state=await diagnostic();
  if(state.phase!==8&&state.phase!==9)
    throw Error(`Match ${matchIndex} did not naturally reach Results/Prize before the 12-minute bound: ${JSON.stringify({phase:state.phase,p0:state.p0,p1:state.p1,status:state.status})}`);
  await retainResultsEntry(`match-${matchIndex}-results-entry`);
  const result={match:matchIndex,entered_results_phase:state.phase,
    source_diagnostics:state.diagnostics,terminal_match:state.match,
    memory_at_results:state.memory,players:expected.map(({name,kind})=>({name,kind,cpu:9,stocks:4}))};
  report.matches.push(result);
  report.phases.push({label:`match ${matchIndex} naturally reached Results/Prize`,phase:state.phase,match:state.match});
  // Ordinary Start input advances authored Results/Prize routing. Stop only
  // when the actual source menu returns to CSS; never force a scene reset.
  const resumeResultsIfPaused=async state=>{
    if(!state.status.startsWith('Paused after a timing disruption'))return state;
    const row={match:matchIndex,phase:state.phase,source_diagnostics:state.diagnostics,
      action:'resumed-at-same-source-frame-before-results-input'};
    report.source_timing_disruptions.push(row);
    await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
    await page.waitForTimeout(250);
    const resumed=await diagnostic();
    if(resumed.error)throw Error(`Results ${matchIndex} resume: ${resumed.error}`);
    return resumed;
  };
  const readResultsFrame=state=>{
    const frame=state.diagnostics.match(/(?:Results|Prize) source frame: (\d+)/);
    assert(frame,'Results/Prize source-frame diagnostic is unavailable');
    return Number(frame[1]);
  };
  const waitForResultsFrame=async(target,label)=>{
    const waitDeadline=Date.now()+60000;
    while(Date.now()<waitDeadline){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`${label}: ${state.error}`);
      if(state.phase===1)return state;
      if(state.phase===5||state.phase===6){await page.waitForTimeout(8);continue;}
      assert(state.phase===8||state.phase===9,`${label}: unexpected phase ${state.phase}`);
      if(readResultsFrame(state)>=target)return state;
      await page.waitForTimeout(8);
    }
    throw Error(`${label}: Results source frame did not reach ${target}`);
  };
  const waitForResultsInternalPhase=async(targetFrame,targetPhase,label)=>{
    const waitDeadline=Date.now()+60000;
    while(Date.now()<waitDeadline){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`${label}: ${state.error}`);
      if(state.phase===1)return state;
      assert(state.phase===8||state.phase===9,`${label}: unexpected scene phase ${state.phase}`);
      if(readResultsFrame(state)>=targetFrame){
        const trace=await readResultsSourcePadTrace();
        assert(!trace.overflow,`${label}: Results source trace overflowed`);
        const sourceState=trace.samples.at(-1)?.results_state_after_tick;
        if(sourceState?.phase===targetPhase&&sourceState.source_frame>=targetFrame)return state;
        if(sourceState?.phase>targetPhase)
          throw Error(`${label}: original Results internal phase advanced past ${targetPhase} before P1 input (phase ${sourceState.phase})`);
      }
      await page.waitForTimeout(16);
    }
    throw Error(`${label}: Results internal phase ${targetPhase} was not observed by source frame ${targetFrame}`);
  };
  const queueSourceStart=async(target,label,beforeQueue)=>{
    const ready=await waitForResultsFrame(target,`${label} source-tick gate`);
    if(ready.phase===1)return {natural_css:true,source_frame:null};
    assert(ready.phase===8||ready.phase===9,
      `${label}: unexpected source phase ${ready.phase} before P1 input`);
    const preQueue=beforeQueue?await beforeQueue(ready):null;
    let pageTransitionCheck=null;
    if(preQueue){
      pageTransitionCheck={...preQueue,status:'passed-before-queue',
        attempted_confirmation_source_frame:null,confirmation_consumed:false};
      report.results_page_transition_checks.push(pageTransitionCheck);
    }
    const queued=await page.evaluate(queueResultsP1StartAtCurrentSource,
      {button:buttonStart,duration:10});
    assert.equal(queued.result,1,
      `${label}: raw P1 Start PAD queue failed at Results frame ${queued.source_frame}`+
      ` (running_before_queue=${queued.running_before_queue};`+
      ` resumed_after_timing_pause=${queued.resumed_after_timing_pause};`+
      ` status=${queued.status_before_queue}; ${queued.diagnostics})`);
    if(preQueue){
      assert(preQueue.transitions.every(row=>row.source_frame<=queued.source_frame),
        'Disconnected CPU pages must advance no later than the queued P1 confirmation tick');
      pageTransitionCheck.status='pass';
      pageTransitionCheck.attempted_confirmation_source_frame=queued.source_frame;
      pageTransitionCheck.confirmation_consumed=true;
    }
    report.controller_inputs.push({device:'development raw PAD at source tick',port:0,
      buttons:buttonStart,duration:10,target_results_source_frame:target,
      queued_at_results_source_frame:queued.source_frame,
      running_before_queue:queued.running_before_queue,
      resumed_after_timing_pause:queued.resumed_after_timing_pause,label});
    await waitForResultsFrame(queued.source_frame+10,`${label} ten-tick P1 Start hold`);
    return queued.source_frame;
  };
  const waitForCpuPagesBeforeKeyboard=async(targetFrame,label)=>{
    const waitDeadline=Date.now()+60000;
    let lastTraceFrame=-1;
    while(Date.now()<waitDeadline){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`${label}: ${state.error}`);
      if(state.phase===1)return {natural_css:true,source_frame:null,transitions:[]};
      assert(state.phase===8||state.phase===9,`${label}: unexpected source phase ${state.phase}`);
      const diagnosticFrame=readResultsFrame(state);
      if(diagnosticFrame>=targetFrame&&diagnosticFrame-lastTraceFrame>=12){
        const trace=await readResultsSourcePadTrace();
        const gate=assertResultsCpuPagesAfterInitialP1Keyboard(trace,targetFrame);
        if(gate)return {natural_css:false,...gate};
        lastTraceFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame??diagnosticFrame;
      }
      await page.waitForTimeout(16);
    }
    throw Error(`${label}: disconnected CPU pages did not auto-advance after the consumed initial P1 Enter (last scheduled trigger lower bound ${targetFrame})`);
  };
  // Entry can be frame zero of the original fade. Retain a genuinely
  // advancing Results scene before sending the continuation controller input.
  const resultsDeadline=Date.now()+60000;
  let resultsFrame=0;
  while(Date.now()<resultsDeadline){
    state=await resumeResultsIfPaused(await diagnostic());
    if(state.error)throw Error(`Results ${matchIndex} advance: ${state.error}`);
    assert(state.phase===8||state.phase===9,'Results advanced without continuation input');
    const frame=state.diagnostics.match(/(?:Results|Prize) source frame: (\d+)/);
    assert(frame,'Results/Prize source-frame diagnostic is unavailable');
    resultsFrame=Number(frame[1]);
    if(resultsFrame>=180)break;
    await page.waitForTimeout(100);
  }
  assert(resultsFrame>=180,`Results ${matchIndex} did not advance through its source fade`);
  result.rendered_results_source_frame=resultsFrame;
  if(resultsInputMode==='source-tick'){
    report.phases.push(`Results ${matchIndex}: P1-only ten-source-tick pulses; keyboard path not used`);
    const starts=[];
    const firstReady=await waitForResultsInternalPhase(180,2,
      `results-${matchIndex}-first-P1-start-phase-gate`);
    const first=firstReady.phase===1?{natural_css:true,source_frame:null}:
      await queueSourceStart(readResultsFrame(firstReady),`results-${matchIndex}-source-start-1`);
    if(first.natural_css){
      result.results_source_input_stop={reason:'natural CSS return before first P1 source sample',
        source_frame:first.source_frame};
    }else{
      starts.push(first);
      await writeProgress(`match-${matchIndex}-natural-results-after-first-start`);
      await screenshot(`match-${matchIndex}-natural-results-after-first-start`);
      const confirmation=await queueSourceStart(600,
        `results-${matchIndex}-source-confirm-after-auto-page`,async ready=>{
      const trace=await readResultsSourcePadTrace();
      assert(!trace.overflow,'Pre-confirmation Results trace overflowed');
      const transitions=summarizeResultsPadTrace(trace).results_page_transitions;
      const cpuTransitions=transitions.filter(row=>row.from===0&&row.to===1&&row.slot>=2);
      const beforeConfirmFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame;
      assert(Number.isInteger(beforeConfirmFrame)&&beforeConfirmFrame>=readResultsFrame(ready),
        'Results source trace did not reach the pre-confirmation source boundary');
      assert.deepEqual(cpuTransitions.map(row=>row.slot),[2,3],
        'Disconnected CPU statistics pages must auto-advance before P1 confirmation');
      assert(cpuTransitions.every(row=>row.phase===3&&row.stats_phase===2&&
        row.source_frame<beforeConfirmFrame),
        'Disconnected CPU page transitions must be observed in active statistics before P1 confirmation');
      assert.deepEqual(transitions.map(row=>row.slot),[2,3],
        'Connected neutral ports or another Results player page changed before P1 confirmation');
      return {match:matchIndex,status:'pass',
        input:'P1-only source-tick Start; historical keyboard path not used',
        source_frame_before_confirmation:beforeConfirmFrame,
        confirmation_queue_target:600,transitions:cpuTransitions};
      });
      if(confirmation.natural_css){
        result.results_source_input_stop={reason:'natural CSS return before the post-auto-page confirmation',
          source_frame:confirmation.source_frame,
          auto_page_gate:'not reached; inspect retained PAD trace'};
      }else{
        starts.push(confirmation);
        // Some authored Results routes need another ordinary P1 confirmation.
        // Stop as soon as original CSS returns; never queue into a later scene.
        for(let pulse=2;pulse<3&&state.phase!==1;pulse++){
          const next=await queueSourceStart(starts.at(-1)+240,
            `results-${matchIndex}-source-start-${pulse+1}`);
          if(next.natural_css){
            result.results_source_input_stop={reason:'natural CSS return before optional continuation',
              source_frame:next.source_frame};
            break;
          }
          starts.push(next);
        }
      }
    }
    result.results_source_start_pulse_frames=starts;
    result.results_source_confirmation_count=starts.length;
  }else if(resultsInputMode==='keyboard-gated'){
    const inputEventStart=await page.evaluate(()=>
      window.__meleeWebResultsInputEvents?.length||0);
    const keyboardPhaseReady=await waitForResultsInternalPhase(180,2,
      `results-${matchIndex}-first-keyboard-enter-phase-gate`);
    if(keyboardPhaseReady.phase===1){
      result.results_keyboard_input_stop={reason:'natural CSS return before phase-2 keyboard gate'};
      throw Error(`Results ${matchIndex} returned to CSS before the original fade reached phase 2`);
    }
    result.results_keyboard_phase_gate={status:'passed',source_phase:2,
      source_frame:readResultsFrame(keyboardPhaseReady)};
    report.phases.push(`Results ${matchIndex}: original phase 2 observed before ordinary Enter`);
    // Retain the ordinary trusted keyboard path, but stop retrying as soon as
    // a P1 Start is actually consumed. Then wait with no further input until
    // the disconnected CPU pages auto-advance in source statistics.
    const initialEnterTargets=[198,296,394,509];
    const initialEnterDispatches=[];
    let initialCssReturn=false;
    let initialStartObserved=false;
    for(const targetFrame of initialEnterTargets){
      const ready=await waitForResultsFrame(targetFrame,
        `results-${matchIndex}-keyboard-prefix-${targetFrame}`);
      if(ready.phase===1){initialCssReturn=true;break;}
      assert(ready.phase===8||ready.phase===9,
        `Results ${matchIndex} cannot send Enter in source phase ${ready.phase}`);
      const sourceFrameBefore=readResultsFrame(ready);
      await driver.pressChord(['Enter'],{holdMs:160,releaseMs:120});
      report.controller_inputs.push({device:'keyboard-to-source-PAD',key:'Enter',
        hold_ms:160,release_ms:120,target_source_frame:targetFrame,
        source_frame_before:sourceFrameBefore,
        label:`results-${matchIndex}-keyboard-prefix-${targetFrame}`});
      initialEnterDispatches.push({target_source_frame:targetFrame,source_frame_before:sourceFrameBefore});
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`Results ${matchIndex} Enter at ${targetFrame}: ${state.error}`);
      if(state.phase===1){initialCssReturn=true;break;}
      const trace=await readResultsSourcePadTrace();
      initialStartObserved=summarizeResultsPadTrace(trace).p1_start_runs.length>0;
      if(initialStartObserved)break;
    }
    if(initialCssReturn){
      result.results_keyboard_input_stop={reason:'natural CSS return before the CPU auto-page gate',
        initial_enter_dispatches:initialEnterDispatches};
      throw Error(`Results ${matchIndex} returned to CSS before the disconnected CPU auto-page gate`);
    }else{
      await retainResultsInputEvents();
      const initialInputEvents=(report.results_input_events||[]).slice(inputEventStart);
      const initialKeydowns=initialInputEvents.filter(row=>row.kind==='keydown');
      assert(initialStartObserved,
        'The ordinary keyboard trigger attempts did not produce a source-consumed P1 Start');
      assert.equal(initialKeydowns.length,initialEnterDispatches.length,
        'Each scheduled initial ordinary keyboard Enter must retain one trusted keydown');
      assert(initialKeydowns.every((row,index)=>row.isTrusted&&!row.repeat&&
        row.resultsSourceFrameAtEvent>=initialEnterTargets[index]),
        'The initial Results Enter edges must preserve their source-tick lower bounds');
      assert.deepEqual(initialEnterDispatches.map(row=>row.target_source_frame),
        initialEnterTargets.slice(0,initialEnterDispatches.length));
      const initialDispatchFrame=initialEnterDispatches.at(-1).source_frame_before;
      await writeProgress(`match-${matchIndex}-initial-start-before-auto-pages`);
      await screenshot(`match-${matchIndex}-initial-start-before-auto-pages`);
      const gate=await waitForCpuPagesBeforeKeyboard(
        initialEnterDispatches.at(-1).target_source_frame,
        `results-${matchIndex}-auto-pages-before-confirmation`);
      if(gate.natural_css){
        result.results_keyboard_input_stop={reason:'natural CSS return before both CPU pages auto-advanced'};
        throw Error(`Results ${matchIndex} returned to CSS before both disconnected CPU pages auto-advanced`);
      }
      const pageCheck={match:matchIndex,status:'source-auto-pages-observed',
        input:'ordinary trusted Enter trigger attempts stop on first consumed P1 Start; then source-gated 160ms/120ms Enter confirmation',
        auto_page_gate_poll_lower_bound_source_frame:initialEnterDispatches.at(-1).target_source_frame,
        initial_enter_pulse_count:initialEnterDispatches.length,
        initial_enter_dispatches:initialEnterDispatches,
        initial_dispatch_source_frame:initialDispatchFrame,
        initial_keydown_source_frame:initialKeydowns.at(-1).resultsSourceFrameAtEvent,
        initial_keydown_source_frames:initialKeydowns.map(row=>row.resultsSourceFrameAtEvent),
        initial_consumed_p1_start:gate.initial_start,
        stats_phase_start_source_frame:gate.stats_phase_start_source_frame,
        cpu_page_delay_source_ticks:gate.cpu_page_delay_source_ticks,
        source_frame_before_confirmation:gate.source_frame,
        transitions:gate.transitions,post_page_start_runs:gate.post_page_start_runs,
        post_gate_ordinary_enter_dispatch_source_frame:null,
        confirmation_source_frame:null,keyboard_keydown_source_frame:null,
        confirmation_consumed:false};
      report.results_page_transition_checks.push(pageCheck);
      result.results_keyboard_page_gate={source_frame:gate.source_frame,
        initial_start:gate.initial_start,
        stats_phase_start_source_frame:gate.stats_phase_start_source_frame,
        cpu_page_delay_source_ticks:gate.cpu_page_delay_source_ticks,
        transitions:gate.transitions,post_page_start_runs:gate.post_page_start_runs,
        connectedness:gate.summary.port_error_values};
      report.phases.push(`Results ${matchIndex}: CPU pages auto-advanced before next ordinary Enter confirmation`);
      await writeProgress(`match-${matchIndex}-auto-pages-before-confirmation`);
      await screenshot(`match-${matchIndex}-auto-pages-before-confirmation`);
      const lastCpuPageTransition=Math.max(...gate.transitions.map(row=>row.source_frame));
      state=await waitForResultsFrame(lastCpuPageTransition+1,
        `results-${matchIndex}-confirmation-after-both-auto-pages`);
      assert(state.phase===8||state.phase===9,
        `Results ${matchIndex} returned to CSS before the post-page keyboard confirmation`);
      const confirmationLowerBound=readResultsFrame(state);
      assert(confirmationLowerBound>lastCpuPageTransition,
        'Ordinary P1 confirmation must be dispatched at a source frame after both CPU page transitions');
      pageCheck.source_frame_before_confirmation=confirmationLowerBound;
      let pulses=initialEnterDispatches.length;
      for(;pulses<48&&state.phase!==1;pulses++){
        state=await resumeResultsIfPaused(await diagnostic());
        if(state.error)throw Error(`Results ${matchIndex} before Enter pulse ${pulses}: ${state.error}`);
        if(state.phase===1)break;
        assert(state.phase===8||state.phase===9,
          `Results ${matchIndex} cannot send ordinary Enter in source phase ${state.phase}`);
        const sourceFrameBefore=readResultsFrame(state);
        await driver.pressChord(['Enter'],{holdMs:160,releaseMs:120});
        report.controller_inputs.push({device:'keyboard-to-source-PAD',key:'Enter',
          hold_ms:160,release_ms:120,source_frame_before:sourceFrameBefore,
          label:`results-${matchIndex}-keyboard-gated-continue-${pulses}`});
        if(pulses===initialEnterDispatches.length)
          pageCheck.post_gate_ordinary_enter_dispatch_source_frame=sourceFrameBefore;
        const pulseDeadline=Date.now()+2500;
        do{
          state=await resumeResultsIfPaused(await diagnostic());
          if(state.error)throw Error(`Results ${matchIndex} Enter pulse ${pulses}: ${state.error}`);
          if(state.phase===1)break;
          if(state.phase!==5&&state.phase!==6&&state.phase!==8&&state.phase!==9)
            throw Error(`Results ${matchIndex} entered unexpected source phase ${state.phase}`);
          await page.waitForTimeout(100);
        }while(Date.now()<pulseDeadline);
      }
      result.results_keyboard_pulse_count=pulses;
      await retainResultsInputEvents();
      result.results_keyboard_events=(report.results_input_events||[]).slice(inputEventStart);
      assert.equal(result.results_keyboard_events.length,pulses*2,
        'Every ordinary keyboard pulse must retain one original Enter keydown and keyup');
      assert(result.results_keyboard_events.every(row=>row.key==='Enter'&&
          row.isTrusted&&!row.repeat),
        'Keyboard confirmation events must remain trusted, non-repeat Enter edges');
      const keydowns=result.results_keyboard_events.filter(row=>row.kind==='keydown');
      const keyups=result.results_keyboard_events.filter(row=>row.kind==='keyup');
      assert.equal(keydowns.length,pulses,
        'Every scheduled keyboard pulse must retain one trusted Enter keydown');
      assert.equal(keyups.length,pulses,
        'Every scheduled keyboard pulse must retain one trusted Enter keyup');
      const initialPulseCount=pageCheck.initial_enter_pulse_count;
      const initialStart=gate.initial_start;
      pageCheck.initial_keydown_source_frame=keydowns[initialPulseCount-1]?.resultsSourceFrameAtEvent??null;
      const initialStartPulseIndex=keydowns.findIndex((row,index)=>
        row.resultsSourceFrameAtEvent<=initialStart.first_source_frame&&
        keyups[index]?.resultsSourceFrameAtEvent>=initialStart.first_source_frame);
      assert(initialStartPulseIndex>=0,
        'A trusted initial Enter down/up source-frame bracket must contain the first consumed P1 Start');
      pageCheck.initial_start_keyboard_event_bracket={
        keydown_source_frame:keydowns[initialStartPulseIndex].resultsSourceFrameAtEvent,
        keyup_source_frame:keyups[initialStartPulseIndex].resultsSourceFrameAtEvent};
      const lastPageTransition=Math.max(...gate.transitions.map(item=>item.source_frame));
      const postPageKeydownIndex=keydowns.findIndex(row=>
        row.resultsSourceFrameAtEvent>lastPageTransition);
      assert(postPageKeydownIndex>=initialPulseCount,
        'A new trusted ordinary Enter confirmation must be dispatched after both automatic CPU page transitions');
      const postPageKeydown=keydowns[postPageKeydownIndex];
      assert(keydowns.slice(initialPulseCount).every(row=>
        row.resultsSourceFrameAtEvent>lastPageTransition),
        'Every confirmation Enter edge after the initial trigger must follow both automatic CPU page transitions');
      pageCheck.keyboard_keydown_source_frame=postPageKeydown.resultsSourceFrameAtEvent;
      pageCheck.status='post-page-keyboard-dispatched';
    }
  }else{
    await writeProgress(`match-${matchIndex}-natural-results`);
    await screenshot(`match-${matchIndex}-natural-results`);
    for(let pulse=0;pulse<48&&state.phase!==1;pulse++){
      state=await resumeResultsIfPaused(state);
      await driver.pressChord(['Enter'],{holdMs:160,releaseMs:120});
      report.controller_inputs.push({device:'keyboard-to-source-PAD',key:'Enter',
        hold_ms:160,release_ms:120,label:`results-${matchIndex}-continue-${pulse}`});
      if(pulse===0)
        report.phases.push(`Results ${matchIndex}: focused Enter sent through the ordinary keyboard-to-source-PAD path`);
      const pulseDeadline=Date.now()+2500;
      do{
        state=await resumeResultsIfPaused(await diagnostic());
        if(state.error)throw Error(`Results ${matchIndex}: ${state.error}`);
        if(state.phase===1)break;
        if(state.phase!==5&&state.phase!==6&&state.phase!==8&&state.phase!==9)
          throw Error(`Results ${matchIndex} entered unexpected source phase ${state.phase}`);
        await page.waitForTimeout(100);
      }while(Date.now()<pulseDeadline);
    }
  }
  if(resultsInputMode==='source-tick'&&state.phase!==1){
    const returnDeadline=Date.now()+60000;
    while(Date.now()<returnDeadline&&state.phase!==1){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`Results ${matchIndex} source-tick return: ${state.error}`);
      if(state.phase===1)break;
      assert([5,6,8,9].includes(state.phase),
        `Results ${matchIndex} source-tick return entered unexpected phase ${state.phase}`);
      await page.waitForTimeout(8);
    }
  }
  assert.equal(state.phase,1,`Natural Results ${matchIndex} did not return to original CSS`);
  const returned=await writeProgress(`match-${matchIndex}-returned-css`);
  assert(returned.memory.menu_present,'CSS owner should be live after Results return');
  assert(!returned.memory.match_present&&!returned.memory.results_present,
    'Prior match and Results owners must be torn down at CSS return');
  await screenshot(`match-${matchIndex}-returned-css`);
  const sourcePadTraceRecord=await retainResultsSourcePadTrace(matchIndex,'natural-results-to-css');
  if(resultsInputMode==='keyboard-gated'){
    assert(result.results_keyboard_page_gate,
      'Keyboard-gated Results cannot pass without the observed disconnected CPU auto-page gate');
    const sourcePadTrace=sourcePadTraceRecord?.trace;
    const sourcePadSummary=sourcePadTraceRecord?.summary;
    assert(sourcePadTrace&&sourcePadSummary&&!sourcePadSummary.overflow,
      'Keyboard-gated Results validation requires a complete raw PAD trace');
    assert.deepEqual(sourcePadSummary.tick_failed,[],
      'Every retained Results source tick must return before the CSS lifecycle check');
    if(result.results_keyboard_page_gate){
      assert.deepEqual(sourcePadSummary.results_page_transitions.filter(row=>
        row.from===0&&row.to===1).map(row=>row.slot),[2,3],
        'Both disconnected CPU pages must auto-advance from page zero before keyboard confirmation');
      assert(sourcePadSummary.results_page_transitions.every(row=>
        (row.slot===2||row.slot===3)&&row.to===row.from+1),
        'Only disconnected CPU pages may auto-advance during the retained Results trace');
    }
    assert.deepEqual(sourcePadSummary.port_error_values,[[0],[0],[-1],[-1]],
      'Natural P1/P2-connected and CPU-P3/P4-disconnected port status changed');
    const neutralResultsAnalogFields=['stick_x','stick_y','substick_x','substick_y',
      'trigger_left','trigger_right','analog_a','analog_b','ext_button'];
    assert(sourcePadTrace.samples.every(row=>row.pads.every((pad,port)=>
      (port===0?(pad.button===0||pad.button===buttonStart):pad.button===0)&&
      pad.err===(port<2?0:-1)&&
      neutralResultsAnalogFields.every(field=>pad[field]===0))),
      'Only P1 Start may be pressed; keep every other button/axis neutral and preserve all four port statuses');
    if(result.results_keyboard_page_gate){
      const pageCheck=report.results_page_transition_checks.find(row=>
        row.match===matchIndex&&row.status==='post-page-keyboard-dispatched');
      assert(pageCheck,'The source-tick CPU page gate was not retained before the next keyboard confirmation');
      const pageZeroTransitions=sourcePadSummary.results_page_transitions.filter(row=>
        row.from===0&&row.to===1);
      const latestTransition=Math.max(...pageZeroTransitions.map(row=>row.source_frame));
      const firstTransition=Math.min(...pageZeroTransitions.map(row=>row.source_frame));
      const initialStart=sourcePadSummary.p1_start_runs.find(row=>
        row.last_source_frame<firstTransition);
      assert(initialStart&&initialStart.last_source_frame<firstTransition,
        'The initial P1 Start must enter statistics before the automatic CPU page transitions');
      const beforeInitialStart=sourcePadTrace.samples.find(row=>
        row.source_frame===initialStart.first_source_frame-1);
      assert(beforeInitialStart?.results_state_after_tick?.phase===2,
        'The first consumed keyboard Start must be preceded by the original Results fade phase');
      const keyboardEvents=result.results_keyboard_events||[];
      const keydowns=keyboardEvents.filter(row=>row.kind==='keydown');
      const keyups=keyboardEvents.filter(row=>row.kind==='keyup');
      assert.equal(keyboardEvents.length,result.results_keyboard_pulse_count*2,
        'Each ordinary Results Enter pulse must retain one trusted keydown and keyup');
      assert.equal(keydowns.length,result.results_keyboard_pulse_count,
        'Retained keyboard keydown count must match sent historical Enter pulses');
      assert.equal(keyups.length,result.results_keyboard_pulse_count,
        'Retained keyboard keyup count must match sent historical Enter pulses');
      const resultsKeyboardInputs=report.controller_inputs.filter(row=>
        row.device==='keyboard-to-source-PAD'&&
        row.label?.startsWith(`results-${matchIndex}-keyboard-`));
      assert.equal(resultsKeyboardInputs.length,keydowns.length,
        'Each retained ordinary Results Enter edge must have its original hold/release input record');
      assert(resultsKeyboardInputs.every(row=>row.key==='Enter'&&
          row.hold_ms===160&&row.release_ms===120),
        'Ordinary keyboard Enter must preserve its 160ms hold and 120ms release semantics');
      assert(keyboardEvents.every(row=>row.key==='Enter'&&row.isTrusted&&!row.repeat),
        'Every retained ordinary keyboard edge must be a trusted non-repeat Enter');
      const initialPulseCount=pageCheck.initial_enter_pulse_count;
      const initialStartPulseIndex=keydowns.findIndex((row,index)=>
        row.resultsSourceFrameAtEvent<=initialStart.first_source_frame&&
        keyups[index]?.resultsSourceFrameAtEvent>=initialStart.first_source_frame);
      assert(initialStartPulseIndex>=0&&initialStartPulseIndex<initialPulseCount,
        'The initial consumed P1 Start must lie within one of the ordinary Enter down/up source-frame brackets');
      pageCheck.initial_start_keyboard_event_bracket={
        keydown_source_frame:keydowns[initialStartPulseIndex].resultsSourceFrameAtEvent,
        keyup_source_frame:keyups[initialStartPulseIndex].resultsSourceFrameAtEvent};
      const firstTransitionFrame=Math.min(...pageZeroTransitions.map(row=>row.source_frame));
      assert(!keydowns.some(row=>row.resultsSourceFrameAtEvent>firstTransitionFrame&&
          row.resultsSourceFrameAtEvent<=latestTransition),
        'No ordinary Enter confirmation may be dispatched between staggered CPU page transitions');
      const firstPostPageKeydownIndex=keydowns.findIndex(row=>
        row.resultsSourceFrameAtEvent>latestTransition);
      assert(firstPostPageKeydownIndex>=initialPulseCount,
        'The ordinary Enter confirmation must be dispatched after both automatic CPU page transitions');
      assert(keydowns.slice(initialPulseCount).every(row=>
        row.resultsSourceFrameAtEvent>latestTransition),
        'Every ordinary Enter confirmation must be dispatched after both automatic CPU page transitions');
      const postPageConfirmation=findConsumedResultsStartKeyboardAttempt(
        keydowns.slice(initialPulseCount),keyups.slice(initialPulseCount),
        sourcePadSummary.p1_start_runs,latestTransition);
      assert(postPageConfirmation.accepted,
        'A post-page Enter down/up source-frame bracket must contain a consumed P1 Start sample');
      const postPageStartFrame=postPageConfirmation.accepted.consumed_start_source_frame;
      const postPageKeydownIndex=initialPulseCount+postPageConfirmation.accepted.index;
      const postPageKeydown=keydowns[postPageKeydownIndex];
      pageCheck.post_page_confirmation_attempts=postPageConfirmation.attempts;
      pageCheck.post_page_start_runs=sourcePadSummary.p1_start_runs.filter(row=>
        row.first_source_frame>latestTransition);
      pageCheck.confirmation_source_frame=postPageStartFrame;
      pageCheck.keyboard_keydown_source_frame=postPageKeydown.resultsSourceFrameAtEvent;
      pageCheck.confirmation_attempt_index=postPageConfirmation.accepted.index;
      pageCheck.confirmation_consumed=pageCheck.confirmation_source_frame!==null;
      pageCheck.status='pass-input-dispatched-after-pages';
      result.results_keyboard_page_verification={
        status:'pass-input-dispatched-after-pages',
        initial_input_phase_gate:result.results_keyboard_phase_gate,
        auto_page_gate_poll_lower_bound_source_frame:pageCheck.auto_page_gate_poll_lower_bound_source_frame,
        initial_keyboard_keydown_source_frames:pageCheck.initial_keydown_source_frames,
        source_frame_before_confirmation:result.results_keyboard_page_gate.source_frame,
        initial_p1_start_source_frame:initialStart.first_source_frame,
        stats_phase_start_source_frame:pageCheck.stats_phase_start_source_frame,
        cpu_page_delay_source_ticks:pageCheck.cpu_page_delay_source_ticks,
        keyboard_keydown_source_frame:pageCheck.keyboard_keydown_source_frame,
        confirmation_p1_start_source_frame:pageCheck.confirmation_source_frame,
        confirmation_attempt_index:pageCheck.confirmation_attempt_index,
        post_page_confirmation_attempts:pageCheck.post_page_confirmation_attempts,
        confirmation_consumed:pageCheck.confirmation_consumed,
        cpu_page_transitions:sourcePadSummary.results_page_transitions,
        p1_start_runs:sourcePadSummary.p1_start_runs,
        post_page_start_runs:sourcePadSummary.p1_start_runs.filter(row=>
          row.first_source_frame>latestTransition),
        port_error_values:sourcePadSummary.port_error_values,
        trusted_keydowns:keydowns.length,tick_failed_source_frames:sourcePadSummary.tick_failed};
    }
    assert.equal(result.results_keyboard_page_verification?.status,
      'pass-input-dispatched-after-pages',
      'Keyboard-gated Results cannot pass without a retained post-page keyboard confirmation');
  }else if(resultsInputMode==='source-tick'){
    const sourcePadTrace=sourcePadTraceRecord?.trace;
    const sourcePadSummary=sourcePadTraceRecord?.summary;
    assert(sourcePadTrace&&sourcePadSummary&&!sourcePadSummary.overflow,
      'Source-tick Results validation requires a complete raw PAD trace');
    const startRows=sourcePadTrace.samples.filter(row=>row.pads[0].button===buttonStart);
    const expectedRows=result.results_source_start_pulse_frames.flatMap(first=>
      Array.from({length:10},(_,offset)=>first+offset));
    assert.deepEqual(startRows.map(row=>row.source_frame),expectedRows,
      'Source-tick Results P1 Start samples differ from the queued ten-tick pulses');
    const p1ActiveRows=sourcePadTrace.samples.filter(row=>row.pads[0].button!==0);
    assert.deepEqual(p1ActiveRows.map(row=>row.source_frame),expectedRows,
      'Source-tick Results input emitted an unexpected P1 button or lost its neutral release edges');
    const releases=result.results_source_start_pulse_frames.map(frame=>
      sourcePadTrace.samples.find(row=>row.source_frame===frame+10));
    assert(releases.every(row=>row&&row.tick_returned&&row.pads[0].button===0&&row.pads[0].err===0),
      'Each ten-tick P1 Start hold must be followed by a consumed neutral release sample');
    assert(startRows.every(row=>row.tick_returned&&row.pads[0].err===0),
      'Every scheduled P1 Start sample must return normally with a connected P1');
    assert(startRows.every(row=>row.pads[1].button===0&&row.pads[1].err===0&&
      row.pads[2].button===0&&row.pads[2].err===-1&&
      row.pads[3].button===0&&row.pads[3].err===-1),
      'Results P1-only source input changed another port or its natural connectedness');
    const cpuPageTransitions=sourcePadSummary.results_page_transitions.filter(
      row=>row.from===0&&row.to===1&&row.slot>=2);
    if(result.results_source_start_pulse_frames.length){
      const firstStart=result.results_source_start_pulse_frames[0];
      const preceding=sourcePadTrace.samples.find(row=>row.source_frame===firstStart-1);
      assert(preceding&&preceding.results_state_after_tick?.phase===2,
        'The first P1 Start edge must be consumed from original Results phase 2, not ignored during its fade');
    }
    if(report.results_page_transition_checks.some(row=>row.match===matchIndex&&row.status==='pass')){
      assert.deepEqual(cpuPageTransitions.map(row=>row.slot),[2,3],
        'Disconnected CPU statistics pages did not each auto-advance exactly once');
      const confirmationFrame=result.results_page_transition_checks.find(
        row=>row.match===matchIndex&&row.status==='pass').confirmation_source_frame;
      assert(cpuPageTransitions.every(row=>row.phase===3&&row.stats_phase===2&&
        row.source_frame<confirmationFrame),
        'Disconnected CPU pages must auto-advance in the active statistics phase before P1 confirmation');
    }
    if(report.results_page_transition_checks.some(row=>row.match===matchIndex&&row.status==='pass'))
      assert.deepEqual(sourcePadSummary.results_page_transitions.map(row=>row.slot),[2,3],
        'Connected neutral ports or another Results player page changed unexpectedly');
    const autoPageStatus=report.results_page_transition_checks.some(
      row=>row.match===matchIndex&&row.status==='pass')?'pass':
      result.results_source_input_stop?.auto_page_gate||'not reached before natural CSS';
    report.results_source_pad_verification={status:'pass',auto_page_gate:autoPageStatus,
      start_frames:result.results_source_start_pulse_frames,
      held_ticks:startRows.length,ports:{p1:'connected, P1 Start only',p2:'connected, neutral',
        p3:'disconnected CPU PAD',p4:'disconnected CPU PAD'},
      disconnected_cpu_page_transitions:cpuPageTransitions,tick_returned:true};
  }
}

try{
  const discStat=await fs.stat(values.disc);
  report.disc={bytes:discStat.size,sha256:await sha256(values.disc)};
  report.url=values.url;
  const {chromium,browser:launchOptions,browserPath,playwrightPath}=await loadBrowserTools(values.playwright);
  browser=await chromium.launch({...browserLaunchOptions(launchOptions),headless:true});
  report.browser={name:'headless Chrome',executable:path.basename(browserPath),version:browser.version(),playwright:playwrightPath};
  page=await browser.newPage({viewport:{width:1280,height:900},deviceScaleFactor:1});
  // Hash the bytes actually served to this page, not a guessed build directory.
  // Null-video references and these rendered functional runs remain separate.
  page.on('response',response=>{
    const address=new URL(response.url());
    if(address.origin!==url.origin||! /\.(?:html|css|m?js|wasm)$/.test(address.pathname))return;
    if(address.pathname.endsWith('/gameplay_menu_browser.wasm')&&localWasmIdentity){
      const headers=response.headers();
      const contentLength=Number(headers['content-length']||0);
      const contentEncoding=headers['content-encoding']||null;
      if(response.status()!==200||contentLength!==localWasmIdentity.bytes||contentEncoding){
        artifactReads.push(Promise.resolve({url:response.url(),error:
          'Wasm HTTP status/length/encoding does not match the local static artifact identity'}));
        return;
      }
      report.provenance.served_artifacts.push({url:response.url(),status:response.status(),
        bytes:localWasmIdentity.bytes,sha256:localWasmIdentity.sha256,
        hash_basis:{method:'same-origin-static-file-sha256-plus-http-metadata',
          path:localWasmIdentity.path,content_length:contentLength,content_encoding:contentEncoding}});
      return;
    }
    artifactReads.push(response.body().then(bytes=>{
      report.provenance.served_artifacts.push({url:response.url(),status:response.status(),
        bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
    }).catch(error=>({url:response.url(),error:error.message})));
  });
  page.setDefaultTimeout(30000);page.setDefaultNavigationTimeout(60000);
  page.on('pageerror',e=>report.page_errors.push({kind:'pageerror',message:e.stack||e.message}));
  page.on('console',m=>{
    const message=m.text();
    if(/Unsupported native fighter command opcode|Bound fighter/.test(message)&&
       report.native_command_errors.length<24)
      report.native_command_errors.push({type:m.type(),message});
    if(m.type()==='error'||message.startsWith('SHEIK_FINISH_')||
        message.startsWith('Unsupported source CPU carry:'))
      report.page_errors.push({kind:'console',message});
  });
  driver=createBrowserDriver(page,{timeoutMs:60000,deadline:Date.now()+65*60*1000});
  const response=await page.goto(values.url,{waitUntil:'domcontentloaded'});
  assert.equal(response?.status(),200,'runtime.html must load through the real HTTP server');
  assert.equal(response.headers()['cross-origin-opener-policy'],'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'],'require-corp');
  report.gpu=await page.evaluate(async()=>{
    const adapter=await navigator.gpu?.requestAdapter();
    return {cross_origin_isolated:crossOriginIsolated,adapter_available:!!adapter,
      info:adapter?{vendor:adapter.info.vendor,architecture:adapter.info.architecture,
        device:adapter.info.device,description:adapter.info.description}:null};
  });
  assert(report.gpu.cross_origin_isolated&&report.gpu.adapter_available,
    'Rendered validation requires an isolated page and a WebGPU adapter');
  await driver.waitForImport();
  await installResultsInputObserver();
  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  await driver.launch(1);
  await waitFor('original VS CSS initial entry',s=>s.phase===1&&s.css,60000);
  await screenshot('initial-css');
  report.initial_css=await writeProgress('initial-css');
  await configureRoster(lineup);
  if(values['setup-only']){report.result='setup-only-pass';}
  else await runMatch(1,lineup);
  if(!values['setup-only']&&matchCount===2){
    let secondLineup=lineup;
    if(values.lineup==='B'){
      secondLineup=lineup.map(fighter=>({...fighter}));
      // Zelda and Sheik share one source CSS icon/ckind. The visible icon row
      // cannot select the alternate fighter identity by asking the geometry
      // observer for a nonexistent second icon; exercise Sheik through the
      // original in-match down-B transition in the dedicated form trace.
      report.phases.push('B match 2 retains Zelda at the shared CSS icon; Sheik is covered by a separate in-match down-B scenario');
    }
    const retained=await verifyRoster(secondLineup,'four-CPU9 retained roster before second match');
    assert.equal(retained.length,4);
    await runMatch(2,secondLineup);
  }
  if(!values['setup-only'])report.result='pass';
}catch(error){
  report.failure={message:error.message,stack:error.stack};
  process.exitCode=1;
  if(page&&!page.isClosed()){
    await retainResultsEntry('failure-latest-entry');
    await diagnostic().then(state=>{report.failure.diagnostics=state;}).catch(()=>{});
    await screenshot('failure').catch(()=>{});
    await page.locator('body').textContent().then(text=>fs.writeFile(path.join(output,'page.txt'),text)).catch(()=>{});
  }
}finally{
  if(page&&!page.isClosed()){
    await retainResultsInputEvents();
    if(activeMatchIndex!==null&&report.matches.some(row=>row.match===activeMatchIndex)&&
       !report.results_source_pad_traces.some(row=>row.match===activeMatchIndex))
      await retainResultsSourcePadTrace(activeMatchIndex,'final-state-after-match-stop');
  }
  report.final_diagnostics=page&&!page.isClosed()?await diagnostic().catch(error=>({error:error.message})):null;
  report.controller_inputs=report.controller_inputs||[];
  report.controller_input_summary={pad_samples:report.pad_sample_count,first_samples:report.controller_inputs.slice(0,48),last_samples:report.controller_inputs.slice(-24)};
  const artifactFailures=(await Promise.all(artifactReads)).filter(Boolean);
  report.provenance.source_end=sourceProvenance();
  report.provenance.source_unchanged=JSON.stringify(report.provenance.source_start)===
    JSON.stringify(report.provenance.source_end);
  report.provenance.artifact_read_failures=artifactFailures;
  if(artifactFailures.length){
    report.result='fail';process.exitCode=1;
    report.failure??={message:'Failed to retain served artifact provenance'};
  }
  if(page&&!page.isClosed())driver?.dispose();
  if(browser)await browser.close();
  await fs.writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
}
if(!['pass','setup-only-pass'].includes(report.result))
  throw Error(report.failure?.message||'Headless CPU9 lineup scenario failed');
console.log(JSON.stringify({result:report.result,lineup:report.lineup,matches:report.matches.length,
  browser:report.browser,output}));
