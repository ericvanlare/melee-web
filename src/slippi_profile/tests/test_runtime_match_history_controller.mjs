// SPDX-License-Identifier: GPL-3.0-or-later
// Host transport controls with a synthetic ABI; no native/browser gameplay claim.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRuntimeMatchHistoryController,decodeRuntimeMatchHistoryStatus,
  decodeRuntimeMatchHistoryInput,decodeRuntimeMatchHistoryPollTicket,
  decodeRuntimeMatchHistoryEpisodeInputRow,decodeRuntimeMatchHistoryPrefixHandoff,
  validateRuntimeMatchStartPrefix} from '../runtime_match_history_controller.mjs';

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

function episodeFixture(){
  const f=fixture(),{module,put,view}=f;
  const ticketPointer=16384,rowPointer=16512;
  let current=0,phase=0,incarnation=0,forwardSteps=0,replaySteps=0,restoreCount=0,ownerState=1;
  let pollSerial=0n,lastPlan=null,enableCalls=0,latestTicketId=0;
  const raw=Uint8Array.from({length:44},(_,index)=>(index*7+3)&255);
  const writeMetadata=()=>{
    put(8,ownerState);put(32,0);put(36,0);put(40,1);
    put(56,phase);put(64,0);put(68,current);put(72,0);put(76,incarnation);
    put(80,restoreCount);put(84,0);put(88,forwardSteps);put(92,replaySteps);
    view.setBigUint64(352,pollSerial,true);put(112,pollSerial?1:0);put(116,0);put(120,0);put(124,0);
  };
  const writeTicket=seq=>{
    const bytes=new Uint8Array(88),data=new DataView(bytes.buffer);
    data.setUint32(0,1,true);data.setUint32(4,88,true);data.setUint32(8,seq,true);
    data.setUint32(12,current,true);data.setUint32(16,incarnation,true);
    data.setUint32(20,0,true);data.setUint32(24,1,true);
    data.setBigUint64(32,pollSerial,true);bytes.set(raw,40);module.HEAPU8.set(bytes,ticketPointer);
  };
  const writeReceipt=(state,plan=raw)=>{
    const bytes=new Uint8Array(176),data=new DataView(bytes.buffer);
    data.setUint32(0,1,true);data.setUint32(4,176,true);data.setUint32(8,latestTicketId,true);
    data.setUint32(12,0,true);data.setUint32(16,1,true);data.setUint32(20,0,true);
    data.setUint32(24,1,true);data.setUint32(28,state,true);data.setBigUint64(32,pollSerial,true);
    bytes.set(raw,40);if(state){bytes.set(plan,84);bytes.set(plan,128);}
    module.HEAPU8.set(bytes,rowPointer);
  };
  const request=command=>{
    f.calls.push(command);const seq=view.getUint32(280,true)+1;put(16,command);put(24,seq);
    queueMicrotask(()=>{
      if(command===1){phase=2;}
      if(command===18){phase=3;incarnation=1;}
      if(command===12){pollSerial=9007199254740993n+BigInt(seq);latestTicketId=seq;phase=phase===4?4:21;writeTicket(seq);writeReceipt(0);}
      if(command===13){current++;phase=21;forwardSteps++;writeReceipt(1,lastPlan??raw);}
      if(command===14){phase=5;put(72,current);}
      if(command===15){current++;replaySteps++;}
      if(command===16){phase=12;put(72,current);}
      if(command===17){phase=21;incarnation++;put(72,0);replaySteps=0;}
      if(command===10)ownerState=2;
      writeMetadata();put(16,0);put(20,command);put(28,seq);
    });
    return 1;
  };
  module[prefix+'episode_enable']=()=>{enableCalls++;return 1;};
  module[prefix+'ticket_bytes']=()=>88;module[prefix+'ticket_data']=()=>ticketPointer;
  module[prefix+'input_row_bytes']=()=>176;module[prefix+'input_row_data']=ordinal=>ordinal===0?rowPointer:0;
  module[prefix+'request']=request;
  module[prefix+'request_episode_forward']=(ticketId,pointer,size)=>{
    if(size!==44||ticketId!==latestTicketId)return 0;
    lastPlan=module.HEAPU8.slice(pointer,pointer+size);
    return request(13);
  };
  module[prefix+'request_episode_replay']=(ticketId,pointer,size)=>{
    if(size!==44||ticketId!==latestTicketId)return 0;
    lastPlan=module.HEAPU8.slice(pointer,pointer+size);
    return request(15);
  };
  writeMetadata();
  return {...f,episodeState:()=>({current,phase,incarnation,forwardSteps,replaySteps,restoreCount,ownerState,
    pollSerial,lastPlan,enableCalls,ticketPointer,rowPointer})};
}

function confirmedPrefixFixture({failCommand=0}={}){
  const f=episodeFixture(),{module,put,view}=f;
  const stringPointer=2048,ticketPointer=16384,originPointer=16768,boundaryPointer=16960,checksumPointer=17152;
  const rows=[2,3].map((sourceFrame,index)=>{
    const raw=Uint8Array.from({length:44},(_,offset)=>(index*53+offset*11+7)&255);
    const bytes=new Uint8Array(176),data=new DataView(bytes.buffer);
    [1,176,700+index,sourceFrame,1,0,1,1]
      .forEach((value,field)=>data.setUint32(field*4,value,true));
    data.setBigUint64(32,9007199254741001n+BigInt(index),true);
    bytes.set(raw,40);bytes.set(raw,84);bytes.set(raw,128);
    return {bytes,ticketId:700+index,sourceFrame,incarnation:1,localPort:0,remotePort:1,
      pollSerial:(9007199254741001n+BigInt(index)).toString(),raw};
  });
  const state={ownerState:0,firstFailure:0,pending:0,lastCommand:0,requestSequence:0,completeSequence:0,
    expectedFrame:0,localPort:0,remotePort:1,phase:0,preFrame:0,currentFrame:0,sealedEnd:0,
    incarnation:0,restoreCount:0,endpointMask:0,forwardSteps:0,replaySteps:0,pollSerial:0n,
    inputReady:0,gpuPending:0,endpointActive:0,rendererErrors:0,confirmedFrame:2,
    latestTicketId:0,latestRaw:new Uint8Array(44),forwarded:[],descriptor:{
      schema:'melee-web-runtime-prefix-handoff-v1',oldPreFrame:0,confirmedFrame:2,
      selectedEndFrame:4,oldIncarnation:1,finishRequestSequence:0,rebaseRequestSequence:0,
      suffixCount:2,suffixCursor:0,localPort:0,remotePort:1,worldGeneration:'41',contextEpoch:'9',
      ownerStorage:'0000000000000001',ownerContext:'0000000000000002',
      ownerFlow:'0000000000000003',ownerAudio:'0000000000000004'}};
  let ticketBytesReads=0,ticketDataReads=0;
  const writeStatus=()=>{
    put(0,1);put(4,128);put(8,state.ownerState);put(12,state.firstFailure);put(16,state.pending);
    put(20,state.lastCommand);put(24,state.requestSequence);put(28,state.completeSequence);
    put(32,state.expectedFrame);put(36,state.localPort);put(40,state.remotePort);put(44,0);
    put(48,1);put(52,64);put(56,state.phase);put(60,0);put(64,state.preFrame);
    put(68,state.currentFrame);put(72,state.sealedEnd);put(76,state.incarnation);
    put(80,state.restoreCount);put(84,state.endpointMask);put(88,state.forwardSteps);
    put(92,state.replaySteps);view.setBigUint64(352,state.pollSerial,true);
    put(104,0);put(108,0);put(112,state.inputReady);put(116,state.gpuPending);
    put(120,state.endpointActive);put(124,state.rendererErrors);
  };
  const writeString=value=>{
    const bytes=new TextEncoder().encode(value+'\0');module.HEAPU8.set(bytes,stringPointer);return stringPointer;
  };
  const writeTicket=(ticketId,sourceFrame,incarnation,pollSerial,raw)=>{
    const bytes=new Uint8Array(88),data=new DataView(bytes.buffer);
    [1,88,ticketId,sourceFrame,incarnation,0,1,0]
      .forEach((value,field)=>data.setUint32(field*4,value,true));
    data.setBigUint64(32,BigInt(pollSerial),true);bytes.set(raw,40);
    module.HEAPU8.set(bytes,ticketPointer);
  };
  const sourceBoundary=()=>{
    const bytes=new Uint8Array(176),data=new DataView(bytes.buffer);
    [1,176,500,1,1,0,1,2].forEach((value,field)=>data.setUint32(field*4,value,true));
    data.setBigUint64(32,8800n,true);
    const raw=Uint8Array.from({length:44},(_,offset)=>(offset*9+5)&255);
    bytes.set(raw,40);bytes.set(raw,84);bytes.set(raw,128);
    module.HEAPU8.set(bytes,boundaryPointer);
  };
  const request=command=>{
    f.calls.push(command);state.pending=command;state.requestSequence++;writeStatus();
    queueMicrotask(()=>{
      state.pending=0;state.completeSequence=state.requestSequence;state.lastCommand=command;
      if(command===failCommand){state.firstFailure=9;writeStatus();return;}
      if(command===1){state.phase=1;state.inputReady=0;}
      else if(command===18){state.phase=3;state.incarnation=1;state.currentFrame=state.preFrame;
        state.forwardSteps=0;state.replaySteps=0;state.restoreCount=0;state.inputReady=0;}
      else if(command===20){state.phase=23;state.currentFrame=state.confirmedFrame;
        state.sealedEnd=state.confirmedFrame;state.inputReady=1;
        state.descriptor.finishRequestSequence=state.requestSequence;}
      else if(command===21){state.phase=25;}
      else if(command===22){state.phase=26;}
      else if(command===23){state.expectedFrame=state.confirmedFrame;state.preFrame=state.confirmedFrame;
        state.currentFrame=state.confirmedFrame;state.sealedEnd=0;state.phase=0;state.incarnation=0;
        state.restoreCount=0;state.endpointMask=0;state.forwardSteps=0;state.replaySteps=0;
        state.pollSerial=0n;state.inputReady=0;state.descriptor.rebaseRequestSequence=state.requestSequence;}
      else if(command===24){
        const origin=rows[state.descriptor.suffixCursor];
        if(origin){state.latestTicketId=origin.ticketId;state.latestRaw=origin.raw.slice();
          state.currentFrame=state.confirmedFrame+state.descriptor.suffixCursor;
          state.pollSerial=BigInt(origin.pollSerial);state.inputReady=1;
          writeTicket(origin.ticketId,origin.sourceFrame,state.incarnation,origin.pollSerial,origin.raw);}
      }else if(command===12){
        state.latestTicketId=state.requestSequence;state.latestRaw=Uint8Array.from({length:44},(_,i)=>(i+3)&255);
        state.pollSerial=9007199254741111n+BigInt(state.requestSequence);
        state.inputReady=1;state.phase=state.phase===4?4:21;
        writeTicket(state.latestTicketId,state.currentFrame,state.incarnation,state.pollSerial,state.latestRaw);
      }else if(command===13){
        state.currentFrame++;state.phase=21;state.forwardSteps++;state.inputReady=1;
        state.descriptor.suffixCursor++;
      }
      if(command===10)state.ownerState=2;
      writeStatus();
    });
    return 1;
  };
  module[prefix+'take_over']=(frame,local,remote)=>{
    state.ownerState=1;state.expectedFrame=frame;state.preFrame=frame;state.currentFrame=frame;
    state.localPort=local;state.remotePort=remote;writeStatus();return 1;
  };
  module[prefix+'request']=request;
  module[prefix+'request_prefix_finish']=frame=>{state.confirmedFrame=frame;return request(20);};
  module[prefix+'request_prefix_rebase']=frame=>{state.confirmedFrame=frame;return request(23);};
  module[prefix+'episode_enable']=()=>1;module[prefix+'arena_enable']=()=>1;
  module[prefix+'cpu_arena_endpoint_data']=()=>19000;
  module[prefix+'cpu_arena_endpoint_bytes']=()=>144;
  module[prefix+'identity_json']=()=>writeString(JSON.stringify({...host,source_frame:0,
    current_rng:'fedcba98',selection:{stage:2}}));
  module[prefix+'rebase_identity_json']=()=>writeString(JSON.stringify({...host,source_frame:2,
    current_rng:'01234567',selection:{stage:2}}));
  module[prefix+'prefix_handoff_json']=()=>writeString(JSON.stringify(state.descriptor));
  module[prefix+'prefix_origin_row_bytes']=()=>176;
  module[prefix+'prefix_origin_row_data']=ordinal=>{
    const row=rows[ordinal];if(!row)return 0;
    module.HEAPU8.set(row.bytes,originPointer);return originPointer;
  };
  module[prefix+'input_row_bytes']=()=>176;
  module[prefix+'input_row_data']=ordinal=>{sourceBoundary();return ordinal===1?boundaryPointer:0;};
  module[prefix+'ticket_bytes']=()=>{ticketBytesReads++;return 88;};
  module[prefix+'ticket_data']=()=>{ticketDataReads++;return ticketPointer;};
  module[prefix+'request_episode_forward']=(ticketId,pointer,size)=>{
    if(size!==44||ticketId!==state.latestTicketId)return 0;
    state.forwarded.push(module.HEAPU8.slice(pointer,pointer+size));return request(13);
  };
  module[prefix+'checksum_data']=()=>checksumPointer;
  module[prefix+'checksum_bytes']=()=>64;
  const checksum=new Uint8Array(64),checksumView=new DataView(checksum.buffer);
  checksumView.setUint32(0,1,true);checksumView.setUint32(4,3,true);checksumView.setUint32(12,2,true);
  module.HEAPU8.set(checksum,checksumPointer);
  writeStatus();
  return {...f,state,rows,originPointer,writeStatus,setPhase:(phase,extra={})=>{
    Object.assign(state,{phase},extra);writeStatus();
  },mutateHandoff:fn=>{fn(state.descriptor);},writeString,
  ticketReads:()=>({bytes:ticketBytesReads,data:ticketDataReads})};
}

async function preparePrefixAdoption({failCommand=0}={}){
  const f=confirmedPrefixFixture({failCommand}),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  owner.identity(host);owner.enableArenaHistory();
  await owner.command('capture');
  f.setPhase(11,{expectedFrame:0,preFrame:0,currentFrame:2,sealedEnd:4,incarnation:1,
    restoreCount:1,forwardSteps:4,replaySteps:2,pollSerial:990n,inputReady:1});
  await owner.command('finishPrefix',2);
  f.setPhase(24);
  const checksum=owner.checksum();
  const origins=[owner.prefixOrigin(0),owner.prefixOrigin(1)];
  await owner.command('closePrefix');await owner.command('retirePrefix');
  const rebase=await owner.command('rebaseConfirmedPrefix',2);
  await owner.command('capture');await owner.command('beginEpisode');
  f.setPhase(4);
  return {f,owner,checksum,origins,rebase};
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
  assert.throws(()=>owner.enableEpisode(),/episode_enable is unavailable/);
  assert.throws(()=>owner.enableArenaHistory(),/episode_enable is unavailable/);
  const fresh=fixture({agreedStartExports:false});
  assert.throws(()=>createRuntimeMatchHistoryController(fresh.module).enableArenaHistory(),/arena_enable is unavailable/);
  assert.deepEqual(f.allocations.map(row=>row.size),[11]);
  assert.deepEqual(f.frees,[]);
});

test('episode mode uses preallocated 44-byte plans, exact poll tickets and detached receipts',async()=>{
  const f=episodeFixture(),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  assert.equal(owner.enableEpisode(),true);
  assert.deepEqual(f.allocations.map(row=>row.size),[11,44]);
  assert.equal(f.episodeState().enableCalls,1);
  await owner.command('capture');
  const allocationsAfterCapture=f.allocations.length;
  await owner.command('beginEpisode');
  f.put(56,4); // The native Armed PRE endpoint has been copied.
  await owner.command('pollTicket');
  const ticket=owner.ticket();
  assert.equal(ticket.ticketId,3);
  assert.equal(ticket.sourceFrame,0);
  assert.equal(ticket.incarnation,1);
  assert.equal(ticket.pollSerial,'9007199254740996');
  const pending=owner.receipt(0);
  assert.equal(pending.ordinal,0);
  assert.equal(pending.state,0);
  assert.deepEqual(pending.raw,ticket.raw);
  assert(pending.raw.some(Boolean),'state 0 retains actual nonzero PAD before forward');
  assert.deepEqual(pending.forward,new Uint8Array(44));
  assert.deepEqual(pending.selected,new Uint8Array(44));
  const plan=ticket.raw.slice();plan[11]^=0x20;
  await owner.forwardTicket(ticket.ticketId,plan);
  assert.deepEqual(f.episodeState().lastPlan,plan,'the controller forwards all four exact PAD rows without rewriting them');
  assert.equal(f.allocations.length,allocationsAfterCapture,'episode commands allocate no Wasm scratch after PRE capture');
  const receipt=owner.receipt(0);
  assert.equal(receipt.ordinal,0);
  assert.equal(receipt.incarnation,1,'receipt preserves its original poll incarnation');
  assert.equal(receipt.state,1);
  const rawBefore=receipt.rawBytes.slice();
  f.module.HEAPU8.fill(0,f.episodeState().rowPointer,f.episodeState().rowPointer+176);
  assert.deepEqual(receipt.rawBytes,rawBefore,'the row is copied before any later native call can mutate borrowed storage');
  assert.throws(()=>owner.remotePad(new Uint8Array(11)),/disabled in episode mode/);
  await assert.rejects(()=>owner.command('forward'),/disabled in episode mode/);
  assert.deepEqual(f.calls,[1,18,12,13]);
  await owner.command('cleanup');
  assert.deepEqual(f.frees.slice(-2),[f.allocations[1].pointer,f.allocations[0].pointer]);
});

test('episode receipt decoders preserve uint64 serials and reject malformed ticket and row bindings',()=>{
  const ticket=new Uint8Array(88),ticketView=new DataView(ticket.buffer);
  ticketView.setUint32(0,1,true);ticketView.setUint32(4,88,true);ticketView.setUint32(8,7,true);
  ticketView.setUint32(12,176,true);ticketView.setUint32(16,2,true);ticketView.setUint32(20,0,true);
  ticketView.setUint32(24,1,true);ticketView.setBigUint64(32,9007199254740993n,true);
  ticket.set(Uint8Array.from({length:44},(_,index)=>index),40);
  const decodedTicket=decodeRuntimeMatchHistoryPollTicket(ticket);
  ticket.fill(0);
  assert.equal(decodedTicket.pollSerial,'9007199254740993');
  assert.equal(decodedTicket.ticketId,7);
  assert.equal(decodedTicket.raw[0],0,'ticket PAD rows are detached copies');
  const malformedTicket=decodedTicket.rawBytes.slice();new DataView(malformedTicket.buffer).setUint32(28,1,true);
  assert.throws(()=>decodeRuntimeMatchHistoryPollTicket(malformedTicket),/metadata violation/);
  const zeroTicketSerial=decodedTicket.rawBytes.slice();
  new DataView(zeroTicketSerial.buffer).setBigUint64(32,0n,true);
  assert.throws(()=>decodeRuntimeMatchHistoryPollTicket(zeroTicketSerial),/metadata violation/);

  const row=new Uint8Array(176),view=new DataView(row.buffer);
  view.setUint32(0,1,true);view.setUint32(4,176,true);view.setUint32(8,7,true);
  view.setUint32(12,176,true);view.setUint32(16,1,true);view.setUint32(20,0,true);
  view.setUint32(24,1,true);view.setUint32(28,2,true);view.setBigUint64(32,9007199254740993n,true);
  const raw=Uint8Array.from({length:44},(_,index)=>(index*13)&255),selected=raw.slice();
  selected[11]^=0x80;row.set(raw,40);row.set(selected,84);row.set(selected,128);
  const decodedRow=decodeRuntimeMatchHistoryEpisodeInputRow(row);
  assert.equal(decodedRow.incarnation,1);assert.equal(decodedRow.pollSerial,'9007199254740993');
  const zeroRowSerial=row.slice();
  new DataView(zeroRowSerial.buffer).setBigUint64(32,0n,true);
  assert.throws(()=>decodeRuntimeMatchHistoryEpisodeInputRow(zeroRowSerial),/metadata violation/);
  const changedUnused=row.slice();changedUnused[84+22]^=1;
  assert.throws(()=>decodeRuntimeMatchHistoryEpisodeInputRow(changedUnused),/local or unused PAD port/);
  const nonzeroEmpty=new Uint8Array(176),emptyView=new DataView(nonzeroEmpty.buffer);
  [1,176,7,176,1,0,1,0].forEach((value,index)=>emptyView.setUint32(index*4,value,true));
  emptyView.setBigUint64(32,1n,true);nonzeroEmpty[84]=1;
  assert.throws(()=>decodeRuntimeMatchHistoryEpisodeInputRow(nonzeroEmpty),/Unconsumed episode ticket/);
});

test('confirmed-prefix handoff decoder preserves bounded source identity and rejects stale metadata',()=>{
  const handoff={schema:'melee-web-runtime-prefix-handoff-v1',oldPreFrame:0,confirmedFrame:2,
    selectedEndFrame:4,oldIncarnation:1,finishRequestSequence:7,rebaseRequestSequence:0,
    suffixCount:2,suffixCursor:1,localPort:0,remotePort:1,worldGeneration:'41',contextEpoch:'9',
    ownerStorage:'0000000000000001',ownerContext:'0000000000000002',
    ownerFlow:'0000000000000003',ownerAudio:'0000000000000004'};
  assert.deepEqual(decodeRuntimeMatchHistoryPrefixHandoff(JSON.stringify(handoff)),handoff);
  const reject=(label,mutate,pattern=/Runtime prefix handoff/)=>{
    const value=structuredClone(handoff);mutate(value);
    assert.throws(()=>decodeRuntimeMatchHistoryPrefixHandoff(JSON.stringify(value)),pattern,label);
  };
  reject('extra fields are refused',value=>{value.reserved=0;},/exact field set/);
  reject('confirmed frame must be strictly inside the selected interval',value=>{value.confirmedFrame=4;});
  reject('suffix count must describe the untouched tail',value=>{value.suffixCount=1;});
  reject('rebase request must follow finish',value=>{value.rebaseRequestSequence=7;});
  reject('uint64 metadata stays canonical decimal',value=>{value.worldGeneration='041';},/canonical uint64 decimal/);
  reject('owner tokens stay fixed-width lowercase hex',value=>{value.ownerFlow='00000000000000AF';},/owner token/);
  reject('zero owner tokens are invalid',value=>{value.ownerAudio='0000000000000000';},/owner token/);
});

test('confirmed-prefix controller copies the actual C checksum and replays suffix tickets without polling',async()=>{
  const {f,owner,checksum,origins,rebase}=await preparePrefixAdoption();
  const record=new DataView(checksum.buffer);
  assert.deepEqual([record.getUint32(0,true),record.getUint32(4,true),record.getUint32(12,true)],[1,3,2]);
  assert.deepEqual([rebase.status.expectedFrame,rebase.status.metadata.preFrame,
    rebase.status.completeSequence,rebase.status.lastCompletedCommand],[2,2,5,23]);
  assert.deepEqual(owner.prefixHandoff(),{
    schema:'melee-web-runtime-prefix-handoff-v1',oldPreFrame:0,confirmedFrame:2,
    selectedEndFrame:4,oldIncarnation:1,finishRequestSequence:2,rebaseRequestSequence:5,
    suffixCount:2,suffixCursor:0,localPort:0,remotePort:1,worldGeneration:'41',contextEpoch:'9',
    ownerStorage:'0000000000000001',ownerContext:'0000000000000002',
    ownerFlow:'0000000000000003',ownerAudio:'0000000000000004'});
  f.module.HEAPU8.fill(0,f.originPointer,f.originPointer+176);
  assert.equal(origins[0].sourceFrame,2);
  assert.deepEqual(origins[0].rawBytes.slice(40,84),origins[0].raw,
    'the origin accessor returned an owned copy of the immutable 176-byte row');
  assert.deepEqual(f.calls,[1,20,21,22,23,1,18]);
  let ticketReads=f.ticketReads();
  for(const origin of origins){
    await owner.command('adoptNextPrefixTicket');
    const ticket=owner.adoptedTicket(origin);
    assert.equal(ticket.ticketId,origin.ticketId);
    assert.notEqual(ticket.ticketId,f.state.requestSequence,
      'the preserved source ticket ID is not the new capsule request sequence');
    assert.equal(ticket.sourceFrame,origin.sourceFrame);
    assert.equal(ticket.incarnation,1,'the adopted ticket binds to the fresh capsule incarnation');
    assert.equal(ticket.pollSerial,origin.pollSerial);
    assert.deepEqual(ticket.raw,origin.raw);
    const reads=f.ticketReads();
    assert.deepEqual({bytes:reads.bytes-ticketReads.bytes,data:reads.data-ticketReads.data},{bytes:1,data:1},
      'one cmd24 completion yields one copied 88-byte ticket');
    ticketReads=reads;
    const plan=ticket.raw.slice();plan[11]^=0x40;
    await owner.forwardTicket(ticket.ticketId,plan);
    assert.deepEqual(f.state.forwarded.at(-1),plan);
  }
  assert.equal(owner.prefixHandoff().suffixCursor,2);
  await owner.command('pollTicket');
  assert.equal(owner.ticket().sourceFrame,4,'fresh polling resumes only after the preserved suffix ends');
  assert.deepEqual(f.calls,[1,20,21,22,23,1,18,24,13,24,13,12]);
  for(const command of [20,21,22,23,24,13,12])
    assert.equal(f.calls.filter(value=>value===command).length,command===13?2:command===24?2:1,
      `native command ${command} is dispatched exactly once per suffix boundary`);
});

test('confirmed-prefix controller rejects stale handoffs, malformed origins, and mismatched adopted tickets',async()=>{
  const stale=await preparePrefixAdoption();
  stale.f.mutateHandoff(value=>{value.finishRequestSequence=100;value.rebaseRequestSequence=101;});
  assert.throws(()=>stale.owner.prefixHandoff(),/request sequence is ahead/);

  const malformed=await preparePrefixAdoption();
  malformed.f.rows[0].bytes[0]=9;
  assert.throws(()=>malformed.owner.prefixOrigin(0),/metadata violation/);

  const mismatch=await preparePrefixAdoption();
  await mismatch.owner.command('adoptNextPrefixTicket');
  const wrong={...mismatch.origins[0],rawBytes:mismatch.origins[0].rawBytes.slice()};
  wrong.rawBytes[40]^=1;
  const reads=mismatch.f.ticketReads();
  assert.throws(()=>mismatch.owner.adoptedTicket(wrong),/exact immutable origin row/);
  assert.deepEqual(mismatch.f.ticketReads(),reads,
    'a mismatched caller row is rejected before reading adopted ticket storage');
});

test('confirmed-prefix generic close refusal is dispatched once and remains terminal',async()=>{
  const f=confirmedPrefixFixture({failCommand:21}),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});owner.identity(host);owner.enableArenaHistory();
  await owner.command('capture');
  f.setPhase(11,{expectedFrame:0,preFrame:0,currentFrame:2,sealedEnd:4,incarnation:1,
    restoreCount:1,forwardSteps:4,replaySteps:2,pollSerial:990n,inputReady:1});
  await owner.command('finishPrefix',2);f.setPhase(24);
  await assert.rejects(owner.command('closePrefix'),/Native history refused/);
  assert.deepEqual(f.calls,[1,20,21]);
  await assert.rejects(owner.command('retirePrefix'),/Native history refused/);
  assert.deepEqual(f.calls,[1,20,21],'the failed close is never retried and later work is blocked');
});

test('confirmed-prefix ticket adoption refusal does not read or repeat the native ticket',async()=>{
  const {f,owner,origins}=await preparePrefixAdoption({failCommand:24});
  await assert.rejects(owner.command('adoptNextPrefixTicket'),/Native history refused/);
  assert.deepEqual(f.calls,[1,20,21,22,23,1,18,24]);
  assert.deepEqual(f.ticketReads(),{bytes:0,data:0});
  assert.throws(()=>owner.adoptedTicket(origins[0]),/Native history refused/);
  assert.deepEqual(f.calls,[1,20,21,22,23,1,18,24]);
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

function checksumFixture(phase=6,pre=0){
  const f=fixture();
  const owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:pre,localPort:0,remotePort:1});
  f.put(56,phase); // history.phase
  f.put(64,pre); // history.preFrame
  f.put(68,pre+1); // history.currentFrame
  f.put(72,pre+1); // history.sealedEnd
  f.put(88,1); // history.forwardSteps
  f.put(92,phase===13?1:0); // history.replaySteps
  f.put(96,17); // history.pollSerial, low word
  f.put(112,1); // inputReady
  const pointer=16384;
  f.view.setUint32(pointer,pre,true);
  f.view.setUint32(pointer+4,3,true);
  f.view.setUint32(pointer+12,pre+1,true);
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

test('checksum binds the second window to match input ordinal one and source frame two',()=>{
  const f=checksumFixture(13,1);
  const bytes=f.owner.checksum(),record=new DataView(bytes.buffer);
  assert.equal(record.getUint32(0,true),1);
  assert.equal(record.getUint32(12,true),2);
  f.view.setUint32(f.pointer,0,true);
  assert.throws(()=>f.owner.checksum(),/checksum tick, match scene or source frame differs/);
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

test('confirmed rebase acknowledges retired END and opens the next capture without resetting sequence',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  const identity=source_frame=>JSON.stringify({...host,source_frame,
    current_rng:source_frame?'01234567':'fedcba98',selection:{stage:2}});
  const stringPointer=2048;
  const publishIdentity=frame=>{
    f.module.HEAPU8.set(new TextEncoder().encode(identity(frame)+'\0'),stringPointer);
    return stringPointer;
  };
  f.module[prefix+'identity_json']=()=>publishIdentity(0);
  f.module[prefix+'rebase_identity_json']=()=>publishIdentity(1);
  let requestedEnd;
  f.module[prefix+'request_rebase']=end=>{
    requestedEnd=end;
    f.put(32,end);f.put(56,0);f.put(64,end);f.put(68,end);f.put(72,0);
    f.put(88,0);f.put(104,40);f.put(112,0);
    return f.module[prefix+'request'](11);
  };
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  owner.identity(host);
  await owner.command('capture');
  assert.throws(()=>owner.identity(host),/before capture/);
  f.put(56,18);f.put(68,1);f.put(72,1);f.put(88,1);f.put(108,40);f.put(112,1);
  const fresh=await owner.command('rebase',1);
  assert.equal(requestedEnd,1);
  assert.equal(fresh.identity,identity(1));
  assert.deepEqual([fresh.status.expectedFrame,fresh.status.metadata.currentFrame,
    fresh.status.completeSequence,fresh.status.lastCompletedCommand],[1,1,2,11]);
  assert.equal((await owner.command('capture')).completeSequence,3);
  assert.deepEqual(f.calls,[1,11,1]);
  assert.equal(owner.journal()[1].identity,identity(1));
  await owner.command('cleanup');
});

test('confirmed rebase rejects an unretired capsule before asking the native owner',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  await owner.command('capture');
  await assert.rejects(owner.command('rebase',1),/actual retired END/);
  assert.deepEqual(f.calls,[1]);
  await assert.rejects(owner.command('capture'),/actual retired END/);
  await owner.command('cleanup');
  assert.deepEqual(f.calls,[1,10]);
});


test('arena history requires explicit native enablement before capture and keeps legacy mode separate',async()=>{
  const f=episodeFixture(),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  let arenaCalls=0;
  f.module[prefix+'arena_enable']=()=>{arenaCalls++;return f.module[prefix+'episode_enable']();};
  f.module[prefix+'cpu_arena_endpoint_data']=()=>0;
  f.module[prefix+'cpu_arena_endpoint_bytes']=()=>144;
  assert.equal(owner.enableArenaHistory(),true);
  assert.equal(arenaCalls,1);
  assert.throws(()=>owner.enableArenaHistory(),/already enabled/);
  assert.throws(()=>owner.enableEpisode(),/already enabled/);
  assert.throws(()=>owner.endpoint({arenaHistoryEndpoint:false}),/requires its typed arena/);
  assert.throws(()=>owner.endpoint({particleCellEndpoint:false}),/requires its typed arena/);
  await owner.command('capture');
  assert.throws(()=>owner.enableArenaHistory(),/before PRE capture/);
  const g=episodeFixture(),legacy=createRuntimeMatchHistoryController(g.module);
  assert.throws(()=>legacy.endpoint({arenaHistoryEndpoint:true}),/was not enabled/);
});

test('owned idle preparation retains renderer receipt without changing status polling',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  const calls=[];
  f.put(116,1);
  f.module[prefix+'renderer_prepare_json']=command=>{
    calls.push(command);
    f.put(116,0);
    return 2048;
  };
  const utf8=f.module.UTF8ToString;
  f.module.UTF8ToString=pointer=>pointer===2048?JSON.stringify({schema:1,command:5,action:1,
    before:{cacheStatus:1},after:{cacheStatus:0}}):utf8(pointer);
  assert.equal(owner.status().gpuPending,1);
  assert.deepEqual(calls,[]);
  assert.equal((await owner.command('idle')).lastCompletedCommand,5);
  assert.deepEqual(calls,[5]);assert.deepEqual(f.calls,[5]);
  assert.equal(owner.journal()[0].rendererPreparation.flush.after.cacheStatus,0);
});

test('alternate renderer work retains its receipt on the original idle and cleanup deadlines',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module,{commandTimeoutMs:30});
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  f.put(116,1);
  let lastCommand=0;
  f.module[prefix+'renderer_prepare_json']=command=>{lastCommand=command;return 2048;};
  f.module.UTF8ToString=()=>JSON.stringify({schema:1,command:lastCommand,action:0,
    before:{pendingStaging:1},after:{pendingStaging:1}});
  await assert.rejects(owner.command('idle'),/idle exceeded its 30 ms deadline.*pendingStaging/);
  await assert.rejects(owner.command('cleanup'),/cleanup exceeded its 30 ms deadline.*pendingStaging/);
  assert.deepEqual(f.calls,[]);
  assert.equal(owner.journal()[1].rendererPreparation.latest.command,10);
});

test('arena controller rebases a four-tick retired END and keeps episode mode for the next capture',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  const identity=source_frame=>JSON.stringify({...host,source_frame,
    current_rng:source_frame?'01234567':'fedcba98',selection:{stage:2}});
  const publish=frame=>{
    f.module.HEAPU8.set(new TextEncoder().encode(identity(frame)+'\0'),2048);
    return 2048;
  };
  f.module[prefix+'identity_json']=()=>publish(0);
  f.module[prefix+'rebase_identity_json']=()=>publish(4);
  f.module[prefix+'arena_enable']=()=>1;
  f.module[prefix+'cpu_arena_endpoint_data']=()=>0;
  f.module[prefix+'cpu_arena_endpoint_bytes']=()=>144;
  f.module[prefix+'request_rebase']=end=>{
    assert.equal(end,4);
    f.put(32,end);f.put(56,0);f.put(64,end);f.put(68,end);f.put(72,0);
    f.put(76,0);f.put(80,0);f.put(84,0);f.put(88,0);f.put(92,0);
    f.view.setBigUint64(352,0n,true);
    f.put(104,40);f.put(112,0);
    return f.module[prefix+'request'](11);
  };
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  owner.identity(host);
  owner.enableArenaHistory();
  await owner.command('capture');
  f.put(56,18);f.put(68,4);f.put(72,4);f.put(76,3);f.put(80,2);
  f.put(88,4);f.put(92,4);f.put(108,40);f.put(112,1);
  f.view.setBigUint64(352,465n,true);
  const fresh=await owner.command('rebase',4);
  assert.equal(fresh.identity,identity(4));
  assert.deepEqual([fresh.status.expectedFrame,fresh.status.metadata.currentFrame,
    fresh.status.completeSequence,fresh.status.metadata.pollSerial],[4,4,2,'0']);
  assert.equal((await owner.command('capture')).completeSequence,3);
  await assert.rejects(owner.command('forward'),/Legacy one-window/);
  assert.deepEqual(f.calls,[1,11,1]);
});

test('recording present and idle preserve the actual forward source boundary',async()=>{
  const f=fixture(),owner=createRuntimeMatchHistoryController(f.module);
  f.module[prefix+'arena_enable']=()=>1;
  f.module[prefix+'cpu_arena_endpoint_data']=()=>0;
  f.module[prefix+'cpu_arena_endpoint_bytes']=()=>144;
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  await assert.rejects(owner.command('recordingPresent'),/explicit arena history/);
  owner.enableArenaHistory();
  await owner.command('capture');
  f.put(56,21);f.put(68,1);f.put(76,1);f.put(88,1);f.put(108,20);f.put(112,1);
  f.view.setBigUint64(352,9007199254740993n,true);
  const request=f.module[prefix+'request'];
  f.module[prefix+'request']=command=>{
    const result=request(command);
    queueMicrotask(()=>{
      if(command===19){f.put(56,22);f.put(116,1);}
      if(command===5)f.put(56,21);
    });
    return result;
  };
  const submitted=await owner.command('recordingPresent');
  assert.equal(submitted.metadata.phase,22);
  const receipt={schema:1,command:19,requestSequence:submitted.completeSequence,
    sourceFrameBefore:1,sourceFrameAfter:1,priorSubmission:'9007199254741001',
    submission:'9007199254741002',presentationSerial:'1'};
  const publish=value=>f.module.HEAPU8.set(new TextEncoder().encode(JSON.stringify(value)+'\0'),2048);
  f.module[prefix+'presentation_json']=()=>2048;
  publish(receipt);
  const copied=owner.presentation();
  assert.deepEqual(copied,receipt);
  publish({...receipt,sourceFrameAfter:2});
  assert.throws(()=>owner.presentation(),/differs from completed native draw/);
  assert.deepEqual(copied,receipt);
  publish({...receipt,submission:receipt.priorSubmission});
  assert.throws(()=>owner.presentation(),/submission did not advance/);
  publish({...receipt,presentationSerial:'18446744073709551616'});
  assert.throws(()=>owner.presentation(),/outside uint64 bounds/);
  f.put(116,0);
  const idle=await owner.command('idle');
  assert.equal(idle.metadata.phase,21);
  assert.equal(idle.metadata.currentFrame,1);
  assert.equal(idle.metadata.pollSerial,'9007199254740993');
  assert.throws(()=>owner.presentation(),/completed draw before idle/);
  for(const phase of [7,14]){
    f.put(56,phase);f.put(20,4);
    const sealed={...receipt,command:4,requestSequence:idle.completeSequence};
    publish(sealed);
    assert.deepEqual(owner.presentation(),sealed);
  }
  assert.deepEqual(f.calls,[1,19,5]);
});

function inlineFixture(options){
  const f=episodeFixture();
  f.module[prefix+'arena_enable']=()=>1;
  f.module[prefix+'cpu_arena_endpoint_data']=()=>0;
  f.module[prefix+'cpu_arena_endpoint_bytes']=()=>144;
  let enableCalls=0;
  f.module[prefix+'inline_dispatch_enable']=()=>{enableCalls++;return 1;};
  const owner=createRuntimeMatchHistoryController(f.module,options);
  owner.takeOver({sourceFrame:0,localPort:0,remotePort:1});
  return {...f,owner,inlineEnableCalls:()=>enableCalls};
}

test('inline forward retains the plan and forbids native reentry while Asyncify is suspended',async()=>{
  const f=inlineFixture(),{owner,module}=f;
  assert.throws(()=>owner.enableInlineDispatch(),/explicit arena history/);
  owner.enableArenaHistory();
  assert.throws(()=>owner.enableInlineDispatch(),/requires Asyncify ccall/);
  let resolveForward,startedForward;
  const forwardStarted=new Promise(resolve=>{startedForward=resolve;});
  let statusCalls=0,callInFlight=false;
  const status=module[prefix+'status'];
  module[prefix+'status']=()=>{
    assert.equal(callInFlight,false,'status entered native during Asyncify');
    statusCalls++;
    return status();
  };
  module.ccall=async(name,result,types,args,options)=>{
    assert.equal(result,'number');
    assert.deepEqual(types,args.map(()=>'number'));
    assert.deepEqual(options,{async:true});
    if(name===prefix.slice(1)+'request_episode_forward'){
      callInFlight=true;
      startedForward(args);
      await new Promise(resolve=>{resolveForward=resolve;});
      callInFlight=false;
    }
    return module['_'+name](...args);
  };
  assert.equal(owner.enableInlineDispatch(),true);
  assert.equal(f.inlineEnableCalls(),1);
  assert.throws(()=>owner.enableInlineDispatch(),/enabled once/);
  await owner.command('capture');
  await owner.command('beginEpisode');
  f.put(56,4); // Armed PRE is available after its endpoint copy.
  await owner.command('pollTicket');
  const ticket=owner.ticket(),plan=ticket.raw.slice();
  plan[12]^=1;
  const expected=plan.slice();
  const task=owner.forwardTicket(ticket.ticketId,plan);
  const [id,pointer,size]=await forwardStarted;
  assert.equal(id,ticket.ticketId);
  assert.equal(size,44);
  const reads=statusCalls;
  plan.fill(0);
  assert.throws(()=>owner.status(),/Asyncify call is in progress/);
  assert.throws(()=>owner.ticket(),/command is in progress/);
  await assert.rejects(owner.command('cleanup'),/cleanup owner unavailable/);
  await assert.rejects(owner.command('pollTicket'),/command is in progress/);
  assert.equal(statusCalls,reads);
  assert.deepEqual(f.frees,[]);
  assert.deepEqual(module.HEAPU8.slice(pointer,pointer+size),expected);
  resolveForward();
  assert.equal((await task).metadata.currentFrame,1);
  assert.deepEqual(owner.receipt(0).forward,expected);
  await owner.command('cleanup');
  assert.equal(f.frees.length,2);
});

test('inline idle waits for yielded renderer preparation and rejects a late return without dispatch',async(t)=>{
  const f=inlineFixture({commandTimeoutMs:30}),{owner,module}=f;
  const clock=t.mock.method(performance,'now',()=>now);
  let now=0,resolvePreparation,startedPreparation;
  const preparationStarted=new Promise(resolve=>{startedPreparation=resolve;});
  module[prefix+'renderer_prepare_json']=()=>assert.fail('direct renderer preparation call');
  module.ccall=async(name,result,types,args,options)=>{
    assert.deepEqual(options,{async:true});
    if(name!==prefix.slice(1)+'renderer_prepare_json')return module['_'+name](...args);
    assert.deepEqual([result,types,args],['number',['number'],[5]]);
    startedPreparation();
    await new Promise(resolve=>{resolvePreparation=resolve;});
    f.put(116,0);
    return 2048;
  };
  const utf8=module.UTF8ToString;
  module.UTF8ToString=pointer=>pointer===2048?JSON.stringify({schema:1,command:5,action:1,
    before:{cacheStatus:1},after:{cacheStatus:0}}):utf8(pointer);
  owner.enableArenaHistory();
  owner.enableInlineDispatch();
  f.put(116,1);
  const idle=owner.command('idle');
  await preparationStarted;
  now=31;
  assert.throws(()=>owner.status(),/Asyncify call is in progress/);
  await assert.rejects(owner.command('cleanup'),/cleanup owner unavailable/);
  assert.deepEqual(f.calls,[]);
  resolvePreparation();
  await assert.rejects(idle,/idle exceeded its 30 ms deadline/);
  assert.deepEqual(f.calls,[]);
  assert.equal(owner.journal()[0].rendererPreparation.flush.after.cacheStatus,0);
  await owner.command('cleanup');
  assert.deepEqual(f.calls,[10]);
  clock.mock.restore();
});

test('inline request refusal and Asyncify rejection preserve first failure and allow owned cleanup',async()=>{
  for(const failure of ['refuse','reject','native']){
    const f=inlineFixture(),{owner,module}=f;
    const original=Error('Asyncify rejected after settling');
    module.ccall=async(name,_type,_types,args)=>{
      if(args[0]===1){
        if(failure==='refuse')return 0;
        if(failure==='reject')throw original;
        f.put(12,9);
      }
      return module['_'+name](...args);
    };
    owner.enableArenaHistory();
    owner.enableInlineDispatch();
    const expected=failure==='reject'?original:/Native history .*refused: synthetic refusal/;
    await assert.rejects(owner.command('capture'),expected);
    await assert.rejects(owner.command('capture'),expected);
    assert.equal((await owner.command('cleanup')).ownerState,2);
  }
});

test('inline dispatch does not accept a completed command after its deadline',async(t)=>{
  const f=inlineFixture({commandTimeoutMs:30}),{owner,module}=f;
  let now=0;
  t.mock.method(performance,'now',()=>now);
  module.ccall=async(name,_type,_types,args)=>{
    const result=module['_'+name](...args);
    if(args[0]===1)now=31;
    return result;
  };
  owner.enableArenaHistory();
  owner.enableInlineDispatch();
  await assert.rejects(owner.command('capture'),/capture exceeded its 30 ms deadline/);
  assert.equal(owner.status().lastCompletedCommand,1);
  assert.deepEqual(f.frees,[]);
  assert.equal((await owner.command('cleanup')).ownerState,2);
  assert.equal(f.frees.length,2);
});
