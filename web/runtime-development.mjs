/** Development tools attach to the shared player; this file is excluded from public builds. */
import {mountMeleeRuntime} from './melee-runtime.mjs';
import {mountControllerSettings} from './controller-settings.mjs';
import {createRuntimeAudio} from './runtime-audio.mjs';
import {loadNativeGameDisc} from './runtime-audio-assets.mjs';
const developmentHooks = {};
let owner, controllerSettings, Module, boundary, status, check, put, prepareAudio, pauseAudioForPreparation;
let syncAudio, unloadAndSave, prepareNativeResources, waitForAudioAck;
let preparationKeepsAudio = false;
const stop = error => owner.stop(error);
const $=id=>document.getElementById(id);const startupUrl=new URL(location.href),clearRenderCacheOnLoad=startupUrl.searchParams.get('render-cache')==='clear',hitchCaptureFromUrl=startupUrl.searchParams.get('hitch-capture')==='1',hitchCausalFromUrl=startupUrl.searchParams.get('hitch-causal')==='1',hitchUserTimingFromUrl=startupUrl.searchParams.get('hitch-marks')==='1';if(clearRenderCacheOnLoad){startupUrl.searchParams.delete('render-cache');history.replaceState(null,'',startupUrl);}let ready=false,fatal=false,bundle=false,inputDirty=true,importing=false,uiMessage="";let frameLast=0,perfLastReport=0,activeFrames=0,worstFrame=0,worstBrowserCallback=null,longFrames=0,browserLongTasks=0,browserLongTaskWorst=0;let nativeTimingFrames=0,nativeTimingWorst=0,nativeLongFrames=0,nativeOverBudgetFrames=0,nativeSourceSteps=0,nativeSourceDraws=0,nativeWorstTiming=null,nativeFirstUse=0,nativeFirstUseWorst=0,nativePreviousTiming=null,nativeLastTiming=null,livePipelineQueued=0,livePipelineCreated=0,liveTextureUploads=0,liveStagingUsedBytes=0,peakStagingUsedBytes=0;let constructionCounts={},constructionWorst=0,constructionFirstUseWorst=0,preparationSince=0,preparationLabel="",preparationSequence=0,activePreparation=null,pendingEntryProfile=null,settlingEntryProfile=null,entryProfiles=[],latestNativePreparation=null;let preparationCount=0,preparationWorst=0;let diagnosticCaptureInvalid=false,audioStateAcknowledged=true,audioAckWaiters=[],stockCheckActive=false,latestAudio={queued:0,underruns:0,overflows:0},actionSweepActive=false,actionSweepFocusLost=false,selectionDriveActive=false,sweepAutomaticResumes=0;
window.meleeHitchCapture=null;let runtimeCacheSyncCapability=null,runtimeCacheSyncDropped=0;const runtimeCacheSyncQueue=[];const reportRuntimeCacheSync=event=>{const capture=window.meleeHitchCapture;if(capture?.observeCacheSync){capture.observeCacheSync(event);return;}if(hitchCausalFromUrl){if(runtimeCacheSyncQueue.length<128)runtimeCacheSyncQueue.push(event);else runtimeCacheSyncDropped++;}};const hitchCaptureLoading=import('./hitch-capture.mjs').then(({createHitchCapture})=>{const capture=createHitchCapture({enabled:hitchCaptureFromUrl,userTiming:hitchUserTimingFromUrl,cacheSyncDiagnostics:hitchCausalFromUrl});window.meleeHitchCapture=capture;if(runtimeCacheSyncCapability)capture.setCacheSyncCapability(runtimeCacheSyncCapability);for(const event of runtimeCacheSyncQueue.splice(0))capture.observeCacheSync(event);if(runtimeCacheSyncDropped)capture.noteCacheSyncLoss(runtimeCacheSyncDropped);const toggle=$('hitch-capture');if(toggle){toggle.checked=hitchCaptureFromUrl;toggle.onchange=()=>{capture.setEnabled(toggle.checked);if(hitchCausalFromUrl)Module?.setRuntimeCacheSyncDiagnostics?.(toggle.checked);};}return capture;}).catch(error=>{window.meleeHitchCaptureLoadError=String(error?.message||error);const toggle=$('hitch-capture');if(toggle)toggle.disabled=true;return null;});window.meleeHitchCaptureLoading=hitchCaptureLoading;
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
if(typeof PerformanceObserver==='function'&&PerformanceObserver.supportedEntryTypes?.includes('longtask'))new PerformanceObserver(list=>{for(const entry of list.getEntries()){browserLongTasks++;browserLongTaskWorst=Math.max(browserLongTaskWorst,entry.duration);log(`Browser long task ${JSON.stringify({started:entry.startTime,duration_ms:entry.duration,name:entry.name})}`);}}).observe({type:'longtask',buffered:true});
function resetTiming(clearConstruction=true){frameLast=0;perfLastReport=0;activeFrames=0;worstFrame=0;worstBrowserCallback=null;longFrames=0;browserLongTasks=0;browserLongTaskWorst=0;nativeTimingFrames=0;nativeTimingWorst=0;nativeLongFrames=0;nativeOverBudgetFrames=0;nativeSourceSteps=0;nativeSourceDraws=0;nativeWorstTiming=null;nativeFirstUse=0;nativeFirstUseWorst=0;nativePreviousTiming=null;nativeLastTiming=null;livePipelineQueued=0;livePipelineCreated=0;liveTextureUploads=0;liveStagingUsedBytes=0;peakStagingUsedBytes=0;diagnosticCaptureInvalid=false;window.meleeHitchCapture?.reset?.();if(clearConstruction){constructionCounts={};constructionWorst=0;constructionFirstUseWorst=0;preparationCount=0;preparationWorst=0;preparationSince=0;preparationLabel='';preparationSequence=0;activePreparation=null;pendingEntryProfile=null;settlingEntryProfile=null;entryProfiles=[];latestNativePreparation=null;}$('render-metrics').textContent='Native timing diagnostics are waiting for the native player.';if(clearConstruction)$('construction-metrics').textContent='Scene construction diagnostics are waiting for the native player.';}
function renderConstructionMetrics(){const construction=Object.entries(constructionCounts).map(([name,count])=>`${name} ${count}`).join(' · ')||'none';const latest=entryProfiles.length?entryProfiles[entryProfiles.length-1]:null;const settle=latest&&latest.pipeline_settle_ms!==undefined?` · pipeline settle ${latest.pipeline_settle_ms.toFixed(2)} ms/${latest.pipeline_settle_frames} callbacks`:latest&&settlingEntryProfile===latest?` · pipelines settling (${latest.queued_total} initially queued)`:'';const profile=latest?` · latest ${latest.kind} prep ${latest.preparation_wall_ms.toFixed(2)} ms (construct ${latest.construction_ms.toFixed(2)}, wait/schedule ${latest.wait_schedule_ms.toFixed(2)}) · first draw ${latest.first_draw_ms.toFixed(2)} ms after ${latest.first_draw_delay_ms.toFixed(2)} ms · uploads ${latest.texture_upload_bytes} B · pipelines q${latest.queued_delta}/c${latest.created_delta}${settle}`:'';const p=latestNativePreparation,detail=p?` · native prep a${Number(p.audio_wait_ms).toFixed(2)} c${Number(p.construction_ms).toFixed(2)} r${Number(p.render_wait_ms).toFixed(2)} ms/${p.callbacks} callbacks (CPU ${Number(p.render_cpu_ms).toFixed(2)}, max ${Number(p.max_callback_ms).toFixed(2)}, uploads ${p.texture_upload_bytes} B, pipelines q${p.queued_delta}/c${p.created_delta})`:'';$('construction-metrics').textContent=`Construction ${construction} · worst ${constructionWorst.toFixed(2)} ms · construct ${constructionFirstUseWorst.toFixed(2)} ms · preparations ${preparationCount} (worst ${preparationWorst.toFixed(2)} ms; source entry/teardown separate)${profile}${detail}`;}
window.menuConstructionTiming=data=>{const kind=data.kind||'unknown';constructionCounts[kind]=(constructionCounts[kind]||0)+1;constructionWorst=Math.max(constructionWorst,data.total_ms||0);constructionFirstUseWorst=Math.max(constructionFirstUseWorst,data.construct_ms||0);const profile=activePreparation||pendingEntryProfile;if(profile&&!profile.first_draw)profile.construction.push(data);log(`Native construction ${JSON.stringify(data)}`);renderConstructionMetrics();};
window.menuPreparationProfile=data=>{latestNativePreparation=data;const profile=activePreparation||pendingEntryProfile;if(profile)profile.native_preparation=data;log(`Native preparation profile ${JSON.stringify(data)}`);renderConstructionMetrics();};
// Keep one worst callback for budget diagnosis; no per-frame state trace or log.
window.menuRuntimeTimingError=error=>{diagnosticCaptureInvalid=true;stop(error);};
window.menuRuntimeTiming=data=>{if(!data.valid)return;const now=performance.now(),total=data.total_ms||0,preparation=data.preparation_ms||0,active=Math.max(0,total-preparation),resourceActivity=Number(data.queued_delta||0)||Number(data.created_delta||0)||Number(data.texture_upload_bytes||0);nativePreviousTiming=nativeLastTiming;nativeLastTiming=data;nativeTimingFrames++;nativeSourceSteps+=Number(data.source_steps||0);nativeSourceDraws+=Number(data.source_draws||0);const hitch=window.meleeHitchCapture?.isEnabled?.()?window.meleeHitchCapture.observeNativeTiming?.({current:data,previous:nativePreviousTiming,activeMs:active,totalMs:total,preparationMs:preparation,timestamp:now,context:{cache_state:Module.runtimeCacheState?.state||'unknown',cache_dirty:!!Module.runtimeCacheState?.dirty}}):null;if(hitch?.invalid||window.meleeHitchCapture?.isInvalid?.()){diagnosticCaptureInvalid=true;if(retailRun&&!retailRun.failure)retailRun.failure='Diagnostic hitch capture overflow';}if(active>1000/60)nativeOverBudgetFrames++;if(active>nativeTimingWorst){nativeTimingWorst=active;nativeWorstTiming={...data,active_ms:active,source:active>1000/60?Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()):null};}if(active>1000/30)nativeLongFrames++;if(!preparation){livePipelineQueued+=Math.max(0,Number(data.queued_delta||0));livePipelineCreated+=Math.max(0,Number(data.created_delta||0));liveTextureUploads+=Math.max(0,Number(data.texture_upload_bytes||0));const stagingUsed=Math.max(0,Number(data.staging_used_bytes||0));liveStagingUsedBytes+=stagingUsed;peakStagingUsedBytes=Math.max(peakStagingUsedBytes,stagingUsed);}if(settlingEntryProfile){settlingEntryProfile.pipeline_settle_frames++;if(Number(data.queued_total||0)===0){settlingEntryProfile.pipeline_settle_ms=Math.max(0,now-settlingEntryProfile.first_draw_at);log(`Scene pipeline settle ${JSON.stringify({sequence:settlingEntryProfile.sequence,kind:settlingEntryProfile.kind,duration_ms:settlingEntryProfile.pipeline_settle_ms,callbacks:settlingEntryProfile.pipeline_settle_frames,created_total:Number(data.created_total||0)})}`);settlingEntryProfile=null;renderConstructionMetrics();}}if(data.first_use){nativeFirstUse++;nativeFirstUseWorst=Math.max(nativeFirstUseWorst,active);if(pendingEntryProfile){const profile=pendingEntryProfile;pendingEntryProfile=null;profile.first_draw=true;profile.first_draw_at=now;profile.kind=profile.construction.length?profile.construction[profile.construction.length-1].kind:'source-scene';profile.construction_ms=profile.construction.reduce((sum,item)=>sum+Number(item.total_ms||0),0);profile.wait_schedule_ms=Math.max(0,profile.preparation_wall_ms-profile.construction_ms);profile.first_draw_delay_ms=Math.max(0,now-profile.preparation_done);profile.first_draw_ms=total;profile.draw_ms=Number(data.draw_ms||0);profile.end_ms=Number(data.end_ms||0);profile.texture_upload_bytes=Number(data.texture_upload_bytes||0);profile.queued_delta=Number(data.queued_delta||0);profile.created_delta=Number(data.created_delta||0);profile.queued_total=Number(data.queued_total||0);profile.created_total=Number(data.created_total||0);profile.wasm_heap_bytes=Number(data.wasm_heap_bytes||0);profile.render_cache_state=Module.runtimeCacheState?.state||'unknown';profile.render_cache_bytes=Number(Module.runtimeCacheState?.fileBytes||0);profile.pipeline_settle_frames=0;if(profile.queued_total)settlingEntryProfile=profile;else profile.pipeline_settle_ms=0;entryProfiles.push(profile);log(`Scene entry profile ${JSON.stringify(profile)}`);renderConstructionMetrics();}}if(data.first_use||active>1000/30||preparation||resourceActivity)log(`${!preparation&&resourceActivity?'Live render resource':'Native callback'} ${JSON.stringify({...data,source:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())})}`);};
developmentHooks.preparation=(label,keepAudio=false)=>{preparationLabel=label||'Preparing original scene';preparationKeepsAudio=!!keepAudio;preparationSince=performance.now();window.meleeHitchCapture?.setPreparation?.(true,preparationSince);activePreparation={sequence:++preparationSequence,label:preparationLabel,requested_at:preparationSince,construction:[],first_draw:false,audio_continuity:preparationKeepsAudio};uiMessage='';$('status').textContent=`${preparationLabel} · audio ${preparationKeepsAudio?'continuing':'paused'}`;};
developmentHooks.preparationDone=()=>{if(preparationSince){const now=performance.now();window.meleeHitchCapture?.setPreparation?.(false,now);const duration=now-preparationSince;preparationCount++;preparationWorst=Math.max(preparationWorst,duration);if(activePreparation){activePreparation.preparation_done=now;activePreparation.preparation_wall_ms=duration;pendingEntryProfile=activePreparation;activePreparation=null;}log(`Native preparation boundary {"label":${JSON.stringify(preparationLabel)},"duration_ms":${duration.toFixed(3)}}`);renderConstructionMetrics();}preparationSince=0;preparationLabel='';uiMessage='';};
developmentHooks.preparationCanceled=()=>{window.meleeHitchCapture?.setPreparation?.(false);preparationSince=0;preparationLabel='';preparationKeepsAudio=false;activePreparation=null;pendingEntryProfile=null;uiMessage='';};
developmentHooks.preparationFailed=error=>{window.meleeHitchCapture?.setPreparation?.(false);preparationSince=0;preparationLabel='';preparationKeepsAudio=false;activePreparation=null;pendingEntryProfile=null;uiMessage=error||'Native preparation failed';$('status').textContent=uiMessage;if(retailRun)retailRun.failure=uiMessage;};
developmentHooks.cacheSettled=()=>{Module.markRuntimeCacheDirty?.();};
developmentHooks.cacheWriteFailed=message=>{$('cache-status').textContent=message;};
window.menuCacheWritesFlushed=data=>{
  if(data.ok&&data.flushed)Module.markRuntimeCacheDirty?.();
  if(!data.ok)$('cache-status').textContent='Optional render cache was not saved: native cache writes failed. Reload to retry storage.';
  log(`Native cache transaction flush ${JSON.stringify(data)}`);
};
developmentHooks.frame=wasRunning=>{
 if(!ready||fatal)return;
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
window.menuObserveFighter=kind=>{const ids=Module._malloc(16),geometry=Module._malloc(32);try{if(!Module._melee_web_css_observe(kind,ids,geometry))return null;return{ids:Array.from(Module.HEAP32.subarray(ids>>2,(ids>>2)+4)),geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8))};}finally{Module._free(ids);Module._free(geometry);}};
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
async function finishRetailReplay(reason){
 const run=retailRun;if(!run||run.finishing)return;run.finishing=true;window.meleeHitchCapture?.stop?.();
 const metrics=replayMetrics(run),diagnosticCapture=window.meleeHitchCapture?.report?.()||null;run.memory.before_teardown=replayMemorySnapshot();let teardown=false;
 try{teardown=await unloadAndSave();if(!teardown)throw Error(status());}catch(error){reason=reason||error.message;}
 run.memory.after_teardown=replayMemorySnapshot();
 run.paintControl?.restore();
 // The disabled-state acknowledgement includes final audio counters.
 metrics.audioUnderrunFrames=Number(latestAudio.underruns||0)-(run.baseline?.underruns??0);
 metrics.audioOverflowFrames=Number(latestAudio.overflows||0)-(run.baseline?.overflows??0);
 if(diagnosticCapture?.invalid){diagnosticCaptureInvalid=true;if(!run.failure)run.failure='Diagnostic hitch capture overflow';}
 reason=reason||run.failure;
 const failures=[];if(reason)failures.push(reason);if(!teardown)failures.push('teardown incomplete');if(metrics.sourceFrames!==run.frames)failures.push('incomplete input timeline');if(metrics.sourceSteps!==run.frames||metrics.sourceDraws!==(run.expectedDraws??run.frames))failures.push('source tick/draw count mismatch');
 if(diagnosticCaptureInvalid)failures.push('diagnostic capture invalid');if(!run.observe){for(const key of ['browserCallbackGaps','browserLongTasks','nativeCallbacksOver33ms','livePipelinesQueued','livePipelinesCreated','preparationPauses','audioUnderrunFrames','audioOverflowFrames','focusLost'])if(metrics[key])failures.push(key);}
 const sourceMatch=run.sourceMatch||{complete:false,outcome:null,winner:null};
 const report={input_scheduling:['per_tick','startup_clock','recorded_queue'][run.scheduling??0],scheduling_equivalence:'not_evaluated',schema:'melee-web-browser-retail-replay',version:1,recipe_sha256:run.hash,frames:run.frames,mode:run.observe?'state_capture':'performance',complete:teardown&&metrics.sourceFrames===run.frames&&!reason,pass:failures.length===0,failures,performance:run.observe?'not_evaluated':'measured',gold_admitted:false,pixels:'not_compared',source_match:sourceMatch,instrumented_timing_resumes:run.timingResumes||0,metrics,diagnostic_capture:diagnosticCapture,diagnostic_page_paint:run.paintControl?.evidence||null,memory:run.memory,preparation:run.preparation,cache:run.cache,user_agent:navigator.userAgent,resolution:[640,480],device_pixel_ratio:devicePixelRatio};
 if(run.observe){const trace=run.rows.join('\n')+'\n';report.trace_sha256=await replayHash(new TextEncoder().encode(trace));replayDownload('retail-port.jsonl',trace);if(run.timerRows.length){const timers=run.timerRows.join('\n')+'\n';report.timer_trace_sha256=await replayHash(new TextEncoder().encode(timers));replayDownload('retail-timer.jsonl',timers);}}
 const text=JSON.stringify(report,null,2);window.lastRetailReplayReport=report;$('retail-replay-report').textContent=text;replayDownload('retail-browser-report.json',text);retailRun=null;$('launch').disabled=fatal||!bundle;$('disc').disabled=fatal||importing;$('pause').disabled=$('unload').disabled=true;syncAudio();
}
window.menuReplayStarted=(frames,observe,draws=frames,scheduling=0)=>{const run=retailRun;if(!run)throw Error('Missing browser replay owner');if(!Number.isInteger(draws)||draws<1||draws>frames)throw Error('Invalid replay clock draw count');if(![0,1,2].includes(scheduling)||(scheduling===2&&!observe))throw Error('Invalid replay scheduling scope');run.scheduling=scheduling;run.frames=frames;run.expectedDraws=draws;run.observe=observe;run.preparation=latestNativePreparation;run.memory.prepared=replayMemorySnapshot();resetTiming(false);run.baseline={preparations:preparationCount,heap:Module.HEAPU8.length,underruns:Number(latestAudio.underruns||0),overflows:Number(latestAudio.overflows||0)};run.lastProgress=performance.now();};
window.menuReplayCompleted=(frames,matchComplete,outcome,winner)=>{if(retailRun){retailRun.consumed=frames;retailRun.completed=true;retailRun.sourceMatch={complete:!!matchComplete,outcome:Number.isInteger(outcome)?outcome:null,winner:Number.isInteger(winner)?winner:null};setTimeout(()=>finishRetailReplay(null),0);}};
window.menuReplayPoll=()=>{
 $('retail-replay-start').disabled=!ready||fatal||!bundle||replayLoading||!!retailRun||!$('retail-replay-file').files[0];
 const run=retailRun;if(!run||run.finishing||run.completed)return;
 if(run.failure){finishRetailReplay(run.failure);return;}
 if(window.meleeHitchCapture?.isInvalid?.()){diagnosticCaptureInvalid=true;if(!run.failure)run.failure='Diagnostic hitch capture overflow';}
 const now=performance.now(),cursor=Module._melee_web_native_menu_replay_cursor();
 if(cursor!==run.lastCursor){run.lastCursor=cursor;run.lastProgress=now;}
 if(run.baseline&&now-run.lastProgress>700){const reason=status();if(!Module._melee_web_native_menu_running()&&!preparationSince){
  // Diagnostic printf/JSON collection can disrupt scheduling. Only the explicitly
  // instrumented state run may resume; performance mode retains the hard failure.
  if(run.observe&&reason.startsWith('Paused after a timing disruption')){run.timingResumes=(run.timingResumes||0)+1;run.lastProgress=now;boundary(()=>Module._melee_web_native_menu_pause(0));}
  else finishRetailReplay(reason||'Replay stopped');
 }}
 if(now-run.started>900000)finishRetailReplay('Replay exceeded bounded wall time');
};
$('retail-replay-start').onclick=async()=>{
 if(replayLoading||retailRun||!ready||fatal||!bundle)return;
 replayLoading=true;$('retail-replay-start').disabled=$('disc').disabled=$('launch').disabled=true;
 try{
  if(window.meleeHitchCaptureLoading)await window.meleeHitchCaptureLoading;if(window.meleeHitchCaptureLoadError&&hitchCaptureFromUrl)throw Error(`Hitch capture unavailable: ${window.meleeHitchCaptureLoadError}`);
  const file=$('retail-replay-file').files[0];if(!file||file.size<328||file.size>1909162)throw Error('Invalid bounded MWRC input recipe');
  const bytes=new Uint8Array(await file.arrayBuffer()),hash=await replayHash(bytes),observe=$('retail-replay-mode').value==='state';
  if(!await unloadAndSave())throw Error(status());resetTiming(false);await prepareAudio();await pauseAudioForPreparation();
  for(const old of $('retail-replay-downloads').querySelectorAll('a'))URL.revokeObjectURL(old.href);$('retail-replay-downloads').replaceChildren();replayEvidence=[];$('save-replay-evidence').disabled=true;
  retailRun={hash,observe,rows:[],timerRows:[],memory:{before_preparation:replayMemorySnapshot()},frames:0,started:performance.now(),lastProgress:performance.now(),lastCursor:0,focusLost:false,cache:{state:Module.runtimeCacheState?.state||'unknown',bytes:Number(Module.runtimeCacheState?.fileBytes||0),cleared_on_startup:clearRenderCacheOnLoad,driver_cache:'uncontrolled'}};
  uiMessage='';$('retail-replay-report').textContent='Preparing reference replay…';$('launch').disabled=true;$('pause').disabled=$('unload').disabled=false;
  retailRun.paintControl=beginReplayPaintControl();
  await boundary(()=>{const ptr=Module._malloc(bytes.length);try{if(!ptr)throw Error('Replay allocation failed');Module.HEAPU8.set(bytes,ptr);check(Module._melee_web_native_menu_replay(ptr,bytes.length,observe?1:0));}finally{Module._free(ptr);}});
  $('canvas').focus();inputDirty=true;syncAudio();
 }catch(error){if(retailRun)await finishRetailReplay(error.message);else $('retail-replay-report').textContent=error.message;}
 finally{replayLoading=false;if(!retailRun){$('disc').disabled=fatal||importing;$('launch').disabled=fatal||!bundle;}}
};

$('disc').onchange=async()=>{
  const file=$('disc').files[0];if(!file)return;stockCheckActive=false;resetTiming();
  try{await owner.handle.importDisc(file);}catch(error){log(error.message);$('status').textContent=error.message;}
  finally{$('disc').value='';}
};
$('launch').onclick=async()=>{
  if($('launch').disabled)return;
  try{if(window.meleeHitchCaptureLoading)await window.meleeHitchCaptureLoading;
    if(window.meleeHitchCaptureLoadError&&hitchCaptureFromUrl)throw Error(window.meleeHitchCaptureLoadError);
    resetTiming(false);await owner.handle.start();
  }catch(error){log(error.message);$('status').textContent=error.message;}
};
$('pause').onclick=()=>{
  if(retailRun&&!retailRun.observe){finishRetailReplay('Manual pause/resume requested during performance replay');return;}
  const state=owner.handle.getState();
  (state.paused?owner.handle.resume():owner.handle.pause()).catch(error=>log(error.message));
};
$('unload').onclick=async()=>{try{await owner.handle.unload();stockCheckActive=false;}catch(error){log(error.message);}};
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
  await mountMeleeRuntime({canvas:$('canvas'),createAudio:createRuntimeAudio,readDisc:loadNativeGameDisc,loaderUrl:new URL('./gameplay_menu_browser.js',import.meta.url),
    onOwner(context){owner=context;({Module,boundary,status,check,put,prepareAudio,pauseAudioForPreparation,
      syncAudio,unloadAndSave,prepareNativeResources,waitForAudioAck}=context);},
    configureModule(module){
      installRuntimeCache(module,report=>{const size=report.fileBytes?` · ${report.fileBytes} persisted bytes`:'';
        $('cache-status').textContent=report.message+size;runtimeCacheSyncCapability=report.sync_diagnostics||null;window.meleeHitchCapture?.setCacheSyncCapability?.(runtimeCacheSyncCapability||{});$('export-render-cache').disabled=!report.mounted||!report.populated;
        if(report.state==='saved'&&report.lastSaveMs!==null)log(`Render cache save ${JSON.stringify({duration_ms:report.lastSaveMs,file_bytes:report.fileBytes})}`);
      },{clearOnLoad:clearRenderCacheOnLoad,syncDiagnostics:hitchCausalFromUrl,now:()=>performance.now(),onSync:reportRuntimeCacheSync});
    },
    onState(state){
      ready=state.ready;fatal=state.state==='error';bundle=state.bundle;importing=state.state==='importing';uiMessage=state.message;
      $('status').textContent=state.message;$('status').dataset.phase=state.phase;
      $('disc').disabled=!state.canImport||!!retailRun||replayLoading;
      $('launch').disabled=!state.canStart||!!retailRun||replayLoading;
      $('pause').disabled=!state.canPause;$('unload').disabled=!state.canUnload;
      $('reload-app').disabled=$('reset-render-cache').disabled=!ready;
      controllerSettings.setState(state);
    },
    onError(error){log(error.message);},
    onLog(text,isError){
      if(retailRun?.observe&&!isError&&text.startsWith('{"record":')){
        if(retailRun.rows.length>=36004)throw Error('Replay trace exceeded its record bound');retailRun.rows.push(text);
      }else if(retailRun?.observe&&isError&&text.startsWith('TIMER_AUDIT ')){
        if(retailRun.timerRows.length>=36003)throw Error('Timer trace exceeded its record bound');retailRun.timerRows.push(text.slice(12));
      }else log(text);
    },
    onEvent(name,data){
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
} catch(error){log(error.message);$('status').textContent='Stopped: '+error.message;}
