// SPDX-License-Identifier: GPL-3.0-or-later
// Host transport controls with a synthetic ABI; no native/browser gameplay claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRuntimeMatchHistoryController,decodeRuntimeMatchHistoryStatus,
  decodeRuntimeMatchHistoryInput,validateRuntimeMatchStartPrefix} from '../runtime_match_history_controller.mjs';

const prefix='_melee_web_native_menu_match_history_';
const HOST_FIELDS=['runtime_sha256','asset_identity_sha256','save_profile_sha256','profile_identity'];
const DEFAULT_PUBLICATION={schema:2,bytes:48,state:5,sourceFrame:0,sourceDraws:3,
  padPolicy:1,firstUsePending:0,reserved:0,
  priorSubmission:9007199254741001n,sourceSubmission:9007199254741004n};
function fixture({failCommand=0,hangCommand=0,allocationPointer=4096,startPhase=0,
  failStart='',offerText=' { "source" : "native-offer" } ',
  agreementText=' { "source" : "native-agreement" } ',
  liveIdentityText=' { "source" : "native-live-identity" } ',
  prefixText=' { "source" : "native-prefix" } ',
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
    module[prefix+'start_prefix_json']=()=>{
      startCalls.push({name:'prefix',phase:nativeStartPhase});
      if(failStart==='prefix'||nativeStartPhase!==5)return refuse('prefix',55);
      return borrowedString(prefixText);
    };
  }
  return {module,calls,put,view,allocations,frees,padCalls,startCalls,
    setStartPhase:value=>{nativeStartPhase=value;},writePublication};
}

const host=Object.freeze({runtime_sha256:'a'.repeat(64),asset_identity_sha256:'b'.repeat(64),
  save_profile_sha256:'c'.repeat(64),profile_identity:'slippi-profile-v1'});
const startIdentity={match_id:'local-start-1',seed:0xfedcba98,stage_id:2,
  players:[{port:0,character:0},{port:1,character:0}]};
const prefixPad='0a'.repeat(44);
const makePrefix=(overrides={})=>{
  const prior=BigInt(DEFAULT_PUBLICATION.priorSubmission);
  return {
    schema:'melee-web-runtime-match-start-prefix-v1',
    input_provenance:'ordinary-local-four-port-pad-v1',
    source_ticks:3,source_draws:3,final_ready_tick:13,
    initial_resident_transfer_drains:0,final_resident_transfer_drains:1,
    rows:[0,1,2].map(index=>({poll_serial:'9007199254740993',tick_before:10+index,
      tick_after:11+index,source_frame_before:0,source_frame_after:0,
      hud_ready_before:false,hud_ready_after:index===2,pad_hex:prefixPad,
      draw_submission:(prior+BigInt(index+1)).toString()})),
    live_identity:startIdentity,
    ...overrides,
  };
};
const validPrefix=makePrefix();
const liveIdentityText=' { "players" : [ { "character" : 0, "port" : 0 }, { "character" : 0, "port" : 1 } ], "stage_id" : 2, "seed" : 4275878552, "match_id" : "local-start-1" } ';

test('start-prefix validator accepts exact ordinary local startup rows and lossless native receipt bindings',()=>{
  const nativePrefix={...validPrefix,live_identity:{...startIdentity}};
  const rawJSON=` \n${JSON.stringify(nativePrefix)} \n`;
  const f=fixture({startPhase:5,liveIdentityText}),owner=createRuntimeMatchHistoryController(f.module);
  const publication=owner.startPublication(),liveIdentity=owner.startLiveIdentity();
  const parsed=validateRuntimeMatchStartPrefix(rawJSON,{publication,liveIdentity});
  assert.equal(parsed.source_ticks,3);assert.equal(parsed.source_draws,3);
  assert.equal(parsed.rows[0].poll_serial,'9007199254740993','poll serial remains lossless above Number safe integer range');
  assert.equal(parsed.rows[1].poll_serial,parsed.rows[0].poll_serial,'ordinary PAD batching may reuse its poll serial');
  assert.equal(parsed.rows.at(-1).draw_submission,publication.sourceSubmission);
  assert.equal(parsed.live_identity.match_id,'local-start-1');
  assert.equal(rawJSON.includes(' \n'),true,'caller retains exact native prefix JSON separately from parsed validation');
});

test('start-prefix validator refuses malformed provenance, bounds, tick/HUD rows, and receipt mismatches',()=>{
  const f=fixture({startPhase:5,liveIdentityText}),owner=createRuntimeMatchHistoryController(f.module);
  const publication=owner.startPublication(),liveIdentity=owner.startLiveIdentity();
  const reject=(label,mutate,pattern)=>{
    const value=structuredClone(validPrefix);mutate(value);
    assert.throws(()=>validateRuntimeMatchStartPrefix(JSON.stringify(value),{publication,liveIdentity}),
      pattern??/Runtime start prefix/,label);
  };
  reject('wrong schema',value=>{value.schema='other';},/schema or input provenance/);
  reject('wrong provenance',value=>{value.input_provenance='relay';},/schema or input provenance/);
  reject('tick and draw counts differ',value=>{value.source_draws=2;},/count, ready tick/);
  reject('source prefix exceeds bounded ticks',value=>{value.source_ticks=1025;},/count, ready tick/);
  reject('row count differs',value=>{value.rows.pop();},/count, ready tick/);
  reject('final ready tick differs',value=>{value.final_ready_tick=12;},/final ready tick/);
  reject('tick does not advance once',value=>{value.rows[1].tick_after++;},/contiguous single step/);
  reject('ticks are not contiguous',value=>{value.rows[1].tick_before++;value.rows[1].tick_after++;},/contiguous single step/);
  reject('source tick would wrap uint32',value=>{value.rows[0].tick_before=0xffffffff;value.rows[0].tick_after=0;},/contiguous single step/);
  reject('source frame advances',value=>{value.rows[0].source_frame_after=1;},/frame-zero HUD-ready/);
  reject('HUD ready arrives early',value=>{value.rows[0].hud_ready_after=true;},/frame-zero HUD-ready/);
  reject('last HUD row does not transition to ready',value=>{value.rows.at(-1).hud_ready_after=false;},/frame-zero HUD-ready/);
  reject('PAD byte extent is short',value=>{value.rows[0].pad_hex='00'.repeat(43);},/PAD extent/);
  reject('PAD hex must be lowercase',value=>{value.rows[0].pad_hex='AA'.repeat(44);},/PAD extent/);
  reject('poll serial must be positive',value=>{value.rows[0].poll_serial='0';},/poll serial is outside/);
  reject('poll serial must be canonical decimal',value=>{value.rows[0].poll_serial='01';},/not canonical/);
  reject('poll serial cannot move backwards',value=>{value.rows[1].poll_serial='9007199254740992';},/moved backwards/);
  reject('draw submission must be canonical decimal',value=>{value.rows[0].draw_submission='09007199254740993';},/not canonical/);
  reject('draw submission must be greater than prior receipt',value=>{value.rows[0].draw_submission=publication.priorSubmission;},/outside its publication interval/);
  reject('draw submissions must increase',value=>{value.rows[1].draw_submission=value.rows[0].draw_submission;},/outside its publication interval/);
  reject('draw submission cannot exceed publication',value=>{value.rows[2].draw_submission='9007199254741005';},/outside its publication interval/);
  reject('resident transfer drain is boolean',value=>{value.initial_resident_transfer_drains=2;},/resident-transfer drain bounds/);
  reject('live identity must match all held fields',value=>{value.live_identity.seed++;},/embedded live identity differs/);
  const laterReceiptFixture=fixture({startPhase:5,liveIdentityText,
    publication:{...DEFAULT_PUBLICATION,sourceSubmission:9007199254741005n}});
  const laterReceipt=createRuntimeMatchHistoryController(laterReceiptFixture.module).startPublication();
  assert.throws(()=>validateRuntimeMatchStartPrefix(JSON.stringify(validPrefix),
    {publication:laterReceipt,liveIdentity}),/final ready tick or publication submission/,
  'last source draw must be the publication final submission');
  const wrongPublication={...publication,sourceSubmission:'9007199254741005'};
  assert.throws(()=>validateRuntimeMatchStartPrefix(JSON.stringify(validPrefix),
    {publication:wrongPublication,liveIdentity}),/differs from its fixed 48-byte receipt/);
  assert.throws(()=>validateRuntimeMatchStartPrefix(JSON.stringify(validPrefix),
    {publication,liveIdentity:'{}'}),/embedded live identity differs/);
});

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
  assert.throws(()=>owner.startPrefix(),/start_state is unavailable/);
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
  assert.equal(receipt.state,5);assert.equal(receipt.sourceFrame,0);assert.equal(receipt.sourceDraws,3);
  assert.equal(receipt.priorSubmission,'9007199254741001');
  assert.equal(receipt.sourceSubmission,'9007199254741004');
  const frozenPublication=receipt.raw.slice();
  const allocationCount=f.allocations.length,freeCount=f.frees.length;
  const prefix=owner.startPrefix();
  assert.equal(prefix,' { "source" : "native-prefix" } ','borrowed prefix is copied without parsing or normalization');
  const liveIdentity=owner.startLiveIdentity();
  assert.equal(liveIdentity,' { "source" : "native-live-identity" } ');
  assert.equal(prefix,' { "source" : "native-prefix" } ','later native identity read cannot mutate the copied prefix');
  assert.deepEqual(receipt.raw,frozenPublication,'publication bytes are copied out of native storage');
  assert.equal(f.allocations.length,allocationCount,'held-frame reads allocate no host arguments');
  assert.equal(f.frees.length,freeCount,'held-frame reads perform no native frees');
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

test('start-prefix read requires the held frame-zero native phase and latches native refusal',()=>{
  const stale=fixture({startPhase:2}),staleOwner=createRuntimeMatchHistoryController(stale.module);
  assert.throws(()=>staleOwner.startPrefix(),/requires held frame zero/);
  assert.equal(stale.startCalls.length,0,'stale phase is rejected before asking native for its borrowed prefix');
  assert.deepEqual(stale.allocations.map(row=>row.size),[11]);

  const failed=fixture({startPhase:5,failStart:'prefix'}),failedOwner=createRuntimeMatchHistoryController(failed.module);
  assert.throws(()=>failedOwner.startPrefix(),/Native agreed-start start_prefix_json refused: synthetic refusal/);
  assert.throws(()=>failedOwner.startPrefix(),/start_prefix_json refused/);
  assert.equal(failed.startCalls.filter(call=>call.name==='prefix'&&!call.refused).length,1,
    'first native failure blocks another prefix read');
  assert.deepEqual(failed.allocations.map(row=>row.size),[11],'prefix reads never allocate Wasm arguments');
  assert.deepEqual(failed.frees,[]);
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

function checksumFixture(phase=6){
  const f=fixture();
  const owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  f.put(56,phase); // history.phase
  f.put(68,1); // history.currentFrame
  f.put(72,1); // history.sealedEnd
  f.put(88,1); // history.forwardSteps
  f.put(92,phase===13?1:0); // history.replaySteps
  f.put(96,17); // history.pollSerial, low word
  f.put(112,1); // inputReady
  const pointer=16384;
  f.view.setUint32(pointer+4,3,true);
  f.view.setUint32(pointer+12,1,true);
  f.view.setBigUint64(pointer+56,0xfedcba9876543210n,true);
  f.module[prefix+'checksum_data']=()=>pointer;
  f.module[prefix+'checksum_bytes']=()=>64;
  return {...f,owner,pointer};
}

test('checksum copies the exact native CPU endpoint bytes before another native call',()=>{
  for(const phase of [6,13]){
    const f=checksumFixture(phase);
    const expected=f.module.HEAPU8.slice(f.pointer,f.pointer+64);
    const getStatus=f.module[prefix+'status'];
    let statusReads=0;
    f.module[prefix+'status']=()=>{
      if(++statusReads===2)f.module.HEAPU8.fill(0,f.pointer,f.pointer+64);
      return getStatus();
    };
    const allocationCount=f.allocations.length;
    assert.deepEqual(f.owner.checksum(),expected);
    assert.equal(statusReads,2);
    assert.equal(f.allocations.length,allocationCount);
    assert.equal(f.frees.length,0);
  }
});

test('checksum refuses wrong phase, borrowed extent, frame and changed owner status',()=>{
  const old=checksumFixture();
  delete old.module[prefix+'checksum_data'];
  assert.throws(()=>old.owner.checksum(),/checksum exports are unavailable/);

  const phase=checksumFixture(5);
  phase.module[prefix+'checksum_data']=()=>assert.fail('read before copied CPU endpoint');
  assert.throws(()=>phase.owner.checksum(),/requires a copied, idle CPU endpoint/);

  const size=checksumFixture();
  size.module[prefix+'checksum_bytes']=()=>63;
  assert.throws(()=>size.owner.checksum(),/ABI must be exactly 64 bytes/);

  const extent=checksumFixture();
  extent.module[prefix+'checksum_data']=()=>extent.module.HEAPU8.length-63;
  assert.throws(()=>extent.owner.checksum(),/borrowed extent/);

  const frame=checksumFixture();
  frame.view.setUint32(frame.pointer+12,2,true);
  assert.throws(()=>frame.owner.checksum(),/source frame differs/);

  const owner=checksumFixture();
  owner.module[prefix+'checksum_data']=()=>{owner.put(96,18);return owner.pointer;};
  assert.throws(()=>owner.owner.checksum(),/changed its native owner status/);
});
