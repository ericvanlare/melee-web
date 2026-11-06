#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
/* One-page ordinary-runtime admission experiment for the private retained
 * history owner. The page uses the real runtime, local disc loader and source
 * keyboard path. Only the two private diagnostic ESM modules are supplied
 * from this checkout; no source input, network or replay service is mocked. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from './browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from './browser_tools.mjs';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const WASM_REL='gameplay_menu_browser.wasm';
const PROFILE='slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20';
const CARD_BYTES=0x1790+7*0x1f2c;
const options=Object.fromEntries(['url','disc','out','playwright','runtime-wasm-sha256']
  .map(name=>[name,{type:'string'}]));
const {values}=parseArgs({options,strict:true});
for(const name of ['url','disc','out','runtime-wasm-sha256'])
  if(!values[name])throw Error(`--${name} is required`);
assert.match(values['runtime-wasm-sha256'],/^[0-9a-f]{64}$/,
  'The ordinary runtime Wasm requires its exact reviewed SHA-256');
const baseUrl=new URL(values.url);
assert.equal(baseUrl.protocol,'http:','The runtime must be served over loopback HTTP');
assert(['127.0.0.1','localhost','[::1]'].includes(baseUrl.hostname),
  'The runtime server must be local');
const discPath=path.resolve(values.disc),out=path.resolve(values.out);
await fs.access(discPath);
try{await fs.access(out);throw Error(`Refusing to overwrite evidence directory ${out}`);}
catch(error){if(error.code!=='ENOENT')throw error;}
await fs.mkdir(out,{recursive:true});

const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const hashFile=async file=>{
  const digest=createHash('sha256');
  for await(const chunk of createReadStream(file))digest.update(chunk);
  return digest.digest('hex');
};
const controllerPath=path.join(ROOT,'src/slippi_profile/runtime_match_history_controller.mjs');
const endpointPath=path.join(ROOT,'src/slippi_profile/tests/cpu_endpoint.mjs');
const profilePath=path.join(ROOT,'src/slippi_profile/profile.h');
const controllerBytes=await fs.readFile(controllerPath),endpointBytes=await fs.readFile(endpointPath);
const profileSource=await fs.readFile(profilePath,'utf8');
assert(profileSource.includes(`"${PROFILE}"`),'Source profile header no longer pins V20');
const discImageHash=await hashFile(discPath);
const wasmPath=path.join(ROOT,'build/browser-match-history-release',WASM_REL);
const actualWasmHash=await hashFile(wasmPath);
assert.equal(actualWasmHash,values['runtime-wasm-sha256'],
  'Built ordinary runtime differs from the reviewed Wasm identity');

const report={schema:'melee-web-runtime-match-history-admission-v1',
  scope:'one headless ordinary-runtime page; original CSS/SSS keyboard route; native agreed start and held frame-zero terminal takeover only; no capture, network session, replay or rollback command',
  result:'RUNNING',started_at:new Date().toISOString(),
  runtime:{url:baseUrl.href,wasm_sha256:actualWasmHash,profile_identity:PROFILE},
  disc:{image_sha256:discImageHash,validated_identity:null},
  diagnostic_module_bindings:[
    {url:new URL('/__melee_diag__/runtime_match_history_controller.mjs',baseUrl).href,
      checkout_path:path.relative(ROOT,controllerPath),sha256:sha256(controllerBytes)},
    {url:new URL('/__melee_diag__/tests/cpu_endpoint.mjs',baseUrl).href,
      checkout_path:path.relative(ROOT,endpointPath),sha256:sha256(endpointBytes)},
  ],
  route:{controls:null,css_before:null,css_after:null,css_start:null,
    sss_readiness:null,stage_input:null,sss_confirm:null,stage:null,match:null,
    agreed_start:null,takeover:null,identity:null,cleanup:null},
  checks:[],browser_errors:[],websockets:[],screenshots:[],
  cleanup:{native_owner:'not_started',browser:'not_started',server:'externally_owned_by_capture_owner_deadline'},
};
const pageErrors=[],consoleMessages=[];
let droppedConsoleMessages=0;
let browser,context,page,driver,controllerInstalled=false,takenOver=false;
let nativeCleanupSucceeded=false,browserCdp=null;
let resultError=null;
let hostAgreement=null,profileSnapshot=null,discIdentity=null;

function addCheck(name,details=null){report.checks.push({name,details});}
async function retainNativeLog(boundary){
  if(!page)return;
  try{
    // The page keeps a 16,000-character log window. Retain the whole window,
    // especially after native unload, where sticky owner failures are reported.
    const text=await page.evaluate(()=>document.querySelector('#log')?.textContent??'');
    const file=`native-log-${boundary}.txt`;
    await fs.writeFile(path.join(out,file),text);
    (report.native_logs??=[]).push({boundary,file,bytes:Buffer.byteLength(text),
      sha256:sha256(Buffer.from(text)),scope:'current native/UI log window; not a complete session log'});
  }catch(error){
    (report.native_log_errors??=[]).push({boundary,error:String(error?.message||error)});
  }
}
function noteFailure(stage,error){
  const message=`${stage}: ${String(error?.message||error)}`;
  (report.cleanup_failures??=[]).push(message);
  // Keep the original first boundary cause when cleanup also has problems.
  if(report.result!=='FAIL'){
    report.result='FAIL';
    report.failure={name:error?.name||'CleanupError',message,stack:error?.stack||null};
    resultError=message;
  }
}
async function observePageHealth(label,{requireRunning=false}={}){
  const state=await page.evaluate(()=>{
    const m=globalThis.Module;
    const text=()=>{
      try{return typeof m?._melee_web_native_menu_message==='function'
        ?m.UTF8ToString(m._melee_web_native_menu_message()):null;}catch{return null;}
    };
    const timing=text();
    const error=document.querySelector('#status')?.dataset.runtimeError||
      document.querySelector('#error-dialog[open]')&&
        (document.querySelector('#error')?.textContent||'Application error');
    const running=typeof m?._melee_web_native_menu_running==='function'
      ?m._melee_web_native_menu_running():null;
    const history=globalThis.__meleeRuntimeMatchHistory?.status()??null;
    const historyError=history&&(history.firstFailure||history.metadata.firstFailure||history.rendererErrors)
      ?m.UTF8ToString(m._melee_web_native_menu_match_history_error()):null;
    return {timing_message:timing,runtime_error:error||null,running,history,history_error:historyError,
      phase:typeof m?._melee_web_native_menu_phase==='function'?m._melee_web_native_menu_phase():null,
      replayActive:!!globalThis.meleeNetRuntimeLockstepSnapshot?.(),
      diagnosticStatus:document.querySelector('#status')?.textContent?.slice(0,600)||null};
  });
  if(state.runtime_error)throw Error(`${label}: ${state.runtime_error}`);
  if(state.history_error)throw Error(`${label}: ${state.history_error}`);
  if(/^Paused after a timing disruption/i.test(state.timing_message||''))
    throw Error(`${label}: timing pause observed; automatic recovery is forbidden`);
  if(requireRunning&&state.running!==1)throw Error(`${label}: source runtime stopped (${state.timing_message||'no native message'})`);
  if(state.replayActive)throw Error(`${label}: a network/replay runtime owner is active`);
  if(pageErrors.length)throw Error(`${label}: browser error ${pageErrors[0]}`);
  return state;
}

const door=(setup,port)=>({kind:setup.doors[port*10],slotType:setup.doors[port*10+4],
  character:setup.doors[port*10+3],slot:setup.doors[port*10+6]});

async function waitNativePhase(phase,label,{timeoutMs=30000,deadline=Date.now()+timeoutMs}={}){
  let last=null;
  while(Date.now()<deadline){
    last=await observePageHealth(label);
    const current=await page.evaluate(()=>({
      phase:Module._melee_web_native_menu_phase(),
      running:Module._melee_web_native_menu_running()}));
    // CSS/SSS scene changes may legitimately pause the regular loop while
    // original source assets are prepared. Continue only to the requested
    // phase with active callbacks; observePageHealth still rejects errors and
    // timing faults on every poll, with no recovery path.
    if(current.phase===phase&&current.running===1)return {...last,...current};
    await page.waitForTimeout(20);
  }
  throw Error(`${label}: native phase ${phase} not reached: ${JSON.stringify(last)}`);
}

async function waitInitialCss(deadline){
  // The source observer reports an unpublished dirty slider matrix as not
  // ready. Phase 1 alone can precede the first draw that publishes it. Share
  // the entry deadline; never advance source state or recover a runtime fault.
  const readiness={null_observations:0,last_health:null};
  report.route.css_readiness=readiness;
  while(Date.now()<deadline){
    readiness.last_health=await observePageHealth('original CSS publication',{requireRunning:true});
    const setup=await page.evaluate(()=>window.menuObserveCssSetup?.()??null);
    if(setup!==null){
      assert(setup.cursors?.length===16&&setup.doors?.length===40&&setup.geometry?.length===48,
        'Original CSS source observer returned malformed extents');
      return setup;
    }
    readiness.null_observations++;
    await page.waitForTimeout(20);
  }
  throw Error(`Original CSS source observation was not published within entry deadline: ${JSON.stringify(readiness)}`);
}

async function observeMenuSourceFrame(){
  return page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>{
    const module=globalThis.Module;
    const phase=module?._melee_web_native_menu_phase?.()??null;
    const running=module?._melee_web_native_menu_running?.()??null;
    const messagePointer=module?._melee_web_native_menu_message?.();
    const diagnosticsPointer=module?._melee_web_native_menu_diagnostics?.();
    const state={at_ms:performance.now(),phase,running,
      message:messagePointer?module.UTF8ToString(messagePointer):null,
      diagnostics:diagnosticsPointer?module.UTF8ToString(diagnosticsPointer):null,
      runtime_error:document.querySelector('#status')?.dataset.runtimeError||null,
      hidden:document.hidden,focused:document.hasFocus(),css:null,sss:null,input:null,
      observer_error:null};
    if(phase===1){
      if(typeof module?._melee_web_css_observe_port!=='function'){
        state.observer_error='CSS source observer export is absent';
      }else{
        const ids=module._malloc(14*4),geometry=module._malloc(8*4);
        if(!ids||!geometry){
          if(ids)module._free(ids);if(geometry)module._free(geometry);
          state.observer_error='CSS source observer allocation failed';
        }else{
          try{
            const observed=module._melee_web_css_observe_port(0,8,ids,geometry)===1;
            state.css={observed,ids:observed?
              Array.from(module.HEAP32.subarray(ids>>2,(ids>>2)+14)):null,
              geometry:observed?
                Array.from(module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8)):null};
          }catch(error){state.observer_error=`CSS source observation failed: ${String(error?.message||error)}`;}
          finally{module._free(ids);module._free(geometry);}
        }
      }
    }
    if(phase===3){
      const sss={stage:null,gate:null};
      try{
        sss.stage=typeof window.menuObserveStage==='function'?window.menuObserveStage(32):null;
      }catch(error){sss.stage_error=String(error?.message||error);}
      if(typeof module?._melee_web_sss_observe_input_gate!=='function'){
        sss.gate={available:false,observed:false,cooldown:null};
      }else{
        const cooldown=module._malloc(4);
        if(!cooldown){
          sss.gate={available:true,observed:false,cooldown:null,error:'SSS cooldown observation allocation failed'};
        }else{
          try{
            const observed=module._melee_web_sss_observe_input_gate(32,cooldown)===1;
            sss.gate={available:true,observed,
              cooldown:observed?module.HEAP32[cooldown>>2]:null};
          }catch(error){
            sss.gate={available:true,observed:false,cooldown:null,
              error:String(error?.message||error)};
          }finally{module._free(cooldown);}
        }
      }
      state.sss=sss;
    }
    try{
      if(typeof module?._melee_web_input_message==='function')
        state.input=JSON.parse(module.UTF8ToString(module._melee_web_input_message()));
    }catch(error){state.observer_error||=`Input state observation failed: ${String(error?.message||error)}`;}
    resolve(state);
  })));
}

function assertMenuSourceHealth(state,label){
  if(state.runtime_error)throw Error(`${label}: ${state.runtime_error}`);
  if(/^Paused after a timing disruption/i.test(state.message||''))
    throw Error(`${label}: timing pause observed; automatic recovery is forbidden`);
  if(state.observer_error)throw Error(`${label}: ${state.observer_error}`);
  if(pageErrors.length)throw Error(`${label}: browser error ${pageErrors[0]}`);
  return state;
}

function cssStartReady(state){
  const ids=state.css?.ids;
  return state.phase===1&&state.running===1&&state.css?.observed===true&&
    ids?.length===14&&ids[5]===0&&ids[7]===0&&ids[8]!==0;
}

function sssInputReady(state){
  const gate=state.sss?.gate,stage=state.sss?.stage;
  return state.phase===3&&state.running===1&&stage?.ids?.length===4&&
    stage.geometry?.length===6&&gate?.available===true&&gate.observed===true&&
    Number.isInteger(gate.cooldown)&&gate.cooldown===0;
}

function menuReadinessSample(state){
  const ids=state.css?.ids,gate=state.sss?.gate,stage=state.sss?.stage;
  return {at_ms:state.at_ms,phase:state.phase,running:state.running,
    css_observed:state.css?.observed??null,css_cooldown:ids?.[5]??null,
    css_pending_scene:ids?.[7]??null,css_start_ready:ids?.[8]??null,
    css_last_start_trigger:ids?.[11]??null,css_last_start_ready:ids?.[12]??null,
    css_last_start_pending:ids?.[13]??null,
    sss_observed:stage!==null&&stage!==undefined,sss_selected_stage:stage?.ids?.[1]??null,
    sss_gate_available:gate?.available??null,sss_gate_observed:gate?.observed??null,
    sss_cooldown:gate?.cooldown??null};
}

async function waitForSourceReady({deadline,predicate,label,readiness}){
  let last=null;
  while(Date.now()<deadline){
    last=await observeMenuSourceFrame();
    readiness.last_state=last;
    try{assertMenuSourceHealth(last,label);}
    catch(error){readiness.outcome='observer-error';readiness.failure_state=last;throw error;}
    readiness.samples.push(menuReadinessSample(last));
    if(Date.now()>=deadline){
      readiness.outcome='deadline';readiness.failure_state=last;
      throw Error(`${label}: live source readiness was not observed before the shared 30-second route deadline: ${JSON.stringify(readiness.samples.at(-1)||null)}`);
    }
    if(predicate(last)){
      readiness.outcome='ready';
      readiness.ready_state=last;
      return last;
    }
    if(last.phase===3&&last.sss?.gate?.available===false){
      readiness.outcome='observer-unavailable';
      readiness.failure_state=last;
      throw Error(`${label}: private SSS cooldown observer export is unavailable`);
    }
  }
  readiness.outcome='deadline';
  readiness.failure_state=last;
  throw Error(`${label}: live source readiness did not become true before the shared 30-second route deadline: ${JSON.stringify(readiness.samples.at(-1)||null)}`);
}

async function configureControls(){
  await page.locator('#controls-open').click();
  await page.locator('#controls-dialog[open]').waitFor();
  await page.locator('#keyboard-layout').selectOption('two');
  await page.locator('#player-one-source').selectOption('keyboard');
  await page.locator('#player-two-source').selectOption('keyboard');
  await page.waitForFunction(()=>document.querySelector('#keyboard-layout')?.value==='two'&&
    document.querySelector('#player-one-source')?.value==='keyboard'&&
    document.querySelector('#player-two-source')?.value==='keyboard'&&
    document.querySelector('#player-one-source-status')?.textContent?.trim()==='Keyboard'&&
    document.querySelector('#player-two-source-status')?.textContent?.trim()==='Keyboard');
  report.route.controls=await page.evaluate(()=>({layout:document.querySelector('#keyboard-layout').value,
    playerOne:document.querySelector('#player-one-source').value,
    playerTwo:document.querySelector('#player-two-source').value,
    statusOne:document.querySelector('#player-one-source-status').textContent.trim(),
    statusTwo:document.querySelector('#player-two-source-status').textContent.trim()}));
  assert.deepEqual(report.route.controls,{layout:'two',playerOne:'keyboard',playerTwo:'keyboard',
    statusOne:'Keyboard',statusTwo:'Keyboard'});
  await page.locator('#controls-close').click();
  await page.locator('#controls-dialog').waitFor({state:'hidden'});
  addCheck('real controls select two-player keyboard for both ports',report.route.controls);
}

function verifyOriginalHumans(setup){
  // The ordinary two-player menu initializer selects two Human Mario doors.
  // Read that actual source configuration instead of toggling a presumed CPU.
  report.route.css_before=[0,1,2,3].map(port=>door(setup,port));
  assert.deepEqual(report.route.css_before.map(row=>[row.kind,row.slotType]),
    [[0,0],[0,0],[3,3],[3,3]],'Original CSS did not present two Human doors');
  assert.deepEqual(report.route.css_before.slice(0,2).map(row=>row.character),[8,8],
    'Original CSS did not present Mario on both active doors');
  report.route.css_after=report.route.css_before;
  addCheck('original CSS presents two Human Mario doors',report.route.css_after);
}

async function chooseFinalDestination(){
  // CSS Start and the SSS's own source-authored input gate share one 30-second
  // wall bound. Read source cooldowns directly; do not substitute elapsed time.
  const stageDeadline=Date.now()+30000;
  const cssStartRoute=report.route.css_start={
    readiness:{deadline_ms:30000,samples:[],outcome:'waiting'},
    before_key:null,after_key:null,after_key_error:null};
  const cssReady=await waitForSourceReady({deadline:stageDeadline,
    predicate:cssStartReady,label:'original CSS Start source readiness',
    readiness:cssStartRoute.readiness});
  cssStartRoute.before_key=cssReady;
  let cssStartError=null;
  try{await driver.pressChord(['Enter'],{holdMs:80,releaseMs:100});}
  catch(error){cssStartError=error;}
  try{
    cssStartRoute.after_key=await observeMenuSourceFrame();
    assertMenuSourceHealth(cssStartRoute.after_key,'original CSS Start after-key source state');
  }catch(error){cssStartRoute.after_key_error=String(error?.message||error);}
  if(cssStartError)throw cssStartError;
  if(cssStartRoute.after_key_error)
    throw Error(`Could not retain original CSS Start after-key source state: ${cssStartRoute.after_key_error}`);
  addCheck('original CSS Start was sent by the keyboard driver after source cooldown/readiness',
    menuReadinessSample(cssStartRoute.before_key));

  const sssReadinessRoute=report.route.sss_readiness={
    deadline_ms:30000,deadline_at:new Date(stageDeadline).toISOString(),
    samples:[],outcome:'waiting',ready_state:null,failure_state:null};
  let sourceState=await waitForSourceReady({deadline:stageDeadline,
    predicate:sssInputReady,label:'original SSS source input readiness',
    readiness:sssReadinessRoute});
  const stageInputRoute=report.route.stage_input={
    before_first_movement:null,movements:[],after_last_movement:null,
    complete:false,failure_state:null};
  sourceState=await observeMenuSourceFrame();
  stageInputRoute.before_first_movement=sourceState;
  try{assertMenuSourceHealth(sourceState,'original SSS immediately before first cursor movement');}
  catch(error){stageInputRoute.failure_state=sourceState;throw error;}
  if(!sssInputReady(sourceState)){
    stageInputRoute.failure_state=sourceState;
    throw Error(`Original SSS source cooldown was not zero immediately before cursor movement: ${JSON.stringify(menuReadinessSample(sourceState))}`);
  }
  addCheck('original SSS cursor stage was source-ready before cursor input',
    menuReadinessSample(sourceState));
  let stage=sourceState.sss.stage;
  for(let attempt=0;attempt<240&&stage?.ids?.[1]!==32;attempt++){
    if(Date.now()>=stageDeadline){
      stageInputRoute.failure_state=sourceState;
      throw Error(`Original SSS cursor selection exceeded the shared 30-second route deadline: ${JSON.stringify(menuReadinessSample(sourceState))}`);
    }
    assert(sssInputReady(sourceState),
      `Original SSS source cooldown/readiness was unavailable before cursor movement ${attempt+1}: ${JSON.stringify(menuReadinessSample(sourceState))}`);
    const [x,y,tx,ty]=stage.geometry,keys=[];
    if(Math.abs(tx-x)>1.2)keys.push(tx>x?'d':'a');
    if(Math.abs(ty-y)>1.2)keys.push(ty>y?'w':'s');
    if(!keys.length)keys.push(tx>=x?'d':'a');
    await driver.pressChord(keys,{holdMs:24,releaseMs:24});
    const afterMovement=await observeMenuSourceFrame();
    stageInputRoute.movements.push({keys,after:menuReadinessSample(afterMovement)});
    stageInputRoute.after_last_movement=afterMovement;
    try{assertMenuSourceHealth(afterMovement,`SSS source cursor movement ${attempt+1}`);}
    catch(error){stageInputRoute.failure_state=afterMovement;throw error;}
    sourceState=afterMovement;
    if(!sssInputReady(sourceState)){
      stageInputRoute.failure_state=sourceState;
      throw Error(`Original SSS source cooldown/readiness was unavailable after cursor movement ${attempt+1}: ${JSON.stringify(menuReadinessSample(sourceState))}`);
    }
    stage=sourceState.sss.stage;
  }
  if(stage?.ids?.[1]!==32){
    stageInputRoute.failure_state=sourceState;
    throw Error(`Original SSS did not highlight Final Destination: ${JSON.stringify(menuReadinessSample(sourceState))}`);
  }
  stageInputRoute.complete=true;
  report.route.stage=stage;
  const sssConfirmRoute=report.route.sss_confirm={before_key:null,after_key:null,after_key_error:null};
  if(Date.now()>=stageDeadline){
    sssConfirmRoute.failure_state=sourceState;
    throw Error(`Original SSS confirmation exceeded the shared 30-second route deadline: ${JSON.stringify(menuReadinessSample(sourceState))}`);
  }
  const confirmState=await observeMenuSourceFrame();
  sssConfirmRoute.before_key=confirmState;
  try{assertMenuSourceHealth(confirmState,'original SSS immediately before Final Destination confirmation');}
  catch(error){sssConfirmRoute.failure_state=confirmState;throw error;}
  if(!sssInputReady(confirmState)||confirmState.sss.stage.ids[1]!==32){
    sssConfirmRoute.failure_state=confirmState;
    throw Error(`Original SSS confirmation requires source cooldown zero, live readiness, and Final Destination selected: ${JSON.stringify(menuReadinessSample(confirmState))}`);
  }
  let sssConfirmError=null;
  try{await driver.pressChord(['j'],{holdMs:80,releaseMs:100});}
  catch(error){sssConfirmError=error;}
  try{
    sssConfirmRoute.after_key=await observeMenuSourceFrame();
    assertMenuSourceHealth(sssConfirmRoute.after_key,'original SSS confirmation after-key source state');
  }catch(error){sssConfirmRoute.after_key_error=String(error?.message||error);}
  if(sssConfirmError)throw sssConfirmError;
  if(sssConfirmRoute.after_key_error)
    throw Error(`Could not retain original SSS confirmation after-key source state: ${sssConfirmRoute.after_key_error}`);
  addCheck('SSS cursor movement and Final Destination confirmation were gated by the live source cooldown observer',
    menuReadinessSample(sssConfirmRoute.before_key));
  await agreeOriginalMatchStart();
}

async function agreeOriginalMatchStart(){
  const route=report.route.agreed_start;
  assert(route?.enabled===1,'Original-menu agreed-start owner was not enabled');
  const deadline=Date.now()+90000;
  let state=null;
  while(Date.now()<deadline){
    state=await observePageHealth('waiting for source-selected match offer');
    const startState=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startState());
    route.last_state=startState;
    if(startState===2){
      route.agreement=await page.evaluate(({host})=>{
        const controller=globalThis.__meleeRuntimeMatchHistory;
        const offerJSON=controller.startOffer(host),offer=JSON.parse(offerJSON);
        if(offer.schema!=='melee-web-runtime-match-start-offer-v1')throw Error('Unexpected native offer schema');
        const seedText=offer.selection?.random_seed;
        if(typeof seedText!=='string'||!/^(?:0x)?[0-9a-fA-F]{8}$/.test(seedText))
          throw Error('Native offer omitted its exact source selection seed');
        const seed=Number.parseInt(seedText.replace(/^0x/,''),16);
        // This one-page admission uses its own actual source-selected seed.
        // A paired session must exchange and compare these native agreement
        // bytes before either peer commits; this is not network evidence.
        const agreementJSON=controller.startAgreement(seed,host);
        const committedState=controller.startCommit(seed,agreementJSON,host);
        return {offer_json:offerJSON,agreement_json:agreementJSON,seed,
          seed_provenance:'actual one-page source-selected match seed',committed_state:committedState};
      },{host:hostAgreement});
      break;
    }
    await page.waitForTimeout(25);
  }
  assert.equal(route.agreement?.committed_state,3,
    `Original source selection did not reach an agreed start: ${JSON.stringify({state,start:route.last_state})}`);
  while(Date.now()<deadline){
    state=await observePageHealth('waiting for agreed frame-zero match publication');
    const startState=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startState());
    route.last_state=startState;
    if(startState===5){
      const held=await page.evaluate(()=>{
        const controller=globalThis.__meleeRuntimeMatchHistory;
        const publication=controller.startPublication();
        return {publication:{...publication,raw:Array.from(publication.raw)},
          live_identity:controller.startLiveIdentity(),
          match:JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_observe())),
          running:Module._melee_web_native_menu_running()};
      });
      route.held=held;report.route.match=held.match;
      assert.equal(held.running,0,'Agreed match must remain held before terminal takeover');
      assert.equal(held.match.frame,0,'Agreed match advanced before terminal takeover');
      break;
    }
    await page.waitForTimeout(25);
  }
  // ready is the original post-intro HUD enable flag. The native held-state
  // receipt proves construction/publication before any source tick; retain
  // that HUD flag as telemetry rather than waiting through Entry/Ready/Go.
  assert(route.held&&report.route.match?.frame===0,
    `Agreed original match did not publish frame zero: ${JSON.stringify({state,start:route.last_state})}`);
  addCheck('native agreement constructs and publishes the supported match once at held frame zero',{
    frame:report.route.match.frame,stage:report.route.match.rules.stage,
    sourceCharacters:report.route.match.rules.player_source_characters,
    stocks:report.route.match.rules.player_stocks});
}

async function hashProfileSnapshot(){
  return page.evaluate(()=>{
    const m=globalThis.Module,length=0x1790+7*0x1f2c;
    if(typeof m?._melee_web_native_menu_snapshot_save_profile!=='function')
      throw Error('Real source save-profile snapshot export is unavailable');
    const ptr=m._malloc(length);
    if(!ptr)throw Error('Unable to allocate save-profile identity buffer');
    try{
      if(m._melee_web_native_menu_snapshot_save_profile(ptr,length,0)!==1)
        throw Error(`Source save-profile export refused: ${m.UTF8ToString(m._melee_web_native_menu_message())}`);
      const copy=m.HEAPU8.slice(ptr,ptr+length);
      return crypto.subtle.digest('SHA-256',copy).then(value=>({bytes:length,
        sha256:Array.from(new Uint8Array(value),byte=>byte.toString(16).padStart(2,'0')).join('')}));
    }finally{m._free(ptr);}
  });
}

async function admitTerminalTakeover(){
  assert(hostAgreement&&profileSnapshot&&discIdentity,'Pre-match source identities were not collected');
  assert.equal(report.websockets.length,0,'A WebSocket session was opened before terminal admission');
  assert.equal(report.route.lockstep_snapshot,null,'A lockstep/replay owner was active before admission');
  if(pageErrors.length)throw Error(`Browser error before terminal admission: ${pageErrors[0]}`);
  const host=hostAgreement,disc=discIdentity,profile=profileSnapshot;
  report.disc.validated_identity={algorithm:disc.algorithm,dol:disc.dol,fst:disc.fst,
    dol_size:disc.dolSize,fst_size:disc.fstSize,
    canonical_identity_sha256:host.asset_identity_sha256};
  report.save_profile={bytes:profile.bytes,sha256:profile.sha256,
    provenance:'actual active source-host card snapshot through _melee_web_native_menu_snapshot_save_profile'};
  report.host_identity=host;

  await page.screenshot({path:path.join(out,'ordinary-match-before-takeover.png'),fullPage:false});
  report.screenshots.push('ordinary-match-before-takeover.png');
  const transfer=await page.evaluate(async ({host})=>{
    const module=globalThis.Module,controller=globalThis.__meleeRuntimeMatchHistory;
    if(!controller)throw Error('Private history controller module was not installed');
    // Only readiness observations repeat. The native takeover itself is tried
    // once, immediately after its source-frame observation in the same task.
    const idleDeadline=performance.now()+5000;
    let status=controller.status();
    while(status.gpuPending!==0){
      const message=module.UTF8ToString(module._melee_web_native_menu_message());
      if(status.firstFailure||status.metadata.firstFailure||status.rendererErrors||
        /^Paused after a timing disruption/i.test(message))
        throw Error(`Fault before takeover: ${message} (${JSON.stringify(status)})`);
      if(performance.now()>=idleDeadline)
        throw Error(`GPU/source admission did not become idle: ${JSON.stringify(status)}`);
      await new Promise(resolve=>setTimeout(resolve,5));
      status=controller.status();
    }
    const asyncifyState=module.Asyncify&&Number.isInteger(module.Asyncify.state)
      ?module.Asyncify.state:null;
    if(status.firstFailure||status.metadata.firstFailure||status.rendererErrors)
      throw Error(`History/status reports a preexisting failure: ${JSON.stringify(status)}`);
    if(document.querySelector('#status')?.dataset.runtimeError)
      throw Error(document.querySelector('#status').dataset.runtimeError);
    if(window.meleeNetRuntimeLockstepSnapshot?.()!=null)
      throw Error('A network/replay runtime owner became active before terminal takeover');
    const message=module.UTF8ToString(module._melee_web_native_menu_message());
    if(/^Paused after a timing disruption/i.test(message)||module._melee_web_native_menu_running()!==0||controller.startState()!==5)
      throw Error(`Source was not held cleanly at agreed frame zero: ${message}`);
    if(asyncifyState!==null&&asyncifyState!==0)
      throw Error(`Asyncify is not idle at admission (state ${asyncifyState})`);
    const match=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
    if(match.paused||match.ending||match.complete||match.rules?.stage!==32||
      match.rules?.match_kind!==1||match.rules?.is_stock!==1||match.rules?.is_vs!==1||
      match.rules?.is_teams!==0||JSON.stringify(match.rules?.player_stocks)!=='[4,4]'||
      JSON.stringify(match.rules?.player_source_characters)!=='[8,8]'||
      JSON.stringify(match.observed_player_source_slots)!=='[0,1]'||
      JSON.stringify(match.rules?.resolved_controller_ports)!=='[0,1]'||
      !Array.isArray(match.players)||match.players.length!==2||match.players.some(player=>!player.human||player.fighter!==0))
      throw Error(`Original source match did not satisfy exact takeover identity: ${JSON.stringify(match)}`);
    // This is deliberately one synchronous browser task: the fresh source
    // observation immediately authenticates the one terminal owner handoff.
    const sourceFrame=match.frame;
    if(sourceFrame!==0)throw Error('Agreed source frame advanced before takeover');
    const takeover=controller.takeOver({sourceFrame,localPort:0,remotePort:1});
    return {sourceFrame,match,statusBefore:status,statusAfter:takeover,asyncifyState,
      nativeMessage:module.UTF8ToString(module._melee_web_native_menu_message()),
      callbackTiming:window.meleeNetRuntimeCallbackTimingSnapshot?.()??null,
      pageStatus:document.querySelector('#status')?.textContent?.slice(0,600)||null};
  },{host});
  takenOver=true;
  report.route.takeover=transfer;
  assert.equal(transfer.statusAfter.ownerState,1);
  assert.equal(transfer.statusAfter.expectedFrame,transfer.sourceFrame);
  assert.equal(transfer.statusAfter.gpuPending,0);
  addCheck('same-turn read-only match/source-frame admission and terminal takeover',{
    source_frame:transfer.sourceFrame,gpu_pending:transfer.statusAfter.gpuPending,
    asyncify_state:transfer.asyncifyState,first_failure:transfer.statusAfter.firstFailure,
    renderer_errors:transfer.statusAfter.rendererErrors});

  const sourceIdentity=await page.evaluate(({host})=>{
    const identity=globalThis.__meleeRuntimeMatchHistory.identity(host);
    const status=globalThis.__meleeRuntimeMatchHistory.status();
    return {identity,status};
  },{host});
  const parsed=JSON.parse(sourceIdentity.identity);
  assert.equal(sourceIdentity.identity,report.route.agreed_start.held.live_identity,
    'Terminal takeover changed the native live identity of the held match');
  assert.equal(parsed.source_frame,transfer.sourceFrame);
  assert.equal(parsed.runtime_sha256,host.runtime_sha256);
  assert.equal(parsed.asset_identity_sha256,host.asset_identity_sha256);
  assert.equal(parsed.save_profile_sha256,host.save_profile_sha256);
  report.route.identity={source_json:sourceIdentity.identity,status:sourceIdentity.status};
  addCheck('source-serialized identity binds the actual runtime, disc, save and frame',{
    source_frame:parsed.source_frame,asset_identity_sha256:parsed.asset_identity_sha256,
    save_profile_sha256:parsed.save_profile_sha256});

  await page.screenshot({path:path.join(out,'terminal-owner-admitted.png'),fullPage:false});
  report.screenshots.push('terminal-owner-admitted.png');
  const clean=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.command('cleanup'));
  report.route.cleanup=clean;
  nativeCleanupSucceeded=clean.ownerState===2&&clean.gpuPending===0;
  assert(nativeCleanupSucceeded,'Native terminal cleanup did not close the ordinary source owners');
  addCheck('native terminal command retires match and renderer owners',{
    owner_state:clean.ownerState,gpu_pending:clean.gpuPending});
  report.cleanup.native_owner='retired';
}

try{
  const {chromium,browser:browserConfig,browserPath,playwrightPath}=await loadBrowserTools(values.playwright);
  browser=await chromium.launch(browserLaunchOptions(browserConfig,{headed:false,timeout:60000}));
  report.browser={version:browser.version(),executable:path.basename(browserPath),playwright:playwrightPath,
    mode:'headless',speaker_output:'muted; Web Audio/source processing left enabled'};
  context=await browser.newContext({viewport:{width:1280,height:960},deviceScaleFactor:1});
  page=await context.newPage();
  page.setDefaultTimeout(90000);page.setDefaultNavigationTimeout(90000);
  page.on('pageerror',error=>pageErrors.push(error.stack||error.message));
  page.on('console',message=>{
    const type=message.type(),text=message.text();
    if(type==='error')pageErrors.push(text);
    if(consoleMessages.length<10000)consoleMessages.push({type,text});
    else droppedConsoleMessages++;
  });
  page.on('websocket',socket=>report.websockets.push(socket.url()));
  page.on('request',request=>{
    const url=new URL(request.url());
    if(!['127.0.0.1','localhost','[::1]'].includes(url.hostname)&&
      url.protocol!=='data:'&&url.protocol!=='blob:')
      report.external_requests??=[],report.external_requests.push({url:url.origin,resource:request.resourceType()});
  });
  const diagBase=new URL('/__melee_diag__/',baseUrl).href;
  await page.route('**/__melee_diag__/runtime_match_history_controller.mjs',route=>route.fulfill({
    status:200,contentType:'text/javascript; charset=utf-8',body:controllerBytes}));
  await page.route('**/__melee_diag__/tests/cpu_endpoint.mjs',route=>route.fulfill({
    status:200,contentType:'text/javascript; charset=utf-8',body:endpointBytes}));
  driver=createBrowserDriver(page,{surface:'development',timeoutMs:90000});
  const response=await page.goto(baseUrl.href,{waitUntil:'domcontentloaded'});
  assert.equal(response.status(),200);
  await driver.waitForImport();
  await page.waitForFunction(()=>globalThis.Module&&typeof Module._melee_web_native_menu_match_history_take_over==='function'&&
    typeof Module._melee_web_native_menu_match_observe==='function',null,{timeout:90000});
  await page.evaluate(async url=>{
    const {createRuntimeMatchHistoryController}=await import(url);
    globalThis.__meleeRuntimeMatchHistory=createRuntimeMatchHistoryController(globalThis.Module);
  },new URL('runtime_match_history_controller.mjs',diagBase).href);
  controllerInstalled=true;
  const preRouteOwners=await page.evaluate(()=>({lockstep:window.meleeNetRuntimeLockstepSnapshot?.()??null,
    replay:!!document.querySelector('#retail-replay-file')?.files?.length}));
  assert.deepEqual(preRouteOwners,{lockstep:null,replay:false});
  report.route.lockstep_snapshot=preRouteOwners.lockstep;
  report.route.replay_file_selected=preRouteOwners.replay;
  await configureControls();
  await driver.selectDisc(discPath);
  await driver.waitForStart();
  // waitForStart is the loader's prepared-state gate. cache_idle instead
  // requires every source owner to be unloaded and cannot admit a loaded disc.
  await observePageHealth('prepared local disc before source profile export');
  profileSnapshot=await hashProfileSnapshot();
  assert.equal(profileSnapshot.bytes,CARD_BYTES);
  const loadedIdentity=await page.evaluate(()=>window.meleeNetPeerIdentity());
  assert.equal(loadedIdentity.algorithm,'sha256');
  assert.equal(loadedIdentity.wasm,actualWasmHash);
  discIdentity=loadedIdentity.disc;
  assert(discIdentity&&discIdentity.algorithm==='sha256'&&/^[0-9a-f]{64}$/.test(discIdentity.dol)&&
    /^[0-9a-f]{64}$/.test(discIdentity.fst)&&Number.isSafeInteger(discIdentity.dolSize)&&
    Number.isSafeInteger(discIdentity.fstSize),
    'Normal runtime loader returned an invalid validated disc identity');
  const assetIdentity=JSON.stringify({algorithm:discIdentity.algorithm,dol:discIdentity.dol,
    fst:discIdentity.fst,dolSize:discIdentity.dolSize,fstSize:discIdentity.fstSize});
  hostAgreement={runtime_sha256:loadedIdentity.wasm,
    asset_identity_sha256:sha256(Buffer.from(assetIdentity)),
    save_profile_sha256:profileSnapshot.sha256,profile_identity:PROFILE};
  report.save_profile={bytes:profileSnapshot.bytes,sha256:profileSnapshot.sha256,
    provenance:'actual active source-host card snapshot through _melee_web_native_menu_snapshot_save_profile'};
  report.disc.validated_identity={algorithm:discIdentity.algorithm,dol:discIdentity.dol,
    fst:discIdentity.fst,dol_size:discIdentity.dolSize,fst_size:discIdentity.fstSize,
    canonical_identity_sha256:hostAgreement.asset_identity_sha256};
  report.host_identity=hostAgreement;
  const cssEntryDeadline=Date.now()+90000;
  await page.locator('#launch').click();
  await waitNativePhase(1,'original CSS entry',{deadline:cssEntryDeadline});
  verifyOriginalHumans(await waitInitialCss(cssEntryDeadline));
  report.route.agreed_start={enabled:await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.startEnable()),
    last_state:1,agreement:null,held:null};
  await chooseFinalDestination();
  await admitTerminalTakeover();
  report.result='PASS';
}catch(error){
  resultError=error;
  report.result='FAIL';
  report.failure={name:error?.name||'Error',message:String(error?.message||error),stack:error?.stack||null};
}finally{
  await retainNativeLog('before-cleanup');
  if(resultError&&page){
    try{report.failure_diagnostics=await driver?.diagnostics?.()??{unavailable:'browser driver unavailable'};}
    catch(error){report.failure_diagnostics={unavailable:String(error?.message||error)};}
    try{
      const failureShot='failure-boundary.png';
      await page.screenshot({path:path.join(out,failureShot),fullPage:false,timeout:4000});
      report.screenshots.push(failureShot);
    }catch(error){report.failure_screenshot_error=String(error?.message||error);}
  }
  if(!nativeCleanupSucceeded&&page){
    try{
      const owner=await page.evaluate(()=>{
        const controller=globalThis.__meleeRuntimeMatchHistory;
        const module=globalThis.Module;
        let status=null;
        try{status=controller?.status?.()??null;}catch{}
        return {state:status?.ownerState??null,gpuPending:status?.gpuPending??null,
          runtimePresent:!!module&&typeof module._melee_web_native_menu_phase==='function'&&
            typeof module._melee_web_native_menu_running==='function',
          running:module?._melee_web_native_menu_running?.()??null};
      });
      if((controllerInstalled||takenOver)&&(takenOver||owner.state===1)){
        const clean=await page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.command('cleanup'));
        report.route.cleanup=clean;nativeCleanupSucceeded=clean.ownerState===2&&clean.gpuPending===0;
        if(!nativeCleanupSucceeded)noteFailure('native terminal cleanup refused',JSON.stringify(clean));
      }else if(owner.runtimePresent&&driver){
        // An ordinary runtime may own prepared source state even while its
        // callback loop is stopped. Use its real unload path without resuming.
        await driver.unload();report.cleanup.native_owner='ordinary-unload-before-takeover';
      }
      if(nativeCleanupSucceeded)report.cleanup.native_owner='retired-after-failure-path';
    }catch(error){report.cleanup.native_owner=`failed: ${String(error?.message||error)}`;
      noteFailure('native/source cleanup failed',error);}
  }
  await retainNativeLog('after-cleanup');
  if(browser){
    try{
      browserCdp=await browser.newBrowserCDPSession();
      const processInfo=await browserCdp.send('SystemInfo.getProcessInfo');
      assert(Array.isArray(processInfo?.processInfo)&&processInfo.processInfo.length>0,
        'Chrome CDP returned no browser process inventory');
      assert(processInfo.processInfo.every(row=>row&&Number.isInteger(row.id)&&row.id>0&&
        typeof row.type==='string'&&row.type.length>0),'Chrome CDP process inventory is malformed');
      report.browser_process_info=processInfo.processInfo;
      report.cleanup.browser_cdp='process inventory captured';
    }catch(error){report.browser_process_info=[];
      report.cleanup.browser_cdp=`failed: ${String(error?.message||error)}`;
      noteFailure('Chrome CDP process attribution failed',error);}
    finally{
      if(browserCdp){
        try{await browserCdp.detach();report.cleanup.browser_cdp+='; detached';}
        catch(error){report.cleanup.browser_cdp+='; detach failed';
          noteFailure('Chrome CDP detach failed',error);}
        browserCdp=null;
      }
    }
  }
  if(context){
    try{await context.close();report.cleanup.browser='context-closed';}
    catch(error){report.cleanup.browser=`context-close-failed: ${String(error?.message||error)}`;
      noteFailure('browser context cleanup failed',error);}
  }
  if(browser){
    try{await browser.close();report.cleanup.browser+='; browser-closed';}
    catch(error){report.cleanup.browser+='; browser-close-failed';
      noteFailure('browser process cleanup failed',error);}
  }
  report.browser_errors=pageErrors;
  await fs.writeFile(path.join(out,'console.json'),JSON.stringify({messages:consoleMessages,
    dropped_messages:droppedConsoleMessages},null,2)+'\n');
  report.console_log={file:'console.json',messages:consoleMessages.length,
    dropped_messages:droppedConsoleMessages};
  report.finished_at=new Date().toISOString();
  report.resultError=resultError?String(resultError.message||resultError):null;
  await fs.writeFile(path.join(out,'result.json'),JSON.stringify(report,null,2)+'\n');
}
console.log(JSON.stringify({result:report.result,checks:report.checks.map(row=>row.name),
  failure:report.resultError,report:path.join(out,'result.json')}));
if(report.result!=='PASS')process.exitCode=1;
