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
import {createRuntimeMatchAdmission} from './runtime_match_history_admission.mjs';

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
  scope:'one headless ordinary-runtime page; original CSS/SSS keyboard route; native agreed start, recorded ordinary-local Entry/Ready/Go input and draw prefix, then held gameplay-frame-zero terminal takeover; no capture, network session, replay or rollback command',
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
let browser,context,page,driver,admission,controllerInstalled=false,takenOver=false;
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
  report.last_health={label,...state};
  if(state.history_error)throw Error(`${label}: ${state.history_error}`);
  if(state.runtime_error)throw Error(`${label}: ${state.runtime_error}`);
  if(/^Paused after a timing disruption/i.test(state.timing_message||''))
    throw Error(`${label}: timing pause observed; automatic recovery is forbidden`);
  if(requireRunning&&state.running!==1)throw Error(`${label}: source runtime stopped (${state.timing_message||'no native message'})`);
  if(state.replayActive)throw Error(`${label}: a network/replay runtime owner is active`);
  if(pageErrors.length)throw Error(`${label}: browser error ${pageErrors[0]}`);
  return state;
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
    if(!match.ready||match.paused||match.ending||match.complete||match.rules?.stage!==32||
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
    const {createRuntimeMatchHistoryController,validateRuntimeMatchStartPrefix}=await import(url);
    globalThis.__meleeRuntimeMatchHistory=createRuntimeMatchHistoryController(globalThis.Module);
    globalThis.__meleeValidateRuntimeMatchStartPrefix=validateRuntimeMatchStartPrefix;
  },new URL('runtime_match_history_controller.mjs',diagBase).href);
  controllerInstalled=true;
  const preRouteOwners=await page.evaluate(()=>({lockstep:window.meleeNetRuntimeLockstepSnapshot?.()??null,
    replay:!!document.querySelector('#retail-replay-file')?.files?.length}));
  assert.deepEqual(preRouteOwners,{lockstep:null,replay:false});
  report.route.lockstep_snapshot=preRouteOwners.lockstep;
  report.route.replay_file_selected=preRouteOwners.replay;
  admission=createRuntimeMatchAdmission({page,driver,report,host:()=>hostAgreement,pageErrors:()=>pageErrors});
  await admission.configureControls();
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
  const offer=await admission.prepareOffer();
  await admission.agreeAndCommit({seed:offer.seed,
    seedProvenance:'actual one-page source-selected match seed'});
  await admission.waitHeld();
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
