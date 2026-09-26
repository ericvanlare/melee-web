#!/usr/bin/env node
/** Headless rendered four-CPU9 source-menu match/Results/CSS loop fixture.
 * Inputs are bounded raw PAD samples consumed by the live original CSS/SSS;
 * fighter setup and match state are read-only source observations. This does
 * not compare against retail, physical controllers, timing, pixels or PCM. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

const {values}=parseArgs({options:{...Object.fromEntries(
  ['url','disc','out','lineup','playwright'].map(name=>[name,{type:'string'}])),
  matches:{type:'string'},'setup-only':{type:'boolean'}}});
if(!values.url||!values.disc||!values.out||!['A','B'].includes(values.lineup))
  throw Error('Use --url http://127.0.0.1:PORT/runtime.html --disc OWNED_CISO --out NEW_DIRECTORY --lineup A|B [--matches 1|2] [--setup-only] [--playwright PACKAGE_DIR]');
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
const lineup=values.lineup==='A'?
  [{name:'Game & Watch',kind:3,position:[7.1,2.5]},
   {name:'Kirby',kind:4,position:[0.1,9.5]},
   {name:'Ice Climbers',kind:14,position:[-6.9,9.5]},
   {name:'Fox',kind:2,position:[-20.9,9.5]}]:
  [{name:'Samus',kind:16,position:[7.1,9.5]},
   {name:'Yoshi',kind:17,position:[7.1,16.5]},
   {name:'Zelda',kind:18,position:[9.1,9.5]},
   {name:'Falco',kind:20,position:[-32.2,9.5]}];
const report={schema:'melee-web-cpu9-lineup-browser-v1',result:'fail',
  scope:'Headless Chrome rendered gameplay; live source CSS/SSS controller input, four CPU9 players, four stocks, Final Destination; natural Results→CSS→second match. No retail comparison, pixels, PCM, foreground timing, physical-controller or performance claim.',
  lineup:values.lineup,players:lineup.map(({name,kind})=>({name,kind,cpu:9,stocks:4})),
  matches:[],screenshots:[],source_progress:[],pad_sample_count:0,page_errors:[],phases:[],controller_inputs:[]};
report.source_timing_disruptions=[];
report.native_command_errors=[];
let browser,page,driver;
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
  const result={match:matchIndex,entered_results_phase:state.phase,
    source_diagnostics:state.diagnostics,terminal_match:state.match,
    memory_at_results:state.memory,players:expected.map(({name,kind})=>({name,kind,cpu:9,stocks:4}))};
  report.matches.push(result);
  report.phases.push({label:`match ${matchIndex} naturally reached Results/Prize`,phase:state.phase,match:state.match});
  await writeProgress(`match-${matchIndex}-natural-results`);
  await screenshot(`match-${matchIndex}-natural-results`);
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
  assert.equal(state.phase,1,`Natural Results ${matchIndex} did not return to original CSS`);
  const returned=await writeProgress(`match-${matchIndex}-returned-css`);
  assert(returned.memory.menu_present,'CSS owner should be live after Results return');
  assert(!returned.memory.match_present&&!returned.memory.results_present,
    'Prior match and Results owners must be torn down at CSS return');
  await screenshot(`match-${matchIndex}-returned-css`);
}

try{
  const discStat=await fs.stat(values.disc);
  report.disc={bytes:discStat.size,sha256:await sha256(values.disc)};
  report.url=values.url;
  const {chromium,browser:launchOptions,browserPath,playwrightPath}=await loadBrowserTools(values.playwright);
  browser=await chromium.launch({...browserLaunchOptions(launchOptions),headless:true});
  report.browser={name:'headless Chrome',executable:path.basename(browserPath),version:browser.version(),playwright:playwrightPath};
  page=await browser.newPage({viewport:{width:1280,height:900},deviceScaleFactor:1});
  page.setDefaultTimeout(30000);page.setDefaultNavigationTimeout(60000);
  page.on('pageerror',e=>report.page_errors.push({kind:'pageerror',message:e.stack||e.message}));
  page.on('console',m=>{
    const message=m.text();
    if(/Unsupported native fighter command opcode|Bound fighter/.test(message)&&
       report.native_command_errors.length<24)
      report.native_command_errors.push({type:m.type(),message});
    if(m.type()==='error'||message.startsWith('SHEIK_FINISH_'))
      report.page_errors.push({kind:'console',message});
  });
  driver=createBrowserDriver(page,{timeoutMs:60000,deadline:Date.now()+65*60*1000});
  const response=await page.goto(values.url,{waitUntil:'domcontentloaded'});
  assert.equal(response?.status(),200,'runtime.html must load through the real HTTP server');
  assert.equal(response.headers()['cross-origin-opener-policy'],'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'],'require-corp');
  await driver.waitForImport();
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
    await diagnostic().then(state=>{report.failure.diagnostics=state;}).catch(()=>{});
    await screenshot('failure').catch(()=>{});
    await page.locator('body').innerText().then(text=>fs.writeFile(path.join(output,'page.txt'),text)).catch(()=>{});
  }
}finally{
  report.final_diagnostics=page&&!page.isClosed()?await diagnostic().catch(error=>({error:error.message})):null;
  report.controller_inputs=report.controller_inputs||[];
  report.controller_input_summary={pad_samples:report.pad_sample_count,first_samples:report.controller_inputs.slice(0,48),last_samples:report.controller_inputs.slice(-24)};
  report.controller_inputs=undefined;
  if(page&&!page.isClosed())driver?.dispose();
  if(browser)await browser.close();
  await fs.writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
}
if(!['pass','setup-only-pass'].includes(report.result))
  throw Error(report.failure?.message||'Headless CPU9 lineup scenario failed');
console.log(JSON.stringify({result:report.result,lineup:report.lineup,matches:report.matches.length,
  browser:report.browser,output}));
