// SPDX-License-Identifier: GPL-3.0-or-later
// Host transport controls with a synthetic ABI; no native/browser gameplay claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRuntimeMatchHistoryController,decodeRuntimeMatchHistoryStatus,
  decodeRuntimeMatchHistoryInput} from '../runtime_match_history_controller.mjs';

const prefix='_melee_web_native_menu_match_history_';
const HOST_FIELDS=['runtime_sha256','asset_identity_sha256','save_profile_sha256','profile_identity'];
const DEFAULT_PUBLICATION={schema:1,bytes:48,state:5,sourceFrame:0,sourceDraws:1,
  padPolicy:1,firstUsePending:0,reserved:0,
  priorSubmission:9007199254741001n,sourceSubmission:9007199254741002n};
function fixture({failCommand=0,hangCommand=0,allocationPointer=4096,startPhase=0,
  failStart='',offerText=' { "source" : "native-offer" } ',
  agreementText=' { "source" : "native-agreement" } ',
  liveIdentityText=' { "source" : "native-live-identity" } ',
  publication=DEFAULT_PUBLICATION,publicationBytes=48,agreedStartExports=true}={}){
  const heap=new Uint8Array(65536),view=new DataView(heap.buffer),calls=[],allocations=[],frees=[],padCalls=[],startCalls=[];
  const put=(offset,value)=>view.setUint32(256+offset,value,true);
  put(0,1);put(4,128);put(48,1);put(52,64);
  const startStringPointer=2048,publicationPointer=8192,errorPointer=1024;
  const encoder=new TextEncoder(),decoder=new TextDecoder();
  let nativeStartPhase=startPhase;
  const cString=pointer=>{
    const end=heap.indexOf(0,pointer);
    if(end<0)throw Error('synthetic native string lacks terminator');
    return decoder.decode(heap.subarray(pointer,end));
  };
  const borrowedString=value=>{
    const bytes=encoder.encode(value+'\0');heap.set(bytes,startStringPointer);return startStringPointer;
  };
  const writePublication=value=>{
    const bytes=new Uint8Array(48),data=new DataView(bytes.buffer);
    ['schema','bytes','state','sourceFrame','sourceDraws','padPolicy','firstUsePending','reserved']
      .forEach((key,index)=>data.setUint32(index*4,value[key],true));
    data.setBigUint64(32,BigInt(value.priorSubmission),true);
    data.setBigUint64(40,BigInt(value.sourceSubmission),true);
    heap.set(bytes,publicationPointer);
  };
  writePublication(publication);
  let nextPointer=allocationPointer;
  const module={HEAPU8:heap,
    _malloc:size=>{const pointer=nextPointer;allocations.push({pointer,size});nextPointer+=size+16;return pointer;},
    _free:pointer=>frees.push(pointer),UTF8ToString:pointer=>pointer===errorPointer?'synthetic refusal':cString(pointer),
    _melee_web_net_validate_pad:(pointer,size)=>{padCalls.push({operation:'validate',pointer,size,
      pad:[...heap.subarray(pointer,pointer+size)]});return 1;}};
  for(const name of ['take_over','configure','remote_pad','request','status','status_bytes','input','input_bytes',
    'error','identity_json','cpu_endpoint_begin','cpu_endpoint_record','cpu_endpoint_data','cpu_endpoint_bytes',
    'cpu_endpoint_end','motor_data','motor_bytes','motor_commit_data','motor_commit_bytes','pcm_data','pcm_bytes'])module[prefix+name]=()=>0;
  module[prefix+'status']=()=>256;module[prefix+'status_bytes']=()=>128;
  module[prefix+'error']=()=>errorPointer;
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
  if(agreedStartExports){
    const recordHost=pointers=>pointers.map(cString);
    const refuse=(name,code)=>{put(12,code);startCalls.push({name,refused:true});return 0;};
    module[prefix+'start_enable']=()=>{
      startCalls.push({name:'enable',phase:nativeStartPhase});
      if(failStart==='enable'||nativeStartPhase!==0)return refuse('enable',51);
      nativeStartPhase=1;return 1;
    };
    module[prefix+'start_state']=()=>nativeStartPhase;
    module[prefix+'start_offer_json']=(...pointers)=>{
      startCalls.push({name:'offer',host:recordHost(pointers),phase:nativeStartPhase});
      if(failStart==='offer'||nativeStartPhase!==2)return refuse('offer',52);
      return borrowedString(offerText);
    };
    module[prefix+'start_agreement_json']=(seed,...pointers)=>{
      startCalls.push({name:'agreement',seed,host:recordHost(pointers),phase:nativeStartPhase});
      if(failStart==='agreement'||nativeStartPhase!==2)return refuse('agreement',53);
      return borrowedString(agreementText);
    };
    module[prefix+'start_commit']=(seed,expectedPointer,expectedBytes,...pointers)=>{
      const exactBytes=heap.slice(expectedPointer,expectedPointer+expectedBytes);
      const terminated=heap[expectedPointer+expectedBytes]===0;
      startCalls.push({name:'commit',seed,exactBytes,terminated,host:recordHost(pointers),phase:nativeStartPhase});
      if(failStart==='commit'||nativeStartPhase!==2||!terminated)return refuse('commit',54);
      nativeStartPhase=3;return 1;
    };
    module[prefix+'start_publication_bytes']=()=>publicationBytes;
    module[prefix+'start_publication_data']=()=>nativeStartPhase===5?publicationPointer:0;
    module[prefix+'start_live_identity_json']=()=>nativeStartPhase===5?borrowedString(liveIdentityText):0;
  }
  return {module,calls,put,view,allocations,frees,padCalls,startCalls,
    setStartPhase:value=>{nativeStartPhase=value;},writePublication};
}

const host=Object.freeze({runtime_sha256:'a'.repeat(64),asset_identity_sha256:'b'.repeat(64),
  save_profile_sha256:'c'.repeat(64),profile_identity:'slippi-profile-v1'});

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

test('agreed-start exports stay optional until called on an older production module',()=>{
  const f=fixture({agreedStartExports:false}),owner=createRuntimeMatchHistoryController(f.module);
  assert.equal(owner.status().ownerState,0);
  assert.throws(()=>owner.startEnable(),/start_enable is unavailable/);
  assert.deepEqual(f.allocations.map(row=>row.size),[11]);
  assert.deepEqual(f.frees,[]);
});

test('agreed-start forwards host identities and seed while returning exact native bytes and held-frame receipt',()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module),seed=0xfedcba98;
  const offer=owner.startEnable();
  assert.equal(offer,1,'native CSS opt-in reaches enabled state');
  f.setStartPhase(2); // Original menu has captured its source-selected offer.
  const exactOffer=owner.startOffer(host);
  assert.equal(exactOffer,' { "source" : "native-offer" } ');
  const exactAgreement=owner.startAgreement(seed,host);
  assert.equal(exactAgreement,' { "source" : "native-agreement" } ');
  assert.equal(owner.startCommit(seed,exactAgreement,host),3);
  assert.deepEqual(f.startCalls.filter(call=>['offer','agreement'].includes(call.name))
    .map(call=>call.host),[HOST_FIELDS.map(key=>host[key]),HOST_FIELDS.map(key=>host[key])]);
  const commit=f.startCalls.find(call=>call.name==='commit');
  assert.equal(commit.seed,seed);
  assert.deepEqual(commit.exactBytes,new TextEncoder().encode(exactAgreement));
  assert.equal(commit.terminated,true);
  assert.deepEqual(commit.host,HOST_FIELDS.map(key=>host[key]));
  assert.deepEqual(f.allocations.map(row=>row.size),[11,
    ...HOST_FIELDS.map(key=>host[key].length+1),...HOST_FIELDS.map(key=>host[key].length+1),
    new TextEncoder().encode(exactAgreement).length+1,
    ...HOST_FIELDS.map(key=>host[key].length+1)],'offer/agreement/commit use temporary, exact host buffers');
  assert.equal(f.frees.length,f.allocations.length-1,'all start arguments are released while retained PAD scratch remains owned');

  f.setStartPhase(5);
  const receipt=owner.startPublication();
  assert.equal(receipt.state,5);assert.equal(receipt.sourceFrame,0);assert.equal(receipt.sourceDraws,1);
  assert.equal(receipt.priorSubmission,'9007199254741001');
  assert.equal(receipt.sourceSubmission,'9007199254741002');
  const frozenPublication=receipt.raw.slice();
  const liveIdentity=owner.startLiveIdentity();
  assert.equal(liveIdentity,' { "source" : "native-live-identity" } ');
  assert.deepEqual(receipt.raw,frozenPublication,'publication bytes are copied out of native storage');
  assert.equal(f.allocations.map(row=>row.size).length,14,'read-only held-frame inspection allocates no host arguments');
});

test('wrong native phase and native first failure latch without retry or allocation leak',()=>{
  const f=fixture({startPhase:1}),owner=createRuntimeMatchHistoryController(f.module);
  assert.throws(()=>owner.startOffer(host),/Native agreed-start start_offer_json refused: synthetic refusal/);
  const allocationsAfterRefusal=f.allocations.length,freesAfterRefusal=f.frees.length;
  assert.equal(allocationsAfterRefusal,5,'failed offer allocated only four bounded host strings');
  assert.equal(freesAfterRefusal,4,'failed offer freed each temporary host string');
  assert.throws(()=>owner.startAgreement(7,host),/start_offer_json refused/);
  assert.equal(f.startCalls.filter(call=>call.name==='agreement').length,0,'latched failure prevents retry');
  assert.equal(f.allocations.length,allocationsAfterRefusal);
  assert.equal(f.frees.length,freesAfterRefusal);

  const failed=fixture({failStart:'enable'}),failedOwner=createRuntimeMatchHistoryController(failed.module);
  assert.throws(()=>failedOwner.startEnable(),/Native agreed-start start_enable refused: synthetic refusal/);
  assert.throws(()=>failedOwner.startState(),/start_enable refused/);
});

test('held-frame publication rejects malformed fixed receipt and live identity requires held state',()=>{
  const malformed={...DEFAULT_PUBLICATION,reserved:1};
  const f=fixture({startPhase:5,publication:malformed}),owner=createRuntimeMatchHistoryController(f.module);
  assert.throws(()=>owner.startPublication(),/publication receipt schema or held-frame owner/);
  assert.throws(()=>owner.startLiveIdentity(),/publication receipt schema or held-frame owner/,
    'the first malformed held-owner receipt remains the controller first failure');

  const wrongPhase=fixture({startPhase:2}),wrongOwner=createRuntimeMatchHistoryController(wrongPhase.module);
  assert.throws(()=>wrongOwner.startLiveIdentity(),/requires held frame zero/);
  assert.equal(wrongPhase.startCalls.length,0,'live identity is never read from an unheld native phase');
  assert.deepEqual(wrongPhase.allocations.map(row=>row.size),[11]);
});

test('agreed-start argument paths refuse allocator churn after match-history capture',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:176,localPort:0,remotePort:1});
  await owner.command('capture');
  const allocations=f.allocations.length,frees=f.frees.length;
  assert.throws(()=>owner.startOffer(host),/cannot be allocated after match-history capture/);
  assert.throws(()=>owner.startAgreement(1,host),/cannot be allocated after match-history capture/);
  assert.throws(()=>owner.startCommit(1,'{}',host),/cannot be allocated after match-history capture/);
  assert.equal(f.allocations.length,allocations);
  assert.equal(f.frees.length,frees);
  await owner.command('cleanup');
});
