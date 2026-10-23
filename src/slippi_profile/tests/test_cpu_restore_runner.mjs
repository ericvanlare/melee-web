import assert from 'node:assert/strict';
import {test} from 'node:test';
import {validateCpuRestoreConfiguration,runBrowserCpuRestore,CPU_EVIDENCE_BYTES} from './cpu_restore_runner.mjs';
import {checkedAsyncCcall} from './runner_contract.mjs';
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
function environment({failCommand=0,configurationFailure=false,shared=false}={}){
  const heap=new Uint8Array(shared?new SharedArrayBuffer(65536):new ArrayBuffer(65536));let queries=0,nativeReads=0;
  const obs=128,pad=1536,diag=1600,evidence=32768;const view=new DataView(heap.buffer);view.setUint32(obs,176,true);view.setUint32(diag,2,true);
  const order=[],commands=[];let freed=0;
  const rawPad=Buffer.from('AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=','base64');
  const writeEvidence=()=>{
    view.setUint32(evidence,1,true);view.setUint32(evidence+4,CPU_EVIDENCE_BYTES,true);view.setUint32(evidence+8,5,true);
    view.setUint32(evidence+16,176,true);view.setUint32(evidence+20,177,true);view.setUint32(evidence+24,0,true);view.setUint32(evidence+28,1,true);
    view.setUint32(evidence+11368,0x3fe,true);view.setUint32(evidence+11372,0x3fe,true);
    view.setUint32(evidence+32,177,true);view.setUint32(evidence+1312,177,true);
    heap.set(rawPad,evidence+11136);heap.set(rawPad,evidence+11180);
  };
  writeEvidence();const module={HEAPU8:heap,UTF8ToString:value=>value,
    _malloc:()=>2048,_free:()=>{freed++;},
    _melee_web_snapshot_observation_size:()=>{nativeReads++;return 1280;},_melee_web_snapshot_pcm_size:()=>4272,
    _melee_web_synctest_draw_diagnostics_size:()=>132,
    _melee_web_snapshot_observation:()=>obs,_melee_web_snapshot_pcm:()=>4096,_melee_web_snapshot_input:()=>pad,
    _melee_web_synctest_draw_diagnostics:()=>diag,_melee_web_snapshot_error:()=>'refused fixture',
    _melee_web_synctest_cpu_restore_evidence:()=>evidence,_melee_web_synctest_cpu_restore_evidence_bytes:()=>CPU_EVIDENCE_BYTES,
    _melee_web_synctest_cpu_restore_configure:()=>{order.push('configure');return configurationFailure?7:0;},
    _melee_web_synctest_submission_json:()=>{queries++;order.push('poll');return JSON.stringify({rendererErrorCount:0,pendingFramePackets:queries===1?1:0,pendingStagingBuffers:0,workerBusy:0});},
    ccall:async(name,type,types,args)=>{
      if(name==='melee_web_snapshot_quiescent'){order.push('transfer-quiescent');return 1;}
      assert.equal(name,'melee_web_synctest_cpu_restore_command');const command=args[0];order.push(command);commands.push(command);
      if(command===failCommand)return 99;
      if(command===2){view.setUint32(obs,177,true);heap.set(rawPad,pad);}
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
