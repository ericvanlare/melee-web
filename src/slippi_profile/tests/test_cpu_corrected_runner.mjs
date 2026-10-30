// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import {compareCorrectedRawPairs,validateCorrectedCommandEvidence,recordPredictedActionDivergence} from './cpu_corrected_runner.mjs';
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
  assert.throws(()=>validateCpuRestoreRouteSelection({...config,'--slippi-particle-presentation-cache':false}),/private v18/);
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
