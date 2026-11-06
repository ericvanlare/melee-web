#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
/* Two independently owned headless pages share the original ordinary route.
 * The held-statics phase retains exact configured rows before capture; the
 * peer-correction phase performs one asymmetric actual-input correction. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {createRoomId} from './net_lockstep_websocket_relay.mjs';
import {parseNetChecksum} from './net_lockstep_core.mjs';
import {startRoomRelayRuntime} from './net_room_relay_runtime_owner.mjs';
import {createBrowserDriver} from './browser_driver.mjs';
import {browserLaunchOptions,loadBrowserTools} from './browser_tools.mjs';
import {createRuntimeMatchAdmission} from './runtime_match_history_admission.mjs';
import {validateCpuRestoreConfiguration} from '../src/slippi_profile/tests/cpu_restore_runner.mjs';
import {compareCpuReferenceParticleEndpoints,CPU_ENDPOINT_PROVENANCE,CPU_ENDPOINT_VIEW,
  decodeCpuParticleEndpointResult} from '../src/slippi_profile/tests/cpu_endpoint.mjs';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const PROFILE='slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20';
// SDL two-keyboard layout: KeyD and ArrowLeft map to signed main-stick X.
const P1_RIGHT=Buffer.from([0,0,127,0,0,0,0,0,0,0,0]);
const P2_LEFT=Buffer.from([0,0,129,0,0,0,0,0,0,0,0]);
const CARD_BYTES=0x1790+7*0x1f2c;
const optionNames=['url','disc','out','playwright','runtime-wasm-sha256','linked-config','manifest','map','phase'];
const {values}=parseArgs({options:Object.fromEntries(optionNames.map(name=>[name,{type:'string'}])),strict:true});
for(const name of optionNames)if(!values[name])throw Error(`--${name} is required`);
assert(['held-statics','peer-correction'].includes(values.phase),
  'Supported bounded phases are held-statics and peer-correction');
assert.match(values['runtime-wasm-sha256'],/^[0-9a-f]{64}$/,'Exact reviewed runtime Wasm SHA-256 is required');
const baseUrl=new URL(values.url);
assert.equal(baseUrl.protocol,'http:','The runtime must be served over loopback HTTP');
assert(['127.0.0.1','localhost','[::1]'].includes(baseUrl.hostname),'The runtime server must be local');
if(values.phase==='peer-correction')
  assert(['http://127.0.0.1:8787','http://localhost:8787'].includes(baseUrl.origin),
    'Peer correction needs a loopback runtime origin allowed by the pinned Worker on port 8787');
const discPath=path.resolve(values.disc),out=path.resolve(values.out);
for(const file of [discPath,values['linked-config'],values.manifest,values.map])await fs.access(path.resolve(file));
try{await fs.access(out);throw Error(`Refusing to overwrite evidence directory ${out}`);}
catch(error){if(error.code!=='ENOENT')throw error;}
await fs.mkdir(out,{recursive:true});

const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const hashFile=async file=>{
  const digest=createHash('sha256');for await(const chunk of createReadStream(file))digest.update(chunk);return digest.digest('hex');
};
const configPath=path.resolve(values['linked-config']),manifestPath=path.resolve(values.manifest),mapPath=path.resolve(values.map);
const config=JSON.parse(await fs.readFile(configPath,'utf8'));
assert.equal(config.schema,'aggregate-cpu-final-linked-configuration-v1','Reviewed linked config schema drift');
assert.equal(typeof config.policyFile,'string','Reviewed linked config omitted its adjacent policy file');
const policyPath=path.resolve(path.dirname(configPath),config.policyFile);
const policy=JSON.parse(await fs.readFile(policyPath,'utf8'));
const manifest=JSON.parse(await fs.readFile(manifestPath,'utf8'));
const [actualWasmHash,mapSha256,manifestSha256,policySha256,configSha256,discImageHash]=await Promise.all([
  hashFile(path.join(ROOT,'build/browser-match-history-release/gameplay_menu_browser.wasm')),
  hashFile(mapPath),hashFile(manifestPath),hashFile(policyPath),hashFile(configPath),hashFile(discPath),
]);
assert.equal(actualWasmHash,values['runtime-wasm-sha256'],'Built ordinary runtime differs from its reviewed Wasm identity');
const identities={wasmSha256:actualWasmHash,mapSha256,manifestSha256,policySha256};
assert.equal(manifest.configuration_identity?.link_map_sha256,mapSha256,'Exact source-state manifest does not bind the supplied link map');
const wire=validateCpuRestoreConfiguration(config,policy,manifest,identities);

const identityKey=row=>JSON.stringify([row.producer_target,row.source,row.input_object,row.input_section]);
const allManifestRows=[...(manifest.mutable_input_sections||[]),...(manifest.excluded_mutable_input_sections||[])];
const manifestByKey=new Map(allManifestRows.map(row=>[identityKey(row),row]));
assert.equal(manifestByKey.size,allManifestRows.length,'Manifest repeats an exact source row identity');
const configByKey=new Map(config.rows.map(row=>[identityKey(row),row]));
const policyByKey=new Map(policy.rows.map(row=>[identityKey(row),row]));
assert.equal(configByKey.size,config.rows.length,'Linked config repeats an exact source row identity');
assert.equal(policyByKey.size,policy.rows.length,'Reviewed policy repeats an exact source row identity');
const wirePolicy=new Map([
  ['ORIGINAL_SOURCE_CPU_VALUE_BUNDLE_RESTORE',[1,1]],
  ['INCLUDED_ORIGINAL_MPLIB_CPU_VALUE_BUNDLE_RESTORE',[1,2]],
  ['AUTHORED_COLLISION_TABLE_EQUALITY_GUARD',[2,2]],
  ['SOURCE_SELECTED_HEAP_EQUALITY_GUARD_NO_REWIND',[2,1]],
  ['PATCHED_SCOPE_OR_BASELINE_EQUALITY_GUARD_NO_REWIND',[2,1]],
  ['PROJECT_COLLISION_OWNER_IDENTITY_EQUALITY_GUARD',[2,2]],
  ['TYPED_CLOCK_WRITER_EXCLUDE_FROM_BYTE_BUNDLE',null],
  ['TYPED_CADENCE_WRITER_EXCLUDE_FROM_BYTE_BUNDLE',null],
]);
const joined=[];
for(const reviewed of policy.rows){
  const key=identityKey(reviewed),current=manifestByKey.get(key),configured=configByKey.get(key);
  assert(current&&configured,`Exact linked row join failed for ${key}`);
  const mapping=wirePolicy.get(reviewed.aggregate_policy);
  assert(mapping!==undefined,`Unknown reviewed policy ${reviewed.aggregate_policy}`);
  assert.equal(configured.aggregate_policy,reviewed.aggregate_policy,`Linked policy changed for ${key}`);
  if(mapping===null)continue; // Typed clock/cadence rows are intentionally outside the byte bundle.
  assert.equal(current.address,configured.address,`Linked row address changed for ${key}`);
  assert.equal(current.size,configured.size,`Linked row extent changed for ${key}`);
  joined.push({wire:[current.address,current.size,...mapping],identity:{producer_target:current.producer_target,
    source:current.source,input_object:current.input_object,input_section:current.input_section,
    address:current.address,size:current.size,policy:reviewed.aggregate_policy,
    wire_policy:mapping[0],domain:mapping[1]}});
}
joined.sort((a,b)=>a.wire[0]-b.wire[0]);
assert.deepEqual(joined.map(row=>row.wire),wire,'Exact identity join differs from the existing CPU config validator wire');
assert.equal(joined.length,wire.length,'Reviewed typed clock/cadence exclusions or wire row count changed');

const controllerPath=path.join(ROOT,'src/slippi_profile/runtime_match_history_controller.mjs');
const endpointPath=path.join(ROOT,'src/slippi_profile/tests/cpu_endpoint.mjs');
const peerProbePath=path.join(ROOT,'src/slippi_profile/tests/runtime_peer_history_probe.mjs');
const peerModulePath=path.join(ROOT,'src/slippi_profile/runtime_match_history_peer.mjs');
const lockstepCorePath=path.join(ROOT,'scripts/net_lockstep_core.mjs');
const websocketRelayPath=path.join(ROOT,'scripts/net_lockstep_websocket_relay.mjs');
const profilePath=path.join(ROOT,'src/slippi_profile/profile.h');
const [controllerBytes,endpointBytes,peerProbeBytes,peerModuleBytes,lockstepCoreBytes,
  websocketRelayBytes,profileSource]=await Promise.all([
  fs.readFile(controllerPath),fs.readFile(endpointPath),fs.readFile(peerProbePath),
  fs.readFile(peerModulePath),fs.readFile(lockstepCorePath),fs.readFile(websocketRelayPath),
  fs.readFile(profilePath,'utf8'),
]);
assert(profileSource.includes(`"${PROFILE}"`),'Source profile header no longer pins V20');
const diagnosticBindings=[
  {url:new URL('/__melee_diag__/src/slippi_profile/runtime_match_history_controller.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,controllerPath),sha256:sha256(controllerBytes)},
  {url:new URL('/__melee_diag__/src/slippi_profile/tests/cpu_endpoint.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,endpointPath),sha256:sha256(endpointBytes)},
  {url:new URL('/__melee_diag__/src/slippi_profile/tests/runtime_peer_history_probe.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,peerProbePath),sha256:sha256(peerProbeBytes)},
  {url:new URL('/__melee_diag__/src/slippi_profile/runtime_match_history_peer.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,peerModulePath),sha256:sha256(peerModuleBytes)},
  {url:new URL('/__melee_diag__/scripts/net_lockstep_core.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,lockstepCorePath),sha256:sha256(lockstepCoreBytes)},
  {url:new URL('/__melee_diag__/scripts/net_lockstep_websocket_relay.mjs',baseUrl).href,
    checkout_path:path.relative(ROOT,websocketRelayPath),sha256:sha256(websocketRelayBytes)},
];
const diagnosticModuleBodies=new Map([
  [diagnosticBindings[0].url,controllerBytes],[diagnosticBindings[1].url,endpointBytes],
  [diagnosticBindings[2].url,peerProbeBytes],[diagnosticBindings[3].url,peerModuleBytes],
  [diagnosticBindings[4].url,lockstepCoreBytes],[diagnosticBindings[5].url,websocketRelayBytes],
]);
const diagnosticImportResolutions=[];
for(const binding of diagnosticBindings){
  const source=diagnosticModuleBodies.get(binding.url).toString('utf8');
  const specifiers=[...source.matchAll(/\b(?:from\s*|import\s*)['"]([^'"]+)['"]/g)]
    .map(match=>match[1]).filter(specifier=>specifier.startsWith('./')||specifier.startsWith('../'));
  for(const specifier of specifiers){
    const resolved=new URL(specifier,binding.url).href;
    assert(diagnosticModuleBodies.has(resolved),
      `Routed browser module ${binding.checkout_path} imports an unmapped relative module ${specifier} -> ${resolved}`);
    diagnosticImportResolutions.push({from:binding.url,specifier,to:resolved});
  }
}

const report={schema:values.phase==='held-statics'?
    'melee-web-runtime-match-history-paired-held-statics-v1':
    'melee-web-runtime-match-history-paired-peer-correction-v1',
  scope:values.phase==='held-statics'?
    'two independently owned headless browser pages; original CSS/SSS keyboard route; exact raw alpha/beta source card snapshots retained before comparison; same source-selected context and alpha-seeded agreed frame-zero start; terminal owner identity; exact reviewed static rows copied at held frame zero before capture/begin; no forward, present, replay, correction, or rollback claim':
    'two independently owned headless browser pages; exact shared source card and agreed frame-zero start; browser-owned page WebSocket peers; one alpha speculative actual P1 step using beta held-prefix P2 prediction, beta on-time actual P2 after receiving alpha actual P1, then one alpha typed restore/replay/correction; strict alpha Armed vs Restored PRE and all-64-byte CPU semantic checksum agreement against beta; local nine-domain/particle endpoints retained without cross-peer full endpoint equality',
  result:'RUNNING',started_at:new Date().toISOString(),phase:values.phase,
  runtime:{url:baseUrl.href,wasm_sha256:actualWasmHash,profile_identity:PROFILE},
  disc:{image_sha256:discImageHash},
  linked_configuration:{path:configPath,sha256:configSha256,policy_path:policyPath,
    policy_sha256:policySha256,manifest_path:manifestPath,manifest_sha256:manifestSha256,
    map_path:mapPath,map_sha256:mapSha256,wire_rows:wire.length,
    scope:'validated by existing validateCpuRestoreConfiguration; typed clock/cadence policies excluded from static byte bundle'},
  diagnostic_module_bindings:diagnosticBindings,
  diagnostic_relative_import_resolutions:diagnosticImportResolutions,
  coordination:{type:'Node-local diagnostic offer/agreement exchange only',seed_source:'alpha native CSS/SSS offer',
    gameplay_input_or_relay_transport:false,shared_profile_input:null},
  pages:{alpha:{route:{},disc:{},checks:[],cleanup:{native_owner:'not_started',browser:'not_started'}},
    beta:{route:{},disc:{},checks:[],cleanup:{native_owner:'not_started',browser:'not_started'}}},
  startup_comparison:null,static_comparison:null,checks:[],failure:null,
  cleanup_failures:[],finished_at:null};
const addCheck=(name,details=null)=>report.checks.push({name,details});
const owned=[];let resultError=null,relayRuntime=null;
// All launched operations must settle before cleanup can inspect their owners.
async function joinOwned(label,operations){
  let firstFailure=null;
  const settled=await Promise.allSettled(operations.map((operation,index)=>
    Promise.resolve(operation).catch(error=>{
      firstFailure??=error;
      (report.operation_failures??=[]).push({label,index,error:String(error?.message||error)});
      throw error;
    })));
  if(firstFailure)throw firstFailure;
  return settled.map(row=>row.value);
}


async function profileSnapshot(page){
  return page.evaluate(()=>{
    const m=globalThis.Module,length=0x1790+7*0x1f2c;
    if(typeof m?._melee_web_native_menu_snapshot_save_profile!=='function')throw Error('Source save-profile snapshot export is unavailable');
    const ptr=m._malloc(length);if(!ptr)throw Error('Unable to allocate save-profile identity buffer');
    try{
      if(m._melee_web_native_menu_snapshot_save_profile(ptr,length,0)!==1)
        throw Error(`Source save-profile export refused: ${m.UTF8ToString(m._melee_web_native_menu_message())}`);
      const copy=m.HEAPU8.slice(ptr,ptr+length);
      let binary='';
      for(let offset=0;offset<copy.length;offset+=0x8000)
        binary+=String.fromCharCode(...copy.subarray(offset,offset+0x8000));
      return crypto.subtle.digest('SHA-256',copy).then(value=>({bytes:length,
        sha256:Array.from(new Uint8Array(value),byte=>byte.toString(16).padStart(2,'0')).join(''),
        bytes_base64:btoa(binary)}));
    }finally{m._free(ptr);}
  });
}

async function retainSourceCardSnapshot(role,profile){
  const bytes=Buffer.from(profile.bytes_base64,'base64'),file=`${role}-source-card.bin`;
  await fs.writeFile(path.join(out,file),bytes,{flag:'wx'});
  const retained={file,bytes:bytes.length,sha256:sha256(bytes),
    provenance:'raw bytes copied from the actual source-host card snapshot before profile identity comparison'};
  report.pages[role].source_card_snapshot=retained;
  return {bytes,retained};
}

async function stagePersonalProfile(page,profile){
  const staged=await page.evaluate(async ({base64,expectedBytes,expectedSha256})=>{
    const encoded=atob(base64),bytes=new Uint8Array(encoded.length);
    for(let index=0;index<encoded.length;index++)bytes[index]=encoded.charCodeAt(index);
    if(bytes.length!==expectedBytes)
      throw Error(`Shared source profile has ${bytes.length} bytes; expected ${expectedBytes}`);
    const digest=await crypto.subtle.digest('SHA-256',bytes);
    const sha256=Array.from(new Uint8Array(digest),byte=>byte.toString(16).padStart(2,'0')).join('');
    if(sha256!==expectedSha256)throw Error('Shared source profile bytes changed before beta staging');
    // The development driver already waited for import readiness. Recheck
    // the actual controls and native owner after the async digest and in the
    // same task as the setter, before selecting a disc.
    const disc=document.querySelector('#disc'),unload=document.querySelector('#unload');
    if(!(disc instanceof HTMLInputElement)||disc.type!=='file'||disc.disabled||
      !disc.getClientRects().length||disc.files?.length!==0||unload?.disabled!==true)
      throw Error('Beta development runtime must be import-ready with no selected or loaded disc');
    const runtimeError=document.querySelector('#status')?.dataset.runtimeError;
    if(runtimeError)throw Error(runtimeError);
    const module=globalThis.Module;
    if(typeof module?._melee_web_native_menu_set_save_profile!=='function'||
      typeof module._melee_web_native_menu_phase!=='function'||
      typeof module._melee_web_native_menu_running!=='function')
      throw Error('Existing native Personal-profile owner checks are unavailable');
    const phase=module._melee_web_native_menu_phase(),running=module._melee_web_native_menu_running();
    if(phase!==0||running!==0)
      throw Error(`Beta source owner already exists before Personal-profile staging (phase ${phase}, running ${running})`);
    const ptr=module._malloc(bytes.length);
    if(!ptr)throw Error('Unable to allocate exact shared source profile transfer memory');
    try{
      module.HEAPU8.set(bytes,ptr);
      if(module._melee_web_native_menu_set_save_profile(1,ptr,bytes.length)!==1)
        throw Error(`Existing native Personal-profile setter refused beta input: ${module.UTF8ToString(module._melee_web_native_menu_message())}`);
    }finally{module._free(ptr);}
    return {mode:'personal',bytes:bytes.length,sha256,development_import_ready:true,
      native_phase_before:phase,native_running_before:running,
      provenance:'exact alpha source-host card bytes passed to the existing native Personal-profile setter'};
  },{base64:profile.bytes_base64,expectedBytes:CARD_BYTES,expectedSha256:profile.sha256});
  assert.equal(staged.sha256,profile.sha256,'Beta staged a different source profile than alpha exported');
  return staged;
}

function compareProfileBytes(expected,actual){
  const left=Buffer.from(expected.bytes_base64,'base64'),right=Buffer.from(actual.bytes_base64,'base64');
  assert.equal(left.length,CARD_BYTES,'Alpha source profile byte extent changed');
  assert.equal(right.length,CARD_BYTES,'Beta source profile byte extent changed');
  let firstMismatch=null,differentByteCount=0;
  for(let offset=0;offset<CARD_BYTES;offset++)if(left[offset]!==right[offset]){
    firstMismatch??={offset,alpha:left[offset],beta:right[offset]};differentByteCount++;
  }
  return {equal:differentByteCount===0,alpha_sha256:expected.sha256,beta_sha256:actual.sha256,
    bytes_compared:CARD_BYTES,different_byte_count:differentByteCount,first_mismatch:firstMismatch};
}

async function startOwnedPage(role,chromium,browserConfig,browserPath,playwrightPath,onCreated,sharedProfile=null){
  const item={role,browser:null,context:null,page:null,driver:null,admission:null,
    pageErrors:[],consoleMessages:[],websockets:[],externalRequests:[],controllerInstalled:false,
    takenOver:false,nativeCleanupSucceeded:false,host:null,offer:null,profile:null,profileInput:null,disc:null};
  const pageReport=report.pages[role];
  onCreated?.(item);
  try{
    item.browser=await chromium.launch(browserLaunchOptions(browserConfig,{headed:false,timeout:60000}));
    pageReport.browser={version:item.browser.version(),executable:path.basename(browserPath),
      playwright:playwrightPath,mode:'headless',speaker_output:'muted; Web Audio/source processing left enabled',
      independently_owned_instance:true};
    item.context=await item.browser.newContext({viewport:{width:1280,height:960},deviceScaleFactor:1});
    item.page=await item.context.newPage();item.page.setDefaultTimeout(90000);item.page.setDefaultNavigationTimeout(90000);
    item.page.on('pageerror',error=>item.pageErrors.push(error.stack||error.message));
    item.page.on('console',message=>{
      const type=message.type(),text=message.text();if(type==='error')item.pageErrors.push(text);
      if(item.consoleMessages.length<10000)item.consoleMessages.push({type,text});
    });
    item.page.on('websocket',socket=>item.websockets.push(socket.url()));
    item.page.on('request',request=>{
      const url=new URL(request.url());
      if(!['127.0.0.1','localhost','[::1]'].includes(url.hostname)&&url.protocol!=='data:'&&url.protocol!=='blob:')
        item.externalRequests.push({url:url.origin,resource:request.resourceType()});
    });
    const diagBase=new URL('/__melee_diag__/',baseUrl).href;
    for(const binding of diagnosticBindings){
      const source=diagnosticModuleBodies.get(binding.url);
      if(!source)throw Error(`Diagnostic module source is unavailable for ${binding.checkout_path}`);
      const pathname=new URL(binding.url).pathname;
      await item.page.route(`**${pathname}`,route=>route.fulfill({
        status:200,contentType:'text/javascript; charset=utf-8',body:source}));
    }
    item.driver=createBrowserDriver(item.page,{surface:'development',timeoutMs:90000});
    const response=await item.page.goto(baseUrl.href,{waitUntil:'domcontentloaded'});assert.equal(response.status(),200);
    await item.driver.waitForImport();
    await item.page.waitForFunction(()=>globalThis.Module&&
      typeof Module._melee_web_native_menu_match_history_take_over==='function'&&
      typeof Module._melee_web_native_menu_match_observe==='function',null,{timeout:90000});
    await item.page.evaluate(async url=>{
      const {createRuntimeMatchHistoryController,validateRuntimeMatchStartPrefix}=await import(url);
      globalThis.__meleeRuntimeMatchHistory=createRuntimeMatchHistoryController(globalThis.Module);
      globalThis.__meleeValidateRuntimeMatchStartPrefix=validateRuntimeMatchStartPrefix;
    },new URL('src/slippi_profile/runtime_match_history_controller.mjs',diagBase).href);
    item.controllerInstalled=true;
    const preRouteOwners=await item.page.evaluate(()=>({lockstep:window.meleeNetRuntimeLockstepSnapshot?.()??null,
      replay:!!document.querySelector('#retail-replay-file')?.files?.length}));
    assert.deepEqual(preRouteOwners,{lockstep:null,replay:false},`${role} had a runtime network/replay owner before menu admission`);
    pageReport.route.lockstep_snapshot=preRouteOwners.lockstep;pageReport.route.replay_file_selected=preRouteOwners.replay;
    item.admission=createRuntimeMatchAdmission({page:item.page,driver:item.driver,report:pageReport,
      host:()=>item.host,pageErrors:()=>item.pageErrors});
    await item.admission.configureControls();
    if(sharedProfile){
      pageReport.save_profile_input={mode:'personal',bytes:CARD_BYTES,
        sha256:sharedProfile.sha256,source_role:'alpha',state:'staging-before-beta-disc-selection'};
      item.profileInput=await stagePersonalProfile(item.page,sharedProfile);
      pageReport.save_profile_input={...pageReport.save_profile_input,...item.profileInput,state:'staged'};
    }
    await item.driver.selectDisc(discPath);await item.driver.waitForStart();
    const profile=await profileSnapshot(item.page);
    const retainedProfile=await retainSourceCardSnapshot(role,profile);
    pageReport.save_profile={bytes:profile.bytes,sha256:profile.sha256,source_snapshot_sha256:profile.sha256,
      retained_file:retainedProfile.retained.file,retained_file_sha256:retainedProfile.retained.sha256,
      provenance:'actual active source-host card snapshot through _melee_web_native_menu_snapshot_save_profile; raw bytes retained before comparison'};
    assert.equal(profile.bytes,CARD_BYTES,'Source card snapshot reported the wrong extent');
    assert.equal(retainedProfile.bytes.length,CARD_BYTES,'Retained source card snapshot has the wrong extent');
    assert.equal(retainedProfile.retained.sha256,profile.sha256,
      `${role} retained source card bytes differ from the browser-computed source snapshot hash`);
    if(sharedProfile){
      const comparison=compareProfileBytes(sharedProfile,profile);
      pageReport.save_profile.shared_input_comparison=comparison;
      report.coordination.shared_profile_input.beta_source_snapshot_sha256=profile.sha256;
      report.coordination.shared_profile_input.source_hashes_equal=
        sharedProfile.sha256===profile.sha256&&comparison.equal;
      assert(comparison.equal,`${role} source-loaded card bytes differ from alpha's exact Personal input: ${JSON.stringify(comparison.first_mismatch)}`);
    }
    const loadedIdentity=await item.page.evaluate(()=>window.meleeNetPeerIdentity());
    assert.equal(loadedIdentity.algorithm,'sha256');assert.equal(loadedIdentity.wasm,actualWasmHash);
    const disc=loadedIdentity.disc;
    assert(disc&&disc.algorithm==='sha256'&&/^[0-9a-f]{64}$/.test(disc.dol)&&/^[0-9a-f]{64}$/.test(disc.fst)&&
      Number.isSafeInteger(disc.dolSize)&&Number.isSafeInteger(disc.fstSize),`${role} loaded disc identity is invalid`);
    const assetIdentity=JSON.stringify({algorithm:disc.algorithm,dol:disc.dol,fst:disc.fst,
      dolSize:disc.dolSize,fstSize:disc.fstSize});
    item.host={runtime_sha256:loadedIdentity.wasm,asset_identity_sha256:sha256(Buffer.from(assetIdentity)),
      save_profile_sha256:profile.sha256,profile_identity:PROFILE};
    item.profile=profile;item.disc=disc;
    pageReport.host_identity=item.host;
    pageReport.disc.validated_identity={algorithm:disc.algorithm,dol:disc.dol,fst:disc.fst,
      dol_size:disc.dolSize,fst_size:disc.fstSize,canonical_identity_sha256:item.host.asset_identity_sha256};
    pageReport.offer=await item.admission.prepareOffer();
    item.offer=pageReport.offer;
    return item;
  }catch(error){
    pageReport.failure={message:String(error?.message||error),stack:error?.stack||null};
    throw error;
  }
}

function selectionWithoutSeed(offer){
  const selection=structuredClone(offer?.offer?.selection);
  assert(selection&&typeof selection==='object'&&!Array.isArray(selection),'Native offer omitted pointer-free source selection');
  assert.equal(typeof selection.random_seed,'string','Native selection omitted source seed');
  delete selection.random_seed;
  return selection;
}
function compareStartupPrefixes(alpha,beta){
  const left=JSON.parse(alpha.offer.route.held.prefix_json),right=JSON.parse(beta.offer.route.held.prefix_json);
  const compared=['schema','input_provenance','source_ticks','source_draws','final_ready_tick',
    'initial_resident_transfer_drains','final_resident_transfer_drains'];
  const mismatches=[];
  for(const field of compared)if(!Object.is(left[field],right[field]))mismatches.push({path:field,alpha:left[field],beta:right[field]});
  if(!Array.isArray(left.rows)||!Array.isArray(right.rows)||left.rows.length!==right.rows.length)
    mismatches.push({path:'rows.length',alpha:left.rows?.length??null,beta:right.rows?.length??null});
  else for(let index=0;index<left.rows.length;index++){
    // Poll serials and draw submission IDs are page-local counters. The source
    // ticks, all four-port raw PAD bytes, and HUD-ready boundary compare exactly.
    for(const field of ['tick_before','tick_after','source_frame_before','source_frame_after',
      'hud_ready_before','hud_ready_after','pad_hex'])
      if(!Object.is(left.rows[index][field],right.rows[index][field]))
        mismatches.push({path:`rows[${index}].${field}`,alpha:left.rows[index][field],beta:right.rows[index][field]});
  }
  return {equal:mismatches.length===0,compared_fields:compared.concat('rows[].tick_before','rows[].tick_after',
    'rows[].source_frame_before','rows[].source_frame_after','rows[].hud_ready_before','rows[].hud_ready_after','rows[].pad_hex'),
    intentionally_page_local:['rows[].poll_serial','rows[].draw_submission'],
    alpha:{source_ticks:left.source_ticks,source_draws:left.source_draws,final_ready_tick:left.final_ready_tick,
      rows:left.rows.map(({tick_before,tick_after,source_frame_before,source_frame_after,hud_ready_before,hud_ready_after,pad_hex})=>
        ({tick_before,tick_after,source_frame_before,source_frame_after,hud_ready_before,hud_ready_after,pad_hex}))},
    beta:{source_ticks:right.source_ticks,source_draws:right.source_draws,final_ready_tick:right.final_ready_tick,
      rows:right.rows.map(({tick_before,tick_after,source_frame_before,source_frame_after,hud_ready_before,hud_ready_after,pad_hex})=>
        ({tick_before,tick_after,source_frame_before,source_frame_after,hud_ready_before,hud_ready_after,pad_hex}))},
    first_mismatch:mismatches[0]??null,mismatches};
}
async function terminalTakeover(item){
  const page=item.page,host=item.host,role=item.role,route=report.pages[role].route;
  assert.equal(item.pageErrors.length,0,`${role} reported a browser error before takeover: ${item.pageErrors[0]}`);
  const transfer=await page.evaluate(async ({localPort,remotePort})=>{
    const module=globalThis.Module,controller=globalThis.__meleeRuntimeMatchHistory;
    const idleDeadline=performance.now()+5000;
    let status=controller.status();
    while(status.gpuPending!==0){
      if(status.firstFailure||status.metadata.firstFailure||status.rendererErrors)
        throw Error(`Renderer/source failure before takeover: ${module.UTF8ToString(module._melee_web_native_menu_match_history_error())}`);
      if(performance.now()>=idleDeadline)throw Error(`Renderer did not become idle before takeover: ${JSON.stringify(status)}`);
      await new Promise(resolve=>setTimeout(resolve,5));
      status=controller.status();
    }
    if(status.ownerState!==0||status.firstFailure||status.rendererErrors||status.gpuPending!==0)
      throw Error(`Pre-takeover history/renderer status is not clean: ${JSON.stringify(status)}`);
    const asyncifyState=module.Asyncify&&Number.isInteger(module.Asyncify.state)?module.Asyncify.state:null;
    if(asyncifyState!==null&&asyncifyState!==0)throw Error(`Asyncify is not idle before takeover (${asyncifyState})`);
    if(window.meleeNetRuntimeLockstepSnapshot?.()!=null)throw Error('Network/replay owner activated before terminal takeover');
    if(module._melee_web_native_menu_running()!==0||controller.startState()!==5)
      throw Error('Source was not held at agreed frame zero before terminal takeover');
    const match=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
    if(!match.ready||match.paused||match.ending||match.complete||match.frame!==0||match.rules?.stage!==32||
      match.rules?.match_kind!==1||match.rules?.is_stock!==1||match.rules?.is_vs!==1||match.rules?.is_teams!==0||
      JSON.stringify(match.rules?.player_stocks)!=='[4,4]'||JSON.stringify(match.rules?.player_source_characters)!=='[8,8]'||
      JSON.stringify(match.observed_player_source_slots)!=='[0,1]'||JSON.stringify(match.rules?.resolved_controller_ports)!=='[0,1]'||
      !Array.isArray(match.players)||match.players.length!==2||match.players.some(player=>!player.human||player.fighter!==0))
      throw Error(`Exact original source start identity refused: ${JSON.stringify(match)}`);
    const statusAfterRead=controller.status();
    if(JSON.stringify(status)!==JSON.stringify(statusAfterRead))throw Error('History status changed during read-only frame-zero admission');
    const runtimeError=document.querySelector('#status')?.dataset.runtimeError||
      document.querySelector('#error-dialog[open]')&&(document.querySelector('#error')?.textContent||'Application error');
    if(runtimeError)throw Error(`Runtime error before takeover: ${runtimeError}`);
    const nativeMessage=module.UTF8ToString(module._melee_web_native_menu_message());
    if(/^Paused after a timing disruption/i.test(nativeMessage))
      throw Error(`Timing pause before takeover: ${nativeMessage}`);
    const takeover=controller.takeOver({sourceFrame:0,localPort,remotePort});
    return {match,status_before:status,status_after:takeover,asyncify_state:asyncifyState};
  },{localPort:role==='alpha'?0:1,remotePort:role==='alpha'?1:0});
  item.takenOver=true;route.takeover=transfer;
  assert.equal(item.pageErrors.length,0,`${role} reported a browser error during takeover: ${item.pageErrors[0]}`);
  assert.equal(transfer.status_after.ownerState,1);assert.equal(transfer.status_after.expectedFrame,0);
  assert.equal(transfer.status_after.gpuPending,0);
  const identity=await page.evaluate(host=>({identity:globalThis.__meleeRuntimeMatchHistory.identity(host),
    live_identity:globalThis.__meleeRuntimeMatchHistory.startLiveIdentity(),
    prefix_json:globalThis.__meleeRuntimeMatchHistory.startPrefix()}),host);
  transfer.identity=identity.identity;transfer.live_identity=identity.live_identity;transfer.prefix_json=identity.prefix_json;
  assert.equal(identity.identity,route.agreed_start.held.live_identity,
    `${role} terminal identity differs from the held source identity`);
  const parsedIdentity=JSON.parse(identity.identity);
  for(const key of ['runtime_sha256','asset_identity_sha256','save_profile_sha256'])
    assert.equal(parsedIdentity[key],host[key],`${role} identity omitted loaded ${key}`);
  assert.equal(parsedIdentity.source_frame,0);
  route.identity={source_json:identity.identity,status:transfer.status_after};
  return transfer;
}
async function retainNativeLog(item,boundary){
  if(!item.page)return;
  try{
    const text=await item.page.evaluate(()=>document.querySelector('#log')?.textContent??'');
    const file=`${item.role}-native-log-${boundary}.txt`;
    await fs.writeFile(path.join(out,file),text);
    const row={file,bytes:Buffer.byteLength(text),sha256:sha256(Buffer.from(text)),
      scope:'current native/UI log window; not a complete session log'};
    (report.pages[item.role].native_logs??=[]).push({boundary,...row});
  }catch(error){
    (report.pages[item.role].native_log_errors??=[]).push({boundary,error:String(error?.message||error)});
  }
}
async function retainHeldScreenshots(items){
  for(const item of items){
    const file=`${item.role}-held-frame-zero.png`;
    const bytes=await item.page.screenshot({path:path.join(out,file),fullPage:false});
    assert(bytes.length>=8&&Buffer.from(bytes).subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])),
      `${item.role} held-boundary screenshot is not PNG`);
    const diagnostics=await item.driver.diagnostics();
    report.pages[item.role].route.held_boundary_capture={file,bytes:bytes.length,sha256:sha256(bytes),
      browser_driver:diagnostics,provenance:'headless viewport at agreed frame zero before terminal takeover'};
  }
}
async function readHeldStaticRows(item,staticRows){
  const result=await item.page.evaluate(({rows})=>{
    const controller=globalThis.__meleeRuntimeMatchHistory,module=globalThis.Module;
    const statusBefore=controller.status();
    const journalBefore=controller.journal();
    const matchBefore=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
    if(statusBefore.ownerState!==1||statusBefore.expectedFrame!==0||statusBefore.gpuPending!==0||
      statusBefore.endpointActive!==0||statusBefore.pendingCommand!==0||statusBefore.inputReady!==0||
      statusBefore.metadata.phase!==0||statusBefore.metadata.forwardSteps!==0||statusBefore.metadata.replaySteps!==0||
      statusBefore.metadata.restoreCount!==0||module._melee_web_native_menu_running()!==0||
      journalBefore.length!==0||matchBefore.frame!==0||matchBefore.ready!==true)
      throw Error(`Held-static observation requires frame-zero terminal ownership with no capsule/capture/begin: ${JSON.stringify({statusBefore,journalBefore,matchBefore})}`);
    const heap=module.HEAPU8,heapBuffer=heap.buffer,heapLength=heap.byteLength;
    if(!(heap instanceof Uint8Array)||!heapLength||(typeof SharedArrayBuffer!=='undefined'&&heapBuffer instanceof SharedArrayBuffer))
      throw Error('Held-static observation refused missing, empty, or shared Wasm memory');
    const raw=[];
    for(const row of rows){
      if(!Number.isInteger(row.address)||!Number.isInteger(row.size)||row.address<=0||row.size<=0||
        row.address+row.size>heapLength)throw Error(`Reviewed static row extent is outside held Wasm memory: ${JSON.stringify(row.identity)}`);
      raw.push(heap.slice(row.address,row.address+row.size));
    }
    const matchAfter=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
    const statusAfter=controller.status();
    const journalAfter=controller.journal();
    if(module.HEAPU8!==heap||heap.buffer!==heapBuffer||heap.byteLength!==heapLength||
      JSON.stringify(statusBefore)!==JSON.stringify(statusAfter)||JSON.stringify(journalBefore)!==JSON.stringify(journalAfter)||
      JSON.stringify(matchBefore)!==JSON.stringify(matchAfter)||
      module._melee_web_native_menu_running()!==0)
      throw Error('Held-static copy changed Wasm identity, native owner status, or source match frame');
    return {status_before:statusBefore,status_after:statusAfter,journal_before:journalBefore,
      journal_after:journalAfter,match_before:matchBefore,match_after:matchAfter,
      rows:raw.map(bytes=>Array.from(bytes))};
  },{rows:staticRows});
  assert.equal(result.rows.length,joined.length,`${item.role} static rows truncated`);
  const rows=result.rows.map((bytes,index)=>{
    assert.equal(bytes.length,joined[index].identity.size,`${item.role} row byte extent changed`);
    const buffer=Buffer.from(bytes);
    return {...joined[index].identity,bytes:buffer.length,bytes_base64:buffer.toString('base64'),sha256:sha256(buffer)};
  });
  const pageReport=report.pages[item.role];
  pageReport.static_observation={status_before:result.status_before,status_after:result.status_after,
    match_frame:result.match_after.frame,match_ready:result.match_after.ready,
    command_journal_before:result.journal_before,command_journal_after:result.journal_after,rows:rows.length};
  const file=`${item.role}-static-rows.json`,payload={schema:'melee-web-runtime-held-static-rows-v1',
    role:item.role,source_frame:0,configuration_sha256:configSha256,wire_rows:wire.length,
    bytes_provenance:'raw bytes copied from the exact validated configured address/extent while the actual source match was held at frame zero',rows};
  const content=JSON.stringify(payload,null,2)+'\n';await fs.writeFile(path.join(out,file),content);
  pageReport.static_rows={file,sha256:sha256(Buffer.from(content)),rows:rows.length,
    total_bytes:rows.reduce((sum,row)=>sum+row.bytes,0)};
  return payload;
}
function compareStaticRows(alpha,beta){
  assert.equal(alpha.rows.length,beta.rows.length,'Per-page static row count differs');
  const mismatches=[];let totalDifferentBytes=0,first=null;
  for(let index=0;index<alpha.rows.length;index++){
    const a=alpha.rows[index],b=beta.rows[index];
    assert.deepEqual({producer_target:a.producer_target,source:a.source,input_object:a.input_object,
      input_section:a.input_section,address:a.address,size:a.size,policy:a.policy,wire_policy:a.wire_policy,domain:a.domain},
    {producer_target:b.producer_target,source:b.source,input_object:b.input_object,
      input_section:b.input_section,address:b.address,size:b.size,policy:b.policy,wire_policy:b.wire_policy,domain:b.domain},
      `Exact static row identity differs at wire row ${index}`);
    const left=Buffer.from(a.bytes_base64,'base64'),right=Buffer.from(b.bytes_base64,'base64');
    assert.equal(left.length,a.size);assert.equal(right.length,b.size);
    let firstOffset=-1,count=0;
    for(let offset=0;offset<left.length;offset++)if(left[offset]!==right[offset]){
      if(firstOffset<0)firstOffset=offset;count++;totalDifferentBytes++;
    }
    if(firstOffset>=0){
      const mismatch={wire_index:index,row_identity:{producer_target:a.producer_target,source:a.source,
        input_object:a.input_object,input_section:a.input_section,address:a.address,size:a.size,policy:a.policy,
        wire_policy:a.wire_policy,domain:a.domain},first_byte_offset:firstOffset,
        alpha_byte:left[firstOffset],beta_byte:right[firstOffset],different_byte_count:count};
      first??=mismatch;mismatches.push(mismatch);
    }
  }
  return {equal:mismatches.length===0,rows_compared:alpha.rows.length,
    rows_with_differences:mismatches.length,total_different_bytes:totalDifferentBytes,
    first_mismatch:first,all_mismatches:mismatches,
    raw_files:{alpha:report.pages.alpha.static_rows,beta:report.pages.beta.static_rows}};
}
async function captureBrowserProcessInventory(item){
  let cdp=null;
  try{
    cdp=await item.browser.newBrowserCDPSession();
    const response=await cdp.send('SystemInfo.getProcessInfo'),rows=response?.processInfo;
    assert(Array.isArray(rows)&&rows.length>0,'Chrome CDP returned no process inventory');
    assert(rows.every(row=>row&&Number.isInteger(row.id)&&row.id>0&&typeof row.type==='string'&&row.type.length>0),
      'Chrome CDP process inventory is malformed');
    assert.equal(new Set(rows.map(row=>row.id)).size,rows.length,'Chrome CDP inventory repeats a PID within one owned browser');
    report.pages[item.role].browser_process_info=rows;
    report.pages[item.role].cleanup.browser_cdp='process inventory captured';
    report.pages[item.role].browser_system_info=await cdp.send('SystemInfo.getInfo');
  }catch(error){
    report.pages[item.role].browser_process_info=[];
    report.pages[item.role].cleanup.browser_cdp=`failed: ${String(error?.message||error)}`;
    report.cleanup_failures.push(`${item.role} Chrome CDP inventory: ${String(error?.message||error)}`);
  }finally{
    if(cdp)try{await cdp.detach();report.pages[item.role].cleanup.browser_cdp+='; detached';}
    catch(error){report.cleanup_failures.push(`${item.role} CDP detach: ${String(error?.message||error)}`);}
  }
}

function heldPortPad(held,port,label){
  const prefix=JSON.parse(held.prefix_json),row=prefix.rows.at(-1);
  assert(row&&typeof row.pad_hex==='string'&&/^[0-9a-f]{88}$/.test(row.pad_hex),
    `${label} held prefix omitted its literal four-port PAD`);
  const frame=Buffer.from(row.pad_hex,'hex');
  return {pollSerial:row.poll_serial,pad:frame.subarray(port*11,(port+1)*11)};
}
async function peerOperation(item,operation,value=null){
  return item.page.evaluate(async ({operation,value})=>{
    const bridge=globalThis.__meleeRuntimePeerHistoryProbe;
    if(!bridge)throw Error('Page-owned runtime peer-history bridge is unavailable');
    return bridge.call(operation,value);
  },{operation,value});
}
function requireHistoryStatus(status,label,{phase,sourceFrame,restoreCount,forwardSteps,replaySteps,gpuIdle,pollSerial}={}){
  assert(status&&status.ownerState===1&&status.firstFailure===0&&status.pendingCommand===0&&
    status.completeSequence===status.requestSequence&&status.rendererErrors===0&&status.endpointActive===0&&
    status.metadata?.schema===1&&status.metadata.bytes===64&&status.metadata.firstFailure===0&&
    status.metadata.preFrame===0&&status.expectedFrame===0,
    `${label} native history owner/status is unhealthy: ${JSON.stringify(status)}`);
  const expected={phase,currentFrame:sourceFrame,restoreCount,forwardSteps,replaySteps,pollSerial};
  for(const [key,value] of Object.entries(expected))if(value!==undefined)
    assert.equal(status.metadata[key],value,`${label} native metadata ${key} differs`);
  if(gpuIdle!==undefined)assert.equal(status.gpuPending,gpuIdle?0:1,`${label} GPU pending state differs`);
  return status;
}
async function recordHistoryStatus(item,label,expected){
  const status=await peerOperation(item,'status');
  report.pages[item.role].match_history.statuses.push({label,value:status});
  return requireHistoryStatus(status,`${item.role} ${label}`,expected);
}
async function historyCommand(item,name,expected){
  const journal=report.pages[item.role].match_history.commands;
  const entry={name,before:await recordHistoryStatus(item,`before:${name}`)};
  journal.push(entry);
  try{
    entry.after=await peerOperation(item,'command',name);
    return requireHistoryStatus(entry.after,`${item.role} after:${name}`,expected);
  }catch(error){
    entry.error=String(error?.message||error);
    try{entry.failure_status=await peerOperation(item,'status');}
    catch(statusError){entry.failure_status_error=String(statusError);}
    throw error;
  }
}
async function retainHistoryEndpoint(item,label,{view,provenance,sourceFrame,phase}){
  const wire=await peerOperation(item,'endpoint',{view,provenance,
    expectedSourceFrame:String(sourceFrame),particleCellEndpoint:true});
  const file=`${item.role}-${label}-endpoint.json`,content=JSON.stringify(wire,null,2)+'\n';
  await fs.writeFile(path.join(out,file),content,{flag:'wx'});
  const retained={file,bytes:Buffer.byteLength(content),sha256:sha256(Buffer.from(content)),
    view,provenance,source_frame:String(sourceFrame),domains:9,
    provenance_note:'full locally copied native raw nine-domain and particle endpoint envelope'};
  const pageHistory=report.pages[item.role].match_history;
  pageHistory.endpoints[label]=retained;
  const decoded=decodeCpuParticleEndpointResult(wire);
  assert.equal(decoded.view,view,`${item.role} ${label} endpoint view differs`);
  assert.equal(decoded.provenance,provenance,`${item.role} ${label} endpoint provenance differs`);
  assert.equal(decoded.sourceFrame,String(sourceFrame),`${item.role} ${label} endpoint frame differs`);
  await recordHistoryStatus(item,`after-endpoint:${label}`,{
    phase,sourceFrame,gpuIdle:true});
  return decoded;
}
function effectToken(endpoint){
  const row=endpoint.domains[1].records.find(record=>record.kindName==='EffectToken'&&record.index===0);
  assert(row?.fields.length===1&&/^(0|[1-9][0-9]*)$/.test(row.fields[0]),'PRE effect token is missing');
  return row.fields[0];
}
function decodeInput(receipt,item,label,{remotePad,forwardRemotePad=remotePad,status,corrected=false,priorPollSerial}){
  const raw=Buffer.from(receipt?.rawBase64||'','base64');
  const forward=Buffer.from(receipt?.forwardBase64||'','base64');
  const correctedBytes=Buffer.from(receipt?.correctedBase64||'','base64');
  assert.equal(receipt?.schema,1,`${item.role} ${label} input schema differs`);
  assert.equal(receipt?.bytes,168,`${item.role} ${label} input receipt extent differs`);
  assert.equal(receipt?.inputReady,1,`${item.role} ${label} input is not ready`);
  assert.equal(receipt?.localPort,item.role==='alpha'?0:1,`${item.role} ${label} local port differs`);
  assert.equal(receipt?.remotePort,item.role==='alpha'?1:0,`${item.role} ${label} remote port differs`);
  assert.equal(raw.length,44,`${item.role} ${label} raw PAD is not 44 bytes`);
  assert.equal(forward.length,44,`${item.role} ${label} forward PAD is not 44 bytes`);
  assert.equal(correctedBytes.length,44,`${item.role} ${label} corrected PAD is not 44 bytes`);
  assert.match(receipt.pollSerial,/^[1-9][0-9]{0,19}$/,`${item.role} ${label} poll serial is not canonical`);
  if(priorPollSerial!==undefined)assert(BigInt(receipt.pollSerial)>BigInt(priorPollSerial),
    `${item.role} ${label} poll did not advance beyond the actual held prefix`);
  const localPort=item.role==='alpha'?0:1,remotePort=1-localPort;
  const localPad=raw.subarray(localPort*11,(localPort+1)*11);
  assert.equal(receipt.pollSerial,status.metadata.pollSerial,`${item.role} ${label} PAD poll differs from command receipt`);
  assert.deepEqual(localPad,item.role==='alpha'?P1_RIGHT:P2_LEFT,
    `${item.role} ${label} actual keyboard PAD differs from the source mapping`);
  const expectedForward=overwritePort(raw,remotePort,forwardRemotePad);
  assert(forward.equals(expectedForward),`${item.role} ${label} forward changed bytes outside its expected remote PAD`);
  if(corrected){
    assert.equal(receipt.correctedReady,1,`${item.role} ${label} correction is not ready`);
    const expected=overwritePort(raw,remotePort,remotePad);
    assert(correctedBytes.equals(expected),`${item.role} ${label} corrected PAD differs from retained local plus actual peer PAD`);
  }else assert.equal(receipt.correctedReady,0,`${item.role} ${label} corrected PAD appeared before restore`);
  return {raw,forward,corrected:correctedBytes,localPad:Buffer.from(localPad)};
}
async function retainHistoryInput(item,label,expected){
  const receipt=await peerOperation(item,'input');
  const file=`${item.role}-${label}-input.json`,content=JSON.stringify(receipt,null,2)+'\n';
  await fs.writeFile(path.join(out,file),content,{flag:'wx'});
  const retained={file,bytes:Buffer.byteLength(content),sha256:sha256(Buffer.from(content)),
    poll_serial:receipt.pollSerial,local_port:receipt.localPort,remote_port:receipt.remotePort,
    raw_base64:receipt.rawBase64,forward_base64:receipt.forwardBase64,
    corrected_base64:receipt.correctedBase64,corrected_ready:receipt.correctedReady};
  const pageHistory=report.pages[item.role].match_history;
  pageHistory.inputs[label]=retained;
  const decoded=decodeInput(receipt,item,label,expected);
  return {receipt,decoded};
}
async function retainChecksum(item,label){
  const encoded=await peerOperation(item,'checksum'),raw=Buffer.from(encoded,'base64');
  assert.equal(raw.length,64,`${item.role} ${label} native checksum is not exactly64 bytes`);
  const parsed=parseNetChecksum(raw);
  assert.equal(parsed.tick,0,`${item.role} ${label} checksum match-input ordinal differs`);
  assert.equal(parsed.scene,3,`${item.role} ${label} checksum is not the match scene`);
  assert.equal(parsed.frame,1,`${item.role} ${label} checksum source frame differs`);
  const file=`${item.role}-${label}-native-checksum.bin`;
  await fs.writeFile(path.join(out,file),raw,{flag:'wx'});
  const retained={file,bytes:raw.length,sha256:sha256(raw),base64:raw.toString('base64'),parsed,
    provenance:'copied local 64-byte native scene checksum at the checked CPU_END history phase'};
  const pageHistory=report.pages[item.role].match_history;
  pageHistory.checksums[label]=retained;
  return {raw,parsed,retained};
}
function checksumDifference(left,right){
  const result=byteDifference(left.raw,right.raw);
  // Use the existing lockstep channel order and also retain the first raw byte.
  const channels=[
    ['header',0,16],['input',24,32],['master_pad',32,40],
    ['scene_state',16,20],['scene_state',40,48],['total',56,64],
    ['objects',20,24],['objects',48,56],
  ];
  result.first_mismatch_channel=channels.find(([,from,to])=>
    !left.raw.subarray(from,to).equals(right.raw.subarray(from,to)))?.[0]??null;
  return result;
}
function overwritePort(frame,port,pad){
  assert.equal(frame.length,44);assert.equal(pad.length,11);
  const result=Buffer.from(frame);pad.copy(result,port*11);return result;
}
function byteDifference(left,right){
  const length=Math.max(left.length,right.length);let first=null,count=0;
  for(let offset=0;offset<length;offset++)if(left[offset]!==right[offset]){
    first??={offset,left:left[offset]??null,right:right[offset]??null};count++;
  }
  return {equal:count===0,bytes_left:left.length,bytes_right:right.length,different_byte_count:count,first_mismatch:first};
}
async function installPeerHistoryBridge(item,relayUrl,roomId){
  const sourceIdentityJson=report.pages[item.role].route.takeover.identity;
  return item.page.evaluate(async ({moduleUrl,role,relayUrl,roomId,sourceIdentityJson,loadedIdentity})=>{
    const {installRuntimePeerHistoryProbe}=await import(moduleUrl);
    return installRuntimePeerHistoryProbe({role,relayUrl,roomId,sourceIdentityJson,loadedIdentity});
  },{moduleUrl:new URL('src/slippi_profile/tests/runtime_peer_history_probe.mjs',
      new URL('/__melee_diag__/',baseUrl)).href,
    role:item.role,relayUrl,roomId,sourceIdentityJson,loadedIdentity:item.host});
}
async function runPeerCorrection(alpha,beta){
  const alphaHeld=report.pages.alpha.route.agreed_start.held;
  const betaHeld=report.pages.beta.route.agreed_start.held;
  const alphaPrior=heldPortPad(alphaHeld,0,'Alpha');
  const betaPrior=heldPortPad(betaHeld,1,'Beta');
  const predP2=betaPrior.pad;
  for(const item of [alpha,beta]){
    report.pages[item.role].match_history={statuses:[],commands:[],inputs:{},endpoints:{},checksums:{}};
    const initial=await recordHistoryStatus(item,'initial',{phase:0,sourceFrame:0,
      restoreCount:0,forwardSteps:0,replaySteps:0,gpuIdle:true});
    assert.equal(initial.localPort,item.role==='alpha'?0:1);
    assert.equal(initial.remotePort,1-initial.localPort);
    assert.equal(initial.inputReady,0);
  }
  const peerReport=report.peer_correction={
    claim:'one actual asymmetric wrong-prediction correction against one on-time peer CPU_END reference',
    checksum_exchange:'none; both native 64-byte receipts are copied locally and compared by the Node driver',
    initial_prediction:{role:'alpha',source:'beta actual held-prefix final row, port 1',bytes_base64:predP2.toString('base64'),
      source_poll_serial:betaPrior.pollSerial},
    pages:{alpha:{port:0},beta:{port:1}},comparisons:{},checks:[],
    endpoint_comparison_scope:'all raw nine-domain and particle endpoints are retained per page; only alpha Armed PRE vs Restored PRE is strictly compared within one source owner; no cross-peer endpoint equality claim',
  };
  const roomRelay=relayRuntime=await startRoomRelayRuntime({producer:{wasmSha256:actualWasmHash,profile:PROFILE},evidenceDir:out,
    diagnostic:message=>{(peerReport.relay_diagnostics??=[]).push(String(message));}});
  const roomId=createRoomId();
  const relayUrl=`ws://127.0.0.1:${roomRelay.port}`;
  peerReport.relay={base:roomRelay.base,port:roomRelay.port,relay_url:relayUrl,room_id:roomId,
    owner_process:roomRelay.ownerProcess,runtime_identity:roomRelay.runtimeIdentity,hashes:roomRelay.hashes,
    transport:'existing pinned local Worker relay; page peers own the only diagnostic WebSockets'};
  await joinOwned('peer-bridge-install',[
    installPeerHistoryBridge(alpha,relayUrl,roomId).then(value=>{alpha.peerInstalled=true;return value;}),
    installPeerHistoryBridge(beta,relayUrl,roomId).then(value=>{beta.peerInstalled=true;return value;}),
  ]).then(([a,b])=>{peerReport.pages.alpha.bridge=a;peerReport.pages.beta.bridge=b;});
  const [alphaPeer,betaPeer]=await joinOwned('peer-start',[
    peerOperation(alpha,'peerStart'),peerOperation(beta,'peerStart')]);
  peerReport.pages.alpha.peer_start=alphaPeer;peerReport.pages.beta.peer_start=betaPeer;
  assert(alphaPeer.ready&&betaPeer.ready&&alphaPeer.agreement_hash);
  assert.equal(alphaPeer.agreement_hash,betaPeer.agreement_hash);
  for(const item of [alpha,beta]){
    assert.equal(item.websockets.length,1,`${item.role} must own exactly one actual peer WebSocket`);
    assert.equal(item.websockets[0],`${relayUrl}/v1/rooms/${roomId}/socket`,
      `${item.role} WebSocket is not the selected local relay room`);
  }
  peerReport.pages.alpha.websocket=alpha.websockets[0];peerReport.pages.beta.websocket=beta.websockets[0];

  await joinOwned('capture-history',[historyCommand(alpha,'capture',{phase:1,sourceFrame:0,
    restoreCount:0,forwardSteps:0,replaySteps:0}),historyCommand(beta,'capture',{phase:1,sourceFrame:0,
    restoreCount:0,forwardSteps:0,replaySteps:0})]);
  const captured=await joinOwned('capture-pre-endpoints',[
    retainHistoryEndpoint(alpha,'capture-pre',{view:CPU_ENDPOINT_VIEW.pre,
      provenance:CPU_ENDPOINT_PROVENANCE.sharedPre,sourceFrame:0,phase:2}),
    retainHistoryEndpoint(beta,'capture-pre',{view:CPU_ENDPOINT_VIEW.pre,
      provenance:CPU_ENDPOINT_PROVENANCE.sharedPre,sourceFrame:0,phase:2})]);
  await joinOwned('begin-history',[historyCommand(alpha,'begin',{phase:3,sourceFrame:0,
    restoreCount:0,forwardSteps:0,replaySteps:0}),historyCommand(beta,'begin',{phase:3,sourceFrame:0,
    restoreCount:0,forwardSteps:0,replaySteps:0})]);
  const armed=await joinOwned('armed-pre-endpoints',[
    retainHistoryEndpoint(alpha,'armed-pre',{view:CPU_ENDPOINT_VIEW.pre,
      provenance:CPU_ENDPOINT_PROVENANCE.sharedPre,sourceFrame:0,phase:4}),
    retainHistoryEndpoint(beta,'armed-pre',{view:CPU_ENDPOINT_VIEW.pre,
      provenance:CPU_ENDPOINT_PROVENANCE.sharedPre,sourceFrame:0,phase:4})]);
  for(const [index,item] of [alpha,beta].entries()){
    const captureToken=effectToken(captured[index]),armedToken=effectToken(armed[index]);
    peerReport.pages[item.role].effect_token={captured:captureToken,armed:armedToken};
    assert.equal(captureToken,'0',`${item.role} capture PRE must have the pre-arm effect token`);
    assert(BigInt(armedToken)>0n,`${item.role} Armed PRE must have an incarnation effect token`);
  }
  await peerOperation(alpha,'remotePad',[...predP2]);
  peerReport.prediction_staged_before_alpha_forward=true;

  const alphaForwardStatus=await historyCommandWithKey(alpha,'KeyD','forward',{
    phase:5,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0});
  const alphaInput=await retainHistoryInput(alpha,'forward',{remotePad:predP2,status:alphaForwardStatus,
    priorPollSerial:alphaPrior.pollSerial});
  const alphaP1=alphaInput.decoded.localPad;
  const alphaPeerReceipt=await peerOperation(alpha,'peerPublish',{port:0,tick:0,
    pollSerial:alphaInput.receipt.pollSerial,pad:[...alphaP1]});
  peerReport.pages.alpha.local_publish=alphaPeerReceipt;
  peerReport.pages.alpha.predicted_remote={port:1,pad_base64:predP2.toString('base64'),
    provenance:'actual beta held-prefix last raw port-1 sample; used as alpha native prediction'};
  await retainHistoryEndpoint(alpha,'predicted-cpu',{view:CPU_ENDPOINT_VIEW.cpuEnd,
      provenance:CPU_ENDPOINT_PROVENANCE.reference,sourceFrame:1,phase:6});
  const alphaPredChecksum=await retainChecksum(alpha,'predicted-cpu');
  peerReport.pages.alpha.predicted_cpu={endpoint:report.pages.alpha.match_history.endpoints['predicted-cpu'],
    checksum:alphaPredChecksum.retained};
  await historyCommand(alpha,'present',{phase:7,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0});
  await historyCommand(alpha,'idle',{phase:8,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0,gpuIdle:true});
  await retainHistoryEndpoint(alpha,'predicted-draw',{view:CPU_ENDPOINT_VIEW.drawEnd,
    provenance:CPU_ENDPOINT_PROVENANCE.reference,sourceFrame:1,phase:9});
  peerReport.pages.alpha.predicted_draw=report.pages.alpha.match_history.endpoints['predicted-draw'];

  const alphaRemote=await peerOperation(beta,'peerWaitRemote');
  peerReport.pages.beta.alpha_actual_received=alphaRemote;
  const receivedAlpha=Buffer.from(alphaRemote.receipt.pad);
  assert.deepEqual(receivedAlpha,alphaP1,'Beta page peer received a different actual alpha native P1 poll');
  await peerOperation(beta,'remotePad',[...receivedAlpha]);
  const betaForwardStatus=await historyCommandWithKey(beta,'ArrowLeft','forward',{
    phase:5,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0});
  const betaInput=await retainHistoryInput(beta,'forward',{remotePad:receivedAlpha,status:betaForwardStatus,
    priorPollSerial:betaPrior.pollSerial});
  const betaP2=betaInput.decoded.localPad;
  assert(!predP2.equals(betaP2),
    'Actual beta ArrowLeft P2 sample equals alpha prediction; wrong-prediction control is vacuous');
  const betaPeerReceipt=await peerOperation(beta,'peerPublish',{port:1,tick:0,
    pollSerial:betaInput.receipt.pollSerial,pad:[...betaP2]});
  peerReport.pages.beta.local_publish=betaPeerReceipt;
  peerReport.pages.beta.actual_remote={port:0,pad_base64:receivedAlpha.toString('base64'),
    provenance:'alpha actual KeyD native P1 poll received before beta on-time forward'};
  peerReport.differing_prediction={predicted_p2_base64:predP2.toString('base64'),
    actual_p2_base64:betaP2.toString('base64'),pad_comparison:byteDifference(predP2,betaP2),
    beta_poll_serial:betaInput.receipt.pollSerial};
  await retainHistoryEndpoint(beta,'on-time-cpu',{view:CPU_ENDPOINT_VIEW.cpuEnd,
      provenance:CPU_ENDPOINT_PROVENANCE.reference,sourceFrame:1,phase:6});
  const betaOnTimeChecksum=await retainChecksum(beta,'on-time-reference');
  peerReport.pages.beta.on_time_cpu={endpoint:report.pages.beta.match_history.endpoints['on-time-cpu'],
    checksum:betaOnTimeChecksum.retained};
  const wrongPredictionChecksumDifference=checksumDifference(alphaPredChecksum,betaOnTimeChecksum);
  peerReport.comparisons.wrong_prediction_checksum_bytes=wrongPredictionChecksumDifference;
  peerReport.comparisons.wrong_prediction_scene_state={alpha:alphaPredChecksum.parsed.scene_state,
    beta:betaOnTimeChecksum.parsed.scene_state,
    different:alphaPredChecksum.parsed.scene_state!==betaOnTimeChecksum.parsed.scene_state};
  assert.notEqual(alphaPredChecksum.parsed.scene_state,betaOnTimeChecksum.parsed.scene_state,
    'Wrong prediction changed input bytes but not the native scene_state checksum');
  await historyCommand(beta,'present',{phase:7,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0});
  await historyCommand(beta,'idle',{phase:8,sourceFrame:1,restoreCount:0,forwardSteps:1,replaySteps:0,gpuIdle:true});
  await retainHistoryEndpoint(beta,'on-time-draw',{view:CPU_ENDPOINT_VIEW.drawEnd,
    provenance:CPU_ENDPOINT_PROVENANCE.reference,sourceFrame:1,phase:9});
  peerReport.pages.beta.on_time_draw=report.pages.beta.match_history.endpoints['on-time-draw'];
  const betaActualRemote=await peerOperation(alpha,'peerWaitRemote');
  peerReport.pages.alpha.beta_actual_received=betaActualRemote;
  const actualP2=Buffer.from(betaActualRemote.receipt.pad);
  assert.deepEqual(actualP2,betaP2,'Alpha peer received a different actual beta native P2 poll');

  await historyCommand(alpha,'restore',{phase:10,sourceFrame:0,restoreCount:1,forwardSteps:1,replaySteps:0,
    pollSerial:alphaInput.receipt.pollSerial});
  const alphaRestored=await retainHistoryEndpoint(alpha,'restored-pre',{view:CPU_ENDPOINT_VIEW.pre,
    provenance:CPU_ENDPOINT_PROVENANCE.sharedPre,sourceFrame:0,phase:11});
  const restoredPreComparison=compareCpuReferenceParticleEndpoints(armed[0],alphaRestored);
  peerReport.comparisons.alpha_armed_vs_restored_pre=restoredPreComparison;
  assert(restoredPreComparison.equal,'Alpha complete Armed PRE differs from its complete Restored PRE');
  await peerOperation(alpha,'remotePad',[...actualP2]);
  const correctedStatus=await historyCommand(alpha,'correct',{phase:12,sourceFrame:1,
    restoreCount:1,forwardSteps:1,replaySteps:1,pollSerial:alphaInput.receipt.pollSerial});
  const alphaCorrectedInput=await retainHistoryInput(alpha,'corrected',{
    remotePad:actualP2,forwardRemotePad:predP2,corrected:true,status:correctedStatus});
  assert(alphaCorrectedInput.decoded.raw.equals(alphaInput.decoded.raw),
    'Alpha corrected receipt changed the retained raw four-port PAD poll');
  const authoritativePadComparison=byteDifference(alphaCorrectedInput.decoded.corrected,betaInput.decoded.forward);
  peerReport.comparisons.authoritative_44_byte_pad=authoritativePadComparison;
  assert(authoritativePadComparison.equal,
    `Alpha corrected and beta on-time authoritative PAD differ at ${JSON.stringify(authoritativePadComparison.first_mismatch)}`);
  await retainHistoryEndpoint(alpha,'corrected-cpu',{view:CPU_ENDPOINT_VIEW.cpuEnd,
      provenance:CPU_ENDPOINT_PROVENANCE.corrected,sourceFrame:1,phase:13});
  const alphaCorrectedChecksum=await retainChecksum(alpha,'corrected-cpu');
  peerReport.pages.alpha.corrected_cpu={endpoint:report.pages.alpha.match_history.endpoints['corrected-cpu'],
    checksum:alphaCorrectedChecksum.retained};
  const correctedChecksumComparison=checksumDifference(alphaCorrectedChecksum,betaOnTimeChecksum);
  peerReport.comparisons.corrected_vs_beta_on_time_native_checksum_64=correctedChecksumComparison;
  assert(correctedChecksumComparison.equal,
    `Alpha corrected native checksum differs from beta on-time in ${correctedChecksumComparison.first_mismatch_channel} at ${JSON.stringify(correctedChecksumComparison.first_mismatch)}`);
  await historyCommand(alpha,'present',{phase:14,sourceFrame:1,restoreCount:1,forwardSteps:1,replaySteps:1});
  await historyCommand(alpha,'idle',{phase:15,sourceFrame:1,restoreCount:1,forwardSteps:1,replaySteps:1,gpuIdle:true});
  await retainHistoryEndpoint(alpha,'corrected-draw',{view:CPU_ENDPOINT_VIEW.drawEnd,
    provenance:CPU_ENDPOINT_PROVENANCE.corrected,sourceFrame:1,phase:16});
  peerReport.pages.alpha.corrected_draw=report.pages.alpha.match_history.endpoints['corrected-draw'];
  for(const item of [alpha,beta]){
    const file=`${item.role}-final-match.png`;
    const bytes=await item.page.screenshot({path:path.join(out,file),fullPage:false,timeout:4000});
    peerReport.pages[item.role].final_screenshot={file,bytes:bytes.length,sha256:sha256(bytes)};
  }
  peerReport.endpoints_retained_per_page={alpha:Object.keys(report.pages.alpha.match_history.endpoints),
    beta:Object.keys(report.pages.beta.match_history.endpoints),cross_peer_raw_endpoint_comparison:false};
  await joinOwned('close-history',[historyCommand(alpha,'close',{phase:17,sourceFrame:1,
    restoreCount:1,forwardSteps:1,replaySteps:1}),historyCommand(beta,'close',{phase:17,sourceFrame:1,
    restoreCount:0,forwardSteps:1,replaySteps:0})]);
  const retired=await joinOwned('retire-history',[historyCommand(alpha,'retire',{phase:18,sourceFrame:1,
    restoreCount:1,forwardSteps:1,replaySteps:1}),historyCommand(beta,'retire',{phase:18,sourceFrame:1,
    restoreCount:0,forwardSteps:1,replaySteps:0})]);
  peerReport.final_history_retirement={alpha:retired[0],beta:retired[1]};
  peerReport.checks.push('both actual browser-owned peers exchanged exactly one native-validated local sample',
    'actual beta P2 differs from alpha held-prefix prediction and changes native scene_state checksum',
    'alpha complete Armed PRE equals Restored PRE before retained-input correction',
    'alpha corrected authoritative 44-byte PAD equals beta on-time PAD',
    'all 64 alpha corrected native checksum bytes equal beta on-time checksum bytes',
    'both pages retained local raw nine-domain/particle endpoints and completed source present/idle/DRAW/close/retire');
  peerReport.local_endpoint_comparison_only=restoredPreComparison;
  for(const item of [alpha,beta]){
    assert.equal(item.pageErrors.length,0,`${item.role} browser errors: ${item.pageErrors[0]}`);
    assert.equal(item.externalRequests.length,0,`${item.role} issued an external request`);
  }
  peerReport.status='peer-correction-complete';
  report.result='PEER_CORRECTION_COMPLETE';
  addCheck('one asymmetric wrong-prediction peer correction against beta on-time reference',{
    alpha_local_polls:1,beta_local_polls:1,actual_remote_samples_each:1,
    wrong_prediction_scene_state_changed:true,corrected_full_64_byte_checksum_equal:true,
    corrected_authoritative_pad_44_bytes_equal:true,pre_restore_equal:true,
    local_endpoint_sets:{alpha:Object.keys(report.pages.alpha.match_history.endpoints),
      beta:Object.keys(report.pages.beta.match_history.endpoints)}});
}
async function historyCommandWithKey(item,key,name,expected){
  const page=item.page;
  let commandFailure=null,releaseFailure=null,pressed=false;
  await page.locator('#canvas').focus();
  assert.equal(await page.evaluate(()=>document.activeElement?.id),'canvas',`${item.role} canvas focus was not retained for actual ${key}`);
  try{
    pressed=true;await page.keyboard.down(key);
    return await historyCommand(item,name,expected);
  }catch(error){commandFailure=error;throw error;}
  finally{
    if(pressed)try{await page.keyboard.up(key);item.keyToRelease=null;}
    catch(error){releaseFailure=error;item.keyToRelease=key;}
    if(releaseFailure){
      (report.pages[item.role].action_release_errors??=[]).push({key,error:String(releaseFailure?.message||releaseFailure)});
      if(!commandFailure)throw releaseFailure;
    }
  }
}

try{
  const {chromium,browser:browserConfig,browserPath,playwrightPath}=await loadBrowserTools(values.playwright);
  const alpha=await startOwnedPage('alpha',chromium,browserConfig,browserPath,playwrightPath,item=>owned.push(item));
  const alphaProfileBytes=Buffer.from(alpha.profile.bytes_base64,'base64');
  assert.equal(alphaProfileBytes.length,CARD_BYTES,'Alpha source snapshot has the wrong card extent');
  const alphaProfileHash=sha256(alphaProfileBytes);
  assert.equal(alphaProfileHash,alpha.profile.sha256,'Alpha source snapshot hash does not match its raw bytes');
  report.coordination.shared_profile_input={mode:'personal',bytes:CARD_BYTES,
    alpha_source_snapshot_sha256:alpha.profile.sha256,beta_staged_input_sha256:alphaProfileHash,
    beta_source_snapshot_sha256:null,staged_before_beta_disc_selection:true,
    alpha_source_snapshot_file:report.pages.alpha.source_card_snapshot.file,
    provenance:'actual raw alpha source-host card snapshot retained in the run output and passed to beta through the existing native Personal-profile setter'};
  const beta=await startOwnedPage('beta',chromium,browserConfig,browserPath,playwrightPath,
    item=>owned.push(item),alpha.profile);
  report.coordination.shared_profile_input.beta_source_snapshot_sha256=beta.profile.sha256;
  report.coordination.shared_profile_input.source_hashes_equal=
    alpha.profile.sha256===alphaProfileHash&&alphaProfileHash===beta.profile.sha256;
  assert.deepEqual(alpha.host,beta.host,'Loaded runtime/disc/save/profile identities differ across browser owners');
  addCheck('two distinct browser processes loaded the same reviewed runtime, disc, and save identity',alpha.host);
  report.coordination.alpha_local_seed_provenance=alpha.offer.offer.local_menu_rng_seed_provenance;
  report.coordination.beta_local_seed_provenance=beta.offer.offer.local_menu_rng_seed_provenance;
  report.coordination.alpha_source_seed=alpha.offer.seed;
  const alphaSelection=selectionWithoutSeed(alpha.offer),betaSelection=selectionWithoutSeed(beta.offer);
  report.coordination.source_selection_without_seed={alpha:alphaSelection,beta:betaSelection,
    equal:JSON.stringify(alphaSelection)===JSON.stringify(betaSelection)};
  assert.deepEqual(alphaSelection,betaSelection,
    'Pointer-free full source selections differ outside random_seed; stop before a shared commit');
  const agreementJSON=await alpha.admission.prepareAgreement({seed:alpha.offer.seed});
  report.coordination.per_page_agreement_jsons={alpha:agreementJSON,beta:null,
    equal_before_either_commit:false};
  const betaAgreementJSON=await beta.admission.prepareAgreement({seed:alpha.offer.seed});
  report.coordination.per_page_agreement_jsons.beta=betaAgreementJSON;
  report.coordination.per_page_agreement_jsons.equal_before_either_commit=betaAgreementJSON===agreementJSON;
  assert.equal(betaAgreementJSON,agreementJSON,'Shared exact native agreement JSON changed on beta');
  const heldDeadline=Date.now()+90000;
  await joinOwned('commit-agreement',[
    alpha.admission.commitAgreement({seed:alpha.offer.seed,agreementJSON,
      seedProvenance:'alpha native source-selected seed; Node-local diagnostic coordination'}),
    beta.admission.commitAgreement({seed:alpha.offer.seed,agreementJSON:betaAgreementJSON,
      seedProvenance:'alpha native source-selected seed; Node-local diagnostic coordination'}),
  ]);
  report.coordination.agreement_json=agreementJSON;
  const [alphaHeld,betaHeld]=await joinOwned('held-publication',[
    alpha.admission.waitHeld({deadline:heldDeadline}),beta.admission.waitHeld({deadline:heldDeadline})]);
  report.startup_comparison=compareStartupPrefixes(
    {offer:{route:{held:report.pages.alpha.route.agreed_start.held}}},
    {offer:{route:{held:report.pages.beta.route.agreed_start.held}}});
  assert(report.startup_comparison.equal,`Startup prefixes differ before takeover: ${JSON.stringify(report.startup_comparison.first_mismatch)}`);
  const liveIdentityEqual=alphaHeld.live_identity===betaHeld.live_identity;
  report.pages.alpha.route.live_identity=alphaHeld.live_identity;
  report.pages.beta.route.live_identity=betaHeld.live_identity;
  report.startup_comparison.live_identity_equal=liveIdentityEqual;
  report.startup_comparison.live_identity_first_mismatch=liveIdentityEqual?null:{
    alpha:JSON.parse(alphaHeld.live_identity),beta:JSON.parse(betaHeld.live_identity)};
  assert(liveIdentityEqual,'Full pointer-free live source identities differ across the held pair');
  await retainHeldScreenshots([alpha,beta]);
  await joinOwned('terminal-takeover',[terminalTakeover(alpha),terminalTakeover(beta)]);
  assert.equal(alpha.websockets.length,0,'Alpha opened a WebSocket before the held-static diagnostic');
  assert.equal(beta.websockets.length,0,'Beta opened a WebSocket before the held-static diagnostic');
  for(const item of [alpha,beta]){
    await item.page.evaluate(rows=>globalThis.__meleeRuntimeMatchHistory.configure(rows),wire);
    const identity=await item.page.evaluate(host=>globalThis.__meleeRuntimeMatchHistory.identity(host),item.host);
    assert.equal(identity,report.pages[item.role].route.takeover.identity,`${item.role} identity changed after exact static configuration`);
    report.pages[item.role].route.configured_identity=identity;
  }
  if(values.phase==='peer-correction')await runPeerCorrection(alpha,beta);
  else{
    const staticReadRows=joined.map(({identity})=>({address:identity.address,size:identity.size,identity}));
    const [alphaRows,betaRows]=await joinOwned('held-static-read',[
      readHeldStaticRows(alpha,staticReadRows),readHeldStaticRows(beta,staticReadRows)]);
    report.static_comparison=compareStaticRows(alphaRows,betaRows);
    report.static_comparison.outcome=report.static_comparison.equal?'baseline-rows-equal':'baseline-differences-observed';
    report.static_comparison.capture_called=false;report.static_comparison.begin_called=false;
    addCheck('complete exact reviewed static row set copied while both source owners remain held',{
      rows:wire.length,total_bytes:report.pages.alpha.static_rows.total_bytes,
      equal:report.static_comparison.equal,rows_with_differences:report.static_comparison.rows_with_differences,
      first_mismatch:report.static_comparison.first_mismatch});
    report.result='BASELINE_DIAGNOSTIC_COMPLETE';
  }
}catch(error){
  resultError=error;report.result='FAIL';
  report.failure={name:error?.name||'Error',message:String(error?.message||error),stack:error?.stack||null};
}
finally{
  await Promise.all(owned.filter(item=>item.peerInstalled).map(async item=>{
    try{
      const cleanup=report.pages[item.role].cleanup;
      cleanup.peer=await peerOperation(item,'peerClose');
      assert.equal(cleanup.peer.closed,true);
    }catch(error){report.cleanup_failures.push(`${item.role} peer close: ${String(error)}`);}
  }));
  for(const item of [...owned].reverse()){
    const pageReport=report.pages[item.role];
    if(item.page){
      if(resultError){
        try{
          const file=`${item.role}-failure-boundary.png`;
          const bytes=await item.page.screenshot({path:path.join(out,file),fullPage:false,timeout:4000});
          pageReport.failure_screenshot={file,bytes:bytes.length,sha256:sha256(bytes)};
        }catch(error){pageReport.failure_screenshot_error=String(error?.message||error);}
        try{pageReport.failure_diagnostics=await item.driver?.diagnostics?.()??{unavailable:'browser driver unavailable'};}
        catch(error){pageReport.failure_diagnostics={unavailable:String(error?.message||error)};}
      }
      if(item.keyToRelease)try{await item.page.keyboard.up(item.keyToRelease);}
      catch(error){report.cleanup_failures.push(`${item.role} key release: ${String(error)}`);}
      await retainNativeLog(item,'before-cleanup');
      try{
        const actualOwner=await item.page.evaluate(()=>{
          const controller=globalThis.__meleeRuntimeMatchHistory,module=globalThis.Module;
          let state=null,statusError=null;
          if(controller)try{state=controller.status().ownerState;}
          catch(error){statusError=String(error?.message||error);}
          return {state,statusError,controllerPresent:!!controller,
            runtimePresent:!!module&&typeof module._melee_web_native_menu_phase==='function'&&
              typeof module._melee_web_native_menu_running==='function'};
        });
        if(actualOwner.statusError&&actualOwner.controllerPresent)
          throw Error(`Cannot determine terminal owner state for cleanup: ${actualOwner.statusError}`);
        if(item.takenOver||actualOwner.state===1){
          item.takenOver=true;
          const clean=await item.page.evaluate(()=>globalThis.__meleeRuntimeMatchHistory.command('cleanup'));
          item.nativeCleanupSucceeded=clean.ownerState===2&&clean.gpuPending===0;
          pageReport.route.cleanup=clean;
          if(!item.nativeCleanupSucceeded)throw Error(`Native terminal cleanup refused: ${JSON.stringify(clean)}`);
          pageReport.cleanup.native_owner='retired';
        }else if(actualOwner.runtimePresent&&item.driver){
          await item.driver.unload();pageReport.cleanup.native_owner='ordinary-unload-before-takeover';
        }else pageReport.cleanup.native_owner='no native source owner active';
      }catch(error){
        pageReport.cleanup.native_owner=`failed: ${String(error?.message||error)}`;
        report.cleanup_failures.push(`${item.role} native/source cleanup: ${String(error?.message||error)}`);
      }
      await retainNativeLog(item,'after-cleanup');
      item.driver?.dispose?.();
      pageReport.browser_errors=item.pageErrors;
      pageReport.websockets=item.websockets;pageReport.external_requests=item.externalRequests;
      pageReport.console_messages=item.consoleMessages;
    }
    if(item.browser)await captureBrowserProcessInventory(item);
    if(item.context)try{await item.context.close();pageReport.cleanup.browser='context-closed';}
    catch(error){pageReport.cleanup.browser=`context-close-failed: ${String(error?.message||error)}`;
      report.cleanup_failures.push(`${item.role} browser context cleanup: ${String(error?.message||error)}`);}
    if(item.browser)try{await item.browser.close();pageReport.cleanup.browser+='; browser-closed';}
    catch(error){pageReport.cleanup.browser=`browser-close-failed: ${String(error?.message||error)}`;
      report.cleanup_failures.push(`${item.role} browser process cleanup: ${String(error?.message||error)}`);}
  }
  if(relayRuntime)try{
    report.peer_correction.relay.cleanup=await relayRuntime.close(!resultError&&!report.cleanup_failures.length);
  }catch(error){report.cleanup_failures.push(`Relay cleanup: ${String(error)}`);}
  const inventories=Object.entries(report.pages).flatMap(([role,pageReport])=>
    (pageReport.browser_process_info??[]).map(row=>({...row,owner_role:role})));
  report.browser_process_info=inventories;
  const byPid=new Map();
  for(const row of inventories){if(!byPid.has(row.id))byPid.set(row.id,[]);byPid.get(row.id).push(row);}
  const overlaps=[...byPid.entries()].filter(([,rows])=>new Set(rows.map(row=>row.owner_role)).size>1)
    .map(([id,rows])=>({id,roles:[...new Set(rows.map(row=>row.owner_role))],types:[...new Set(rows.map(row=>row.type))]}));
  const browserPids=Object.fromEntries(['alpha','beta'].map(role=>[role,
    (report.pages[role].browser_process_info??[]).filter(row=>row.type==='browser').map(row=>row.id)]));
  report.browser_process_pid_attribution={per_owner_browser_pids:browserPids,
    cross_owner_pid_overlaps:overlaps,source:'actual SystemInfo.getProcessInfo rows from each independently launched CDP session'};
  const alphaPids=new Set(browserPids.alpha),betaPids=new Set(browserPids.beta);
  if(!alphaPids.size||!betaPids.size||[...alphaPids].some(pid=>betaPids.has(pid)))
    report.cleanup_failures.push('The two owned headless Chrome browser processes were not independently attributed');
  if(report.cleanup_failures.length&&report.result!=='FAIL'){
    report.result='FAIL';report.failure={name:'CleanupError',message:report.cleanup_failures[0],stack:null};
  }
  report.finished_at=new Date().toISOString();
  report.failure_cause=resultError?String(resultError.message||resultError):null;
  await fs.writeFile(path.join(out,'result.json'),JSON.stringify(report,null,2)+'\n');
}
console.log(JSON.stringify({result:report.result,phase:report.phase,static_comparison:report.static_comparison,
  failure:report.failure?.message||null,report:path.join(out,'result.json')}));
if(report.result==='FAIL')process.exitCode=1;
