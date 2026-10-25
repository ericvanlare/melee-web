// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {runBrowserCpuTwoTick,decodeCpuTwoTickEvidence,decodeCpuPayloadFailureWitness,decodeCpuClassPieceWitness,decodeCpuClassPieceCaptureWitness,decodeCpuLObjWriterEvidence,validateCpuTwoTickFinalEvidence,
  CPU_TWO_TICK_EVIDENCE_BYTES,TWO_TICK_PAD_300,TWO_TICK_PAD_301} from './cpu_two_tick_runner.mjs';
import {validateCpuTwoTickDrawSample,retainCpuRestoreSample,recordCpuRestoreFailure} from './run_write_census.mjs';
import {inspectInteractiveObservation,validateSelectedDrawPath,validateSourceInputMapping,decodeDrawDiagnostics} from './runner_contract.mjs';

const pairSize=11416,totalBytes=35368,pairOffsets=[64,11480,22896],diagOffset=34312;
const b64=bytes=>Buffer.from(bytes).toString('base64');
function observation(frame) {
  const bytes=new Uint8Array(1280),view=new DataView(bytes.buffer);
  view.setUint32(0,frame,true);view.setUint32(8,1,true);
  for(let player=0;player<2;player++){
    const base=72+192*player;view.setInt32(base+20,0,true);view.setUint32(base+136,player,true);
    view.setInt32(base+148,4,true);view.setInt32(base+152,0,true);
  }
  return bytes;
}
function diag(drawCount) {
  const bytes=new Uint8Array(132),view=new DataView(bytes.buffer),slots=6;
  view.setUint32(0,slots,true);view.setUint32(4,0,true);
  for(const offset of [8,32,56])for(let slot=0;slot<slots;slot++)view.setUint32(offset+slot*4,drawCount,true);
  for(let slot=0;slot<slots;slot++){view.setInt32(80+slot*4,slot<2?slot:-1,true);view.setInt32(104+slot*4,slot<2?0:-1,true);}
  view.setUint32(128,3,true);return bytes;
}
function writePair(target,offset,{pad,pre,post,strict}) {
  const row=target.subarray(offset,offset+pairSize),view=new DataView(row.buffer,row.byteOffset,row.byteLength);
  view.setUint32(0,1,true);view.setUint32(4,pairSize,true);view.setUint32(8,2,true);
  view.setUint32(12,0,true);view.setUint32(16,pre,true);view.setUint32(20,post,true);
  view.setUint32(24,0,true);view.setUint32(28,1,true);
  const fill=(start,size,value)=>row.fill(value,start,start+size);
  const obs=observation(post);row.set(obs,32);row.set(obs,1312);
  fill(2592,4272,5);fill(6864,4272,5);
  const padBytes=Buffer.from(pad,'base64');row.set(padBytes,11136);row.set(padBytes,11180);
  fill(11224,32,7);fill(11256,32,7);fill(11288,40,9);fill(11328,40,9);
  view.setUint32(11368,strict?0x3fe:0,true);view.setUint32(11372,strict?0x3fe:0,true);
  view.setUint32(11380,0,true);
}
function makeEvidence({state=5,mask=255,lastCommand=13}={}) {
  const bytes=new Uint8Array(totalBytes),view=new DataView(bytes.buffer);
  for(const [offset,value]of [[0,1],[4,totalBytes],[8,2],[12,state],[16,0],[20,300],[24,302],
    [28,0],[32,1],[36,1],[40,0x3fe],[44,0x3fe],[48,0x3fe],[52,0x3fe],[56,mask],[60,lastCommand]])view.setUint32(offset,value,true);
  writePair(bytes,pairOffsets[0],{pad:TWO_TICK_PAD_300,pre:300,post:301,strict:false});
  writePair(bytes,pairOffsets[1],{pad:TWO_TICK_PAD_301,pre:301,post:302,strict:true});
  writePair(bytes,pairOffsets[2],{pad:TWO_TICK_PAD_301,pre:302,post:302,strict:true});
  const slots=[diag(0),diag(0),diag(1),diag(1),diag(2),diag(2),diag(2),diag(3)];
  slots.forEach((row,index)=>bytes.set(row,diagOffset+index*132));
  return bytes;
}

function makeWitness(failed=false) {
  const bytes=new Uint8Array(256),v=new DataView(bytes.buffer),u=(o,n)=>v.setUint32(o,n,true);
  u(0,1);u(4,256);if(!failed)return bytes;
  for(const[o,n]of [[8,2],[12,1],[16,1],[20,168],[24,2],[32,7],[36,5],[40,0x80010000],[44,64],[48,0],[52,32],
      [184,1],[188,1],[196,1],[232,32],[236,2],[240,3],[244,64],[248,0],[252,5]])u(o,n);
  for(const[o,n]of [[64,0x10000n],[72,0xfffffffffffffff1n],[80,9n],[88,1n],[96,12n],
      [200,0x12340n],[208,0xfffffffffffffff1n],[216,9n],[224,1n]])v.setBigUint64(o,n,true);
  bytes.fill(9,120,152);bytes.fill(9,152,184);bytes[157]=8;return bytes;
}
function makeRunner({fault=null}={}) {
  const memory=new Uint8Array(100000),heap=new Uint8Array(memory.buffer);
  const ptr={evidence:1000,observation:40000,input:42000,pcm:43000,draw:50000};
  const checkpointPad=b64(new Uint8Array(44).fill(1));
  memory.set(observation(300),ptr.observation);memory.set(Buffer.from(checkpointPad,'base64'),ptr.input);memory.set(diag(0),ptr.draw);
  let witness=makeWitness();
  const commandSequence=[];let drawCount=0,frameId=10,current=makeEvidence({state:9,mask:1,lastCommand:8}),lastCommand=8;
  const synctest={};
  const writeLivePair=(decoded,pairName)=>{
    const pair=decoded[pairName];
    memory.set(Buffer.from(pair.baselineObservation,'base64'),ptr.observation);
    memory.set(Buffer.from(pair.baselinePad,'base64'),ptr.input);
    memory.set(Buffer.from(pair.baselinePcm,'base64'),ptr.pcm);
  };
  const setLive=(evidence,drawIndex,cmd)=>{
    current=evidence;const decoded=decodeCpuTwoTickEvidence(evidence);const d=decoded.diagnostics[drawIndex];
    if(d)memory.set(Buffer.from(d.base64,'base64'),ptr.draw);
    if(cmd===9)writeLivePair(decoded,'pair300');
    if(cmd===10)writeLivePair(decoded,'pair301');
    if(drawIndex>=7){const p=decoded.drawnPair301;
      memory.set(Buffer.from(p.replayObservation,'base64'),ptr.observation);
      memory.set(Buffer.from(p.replayPcm,'base64'),ptr.pcm);memory.set(Buffer.from(p.replayPad,'base64'),ptr.input);}
  };
  const origin=new Uint8Array(320);new DataView(origin.buffer).setUint32(0,1,true);new DataView(origin.buffer).setUint32(4,320,true);
  const module={HEAPU8:heap,
    _melee_web_synctest_cpu_lobj_writer_evidence_bytes:()=>fault==='bad-writer-size'?16783:16784,
    _melee_web_synctest_cpu_lobj_writer_evidence:()=>{const p=70000;heap.fill(0,p,p+16784);const v=new DataView(heap.buffer);v.setUint32(p,1,true);v.setUint32(p+4,16784,true);if(fault==='malformed-writer-after-11'&&commandSequence.includes(11))v.setUint32(p+140,1,true);return p;},
    _melee_web_synctest_cpu_class_piece_capture_witness_bytes:()=>fault==='bad-capture-piece-size'?159:160,
    _melee_web_synctest_cpu_class_piece_capture_witness:()=>{const p=66500;heap.fill(0,p,p+160);const v=new DataView(heap.buffer);v.setUint32(p,1,true);v.setUint32(p+4,160,true);v.setUint32(p+16,1,true);return p;},
    _melee_web_synctest_cpu_class_piece_witness_bytes:()=>fault==='bad-class-size'?207:208,
    _melee_web_synctest_cpu_class_piece_witness:()=>{const p=66000;heap.fill(0,p,p+208);const v=new DataView(heap.buffer);v.setUint32(p,1,true);v.setUint32(p+4,208,true);return p;},
    _melee_web_synctest_cpu_allocation_origin_witness_bytes:()=>fault==='bad-origin-size'?319:320,
    _melee_web_synctest_cpu_allocation_origin_witness:()=>{heap.set(origin,65400);return 65400;},
    _melee_web_synctest_cpu_payload_failure_witness_bytes:()=>fault==='bad-witness-size'?255:256,
    _melee_web_synctest_cpu_payload_failure_witness:()=>{heap.set(witness,65000);return 65000;},
    _melee_web_synctest_cpu_two_tick_evidence_bytes:()=>totalBytes,
    _melee_web_synctest_cpu_two_tick_evidence:()=>{
      if(fault==='throw-and-refresh-error'&&lastCommand===9)throw new Error('injected evidence refresh failure');
      heap.set(current,ptr.evidence);return ptr.evidence;},
    _melee_web_snapshot_observation_size:()=>1280,_melee_web_snapshot_pcm_size:()=>4272,
    _melee_web_synctest_draw_diagnostics_size:()=>132,
    _melee_web_snapshot_observation:()=>ptr.observation,_melee_web_snapshot_input:()=>ptr.input,
    _melee_web_snapshot_pcm:()=>ptr.pcm,_melee_web_synctest_draw_diagnostics:()=>ptr.draw,
    _melee_web_synctest_cpu_restore_configure:()=>0,_melee_web_snapshot_error:()=>1,
    _melee_web_synctest_submission_json:()=>{
      if(lastCommand===9)memory.set(diag(1),ptr.draw);
      if(lastCommand===10)memory.set(diag(2),ptr.draw);
      if(lastCommand===12)memory.set(diag(3),ptr.draw);
      return 1;
    },
    _melee_web_synctest_motion_boundary:()=>0,_melee_web_synctest_is_mario_kind:()=>1,
    _malloc:()=>60000,_free:()=>{},
    UTF8ToString:pointer=>pointer===1?JSON.stringify({rendererErrorCount:0,pendingFramePackets:0,pendingStagingBuffers:0,workerBusy:0,drawCallCount:drawCount,lastBeginFrameId:frameId,lastEndFrameId:frameId}):'',
    ccall:async(name,_result,_types,args)=>{
      if(name==='melee_web_synctest_cpu_restore_command'){
        const cmd=args[0];commandSequence.push(cmd);lastCommand=cmd;
        if((fault==='throw-command-9'||fault==='throw-and-refresh-error')&&cmd===9)throw new Error('injected ccall throw');
        const states={8:[9,1,0,0],9:[10,3,1,1],10:[11,15,3,2],11:[12,127,4,2],12:[13,127,7,3],13:[5,255,7,3]};
        const [state,mask,drawIndex,draws]=states[cmd];drawCount=draws;frameId=10+draws;
        const raw=makeEvidence({state,mask,lastCommand:cmd});
        if(fault==='bad-sample300-pad'&&cmd===9)raw[64+11136]^=1;
        if(fault==='bad-sample301-pad'&&cmd===10)raw[11480+11136]^=1;
        if(fault==='late-validation'&&cmd===13)new DataView(raw.buffer).setUint32(40,0,true);
        if((fault==='payload-refused-11'||fault==='payload-throw-11')&&cmd===11){
          const v=new DataView(raw.buffer);v.setUint32(12,6,true);v.setUint32(16,1216,true);
          v.setUint32(40,30,true);v.setUint32(44,14,true);v.setUint32(11480+11376,4,true);v.setUint32(11480+11380,2,true);
          witness=makeWitness(true);
        }
        setLive(raw,drawIndex,cmd);
        if(fault==='payload-throw-11'&&cmd===11)throw new Error('after-witness injected throw');
        if(fault==='payload-refused-11'&&cmd===11)return 1216;
        return fault==='nonzero-command-9'&&cmd===9?7:0;
      }
      if(name==='melee_web_snapshot_quiescent')return 1;
      throw new Error(`unexpected ccall ${name}`);
    }};
  synctest.module=module;
  const page={evaluate:async(fn,plan)=>{
    globalThis.window={__slippiSynctest:synctest};
    try{return await fn(plan);}finally{delete globalThis.window;}
  }};
  const helpers={checkedSource:'async function checked(module){const value=await module.ccall("melee_web_snapshot_quiescent","number",[],[],{async:true});if(value!==1)throw Error("not quiescent");return value}',
    inputMappingSource:validateSourceInputMapping.toString(),inspectSource:inspectInteractiveObservation.toString(),
    selectedDrawSource:validateSelectedDrawPath.toString(),decodeDrawSource:decodeDrawDiagnostics.toString()};
  return {page,commandSequence,run:()=>runBrowserCpuTwoTick(page,[[100,4,1,1]],
    {boundary:{sourceFrame:300},pad:checkpointPad},
    [{sourceSlot:0,initializerPort:0,rawController:0},{sourceSlot:1,initializerPort:2,rawController:1}],[4,4],helpers)};
}

test('two-tick decoder enforces exact evidence size, pair identity, frame progression, and full final domains',()=>{
  const evidence=decodeCpuTwoTickEvidence(makeEvidence());
  assert.equal(CPU_TWO_TICK_EVIDENCE_BYTES,totalBytes);
  assert.equal(evidence.pair300.rawFiveMatch,true);assert.equal(evidence.pair301.rawFiveMatch,true);
  assert.equal(evidence.drawnPair301.rawFiveMatch,true);assert.equal(validateCpuTwoTickFinalEvidence(evidence,300),true);
  const short=makeEvidence().subarray(0,totalBytes-1);assert.throws(()=>decodeCpuTwoTickEvidence(short),/extent/);
  const badMask=decodeCpuTwoTickEvidence(makeEvidence({mask:127}));
  assert.throws(()=>validateCpuTwoTickFinalEvidence(badMask,300),/header contract/);
  const badIntermediate=makeEvidence();new DataView(badIntermediate.buffer).setUint32(64+11368,0x3fe,true);
  assert.throws(()=>validateCpuTwoTickFinalEvidence(decodeCpuTwoTickEvidence(badIntermediate),300),/sample 300/);
  const badPad=makeEvidence();badPad[64+11136]=9;badPad[64+11180]=9;
  assert.throws(()=>validateCpuTwoTickFinalEvidence(decodeCpuTwoTickEvidence(badPad),300),/literal PAD/);
  const badDrawnPhase=makeEvidence();new DataView(badDrawnPhase.buffer).setUint32(pairOffsets[2]+20,303,true);
  assert.throws(()=>validateCpuTwoTickFinalEvidence(decodeCpuTwoTickEvidence(badDrawnPhase),300),/drawn endpoint/);
  const changedCorrection=makeEvidence();changedCorrection[diagOffset+5*132+12]++;
  assert.throws(()=>validateCpuTwoTickFinalEvidence(decodeCpuTwoTickEvidence(changedCorrection),300),/correction tick/);
});

test('actual two-tick runner emits frozen commands, checks both baselines before advancing, and waits after draws',async()=>{
  const fixture=makeRunner();const result=await fixture.run();
  assert.deepEqual(fixture.commandSequence,[8,9,10,11,12,13]);
  assert.deepEqual(result.cpuRestore.gpuWaits.map(row=>row.sample),[300,301,'final-present']);
  assert.equal(result.cpuRestore.drawBoundaries.length,8);
  assert.equal(result.cpuRestore.mode,'drawn-reference-no-draw-two-tick-v1');
  assert.equal(result.cpuRestore.rawSamples.sample300.pad,TWO_TICK_PAD_300);
  assert.equal(result.cpuRestore.rawSamples.sample301.pad,TWO_TICK_PAD_301);
  assert.equal(result.cpuRestore.baselineSamples.sample300.pad,TWO_TICK_PAD_300);
  assert.equal(result.cpuRestore.baselineSamples.sample301.pad,TWO_TICK_PAD_301);
  assert.equal(result.cpuRestore.nativeEvidence.lastCommand,13);
  // Cross the actual runner/outer-validator/report-retainer boundary with this
  // returned sample. Separate handwritten outer fixtures missed the V2 bug.
  const mapping=[{sourceSlot:0,initializerPort:0,rawController:0},{sourceSlot:1,initializerPort:2,rawController:1}];
  const outer=validateCpuTwoTickDrawSample(result,decodeDrawDiagnostics(diag(0)),
    {beginFrameId:10,endFrameId:10},mapping);
  assert.deepEqual(outer.baselineFrames.map(row=>row.beginFrameId),[11,12]);
  assert.equal(outer.frame.beginFrameId,13);
  const report={execution:{}};retainCpuRestoreSample(report,result);
  assert.equal(report.execution.restorePerformed,true);
  assert.equal(report.cpuRawSample,result);
  assert.equal(result.cpuRestore.baselineSamples.sample300.drawDiagnostics.bytes,132);
  assert.equal(result.cpuRestore.baselineSamples.sample301.drawDiagnostics.bytes,132);
  const missing={...result,cpuRestore:{...result.cpuRestore,drawBoundaries:result.cpuRestore.drawBoundaries.slice(0,7)}};
  assert.throws(()=>validateCpuTwoTickDrawSample(missing,decodeDrawDiagnostics(diag(0)),
    {beginFrameId:10,endFrameId:10},mapping),/draw boundary evidence/);
  const stale={...result,cpuRestore:{...result.cpuRestore,baselineGpuStatus301:result.cpuRestore.baselineGpuStatus300}};
  assert.throws(()=>validateCpuTwoTickDrawSample(stale,decodeDrawDiagnostics(diag(0)),
    {beginFrameId:10,endFrameId:10},mapping),/frame/i);
});

test('bad sample 300 baseline stops before command 10',async()=>{
  const fixture=makeRunner({fault:'bad-sample300-pad'});
  await assert.rejects(fixture.run(),/sample 300 live baseline PAD\/observation drift/);
  assert.deepEqual(fixture.commandSequence,[8,9]);
  assert.equal(globalThis.window,undefined);
});

test('bad sample 301 baseline stops before command 11 restore',async()=>{
  const fixture=makeRunner({fault:'bad-sample301-pad'});
  await assert.rejects(fixture.run(),/sample 301 live baseline PAD\/observation drift/);
  assert.deepEqual(fixture.commandSequence,[8,9,10]);
});

test('nonzero ccall result keeps its returned evidence and does not advance',async()=>{
  const fixture=makeRunner({fault:'nonzero-command-9'});
  let caught;try{await fixture.run();}catch(error){caught=error;}
  assert.match(String(caught),/command 9 refused 7/);
  assert.deepEqual(fixture.commandSequence,[8,9]);
  assert.equal(caught.cpuRestorePartial.commands[1].outcome,'returned-nonzero');
  assert.equal(caught.cpuRestorePartial.commands[1].nativeEvidence.lastCommand,9);
  const report={execution:{}};recordCpuRestoreFailure(report,caught);
  assert.equal(report.execution.cpuRestore,caught.cpuRestorePartial);
  assert.equal(report.execution.cpuRestore.commands[1].result,7);
  assert.equal(report.execution.cpuOuterValidation,'failed');
});

test('thrown ccall preserves its failure and refreshes the last readable native evidence',async()=>{
  const fixture=makeRunner({fault:'throw-command-9'});
  let caught;try{await fixture.run();}catch(error){caught=error;}
  assert.match(String(caught),/injected ccall throw/);
  assert.deepEqual(fixture.commandSequence,[8,9]);
  assert.equal(caught.cpuRestorePartial.commands[1].outcome,'threw');
  assert.equal(caught.cpuRestorePartial.commands[1].refreshedNativeEvidence.lastCommand,8);
  assert.equal(caught.cpuRestorePartial.commands[1].error,'Error: injected ccall throw');
});

test('late final validation failure retains final native evidence and issued command history',async()=>{
  const fixture=makeRunner({fault:'late-validation'});
  let caught;try{await fixture.run();}catch(error){caught=error;}
  assert.match(String(caught),/header contract/);
  assert.deepEqual(fixture.commandSequence,[8,9,10,11,12,13]);
  assert.equal(caught.cpuRestorePartial.commands.at(-1).nativeEvidence.lastCommand,13);
  assert.equal(caught.cpuRestorePartial.nativeEvidence.lastCommand,13);
  const report={execution:{}};recordCpuRestoreFailure(report,caught);
  assert.equal(report.execution.cpuRestore.nativeEvidence.lastCommand,13);
  assert.equal(report.execution.cpuOuterValidation,'failed');
});


test('evidence refresh failure is recorded separately without replacing the thrown ccall error',async()=>{
  const fixture=makeRunner({fault:'throw-and-refresh-error'});
  let caught;try{await fixture.run();}catch(error){caught=error;}
  assert.match(String(caught),/injected ccall throw/);
  assert.deepEqual(fixture.commandSequence,[8,9]);
  const command=caught.cpuRestorePartial.commands[1];
  assert.equal(command.outcome,'threw');
  assert.equal(command.error,'Error: injected ccall throw');
  assert.equal(command.evidenceRefreshError,'Error: injected evidence refresh failure');
});

test('payload witness decoder preserves exact64-bit identity and refuses malformed bounds/lifetimes',()=>{
  const raw=makeWitness(true),d=decodeCpuPayloadFailureWitness(raw);
  assert.equal(d.window.generation,'fffffffffffffff1');assert.equal(d.allocationGeneration,d.window.generation);
  assert.equal(d.window.row,7);assert.equal(d.window.offset,5);assert.equal(d.attribution,1);
  for(const[offset,value]of [[4,255],[52,33],[232,0],[240,2],[244,65],[248,2]]){
    const bad=raw.slice();new DataView(bad.buffer).setUint32(offset,value,true);
    assert.throws(()=>decodeCpuPayloadFailureWitness(bad),/witness/);
  }
  const bad=raw.slice();bad[208]^=1;assert.throws(()=>decodeCpuPayloadFailureWitness(bad),/lifetime/);
});
test('actual runner retains fixed witness and original failure before outer validation or ccall exception',async()=>{
  for(const fault of ['payload-refused-11','payload-throw-11']){
    const f=makeRunner({fault});let caught;try{await f.run();}catch(e){caught=e;}
    assert.ok(caught);assert.deepEqual(f.commandSequence,[8,9,10,11]);
    const p=caught.cpuRestorePartial;
    assert.equal(p.nativeEvidence.firstFailure,1216);assert.equal(p.nativeEvidence.checkedCpu,30);
    assert.equal(p.payloadFailureWitness.window.row,7);assert.equal(p.payloadFailureWitness.window.generation,'fffffffffffffff1');
    assert.equal(Buffer.from(p.payloadFailureWitnessRaw,'base64').length,256);
    assert.equal(Buffer.from(p.lobjWriterEvidenceRaw,'base64').length,16784);
    assert.equal(p.lobjWriterEvidence.livePieceLifetime,'UNKNOWN');
    assert.equal(Buffer.from(p.classPieceWitnessRaw,'base64').length,208);
    assert.equal(Buffer.from(p.classPieceCaptureWitnessRaw,'base64').length,160);
    assert.equal(p.classPieceCaptureWitness.scope,'CAPTURE_TIME_ONLY');
    assert.equal(p.commands.at(-1).classPieceCaptureWitness.status,0);
    assert.equal(p.classPieceWitness.status,0);assert.equal(p.commands.at(-1).classPieceWitness.status,0);
    if(fault==='payload-refused-11')assert.equal(p.commands.at(-1).payloadFailureWitness.window.offset,5);
    else assert.equal(p.commands.at(-1).refreshedNativeEvidence.firstFailure,1216);
  }
});
test('new observer failure cannot erase already-retained original native evidence',async()=>{
  const f=makeRunner({fault:'bad-witness-size'});let caught;try{await f.run();}catch(e){caught=e;}
  assert.match(String(caught),/witness ABI/);assert.equal(caught.cpuRestorePartial.nativeEvidence.lastCommand,8);
  assert.deepEqual(f.commandSequence,[8]);
});

// Production decoder: fixed origin evidence is separate from legacy ABIs.
test('allocation origin strict decoder and unknown/refused identities',async()=>{
 const {decodeCpuAllocationOriginWitness}=await import('./cpu_two_tick_runner.mjs');
 const b=new Uint8Array(320),v=new DataView(b.buffer);v.setUint32(0,1,true);v.setUint32(4,320,true);
 assert.equal(decodeCpuAllocationOriginWitness(b).status,0);
 v.setUint32(8,1,true);assert.throws(()=>decodeCpuAllocationOriginWitness(b));
 v.setUint32(16,1,true);v.setUint32(20,1,true);v.setBigUint64(32,32n,true);v.setBigUint64(40,1n,true);v.setBigUint64(48,1n,true);v.setBigUint64(56,1n,true);v.setUint32(68,64,true);
 v.setUint32(72,1,true);v.setUint32(76,22,true);v.setUint32(80,7,true);b.set(Buffer.from('class.c'),88);
 assert.equal(decodeCpuAllocationOriginWitness(b).path,'class.c');
 v.setUint32(80,232,true);assert.throws(()=>decodeCpuAllocationOriginWitness(b));
});

test('class piece208 diagnostic rejects malformed geometry and keeps UNKNOWN/refusal explicit',()=>{
 const b=new Uint8Array(208),v=new DataView(b.buffer),set=(o,n)=>v.setUint32(o,n,true);
 set(0,1);set(4,208);assert.equal(decodeCpuClassPieceWitness(b).status,0);
 set(12,4);assert.equal(decodeCpuClassPieceWitness(b).reason,4);
 set(8,1);assert.throws(()=>decodeCpuClassPieceWitness(b),/geometry/);
 set(8,2);set(12,7);assert.equal(decodeCpuClassPieceWitness(b).status,2);
 set(192,1);assert.throws(()=>decodeCpuClassPieceWitness(b),/schema/);set(192,0);
 set(4,207);assert.throws(()=>decodeCpuClassPieceWitness(b),/schema/);
 assert.throws(()=>decodeCpuClassPieceWitness(b.slice(0,207)),/extent/);
});

test('class observer ABI refusal preserves original native and earlier witness evidence',async()=>{
 const f=makeRunner({fault:'bad-class-size'});let error;try{await f.run();}catch(e){error=e;}
 assert.match(String(error),/Class piece ABI/);assert.equal(error.cpuRestorePartial.nativeEvidence.lastCommand,8);
 assert.equal(Buffer.from(error.cpuRestorePartial.payloadFailureWitnessRaw,'base64').length,256);
 assert.equal(Buffer.from(error.cpuRestorePartial.allocationOriginWitnessRaw,'base64').length,320);
 assert.deepEqual(f.commandSequence,[8]);
});

test('capture-time160 strict decoder cannot claim live continuity or accept malformed scope',()=>{
 const b=new Uint8Array(160),v=new DataView(b.buffer),set=(o,n)=>v.setUint32(o,n,true),wide=(o,n)=>v.setBigUint64(o,BigInt(n),true);
 set(0,1);set(4,160);set(16,1);assert.equal(decodeCpuClassPieceCaptureWitness(b).scope,'CAPTURE_TIME_ONLY');
 set(8,1);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/geometry/);
 set(20,1);set(24,1);set(28,7);set(32,4);set(36,3);set(48,8);set(52,0x80000000);set(56,1024);
 for(const o of [64,72,80,104,112])wide(o,1);wide(88,10);wide(96,11);
 set(120,0x80000000);set(124,8);set(128,64);set(132,48);set(136,1);set(140,4096);set(144,4096);
 const d=decodeCpuClassPieceCaptureWitness(b);assert.equal(d.status,1);assert.equal(d.liveReason,7);assert.equal(d.liveClassInfo,undefined);
 set(32,2);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/frozen/);set(32,4);
 wide(96,12);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/frozen/);wide(96,11);
 set(140,0);set(144,0);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/saved header/);set(140,4096);set(144,4096);
 set(144,4097);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/header/);set(144,4096);
 set(148,1);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/bounds/);set(148,0);
 set(152,1);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/schema/);set(152,0);
 set(16,0);assert.throws(()=>decodeCpuClassPieceCaptureWitness(b),/schema/);
 assert.throws(()=>decodeCpuClassPieceCaptureWitness(b.slice(0,159)),/extent/);
});
test('capture-time observer failure preserves native and all earlier diagnostic evidence',async()=>{
 const f=makeRunner({fault:'bad-capture-piece-size'});let error;try{await f.run();}catch(e){error=e;}
 assert.match(String(error),/Capture-time piece ABI/);const p=error.cpuRestorePartial;
 assert.equal(p.nativeEvidence.lastCommand,8);assert.equal(Buffer.from(p.classPieceWitnessRaw,'base64').length,208);
 assert.equal(Buffer.from(p.allocationOriginWitnessRaw,'base64').length,320);assert.deepEqual(f.commandSequence,[8]);
});

// New source-write evidence stays diagnostic; no owner/restore acceptance.
test('lobj writer unfinalized/refused schema and bounded decoder',()=>{
 const bytes=new Uint8Array(16784),v=new DataView(bytes.buffer);v.setUint32(0,1,true);v.setUint32(4,16784,true);
 assert.equal(decodeCpuLObjWriterEvidence(bytes).livePieceLifetime,'UNKNOWN');
 for(const [offset,value]of [[4,16783],[20,129],[28,2],[140,1],[392,1]]){
  const bad=bytes.slice();new DataView(bad.buffer).setUint32(offset,value,true);assert.throws(()=>decodeCpuLObjWriterEvidence(bad));
 }
 const bad=bytes.slice();bad[16783]=1;assert.throws(()=>decodeCpuLObjWriterEvidence(bad),/unused/);
});

// Explicit JS shape model, not actual native output or independent POST12 proof.
function modeledQualifiedWriter(){
 const bytes=new Uint8Array(16784),v=new DataView(bytes.buffer);
 const u=(o,x)=>v.setUint32(o,x,true),n=(o,x)=>v.setBigUint64(o,BigInt(x),true);
 for(const[o,x]of [[0,1],[4,16784],[8,1],[16,1],[20,2],[24,2],[80,1],[84,2],[88,1],[92,77],[100,1],[104,0],[108,1],[112,1],[116,7],[120,93],[124,0x80010000],[128,1024],[132,0x22],[136,0x11]])u(o,x);
 for(const o of [32,40,48,56,64,72])n(o,1);n(144,0x10000);n(152,7);
 const path=new TextEncoder().encode('src/sysdolphin/baselib/lobj.c');u(96,path.length);bytes.set(path,160);
 for(let i=0;i<2;++i){const b=400+i*128;
  for(const[o,x]of [[0,i+1],[4,9+i],[8,1],[16,7],[20,0x80010000],[24,1024],[28,92],[64,0x80010020],[68,60],[72,224],[76,212],[80,18],[84,0x2000],[88,0x2000],[92,3]])u(b+o,x);
  for(const[o,x]of [[32,0x10000],[40,7],[48,0x10020],[56,0x1005c]])n(b+o,x);
  bytes.fill(i?0x22:0x11,b+96,b+108);bytes.fill(i?0x33:0x22,b+108,b+120);
 }
 return bytes;
}
test('qualified writer model validates PRE row, phase and internal candidate set only',()=>{
 const raw=modeledQualifiedWriter(),e=decodeCpuLObjWriterEvidence(raw);
 assert.equal(e.status,1);assert.equal(e.livePieceLifetime,'UNKNOWN');assert.equal(e.matchedIndex,0);assert.equal(e.postCount,1);assert.equal(e.drawnCount,2);
 const mutations=[[400+16,8],[104,1],[108,2],[132,0x33],[16,0],[28,1],[400+84,0],[400+88,0x2004],[400+80,0],[400+68,59],[400+72,223],[400+92,0],[400+120,1],[140,1],[84,0],[400+8,0],[400+4,0],[132,0x11]];
 for(const[o,x]of mutations){const bad=raw.slice();new DataView(bad.buffer).setUint32(o,x,true);assert.throws(()=>decodeCpuLObjWriterEvidence(bad),`qualified mutation ${o}=${x}`);}
 // Two identical qualifying pre-POST events require exact count and last index.
 const repeated=raw.slice(),v=new DataView(repeated.buffer);repeated.copyWithin(528,400,528);v.setUint32(528,2,true);v.setUint32(80,2,true);v.setUint32(104,1,true);v.setUint32(108,2,true);
 assert.equal(decodeCpuLObjWriterEvidence(repeated).matchedCount,2);
 for(const[o,x]of [[104,0],[108,1],[528+16,8]]){const bad=repeated.slice();new DataView(bad.buffer).setUint32(o,x,true);assert.throws(()=>decodeCpuLObjWriterEvidence(bad),/candidate-set|first-difference/);}
});

test('actual runner retains malformed16784 raw and native partial before diagnostic decode throws',async()=>{
 const f=makeRunner({fault:'malformed-writer-after-11'});let caught;try{await f.run();}catch(e){caught=e;}
 assert.ok(caught);assert.deepEqual(f.commandSequence,[8,9,10,11]);
 const p=caught.cpuRestorePartial,raw=Buffer.from(p.lobjWriterEvidenceRaw,'base64');
 assert.equal(raw.length,16784);assert.equal(raw.readUInt32LE(140),1);
 assert.ok(p.nativeEvidence);assert.equal(Buffer.from(p.payloadFailureWitnessRaw,'base64').length,256);
});
