#!/usr/bin/env node
/**
 * Ordinary two-keyboard whole-match sequences for the selected development
 * fighters. This records functional route evidence only; it does not claim
 * retail equivalence, rendering/audio equivalence, latency or performance.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {loadBrowserTools} from '../scripts/browser_tools.mjs';

const {values}=parseArgs({options:Object.fromEntries(
  ['url','disc','out','playwright','sequence'].map(key=>[key,{type:'string'}]))});
if(!values.url||!values.disc||!values.out)
  throw Error('Use --url DEVELOPMENT_RUNTIME_URL --disc OWNED_DISC --out NEW_LOCAL_DIRECTORY [--sequence NAME] [--playwright PACKAGE_DIR]');

const evidencePath=path.resolve(new URL('../docs/evidence/versus-sequences-v1.json',import.meta.url).pathname);
const inventory=JSON.parse(await fs.readFile(evidencePath,'utf8'));
assert.equal(inventory.schema,'melee-web-versus-sequences-v1');
assert.equal(inventory.status,'predeclared');
const selected=values.sequence?
  inventory.inventories.filter(item=>item.name===values.sequence):inventory.inventories;
if(values.sequence&&!selected.length)throw Error(`Unknown predeclared sequence: ${values.sequence}`);
await fs.mkdir(values.out); // Preserve the first failure; never overwrite a run.

const {chromium,browser:launchOptions}=await loadBrowserTools(values.playwright);
const browser=await chromium.launch({...launchOptions,headless:false});
const page=await browser.newPage({viewport:{width:1280,height:960}});
const driver=createBrowserDriver(page,{timeoutMs:60000,deadline:Date.now()+1800000});
const report={schema:inventory.report_schema,inventory:evidencePath,
  inventory_spec:inventory,
  harness_sha256:createHash('sha256').update(await fs.readFile(new URL(import.meta.url))).digest('hex'),
  inventory_sha256:createHash('sha256').update(await fs.readFile(evidencePath)).digest('hex'),
  inventory_status:inventory.status,browser:browser.version(),sequence_filter:values.sequence||null,
  scope:inventory.scope,checks:[],boundaries:[],sequences:[],owner_lifetime_limit:128,
  owner_lifetime:null,completed:false};

const check=async(name,run)=>{await run();report.checks.push(name);console.log(name);};
const runtimeError=()=>page.locator('#status').getAttribute('data-runtime-error');
async function snapshot(boundary,sequence,matchIndex,stage){
  const state=await page.evaluate(()=>({
    phase:Module._melee_web_native_menu_phase(),
    running:!!Module._melee_web_native_menu_running(),
    match:window.menuObserveMatch(),
    memory:JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_memory())),
    diagnostics:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),
    input:JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())),
    owner_lifetime_count:window.__versusOwnerLifetime?.events.length||0
  }));
  report.boundaries.push({boundary,stage:stage||null,sequence,match_index:matchIndex,...state});
  return state;
}
async function waitMatchReady(){
  await page.waitForFunction(()=>{
    const error=document.querySelector('#status')?.dataset.runtimeError;
    if(error)return {error};
    const state=window.menuObserveMatch?.();
    return Module._melee_web_native_menu_phase()===7&&Module._melee_web_native_menu_running()&&
      state?.frame>0&&state.ready&&!state.paused&&!state.ending?{ready:true}:false;
  },null,{timeout:60000}).then(async result=>{
    const value=await result.jsonValue();await result.dispose();
    if(value?.error)throw Error(value.error);
  });
}
async function waitForStockDrop(previous){
  const result=await page.waitForFunction(previous=>{
    const error=document.querySelector('#status')?.dataset.runtimeError;
    if(error)return {error};
    const state=window.menuObserveMatch?.();
    if(!state?.players)return false;
    return state.complete||(state.ready&&state.players[0].stocks<previous)?state:false;
  },previous,{timeout:45000});
  const value=await result.jsonValue();await result.dispose();
  if(value?.error)throw Error(value.error);
  return value;
}
async function waitForGroundedRespawn(stocks){
  const result=await page.waitForFunction(stocks=>{
    const error=document.querySelector('#status')?.dataset.runtimeError;
    if(error)return {error};
    const state=window.menuObserveMatch?.();
    const player=state?.players?.[0];
    return state?.ready&&!state.paused&&!state.ending&&player?.stocks===stocks&&
      player.motion===14&&player.groundAir===0?{ready:true}:false;
  },stocks,{timeout:45000});
  const value=await result.jsonValue();await result.dispose();
  if(value?.error)throw Error(value.error);
}
async function waitForCssOrPrize(){
  const result=await page.waitForFunction(()=>{
    const error=document.querySelector('#status')?.dataset.runtimeError;
    if(error)return {error};
    const phase=Module._melee_web_native_menu_phase();
    return (phase===1||phase===9)&&Module._melee_web_native_menu_running()?{phase}:false;
  },null,{timeout:60000});
  const value=await result.jsonValue();await result.dispose();
  if(value?.error)throw Error(value.error);
  return value.phase;
}
async function ownerLifetimeState(){
  return page.evaluate(()=>{
    const state=window.__versusOwnerLifetime;
    return state?{limit:state.limit,overflow:state.overflow,events:state.events}:null;
  });
}
function validateOwnerEvent(event){
  assert.equal(typeof event.boundary,'string','Owner lifetime callback omitted its boundary name');
  const memory=event.memory;
  assert.ok(memory&&typeof memory==='object',`${event.boundary}: owner lifetime memory is missing`);
  for(const field of ['source_allocation_identity','source_allocation_generation',
    'source_allocation_bytes','source_world_generation','source_objects','source_processes',
    'source_heap_free_bytes','cached_archives','cached_audio_banks'])
    assert.equal(typeof memory[field],'number',`${event.boundary}: missing ${field}`);
  if(/-after-(?:teardown|scene-teardown|unload)$/.test(event.boundary)){
    assert.equal(memory.source_world_generation,0,`${event.boundary}: source world generation survived teardown`);
    assert.equal(memory.source_objects,0,`${event.boundary}: source objects survived teardown`);
    assert.equal(memory.source_processes,0,`${event.boundary}: source processes survived teardown`);
  }
  return memory;
}
async function assertOwnerTeardown(sequence,matchIndex,startCount,expectPrize){
  const state=await ownerLifetimeState();
  assert.ok(state,`${sequence} match ${matchIndex}: owner lifetime collector was not installed`);
  assert.equal(state.overflow,false,'Owner lifetime collector exceeded its bounded 128-event report');
  const events=state.events.slice(startCount);
  const names=events.map(event=>event.boundary);
  assert.ok(names.includes('match-after-teardown'),
    `${sequence} match ${matchIndex}: missing match-after-teardown owner event`);
  assert.ok(names.includes('results-after-teardown'),
    `${sequence} match ${matchIndex}: missing results-after-teardown owner event`);
  if(expectPrize)assert.ok(names.includes('prize-after-teardown'),
    `${sequence} match ${matchIndex}: missing prize-after-teardown owner event`);
  for(const event of events)validateOwnerEvent(event);
}
function insideTarget(observation){
  const [, , modelX,modelY,left,right,top,bottom]=observation.geometry;
  return modelX>left&&modelX<right&&modelY<top&&modelY>bottom;
}
function moveKeys(currentX,currentY,targetX,targetY,port){
  const tolerance=0.62;
  const keys=[];
  if(Math.abs(targetX-currentX)>tolerance)keys.push(port===0?(targetX>currentX?'d':'a'):(targetX>currentX?'ArrowRight':'ArrowLeft'));
  if(Math.abs(targetY-currentY)>tolerance)keys.push(port===0?(targetY>currentY?'w':'s'):(targetY>currentY?'ArrowUp':'ArrowDown'));
  return keys;
}
const attackKey=port=>port===0?'j':'ShiftRight';
const startKey=port=>port===0?'Enter':'End';
// CSS/StartMeleeData uses CharacterKind values, while menuObserveMatch exposes
// the source FighterKind stored in the live Fighter object.
const fighterKindByCharacterKind=Object.freeze(new Map([
  [8,0],   // Mario
  [22,21], // Dr. Mario
  [2,1],   // Fox
  [20,22], // Falco
  [9,18],  // Marth
  [23,26], // Roy
  [6,6],   // Link
  [21,20], // Young Link
]));
async function selectFighter(kind,port){
  for(let attempt=0;attempt<360;attempt++){
    const observed=await page.evaluate(({kind,port})=>window.menuObserveFighter(kind,port),{kind,port});
    if(!observed)throw Error(`CSS fighter observer unavailable for kind ${kind}, port ${port}`);
    assert.equal(observed.ids[0],port,`CSS observer returned the wrong cursor port for fighter ${kind}`);
    const [cursorX,cursorY,modelX,modelY,left,right,top,bottom]=observed.geometry;
    if(observed.ids[1]<0&&observed.ids[2]===kind&&modelX>left&&modelX<right&&modelY<top&&modelY>bottom)return;
    let targetX,targetY,ready;
    if(observed.ids[1]<0){
      targetX=modelX-3.8;targetY=modelY+2.6;
      ready=(targetX-cursorX)**2+(targetY-cursorY)**2<9;
    }else{
      targetX=(left+right)*0.5-2.7;targetY=(top+bottom)*0.5+2;
      ready=modelX>left&&modelX<right&&modelY<top&&modelY>bottom;
    }
    const keys=ready?[attackKey(port)]:moveKeys(cursorX,cursorY,targetX,targetY,port);
    if(!keys.length)keys.push(attackKey(port));
    await driver.pressChord(keys,{holdMs:20,releaseMs:20});
  }
  throw Error(`CSS fighter ${kind} on port ${port} did not converge through ordinary keyboard input`);
}
async function selectStage(stage){
  for(let attempt=0;attempt<300;attempt++){
    const observed=await page.evaluate(stage=>window.menuObserveStage(stage),stage);
    if(!observed)throw Error(`SSS stage observer unavailable for kind ${stage}`);
    const [tileIndex,selected,targetIndex,targetStage]=observed.ids;
    const [x,y,targetX,targetY]=observed.geometry;
    if(selected===stage)return;
    assert.ok(tileIndex>=0&&tileIndex<=30,'Original SSS tile index is invalid');
    assert.ok(targetIndex>=0&&targetIndex<30,'Original SSS target tile is invalid');
    assert.equal(targetStage,stage,'Original SSS target identity changed');
    const keys=moveKeys(x,y,targetX,targetY,0);
    // The observer's first ID is a tile index (30 means no hovered tile),
    // not a controller port. Confirm only once the actual selected ID agrees.
    if(!keys.length)keys.push(targetX>=x?'d':'a');
    await driver.pressChord(keys,{holdMs:20,releaseMs:20});
  }
  throw Error(`SSS stage ${stage} did not converge through ordinary keyboard input`);
}
async function enterMatch(match){
  await driver.waitForPhase(1);
  await selectFighter(match.p1,0);await selectFighter(match.p2,1);
  // Original CSS ignores Start during its 30-tick entry debounce. A default
  // Mario selection can converge before even one source tick has elapsed.
  await page.waitForTimeout(700);
  await driver.pressChord([startKey(0),startKey(1)],{holdMs:100,releaseMs:200});
  await driver.waitForPhase(3);await page.waitForTimeout(700);await selectStage(match.stage);
  await driver.pressChord([attackKey(0)],{holdMs:100,releaseMs:180});
  await waitMatchReady();
  const state=await page.evaluate(()=>window.menuObserveMatch());
  assert.deepEqual(state.players.map(player=>player.stocks),[4,4],'Match did not start with four stocks');
  assert.equal(state.players[0].fighter,fighterKindByCharacterKind.get(match.p1),
    'P1 source FighterKind differs from the selected CSS CharacterKind');
  assert.equal(state.players[1].fighter,fighterKindByCharacterKind.get(match.p2),
    'P2 source FighterKind differs from the selected CSS CharacterKind');
}
async function eliminateP1(sequence,matchIndex){
  let expected=4;const stockPath=[4];let respawns=0;const actions=[];
  while(expected>0){
    await waitForGroundedRespawn(expected);
    const before=await page.evaluate(()=>window.menuObserveMatch());
    if(before.complete)break;
    const x=before.players[0].x;const horizontal=x>=0?'a':'d';
    actions.push({stock:expected,horizontal,start_x:x,start_y:before.players[0].y});
    // Ordinary P1 movement toward an edge, followed by a jump while retaining
    // the edge direction. P2 receives no keyboard events in this loop.
    await driver.pressChord([horizontal],{holdMs:1200,releaseMs:40});
    const edge=await page.evaluate(()=>window.menuObserveMatch());
    actions.at(-1).edge_x=edge.players[0].x;actions.at(-1).edge_y=edge.players[0].y;
    await driver.pressChord([horizontal,'u'],{holdMs:600,releaseMs:80});
    const after=await waitForStockDrop(expected);const stocks=after.players[0].stocks;
    assert.ok(stocks<expected,`P1 did not lose a stock from the bounded edge/jump action at ${expected}`);
    expected=stocks;stockPath.push(stocks);
    console.log(`${sequence} match ${matchIndex}: P1 stocks ${stocks}`);
    if(stocks>0)respawns++;
  }
  // Source MatchEnd publishes winners at close, after the ending flow has
  // reported complete. Observe its terminal ranking rather than guessing.
  const completeHandle=await page.waitForFunction(()=>{
    const state=window.menuObserveMatch();
    return state?.complete&&state.terminal?state:false;
  },null,{timeout:60000});
  const final=await completeHandle.jsonValue();await completeHandle.dispose();
  assert.deepEqual(stockPath,[4,3,2,1,0],'Observed P1 stock path differs from four-stock elimination');
  assert.equal(respawns,3,'Observed respawn count differs from three respawns');
  assert.equal(final.complete,true,'Source match did not report completion');
  assert.equal(final.outcome,2,'Source match did not report stock elimination');
  assert.equal(final.terminal.outcome,2,'Source MatchEnd outcome differs from stock elimination');
  assert.deepEqual(final.terminal.winners,[1],'P2 was not the source MatchEnd winner after P1 elimination');
  return {stock_path:stockPath,respawns,complete:final.complete,outcome:final.outcome,
    winner:final.terminal.winners[0],actions,final};
}
async function confirmResults(sequence,matchIndex){
  await driver.waitForPhase(8);await snapshot('Results',sequence,matchIndex,'Results');
  // Results can stay on the original statistics page after one Start. Keep
  // sending ordinary release-separated Start chords while phase 8 remains;
  // each attempt is bounded and the source phase decides when to stop.
  let resultConfirmations=0;
  for(;resultConfirmations<8;resultConfirmations++){
    if(await page.evaluate(()=>Module._melee_web_native_menu_phase())!==8)break;
    await driver.waitForPhase(8);
    await driver.pressChord([startKey(0),startKey(1)],{holdMs:120,releaseMs:1380});
  }
  const phase=await waitForCssOrPrize();
  let prize=false;
  if(phase===9){
    prize=true;
    await snapshot('Prize',sequence,matchIndex,'Prize');
    let prizeConfirmations=0;
    for(;prizeConfirmations<8;prizeConfirmations++){
      if(await page.evaluate(()=>Module._melee_web_native_menu_phase())!==9)break;
      await driver.waitForPhase(9);
      await driver.pressChord([startKey(0),startKey(1)],{holdMs:120,releaseMs:1380});
    }
    await driver.waitForPhase(1);
  }
  else assert.equal(phase,1,'Results did not return to CSS or the original Prize route');
  await snapshot('CSS after Results/Prize',sequence,matchIndex,'CSS');
  return {result_confirmations:resultConfirmations,prize};
}

try{
  await page.goto(values.url);
  // Native owner teardown hooks are diagnostics only. The collector is
  // bounded so a runaway callback cannot turn a browser run into unbounded
  // evidence; overflow is rejected by this test, never by the runtime.
  await page.evaluate(limit=>{
    const state={limit,overflow:false,events:[]};
    window.__versusOwnerLifetime=state;
    window.menuOwnerLifetime=(boundary,memory)=>{
      if(state.events.length>=state.limit){state.overflow=true;return;}
      state.events.push({boundary,memory:JSON.parse(JSON.stringify(memory))});
    };
  },128);
  await driver.waitForImport();
  await page.waitForFunction(()=>Module._melee_web_native_menu_cache_idle()===1,null,{timeout:60000});
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('two');
  await page.getByLabel('Player 1 input source',{exact:true}).selectOption('keyboard');
  await page.getByLabel('Player 2 input source',{exact:true}).selectOption('keyboard');
  await page.locator('#controls-close').click();
  await check('owned disc -> original CSS',async()=>{await driver.selectDisc(values.disc);await driver.waitForStart();await driver.launch();});
  const documentMarker=`versus-sequences-${Date.now()}`;
  await page.evaluate(marker=>{window.versusSequencesDocument=marker;},documentMarker);
  for(const sequence of selected){
    const sequenceReport={name:sequence.name,matches:[],started:false,completed:false};
    report.sequences.push(sequenceReport);
    await check(`${sequence.name}: pre-match CSS boundary`,async()=>{
      await snapshot('CSS before match',sequence.name,null,'CSS');
    });
    for(let index=0;index<sequence.matches.length;index++){
      const match=sequence.matches[index];
      const ownerStart=(await ownerLifetimeState())?.events.length||0;
      await check(`${sequence.name} match ${index+1}: CSS -> SSS -> four-stock match`,async()=>{
        await enterMatch(match);sequenceReport.started=true;
        console.log(`${sequence.name} match ${index+1}: four-stock gameplay ready`);
        await snapshot('match start',sequence.name,index+1,'VS');
        const elimination=await eliminateP1(sequence.name,index+1);
        sequenceReport.matches.push({match,elimination});
      });
      await check(`${sequence.name} match ${index+1}: Results -> CSS`,async()=>{
        const route=await confirmResults(sequence.name,index+1);
        sequenceReport.matches.at(-1).route=route;
        await assertOwnerTeardown(sequence.name,index+1,ownerStart,route.prize);
        assert.equal(await page.evaluate(()=>window.versusSequencesDocument),documentMarker);
      });
    }
    sequenceReport.completed=true;
    await snapshot('CSS after sequence',sequence.name,null,'CSS');
  }
  await check('same source backing arena across every CSS/Results/Prize boundary',async()=>{
    const owned=report.boundaries.filter(entry=>entry.memory?.source_session_owned);
    assert.ok(owned.length>0,'No source arena ownership boundary was recorded');
    const first=owned[0].memory;
    assert.ok(first.source_allocation_identity>0&&first.source_allocation_generation>0,
      'Source arena identity/generation was not published');
    for(const entry of owned){
      for(const field of ['source_allocation_identity','source_allocation_generation','source_allocation_bytes'])
        assert.equal(entry.memory[field],first[field],`${entry.boundary}: ${field}`);
    }
  });
  await check('bounded owner lifetime report has one retained source arena',async()=>{
    const state=await ownerLifetimeState();
    assert.ok(state,'Owner lifetime collector did not publish a report');
    assert.equal(state.overflow,false,'Owner lifetime collector overflowed its 128-event bound');
    assert.ok(state.events.length<128,`Owner lifetime event count ${state.events.length} reached the bound`);
    const memories=state.events.map(validateOwnerEvent);
    assert.ok(memories.length>0,'Owner lifetime report contains no events');
    const arenaKeys=new Set(memories.map(memory=>[
      memory.source_allocation_identity,memory.source_allocation_generation,
      memory.source_allocation_bytes].join(':')));
    assert.equal(arenaKeys.size,1,'Source owner lifetime used more than one retained arena');
    assert.equal(memories[0].source_allocation_bytes,32*1024*1024,
      'Source retained arena size differs from the owned 32 MiB session');
  });
  await check('final unload releases the retained source arena',async()=>{
    await driver.unload();
    const state=await ownerLifetimeState();
    assert.equal(state.overflow,false,'Owner lifetime collector overflowed during unload');
    const last=state.events.at(-1);
    assert.equal(last.boundary,'session-after-unload');
    const memory=validateOwnerEvent(last);
    assert.equal(memory.source_session_owned,false);
    assert.equal(memory.source_allocation_identity,0);
    assert.equal(memory.source_allocation_bytes,0);
  });
  report.completed=true;
}catch(error){
  report.failure={message:error.message,stack:error.stack,diagnostics:error.diagnostics||null,
    status:await runtimeError().catch(()=>null)};
  process.exitCode=1;console.error(error.stack||error.message);
}finally{
  report.owner_lifetime=await ownerLifetimeState().catch(error=>({unavailable:error.message}));
  report.diagnostics=await driver.diagnostics().catch(error=>({unavailable:error.message}));
  report.source=await page.evaluate(()=>({
    native:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),
    input:JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())),
    document:window.versusSequencesDocument||null
  })).catch(error=>({unavailable:error.message}));
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify(report,null,2)+'\n');
  await page.screenshot({path:path.join(values.out,'boundary.png'),fullPage:true}).catch(()=>{});
  driver.dispose();await browser.close();
}
