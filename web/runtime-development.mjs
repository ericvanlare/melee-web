/** Development tools attach to the shared player; this file is excluded from public builds. */
import {mountMeleeRuntime} from './melee-runtime.mjs';
import {mountControllerSettings} from './controller-settings.mjs';
import {createRuntimeAudio} from './runtime-audio.mjs';
import {loadNativeGameDisc, openNativeGameSession} from './runtime-audio-assets.mjs';
import {attemptNetworkTimingPauseResume, evaluateNetworkTimingPause} from './net-timing-pause.mjs';
import {createRuntimeLockstepSession} from './net_lockstep_runtime_owner.mjs';
import {createDevelopmentLockstepOwner} from './net_lockstep_development_owner.mjs';
import {createRoomInvite, parseRoomInvite} from './net_lockstep_room_invite.mjs';
import {LOCKSTEP_DELAY, LOCKSTEP_MAX_SOURCE_TICKS} from './net_lockstep_core.mjs';
import {isStagingRoomSurface, parseStagingRoomInvite, stagingRoomInviteFromHash,
  stagingRoomInviteLink, STAGING_HOST, STAGING_RELAY_URL} from './staging-room.mjs';
const stagingRoomEnabled=isStagingRoomSurface(window.location);
window.meleeStagingRoom=Object.freeze({enabled:stagingRoomEnabled,relayUrl:STAGING_RELAY_URL,
  parseInvite:text=>parseStagingRoomInvite(text,parseRoomInvite),
  inviteFromHash:hash=>stagingRoomInviteFromHash(hash,parseRoomInvite),
  inviteLink:(url,text)=>stagingRoomInviteLink(url,text,parseRoomInvite)});
if(stagingRoomEnabled){
 document.body.dataset.surface='staging';document.title='Melee Web · Play together';
 document.getElementById('runtime-kicker').textContent='Hosted multiplayer alpha';
 document.getElementById('page-heading').textContent='Play together';
 document.getElementById('staging-intro').textContent='Play together using your own local Melee disc; your disc stays on your device. Both players import their disc and set Controls before Host or Join. The hosted relay requires Internet access.';
 document.getElementById('staging-intro').hidden=false;
 document.getElementById('disc-label').firstChild.textContent='Choose your own Melee disc image ';
 document.getElementById('room-help').textContent='Host is Player 1 (controller port 1 · PAD 0). Join is Player 2 (controller port 2 · PAD 1). Use the role-specific Controls steps below before connecting.';
 document.getElementById('role-controls-help').hidden=false;
 document.getElementById('lockstep-relay-url').value=STAGING_RELAY_URL;
 document.getElementById('lockstep-status').textContent='Choose your own disc and open Controls. Host as Player 1 or join as Player 2 when your disc is ready.';
 document.getElementById('input-guidance').textContent='After both players connect, continue through the original character and stage screens. This alpha does not assess match results.';
 document.getElementById('lockstep-host').textContent='Host game · Player 1';
 document.getElementById('lockstep-join').textContent='Join game · Player 2';
 document.getElementById('lockstep-leave').textContent='Leave room';
 document.getElementById('unload').textContent='Unload disc';
 document.getElementById('lockstep-link-label').hidden=false;
 document.getElementById('lockstep-copy-link').hidden=false;
 try{
  const invite=window.meleeStagingRoom.inviteFromHash(window.location.hash);
  if(invite){
   document.getElementById('lockstep-join-invite').value=invite;
   document.getElementById('lockstep-status').textContent='Invite loaded. Choose your own disc, check Controls, then select Join game · Player 2.';
  }
 }catch(error){document.getElementById('lockstep-status').textContent=`Invite link could not be used: ${error?.message||String(error)}`;}
}
const developmentHooks = {};
const markFirstReplayBoundary=(...args)=>{try{window.__meleeReplayBoundaryProbe?.mark?.(...args);}catch{}};
const RETAIL_REPLAY_LEGACY_MAX_FRAMES = 36000;
const RETAIL_REPLAY_WHOLE_SESSION_MAX_FRAMES = 108000;
const RETAIL_REPLAY_STATE_RECORD_OVERHEAD = 4;
const RETAIL_REPLAY_SESSION_RECORD_OVERHEAD = 32 + 2;
const RETAIL_REPLAY_TIMER_RECORD_OVERHEAD = 3;
const RETAIL_REPLAY_LEGACY_WALL_TIME_MS = 900000;
const RETAIL_REPLAY_WHOLE_SESSION_WALL_TIME_MS = RETAIL_REPLAY_LEGACY_WALL_TIME_MS * 3;
function retailReplayWallTimeMs(wholeSession) {
  return wholeSession ? RETAIL_REPLAY_WHOLE_SESSION_WALL_TIME_MS : RETAIL_REPLAY_LEGACY_WALL_TIME_MS;
}
// Keep the upload ceiling identical to kRetailReplayWholeSessionMaxBytes in
// gameplay_retail_recipe.hpp. The v9/v10 profiles admit exactly three setups;
// the span table admits all 32 bounded spans.
const RETAIL_REPLAY_MAX_BYTES = 16 + 4 + 8 + (0x18 + 0x55E8 + 0x148 + 6) +
  4 + 3 * 0x138 + 822 + RETAIL_REPLAY_WHOLE_SESSION_MAX_FRAMES * 44 + 2 + 32 * 12;
let owner, controllerSettings, Module, boundary, status, check, put, prepareAudio, pauseAudioForPreparation;
let syncAudio, unloadAndSave, prepareNativeResources, waitForAudioAck, waitForAudioRender;
let runtimeLockstepEntry = null;
let preparationKeepsAudio = false;
const stop = error => owner.stop(error);
const $=id=>document.getElementById(id);const startupUrl=new URL(location.href),clearRenderCacheOnLoad=startupUrl.searchParams.get('render-cache')==='clear',hitchCaptureFromUrl=startupUrl.searchParams.get('hitch-capture')==='1',hitchCausalFromUrl=startupUrl.searchParams.get('hitch-causal')==='1',hitchUserTimingFromUrl=startupUrl.searchParams.get('hitch-marks')==='1';if(clearRenderCacheOnLoad){startupUrl.searchParams.delete('render-cache');history.replaceState(null,'',startupUrl);}let ready=false,fatal=false,bundle=false,inputDirty=true,importing=false,uiMessage="";let frameLast=0,perfLastReport=0,activeFrames=0,worstFrame=0,worstBrowserCallback=null,longFrames=0,browserLongTasks=0,browserLongTaskWorst=0;let nativeTimingFrames=0,nativeTimingWorst=0,nativeLongFrames=0,nativeOverBudgetFrames=0,nativeSourceSteps=0,nativeSourceDraws=0,nativeWorstTiming=null,nativeFirstUse=0,nativeFirstUseWorst=0,nativePreviousTiming=null,nativeLastTiming=null,livePipelineQueued=0,livePipelineCreated=0,liveTextureUploads=0,liveStagingUsedBytes=0,peakStagingUsedBytes=0;let constructionCounts={},constructionWorst=0,constructionFirstUseWorst=0,preparationSince=0,preparationLabel="",preparationSequence=0,activePreparation=null,pendingEntryProfile=null,settlingEntryProfile=null,entryProfiles=[],latestNativePreparation=null;let preparationCount=0,preparationWorst=0;let diagnosticCaptureInvalid=false,audioStateAcknowledged=true,audioAckWaiters=[],stockCheckActive=false,latestAudio={queued:0,underruns:0,overflows:0},actionSweepActive=false,actionSweepFocusLost=false,selectionDriveActive=false,sweepAutomaticResumes=0;
window.meleeHitchCapture=null;let runtimeCacheSyncCapability=null,runtimeCacheSyncDropped=0;const runtimeCacheSyncQueue=[];const reportRuntimeCacheSync=event=>{const capture=window.meleeHitchCapture;if(capture?.observeCacheSync){capture.observeCacheSync(event);return;}if(hitchCausalFromUrl){if(runtimeCacheSyncQueue.length<128)runtimeCacheSyncQueue.push(event);else runtimeCacheSyncDropped++;}};const hitchCaptureLoading=import('./hitch-capture.mjs').then(({createHitchCapture})=>{const capture=createHitchCapture({enabled:hitchCaptureFromUrl,userTiming:hitchUserTimingFromUrl,cacheSyncDiagnostics:hitchCausalFromUrl});window.meleeHitchCapture=capture;if(runtimeCacheSyncCapability)capture.setCacheSyncCapability(runtimeCacheSyncCapability);for(const event of runtimeCacheSyncQueue.splice(0))capture.observeCacheSync(event);if(runtimeCacheSyncDropped)capture.noteCacheSyncLoss(runtimeCacheSyncDropped);const toggle=$('hitch-capture');if(toggle){toggle.checked=hitchCaptureFromUrl;toggle.onchange=()=>{capture.setEnabled(toggle.checked);if(hitchCausalFromUrl)Module?.setRuntimeCacheSyncDiagnostics?.(toggle.checked);};}return capture;}).catch(error=>{window.meleeHitchCaptureLoadError=String(error?.message||error);const toggle=$('hitch-capture');if(toggle)toggle.disabled=true;return null;});window.meleeHitchCaptureLoading=hitchCaptureLoading;
const LOCKSTEP_ROOM_TIMEOUT_MS=120000;
const lockstepStagingMode=window.meleeStagingRoom?.enabled===true;
let lockstepRoomCanStart=false,lockstepRoomConfigured=false,lockstepRoomStarting=false,lockstepRoomLeaving=false;
let lockstepRoomArmed=false,lockstepRoomFinished=false,lockstepRoomCloseAttempted=false,lockstepRoomUnloading=false,lockstepRoomRole=null,lockstepRoomTimer=null;
function lockstepRoomStatus(text){$('lockstep-status').textContent=text;}
function stopLockstepRoomPolling(){clearInterval(lockstepRoomTimer);lockstepRoomTimer=null;}
function readLockstepRoomHealth(){
 try{return {health:window.meleeNetRuntimeLockstepHealth?.()??null,error:null};}
 catch(error){return {health:null,error};}
}
function syncLockstepRoomControls(observedHealth=undefined){
 const canStart=lockstepRoomCanStart&&ready&&!fatal&&bundle&&!lockstepRoomConfigured&&!lockstepRoomStarting&&!lockstepRoomUnloading;
 $('lockstep-host').disabled=!canStart;
 $('lockstep-join').disabled=!canStart;
 $('lockstep-copy').disabled=!$('lockstep-host-invite').value;
 if($('lockstep-copy-link'))$('lockstep-copy-link').disabled=!$('lockstep-host-link')?.value;
 let health=observedHealth;
 if(health===undefined&&lockstepRoomConfigured)health=readLockstepRoomHealth().health;
 if(health?.armed===true)lockstepRoomArmed=true;
 $('lockstep-leave').disabled=!lockstepRoomConfigured||lockstepRoomLeaving||lockstepRoomCloseAttempted||lockstepRoomUnloading||
  !(health?.armed===true||lockstepRoomArmed);
 $('launch').disabled=!lockstepRoomCanStart||!!retailRun||replayLoading||lockstepRoomConfigured||lockstepRoomStarting||lockstepRoomUnloading;
}
function finishLockstepRoomStatus(text,observedHealth=undefined){
 lockstepRoomFinished=true;stopLockstepRoomPolling();lockstepRoomStatus(text);
 if(lockstepStagingMode)$('lockstep-retry').hidden=false;
 syncLockstepRoomControls(observedHealth);
}
function waitingLockstepRoomStatus(){return lockstepRoomRole==='alpha'
 ?(lockstepStagingMode?'Waiting for Player 2. Share the invite link with them.':'Waiting for Player 2. Copy the host invite to the other player.')
 :(lockstepStagingMode?'Waiting for Player 1. Ask the host to share their invite link.':'Waiting for Player 1.');}
function observeLockstepRoom(){
 if(!lockstepRoomConfigured||lockstepRoomFinished)return;
 const {health,error}=readLockstepRoomHealth();
 if(error){finishLockstepRoomStatus(`Error: lockstep health unavailable (${error?.message||String(error)}).`,null);return;}
 if(health?.armed===true)lockstepRoomArmed=true;
 syncLockstepRoomControls(health);
 if(!health){lockstepRoomStatus(`Preparing ${lockstepRoomRole==='alpha'?'Host':'Join'} session.`);return;}
 const peer=health.peer,protocol=peer?.protocol;
 const failure=health.failure||peer?.failure;
 if(failure){finishLockstepRoomStatus(`Error: ${failure}`,health);return;}
 if(protocol?.terminal){
  const reason=protocol.terminal.reason||protocol.terminal.kind||'lockstep ended';
  if(protocol.terminal.kind==='disconnect'||peer?.endpointClosed||peer?.closed)
   finishLockstepRoomStatus('Disconnected. Route outcome is not assessed.',health);
  else finishLockstepRoomStatus(`Error: ${reason}`,health);
  return;
 }
 if(peer?.endpointClosed||peer?.closed){finishLockstepRoomStatus('Disconnected. Route outcome is not assessed.',health);return;}
 if(Number(protocol?.local_checksum_ticks)>=LOCKSTEP_MAX_SOURCE_TICKS){
  finishLockstepRoomStatus('Session limit reached. Route completion is unverified.',health);return;
 }
 if(protocol?.ready===true){lockstepRoomStatus(lockstepStagingMode?'Connected. Choose your characters and stage to start the match.':'Connected. Match results are not assessed here.');return;}
 lockstepRoomStatus(health.armed===true?waitingLockstepRoomStatus():
  `Preparing ${lockstepRoomRole==='alpha'?'Host':'Join'} session.`);
}
function startLockstepRoom(role,invite){
 if(lockstepRoomConfigured||lockstepRoomStarting)throw Error('This page already owns a lockstep session; reload and import a fresh disc to start another.');
 if(!lockstepRoomCanStart)throw Error('Import a disc and prepare a fresh runtime before starting a relay room.');
 lockstepRoomRole=role;lockstepRoomStarting=true;syncLockstepRoomControls();
 try{
  window.meleeNetConfigureRuntimeLockstep({role,roomId:invite.roomId,url:invite.url,
   timeoutMs:LOCKSTEP_ROOM_TIMEOUT_MS,transport:'relay'});
  lockstepRoomConfigured=true;lockstepRoomFinished=false;lockstepRoomCloseAttempted=false;lockstepRoomArmed=false;
  lockstepRoomStatus(`Preparing ${role==='alpha'?'Host':'Join'} session.`);
  stopLockstepRoomPolling();lockstepRoomTimer=setInterval(observeLockstepRoom,1000);
  Promise.resolve(window.meleeNetBeginLockstep(invite.seed,invite.sourceTicks)).then(()=>{
   if(!lockstepRoomLeaving)observeLockstepRoom();
  },error=>{
   if(!lockstepRoomFinished&&!lockstepRoomCloseAttempted&&!lockstepRoomUnloading)finishLockstepRoomStatus(`Error: ${error?.message||String(error)}`);
  }).finally(()=>{lockstepRoomStarting=false;syncLockstepRoomControls();});
 }catch(error){lockstepRoomStarting=false;if(lockstepRoomConfigured)finishLockstepRoomStatus(`Error: ${error?.message||String(error)}`);else{syncLockstepRoomControls();lockstepRoomStatus(`Error: ${error?.message||String(error)}`);}throw error;}
 syncLockstepRoomControls();
}
function showLockstepRoomError(error){lockstepRoomStatus(`Error: ${error?.message||String(error)}`);}
$('lockstep-host').onclick=()=>{
 try{
  const text=createRoomInvite({url:lockstepStagingMode?window.meleeStagingRoom.relayUrl:$('lockstep-relay-url').value.trim()});
  const invite=lockstepStagingMode?window.meleeStagingRoom.parseInvite(text):parseRoomInvite(text);
  $('lockstep-host-invite').value=text;syncLockstepRoomControls();
  if(lockstepStagingMode){
   const link=window.meleeStagingRoom.inviteLink(window.location.href,text);
   $('lockstep-host-link').value=link;
   try{history.replaceState(history.state,'',link);}catch{$('lockstep-copy-status').textContent=' Invite created; copy the invite link below.';}
   syncLockstepRoomControls();
  }
  startLockstepRoom('alpha',invite);
 }catch(error){showLockstepRoomError(error);}
};
$('lockstep-join').onclick=()=>{
 try{const inviteText=$('lockstep-join-invite').value.trim();
  startLockstepRoom('beta',lockstepStagingMode?window.meleeStagingRoom.parseInvite(inviteText):parseRoomInvite(inviteText));}
 catch(error){showLockstepRoomError(error);}
};
$('lockstep-copy').onclick=async()=>{
 const invite=$('lockstep-host-invite').value;if(!invite)return;
 try{await navigator.clipboard.writeText(invite);$('lockstep-copy-status').textContent=' Invite copied.';}
 catch{$('lockstep-host-invite').focus();$('lockstep-host-invite').select();$('lockstep-copy-status').textContent=' Select and copy the invite.';}
};
$('lockstep-copy-link')?.addEventListener('click',async()=>{
 const link=$('lockstep-host-link')?.value;if(!link)return;
 try{await navigator.clipboard.writeText(link);$('lockstep-copy-status').textContent=' Invite link copied.';}
 catch{$('lockstep-host-link').focus();$('lockstep-host-link').select();$('lockstep-copy-status').textContent=' Select and copy the invite link.';}
});
$('lockstep-host-invite').oninput=syncLockstepRoomControls;
$('lockstep-retry')?.addEventListener('click',()=>window.location.reload());
$('lockstep-leave').onclick=async()=>{
 if(!lockstepRoomConfigured||lockstepRoomLeaving||lockstepRoomCloseAttempted)return;
 const {health,error}=readLockstepRoomHealth();
 if(error&&!lockstepRoomArmed){finishLockstepRoomStatus(`Error: lockstep health unavailable (${error?.message||String(error)}); teardown is unconfirmed.`,null);return;}
 if(health?.armed===true)lockstepRoomArmed=true;
 if(!lockstepRoomArmed){lockstepRoomStatus('Preparing session. Leave becomes available after the runtime owner is armed.');return;}
 lockstepRoomLeaving=true;lockstepRoomCloseAttempted=true;stopLockstepRoomPolling();
 lockstepRoomStatus('Leaving room; teardown is pending.');syncLockstepRoomControls(health);
 try{await window.meleeNetCloseRuntimeLockstep('normal');lockstepRoomFinished=true;lockstepRoomStatus(lockstepStagingMode?'You left the room. Reload and import your disc to try again.':'Disconnected by your choice. Route outcome is not assessed.');if(lockstepStagingMode)$('lockstep-retry').hidden=false;}
 catch(error){lockstepRoomFinished=true;lockstepRoomStatus(`Error: intentional disconnect teardown was not confirmed (${error?.message||String(error)}).`);if(lockstepStagingMode)$('lockstep-retry').hidden=false;}
 finally{lockstepRoomLeaving=false;syncLockstepRoomControls();}
};
// Diagnostic A/B only. Visibility preserves layout and every DOM/counter update;
// activation and geometry checks happen before native replay entry, outside play.
function beginReplayPaintControl(){
  const mode=startupUrl.searchParams.get('hitch-ui-paint')||'normal';
  if(!['normal','hidden'].includes(mode))throw Error('Unknown diagnostic page-paint mode');
  if(mode==='hidden'&&!hitchCaptureFromUrl)throw Error('Page-paint control requires hitch capture');
  const root=document.documentElement,canvas=$('canvas');
  const geometry=()=>{const r=canvas.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height,buffer_width:canvas.width,buffer_height:canvas.height,dpr:devicePixelRatio};};
  const evidence={mode,diagnostic_only:mode==='hidden',started_ms:performance.now(),ended_ms:null,geometry_before:geometry(),geometry_after:null,restored:false};
  const restore=()=>{if(evidence.restored)return;root.classList.remove('hitch-paint-hidden');evidence.ended_ms=performance.now();evidence.restored=true;};
  try{
    if(mode==='hidden')root.classList.add('hitch-paint-hidden');
    evidence.geometry_after=geometry();
    if(JSON.stringify(evidence.geometry_before)!==JSON.stringify(evidence.geometry_after))throw Error('Page-paint control changed canvas geometry');
    return{evidence,restore};
  }catch(error){restore();throw error;}
}
const log=text=>$('log').textContent=($('log').textContent+'\n'+text).slice(-16000);
const STADIUM_TEXT_TAIL_MAX_RECORDS=8192;
const STADIUM_TEXT_TAIL_MAX_BYTES=1024*1024;
const STADIUM_TEXT_TAIL_PREFIXES=[
  'C1_SIS_TEXT_EVENT ',
  'C1_HUD_TEXT_PHASE ',
  'C1_NAMETAG_TEXT ',
  'STADIUM_TEXT_MEMBERSHIP_OBSERVATION ',
];
const stadiumTextTailProbe={
  enabled:false,records:[],bytes:0,totalRecords:0,totalBytes:0,
  droppedRecords:0,droppedBytes:0,overflow:false,
  reset(){this.enabled=false;this.records=[];this.bytes=0;this.totalRecords=0;
    this.totalBytes=0;this.droppedRecords=0;this.droppedBytes=0;this.overflow=false;},
  enable(){this.reset();this.enabled=true;return true;},
  disable(){this.enabled=false;return this.snapshot();},
  observe(text){
    if(!this.enabled||typeof text!=='string'||
       !STADIUM_TEXT_TAIL_PREFIXES.some(prefix=>text.startsWith(prefix)))return;
    const bytes=new TextEncoder().encode(text).byteLength;
    this.totalRecords++;this.totalBytes+=bytes;
    if(this.records.length>=STADIUM_TEXT_TAIL_MAX_RECORDS||
       this.bytes+bytes>STADIUM_TEXT_TAIL_MAX_BYTES){
      this.droppedRecords++;this.droppedBytes+=bytes;this.overflow=true;return;
    }
    // Addresses are opaque event identities for append/remove correlation;
    // this log grants no dereference or ownership authority.
    this.records.push(text);this.bytes+=bytes;
  },
  snapshot(){return{schema:'melee-web-stadium-first-sss-text-tail-log-v1',
    enabled:this.enabled,records:this.records.slice(),retained_records:this.records.length,
    retained_bytes:this.bytes,total_records:this.totalRecords,total_bytes:this.totalBytes,
    dropped_records:this.droppedRecords,dropped_bytes:this.droppedBytes,overflow:this.overflow,
    limits:{records:STADIUM_TEXT_TAIL_MAX_RECORDS,bytes:STADIUM_TEXT_TAIL_MAX_BYTES}};},
};
window.__meleeWebStadiumTextTailProbe=stadiumTextTailProbe;
if(typeof PerformanceObserver==='function'&&PerformanceObserver.supportedEntryTypes?.includes('longtask'))new PerformanceObserver(list=>{for(const entry of list.getEntries()){browserLongTasks++;browserLongTaskWorst=Math.max(browserLongTaskWorst,entry.duration);log(`Browser long task ${JSON.stringify({started:entry.startTime,duration_ms:entry.duration,name:entry.name})}`);}}).observe({type:'longtask',buffered:true});
function resetTiming(clearConstruction=true){frameLast=0;perfLastReport=0;activeFrames=0;worstFrame=0;worstBrowserCallback=null;longFrames=0;browserLongTasks=0;browserLongTaskWorst=0;nativeTimingFrames=0;nativeTimingWorst=0;nativeLongFrames=0;nativeOverBudgetFrames=0;nativeSourceSteps=0;nativeSourceDraws=0;nativeWorstTiming=null;nativeFirstUse=0;nativeFirstUseWorst=0;nativePreviousTiming=null;nativeLastTiming=null;livePipelineQueued=0;livePipelineCreated=0;liveTextureUploads=0;liveStagingUsedBytes=0;peakStagingUsedBytes=0;diagnosticCaptureInvalid=false;window.meleeHitchCapture?.reset?.();if(clearConstruction){constructionCounts={};constructionWorst=0;constructionFirstUseWorst=0;preparationCount=0;preparationWorst=0;preparationSince=0;preparationLabel='';preparationSequence=0;activePreparation=null;pendingEntryProfile=null;settlingEntryProfile=null;entryProfiles=[];latestNativePreparation=null;}$('render-metrics').textContent='Native timing diagnostics are waiting for the native player.';if(clearConstruction)$('construction-metrics').textContent='Scene construction diagnostics are waiting for the native player.';}
function renderConstructionMetrics(){const construction=Object.entries(constructionCounts).map(([name,count])=>`${name} ${count}`).join(' · ')||'none';const latest=entryProfiles.length?entryProfiles[entryProfiles.length-1]:null;const settle=latest&&latest.pipeline_settle_ms!==undefined?` · pipeline settle ${latest.pipeline_settle_ms.toFixed(2)} ms/${latest.pipeline_settle_frames} callbacks`:latest&&settlingEntryProfile===latest?` · pipelines settling (${latest.queued_total} initially queued)`:'';const profile=latest?` · latest ${latest.kind} prep ${latest.preparation_wall_ms.toFixed(2)} ms (construct ${latest.construction_ms.toFixed(2)}, wait/schedule ${latest.wait_schedule_ms.toFixed(2)}) · first draw ${latest.first_draw_ms.toFixed(2)} ms after ${latest.first_draw_delay_ms.toFixed(2)} ms · uploads ${latest.texture_upload_bytes} B · pipelines q${latest.queued_delta}/c${latest.created_delta}${settle}`:'';const p=latestNativePreparation,detail=p?` · native prep a${Number(p.audio_wait_ms).toFixed(2)} c${Number(p.construction_ms).toFixed(2)} r${Number(p.render_wait_ms).toFixed(2)} ms/${p.callbacks} callbacks (CPU ${Number(p.render_cpu_ms).toFixed(2)}, max ${Number(p.max_callback_ms).toFixed(2)}, uploads ${p.texture_upload_bytes} B, pipelines q${p.queued_delta}/c${p.created_delta})`:'';$('construction-metrics').textContent=`Construction ${construction} · worst ${constructionWorst.toFixed(2)} ms · construct ${constructionFirstUseWorst.toFixed(2)} ms · preparations ${preparationCount} (worst ${preparationWorst.toFixed(2)} ms; source entry/teardown separate)${profile}${detail}`;}
window.menuConstructionTiming=data=>{const kind=data.kind||'unknown';constructionCounts[kind]=(constructionCounts[kind]||0)+1;constructionWorst=Math.max(constructionWorst,data.total_ms||0);constructionFirstUseWorst=Math.max(constructionFirstUseWorst,data.construct_ms||0);const profile=activePreparation||pendingEntryProfile;if(profile&&!profile.first_draw)profile.construction.push(data);log(`Native construction ${JSON.stringify(data)}`);renderConstructionMetrics();};
window.menuPreparationProfile=data=>{latestNativePreparation=data;const profile=activePreparation||pendingEntryProfile;if(profile)profile.native_preparation=data;log(`Native preparation profile ${JSON.stringify(data)}`);renderConstructionMetrics();};
// Keep one worst callback for budget diagnosis; no per-frame state trace or log.
window.menuRuntimeTimingError=error=>{diagnosticCaptureInvalid=true;stop(error);};
window.menuRuntimeTiming=data=>{if(!data.valid)return;const now=performance.now(),total=data.total_ms||0,preparation=data.preparation_ms||0,active=Math.max(0,total-preparation),resourceActivity=Number(data.queued_delta||0)||Number(data.created_delta||0)||Number(data.texture_upload_bytes||0);nativePreviousTiming=nativeLastTiming;nativeLastTiming=data;nativeTimingFrames++;nativeSourceSteps+=Number(data.source_steps||0);nativeSourceDraws+=Number(data.source_draws||0);const hitch=window.meleeHitchCapture?.isEnabled?.()?window.meleeHitchCapture.observeNativeTiming?.({current:data,previous:nativePreviousTiming,activeMs:active,totalMs:total,preparationMs:preparation,timestamp:now,context:{cache_state:Module.runtimeCacheState?.state||'unknown',cache_dirty:!!Module.runtimeCacheState?.dirty}}):null;if(hitch?.invalid||window.meleeHitchCapture?.isInvalid?.()){diagnosticCaptureInvalid=true;if(retailRun&&!retailRun.failure)retailRun.failure='Diagnostic hitch capture overflow';}if(active>1000/60)nativeOverBudgetFrames++;if(active>nativeTimingWorst){nativeTimingWorst=active;nativeWorstTiming={...data,active_ms:active,source:active>1000/60?Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()):null};}if(active>1000/30)nativeLongFrames++;if(!preparation){livePipelineQueued+=Math.max(0,Number(data.queued_delta||0));livePipelineCreated+=Math.max(0,Number(data.created_delta||0));liveTextureUploads+=Math.max(0,Number(data.texture_upload_bytes||0));const stagingUsed=Math.max(0,Number(data.staging_used_bytes||0));liveStagingUsedBytes+=stagingUsed;peakStagingUsedBytes=Math.max(peakStagingUsedBytes,stagingUsed);}if(settlingEntryProfile){settlingEntryProfile.pipeline_settle_frames++;if(Number(data.queued_total||0)===0){settlingEntryProfile.pipeline_settle_ms=Math.max(0,now-settlingEntryProfile.first_draw_at);log(`Scene pipeline settle ${JSON.stringify({sequence:settlingEntryProfile.sequence,kind:settlingEntryProfile.kind,duration_ms:settlingEntryProfile.pipeline_settle_ms,callbacks:settlingEntryProfile.pipeline_settle_frames,created_total:Number(data.created_total||0)})}`);settlingEntryProfile=null;renderConstructionMetrics();}}if(data.first_use){nativeFirstUse++;nativeFirstUseWorst=Math.max(nativeFirstUseWorst,active);if(pendingEntryProfile){const profile=pendingEntryProfile;pendingEntryProfile=null;profile.first_draw=true;profile.first_draw_at=now;profile.kind=profile.construction.length?profile.construction[profile.construction.length-1].kind:'source-scene';profile.construction_ms=profile.construction.reduce((sum,item)=>sum+Number(item.total_ms||0),0);profile.wait_schedule_ms=Math.max(0,profile.preparation_wall_ms-profile.construction_ms);profile.first_draw_delay_ms=Math.max(0,now-profile.preparation_done);profile.first_draw_ms=total;profile.draw_ms=Number(data.draw_ms||0);profile.end_ms=Number(data.end_ms||0);profile.texture_upload_bytes=Number(data.texture_upload_bytes||0);profile.queued_delta=Number(data.queued_delta||0);profile.created_delta=Number(data.created_delta||0);profile.queued_total=Number(data.queued_total||0);profile.created_total=Number(data.created_total||0);profile.wasm_heap_bytes=Number(data.wasm_heap_bytes||0);profile.render_cache_state=Module.runtimeCacheState?.state||'unknown';profile.render_cache_bytes=Number(Module.runtimeCacheState?.fileBytes||0);profile.pipeline_settle_frames=0;if(profile.queued_total)settlingEntryProfile=profile;else profile.pipeline_settle_ms=0;entryProfiles.push(profile);log(`Scene entry profile ${JSON.stringify(profile)}`);renderConstructionMetrics();}}if(data.first_use||active>1000/30||preparation||resourceActivity)log(`${!preparation&&resourceActivity?'Live render resource':'Native callback'} ${JSON.stringify({...data,source:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())})}`);};
developmentHooks.preparation=(label,keepAudio=false)=>{$('status').dataset.runtimeError='';preparationLabel=label||'Preparing original scene';preparationKeepsAudio=!!keepAudio;preparationSince=performance.now();window.meleeHitchCapture?.setPreparation?.(true,preparationSince);activePreparation={sequence:++preparationSequence,label:preparationLabel,requested_at:preparationSince,construction:[],first_draw:false,audio_continuity:preparationKeepsAudio};uiMessage='';$('status').textContent=`${preparationLabel} · audio ${preparationKeepsAudio?'continuing':'paused'}`;};
developmentHooks.preparationDone=()=>{if(preparationSince){const now=performance.now();window.meleeHitchCapture?.setPreparation?.(false,now);const duration=now-preparationSince;preparationCount++;preparationWorst=Math.max(preparationWorst,duration);if(activePreparation){activePreparation.preparation_done=now;activePreparation.preparation_wall_ms=duration;pendingEntryProfile=activePreparation;activePreparation=null;}log(`Native preparation boundary {"label":${JSON.stringify(preparationLabel)},"duration_ms":${duration.toFixed(3)}}`);renderConstructionMetrics();}preparationSince=0;preparationLabel='';uiMessage='';};
developmentHooks.preparationCanceled=()=>{window.meleeHitchCapture?.setPreparation?.(false);preparationSince=0;preparationLabel='';preparationKeepsAudio=false;activePreparation=null;pendingEntryProfile=null;uiMessage='';};
developmentHooks.preparationFailed=error=>{window.meleeHitchCapture?.setPreparation?.(false);preparationSince=0;preparationLabel='';preparationKeepsAudio=false;activePreparation=null;pendingEntryProfile=null;uiMessage=error||'Native preparation failed';$('status').dataset.runtimeError=uiMessage;$('status').textContent=uiMessage;if(retailRun)retailRun.failure=uiMessage;};
developmentHooks.cacheSettled=()=>{Module.markRuntimeCacheDirty?.();};
developmentHooks.cacheWriteFailed=message=>{$('cache-status').textContent=message;};
window.menuCacheWritesFlushed=data=>{
  if(data.ok&&data.flushed)Module.markRuntimeCacheDirty?.();
  if(!data.ok)$('cache-status').textContent='Optional render cache was not saved: native cache writes failed. Reload to retry storage.';
  log(`Native cache transaction flush ${JSON.stringify(data)}`);
};
developmentHooks.frame=wasRunning=>{
 if(!ready||fatal)return;
 try{runtimeLockstepEntry?.onFrame();}catch(error){owner.stop(error);}
 const now=performance.now();
 const active=!importing&&!document.hidden&&!!wasRunning;
 if(active){
  if(frameLast){const interval=now-frameLast;if(interval>worstFrame){worstFrame=interval;worstBrowserCallback={interval_ms:interval,started:frameLast,ended:now,native:nativeLastTiming?{...nativeLastTiming}:null,previous_native:nativePreviousTiming?{...nativePreviousTiming}:null};}if(interval>1000/30){++longFrames;const hitch=window.meleeHitchCapture?.isEnabled?.()?window.meleeHitchCapture.observeBrowserGap?.({started:frameLast,ended:now,intervalMs:interval,timestamp:now,currentNative:nativeLastTiming,previousNative:nativePreviousTiming,preparation:!!preparationSince,context:{cache_state:Module.runtimeCacheState?.state||'unknown',cache_dirty:!!Module.runtimeCacheState?.dirty}}):null;if(hitch?.invalid||window.meleeHitchCapture?.isInvalid?.()){diagnosticCaptureInvalid=true;if(retailRun&&!retailRun.failure)retailRun.failure='Diagnostic hitch capture overflow';}log(`Browser callback gap ${JSON.stringify({interval_ms:interval,native:nativeLastTiming,source:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),cache_state:Module.runtimeCacheState?.state||'unknown',cache_dirty:!!Module.runtimeCacheState?.dirty,preparation:preparationLabel||null})}`);}}
  frameLast=now;++activeFrames;
 }else frameLast=0;
 if(now-perfLastReport>=250){
  $('perf-metrics').textContent=`Active browser callbacks ${activeFrames} · worst callback interval ${worstFrame.toFixed(2)} ms · >33.3 ms ${longFrames} · browser long tasks ${browserLongTasks} (worst ${browserLongTaskWorst.toFixed(2)} ms)`;
  const last=nativeLastTiming;const phase=last?`last phases p${Number(last.preparation_ms||0).toFixed(2)} i${Number(last.input_ms||0).toFixed(2)} s${Number(last.simulation_audio_ms||0).toFixed(2)} b${Number(last.begin_ms||0).toFixed(2)} d${Number(last.draw_ms||0).toFixed(2)} e${Number(last.end_ms||0).toFixed(2)} ms`:'last phases —';
  $('render-metrics').textContent=`Native callbacks ${nativeTimingFrames} · worst active callback ${nativeTimingWorst.toFixed(2)} ms · >16.67 ms ${nativeOverBudgetFrames} · >33.3 ms ${nativeLongFrames} · first-use draws ${nativeFirstUse} (worst ${nativeFirstUseWorst.toFixed(2)} ms) · ${phase} (preparation excluded here and reported separately; full first-use/long callbacks in log)`;
  if($('scene-check').closest('details').open)$('scene-check').textContent=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
  if($('input').closest('details').open)$('input').textContent=Module.UTF8ToString(Module._melee_web_input_message());
  perfLastReport=now;
 }
 if(preparationSince)$('status').textContent=`${preparationLabel} · ${Math.round(now-preparationSince)} ms · audio ${preparationKeepsAudio?'continuing':'paused'}`;else if(!importing&&!uiMessage)$('status').textContent=Module.UTF8ToString(Module._melee_web_native_menu_message());
 $('status').dataset.phase=Module._melee_web_native_menu_phase();
 window.menuReplayPoll?.();
 const nativePhase=Module._melee_web_native_menu_phase();if(nativePhase!==7)stockCheckActive=false;
 $('stock-check').disabled=!Module._melee_web_native_menu_stock_check_ready();
 $('pad-send').disabled=!!retailRun||importing||!!preparationSince||fatal||!Module._melee_web_native_menu_running()||stockCheckActive||actionSweepActive;
 $('action-sweep').disabled=!!retailRun||actionSweepActive||fatal||stockCheckActive||nativePhase!==7||!Module._melee_web_native_menu_running();
 $('drive-marth').disabled=selectionDriveActive||actionSweepActive||fatal||stockCheckActive||nativePhase!==1||!Module._melee_web_native_menu_running();
 $('drive-dream-land').disabled=selectionDriveActive||actionSweepActive||fatal||stockCheckActive||nativePhase!==3||!Module._melee_web_native_menu_running();
 syncAudio();
};
for(const type of ['focus','blur','visibilitychange','focusin','focusout'])window.addEventListener(type,event=>{inputDirty=true;const lost=document.hidden||!document.hasFocus();if(actionSweepActive&&lost)actionSweepFocusLost=true;if(retailRun&&lost){retailRun.focusLost=true;retailRun.firstFocusLoss??={at:performance.now(),type:event.type,hidden:document.hidden,active_element:document.activeElement?.id||null,source_started:!!retailRun.baseline,preparation:preparationLabel||null};}},true);
$('stock-check').onclick=()=>boundary(()=>check(Module._melee_web_native_menu_stock_check())).then(()=>{stockCheckActive=true;$('canvas').focus();inputDirty=true;});
$('confirm-check').onclick=()=>boundary(()=>Module._melee_web_native_menu_confirm_check()).then(()=>{$('canvas').focus();inputDirty=true;});
async function driveSourceSelection(button,nativeStep,kind,label){if(selectionDriveActive)return;selectionDriveActive=true;button.disabled=true;$('canvas').focus();inputDirty=true;try{for(let tick=0;tick<240;tick++){const result=await boundary(()=>nativeStep(kind));check(result);if(result===2){$('pad-status').textContent=`${label} reached through original source input after ${tick} steps.`;return;}await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));}throw Error(`${label} did not converge within 240 source input steps.`);}catch(error){padError(error);}finally{selectionDriveActive=false;}}
$('drive-marth').onclick=()=>driveSourceSelection($('drive-marth'),Module._melee_web_native_menu_drive_fighter,9,'Marth selection');
$('drive-dream-land').onclick=()=>driveSourceSelection($('drive-dream-land'),Module._melee_web_native_menu_drive_stage,28,'Dream Land cursor');
function padNumber(id){const raw=$(id).value.trim();if(!raw)throw Error(`${id} must be numeric`);const value=Number(raw);if(!Number.isInteger(value))throw Error(`${id} must be an integer`);return value;}
function padError(error){$('pad-status').textContent=error.message||String(error);log($('pad-status').textContent);}
$('pad-send').onclick=()=>{try{
 const port=padNumber('pad-port'),buttons=padNumber('pad-buttons'),stickX=padNumber('pad-stick-x'),stickY=padNumber('pad-stick-y'),duration=padNumber('pad-duration');
 boundary(()=>check(Module._melee_web_native_menu_pad_sample(port,buttons,stickX,stickY,duration))).then(()=>{$('pad-status').textContent=`Queued P${port+1} PAD sample for ${duration} source tick${duration===1?'':'s'}.`;inputDirty=true;}).catch(padError);
}catch(error){padError(error);}};
/* Read-only source observations and the same checked raw-PAD queue used by the
 * visible diagnostics. Kept on window so browser automation can validate the
 * real CSS/SSS route without writing source selection globals. */
window.menuObserveFighter=kind=>{const ids=Module._malloc(14*4),geometry=Module._malloc(8*4);if(!ids||!geometry){if(ids)Module._free(ids);if(geometry)Module._free(geometry);return null;}try{if(!Module._melee_web_css_observe(kind,ids,geometry))return null;return{ids:Array.from(Module.HEAP32.subarray(ids>>2,(ids>>2)+4)),geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8))};}finally{Module._free(ids);Module._free(geometry);}};
window.menuObserveCssSetup=()=>{if(!Module._melee_web_native_menu_phase||Module._melee_web_native_menu_phase()!==1)return null;const cursors=Module._malloc(64),doors=Module._malloc(160),geometry=Module._malloc(192);if(!cursors||!doors||!geometry){if(cursors)Module._free(cursors);if(doors)Module._free(doors);if(geometry)Module._free(geometry);return null;}try{if(!Module._melee_web_css_observe_setup(cursors,doors,geometry))return null;return{cursors:Array.from(Module.HEAP32.subarray(cursors>>2,(cursors>>2)+16)),doors:Array.from(Module.HEAP32.subarray(doors>>2,(doors>>2)+40)),geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+48))};}finally{Module._free(cursors);Module._free(doors);Module._free(geometry);}};
window.menuObserveFighterPort=(port,kind)=>{if(!Module._melee_web_native_menu_phase||Module._melee_web_native_menu_phase()!==1)return null;const ids=Module._malloc(14*4),geometry=Module._malloc(8*4);if(!ids||!geometry){if(ids)Module._free(ids);if(geometry)Module._free(geometry);return null;}try{if(!Module._melee_web_css_observe_port(port,kind,ids,geometry))return null;return{ids:Array.from(Module.HEAP32.subarray(ids>>2,(ids>>2)+4)),geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8))};}finally{Module._free(ids);Module._free(geometry);}};
window.menuObserveStage=kind=>{const ids=Module._malloc(16),geometry=Module._malloc(24);try{if(!Module._melee_web_sss_observe(kind,ids,geometry))return null;return{ids:Array.from(Module.HEAP32.subarray(ids>>2,(ids>>2)+4)),geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+6))};}finally{Module._free(ids);Module._free(geometry);}};
window.menuDiagnosticPad=(port,buttons,stickX,stickY,duration=1)=>boundary(()=>check(Module._melee_web_native_menu_pad_sample(port,buttons,stickX,stickY,duration)));
window.menuDiagnosticPadFull=(port,buttons,stickX,stickY,cstickX,cstickY,triggerL,triggerR,duration=1)=>boundary(()=>check(Module._melee_web_native_menu_pad_sample_full(port,buttons,stickX,stickY,cstickX,cstickY,triggerL,triggerR,duration)));
window.menuObservePlayer=(player=0)=>{const storage=Module._malloc(24);try{if(!Module._melee_web_native_menu_player_state(player,storage,storage+4,storage+8,storage+12,storage+16,storage+20))return null;return{fighterKind:Module.HEAP32[storage>>2],motion:Module.HEAP32[(storage>>2)+1],groundAir:Module.HEAP32[(storage>>2)+2],frame:(Module.HEAP32[(storage>>2)+3]>>>0),x:Module.HEAPF32[(storage>>2)+4],y:Module.HEAPF32[(storage>>2)+5]};}finally{Module._free(storage);}};
const nextAnimationFrame=()=>new Promise(resolve=>requestAnimationFrame(resolve));
async function waitSourceFrame(target,observe){let stagnantSince=performance.now(),last=-1;for(;;){await nextAnimationFrame();const state=window.menuObservePlayer();if(!state)throw Error('The source match stopped before the sweep completed.');observe?.(state);if(state.frame>=target)return state;if(state.frame!==last){last=state.frame;stagnantSince=performance.now();continue;}const stoppedFor=performance.now()-stagnantSince;if(stoppedFor>500&&status().startsWith('Paused after a timing disruption')){await boundary(()=>Module._melee_web_native_menu_pause(0));sweepAutomaticResumes++;stagnantSince=performance.now();continue;}if(stoppedFor>1800)throw Error(`Source frame ${state.frame} stopped advancing: ${status()}`);}}
async function sweepSample(sample,observe){let remaining=sample.duration,state=window.menuObservePlayer();if(!state)throw Error('A ready source player is required.');while(remaining){const duration=Math.min(120,remaining),target=state.frame+duration;await window.menuDiagnosticPadFull(0,sample.buttons,sample.stickX,sample.stickY,sample.cstickX,sample.cstickY,sample.triggerL,sample.triggerR,duration);state=await waitSourceFrame(target,observe);remaining-=duration;}return state;}
async function settleSweepPlayer(observe){
 const neutral=duration=>({buttons:0,stickX:0,stickY:0,cstickX:0,cstickY:0,triggerL:0,triggerR:0,duration});
 const firstFrame=window.menuObservePlayer()?.frame;
 for(let n=0;n<180;n++){
  const state=window.menuObservePlayer();
  if(!state||state.frame-firstFrame>720)break;
  // Source Ottotto/OttottoWait (245/246) persist at platform edges under neutral
  // input. They accept movement like Wait; waiting longer cannot recover them.
  if(state.groundAir===0&&((state.motion>=14&&state.motion<=23)||[245,246].includes(state.motion))){
   if(state.motion===14&&Math.abs(state.x)<=12){await sweepSample(neutral(2),observe);return;}
   // Walk with frequent position feedback. Fixed dash/release bursts can
   // oscillate past center or repeatedly turn a fighter without moving him.
   if(Math.abs(state.x)>12)await sweepSample({...neutral(4),stickX:state.x>0?-40:40},observe);
   else await sweepSample(neutral(24),observe);
  }else await sweepSample(neutral(120),observe);
 }
 throw Error(`P1 did not return to the grounded source Wait state near stage center within the bounded source-input recovery period: ${JSON.stringify(window.menuObservePlayer())}`);
}
function sweepReportText(report){return JSON.stringify(report,null,2);}
async function runActionSweep(){if(actionSweepActive)return;actionSweepActive=true;actionSweepFocusLost=false;$('action-sweep').disabled=true;$('canvas').focus();inputDirty=true;await nextAnimationFrame();await nextAnimationFrame();const initial=window.menuObservePlayer(),caseReports=[];let currentCase='initialization',report,inventory=null;try{
 if(!initial)throw Error('Start a ready match before running the action sweep.');
 const {actionInventory}=await import('./action-sweep.mjs');inventory=actionInventory(initial.fighterKind);if(!inventory)throw Error(`No versioned action inventory exists for fighter kind ${initial.fighterKind}.`);
 resetTiming(false);sweepAutomaticResumes=0;const baseline={audioUnderruns:Number(latestAudio.underruns||0),preparations:preparationCount,sourceFrame:initial.frame,wasmHeap:Number(Module.HEAPU8.length)};
 $('action-report').textContent=`Running ${inventory.fighter} visible action sweep…`;
 for(const test of inventory.cases){currentCase=test.name;const seen=new Set();const observe=state=>seen.add(state.motion);if(test.settle)await settleSweepPlayer(null);for(const sample of test.inputs)await sweepSample(sample,observe);const missing=test.expect.filter(([first,last])=>![...seen].some(value=>value>=first&&value<=last));if(missing.length)throw Error(`${test.name} did not reach expected source motion range(s) ${JSON.stringify(missing)}; saw ${JSON.stringify([...seen])}`);caseReports.push({name:test.name,pass:true,sourceMotions:[...seen]});$('action-report').textContent=`Running ${inventory.fighter} visible action sweep…\n${caseReports.length}/${inventory.cases.length} ${test.name}`;}
 currentCase='stage scheduler soak';let state=window.menuObservePlayer();while(state.frame-baseline.sourceFrame<inventory.minimumStageFrames){const remaining=inventory.minimumStageFrames-(state.frame-baseline.sourceFrame);state=await sweepSample({buttons:0,stickX:0,stickY:0,cstickX:0,cstickY:0,triggerL:0,triggerR:0,duration:Math.min(120,remaining)},null);}
 const metrics={sourceFrames:state.frame-baseline.sourceFrame,browserCallbacks:activeFrames,worstBrowserCallbackMs:Number(worstFrame.toFixed(3)),browserCallbackGaps:longFrames,browserLongTasks,browserLongTaskWorstMs:Number(browserLongTaskWorst.toFixed(3)),nativeCallbacks:nativeTimingFrames,worstNativeCallbackMs:Number(nativeTimingWorst.toFixed(3)),nativeCallbacksOverBudget:nativeOverBudgetFrames,nativeCallbacksOver33ms:nativeLongFrames,audioUnderrunFrames:Number(latestAudio.underruns||0)-baseline.audioUnderruns,livePipelinesQueued:livePipelineQueued,livePipelinesCreated:livePipelineCreated,liveTextureUploadBytes:liveTextureUploads,liveStagingUsedBytes,peakStagingUsedBytes,preparationPauses:preparationCount-baseline.preparations,automaticTimingResumes:sweepAutomaticResumes,focusLost:actionSweepFocusLost,wasmHeapGrowthBytes:Number(Module.HEAPU8.length)-baseline.wasmHeap,diagnostic_capture:window.meleeHitchCapture?.report?.()||null};
 const failures=[];if(metrics.browserCallbackGaps)failures.push('browser callback gap');if(metrics.browserLongTasks)failures.push('browser long task');if(metrics.nativeCallbacksOver33ms)failures.push('native callback over 33.3 ms');if(diagnosticCaptureInvalid)failures.push('diagnostic capture invalid');if(metrics.audioUnderrunFrames)failures.push('audio underrun');if(metrics.livePipelinesQueued||metrics.livePipelinesCreated)failures.push('live pipeline creation');if(metrics.preparationPauses)failures.push('automatic render preparation pause');if(metrics.automaticTimingResumes)failures.push('automatic timing pause');if(metrics.focusLost)failures.push('focus/visibility loss');if(!Module._melee_web_native_menu_running())failures.push(status());report={version:1,inventory:inventory.id,fighter:inventory.fighter,fighterKind:initial.fighterKind,pass:failures.length===0,failedCase:failures.length?'acceptance':null,failures,metrics,cases:caseReports,cache:{state:Module.runtimeCacheState?.state||'unknown',bytes:Number(Module.runtimeCacheState?.fileBytes||0),cleared_on_startup:clearRenderCacheOnLoad,driver_cache:'uncontrolled'}};
 }catch(error){report={version:1,inventory:inventory?.id||null,fighter:inventory?.fighter||null,fighterKind:initial?.fighterKind??null,pass:false,failedCase:currentCase,failures:[error.message||String(error)],metrics:{browserCallbackGaps:longFrames,browserLongTasks,nativeCallbacksOverBudget:nativeOverBudgetFrames,nativeCallbacksOver33ms:nativeLongFrames,audioUnderrunFrames:Number(latestAudio.underruns||0),livePipelinesQueued:livePipelineQueued,livePipelinesCreated:livePipelineCreated,liveTextureUploadBytes:liveTextureUploads,focusLost:actionSweepFocusLost,diagnostic_capture:window.meleeHitchCapture?.report?.()||null},cases:caseReports};}
 finally{actionSweepActive=false;$('action-report').textContent=sweepReportText(report);log(`Action performance report ${JSON.stringify(report)}`);window.lastActionSweepReport=report;}
}
$('action-sweep').onclick=runActionSweep;
async function reloadApplication(clearRenderCache){
 for(const id of ['disc','launch','pause','unload','reload-app','reset-render-cache','export-render-cache','pad-send'])$(id).disabled=true;
 try{if(ready&&!fatal)await unloadAndSave();}catch(error){log(`Application reload teardown: ${error.message||String(error)}`);}
 const next=new URL(location.href);if(clearRenderCache)next.searchParams.set('render-cache','clear');else next.searchParams.delete('render-cache');location.replace(next);
}
$('reload-app').onclick=()=>reloadApplication(false);
$('reset-render-cache').onclick=()=>reloadApplication(true);
let cacheEvidence=[],replayEvidence=[];
async function saveEvidence(artifacts){if(retailRun||(!fatal&&Module._melee_web_native_menu_running()))throw Error('Unload before saving evidence');const saved=[];for(const [name,bytes] of artifacts){const response=await fetch(`/__melee_evidence/${name}`,{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:bytes});if(!response.ok)throw Error(`Local evidence save failed (${response.status}); start the loopback server with --evidence-directory`);saved.push(await response.json());}$('evidence-status').textContent=JSON.stringify(saved,null,2);}
$('save-replay-evidence').onclick=()=>saveEvidence(replayEvidence).catch(error=>$('evidence-status').textContent=error.message);
$('save-cache-evidence').onclick=()=>saveEvidence(cacheEvidence).catch(error=>$('evidence-status').textContent=error.message);
$('export-render-cache').onclick=async()=>{try{const cacheState=await boundary(()=>Module._melee_web_native_menu_cache_idle());if(cacheState===0)throw Error('Unload before exporting cache');if(cacheState!==1)throw Error('Native cache writes failed; export is unavailable until reload');if(!await Module.saveRuntimeCache?.())throw Error('Cache persistence failed; export was not completed');cacheEvidence=[];const exports=$('cache-exports');for(const old of exports.querySelectorAll('a'))URL.revokeObjectURL(old.href);exports.replaceChildren();for(const name of Module.FS.readdir('/melee-render-cache')){if(name==='.'||name==='..')continue;const bytes=Module.FS.readFile(`/melee-render-cache/${name}`),link=document.createElement('a');link.href=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'}));if(name==='pipeline_cache.db'||(name==='pipeline_cache.db-wal'&&bytes.length)){cacheEvidence.push([name.replace('pipeline_cache','render-cache'),bytes]);$('save-cache-evidence').disabled=false;}link.download=`melee-render-${name}`;link.textContent=`Download ${name}`;exports.append(' ',link);}}catch(error){log(`Render cache export: ${error.message||String(error)}`);}};
// Replay setup/decoding and evidence export stay outside the source clock.
let retailRun=null,replayLoading=false;
const STOPPED_EVIDENCE_LEASE_MS=30000;
let stoppedEvidenceSequence=0;
// Capture tools own the stopped replay through exports, not only its pause.
// Acquisition and native reason-5 acknowledgement share one synchronous turn.
window.menuReplayAcquireStoppedEvidence=(cursor,pauseAndAcknowledge)=>{
 const run=retailRun;
 const available=()=>retailRun===run&&run&&!run.observe&&run.baseline&&!run.finishing&&!run.completed&&!run.failure&&!fatal&&!preparationSince&&!run.stoppedEvidence&&!window.meleeHitchCapture?.isInvalid?.();
 if(!available()||!Number.isSafeInteger(cursor)||cursor<1||Module._melee_web_native_menu_replay_cursor()!==cursor||typeof pauseAndAcknowledge!=='function')throw Error('Stopped evidence requires an available performance replay owner and exact cursor');
 const pause=pauseAndAcknowledge();
 if(!available()||pause?.acknowledged!==true||Module._melee_web_native_menu_running()!==0||Module._melee_web_native_menu_replay_cursor()!==cursor)throw Error('Stopped evidence requires a fresh native pause acknowledgement and unchanged owner/cursor');
 const acquired=performance.now();
 const lease={token:++stoppedEvidenceSequence,cursor,acquired_at_ms:acquired,expires_at_ms:acquired+STOPPED_EVIDENCE_LEASE_MS,maximum_duration_ms:STOPPED_EVIDENCE_LEASE_MS};
 run.stoppedEvidence=lease;return{...lease,pause};
};
window.menuReplayFinishStoppedEvidence=async(token,reason)=>{
 const run=retailRun;
 if(!run||run.stoppedEvidence?.token!==token)throw Error('Stopped evidence lease owner/token is no longer available');
 run.stoppedEvidence.released_at_ms=performance.now();
 run.stoppedEvidence.release_reason=reason;
 await finishRetailReplay(reason);
 return{...run.stoppedEvidence};
};
// Keep read-only diagnostics used by local capture tools after moving out of the inline script.
Object.defineProperties(window, {
  nativeSourceSteps: {get: () => nativeSourceSteps},
  retailRun: {get: () => retailRun},
});
const replayHash=async bytes=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),v=>v.toString(16).padStart(2,'0')).join('');
function replayDownload(name,text){replayEvidence.push([name,text]);$('save-replay-evidence').disabled=false;const link=document.createElement('a');link.href=URL.createObjectURL(new Blob([text],{type:'application/json'}));link.download=name;link.textContent=`Download ${name}`;$('retail-replay-downloads').append(' ',link);}
// Sample allocator ownership outside the live source clock, never per tick.
const replayMemorySnapshot=()=>{
 // Do not walk potentially damaged allocator metadata after a native abort.
 if(fatal)return{available:false,reason:'runtime aborted'};
 try{return JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_memory()));}
 catch(error){return{available:false,reason:String(error.message||error)};}
};
function replayMetrics(run){return{sourceFrames:run.consumed||Module._melee_web_native_menu_replay_cursor(),worstBrowserCallback,sourceSteps:nativeSourceSteps,sourceDraws:nativeSourceDraws,browserCallbacks:activeFrames,worstBrowserCallbackMs:worstFrame,browserCallbackGaps:longFrames,browserLongTasks,browserLongTaskWorstMs:browserLongTaskWorst,nativeCallbacks:nativeTimingFrames,worstNativeCallbackMs:nativeTimingWorst,nativeCallbacksOver33ms:nativeLongFrames,nativeCallbacksOverBudget:nativeOverBudgetFrames,worstNativeCallback:nativeWorstTiming,livePipelinesQueued:livePipelineQueued,livePipelinesCreated:livePipelineCreated,liveTextureUploadBytes:liveTextureUploads,liveStagingUsedBytes,peakStagingUsedBytes,preparationPauses:preparationCount-(run.baseline?.preparations??preparationCount),wasmHeapGrowthBytes:Module.HEAPU8.length-(run.baseline?.heap??Module.HEAPU8.length),focusLost:run.focusLost,firstFocusLoss:run.firstFocusLoss||null};}
function finishRetailReplay(reason){
 const run=retailRun;if(!run)return Promise.resolve();if(run.finishing)return run.finishPromise;run.finishing=true;
 if(run.stoppedEvidence){run.stoppedEvidence.finished_at_ms=performance.now();run.stoppedEvidence.finish_reason=reason;}
 run.finishPromise=(async()=>{
 window.meleeHitchCapture?.stop?.();
 const metrics=replayMetrics(run),diagnosticCapture=window.meleeHitchCapture?.report?.()||null;run.memory.before_teardown=replayMemorySnapshot();let teardown=false;
 try{teardown=await unloadAndSave();if(!teardown)throw Error(status());}catch(error){reason=reason||error.message;}
 run.memory.after_teardown=replayMemorySnapshot();
 run.paintControl?.restore();
 // The disabled-state acknowledgement includes final audio counters.
 metrics.audioUnderrunFrames=Number(latestAudio.underruns||0)-(run.baseline?.underruns??0);
 metrics.audioOverflowFrames=Number(latestAudio.overflows||0)-(run.baseline?.overflows??0);
 if(diagnosticCapture?.invalid){diagnosticCaptureInvalid=true;if(!run.failure)run.failure='Diagnostic hitch capture overflow';}
 reason=reason||run.failure;
 const failures=[];if(run.diagnosticEntityPrefix){if(run.finalScene!==3||!Number.isInteger(run.prefixProgress?.bound_observations)||run.prefixProgress.bound_observations<(run.diagnosticEntityPrefixMode===2?61:60)||run.prefixProgress.bound_observations>(run.diagnosticEntityPrefixMode===2?604:64)||run.prefixProgress.observations!==run.prefixProgress.bound_observations||run.prefixProgress.first_source_tick!==0||run.prefixProgress.last_source_tick!==run.prefixProgress.bound_observations-1)failures.push('diagnostic prefix source boundary incomplete');}else if(run.wholeSession&&run.finalScene!==1)failures.push('whole-session final CSS was not entered');if(reason)failures.push(reason);if(!teardown)failures.push('teardown incomplete');if(metrics.sourceFrames!==run.frames)failures.push('incomplete input timeline');if(metrics.sourceSteps!==run.frames||metrics.sourceDraws!==(run.expectedDraws??run.frames))failures.push('source tick/draw count mismatch');
 if(diagnosticCaptureInvalid)failures.push('diagnostic capture invalid');if(!run.observe){for(const key of ['browserCallbackGaps','browserLongTasks','nativeCallbacksOver33ms','livePipelinesQueued','livePipelinesCreated','preparationPauses','audioUnderrunFrames','audioOverflowFrames','focusLost'])if(metrics[key])failures.push(key);}
 const sourceMatch=run.diagnosticEntityPrefix?{complete:false,outcome:null,winner:null}:run.sourceMatch||{complete:false,outcome:null,winner:null};
 const report={input_scheduling:['per_tick','startup_clock','recorded_queue'][run.scheduling??0],scheduling_equivalence:'not_evaluated',schema:'melee-web-browser-retail-replay',version:1,recipe_sha256:run.hash,frames:run.frames,mode:run.observe?'state_capture':'performance',complete:!run.diagnosticEntityPrefix&&teardown&&metrics.sourceFrames===run.frames&&!reason,...(run.diagnosticEntityPrefix?{diagnostic_prefix:run.diagnosticEntityPrefixMode===2?'jiggly-ice-mario-fox-active60-v1':'jiggly-ice-mario-fox-v1',diagnostic_prefix_complete:teardown&&failures.length===0,whole_session_equivalent:false,comparison_source_ticks:run.diagnosticEntityPrefixMode===2?(run.prefixProgress?.bound_observations??null):60,...(run.diagnosticEntityPrefixMode===2?{comparison_active_clock_ticks:60}:{}),source_progress:run.prefixProgress}:{}),pass:failures.length===0,failures,performance:run.observe?'not_evaluated':'measured',gold_admitted:false,pixels:'not_compared',source_match:sourceMatch,final_scene:run.finalScene??null,instrumented_timing_resumes:run.timingResumes||0,metrics,diagnostic_capture:diagnosticCapture,diagnostic_page_paint:run.paintControl?.evidence||null,stopped_evidence_lease:run.stoppedEvidence?{...run.stoppedEvidence}:null,memory:run.memory,preparation:run.preparation,cache:run.cache,user_agent:navigator.userAgent,resolution:[640,480],device_pixel_ratio:devicePixelRatio};
 if(run.observe){const trace=run.rows.join('\n')+'\n';report.trace_sha256=await replayHash(new TextEncoder().encode(trace));replayDownload('retail-port.jsonl',trace);if(run.timerRows.length){const timers=run.timerRows.join('\n')+'\n';report.timer_trace_sha256=await replayHash(new TextEncoder().encode(timers));replayDownload('retail-timer.jsonl',timers);}}
 const text=JSON.stringify(report,null,2);window.lastRetailReplayReport=report;$('retail-replay-report').textContent=text;replayDownload('retail-browser-report.json',text);retailRun=null;$('launch').disabled=fatal||!bundle;$('disc').disabled=fatal||importing;$('pause').disabled=$('unload').disabled=true;syncAudio();
 })();return run.finishPromise;
}
window.menuReplayStarted=(frames,observe,draws=frames,scheduling=0)=>{
 markFirstReplayBoundary('menu_replay_started_js_begin',frames,draws,scheduling);
 const run=retailRun;if(!run)throw Error('Missing browser replay owner');
 if(!Number.isInteger(draws)||draws<1||draws>frames)throw Error('Invalid replay clock draw count');
 if(![0,1,2].includes(scheduling)||(scheduling===2&&!observe))throw Error('Invalid replay scheduling scope');
 run.scheduling=scheduling;run.frames=frames;run.expectedDraws=draws;run.observe=observe;run.preparation=latestNativePreparation;
 markFirstReplayBoundary('native_memory_snapshot_begin',frames,draws,scheduling);
 run.memory.prepared=replayMemorySnapshot();
 markFirstReplayBoundary('native_memory_snapshot_returned',frames,draws,scheduling);
 resetTiming(false);
 run.baseline={preparations:preparationCount,heap:Module.HEAPU8.length,underruns:Number(latestAudio.underruns||0),overflows:Number(latestAudio.overflows||0)};
 run.lastProgress=performance.now();
 markFirstReplayBoundary('menu_replay_started_js_returned',frames,draws,scheduling);
};
window.menuReplayCompleted=(frames,matchComplete,outcome,winner,finalScene,diagnosticEntityPrefix=false,observations=0,firstSourceTick=0,lastSourceTick=0,boundObservations=0)=>{if(retailRun){if((retailRun.diagnosticEntityPrefixMode??(retailRun.diagnosticEntityPrefix?1:0))!==Number(diagnosticEntityPrefix)){const run=retailRun;run.failure=run.failure||'Native completion prefix mode disagrees with requested recipe';setTimeout(()=>{if(retailRun===run)return finishRetailReplay(run.failure);},0);return;}retailRun.prefixProgress={observations,bound_observations:boundObservations,first_source_tick:firstSourceTick,last_source_tick:lastSourceTick};retailRun.finalScene=finalScene;retailRun.consumed=frames;retailRun.completed=true;retailRun.sourceMatch={complete:!!matchComplete,outcome:Number.isInteger(outcome)?outcome:null,winner:Number.isInteger(winner)?winner:null};setTimeout(()=>finishRetailReplay(null),0);}};
window.menuReplayPoll=()=>{
 $('retail-replay-start').disabled=!ready||fatal||!bundle||replayLoading||!!retailRun||!$('retail-replay-file').files[0];
 const run=retailRun;if(!run||run.finishing||run.completed)return;
 if(run.failure){finishRetailReplay(run.failure);return;}
 if(window.meleeHitchCapture?.isInvalid?.()){diagnosticCaptureInvalid=true;if(!run.failure)run.failure='Diagnostic hitch capture overflow';}
 const now=performance.now(),cursor=Module._melee_web_native_menu_replay_cursor();
 if(cursor!==run.lastCursor){run.lastCursor=cursor;run.lastProgress=now;}
 const lease=run.stoppedEvidence;
 if(lease){
  const invalid=run.failure||(now>=lease.expires_at_ms?'Stopped evidence lease expired':
   cursor!==lease.cursor?'Stopped evidence cursor changed':
   Module._melee_web_native_menu_running()!==0?'Stopped evidence source resumed':
   preparationSince?'Stopped evidence preparation restarted':null);
  if(invalid){run.failure=invalid;finishRetailReplay(invalid);return;}
 }
 if(!lease&&run.baseline&&now-run.lastProgress>700){const reason=status();if(!Module._melee_web_native_menu_running()&&!preparationSince){
  // Diagnostic printf/JSON collection can disrupt scheduling. Only the explicitly
  // instrumented state run may resume; performance mode retains the hard failure.
  if(run.observe&&reason.startsWith('Paused after a timing disruption')){run.timingResumes=(run.timingResumes||0)+1;run.lastProgress=now;boundary(()=>Module._melee_web_native_menu_pause(0));}
  else finishRetailReplay(reason||'Replay stopped');
 }}
 if(now-run.started>retailReplayWallTimeMs(run.wholeSession))finishRetailReplay('Replay exceeded bounded wall time');
};
// Development-only networked-session entry (Track A1). It mirrors the
// whole-session replay entry: the canonical freshly prepared CSS owner receives
// one agreed seed, then the ordinary native launch enters it. Per-tick PAD
// frames and checksum records cross only the _melee_web_net_* exports.
// Private constructor-only diagnostic entry. The ordinary Play control still prepares.
let firstCssConstructorLaunchAttempted=false;
let freshFirstCssLaunchLease=null,freshFirstCssKickAttempted=false;
const launchPreparedFirstCss=async(expectedGeneration,fresh,finite=false)=>{
 if(firstCssConstructorLaunchAttempted)throw Error('CSS constructor launch was already attempted.');
 firstCssConstructorLaunchAttempted=true;
 const launchOwner=owner,launchModule=Module,launchHandle=owner?.handle;
 const launchBoundary=boundary,launchCheck=check;
 const assertIdentity=()=>{
  if(!launchOwner||owner!==launchOwner||Module!==launchModule||
     owner.handle!==launchHandle||boundary!==launchBoundary||check!==launchCheck)
   throw Error('CSS constructor launch owner changed.');
 };
 const assertOwner=()=>{
  assertIdentity();
  const state=launchHandle.getState();
  if(state.state!=='prepared'||state.busy||state.loading||state.requiresReload||
     state.ready!==true||state.canStart!==true||state.phase!==0||state.running)
   throw Error('CSS constructor launch requires an idle prepared owner.');
 };
 const assertArmed=()=>{
  assertOwner();
  if(!Number.isSafeInteger(expectedGeneration)||expectedGeneration<=0||
     typeof launchModule[finite?'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_arm':fresh?'_melee_web_native_menu_stadium_first_css_fresh_row_v3_arm':
       '_melee_web_native_menu_stadium_first_css_constructor_return_v2_arm']!=='function'||
     typeof launchModule._melee_web_native_menu_stadium_first_css_draw_observe!=='function')
   throw Error('Private CSS constructor launch capability is absent.');
  const pointer=launchModule._melee_web_native_menu_stadium_first_css_draw_observe();
  const observation=pointer?JSON.parse(launchModule.UTF8ToString(pointer)):null;
  if(observation?.schema!==(finite?'melee-web-stadium-first-css-finite-observation-v4':fresh?'melee-web-stadium-first-css-fresh-row-observation-v3':
       'melee-web-stadium-first-css-constructor-return-observation-v2')||
     observation.mode!==(finite?'fresh_finite_css_prefix_v4':fresh?'fresh_consumed_tick_draw_v3':'constructor_return_only_v2')||observation.state!=='armed'||
     observation.error!==null||observation.native_phase!==0||observation.running!==false||
     observation.host_entered!==false||observation.armed_owner_stable!==true||
     observation.armed_world_generation!==expectedGeneration||
     (!fresh&&observation.constructor_return_terminal!==false)||
     (fresh&&(observation.host_tick_calls!==0||observation.host_draw_calls!==0||
       observation.aurora_begin_calls!==0||observation.aurora_end_calls!==0))||
     observation.source_steps!==0||observation.source_draws!==0||
     launchModule._melee_web_native_menu_phase()!==0||
     launchModule._melee_web_native_menu_running()!==0)
   throw Error('CSS constructor launch requires the unchanged armed CREATED owner.');
 };
 // All native observations and entry use the existing owner command boundary.
 assertIdentity();
 if(typeof launchBoundary!=='function')throw Error('CSS constructor launch boundary is absent.');
 await launchBoundary(assertArmed);
 resetTiming(false);
 await prepareAudio();await pauseAudioForPreparation();await waitForAudioRender();
 await launchBoundary(()=>{assertArmed();launchCheck(launchModule._melee_web_native_menu_launch());});
 if(fresh)freshFirstCssLaunchLease={owner:launchOwner,Module:launchModule,handle:launchHandle,
   boundary:launchBoundary,check:launchCheck,generation:expectedGeneration,finite};
 $('canvas').focus();inputDirty=true;syncAudio();
};

window.meleeStadiumFirstCssConstructorReturnLaunch=generation=>launchPreparedFirstCss(generation,false);
window.meleeStadiumFirstCssFreshRowLaunch=generation=>launchPreparedFirstCss(generation,true);

window.meleeStadiumFirstCssFreshRowKick=async()=>{
 if(freshFirstCssKickAttempted)throw Error('Fresh CSS row kick was already attempted.');
 freshFirstCssKickAttempted=true;
 const lease=freshFirstCssLaunchLease;
 if(!lease)throw Error('Fresh CSS row has no successful prepared-launch lease.');
 return await lease.boundary(()=>{
  if(owner!==lease.owner||Module!==lease.Module||owner.handle!==lease.handle||
     boundary!==lease.boundary||check!==lease.check)
   throw Error('Fresh CSS row kick owner changed.');
  const state=lease.handle.getState();
  if(state.state!=='css'||state.busy||state.loading||state.requiresReload||
     state.phase!==1||state.running!==true)
   throw Error('Fresh CSS row requires its held CSS owner before kick.');
  const pointer=Module._melee_web_native_menu_stadium_first_css_draw_observe?.();
  const observed=pointer?JSON.parse(Module.UTF8ToString(pointer)):null;
  if(observed?.schema!=='melee-web-stadium-first-css-fresh-row-observation-v3'||
     observed.mode!=='fresh_consumed_tick_draw_v3'||observed.state!=='entered'||
     observed.error!==null||observed.armed_owner_stable!==true||
     observed.armed_world_generation!==lease.generation||
     observed.source_steps!==0||observed.source_draws!==0||
     observed.host_tick_calls!==0||observed.host_draw_calls!==0||
     observed.aurora_begin_calls!==0||observed.aurora_end_calls!==0||
     typeof Module._melee_web_native_menu_stadium_first_css_fresh_row_v3_kick!=='function')
   throw Error('Fresh CSS row kick lost its unchanged one-use CSS entry.');
  return lease.check(Module._melee_web_native_menu_stadium_first_css_fresh_row_v3_kick());
 });
};

// Finite mode uses the same prepared launch and owner command queue. Each
// stage reserves its one-use attempt before enqueueing and rechecks after wait.
window.meleeStadiumFirstCssFreshFiniteLaunch=generation=>launchPreparedFirstCss(generation,true,true);
const freshFiniteCssAttempts=new Set();
window.meleeStadiumFirstCssFreshFiniteStage=async stage=>{
 const exports={first:'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_first_kick',
  stream_arm:'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm',
  stream_kick:'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick',
  final_arm:'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm',
  final_kick:'_melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_kick'};
 if(!Object.hasOwn(exports,stage)||freshFiniteCssAttempts.has(stage))
  throw Error('Fresh finite CSS stage is unknown or already attempted.');
 freshFiniteCssAttempts.add(stage);
 const lease=freshFirstCssLaunchLease;
 if(!lease?.finite)throw Error('Fresh finite CSS has no prepared finite-owner lease.');
 return await lease.boundary(()=>{
  if(owner!==lease.owner||Module!==lease.Module||owner?.handle!==lease.handle||
     boundary!==lease.boundary||check!==lease.check)
   throw Error('Fresh finite CSS queued owner changed.');
  const state=lease.handle.getState();
  const first=stage==='first';
  if(state.scene!=='css'||state.state!==(first?'css':'paused')||
     state.paused!==!first||state.busy||state.loading||state.requiresReload||
     state.phase!==1||state.running!==first)
   throw Error('Fresh finite CSS stage requires its exact CSS owner state.');
  const pointer=Module._melee_web_native_menu_stadium_first_css_draw_observe?.();
  const observed=pointer?JSON.parse(Module.UTF8ToString(pointer)):null;
  if(observed?.schema!=='melee-web-stadium-first-css-finite-observation-v4'||
     observed.mode!=='fresh_finite_css_prefix_v4'||observed.error!==null||
     observed.armed_owner_stable!==true||observed.armed_world_generation!==lease.generation||
     typeof Module[exports[stage]]!=='function')
   throw Error('Fresh finite CSS stage lost its native capability.');
  const stream=observed.postdraw_stream,final=observed.final_pending_css_draw;
  const baseline=observed.state==='complete'&&observed.host_tick_calls===1&&
   observed.host_draw_calls===1&&observed.source_steps===1&&observed.source_draws===1&&
   observed.aurora_begin_calls===1&&observed.aurora_end_calls===1&&observed.frame_end_returned===true;
  const terminal=stream?.terminal===true&&stream.failed===false&&
   stream.outcome==='stop_after_last_input_request'&&stream.tick_result===3;
  const allowed=stage==='first'?observed.state==='entered'&&
   observed.host_tick_calls===0&&observed.host_draw_calls===0&&
   observed.source_steps===0&&observed.source_draws===0&&
   observed.aurora_begin_calls===0&&observed.aurora_end_calls===0:
   stage==='stream_arm'?baseline&&!stream:
   stage==='stream_kick'?baseline&&stream?.status==='armed'&&stream.failed===false&&
    stream.consumed_inputs===0&&stream.matched_ticks===0&&stream.matched_draws===0:
   stage==='final_arm'?baseline&&terminal&&!final:
   baseline&&terminal&&final?.status==='armed'&&final.failed===false&&final.attempted===false;
  if(!allowed)throw Error('Fresh finite CSS queued stage boundary changed.');
  return lease.check(Module[exports[stage]]());
 });
};

window.meleeNetTimingPauseEligibilitySnapshot=()=>{
 if(!owner?.handle||typeof Module._melee_web_net_status!=='function')
  return {eligible:false,reasons:['owner-unavailable'],network_status:null,native_running:null,native_message:null,owner_state:null};
 const network=JSON.parse(Module.UTF8ToString(Module._melee_web_net_status()));
 const running=Module._melee_web_native_menu_running();
 const message=Module.UTF8ToString(Module._melee_web_native_menu_message());
 const ownerState=owner.handle.getState();
 return {network_status:network,native_running:running,native_message:message,owner_state:ownerState,
  ...evaluateNetworkTimingPause({networkStatus:network,nativeRunning:running,message,ownerState})};
};
window.meleeNetCanResumeTimingPause=()=>{
 const snapshot=window.meleeNetTimingPauseEligibilitySnapshot();
 const checks=window.__meleeNetTimingPauseChecks||(window.__meleeNetTimingPauseChecks=[]);
 if(checks.length>=32)checks.shift();
 checks.push(snapshot);
 return snapshot.eligible;
};
window.meleeNetTimingPauseDiagnostics=()=>({checks:window.__meleeNetTimingPauseChecks||[],
 current:window.meleeNetTimingPauseEligibilitySnapshot()});
window.meleeNetRuntimeCallbackTimingSnapshot=()=>Object.freeze({frames:nativeTimingFrames,
 over_budget_frames:nativeOverBudgetFrames,long_frames:nativeLongFrames,
 worst_active_ms:nativeTimingWorst,worst:nativeWorstTiming?Object.freeze({...nativeWorstTiming}):null,
 previous:nativePreviousTiming?Object.freeze({...nativePreviousTiming}):null,
 last:nativeLastTiming?Object.freeze({...nativeLastTiming}):null});
window.meleeNetRuntimeDiagnosticExport=async()=>owner?.handle?.exportDiagnostics?.()??null;
window.meleeNetResumeTimingPause=async()=>{
 return attemptNetworkTimingPauseResume({canResume:()=>window.meleeNetCanResumeTimingPause(),
  resume:()=>owner.handle.resume(),isRunning:()=>Module._melee_web_native_menu_running()===1});
};
window.meleeNetBegin=async(seed,maxFrames)=>{
 if(retailRun||replayLoading||!ready||fatal||!bundle||typeof Module._melee_web_native_menu_net_begin!=='function')throw Error('Networked session is unavailable');
 if(owner.handle.getState().state!=='prepared')throw Error('A networked session requires a freshly imported disc before opening character select.');
 resetTiming(false);await prepareAudio();await pauseAudioForPreparation();
 await boundary(()=>check(Module._melee_web_native_menu_net_begin(seed>>>0,maxFrames>>>0)));
 await waitForAudioRender();
 await boundary(()=>check(Module._melee_web_native_menu_launch()));
 $('canvas').focus();inputDirty=true;syncAudio();
};
window.meleeNetBeginLockstep=async(seed,maxFrames)=>{
 if(retailRun||replayLoading||!ready||fatal||!bundle||typeof Module._melee_web_native_menu_net_begin_lockstep!=='function')throw Error('Lockstep session is unavailable');
 if(owner.handle.getState().state!=='prepared')throw Error('A lockstep session requires a freshly imported disc before opening character select.');
 if(!runtimeLockstepEntry?.state().configured){
  resetTiming(false);await prepareAudio();await pauseAudioForPreparation();
  await boundary(()=>check(Module._melee_web_native_menu_net_begin_lockstep(seed>>>0,maxFrames>>>0)));
  await waitForAudioRender();
  await boundary(()=>check(Module._melee_web_native_menu_launch()));
  $('canvas').focus();inputDirty=true;syncAudio();
  return;
 }
 await runtimeLockstepEntry.begin(seed,maxFrames);
};
window.meleeNetConfigureRuntimeLockstep=config=>{
 if(!ready||fatal)throw Error('Runtime lockstep configuration is unavailable');
 if(!runtimeLockstepEntry)runtimeLockstepEntry=createDevelopmentLockstepOwner({Module,owner,inputDelay:LOCKSTEP_DELAY,sourceTickLimit:LOCKSTEP_MAX_SOURCE_TICKS,
  getContext:()=>({ready,fatal,bundle,replayActive:!!retailRun||replayLoading,
   ownerState:owner.handle.getState().state,phase:Module._melee_web_native_menu_phase()}),
  getIdentity:()=>window.meleeNetPeerIdentity(),
  createSession:options=>createRuntimeLockstepSession(options),
  runNativeStart:async({seed,sourceTicks})=>{
   resetTiming(false);await prepareAudio();await pauseAudioForPreparation();
   await boundary(()=>check(Module._melee_web_native_menu_net_begin_lockstep(seed,sourceTicks)));
   await waitForAudioRender();
   await boundary(()=>check(Module._melee_web_native_menu_launch()));
   $('canvas').focus();inputDirty=true;syncAudio();
  }});
 return runtimeLockstepEntry.configure(config);
};
window.meleeNetRuntimeLockstepSnapshot=()=>runtimeLockstepEntry?.snapshot()??null;
window.meleeNetRuntimeLockstepHealth=options=>runtimeLockstepEntry?.health(options)??null;
window.meleeNetCloseRuntimeLockstep=mode=>runtimeLockstepEntry?.close({mode})??Promise.resolve(null);
window.meleeNetPeerIdentity=async()=>{
 if(!owner?.handle?.discIdentity)throw Error('Lockstep disc identity is unavailable');
 const response=await fetch(new URL('./gameplay_menu_browser.wasm',import.meta.url),{cache:'no-store'});
 if(!response.ok)throw Error(`Could not read lockstep Wasm identity (${response.status})`);
 const bytes=await response.arrayBuffer();
 const digest=await crypto.subtle.digest('SHA-256',bytes);
 const wasm=Array.from(new Uint8Array(digest),value=>value.toString(16).padStart(2,'0')).join('');
 return {algorithm:'sha256',wasm,disc:await owner.handle.discIdentity()};
};
$('retail-replay-start').onclick=async()=>{
 if(replayLoading||retailRun||!ready||fatal||!bundle)return;
 replayLoading=true;$('retail-replay-start').disabled=$('disc').disabled=$('launch').disabled=true;
 try{
  if(window.meleeHitchCaptureLoading)await window.meleeHitchCaptureLoading;if(window.meleeHitchCaptureLoadError&&hitchCaptureFromUrl)throw Error(`Hitch capture unavailable: ${window.meleeHitchCaptureLoadError}`);
  // Native parsing applies the legacy 36,000-frame limit and the v8/v9/v10
  // whole-session 108,000-frame limit after reading the version. This upload
  // bound admits the largest checked v8 envelope without widening legacy
  // formats or allowing an unbounded allocation.
  const file=$('retail-replay-file').files[0];if(!file||file.size<328||file.size>RETAIL_REPLAY_MAX_BYTES)throw Error('Invalid bounded MWRC input recipe');
  const bytes=new Uint8Array(await file.arrayBuffer()),hash=await replayHash(bytes),observe=$('retail-replay-mode').value==='state';
  // The v8 route enters the canonical, freshly prepared CSS owner. Unloading
  // here would discard its scoped assets before native launch. This header
  // probe selects ownership only; the native decoder validates the recipe and
  // rejects an already used source heap before accepting it.
  const header=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const wholeVersion=bytes.length>=8&&header.getUint32(0,false)===0x4d575243?header.getUint32(4,false):0;
  const wholeSession=wholeVersion===8||wholeVersion===9||wholeVersion===10;
  // Requested scope survives native preparation failure/time-out. This probe
  // is not admission: the native decoder still validates the complete recipe.
  const diagnosticEntityPrefixMode=wholeVersion===8&&bytes.length>=24&&[1,2].includes(header.getUint16(22,false))?header.getUint16(22,false):0;
  const diagnosticEntityPrefix=diagnosticEntityPrefixMode!==0;
  if(wholeSession&&owner.handle.getState().state!=='prepared')throw Error('Whole-session replay requires a freshly imported disc before opening character select.');
  if(!wholeSession&&!await unloadAndSave())throw Error(status());resetTiming(false);await prepareAudio();await pauseAudioForPreparation();
  for(const old of $('retail-replay-downloads').querySelectorAll('a'))URL.revokeObjectURL(old.href);$('retail-replay-downloads').replaceChildren();replayEvidence=[];$('save-replay-evidence').disabled=true;
  retailRun={hash,observe,wholeSession,diagnosticEntityPrefix,diagnosticEntityPrefixMode,rows:[],timerRows:[],memory:{before_preparation:replayMemorySnapshot()},frames:0,started:performance.now(),lastProgress:performance.now(),lastCursor:0,focusLost:false,cache:{state:Module.runtimeCacheState?.state||'unknown',bytes:Number(Module.runtimeCacheState?.fileBytes||0),cleared_on_startup:clearRenderCacheOnLoad,driver_cache:'uncontrolled'}};
  const run=retailRun;
  uiMessage='';$('retail-replay-report').textContent='Preparing reference replay…';$('launch').disabled=true;$('pause').disabled=$('unload').disabled=false;
  retailRun.paintControl=beginReplayPaintControl();
  await boundary(()=>{const ptr=Module._malloc(bytes.length);try{if(!ptr)throw Error('Replay allocation failed');Module.HEAPU8.set(bytes,ptr);check(Module._melee_web_native_menu_replay(ptr,bytes.length,observe?1:0));}finally{Module._free(ptr);}});
  // A whole-session recipe keeps one source arena and cursors through the real
  // scene chain, so it enters CSS through the ordinary launch instead of the
  // single-match replay's direct match construction.
  if(Module._melee_web_native_menu_replay_whole_session?.()){
   await waitForAudioRender();
   if(retailRun!==run||run.finishing)return;
   // A stop can also arrive after readiness but before this native command.
   await boundary(()=>{if(retailRun===run&&!run.finishing)check(Module._melee_web_native_menu_launch());});
   if(retailRun!==run||run.finishing)return;
  }
  $('canvas').focus();inputDirty=true;syncAudio();
 }catch(error){if(retailRun)await finishRetailReplay(error.message);else $('retail-replay-report').textContent=error.message;}
 finally{replayLoading=false;if(!retailRun){$('disc').disabled=fatal||importing;$('launch').disabled=fatal||!bundle;}}
};

$('disc').onchange=async()=>{
  const file=$('disc').files[0];if(!file)return;$('status').dataset.runtimeError='';stockCheckActive=false;resetTiming();
  try{await owner.handle.importDisc(file);}catch(error){log(error.message);$('status').dataset.runtimeError=error.message;$('status').textContent=error.message;}
  finally{$('disc').value='';}
};
$('launch').onclick=async()=>{
  if($('launch').disabled)return;$('status').dataset.runtimeError='';
  try{if(window.meleeHitchCaptureLoading)await window.meleeHitchCaptureLoading;
    if(window.meleeHitchCaptureLoadError&&hitchCaptureFromUrl)throw Error(window.meleeHitchCaptureLoadError);
    resetTiming(false);await owner.handle.start();
  }catch(error){log(error.message);$('status').dataset.runtimeError=error.message;$('status').textContent=error.message;}
};
$('pause').onclick=()=>{
  if(retailRun&&!retailRun.observe){finishRetailReplay('Manual pause/resume requested during performance replay');return;}
  const state=owner.handle.getState();
  (state.paused?owner.handle.resume():owner.handle.pause()).catch(error=>log(error.message));
};
$('unload').onclick=async()=>{$('status').dataset.runtimeError='';if(lockstepRoomConfigured){lockstepRoomUnloading=true;stopLockstepRoomPolling();lockstepRoomStatus('Runtime unloading; room outcome is not assessed.');syncLockstepRoomControls();}try{
  // A diagnostic replay owns its collected rows until finishRetailReplay
  // exports them. Ordinary owner unload alone would lose a bounded prefix.
  if(retailRun)await finishRetailReplay('Manual unload stopped the replay');
  else await owner.handle.unload();
  stockCheckActive=false;
  if(lockstepRoomConfigured){lockstepRoomUnloading=false;lockstepRoomFinished=true;lockstepRoomCloseAttempted=true;lockstepRoomStatus('Runtime unloaded. Room outcome is unverified.');if(lockstepStagingMode)$('lockstep-retry').hidden=false;syncLockstepRoomControls();}
}catch(error){log(error.message);$('status').dataset.runtimeError=error.message;if(lockstepRoomConfigured){lockstepRoomUnloading=false;lockstepRoomFinished=true;lockstepRoomStatus(`Error: runtime unload failed; room teardown is unconfirmed (${error?.message||String(error)}).`);syncLockstepRoomControls();}}};
controllerSettings = mountControllerSettings({
  container: $('controls-dialog'),
  storage: window.parent === window ? undefined : null,
  legacyKeyboard: [$('keyboard'), $('keyboard2')],
  expose: true,
  onError(error){ log(error.message); $('status').textContent = error.message; },
  focus: () => owner?.handle.focus?.(),
  openButton: $('controls-open'),
});
try {
  await mountMeleeRuntime({canvas:$('canvas'),createAudio:createRuntimeAudio,readDisc:loadNativeGameDisc,openDisc:openNativeGameSession,loaderUrl:new URL('./gameplay_menu_browser.js',import.meta.url),
    stagingDiagnosticsOrigin:`https://${STAGING_HOST}`,
    onOwner(context){owner=context;({Module,boundary,status,check,put,prepareAudio,pauseAudioForPreparation,
      syncAudio,unloadAndSave,prepareNativeResources,waitForAudioAck,waitForAudioRender}=context);},
    configureModule(module){
      installRuntimeCache(module,report=>{const size=report.fileBytes?` · ${report.fileBytes} persisted bytes`:'';
        $('cache-status').textContent=report.message+size;runtimeCacheSyncCapability=report.sync_diagnostics||null;window.meleeHitchCapture?.setCacheSyncCapability?.(runtimeCacheSyncCapability||{});$('export-render-cache').disabled=!report.mounted||!report.populated;
        if(report.state==='saved'&&report.lastSaveMs!==null)log(`Render cache save ${JSON.stringify({duration_ms:report.lastSaveMs,file_bytes:report.fileBytes})}`);
      },{clearOnLoad:clearRenderCacheOnLoad,syncDiagnostics:hitchCausalFromUrl,now:()=>performance.now(),onSync:reportRuntimeCacheSync});
    },
    onState(state){
      ready=state.ready;fatal=state.state==='error';bundle=state.bundle;importing=state.state==='importing';uiMessage=state.message;
      lockstepRoomCanStart=state.canStart;
      if(fatal&&lockstepRoomConfigured)finishLockstepRoomStatus(`Error: ${state.message||'runtime stopped'}. Room outcome is unverified.`);
      $('status').textContent=state.message;$('status').dataset.phase=state.phase;
      $('disc').disabled=!state.canImport||!!retailRun||replayLoading;
      $('launch').disabled=!state.canStart||!!retailRun||replayLoading;
      $('pause').disabled=!state.canPause;$('unload').disabled=!state.canUnload;
      $('reload-app').disabled=$('reset-render-cache').disabled=!ready;
      controllerSettings.setState(state);
      syncLockstepRoomControls();
    },
    onError(error){log(error.message);$('status').dataset.runtimeError=error.message;},
    onLog(text,isError){
      window.__meleeWebStadiumTextTailProbe?.observe?.(text);
      const boundaryProbe=globalThis.__meleeReplayBoundaryProbe;
      const probeEnabled=boundaryProbe?.enabled===true;
      let replayHeader=false,sessionFrame=false,frameIndex=-1;
      if(probeEnabled){
        replayHeader=!isError&&text.startsWith('{"record":"header"');
        sessionFrame=!isError&&text.startsWith('{"record":"session_frame"');
        if(sessionFrame){
          const index=Number(text.match(/,"index":(\d+),/)?.[1]);
          frameIndex=Number.isSafeInteger(index)?index:-1;
        }
      }
      if(probeEnabled&&replayHeader)markFirstReplayBoundary('header_onLog_begin',0,0,0);
      if(probeEnabled&&sessionFrame){
        markFirstReplayBoundary('session_frame_onLog_begin',frameIndex,0,0);
        if(frameIndex===0)markFirstReplayBoundary('frame0_onLog_begin',frameIndex,0,0);
      }
      if(retailRun?.observe&&!isError&&text.startsWith('{"record":')){
        const limit=retailRun.wholeSession
          ? RETAIL_REPLAY_WHOLE_SESSION_MAX_FRAMES+RETAIL_REPLAY_SESSION_RECORD_OVERHEAD
          : RETAIL_REPLAY_LEGACY_MAX_FRAMES+RETAIL_REPLAY_STATE_RECORD_OVERHEAD;
        if(retailRun.rows.length>=limit)throw Error('Replay trace exceeded its record bound');retailRun.rows.push(text);
      }else if(retailRun?.observe&&isError&&text.startsWith('TIMER_AUDIT ')){
        const limit=retailRun.frames>RETAIL_REPLAY_LEGACY_MAX_FRAMES
          ? RETAIL_REPLAY_WHOLE_SESSION_MAX_FRAMES+RETAIL_REPLAY_TIMER_RECORD_OVERHEAD
          : RETAIL_REPLAY_LEGACY_MAX_FRAMES+RETAIL_REPLAY_TIMER_RECORD_OVERHEAD;
        if(retailRun.timerRows.length>=limit)throw Error('Timer trace exceeded its record bound');retailRun.timerRows.push(text.slice(12));
      }else log(text);
      if(probeEnabled&&replayHeader)markFirstReplayBoundary('header_onLog_returned',0,0,0);
      if(probeEnabled&&sessionFrame){
        if(frameIndex===0)markFirstReplayBoundary('frame0_onLog_returned',frameIndex,0,0);
        markFirstReplayBoundary('session_frame_onLog_returned',frameIndex,0,0);
      }
    },
    onEvent(name,data){
      if(name==='assetScopeReleased'||name==='assetScopeCommitted'){
        log(`Native asset scope ${name} ${JSON.stringify(data)}`);return;
      }
      if(name==='preparation'){developmentHooks.preparation(data.label,data.keepAudio);return;}
      if(name==='audio'){
        latestAudio={...latestAudio,...data};
        $('audio-metrics').textContent=`Audio queue ${data.queued} frames · underrun ${data.underruns} frames · stereo with original reverb/delay · replacement DSP coefficients`;
        return;
      }
      if(name==='fatal'&&retailRun){retailRun.failure=data;setTimeout(()=>finishRetailReplay(data),0);}
      developmentHooks[name]?.(data);
    },
  });
  await controllerSettings.bindPlayer(owner?.handle);
} catch(error){log(error.message);$('status').dataset.runtimeError=error.message;$('status').textContent='Stopped: '+error.message;}
