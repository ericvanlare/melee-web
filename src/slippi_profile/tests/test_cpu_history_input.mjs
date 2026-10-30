// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import {buildCpuHistoryCorpus,createCpuHistoryTimeline,admitCpuHistoryRows,
  planCpuHistoryReplay} from './cpu_history_input.mjs';

const corpus=buildCpuHistoryCorpus().map(value=>Buffer.from(value,'base64'));
function fixture(){return {timeline:createCpuHistoryTimeline(),selected:[],received:new Map()};}
function forward(f,index){
  const tick=300+index;
  f.timeline.recordLocal(tick,corpus[index].subarray(0,11));
  const plan=f.timeline.planForward();
  assert.equal(plan.kind,'forward');
  assert.equal(plan.tick,tick);
  return plan;
}
function acknowledge(f,plan){
  f.timeline.commitForward(plan);
  f.selected.push({tick:plan.tick,frame:Buffer.from(plan.frame)});
}
function arrival(f,index){
  admitCpuHistoryRows(f.timeline,f.received,[{tick:300+index,remotePad:corpus[index].subarray(11,22)}]);
}
function corrected(f){
  const plan=planCpuHistoryReplay(f.timeline,f.selected,f.received,300);
  // This test models completion only; the browser driver must first verify the
  // actual native replay and strict endpoint/raw comparisons.
  f.timeline.commitReplay(plan.replay);
  f.selected=plan.frames.map(row=>({tick:row.tick,frame:Buffer.from(row.frame)}));
  return plan;
}
test('two real dirty arrivals replay the same full PRE with a second dirty suffix',()=>{
  const f=fixture();
  acknowledge(f,forward(f,0));acknowledge(f,forward(f,1));
  arrival(f,0);arrival(f,1);
  assert.throws(()=>f.timeline.planForward(),/Correction replay/);
  const first=corrected(f);
  assert.equal(first.dirtyFrom,300);assert.equal(first.frames.length,2);
  assert.deepEqual(first.frames.map(row=>row.frame),corpus.slice(0,2));
  acknowledge(f,forward(f,2));arrival(f,2);
  const second=corrected(f);
  assert.equal(second.anchorSample,300);assert.equal(second.dirtyFrom,302);
  assert.equal(second.replay.frames.length,1);assert.equal(second.frames.length,3);
  assert.deepEqual(second.frames.map(row=>row.unchangedPrefix),[true,true,false]);
  assert.deepEqual(second.frames.map(row=>row.frame),corpus.slice(0,3));
  assert.equal(f.timeline.status().dirty_from,null);
});
test('receive waits for native acknowledgement before dirtying the executed history',()=>{
  const f=fixture(),plan=forward(f,0);
  assert.throws(()=>arrival(f,0),/native execution acknowledgement/);
  assert.equal(f.received.size,0);
  acknowledge(f,plan);arrival(f,0);
  assert.equal(f.timeline.status().dirty_from,300);
});
test('an altered executed prefix is refused before a replay plan is created',()=>{
  const f=fixture();
  acknowledge(f,forward(f,0));acknowledge(f,forward(f,1));arrival(f,0);arrival(f,1);corrected(f);
  acknowledge(f,forward(f,2));arrival(f,2);
  f.selected[0].frame[13]=0;
  assert.throws(()=>planCpuHistoryReplay(f.timeline,f.selected,f.received,300),/unchanged prefix/);
  assert.equal(f.timeline.status().plan_outstanding,false);
});
test('missing authority, out-of-anchor history and conflicting received bytes fail closed',()=>{
  const f=fixture();acknowledge(f,forward(f,0));acknowledge(f,forward(f,1));arrival(f,0);
  assert.throws(()=>planCpuHistoryReplay(f.timeline,f.selected,f.received,300),/confirmed dirty suffix/);
  arrival(f,1);
  assert.throws(()=>planCpuHistoryReplay(f.timeline,f.selected,f.received,301),/confirmed dirty suffix/);
  assert.throws(()=>admitCpuHistoryRows(f.timeline,f.received,[{tick:300,remotePad:new Uint8Array(11)}]),/conflicts/);
});
