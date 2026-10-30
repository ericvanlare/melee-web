// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import {compareCorrectedRawPairs,validateCorrectedCommandEvidence,recordPredictedActionDivergence,
  createCorrectedCommandTimer,operateCorrectedInPage} from './cpu_corrected_runner.mjs';
import {validateCpuRestoreRouteSelection} from './run_write_census.mjs';

const lanes={Observation:1280,Pcm:4272,Pad:44,RngProfile:32,Transfer:40};
function rawPairSet(){
  const reference={tickPairs:[]},corrected={tickPairs:[]};
  for(let i=0;i<7;++i){
    const a={preFrame:176+i,postFrame:177+i},b={...a};
    for(const [lane,size] of Object.entries(lanes)){
      const bytes=Buffer.alloc(size,i+1).toString('base64');
      a[`baseline${lane}`]=bytes;b[`replay${lane}`]=bytes;
      // A wrong speculative branch must never supply the expected state.
      b[`baseline${lane}`]=Buffer.alloc(size,55).toString('base64');
    }
    reference.tickPairs.push(a);corrected.tickPairs.push(b);
  }
  return {reference,corrected};
}
test('corrected raw comparison uses independent reference and every exact lane',()=>{
  const {reference,corrected}=rawPairSet();
  assert.equal(compareCorrectedRawPairs(reference,corrected).equal,true);
  for(const lane of Object.keys(lanes)){
    const corrupt=structuredClone(corrected),bytes=Buffer.from(corrupt.tickPairs[4][`replay${lane}`],'base64');
    bytes[1]^=1;corrupt.tickPairs[4][`replay${lane}`]=bytes.toString('base64');
    assert.throws(()=>compareCorrectedRawPairs(reference,corrupt),new RegExp(`${lane} differs at sample 304, byte 1`));
  }
  assert.throws(()=>compareCorrectedRawPairs(reference,{tickPairs:corrected.tickPairs.slice(1)}),/seven exact pairs/);
  const changed=structuredClone(corrected);changed.tickPairs[0].postFrame++;
  assert.throws(()=>compareCorrectedRawPairs(reference,changed),/source frame differs/);
});
test('local command seal never stands in for independent equality',()=>{
  const e={schema:2,mode:7,lastCommand:49,firstFailure:0,state:7,preFrame:176,postFrame:183,
    cpuEquality:0,drawnEquality:0,checkedCpu:0,matchedCpu:0,checkedDrawn:0,matchedDrawn:0,
    commitResult:0,baselineStepNoDrawMask:127,replayStepNoDrawMask:127};
  assert.equal(validateCorrectedCommandEvidence(e,49,176),e);
  for(const patch of [{state:8},{firstFailure:1},{cpuEquality:1},{matchedCpu:1022},{postFrame:184},
    {commitResult:1},{baselineStepNoDrawMask:63},{replayStepNoDrawMask:63}])
    assert.throws(()=>validateCorrectedCommandEvidence({...e,...patch},49,176));
  assert.throws(()=>validateCorrectedCommandEvidence(e,50,176));
});
test('changed-input route requires the existing seven-tick private profile',()=>{
  const config={'--correct-predicted-branch':true,'--aggregate-cpu-drawn-reference-seven-tick':true,
    '--aggregate-cpu-restore-config':'fixture','--slippi-lobj-presentation-cache':true,
    '--slippi-tobj-presentation-cache':true,'--slippi-particle-presentation-cache':true};
  assert.equal(validateCpuRestoreRouteSelection(config),'independent-browser-correction-v1');
  assert.throws(()=>validateCpuRestoreRouteSelection({'--correct-predicted-branch':true}),/fixed seven-tick/);
  assert.throws(()=>validateCpuRestoreRouteSelection({...config,'--slippi-particle-presentation-cache':false}),/private v19/);
});
test('wrong-branch proof needs changed selection requests, not changed cache identity',()=>{
  const endpoint=motion=>({domains:[{id:3,records:[{kindName:'ActionEvent',index:0,
    fieldNames:['Kind','Destination','Motion','Slot','ResultDomain','Clip'],
    fields:['2','100',motion,'0','0','200'],bytesBase64:''}]}]});
  assert.equal(recordPredictedActionDivergence(endpoint('60'),endpoint('14')).different,true);
  assert.throws(()=>recordPredictedActionDivergence(endpoint('60'),endpoint('60')),/selection requests/);
  const changedClip=endpoint('60');changedClip.domains[0].records[0].fields[5]='999';
  assert.throws(()=>recordPredictedActionDivergence(endpoint('60'),changedClip),/selection requests/);
});

test('corrected command timing separates setup, async native call, GPU wait and evidence work',async()=>{
  let clock=0,failNative=false;
  const heap=new Uint8Array(100000),priorWindow=Object.getOwnPropertyDescriptor(globalThis,'window');
  const priorPerformance=Object.getOwnPropertyDescriptor(globalThis,'performance');
  const priorBtoa=Object.getOwnPropertyDescriptor(globalThis,'btoa');
  const module={HEAPU8:heap,
    _melee_web_snapshot_input:()=>{clock+=3;return 1;},
    _melee_web_synctest_cpu_corrected_evidence_bytes:()=>93512,
    _melee_web_synctest_cpu_corrected_evidence:()=>100,
    _melee_web_synctest_cpu_corrected_loop_evidence_bytes:()=>40,
    _melee_web_synctest_cpu_corrected_loop_evidence:()=>{clock+=2;return 94000;},
    _melee_web_synctest_cpu_corrected_loop_motor_commit_receipt_bytes:()=>64,
    _melee_web_synctest_cpu_corrected_loop_motor_commit_receipt:()=>{clock+=1;return 95000;},
    _melee_web_snapshot_observation:()=>{clock+=2;return 94000;},
    _melee_web_synctest_draw_diagnostics:()=>{clock+=1;return 96000;},
    _melee_web_synctest_motion_boundary:()=>0,_melee_web_synctest_is_mario_kind:()=>1,
    async ccall(){clock+=7;if(failNative)throw nativeFailure;return 0;},
    UTF8ToString:()=>''};
  const nativeFailure=new Error('native command failed');
  const mapping=[{sourceSlot:0,initializerPort:0,rawController:0},
    {sourceSlot:1,initializerPort:0,rawController:1}];
  globalThis.window={__slippiSynctest:{module},__timingAdvance:milliseconds=>{clock+=milliseconds;}};
  Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>clock}});
  Object.defineProperty(globalThis,'btoa',{configurable:true,value:value=>{
    if(value.length===132)clock+=2;
    return Buffer.from(value,'binary').toString('base64');
  }});
  const base={commandTimingSource:createCorrectedCommandTimer.toString(),pad:Buffer.alloc(44).toString('base64')};
  try{
    const retire=await operateCorrectedInPage({...base,command:53,readLoopEvidence:true,
      readLoopMotorEvidence:true});
    assert.equal(retire.timing.preCallSetupMs,3);
    assert.equal(retire.timing.nativeCallWallMs,7);
    assert.equal(retire.timing.postCallEvidenceMs,3);
    assert.equal(retire.timing.gpuQuiescenceWaitMs,0);
    assert.equal(retire.timing.clock,'browser.performance.now');
    assert.match(retire.timing.interpretation,/Asyncify waits.*not pure CPU or GPU/);
    assert.deepEqual(window.__slippiSynctest.cpuCorrectedProgress.commands[0].timing,retire.timing,
      'retirement timing remains in page progress as well as the returned command');

    const draw=await operateCorrectedInPage({...base,command:32,draw:true,waitSource:
      'async function wait(){window.__timingAdvance(4);return {waited:true};}',
      decodeDrawSource:'bytes=>({slotCount:2})',inputMappingSource:'value=>value',
      inspectSource:'(observation,context)=>({interactive:true,players:[]})',mapping,stocks:[4,4]});
    assert.equal(draw.timing.nativeCallWallMs,7);
    assert.equal(draw.timing.gpuQuiescenceWaitMs,4);
    assert.equal(draw.timing.postCallEvidenceMs,5,
      'the wait is excluded while page-side diagnostic base64 encoding remains timed');

    failNative=true;
    await assert.rejects(()=>operateCorrectedInPage({...base,command:33}),error=>error===nativeFailure,
      'timing capture must preserve the original native command error');
    const failed=window.__slippiSynctest.cpuCorrectedProgress.commands.at(-1);
    assert.equal(failed.error,'Error: native command failed');
    assert.equal(failed.timing.phase,'failed');
    assert.equal(failed.timing.failedDuring,'native-call');
    assert.equal(failed.timing.nativeCallWallMs,7);
  }finally{
    if(priorWindow)Object.defineProperty(globalThis,'window',priorWindow);else delete globalThis.window;
    if(priorPerformance)Object.defineProperty(globalThis,'performance',priorPerformance);
    else delete globalThis.performance;
    if(priorBtoa)Object.defineProperty(globalThis,'btoa',priorBtoa);else delete globalThis.btoa;
  }
});
