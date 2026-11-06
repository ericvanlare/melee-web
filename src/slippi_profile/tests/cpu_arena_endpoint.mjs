// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in host decoder and same-page restore proof for the native source arena
// endpoint. The legacy schema-312 comparator remains byte-exact and unchanged.
import {Buffer} from 'node:buffer';
import {isDeepStrictEqual} from 'node:util';
import {
  CPU_ENDPOINT_PROVENANCE, CPU_ENDPOINT_VIEW,
  decodeCpuParticleEndpointResult, validateCpuParticleEndpointAgainstPre,
} from './cpu_endpoint.mjs';

const ENVELOPE_SCHEMA=3,ARENA_SCHEMA=1,ARENA_BYTES=144,U32_MAX=0xffff_ffffn;
const MAX_ARENA_BYTES=64n*1024n*1024n,MAX_HISTORY_SPAN=7n;
const U64_MAX=0xffff_ffff_ffff_ffffn,NO_MISMATCH=U64_MAX;
const fail=message=>{throw Error(`CPU arena endpoint ${message}`);};
const requireValue=(condition,message)=>{if(!condition)fail(message);};
const canonicalU64=(value,label)=>{
  if(typeof value!=='string'||!/^(0|[1-9][0-9]*)$/.test(value))fail(`${label} is not a canonical u64`);
  const parsed=BigInt(value);if(parsed>U64_MAX)fail(`${label} exceeds u64`);return parsed;
};
const exactKeys=(value,keys,label)=>{
  if(!value||Object.keys(value).sort().join(',')!==[...keys].sort().join(','))
    fail(`${label} has missing or unrecognized fields`);
};
const decodeCanonicalBase64=(value,label)=>{
  if(typeof value!=='string'||value.length%4||
    !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    fail(`${label} is not canonical base64`);
  const bytes=Buffer.from(value,'base64');
  if(bytes.toString('base64')!==value)fail(`${label} has a noncanonical base64 encoding`);
  return bytes;
};

function payloadHeader(endpoint){
  const header=endpoint.domains[3]?.records.find(row=>row.kindName==='Header');
  if(!header)fail('Payload.Header is missing');
  const read=name=>{
    const index=header.fieldNames.indexOf(name);
    if(index<0)fail(`Payload.Header.${name} is missing`);
    return canonicalU64(header.fields[index],`Payload.Header.${name}`);
  };
  return {header,watermark:read('Watermark'),revision:read('TopologyRevision')};
}
function arenaSidecar(value){
  const bytes=decodeCanonicalBase64(value.arenaHistoryEndpointBase64,'arena sidecar');
  if(bytes.length!==ARENA_BYTES)fail(`sidecar extent ${bytes.length} is not exactly ${ARENA_BYTES}`);
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const u32=offset=>view.getUint32(offset,true),u64=offset=>view.getBigUint64(offset,true);
  const result={schema:u32(0),bytes:u32(4),view:u32(8),provenance:u32(12),sourceFrame:u32(16),
    incarnation:u32(20),restoreCount:u32(24),flags:u32(28),
    prePieceWatermark:u32(32),currentPieceWatermark:u32(36),poolRefusal:u32(40),
    originRefusal:u32(44),pieceRefusal:u32(48),reserved:u32(52),
    effectToken:u64(56),preRevision:u64(64),currentRevision:u64(72),
    preWatermark:u64(80),currentWatermark:u64(88),prePieceRevision:u64(96),
    currentPieceRevision:u64(104),arenaBase:u64(112),arenaBytes:u64(120),
    bytesCompared:u64(128),firstMismatchOffset:u64(136),rawBase64:bytes.toString('base64')};
  if(result.schema!==ARENA_SCHEMA||result.bytes!==ARENA_BYTES||
    result.view!==value.view||result.provenance!==value.provenance||
    BigInt(result.sourceFrame)!==canonicalU64(value.sourceFrame,'sourceFrame')||
    (result.incarnation===0&&(value.view!==CPU_ENDPOINT_VIEW.pre||result.effectToken!==0n||result.restoreCount!==0))||
    (result.incarnation!==0&&result.effectToken===0n)||(result.flags&~3)!==0||result.reserved!==0||
    result.poolRefusal!==0||result.originRefusal!==0||result.pieceRefusal!==0)
    fail('sidecar schema, endpoint identity, flags, reserved, or refusal fields are invalid');
  if(result.incarnation>3||result.restoreCount>2)fail('sidecar incarnation or restore count exceeds native history bounds');
  if(result.arenaBase===0n||result.arenaBase>U32_MAX||result.arenaBytes<128n||
    result.arenaBytes>MAX_ARENA_BYTES||result.arenaBase+result.arenaBytes>0x1_0000_0000n)
    fail('sidecar arena bounds or complete-byte comparison extent is invalid');
  if((result.flags&1)!==0){
    if(result.firstMismatchOffset!==NO_MISMATCH||result.bytesCompared!==result.arenaBytes)
      fail('exact arena flag does not cover the complete arena');
  }else if(result.firstMismatchOffset>=result.arenaBytes||
    result.bytesCompared!==result.firstMismatchOffset+1n)
    fail('non-exact arena flag lacks a truthful first-mismatch prefix');
  if(value.view===CPU_ENDPOINT_VIEW.pre&&result.flags!==3)
    fail('PRE sidecar did not prove exact arena and shadow bytes');
  const effect= value.domains[1]?.records.find(row=>row.kindName==='EffectToken');
  if(!effect||effect.fields.length!==1||canonicalU64(effect.fields[0],'Effects.EffectToken')!==result.effectToken)
    fail('sidecar effect token differs from the raw Effects endpoint');
  const arenaEnd=result.arenaBase+result.arenaBytes;
  for(const row of value.domains[3].records){
    if(row.kindName!=='PayloadRow')continue;
    const host=canonicalU64(row.fields[row.fieldNames.indexOf('HostAddress')],'PayloadRow.HostAddress');
    const requested=canonicalU64(row.fields[row.fieldNames.indexOf('RequestedBytes')],'PayloadRow.RequestedBytes');
    if(requested===0n||host<result.arenaBase||host+requested>arenaEnd)
      fail(`PayloadRow[${row.index}] escapes its exact host arena extent`);
  }
  const payload=payloadHeader(value);
  if(payload.watermark!==result.currentWatermark||payload.revision!==result.currentRevision)
    fail('sidecar current allocation/revision fences differ from raw Payload.Header');
  if(result.preRevision>result.currentRevision||result.preWatermark>result.currentWatermark||
    BigInt(result.prePieceWatermark)>BigInt(result.currentPieceWatermark)||
    result.prePieceRevision>result.currentPieceRevision)
    fail('sidecar current fences precede immutable PRE fences');
  if(value.view===CPU_ENDPOINT_VIEW.pre&&result.restoreCount===0&&
    (result.preRevision!==result.currentRevision||result.preWatermark!==result.currentWatermark||
     result.prePieceRevision!==result.currentPieceRevision||
     result.prePieceWatermark!==result.currentPieceWatermark))
    fail('initial PRE sidecar current fences differ from immutable PRE fences');
  return result;
}

function legacyParticleView(value){
  // Preserve every schema-1 raw row and particle-sidecar value exactly. Only
  // project schema 3 to the existing schema-2 envelope for its validator.
  return {schema:2,rawSchema:value.rawSchema,recordBytes:value.recordBytes,
    view:value.view,provenance:value.provenance,sourceFrame:value.sourceFrame,
    revalidated:value.revalidated,domains:value.domains,particleCells:value.particleCells};
}

/** Validate the schema-3 wrapper, the unchanged schema-1/particle payload, and
 * the copied 144-byte native witness; return the original envelope unchanged. */
export function decodeCpuArenaEndpointResult(value){
  exactKeys(value,['schema','rawSchema','recordBytes','view','provenance','sourceFrame',
    'revalidated','domains','particleCells','arenaHistoryEndpointBase64'],'schema-3 envelope');
  requireValue(value.schema===ENVELOPE_SCHEMA&&value.rawSchema===1&&value.recordBytes===312&&
    value.revalidated===true,'schema-3 envelope header is invalid');
  requireValue([1,2,3].includes(value.view)&&[1,2,3].includes(value.provenance),
    'view/provenance is invalid');
  canonicalU64(value.sourceFrame,'sourceFrame');
  try{decodeCpuParticleEndpointResult(legacyParticleView(value));}
  catch(error){fail(`wrapped raw/particle endpoint is invalid: ${error?.message||error}`);}
  arenaSidecar(value);
  return value;
}

function compareRawRecordExact(left,right,label,{mutablePayloadHeader=false}={}){
  for(const key of ['kind','kindName','index','count','metadataBytes','borrowedBytes'])
    if(left[key]!==right[key])fail(`${label}.${key} differs`);
  if(!isDeepStrictEqual(left.fieldNames,right.fieldNames))fail(`${label}.fieldNames differ`);
  if(left.fields.length!==right.fields.length)fail(`${label}.metadata field count differs`);
  const permitted=mutablePayloadHeader?new Set(['Watermark','TopologyRevision']):new Set();
  for(let index=0;index<left.fields.length;index++){
    const name=left.fieldNames[index];
    if(!permitted.has(name)&&canonicalU64(left.fields[index],`${label}.${name}`)!==
      canonicalU64(right.fields[index],`${label}.${name}`))
      fail(`${label}.metadata[${name}] differs`);
  }
  const leftBytes=decodeCanonicalBase64(left.bytesBase64,`${label}.left bytes`);
  const rightBytes=decodeCanonicalBase64(right.bytesBase64,`${label}.right bytes`);
  if(leftBytes.length!==left.borrowedBytes||rightBytes.length!==right.borrowedBytes||
    !leftBytes.equals(rightBytes))fail(`${label}.borrowed bytes differ`);
}

function compareRestoredPreRaw(armed,restored){
  if(armed.sourceFrame!==restored.sourceFrame)fail('Armed/Restored PRE source-frame stamps differ');
  if(armed.domains.length!==9||restored.domains.length!==9)fail('all nine raw domains are required');
  for(let domainIndex=0;domainIndex<9;domainIndex++){
    const left=armed.domains[domainIndex],right=restored.domains[domainIndex];
    if(left.id!==domainIndex+1||right.id!==left.id||left.name!==right.name||
      left.records.length!==right.records.length)fail(`PRE domain ${domainIndex+1} identity/count differs`);
    for(let index=0;index<left.records.length;index++){
      const a=left.records[index],b=right.records[index];
      const mutablePayloadHeader=domainIndex===3&&a.kindName==='Header';
      compareRawRecordExact(a,b,`${left.name}.${a.kindName}[${a.index}]`,{mutablePayloadHeader});
    }
  }
}

function compareParticlePre(armed,restored){
  const a=armed.particleCells,r=restored.particleCells;
  const {historySerial:armedSerial,...armedStable}=a;
  const {historySerial:restoredSerial,...restoredStable}=r;
  canonicalU64(armedSerial,'Armed particle historySerial');
  canonicalU64(restoredSerial,'Restored particle historySerial');
  if(!isDeepStrictEqual(armedStable,restoredStable))
    fail('PRE captured/live particle identities or incarnation relationships differ');
}
function ownerInvariant(endpoint){
  const row=endpoint.domains[0]?.records.find(item=>item.kindName==='Owner');
  if(!row)fail('raw native source owner row is missing');
  return row.fieldNames.flatMap((name,index)=>name==='SourceFrame'?[]:[[name,row.fields[index]]]);
}

/** Compare one same-page Armed PRE, Restored PRE and the selected predicted DRAW_END
 * receipt. Raw PRE rows stay byte-exact except Payload.Header watermark and
 * topology revision, which must be witnessed by the native arena sidecar. */
export function compareCpuArenaRestoredPre(armedPre,restoredPre,selectedEnd){
  decodeCpuArenaEndpointResult(armedPre);decodeCpuArenaEndpointResult(restoredPre);
  decodeCpuArenaEndpointResult(selectedEnd);
  if(armedPre.view!==CPU_ENDPOINT_VIEW.pre||armedPre.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre||
    restoredPre.view!==CPU_ENDPOINT_VIEW.pre||restoredPre.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre)
    fail('Armed and Restored endpoints must both be Shared PRE');
  if(selectedEnd.view!==CPU_ENDPOINT_VIEW.drawEnd||
    selectedEnd.provenance!==CPU_ENDPOINT_PROVENANCE.reference)
    fail('selected endpoint must be the Reference DRAW_END preceding restore');
  const armed=arenaSidecar(armedPre),restored=arenaSidecar(restoredPre),selected=arenaSidecar(selectedEnd);
  const span=BigInt(selectedEnd.sourceFrame)-BigInt(armedPre.sourceFrame);
  if(armedPre.sourceFrame!==restoredPre.sourceFrame||span<=0n||span>MAX_HISTORY_SPAN)
    fail('selected END must be within one to seven frames after the same PRE boundary');
  if(!isDeepStrictEqual(ownerInvariant(armedPre),ownerInvariant(restoredPre))||
    !isDeepStrictEqual(ownerInvariant(armedPre),ownerInvariant(selectedEnd)))
    fail('native source owner identity differs across the selected history endpoints');
  compareRestoredPreRaw(armedPre,restoredPre);
  compareParticlePre(armedPre,restoredPre);
  try{
    validateCpuParticleEndpointAgainstPre(legacyParticleView(selectedEnd),legacyParticleView(armedPre));
  }catch(error){fail(`captured particle authority differs from Armed PRE: ${error?.message||error}`);}

  if(armed.incarnation!==1||armed.restoreCount!==0)
    fail('Armed PRE must retain original incarnation 1 and restore count 0');
  if(restored.incarnation!==selected.incarnation)
    fail('Restored PRE incarnation differs from the selected END incarnation');
  if(selected.restoreCount>=2||restored.restoreCount!==selected.restoreCount+1)
    fail('Restored PRE restore count is not the next selected first/second restore');
  const immutable=['effectToken','preRevision','preWatermark','prePieceRevision','prePieceWatermark',
    'arenaBase','arenaBytes'];
  for(const field of immutable){
    if(armed[field]!==restored[field]||armed[field]!==selected[field])
      fail(`immutable PRE ${field} fence differs across endpoints`);
  }
  if(armed.currentRevision!==armed.preRevision||armed.currentWatermark!==armed.preWatermark||
    armed.currentPieceRevision!==armed.prePieceRevision||
    armed.currentPieceWatermark!==armed.prePieceWatermark)
    fail('Armed PRE current fence differs from its immutable PRE fence');
  if(restored.currentRevision!==selected.currentRevision+1n||
    restored.currentWatermark!==selected.currentWatermark||
    restored.currentPieceRevision!==selected.currentPieceRevision+1n||
    restored.currentPieceWatermark!==selected.currentPieceWatermark)
    fail('Restored PRE revision/watermark fences do not follow the selected END by one restore');
  if(restoredPre.particleCells.historySerial!==selectedEnd.particleCells.historySerial)
    fail('Restored particle historySerial differs from selected END without a new constructor');
  const armedPayload=payloadHeader(armedPre),restoredPayload=payloadHeader(restoredPre),selectedPayload=payloadHeader(selectedEnd);
  if(armedPayload.watermark!==armed.currentWatermark||restoredPayload.watermark!==restored.currentWatermark||
    selectedPayload.watermark!==selected.currentWatermark||armedPayload.revision!==armed.currentRevision||
    restoredPayload.revision!==restored.currentRevision||selectedPayload.revision!==selected.currentRevision)
    fail('raw Payload.Header fences do not join their native sidecar receipts');
  return {equal:true,preSourceFrame:armedPre.sourceFrame,selectedSourceFrame:selectedEnd.sourceFrame,
    selectedRestoreCount:selected.restoreCount,restoredRestoreCount:restored.restoreCount,
    rawDomains:9,rawPreDifferencesAllowed:['Payload.Header.Watermark','Payload.Header.TopologyRevision'],
    sidecarBytes:ARENA_BYTES,arenaBytes:armed.arenaBytes.toString(),
    currentRevision:restored.currentRevision.toString(),currentPieceRevision:restored.currentPieceRevision.toString(),
    particleHistorySerial:restoredPre.particleCells.historySerial,
    capturedRows:restoredPre.particleCells.capturedCount,liveRows:restoredPre.particleCells.liveCount};
}
