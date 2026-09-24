// Exercise the actual page event handlers across asynchronous startup failures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const page=fs.readFileSync(new URL('../web/runtime-development.mjs',import.meta.url),'utf8');
const start=page.slice(page.indexOf("$('retail-replay-start').onclick="),page.indexOf("\n$('disc').onchange="));
const pause=page.slice(page.indexOf("$('pause').onclick="),page.indexOf("\n$('unload').onclick="));
const completion=page.slice(page.indexOf('window.menuReplayCompleted='),page.indexOf('\nwindow.menuReplayPoll='));
assert(start&&pause&&completion);
{
 // Construction can fail before menuReplayStarted publishes a baseline.
 // Its exact error must finish the replay on the next callback, not after
 // the 15-minute watchdog, and teardown must not run reentrantly in Wasm.
 const failed=page.split('\n').find(line=>line.startsWith('developmentHooks.preparationFailed='));
 const poll=page.slice(page.indexOf('window.menuReplayPoll='),page.indexOf("\n$('retail-replay-start').onclick="));
 const ended=[],display={files:[{}],dataset:{}};
 const scope={developmentHooks:{},window:{},retailRun:{},ready:true,fatal:false,bundle:true,
  replayLoading:false,$:()=>display,finishRetailReplay:reason=>ended.push(reason)};
 vm.createContext(scope);vm.runInContext(failed+'\n'+poll,scope);
 scope.developmentHooks.preparationFailed('PlLk.dat: invalid descriptor');
 assert.equal(ended.length,0,'Teardown is deferred until the next poll');
 assert.equal(scope.preparationSince,0);
 assert.equal(display.dataset.runtimeError,'PlLk.dat: invalid descriptor');
 scope.window.menuReplayPoll();
 assert.deepEqual(ended,['PlLk.dat: invalid descriptor']);
 scope.retailRun.finishing=true;scope.window.menuReplayPoll();
 assert.equal(ended.length,1,'A failing replay cannot start teardown twice');
}
{
 const shared=fs.readFileSync(new URL('../web/melee-runtime.mjs',import.meta.url),'utf8');
 const cacheReader=shared.slice(shared.indexOf('  function readNativeCacheIdle() {'),shared.indexOf('  function clearStartupTimeout() {'));
 const unload=shared.slice(shared.indexOf('  async function unloadAndSave() {'),shared.indexOf('  async function put('));
 assert(unload.includes('boundary(readNativeCacheIdle)'));
 const flush=page.slice(page.indexOf('window.menuCacheWritesFlushed='),page.indexOf('\n};',page.indexOf('window.menuCacheWritesFlushed='))+3);
 for(const finalState of [1,-1,2]){
  const events=[],display={};let idleCalls=0;
  const scope={window:{},prepared:true,callbacks:{menuPreparationCanceled:()=>events.push('cancel')},performance,
   emit:(name,message)=>{assert.equal(name,'cacheWriteFailed');display.textContent=message;},
   $:()=>display,log:text=>events.push(text),syncAudio:()=>events.push('sync-audio'),
   pauseAudioForPreparation:async()=>events.push('audio-stopped'),boundary:async run=>run(),
   Module:{runtimeCacheState:{dirty:false},
    _melee_web_native_menu_unload:()=>{events.push('unload');return 1;},
    _melee_web_native_menu_cache_idle:()=>{events.push('idle-check');return idleCalls++?finalState:0;},
    markRuntimeCacheDirty(){this.runtimeCacheState.dirty=true;events.push('dirty');},
    saveRuntimeCache:async()=>{events.push('save');return true;}}};
  vm.createContext(scope);vm.runInContext(cacheReader+'\n'+unload+'\n'+flush,scope);
  if(finalState===2){
   await assert.rejects(scope.unloadAndSave(),/Invalid native cache idle state: 2/);
   assert.equal(events.includes('save'),false,'Invalid cache state must not publish a saved cache');
   assert.equal(display.textContent,undefined,'Invalid state is not an optional persistence failure');
   continue;
  }
  scope.window.menuCacheWritesFlushed({ok:finalState===1,flushed:true});
  assert.equal(await scope.unloadAndSave(),true,'Optional cache failure cannot undo successful source teardown');
  assert.deepEqual(events.filter(x=>['unload','audio-stopped','idle-check','save'].includes(x)),
   finalState===1?['unload','audio-stopped','idle-check','idle-check','save']:['unload','audio-stopped','idle-check','idle-check']);
  if(finalState===-1){assert.equal(scope.Module.runtimeCacheState.dirty,false);assert.match(display.textContent,/native cache writes failed/);}
 }
 const exportHandler=page.split('\n').find(line=>line.startsWith("$('export-render-cache').onclick="));
 for(const cacheState of [0,-1,1]){
  const button={},errors=[];let reads=0,saves=0;
  const scope={$:()=>button,log:text=>errors.push(text),boundary:async run=>run(),Module:{
   _melee_web_native_menu_cache_idle:()=>cacheState,
   saveRuntimeCache:async()=>{saves++;return false;},
   FS:{readdir:()=>{reads++;return [];}}}};
  vm.createContext(scope);vm.runInContext(exportHandler,scope);await button.onclick();
  assert.equal(reads,0,'Export must not publish stale files before native flush and persistence both succeed');
  assert.equal(saves,cacheState===1?1:0);assert.equal(errors.length,1);
 }
}
{
 const handler=page.split('\n').find(line=>line.startsWith('window.menuRuntimeTimingError='));
 const failed=[];
 const scope={window:{},diagnosticCaptureInvalid:false,stop:error=>failed.push(error)};
 vm.createContext(scope);vm.runInContext(handler,scope);
 scope.window.menuRuntimeTimingError('Native menu timing JSON exceeded 4096 bytes');
 assert.equal(scope.diagnosticCaptureInvalid,true);
 assert.deepEqual(failed,['Native menu timing JSON exceeded 4096 bytes'],
  'Missing native timing must stop the replay instead of being ignored as an inactive callback');
}
{
 const save=page.split('\n').find(line=>line.startsWith('async function saveEvidence('));
 let nativeCalls=0;const posted=[];const display={};
 const scope={fatal:true,retailRun:null,$:()=>display,
  Module:{_melee_web_native_menu_running(){nativeCalls++;return 1;}},
  fetch:async(path,options)=>{posted.push([path,options.body]);return{ok:true,json:async()=>({path})};}};
 vm.createContext(scope);vm.runInContext(save,scope);
 await scope.saveEvidence([['retail-browser-report.json','retained failure']]);
 assert.equal(nativeCalls,0,'Saving retained crash evidence must not call the aborted module');
 assert.deepEqual(posted,[['/__melee_evidence/retail-browser-report.json','retained failure']]);
 scope.fatal=false;
 await assert.rejects(scope.saveEvidence([]),/Unload before saving evidence/);
 scope.fatal=true;scope.retailRun={finishing:true};
 await assert.rejects(scope.saveEvidence([]),/Unload before saving evidence/);
 assert.equal(posted.length,1,'An unfinished live or failing run cannot export evidence yet');
}
{
 const timing=page.split('\n').find(line=>line.startsWith('window.menuRuntimeTiming='));
 const reset=page.split('\n').find(line=>line.startsWith('function resetTiming('));
 const metrics=page.split('\n').find(line=>line.startsWith('function replayMetrics('));
 let diagnostics=0;
 const scope={window:{},performance,$:()=>({}),log(){},
  settlingEntryProfile:null,pendingEntryProfile:null,
  Module:{HEAPU8:new Uint8Array(2048),_melee_web_native_menu_replay_cursor:()=>42,
   _melee_web_native_menu_diagnostics(){diagnostics++;return 'source tick 42';},UTF8ToString:x=>x}};
 vm.createContext(scope);vm.runInContext(reset+'\n'+timing+'\n'+metrics,scope);
 scope.resetTiming();
 const sample=(frame,total,preparation=0)=>scope.window.menuRuntimeTiming({
  valid:1,frame,total_ms:total,preparation_ms:preparation,draw_ms:4,end_ms:3,
  end_phases:{last_frame:frame,staging_writes_ms:frame===3?2:0.1}});
 scope.window.menuRuntimeTiming({valid:0,total_ms:999});
 sample(1,10);sample(2,120,110);sample(3,22);
 for(let frame=4;frame<10004;frame++)sample(frame,8);
 let report=scope.replayMetrics({consumed:42,baseline:{heap:2048,preparations:0}});
 assert.equal(report.worstNativeCallbackMs,22);
 assert.equal(report.nativeCallbacksOverBudget,1);
 assert.equal(report.nativeCallbacksOver33ms,0,'Optimization target is separate from hitch gate');
 assert.equal(report.worstNativeCallback.frame,3,'Retain worst phases, not the last callback');
 assert.equal(report.worstNativeCallback.draw_ms,4);
 assert.equal(report.worstNativeCallback.end_phases.last_frame,3);
 assert.equal(report.worstNativeCallback.end_phases.staging_writes_ms,2,
  'Later callbacks must not overwrite the retained submission breakdown');
 assert.equal(report.worstNativeCallback.source,'source tick 42');
 assert.equal(diagnostics,2,'Only a new over-budget maximum or existing preparation log reads source diagnostics');
 sample(10004,34);
 report=scope.replayMetrics({consumed:42,baseline:{heap:2048,preparations:0}});
 scope.window.menuRuntimeTiming({valid:1,total_ms:8,source_steps:2,source_draws:2});
 report=scope.replayMetrics({consumed:42,baseline:{heap:2048,preparations:0}});
 assert.equal(report.sourceSteps,2);assert.equal(report.sourceDraws,2);
 assert.equal(report.nativeCallbacksOverBudget,2);
 assert.equal(report.nativeCallbacksOver33ms,1);
 scope.resetTiming(false);
 report=scope.replayMetrics({consumed:42,baseline:{heap:2048,preparations:0}});
 assert.equal(report.sourceSteps,0);assert.equal(report.sourceDraws,0);
 assert.equal(report.worstNativeCallback,null);
 assert.equal(report.nativeCallbacksOverBudget,0,'A new replay starts its own bounded timing record');
}
{
 const logHook=page.slice(page.indexOf('    onLog(text,isError){'),page.indexOf('    onEvent(name,data){'));
 const replayLimits=page.match(/const RETAIL_REPLAY_(?:LEGACY_MAX_FRAMES|WHOLE_SESSION_MAX_FRAMES|STATE_RECORD_OVERHEAD|SESSION_RECORD_OVERHEAD|TIMER_RECORD_OVERHEAD|MAX_BYTES)[^;]*;/g).join('\n');
 const moduleLine=replayLimits+'\nconst hooks={'+logHook+'}; var Module={print:text=>hooks.onLog(text,false),printErr:text=>hooks.onLog(text,true)};';
 const logged=[];
 const scope={$:()=>({}),retailRun:{observe:true,rows:[],timerRows:[]},
  log:text=>logged.push(text),stop(){}};
 vm.createContext(scope);vm.runInContext(moduleLine,scope);
 const state='{"record":"frame","index":0}';
 const timer='{"record":"frame","index":0,"seconds":480}';
 scope.Module.print(state);
 scope.Module.printErr('TIMER_AUDIT '+timer);
 assert.deepEqual(scope.retailRun.rows,[state]);
 assert.deepEqual(scope.retailRun.timerRows,[timer]);
 assert.equal(logged.length,0,'Timer sidecars must not flood browser logging');
 scope.Module.printErr('real native error');
 assert.deepEqual(logged,['real native error']);
 scope.retailRun.observe=false;
 scope.Module.printErr('TIMER_AUDIT '+timer);
 assert.equal(scope.retailRun.timerRows.length,1,'Performance runs do not collect state');
 scope.retailRun.observe=true;
 scope.retailRun.timerRows.length=36003;
 assert.throws(()=>scope.Module.printErr('TIMER_AUDIT '+timer),/record bound/);
 scope.retailRun={observe:true,wholeSession:true,frames:46835,rows:[],timerRows:[]};
 scope.retailRun.rows.length=108033;
 scope.Module.print(state);
 assert.equal(scope.retailRun.rows.length,108034,'v8 admits bounded per-span initialization records');
 scope.retailRun.timerRows.length=108002;
 scope.Module.printErr('TIMER_AUDIT '+timer);
 assert.equal(scope.retailRun.timerRows.length,108003,'v8 timer capture keeps its three-budget record bound');
 scope.retailRun.rows.length=108034;
 assert.throws(()=>scope.Module.print(state),/record bound/);
 scope.retailRun.timerRows.length=108003;
 assert.throws(()=>scope.Module.printErr('TIMER_AUDIT '+timer),/record bound/);
}
{
 const memory=page.slice(page.indexOf('const replayMemorySnapshot='),page.indexOf('\nfunction replayMetrics('));
 let nativeCalls=0;
 const scope={fatal:true,Module:{_melee_web_native_menu_memory(){nativeCalls++;throw Error('unavailable');}}};
 vm.createContext(scope);vm.runInContext(memory,scope);
 assert.equal(vm.runInContext('replayMemorySnapshot().available',scope),false);
 assert.equal(nativeCalls,0,'A native abort must not trigger an allocator walk');
 scope.fatal=false;
 assert.equal(vm.runInContext('replayMemorySnapshot().reason',scope),'unavailable');
 assert.equal(nativeCalls,1,'Optional diagnostics failure must not prevent replay evidence');
}
function harness(unload=true,wholeSession=false){
 let resolveBytes;
 const pending=new Promise(resolve=>resolveBytes=resolve);
 const elements=new Map();
 const $=id=>{if(!elements.has(id))elements.set(id,{disabled:false,textContent:'',files:[],value:'performance',querySelectorAll:()=>[],replaceChildren(){},focus(){}});return elements.get(id);};
 $('retail-replay-file').files=[{size:1194,arrayBuffer:()=>pending}];
 const calls={native:0,unload:0,audio:0,paused:0,timingResets:0,failed:[],launch:0};
 const scope={$,retailRun:null,replayLoading:false,ready:true,fatal:false,bundle:true,importing:false,
  replayEvidence:[],uiMessage:'',inputDirty:false,clearRenderCacheOnLoad:false,
  window:{},setTimeout:fn=>fn(),
  owner:{handle:{getState:()=>({state:'prepared'})}},
  TextEncoder,Uint8Array,DataView,URL,performance,replayHash:async()=> 'a'.repeat(64),
  replayMemorySnapshot:()=>({wasm_heap_bytes:2048}),
  status:()=> 'teardown failed',unloadAndSave:async()=>{calls.unload++;return unload;},
  resetTiming:()=>{calls.timingResets++;},prepareAudio:async()=>{calls.audio++;},pauseAudioForPreparation:async()=>{},
  beginReplayPaintControl:()=>({evidence:{mode:'normal'},restore(){}}),
  boundary:async fn=>fn(),check:value=>assert.equal(value,1),syncAudio(){},
  finishRetailReplay:async reason=>{calls.failed.push(reason);calls.completedRun=scope.retailRun;scope.retailRun=null;},
  Module:{HEAPU8:new Uint8Array(2048),_malloc:()=>1,_free(){},
   _melee_web_native_menu_replay:()=>{calls.native++;return 1;},
   _melee_web_native_menu_replay_whole_session:()=>wholeSession?1:0,
   _melee_web_native_menu_launch:()=>{calls.launch++;return 1;},
   _melee_web_native_menu_running:()=>1,
   _melee_web_native_menu_pause:()=>{calls.paused++;}}};
 const replayLimits=page.match(/const RETAIL_REPLAY_(?:LEGACY_MAX_FRAMES|WHOLE_SESSION_MAX_FRAMES|STATE_RECORD_OVERHEAD|SESSION_RECORD_OVERHEAD|TIMER_RECORD_OVERHEAD|MAX_BYTES)[^;]*;/g).join('\n');
 vm.createContext(scope);vm.runInContext(replayLimits+'\n'+start+'\n'+pause,scope);
 return {$,scope,calls,resolveBytes,play:()=>$('retail-replay-start').onclick()};
}
{
 const h=harness();
 vm.runInContext(completion,h.scope);
 h.scope.retailRun={};
 h.scope.window.menuReplayCompleted(686,1,2,1);
 assert.equal(h.calls.completedRun.sourceMatch.complete,true);
 assert.equal(h.calls.completedRun.sourceMatch.outcome,2);
 assert.equal(h.calls.completedRun.sourceMatch.winner,1);
}
{
 const h=harness();
 vm.runInContext(completion,h.scope);
 h.scope.retailRun={};
 h.scope.window.menuReplayCompleted(686);
 assert.equal(h.calls.completedRun.sourceMatch.complete,false);
 assert.equal(h.calls.completedRun.sourceMatch.outcome,null);
 assert.equal(h.calls.completedRun.sourceMatch.winner,null);
}
{
 const h=harness();const first=h.play();const duplicate=h.play();
 assert.equal(h.scope.replayLoading,true,'replayLoading is set before the first async read');
 assert.equal(h.$('disc').disabled,true,'disc import is disabled during replay loading');
 h.resolveBytes(new ArrayBuffer(1194));await Promise.all([first,duplicate]);
 assert.equal(h.calls.unload,1);assert.equal(h.calls.native,1);assert.equal(h.scope.replayLoading,false);
 await h.play();assert.equal(h.calls.native,1,'An active replay cannot be replaced');
}
{
 const h=harness(false);const first=h.play();h.resolveBytes(new ArrayBuffer(1194));await first;
 assert.equal(h.calls.native,0);assert.equal(h.calls.audio,0);
 assert.equal(h.$('retail-replay-report').textContent,'teardown failed');
 assert.equal(h.scope.replayLoading,false);assert.equal(h.$('disc').disabled,false);
}
{
 const h=harness(true,true);const playing=h.play();
 const bytes=new ArrayBuffer(1194),header=new DataView(bytes);
 header.setUint32(0,0x4d575243,false);header.setUint32(4,8,false);
 h.resolveBytes(bytes);await playing;
 assert.equal(h.calls.unload,0,'Whole-session replay retains the canonical prepared CSS assets');
 assert.equal(h.calls.native,1);
 assert.equal(h.calls.launch,1,'A whole-session recipe enters CSS through the ordinary launch');
}
{
 const h=harness(true,true);
 h.scope.owner.handle.getState=()=>({state:'match'});
 const playing=h.play(),bytes=new ArrayBuffer(1194),header=new DataView(bytes);
 header.setUint32(0,0x4d575243,false);header.setUint32(4,8,false);
 h.resolveBytes(bytes);await playing;
 assert.equal(h.calls.unload,0,'A rejected late upload must preserve the active match');
 assert.equal(h.calls.native,0);assert.equal(h.calls.launch,0);
 assert.equal(h.calls.failed.length,0,'No replay owner exists to tear down');
 assert.match(h.$('retail-replay-report').textContent,/freshly imported disc/);
}
{
 const h=harness();const playing=h.play();h.resolveBytes(new ArrayBuffer(1194));await playing;
 assert.equal(h.calls.native,1);
 assert.equal(h.calls.launch,0,'A single-match recipe keeps its direct match construction');
}
{
 const h=harness();h.scope.retailRun={observe:false};await h.$('pause').onclick();
 assert.equal(h.calls.paused,0);assert.match(h.calls.failed[0],/Manual pause\/resume/);
}
{
 const h=harness();
 h.scope.Module._melee_web_native_menu_replay=()=>{assert.equal(h.calls.timingResets,1,'Reset previous active timing before native entry can reject');throw Error('Fresh application required');};
 const playing=h.play();h.resolveBytes(new ArrayBuffer(1194));await playing;
 assert.equal(h.calls.timingResets,1);
 assert.deepEqual(h.calls.failed,['Fresh application required']);
 assert.equal(h.calls.completedRun.frames,0,'Rejected entry consumes no source tick');
}
{
 const reset=page.split('\n').find(line=>line.startsWith('function resetTiming('));
 const frame=page.slice(page.indexOf('developmentHooks.frame='),page.indexOf("\nfor(const type of ['focus'")).replace('developmentHooks.frame=', 'window.menuFrame=');
 const metrics=page.split('\n').find(line=>line.startsWith('function replayMetrics('));
 let clock=1000,polls=0;
 const elements=new Map();
 const $=id=>{if(!elements.has(id))elements.set(id,{disabled:false,textContent:'',dataset:{},closest:()=>({open:false})});return elements.get(id);};
 const scope={window:{menuReplayPoll:()=>{polls++;}},performance:{now:()=>clock},document:{hidden:false,hasFocus:()=>true,activeElement:null},
  ready:true,fatal:false,importing:false,uiMessage:'hold',preparationSince:0,preparationLabel:'',preparationKeepsAudio:false,
  retailRun:null,stockCheckActive:false,actionSweepActive:false,selectionDriveActive:false,$,log(){},syncAudio(){},
  Module:{HEAPU8:new Uint8Array(2048),_melee_web_native_menu_phase:()=>7,_melee_web_native_menu_stock_check_ready:()=>1,
   _melee_web_native_menu_running:()=>1,_melee_web_native_menu_message:()=>0,_melee_web_native_menu_diagnostics:()=>0,
   _melee_web_input_message:()=>0,_melee_web_native_menu_replay_cursor:()=>42,UTF8ToString:value=>value}};
 vm.createContext(scope);vm.runInContext(reset+'\n'+frame+'\n'+metrics,scope);
 scope.resetTiming();scope.frameLast=1000;scope.perfLastReport=2000;
 scope.nativePreviousTiming={frame:3,total_ms:7,end_phases:{staging_writes_ms:0.4}};
 scope.nativeLastTiming={frame:4,total_ms:18,end_phases:{staging_writes_ms:13}};clock=1034.72;
 let report=scope.replayMetrics({consumed:42});
 assert.equal(report.worstBrowserCallback,null,'No interval is recorded before a callback runs');
 scope.window.menuFrame(true);report=scope.replayMetrics({consumed:42});
 assert.ok(Math.abs(report.worstBrowserCallback.interval_ms-34.72)<1e-9);
 assert.equal(report.worstBrowserCallback.started,1000);assert.equal(report.worstBrowserCallback.ended,1034.72);
 assert.equal(report.worstBrowserCallback.native.frame,4);assert.equal(report.worstBrowserCallback.previous_native.frame,3);
 assert.equal(report.worstBrowserCallback.native.end_phases.staging_writes_ms,13);
 assert.equal(report.worstBrowserCallback.previous_native.end_phases.staging_writes_ms,0.4);
 assert.equal(report.browserCallbacks,1);assert.equal(polls,1,'Replay polling remains per callback');
 scope.resetTiming(false);assert.equal(scope.replayMetrics({consumed:42}).worstBrowserCallback,null,'Reset clears the bounded browser interval record');
}
{
 const reset=page.split('\n').find(line=>line.startsWith('function resetTiming('));
 const frame=page.slice(page.indexOf('developmentHooks.frame='),page.indexOf("\nfor(const type of ['focus'")).replace('developmentHooks.frame=', 'window.menuFrame=');
 let clock=1016,polls=0,sceneReads=0,inputReads=0;
 const elements=new Map();
 const $=id=>{if(!elements.has(id))elements.set(id,{disabled:false,textContent:'',dataset:{},closest:()=>({open:false})});return elements.get(id);};
 $('scene-check').closest=()=>{sceneReads++;return{open:true};};$('input').closest=()=>{inputReads++;return{open:true};};
 const scope={window:{menuReplayPoll:()=>{polls++;}},performance:{now:()=>clock},document:{hidden:false,hasFocus:()=>true,activeElement:null},
  ready:true,fatal:false,importing:false,uiMessage:'hold',preparationSince:0,preparationLabel:'',preparationKeepsAudio:false,
  retailRun:null,stockCheckActive:false,actionSweepActive:false,selectionDriveActive:false,$,log(){},syncAudio(){},
  Module:{HEAPU8:new Uint8Array(2048),_melee_web_native_menu_phase:()=>7,_melee_web_native_menu_stock_check_ready:()=>1,
   _melee_web_native_menu_running:()=>1,_melee_web_native_menu_message:()=>0,_melee_web_native_menu_diagnostics:()=>0,
   _melee_web_input_message:()=>0,UTF8ToString:value=>value}};
 vm.createContext(scope);vm.runInContext(reset+'\n'+frame.replace('developmentHooks.frame=', 'window.menuFrame='),scope);scope.resetTiming();scope.frameLast=1000;scope.perfLastReport=1000;
 scope.window.menuFrame(true);assert.equal(sceneReads,0);assert.equal(inputReads,0);assert.equal(polls,1);
 clock=1260;scope.window.menuFrame(true);assert.equal(sceneReads,1);assert.equal(inputReads,1);
 assert.equal(polls,2,'Replay polling remains per callback while diagnostics refresh is throttled');assert.equal(scope.activeFrames,2);
}
console.log('Actual browser replay handlers: duplicate start, failed teardown and manual timing pause rejected.');
