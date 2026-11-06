// SPDX-License-Identifier: GPL-3.0-or-later
// Focused controls for the opt-in native arena receipt and same-page restore
// comparison. These fixtures are structural, not source/gameplay evidence.
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {
  CPU_ENDPOINT_LAYOUT,CPU_ENDPOINT_RECORD_BYTES,collectCpuEndpointInPage,compareCpuEndpoints,
} from './cpu_endpoint.mjs';
import {compareCpuArenaRestoredPre,decodeCpuArenaEndpointResult} from './cpu_arena_endpoint.mjs';

const PTR=512,HEAP_BYTES=131072,ARENA_PTR=120000,PARTICLE_HEADER_PTR=100000,PARTICLE_ROW_PTR=100064;
const HOST_BASE=0x9000,SOURCE_BASE=0x8000,DESCRIPTOR=0xa000,ARENA_BASE=0x1000,ARENA_EXTENT=64*1024;
const U64_MAX=0xffff_ffff_ffff_ffffn;
const key=(domain,kind,index)=>`${domain}/${kind}/${index}`;
const toWire=value=>JSON.parse(JSON.stringify(value));

function named(values,fields,name,value){values.set(name,BigInt(value));}
function makeFixture(){
  const heap=new Uint8Array(HEAP_BYTES),heapView=new DataView(heap.buffer),rows=new Map(),calls=[];
  let nextBorrow=32768,settings=null,batchActive=false,endpointBytes=144,endStatus=0;
  const fieldsFor=(domain,kind,index)=>{
    const fields=kind==='Header'&&CPU_ENDPOINT_LAYOUT.domains[domain-1].header?
      CPU_ENDPOINT_LAYOUT.domains[domain-1].header.fields:
      CPU_ENDPOINT_LAYOUT.domains[domain-1].groups.find(group=>group.row.kindName===kind)?.row.fields;
    assert(fields,`${domain}/${kind}`);return fields;
  };
  const buildRow=(domainSpec,rowSpec,index,count)=>{
    const v=new Map(),set=(name,value)=>named(v,rowSpec.fields,name,value);
    if(rowSpec.kindName==='Header'){
      if(domainSpec.id===3)Object.entries({LeafSchema:1,BindingCount:1,SlotsPerBinding:2,CacheCount:1,EventCount:1}).forEach(([n,x])=>set(n,x));
      if(domainSpec.id===4)Object.entries({Schema:1,HeapHandle:4,HeapBytes:64000,OwnerEpoch:3,World:1,
        Watermark:settings?.currentWatermark??11,TopologyRevision:settings?.currentRevision??5,
        ArenaBegin:4096,ArenaEnd:60000,RowCount:1,FreeCellCount:1,AllocatedCellCount:1,PayloadBytes:32}).forEach(([n,x])=>set(n,x));
      if(domainSpec.id===5)Object.entries({Identity:101,Epoch:3,Generation:4,SourceValid:1,Complete:1,
        OpeningDemo:0,ClockFieldCount:19,ProcessMaskBytes:3,ProfilePhaseBytes:2,CadenceFieldCount:14}).forEach(([n,x])=>set(n,x));
      if(domainSpec.id===7)set('RangeCount',2);
      if(domainSpec.id===8)Object.entries({Generation:3,OwnedRangeCount:1,CommandBudgetAddress:20000,SoundModeAddress:21000}).forEach(([n,x])=>set(n,x));
      if(domainSpec.id===9)Object.entries({TableIdentity:30000,RowCount:1}).forEach(([n,x])=>set(n,x));
    }
    if(domainSpec.id===1&&rowSpec.kindName==='Owner'){
      set('Storage',0x20000000000001n);set('Audio',25000);set('World',1);set('ContextEpoch',3);
      set('SourceFrame',settings?.sourceFrame??0);
    }
    if(domainSpec.id===2&&rowSpec.kindName==='EffectToken')set('Token',settings?.effectToken??7);
    if(rowSpec.kindName==='ActionSlot'){
      set('Binding',Math.floor(index/2));set('Slot',index%2);set('SymbolBytes',3);
    }
    if(rowSpec.kindName==='ActionCacheEntry'){
      set('CacheIndex',Math.floor(index/2));set('Entry',index%2);set('SymbolBytes',3);
    }
    if(rowSpec.kindName==='ActionEvent')set('SymbolBytes',3);
    if(rowSpec.kindName==='PayloadRow'){
      set('Index',index);set('SourceAddress',SOURCE_BASE);set('RequestedBytes',32);
      set('HostAddress',HOST_BASE);set('AllocationGeneration',7);
    }
    if(rowSpec.kindName==='PayloadFreeCell'||rowSpec.kindName==='PayloadAllocatedCell'){
      set('Kind',rowSpec.kindName==='PayloadFreeCell'?1:2);set('Index',index);
    }
    if(domainSpec.id===5&&rowSpec.kindName==='FlowProcessMask')set('Extent',3);
    if(domainSpec.id===5&&rowSpec.kindName==='FlowProfilePhase')set('Extent',2);
    if(domainSpec.id===5&&rowSpec.kindName==='FlowCadenceRecord')set('AlarmExtent',4);
    if(domainSpec.id===6&&rowSpec.kindName==='ContextRange')set('Extent',3);
    if(domainSpec.id===7&&rowSpec.kindName==='BootstrapTicks')set('Extent',3);
    if(domainSpec.id===7&&rowSpec.kindName==='BootstrapDisabledLinks')set('Extent',4);
    if(domainSpec.id===8&&rowSpec.kindName==='AudioOwnedRange'){
      set('Kind',1);set('Address',30000);set('Extent',3);set('OwnerIdentity',25000);set('Generation',3);
    }
    if((rowSpec.kindName==='AudioCommandBudget'||rowSpec.kindName==='AudioSoundMode'))set('Extent',4);
    if(domainSpec.id===9&&rowSpec.kindName==='StaticRow'){set('Index',index);set('Extent',5);}
    const values=rowSpec.fields.map((name,field)=>v.has(name)?v.get(name):BigInt(0x1000+domainSpec.id*100+rowSpec.kind*10+index+field));
    let borrowedAddress=0,borrowedBytes=0;
    if(rowSpec.borrowField){
      borrowedBytes=Number(values[rowSpec.fields.indexOf(rowSpec.borrowField)]);
      if(borrowedBytes){borrowedAddress=nextBorrow;nextBorrow+=borrowedBytes+8;
        for(let offset=0;offset<borrowedBytes;offset++)heap[borrowedAddress+offset]=(domainSpec.id*31+rowSpec.kind*7+index+offset)&255;}
    }
    const record={status:0,schema:1,view:settings?.view??1,provenance:settings?.provenance??1,
      domain:domainSpec.id,kind:rowSpec.kind,index,count,metadataBytes:values.length*8,flags:0,
      fields:values,borrowedAddress,borrowedBytes,reserved:0};
    if(rowSpec.kindName==='PayloadRow'){
      record.borrowedAddress=HOST_BASE;record.borrowedBytes=32;heap.fill(0x44,HOST_BASE,HOST_BASE+32);
    }
    rows.set(key(domainSpec.id,rowSpec.kind,index),{record,fieldNames:rowSpec.fields});
  };
  const updateSettings=next=>{
    settings={...next};
    for(const spec of CPU_ENDPOINT_LAYOUT.domains){
      let header=null;
      if(spec.header){buildRow(spec,spec.header,0,1);header=rows.get(key(spec.id,0,0)).record;}
      for(const group of spec.groups){
        let count;if(Number.isInteger(group.count))count=group.count;
        else if(typeof group.count==='string')count=Number(header.fields[spec.header.fields.indexOf(group.count)]);
        else if(group.count.optional)count=BigInt(header.fields[spec.header.fields.indexOf(group.count.optional)])>0n?1:0;
        else{const base=BigInt(header.fields[spec.header.fields.indexOf(group.count.field)]);
          const factor=group.count.factorField?BigInt(header.fields[spec.header.fields.indexOf(group.count.factorField)]):BigInt(group.count.factor||1);
          count=Number(base*factor);}
        for(let index=0;index<count;index++)buildRow(spec,group.row,index,count);
      }
    }
    const anchor=rows.get(key(2,4,0)).record;
    const anchorNames=fieldsFor(2,'ParticleAnchor');
    anchor.fields[anchorNames.indexOf('World')]=1n;anchor.fields[anchorNames.indexOf('Serial')]=2n;
    anchor.fields[anchorNames.indexOf('AnchorToken')]=3n;anchor.fields[anchorNames.indexOf('ParticleCount')]=1n;
    anchor.fields[anchorNames.indexOf('AppsrtCount')]=0n;
    const payload=rows.get(key(4,0,0)).record,payloadNames=fieldsFor(4,'Header');
    payload.fields[payloadNames.indexOf('PayloadBytes')]=32n;
  };
  const writeRawRecord=record=>{
    heap.fill(0,PTR,PTR+CPU_ENDPOINT_RECORD_BYTES);
    heapView.setUint32(PTR,record.status,true);heapView.setUint32(PTR+4,record.schema,true);
    heapView.setUint32(PTR+8,record.view,true);heapView.setUint32(PTR+12,record.provenance,true);
    heapView.setUint32(PTR+16,record.domain,true);heapView.setUint32(PTR+20,record.kind,true);
    heapView.setUint32(PTR+24,record.index,true);heapView.setUint32(PTR+28,record.count,true);
    heapView.setUint32(PTR+32,record.metadataBytes,true);heapView.setUint32(PTR+36,record.flags,true);
    record.fields.forEach((value,index)=>heapView.setBigUint64(PTR+40+index*8,BigInt(value),true));
    heapView.setBigUint64(PTR+296,BigInt(record.borrowedAddress),true);
    heapView.setUint32(PTR+304,record.borrowedBytes,true);heapView.setUint32(PTR+308,record.reserved,true);
  };
  const writeParticleHeader=()=>{
    heapView.setUint32(PARTICLE_HEADER_PTR,1,true);heapView.setUint32(PARTICLE_HEADER_PTR+4,64,true);
    heapView.setUint32(PARTICLE_HEADER_PTR+8,1,true);heapView.setUint32(PARTICLE_HEADER_PTR+12,1,true);
    heapView.setBigUint64(PARTICLE_HEADER_PTR+16,1n,true);heapView.setBigUint64(PARTICLE_HEADER_PTR+24,2n,true);
    heapView.setBigUint64(PARTICLE_HEADER_PTR+32,3n,true);
    heapView.setBigUint64(PARTICLE_HEADER_PTR+40,BigInt(settings?.particleHistorySerial??2),true);
    heap.fill(0,PARTICLE_HEADER_PTR+48,PARTICLE_HEADER_PTR+64);
  };
  const writeParticleRow=kind=>{
    heapView.setUint32(PARTICLE_ROW_PTR,1,true);heapView.setUint32(PARTICLE_ROW_PTR+4,96,true);
    heapView.setUint32(PARTICLE_ROW_PTR+8,kind,true);heapView.setUint32(PARTICLE_ROW_PTR+12,0,true);
    heapView.setUint32(PARTICLE_ROW_PTR+16,0,true);heapView.setUint32(PARTICLE_ROW_PTR+20,8,true);
    for(const [offset,value] of [[24,2n],[32,BigInt(HOST_BASE+8)],[40,1n],[48,3n],
      [56,7n],[64,BigInt(HOST_BASE)],[72,BigInt(DESCRIPTOR)]])heapView.setBigUint64(PARTICLE_ROW_PTR+offset,value,true);
    heapView.setUint32(PARTICLE_ROW_PTR+80,SOURCE_BASE+8,true);heapView.setUint32(PARTICLE_ROW_PTR+84,8,true);
    heapView.setUint32(PARTICLE_ROW_PTR+88,8,true);heapView.setUint32(PARTICLE_ROW_PTR+92,0,true);
  };
  const writeArena=()=>{
    const buffer=heap.subarray(ARENA_PTR,ARENA_PTR+144);buffer.fill(0);const v=new DataView(buffer.buffer,buffer.byteOffset,buffer.byteLength);
    const s=settings.sidecar;v.setUint32(0,1,true);v.setUint32(4,144,true);
    for(const [offset,name] of [[8,'view'],[12,'provenance'],[16,'sourceFrame'],[20,'incarnation'],
      [24,'restoreCount'],[28,'flags'],[32,'prePieceWatermark'],[36,'currentPieceWatermark'],
      [40,'poolRefusal'],[44,'originRefusal'],[48,'pieceRefusal'],[52,'reserved']])v.setUint32(offset,s[name]??0,true);
    for(const [offset,name] of [[56,'effectToken'],[64,'preRevision'],[72,'currentRevision'],
      [80,'preWatermark'],[88,'currentWatermark'],[96,'prePieceRevision'],[104,'currentPieceRevision'],
      [112,'arenaBase'],[120,'arenaBytes'],[128,'bytesCompared'],[136,'firstMismatchOffset']])
      v.setBigUint64(offset,BigInt(s[name]??0),true);
  };
  updateSettings({view:1,provenance:1,sourceFrame:0,effectToken:7,currentWatermark:11,currentRevision:5});
  const module={HEAPU8:heap,calledRun:true,
    _melee_web_native_menu_match_history_cpu_endpoint_begin:(view,provenance)=>{
      calls.push(['begin',view,provenance]);batchActive=true;settings.view=view;settings.provenance=provenance;updateSettings(settings);return 0;},
    _melee_web_native_menu_match_history_cpu_endpoint_data:()=>PTR,
    _melee_web_native_menu_match_history_cpu_endpoint_bytes:()=>CPU_ENDPOINT_RECORD_BYTES,
    _melee_web_native_menu_match_history_cpu_endpoint_record:(domain,kind,index)=>{
      calls.push(['record',domain,kind,index]);const row=rows.get(key(domain,kind,index));
      if(!row)return 5;writeRawRecord(row.record);return 0;},
    _melee_web_native_menu_match_history_cpu_endpoint_end:()=>{
      calls.push(['end']);batchActive=false;return endStatus;},
    _melee_web_native_menu_match_history_cpu_particle_endpoint_header:()=>{
      calls.push(['particle-header']);assert.equal(batchActive,true);writeParticleHeader();return 0;},
    _melee_web_native_menu_match_history_cpu_particle_endpoint_header_data:()=>PARTICLE_HEADER_PTR,
    _melee_web_native_menu_match_history_cpu_particle_endpoint_header_bytes:()=>64,
    _melee_web_native_menu_match_history_cpu_particle_endpoint_row:(kind,index)=>{
      calls.push(['particle-row',kind,index]);assert.equal(batchActive,true);if(index!==0||![1,2].includes(kind))return 5;writeParticleRow(kind);return 0;},
    _melee_web_native_menu_match_history_cpu_particle_endpoint_row_data:()=>PARTICLE_ROW_PTR,
    _melee_web_native_menu_match_history_cpu_particle_endpoint_row_bytes:()=>96,
    _melee_web_native_menu_match_history_cpu_arena_endpoint_data:()=>{
      calls.push(['arena-data']);assert.equal(batchActive,true);writeArena();return ARENA_PTR;},
    _melee_web_native_menu_match_history_cpu_arena_endpoint_bytes:()=>{
      calls.push(['arena-bytes']);assert.equal(batchActive,true);return endpointBytes;},
  };
  return {module,calls,
    observe(next){updateSettings(next);},setEndStatus(value){endStatus=value;},setEndpointBytes(value){endpointBytes=value;},
    get batchActive(){return batchActive;}};
}

function config({view,provenance,sourceFrame,effectToken=7,incarnation=1,restoreCount=0,
  currentRevision,preRevision=5,currentWatermark,preWatermark=11,currentPieceRevision,
  prePieceRevision=2,currentPieceWatermark=4,prePieceWatermark=4,flags=3,
  bytesCompared=ARENA_EXTENT,firstMismatchOffset=U64_MAX,particleHistorySerial=2}={}){
  return {view,provenance,sourceFrame,effectToken,currentWatermark,currentRevision,particleHistorySerial,
    sidecar:{view,provenance,sourceFrame,incarnation,restoreCount,flags,
      prePieceWatermark,currentPieceWatermark,poolRefusal:0,originRefusal:0,pieceRefusal:0,reserved:0,
      effectToken,preRevision,currentRevision,preWatermark,currentWatermark,
      prePieceRevision,currentPieceRevision,arenaBase:ARENA_BASE,arenaBytes:ARENA_EXTENT,
      bytesCompared,firstMismatchOffset}};
}
async function collectArena(fixture,options){
  fixture.observe(options);
  const value=collectCpuEndpointInPage({view:options.view,provenance:options.provenance,
    expectedSourceFrame:options.sourceFrame,particleCellEndpoint:true,arenaHistoryEndpoint:true},
  {runtime:'match-history',module:fixture.module});
  return decodeCpuArenaEndpointResult(toWire(value));
}
function triplet(){
  const fixture=makeFixture();
  const armed=config({view:1,provenance:1,sourceFrame:0,currentWatermark:11,currentRevision:5,
    currentPieceRevision:2,prePieceRevision:2});
  const selected=config({view:3,provenance:2,sourceFrame:1,currentWatermark:12,currentRevision:8,
    currentPieceRevision:6,currentPieceWatermark:5,flags:0,bytesCompared:6,firstMismatchOffset:5,
    particleHistorySerial:8});
  const restored=config({view:1,provenance:1,sourceFrame:0,restoreCount:1,currentWatermark:12,
    currentRevision:9,currentPieceRevision:7,currentPieceWatermark:5,particleHistorySerial:8});
  return {fixture,armed,selected,restored};
}

test('opt-in collector copies the real 144-byte witness inside the batch and preserves schema 1 rows',async()=>{
  const fixture=makeFixture(),options=config({view:1,provenance:1,sourceFrame:0,effectToken:0,incarnation:0,
    currentWatermark:11,currentRevision:5,currentPieceRevision:2,prePieceRevision:2,particleHistorySerial:2});
  const capture=await collectArena(fixture,options);
  assert.equal(capture.schema,3);assert.equal(capture.rawSchema,1);assert.equal(capture.particleCells.schema,1);
  assert.equal(capture.arenaHistoryEndpointBase64.length,192);
  const calls=fixture.calls.map(row=>row[0]);
  assert(calls.indexOf('arena-data')>calls.indexOf('particle-row'));
  assert(calls.indexOf('arena-bytes')<calls.indexOf('end'));
  assert.equal(fixture.batchActive,false);
  const raw={schema:1,recordBytes:capture.recordBytes,view:capture.view,provenance:capture.provenance,
    sourceFrame:capture.sourceFrame,revalidated:capture.revalidated,domains:capture.domains};
  assert.throws(()=>compareCpuEndpoints(raw,{...raw,domains:structuredClone(raw.domains).map((domain,index)=>
    index===3?{...domain,records:domain.records.map(record=>record.kindName==='Header'?{
      ...record,fields:record.fields.map((field,i)=>record.fieldNames[i]==='Watermark'?'12':field)}:record)}:domain)}),
    /Payload\.Header\[0\]\.metadata\[Watermark\]/);
});

test('same-page Armed PRE, selected END and Restored PRE prove exact fences for first and second restore',async()=>{
  const {fixture,armed,selected,restored}=triplet();
  const armedValue=await collectArena(fixture,armed),selectedValue=await collectArena(fixture,selected),restoredValue=await collectArena(fixture,restored);
  assert.deepEqual(compareCpuArenaRestoredPre(armedValue,restoredValue,selectedValue),{
    equal:true,preSourceFrame:'0',selectedSourceFrame:'1',selectedRestoreCount:0,restoredRestoreCount:1,
    rawDomains:9,rawPreDifferencesAllowed:['Payload.Header.Watermark','Payload.Header.TopologyRevision'],
    sidecarBytes:144,arenaBytes:String(ARENA_EXTENT),currentRevision:'9',currentPieceRevision:'7',
    particleHistorySerial:'8',capturedRows:1,liveRows:1});
  const selectedAgain=config({...selected,incarnation:2,restoreCount:1,currentRevision:9,currentPieceRevision:7,
    currentPieceWatermark:5,
    bytesCompared:ARENA_EXTENT,firstMismatchOffset:U64_MAX,flags:1,particleHistorySerial:9});
  const restoredAgain=config({...restored,incarnation:2,restoreCount:2,currentRevision:10,currentPieceRevision:8,
    currentPieceWatermark:5,
    flags:3,bytesCompared:ARENA_EXTENT,firstMismatchOffset:U64_MAX,particleHistorySerial:9});
  const selectedAgainValue=await collectArena(fixture,selectedAgain),restoredAgainValue=await collectArena(fixture,restoredAgain);
  assert.equal(compareCpuArenaRestoredPre(armedValue,restoredAgainValue,selectedAgainValue).restoredRestoreCount,2);
});

test('decoder and comparator reject absent, truncated, false-exact, stale and unjoined receipts',async()=>{
  const {fixture,armed,selected,restored}=triplet();
  const a=await collectArena(fixture,armed),s=await collectArena(fixture,selected),r=await collectArena(fixture,restored);
  const missing=structuredClone(a);delete missing.arenaHistoryEndpointBase64;
  assert.throws(()=>decodeCpuArenaEndpointResult(missing),/missing or unrecognized/);
  const short=structuredClone(a);short.arenaHistoryEndpointBase64=Buffer.alloc(143).toString('base64');
  assert.throws(()=>decodeCpuArenaEndpointResult(short),/not exactly 144/);
  const badExact=structuredClone(s),side=Buffer.from(badExact.arenaHistoryEndpointBase64,'base64');
  new DataView(side.buffer,side.byteOffset,side.byteLength).setBigUint64(136,U64_MAX,true);
  badExact.arenaHistoryEndpointBase64=side.toString('base64');
  assert.throws(()=>decodeCpuArenaEndpointResult(badExact),/truthful first-mismatch prefix/);
  for(const [label,mutate,pattern] of [
    ['stale revision',value=>{value.sidecar.currentRevision=7;value.currentRevision=7;},/fences do not follow/],
    ['wrong source owner token',value=>{value.effectToken=value.sidecar.effectToken=8;},/immutable PRE effectToken/],
    ['wrong immutable fence',value=>{value.sidecar.prePieceRevision=3;},/immutable PRE.*differs/],
    ['wrong arena extent',value=>{value.sidecar.arenaBytes=ARENA_EXTENT+1;},/immutable PRE arenaBytes/],
    ['wrong incarnation',value=>{value.sidecar.incarnation=2;},/incarnation differs/],
    ['earlier CPU receipt',value=>{value.view=value.sidecar.view=2;},/Reference DRAW_END/],
    ['selected END beyond seven frames',value=>{value.sourceFrame=value.sidecar.sourceFrame=8;},/one to seven frames/],
  ]){
    const bad={...selected,sidecar:{...selected.sidecar}};mutate(bad);
    const badValue=await collectArena(fixture,bad);
    assert.throws(()=>compareCpuArenaRestoredPre(a,r,badValue),pattern,label);
  }
});

test('decoder refuses sidecar counters and host arena ranges outside native bounds',async()=>{
  const {fixture,selected}=triplet();
  for(const [override,pattern] of [
    [{incarnation:4},/incarnation or restore count exceeds/],
    [{restoreCount:3},/incarnation or restore count exceeds/],
    [{arenaBase:HOST_BASE+1},/PayloadRow\[0\] escapes/],
    [{arenaBytes:HOST_BASE-ARENA_BASE+31},/PayloadRow\[0\] escapes/],
    [{arenaBytes:64*1024*1024+1},/sidecar arena bounds/],
  ]){
    const malformed={...selected,sidecar:{...selected.sidecar,...override}};
    await assert.rejects(collectArena(fixture,malformed),pattern);
  }
});

test('PRE comparator retains every payload and non-payload byte except the two sidecar-witnessed header fields',async()=>{
  for(const [name,mutate,pattern] of [
    ['payload bytes',endpoint=>{const row=endpoint.domains[3].records.find(r=>r.kindName==='PayloadRow');
      const bytes=Buffer.from(row.bytesBase64,'base64');bytes[0]^=1;row.bytesBase64=bytes.toString('base64');},/Payload\.PayloadRow\[0\]\.borrowed bytes differ/],
    ['other-domain metadata',endpoint=>{endpoint.domains[2].records.find(r=>r.kindName==='ActionBinding').fields[1]='5000';},/Actions\.ActionBinding\[0\]\.metadata\[Generation\] differs/],
    ['payload header join',endpoint=>{const header=endpoint.domains[3].records[0];header.fields[header.fieldNames.indexOf('Watermark')]='10';},/sidecar current allocation\/revision fences/],
  ]){
    const {fixture,armed,selected,restored}=triplet();
    const a=await collectArena(fixture,armed),s=await collectArena(fixture,selected),r=await collectArena(fixture,restored);
    const changed=structuredClone(r);mutate(changed);
    assert.throws(()=>compareCpuArenaRestoredPre(a,changed,s),pattern,name);
  }
});

test('collector closes the active batch and preserves end revalidation failure',()=>{
  const fixture=makeFixture(),options=config({view:2,provenance:2,sourceFrame:1,currentWatermark:12,
    currentRevision:8,currentPieceRevision:6,currentPieceWatermark:5,flags:0,bytesCompared:1,
    firstMismatchOffset:0,particleHistorySerial:4});
  fixture.observe(options);fixture.setEndStatus(0x53c);
  assert.throws(()=>collectCpuEndpointInPage({view:2,provenance:2,expectedSourceFrame:1,
    particleCellEndpoint:true,arenaHistoryEndpoint:true},{runtime:'match-history',module:fixture.module}),error=>{
    assert.equal(error.firstFailure,null);assert.match(error.endFailure,/end\/revalidation refused with status 1340/);
    return true;
  });
  assert(fixture.calls.some(row=>row[0]==='arena-data'));
  assert.equal(fixture.calls.at(-1)[0],'end');assert.equal(fixture.batchActive,false);
});

test('arena option refuses legacy binding and malformed native extent before accepting data',()=>{
  const fixture=makeFixture();
  assert.throws(()=>collectCpuEndpointInPage({view:1,provenance:1,particleCellEndpoint:true,
    arenaHistoryEndpoint:true},{runtime:'other',module:fixture.module}),/runtime binding/);
  const wrongExtent=makeFixture();wrongExtent.setEndpointBytes(143);
  const options=config({view:1,provenance:1,sourceFrame:0,effectToken:0,incarnation:0,
    currentWatermark:11,currentRevision:5,currentPieceRevision:2,prePieceRevision:2});
  wrongExtent.observe(options);
  assert.throws(()=>collectCpuEndpointInPage({view:1,provenance:1,expectedSourceFrame:0,
    particleCellEndpoint:true,arenaHistoryEndpoint:true},{runtime:'match-history',module:wrongExtent.module}),
    /extent is not exactly 144/);
  assert.equal(wrongExtent.calls.at(-1)[0],'end');
});
