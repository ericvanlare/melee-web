import assert from 'node:assert/strict';
import {configureDepthNoWorkBootV1,requireDepthNoWorkComponentV1} from '../web/melee-runtime.mjs';
let passed=0;
{
 const calls=[],receiver={};let phase='INSTALLED';
 const module={onRuntimeInitialized(...args){calls.push(['prior',this,args]);return 17;},
  __meleeWebDepthNoWorkV1:{status:()=>({schema:'melee-web-private-depth-no-work-v1',phase,firstFailure:null,bridgeFailure:false})},
  _aurora_browser_depth_no_work_premain(){calls.push(['native-premain']);phase='PREMAIN';return 1;}};
 configureDepthNoWorkBootV1(module);assert.equal(module.onRuntimeInitialized.call(receiver,5),17);
 assert.equal(calls[0][0],'native-premain');assert.equal(calls[1][0],'prior');assert.equal(calls[1][1],receiver);assert.deepEqual(calls[1][2],[5]);
 assert.throws(()=>requireDepthNoWorkComponentV1(module));phase='ACTIVE';assert.equal(requireDepthNoWorkComponentV1(module).phase,'ACTIVE');++passed;
}
for(const broken of ['missing','false','dirty','bridge']){
 let called=false;const status={schema:'melee-web-private-depth-no-work-v1',phase:'PREMAIN',firstFailure:null,bridgeFailure:false};
 const module={onRuntimeInitialized(){called=true;},__meleeWebDepthNoWorkV1:{status:()=>status},_aurora_browser_depth_no_work_premain:()=>broken==='false'?0:1};
 if(broken==='missing')delete module._aurora_browser_depth_no_work_premain;if(broken==='dirty')status.firstFailure={code:'DEPTH_FORBIDDEN_READ'};if(broken==='bridge')status.bridgeFailure=true;
 configureDepthNoWorkBootV1(module);assert.throws(()=>module.onRuntimeInitialized());assert.equal(called,false);++passed;
}
assert.throws(()=>configureDepthNoWorkBootV1({}));++passed;
console.log(`depth no-work actual boot helper: ${passed} controls passed; no native/GPU fixture claim`);
