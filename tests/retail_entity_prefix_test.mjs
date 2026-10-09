import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const page=fs.readFileSync(new URL('../web/runtime-development.mjs',import.meta.url),'utf8');
const finish=page.slice(page.indexOf('function finishRetailReplay('),page.indexOf('window.menuReplayStarted='));
for(const [prefix,teardown,observations,bound,mode] of [[true,true,60,60],[true,false,60,60],[true,true,59,60],[true,true,61,61],[true,true,60,61],[true,true,65,65],[false,true,0,0],[true,true,120,120,2],[true,false,120,120,2],[true,true,60,60,2],[true,true,605,605,2]]){
 let release,unloads=0;
 const pending=new Promise(resolve=>{release=resolve;});
 const run={memory:{},baseline:{},frames:62,wholeSession:true,observe:true,rows:[],timerRows:[],
  diagnosticEntityPrefix:prefix,diagnosticEntityPrefixMode:prefix?(mode??1):0,prefixProgress:{observations,bound_observations:bound,first_source_tick:0,last_source_tick:observations-1},
  finalScene:prefix?3:1,sourceMatch:{complete:false,outcome:0,winner:-1}};
 const downloads=[];
 const scope={window:{},retailRun:run,performance:{now:()=>900},
  replayMetrics:()=>({sourceFrames:62,sourceSteps:62,sourceDraws:62}),
  replayMemorySnapshot:()=>({available:true}),unloadAndSave:async()=>{unloads++;return pending;},
  status:()=> 'checked teardown rejected',latestAudio:{},diagnosticCaptureInvalid:false,
  navigator:{userAgent:'synthetic'},devicePixelRatio:1,TextEncoder,
  replayHash:async()=> 'synthetic',replayDownload:(name,text)=>downloads.push([name,text]),
  $:()=>({}),syncAudio(){},fatal:false,bundle:true,importing:false};
 vm.createContext(scope);vm.runInContext(finish,scope);
 const done=scope.finishRetailReplay(null);
 assert.equal(unloads,1);assert.equal(downloads.length,0,'Evidence waits for actual owned teardown');
 release(teardown);await done;
 const report=scope.window.lastRetailReplayReport;
 const prefixReady=bound>=(mode===2?61:60)&&bound<=(mode===2?604:64)&&observations===bound;
 assert.equal(report.pass,teardown&&(!prefix||prefixReady));
 if(prefix){
  assert.equal(report.complete,false);
  assert.equal(report.whole_session_equivalent,false);
  assert.equal(report.diagnostic_prefix_complete,teardown&&prefixReady);
  assert.deepEqual(JSON.parse(JSON.stringify(report.source_match)),{complete:false,outcome:null,winner:null});
 }else assert.equal(report.complete,true,'Flag-zero whole-session report remains unchanged');
 assert.equal(scope.retailRun,null);
}
console.log('actual prefix report/awaited teardown controls passed');

// Execute the actual header/owner initialization and callback, so a failure
// before menuReplayCompleted cannot silently become a whole-session report.
const requestedBlock=page.slice(page.indexOf('  const header=new DataView('),page.indexOf('  const run=retailRun;',page.indexOf('  const header=new DataView(')));
const completionBlock=page.slice(page.indexOf('window.menuReplayCompleted='),page.indexOf('window.menuReplayPoll='));
for(const [mode,completed] of [[1,null],[1,false],[0,true],[2,null],[2,1]]){
 const requested=mode!==0;
 const bytes=new Uint8Array(328);const header=new DataView(bytes.buffer);
 header.setUint32(0,0x4d575243);header.setUint32(4,8);header.setUint16(20,2);header.setUint16(22,mode);
 const callbacks=[];let unloads=0;
 const element={disabled:false,querySelectorAll:()=>[],replaceChildren(){}};
 const scope={window:{},retailRun:null,bytes,hash:'synthetic',observe:true,
  owner:{handle:{getState:()=>({state:'prepared'})}},$:()=>element,URL:{revokeObjectURL(){}},
  resetTiming(){},prepareAudio:async()=>{},pauseAudioForPreparation:async()=>{},
  performance:{now:()=>0},Module:{},clearRenderCacheOnLoad:false,replayEvidence:[],
  replayMemorySnapshot:()=>({available:true}),replayMetrics:()=>({sourceFrames:0,sourceSteps:0,sourceDraws:0}),
  unloadAndSave:async()=>{unloads++;return true;},status:()=>'',latestAudio:{},diagnosticCaptureInvalid:false,
  navigator:{userAgent:'synthetic'},devicePixelRatio:1,TextEncoder,replayHash:async()=> 'synthetic',
  replayDownload(){},syncAudio(){},fatal:false,bundle:true,importing:false,
  setTimeout:fn=>callbacks.push(fn)};
 vm.createContext(scope);vm.runInContext(finish,scope);
 await vm.runInContext('(async()=>{'+requestedBlock+'})()',scope);
 assert.equal(scope.retailRun.diagnosticEntityPrefix,requested);
 if(completed!==null){vm.runInContext(completionBlock,scope);scope.window.menuReplayCompleted(0,false,0,-1,3,completed,60,0,59,60);
  assert.equal(scope.retailRun.diagnosticEntityPrefix,requested,'Callback cannot overwrite requested identity');
  assert.match(scope.retailRun.failure,/mode disagrees/);
  await callbacks[0]();await scope.retailRun?.finishPromise;
 }else await scope.finishRetailReplay('Replay exceeded bounded wall time');
 const report=scope.window.lastRetailReplayReport;assert.equal(report.pass,false);assert.equal(unloads,1);
 if(requested){assert.equal(report.diagnostic_prefix,mode===2?'jiggly-ice-mario-fox-active60-v1':'jiggly-ice-mario-fox-v1');
  if(mode===2)assert.equal(report.comparison_source_ticks,null,'Unknown completion count stays unknown on early failure');assert.equal(report.diagnostic_prefix_complete,false);
  assert.equal(report.whole_session_equivalent,false);assert.equal(report.complete,false);
  assert(!report.failures.includes('whole-session final CSS was not entered'));
 }else assert.equal(report.diagnostic_prefix,undefined);
}
console.log('actual requested-mode early failure/mismatch controls passed');
