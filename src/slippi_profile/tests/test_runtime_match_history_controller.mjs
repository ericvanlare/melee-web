// SPDX-License-Identifier: GPL-3.0-or-later
// Host transport controls with a synthetic ABI; no native/browser gameplay claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRuntimeMatchHistoryController,decodeRuntimeMatchHistoryStatus,
  decodeRuntimeMatchHistoryInput} from '../runtime_match_history_controller.mjs';

const prefix='_melee_web_native_menu_match_history_';
function fixture({failCommand=0,hangCommand=0,allocationPointer=4096}={}){
  const heap=new Uint8Array(65536),view=new DataView(heap.buffer),calls=[],allocations=[],frees=[],padCalls=[];
  const put=(offset,value)=>view.setUint32(256+offset,value,true);
  put(0,1);put(4,128);put(48,1);put(52,64);
  let nextPointer=allocationPointer;
  const module={HEAPU8:heap,
    _malloc:size=>{const pointer=nextPointer;allocations.push({pointer,size});nextPointer+=size+16;return pointer;},
    _free:pointer=>frees.push(pointer),UTF8ToString:()=> 'synthetic refusal',
    _melee_web_net_validate_pad:(pointer,size)=>{padCalls.push({operation:'validate',pointer,size,
      pad:[...heap.subarray(pointer,pointer+size)]});return 1;}};
  for(const name of ['take_over','configure','remote_pad','request','status','status_bytes','input','input_bytes',
    'error','identity_json','cpu_endpoint_begin','cpu_endpoint_record','cpu_endpoint_data','cpu_endpoint_bytes',
    'cpu_endpoint_end','motor_data','motor_bytes','motor_commit_data','motor_commit_bytes','pcm_data','pcm_bytes'])module[prefix+name]=()=>0;
  module[prefix+'status']=()=>256;module[prefix+'status_bytes']=()=>128;
  module[prefix+'input_bytes']=()=>168;
  module[prefix+'configure']=()=>1;
  module[prefix+'remote_pad']=(pointer,size)=>{padCalls.push({operation:'remote',pointer,size,
    pad:[...heap.subarray(pointer,pointer+size)]});return 1;};
  module[prefix+'take_over']=(frame,local,remote)=>{put(8,1);put(32,frame);put(36,local);put(40,remote);return 1;};
  module[prefix+'request']=command=>{
    calls.push(command);put(16,command);put(24,view.getUint32(280,true)+1);
    if(command!==hangCommand)queueMicrotask(()=>{
      if(command===failCommand)put(12,9);
      put(16,0);put(20,command);put(28,view.getUint32(280,true));
      if(command===10)put(8,2);
    });
    return 1;
  };
  return {module,calls,put,view,allocations,frees,padCalls};
}

test('controller joins exact native completion sequence and forbids concurrent commands',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  assert.equal(owner.takeOver({sourceFrame:176,localPort:0,remotePort:1}).ownerState,1);
  const pending=owner.command('capture');
  await assert.rejects(()=>owner.command('begin'),/in progress/);
  const result=await pending;
  assert.equal(result.lastCompletedCommand,1);assert.equal(result.completeSequence,1);
  assert.deepEqual(f.calls,[1]);assert.equal(owner.journal()[0].status,'complete');
  await owner.command('cleanup');
  await assert.rejects(()=>owner.command('capture'),/cleaned/);
});

test('first native failure blocks later work but still permits owned cleanup',async()=>{
  const f=fixture({failCommand:1}),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:176,localPort:0,remotePort:1});
  await assert.rejects(()=>owner.command('capture'),/Native history refused/);
  await assert.rejects(()=>owner.command('begin'),/Native history refused/);
  const result=await owner.command('cleanup');
  assert.equal(result.ownerState,2);assert.equal(result.firstFailure,9);
  assert.deepEqual(f.calls,[1,10]);assert.equal(owner.journal()[0].status,'failed');
});

test('timed out native work is never retried or reported complete',async()=>{
  const f=fixture({hangCommand:1}),owner=createRuntimeMatchHistoryController(f.module,{commandTimeoutMs:1});
  owner.takeOver({sourceFrame:176,localPort:0,remotePort:1});
  await assert.rejects(()=>owner.command('capture'),/deadline/);
  await assert.rejects(()=>owner.command('begin'),/deadline/);
  assert.deepEqual(f.calls,[1]);assert.equal(owner.journal()[0].status,'failed');
});

test('PAD scratch is allocated before capture, reused without allocator churn, and freed after native cleanup',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  const scratch=f.allocations[0];
  assert.deepEqual(scratch,{pointer:4096,size:11},'controller reserves one exact PAD argument before capture');
  assert.deepEqual(f.frees,[]);
  owner.configure([[1,1,1,1]]);
  assert.deepEqual(f.allocations.map(row=>row.size),[11,16],
    'linked-table configuration remains a pre-capture host allocation');
  assert.deepEqual(f.frees,[f.allocations[1].pointer]);
  owner.takeOver({sourceFrame:176,localPort:0,remotePort:1});
  await owner.command('capture');
  const allocationCount=f.allocations.length,freeCount=f.frees.length;
  const pad=Uint8Array.from([4,0,127,128,0,0,20,0,30,0,0]);
  assert.equal(owner.validatePad(pad),true);
  owner.remotePad(pad);
  assert.deepEqual(f.padCalls,[
    {operation:'validate',pointer:scratch.pointer,size:11,pad:[...pad]},
    {operation:'remote',pointer:scratch.pointer,size:11,pad:[...pad]},
  ]);
  assert.equal(f.allocations.length,allocationCount,'postcapture PAD path made no Wasm allocation');
  assert.equal(f.frees.length,freeCount,'postcapture PAD path made no Wasm free');
  assert.throws(()=>owner.configure([[1,1,1,1]]),/configured before capture/);
  assert.throws(()=>owner.identity({}),/read before capture/);
  assert.equal(f.allocations.length,allocationCount,'late configuration or identity cannot churn allocator');
  assert.equal(f.frees.length,freeCount);
  await owner.command('cleanup');
  assert.equal(f.frees.length,freeCount+1);
  assert.equal(f.frees.at(-1),scratch.pointer,'PAD argument is released only after ownerState is terminal');
});

test('constructor releases an unusable PAD allocation before refusing ownership',()=>{
  const f=fixture({allocationPointer:65530});
  assert.throws(()=>createRuntimeMatchHistoryController(f.module),/allocation is outside Wasm memory/);
  assert.deepEqual(f.frees,[65530],'a constructor refusal cannot leak its unusable scratch pointer');
});

test('input receipt preserves a lossless poll serial and exact local PAD through correction',()=>{
  const bytes=new Uint8Array(168),view=new DataView(bytes.buffer);
  [1,168,1,1].forEach((value,index)=>view.setUint32(index*4,value,true));
  view.setBigUint64(16,9007199254740993n,true);view.setUint32(24,1,true);view.setUint32(28,0,true);
  const pad=Uint8Array.from([4,0,127,128,0,0,20,0,30,0,0]);
  for(const start of [32,76,120])bytes.set(pad,start+11);
  const result=decodeRuntimeMatchHistoryInput(bytes);
  assert.equal(result.pollSerial,'9007199254740993');assert.deepEqual(result.localPad,pad);
  bytes[131]^=1;assert.throws(()=>decodeRuntimeMatchHistoryInput(bytes),/retained local PAD/);
});

test('status rejects invalid bounds while retaining native failure codes for cleanup',()=>{
  const f=fixture();f.put(12,42);
  assert.equal(decodeRuntimeMatchHistoryStatus(f.module.HEAPU8.slice(256,384)).firstFailure,42);
  f.put(116,2);assert.throws(()=>decodeRuntimeMatchHistoryStatus(f.module.HEAPU8.slice(256,384)),/bounded-owner/);
});
