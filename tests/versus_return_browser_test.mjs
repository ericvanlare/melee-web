#!/usr/bin/env node
/** Ordinary keyboard regression for the No Contest -> Results -> optional Prize -> CSS route.
 * Does not establish retail equivalence, physical input, or performance acceptance.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {loadBrowserTools} from '../scripts/browser_tools.mjs';
const {values}=parseArgs({options:Object.fromEntries(
  ['url','disc','out','playwright'].map(key=>[key,{type:'string'}]))});
if(!values.url||!values.disc||!values.out)
  throw Error('Use --url DEVELOPMENT_RUNTIME_URL --disc OWNED_DISC --out NEW_LOCAL_DIRECTORY [--playwright PACKAGE_DIR]');
await fs.mkdir(values.out); // Preserve the first failure; never overwrite a run.
const {chromium,browser:options}=await loadBrowserTools(values.playwright);
const browser=await chromium.launch({...options,headless:false});
const page=await browser.newPage({viewport:{width:1280,height:960}});
const driver=createBrowserDriver(page,{timeoutMs:60000,deadline:Date.now()+300000});
const report={schema:'melee-web-versus-return-browser-v1',browser:browser.version(),
  scope:'Ordinary B0XX keyboard, human P1/CPU P2, Mario/FD, three No Contest Results returns in one document with the same source arena. No retail allocator equivalence, physical input or performance acceptance.',
  checks:[],memory:[],prizeVisits:[],completed:false};
async function check(name,run){await run();report.checks.push(name);console.log(name);}
async function memory(boundary){report.memory.push({boundary,...await page.evaluate(()=>
  JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_memory())))});}
async function waitSourcePause(){
  await page.waitForFunction(()=>Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()).includes('source pause: 1'),null,{timeout:15000});
}
async function confirmResults(match){
  // The first Start can open the original statistics panels. A later Start
  // confirms the human panel; retain ordinary release edges between presses.
  for(let confirmation=0;confirmation<8;confirmation++){
    if(await page.evaluate(()=>Module._melee_web_native_menu_phase())!==8)break;
    await driver.waitForPhase(8);
    await driver.pressChord(['7'],{holdMs:120,releaseMs:1380});
  }
  assert.notEqual(await page.evaluate(()=>Module._melee_web_native_menu_phase()),8,
    'Original Results did not finish its bounded Start confirmation sequence');
  // Original profile progress may select Prize before returning to CSS.
  // Observe that route and confirm with ordinary Start presses; do not alter
  // pending flags, source routing, or the timing pause policy.
  const boundary=await page.waitForFunction(()=>{
    const error=document.querySelector('#status')?.dataset.runtimeError;
    if(error)return {error};
    const phase=Module._melee_web_native_menu_phase();
    return (phase===1||phase===9)&&Module._melee_web_native_menu_running()?{phase}:false;
  },null,{timeout:60000});
  const state=await boundary.jsonValue();await boundary.dispose();
  if(state.error)throw Error(state.error);
  if(state.phase===9){
    report.prizeVisits.push(match);await memory(`prize-match-${match}`);
    for(let confirmation=0;confirmation<120;confirmation++){
      await driver.waitForPhase(9);
      await driver.pressChord(['7'],{holdMs:120,releaseMs:380});
      const next=await page.evaluate(()=>({
        phase:Module._melee_web_native_menu_phase(),
        error:document.querySelector('#status')?.dataset.runtimeError,
      }));
      if(next.error)throw Error(next.error);
      if(next.phase!==9)break;
    }
  }
  await driver.waitForPhase(1);
}
async function enterMatch(){
  await driver.pressChord(['7']);await driver.waitForPhase(3);
  await page.waitForTimeout(700);
  // Read source cursor geometry, then use ordinary browser keyboard events.
  // The observer does not mutate selections or inject PAD samples.
  for(let attempt=0;attempt<240;attempt++){
    const {ids,geometry:[x,y,tx,ty]}=await page.evaluate(()=>window.menuObserveStage(32));
    if(ids[1]===32)break;
    const keys=[];
    if(Math.abs(tx-x)>1.2)keys.push(tx>x?'4':'2');
    if(Math.abs(ty-y)>1.2)keys.push(ty>y?']':'3');
    if(!keys.length)keys.push(tx>=x?'4':'2');
    await driver.pressChord(keys,{holdMs:20,releaseMs:20});
  }
  assert.equal((await page.evaluate(()=>window.menuObserveStage(32))).ids[1],32);
  await driver.pressChord(['m']);await driver.waitForPhase(7);
  await page.waitForFunction(()=>window.menuObservePlayer(1)?.frame>=180,null,{timeout:60000});
}
try {
  await page.goto(values.url);await driver.waitForImport();
  // Import replacement requires an idle renderer. Observe its existing ready
  // signal before starting import; never retry a timed-out preparation.
  await page.waitForFunction(()=>Module._melee_web_native_menu_cache_idle()===1,null,{timeout:60000});
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.getByLabel('Player 1 input source',{exact:true}).selectOption('keyboard');
  await page.getByLabel('Player 2 input source',{exact:true}).selectOption('off');
  await page.locator('#controls-close').click();
  await check('owned disc -> original CSS',async()=>{
    await driver.selectDisc(values.disc);await driver.waitForStart();await driver.launch();
  });
  const marker='versus-return-'+Date.now();
  await page.evaluate(marker=>{window.versusReturnDocument=marker;},marker);
  for(let match=1;match<=3;match++){
    await page.waitForTimeout(1200);await memory(`css-before-match-${match}`);
    await check(`match ${match}: original CSS -> SSS -> playable match`,enterMatch);
    await check(`match ${match}: source pause -> LRAS -> Results -> Start -> stable CSS`,async()=>{
      await driver.pressChord(['7']);await waitSourcePause();await page.waitForTimeout(700);
      // First use the reported short chord; subsequent runs hold it across
      // the ownership change to catch fabricated button edges on CSS entry.
      await driver.pressChord(['q','9','m','7'],{holdMs:match===1?120:1000,releaseMs:150});
      await driver.waitForPhase(8);await page.waitForTimeout(4500);
      await memory(`results-match-${match}`);
      if(match===1){
        // Explicit host pause for a stable rendering artifact, outside timing evidence.
        // Wait for each pause/resume to publish its state before the next click:
        // a second click that lands during the first operation is rejected, which
        // would leave the scene paused for the rest of the route.
        await page.locator('#pause').click();
        await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('Paused'),null,{timeout:15000});
        await page.locator('#canvas').screenshot({path:path.join(values.out,'results.png')});
        await page.locator('#pause').click();
        await page.waitForFunction(()=>!document.querySelector('#status').textContent.startsWith('Paused'),null,{timeout:15000});
      }
      await confirmResults(match);await page.waitForTimeout(1200);await driver.waitForPhase(1);
      assert.equal(await page.evaluate(()=>window.versusReturnDocument),marker);
      assert.match(await page.evaluate(()=>Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())),
        new RegExp(`Completed matches: ${match} ·`));
    });
    await memory(`css-after-match-${match}`);
  }
  await check('same source backing arena across all three matches',async()=>{
    const first=report.memory[0];
    assert.equal(first.source_session_owned,true);
    assert.equal(first.source_allocation_bytes,32*1024*1024);
    assert.ok(first.source_allocation_identity>0&&first.source_allocation_generation>0);
    for(const entry of report.memory){
      assert.equal(entry.source_session_owned,true);
      for(const field of ['source_allocation_identity','source_allocation_generation','source_allocation_bytes'])
        assert.equal(entry[field],first[field],`${entry.boundary}: ${field}`);
    }
  });
  report.completed=true;
} catch(error){
  report.failure={message:error.message,stack:error.stack,diagnostics:error.diagnostics};
  process.exitCode=1;console.error(error.message);
} finally {
  report.diagnostics=await driver.diagnostics();
  report.source=await page.evaluate(()=>({
    native:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),
    input:JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())),
  })).catch(error=>({unavailable:error.message}));
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify(report,null,2)+'\n');
  // Capture only after the active route; screenshots can stall live callbacks.
  await page.screenshot({path:path.join(values.out,'boundary.png'),fullPage:true}).catch(()=>{});
  driver.dispose();await browser.close();
}
