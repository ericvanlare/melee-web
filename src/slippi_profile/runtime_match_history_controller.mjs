// SPDX-License-Identifier: GPL-3.0-or-later
// Private ordinary-runtime owner. The transport admits native commands and
// observes their completion without polling PAD or performing source work.
import {collectCpuEndpointInPage} from './tests/cpu_endpoint.mjs';

export const MATCH_HISTORY_COMMAND=Object.freeze({capture:1,begin:2,forward:3,present:4,
  idle:5,restore:6,correct:7,close:8,retire:9,cleanup:10,rebase:11,
  pollTicket:12,forwardTicket:13,seal:14,replayTicket:15,finishReplay:16,
  continue:17,beginEpisode:18,recordingPresent:19,finishPrefix:20,
  closePrefix:21,retirePrefix:22,rebaseConfirmedPrefix:23,
  adoptNextPrefixTicket:24,sealPresentedFrontier:25,terminalTailBegin:26,
  terminalTailTicket:27,terminalTailForward:28,terminalTailPresent:29,
  finishTerminalTail:30});
const PREFIX='_melee_web_native_menu_match_history_';
const QUEUE_COMPLETION_OBSERVER_KEY='__meleeWebRuntimeHistoryQueueCompletionV1';
const STATUS_BYTES=128,INPUT_BYTES=168,PAD_BYTES=11,FRAME_BYTES=44,TICKET_BYTES=88,INPUT_ROW_BYTES=176;
const START_STATE_MAX=7,START_PUBLICATION_BYTES=48,START_HELD_STATE=5,START_PREFIX_MAX=1024;
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
const PREFIX_HANDOFF_SCHEMA='melee-web-runtime-prefix-handoff-v1';
const PREFIX_HANDOFF_FIELDS=['schema','oldPreFrame','confirmedFrame','selectedEndFrame',
  'oldIncarnation','finishRequestSequence','rebaseRequestSequence','suffixCount','suffixCursor',
  'localPort','remotePort','worldGeneration','contextEpoch','ownerStorage','ownerContext',
  'ownerFlow','ownerAudio'];
const PREFIX_HANDOFF_U32_FIELDS=['oldPreFrame','confirmedFrame','selectedEndFrame',
  'oldIncarnation','finishRequestSequence','rebaseRequestSequence','suffixCount','suffixCursor',
  'localPort','remotePort'];
const PREFIX_OWNER_FIELDS=['ownerStorage','ownerContext','ownerFlow','ownerAudio'];
const TERMINAL_TAIL_SCHEMA='melee-web-runtime-terminal-tail-v1';
const TERMINAL_TAIL_FIELDS=['schema','requestSequence','preSourceFrame','preOrdinal',
  'preCompletedTick','firstSourceFrame','firstOrdinal','firstCompletedTick',
  'selectedSourceFrame','selectedOrdinal','selectedCompletedTick','selectedTicketId',
  'selectedPollSerial','sourceFrame','consumedOrdinal','completedTick','incarnation',
  'localPort','remotePort','suffixCount','suffixCursor','worldGeneration','contextEpoch'];
const TERMINAL_FINAL_SCHEMA='melee-web-runtime-terminal-final-v1';
const STAGING_JOURNAL_LIMITS=Object.freeze({commands:256,pacing:256,networkWaits:64});
const TERMINAL_FINAL_FIELDS=['schema','firstSourceFrame','firstOrdinal','firstCompletedTick',
  'selectedSourceFrame','selectedOrdinal','selectedCompletedTick','sourceFrame',
  'consumedOrdinal','lastInputOrdinal','completedTick','ticketId','requestSequence',
  'pollSerial','localPort','remotePort','outcome','winner','randomSeed','worldGeneration',
  'contextEpoch','presentationSerial','submission','checksumTick','checksumFrame'];
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
const sameBytes=(left,right)=>left instanceof Uint8Array&&right instanceof Uint8Array&&
  left.length===right.length&&left.every((value,index)=>value===right[index]);

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

export function decodeRuntimeMatchHistoryStatus(bytes,{episodeMode=false,arenaMode=false}={}){
  if(!(bytes instanceof Uint8Array)||bytes.length!==STATUS_BYTES)throw Error('Runtime history status extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(data,statusNames),metadata=fields(data,metadataNames,48);
  metadata.pollSerial=data.getBigUint64(96,true).toString();
  metadata.preAudioPhase=data.getUint32(104,true);metadata.currentAudioPhase=data.getUint32(108,true);
  Object.assign(result,{metadata,...fields(data,['inputReady','gpuPending','endpointActive','rendererErrors'],112)});
  const stepLimit=episodeMode?7:1,restoreLimit=episodeMode?2:1,phaseLimit=arenaMode?41:episodeMode?21:20,
    commandLimit=arenaMode?30:episodeMode?18:11;
  if(result.schema!==1||result.bytes!==STATUS_BYTES||result.reserved!==0||result.ownerState>2||
    result.pendingCommand>commandLimit||result.lastCompletedCommand>commandLimit||result.completeSequence>result.requestSequence||
    result.inputReady>1||result.gpuPending>1||result.endpointActive>1||
    metadata.schema!==1||metadata.bytes!==64||metadata.phase>phaseLimit||metadata.restoreCount>restoreLimit||
    metadata.incarnation>(episodeMode?3:1)||metadata.forwardSteps>stepLimit||metadata.replaySteps>stepLimit||
    (!episodeMode&&metadata.phase===21))
    throw Error('Runtime history status schema or bounded-owner violation');
  return result;
}

export function decodeRuntimeMatchHistoryPacing(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.byteLength!==64)throw Error('Native pacing receipt extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(data,['schema','bytes','phase','currentFrame','steps','pendingSteps','stalled','stallReason']);
  for(const [index,key] of ['elapsedMs','pendingTicks','triggeringValue','threshold'].entries())
    result[key]=data.getFloat64(32+index*8,true);
  if(result.schema!==1||result.bytes!==64||result.phase>41||result.steps>1||
      result.pendingSteps>8||result.stalled>1||result.stallReason>3||
      (result.stalled?(result.steps!==0||result.stallReason===0):result.stallReason!==0)||
      !Number.isFinite(result.elapsedMs)||result.elapsedMs<0||
      !Number.isFinite(result.pendingTicks)||result.pendingTicks<0||
      (result.stallReason!==2&&!Number.isFinite(result.triggeringValue))||
      !Number.isFinite(result.threshold))
    throw Error('Native pacing receipt schema or clock invariant refused');
  return result;
}

export function decodeRuntimeMatchHistoryPrefixHandoff(rawJSON){
  if(typeof rawJSON!=='string'||!rawJSON)throw Error('Runtime prefix handoff JSON is required');
  let result;
  try{result=JSON.parse(rawJSON);}catch{throw Error('Runtime prefix handoff JSON parse refused');}
  if(!hasExactKeys(result,PREFIX_HANDOFF_FIELDS)||result.schema!==PREFIX_HANDOFF_SCHEMA)
    throw Error('Runtime prefix handoff schema or exact field set violation');
  for(const name of PREFIX_HANDOFF_U32_FIELDS)u32(result[name],`Prefix handoff ${name}`);
  if(result.oldPreFrame>=result.confirmedFrame||result.confirmedFrame>=result.selectedEndFrame||
    result.selectedEndFrame-result.oldPreFrame>7||
    result.oldIncarnation<1||result.oldIncarnation>3||
    !result.finishRequestSequence||
    (result.rebaseRequestSequence!==0&&
      result.rebaseRequestSequence<=result.finishRequestSequence)||
    result.suffixCount!==result.selectedEndFrame-result.confirmedFrame||
    !result.suffixCount||result.suffixCursor>result.suffixCount||
    ![0,1].includes(result.localPort)||![0,1].includes(result.remotePort)||
    result.localPort===result.remotePort)
    throw Error('Runtime prefix handoff bounds or source interval violation');
  try{
    canonicalU64(result.worldGeneration,'handoff world generation',{positive:true});
    canonicalU64(result.contextEpoch,'handoff context epoch',{positive:true});
  }catch(error){throw Error(`Runtime prefix handoff ${error.message}`);}
  for(const name of PREFIX_OWNER_FIELDS){
    if(typeof result[name]!=='string'||!/^[0-9a-f]{16}$/.test(result[name])||
      BigInt('0x'+result[name])===0n)
      throw Error(`Runtime prefix handoff ${name} is not a nonzero fixed-width owner token`);
  }
  return result;
}

export function decodeRuntimeMatchHistoryTerminalReceipt(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.byteLength!==104)
    throw Error('Runtime terminal receipt extent');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(view,['schema','bytes','flags','incarnation','preSourceFrame',
    'selectedSourceFrame','preOrdinal','selectedOrdinal','firstOrdinal','firstSourceFrame',
    'ticketId','localPort','remotePort','reserved']);
  for(const [index,name] of ['preCompletedTick','selectedCompletedTick','firstCompletedTick',
      'worldGeneration','contextEpoch','pollSerial'].entries())
    result[name]=view.getBigUint64(56+index*8,true).toString();
  if(result.schema!==1||result.bytes!==104||result.reserved||![0,1,3].includes(result.flags))
    throw Error('Runtime terminal receipt schema or flags');
  if(result.flags===0){
    if(bytes.subarray(12).some(byte=>byte!==0))throw Error('Empty runtime terminal receipt has payload');
    return Object.freeze(result);
  }
  const relative=(ordinal,sourceFrame,tick)=>ordinal>result.preOrdinal&&
    ordinal-result.preOrdinal<=7&&sourceFrame===ordinal-1&&
    BigInt(tick)===BigInt(result.preCompletedTick)+BigInt(ordinal-result.preOrdinal);
  const selected=result.flags===3;
  if(result.preSourceFrame!==result.preOrdinal||result.incarnation<1||result.incarnation>3||
     result.worldGeneration==='0'||result.contextEpoch==='0'||
     !relative(result.firstOrdinal,result.firstSourceFrame,result.firstCompletedTick)||
     (selected?(!result.ticketId||result.pollSerial==='0'||result.localPort>1||result.remotePort>1||
       result.localPort===result.remotePort||
       !relative(result.selectedOrdinal,result.selectedSourceFrame,result.selectedCompletedTick)):
       (result.selectedOrdinal||result.selectedSourceFrame||result.ticketId||result.localPort||
        result.remotePort||result.selectedCompletedTick!=='0'||result.pollSerial!=='0')))
    throw Error('Runtime terminal receipt cursor or selected ticket differs');
  return Object.freeze(result);
}

export function decodeRuntimeMatchHistoryTerminalTail(rawJSON){
  if(typeof rawJSON!=='string'||!rawJSON)throw Error('Runtime terminal-tail JSON is required');
  let result;
  try{result=JSON.parse(rawJSON);}catch{throw Error('Runtime terminal-tail JSON parse refused');}
  if(!hasExactKeys(result,TERMINAL_TAIL_FIELDS)||result.schema!==TERMINAL_TAIL_SCHEMA)
    throw Error('Runtime terminal-tail schema or exact field set violation');
  for(const name of ['requestSequence','preSourceFrame','preOrdinal','firstSourceFrame','firstOrdinal',
    'selectedSourceFrame','selectedOrdinal','selectedTicketId','sourceFrame','consumedOrdinal',
    'incarnation','localPort','remotePort','suffixCount','suffixCursor'])
    u32(result[name],`Terminal-tail ${name}`);
  if(!result.requestSequence||!result.selectedTicketId||result.preSourceFrame!==result.preOrdinal||
    result.firstOrdinal<=result.preOrdinal||result.selectedOrdinal<=result.preOrdinal||
    result.firstOrdinal-result.preOrdinal>7||result.selectedOrdinal-result.preOrdinal>7||
    result.firstSourceFrame!==result.firstOrdinal-1||
    result.selectedSourceFrame!==result.selectedOrdinal-1||result.selectedSourceFrame!==result.sourceFrame||
    result.consumedOrdinal<result.selectedOrdinal+result.suffixCursor||
    result.suffixCursor>result.suffixCount||result.selectedOrdinal-result.preOrdinal+result.suffixCount>7||
    result.incarnation<1||result.incarnation>3||![0,1].includes(result.localPort)||
    ![0,1].includes(result.remotePort)||result.localPort===result.remotePort)
    throw Error('Runtime terminal-tail source, ordinal, suffix, incarnation, or port bounds violation');
  for(const name of ['preCompletedTick','firstCompletedTick','selectedCompletedTick','completedTick'])
    canonicalU64(result[name],`terminal-tail ${name}`);
  for(const name of ['worldGeneration','contextEpoch'])
    canonicalU64(result[name],`terminal-tail ${name}`,{positive:true});
  const anchor=BigInt(result.preCompletedTick)-BigInt(result.preOrdinal);
  if(BigInt(result.firstCompletedTick)-BigInt(result.firstOrdinal)!==anchor||
    BigInt(result.selectedCompletedTick)-BigInt(result.selectedOrdinal)!==anchor||
    BigInt(result.completedTick)-BigInt(result.consumedOrdinal)!==anchor)
    throw Error('Runtime terminal-tail completed tick differs from its PRE anchor');
  canonicalU64(result.selectedPollSerial,'terminal-tail selected poll serial',{positive:true});
  return result;
}

export function decodeRuntimeMatchHistoryTerminalTailTicket(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.length!==160)
    throw Error('Runtime terminal-tail ticket extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result={schema:data.getUint32(0,true),bytes:data.getUint32(4,true),flags:data.getUint32(8,true),
    ticketId:data.getUint32(12,true),requestSequence:data.getUint32(16,true),
    sourceFrame:data.getUint32(20,true),inputOrdinal:data.getUint32(24,true),
    originTicketId:data.getUint32(28,true),originSourceFrame:data.getUint32(32,true),
    incarnation:data.getUint32(36,true),originIncarnation:data.getUint32(40,true),
    localPort:data.getUint32(44,true),remotePort:data.getUint32(48,true),
    reserved:data.getUint32(52,true),completedTick:data.getBigUint64(56,true).toString(),
    pollSerial:data.getBigUint64(64,true).toString(),rawPad:bytes.slice(72,116),
    appliedPad:bytes.slice(116,160),rawBytes:bytes.slice()};
  if(result.schema!==1||result.bytes!==160||![0,1].includes(result.flags)||!result.ticketId||
    !result.requestSequence||result.reserved||result.incarnation<1||result.incarnation>3||
    ![0,1].includes(result.localPort)||![0,1].includes(result.remotePort)||
    result.localPort===result.remotePort||result.pollSerial==='0'||
    (result.flags===0&&(result.originTicketId||result.originSourceFrame||result.originIncarnation))||
    (result.flags===1&&(result.ticketId!==result.originTicketId||
      result.originSourceFrame!==result.inputOrdinal||result.originIncarnation<1||
      result.originIncarnation>result.incarnation)))
    throw Error('Runtime terminal-tail ticket metadata or origin violation');
  canonicalU64(result.completedTick,'terminal-tail ticket completed tick');
  canonicalU64(result.pollSerial,'terminal-tail ticket poll serial',{positive:true});
  return result;
}

export function decodeRuntimeMatchHistoryTerminalFinal(rawJSON){
  if(typeof rawJSON!=='string'||!rawJSON)throw Error('Runtime terminal-final JSON is required');
  let result;
  try{result=JSON.parse(rawJSON);}catch{throw Error('Runtime terminal-final JSON parse refused');}
  if(!hasExactKeys(result,TERMINAL_FINAL_FIELDS)||result.schema!==TERMINAL_FINAL_SCHEMA)
    throw Error('Runtime terminal-final schema or exact field set violation');
  for(const name of ['firstSourceFrame','firstOrdinal','selectedSourceFrame','selectedOrdinal',
    'sourceFrame','consumedOrdinal','lastInputOrdinal','ticketId','requestSequence','localPort',
    'remotePort','randomSeed','checksumTick','checksumFrame'])
    u32(result[name],`Terminal-final ${name}`);
  for(const name of ['outcome','winner'])
    if(!Number.isInteger(result[name])||result[name]<-0x80000000||result[name]>0x7fffffff)
      throw Error(`Terminal-final ${name} is not int32`);
  if(!result.ticketId||!result.requestSequence||result.consumedOrdinal!==result.lastInputOrdinal+1||
    !result.firstOrdinal||!result.selectedOrdinal||result.selectedOrdinal>result.lastInputOrdinal||
    result.firstSourceFrame!==result.firstOrdinal-1||result.selectedSourceFrame!==result.selectedOrdinal-1||
    result.selectedSourceFrame!==result.sourceFrame||
    result.checksumTick!==result.lastInputOrdinal||result.checksumFrame!==result.sourceFrame||
    ![0,1].includes(result.localPort)||![0,1].includes(result.remotePort)||
    result.localPort===result.remotePort)
    throw Error('Runtime terminal-final ordinal, frame, ticket, or port relationship violation');
  for(const name of ['firstCompletedTick','selectedCompletedTick','completedTick'])
    canonicalU64(result[name],`terminal-final ${name}`);
  for(const name of ['pollSerial','worldGeneration','contextEpoch','presentationSerial','submission'])
    canonicalU64(result[name],`terminal-final ${name}`,{positive:true});
  const anchor=BigInt(result.selectedCompletedTick)-BigInt(result.selectedOrdinal);
  if(BigInt(result.firstCompletedTick)-BigInt(result.firstOrdinal)!==anchor||
    BigInt(result.completedTick)-BigInt(result.consumedOrdinal)!==anchor)
    throw Error('Runtime terminal-final completed tick differs from its selected anchor');
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

export function createRuntimeMatchHistoryController(module,{commandTimeoutMs=5000,mode='diagnostic'}={}){
  if(!module||!(module.HEAPU8 instanceof Uint8Array)||
    !Number.isInteger(commandTimeoutMs)||commandTimeoutMs<1||commandTimeoutMs>5000)
    throw Error('Runtime history requires its loaded Module and bounded command deadline');
  if(!['diagnostic','staging'].includes(mode))
    throw Error('Runtime history controller mode must be diagnostic or staging');
  const diagnosticMode=mode==='diagnostic';
  const commonNames=['take_over','configure','request','status','status_bytes','error','identity_json'];
  const diagnosticNames=['remote_pad','input','input_bytes','cpu_endpoint_begin','cpu_endpoint_record',
    'cpu_endpoint_data','cpu_endpoint_bytes','cpu_endpoint_end','motor_data','motor_bytes',
    'motor_commit_data','motor_commit_bytes','pcm_data','pcm_bytes'];
  const requiredNames=[...commonNames,...(diagnosticMode?diagnosticNames:['cpu_endpoint_admit'])];
  if(requiredNames.some(name=>typeof module[PREFIX+name]!=='function')||
    ['_malloc','_free','_melee_web_net_validate_pad','UTF8ToString'].some(name=>typeof module[name]!=='function'))
    throw Error('Private runtime history exports are unavailable');
  if(module[PREFIX+'status_bytes']()!==STATUS_BYTES||
    (diagnosticMode&&module[PREFIX+'input_bytes']()!==INPUT_BYTES))
    throw Error('Private runtime history ABI differs from its controller');
  let busy=false,failed=null,terminal=false,captured=false,episodeMode=false,arenaMode=false;
  let inlineMode=false,nativeCallPending=false;
  let identityText=null,lastPresentationSubmission=null;
  let adoptedPrefixTicket=null,unclaimedPrefixTicket=false,confirmedPrefixActive=false;
  let lastPolledEpisodeTicket=null,terminalCandidate=null,terminalTailActive=false;
  let terminalTailLastTicket=null,terminalTailFinalCopy=null,terminalTailAgreementConfirmed=false;
  const journal=[],pacingJournal=[],pacingWaitJournal=[];
  const journalDropped={commands:0,pacing:0,networkWaits:0};
  const appendJournal=(kind,entry)=>{
    const target=kind==='commands'?journal:kind==='pacing'?pacingJournal:pacingWaitJournal;
    const limit=diagnosticMode?Infinity:STAGING_JOURNAL_LIMITS[kind];
    if(target.length>=limit){target.shift();journalDropped[kind]++;}
    target.push(entry);
  };
  const journalRetention=()=>Object.freeze({
    commands:Object.freeze({retained:journal.length,dropped:journalDropped.commands,
      limit:diagnosticMode?null:STAGING_JOURNAL_LIMITS.commands}),
    pacing:Object.freeze({retained:pacingJournal.length,dropped:journalDropped.pacing,
      limit:diagnosticMode?null:STAGING_JOURNAL_LIMITS.pacing}),
    networkWaits:Object.freeze({retained:pacingWaitJournal.length,dropped:journalDropped.networkWaits,
      limit:diagnosticMode?null:STAGING_JOURNAL_LIMITS.networkWaits}),
  });
  let activePacingWait=null;
  let pacingStarted=false;
  const queueCompletionWindow=globalThis.window??null;
  let queueCompletionInstalled=false,queueCompletionClosed=false;
  let latestQueueCompletion=null,consumedQueueCompletion=null,pendingQueueCompletion=null;
  const queueCompletionWords=(lo,hi)=>{
    if(!Number.isInteger(lo)||lo<0||lo>UINT32_MAX||
       !Number.isInteger(hi)||hi<0||hi>UINT32_MAX)return null;
    return ((BigInt(hi)<<32n)|BigInt(lo)).toString();
  };
  const queueCompletionMatches=(actual,target)=>actual&&target&&
    actual.frameId===target.frameId&&actual.stagingGeneration===target.stagingGeneration&&
    actual.registrationSequence===target.registrationSequence;
  const queueCompletionNewer=(candidate,prior)=>{
    if(!prior)return true;
    const generation=BigInt(candidate.stagingGeneration),oldGeneration=BigInt(prior.stagingGeneration);
    return generation>oldGeneration||
      (generation===oldGeneration&&BigInt(candidate.registrationSequence)>BigInt(prior.registrationSequence));
  };
  const queueCompletionObserver=Object.freeze({notify(frameLo,frameHi,generationLo,generationHi,registrationLo,registrationHi){
    if(queueCompletionClosed)return;
    try{
      const frameId=queueCompletionWords(frameLo,frameHi);
      const stagingGeneration=queueCompletionWords(generationLo,generationHi);
      const registrationSequence=queueCompletionWords(registrationLo,registrationHi);
      if(frameId===null||stagingGeneration===null||registrationSequence===null)return;
      const completion=Object.freeze({frameId,stagingGeneration,registrationSequence});
      if(queueCompletionNewer(completion,latestQueueCompletion))latestQueueCompletion=completion;
      if(pendingQueueCompletion&&queueCompletionMatches(latestQueueCompletion,pendingQueueCompletion.target)){
        const pending=pendingQueueCompletion;pendingQueueCompletion=null;
        consumedQueueCompletion=latestQueueCompletion;pending.resolve(true);
      }
    }catch(_){/* A diagnostic notification must never throw into Aurora's callback. */}
  }});
  const installQueueCompletionObserver=()=>{
    if(!queueCompletionWindow)return;
    if(Object.hasOwn(queueCompletionWindow,QUEUE_COMPLETION_OBSERVER_KEY))
      throw Error('A private runtime-history queue-completion observer is already active');
    Object.defineProperty(queueCompletionWindow,QUEUE_COMPLETION_OBSERVER_KEY,
      {value:queueCompletionObserver,configurable:true,enumerable:false,writable:false});
    queueCompletionInstalled=true;
  };
  const closeQueueCompletionObserver=()=>{
    if(queueCompletionClosed)return;
    queueCompletionClosed=true;
    latestQueueCompletion=null;consumedQueueCompletion=null;
    if(pendingQueueCompletion){const pending=pendingQueueCompletion;pendingQueueCompletion=null;pending.resolve(false);}
    if(queueCompletionInstalled&&queueCompletionWindow){
      try{
        const descriptor=Object.getOwnPropertyDescriptor(queueCompletionWindow,QUEUE_COMPLETION_OBSERVER_KEY);
        if(descriptor?.value===queueCompletionObserver&&descriptor.configurable)
          delete queueCompletionWindow[QUEUE_COMPLETION_OBSERVER_KEY];
      }catch(_){/* A foreign replacement cannot make teardown fail. */}
    }
    queueCompletionInstalled=false;
  };
  const waitForQueueCompletion=(target,deadline)=>{
    if(!queueCompletionInstalled||queueCompletionClosed)return Promise.resolve(false);
    if(queueCompletionMatches(latestQueueCompletion,target)){
      if(queueCompletionMatches(consumedQueueCompletion,target))return Promise.resolve(null);
      consumedQueueCompletion=latestQueueCompletion;return Promise.resolve(true);
    }
    if(pendingQueueCompletion)return Promise.reject(Error('Private runtime-history queue waiter already has an owner'));
    if(deadline-performance.now()<=0)return Promise.resolve(false);
    let resolveEvent,timer=0;
    const eventPromise=new Promise(resolve=>{resolveEvent=resolve;});
    pendingQueueCompletion={target,resolve:resolveEvent};
    if(queueCompletionMatches(latestQueueCompletion,target)){
      consumedQueueCompletion=latestQueueCompletion;pendingQueueCompletion=null;resolveEvent(true);
    }
    const deadlinePromise=new Promise(resolve=>{
      timer=setTimeout(()=>{
        if(pendingQueueCompletion?.resolve===resolveEvent)pendingQueueCompletion=null;
        resolve(false);
      },Math.min(5,Math.max(0,deadline-performance.now())));
    });
    return Promise.race([eventPromise,deadlinePromise]).finally(()=>{
      if(timer)clearTimeout(timer);
      if(pendingQueueCompletion?.resolve===resolveEvent)pendingQueueCompletion=null;
    });
  };
  const read=(pointer,size)=>{
    if(!Number.isInteger(pointer)||pointer<-0x80000000||pointer>0xffffffff)throw Error('Native history pointer');
    const start=pointer>>>0,heap=module.HEAPU8;
    if(typeof SharedArrayBuffer!=='undefined'&&heap.buffer instanceof SharedArrayBuffer)
      throw Error('Private history refuses shared memory');
    if(!start||!Number.isSafeInteger(size)||size<=0||start+size>heap.length)throw Error('Native history borrowed extent');
    return heap.slice(start,start+size);
  };
  const status=()=>{
    if(nativeCallPending)throw Error('Runtime history Asyncify call is in progress');
    return decodeRuntimeMatchHistoryStatus(read(module[PREFIX+'status'](),STATUS_BYTES),{episodeMode,arenaMode});
  };
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
  const readPrefixHandoff=()=>{
    if(!arenaMode)throw Error('Confirmed-prefix handoff requires explicit arena history mode');
    const getter=module[PREFIX+'prefix_handoff_json'];
    if(typeof getter!=='function')throw Error('Private confirmed-prefix handoff export is unavailable');
    const before=check(status());
    const pointer=getter();
    if(!Number.isInteger(pointer)||pointer<=0||pointer>UINT32_MAX)
      throw latch(Error('Native confirmed-prefix handoff is unavailable'));
    // The returned native JSON is borrowed; copy it before asking for status.
    const text=module.UTF8ToString(pointer>>>0);
    let result;
    try{result=decodeRuntimeMatchHistoryPrefixHandoff(text);}
    catch(error){throw latch(error);}
    const after=check(status());
    if(!sameJsonValue(before,after))
      throw latch(Error('Reading confirmed-prefix handoff changed native owner status'));
    const h=before.metadata;
    if(result.finishRequestSequence>before.requestSequence||
      (result.rebaseRequestSequence!==0&&result.rebaseRequestSequence>before.requestSequence))
      throw latch(Error('Confirmed-prefix handoff request sequence is ahead of its owner'));
    if(result.rebaseRequestSequence===0){
      if(![23,24,25,26].includes(h.phase)||h.preFrame!==result.oldPreFrame||
        h.currentFrame!==result.confirmedFrame||h.incarnation!==result.oldIncarnation)
        throw latch(Error('Confirmed-prefix handoff differs from its retired CPU prefix owner'));
    }else if(before.expectedFrame!==result.confirmedFrame||
      before.completeSequence<result.rebaseRequestSequence){
      throw latch(Error('Confirmed-prefix handoff differs from its fresh PRE rebase owner'));
    }
    return result;
  };
  const readPrefixOrigin=ordinal=>{
    u32(ordinal,'Confirmed-prefix suffix ordinal');
    const handoff=readPrefixHandoff();
    if(ordinal>=handoff.suffixCount)
      throw Error('Confirmed-prefix origin row ordinal is outside the retained suffix');
    const sizeExport=module[PREFIX+'prefix_origin_row_bytes'];
    const dataExport=module[PREFIX+'prefix_origin_row_data'];
    if(typeof sizeExport!=='function'||typeof dataExport!=='function')
      throw Error('Private confirmed-prefix origin-row exports are unavailable');
    const before=check(status());
    if(sizeExport()!==INPUT_ROW_BYTES)
      throw latch(Error('Native confirmed-prefix origin row ABI differs from the 176-byte receipt'));
    const pointer=dataExport(ordinal);
    if(!pointer)throw latch(Error(`Native confirmed-prefix origin row ${ordinal} is unavailable`));
    const bytes=read(pointer,INPUT_ROW_BYTES);
    let row;
    try{row=decodeRuntimeMatchHistoryEpisodeInputRow(bytes);}
    catch(error){throw latch(error);}
    const after=check(status());
    if(!sameJsonValue(before,after)||
      (handoff.rebaseRequestSequence===0&&
        (after.metadata.preFrame!==handoff.oldPreFrame||
         after.metadata.incarnation!==handoff.oldIncarnation))||
      (handoff.rebaseRequestSequence!==0&&after.expectedFrame!==handoff.confirmedFrame)||
      row.sourceFrame!==handoff.confirmedFrame+ordinal||
      row.localPort!==handoff.localPort||
      row.remotePort!==handoff.remotePort||row.state===0)
      throw latch(Error('Confirmed-prefix origin row differs from its immutable old suffix owner'));
    row.ordinal=row.sourceFrame-handoff.oldPreFrame;
    row.suffixOrdinal=ordinal;
    return row;
  };
  const readEpisodeRow=ordinal=>{
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
  };
  const readTerminalReceipt=()=>{
    const before=check(status());
    if(!arenaMode||before.ownerState!==1||before.pendingCommand||before.endpointActive||
       !before.completeSequence||before.completeSequence!==before.requestSequence||
       before.lastCompletedCommand>=26)
      throw Error('Runtime terminal receipt requires its completed arena owner');
    if(episodeExport('terminal_receipt_bytes')()!==104)
      throw Error('Native runtime terminal receipt is unavailable or has an invalid extent');
    const pointer=episodeExport('terminal_receipt_data')();
    if(!pointer)throw Error('Native runtime terminal receipt is unavailable');
    const receipt=decodeRuntimeMatchHistoryTerminalReceipt(read(pointer,104));
    const after=check(status());
    if(!sameJsonValue(before,after)||
       (receipt.flags!==0&&(receipt.preSourceFrame!==before.metadata.preFrame||
        receipt.incarnation!==before.metadata.incarnation))||
       (receipt.flags===3&&(receipt.selectedSourceFrame!==before.metadata.currentFrame||
        receipt.localPort!==before.localPort||receipt.remotePort!==before.remotePort)))
      throw Error('Native runtime terminal receipt changed its completed owner');
    return receipt;
  };
  const bindTerminalCandidate=(before,after,ticketId,planBytes,replayed)=>{
    const receipt=readTerminalReceipt();
    const rowOrdinal=before.metadata.currentFrame-before.metadata.preFrame;
    if(receipt.flags!==3||receipt.selectedOrdinal!==before.metadata.currentFrame+1||
       receipt.selectedSourceFrame!==before.metadata.currentFrame||receipt.ticketId!==ticketId||
       after.metadata.currentFrame!==receipt.selectedSourceFrame)
      throw Error('Selected terminal traversal does not match the completed ticket');
    const row=readEpisodeRow(rowOrdinal);
    if(row.ticketId!==ticketId||row.pollSerial!==receipt.pollSerial||row.state!==(replayed?2:1)||
       row.sourceFrame!==receipt.selectedSourceFrame||row.incarnation>receipt.incarnation||
       !sameBytes(row.selected,planBytes)||(!replayed&&!sameBytes(row.forward,planBytes)))
      throw Error('Selected terminal traversal differs from the retained applied row');
    if(!replayed){
      const polled=adoptedPrefixTicket??lastPolledEpisodeTicket;
      if(!polled||polled.ticketId!==row.ticketId||polled.pollSerial!==row.pollSerial||
         polled.sourceFrame!==row.sourceFrame||polled.incarnation!==row.incarnation||
         polled.localPort!==row.localPort||polled.remotePort!==row.remotePort||
         !sameBytes(polled.raw,row.raw))
        throw Error('Selected terminal forward differs from its copied actual poll ticket');
    }
    terminalCandidate=Object.freeze({inputOrdinal:receipt.selectedOrdinal,
      sourceFrame:receipt.selectedSourceFrame,rowOrdinal,ticket:row,
      appliedPad:planBytes.slice(),receipt,status:after});
  };
  const readTerminalTailDescriptor=()=>{
    if(!arenaMode||!terminalTailActive||!terminalCandidate)
      throw Error('Terminal-tail descriptor requires its selected retired terminal owner');
    const before=check(status()),getter=module[PREFIX+'terminal_tail_json'];
    if(typeof getter!=='function'||before.ownerState!==1||before.pendingCommand||before.gpuPending||
       before.endpointActive||before.completeSequence!==before.requestSequence||
       ![36,37,38,39,40].includes(before.metadata.phase))
      throw Error('Terminal-tail descriptor requires an idle active native tail');
    const pointer=getter();
    if(!Number.isInteger(pointer)||pointer<=0||pointer>UINT32_MAX)
      throw latch(Error('Native terminal-tail descriptor is unavailable'));
    const text=module.UTF8ToString(pointer>>>0); // Copy borrowed native text before another export.
    let receipt;
    try{receipt=decodeRuntimeMatchHistoryTerminalTail(text);}
    catch(error){throw latch(error);}
    const after=check(status());
    if(!sameJsonValue(before,after)||receipt.requestSequence!==before.completeSequence||
       receipt.sourceFrame!==terminalCandidate.sourceFrame||
       receipt.selectedSourceFrame!==terminalCandidate.sourceFrame||
       receipt.selectedOrdinal!==terminalCandidate.inputOrdinal||
       receipt.selectedTicketId!==terminalCandidate.ticket.ticketId||
       receipt.selectedPollSerial!==terminalCandidate.ticket.pollSerial||
       ['preSourceFrame','preOrdinal','preCompletedTick','firstSourceFrame','firstOrdinal',
         'firstCompletedTick','selectedCompletedTick','worldGeneration','contextEpoch'].some(
           field=>receipt[field]!==terminalCandidate.receipt[field])||
       receipt.localPort!==terminalCandidate.ticket.localPort||
       receipt.remotePort!==terminalCandidate.ticket.remotePort)
      throw latch(Error('Native terminal-tail descriptor differs from the retained first-terminal forward'));
    return receipt;
  };
  const readTerminalTailTicket=()=>{
    if(!terminalTailActive)throw Error('Terminal-tail ticket requires its active tail owner');
    const before=check(status()),size=module[PREFIX+'terminal_tail_ticket_bytes'],data=module[PREFIX+'terminal_tail_ticket_data'];
    if(typeof size!=='function'||typeof data!=='function'||before.ownerState!==1||
       before.pendingCommand||before.gpuPending||before.endpointActive||
       before.completeSequence!==before.requestSequence||
       ![37,38,39,36,40].includes(before.metadata.phase)||
       ![27,28,29,5].includes(before.lastCompletedCommand))
      throw Error('Terminal-tail ticket requires a completed native ticket or forward');
    if(size()!==160)throw latch(Error('Native terminal-tail ticket ABI differs from its 160-byte controller'));
    const pointer=data();
    if(!Number.isInteger(pointer)||pointer<=0||pointer>UINT32_MAX)
      throw latch(Error('Native terminal-tail ticket is unavailable'));
    const ticket=decodeRuntimeMatchHistoryTerminalTailTicket(read(pointer,160));
    const after=check(status());
    const descriptor=readTerminalTailDescriptor();
    const expectedOrdinal=before.metadata.phase===37?descriptor.consumedOrdinal:
      before.metadata.phase===36?descriptor.consumedOrdinal-1:descriptor.consumedOrdinal-1;
    const expectedTick=before.metadata.phase===37?BigInt(descriptor.completedTick):
      BigInt(descriptor.completedTick)-1n;
    if(!sameJsonValue(before,after)||ticket.sourceFrame!==descriptor.sourceFrame||
       ticket.inputOrdinal!==expectedOrdinal||BigInt(ticket.completedTick)!==expectedTick||
       ticket.localPort!==descriptor.localPort||ticket.remotePort!==descriptor.remotePort||
       (before.metadata.phase===37&&ticket.requestSequence!==before.completeSequence))
      throw latch(Error('Native terminal-tail ticket differs from its current source cursor'));
    terminalTailLastTicket=ticket;
    return ticket;
  };
  const readTerminalTailFinal=()=>{
    if(!terminalTailActive||!terminalTailLastTicket)
      throw Error('Terminal-final receipt requires a copied final tail ticket');
    const before=check(status());
    if(before.ownerState!==1||before.pendingCommand||before.gpuPending||before.endpointActive||
       before.completeSequence!==before.requestSequence||before.metadata.phase!==40||
       before.lastCompletedCommand!==5)
      throw Error('Terminal-final receipt requires the completed final present and idle fence');
    const getter=module[PREFIX+'terminal_tail_final_json'];
    if(typeof getter!=='function')throw Error('Private terminal-final JSON export is unavailable');
    const pointer=getter();
    if(!Number.isInteger(pointer)||pointer<=0||pointer>UINT32_MAX)
      throw latch(Error('Native terminal-final receipt is unavailable'));
    const text=module.UTF8ToString(pointer>>>0); // Copy borrowed JSON immediately.
    let receipt;
    try{receipt=decodeRuntimeMatchHistoryTerminalFinal(text);}
    catch(error){throw latch(error);}
    const extent=module[PREFIX+'terminal_tail_checksum_bytes'];
    const checksumData=module[PREFIX+'terminal_tail_checksum_data'];
    if(typeof extent!=='function'||typeof checksumData!=='function'||extent()!==64)
      throw latch(Error('Native terminal checksum ABI differs from its 64-byte record'));
    const checksumPointer=checksumData();
    if(!Number.isInteger(checksumPointer)||checksumPointer<=0||checksumPointer>UINT32_MAX)
      throw latch(Error('Native terminal checksum is unavailable'));
    const checksum=read(checksumPointer,64); // Copy borrowed record before reading status.
    const descriptor=readTerminalTailDescriptor();
    const ticket=readTerminalTailTicket();
    const record=new DataView(checksum.buffer,checksum.byteOffset,checksum.byteLength);
    const after=check(status());
    if(!sameJsonValue(before,after)||receipt.sourceFrame!==descriptor.sourceFrame||
       ['firstSourceFrame','firstOrdinal','firstCompletedTick','selectedSourceFrame',
         'selectedOrdinal','selectedCompletedTick'].some(field=>receipt[field]!==descriptor[field])||
       receipt.consumedOrdinal!==descriptor.consumedOrdinal||
       receipt.lastInputOrdinal+1!==receipt.consumedOrdinal||
       receipt.ticketId!==ticket.ticketId||receipt.requestSequence!==ticket.requestSequence||
       receipt.pollSerial!==ticket.pollSerial||receipt.localPort!==ticket.localPort||
       receipt.remotePort!==ticket.remotePort||receipt.completedTick!==descriptor.completedTick||
       receipt.worldGeneration!==descriptor.worldGeneration||receipt.contextEpoch!==descriptor.contextEpoch||
       receipt.checksumTick!==receipt.lastInputOrdinal||receipt.checksumFrame!==receipt.sourceFrame||
       record.getUint32(0,true)!==receipt.lastInputOrdinal||record.getUint32(4,true)!==3||
       record.getUint32(12,true)!==receipt.sourceFrame)
      throw latch(Error('Native final result, source cursor, ticket, owner, or checksum record differs'));
    terminalTailFinalCopy=Object.freeze({receipt,checksum:checksum.slice(),ticket});
    return Object.freeze({receipt,checksum:checksum.slice(),ticket});
  };
  const pacingWaitTransition=(begin,kind,inputOrdinal)=>{
    active();
    u32(inputOrdinal,'Network wait input ordinal');
    if(!pacingStarted||!arenaMode||!inlineMode||![1,2,3,4].includes(kind)||
       begin===Boolean(activePacingWait)||
       (!begin&&(activePacingWait.kind!==kind||activePacingWait.inputOrdinal!==inputOrdinal)))
      throw Error('Network wait requires the exact active native pacing owner and missing input');
    const name=begin?'pacing_wait_begin':'pacing_wait_end';
    const transition=episodeExport(name),getReceipt=episodeExport('pacing_wait_json');
    const before=check(status());
    if(before.ownerState!==1||before.pendingCommand||before.gpuPending||before.endpointActive||
       before.requestSequence!==before.completeSequence)
      throw Error('Network wait requires a completed quiescent native owner');
    if(transition(kind,inputOrdinal)!==1){
      if(begin){
        const sample=module[PREFIX+'pacing_sample'];
        const pointer=typeof sample==='function'?sample(0):0;
        if(pointer){
          const receipt=decodeRuntimeMatchHistoryPacing(read(pointer,64));
          if(receipt.stalled){
            appendJournal('pacing',{budget:0,...receipt});
            const error=Error(`Native pacing clock stalled: reason ${receipt.stallReason}, value ${receipt.triggeringValue}, threshold ${receipt.threshold}`);
            error.pacingSample=receipt;
            throw latch(error);
          }
        }
      }
      throw latch(Error(`Native pacing network wait ${begin?'begin':'end'} refused: ${errorText()}`));
    }
    const pointer=getReceipt();
    if(!Number.isInteger(pointer)||pointer<=0||pointer>UINT32_MAX)
      throw latch(Error('Native pacing network wait receipt is unavailable'));
    let receipt;
    try{receipt=JSON.parse(module.UTF8ToString(pointer));}
    catch(error){throw latch(Error('Native pacing network wait receipt is malformed',{cause:error}));}
    const after=check(status());
    const expectedTotal=pacingWaitJournal.at(-1)?.totalExcludedMs??0;
    if(!hasExactKeys(receipt,['schema','active','kind','inputOrdinal','sourceFrame','phase',
      'requestSequence','excludedMs','totalExcludedMs','pendingTicks'])||receipt.schema!==1||
      receipt.active!==Number(begin)||receipt.kind!==kind||receipt.inputOrdinal!==inputOrdinal||
      receipt.sourceFrame!==before.metadata.currentFrame||receipt.phase!==before.metadata.phase||
      receipt.requestSequence!==before.completeSequence||!sameJsonValue(before,after)||
      ![receipt.excludedMs,receipt.totalExcludedMs,receipt.pendingTicks].every(value=>Number.isFinite(value)&&value>=0)||
      (begin?(receipt.excludedMs!==0||receipt.totalExcludedMs!==expectedTotal):
        (receipt.pendingTicks!==activePacingWait.pendingTicks||
         receipt.totalExcludedMs!==expectedTotal+receipt.excludedMs)))
      throw latch(Error('Native pacing network wait receipt changed owner, duration, or debt'));
    activePacingWait=begin?receipt:null;
    appendJournal('networkWaits',receipt);
    return {...receipt};
  };
  const inlineCall=async(name,...args)=>{
    nativeCallPending=true;
    try{return await module.ccall(PREFIX.slice(1)+name,'number',args.map(()=>'number'),args,{async:true});}
    finally{nativeCallPending=false;}
  };
  const requireInlineRequest=async(name,...args)=>{
    if(typeof module[PREFIX+name]!=='function')
      throw Error(`Private inline runtime history export ${name} is unavailable`);
    if(await inlineCall(name,...args)!==1)
      throw Error(`Native history ${name} refused: ${errorText()}`);
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
  const deadlineError=(label,detail)=>
    latch(Error(`Runtime history ${label} exceeded its ${commandTimeoutMs} ms deadline${detail?.()??''}`));
  const wait=async(predicate,deadline,label,allowFailure=false,onBlocked=null,detail=null,acceptLateCompletion=false,waitReceipt=null)=>{
    let waiterStartedAt=null;
    const updateElapsed=()=>{
      if(waitReceipt&&waiterStartedAt!==null)
        waitReceipt.elapsedMs=Math.max(0,performance.now()-waiterStartedAt);
    };
    const noteWake=(kind,started)=>{
      if(!waitReceipt)return;
      if(waiterStartedAt===null)waiterStartedAt=started;
      waitReceipt.waitCount++;
      waitReceipt.lastWake=kind;
      if(kind==='event')waitReceipt.eventWakeCount++;
      else waitReceipt.poll5msWakeCount++;
      updateElapsed();
    };
    for(;;){
      const snapshot=check(status(),allowFailure);
      if(inlineMode&&!acceptLateCompletion&&performance.now()>=deadline){
        updateElapsed();throw deadlineError(label,detail);
      }
      if(predicate(snapshot)){updateElapsed();return snapshot;}
      if(performance.now()>=deadline){
        updateElapsed();throw deadlineError(label,detail);
      }
      if(waitReceipt&&waiterStartedAt===null)waiterStartedAt=performance.now();
      let completionTarget=null;
      if(onBlocked){
        const blocked=onBlocked(snapshot);
        completionTarget=inlineMode?await blocked:blocked;
        if(performance.now()>=deadline){
          updateElapsed();throw deadlineError(label,detail);
        }
      }
      if(completionTarget&&waitReceipt){
        if(waitReceipt.target===null)waitReceipt.target=completionTarget;
        else if(!queueCompletionMatches(waitReceipt.target,completionTarget))completionTarget=null;
      }
      if(completionTarget){
        const waitStarted=performance.now();
        const notified=await waitForQueueCompletion(completionTarget,deadline);
        if(notified===null){
          const pollStarted=performance.now();
          await new Promise(resolve=>setTimeout(resolve,5));
          noteWake('poll5ms',pollStarted);
        }else{
          noteWake(notified?'event':'poll5ms',waitStarted);
        }
      }else{
        const pollStarted=performance.now();
        await new Promise(resolve=>setTimeout(resolve,5));
        noteWake('poll5ms',pollStarted);
      }
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
    installQueueCompletionObserver();
    return Object.freeze({
    mode,
    status,
    journal:()=>structuredClone(journal),
    pacingJournal:()=>structuredClone(pacingJournal),
    pacingWaitJournal:()=>structuredClone(pacingWaitJournal),
    journalRetention,
    pacingWaitBegin(kind,inputOrdinal){return pacingWaitTransition(true,kind,inputOrdinal);},
    pacingWaitEnd(kind,inputOrdinal){return pacingWaitTransition(false,kind,inputOrdinal);},
    sourceRefillBoundary(frame){
      active();u32(frame,'Refill boundary source frame');
      if(diagnosticMode||!pacingStarted||activePacingWait)
        throw Error('Refill boundary requires the live staging pacing owner');
      const observe=episodeExport('refill_boundary');
      const boundary=observe(frame);
      if(boundary!==1&&boundary!==2)
        throw latch(Error(`Native refill boundary refused at source frame ${frame}`));
      return boundary===2?'owned-pending':'quiet';
    },

    pacingStart(){
      active();
      if(pacingStarted||!arenaMode||!inlineMode||!captured)
        throw Error('Pacing requires a once-only prepared inline arena owner');
      const start=module[PREFIX+'pacing_start'];
      if(typeof start!=='function'||start()!==1)
        throw latch(Error(`Native pacing start refused: ${errorText()}`));
      pacingStarted=true;
    },
    pacingSample(budget){
      active();
      if(!pacingStarted||![0,1].includes(budget))throw Error('Pacing requires a started owner and zero/one budget');
      if(activePacingWait)throw Error('Pacing sample is unavailable during an owned network wait');
      const sample=module[PREFIX+'pacing_sample'],size=module[PREFIX+'pacing_sample_bytes'];
      if(typeof sample!=='function'||typeof size!=='function'||size()!==64)
        throw latch(Error('Native pacing exports or receipt extent unavailable'));
      const pointer=sample(budget);
      if(!pointer)throw latch(Error(`Native pacing sample refused: ${errorText()}`));
      // Preserve the clock's first failure before status() can turn it into a
      // generic native owner error. The clock itself never auto-resumes.
      const receipt=decodeRuntimeMatchHistoryPacing(read(pointer,64));
      appendJournal('pacing',{budget,...receipt});
      if(receipt.stalled){
        const error=Error(`Native pacing clock stalled: reason ${receipt.stallReason}, value ${receipt.triggeringValue}, threshold ${receipt.threshold}`);
        error.pacingSample=receipt;throw latch(error);
      }
      const after=check(status());
      if(after.metadata.phase!==receipt.phase||after.metadata.currentFrame!==receipt.currentFrame)
        throw latch(Error('Pacing sample changed its source owner'));
      return receipt;
    },
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
    enableArenaHistory(){
      active();
      if(captured)throw Error('Arena history must be enabled before PRE capture');
      if(episodeMode)throw Error('Runtime episode mode is already enabled');
      check(status());
      episodeExport('arena_enable');
      if(diagnosticMode){
        episodeExport('cpu_arena_endpoint_data');
        episodeExport('cpu_arena_endpoint_bytes');
      }
      allocateFrameArgument();
      requireEpisodeSuccess('arena_enable');
      episodeMode=true;arenaMode=true;
      check(status());
      return true;
    },
    prefixHandoff(){
      active();
      return readPrefixHandoff();
    },
    prefixOrigin(ordinal){
      active();
      return readPrefixOrigin(ordinal);
    },
    terminalTailBegin(selectedOrdinal){
      active();
      selectedOrdinal=u32(selectedOrdinal,'Selected terminal input ordinal');
      const candidate=terminalCandidate;
      if(!candidate||candidate.inputOrdinal!==selectedOrdinal)
        throw Error('Terminal-tail begin requires the retained selected terminal forward');
      const row=this.receipt(candidate.rowOrdinal);
      if(row.ordinal!==candidate.rowOrdinal||row.sourceFrame!==candidate.sourceFrame||
         row.ticketId!==candidate.ticket.ticketId||row.incarnation!==candidate.ticket.incarnation||
         row.pollSerial!==candidate.ticket.pollSerial||row.state!==candidate.ticket.state||
         !sameBytes(row.raw,candidate.ticket.raw)||!sameBytes(row.forward,candidate.ticket.forward)||
         !sameBytes(row.selected,candidate.appliedPad))
        throw Error('Terminal-tail begin lost the exact actual first-terminal receipt');
      return this.command('terminalTailBegin',selectedOrdinal);
    },
    requestTerminalTailTicket(){return this.command('terminalTailTicket');},
    forwardTerminalTailTicket(){
      if(!terminalTailLastTicket)throw Error('Terminal-tail forward has no copied native ticket');
      return this.command('terminalTailForward',terminalTailLastTicket.requestSequence);
    },
    presentTerminalTail(){return this.command('terminalTailPresent');},
    terminalTailDescriptor(){
      active();
      return readTerminalTailDescriptor();
    },
    terminalTailTicket(){
      active();
      return readTerminalTailTicket();
    },
    terminalTailSetRemotePad(value){
      active();
      if(!terminalTailActive)throw Error('Terminal-tail remote PAD requires its active native owner');
      const bytes=pad(value),before=check(status());
      if(before.metadata.phase!==37||before.lastCompletedCommand!==27||
         before.pendingCommand||before.inputReady!==1||before.completeSequence!==before.requestSequence)
        throw Error('Terminal-tail remote PAD requires its exact completed native ticket');
      const admit=module[PREFIX+'terminal_tail_remote_pad'];
      if(typeof admit!=='function')throw Error('Native terminal-tail remote PAD export is unavailable');
      const pointer=writePadArgument(bytes);
      if(admit(pointer,PAD_BYTES)!==1)throw latch(Error(`Native terminal-tail remote PAD refused: ${errorText()}`));
      const after=check(status());
      if(!sameJsonValue(before,after))throw latch(Error('Terminal-tail remote PAD admission changed its native command status'));
      return true;
    },
    terminalTailFinal(){
      active();
      return readTerminalTailFinal();
    },
    finishTerminalTail(agreement){
      active();
      const final=terminalTailFinalCopy??readTerminalTailFinal();
      if(!agreement||agreement.input_ordinal!==final.receipt.lastInputOrdinal||
         agreement.source_frame!==final.receipt.sourceFrame||
         agreement.outcome!==final.receipt.outcome||agreement.winner!==final.receipt.winner||
         agreement.accepted_by!=='lockstep-match-final'||
         typeof agreement.agreement_hash!=='string'||!/^[0-9a-f]{64}$/.test(agreement.agreement_hash)||
         agreement.matched_bytes!==64)
        throw Error('Terminal-tail finish requires the exact completed peer final agreement');
      terminalTailAgreementConfirmed=true;
      return this.command('finishTerminalTail',{consumedOrdinal:final.receipt.consumedOrdinal,
        lastInputOrdinal:final.receipt.lastInputOrdinal});
    },
    enableInlineDispatch(){
      active();
      if(!arenaMode||captured||inlineMode)
        throw Error('Inline dispatch requires explicit arena history before PRE capture and can only be enabled once');
      const snapshot=check(status());
      if(snapshot.ownerState!==1||snapshot.pendingCommand||snapshot.requestSequence||snapshot.endpointActive)
        throw Error('Inline dispatch requires a fresh terminal takeover owner');
      if(typeof module.ccall!=='function')throw Error('Inline dispatch requires Asyncify ccall');
      requireEpisodeSuccess('inline_dispatch_enable');
      inlineMode=true;
      check(status());
      return true;
    },
    startEnable(role){
      const name=role===undefined?'start_enable':'start_lobby_enable';
      if(role!==undefined&&role!=='alpha'&&role!=='beta')
        throw Error('Online shared selection requires the alpha or beta role');
      startPreflight(name,{inspectState:true});
      const result=role===undefined?startExport(name)():startExport(name)(role==='alpha'?1:2);
      if(result!==1)startRefusal(name);
      const state=readStartState();
      if(state!==1)throw latch(Error(`Native agreed-start enable completed in state ${state}, expected 1`));
      return state;
    },
    startState(){
      return readStartState();
    },
    startChoice(){
      const name='start_choice_json';
      startPreflight(name);
      const pointer=startExport(name)();
      if(!pointer){
        check(status());
        return null;
      }
      try{
        const choice=JSON.parse(startTextResult(name,pointer));
        if(!hasExactKeys(choice,['character','costume']))
          throw Error('Native online choice has unexpected fields');
        u32(choice.character,'Native online character');
        u32(choice.costume,'Native online costume');
        return Object.freeze(choice);
      }catch(error){throw latch(error);}
    },
    startSelectChoices(alpha,beta){
      const name='start_select_choices';
      for(const choice of [alpha,beta]){
        if(!hasExactKeys(choice,['character','costume']))
          throw Error('Online choice requires a character and costume');
        u32(choice.character,'Online character');
        u32(choice.costume,'Online costume');
      }
      startPreflight(name);
      if(startExport(name)(alpha.character,alpha.costume,beta.character,beta.costume)!==1)
        startRefusal(name);
      check(status());
      return true;
    },
    startSelectStage(stage){
      const name='start_select_stage';
      u32(stage,'Online host stage');
      startPreflight(name);
      if(startExport(name)(stage)!==1)startRefusal(name);
      check(status());
      return true;
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
    ...(diagnosticMode?{remotePad(value){active();if(episodeMode)throw Error('Legacy remote PAD injection is disabled in episode mode');
      const bytes=pad(value);const pointer=writePadArgument(bytes);
      requireSuccess('remote_pad',pointer,PAD_BYTES);}}:{}),
    ticket(){
      active();if(!episodeMode)throw Error('Episode poll tickets require explicit episode mode');
      if(unclaimedPrefixTicket||adoptedPrefixTicket)
        throw Error('Adopted suffix tickets require their paired adoptedTicket path and forward');
      if(confirmedPrefixActive){
        const handoff=readPrefixHandoff();
        if(!handoff.rebaseRequestSequence||handoff.suffixCursor<handoff.suffixCount)
          throw Error('Retained suffix tickets are read only through adoptedTicket');
      }
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
      lastPolledEpisodeTicket=ticket;
      return ticket;
    },
    adoptedTicket(originalRow){
      active();
      if(!arenaMode||!episodeMode)
        throw Error('Adopted prefix tickets require explicit arena history mode');
      const handoff=readPrefixHandoff(),before=check(status());
      const cursor=handoff.suffixCursor;
      if(!unclaimedPrefixTicket||!captured||!handoff.rebaseRequestSequence||cursor>=handoff.suffixCount||
        before.ownerState!==1||before.expectedFrame!==handoff.confirmedFrame||
        ![4,21].includes(before.metadata.phase)||
        before.metadata.currentFrame!==handoff.confirmedFrame+cursor||
        !before.metadata.incarnation||before.metadata.restoreCount!==0||
        before.pendingCommand||before.completeSequence!==before.requestSequence||
        before.lastCompletedCommand!==MATCH_HISTORY_COMMAND.adoptNextPrefixTicket||
        before.inputReady!==1)
        throw latch(Error('Adopted prefix ticket requires the exact completed native suffix handoff'));
      const origin=readPrefixOrigin(cursor);
      if(!originalRow||originalRow.suffixOrdinal!==cursor||
        originalRow.ticketId!==origin.ticketId||originalRow.sourceFrame!==origin.sourceFrame||
        originalRow.incarnation!==origin.incarnation||originalRow.pollSerial!==origin.pollSerial||
        originalRow.localPort!==origin.localPort||originalRow.remotePort!==origin.remotePort||
        !sameBytes(originalRow.rawBytes,origin.rawBytes))
        throw latch(Error('Adopted prefix ticket was not paired with its exact immutable origin row'));
      if(episodeExport('ticket_bytes')()!==TICKET_BYTES)
        throw latch(Error('Native adopted ticket ABI differs from its 88-byte poll ticket'));
      const pointer=episodeExport('ticket_data')();
      if(!pointer)throw latch(Error('Native adopted prefix ticket is unavailable'));
      const ticket=decodeRuntimeMatchHistoryPollTicket(read(pointer,TICKET_BYTES));
      const after=check(status());
      const afterHandoff=readPrefixHandoff();
      if(!sameJsonValue(before,after)||afterHandoff.suffixCursor!==cursor||
        !sameJsonValue({...handoff,suffixCursor:afterHandoff.suffixCursor},afterHandoff)||
        ticket.ticketId!==origin.ticketId||ticket.sourceFrame!==origin.sourceFrame||
        ticket.incarnation!==before.metadata.incarnation||
        ticket.localPort!==handoff.localPort||ticket.remotePort!==handoff.remotePort||
        ticket.pollSerial!==origin.pollSerial||after.metadata.pollSerial!==origin.pollSerial||
        !sameBytes(ticket.raw,origin.raw))
        throw latch(Error('Native adopted prefix ticket differs from its preserved original poll receipt'));
      ticket.suffixOrdinal=cursor;
      adoptedPrefixTicket={ticketId:ticket.ticketId,sourceFrame:ticket.sourceFrame,
        incarnation:ticket.incarnation,localPort:ticket.localPort,remotePort:ticket.remotePort,
        pollSerial:ticket.pollSerial,raw:ticket.raw.slice(),suffixOrdinal:cursor};
      unclaimedPrefixTicket=false;
      return ticket;
    },
    receipt(ordinal){
      active();if(!episodeMode)throw Error('Episode input receipts require explicit episode mode');
      return readEpisodeRow(ordinal);
    },
    terminalReceipt(){
      active();
      return readTerminalReceipt();
    },
    ...(diagnosticMode?{input(){
      active();if(episodeMode)throw Error('Episode mode exposes retained poll tickets and input receipts');
      const snapshot=check(status());
      const result=decodeRuntimeMatchHistoryInput(read(module[PREFIX+'input'](),INPUT_BYTES));
      if(result.pollSerial!==snapshot.metadata.pollSerial||result.localPort!==snapshot.localPort||result.remotePort!==snapshot.remotePort)
        throw latch(Error('Native input receipt differs from its live owner'));
      return result;
    },endpoint(request){
      active();check(status());
      if(arenaMode){
        if(request?.arenaHistoryEndpoint===false||request?.particleCellEndpoint===false)
          throw Error('Arena history requires its typed arena and particle receipts');
        return collectCpuEndpointInPage({...request,arenaHistoryEndpoint:true,particleCellEndpoint:true},
          {runtime:'match-history',module});
      }
      if(request?.arenaHistoryEndpoint)throw Error('Arena history was not enabled before capture');
      return collectCpuEndpointInPage(request,{runtime:'match-history',module});
    }}:{}),
    admitEndpoint({view,provenance}){
      active();
      const terminalEndpoint=view===4&&provenance===4&&terminalCandidate&&
        [31,33].includes(check(status()).metadata.phase);
      if(!arenaMode||!inlineMode||!captured||
         (!( [1,2,3].includes(view)&&[1,2,3].includes(provenance))&&!terminalEndpoint))
        throw Error('Native endpoint admission requires an inline arena owner and explicit view/provenance');
      const before=check(status()),started=performance.now();
      if(before.pendingCommand||before.endpointActive||before.gpuPending||
          before.completeSequence!==before.requestSequence)
        throw Error('Native endpoint admission requires an idle owner');
      try{
        const admit=module[PREFIX+'cpu_endpoint_admit'];
        if(typeof admit!=='function')
          throw Error('Native atomic endpoint admission export is unavailable');
        if(admit(view,provenance)!==0)
          throw Error(`Native endpoint admission refused: ${errorText()}`);
        const after=check(status());
        if(after.requestSequence!==before.requestSequence||after.completeSequence!==before.completeSequence||
            after.expectedFrame!==before.expectedFrame||after.metadata.currentFrame!==before.metadata.currentFrame||
            after.localPort!==before.localPort||after.remotePort!==before.remotePort||
            after.ownerState!==1||after.endpointActive||after.gpuPending)
          throw Error('Native endpoint admission changed its source owner');
        return {scope:'native-typed-admission-only',rawRowsPublished:false,view,provenance,
          elapsedMs:performance.now()-started,before,after};
      }catch(error){throw latch(error);}
    },
    ...(diagnosticMode?{
      motor(){active();const size=module[PREFIX+'motor_bytes']();if(size!==1016)throw Error('Runtime motor evidence ABI');return read(module[PREFIX+'motor_data'](),size);},
      motorCommit(){active();const size=module[PREFIX+'motor_commit_bytes']();if(size!==64)throw Error('Runtime motor commit ABI');return read(module[PREFIX+'motor_commit_data'](),size);},
      pcm(){active();const size=module[PREFIX+'pcm_bytes']();if(!Number.isInteger(size)||size<8||size>1068*4||size%8)throw Error('Runtime emitted stereo PCM extent');return read(module[PREFIX+'pcm_data'](),size);},
    }:{}),
    checksum({postDraw=false}={}){
      active();
      if(typeof postDraw!=='boolean')throw Error('Checksum endpoint kind must be explicit');
      const data=module[PREFIX+'checksum_data'],extent=module[PREFIX+'checksum_bytes'];
      if(typeof data!=='function'||typeof extent!=='function')
        throw Error('Private runtime history checksum exports are unavailable');
      try{
        const before=check(status()),history=before.metadata;
        const corrected=history.phase===13||(postDraw&&arenaMode&&history.phase===16);
        const confirmedPrefix=arenaMode&&history.phase===24; // PrefixCpuCopied
        const presented=postDraw&&arenaMode&&
          ((history.phase===28&&history.endpointMask===(1<<8))||
           (history.phase===16&&(history.endpointMask&96)===96));
        const span=history.currentFrame-history.preFrame;
        const episodeStepsOkay=episodeMode&&span>0&&span<=7&&history.forwardSteps===span&&
          (corrected?history.replaySteps===span:history.replaySteps===0);
        let prefixHandoff=null;
        let prefixStepsOkay=false;
        if(confirmedPrefix){
          prefixHandoff=readPrefixHandoff();
          prefixStepsOkay=prefixHandoff.rebaseRequestSequence===0&&
            prefixHandoff.oldPreFrame===history.preFrame&&
            prefixHandoff.confirmedFrame===history.currentFrame&&
            prefixHandoff.selectedEndFrame>history.currentFrame&&
            history.sealedEnd===history.currentFrame&&
            history.forwardSteps===prefixHandoff.selectedEndFrame-prefixHandoff.oldPreFrame&&
            history.replaySteps===prefixHandoff.confirmedFrame-prefixHandoff.oldPreFrame;
          const rowOrdinal=prefixHandoff.confirmedFrame-prefixHandoff.oldPreFrame-1;
          if(!prefixStepsOkay||rowOrdinal<0)
            throw Error('Confirmed-prefix checksum lacks its exact CPU-only replay interval');
          if(episodeExport('input_row_bytes')()!==INPUT_ROW_BYTES)
            throw Error('Confirmed-prefix source-boundary row ABI differs from its 176-byte receipt');
          const rowPointer=episodeExport('input_row_data')(rowOrdinal);
          if(!rowPointer)throw Error('Confirmed-prefix C−1 source-boundary row is unavailable');
          const sourceRow=decodeRuntimeMatchHistoryEpisodeInputRow(read(rowPointer,INPUT_ROW_BYTES));
          if(sourceRow.sourceFrame!==prefixHandoff.confirmedFrame-1||sourceRow.state!==2||
            sourceRow.incarnation>prefixHandoff.oldIncarnation||
            sourceRow.localPort!==prefixHandoff.localPort||sourceRow.remotePort!==prefixHandoff.remotePort||
            !sameJsonValue(check(status()),before))
            throw Error('Confirmed-prefix checksum requires the exact old-incarnation selected C−1 input row');
        }
        const legacyStepsOkay=!episodeMode&&span===1&&history.forwardSteps===1&&
          history.replaySteps===(corrected?1:0);
        if(before.ownerState!==1||before.pendingCommand||before.gpuPending||before.endpointActive||
          before.inputReady!==1||before.completeSequence!==before.requestSequence||
          (postDraw?!presented:(history.phase!==6&&!corrected&&!confirmedPrefix))||history.preFrame!==before.expectedFrame||
          history.preFrame===UINT32_MAX||history.currentFrame<=history.preFrame||
          history.currentFrame!==history.sealedEnd||history.pollSerial==='0'||
          (!episodeStepsOkay&&!prefixStepsOkay&&!legacyStepsOkay))
          throw Error(postDraw?'Runtime history checksum requires an admitted idle presented frontier':
            'Runtime history checksum requires a copied, idle CPU endpoint');
        const size=extent();
        if(size!==64)throw Error('Runtime history checksum ABI must be exactly 64 bytes');
        const pointer=data();
        if(!pointer)throw Error('Native runtime history checksum refused its admitted endpoint');
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
    presentation(){
      active();
      const getter=module[PREFIX+'presentation_json'];
      if(typeof getter!=='function')throw Error('Private runtime history presentation export is unavailable');
      const before=check(status()),command=before.lastCompletedCommand;
      const phase=before.metadata.phase;
      if(!arenaMode||before.ownerState!==1||before.pendingCommand||before.endpointActive||
        !before.completeSequence||before.completeSequence!==before.requestSequence||
        !(command===4&&(phase===7||phase===14)||
          command===19&&(phase===22||phase===30)||command===29&&phase===39))
        throw Error('Runtime history presentation requires a completed draw before idle');
      const pointer=getter();
      if(!Number.isInteger(pointer)||pointer<-0x80000000||pointer>UINT32_MAX||!(pointer>>>0))
        throw Error('Native runtime history presentation receipt is unavailable');
      const receipt=JSON.parse(module.UTF8ToString(pointer>>>0));
      const fields=['schema','command','requestSequence','sourceFrameBefore','sourceFrameAfter',
        'priorSubmission','submission','presentationSerial'];
      if(!hasExactKeys(receipt,fields)||receipt.schema!==1||receipt.command!==command||
        receipt.requestSequence!==before.completeSequence||
        receipt.sourceFrameBefore!==before.metadata.currentFrame||receipt.sourceFrameAfter!==receipt.sourceFrameBefore)
        throw Error('Runtime history presentation receipt differs from completed native draw');
      const prior=canonicalU64(receipt.priorSubmission,'presentation prior submission');
      const submission=canonicalU64(receipt.submission,'presentation submission',{positive:true});
      canonicalU64(receipt.presentationSerial,'presentation serial',{positive:true});
      if(submission<=prior)throw Error('Runtime history presentation submission did not advance');
      const after=check(status());
      if(after.gpuPending>before.gpuPending||!sameJsonValue({...after,gpuPending:before.gpuPending},before))
        throw Error('Runtime history presentation changed its native owner status');
      lastPresentationSubmission=submission.toString();
      return receipt;
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
      const episodeCommands=new Set(['pollTicket','forwardTicket','seal','replayTicket','finishReplay',
        'continue','beginEpisode','recordingPresent','finishPrefix','closePrefix','retirePrefix',
        'rebaseConfirmedPrefix','adoptNextPrefixTicket','sealPresentedFrontier','terminalTailBegin',
        'terminalTailTicket','terminalTailForward','terminalTailPresent','finishTerminalTail']);
      const prefixCommands=new Set(['finishPrefix','closePrefix','retirePrefix',
        'rebaseConfirmedPrefix','adoptNextPrefixTicket']);
      const terminalTailCommands=new Set(['terminalTailBegin','terminalTailTicket',
        'terminalTailForward','terminalTailPresent','finishTerminalTail']);
      const legacyExecution=name==='begin'||name==='forward'||name==='correct'||(name==='rebase'&&!arenaMode);
      let confirmedEnd,ticketId,planBytes,terminalOrdinal,terminalTicketSequence,terminalFinish=null,
        transitionHandoff=null,terminalTailBefore=null,prefixHandoffComplete=false;
      if(name==='rebase'||name==='finishPrefix'||name==='rebaseConfirmedPrefix')confirmedEnd=argument;
      if(name==='rebase'||name==='finishPrefix'||name==='rebaseConfirmedPrefix')
        u32(confirmedEnd,name==='finishPrefix'?'Confirmed prefix frame':'Confirmed END frame');
      else if(name==='terminalTailBegin')terminalOrdinal=u32(argument,'Selected terminal input ordinal');
      else if(name==='terminalTailForward')terminalTicketSequence=u32(argument,'Terminal-tail ticket request sequence');
      else if(name==='finishTerminalTail'){
        if(!hasExactKeys(argument,['consumedOrdinal','lastInputOrdinal']))
          throw Error('Terminal-tail finish requires exactly the native consumed and last-input ordinals');
        terminalFinish={consumedOrdinal:u32(argument.consumedOrdinal,'Terminal-tail consumed ordinal'),
          lastInputOrdinal:u32(argument.lastInputOrdinal,'Terminal-tail last input ordinal')};
      }
      else if(framePayload){
        if(!argument||typeof argument!=='object'||Array.isArray(argument)||
          Object.keys(argument).length!==2||!Object.hasOwn(argument,'ticketId')||!Object.hasOwn(argument,'plan'))
          throw Error('Episode ticket command requires exactly {ticketId, plan}');
        ticketId=u32(argument.ticketId,'Episode ticket ID');
        if(!ticketId||!(argument.plan instanceof Uint8Array)||argument.plan.length!==FRAME_BYTES)
          throw Error('Episode ticket command requires a nonzero ticket ID and exact 44-byte plan');
        planBytes=argument.plan.slice();
      }else if(argument!==undefined)
        throw Error('Only confirmed frame and episode ticket commands take arguments');
      if((name==='recordingPresent'||name==='sealPresentedFrontier')&&!arenaMode)
        throw Error('Recording presentation requires explicit arena history mode');
      if(prefixCommands.has(name)&&!arenaMode)
        throw Error('Confirmed-prefix commands require explicit arena history mode');
      if(terminalTailCommands.has(name)&&(!arenaMode||!episodeMode))
        throw Error('Terminal-tail commands require explicit arena episode mode');
      if(episodeCommands.has(name)&&!episodeMode)
        throw Error('Episode commands require explicit pre-capture episode mode');
      if(episodeMode&&legacyExecution)
        throw Error('Legacy one-window execution commands are disabled in episode mode');
      if(unclaimedPrefixTicket&&name!=='cleanup')
        throw Error('Adopted prefix ticket must be copied before another command');
      if(name!=='cleanup')active();else if(terminal||busy)throw Error('Runtime history cleanup owner unavailable');
      if(activePacingWait&&name!=='cleanup')throw Error('Native commands are disabled during an owned network wait');
      if(terminalTailActive&&!terminalTailCommands.has(name)&&name!=='idle'&&name!=='cleanup')
        throw Error('Ordinary episode continuation is disabled after terminal-tail admission');
      if(name==='terminalTailBegin'&&(!terminalCandidate||terminalOrdinal!==terminalCandidate.inputOrdinal))
        throw Error('Terminal-tail begin must bind the retained actual terminal forward ordinal');
      if(name==='terminalTailTicket'&&!terminalTailActive)
        throw Error('Terminal-tail ticket requires a completed terminal-tail begin');
      if(name==='terminalTailForward'&&(!terminalTailLastTicket||
          terminalTicketSequence!==terminalTailLastTicket.requestSequence))
        throw Error('Terminal-tail forward requires its exact copied ticket request sequence');
      if(name==='terminalTailPresent'&&!terminalTailActive)
        throw Error('Terminal-tail present requires its active tail owner');
      if(name==='finishTerminalTail'&&(!terminalTailActive||!terminalTailFinalCopy||
          !terminalTailAgreementConfirmed||terminalFinish.consumedOrdinal!==terminalTailFinalCopy.receipt.consumedOrdinal||
          terminalFinish.lastInputOrdinal!==terminalTailFinalCopy.receipt.lastInputOrdinal))
        throw Error('Terminal-tail finish requires the matched final receipt and peer ACK');
      if(name==='rebase'&&confirmedPrefixActive){
        const handoff=readPrefixHandoff();
        if(!handoff.rebaseRequestSequence||handoff.suffixCursor!==handoff.suffixCount||
          adoptedPrefixTicket)
          throw Error('Whole-END rebase requires the exhausted confirmed-prefix handoff');
        prefixHandoffComplete=true;
      }
      const waitForRenderer=name==='idle'||name==='cleanup';
      busy=true;const started=performance.now(),deadline=started+commandTimeoutMs;
      const entry={command,name,started,status:'pending'};
      if(waitForRenderer)entry.queueWait={target:null,eventWakeCount:0,poll5msWakeCount:0,
        waitCount:0,elapsedMs:0,lastWake:null};
      appendJournal('commands',entry);
      if(name==='rebase'||name==='finishPrefix'||name==='rebaseConfirmedPrefix')
        entry.confirmedEnd=confirmedEnd;
      try{
        const allowFailure=name==='cleanup';
        const prepare=waitForRenderer?module[PREFIX+'renderer_prepare_json']:null;
        const rendererDetail=()=>entry.rendererPreparation?
          ` (renderer preparation: ${JSON.stringify(entry.rendererPreparation)})`:'';
        const retainPreparation=pointer=>{
          if(!pointer)return null;
          const receipt=JSON.parse(module.UTF8ToString(pointer));
          if(receipt.schema!==1||receipt.command!==command||
             ![0,1,2].includes(receipt.action)||!receipt.before||(receipt.action===1&&!receipt.after))
            throw Error('Native renderer preparation receipt is invalid');
          entry.rendererPreparation??={};
          entry.rendererPreparation.latest=receipt;
          if(receipt.action)entry.rendererPreparation.flush=receipt;
          if(receipt.action===2&&!allowFailure)
            throw Error(`Native history refused: ${errorText()}${rendererDetail()}`);
          const before=receipt.before;
          if(!queueCompletionInstalled||receipt.observed!==1||receipt.action!==0||
             receipt.ownerGate!==0||receipt.submittedFrame!==lastPresentationSubmission||
             before.pendingPackets!==0||before.pendingStaging!==1||before.workerBusy!==0||
             before.queuedPipelines!==0||before.cacheStatus!==0||before.leaseCacheStatus!==0||
             before.rendererErrors!==0||before.staleCallbacks!=='0'||
             before.beginFrame!==receipt.submittedFrame||before.endFrame!==receipt.submittedFrame)
            return null;
          const frameId=canonicalU64(receipt.submittedFrame,'queue-wait submitted frame',{positive:true});
          const stagingGeneration=canonicalU64(before.stagingGeneration,'queue-wait staging generation');
          const registrationSequence=canonicalU64(before.registration,'queue-wait registration sequence',{positive:true});
          return Object.freeze({frameId:frameId.toString(),stagingGeneration:stagingGeneration.toString(),
            registrationSequence:registrationSequence.toString()});
        };
        const onBlocked=typeof prepare==='function'?value=>{
          if(value.pendingCommand||!value.gpuPending)return null;
          if(inlineMode)return inlineCall('renderer_prepare_json',command).then(retainPreparation);
          return retainPreparation(prepare(command));
        }:null;
        const before=await wait(value=>value.pendingCommand===0&&
          (!waitForRenderer||value.gpuPending===0),deadline,name,allowFailure,onBlocked,rendererDetail,
          false,entry.queueWait);
        const recordingIdle=name==='idle'&&before.metadata.phase===22;
        const terminalPresentIdle=name==='idle'&&before.metadata.phase===30&&
          before.lastCompletedCommand===19&&!terminalTailActive;
        const terminalTailIdle=name==='idle'&&before.metadata.phase===39&&
          before.lastCompletedCommand===29&&terminalTailActive;
        const terminalRecordingPresent=name==='recordingPresent'&&terminalCandidate&&
          ((before.metadata.phase===29&&terminalCandidate.ticket.state===1)||
           (before.metadata.phase===31&&before.lastCompletedCommand===20&&terminalCandidate.ticket.state===2))&&
          before.metadata.currentFrame===terminalCandidate.sourceFrame&&
          sameJsonValue(readTerminalReceipt(),terminalCandidate.receipt);
        if(name==='recordingPresent'&&(!captured||before.gpuPending||before.endpointActive||
            (!terminalRecordingPresent&&(before.metadata.phase!==21||
              before.metadata.currentFrame<=before.metadata.preFrame))))
          throw Error('Recording presentation requires a completed forward or selected terminal boundary');
        const terminalSeal=name==='sealPresentedFrontier'&&terminalCandidate&&
          before.metadata.phase===29&&before.lastCompletedCommand===5&&
          before.metadata.currentFrame===terminalCandidate.sourceFrame;
        if(name==='sealPresentedFrontier'&&(!captured||
            (!terminalSeal&&(before.metadata.phase!==21||before.lastCompletedCommand!==5||
              before.metadata.currentFrame<=before.metadata.preFrame))||
            before.gpuPending||before.endpointActive))
          throw Error('Presented frontier seal requires the completed idle of its actual recording draw');
        if(name==='terminalTailBegin'&&(!terminalCandidate||before.ownerState!==1||
            before.metadata.phase!==35||before.lastCompletedCommand!==9||
            before.metadata.currentFrame!==terminalCandidate.sourceFrame||
            before.metadata.endpointMask!==(1<<9)||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence))
          throw Error('Terminal-tail begin requires the exact retired terminal endpoint and owner');
        if(name==='terminalTailTicket'&&(before.ownerState!==1||before.metadata.phase!==36||
            ![26,5].includes(before.lastCompletedCommand)||
            before.gpuPending||before.endpointActive||before.completeSequence!==before.requestSequence))
          throw Error('Terminal-tail ticket requires its completed presented/idle boundary');
        if(name==='terminalTailForward'&&(before.ownerState!==1||before.metadata.phase!==37||
            before.lastCompletedCommand!==27||before.inputReady!==1||before.gpuPending||
            before.endpointActive||before.completeSequence!==before.requestSequence))
          throw Error('Terminal-tail forward requires its exact completed actual ticket');
        if(name==='terminalTailForward'){
          terminalTailBefore=readTerminalTailDescriptor();
          if(terminalTailBefore.consumedOrdinal!==terminalTailLastTicket.inputOrdinal||
             terminalTailBefore.completedTick!==terminalTailLastTicket.completedTick||
             terminalTailBefore.sourceFrame!==terminalTailLastTicket.sourceFrame)
            throw Error('Terminal-tail descriptor cursor differs from its exact pre-forward ticket');
        }
        if(name==='terminalTailPresent'&&(before.ownerState!==1||before.metadata.phase!==38||
            before.lastCompletedCommand!==28||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence))
          throw Error('Terminal-tail present requires one completed applied input');
        if(name==='finishTerminalTail'&&(before.ownerState!==1||before.metadata.phase!==40||
            before.lastCompletedCommand!==5||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence||
            terminalFinish.consumedOrdinal!==terminalTailFinalCopy?.receipt?.consumedOrdinal||
            terminalFinish.lastInputOrdinal!==terminalTailFinalCopy?.receipt?.lastInputOrdinal))
          throw Error('Terminal-tail finish requires the exact final presentation and idle receipt');
        const next=before.requestSequence+1;
        const dispatchGeneric=async()=>{
          if(inlineMode)await requireInlineRequest('request',command);
          else requireSuccess('request',command);
        };
        if(name==='terminalTailBegin'){
          const requestName='request_terminal_tail_begin';
          if(inlineMode)await requireInlineRequest(requestName,terminalOrdinal);
          else requireEpisodeSuccess(requestName,terminalOrdinal);
        }else if(name==='terminalTailTicket'){
          const requestName='request_terminal_tail_ticket';
          if(inlineMode)await requireInlineRequest(requestName);
          else requireEpisodeSuccess(requestName);
        }else if(name==='terminalTailForward'){
          const requestName='request_terminal_tail_forward';
          if(inlineMode)await requireInlineRequest(requestName,terminalTicketSequence);
          else requireEpisodeSuccess(requestName,terminalTicketSequence);
        }else if(name==='terminalTailPresent'){
          const requestName='request_terminal_tail_present';
          if(inlineMode)await requireInlineRequest(requestName);
          else requireEpisodeSuccess(requestName);
        }else if(name==='finishTerminalTail'){
          const requestName='request_terminal_tail_finish';
          if(inlineMode)await requireInlineRequest(requestName,terminalFinish.consumedOrdinal,
            terminalFinish.lastInputOrdinal);
          else requireEpisodeSuccess(requestName,terminalFinish.consumedOrdinal,
            terminalFinish.lastInputOrdinal);
        }else if(name==='rebase'){
          const h=before.metadata;
          const span=confirmedEnd-h.preFrame;
          const supportedSpan=arenaMode?span>=1&&span<=7&&confirmedEnd<=UINT32_MAX-7:span===1;
          if(!captured||!identityText||before.ownerState!==1||before.endpointActive||before.gpuPending||
            before.completeSequence!==before.requestSequence||h.phase!==18||
            h.preFrame!==before.expectedFrame||!supportedSpan||
            confirmedEnd!==h.currentFrame||confirmedEnd!==h.sealedEnd||h.forwardSteps!==span)
            throw Error('Confirmed rebase requires the actual retired END');
          if(typeof module[PREFIX+'request_rebase']!=='function'||
            typeof module[PREFIX+'rebase_identity_json']!=='function')
            throw Error('Private confirmed rebase exports are unavailable');
          if(inlineMode)await requireInlineRequest('request_rebase',confirmedEnd);
          else requireSuccess('request_rebase',confirmedEnd);
        }else if(name==='finishPrefix'){
          const h=before.metadata;
          const terminalPrefix=terminalCandidate?.sourceFrame===confirmedEnd&&
            terminalCandidate.ticket.state===2;
          if(!captured||!identityText||before.ownerState!==1||before.endpointActive||before.gpuPending||
            before.completeSequence!==before.requestSequence||h.phase!==11||
            h.preFrame!==before.expectedFrame||h.currentFrame!==confirmedEnd||
            !h.incarnation||h.restoreCount<1||h.restoreCount>2||
            (terminalPrefix?(h.replaySteps!==terminalCandidate.inputOrdinal-h.preFrame||
              h.replaySteps>h.forwardSteps):
              (confirmedEnd<=h.preFrame||confirmedEnd>=h.sealedEnd||h.sealedEnd-h.preFrame>7||
              h.forwardSteps!==h.sealedEnd-h.preFrame||h.replaySteps!==confirmedEnd-h.preFrame)))
            throw Error('Confirmed-prefix finish requires the actual completed CPU replay prefix inside its selected END');
          const requestName='request_prefix_finish';
          if(inlineMode)await requireInlineRequest(requestName,confirmedEnd);
          else if(episodeExport(requestName)(confirmedEnd)!==1)
            throw latch(Error(`Native history ${requestName} refused: ${errorText()}`));
        }else if(name==='closePrefix'){
          transitionHandoff=readPrefixHandoff();
          const h=before.metadata;
          if(!confirmedPrefixActive||transitionHandoff.rebaseRequestSequence||
            before.ownerState!==1||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence||h.phase!==24||
            h.preFrame!==transitionHandoff.oldPreFrame||
            h.currentFrame!==transitionHandoff.confirmedFrame||
            h.sealedEnd!==transitionHandoff.confirmedFrame||
            h.incarnation!==transitionHandoff.oldIncarnation)
            throw Error('Confirmed-prefix close requires the exact copied CPU prefix endpoint');
          await dispatchGeneric();
        }else if(name==='retirePrefix'){
          transitionHandoff=readPrefixHandoff();
          const h=before.metadata;
          if(!confirmedPrefixActive||transitionHandoff.rebaseRequestSequence||
            before.ownerState!==1||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence||h.phase!==25||
            h.preFrame!==transitionHandoff.oldPreFrame||
            h.currentFrame!==transitionHandoff.confirmedFrame||
            h.sealedEnd!==transitionHandoff.confirmedFrame||
            h.incarnation!==transitionHandoff.oldIncarnation)
            throw Error('Confirmed-prefix retire requires the exact closed CPU prefix endpoint');
          await dispatchGeneric();
        }else if(name==='rebaseConfirmedPrefix'){
          transitionHandoff=readPrefixHandoff();
          const h=before.metadata;
          if(!confirmedPrefixActive||transitionHandoff.rebaseRequestSequence||
            !captured||!identityText||before.ownerState!==1||before.endpointActive||before.gpuPending||
            before.completeSequence!==before.requestSequence||h.phase!==26||
            h.preFrame!==transitionHandoff.oldPreFrame||
            confirmedEnd!==transitionHandoff.confirmedFrame||
            h.currentFrame!==confirmedEnd||h.sealedEnd!==confirmedEnd||
            h.incarnation!==transitionHandoff.oldIncarnation)
            throw Error('Confirmed-prefix rebase requires the exact retired CPU prefix endpoint');
          const requestName='request_prefix_rebase';
          if(inlineMode)await requireInlineRequest(requestName,confirmedEnd);
          else if(episodeExport(requestName)(confirmedEnd)!==1)
            throw latch(Error(`Native history ${requestName} refused: ${errorText()}`));
        }else if(name==='adoptNextPrefixTicket'){
          transitionHandoff=readPrefixHandoff();
          const h=before.metadata;
          if(!confirmedPrefixActive||!transitionHandoff.rebaseRequestSequence||
            !captured||before.ownerState!==1||before.gpuPending||before.endpointActive||
            before.completeSequence!==before.requestSequence||
            transitionHandoff.suffixCursor>=transitionHandoff.suffixCount||
            ![4,21].includes(h.phase)||before.expectedFrame!==transitionHandoff.confirmedFrame||
            h.currentFrame!==transitionHandoff.confirmedFrame+transitionHandoff.suffixCursor||
            !h.incarnation||h.restoreCount!==0||adoptedPrefixTicket)
            throw Error('Adopted prefix ticket requires the current fresh-PRE suffix cursor');
          await dispatchGeneric();
        }else if(name==='pollTicket'&&confirmedPrefixActive){
          transitionHandoff=readPrefixHandoff();
          if(!transitionHandoff.rebaseRequestSequence||
            transitionHandoff.suffixCursor<transitionHandoff.suffixCount)
            throw Error('Fresh PAD polling is disabled until every retained prefix suffix ticket is forwarded');
          await dispatchGeneric();
        }else if(framePayload){
          const h=before.metadata;
          if(!captured||!frameArgument||
            (!adoptedPrefixTicket&&ticketId>before.requestSequence)||
            (name==='forwardTicket'?![4,21].includes(h.phase):h.phase!==11))
            throw Error('Episode ticket command is outside its captured source phase');
          if(confirmedPrefixActive&&name==='forwardTicket'){
            transitionHandoff=readPrefixHandoff();
            if(!transitionHandoff.rebaseRequestSequence)
              throw Error('A retired confirmed-prefix owner cannot continue before fresh PRE rebase');
            if(transitionHandoff.rebaseRequestSequence&&
              transitionHandoff.suffixCursor<transitionHandoff.suffixCount){
              if(!adoptedPrefixTicket||adoptedPrefixTicket.suffixOrdinal!==transitionHandoff.suffixCursor||
                adoptedPrefixTicket.ticketId!==ticketId||
                adoptedPrefixTicket.sourceFrame!==h.currentFrame||
                adoptedPrefixTicket.incarnation!==h.incarnation)
                throw Error('Forwarded prefix suffix requires its exact adopted native ticket');
              for(const port of [adoptedPrefixTicket.localPort,2,3]){
                const from=port*PAD_BYTES,to=from+PAD_BYTES;
                if(!sameBytes(planBytes.subarray(from,to),adoptedPrefixTicket.raw.subarray(from,to)))
                  throw Error('Adopted suffix forward changed local or unused original PAD bytes');
              }
            }
          }else if(confirmedPrefixActive&&name==='replayTicket'){
            transitionHandoff=readPrefixHandoff();
            if(!transitionHandoff.rebaseRequestSequence||
              transitionHandoff.suffixCursor<transitionHandoff.suffixCount)
              throw Error('Retained post-prefix suffixes use adopted forward tickets, not replay tickets');
          }
          const pointer=writeFrameArgument(planBytes);
          const request=name==='forwardTicket'?'request_episode_forward':'request_episode_replay';
          if(inlineMode)await requireInlineRequest(request,ticketId,pointer,FRAME_BYTES);
          else requireEpisodeSuccess(request,ticketId,pointer,FRAME_BYTES);
        }else if(inlineMode)await requireInlineRequest('request',command);
        else requireSuccess('request',command);
        // Completion and deadline checks occur only after native Asyncify
        // settles. Cleanup may release an already retired owner even if late.
        const after=await wait(value=>value.pendingCommand===0&&value.completeSequence===next,
          deadline,name,allowFailure,null,rendererDetail,allowFailure);
        if(after.requestSequence!==next||after.lastCompletedCommand!==command)
          throw Error('Native command completion sequence mismatch');
        if(recordingIdle||terminalPresentIdle||terminalTailIdle||name==='recordingPresent'){
          const {phase:priorPhase,...prior}=before.metadata;
          const {phase:nextPhase,...now}=after.metadata;
          const expectedBefore=recordingIdle?22:
            terminalPresentIdle?30:terminalTailIdle?39:before.metadata.phase;
          const normalPresentAfter=name==='recordingPresent'&&before.metadata.phase===21?22:
            terminalRecordingPresent?30:recordingIdle?21:
            terminalPresentIdle?(terminalCandidate?.ticket.state===2?33:29):0;
          const expectedAfterValid=terminalTailIdle?[36,40].includes(after.metadata.phase):
            nextPhase===normalPresentAfter;
          if(priorPhase!==expectedBefore||!expectedAfterValid||
             !sameJsonValue(prior,now)||before.expectedFrame!==after.expectedFrame||
             before.localPort!==after.localPort||before.remotePort!==after.remotePort||
             after.ownerState!==1||after.endpointActive||
             ((recordingIdle||terminalPresentIdle||terminalTailIdle)&&after.gpuPending))
            throw Error('Recording presentation changed its forward source owner');
        }
        if(episodeCommands.has(name)&&name!=='recordingPresent'){
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
            if(arenaMode&&now.phase===29){
              valid=now.currentFrame===prior.currentFrame&&now.forwardSteps===prior.forwardSteps+1&&
                now.incarnation===prior.incarnation;
              if(valid)bindTerminalCandidate(before,after,ticketId,planBytes,false);
            }else{
              valid=now.phase===21&&now.currentFrame===prior.currentFrame+1&&
                now.currentFrame-now.preFrame<=7&&now.forwardSteps===prior.forwardSteps+1&&
                now.incarnation===prior.incarnation;
            }
            lastPolledEpisodeTicket=null;break;
          case 'seal':
            valid=now.phase===5&&now.currentFrame===prior.currentFrame&&
              now.sealedEnd===now.currentFrame&&now.forwardSteps>0;break;
          case 'sealPresentedFrontier':
            valid=(terminalSeal?now.phase===31:now.phase===27)&&now.currentFrame===prior.currentFrame&&
              now.sealedEnd===now.currentFrame&&now.forwardSteps===prior.forwardSteps&&
              now.incarnation===prior.incarnation&&now.restoreCount===prior.restoreCount&&
              now.replaySteps===prior.replaySteps&&now.pollSerial===prior.pollSerial&&
              now.currentAudioPhase===prior.currentAudioPhase&&!after.gpuPending;break;
          case 'replayTicket':{
            const ending=arenaMode&&now.currentFrame===prior.currentFrame;
            valid=now.phase===11&&(ending||now.currentFrame===prior.currentFrame+1)&&
              now.replaySteps===prior.replaySteps+1&&now.replaySteps<=7&&
              now.incarnation===prior.incarnation;
            if(valid&&ending)bindTerminalCandidate(before,after,ticketId,planBytes,true);
            break;
          }
          case 'finishReplay':
            valid=now.phase===12&&now.currentFrame===now.sealedEnd&&
              now.replaySteps===now.currentFrame-now.preFrame;break;
          case 'continue':
            valid=now.phase===21&&now.incarnation===prior.incarnation+1&&
              now.currentFrame===prior.currentFrame&&now.sealedEnd===0&&now.replaySteps===0&&
              now.restoreCount===prior.restoreCount;break;
          case 'finishPrefix':{
            if(now.phase===31&&terminalCandidate?.ticket.state===2){
              const receipt=readTerminalReceipt();
              valid=sameJsonValue(receipt,terminalCandidate.receipt)&&
                now.currentFrame===confirmedEnd&&now.sealedEnd===confirmedEnd&&
                now.preFrame===prior.preFrame&&now.incarnation===prior.incarnation&&
                now.forwardSteps===prior.forwardSteps&&now.replaySteps===prior.replaySteps;
              break;
            }
            confirmedPrefixActive=true;
            const handoff=readPrefixHandoff();
            valid=now.phase===23&&now.currentFrame===confirmedEnd&&now.sealedEnd===confirmedEnd&&
              now.preFrame===prior.preFrame&&now.incarnation===prior.incarnation&&
              now.forwardSteps===prior.sealedEnd-prior.preFrame&&
              now.replaySteps===confirmedEnd-prior.preFrame&&
              handoff.oldPreFrame===prior.preFrame&&handoff.confirmedFrame===confirmedEnd&&
              handoff.selectedEndFrame===prior.sealedEnd&&handoff.oldIncarnation===prior.incarnation&&
              handoff.finishRequestSequence===after.completeSequence&&
              handoff.rebaseRequestSequence===0&&handoff.suffixCursor===0;
            break;
          }
          case 'closePrefix':{
            const handoff=readPrefixHandoff();
            valid=now.phase===25&&now.currentFrame===handoff.confirmedFrame&&
              now.sealedEnd===handoff.confirmedFrame&&now.preFrame===handoff.oldPreFrame&&
              now.incarnation===handoff.oldIncarnation&&
              handoff.rebaseRequestSequence===0&&sameJsonValue(handoff,transitionHandoff);
            break;
          }
          case 'retirePrefix':{
            const handoff=readPrefixHandoff();
            valid=now.phase===26&&now.currentFrame===handoff.confirmedFrame&&
              now.sealedEnd===handoff.confirmedFrame&&now.preFrame===handoff.oldPreFrame&&
              now.incarnation===handoff.oldIncarnation&&
              handoff.rebaseRequestSequence===0&&sameJsonValue(handoff,transitionHandoff);
            break;
          }
          case 'rebaseConfirmedPrefix':{
            const handoff=readPrefixHandoff();
            valid=now.phase===0&&now.preFrame===confirmedEnd&&now.currentFrame===confirmedEnd&&
              now.sealedEnd===0&&now.incarnation===0&&now.restoreCount===0&&
              now.endpointMask===0&&now.forwardSteps===0&&now.replaySteps===0&&
              now.pollSerial==='0'&&after.expectedFrame===confirmedEnd&&
              handoff.confirmedFrame===confirmedEnd&&
              handoff.rebaseRequestSequence===after.completeSequence;
            break;
          }
          case 'adoptNextPrefixTicket':{
            const handoff=readPrefixHandoff();
            valid=[4,21].includes(now.phase)&&now.currentFrame===prior.currentFrame&&
              now.incarnation===prior.incarnation&&now.forwardSteps===prior.forwardSteps&&
              now.replaySteps===prior.replaySteps&&after.inputReady===1&&now.pollSerial!=='0'&&
              handoff.suffixCursor===transitionHandoff.suffixCursor;
            if(valid)unclaimedPrefixTicket=true;
            break;
          }
          case 'terminalTailBegin':{
            valid=now.phase===36&&now.currentFrame===terminalCandidate.sourceFrame&&
              now.preFrame===prior.preFrame&&now.forwardSteps===prior.forwardSteps&&
              now.incarnation===prior.incarnation&&now.endpointMask===(1<<9)&&
              after.ownerState===1&&!after.gpuPending&&!after.endpointActive;
            if(valid){
              terminalTailActive=true;
              const descriptor=readTerminalTailDescriptor();
              valid=descriptor.requestSequence===after.completeSequence&&
                descriptor.consumedOrdinal===terminalCandidate.inputOrdinal&&
                descriptor.suffixCursor===0;
            }
            break;
          }
          case 'terminalTailTicket':
            valid=now.phase===37&&now.currentFrame===prior.currentFrame&&
              now.preFrame===prior.preFrame&&now.forwardSteps===prior.forwardSteps&&
              now.incarnation===prior.incarnation&&after.inputReady===1&&
              after.ownerState===1&&!after.gpuPending&&!after.endpointActive;break;
          case 'terminalTailForward':{
            const ticket=terminalTailLastTicket;
            if(ticket&&terminalTailBefore){
              const descriptor=readTerminalTailDescriptor();
              const expectedCursor=terminalTailBefore.suffixCursor+(ticket.flags===1?1:0);
              valid=now.phase===38&&now.currentFrame===prior.currentFrame&&
                now.preFrame===prior.preFrame&&now.forwardSteps===prior.forwardSteps&&
                now.incarnation===prior.incarnation&&after.ownerState===1&&
                !after.gpuPending&&!after.endpointActive&&
                descriptor.consumedOrdinal===ticket.inputOrdinal+1&&
                BigInt(descriptor.completedTick)===BigInt(ticket.completedTick)+1n&&
                descriptor.sourceFrame===ticket.sourceFrame&&
                descriptor.suffixCursor===expectedCursor&&
                descriptor.suffixCount===terminalTailBefore.suffixCount&&
                descriptor.selectedOrdinal===terminalTailBefore.selectedOrdinal&&
                descriptor.worldGeneration===terminalTailBefore.worldGeneration&&
                descriptor.contextEpoch===terminalTailBefore.contextEpoch;
            }
            break;
          }
          case 'terminalTailPresent':
            valid=now.phase===39&&now.currentFrame===prior.currentFrame&&
              now.preFrame===prior.preFrame&&now.forwardSteps===prior.forwardSteps&&
              now.incarnation===prior.incarnation&&after.ownerState===1&&
              !after.endpointActive;break;
          case 'finishTerminalTail':
            valid=now.phase===41&&after.ownerState===2&&!after.pendingCommand&&
              after.completeSequence===after.requestSequence&&
              after.lastCompletedCommand===MATCH_HISTORY_COMMAND.finishTerminalTail;
            break;
          default:break;
          }
          if(!valid)throw Error(`Episode ${name} completion differs from its bounded native transition`);
        }
        if(name==='forwardTicket'&&adoptedPrefixTicket){
          const handoff=readPrefixHandoff();
          const {suffixCursor:oldCursor,...oldOwner}=transitionHandoff;
          const {suffixCursor:newCursor,...newOwner}=handoff;
          if(newCursor!==oldCursor+1||!sameJsonValue(oldOwner,newOwner))
            throw Error('Adopted prefix forward did not advance exactly one retained suffix cursor');
          adoptedPrefixTicket=null;
        }
        if(name==='restore')terminalCandidate=null;
        if(terminalCandidate&&name==='close'){
          const prior=before.metadata,now=after.metadata;
          if(prior.phase!==32||prior.endpointMask!==(1<<9)||now.phase!==34||
             now.currentFrame!==terminalCandidate.sourceFrame||
             now.preFrame!==prior.preFrame||now.forwardSteps!==prior.forwardSteps||
             now.incarnation!==prior.incarnation||after.gpuPending||after.endpointActive)
            throw Error('Terminal endpoint close changed its selected source owner');
        }
        if(terminalCandidate&&name==='retire'){
          const prior=before.metadata,now=after.metadata;
          if(prior.phase!==34||now.phase!==35||
             now.currentFrame!==terminalCandidate.sourceFrame||
             now.preFrame!==prior.preFrame||now.forwardSteps!==prior.forwardSteps||
             now.endpointMask!==(1<<9)||now.incarnation!==prior.incarnation||
             after.gpuPending||after.endpointActive)
            throw Error('Terminal endpoint retirement changed its selected source owner');
        }
        if(name==='finishTerminalTail'){
          releasePadArgument();terminal=true;terminalTailActive=false;
          closeQueueCompletionObserver();
        }
        if(name==='cleanup'){
          if(after.ownerState!==2)throw Error('Native cleanup did not retire the owner');
          releasePadArgument();terminal=true;closeQueueCompletionObserver();
          if(inlineMode&&performance.now()>=deadline)throw deadlineError(name,rendererDetail);
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
          if(prefixHandoffComplete){
            const handoffExport=module[PREFIX+'prefix_handoff_json'];
            const originSize=module[PREFIX+'prefix_origin_row_bytes'];
            if(typeof handoffExport!=='function'||handoffExport()!==0||
              typeof originSize!=='function'||originSize()!==0||!sameJsonValue(check(status()),after))
              throw Error('Whole-END rebase did not clear its exhausted confirmed-prefix handoff');
            confirmedPrefixActive=false;
            adoptedPrefixTicket=null;
            unclaimedPrefixTicket=false;
          }
        }
        if(name==='rebaseConfirmedPrefix'){
          const h=after.metadata;
          if(after.ownerState!==1||after.localPort!==before.localPort||after.remotePort!==before.remotePort||
            after.expectedFrame!==confirmedEnd||after.inputReady||after.gpuPending||after.endpointActive||
            h.phase||h.preFrame!==confirmedEnd||h.currentFrame!==confirmedEnd||h.sealedEnd||
            h.incarnation||h.endpointMask||h.forwardSteps||h.restoreCount||h.replaySteps||h.pollSerial!=='0'||
            h.preAudioPhase!==before.metadata.currentAudioPhase||h.currentAudioPhase!==h.preAudioPhase)
            throw Error('Confirmed-prefix rebase acknowledgment did not bind fresh PRE at actual C');
          const pointer=module[PREFIX+'rebase_identity_json']();
          if(!Number.isInteger(pointer)||pointer<=0||pointer>0xffffffff)
            throw Error('Confirmed-prefix rebase has no fresh native identity');
          const identity=module.UTF8ToString(pointer);
          const prior=JSON.parse(identityText),fresh=JSON.parse(identity);
          const {source_frame:priorFrame,current_rng:priorRng,...priorOwner}=prior;
          const {source_frame:frame,current_rng:rng,...freshOwner}=fresh;
          const handoff=readPrefixHandoff();
          if(priorFrame!==before.expectedFrame||!/^[0-9a-f]{8}$/.test(priorRng)||
            frame!==confirmedEnd||!/^[0-9a-f]{8}$/.test(rng)||!sameJsonValue(priorOwner,freshOwner)||
            handoff.rebaseRequestSequence!==after.completeSequence||
            !sameJsonValue(check(status()),after))
            throw Error('Confirmed-prefix rebase changed its fresh identity, handoff, or owner status');
          identityText=identity;
          entry.identity=identity;
          captured=false;
        }
        entry.status='complete';entry.after=after;entry.elapsedMs=performance.now()-started;
        return name==='rebase'||name==='rebaseConfirmedPrefix'?
          {status:after,identity:entry.identity}:after;
      }catch(error){entry.status='failed';entry.error=String(error);entry.elapsedMs=performance.now()-started;throw latch(error);}
      finally{if(name==='cleanup')closeQueueCompletionObserver();busy=false;}
    },
  });
  }catch(error){
    closeQueueCompletionObserver();
    if(allocatedPadArgument){
      try{module._free(allocatedPadArgument);}
      catch(cleanupError){throw new AggregateError([error,cleanupError],
        'Runtime history controller construction and PAD scratch cleanup failed');}
    }
    throw error;
  }
}
