// Exercise the actual page event handlers across asynchronous startup failures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const page=fs.readFileSync(new URL('../web/runtime.html',import.meta.url),'utf8');
const start=page.slice(page.indexOf("$('retail-replay-start').onclick="),page.indexOf('\nvar Module='));
const pause=page.split('\n').find(line=>line.startsWith("$('pause').onclick="));
const completion=page.slice(page.indexOf('window.menuReplayCompleted='),page.indexOf('\nwindow.menuReplayPoll='));
assert(start&&pause&&completion);
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
function harness(unload=true){
 let resolveBytes;
 const pending=new Promise(resolve=>resolveBytes=resolve);
 const elements=new Map();
 const $=id=>{if(!elements.has(id))elements.set(id,{disabled:false,textContent:'',files:[],value:'performance',querySelectorAll:()=>[],replaceChildren(){},focus(){}});return elements.get(id);};
 $('retail-replay-file').files=[{size:1194,arrayBuffer:()=>pending}];
 const calls={native:0,unload:0,audio:0,paused:0,failed:[]};
 const scope={$,retailRun:null,replayLoading:false,ready:true,fatal:false,bundle:true,importing:false,
  replayEvidence:[],uiMessage:'',inputDirty:false,clearRenderCacheOnLoad:false,
  window:{},setTimeout:fn=>fn(),
  TextEncoder,Uint8Array,URL,performance,replayHash:async()=> 'a'.repeat(64),
  replayMemorySnapshot:()=>({wasm_heap_bytes:2048}),
  status:()=> 'teardown failed',unloadAndSave:async()=>{calls.unload++;return unload;},
  prepareAudio:async()=>{calls.audio++;},pauseAudioForPreparation:async()=>{},
  boundary:async fn=>fn(),check:value=>assert.equal(value,1),syncAudio(){},
  finishRetailReplay:async reason=>{calls.failed.push(reason);calls.completedRun=scope.retailRun;scope.retailRun=null;},
  Module:{HEAPU8:new Uint8Array(2048),_malloc:()=>1,_free(){},
   _melee_web_native_menu_replay:()=>{calls.native++;return 1;},
   _melee_web_native_menu_running:()=>1,
   _melee_web_native_menu_pause:()=>{calls.paused++;}}};
 vm.createContext(scope);vm.runInContext(start+'\n'+pause,scope);
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
 assert.equal(h.scope.replayLoading,true);assert.equal(h.$('disc').disabled,true);
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
 const h=harness();h.scope.retailRun={observe:false};await h.$('pause').onclick();
 assert.equal(h.calls.paused,0);assert.match(h.calls.failed[0],/Manual pause\/resume/);
}
console.log('Actual browser replay handlers: duplicate start, failed teardown and manual timing pause rejected.');
