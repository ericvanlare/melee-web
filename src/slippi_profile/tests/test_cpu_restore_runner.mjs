import assert from 'node:assert/strict';
import {test} from 'node:test';
import {validateCpuRestoreConfiguration,runBrowserCpuRestore,CPU_EVIDENCE_BYTES} from './cpu_restore_runner.mjs';
import {validateCpuRestoreDrawSample,retainCpuRestoreSample,recordCpuRestoreFailure,recordFailedCensusExecution} from './run_write_census.mjs';
import {decodeDrawDiagnostics,assertDrawDiagnosticsClear,checkedAsyncCcall} from './runner_contract.mjs';
const hashes={wasmSha256:'a'.repeat(64),mapSha256:'b'.repeat(64),manifestSha256:'c'.repeat(64),policySha256:'d'.repeat(64)};
const row={producer_target:'fighter_source_runtime',source:'src/melee/test.c',input_object:'libfighter_source_runtime.a(test.c.o)',
  input_section:'.bss.actual_state',decision:'source-c-static-candidate',exclusion_reason:null,address:4096,size:4,
  aggregate_policy:'ORIGINAL_SOURCE_CPU_VALUE_BUNDLE_RESTORE'};
const fixture=()=>({policy:{schema:'aggregate-source-static-review-contract-v2',rows:[{...row,address:2048}]},
  manifest:{mutable_input_sections:[{...row}]},config:{schema:'aggregate-cpu-final-linked-configuration-v1',...hashes,rows:[{...row}]}});
test('exact reviewed source identity uses final VMAs, rejects every identity/policy/scope drift',()=>{
  let f=fixture();assert.deepEqual(validateCpuRestoreConfiguration(f.config,f.policy,f.manifest,hashes),[[4096,4,1,1]]);
  for(const mutate of [f=>f.config.wasmSha256='e'.repeat(64),f=>f.config.mapSha256='e'.repeat(64),f=>f.config.manifestSha256='e'.repeat(64),
    f=>f.config.policySha256='e'.repeat(64),f=>f.config.rows[0].address=2048,f=>f.config.rows[0].size=8,f=>f.policy.rows[0].size=8,
    f=>f.config.rows[0].source='src/host.cpp',f=>f.config.rows[0].aggregate_policy='guard-by-exclusion',
    f=>f.policy.rows[0].aggregate_policy='guess',f=>f.config.rows.push({...row}),
    f=>f.manifest.mutable_input_sections.push({...row,input_section:'.bss.unreviewed'})]){
    f=fixture();mutate(f);assert.throws(()=>validateCpuRestoreConfiguration(f.config,f.policy,f.manifest,hashes));
  }
  f=fixture();f.manifest.mutable_input_sections.push({...row,input_section:'.bss.host',source:null,producer_target:null,decision:'excluded'});
  assert.equal(validateCpuRestoreConfiguration(f.config,f.policy,f.manifest,hashes).length,1);
});
function environment({failCommand=0,configurationFailure=false,shared=false,cpuDraw=false,captureDraw=false,drawn=false,drawnMismatch=false,drawnRawMismatch=false}={}){
  const heap=new Uint8Array(shared?new SharedArrayBuffer(65536):new ArrayBuffer(65536));let queries=0,nativeReads=0,drawCalls=0;
  const obs=128,pad=1536,diag=1600,evidence=32768,drawnPointer=45056;const view=new DataView(heap.buffer);view.setUint32(obs,176,true);heap.set(drawBytes(),diag);
  const order=[],commands=[];let freed=0;
  const rawPad=Buffer.from('AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=','base64');
  const writeEvidence=()=>{
    view.setUint32(evidence,1,true);view.setUint32(evidence+4,CPU_EVIDENCE_BYTES,true);view.setUint32(evidence+8,5,true);
    view.setUint32(evidence+16,176,true);view.setUint32(evidence+20,177,true);view.setUint32(evidence+24,0,true);view.setUint32(evidence+28,1,true);
    view.setUint32(evidence+11368,0x3fe,true);view.setUint32(evidence+11372,0x3fe,true);
    view.setUint32(evidence+32,177,true);view.setUint32(evidence+1312,177,true);
    heap.set(rawPad,evidence+11136);heap.set(rawPad,evidence+11180);
  };
  writeEvidence();
  const updateDrawn=(stage,mask)=>{
    view.setUint32(drawnPointer,1,true);view.setUint32(drawnPointer+4,11832,true);
    view.setUint32(drawnPointer+8,stage?1:0,true);view.setUint32(drawnPointer+12,stage,true);
    heap.set(heap.slice(evidence,evidence+CPU_EVIDENCE_BYTES),drawnPointer+16);
    view.setUint32(drawnPointer+16+28,stage===4&&!drawnMismatch?1:0,true);
    view.setUint32(drawnPointer+11828,mask,true);
  };
  updateDrawn(0,0);const module={HEAPU8:heap,UTF8ToString:value=>value,
    _malloc:()=>2048,_free:()=>{freed++;},
    _melee_web_snapshot_observation_size:()=>{nativeReads++;return 1280;},_melee_web_snapshot_pcm_size:()=>4272,
    _melee_web_synctest_draw_diagnostics_size:()=>132,
    _melee_web_snapshot_observation:()=>obs,_melee_web_snapshot_pcm:()=>4096,_melee_web_snapshot_input:()=>pad,
    _melee_web_synctest_draw_diagnostics:()=>diag,_melee_web_snapshot_error:()=>'refused fixture',
    _melee_web_synctest_cpu_drawn_evidence:()=>drawnPointer,_melee_web_synctest_cpu_drawn_evidence_bytes:()=>11832,
    _melee_web_synctest_cpu_restore_evidence:()=>evidence,_melee_web_synctest_cpu_restore_evidence_bytes:()=>CPU_EVIDENCE_BYTES,
    _melee_web_synctest_cpu_restore_configure:()=>{order.push('configure');return configurationFailure?7:0;},
    _melee_web_synctest_submission_json:()=>{queries++;order.push('poll');return JSON.stringify({rendererErrorCount:0,pendingFramePackets:queries===1?1:0,pendingStagingBuffers:0,workerBusy:0,drawCallCount:10,lastBeginFrameId:300+drawCalls,lastEndFrameId:300+drawCalls});},
    ccall:async(name,type,types,args)=>{
      if(name==='melee_web_snapshot_quiescent'){order.push('transfer-quiescent');return 1;}
      assert.equal(name,'melee_web_synctest_cpu_restore_command');const command=args[0];order.push(command);commands.push(command);
      if(command===failCommand)return 99;
      if(command===1&&captureDraw)view.setUint32(diag+8,1501,true);
      if(command===2){view.setUint32(obs,177,true);heap.set(rawPad,pad);if(cpuDraw)heap[diag+131]=1;}
      if(command===6){
        view.setUint32(obs,177,true);heap.set(rawPad,pad);
        heap.set(heap.slice(diag,diag+132),drawnPointer+11432);
        drawCalls++;queries=0;heap.set(drawBytes(true),diag);updateDrawn(1,1);
      }
      if(command===7){
        heap.set(heap.slice(diag,diag+132),drawnPointer+11564);
        if(cpuDraw)heap[diag+131]=1;
        heap.set(heap.slice(diag,diag+132),drawnPointer+11696);updateDrawn(3,7);
      }
      if(command===3){drawCalls++;queries=0;heap.set(drawBytes(true),diag);
        if(drawn){view.setUint32(diag+8,1510,true);view.setUint32(diag+12,1510,true);view.setUint32(diag+56,876,true);view.setUint32(diag+60,876,true);}}
      if(command===4&&drawn){updateDrawn(4,7);if(drawnRawMismatch)heap[drawnPointer+16+1312+4]^=1;}
      if(command===4)assert.ok(queries>=2&&order.indexOf('transfer-quiescent')<order.indexOf(4));
      return 0;
    }};
  const page={evaluate:async(fn,arg)=>fn(arg)};
  return {page,module,order,commands,get freed(){return freed;},get reads(){return nativeReads;},install(){globalThis.window={__slippiSynctest:{module}};}};
}
const helpers={checkedSource:checkedAsyncCcall.toString(),inputMappingSource:'()=>[]',inspectSource:'()=>({interactive:true,players:[]})'};
const checkpoint={boundary:{sourceFrame:176},pad:Buffer.alloc(44).toString('base64')};
test('actual runner page closure configures, retains evidence, waits busy→idle, then admits final present',async()=>{
  const old=globalThis.window;const e=environment();e.install();
  try{const result=await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],helpers);
    assert.deepEqual(e.commands,[1,2,3,4]);assert.equal(e.freed,1);assert.equal(result.cpuRestore.nativeEvidence.equality,1);
    assert.equal(result.cpuRestore.status,'cpu-restore-replay-and-one-present-component-complete');
    assert.equal(result.cpuRestore.nativeEvidence.bytes,CPU_EVIDENCE_BYTES);
  }finally{globalThis.window=old;}
});
test('actual runner refuses before draw, retains partial result and frees configuration wire on every failure',async()=>{
  const old=globalThis.window;
  try{
    for(const failCommand of [1,2]){const e=environment({failCommand});e.install();let error;
      try{await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],helpers);}catch(value){error=value;}
      assert.ok(error?.cpuRestorePartial);assert.equal(error.cpuRestorePartial.commands.at(-1).result,99);
      assert.ok(!e.commands.includes(3));assert.equal(e.freed,1);
    }
    const e=environment({configurationFailure:true});e.install();await assert.rejects(()=>runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],helpers));
    assert.deepEqual(e.commands,[]);assert.equal(e.freed,1);
  }finally{globalThis.window=old;}
});
test('SharedArrayBuffer refusal precedes native reads and configuration',async()=>{
  const old=globalThis.window;const e=environment({shared:true});e.install();
  try{await assert.rejects(()=>runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],helpers),/Shared Wasm heap/);
    assert.equal(e.reads,0);assert.equal(e.freed,0);assert.deepEqual(e.commands,[]);
  }finally{globalThis.window=old;}
});

const mapping=[{sourceSlot:0,initializerPort:0,rawController:0},{sourceSlot:1,initializerPort:2,rawController:1}];
function drawBytes(final=false) {
  const raw=Buffer.alloc(132);raw.writeUInt32LE(6,0);
  for(let slot=0;slot<2;slot++){
    raw.writeUInt32LE(1500+(final?5:0),8+slot*4);
    raw.writeUInt32LE(870+(final?3:0),56+slot*4);
    raw.writeInt32LE(slot,80+slot*4);
  }
  raw.writeUInt32LE(3,128);return raw;
}
function finalSample() {
  const before=drawBytes(),after=drawBytes(true);
  return {observation:Buffer.alloc(1280).toString('base64'),pcm:Buffer.alloc(4272).toString('base64'),
    pad:'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
    status:{drawCallCount:10,lastBeginFrameId:301,lastEndFrameId:301},drawDiagnostics:after.toString('base64'),
    boundary:{interactive:true,players:[{sourceSlot:0,fighterKind:0},{sourceSlot:1,fighterKind:0}]},
    cpuRestore:{status:'cpu-restore-replay-and-one-present-component-complete',commands:[1,2,3,4].map(command=>({command,result:0,nativeEvidence:{equality:1}})),
      nativeEvidence:{equality:1},drawBoundaries:[
        ['before-cpu-baseline-and-replay',before],['after-cpu-capture-before-step',before],
        ['after-cpu-replay-before-final-present',before],['after-final-source-present-and-quiescence',after]]
        .map(([label,raw])=>({label,bytes:132,base64:raw.toString('base64')}))}};
}
const validateFinal=sample=>validateCpuRestoreDrawSample(sample,decodeDrawDiagnostics(drawBytes()),
  {beginFrameId:300,endFrameId:300},mapping);
test('actual outer validator accepts cumulative prefix plus final draw; old zero assertion rejects it',()=>{
  const sample=finalSample();assert.throws(()=>assertDrawDiagnosticsClear(decodeDrawDiagnostics(Buffer.from(sample.drawDiagnostics,'base64')),'old-final-present'),/source draw/);
  assert.equal(validateFinal(sample).fighters.length,2);
});
test('actual outer validator refuses missing/invalid final draw, CPU writes including unused metadata, PAD/input/interactive/GPU drift',()=>{
  let caseIndex=0;
  for(const mutate of [
    s=>{s.cpuRestore.drawBoundaries.pop();},
    s=>{const b=drawBytes();b[131]=1;s.cpuRestore.drawBoundaries[2].base64=b.toString('base64');},
    s=>{s.drawDiagnostics=drawBytes().toString('base64');s.cpuRestore.drawBoundaries[3].base64=s.drawDiagnostics;},
    s=>{s.status.lastBeginFrameId=300;},s=>{s.status.drawCallCount=0;},
    s=>{s.boundary.interactive=false;},s=>{s.boundary.players[1].sourceSlot=0;},
    s=>{s.pad=Buffer.alloc(44).toString('base64');},
    s=>{const b=drawBytes(true);b.writeUInt32LE(1,4);s.drawDiagnostics=b.toString('base64');s.cpuRestore.drawBoundaries[3].base64=s.drawDiagnostics;},
    s=>{const b=drawBytes(true);b.writeUInt32LE(1499,8);s.drawDiagnostics=b.toString('base64');s.cpuRestore.drawBoundaries[3].base64=s.drawDiagnostics;},
  ]){const sample=finalSample();mutate(sample);assert.throws(()=>validateFinal(sample),`refusal case ${caseIndex++}`);}
});
test('actual page closure rejects capture or CPU draw changes before final command and retains command evidence',async()=>{
  const old=globalThis.window;
  try{for(const options of [{cpuDraw:true},{captureDraw:true}]){
    const e=environment(options);e.install();let error;
    try{await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],helpers);}catch(value){error=value;}
    assert.match(String(error),/changed draw diagnostics/);assert.ok(!e.commands.includes(3));
    assert.equal(error.cpuRestorePartial.commands.at(-1).nativeEvidence.equality,1);
    assert.ok(error.cpuRestorePartial.drawBoundaries.length>=2);
  }}finally{globalThis.window=old;}
});
test('actual report assembly retains complete raw/native evidence through later outer failures and partial exceptions',()=>{
  const sample=finalSample(),report={execution:{status:'incomplete'}};
  retainCpuRestoreSample(report,sample);
  sample.boundary.interactive=false;let error;try{validateFinal(sample);}catch(value){error=value;}
  recordFailedCensusExecution(report,error);recordCpuRestoreFailure(report,error);
  assert.equal(report.result,'failed');assert.equal(report.execution.cpuOuterValidation,'failed');
  assert.equal(report.cpuRawSample,sample);assert.equal(report.execution.cpuRestore,sample.cpuRestore);
  assert.equal(report.execution.restorePerformed,true);assert.equal(report.cpuRawSample.pcm,sample.pcm);
  assert.equal(report.execution.cpuRestore.commands[3].nativeEvidence.equality,1);
  const partial={commands:[{command:1,result:1076,nativeEvidence:{firstFailure:1076}}]};
  const failed={execution:{status:'incomplete'}};const refusal=Object.assign(new Error('refusal'),{cpuRestorePartial:partial});
  recordFailedCensusExecution(failed,refusal);recordCpuRestoreFailure(failed,refusal);
  assert.equal(failed.execution.cpuRestore,partial);assert.equal(failed.execution.status,'failed');
  assert.match(failed.execution.restorePerformed,/unknown/);assert.equal(failed.cpuRawSample,undefined);
});
test('actual page exception after final command retains native commands and raw final sample',async()=>{
  const old=globalThis.window;const e=environment();e.install();let error;
  try{
    try{await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],
      {...helpers,inspectSource:'()=>({interactive:false,players:[]})'});}catch(value){error=value;}
    assert.match(String(error),/interactive guard/);assert.deepEqual(e.commands,[1,2,3,4]);
    assert.equal(error.cpuRestorePartial.commands[3].nativeEvidence.equality,1);
    assert.equal(error.cpuRestorePartial.rawSample.observation.length>0,true);
    assert.equal(error.cpuRestorePartial.rawSample.boundary.interactive,false);
    assert.equal(error.cpuRestorePartial.drawBoundaries.at(-1).label,'after-final-source-present-and-quiescence');
  }finally{globalThis.window=old;}
});

test('actual drawn page closure draws baseline before restore, waits twice, retains independent final typed/raw evidence',async()=>{
  const old=globalThis.window,e=environment({drawn:true});e.install();
  try{
    const result=await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],{...helpers,drawnReference:true});
    assert.deepEqual(e.commands,[1,6,7,3,4]);
    const order=e.order;assert.ok(order.indexOf(6)<order.indexOf('poll')&&order.indexOf('transfer-quiescent')<order.indexOf(7));
    assert.equal(order.filter(x=>x==='transfer-quiescent').length,2);
    assert.equal(result.cpuRestore.nativeEvidence.equality,1);assert.equal(result.cpuRestore.drawnEvidence.pair.equality,1);
    assert.equal(result.cpuRestore.drawnEvidence.bytes,11832);assert.equal(result.cpuRestore.drawBoundaries.length,6);
    assert.equal(result.cpuRestore.commands[1].drawnEvidence.stage,1);assert.equal(result.cpuRestore.commands.at(-1).drawnEvidence.stage,4);
  }finally{globalThis.window=old;}
});
test('actual drawn page closure refuses unwanted correction draw and independent postdraw mismatch with partial evidence',async()=>{
  const old=globalThis.window;
  try{for(const options of [{drawn:true,cpuDraw:true},{drawn:true,drawnMismatch:true},{drawn:true,drawnRawMismatch:true}]){
    const e=environment(options);e.install();let error;
    try{await runBrowserCpuRestore(e.page,[[4096,4,1,1]],checkpoint,[],[4,4],{...helpers,drawnReference:true});}catch(value){error=value;}
    assert.ok(error?.cpuRestorePartial);
    if(options.cpuDraw){assert.ok(!e.commands.includes(3));assert.match(String(error),/changed draw diagnostics/);}
    else {assert.equal(e.commands.at(-1),4);assert.ok(error.cpuRestorePartial.rawSample);
      if(options.drawnMismatch)assert.equal(error.cpuRestorePartial.drawnEvidence.pair.equality,0);
      else assert.match(String(error),/Drawn paired evidence differs/);}
  }}finally{globalThis.window=old;}
});
test('actual outer drawn validator requires both presents and phase-matched diagnostics/typed endpoints',()=>{
  const sample=finalSample(),cpu=drawBytes(),baseline=drawBytes(true),final=drawBytes(true);final.writeUInt32LE(1510,8);final.writeUInt32LE(1510,12);final.writeUInt32LE(876,56);final.writeUInt32LE(876,60);
  sample.status.lastBeginFrameId=302;sample.status.lastEndFrameId=302;sample.drawDiagnostics=final.toString('base64');
  sample.cpuRestore.mode='drawn-reference-one-tick-v1';sample.cpuRestore.commands=[1,6,7,3,4].map(command=>({command,result:0}));sample.cpuRestore.baselineGpuStatus={drawCallCount:10,lastBeginFrameId:301,lastEndFrameId:301};
  sample.cpuRestore.drawnEvidence={stage:4,mode:1,pair:{equality:1,firstFailure:0,checkedDomains:0x3fe,matchedDomains:0x3fe,comparison:{status:0}}};
  sample.cpuRestore.drawBoundaries=['before-cpu-baseline-and-replay','after-cpu-capture-before-step','after-cpu-reference-before-baseline-draw',
    'after-baseline-draw-and-quiescence','after-correction-before-final-present','after-final-source-present-and-quiescence']
    .map((label,i)=>({label,bytes:132,base64:(i<3?cpu:i<5?baseline:final).toString('base64')}));
  assert.equal(validateFinal(sample).referenceFighters.length,2);
  const retained={execution:{}};retainCpuRestoreSample(retained,sample);recordCpuRestoreFailure(retained,new Error('later outer failure'));
  assert.equal(retained.execution.restorePerformed,true);assert.equal(retained.cpuRawSample,sample);
  for(const mutate of [s=>s.cpuRestore.baselineGpuStatus.lastBeginFrameId=300,s=>s.status.lastBeginFrameId=301,
    s=>s.cpuRestore.drawBoundaries[2].base64=baseline.toString('base64'),s=>s.cpuRestore.drawnEvidence.pair.matchedDomains=0,
    s=>s.cpuRestore.drawBoundaries[4].base64=final.toString('base64')]){
    const bad=structuredClone(sample);mutate(bad);assert.throws(()=>validateFinal(bad));
  }
});
