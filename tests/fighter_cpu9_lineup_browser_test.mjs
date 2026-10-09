#!/usr/bin/env node
/** Headless rendered four-CPU9 source-menu match/Results/CSS loop fixture.
 * Inputs are bounded raw PAD samples consumed by the live original CSS/SSS;
 * fighter setup and match state are read-only source observations. This does
 * not compare against retail, physical controllers, timing, pixels or PCM.
 * Results defaults to historical keyboard pulses; keyboard-gated preserves
 * that key path while source-tick-gating the CPU page transition. The
 * keyboard-gated-p1-enter preserves the connected split-keyboard ports used
 * for CSS setup, verifies that Enter produces only P1 Start, and observes the
 * disconnected CPU pages before the post-page confirmation.
 * keyboard-gated-two-prefix-source-tick keeps the first two trusted Enter
 * edges at source ticks 192/363, verifies CPU auto-pages, and uses a
 * development-only source pause to dispatch trusted Enter at tick 560 and
 * release it at tick 570. The pause supplies neither PAD nor game state.
 * keyboard-three-prefix mode retains the first three ordinary pulses through
 * Results source frame 560 before continuing to CSS. Source-tick remains a
 * separate controlled PAD path, not a keyboard/reference claim. The
 * source-tick-three-pulse mode preserves the focused 180/360/final-tick PAD
 * schedule and checks disconnected CPU auto-pages before the final pulse. */
import assert from 'node:assert/strict';
import {installRuntimeDiagnosticsCapture, readRuntimeDiagnosticsCapture} from './runtime_callback_recorder.mjs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {Worker} from 'node:worker_threads';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {readResultsEntryPacket,bindResultsEntryPacket} from './results_entry_packet.mjs';
import {queueResultsP1StartAtCurrentSource,scheduleResultsP1StartSequence,
  scheduleResultsSourceFramePauses}
  from './results_source_pad_input.mjs';
import {assertResultsCpuPagesAfterInitialP1Keyboard,
  assertResultsCpuPagesAfterP1KeyboardPrefix,buildResultsPadTraceRecord,
  findConsumedResultsStartKeyboardAttempt,findResultsStartRunAtOrAfter,
  summarizeResultsPadTrace}
  from './results_source_pad_trace.mjs';

const MATCH_PLAYER_SLOTS=Object.freeze(['p0','p1','p2','p3']);
const INCIDENT_REASON_SLOTS=10;
const zeroIncidentReasonCounts=()=>Array(INCIDENT_REASON_SLOTS).fill(0);
const incidentReasonBucket=value=>Number.isInteger(value)&&value>=0&&value<INCIDENT_REASON_SLOTS?value:null;
const {values}=parseArgs({options:{...Object.fromEntries(
  ['url','disc','out','lineup','playwright','build-dir','results-input','cpu-levels'].map(name=>[name,{type:'string'}])),
  teams:{type:'string'},'friendly-fire':{type:'boolean'},
  'results-confirm-frame':{type:'string'},
  'results-observe-after-confirmation':{type:'boolean'},
  matches:{type:'string'},'setup-only':{type:'boolean'},'stage-setup-only':{type:'boolean'},
  'stage-kind':{type:'string'},'wall-bound-seconds':{type:'string'},
  'stop-on-timing-pause':{type:'boolean'},
  'controlled-contention':{type:'boolean'},'user-data-dir':{type:'string'},
  'readiness-preflight':{type:'boolean'},'stage-map-preflight':{type:'boolean'},
  'incident-capture-preflight':{type:'boolean'}}});
const stages=Object.freeze({
  'final-destination':Object.freeze({id:'final-destination',name:'Final Destination',sourceId:0x20,slug:'fd'}),
  battlefield:Object.freeze({id:'battlefield',name:'Battlefield',sourceId:0x1F,slug:'battlefield'}),
});
if(values['readiness-preflight']){
  console.log(JSON.stringify(runReadinessPreflight()));
  process.exit(0);
}
if(values['stage-map-preflight']){
  const header=path.resolve(import.meta.dirname,'../.deps/melee/src/melee/gr/forward.h');
  try{
    const source=await fs.readFile(header,'utf8');
    const authored=name=>{
      const match=source.match(new RegExp(`/\\*\\s*0x([0-9A-Fa-f]+)\\s*\\*/\\s*${name}\\b`));
      return match?Number.parseInt(match[1],16):null;
    };
    const authoredStages={
      'final-destination':authored('St_Kind_Last'),
      battlefield:authored('St_Kind_Battle')};
    assert.equal(authoredStages['final-destination'],stages['final-destination'].sourceId,
      'Final Destination stage id must match the authored pinned St_Kind_Last enum');
    assert.equal(authoredStages.battlefield,stages.battlefield.sourceId,
      'Battlefield stage id must match the authored pinned St_Kind_Battle enum');
    console.log(JSON.stringify({result:'pass',header,authored:authoredStages,stages}));
  }catch(error){
    if(error?.code==='ENOENT'){
      console.log(JSON.stringify({result:'unavailable',reason:'authored_stage_enum_unavailable',header}));
      process.exit(0);
    }
    throw error;
  }
  process.exit(0);
}
async function runIncidentCapturePreflight(){
  const fakePage={evaluate:async(fn,argument)=>fn(argument)};
  const previousSample=globalThis.menuDiagnosticSample;
  const previousIncident=globalThis.menuDiagnosticIncident;
  const previousCapture=globalThis.__meleeWebRuntimeIncidentCampaignCapture;
  const snapshot=()=>JSON.parse(JSON.stringify(globalThis.__meleeWebRuntimeIncidentCampaignCapture));
  try{
    await installRuntimeDiagnosticsCapture({schema_version:1},
      {status:'preflight'},fakePage);
    if(globalThis.__meleeWebRuntimeIncidentCampaignCapture?.status!=='installed')
      throw Error('preflight installer did not expose an installed capture');
    const retain=(reason,value,threshold,sourceFrame=0,scene=7,clockOwner=0)=>
      globalThis.menuDiagnosticIncident(reason,value,threshold,sourceFrame,scene,clockOwner);
    for(let index=0;index<25;index++)retain(7,0,0);
    const preparationOnly=snapshot();
    retain(1,9,8,0,7,1);
    for(let index=0;index<24;index++)retain(7,0,0);
    const guardDropped=snapshot();
    retain(7,1,0);
    for(let index=0;index<24;index++)retain(7,0,0);
    const invalidPreparation=snapshot();
    retain(99,0,0);
    const unknown=snapshot();
    if(preparationOnly.dropped_reason_counts[7]!==1||preparationOnly.reason_counts[7]!==25)
      throw Error('preflight did not preserve dropped preparation reason counts');
    if(guardDropped.dropped_reason_counts[1]!==1||guardDropped.reason_counts[1]!==1)
      throw Error('preflight did not preserve a dropped guard reason');
    if(invalidPreparation.invalid_preparation_count!==1||invalidPreparation.dropped_invalid_preparation_count!==1)
      throw Error('preflight did not preserve an evicted invalid preparation reason');
    if(unknown.unknown_reason_count!==1)
      throw Error('preflight did not count unknown reason codes');
    return {result:'pass',capture_status:'installed',max_incidents:24,preparation_only:preparationOnly,
      guard_after_preparation:guardDropped,invalid_preparation:invalidPreparation,unknown_reason:unknown};
  }finally{
    if(previousSample===undefined)delete globalThis.menuDiagnosticSample;
    else globalThis.menuDiagnosticSample=previousSample;
    if(previousIncident===undefined)delete globalThis.menuDiagnosticIncident;
    else globalThis.menuDiagnosticIncident=previousIncident;
    if(previousCapture===undefined)delete globalThis.__meleeWebRuntimeIncidentCampaignCapture;
    else globalThis.__meleeWebRuntimeIncidentCampaignCapture=previousCapture;
  }
}
if(values['incident-capture-preflight']){
  console.log(JSON.stringify(await runIncidentCapturePreflight()));
  process.exit(0);
}
if(!values.url||!values.disc||!values.out||!['A','B','M'].includes(values.lineup))
  throw Error('Use --url http://127.0.0.1:PORT/runtime.html --disc OWNED_CISO --out NEW_DIRECTORY --lineup A|B|M [--cpu-levels L0,L1,L2,L3] [--teams T0,T1,T2,T3 [--friendly-fire]] [--matches 1|2|3|4] [--setup-only] [--stage-setup-only] [--stage-kind final-destination|battlefield] [--wall-bound-seconds SECONDS] [--stop-on-timing-pause] [--controlled-contention] [--user-data-dir EXTERNAL_PROFILE] [--playwright PACKAGE_DIR] [--build-dir BUILT_RUNTIME_DIR] [--results-input keyboard|keyboard-three-prefix|keyboard-gated|keyboard-gated-p1-enter|keyboard-gated-two-prefix|keyboard-gated-two-prefix-source-tick|source-tick|source-tick-three-pulse] [--results-confirm-frame SOURCE_TICK] [--readiness-preflight] [--stage-map-preflight]');
const stageKind=values['stage-kind']||'final-destination';
if(!Object.hasOwn(stages,stageKind))
  throw Error('--stage-kind must be final-destination or battlefield');
const stage=stages[stageKind];
const stageSetupOnly=values['stage-setup-only']===true;
if(stageSetupOnly&&values['setup-only']===true)
  throw Error('--stage-setup-only cannot be combined with --setup-only');
const campaignRequested=stageSetupOnly||values['stage-kind']!==undefined||values['wall-bound-seconds']!==undefined||
  values['stop-on-timing-pause']===true||values['controlled-contention']===true;
const wallBoundSeconds=campaignRequested?Number(values['wall-bound-seconds']??(stageSetupOnly?120:600)):null;
if(campaignRequested&&(!Number.isInteger(wallBoundSeconds)||wallBoundSeconds<1||wallBoundSeconds>600))
  throw Error('--wall-bound-seconds must be an integer from 1 through 600 in campaign mode');
if(!campaignRequested&&values['wall-bound-seconds']!==undefined)
  throw Error('--wall-bound-seconds requires campaign mode');
const stopOnTimingPause=values['stop-on-timing-pause']===true;
if(stageSetupOnly&&!stopOnTimingPause)
  throw Error('--stage-setup-only requires --stop-on-timing-pause');
const controlledContention=values['controlled-contention']===true;
const userDataDirectory=values['user-data-dir']?path.resolve(values['user-data-dir']):null;
const campaignCondition=controlledContention?'controlled-contention':'shared-host-uncontrolled';
const campaignDeadline=campaignRequested?Date.now()+wallBoundSeconds*1000:null;
const cpuLevels=values['cpu-levels']===undefined?[9,9,9,9]:
  values['cpu-levels'].split(',').map(Number);
if(cpuLevels.length!==4||cpuLevels.some(level=>!Number.isInteger(level)||level<1||level>9))
  throw Error('--cpu-levels must contain exactly four comma-separated integer levels from 1 through 9');
const cpuProfileDescription=cpuLevels.every(level=>level===9)?'four CPU9 players':
  `four CPU players at source-confirmed levels ${cpuLevels.join(',')}`;
// Original VS Team Battle: mnCharSel cycleTeam advances a door through
// (team + 1) % 3, and fn_80262F44 keeps CSS Start hidden until two active
// doors belong to different teams. Request only setups the source can start.
const teams=values.teams===undefined?null:values.teams.split(',').map(Number);
if(teams&&(teams.length!==4||teams.some(team=>!Number.isInteger(team)||team<0||team>2)||
   new Set(teams).size<2))
  throw Error('--teams must contain four source team IDs from 0 through 2 with at least two different teams');
const friendlyFire=values['friendly-fire']===true;
if(friendlyFire&&!teams)throw Error('--friendly-fire applies only to a --teams Team Battle');
const teamDescription=teams?`; Team Battle teams ${teams.join(',')}${friendlyFire?' with Rules Plus friendly fire on':''}`:'';
const resultsInputMode=values['results-input']||'keyboard';
if(!['keyboard','keyboard-three-prefix','keyboard-gated','keyboard-gated-p1-enter',
  'keyboard-gated-two-prefix','keyboard-gated-two-prefix-source-tick','source-tick',
  'source-tick-three-pulse'].includes(resultsInputMode))
  throw Error('--results-input must be keyboard, keyboard-three-prefix, keyboard-gated, keyboard-gated-p1-enter, keyboard-gated-two-prefix, keyboard-gated-two-prefix-source-tick, source-tick, or source-tick-three-pulse');
const sourceTickMode=resultsInputMode==='source-tick'||resultsInputMode==='source-tick-three-pulse';
const sourceTickThreePulse=resultsInputMode==='source-tick-three-pulse';
const keyboardSourceTickConfirmMode=resultsInputMode==='keyboard-gated-two-prefix-source-tick';
const keyboardPrefixGatedMode=resultsInputMode==='keyboard-gated-two-prefix'||
  keyboardSourceTickConfirmMode;
const keyboardGatedMode=resultsInputMode==='keyboard-gated'||
  resultsInputMode==='keyboard-gated-p1-enter'||keyboardPrefixGatedMode;
const keyboardP1EnterMode=resultsInputMode==='keyboard-gated-p1-enter'||keyboardPrefixGatedMode;
const keyboardPortErrors=[[0],[0],[-1],[-1]];
const keyboardAutoPageSlots=[2,3];
const resultsConfirmFrame=Number(values['results-confirm-frame']||
  (keyboardSourceTickConfirmMode?560:600));
if((sourceTickThreePulse||keyboardSourceTickConfirmMode)&&
   (!Number.isInteger(resultsConfirmFrame)||resultsConfirmFrame<=360||resultsConfirmFrame>8191))
  throw Error('--results-confirm-frame must be an integer source tick after the tick-360 pulse and no later than 8191');
if(keyboardSourceTickConfirmMode&&resultsConfirmFrame+10>8191)
  throw Error('The exact keyboard release pause must remain within the Results source cursor bound of 8191');
const resultsObserveAfterConfirmation=values['results-observe-after-confirmation']===true;
if(resultsObserveAfterConfirmation&&(!keyboardSourceTickConfirmMode||Number(values.matches||2)!==1))
  throw Error('--results-observe-after-confirmation requires the exact source-tick keyboard mode and --matches 1');
if(resultsObserveAfterConfirmation&&resultsConfirmFrame+150>8191)
  throw Error('The post-confirmation observation target must remain within the Results source cursor bound of 8191');
if(!sourceTickThreePulse&&!keyboardSourceTickConfirmMode&&
   values['results-confirm-frame']!==undefined)
  throw Error('--results-confirm-frame is only valid with a source-tick Results mode');
const matchCount=Number(values.matches||2);
if(![1,2,3,4].includes(matchCount))throw Error('--matches must be 1, 2, 3 or 4');
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
    sha256:await sha256(wasmPath),
    artifact_scope:'private-development-artifact',
    hash_scope:'exact SHA-256 of gameplay_menu_browser.wasm bytes; not the public runtime graph'};
}
function sourceProvenance(){
  const git=(...args)=>execFileSync('git',args,{cwd:repository,encoding:'utf8'}).trim();
  return {commit:git('rev-parse','HEAD'),tree:git('rev-parse','HEAD^{tree}'),
    status:git('status','--porcelain=v1'),
    tracked_diff_sha256:createHash('sha256').update(
      execFileSync('git',['diff','--binary','HEAD'],{cwd:repository})).digest('hex')};
}
const lineup=values.lineup==='M'?
  Array.from({length:4},()=>({name:'Mario',kind:8})):values.lineup==='A'?
  [{name:'Game & Watch',kind:3,position:[7.1,2.5]},
   {name:'Kirby',kind:4,position:[0.1,9.5]},
   {name:'Ice Climbers',kind:14,position:[-6.9,9.5]},
   {name:'Fox',kind:2,position:[-20.9,9.5]}]:
  [{name:'Samus',kind:16,position:[7.1,9.5]},
   {name:'Yoshi',kind:17,position:[7.1,16.5]},
   {name:'Zelda',kind:18,position:[9.1,9.5]},
   {name:'Falco',kind:20,position:[-32.2,9.5]}];
const continuationScope=resultsObserveAfterConfirmation?
  `natural Results→CPU-page gate→P1 confirmation at tick ${resultsConfirmFrame}→150 source ticks without retry; CSS return is observed if it occurs but is not required`:
  matchCount===1?'natural Results→CSS only':
  `natural Results→CSS→${matchCount-1} subsequent match${matchCount===2?'':'es'}`;
const resultsInputScope=resultsInputMode==='keyboard-three-prefix'?
  'first three ordinary 160/120ms Enter pulses retained through source cursor 560, then ordinary continuation; not an exact historical consumed-PAD replay':
  keyboardSourceTickConfirmMode?
  `P1/P2-connected split keyboard, CPU ports P3/P4 disconnected; trusted Enter down/up edges at source cursors 192/202 and 363/373; assert automatic CPU page transitions before source tick ${resultsConfirmFrame}; pause before samples 192,202,363,373,${resultsConfirmFrame},${resultsConfirmFrame+10} to dispatch trusted edges and retain ten consumed Start samples per pulse${resultsObserveAfterConfirmation?'; stop after three pulses and observe to source tick '+(resultsConfirmFrame+150)+' or natural CSS without a retry':''}; no PAD/game-state injection or live-timing claim`:
  keyboardPrefixGatedMode?
    'P1/P2-connected split keyboard and disconnected CPU ports P3/P4; retain ordinary trusted 160/120ms Enter edges with P1 Start consumed at source ticks 192 and 363, then wait for both source-observed CPU page transitions before the third Enter confirmation. This is a timing discriminator, not a full historical replay':
  keyboardP1EnterMode?
    'ordinary trusted 160/120ms Enter edges; preserve connected split-keyboard ports P1/P2 and disconnected CPU ports P3/P4; source PAD must show P1 Start only, and both disconnected CPU pages must advance before post-page confirmation. The historical failure report omitted Results port status':
  resultsInputMode==='keyboard-gated'?
  'ordinary trusted 160/120ms P1 Enter edges; ports P1/P2 connected and CPU ports P3/P4 disconnected; wait for both disconnected CPU pages before post-page confirmation':
  sourceTickThreePulse?
  `P1-only ten-source-tick Start holds queued at Results ticks 180/360/${resultsConfirmFrame}; disconnected CPU page transitions must precede the tick-${resultsConfirmFrame} confirmation; connectedness and consumed edges retained; controlled PAD path, not literal keyboard-event replay`:null;
const report={schema:'melee-web-cpu9-lineup-browser-v1',result:'fail',
  scope:`Headless Chrome rendered gameplay; live source CSS/SSS controller input, ${cpuProfileDescription}${teamDescription}, four stocks, ${stage.name}; Results continuation input=${resultsInputMode}; ${continuationScope}. No retail comparison, pixels, PCM, foreground timing, physical-controller or performance claim.`,
  campaign:campaignRequested?{kind:'natural-incident',wall_bound_seconds:wallBoundSeconds,
    stage_kind:stage.id,stage_source_id:stage.sourceId,stop_on_timing_pause:stopOnTimingPause,
    contention:campaignCondition,condition:campaignCondition,audio:'enabled',
    reducer_packet:{schema:'melee-web-runtime-incident-reducer-v1',experiments_per_boundary:2,
      stop_on_first_unexpected_pause:true}}:null,
  results_input_mode:resultsInputMode,
  results_input_scope:resultsInputScope,
  stage_setup_only:stageSetupOnly,
  controller_profile:null,
  lineup:values.lineup,players:lineup.map(({name,kind},door)=>({name,kind,cpu:cpuLevels[door],stocks:4,
    team:teams?teams[door]:null})),
  team_battle:teams?{teams,friendly_fire:friendlyFire,source_rules:[
    'mnCharSel_CursorThink toggles StartMeleeData.rules.is_teams at the original CSS Teams control',
    'cycleTeam advances one door through the three authored team colours ((team + 1) % 3)',
    'fn_80262F44 keeps CSS Start hidden until two active doors belong to different teams',
    'fn_8023201C Rules Plus row 1 saves GameRules.friendly_fire; gm_1601 copies it into StartMeleeRules']}:null,
  matches:[],screenshots:[],source_progress:[],pad_sample_count:0,page_errors:[],phases:[],controller_inputs:[],
  results_input_events:[],
  browser_context:{kind:userDataDirectory?'persistent':'temporary',
    cache_reuse:userDataDirectory?'campaign-shared-origin-profile':'temporary-context',
    requested_cache:url.searchParams.get('render-cache')==='clear'?'cold':'warm',
    cache_evidence:{status:'unavailable',observed:false,populated_observed:false,restore_observed:false},
    driver_cache:'uncontrolled'}};
report.timing_pause_receipts=[];
report.match_readiness_failures=[];
report.runtime_diagnostics={schema:'melee-web-runtime-callback-capture-v1',identity:null,
  max_samples:100,max_incidents:24,samples:[],incidents:[],dropped_samples:0,dropped_incidents:0,
  reason_counts:zeroIncidentReasonCounts(),unknown_reason_count:0,
  dropped_reason_counts:zeroIncidentReasonCounts(),dropped_unknown_reason_count:0,
  invalid_preparation_count:0,dropped_invalid_preparation_count:0,
  callback_count:0,status:'not-installed'};
report.source_timing_disruptions=[];
report.native_command_errors=[];
report.results_entry_packets=[];
report.results_entry_packet_reads=[];
report.results_source_pad_traces=[];
report.results_page_transition_checks=[];
report.results_three_pulse_prefixes=[];
report.page_crashes=[];
report.target_crashes=[];
report.audio_diagnostics=[];
report.provenance={source_start:sourceProvenance(),
  harness_sha256:await sha256(new URL(import.meta.url)),
  browser_driver_helper_sha256:await sha256(new URL('../scripts/browser_driver.mjs',import.meta.url)),
  browser_tools_helper_sha256:await sha256(new URL('../scripts/browser_tools.mjs',import.meta.url)),
  results_entry_helper_sha256:await sha256(new URL('./results_entry_packet.mjs',import.meta.url)),
  results_source_pad_trace_helper_sha256:await sha256(new URL('./results_source_pad_trace.mjs',import.meta.url)),
  results_source_pad_input_helper_sha256:await sha256(new URL('./results_source_pad_input.mjs',import.meta.url)),
  local_artifacts:localWasmIdentity?[localWasmIdentity]:[],
  served_artifacts:[]};
report.runtime_diagnostics.identity={schema_version:1,
  source_commit:null,runtime_hash:null,build_profile:'unknown'};
report.runtime_diagnostics.identity_scope={
  status:'unavailable',
  reason:'runtime.html is a private development artifact without the audited public diagnostic meta identity',
  public_runtime_graph_bound:false,
  private_artifact:localWasmIdentity?{
    path:localWasmIdentity.path,bytes:localWasmIdentity.bytes,
    sha256:localWasmIdentity.sha256,hash_scope:localWasmIdentity.hash_scope}:null};
report.campaign_wall_bound=campaignRequested?{
  status:'armed',wall_bound_seconds:wallBoundSeconds,deadline_at_ms:campaignDeadline,
  failure_code:null,evidence_status:'pending'}:null;
const artifactReads=[];
let browser,browserContext,page,driver,browserCdp,activeMatchIndex=null,contentionWorker=null;
let ownedClosePromise=null;
async function closeOwnedBrowserResources(){
  if(ownedClosePromise)return ownedClosePromise;
  ownedClosePromise=(async()=>{
  report.cleanup??={};
  for(const [name,resource,close] of [
    ['driver',driver,()=>driver.dispose()],
    ['cdp',browserCdp,()=>browserCdp.detach()],
    ['context',browserContext,()=>browserContext.close()],
    ['browser',browser,()=>browser.close()],
  ]){
    if(!resource)continue;
    try{await close();report.cleanup[name]={status:name==='driver'?'disposed':'closed'};}
    catch(error){
      const message=String(error?.message||error);
      report.cleanup[name]={status:'failed',error:message};
      report.result='fail';process.exitCode=1;
      report.failure??={message:`Owned ${name} cleanup failed: ${message}`};
    }
  }
  })();
  return ownedClosePromise;
}
const onOwnedInterrupt=signal=>{
  report.interruption??={signal,observed_at:new Date().toISOString()};
  report.result='fail';process.exitCode=1;
  report.failure??={message:`Browser route interrupted by ${signal}`,code:'owned_interruption'};
  void closeOwnedBrowserResources();
};
const onSigint=()=>onOwnedInterrupt('SIGINT');
const onSigterm=()=>onOwnedInterrupt('SIGTERM');
process.on('SIGINT',onSigint);process.on('SIGTERM',onSigterm);

let campaignWallBoundExceeded=false,campaignWallBoundTimer=null,wallBoundTask=null;
function startControlledContention(){
  if(!controlledContention||contentionWorker)return;
  contentionWorker=new Worker(`setInterval(()=>{const end=Date.now()+35;while(Date.now()<end){}},50);`,{eval:true});
  contentionWorker.unref();
}
async function stopControlledContention(){
  if(!contentionWorker)return;
  const worker=contentionWorker;contentionWorker=null;
  await worker.terminate().catch(()=>{});
}
async function readAudioDiagnostics(){
  if(!page||page.isClosed())return null;
  try{return await page.evaluate(()=>window.__meleeWebAudioDiagnostics?.snapshot()||null);}
  catch(error){return {status:'capture-error',error:error.message};}
}
// Called only on Results entry or failure, never by the polling diagnostics.
// Read independently of other observers so an unrelated observer failure does
// not hide the last retained entry. Older frozen builds remain explicitly absent.
async function retainResultsEntry(reason){
  try{
    report.audio_diagnostics.push({reason,snapshot:await readAudioDiagnostics()});
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
      if(typeof Module._melee_web_input_message!=='function')
        throw Error('Controller-service diagnostics are unavailable for a Results Enter event');
      const inputStatus=JSON.parse(Module.UTF8ToString(Module._melee_web_input_message()));
      rows.push({kind,key:event.key,repeat:!!event.repeat,isTrusted:!!event.isTrusted,
        eventTimeMs:event.timeStamp,nativeSourceSteps:Number(window.nativeSourceSteps),
        phase,resultsSourceFrameAtEvent:cursor?Number(cursor[1]):null,
        target:{tagName:event.target?.tagName??null,id:event.target?.id??null},
        activeElement:{tagName:document.activeElement?.tagName??null,
          id:document.activeElement?.id??null},
        canvasFocused:document.activeElement===document.querySelector('canvas'),
        inputServiceStatusAtEvent:inputStatus,
        inputServiceStatusScope:'read-only snapshot at DOM dispatch; PAD values are the most recent source sample, not this key event\'s consumed tick',
        diagnostics});
    };
    window.addEventListener('keydown',event=>record('keydown',event),true);
    window.addEventListener('keyup',event=>record('keyup',event),true);
  });
}
async function retainRuntimeDiagnosticsCapture(){
  if(!page||page.isClosed())return;
  try{
    const capture=await readRuntimeDiagnosticsCapture(page);
    report.runtime_diagnostics=capture;
  }catch(error){
    report.runtime_diagnostics={schema:'melee-web-runtime-callback-capture-v1',status:'capture-error',
      identity:report.runtime_diagnostics.identity,identity_scope:report.runtime_diagnostics.identity_scope,
      samples:[],incidents:[],dropped_samples:0,
      dropped_incidents:0,reason_counts:zeroIncidentReasonCounts(),unknown_reason_count:0,
      dropped_reason_counts:zeroIncidentReasonCounts(),dropped_unknown_reason_count:0,
      invalid_preparation_count:0,dropped_invalid_preparation_count:0,
      callback_count:0,error:error.message};
  }
}
const buttonA=0x0100,buttonStart=0x1000;
const phase=()=>page.evaluate(()=>Module._melee_web_native_menu_phase());
const diagnostic=()=>page.evaluate(stageId=>{
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
    sss:window.menuObserveStage?.(stageId)||null,
    cache:Module.runtimeCacheState?{state:Module.runtimeCacheState.state,mounted:Module.runtimeCacheState.mounted,
      populated:Module.runtimeCacheState.populated,saves:Module.runtimeCacheState.saves,
      clears:Module.runtimeCacheState.clears,dirty:Module.runtimeCacheState.dirty,
      file_bytes:Module.runtimeCacheState.fileBytes,message:Module.runtimeCacheState.message}:null,
  };
},stage.sourceId);
async function screenshot(name){
  const file=path.join(output,`${name}.png`);
  await page.screenshot({path:file,fullPage:false});report.screenshots.push(file);
  const canvas=path.join(output,`${name}-canvas.png`);
  await page.locator('#canvas').screenshot({path:canvas});report.screenshots.push(canvas);
}
function campaignWallBoundError(){
  const error=Error(`Campaign attempt wall bound exhausted after ${wallBoundSeconds} seconds`);
  error.code='campaign_wall_bound_exceeded';
  return error;
}
function triggerCampaignWallBound(){
  if(campaignDeadline===null||campaignWallBoundExceeded)return;
  campaignWallBoundExceeded=true;
  report.result='fail';
  process.exitCode=1;
  report.failure??={code:'campaign_wall_bound_exceeded',message:campaignWallBoundError().message};
  if(report.campaign_wall_bound){
    report.campaign_wall_bound.status='exceeded';
    report.campaign_wall_bound.failure_code='campaign_wall_bound_exceeded';
  }
  wallBoundTask=(async()=>{
    // Keep the final evidence bounded. This task only reads the page and then
    // closes the browser objects created by this invocation; it never resumes
    // source execution, injects input, or changes source policy.
    const watchdogTimeout=Symbol('campaign-watchdog-timeout');
    const bounded=async operation=>{
      let timer=null;
      try{
        return await Promise.race([
          Promise.resolve(operation).catch(()=>undefined),
          new Promise(resolve=>{timer=setTimeout(()=>resolve(watchdogTimeout),1500);}),
        ]);
      }finally{
        if(timer!==null)clearTimeout(timer);
      }
    };
    const hadPage=!!page&&!page.isClosed();
    const runtimeCapture=await bounded(retainRuntimeDiagnosticsCapture());
    const captureTimedOut=runtimeCapture===watchdogTimeout;
    if(page&&!page.isClosed()){
      const stateCapture=await bounded(page.evaluate(()=>({
        status:document.querySelector('#status')?.textContent||'',
        phase:typeof Module!=='undefined'&&typeof Module._melee_web_native_menu_phase==='function'?
          Module._melee_web_native_menu_phase():null,
      })));
      if(stateCapture!==watchdogTimeout&&stateCapture!==undefined)
        report.campaign_wall_bound.evidence_state=stateCapture;
      await bounded(screenshot('campaign-wall-bound'));
      await bounded(page.locator('body').textContent().then(text=>
        fs.writeFile(path.join(output,'campaign-wall-bound-page.txt'),text)));
    }
    if(report.campaign_wall_bound){
      const diagnosticCaptureAvailable=runtimeCapture!==watchdogTimeout&&
        report.runtime_diagnostics?.status==='installed';
      report.campaign_wall_bound.evidence_status=!hadPage?'no-page-to-capture':
        captureTimedOut?'capture-timeout':diagnosticCaptureAvailable?'retained-before-close':'unavailable';
      if(captureTimedOut)report.campaign_wall_bound.evidence_timeout_ms=1500;
    }
    if(page&&!page.isClosed())await bounded(page.close());
    if(browserContext)await bounded(browserContext.close());
    else if(browser)await bounded(browser.close());
  })().catch(error=>{
    if(report.campaign_wall_bound){
      report.campaign_wall_bound.evidence_status='capture-error';
      report.campaign_wall_bound.evidence_error=error.message;
    }
  });
}
function checkCampaignWallBound(){
  if(campaignDeadline!==null&&(campaignWallBoundExceeded||Date.now()>=campaignDeadline)){
    triggerCampaignWallBound();
    throw campaignWallBoundError();
  }
}
function armCampaignWallWatchdog(){
  if(campaignDeadline===null)return null;
  return setTimeout(()=>triggerCampaignWallBound(),Math.max(1,campaignDeadline-Date.now()));
}
function isTimingPause(state){
  return typeof state?.status==='string'&&state.status.startsWith('Paused after a timing disruption');
}
async function checkTimingPause(label,state){
  if(stopOnTimingPause&&isTimingPause(state))await failOnTimingPause(label,state);
  return state;
}
function timingPauseSourceFrame(state){
  const match=String(state?.diagnostics||'').match(/(?:source frame|source cursor): (\d+)/i);
  return match?Number(match[1]):state?.p0?.frame??null;
}
async function failOnTimingPause(label,state){
  await retainRuntimeDiagnosticsCapture();
  const receipt={schema:'melee-web-timing-pause-receipt-v1',label,
    match:activeMatchIndex,phase:state?.phase??null,running:state?.running??null,
    status:isTimingPause(state)?state.status:null,source_frame:timingPauseSourceFrame(state),
    native_message:state?.native_message??null,dom_status:state?.dom_status??state?.status??null,
    runtime_diagnostics:report.runtime_diagnostics,
    diagnostics:typeof state?.diagnostics==='string'?state.diagnostics:null,
    observed_at:new Date().toISOString()};
  report.timing_pause_receipts.push(receipt);
  const suffix=activeMatchIndex===null?'unknown':String(activeMatchIndex);
  await fs.writeFile(path.join(output,`timing-pause-${suffix}.json`),JSON.stringify(receipt,null,2)+'\n')
    .catch(error=>{report.timing_pause_receipt_write_error=error.message;});
  await screenshot(`timing-pause-${suffix}`).catch(error=>{
    report.timing_pause_screenshot_error=error.message;
  });
  throw Error(`${label}: unexpected timing pause at source frame ${receipt.source_frame??'unknown'}`);
}
async function rethrowWithTimingPause(label,error){
  if(stopOnTimingPause){
    const state=await diagnostic().catch(()=>null);
    if(isTimingPause(state))await failOnTimingPause(label,state);
  }
  throw error;
}
const matchPlayerSlots=MATCH_PLAYER_SLOTS;
function validMatchPlayer(value){
  return value&&Number.isInteger(value.fighterKind)&&Number.isInteger(value.motion)&&
    Number.isInteger(value.groundAir)&&Number.isInteger(value.frame)&&Number.isFinite(value.x)&&
    Number.isFinite(value.y);
}
function matchReadiness(state){
  const missingPlayers=MATCH_PLAYER_SLOTS.filter(slot=>!validMatchPlayer(state?.[slot]));
  return {phase:Number.isInteger(state?.phase)?state.phase:null,ready:state?.match?.ready===true,
    observer_error:state?.match?.observer_error===true,missing_players:missingPlayers,
    valid_players:MATCH_PLAYER_SLOTS.filter(slot=>validMatchPlayer(state?.[slot]))};
}
function matchReadinessFailureReason(state,sawPhase7){
  const readiness=matchReadiness(state);
  if(state?.error)return 'runtime-error';
  if(state?.assetFatal)return 'asset-fatal';
  if(state?.nativeCommandError)return 'native-command-error';
  if(readiness.observer_error)return 'match-observer-error';
  if(sawPhase7&&state?.phase!==7)return 'phase-left-before-ready';
  return null;
}
function runReadinessPreflight(){
  const cases=[
    {name:'preparation',state:{phase:3,status:'preparing'},sawPhase7:false,expected:null},
    {name:'runtime-error-outside-match',state:{phase:3,error:true},sawPhase7:false,expected:'runtime-error'},
    {name:'phase-seven-left-before-ready',state:{phase:3,status:'loading'},sawPhase7:true,expected:'phase-left-before-ready'},
    {name:'observer-error',state:{phase:7,match:{observer_error:true}},sawPhase7:true,expected:'match-observer-error'},
  ];
  const observed=cases.map(item=>({name:item.name,phase:item.state.phase,
    reason:matchReadinessFailureReason(item.state,item.sawPhase7)}));
  for(const [index,item] of cases.entries())
    assert.equal(observed[index].reason,item.expected,`readiness preflight ${item.name}`);
  assert.equal(matchReadinessFailureReason(cases[0].state,cases[0].sawPhase7),null,
    'preparation remains pending until its bounded timeout receipt');
  return {result:'pass',observed,timeout_phase:cases[0].state.phase,
    timeout_reason:matchReadinessFailureReason(cases[0].state,cases[0].sawPhase7)??'timeout'};
}
async function failOnMatchReadiness(label,state,reason){
  const readiness=matchReadiness(state);
  const receipt={schema:'melee-web-match-readiness-failure-v1',label,match_index:activeMatchIndex,
    reason,readiness,match_observation:state?.match??null,p0:state?.p0??null,p1:state?.p1??null,
    p2:state?.p2??null,p3:state?.p3??null,status:state?.status??null,
    error:state?.error??null,asset_fatal:state?.assetFatal??null,
    diagnostics:typeof state?.diagnostics==='string'?state.diagnostics:null,
    observed_at:new Date().toISOString()};
  report.match_readiness_failures.push(receipt);
  const suffix=activeMatchIndex===null?'unknown':String(activeMatchIndex);
  await fs.writeFile(path.join(output,`match-readiness-${suffix}.json`),
    JSON.stringify(receipt,null,2)+'\n').catch(error=>{report.match_readiness_receipt_write_error=error.message;});
  await screenshot(`match-readiness-${suffix}`).catch(error=>{
    report.match_readiness_screenshot_error=error.message;
  });
  throw Error(`${label}: source match readiness unavailable (${reason}; missing=${readiness.missing_players.join(',')||'none'})`);
}
async function writeProgress(label){
  const state=await diagnostic();
  const cache=state.cache;
  const evidence=report.browser_context?.cache_evidence;
  if(evidence&&cache){
    evidence.observed=true;
    evidence.last={state:cache.state,populated:cache.populated,clears:cache.clears,
      file_bytes:cache.file_bytes,mounted:cache.mounted,dirty:cache.dirty};
    if(cache.populated===true&&Number(cache.file_bytes)>0){
      evidence.populated_observed=true;
      if(report.browser_context.requested_cache==='warm'&&Number(cache.clears)===0){
        evidence.restore_observed=true;evidence.status='observed-populated';
      }else if(evidence.status==='unavailable')evidence.status='observed-after-clear';
    }else if(evidence.status==='unavailable')evidence.status='observed-not-populated';
  }
  report.source_progress.push({label,phase:state.phase,status:state.status,
    error:state.error,p0:state.p0,p1:state.p1,p2:state.p2,p3:state.p3,match:state.match,
    css:state.css,memory:state.memory,fighterParts:state.fighterParts,
    fighterPartsStep:state.fighterPartsStep,fighterPartsOwner:state.fighterPartsOwner,
    kirbyHatLoad:state.kirbyHatLoad,assetFatal:state.assetFatal,cache:state.cache});
  await fs.writeFile(path.join(output,'progress.json'),JSON.stringify({
    label,phase:state.phase,status:state.status,error:state.error,
    p0:state.p0,p1:state.p1,p2:state.p2,p3:state.p3,match:state.match,
    assetFatal:state.assetFatal,cache:state.cache,at:new Date().toISOString()},null,2)+'\n');
  if(state.error)throw Error(state.error);
  return state;
}
async function unloadAfterNaturalResultsCss(){
  const before=await diagnostic();
  assert.equal(before.phase,1,'Native cache teardown requires the original CSS owner after natural Results return');
  const receipt={schema:'melee-web-runtime-cache-teardown-v1',status:'pending',
    owner:'browser_driver.unload → web/melee-runtime.mjs unloadAndSave',
    before:{phase:before.phase,running:before.running,cache:before.cache},
    after:null,native_cache_idle:null,module_save_runtime_cache_owner:'web/melee-runtime.mjs unloadAndSave',
    observed_at:new Date().toISOString()};
  report.browser_context.cache_teardown=receipt;
  const persistReceipt=()=>fs.writeFile(path.join(output,'cache-teardown.json'),
    JSON.stringify(receipt,null,2)+'\n').catch(error=>{receipt.write_error=error.message;});
  try{
    await driver.unload();
    const after=await diagnostic();
    const nativeCacheIdle=await page.evaluate(()=>typeof Module._melee_web_native_menu_cache_idle==='function'?
      Module._melee_web_native_menu_cache_idle():null);
    receipt.after={phase:after.phase,running:after.running,cache:after.cache};
    receipt.native_cache_idle=nativeCacheIdle;
    receipt.cache_save_count={before:before.cache?.saves??null,after:after.cache?.saves??null};
    receipt.save_observed=Number.isFinite(receipt.cache_save_count.before)&&
      Number.isFinite(receipt.cache_save_count.after)&&
      receipt.cache_save_count.after>receipt.cache_save_count.before;
    receipt.status=nativeCacheIdle===1?'pass':'cache-write-failed';
    if(nativeCacheIdle!==1)
      throw Error(`Native renderer cache was not idle after unload: ${nativeCacheIdle}`);
    await persistReceipt();
  }catch(error){
    if(receipt.status==='pending')receipt.status='failed';
    receipt.error=error.message;
    await retainRuntimeDiagnosticsCapture();
    await persistReceipt();
    throw error;
  }
}
async function waitFor(label,predicate,timeoutMs=30000){
  const deadline=Date.now()+timeoutMs;
  while(Date.now()<deadline){
    const state=await diagnostic();
    await checkTimingPause(label,state);
    if(state.error)throw Error(`${label}: ${state.error}`);
    if(predicate(state))return state;
    await page.waitForTimeout(50);
  }
  const state=await diagnostic();
  await checkTimingPause(label,state);
  throw Error(`${label} timed out after ${timeoutMs} ms: ${JSON.stringify({phase:state.phase,status:state.status,p0:state.p0,p1:state.p1,match:state.match})}`);
}
async function waitForMatchReady(label,timeoutMs=60000){
  const deadline=Date.now()+timeoutMs;
  let sawPhase7=false;
  while(Date.now()<deadline){
    const state=await diagnostic();
    await checkTimingPause(label,state);
    const readiness=matchReadiness(state);
    const failureReason=matchReadinessFailureReason(state,sawPhase7);
    if(failureReason)await failOnMatchReadiness(label,state,failureReason);
    if(state.phase===7){
      sawPhase7=true;
      if(readiness.ready&&readiness.missing_players.length===0)return state;
    }
    await page.waitForTimeout(50);
  }
  const state=await diagnostic();
  await checkTimingPause(label,state);
  const failureReason=matchReadinessFailureReason(state,sawPhase7)??'timeout';
  await failOnMatchReadiness(label,state,failureReason);
}
async function pad(port,buttons=0,stickX=0,stickY=0,duration=1,label='input'){
  assert(port===0||port===1,'the live diagnostic PAD route supports only P1/P2');
  assert(Number.isInteger(duration)&&duration>=1&&duration<=120);
  if(stopOnTimingPause)await checkTimingPause(`${label} before PAD`,await diagnostic());
  try{
    await page.evaluate(({port,buttons,stickX,stickY,duration})=>
      window.menuDiagnosticPad(port,buttons,stickX,stickY,duration),
      {port,buttons,stickX,stickY,duration});
  }catch(error){
    await rethrowWithTimingPause(`${label} PAD`,error);
  }
  report.pad_sample_count++;
  if(report.controller_inputs.length<4000)
    report.controller_inputs.push({port,buttons,stickX,stickY,duration,label});
  // Let the source consume the queued sample and resume ordinary neutral PAD.
  await page.waitForTimeout(Math.max(40,duration*18));
  if(stopOnTimingPause)await checkTimingPause(`${label} after PAD`,await diagnostic());
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
  await checkTimingPause('CSS observation',state);
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
async function setCpuLevel(door,targetLevel){
  let setup=await css(),g=row(setup.geometry,door,12);
  if(row(setup.doors,door,10)[5]===targetLevel)return;
  await move(0,g[8],g[9],`grab-CPU-slider-${door}`);
  await tap(0,buttonA,`grab-CPU-slider-${door}`);
  await waitFor(`CPU slider ${door} grabbed`,s=>row(s.css?.cursors||[],0,4)[2]===door+4,5000);
  for(let sample=0;sample<24;sample++){
    setup=await css();
    const currentLevel=row(setup.doors,door,10)[5];
    if(currentLevel===targetLevel)break;
    const direction=targetLevel>currentLevel?80:-80;
    await pad(0,0,direction,0,8,`set-CPU-${door}-level-${targetLevel}`);
  }
  await pad(0,0,0,0,2,`CPU-${door}-slider-neutral`);
  setup=await css();
  assert.equal(row(setup.doors,door,10)[5],targetLevel,
    `CPU ${door} level must be source-confirmed ${targetLevel}`);
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
    assert.equal(players[door].cpu_level,cpuLevels[door],
      `${label}: door ${door} CPU level must match the requested profile`);
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
  // Source CSS CPU conversion can reset an existing human fighter choice.
  for(let door=0;door<4;door++)await selectCpuCharacter(door,expected[door]);
  for(let door=0;door<4;door++)await setCpuLevel(door,cpuLevels[door]);
  return verifyRoster(expected,`${cpuProfileDescription} ${stage.name} roster before SSS`);
}
const padButtonRight=0x0002,padButtonDown=0x0004,padButtonR=0x0020,padButtonL=0x0040;
const MAIN_SCENE=4,MAIN_MENU_KIND=0,VS_MENU_KIND=2,RULES_MENU_KIND=13,RULES_PLUS_MENU_KIND=15;
const sourceObserve=()=>page.evaluate(()=>JSON.parse(
  Module.UTF8ToString(Module._melee_web_native_menu_source_observe())));
async function waitForSourcePadReady(label,expectedPhases=[11]){
  const deadline=Date.now()+20000;
  let state=null,ownerCanPause=false,nativeMessage='';
  while(Date.now()<deadline){
    state=await diagnostic();
    if(state.error)throw Error(`${label}: ${state.error}`);
    if(state.nativeCommandError)throw Error(`${label}: ${state.nativeCommandError}`);
    nativeMessage=await page.evaluate(()=>
      Module.UTF8ToString(Module._melee_web_native_menu_message()));
    if(isTimingPause({status:nativeMessage}))
      await failOnTimingPause(label,{...state,status:nativeMessage,
        native_message:nativeMessage,dom_status:state.status});
    ownerCanPause=await page.evaluate(()=>{
      const button=document.querySelector('#pause');
      return Boolean(button&&!button.disabled);
    });
    if(state.running===1&&expectedPhases.includes(state.phase)&&ownerCanPause&&
       state.diagnostics.includes('raw PAD: none')){
      report.source_progress.push({label,phase:state.phase,running:state.running,
        status:state.status,native_message:nativeMessage,diagnostics:state.diagnostics,
        owner_can_pause:ownerCanPause});
      return state;
    }
    await page.waitForTimeout(20);
  }
  throw Error(`${label} timed out waiting for an active source PAD owner: ${JSON.stringify({
    phase:state?.phase,running:state?.running,status:state?.status,
    native_message:nativeMessage,diagnostics:state?.diagnostics,
    owner_can_pause:ownerCanPause,error:state?.error})}`);
}
async function waitForSource(label,predicate,timeoutMs=15000){
  const deadline=Date.now()+timeoutMs;
  let observation=null;
  while(Date.now()<deadline){
    const state=await diagnostic();
    await checkTimingPause(label,state);
    if(state.error)throw Error(`${label}: ${state.error}`);
    observation=await sourceObserve();
    if(observation?.source?.valid&&predicate(observation.source))return observation;
    await page.waitForTimeout(50);
  }
  throw Error(`${label} timed out: ${JSON.stringify(observation?.source??observation)}`);
}
// Original menus consume trigger edges; one source tick down and two neutral
// ticks give each navigation input one edge without auto-repeat.
async function sourceMenuTap(buttons,label){
  const releasePhase=buttons===buttonStart?[1]:[11];
  await waitForSourcePadReady(`${label}: owner ready before press`,[11]);
  await pad(0,buttons,0,0,1,label);
  await waitForSourcePadReady(`${label}: owner ready before release`,releasePhase);
  await pad(0,0,0,0,2,`${label}:release`);
  await waitForSourcePadReady(`${label}: owner ready after release`,releasePhase);
}
async function moveSourceMenu(menuKind,total,target,label){
  for(let step=0;step<total*2;step++){
    const observed=await waitForSource(`${label} cursor`,source=>source.scene===MAIN_SCENE&&
      source.menu_kind===menuKind);
    if(observed.source.hovered_selection===target)return observed;
    await sourceMenuTap(padButtonDown,`${label} down`);
  }
  throw Error(`${label}: original menu ${menuKind} cursor did not reach ${target}`);
}
async function enableFriendlyFire(){
  // CSS L+R+Start follows the original mnCharSel_Scene_OnFrame parent route
  // to Main. Rules Plus row 1 is GameRules.friendly_fire (fn_8023201C), and
  // Rules Plus Start commits before mn_80229860(GM_VS) returns to CSS.
  const fullPad=(buttons,triggerLeft,triggerRight,duration,label)=>page.evaluate(args=>
    Module._melee_web_native_menu_pad_sample_full(...args),
    [0,buttons,0,0,0,0,triggerLeft,triggerRight,duration]).then(async accepted=>{
      assert.equal(accepted,1,`${label}: source PAD sample rejected`);
      report.pad_sample_count++;
      report.controller_inputs.push({port:0,buttons,triggerLeft,triggerRight,duration,label});
      await page.waitForTimeout(Math.max(40,duration*18));
    });
  await page.waitForTimeout(800);
  await fullPad(padButtonL|padButtonR|buttonStart,255,255,4,'CSS L+R+Start to original Main');
  const mainReady=await waitFor('active original Main owner before neutral PAD release',state=>
    state.phase===11&&state.running===1&&state.status.startsWith('Original main menu')&&
    state.diagnostics.includes('raw PAD: none'),60000);
  report.source_progress.push({label:'original Main active and queued CSS PAD drained before neutral release',
    phase:mainReady.phase,running:mainReady.running,status:mainReady.status,
    diagnostics:mainReady.diagnostics});
  await fullPad(0,0,0,2,'CSS L+R+Start release');
  await waitForSource('original Main root',source=>source.scene===MAIN_SCENE&&
    source.menu_kind===MAIN_MENU_KIND,60000);
  await screenshot('friendly-fire-main');
  await moveSourceMenu(MAIN_MENU_KIND,5,1,'Main VS');
  await sourceMenuTap(buttonA,'Main VS A');
  await waitForSource('original VS menu',source=>source.menu_kind===VS_MENU_KIND);
  await moveSourceMenu(VS_MENU_KIND,5,3,'VS Rules');
  await sourceMenuTap(buttonA,'VS Rules A');
  await waitForSource('original Rules',source=>source.menu_kind===RULES_MENU_KIND);
  await moveSourceMenu(RULES_MENU_KIND,7,6,'Rules Plus row');
  await sourceMenuTap(buttonA,'Rules Plus A');
  await waitForSource('original Rules Plus',source=>source.menu_kind===RULES_PLUS_MENU_KIND&&
    source.hovered_selection===0);
  await moveSourceMenu(RULES_PLUS_MENU_KIND,5,1,'Rules Plus friendly fire row');
  const before=await sourceObserve();
  assert.equal(before.source.rules.friendly_fire,0,'Fresh Everything profile starts with friendly fire off');
  await sourceMenuTap(padButtonRight,'Rules Plus friendly fire right');
  const edited=await waitForSource('Rules Plus friendly fire on',source=>
    source.menu_kind===RULES_PLUS_MENU_KIND&&source.hovered_selection===1&&
    source.confirmed_selection===1);
  report.phases.push({label:'source Rules Plus friendly fire row set on',source:edited.source});
  await screenshot('friendly-fire-rules-plus-on');
  await sourceMenuTap(buttonStart,'Rules Plus Start commits to GM_VS');
  await waitFor('original CSS after Rules Plus Start',s=>s.phase===1&&s.css,60000);
  const css=await waitForSource('CSS retains committed friendly fire',source=>
    source.scene===1&&source.rules.friendly_fire===1);
  report.phases.push({label:'CSS after source Rules Plus friendly fire commit',source:css.source});
}
async function cssTeams(label){
  const observed=await waitForSource(label,source=>source.scene===1&&source.css_setup?.valid);
  const setup=observed.source.css_setup;
  assert(Array.isArray(setup.door_teams)&&setup.door_teams.length===4,
    `${label}: four-door CSS team observation unavailable`);
  return setup;
}
async function enableCssTeams(){
  const isTeams=async label=>(await waitForSource(label,source=>source.scene===1&&
    source.css_setup?.valid)).source.css_setup.is_teams;
  if(await isTeams('CSS Teams before toggle')===1)return;
  await page.waitForTimeout(800);
  // mnCharSel_CursorThink: PAD A toggles is_teams while the cursor is at
  // x < -25.5 and y > 22 (the original Teams control).
  await move(0,-28,23.3,'CSS-Teams-control');
  await tap(0,buttonA,'CSS Teams toggle');
  const enabled=await waitForSource('CSS Teams enabled',source=>source.scene===1&&
    source.css_setup?.valid&&source.css_setup.is_teams===1,4000);
  report.phases.push({label:'original CSS Teams control enabled',css_setup:enabled.source.css_setup});
  await screenshot('css-teams-enabled');
}
async function assignCssTeams(targets){
  for(let door=0;door<4;door++){
    for(let attempt=0;attempt<4;attempt++){
      const setup=await cssTeams(`door ${door} team before cycle`);
      if(setup.door_teams[door]===targets[door])break;
      const before=setup.door_teams[door];
      const g=row((await css()).geometry,door,12);
      assert(Number.isFinite(g[6])&&Number.isFinite(g[7])&&g[7]>g[6],
        `Original door ${door} team-box bounds are invalid: ${JSON.stringify(g)}`);
      // cycleTeam accepts -5.8 < y < -1.0 inside the door's authored box.
      await move(0,(g[6]+g[7])/2,-3.4,`team-box-door-${door}`);
      await tap(0,buttonA,`cycle-team-door-${door}`);
      await waitForSource(`door ${door} team advanced`,source=>source.scene===1&&
        source.css_setup?.door_teams?.[door]===(before+1)%3,4000);
    }
  }
  const setup=await cssTeams('CSS teams configured');
  assert.equal(setup.is_teams,1,'Teams must remain enabled after team colour input');
  assert.deepEqual(setup.door_teams,targets,'Original CSS door teams must match the requested setup');
  report.phases.push({label:'original CSS team colours configured',css_setup:setup});
  await screenshot('css-teams-configured');
  return setup;
}
function assertTeamMatchEntry(entry,matchIndex){
  const rules=entry.match?.rules;
  assert(rules,`Team match ${matchIndex} entry observation is unavailable`);
  assert.equal(rules.is_teams,1,'The live source StartMeleeData must keep the CSS Teams flag');
  assert.deepEqual(rules.door_teams,teams,
    'The live source StartMeleeData must keep every original CSS door team');
  assert.equal(rules.friendly_fire,friendlyFire?1:0,
    'The live source StartMeleeData must keep the Rules Plus friendly-fire choice');
  report.phases.push({label:`match ${matchIndex} source Team Battle entry`,rules});
}
function assertTeamTerminal(match,matchIndex){
  const terminal=match?.terminal;
  assert(terminal&&Array.isArray(terminal.winners),
    `Team match ${matchIndex} terminal source MatchEnd is unavailable`);
  // gm_GetTeamBattleOutcome returns OUTCOME_TEAM_ELIMINATION (3) once at most
  // one team retains stocks; MatchEnd ranks the remaining team as winners.
  assert.equal(terminal.outcome,3,'A natural Team Battle must end with source OUTCOME_TEAM_ELIMINATION');
  assert(terminal.winners.length>=1&&terminal.winners.every(winner=>
    Number.isInteger(winner)&&winner>=0&&winner<4),
    `Team match ${matchIndex} winners are outside the four doors: ${JSON.stringify(terminal.winners)}`);
  const winnerTeams=[...new Set(terminal.winners.map(winner=>teams[winner]))];
  assert.equal(winnerTeams.length,1,'Every source Team Battle winner must belong to one team');
  return {outcome:terminal.outcome,winners:terminal.winners,winning_team:winnerTeams[0],
    winning_team_doors:teams.flatMap((team,door)=>team===winnerTeams[0]?[door]:[])};
}
async function assertTeamCssReturn(matchIndex){
  const setup=await cssTeams(`CSS team setup after match ${matchIndex} Results`);
  assert.equal(setup.is_teams,1,'Results return must keep the original CSS Teams flag');
  assert.deepEqual(setup.door_teams,teams,'Results return must keep every original CSS door team');
  const observed=await sourceObserve();
  assert.equal(observed.source.rules.friendly_fire,friendlyFire?1:0,
    'Results return must keep the Rules Plus friendly-fire choice');
  report.phases.push({label:`CSS after match ${matchIndex} Team Battle Results`,css_setup:setup,
    friendly_fire:observed.source.rules.friendly_fire});
}
async function chooseStage(){
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
    await checkTimingPause('stage selection',state);
    if(state.error)throw Error(`SSS source error: ${state.error}`);
    if(state.phase!==3)throw Error(`SSS exited before stage confirmation; phase=${state.phase}`);
    const result=await page.evaluate(stageId=>Module._melee_web_native_menu_drive_stage(stageId),stage.sourceId);
    if(result===0)throw Error('Source SSS stage-driver failed: '+await page.evaluate(()=>Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())));
    if(result===2)break;
    await page.waitForTimeout(45);
  }
  const selectedStage=await page.evaluate(stageId=>window.menuObserveStage(stageId),stage.sourceId);
  assert(selectedStage&&selectedStage.ids[1]===stage.sourceId,
    `${stage.name} was not highlighted by the live source SSS`);
  report.phases.push({label:`source SSS highlighted ${stage.name}`,stage:selectedStage});
  await screenshot(`match-${report.matches.length+1}-${stage.slug}-highlighted`);
  await tap(0,buttonA,`confirm-${stage.name}`);
  const ready=await waitForMatchReady('original four-player match entry',stageSetupOnly?30000:60000);
  if(stageSetupOnly){
    report.stage_setup={result:'pass',stage_kind:stage.id,stage_source_id:stage.sourceId,
      selected_stage:selectedStage,phase:ready.phase,running:ready.running,
      readiness:matchReadiness(ready),source_diagnostics:ready.diagnostics,
      screenshot:'match-1-'+stage.slug+'-highlighted'};
  }
  return ready;
}
async function runMatch(matchIndex,expected){
  activeMatchIndex=matchIndex;
  const inputEventStart=await page.evaluate(()=>
    window.__meleeWebResultsInputEvents?.length||0);
  let scheduledSourceTickInputs=[];
  if(keyboardSourceTickConfirmMode){
    const frames=[192,202,363,373,resultsConfirmFrame,resultsConfirmFrame+10];
    const armed=await page.evaluate(scheduleResultsSourceFramePauses,{frames});
    assert.equal(armed.status,'scheduled',
      'The exact keyboard source boundaries must be armed before SSS');
    report.results_source_frame_pause_schedules??=[];
    report.results_source_frame_pause_schedules.push({match:matchIndex,
      method:'development-only pause immediately before Results source samples; no PAD or game-state injection',
      pauses:armed.pauses});
  }
  if(sourceTickThreePulse){
    // Pre-queue through the source PAD boundary while original CSS is idle.
    // The Results source loop owns exact consumption; page polling/screenshot
    // callbacks cannot move these targets or inject a late sample.
    const events=[180,360,resultsConfirmFrame].map(targetFrame=>({targetFrame,port:0,
      button:buttonStart,duration:10}));
    const armed=await page.evaluate(scheduleResultsP1StartSequence,{events});
    assert.equal(armed.status,'scheduled','The source Results PAD schedule must be armed before SSS');
    scheduledSourceTickInputs=armed.events;
    report.results_source_pad_schedules??=[];
    report.results_source_pad_schedules.push({match:matchIndex,
      method:'development source-boundary queue before SSS/match construction',
      events:scheduledSourceTickInputs});
  }
  await chooseStage();
  startControlledContention();
  const entry=await writeProgress(`match-${matchIndex}-entry`);
  if(teams)assertTeamMatchEntry(entry,matchIndex);
  await screenshot(`match-${matchIndex}-entry`);
  const checkpoints=[600,2400,6000,10000,14000,18000,24000];
  let next=0,deadline=Date.now()+12*60*1000,stalledSince=0,terminalTransitionRecorded=false;
  if(campaignDeadline!==null)deadline=Math.min(deadline,campaignDeadline);
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
      if(stopOnTimingPause)await failOnTimingPause(`match ${matchIndex}`,state);
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
  await stopControlledContention();
  if(stopOnTimingPause&&isTimingPause(state))await failOnTimingPause(`match ${matchIndex}`,state);
  if(state.phase!==8&&state.phase!==9)
    throw Error(`Match ${matchIndex} did not naturally reach Results/Prize before the ${campaignRequested?wallBoundSeconds*1000:12*60*1000} ms bound: ${JSON.stringify({phase:state.phase,p0:state.p0,p1:state.p1,status:state.status})}`);
  await retainResultsEntry(`match-${matchIndex}-results-entry`);
  const result={match:matchIndex,entered_results_phase:state.phase,
    source_diagnostics:state.diagnostics,terminal_match:state.match,
    memory_at_results:state.memory,players:expected.map(({name,kind})=>({name,kind,cpu:9,stocks:4}))};
  if(teams)result.team_battle=assertTeamTerminal(state.match,matchIndex);
  report.matches.push(result);
  report.phases.push({label:`match ${matchIndex} naturally reached Results/Prize`,phase:state.phase,match:state.match});
  // Ordinary Start input advances authored Results/Prize routing. Stop only
  // when the actual source menu returns to CSS; never force a scene reset.
  const resumeResultsIfPaused=async state=>{
    if(!state.status.startsWith('Paused after a timing disruption'))return state;
    if(stopOnTimingPause)await failOnTimingPause(`Results ${matchIndex}`,state);
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
  const waitForResultsPauseAtSourceFrame=async(target,label)=>{
    const waitDeadline=Date.now()+60000;
    while(Date.now()<waitDeadline){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(label+': '+state.error);
      if(state.phase===1)throw Error(label+': Results returned to CSS before its scheduled pause');
      assert(state.phase===8||state.phase===9,
        label+': unexpected Results phase '+state.phase);
      const frame=readResultsFrame(state);
      const expectedStatus='Paused at scheduled Results source frame '+target+'.';
      if(state.status===expectedStatus){
        assert.equal(state.running,0,label+': source stepping must be paused');
        assert.equal(frame,target,label+': source pause must retain the exact cursor');
        return state;
      }
      assert(frame<=target,label+': source cursor passed scheduled pause '+target+' at '+frame);
      await page.waitForTimeout(8);
    }
    throw Error(label+': source loop did not pause before Results sample '+target);
  };
  const captureThreePulsePrefix=async()=>{
    state=await waitForResultsFrame(560,
      `Results ${matchIndex} historical three-pulse source checkpoint`);
    assert(state.phase===8||state.phase===9,
      `Results ${matchIndex} left Results/Prize before source frame 560`);
    await writeProgress(`match-${matchIndex}-three-pulse-prefix-source-frame-560`);
    await screenshot(`match-${matchIndex}-three-pulse-prefix-source-frame-560`);
    await retainResultsInputEvents();
    const trace=await readResultsSourcePadTrace();
    const prefixSamples=trace.samples.filter(row=>row.source_frame<=560);
    const prefixTrace={...trace,attempts:prefixSamples.length,
      retained:prefixSamples.length,samples:prefixSamples};
    const summary=summarizeResultsPadTrace(prefixTrace);
    const sample560=prefixSamples.find(row=>row.source_frame===560);
    const intents=report.controller_inputs.filter(row=>
      row.device==='keyboard-to-source-PAD'&&
      row.label?.startsWith(`results-${matchIndex}-continue-`));
    const keyboardEvents=(report.results_input_events||[]).slice(inputEventStart).filter(row=>
      row.phase===8||row.phase===9);
    const keydowns=keyboardEvents.filter(row=>row.kind==='keydown');
    const keyups=keyboardEvents.filter(row=>row.kind==='keyup');
    const consumed=findConsumedResultsStartKeyboardAttempt(
      keydowns,keyups,summary.p1_start_runs,-1);
    const checkpoint={match:matchIndex,status:'captured-through-source-frame-560',
      target_source_frame:560,observed_phase:state.phase,
      observed_source_frame:readResultsFrame(state),
      input_intentions:intents.map(({device,key,hold_ms,release_ms,label})=>
        ({device,key,hold_ms,release_ms,label})),
      keyboard_events:keyboardEvents.map(({kind,key,repeat,isTrusted,nativeSourceSteps,
        resultsSourceFrameAtEvent})=>({kind,key,repeat,isTrusted,nativeSourceSteps,
          resultsSourceFrameAtEvent})),
      consumed_start_attempts:consumed.attempts,source_pad_summary:summary,
      source_state_after_tick_560:sample560?.results_state_after_tick??null,
      pads_at_source_frame_560:sample560?.pads??null,camera_entry:trace.camera_entry};
    report.results_three_pulse_prefixes.push(checkpoint);
    result.results_three_pulse_prefix=checkpoint;
    assert.equal(intents.length,3,
      'Exactly three ordinary keyboard intentions must precede the cursor-560 checkpoint');
    assert.equal(keyboardEvents.length,6,
      'The prefix must retain exactly three ordinary Enter down/up pairs');
    assert.deepEqual(keyboardEvents.map(row=>row.kind),
      ['keydown','keyup','keydown','keyup','keydown','keyup'],
      'The historical input prefix must preserve distinct Enter press/release edges');
    assert(keyboardEvents.every(row=>row.key==='Enter'&&row.isTrusted&&!row.repeat&&
      Number.isInteger(row.resultsSourceFrameAtEvent)&&row.resultsSourceFrameAtEvent<=560),
      'Every ordinary Enter edge must retain a trusted source-frame bracket through cursor 560');
    assert(intents.every(row=>row.hold_ms===160&&row.release_ms===120),
      'The prefix must preserve the historical 160ms hold/120ms release requests');
    assert(!trace.overflow&&sample560&&summary.tick_failed.length===0,
      'The Results trace must retain a returned source sample at cursor 560 without overflow');
    assert.deepEqual(summary.port_error_values,[[0],[0],[-1],[-1]],
      'The prefix must preserve the observed P1/P2-connected, CPU-P3/P4-disconnected profile');
    checkpoint.status='pass';
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
  const queueSourceStartAtExactTick=async(target,label)=>{
    assert(scheduledSourceTickInputs.some(event=>event.targetFrame===target),
      `${label}: source-tick event was not pre-queued before gameplay`);
    const state=await waitForResultsFrame(target+10,`${label} consumed ten-tick PAD hold`);
    assert(state.phase===8||state.phase===9,
      `${label}: Results left before the pre-queued source sample was observed`);
    return target;
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
        const gate=keyboardPrefixGatedMode?
          assertResultsCpuPagesAfterP1KeyboardPrefix(trace,[192,363],{
            expectedPortErrors:keyboardPortErrors,
            expectedDisconnectedCpuSlots:keyboardAutoPageSlots}):
          assertResultsCpuPagesAfterInitialP1Keyboard(trace,targetFrame,{
            expectedPortErrors:keyboardPortErrors,
            expectedDisconnectedCpuSlots:keyboardAutoPageSlots});
        if(gate)return {natural_css:false,...gate};
        lastTraceFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame??diagnosticFrame;
      }
      await page.waitForTimeout(16);
    }
    throw Error(`${label}: disconnected CPU pages did not auto-advance after the consumed initial P1 Enter (last scheduled trigger lower bound ${targetFrame})`);
  };
  const waitForCpuPagesBeforeSourceFrame=async(targetFrame,label)=>{
    const waitDeadline=Date.now()+60000;
    let lastTraceFrame=-1;
    while(Date.now()<waitDeadline){
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`${label}: ${state.error}`);
      if(state.phase===1)return {natural_css:true,source_frame:null,transitions:[]};
      assert(state.phase===8||state.phase===9,`${label}: unexpected source phase ${state.phase}`);
      const diagnosticFrame=readResultsFrame(state);
      // Refresh once the confirmation boundary is near even if the ordinary
      // 12-frame trace stride has not elapsed; otherwise a stale sample can
      // make a real pre-confirmation CPU transition look late.
      if(diagnosticFrame-lastTraceFrame>=12||diagnosticFrame>=targetFrame-1){
        const trace=await readResultsSourcePadTrace();
        assert(!trace.overflow,`${label}: Results source trace overflowed`);
        const summary=summarizeResultsPadTrace(trace);
        assert.deepEqual(summary.tick_failed,[],`${label}: source tick failed before the page gate`);
        assert.deepEqual(summary.port_error_values,[[0],[0],[-1],[-1]],
          `${label}: Results ports changed before the auto-page gate`);
        const transitions=summary.results_page_transitions.filter(row=>row.from===0&&row.to===1);
        if(transitions.length){
          assert.deepEqual(transitions.map(row=>row.slot),[2,3],
            `${label}: only disconnected CPU pages may advance before confirmation`);
          assert(transitions.every(row=>row.phase===3&&row.stats_phase===2&&
            row.source_frame<targetFrame),
            `${label}: CPU pages must auto-advance in active statistics before the target tick`);
          const sourceFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame;
          return {natural_css:false,source_frame:sourceFrame,transitions,
            input_runs:summary.p1_start_runs,connectedness:summary.port_error_values};
        }
        lastTraceFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame??diagnosticFrame;
      }
      if(diagnosticFrame>=targetFrame)
        throw Error(`${label}: confirmation boundary ${targetFrame} arrived before both CPU pages auto-advanced`);
      await page.waitForTimeout(8);
    }
    throw Error(`${label}: both disconnected CPU pages did not auto-advance before source tick ${targetFrame}`);
  };
  // Source-tick-three-pulse waits inside the page's RAF queue so its first
  // edge can be dispatched at cursor 180; other modes retain the ordinary
  // source-observed Results advance gate.
  if(sourceTickThreePulse){
    state=await diagnostic();
    if(state.error)throw Error(`Results ${matchIndex} entry: ${state.error}`);
    assert(state.phase===8||state.phase===9,'Results advanced without continuation input');
    result.rendered_results_source_frame=readResultsFrame(state);
  }else{
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
  }
  if(sourceTickMode){
    report.phases.push(sourceTickThreePulse?
      `Results ${matchIndex}: P1-only Start holds at Results ticks 180/360/${resultsConfirmFrame}; keyboard path not used`:
      `Results ${matchIndex}: P1-only ten-source-tick pulses; keyboard path not used`);
    const starts=[];
    const first=sourceTickThreePulse?
      await queueSourceStartAtExactTick(180,`results-${matchIndex}-source-start-1`):
      await (async()=>{
        const ready=await waitForResultsInternalPhase(180,2,
          `results-${matchIndex}-first-P1-start-phase-gate`);
        return ready.phase===1?{natural_css:true,source_frame:null}:
          queueSourceStart(readResultsFrame(ready),`results-${matchIndex}-source-start-1`);
      })();
    if(first.natural_css){
      result.results_source_input_stop={reason:'natural CSS return before first P1 source sample',
        source_frame:first.source_frame};
    }else{
      if(sourceTickThreePulse)assert.equal(first,180,
        'The first held-Start edge must be queued at Results source tick 180');
      starts.push(first);
      await writeProgress(`match-${matchIndex}-natural-results-after-first-start`);
      await screenshot(`match-${matchIndex}-natural-results-after-first-start`);
      let stopBeforeConfirmation=false;
      if(sourceTickThreePulse){
        const second=await queueSourceStartAtExactTick(360,`results-${matchIndex}-source-start-2`);
        if(second.natural_css){
          result.results_source_input_stop={reason:'natural CSS return before second P1 source sample',
            source_frame:second.source_frame};
          stopBeforeConfirmation=true;
        }else{
          assert.equal(second,360,
            'The intermediate held-Start edge must be queued at Results source tick 360');
          starts.push(second);
          await writeProgress(`match-${matchIndex}-natural-results-after-second-start`);
        }
      }
      const pagesBeforeConfirmation=stopBeforeConfirmation?null:
        sourceTickThreePulse?
          await waitForCpuPagesBeforeSourceFrame(resultsConfirmFrame,
            `results-${matchIndex}-CPU-pages-before-tick-${resultsConfirmFrame}`):null;
      let confirmation=null;
      if(!stopBeforeConfirmation&&sourceTickThreePulse){
        assert(pagesBeforeConfirmation&&!pagesBeforeConfirmation.natural_css,
          `Both disconnected CPU pages must be observed before the tick-${resultsConfirmFrame} confirmation`);
        const pageCheck={match:matchIndex,status:'passed-before-queue',
          input:`P1-only raw PAD Start holds at source ticks 180/360/${resultsConfirmFrame}; ports 0/1 connected and neutral between pulses; CPU ports 2/3 disconnected`,
          source_frame_before_confirmation:pagesBeforeConfirmation.source_frame,
          confirmation_queue_target:resultsConfirmFrame,attempted_confirmation_source_frame:null,
          confirmation_source_frame:null,confirmation_consumed:false,
          transitions:pagesBeforeConfirmation.transitions,
          connectedness:pagesBeforeConfirmation.connectedness};
        report.results_page_transition_checks.push(pageCheck);
        confirmation=await queueSourceStartAtExactTick(resultsConfirmFrame,
          `results-${matchIndex}-source-confirm-after-auto-page`);
        if(confirmation?.natural_css){
          pageCheck.status='natural-css-before-confirmation';
        }else{
          pageCheck.status='queued-awaiting-consumed-trace';
          pageCheck.attempted_confirmation_source_frame=confirmation;
        }
      }else if(!stopBeforeConfirmation){
        confirmation=await queueSourceStart(resultsConfirmFrame,
          `results-${matchIndex}-source-confirm-after-auto-page`,async ready=>{
        const trace=await readResultsSourcePadTrace();
        assert(!trace.overflow,'Pre-confirmation Results trace overflowed');
        const allTransitions=summarizeResultsPadTrace(trace).results_page_transitions;
        const cpuTransitions=allTransitions.filter(row=>row.from===0&&row.to===1&&row.slot>=2);
        const beforeConfirmFrame=trace.samples.at(-1)?.results_state_after_tick?.source_frame;
        assert(Number.isInteger(beforeConfirmFrame)&&beforeConfirmFrame>=readResultsFrame(ready),
          'Results source trace did not reach the pre-confirmation source boundary');
        assert.deepEqual(cpuTransitions.map(row=>row.slot),[2,3],
          'Disconnected CPU statistics pages must auto-advance before P1 confirmation');
        assert(cpuTransitions.every(row=>row.phase===3&&row.stats_phase===2&&
          row.source_frame<beforeConfirmFrame),
          'Disconnected CPU page transitions must be observed in active statistics before P1 confirmation');
        assert.deepEqual(allTransitions.map(row=>row.slot),[2,3],
          'Connected neutral ports or another Results player page changed before P1 confirmation');
      return {match:matchIndex,status:'pass',
        input:'P1-only source-tick Start; historical keyboard path not used',
        source_frame_before_confirmation:beforeConfirmFrame,
        confirmation_queue_target:resultsConfirmFrame,transitions:cpuTransitions};
      });
      }
      if(confirmation?.natural_css){
        result.results_source_input_stop={reason:'natural CSS return before the post-auto-page confirmation',
          source_frame:confirmation.source_frame,
          auto_page_gate:'not reached; inspect retained PAD trace'};
      }else if(confirmation){
        if(sourceTickThreePulse)assert.equal(confirmation,resultsConfirmFrame,
          `The final held-Start edge must be queued at Results source tick ${resultsConfirmFrame}`);
        starts.push(confirmation);
        // Some authored Results routes need another ordinary P1 confirmation.
        // Stop as soon as original CSS returns; never queue into a later scene.
        for(let pulse=2;!sourceTickThreePulse&&pulse<3&&state.phase!==1;pulse++){
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
    result.results_source_start_pulse_count=starts.length;
  }else if(keyboardGatedMode){
    const inputEventStart=await page.evaluate(()=>
      window.__meleeWebResultsInputEvents?.length||0);
    const keyboardPhaseReady=keyboardPrefixGatedMode?
      await waitForResultsFrame(192,`results-${matchIndex}-historical-prefix-source-frame-192`):
      await waitForResultsInternalPhase(180,2,
        `results-${matchIndex}-first-keyboard-enter-phase-gate`);
    if(keyboardPhaseReady.phase===1){
      result.results_keyboard_input_stop={reason:'natural CSS return before phase-2 keyboard gate'};
      throw Error(`Results ${matchIndex} returned to CSS before the keyboard input source-frame gate`);
    }
    result.results_keyboard_phase_gate={status:'passed',source_phase:keyboardPrefixGatedMode?
      'source-frame-prefix':2,
      source_frame:readResultsFrame(keyboardPhaseReady)};
    report.phases.push(keyboardPrefixGatedMode?
      `Results ${matchIndex}: first historical Enter pulse source-gated at frame 192`:
      `Results ${matchIndex}: original phase 2 observed before ordinary Enter`);
    // Retain the ordinary trusted keyboard path, but stop retrying as soon as
    // a P1 Start is actually consumed. Then wait with no further input until
    // the disconnected CPU pages auto-advance in source statistics.
    const initialEnterTargets=keyboardPrefixGatedMode?[192,363]:[198,296,394,509];
    const initialEnterDispatches=[];
    let initialCssReturn=false;
    let initialStartObserved=false;
    for(const targetFrame of initialEnterTargets){
      if(keyboardSourceTickConfirmMode){
        state=await waitForResultsPauseAtSourceFrame(targetFrame,
          `results-${matchIndex}-keyboard-prefix-pause-${targetFrame}`);
        const sourceFrameBefore=readResultsFrame(state);
        assert.equal(sourceFrameBefore,targetFrame,
          'Each historical prefix Enter must be dispatched at its exact source cursor');
        await page.keyboard.down('Enter');
        await retainResultsInputEvents();
        let prefixEvents=(report.results_input_events||[]).slice(inputEventStart);
        const prefixKeydown=prefixEvents.filter(row=>row.kind==='keydown').at(-1);
        assert(prefixKeydown?.isTrusted&&!prefixKeydown.repeat&&
          prefixKeydown.resultsSourceFrameAtEvent===targetFrame,
          'Trusted prefix Enter keydown must occur at its scheduled source cursor');
        await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
        state=await waitForResultsPauseAtSourceFrame(targetFrame+10,
          `results-${matchIndex}-keyboard-prefix-release-${targetFrame+10}`);
        await page.keyboard.up('Enter');
        await retainResultsInputEvents();
        prefixEvents=(report.results_input_events||[]).slice(inputEventStart);
        const prefixKeyup=prefixEvents.filter(row=>row.kind==='keyup').at(-1);
        assert(prefixKeyup?.isTrusted&&!prefixKeyup.repeat&&
          prefixKeyup.resultsSourceFrameAtEvent===targetFrame+10,
          'Trusted prefix Enter keyup must occur after ten consumed source samples');
        report.controller_inputs.push({device:'keyboard-to-source-PAD',key:'Enter',
          hold_source_ticks:10,wall_interval_ms:null,
          target_source_frame:targetFrame,source_frame_before:sourceFrameBefore,
          release_source_frame:targetFrame+10,source_frame_pause_control:true,
          label:`results-${matchIndex}-keyboard-prefix-${targetFrame}`});
        initialEnterDispatches.push({target_source_frame:targetFrame,
          source_frame_before:sourceFrameBefore});
        const trace=await readResultsSourcePadTrace();
        const startRun=summarizeResultsPadTrace(trace).p1_start_runs.find(row=>
          row.first_source_frame===targetFrame&&row.last_source_frame===targetFrame+9);
        initialStartObserved=!!startRun;
        assert(initialStartObserved,
          'Each exact prefix keyboard edge must produce one ten-source-tick P1 Start run');
        await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
        continue;
      }
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
      if(initialStartObserved&&!keyboardPrefixGatedMode)break;
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
      if(keyboardPrefixGatedMode){
        assert.equal(initialEnterDispatches.length,2,
          'The historical Results prefix must retain exactly two Enter pulses before the page gate');
        assert.deepEqual(initialKeydowns.map(row=>row.resultsSourceFrameAtEvent),[192,363],
          'The two pre-confirmation Enter keydowns must retain their exact Results source-frame brackets');
        const initialKeyups=initialInputEvents.filter(row=>row.kind==='keyup');
        assert.deepEqual(initialKeyups.map(row=>row.resultsSourceFrameAtEvent),[202,373],
          'Each 160ms Enter hold must retain its source-consumed release edge before the page gate');
      }
      assert.deepEqual(initialEnterDispatches.map(row=>row.target_source_frame),
        initialEnterTargets.slice(0,initialEnterDispatches.length));
      const initialDispatchFrame=initialEnterDispatches.at(-1).source_frame_before;
      await writeProgress(`match-${matchIndex}-initial-start-before-auto-pages`);
      await screenshot(`match-${matchIndex}-initial-start-before-auto-pages`);
      let gate;
      if(keyboardSourceTickConfirmMode){
        await waitForResultsPauseAtSourceFrame(resultsConfirmFrame,
          `results-${matchIndex}-CPU-pages-source-boundary-${resultsConfirmFrame}`);
        const exactPrefixTrace=await readResultsSourcePadTrace();
        gate=assertResultsCpuPagesAfterP1KeyboardPrefix(exactPrefixTrace,[192,363],{
          expectedPortErrors:keyboardPortErrors,
          expectedDisconnectedCpuSlots:keyboardAutoPageSlots});
        assert(gate,
          'The source-paused trace must prove both automatic CPU page changes before confirmation');
      }else{
        gate=await waitForCpuPagesBeforeKeyboard(
          initialEnterDispatches.at(-1).target_source_frame,
          `results-${matchIndex}-auto-pages-before-confirmation`);
      }
      if(gate.natural_css){
        result.results_keyboard_input_stop={reason:'natural CSS return before all expected CPU pages auto-advanced'};
        throw Error(`Results ${matchIndex} returned to CSS before all expected disconnected CPU pages auto-advanced`);
      }
      const pageCheck={match:matchIndex,status:'source-auto-pages-observed',
        input:keyboardPrefixGatedMode?
          keyboardSourceTickConfirmMode?
            'trusted Enter down/up edges paused at source cursors 192/202 and 363/373; assert disconnected CPU pages before source cursor 560; exact confirmation edges at 560/570 with ten consumed source samples and no host-time claim':
          'historical two trusted 160/120ms Enter pulses at source ticks 192/363; wait for disconnected CPU auto-pages, then send the third P1 confirmation':
          'ordinary trusted Enter trigger attempts stop on first consumed P1 Start; then source-gated 160ms/120ms Enter confirmation',
        expected_disconnected_cpu_slots:keyboardAutoPageSlots,
        controller_port_errors:keyboardPortErrors.map(values=>values[0]),
        auto_page_gate_poll_lower_bound_source_frame:initialEnterDispatches.at(-1).target_source_frame,
        initial_enter_pulse_count:initialEnterDispatches.length,
        initial_enter_dispatches:initialEnterDispatches,
        initial_dispatch_source_frame:initialDispatchFrame,
        initial_keydown_source_frame:initialKeydowns.at(-1).resultsSourceFrameAtEvent,
        initial_keydown_source_frames:initialKeydowns.map(row=>row.resultsSourceFrameAtEvent),
        initial_consumed_p1_start:keyboardPrefixGatedMode?
          gate.initial_start_runs.at(-1):gate.initial_start,
        initial_consumed_p1_starts:keyboardPrefixGatedMode?gate.initial_start_runs:undefined,
        stats_phase_start_source_frame:gate.stats_phase_start_source_frame,
        cpu_page_delay_source_ticks:gate.cpu_page_delay_source_ticks,
        source_frame_before_confirmation:gate.source_frame,
        transitions:gate.transitions,post_page_start_runs:gate.post_page_start_runs,
        post_gate_ordinary_enter_dispatch_source_frame:null,
        confirmation_source_frame:null,keyboard_keydown_source_frame:null,
        confirmation_consumed:false};
      report.results_page_transition_checks.push(pageCheck);
      result.results_keyboard_page_gate={source_frame:gate.source_frame,
        expected_disconnected_cpu_slots:keyboardAutoPageSlots,
        controller_port_errors:keyboardPortErrors.map(values=>values[0]),
        initial_start:keyboardPrefixGatedMode?
          gate.initial_start_runs.at(-1):gate.initial_start,
        initial_start_runs:keyboardPrefixGatedMode?gate.initial_start_runs:undefined,
        stats_phase_start_source_frame:gate.stats_phase_start_source_frame,
        cpu_page_delay_source_ticks:gate.cpu_page_delay_source_ticks,
        transitions:gate.transitions,post_page_start_runs:gate.post_page_start_runs,
        connectedness:gate.summary.port_error_values};
      report.phases.push(`Results ${matchIndex}: CPU pages auto-advanced before next ordinary Enter confirmation`);
      let exactConfirmationReady=false;
      if(keyboardSourceTickConfirmMode){
        const exactGate=gate;
        assert(!exactGate.natural_css&&
          exactGate.transitions.length===keyboardAutoPageSlots.length&&
          exactGate.transitions.every(row=>row.source_frame<resultsConfirmFrame),
          `Both disconnected CPU pages must advance before source tick ${resultsConfirmFrame}`);
        pageCheck.source_tick_page_gate={source_frame:exactGate.source_frame,
          transitions:exactGate.transitions,
          connectedness:exactGate.connectedness??exactGate.summary.port_error_values};
        state=await waitForResultsPauseAtSourceFrame(resultsConfirmFrame,
          `results-${matchIndex}-exact-keyboard-confirmation-tick-${resultsConfirmFrame}`);
        assert(state.phase===8||state.phase===9,
          `Results ${matchIndex} returned to CSS before the source-tick confirmation`);
        assert.equal(readResultsFrame(state),resultsConfirmFrame,
          `The source-tick keyboard pause boundary ${resultsConfirmFrame} was missed`);
        const beforeEnter=await readResultsSourcePadTrace();
        assert(!beforeEnter.overflow,'Pre-confirmation Results trace overflowed');
        const beforeEnterSummary=summarizeResultsPadTrace(beforeEnter);
        const beforeEnterTransitions=beforeEnterSummary.results_page_transitions.filter(row=>
          row.from===0&&row.to===1);
        assert.deepEqual(beforeEnterTransitions.map(row=>row.slot),keyboardAutoPageSlots,
          'Both disconnected CPU pages must auto-advance before trusted confirmation');
        assert(beforeEnterTransitions.every(row=>row.source_frame<resultsConfirmFrame&&
          row.phase===3&&row.stats_phase===2&&row.confirmed===0),
          'Disconnected CPU pages must transition in active statistics before the exact confirmation');
        pageCheck.source_frame_before_confirmation=readResultsFrame(state);
        pageCheck.confirmation_source_frame_target=resultsConfirmFrame;
        pageCheck.post_gate_ordinary_enter_dispatch_source_frame=resultsConfirmFrame;
        pageCheck.exact_boundary_pause={source_frame:resultsConfirmFrame,
          running:state.running,status:state.status,automatic_cpu_pages_before_confirmation:true};
        await page.keyboard.down('Enter');
        await retainResultsInputEvents();
        let exactEvents=(report.results_input_events||[]).slice(inputEventStart);
        const confirmationDown=exactEvents.filter(row=>row.kind==='keydown').at(-1);
        assert(confirmationDown?.isTrusted&&!confirmationDown.repeat&&
          confirmationDown.resultsSourceFrameAtEvent===resultsConfirmFrame,
          'Trusted Enter keydown must dispatch while source stepping is paused at the exact confirmation cursor');
        await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
        state=await waitForResultsPauseAtSourceFrame(resultsConfirmFrame+10,
          `results-${matchIndex}-exact-keyboard-release-tick-${resultsConfirmFrame+10}`);
        await page.keyboard.up('Enter');
        await retainResultsInputEvents();
        exactEvents=(report.results_input_events||[]).slice(inputEventStart);
        const confirmationUp=exactEvents.filter(row=>row.kind==='keyup').at(-1);
        assert(confirmationUp?.isTrusted&&!confirmationUp.repeat&&
          confirmationUp.resultsSourceFrameAtEvent===resultsConfirmFrame+10,
          'Trusted Enter keyup must dispatch at the exact cursor after ten held source samples');
        pageCheck.exact_boundary_release_pause={source_frame:resultsConfirmFrame+10,
          running:state.running,status:state.status};
        report.controller_inputs.push({device:'keyboard-to-source-PAD',key:'Enter',
          hold_source_ticks:10,wall_interval_ms:null,
          target_source_frame:resultsConfirmFrame,release_source_frame:resultsConfirmFrame+10,
          source_frame_pause_control:true,
          label:`results-${matchIndex}-keyboard-gated-continue-2`});
        await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
        state=await waitForResultsFrame(resultsConfirmFrame+11,
          `results-${matchIndex}-exact-keyboard-release-consumed-${resultsConfirmFrame+10}`);
        assert(state.phase===1||state.phase===5||state.phase===6||
          state.phase===8||state.phase===9,
          `Results ${matchIndex} entered an unexpected phase after the exact keyup sample`);
        exactConfirmationReady=true;
      }else{
        await writeProgress(`match-${matchIndex}-auto-pages-before-confirmation`);
        await screenshot(`match-${matchIndex}-auto-pages-before-confirmation`);
        const lastCpuPageTransition=Math.max(...gate.transitions.map(row=>row.source_frame));
        state=await waitForResultsFrame(lastCpuPageTransition+1,
          `results-${matchIndex}-confirmation-after-expected-auto-pages`);
        assert(state.phase===8||state.phase===9,
          `Results ${matchIndex} returned to CSS before the post-page keyboard confirmation`);
        const confirmationLowerBound=readResultsFrame(state);
        assert(confirmationLowerBound>lastCpuPageTransition,
          'Ordinary P1 confirmation must be dispatched at a source frame after every expected CPU page transition');
        pageCheck.source_frame_before_confirmation=confirmationLowerBound;
      }
      let pulses=exactConfirmationReady?3:initialEnterDispatches.length;
      let useExactReadySample=false;
      for(;!resultsObserveAfterConfirmation&&pulses<48&&state.phase!==1;pulses++){
        if(useExactReadySample){
          useExactReadySample=false;
        }else{
          state=await resumeResultsIfPaused(await diagnostic());
          if(state.error)throw Error(`Results ${matchIndex} before Enter pulse ${pulses}: ${state.error}`);
        }
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
      const initialStart=keyboardPrefixGatedMode?
        gate.initial_start_runs.at(-1):gate.initial_start;
      pageCheck.initial_keydown_source_frame=keydowns[initialPulseCount-1]?.resultsSourceFrameAtEvent??null;
      const initialStartPulseIndex=keydowns.findIndex((row,index)=>
        row.resultsSourceFrameAtEvent<=initialStart.first_source_frame&&
        keyups[index]?.resultsSourceFrameAtEvent>=initialStart.first_source_frame);
      assert(initialStartPulseIndex>=0,
        'A trusted initial Enter down/up source-frame bracket must contain the first consumed P1 Start');
      pageCheck.initial_start_keyboard_event_bracket={
        keydown_source_frame:keydowns[initialStartPulseIndex].resultsSourceFrameAtEvent,
        keyup_source_frame:keyups[initialStartPulseIndex].resultsSourceFrameAtEvent};
      if(keyboardPrefixGatedMode){
        const prefixBrackets=gate.initial_start_runs.map(startRun=>{
          const index=keydowns.findIndex((row,eventIndex)=>
            row.resultsSourceFrameAtEvent<=startRun.first_source_frame&&
            keyups[eventIndex]?.resultsSourceFrameAtEvent>=startRun.first_source_frame);
          assert(index>=0&&index<initialPulseCount,
            'Each historical pre-page P1 Start run must be enclosed by its own trusted Enter edge');
          return {source_start_frame:startRun.first_source_frame,
            keydown_source_frame:keydowns[index].resultsSourceFrameAtEvent,
            keyup_source_frame:keyups[index].resultsSourceFrameAtEvent};
        });
        assert.deepEqual(prefixBrackets.map(row=>row.source_start_frame),[192,363]);
        pageCheck.historical_prefix_keyboard_brackets=prefixBrackets;
      }
      const lastPageTransition=Math.max(...gate.transitions.map(item=>item.source_frame));
      const postPageKeydownIndex=keydowns.findIndex(row=>
        row.resultsSourceFrameAtEvent>lastPageTransition);
      assert(postPageKeydownIndex>=initialPulseCount,
        'A new trusted ordinary Enter confirmation must be dispatched after every expected CPU page transition');
      const postPageKeydown=keydowns[postPageKeydownIndex];
      assert(keydowns.slice(initialPulseCount).every(row=>
        row.resultsSourceFrameAtEvent>lastPageTransition),
        'Every confirmation Enter edge after the initial trigger must follow every expected CPU page transition');
      pageCheck.keyboard_keydown_source_frame=postPageKeydown.resultsSourceFrameAtEvent;
      pageCheck.status='post-page-keyboard-dispatched';
      if(keyboardSourceTickConfirmMode){
        const confirmationTrace=await readResultsSourcePadTrace();
        assert(!confirmationTrace.overflow,
          'Exact source-tick keyboard confirmation requires a complete Results PAD trace');
        const confirmationSummary=summarizeResultsPadTrace(confirmationTrace);
        assert.deepEqual(keydowns.slice(0,3).map(row=>row.resultsSourceFrameAtEvent),
          [192,363,resultsConfirmFrame],
          'The trusted P1 Enter keydowns must occur at source ticks 192, 363 and the exact confirmation tick');
        assert.deepEqual(keyups.slice(0,3).map(row=>row.resultsSourceFrameAtEvent),
          [202,373,resultsConfirmFrame+10],
          'Each ten-source-tick held P1 Enter must retain its distinct keyup edge');
        const firstThreeStarts=confirmationSummary.p1_start_runs.slice(0,3);
        assert.deepEqual(firstThreeStarts.map(row=>row.first_source_frame),
          [192,363,resultsConfirmFrame],
          'Only the third source-consumed P1 Start may confirm after the CPU page transitions');
        assert.deepEqual(firstThreeStarts.map(row=>row.last_source_frame),
          [201,372,resultsConfirmFrame+9],
          'The three historical P1 Start holds must each last exactly ten source ticks');
        pageCheck.confirmation_source_frame=firstThreeStarts[2]?.first_source_frame??null;
        pageCheck.confirmation_consumed=pageCheck.confirmation_source_frame!==null;
        assert(pageCheck.transitions.every(row=>row.source_frame<resultsConfirmFrame&&
          row.phase===3&&row.stats_phase===2&&row.confirmed===0),
          'Both disconnected CPU pages must auto-advance in active statistics before confirmation');
        assert.equal(pageCheck.source_frame_before_confirmation,resultsConfirmFrame);
        assert.equal(pageCheck.confirmation_source_frame,resultsConfirmFrame,
          'The source-consumed P1 confirmation must begin at the exact requested source tick');
        assert.equal(pageCheck.keyboard_keydown_source_frame,resultsConfirmFrame,
          'The trusted P1 Enter keydown must be bracketed at the exact source confirmation tick');
      pageCheck.exact_source_tick_keyboard_confirmation='pass';
      }
    }
    if(resultsObserveAfterConfirmation&&state.phase!==1){
      const observationFrame=resultsConfirmFrame+150;
      const observationDeadline=Date.now()+60000;
      while(Date.now()<observationDeadline){
        state=await resumeResultsIfPaused(await diagnostic());
        if(state.error)throw Error(`Results ${matchIndex} post-confirmation observation: ${state.error}`);
        if(state.phase===1)break;
        if(state.phase===5||state.phase===6){await page.waitForTimeout(8);continue;}
        assert(state.phase===8||state.phase===9,
          `Results ${matchIndex} post-confirmation observation entered phase ${state.phase}`);
        const sourceFrame=readResultsFrame(state);
        if(sourceFrame>=observationFrame)break;
        await page.waitForTimeout(8);
      }
      if(state.phase!==1){
        assert(readResultsFrame(state)>=observationFrame,
          `Results ${matchIndex} neither returned to CSS nor advanced through source tick ${observationFrame}`);
        const label=`match-${matchIndex}-post-confirmation-no-retry-source-frame-${observationFrame}`;
        const observation=await writeProgress(label);
        await screenshot(label);
        const retained=await retainResultsSourcePadTrace(matchIndex,
          'post-confirmation-observation-before-any-retry');
        assert(retained?.trace&&!retained.trace.overflow,
          'The no-retry Results observation must retain a complete non-overflowed PAD trace');
        assert.equal(retained.summary.tick_failed.length,0,
          'Results source ticks must continue successfully without a retry pulse');
        assert.deepEqual(retained.summary.p1_start_runs.map(row=>row.first_source_frame),
          [192,363,resultsConfirmFrame],
          'The no-retry observation must retain exactly the historical three P1 Start edges');
        assert.deepEqual(retained.summary.p1_start_runs.map(row=>row.last_source_frame),
          [201,372,resultsConfirmFrame+9],
          'Each held P1 Start edge must retain exactly ten source samples');
        result.results_post_confirmation_observation={status:'pass-no-retry',
          target_source_frame:observationFrame,observed_source_frame:readResultsFrame(state),
          phase:state.phase,running:state.running,
          no_additional_input_after_source_frame:resultsConfirmFrame+9,
          camera_entry:retained.trace.camera_entry,
          last_source_state:retained.trace.samples.at(-1)?.results_state_after_tick??null,
          memory:observation.memory};
        report.results_observation_only=true;
        report.results_observation_only_scope='Natural match and Results entry; exact P1 keyboard edges at 192/363/confirmation; no retry after confirmation; observe through 150 later Results source ticks or natural CSS return. If still in Results, no source exit/destructor claim.';
        report.result='results-observation-pass';
        activeMatchIndex=null;
        return;
      }
    }
  }else{
    await writeProgress(`match-${matchIndex}-natural-results`);
    await screenshot(`match-${matchIndex}-natural-results`);
    const sendOrdinaryKeyboardPulse=async(pulse,requireAdvancingSourceFrame=false)=>{
      state=await resumeResultsIfPaused(await diagnostic());
      if(state.error)throw Error(`Results ${matchIndex} before Enter pulse ${pulse}: ${state.error}`);
      if(state.phase===1)return;
      if(state.phase===5||state.phase===6){
        const sourceFrameBefore=readResultsFrame(state);
        state=await waitForResultsFrame(sourceFrameBefore+1,
          `results-${matchIndex}-continue-${pulse}-scene-transition-gate`);
        if(state.phase===1)return;
      }
      assert(state.phase===8||state.phase===9,
        `Results ${matchIndex} cannot send ordinary Enter in source phase ${state.phase}`);
      if(requireAdvancingSourceFrame){
        const sourceFrameBefore=readResultsFrame(state);
        state=await waitForResultsFrame(sourceFrameBefore+1,
          `results-${matchIndex}-continue-${pulse}-advancing-source-gate`);
        if(state.phase===1)return;
      }
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
    };
    const initialPulseLimit=resultsInputMode==='keyboard-three-prefix'?3:48;
    for(let pulse=0;pulse<initialPulseLimit&&state.phase!==1;pulse++)
      await sendOrdinaryKeyboardPulse(pulse);
    if(resultsInputMode==='keyboard-three-prefix'&&state.phase!==1){
      await captureThreePulsePrefix();
      for(let pulse=3;pulse<48&&state.phase!==1;pulse++)
        await sendOrdinaryKeyboardPulse(pulse,true);
    }
  }
  if(sourceTickMode&&state.phase!==1){
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
  if(teams)await assertTeamCssReturn(matchIndex);
  const sourcePadTraceRecord=await retainResultsSourcePadTrace(matchIndex,'natural-results-to-css');
  const cameraEntry=sourcePadTraceRecord?.trace?.camera_entry;
  assert(cameraEntry,
    `Results ${matchIndex} must retain source camera allocator identity at OnEnter`);
  assert.equal(cameraEntry.source_camera_allocation_generation_after_onenter,
    cameraEntry.source_camera_allocation_generation_before_onenter+1,
    `Results ${matchIndex} must allocate exactly one source camera pool during OnEnter`);
  assert.equal(cameraEntry.source_camera_allocation_subject_count_after_onenter,8,
    `Results ${matchIndex} must retain the original eight-subject camera allocation`);
  assert.equal(cameraEntry.source_pool_after_collision_adoption,
    cameraEntry.source_pool_after_onenter,
    `Results ${matchIndex} collision adoption must preserve the OnEnter camera pool`);
  assert.equal(cameraEntry.source_camera_allocation_generation_after_collision_adoption,
    cameraEntry.source_camera_allocation_generation_after_onenter,
    `Results ${matchIndex} collision adoption must not allocate another source camera pool`);
  assert.equal(cameraEntry.source_camera_allocation_subject_count_after_collision_adoption,8,
    `Results ${matchIndex} collision adoption must retain the OnEnter allocation identity`);
  const subjectEvents=cameraEntry.source_camera_subject_events;
  assert(Array.isArray(subjectEvents),
    "Results "+matchIndex+" must retain source camera subject-list mutations");
  assert.equal(cameraEntry.source_camera_subject_event_overflow,false,
    "Results "+matchIndex+" camera subject-list trace must not overflow");
  for(const phase of [
    'Camera_80029044 before free-pop','Camera_80029044 after active-append',
    'Camera_800290D4 before active-remove','Camera_800290D4 after free-push',
    'Results before HSD_Free','Results after HSD_Free',
    'Results after camera-global restore'])
    assert(subjectEvents.some(event=>event.phase===phase),
      "Results "+matchIndex+" must retain "+phase);
  for(const event of subjectEvents){
    if(!event.phase.startsWith('Camera_80029044')&&
       !event.phase.startsWith('Camera_800290D4'))continue;
    assert.equal(event.subject_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" subject must belong to the original pool");
    assert.equal(event.subject_prev_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" previous link must be null or pool-owned");
    assert.equal(event.subject_next_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" next link must be null or pool-owned");
    assert.equal(event.free_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" free root must be null or pool-owned");
    assert.equal(event.active_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" active root must be null or pool-owned");
    assert.equal(event.tail_in_pool,true,
      "Results "+matchIndex+" "+event.phase+" tail root must be null or pool-owned");
  }
  if(resultsInputMode==='keyboard-three-prefix'){
    const checkpoint=report.results_three_pulse_prefixes.find(row=>row.match===matchIndex);
    assert(checkpoint&&checkpoint.status==='pass',
      'The three-pulse Results prefix must be retained through source cursor 560');
    const finalTrace=sourcePadTraceRecord?.trace;
    const finalSample=finalTrace?.samples.find(row=>row.source_frame===560);
    assert(finalSample,'The final Results trace must retain source cursor 560');
    assert.deepEqual(checkpoint.pads_at_source_frame_560,finalSample.pads,
      'The retained three-pulse cursor-560 PAD snapshot must match the completed trace');
  }
  if(keyboardGatedMode){
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
        row.from===0&&row.to===1).map(row=>row.slot),keyboardAutoPageSlots,
        'Every disconnected CPU page must auto-advance from page zero before keyboard confirmation');
      assert(sourcePadSummary.results_page_transitions.every(row=>
        keyboardAutoPageSlots.includes(row.slot)&&row.to===row.from+1),
        'Only disconnected CPU pages from the declared profile may auto-advance during the retained Results trace');
    }
    assert.deepEqual(sourcePadSummary.port_error_values,keyboardPortErrors,
      'Natural Results port status changed from the declared keyboard profile');
    const neutralResultsAnalogFields=['stick_x','stick_y','substick_x','substick_y',
      'trigger_left','trigger_right','analog_a','analog_b','ext_button'];
    assert(sourcePadTrace.samples.every(row=>row.pads.every((pad,port)=>
      (port===0?(pad.button===0||pad.button===buttonStart):pad.button===0)&&
      pad.err===keyboardPortErrors[port][0]&&
      neutralResultsAnalogFields.every(field=>pad[field]===0))),
      'Only P1 Start may be pressed; keep every other button/axis neutral and preserve all four port statuses');
    if(result.results_keyboard_page_gate){
      const pageCheck=report.results_page_transition_checks.find(row=>
        row.match===matchIndex&&row.status==='post-page-keyboard-dispatched');
      assert(pageCheck,'The source-tick CPU page gate was not retained before the next keyboard confirmation');
      const pageZeroTransitions=sourcePadSummary.results_page_transitions.filter(row=>
        row.from===0&&row.to===1);
      assert.deepEqual(pageZeroTransitions.map(row=>row.slot),keyboardAutoPageSlots,
        'The expected disconnected CPU slots must each auto-advance exactly once');
      const latestTransition=Math.max(...pageZeroTransitions.map(row=>row.source_frame));
      const firstTransition=Math.min(...pageZeroTransitions.map(row=>row.source_frame));
      const initialStart=sourcePadSummary.p1_start_runs.find(row=>
        row.last_source_frame<firstTransition);
      assert(initialStart&&initialStart.last_source_frame<firstTransition,
        'The initial P1 Start must enter statistics before the automatic CPU page transitions');
      const beforeInitialStart=sourcePadTrace.samples.find(row=>
        row.source_frame===initialStart.first_source_frame-1);
      if(!keyboardPrefixGatedMode)
        assert(beforeInitialStart?.results_state_after_tick?.phase===2,
          'The first consumed keyboard Start must be preceded by the original Results fade phase');
      const keyboardEvents=result.results_keyboard_events||[];
      const keydowns=keyboardEvents.filter(row=>row.kind==='keydown');
      const keyups=keyboardEvents.filter(row=>row.kind==='keyup');
      if(keyboardP1EnterMode){
        assert(keyboardEvents.every(row=>row.inputServiceStatusAtEvent?.keyboard_requested_mask===3&&
          row.inputServiceStatusAtEvent?.keyboard_active_mask===3&&
          row.inputServiceStatusAtEvent?.physical_mask===0&&
          JSON.stringify(row.inputServiceStatusAtEvent?.pads?.map(pad=>pad.err))===
            JSON.stringify([0,0,-1,-1])&&
          row.inputServiceStatusAtEvent?.pads?.[1]?.buttons===0),
          'Every P1 Enter edge must preserve connected P1/P2 ports while leaving P2 neutral at DOM dispatch');
      }
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
      if(keyboardSourceTickConfirmMode){
        const sourcePausedInputs=resultsKeyboardInputs.filter(row=>row.source_frame_pause_control);
        assert.equal(sourcePausedInputs.length,3,
          'The two historical prefix pulses and exact confirmation must use source-boundary keyboard controls');
        assert(sourcePausedInputs.every(row=>row.key==='Enter'&&row.hold_source_ticks===10&&
          row.wall_interval_ms===null),
          'Exact keyboard pulses must declare ten consumed source samples without claiming host-time duration');
        assert(resultsKeyboardInputs.filter(row=>!row.source_frame_pause_control).every(row=>
          row.key==='Enter'&&row.hold_ms===160&&row.release_ms===120),
          'Any ordinary post-confirmation keyboard pulse must retain its 160/120ms host interval');
      }else{
        assert(resultsKeyboardInputs.every(row=>row.key==='Enter'&&
            row.hold_ms===160&&row.release_ms===120),
          'Ordinary keyboard Enter must preserve its 160ms hold and 120ms release semantics');
      }
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
        'The ordinary Enter confirmation must be dispatched after every expected CPU page transition');
      assert(keydowns.slice(initialPulseCount).every(row=>
        row.resultsSourceFrameAtEvent>latestTransition),
        'Every ordinary Enter confirmation must be dispatched after every expected CPU page transition');
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
  }else if(sourceTickMode){
    const sourcePadTrace=sourcePadTraceRecord?.trace;
    const sourcePadSummary=sourcePadTraceRecord?.summary;
    assert(sourcePadTrace&&sourcePadSummary&&!sourcePadSummary.overflow,
      'Source-tick Results validation requires a complete raw PAD trace');
    assert.equal(sourcePadSummary.source_consumed_pad_rows,sourcePadTrace.samples.length,
      'Every returned Results tick must retain the source-consumed PAD trigger/release state');
    assert.deepEqual(sourcePadSummary.source_p1_trigger_frames,
      result.results_source_start_pulse_frames,
      'Source-consumed P1 trigger edges must occur exactly at the declared source frames');
    assert.deepEqual(sourcePadSummary.source_p1_release_frames,
      result.results_source_start_pulse_frames.map(frame=>frame+10),
      'Source-consumed P1 release edges must follow each ten-tick hold exactly');
    const consumedPortErrors=Array.from({length:4},(_,port)=>
      [...new Set(sourcePadTrace.samples.map(row=>row.source_consumed_pads[port].err))]);
    assert.deepEqual(consumedPortErrors,[[0],[0],[-1],[-1]],
      'Source-consumed PAD connectedness must remain P1/P2 connected and CPU P3/P4 disconnected');
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
      const expectedPhase=sourceTickThreePulse?1:2;
      assert(preceding&&preceding.results_state_after_tick?.phase===expectedPhase,
        `The first P1 Start edge must be consumed from original Results phase ${expectedPhase}`);
    }
    if(sourceTickThreePulse&&result.results_source_start_pulse_frames.length===3){
      const pulses=result.results_source_start_pulse_frames;
      assert.deepEqual(pulses,[180,360,resultsConfirmFrame],
        'The focused Results reducer must retain its configured exact source-tick pulse schedule');
      const firstCpuTransition=Math.min(...cpuPageTransitions.map(row=>row.source_frame));
      const lastCpuTransition=Math.max(...cpuPageTransitions.map(row=>row.source_frame));
      assert(pulses[1]+9<firstCpuTransition&&lastCpuTransition<pulses[2],
        `Both disconnected CPU pages must auto-advance after the tick-360 pulse and before tick-${resultsConfirmFrame} confirmation`);
    }
    const pageCheck=report.results_page_transition_checks.find(row=>row.match===matchIndex&&
      row.confirmation_queue_target===resultsConfirmFrame&&
      (row.status==='pass'||row.status==='queued-awaiting-consumed-trace'));
    if(pageCheck){
      assert.deepEqual(cpuPageTransitions.map(row=>row.slot),[2,3],
        'Disconnected CPU statistics pages did not each auto-advance exactly once');
      const confirmationRun=findResultsStartRunAtOrAfter(sourcePadSummary.p1_start_runs,
        pageCheck.confirmation_queue_target);
      assert(confirmationRun,
        'The queued source-tick confirmation must match a consumed P1 Start in the retained PAD trace');
      if(sourceTickThreePulse){
        assert.equal(pageCheck.attempted_confirmation_source_frame,resultsConfirmFrame,
          `The source-tick reducer must dispatch its final P1 Start at cursor ${resultsConfirmFrame}`);
        assert.equal(confirmationRun.first_source_frame,resultsConfirmFrame,
          `The source-tick reducer must consume its final P1 Start at cursor ${resultsConfirmFrame}`);
      }
      pageCheck.confirmation_source_frame=confirmationRun.first_source_frame;
      pageCheck.confirmation_source_run=confirmationRun;
      pageCheck.confirmation_consumed=true;
      pageCheck.status='pass';
      const confirmationFrame=pageCheck.confirmation_source_frame;
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

campaignWallBoundTimer=armCampaignWallWatchdog();
try{
  checkCampaignWallBound();
  const discStat=await fs.stat(values.disc);
  report.disc={bytes:discStat.size,sha256:await sha256(values.disc)};
  report.url=values.url;
  report.browser_context.kind=userDataDirectory?'persistent':'temporary';
  report.browser_context.cache_reuse=userDataDirectory?'campaign-shared-origin-profile':'temporary-context';
  report.browser_context.driver_cache='uncontrolled';
  const {chromium,browser:launchOptions,browserPath,playwrightPath}=await loadBrowserTools(values.playwright);
  checkCampaignWallBound();
  const launchConfig={...browserLaunchOptions(launchOptions),headless:true};
  if(userDataDirectory){
    await fs.mkdir(userDataDirectory,{recursive:true});
    browserContext=await chromium.launchPersistentContext(userDataDirectory,launchConfig);
    browser=browserContext.browser();
    for(const existing of browserContext.pages())await existing.close();
  }else{
    browser=await chromium.launch(launchConfig);
    browserContext=null;
  }
  checkCampaignWallBound();
  report.browser={name:'headless Chrome',executable:path.basename(browserPath),version:browser?.version()??'unknown',playwright:playwrightPath};
  if(browser)browserCdp=await browser.newBrowserCDPSession();
  if(browserCdp){
    await browserCdp.send('Target.setDiscoverTargets',{discover:true});
  }
  const lastSourceProgress=()=>{
    const row=report.source_progress.at(-1);
    return row?{label:row.label,phase:row.phase,match_frame:row.match?.frame??null,
      wasm_heap_bytes:row.memory?.wasm_heap_bytes??null}:null;
  };
  browserCdp?.on('Target.targetCrashed',event=>report.target_crashes.push({
    at:new Date().toISOString(),...event,last_source_progress:lastSourceProgress()}));
  page=await (browserContext||browser).newPage({viewport:{width:1280,height:900},deviceScaleFactor:1});
  checkCampaignWallBound();
  page.on('crash',error=>report.page_crashes.push({at:new Date().toISOString(),
    message:error?.message||null,last_source_progress:lastSourceProgress()}));
  // Retain WebAudio state and AudioWorklet queue reports/errors at Results
  // entry and failures. This is observation only: it does not alter PCM,
  // source timing, controller input, or queue capacity.
  await page.addInitScript(()=>{
    const trace={contexts:[],worklets:[],nodes:new WeakMap(),ports:new WeakMap(),installErrors:[]};
    const rememberError=error=>trace.installErrors.push(String(error?.message||error));
    const snapshot=()=>({
      contexts:trace.contexts.map(row=>({sample_rate:row.context.sampleRate,
        state:row.context.state,states:[...row.states]})),
      worklets:trace.worklets.map(row=>({name:row.name,sample_rate:row.sampleRate,
        connected:row.connected,destination_connected:row.destinationConnected,
        pcm_messages:row.pcmMessages,pcm_frames:row.pcmFrames,
        latest_queue_report:row.latestQueueReport,
        queue_reports:[...row.queueReports],errors:[...row.errors]})),
      install_errors:[...trace.installErrors],
    });
    window.__meleeWebAudioDiagnostics={snapshot};

    const NativeAudioContext=window.AudioContext;
    if(NativeAudioContext){
      try{
        Object.defineProperty(window,'AudioContext',{configurable:true,writable:true,
          value:new Proxy(NativeAudioContext,{construct(target,args){
            const context=Reflect.construct(target,args,target);
            const row={context,states:[context.state]};
            try{context.addEventListener('statechange',()=>row.states.push(context.state));}
            catch(error){rememberError(error);}
            trace.contexts.push(row);
            return context;
          }})});
      }catch(error){rememberError(error);}
    }

    const NativeAudioNode=window.AudioNode;
    const nativeConnect=NativeAudioNode?.prototype?.connect;
    if(nativeConnect){
      try{Object.defineProperty(NativeAudioNode.prototype,'connect',{configurable:true,writable:true,
        value(destination,...args){
          const row=trace.nodes.get(this);
          if(row){row.connected=true;row.destinationConnected||=destination===row.context.destination;}
          return nativeConnect.call(this,destination,...args);
        }});}catch(error){rememberError(error);}
    }

    const NativeMessagePort=window.MessagePort;
    const nativePostMessage=NativeMessagePort?.prototype?.postMessage;
    if(nativePostMessage){
      try{Object.defineProperty(NativeMessagePort.prototype,'postMessage',{configurable:true,writable:true,
        value(data,...args){
          const row=trace.ports.get(this);
          if(row&&data?.type==='pcm'){
            row.pcmMessages++;
            if(ArrayBuffer.isView(data.pcm))row.pcmFrames+=Math.floor(data.pcm.length/2);
          }
          return nativePostMessage.call(this,data,...args);
        }});}catch(error){rememberError(error);}
    }

    const NativeAudioWorkletNode=window.AudioWorkletNode;
    if(NativeAudioWorkletNode){
      try{Object.defineProperty(window,'AudioWorkletNode',{configurable:true,writable:true,
        value:new Proxy(NativeAudioWorkletNode,{construct(target,args){
          const node=Reflect.construct(target,args,target),context=args[0];
          const row={context,name:args[1],sampleRate:context.sampleRate,connected:false,
            destinationConnected:false,pcmMessages:0,pcmFrames:0,latestQueueReport:null,
            queueReports:[],errors:[]};
          trace.worklets.push(row);trace.nodes.set(node,row);trace.ports.set(node.port,row);
          try{
            node.port.addEventListener('message',event=>{
              const data=event.data||{},atMs=performance.now();
              if(Number.isFinite(data.queued)||Number.isFinite(data.underruns)||Number.isFinite(data.overflows)){
                row.latestQueueReport={at_ms:atMs,queued:data.queued??null,
                  underruns:data.underruns??null,overflows:data.overflows??null,
                  type:data.type||null};
                row.queueReports.push(row.latestQueueReport);
                if(row.queueReports.length>16)row.queueReports.shift();
              }
              if(data.error){
                row.errors.push({at_ms:atMs,message:String(data.error),
                  last_queue_report:row.latestQueueReport});
                if(row.errors.length>16)row.errors.shift();
              }
            });
            node.port.start();
          }catch(error){rememberError(error);}
          return node;
        }})});}catch(error){rememberError(error);}
    }
  });
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
  driver=createBrowserDriver(page,{timeoutMs:60000,
    deadline:campaignDeadline===null?Date.now()+65*60*1000:campaignDeadline});
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
  report.css_observer_storage = {provider: 'served development helpers; no replacement',
    native_ids_bytes: 56, native_geometry_bytes: 32};
  report.staging_ring = await page.evaluate(() => window.__meleeWebStagingRingStatus ?? null);
  if (new URL(values.url).searchParams.get('melee-web-staging-slots') === '4') {
    assert.equal(report.staging_ring?.frame_slots, 4, 'Explicit ring4 regression requires four native frame slots');
    assert.equal(report.staging_ring?.staging_buffers, 4, 'Explicit ring4 regression requires four native staging buffers');
  }

  await installRuntimeDiagnosticsCapture(report.runtime_diagnostics.identity,
    report.runtime_diagnostics.identity_scope,page);
  await installResultsInputObserver();
  await driver.selectDisc(values.disc);
  await driver.waitForStart();
  if(keyboardP1EnterMode){
    await page.waitForFunction(()=>{
      const input=JSON.parse(Module.UTF8ToString(Module._melee_web_input_message()));
      return input.keyboard_requested_mask===3&&input.physical_mask===0&&
        input.pads[0].err===0&&input.pads[1].err===0&&
        input.pads[2].err===-1&&input.pads[3].err===-1&&
        (input.keyboard_active_mask&~3)===0;
    },null,{timeout:10000});
    const controllerProfile=await page.evaluate(()=>{
      const input=JSON.parse(Module.UTF8ToString(Module._melee_web_input_message()));
      return {keyboard_requested_mask:input.keyboard_requested_mask,
        keyboard_active_mask:input.keyboard_active_mask,physical_mask:input.physical_mask,
        port_errors:input.pads.map(pad=>pad.err)};
    });
    assert.equal(controllerProfile.keyboard_requested_mask,3,
      'P1/P2 split-keyboard ports must remain requested for the historical CSS setup');
    assert.equal(controllerProfile.keyboard_active_mask&~3,0,
      'No keyboard port outside P1/P2 may become active');
    assert.equal(controllerProfile.physical_mask,0,
      'The headless Results profile must not inherit physical controllers');
    assert.deepEqual(controllerProfile.port_errors,[0,0,-1,-1],
      'Preserve the two connected keyboard ports used by the original CSS driver');
    report.controller_profile={kind:'P1/P2 split keyboard; P1-only Results Start',...controllerProfile,
      historical_results_port_status:'not retained; setup drives both ports and Results event/PAD observations are retained here'};
  }
  await driver.launch(1);
  await waitFor('original VS CSS initial entry',s=>s.phase===1&&s.css,60000);
  await screenshot('initial-css');
  report.initial_css=await writeProgress('initial-css');
  if(friendlyFire)await enableFriendlyFire();
  // Enable Teams before doors 3/4 join so the CSS progress boundary sees a
  // third and fourth door entering an active Team Battle setup.
  if(teams)await enableCssTeams();
  await configureRoster(lineup);
  if(teams){
    await assignCssTeams(teams);
    await verifyRoster(lineup,`${cpuProfileDescription} ${stage.name} Team Battle roster before SSS`);
  }
  if(values['setup-only']){report.result='setup-only-pass';}
  else if(stageSetupOnly){
    await chooseStage();
    report.result='stage-setup-pass';
  }else await runMatch(1,lineup);
  if(!values['setup-only']&&!stageSetupOnly){
    const retainedLineup=values.lineup==='B'?lineup.map(fighter=>({...fighter})):lineup;
    for(let matchIndex=2;matchIndex<=matchCount;matchIndex++){
      if(values.lineup==='B'&&matchIndex===2){
        // Zelda and Sheik share one source CSS icon/ckind. The visible icon row
        // cannot select the alternate fighter identity by asking the geometry
        // observer for a nonexistent second icon; exercise Sheik through the
        // original in-match down-B transition in the dedicated form trace.
        report.phases.push('B subsequent matches retain Zelda at the shared CSS icon; Sheik is covered by a separate in-match down-B scenario');
      }
      const retained=await verifyRoster(retainedLineup,
        `${cpuProfileDescription} retained roster before match ${matchIndex}`);
      assert.equal(retained.length,4);
      await runMatch(matchIndex,retainedLineup);
    }
    if(!report.results_observation_only)
      await unloadAfterNaturalResultsCss();
  }
  if(!values['setup-only']&&!stageSetupOnly)report.result=resultsObserveAfterConfirmation?
    'results-observation-pass':'pass';
}catch(error){
  report.failure??=campaignWallBoundExceeded?{
    code:'campaign_wall_bound_exceeded',
    message:campaignWallBoundError().message,stack:error.stack}: {
    message:error.message,stack:error.stack};
  process.exitCode=1;
  if(page&&!page.isClosed()&&!campaignWallBoundExceeded){
    report.failure.audio_diagnostics=await readAudioDiagnostics();
    await retainResultsEntry('failure-latest-entry');
    await diagnostic().then(state=>{report.failure.diagnostics=state;}).catch(()=>{});
    await screenshot('failure').catch(()=>{});
    await page.locator('body').textContent().then(text=>fs.writeFile(path.join(output,'page.txt'),text)).catch(()=>{});
  }
}finally{
  try{
  await stopControlledContention();
  if(campaignWallBoundTimer!==null){
    clearTimeout(campaignWallBoundTimer);campaignWallBoundTimer=null;
  }
  if(wallBoundTask)await wallBoundTask.catch(()=>{});
  if(campaignWallBoundExceeded){
    report.result='fail';process.exitCode=1;
    report.failure??={code:'campaign_wall_bound_exceeded',message:campaignWallBoundError().message};
  }
  if(report.campaign_wall_bound&&!campaignWallBoundExceeded){
    report.campaign_wall_bound.status=['pass','setup-only-pass','stage-setup-pass','results-observation-pass'].includes(report.result)?
      'completed':'cleared';
    report.campaign_wall_bound.timer_status='cleared';
  }
  if(page&&!page.isClosed()&&!campaignWallBoundExceeded){
    await retainResultsInputEvents();
    await retainRuntimeDiagnosticsCapture();
    if(activeMatchIndex!==null&&report.matches.some(row=>row.match===activeMatchIndex)&&
       !report.results_source_pad_traces.some(row=>row.match===activeMatchIndex))
      await retainResultsSourcePadTrace(activeMatchIndex,'final-state-after-match-stop');
  }
  report.final_diagnostics=page&&!page.isClosed()&&!campaignWallBoundExceeded?
    await diagnostic().catch(error=>({error:error.message})):
    report.campaign_wall_bound?.evidence_state??null;
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
  }catch(error){
    const message=String(error?.message||error);
    report.final_evidence_error=message;
    report.result='fail';process.exitCode=1;
    report.failure??={message:`Final evidence collection failed: ${message}`};
  }finally{
    await closeOwnedBrowserResources();
    process.off('SIGINT',onSigint);process.off('SIGTERM',onSigterm);
    await fs.writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  }
}
if(!['pass','setup-only-pass','stage-setup-pass','results-observation-pass'].includes(report.result))
  throw Error(report.failure?.message||'Headless CPU9 lineup scenario failed');
console.log(JSON.stringify({result:report.result,lineup:report.lineup,matches:report.matches.length,
  browser:report.browser,output}));
