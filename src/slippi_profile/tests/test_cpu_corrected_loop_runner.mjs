// SPDX-License-Identifier: GPL-3.0-or-later
// Host controls only: no source simulation or browser acceptance claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {buildCpuCorrectedLoopCapsules,createCpuCorrectedLoopSchedule,
  planCpuCorrectedLoopForward,acknowledgeCpuCorrectedLoopForward,planCpuCorrectedLoopReplay,
  decodeCpuCorrectedLoopMetadata,validateCpuCorrectedLoopCommandEvidence,
  persistCpuCorrectedLoopInterval,runBrowserCpuCorrectedLoop,
  decodeCpuCorrectedLoopMotorEvidence,requireCpuCorrectedLoopMotorEvidence,
  compareCpuCorrectedLoopMotorEvents,hashCpuCorrectedLoopMotorEvents,
  decodeCpuCorrectedLoopMotorCommitReceipt} from './cpu_corrected_loop_runner.mjs';
import {decodeCpuCorrectedLoopEvidence} from './cpu_seven_tick_runner.mjs';
import {createCorrectedCommandTimer,operateCorrectedInPage} from './cpu_corrected_runner.mjs';
import {buildCpuCorrectedTwoWindowFirstCorpus,buildCpuCorrectedTwoWindowSecondCorpus,
  CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44 as neutral} from './cpu_corrected_two_window_runner.mjs';
const encode=value=>Buffer.from(value).toString('base64');
const decode=value=>Buffer.from(value,'base64');
const receipt=Object.freeze({path:'intervals/0.json',sha256:'a'.repeat(64),bytes:42});

function advance(schedule,tick,pad,speculative){
  const plan=planCpuCorrectedLoopForward(schedule,tick,pad,{speculative});
  acknowledgeCpuCorrectedLoopForward(schedule,plan);
  assert.throws(()=>acknowledgeCpuCorrectedLoopForward(schedule,plan),/stale/);
  return {tick,predictedPad44Base64:encode(plan.frame)};
}

test('literal eight-capsule recipe is exactly the continuing 44-tick original history',()=>{
  const recipe=buildCpuCorrectedLoopCapsules();
  assert.deepEqual(recipe.map(row=>row.span),[1,6,7,7,7,7,7,2]);
  assert.deepEqual(recipe.map(row=>row.startSample),[300,301,307,314,321,328,335,342]);
  assert.deepEqual(recipe.flatMap(row=>row.rows),[...buildCpuCorrectedTwoWindowFirstCorpus(),
    ...Array(35).fill(neutral),...buildCpuCorrectedTwoWindowSecondCorpus().slice(0,2)]);
  assert.throws(()=>buildCpuCorrectedLoopCapsules(1,0),/pinned/);
  const schedule=createCpuCorrectedLoopSchedule();
  try{
    for(const capsule of recipe){
      const speculative=capsule.kind.startsWith('speculative');
      const baseline=capsule.rows.map((row,i)=>advance(schedule,capsule.startSample+i,row,speculative));
      if(speculative){
        // Confirmed ticks 301..341 must replace S4 as the prediction seed at342.
        if(capsule.startSample===342)assert.equal(baseline[0].predictedPad44Base64,neutral);
        const actual=capsule.rows.map((row,i)=>({sample:capsule.startSample+i,pad44Base64:row}));
        const correction=planCpuCorrectedLoopReplay(schedule,baseline,actual,capsule.startSample);
        assert.equal(correction.earliestChanged,capsule.startSample);
        assert.deepEqual(correction.frames.map(row=>encode(row.frame)),capsule.rows);
        schedule.timeline.commitReplay(correction.replay);
        assert.throws(()=>schedule.timeline.commitReplay(correction.replay),/current|outstanding|stale/i);
        schedule.lastRemotePad=decode(capsule.rows.at(-1)).subarray(11,22);
      }
    }
    const status=schedule.timeline.status();
    assert.equal(status.simulated_through,343);assert.equal(status.confirmed_through,343);
    assert.equal(status.dirty_from,null);assert.equal(status.plan_outstanding,false);
  }finally{schedule.timeline.dispose();}
});

test('all spans 1..7 replay a late dirty suffix through the whole captured prefix',()=>{
  for(let span=1;span<=7;span++){
    const schedule=createCpuCorrectedLoopSchedule();
    try{
      const baseline=Array.from({length:span},(_,i)=>advance(schedule,300+i,neutral,true));
      const rows=Array.from({length:span},(_,i)=>({sample:300+i,
        pad44Base64:i===span-1?buildCpuCorrectedTwoWindowSecondCorpus()[0]:neutral}));
      const correction=planCpuCorrectedLoopReplay(schedule,baseline,rows,300);
      assert.equal(correction.earliestChanged,300+span-1);
      assert.equal(correction.replay.frames.length,1);
      assert.equal(correction.frames.length,span);
      assert.deepEqual(correction.frames.map(row=>row.unchangedPrefix),
        [...Array(span-1).fill(true),false]);
      assert.deepEqual(correction.frames.map(row=>encode(row.frame)),rows.map(row=>row.pad44Base64));
      assert.equal(schedule.timeline.status().dirty_from,300+span-1,'planning is not acknowledgement');
      schedule.timeline.commitReplay(correction.replay);
      assert.equal(schedule.timeline.status().dirty_from,null);
    }finally{schedule.timeline.dispose();}
  }
});

test('confirmed loop planning accepts an exact received remote PAD while speculative rows still predict',()=>{
  const schedule=createCpuCorrectedLoopSchedule(),literal=decode(neutral),received=Buffer.from(literal.subarray(11,22));
  received[2]=0x12;received[7]=0xa5;
  try{
    assert.throws(()=>planCpuCorrectedLoopForward(schedule,300,neutral,{remotePad:Buffer.alloc(10)}),/11 bytes/);
    const plan=planCpuCorrectedLoopForward(schedule,300,neutral,{remotePad:received});
    assert.equal(plan.remote_source,'actual');
    assert.deepEqual(Buffer.from(plan.frame).subarray(11,22),received);
    acknowledgeCpuCorrectedLoopForward(schedule,plan);
    assert.deepEqual(schedule.lastRemotePad,received);
    assert.throws(()=>planCpuCorrectedLoopForward(schedule,301,neutral,
      {speculative:true,remotePad:received}),/cannot accept PAD before its final draw/);
  }finally{schedule.timeline.dispose();}
});

test('later dirty suffix rejects a changed frozen prefix before source replay',()=>{
  const schedule=createCpuCorrectedLoopSchedule();
  try{
    const baseline=[advance(schedule,300,neutral,true),advance(schedule,301,neutral,true)];
    baseline[0].predictedPad44Base64=buildCpuCorrectedTwoWindowSecondCorpus()[0];
    assert.throws(()=>planCpuCorrectedLoopReplay(schedule,baseline,[{sample:300,pad44Base64:neutral},
      {sample:301,pad44Base64:buildCpuCorrectedTwoWindowSecondCorpus()[0]}],300),/unchanged prefix/);
  }finally{schedule.timeline.dispose();}
});

function nativeEvidence(span,command){
  const bytes=Buffer.alloc(93512),u32=(offset,value)=>bytes.writeUInt32LE(value>>>0,offset);
  const steps=command>=33&&command<=32+span?command-32:command>=40?span:0;
  const replay=command>=43&&command<=42+span?command-42:command>=50?span:0;
  const mask=count=>(1<<count)-1;
  const state=command===32?1:command<=32+span?(command===32+span?3:2):
    ({40:4,41:5,42:6,50:8,51:9}[command]??(command===42+span?7:6));
  [3,93512,span,state,0,100,100+span,command>=42?0:0xffffffff].forEach((v,i)=>u32(i*4,v));
  u32(56,mask(Math.max(1,steps))|(command>=41?128:0)|(mask(replay)<<8)|(command>=51?32768:0));
  u32(60,command);u32(64,mask(steps));u32(68,mask(replay));
  for(const offset of [...Array.from({length:span},(_,i)=>72+i*11416),79984]){
    u32(offset,1);u32(offset+4,11416);u32(offset+24,0xffffffff);
  }
  return bytes;
}

test('all native variable spans enforce exact command phase, masks, fixed diagnostics and zero capacity rows',()=>{
  for(let span=1;span<=7;span++){
    for(const command of [32,...Array.from({length:span},(_,i)=>33+i),40,41,42,
      ...Array.from({length:span},(_,i)=>43+i),50,51]){
      const bytes=nativeEvidence(span,command),evidence=decodeCpuCorrectedLoopEvidence(bytes,span);
      validateCpuCorrectedLoopCommandEvidence(evidence,command,{preFrame:100,span});
      assert.throws(()=>validateCpuCorrectedLoopCommandEvidence({...evidence,lastCommand:0},command,
        {preFrame:100,span}),/drift/);
      assert.throws(()=>validateCpuCorrectedLoopCommandEvidence({...evidence,diagnosticMask:0},command,
        {preFrame:100,span}),/diagnostic/);
      if(span<7){bytes[72+span*11416+32]=1;
        assert.throws(()=>decodeCpuCorrectedLoopEvidence(bytes,span),/unused raw-pair/);}
    }
  }
  for(const span of [0,8,1.5])assert.throws(()=>decodeCpuCorrectedLoopEvidence(Buffer.alloc(93512),span),/1..7/);
  const valid=decodeCpuCorrectedLoopEvidence(nativeEvidence(1,32),1);
  assert.throws(()=>validateCpuCorrectedLoopCommandEvidence({...valid,lastCommand:0,state:0},0,
    {preFrame:100,span:1}),/drift/);
  assert.throws(()=>validateCpuCorrectedLoopCommandEvidence(valid,32,
    {preFrame:0xffffffff,span:1}),/drift/);
  const bytes=Buffer.alloc(40);
  [3,40,99,99,0,1,1000,100,107,7].forEach((v,i)=>bytes.writeUInt32LE(v,i*4));
  assert.equal(decodeCpuCorrectedLoopMetadata(bytes).windowsCaptured,99,'owner is reusable beyond recipe8');
  bytes.writeUInt32LE(100,12);assert.throws(()=>decodeCpuCorrectedLoopMetadata(bytes),/counters/);
});

test('production persistence awaits exactly one sink before retirement and retains its immutable receipt on refusal',async()=>{
  let release;const barrier=new Promise(resolve=>{release=resolve;});const order=[];
  const interval={status:'compared',raw:{base64:'AAAA'}},summary={};
  const pending=persistCpuCorrectedLoopInterval(0,interval,summary,async(index,report)=>{
    assert.equal(index,0);assert.equal(report,interval);order.push('sink');await barrier;
    order.push('saved');return receipt;
  },async()=>{order.push('retire');assert.equal(summary.evidence,receipt);throw Error('retire first failure');});
  await Promise.resolve();assert.deepEqual(order,['sink']);release();
  await assert.rejects(pending,/retire first failure/);
  assert.deepEqual(order,['sink','saved','retire']);assert.equal(summary.evidence,receipt);
  for(const sink of [async()=>{throw Error('disk refused');},async()=>({...receipt,sha256:'bad'})]){
    let retireCount=0;await assert.rejects(persistCpuCorrectedLoopInterval(0,interval,{},sink,
      async()=>{retireCount++;}));assert.equal(retireCount,0);
  }
  const complete={};await persistCpuCorrectedLoopInterval(1,{},complete,async()=>receipt,
    async()=>({captured:8,retired:8,active:0}));
  assert.equal(complete.status,'complete');assert.deepEqual(complete.retirement,{captured:8,retired:8,active:0});
});

test('actual in-page operation enables pristine loop before configuration and reconfigures after each retirement',async()=>{
  const calls=[],heap=new Uint8Array(256);heap.set([3,0,0,0,40],64);
  globalThis.window={__slippiSynctest:{module:{HEAPU8:heap,_malloc:()=>16,_free:()=>{},
    _melee_web_synctest_cpu_corrected_loop_enable:schema=>{calls.push(['enable',schema]);return 0;},
    _melee_web_synctest_cpu_restore_configure:()=>{calls.push(['configure']);return 0;},
    _melee_web_synctest_cpu_corrected_loop_select_span:span=>{calls.push(['span',span]);return 0;},
    ccall:async(_name,_ret,_types,[command])=>{calls.push(['command',command]);return 0;},
    _melee_web_synctest_cpu_corrected_loop_evidence_bytes:()=>40,
    _melee_web_synctest_cpu_corrected_loop_evidence:()=>64}}};
  try{
    for(let i=0;i<2;i++)await operateCorrectedInPage({commandTimingSource:createCorrectedCommandTimer.toString(),
      command:53,loopEnableSchema:i?undefined:3,
      wire:[[1,2,3,4]],selectSpan:i+1,readLoopEvidence:true});
    assert.deepEqual(calls,[['enable',3],['configure'],['span',1],['command',53],
      ['configure'],['span',2],['command',53]]);
  }finally{delete globalThis.window;}
});

test('actual runner pre-sink failure retains full partial source and failed sink is never retried or retired',async()=>{
  for(const sinkFails of [false,true]){
    const progress={nativeEvidenceBase64:'raw-native-proof',commands:[{command:32,result:7}]};
    const commands=[],sinkCalls=[];
    const page={evaluate:async(fn,plan)=>{
      if(plan){commands.push(plan.command);throw Error('capture first failure');}return progress;
    }};
    const sink=async(index,report)=>{sinkCalls.push(structuredClone(report));
      assert.equal(index,0);if(sinkFails)throw Error('disk failure');return receipt;};
    let failure;
    try{await runBrowserCpuCorrectedLoop(page,page,[[1,2,3,4]],
      {pad:neutral,boundary:{sourceFrame:100}},
      [{sourceSlot:0,initializerPort:0,rawController:0},{sourceSlot:1,initializerPort:2,rawController:1}],
      [4,4],{}, {slotCount:2},sink);}catch(error){failure=error;}
    assert.match(String(failure),/capture first failure/);
    assert.deepEqual(commands,[32]);assert.equal(sinkCalls.length,1);
    assert.deepEqual(sinkCalls[0].sourceProgress,{reference:progress,predicted:progress});
    assert.equal(failure.cpuCorrectedLoopPartial.partialInterval.failure,'Error: capture first failure');
    assert.deepEqual(failure.cpuCorrectedLoopPartial.partialInterval.sourceProgress.reference,progress);
  }
});


function motorWire({baseline=[],selected=baseline,lane=1,token=0x20000000000001n,world=17n,epoch=3n}={}){
  const bytes=Buffer.alloc(1016),u32=(offset,value)=>bytes.writeUInt32LE(value,offset),
    u64=(offset,value)=>bytes.writeBigUInt64LE(value,offset);
  [1,1016,1,lane,0,0,0,0].forEach((value,index)=>u32(index*4,value));
  for(const [offset,events] of [[32,baseline],[524,selected]]){
    [1,3,100,3,events.length].forEach((value,index)=>u32(offset+index*4,value));
    u64(offset+20,token);u64(offset+28,world);u64(offset+36,epoch);
    events.forEach((event,index)=>['sourceFrame','channel','command','ordinal'].forEach(
      (key,field)=>u32(offset+44+index*16+field*4,event[key])));
  }
  return bytes;
}
const motorEvent=(sourceFrame=100,channel=0,command=1,ordinal=0)=>({sourceFrame,channel,command,ordinal});
const decodeMotor=options=>decodeCpuCorrectedLoopMotorEvidence(motorWire(options));
const requireMotor=(value,lane=1,baseline=null)=>requireCpuCorrectedLoopMotorEvidence(value,
  {preFrame:100,span:3,selectedLane:lane,baseline});
function motorReceipt(selected,lane){
  const bytes=Buffer.alloc(64);
  [1,64,1,lane,0,selected.eventCount,selected.firstFrame,selected.span].forEach(
    (value,index)=>bytes.writeUInt32LE(value,index*4));
  ['token','world','contextEpoch'].forEach((key,index)=>bytes.writeBigUInt64LE(BigInt(selected[key]),32+index*8));
  bytes.writeBigUInt64LE(BigInt(hashCpuCorrectedLoopMotorEvents(selected.events)),56);return bytes;
}

test('motor evidence preserves exact local identities and compares only ordered source events across instances',()=>{
  const events=[motorEvent(100,0,1,0),motorEvent(100,3,2,1),motorEvent(102,1,0,2)];
  const reference=requireMotor(decodeMotor({baseline:events}));
  const other=requireMotor(decodeMotor({baseline:events,token:0x30000000000003n,world:99n,epoch:19n}));
  assert.equal(reference.selected.token,'9007199254740993');
  assert.notEqual(reference.selected.token,other.selected.token);
  assert.equal(compareCpuCorrectedLoopMotorEvents(reference.selected,other.selected).equal,true);
  assert.equal(hashCpuCorrectedLoopMotorEvents([]),'14695981039346656037');
  const hash=hashCpuCorrectedLoopMotorEvents(events);
  assert.equal(hash,'2145455273860952881','independent little-endian FNV-1a64 vector');
  assert.notEqual(hash,hashCpuCorrectedLoopMotorEvents(events.slice(1)));
  assert.throws(()=>hashCpuCorrectedLoopMotorEvents([{...events[0],command:-1}]),/u32/);
  const baseline=decodeMotor({baseline:[motorEvent(100,1,2)]});
  const corrected=requireMotor(decodeMotor({baseline:[motorEvent(100,1,2)],selected:events,lane:3}),
    3,baseline.baseline);
  assert.equal(compareCpuCorrectedLoopMotorEvents(reference.selected,corrected.selected).equal,true);
  assert.throws(()=>requireMotor(corrected,3,reference.baseline),/baseline journal changed/);
  assert.throws(()=>compareCpuCorrectedLoopMotorEvents(reference.selected,baseline.selected),/authority events differ/);
});

test('empty authority permits empty corrected output while preserving differing wrong-branch events',()=>{
  const authority=requireMotor(decodeMotor({}));
  for(const wrong of [[],[motorEvent()],[motorEvent(100,2,2),motorEvent(102,3,0,1)]]){
    const baseline=decodeMotor({baseline:wrong});
    const corrected=requireMotor(decodeMotor({baseline:wrong,selected:[],lane:3}),3,baseline.baseline);
    assert.equal(corrected.baseline.eventCount,wrong.length);
    assert.equal(compareCpuCorrectedLoopMotorEvents(authority.selected,corrected.selected).eventCount,0);
    const receipt=decodeCpuCorrectedLoopMotorCommitReceipt(motorReceipt(corrected.selected,3),corrected.selected,3);
    assert.equal(receipt.eventHash,'14695981039346656037');assert.equal(receipt.published,0);
  }
});

test('motor wire decoder rejects malformed bounds, order, opcodes, duplicate channels and unused capacity',()=>{
  const mutations=[
    [bytes=>bytes.writeUInt32LE(1015,4),/schema/],
    [bytes=>bytes.writeUInt32LE(1,16),/publication/],
    [bytes=>bytes.writeUInt32LE(1,28),/schema/],
    [bytes=>bytes.writeUInt32LE(29,48),/state\/count/],
    [bytes=>bytes.writeUInt32LE(8,36),/frame extent/],
    [bytes=>bytes.writeUInt32LE(4,44),/frame extent/],
    [bytes=>bytes.writeUInt32LE(99,76),/event frame/],
    [bytes=>bytes.writeUInt32LE(4,80),/event frame/],
    [bytes=>bytes.writeUInt32LE(3,84),/event frame/],
    [bytes=>bytes.writeUInt32LE(1,88),/event frame/],
    [bytes=>{bytes[92]=1;},/unused event/],
    [bytes=>bytes.writeUInt32LE(0,32),/absent sequence/],
  ];
  for(const [mutate,pattern] of mutations){const bytes=motorWire({baseline:[motorEvent()]});mutate(bytes);
    assert.throws(()=>decodeCpuCorrectedLoopMotorEvidence(bytes),pattern);}
  assert.throws(()=>decodeMotor({baseline:[motorEvent(),motorEvent(100,0,2,1)]}),/duplicate channel/);
  assert.throws(()=>decodeMotor({baseline:[motorEvent(102),motorEvent(100,1,1,1)]}),/event frame/);
  assert.throws(()=>decodeCpuCorrectedLoopMotorEvidence(Buffer.alloc(1015)),/extent/);
  const bytes=motorWire();bytes.writeUInt32LE(2,32);bytes.writeUInt32LE(1,44);
  const partial=decodeCpuCorrectedLoopMotorEvidence(bytes);
  assert.equal(partial.baseline.state,2);assert.throws(()=>requireMotor(partial),/complete/);
  const refused=decodeMotor();refused.failure=4;assert.throws(()=>requireMotor(refused),/refused/);
  const badIdentity=decodeMotor();badIdentity.selected.token='1';assert.throws(()=>requireMotor(badIdentity),/identity/);
});

test('native capture-only commit receipt binds selected events, full local identity, lane and publication',()=>{
  const selected=decodeMotor({baseline:[motorEvent()]}).selected;
  const bytes=motorReceipt(selected,1);
  const receipt=decodeCpuCorrectedLoopMotorCommitReceipt(bytes,selected,1);
  assert.equal(receipt.token,selected.token);assert.equal(receipt.contextEpoch,selected.contextEpoch);
  for(const offset of [0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60]){
    const changed=Buffer.from(bytes);changed.writeUInt32LE(changed.readUInt32LE(offset)^1,offset);
    assert.throws(()=>decodeCpuCorrectedLoopMotorCommitReceipt(changed,selected,1));
  }
  assert.throws(()=>decodeCpuCorrectedLoopMotorCommitReceipt(bytes.subarray(0,60),selected,1),/extent/);
});

test('production sink boundary compares motor events before sink and retirement, including empty output',async()=>{
  for(const events of [[],[motorEvent()]]){
    const reference=decodeMotor({baseline:events}),predicted=decodeMotor({baseline:[motorEvent(101,2,2)],selected:events,lane:3});
    const interval={reference:{motorEvidence:reference},predicted:{motorEvidence:predicted}};
    const order=[];
    await persistCpuCorrectedLoopInterval(0,interval,{},async()=>{
      assert.equal(interval.comparisons.motor.equal,true);order.push('sink');return receipt;
    },async()=>{order.push('retire');return {published:false};});
    assert.deepEqual(order,['sink','retire']);
    predicted.selected.events=[motorEvent(102,3,2)];predicted.selected.eventCount=1;
    await assert.rejects(persistCpuCorrectedLoopInterval(0,interval,{},async()=>{
      order.push('wrong sink');return receipt;
    },async()=>order.push('wrong retire')),/authority events differ/);
    assert.deepEqual(order,['sink','retire']);
  }
});

test('actual opt-in in-page motor reads retain evidence and read one unpublished commit receipt after command53',async()=>{
  const calls=[],heap=new Uint8Array(4096),journal=motorWire(),selected=decodeCpuCorrectedLoopMotorEvidence(journal).selected;
  heap.set(journal,512);heap.set(motorReceipt(selected,1),2048);
  heap.set([3,0,0,0,40],64);
  globalThis.window={__slippiSynctest:{module:{HEAPU8:heap,
    ccall:async(_name,_ret,_types,[command])=>{calls.push(['command',command]);return 0;},
    _melee_web_synctest_cpu_corrected_loop_evidence_bytes:()=>40,
    _melee_web_synctest_cpu_corrected_loop_evidence:()=>64,
    _melee_web_synctest_cpu_corrected_loop_motor_commit_receipt_bytes:()=>64,
    _melee_web_synctest_cpu_corrected_loop_motor_commit_receipt:()=>{calls.push(['commit receipt']);return 2048;}}}};
  try{
    const operation=await operateCorrectedInPage({commandTimingSource:createCorrectedCommandTimer.toString(),
      command:53,readLoopEvidence:true,readLoopMotorEvidence:true});
    const committed=decodeCpuCorrectedLoopMotorCommitReceipt(decode(operation.motorCommitReceiptBase64),selected,1);
    assert.equal(committed.published,0);assert.equal(committed.status,1);
    assert.deepEqual(calls,[['command',53],['commit receipt']]);
    assert.equal(window.__slippiSynctest.cpuCorrectedProgress.motorCommitReceiptBase64,operation.motorCommitReceiptBase64);
  }finally{delete globalThis.window;}
});
