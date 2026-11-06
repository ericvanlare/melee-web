// SPDX-License-Identifier: GPL-3.0-or-later
// Private one-tick ordinary-runtime owner. Commands execute on the native
// owner's rAF callback; this transport neither polls PAD nor performs source work.
import {collectCpuEndpointInPage} from './tests/cpu_endpoint.mjs';

export const MATCH_HISTORY_COMMAND=Object.freeze({capture:1,begin:2,forward:3,present:4,
  idle:5,restore:6,correct:7,close:8,retire:9,cleanup:10,rebase:11,
  pollTicket:12,forwardTicket:13,seal:14,replayTicket:15,finishReplay:16,
  continue:17,beginEpisode:18});
const PREFIX='_melee_web_native_menu_match_history_';
const STATUS_BYTES=128,INPUT_BYTES=168,PAD_BYTES=11,FRAME_BYTES=44,TICKET_BYTES=88,INPUT_ROW_BYTES=176;
const START_STATE_MAX=6,START_PUBLICATION_BYTES=48,START_HELD_STATE=5,START_PREFIX_MAX=1024;
const START_PREFIX_SCHEMA='melee-web-runtime-match-start-prefix-v1';
const START_PREFIX_INPUT='ordinary-local-four-port-pad-v1';
const UINT32_MAX=0xffffffff,UINT64_MAX=0xffffffffffffffffn;
const PUBLICATION_FIELDS=['schema','bytes','state','sourceFrame','sourceDraws','padPolicy',
  'firstUsePending','reserved','priorSubmission','sourceSubmission'];
const START_PREFIX_FIELDS=['schema','input_provenance','source_ticks','source_draws',
  'final_ready_tick','initial_resident_transfer_drains','final_resident_transfer_drains','rows','live_identity'];
const START_PREFIX_ROW_FIELDS=['poll_serial','tick_before','tick_after','source_frame_before',
  'source_frame_after','hud_ready_before','hud_ready_after','pad_hex','draw_submission'];
const START_HOST_FIELDS=['runtime_sha256','asset_identity_sha256','save_profile_sha256','profile_identity'];
const statusNames=['schema','bytes','ownerState','firstFailure','pendingCommand','lastCompletedCommand',
  'requestSequence','completeSequence','expectedFrame','localPort','remotePort','reserved'];
const metadataNames=['schema','bytes','phase','firstFailure','preFrame','currentFrame','sealedEnd',
  'incarnation','restoreCount','endpointMask','forwardSteps','replaySteps'];
const u32=(value,label)=>{
  if(!Number.isInteger(value)||value<0||value>0xffffffff)throw Error(`${label} is not uint32`);
  return value;
};
const fields=(data,names,start=0)=>Object.fromEntries(names.map((name,index)=>[name,data.getUint32(start+index*4,true)]));
const pad=value=>{
  if(!(value instanceof Uint8Array)||value.length!==11)throw Error('Remote PAD must be exactly 11 bytes');
  return value.slice();
};
const hasExactKeys=(value,names)=>value&&typeof value==='object'&&!Array.isArray(value)&&
  Object.keys(value).length===names.length&&names.every(name=>Object.hasOwn(value,name));
const canonicalU64=(value,label,{positive=false}={})=>{
  if(typeof value!=='string'||!/^(0|[1-9][0-9]*)$/.test(value))
    throw Error(`Runtime start prefix ${label} is not canonical uint64 decimal`);
  const parsed=BigInt(value);
  if(parsed>UINT64_MAX||(positive&&parsed===0n))
    throw Error(`Runtime start prefix ${label} is outside uint64 bounds`);
  return parsed;
};
const decodeStartPublicationBytes=bytes=>{
  if(!(bytes instanceof Uint8Array)||bytes.length!==START_PUBLICATION_BYTES)
    throw Error('Agreed-start publication receipt extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result={schema:data.getUint32(0,true),bytes:data.getUint32(4,true),
    state:data.getUint32(8,true),sourceFrame:data.getUint32(12,true),
    sourceDraws:data.getUint32(16,true),padPolicy:data.getUint32(20,true),
    firstUsePending:data.getUint32(24,true),reserved:data.getUint32(28,true),
    priorSubmission:data.getBigUint64(32,true).toString(),
    sourceSubmission:data.getBigUint64(40,true).toString(),raw:bytes.slice()};
  if(result.schema!==2||result.bytes!==START_PUBLICATION_BYTES||result.state!==START_HELD_STATE||
    result.sourceFrame!==0||result.sourceDraws<1||result.sourceDraws>START_PREFIX_MAX||
    result.padPolicy!==1||result.firstUsePending!==0||result.reserved!==0||
    BigInt(result.sourceSubmission)<=BigInt(result.priorSubmission))
    throw Error('Agreed-start publication receipt schema or held-frame owner violation');
  return result;
};
const sameJsonValue=(left,right)=>{
  if(Object.is(left,right))return true;
  if(left===null||right===null||typeof left!=='object'||typeof right!=='object'||
    Array.isArray(left)!==Array.isArray(right))return false;
  if(Array.isArray(left))return left.length===right.length&&left.every((value,index)=>sameJsonValue(value,right[index]));
  const leftKeys=Object.keys(left),rightKeys=Object.keys(right);
  return leftKeys.length===rightKeys.length&&leftKeys.every(key=>Object.hasOwn(right,key)&&
    sameJsonValue(left[key],right[key]));
};

// This prefix binds literal ordinary local four-port PAD rows to source ticks;
// poll serials here are not relay receipts, and PAD bytes do not prove actions.
export function validateRuntimeMatchStartPrefix(rawJSON,{publication,liveIdentity}={}){
  if(typeof rawJSON!=='string'||!rawJSON)throw Error('Runtime start prefix JSON is required');
  let parsed,identity;
  try{parsed=JSON.parse(rawJSON);}catch{throw Error('Runtime start prefix JSON parse refused');}
  try{
    if(typeof liveIdentity!=='string'||!liveIdentity)throw Error('missing live identity');
    identity=JSON.parse(liveIdentity);
  }catch{throw Error('Runtime start prefix live identity JSON is invalid');}
  if(!hasExactKeys(parsed,START_PREFIX_FIELDS)||parsed.schema!==START_PREFIX_SCHEMA||
    parsed.input_provenance!==START_PREFIX_INPUT)
    throw Error('Runtime start prefix schema or input provenance violation');
  if(!Number.isInteger(parsed.source_ticks)||parsed.source_ticks<1||parsed.source_ticks>START_PREFIX_MAX||
    parsed.source_draws!==parsed.source_ticks||!Array.isArray(parsed.rows)||
    parsed.rows.length!==parsed.source_ticks||!Number.isInteger(parsed.final_ready_tick)||
    parsed.final_ready_tick<0||parsed.final_ready_tick>UINT32_MAX||
    ![0,1].includes(parsed.initial_resident_transfer_drains)||
    ![0,1].includes(parsed.final_resident_transfer_drains))
    throw Error('Runtime start prefix count, ready tick, or resident-transfer drain bounds violation');
  if(!identity||typeof identity!=='object'||Array.isArray(identity)||
    !sameJsonValue(parsed.live_identity,identity))
    throw Error('Runtime start prefix embedded live identity differs from the held identity');
  if(!publication||!hasExactKeys(publication,PUBLICATION_FIELDS.concat('raw')))
    throw Error('Runtime start prefix publication receipt shape violation');
  const receipt=decodeStartPublicationBytes(publication.raw);
  if(PUBLICATION_FIELDS.some(name=>publication[name]!==receipt[name])||
    receipt.sourceDraws!==parsed.source_draws)
    throw Error('Runtime start prefix publication differs from its fixed 48-byte receipt');
  const priorSubmission=canonicalU64(receipt.priorSubmission,'publication prior submission');
  const finalSubmission=canonicalU64(receipt.sourceSubmission,'publication final submission',{positive:true});
  let previousPoll=0n,previousTick=null,previousSubmission=priorSubmission;
  for(let index=0;index<parsed.rows.length;index++){
    const row=parsed.rows[index];
    if(!hasExactKeys(row,START_PREFIX_ROW_FIELDS))
      throw Error(`Runtime start prefix row ${index} schema violation`);
    const poll=canonicalU64(row.poll_serial,`row ${index} poll serial`,{positive:true});
    const submission=canonicalU64(row.draw_submission,`row ${index} draw submission`,{positive:true});
    if(poll<previousPoll)
      throw Error(`Runtime start prefix row ${index} poll serial moved backwards`);
    if(!Number.isInteger(row.tick_before)||row.tick_before<0||row.tick_before>=UINT32_MAX||
      !Number.isInteger(row.tick_after)||row.tick_after!==row.tick_before+1||
      (previousTick!==null&&row.tick_before!==previousTick))
      throw Error(`Runtime start prefix row ${index} source tick is not a contiguous single step`);
    if(row.source_frame_before!==0||row.source_frame_after!==0||
      typeof row.hud_ready_before!=='boolean'||row.hud_ready_before!==false||
      typeof row.hud_ready_after!=='boolean'||row.hud_ready_after!==(index===parsed.rows.length-1))
      throw Error(`Runtime start prefix row ${index} left the frame-zero HUD-ready boundary`);
    if(typeof row.pad_hex!=='string'||!/^[0-9a-f]{88}$/.test(row.pad_hex))
      throw Error(`Runtime start prefix row ${index} PAD extent or lowercase hex violation`);
    if(submission<=previousSubmission||submission>finalSubmission)
      throw Error(`Runtime start prefix row ${index} draw submission is outside its publication interval`);
    previousPoll=poll;previousTick=row.tick_after;previousSubmission=submission;
  }
  if(parsed.final_ready_tick!==previousTick||previousSubmission!==finalSubmission)
    throw Error('Runtime start prefix final ready tick or publication submission differs from its last row');
  return parsed;
}

export function decodeRuntimeMatchHistoryStatus(bytes,{episodeMode=false}={}){
  if(!(bytes instanceof Uint8Array)||bytes.length!==STATUS_BYTES)throw Error('Runtime history status extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(data,statusNames),metadata=fields(data,metadataNames,48);
  metadata.pollSerial=data.getBigUint64(96,true).toString();
  metadata.preAudioPhase=data.getUint32(104,true);metadata.currentAudioPhase=data.getUint32(108,true);
  Object.assign(result,{metadata,...fields(data,['inputReady','gpuPending','endpointActive','rendererErrors'],112)});
  const stepLimit=episodeMode?7:1,restoreLimit=episodeMode?2:1,phaseLimit=episodeMode?21:20,
    commandLimit=episodeMode?18:11;
  if(result.schema!==1||result.bytes!==STATUS_BYTES||result.reserved!==0||result.ownerState>2||
    result.pendingCommand>commandLimit||result.lastCompletedCommand>commandLimit||result.completeSequence>result.requestSequence||
    result.inputReady>1||result.gpuPending>1||result.endpointActive>1||
    metadata.schema!==1||metadata.bytes!==64||metadata.phase>phaseLimit||metadata.restoreCount>restoreLimit||
    metadata.incarnation>(episodeMode?3:1)||metadata.forwardSteps>stepLimit||metadata.replaySteps>stepLimit||
    (!episodeMode&&metadata.phase===21))
    throw Error('Runtime history status schema or bounded-owner violation');
  return result;
}

export function decodeRuntimeMatchHistoryPollTicket(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.length!==TICKET_BYTES)
    throw Error('Runtime history poll ticket extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result={schema:data.getUint32(0,true),bytes:data.getUint32(4,true),
    ticketId:data.getUint32(8,true),sourceFrame:data.getUint32(12,true),
    incarnation:data.getUint32(16,true),localPort:data.getUint32(20,true),
    remotePort:data.getUint32(24,true),reserved0:data.getUint32(28,true),
    pollSerial:data.getBigUint64(32,true).toString(),raw:bytes.slice(40,84),
    reserved1:data.getUint32(84,true),rawBytes:bytes.slice()};
  if(result.schema!==1||result.bytes!==TICKET_BYTES||!result.ticketId||result.pollSerial==='0'||
    result.incarnation<1||result.incarnation>3||result.localPort>1||result.remotePort>1||
    result.localPort===result.remotePort||result.reserved0||result.reserved1)
    throw Error('Runtime history poll ticket metadata violation');
  return result;
}

export function decodeRuntimeMatchHistoryEpisodeInputRow(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.length!==INPUT_ROW_BYTES)
    throw Error('Runtime history episode input row extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result={schema:data.getUint32(0,true),bytes:data.getUint32(4,true),
    ticketId:data.getUint32(8,true),sourceFrame:data.getUint32(12,true),
    incarnation:data.getUint32(16,true),localPort:data.getUint32(20,true),
    remotePort:data.getUint32(24,true),state:data.getUint32(28,true),
    pollSerial:data.getBigUint64(32,true).toString(),raw:bytes.slice(40,84),
    forward:bytes.slice(84,128),selected:bytes.slice(128,172),reserved:data.getUint32(172,true),
    rawBytes:bytes.slice()};
  if(result.schema!==1||result.bytes!==INPUT_ROW_BYTES||!result.ticketId||result.pollSerial==='0'||
    result.incarnation<1||result.incarnation>3||result.localPort>1||result.remotePort>1||
    result.localPort===result.remotePort||result.state>2||result.reserved)
    throw Error('Runtime history episode input row metadata violation');
  if(result.state===0){
    if(result.forward.some(Boolean)||result.selected.some(Boolean))
      throw Error('Unconsumed episode ticket already contains applied PAD bytes');
    return result;
  }
  if(result.state===1&&!result.forward.every((byte,index)=>byte===result.selected[index]))
    throw Error('Initial episode selection differs from its forward PAD row');
  for(const port of [result.localPort,2,3]){
    const from=port*PAD_BYTES,to=from+PAD_BYTES;
    if(!result.raw.subarray(from,to).every((byte,index)=>byte===result.forward[from+index])||
       !result.raw.subarray(from,to).every((byte,index)=>byte===result.selected[from+index]))
      throw Error('Episode applied input changed a retained local or unused PAD port');
  }
  return result;
}

export function decodeRuntimeMatchHistoryInput(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.length!==INPUT_BYTES)throw Error('Runtime history input extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(data,['schema','bytes','inputReady','correctedReady']);
  Object.assign(result,{pollSerial:data.getBigUint64(16,true).toString(),
    localPort:data.getUint32(24,true),remotePort:data.getUint32(28,true),
    raw:bytes.slice(32,76),forward:bytes.slice(76,120),corrected:bytes.slice(120,164)});
  if(result.schema!==1||result.bytes!==INPUT_BYTES||result.inputReady!==1||result.correctedReady>1||
    result.pollSerial==='0'||result.localPort>1||result.remotePort>1||result.localPort===result.remotePort||
    bytes.subarray(164).some(byte=>byte!==0))throw Error('Runtime history input metadata violation');
  result.localPad=result.raw.slice(result.localPort*11,(result.localPort+1)*11);
  if(result.forward.subarray(result.localPort*11,(result.localPort+1)*11)
      .some((byte,index)=>byte!==result.localPad[index]))throw Error('Forward changed the once-polled local PAD');
  if(result.correctedReady&&result.corrected.subarray(result.localPort*11,(result.localPort+1)*11)
      .some((byte,index)=>byte!==result.localPad[index]))throw Error('Correction changed the retained local PAD');
  return result;
}

export function createRuntimeMatchHistoryController(module,{commandTimeoutMs=5000}={}){
  if(!module||!(module.HEAPU8 instanceof Uint8Array)||
    !Number.isInteger(commandTimeoutMs)||commandTimeoutMs<1||commandTimeoutMs>5000)
    throw Error('Runtime history requires its loaded Module and bounded command deadline');
  const names=['take_over','configure','remote_pad','request','status','status_bytes','input','input_bytes',
    'error','identity_json','cpu_endpoint_begin','cpu_endpoint_record','cpu_endpoint_data','cpu_endpoint_bytes',
    'cpu_endpoint_end','motor_data','motor_bytes','motor_commit_data','motor_commit_bytes','pcm_data','pcm_bytes'];
  if(names.some(name=>typeof module[PREFIX+name]!=='function')||
    ['_malloc','_free','_melee_web_net_validate_pad','UTF8ToString'].some(name=>typeof module[name]!=='function'))
    throw Error('Private runtime history exports are unavailable');
  if(module[PREFIX+'status_bytes']()!==STATUS_BYTES||module[PREFIX+'input_bytes']()!==INPUT_BYTES)
    throw Error('Private runtime history ABI differs from its controller');
  let busy=false,failed=null,terminal=false,captured=false,episodeMode=false;
  let identityText=null;
  const journal=[];
  const read=(pointer,size)=>{
    if(!Number.isInteger(pointer)||pointer<-0x80000000||pointer>0xffffffff)throw Error('Native history pointer');
    const start=pointer>>>0,heap=module.HEAPU8;
    if(typeof SharedArrayBuffer!=='undefined'&&heap.buffer instanceof SharedArrayBuffer)
      throw Error('Private history refuses shared memory');
    if(!start||!Number.isSafeInteger(size)||size<=0||start+size>heap.length)throw Error('Native history borrowed extent');
    return heap.slice(start,start+size);
  };
  const status=()=>decodeRuntimeMatchHistoryStatus(read(module[PREFIX+'status'](),STATUS_BYTES),{episodeMode});
  const errorText=()=>module.UTF8ToString(module[PREFIX+'error']());
  const latch=error=>{failed??=error;return error;};
  const check=(snapshot,allowFailure=false)=>{
    if(!allowFailure&&(snapshot.firstFailure||snapshot.metadata.firstFailure||snapshot.rendererErrors))
      throw latch(Error(`Native history refused: ${errorText()} (${JSON.stringify(snapshot)})`));
    return snapshot;
  };
  const active=()=>{if(terminal)throw Error('Runtime history controller is cleaned');if(busy)throw Error('Runtime history command is in progress');if(failed)throw failed;};
  const allocate=(bytes,callback)=>{
    const pointer=module._malloc(bytes.length);
    if(!pointer)throw Error('Runtime history argument allocation failed');
    try{module.HEAPU8.set(bytes,pointer>>>0);return callback(pointer>>>0);}finally{module._free(pointer);}
  };
  const startExport=name=>{
    const fn=module[PREFIX+name];
    if(typeof fn!=='function')throw Error(`Private agreed-start export ${name} is unavailable`);
    return fn;
  };
  const startHostArguments=(host,label='Agreed-start identity')=>{
    if(!host||START_HOST_FIELDS.some(key=>typeof host[key]!=='string'||!host[key]||host[key].includes('\0')))
      throw Error(`${label} requires explicit loaded host identities`);
    const encoder=new TextEncoder();
    return START_HOST_FIELDS.map(key=>encoder.encode(host[key]+'\0'));
  };
  const startHostCall=(name,prefixArguments,encoded)=>{
    const invoke=(index,pointers)=>{
      if(index===encoded.length){
        const pointer=startExport(name)(...prefixArguments,...pointers);
        if(!pointer)return null;
        if(!Number.isInteger(pointer)||pointer<=0||pointer>0xffffffff)
          throw latch(Error(`Native agreed-start ${name} returned an invalid string pointer`));
        // Native strings are borrowed. Copy their text before allocate() frees
        // any host argument, since _free is itself a later native call.
        const value=module.UTF8ToString(pointer);
        if(typeof value!=='string'||!value)
          throw latch(Error(`Native agreed-start ${name} returned an empty string`));
        return value;
      }
      return allocate(encoded[index],pointer=>invoke(index+1,[...pointers,pointer]));
    };
    return invoke(0,[]);
  };
  const startPreflight=(name,{argumentsAllocated=false,inspectState=false}={})=>{
    active();
    if(argumentsAllocated&&captured)
      throw Error('Agreed-start arguments cannot be allocated after match-history capture');
    startExport(name);
    if(inspectState)startExport('start_state');
    check(status());
  };
  const startRefusal=(name,detail)=>{
    let snapshot;
    try{snapshot=status();}catch(error){throw latch(error);}
    const nativeError=errorText();
    throw latch(Error(`Native agreed-start ${name} refused${detail?`: ${detail}`:''}: ${nativeError} (${JSON.stringify(snapshot)})`));
  };
  const startStateValue=()=>{
    const value=startExport('start_state')();
    if(!Number.isInteger(value)||value<0||value>START_STATE_MAX)
      throw latch(Error('Native agreed-start state is outside its bounded schema'));
    check(status());
    return value;
  };
  const readStartState=()=>{
    active();startExport('start_state');
    return startStateValue();
  };
  const startTextResult=(name,pointer)=>{
    if(!pointer)startRefusal(name);
    if(!Number.isInteger(pointer)||pointer<=0||pointer>0xffffffff)
      throw latch(Error(`Native agreed-start ${name} returned an invalid string pointer`));
    const value=module.UTF8ToString(pointer);
    check(status());
    if(typeof value!=='string'||!value)
      throw latch(Error(`Native agreed-start ${name} returned an empty string`));
    return value;
  };
  const decodeStartPublication=decodeStartPublicationBytes;
  const requireSuccess=(name,...args)=>{
    if(module[PREFIX+name](...args)!==1)throw latch(Error(`Native history ${name} refused: ${errorText()}`));
  };
  const episodeExport=name=>{
    const fn=module[PREFIX+name];
    if(typeof fn!=='function')throw latch(Error(`Optional runtime episode export ${name} is unavailable`));
    return fn;
  };
  const requireEpisodeSuccess=(name,...args)=>{
    if(episodeExport(name)(...args)!==1)
      throw latch(Error(`Native history episode ${name} refused: ${errorText()}`));
  };
  let padArgument=0,padArgumentStart=0,frameArgument=0,frameArgumentStart=0,padArgumentReleased=false;
  const writePadArgument=bytes=>{
    if(!padArgument||padArgumentReleased)throw Error('Runtime history PAD argument storage is unavailable');
    const heap=module.HEAPU8;
    if(!(heap instanceof Uint8Array)||padArgumentStart+PAD_BYTES>heap.length)
      throw Error('Runtime history PAD argument storage is outside Wasm memory');
    heap.set(bytes,padArgumentStart);
    return padArgumentStart;
  };
  const releasePadArgument=()=>{
    if(padArgumentReleased)return;
    if(frameArgument)module._free(frameArgument);
    module._free(padArgument);
    padArgument=0;padArgumentStart=0;frameArgument=0;frameArgumentStart=0;padArgumentReleased=true;
  };
  const allocateFrameArgument=()=>{
    if(frameArgument)return;
    const pointer=module._malloc(FRAME_BYTES);
    if(!pointer)throw latch(Error('Runtime history frame-plan allocation failed'));
    const start=pointer>>>0;
    if(start+FRAME_BYTES>module.HEAPU8.length){
      module._free(pointer);
      throw latch(Error('Runtime history frame-plan allocation is outside Wasm memory'));
    }
    frameArgument=pointer;frameArgumentStart=start;
  };
  const writeFrameArgument=bytes=>{
    if(!frameArgument||padArgumentReleased)throw Error('Runtime history frame-plan storage is unavailable');
    const heap=module.HEAPU8;
    if(!(heap instanceof Uint8Array)||frameArgumentStart+FRAME_BYTES>heap.length)
      throw Error('Runtime history frame-plan storage is outside Wasm memory');
    heap.set(bytes,frameArgumentStart);
    return frameArgumentStart;
  };
  const wait=async(predicate,deadline,label,allowFailure=false)=>{
    for(;;){
      const snapshot=check(status(),allowFailure);
      if(predicate(snapshot))return snapshot;
      if(performance.now()>=deadline)throw latch(Error(`Runtime history ${label} exceeded its ${commandTimeoutMs} ms deadline`));
      await new Promise(resolve=>setTimeout(resolve,5));
    }
  };
  let allocatedPadArgument=0;
  try{
    allocatedPadArgument=module._malloc(PAD_BYTES);
    if(!allocatedPadArgument)throw Error('Runtime history PAD argument allocation failed');
    padArgumentStart=allocatedPadArgument>>>0;
    if(padArgumentStart+PAD_BYTES>module.HEAPU8.length)
      throw Error('Runtime history PAD argument allocation is outside Wasm memory');
    padArgument=allocatedPadArgument;
    return Object.freeze({
    status,
    journal:()=>structuredClone(journal),
    takeOver({sourceFrame,localPort,remotePort}){
      active();u32(sourceFrame,'Source frame');
      if(![0,1].includes(localPort)||![0,1].includes(remotePort)||localPort===remotePort)
        throw Error('Runtime history requires source ports zero and one');
      requireSuccess('take_over',sourceFrame,localPort,remotePort);
      const snapshot=check(status());
      if(snapshot.ownerState!==1||snapshot.expectedFrame!==sourceFrame||
        snapshot.localPort!==localPort||snapshot.remotePort!==remotePort)throw latch(Error('Native takeover identity mismatch'));
      return snapshot;
    },
    configure(wire){
      active();if(captured)throw Error('Runtime history CPU table must be configured before capture');
      if(!Array.isArray(wire)||!wire.length)throw Error('Reviewed linked CPU table required');
      const bytes=new Uint8Array(wire.length*16),view=new DataView(bytes.buffer);let end=0;
      for(let index=0;index<wire.length;index++){
        const row=wire[index];if(!Array.isArray(row)||row.length!==4)throw Error('CPU table row extent');
        row.forEach((value,field)=>view.setUint32(index*16+field*4,u32(value,'CPU table value'),true));
        if(!row[0]||!row[1]||row[0]<end||row[0]+row[1]>0x100000000||
          ![1,2].includes(row[2])||![1,2].includes(row[3]))throw Error('CPU table range/policy');
        end=row[0]+row[1];
      }
      allocate(bytes,pointer=>requireSuccess('configure',pointer,wire.length));
    },
    identity(host){
      active();if(captured)throw Error('Runtime history identity must be read before capture');
      const text=startHostCall('identity_json',[],startHostArguments(host,'Runtime history identity'));
      if(text===null)throw latch(Error(`Native match identity refused: ${errorText()}`));
      const identity=JSON.parse(text);
      if(START_HOST_FIELDS.some(key=>identity[key]!==host[key]))
        throw latch(Error('Native match identity did not bind supplied host identities'));
      identityText=text;
      return text;
    },
    enableEpisode(){
      active();
      if(captured)throw Error('Runtime episode mode must be enabled before PRE capture');
      if(episodeMode)throw Error('Runtime episode mode is already enabled');
      check(status());
      episodeExport('episode_enable');
      allocateFrameArgument();
      requireEpisodeSuccess('episode_enable');
      episodeMode=true;
      check(status());
      return true;
    },
    startEnable(){
      startPreflight('start_enable',{inspectState:true});
      if(startExport('start_enable')()!==1)startRefusal('start_enable');
      const state=readStartState();
      if(state!==1)throw latch(Error(`Native agreed-start enable completed in state ${state}, expected 1`));
      return state;
    },
    startState(){
      return readStartState();
    },
    startOffer(host){
      startPreflight('start_offer_json',{argumentsAllocated:true,inspectState:true});
      const result=startHostCall('start_offer_json',[],startHostArguments(host));
      if(result===null)startRefusal('start_offer_json');
      check(status());
      const state=startStateValue();
      if(state!==2)throw latch(Error(`Native agreed-start offer completed in state ${state}, expected 2`));
      return result;
    },
    startAgreement(seed,host){
      u32(seed,'Negotiated match seed');
      startPreflight('start_agreement_json',{argumentsAllocated:true,inspectState:true});
      const result=startHostCall('start_agreement_json',[seed],startHostArguments(host));
      if(result===null)startRefusal('start_agreement_json');
      check(status());
      const state=startStateValue();
      if(state!==2)throw latch(Error(`Native agreed-start agreement completed in state ${state}, expected 2`));
      return result;
    },
    startCommit(seed,agreedJSON,host){
      u32(seed,'Negotiated match seed');
      startPreflight('start_commit',{argumentsAllocated:true,inspectState:true});
      if(typeof agreedJSON!=='string'||!agreedJSON||agreedJSON.includes('\0'))
        throw Error('Agreed-start commit requires the exact nonempty JSON bytes');
      const encoder=new TextEncoder(),json=encoder.encode(agreedJSON);
      if(!json.length||json.length>65536)throw Error('Agreed-start JSON byte extent');
      const argument=new Uint8Array(json.length+1);argument.set(json);
      const encodedHost=startHostArguments(host);
      const invokeHost=expectedPointer=>{
        const invoke=(index,pointers)=>index===encodedHost.length
          ?startExport('start_commit')(seed,expectedPointer,json.length,...pointers)
          :allocate(encodedHost[index],pointer=>invoke(index+1,[...pointers,pointer]));
        return invoke(0,[]);
      };
      const result=allocate(argument,invokeHost);
      if(result!==1)startRefusal('start_commit');
      check(status());
      const state=startStateValue();
      if(state!==3)throw latch(Error(`Native agreed-start commit completed in state ${state}, expected 3`));
      return state;
    },
    startPublication(){
      const state=readStartState();
      if(state!==START_HELD_STATE)
        throw latch(Error(`Agreed-start publication requires held frame zero, observed state ${state}`));
      const size=startExport('start_publication_bytes')();
      if(size!==START_PUBLICATION_BYTES)
        throw latch(Error(`Agreed-start publication ABI is ${size} bytes, expected ${START_PUBLICATION_BYTES}`));
      const pointer=startExport('start_publication_data')();
      if(!pointer)startRefusal('start_publication_data','held frame-zero publication unavailable');
      try{return decodeStartPublication(read(pointer,START_PUBLICATION_BYTES));}
      catch(error){throw latch(error);}
    },
    startLiveIdentity(){
      const state=readStartState();
      if(state!==START_HELD_STATE)
        throw latch(Error(`Agreed-start live identity requires held frame zero, observed state ${state}`));
      const pointer=startExport('start_live_identity_json')();
      return startTextResult('start_live_identity_json',pointer);
    },
    startPrefix(){
      const state=readStartState();
      if(state!==START_HELD_STATE)
        throw latch(Error(`Agreed-start prefix requires held frame zero, observed state ${state}`));
      const pointer=startExport('start_prefix_json')();
      return startTextResult('start_prefix_json',pointer);
    },
    validatePad(value){
      active();const bytes=pad(value);const pointer=writePadArgument(bytes);
      return module._melee_web_net_validate_pad(pointer,PAD_BYTES)===1;
    },
    remotePad(value){active();if(episodeMode)throw Error('Legacy remote PAD injection is disabled in episode mode');
      const bytes=pad(value);const pointer=writePadArgument(bytes);
      requireSuccess('remote_pad',pointer,PAD_BYTES);},
    ticket(){
      active();if(!episodeMode)throw Error('Episode poll tickets require explicit episode mode');
      const before=check(status());
      if(before.pendingCommand||before.completeSequence!==before.requestSequence||
        before.lastCompletedCommand!==MATCH_HISTORY_COMMAND.pollTicket)
        throw latch(Error('A copied episode poll ticket requires the completed latest pollTicket command'));
      if(episodeExport('ticket_bytes')()!==TICKET_BYTES)
        throw latch(Error('Native episode poll ticket ABI differs from its controller'));
      const pointer=episodeExport('ticket_data')();
      if(!pointer)throw latch(Error('Native episode poll ticket is unavailable'));
      const ticket=decodeRuntimeMatchHistoryPollTicket(read(pointer,TICKET_BYTES));
      const after=check(status());
      if(after.requestSequence!==before.requestSequence||after.completeSequence!==before.completeSequence||
        ticket.ticketId!==before.requestSequence||ticket.sourceFrame!==before.metadata.currentFrame||
        ticket.incarnation!==before.metadata.incarnation||ticket.localPort!==before.localPort||
        ticket.remotePort!==before.remotePort||ticket.pollSerial!==before.metadata.pollSerial||
        !after.inputReady)
        throw latch(Error('Native episode ticket differs from its exact poll completion and live owner'));
      return ticket;
    },
    receipt(ordinal){
      active();if(!episodeMode)throw Error('Episode input receipts require explicit episode mode');
      u32(ordinal,'Episode source ordinal');
      const before=check(status());
      if(before.pendingCommand||before.completeSequence!==before.requestSequence)
        throw latch(Error('Episode receipt requires an idle completed native command'));
      if(episodeExport('input_row_bytes')()!==INPUT_ROW_BYTES)
        throw latch(Error('Native episode input receipt ABI differs from its controller'));
      const pointer=episodeExport('input_row_data')(ordinal);
      if(!pointer)throw latch(Error(`Native episode input receipt ${ordinal} is unavailable`));
      const receipt=decodeRuntimeMatchHistoryEpisodeInputRow(read(pointer,INPUT_ROW_BYTES));
      const after=check(status());
      if(after.requestSequence!==before.requestSequence||after.completeSequence!==before.completeSequence||
        receipt.sourceFrame<before.metadata.preFrame||
        receipt.sourceFrame-before.metadata.preFrame!==ordinal||
        receipt.localPort!==before.localPort||receipt.remotePort!==before.remotePort)
        throw latch(Error('Native episode input receipt ordinal or owner changed'));
      receipt.ordinal=ordinal;
      return receipt;
    },
    input(){
      active();if(episodeMode)throw Error('Episode mode exposes retained poll tickets and input receipts');
      const snapshot=check(status());
      const result=decodeRuntimeMatchHistoryInput(read(module[PREFIX+'input'](),INPUT_BYTES));
      if(result.pollSerial!==snapshot.metadata.pollSerial||result.localPort!==snapshot.localPort||result.remotePort!==snapshot.remotePort)
        throw latch(Error('Native input receipt differs from its live owner'));
      return result;
    },
    endpoint(request){active();check(status());return collectCpuEndpointInPage(request,{runtime:'match-history',module});},
    motor(){active();const size=module[PREFIX+'motor_bytes']();if(size!==1016)throw Error('Runtime motor evidence ABI');return read(module[PREFIX+'motor_data'](),size);},
    motorCommit(){active();const size=module[PREFIX+'motor_commit_bytes']();if(size!==64)throw Error('Runtime motor commit ABI');return read(module[PREFIX+'motor_commit_data'](),size);},
    pcm(){active();const size=module[PREFIX+'pcm_bytes']();if(!Number.isInteger(size)||size<8||size>1068*4||size%8)throw Error('Runtime emitted stereo PCM extent');return read(module[PREFIX+'pcm_data'](),size);},
    checksum(){
      active();
      const data=module[PREFIX+'checksum_data'],extent=module[PREFIX+'checksum_bytes'];
      if(typeof data!=='function'||typeof extent!=='function')
        throw Error('Private runtime history checksum exports are unavailable');
      try{
        const before=check(status()),history=before.metadata;
        const corrected=history.phase===13; // CorrectedCpuCopied
        const span=history.currentFrame-history.preFrame;
        const episodeStepsOkay=episodeMode&&span>0&&span<=7&&history.forwardSteps===span&&
          (corrected?history.replaySteps===span:history.replaySteps===0);
        const legacyStepsOkay=!episodeMode&&span===1&&history.forwardSteps===1&&
          history.replaySteps===(corrected?1:0);
        if(before.ownerState!==1||before.pendingCommand||before.gpuPending||before.endpointActive||
          before.inputReady!==1||before.completeSequence!==before.requestSequence||
          (history.phase!==6&&!corrected)||history.preFrame!==before.expectedFrame||
          history.preFrame===UINT32_MAX||history.currentFrame<=history.preFrame||
          history.currentFrame!==history.sealedEnd||history.pollSerial==='0'||
          (!episodeStepsOkay&&!legacyStepsOkay))
          throw Error('Runtime history checksum requires a copied, idle CPU endpoint');
        const size=extent();
        if(size!==64)throw Error('Runtime history checksum ABI must be exactly 64 bytes');
        const pointer=data();
        if(!pointer)throw Error('Native runtime history checksum refused its copied CPU endpoint');
        const bytes=read(pointer,size); // Copy borrowed data before another native call.
        const record=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
        const expectedTick=episodeMode?history.currentFrame-1:history.preFrame;
        if(record.getUint32(0,true)!==expectedTick||record.getUint32(4,true)!==3||
          record.getUint32(12,true)!==history.currentFrame)
          throw Error('Runtime history checksum tick, match scene or source frame differs');
        if(JSON.stringify(check(status()))!==JSON.stringify(before))
          throw Error('Runtime history checksum changed its native owner status');
        return bytes;
      }catch(error){throw latch(error);}
    },
    forwardTicket(ticketId,plan){
      return this.command('forwardTicket',{ticketId,plan});
    },
    replayTicket(ticketId,plan){
      return this.command('replayTicket',{ticketId,plan});
    },
    async command(name,argument){
      const command=MATCH_HISTORY_COMMAND[name];if(!command)throw Error('Unknown runtime history command');
      const framePayload=name==='forwardTicket'||name==='replayTicket';
      const episodeCommands=new Set(['pollTicket','forwardTicket','seal','replayTicket','finishReplay','continue','beginEpisode']);
      const legacyExecution=name==='begin'||name==='forward'||name==='correct'||name==='rebase';
      let confirmedEnd,ticketId,planBytes;
      if(name==='rebase')confirmedEnd=argument;
      if(name==='rebase')u32(confirmedEnd,'Confirmed END frame');
      else if(framePayload){
        if(!argument||typeof argument!=='object'||Array.isArray(argument)||
          Object.keys(argument).length!==2||!Object.hasOwn(argument,'ticketId')||!Object.hasOwn(argument,'plan'))
          throw Error('Episode ticket command requires exactly {ticketId, plan}');
        ticketId=u32(argument.ticketId,'Episode ticket ID');
        if(!ticketId||!(argument.plan instanceof Uint8Array)||argument.plan.length!==FRAME_BYTES)
          throw Error('Episode ticket command requires a nonzero ticket ID and exact 44-byte plan');
        planBytes=argument.plan.slice();
      }else if(argument!==undefined)throw Error('Only confirmed rebase and episode ticket commands take arguments');
      if(episodeCommands.has(name)&&!episodeMode)
        throw Error('Episode commands require explicit pre-capture episode mode');
      if(episodeMode&&legacyExecution)
        throw Error('Legacy one-window execution commands are disabled in episode mode');
      if(name!=='cleanup')active();else if(terminal||busy)throw Error('Runtime history cleanup owner unavailable');
      busy=true;const started=performance.now(),deadline=started+commandTimeoutMs;
      const entry={command,name,started,status:'pending'};journal.push(entry);
      if(name==='rebase')entry.confirmedEnd=confirmedEnd;
      try{
        const allowFailure=name==='cleanup';
        const before=await wait(value=>value.pendingCommand===0&&
          (!['idle','cleanup'].includes(name)||value.gpuPending===0),deadline,name,allowFailure);
        const next=before.requestSequence+1;
        if(name==='rebase'){
          const h=before.metadata;
          if(!captured||!identityText||before.ownerState!==1||before.endpointActive||before.gpuPending||
            before.completeSequence!==before.requestSequence||h.phase!==18||
            h.preFrame!==before.expectedFrame||confirmedEnd!==h.preFrame+1||
            confirmedEnd!==h.currentFrame||confirmedEnd!==h.sealedEnd||h.forwardSteps!==1)
            throw Error('Confirmed rebase requires the actual retired END');
          if(typeof module[PREFIX+'request_rebase']!=='function'||
            typeof module[PREFIX+'rebase_identity_json']!=='function')
            throw Error('Private confirmed rebase exports are unavailable');
          requireSuccess('request_rebase',confirmedEnd);
        }else if(framePayload){
          const h=before.metadata;
          if(!captured||!frameArgument||ticketId>before.requestSequence||
            (name==='forwardTicket'?![4,21].includes(h.phase):h.phase!==11))
            throw Error('Episode ticket command is outside its captured source phase');
          const pointer=writeFrameArgument(planBytes);
          requireEpisodeSuccess(name==='forwardTicket'?'request_episode_forward':'request_episode_replay',
            ticketId,pointer,FRAME_BYTES);
        }else requireSuccess('request',command);
        const after=await wait(value=>value.pendingCommand===0&&value.completeSequence===next,deadline,name,allowFailure);
        if(after.requestSequence!==next||after.lastCompletedCommand!==command)
          throw Error('Native command completion sequence mismatch');
        if(episodeCommands.has(name)){
          const prior=before.metadata,now=after.metadata;
          let valid=false;
          switch(name){
          case 'beginEpisode':
            valid=now.phase===3&&now.incarnation===1&&now.currentFrame===now.preFrame&&
              now.forwardSteps===0&&now.restoreCount===0;break;
          case 'pollTicket':
            valid=[4,21].includes(now.phase)&&now.currentFrame===prior.currentFrame&&
              after.inputReady===1&&now.pollSerial!=='0';break;
          case 'forwardTicket':
            valid=now.phase===21&&now.currentFrame===prior.currentFrame+1&&
              now.currentFrame-now.preFrame<=7&&now.forwardSteps===prior.forwardSteps+1&&
              now.incarnation===prior.incarnation;break;
          case 'seal':
            valid=now.phase===5&&now.currentFrame===prior.currentFrame&&
              now.sealedEnd===now.currentFrame&&now.forwardSteps>0;break;
          case 'replayTicket':
            valid=now.phase===11&&now.currentFrame===prior.currentFrame+1&&
              now.replaySteps===prior.replaySteps+1&&now.replaySteps<=7&&
              now.incarnation===prior.incarnation;break;
          case 'finishReplay':
            valid=now.phase===12&&now.currentFrame===now.sealedEnd&&
              now.replaySteps===now.currentFrame-now.preFrame;break;
          case 'continue':
            valid=now.phase===21&&now.incarnation===prior.incarnation+1&&
              now.currentFrame===prior.currentFrame&&now.sealedEnd===0&&now.replaySteps===0&&
              now.restoreCount===prior.restoreCount;break;
          default:break;
          }
          if(!valid)throw Error(`Episode ${name} completion differs from its bounded native transition`);
        }
        if(name==='cleanup'){
          if(after.ownerState!==2)throw Error('Native cleanup did not retire the owner');
          releasePadArgument();terminal=true;
        }
        if(name==='capture')captured=true;
        if(name==='rebase'){
          const h=after.metadata;
          if(after.ownerState!==1||after.localPort!==before.localPort||after.remotePort!==before.remotePort||
            after.expectedFrame!==confirmedEnd||after.inputReady||after.gpuPending||after.endpointActive||
            h.phase||h.preFrame!==confirmedEnd||h.currentFrame!==confirmedEnd||h.sealedEnd||
            h.incarnation||h.endpointMask||h.forwardSteps||h.restoreCount||h.replaySteps||h.pollSerial!=='0'||
            h.preAudioPhase!==before.metadata.currentAudioPhase||h.currentAudioPhase!==h.preAudioPhase)
            throw Error('Confirmed rebase acknowledgment did not bind fresh PRE');
          const pointer=module[PREFIX+'rebase_identity_json']();
          if(!Number.isInteger(pointer)||pointer<=0||pointer>0xffffffff)
            throw Error('Confirmed rebase has no fresh native identity');
          const identity=module.UTF8ToString(pointer);
          const prior=JSON.parse(identityText),fresh=JSON.parse(identity);
          const {source_frame:priorFrame,current_rng:priorRng,...priorOwner}=prior;
          const {source_frame:frame,current_rng:rng,...freshOwner}=fresh;
          if(priorFrame!==before.expectedFrame||!/^[0-9a-f]{8}$/.test(priorRng)||
            frame!==confirmedEnd||!/^[0-9a-f]{8}$/.test(rng)||!sameJsonValue(priorOwner,freshOwner)||
            !sameJsonValue(check(status()),after))
            throw Error('Confirmed rebase changed the bound identity or owner status');
          identityText=identity;
          entry.identity=identity;
          captured=false;
        }
        entry.status='complete';entry.after=after;entry.elapsedMs=performance.now()-started;
        return name==='rebase'?{status:after,identity:entry.identity}:after;
      }catch(error){entry.status='failed';entry.error=String(error);entry.elapsedMs=performance.now()-started;throw latch(error);}
      finally{busy=false;}
    },
  });
  }catch(error){
    if(allocatedPadArgument){
      try{module._free(allocatedPadArgument);}
      catch(cleanupError){throw new AggregateError([error,cleanupError],
        'Runtime history controller construction and PAD scratch cleanup failed');}
    }
    throw error;
  }
}
