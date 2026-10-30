// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeCpuHistoryMetadata,requireCpuHistoryMotor,retainCpuHistoryFailureAnchor} from './cpu_history_runner.mjs';
import {decodeCpuCorrectedLoopMotorEvidence,decodeCpuHistoryMotorEvidence}
  from './cpu_corrected_loop_runner.mjs';
import {validateCpuRestoreRouteSelection,recordCpuRestoreFailure} from './run_write_census.mjs';

test('history failure retains raw anchor after a sink failure without retrying it',async()=>{
  const report={},anchor={incarnations:[{replayRows:[{raw:{pad:'literal retained bytes'}}]}]};
  let saves=0;
  await retainCpuHistoryFailureAnchor(report,anchor,true,async()=>{saves++;},new Error('sink refused'));
  assert.equal(report.partialAnchor,anchor);
  assert.equal(report.partialAnchor.incarnations[0].replayRows[0].raw.pad,'literal retained bytes');
  assert.equal(report.partialAnchor.status,'failed');
  assert.match(report.partialAnchor.error,/sink refused/);
  assert.equal(saves,0);
  const other={};
  await retainCpuHistoryFailureAnchor(other,anchor,false,async()=>{saves++;throw new Error('disk write failed');},new Error('compare failed'));
  assert.equal(other.partialAnchor,anchor);
  assert.match(other.evidenceError,/disk write failed/);
  assert.equal(saves,1);
});

test('history metadata rejects extra capsules, overflow, terminal failure and malformed bytes',()=>{
  const words=[1,80,5,73,0,176,179,179,2,2,1,0,1,1,3,5,5,3,0,0];
  const wire=values=>{const bytes=Buffer.alloc(80);values.forEach((value,i)=>bytes.writeUInt32LE(value,i*4));return bytes;};
  assert.equal(decodeCpuHistoryMetadata(wire(words)).restoreCount,2);
  for(const [index,value] of [[1,84],[4,1],[6,184],[8,4],[9,3],[12,2],[13,2],[17,4],[19,1]]){
    const corrupt=[...words];corrupt[index]=value;assert.throws(()=>decodeCpuHistoryMetadata(wire(corrupt)));
  }
  assert.throws(()=>decodeCpuHistoryMetadata(Buffer.alloc(79)),/extent/);
});

test('new motor mode is explicit; legacy mode1 never accepts mode2',()=>{
  const wire=Buffer.alloc(1016);wire.writeUInt32LE(1,0);wire.writeUInt32LE(1016,4);wire.writeUInt32LE(2,8);
  assert.equal(decodeCpuHistoryMotorEvidence(wire).mode,2);
  assert.throws(()=>decodeCpuCorrectedLoopMotorEvidence(wire),/schema\/mode/);
  wire.writeUInt32LE(1,8);assert.equal(decodeCpuCorrectedLoopMotorEvidence(wire).mode,1);
  assert.throws(()=>decodeCpuHistoryMotorEvidence(wire),/schema\/mode/);
});

test('history permits a longer selected prefix while keeping its original motor journal immutable',()=>{
  const baseline={state:1,span:2,framesSeen:2,firstFrame:176,token:'9',world:'10',contextEpoch:'11',events:[],eventCount:0};
  const value={mode:2,failure:0,commitState:0,published:0,selectedLane:3,baseline,
    selected:{...baseline,span:3,framesSeen:3}};
  const options={preFrame:176,span:3,selectedLane:3,baseline:structuredClone(baseline)};
  assert.equal(requireCpuHistoryMotor(value,options),value);
  assert.throws(()=>requireCpuHistoryMotor({...value,selected:{...value.selected,token:'12'}},options),/owner/);
  assert.throws(()=>requireCpuHistoryMotor({...value,baseline:{...baseline,eventCount:1}},options),/original journal/);
  assert.throws(()=>requireCpuHistoryMotor({...value,published:1},options),/capture-only/);
});

test('history route is separate and preserves failures without claiming a successful restore',()=>{
  const route={'--correct-predicted-branch-history-packets':true,'--aggregate-cpu-drawn-reference-seven-tick':true,
    '--aggregate-cpu-restore-config':'fixture','--slippi-lobj-presentation-cache':true,
    '--slippi-tobj-presentation-cache':true,'--slippi-particle-presentation-cache':true};
  assert.equal(validateCpuRestoreRouteSelection(route),'independent-browser-retained-history-packets-v1');
  for(const flag of ['--correct-predicted-branch','--correct-predicted-branch-two-window',
    '--correct-predicted-branch-loop','--correct-predicted-branch-loop-packets'])
    assert.throws(()=>validateCpuRestoreRouteSelection({...route,[flag]:true}),/mutually exclusive/);
  assert.throws(()=>validateCpuRestoreRouteSelection({'--correct-predicted-branch-history-packets':true}),/preparation/);
  const report={execution:{intervalEvidence:[{index:0,sha256:'retained'}]}},error=new Error('first mismatch');
  error.cpuHistoryReport={status:'failed',error:String(error),nativeProgress:{predicted:{commands:[{command:71,result:1}]}}};
  recordCpuRestoreFailure(report,error);
  assert.equal(report.execution.retainedHistory,error.cpuHistoryReport);
  assert.equal(report.execution.intervalEvidence.length,1);
  assert.match(report.execution.restorePerformed,/unknown-on-failure/);
});
