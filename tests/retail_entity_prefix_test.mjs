import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const page=fs.readFileSync(new URL('../web/runtime-development.mjs',import.meta.url),'utf8');
const finish=page.slice(page.indexOf('function finishRetailReplay('),page.indexOf('window.menuReplayStarted='));
for(const [prefix,teardown,observations,bound] of [[true,true,60,60],[true,false,60,60],[true,true,59,60],[true,true,61,61],[true,true,60,61],[true,true,65,65],[false,true,0,0]]){
 let release,unloads=0;
 const pending=new Promise(resolve=>{release=resolve;});
 const run={memory:{},baseline:{},frames:62,wholeSession:true,observe:true,rows:[],timerRows:[],
  diagnosticEntityPrefix:prefix,prefixProgress:{observations,bound_observations:bound,first_source_tick:0,last_source_tick:observations-1},
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
 const prefixReady=bound>=60&&bound<=64&&observations===bound;
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
