// SPDX-License-Identifier: GPL-3.0-or-later
// Mocked endpoint-reader controls. These validate the fixed transport and
// comparator; they are not a gameplay, source-owner, or browser test.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import vm from 'node:vm';
import {
  CPU_ENDPOINT_AUDIO_RANGE, CPU_ENDPOINT_LAYOUT, CPU_ENDPOINT_PROVENANCE,
  CPU_ENDPOINT_RECORD_BYTES, CPU_ENDPOINT_STATUS, CPU_ENDPOINT_VIEW,
  collectCpuEndpointInPage, compareCpuEndpoints, decodeCpuEndpointResult,
  decodeCpuParticleEndpointResult, compareCpuParticleEndpoints, compareCpuReferenceParticleEndpoints,
  validateCpuParticleEndpointAgainstPre,
} from './cpu_endpoint.mjs';

const PTR = 512;
const HEAP_BYTES = 131072;
const toWire = value => JSON.parse(JSON.stringify(value));
const keyOf = (domain, kind, index) => `${domain}/${kind}/${index}`;

function parseCEnum(source, enumName) {
  const match=source.match(new RegExp(`typedef enum ${enumName}\\s*\\{([\\s\\S]*?)\\}\\s*${enumName}\\s*;`));
  assert.ok(match,`missing ${enumName}`);
  const body=match[1].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
  const result=new Map();let previous=-1;
  for(const raw of body.split(',')){
    const token=raw.trim();if(!token)continue;
    const item=token.match(/^([A-Z0-9_]+)(?:\s*=\s*(0x[0-9a-fA-F]+|[0-9]+))?$/);
    assert.ok(item,`cannot parse ${enumName} member ${token}`);
    const value=item[2]===undefined?previous+1:Number(item[2]);
    result.set(item[1],value);previous=value;
  }
  return result;
}

function makeAudioRangeKinds(count,{omitPcmBytes=false}={}) {
  let kinds = [...CPU_ENDPOINT_AUDIO_RANGE.legacyKinds];
  const channelKinds = new Set([
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_PCM,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_FRAMES,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOP_PCM,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOP_AND_NIBBLES,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_COEFFICIENTS,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_PREDICTORS,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_HISTORIES,
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOPING,
  ]);
  for (const streamKind of Object.values(CPU_ENDPOINT_AUDIO_RANGE.streamKinds)) {
    const indexes = channelKinds.has(streamKind) ||
      streamKind === CPU_ENDPOINT_AUDIO_RANGE.streamKinds.PCM_BYTES
      ? [0,1,2,3,4,5] : [0];
    for (const index of indexes)
      kinds.push(CPU_ENDPOINT_AUDIO_RANGE.streamBase + streamKind * CPU_ENDPOINT_AUDIO_RANGE.streamKindStride + index);
  }
  if(omitPcmBytes)kinds=kinds.filter(kind=>kind<CPU_ENDPOINT_AUDIO_RANGE.streamBase||
    Math.floor((kind-CPU_ENDPOINT_AUDIO_RANGE.streamBase)/CPU_ENDPOINT_AUDIO_RANGE.streamKindStride)!==
      CPU_ENDPOINT_AUDIO_RANGE.streamKinds.PCM_BYTES);
  while (kinds.length < count) kinds.push(CPU_ENDPOINT_AUDIO_RANGE.streamBase + 24 * 8);
  return kinds.slice(0,count);
}

function overrides(domain, kind, index, audioKinds) {
  const values = new Map();
  const set = (name, value) => values.set(name, BigInt(value));
  if (kind === 'Header') {
    if (domain === 'Actions') Object.entries({LeafSchema:1,BindingCount:1,SlotsPerBinding:2,CacheCount:1,EventCount:1}).forEach(([n,v])=>set(n,v));
    if (domain === 'Payload') Object.entries({Schema:1,HeapHandle:4,HeapBytes:64000,OwnerEpoch:3,World:2,Watermark:11,TopologyRevision:7,ArenaBegin:4096,ArenaEnd:60000,RowCount:1,FreeCellCount:1,AllocatedCellCount:1,PayloadBytes:3}).forEach(([n,v])=>set(n,v));
    if (domain === 'Flow') Object.entries({Identity:101,Epoch:3,Generation:4,SourceValid:1,Complete:1,OpeningDemo:0,ClockFieldCount:19,ProcessMaskBytes:3,ProfilePhaseBytes:2,CadenceFieldCount:14}).forEach(([n,v])=>set(n,v));
    if (domain === 'Bootstrap') set('RangeCount',2);
    if (domain === 'Audio') Object.entries({Generation:3,OwnedRangeCount:audioKinds.length,CommandBudgetAddress:20000,SoundModeAddress:21000}).forEach(([n,v])=>set(n,v));
    if (domain === 'Statics') Object.entries({TableIdentity:30000,RowCount:1}).forEach(([n,v])=>set(n,v));
  }
  if (domain === 'Owner' && kind === 'Owner') {
    set('Storage', 0x20_0000_0000_0001n); // Above Number's exact integer range.
    set('Audio',25000);
    set('SourceFrame', 300);
  }
  if (kind === 'ActionSlot') { set('Binding', Math.floor(index / 2)); set('Slot', index % 2); set('SymbolBytes',3); }
  if (kind === 'ActionCacheEntry') { set('CacheIndex',Math.floor(index/2)); set('Entry',index%2); set('SymbolBytes',3); }
  if (kind === 'ActionEvent') set('SymbolBytes',3);
  if (kind === 'PayloadRow') { set('Index',index); set('RequestedBytes',3); }
  if (kind === 'PayloadFreeCell') { set('Kind',1); set('Index',index); }
  if (kind === 'PayloadAllocatedCell') { set('Kind',2); set('Index',index); }
  if (kind === 'FlowProcessMask') set('Extent',3);
  if (kind === 'FlowProfilePhase') set('Extent',2);
  if (kind === 'FlowCadenceRecord') set('AlarmExtent',4);
  if (kind === 'ContextRange') set('Extent',3);
  if (kind === 'BootstrapTicks') set('Extent',3);
  if (kind === 'BootstrapDisabledLinks') set('Extent',4);
  if (kind === 'AudioOwnedRange') {
    const rangeKind=audioKinds[index];
    set('Kind',rangeKind);set('Address',30000+index*16);set('Extent',3);
    // Actual FX provider uses its own object identity/generation for kinds7..9.
    const effects=rangeKind>=7&&rangeKind<=9;
    set('OwnerIdentity',effects?27000:25000);
    set('Generation',effects?13:(rangeKind>=CPU_ENDPOINT_AUDIO_RANGE.streamBase?4:3));
    if(rangeKind===7)set('Address',27000);
  }
  if (kind === 'AudioCommandBudget' || kind === 'AudioSoundMode') set('Extent',4);
  if (kind === 'StaticRow') { set('Index',index); set('Extent',5); }
  return values;
}

function countFor(group, header) {
  if (Number.isInteger(group.count)) return group.count;
  if (typeof group.count === 'string') return Number(header.fields[header.fieldNames.indexOf(group.count)]);
  if (group.count.optional) return header.fields[header.fieldNames.indexOf(group.count.optional)] > 0n ? 1 : 0;
  const base = header.fields[header.fieldNames.indexOf(group.count.field)];
  const factor = group.count.factorField
    ? header.fields[header.fieldNames.indexOf(group.count.factorField)] : BigInt(group.count.factor || 1);
  return Number(base * factor);
}

function makeFixture({view=1,provenance=1,baseAddress=32768,borrowMutation=false,audioRangeCount=1,
  omitPcmBytes=false}={}) {
  const heap = new Uint8Array(HEAP_BYTES), rows = new Map();
  const audioKinds=makeAudioRangeKinds(audioRangeCount,{omitPcmBytes});
  let nextAddress = baseAddress, previousBorrow = null, borrowSerial = 0;
  const heapView = new DataView(heap.buffer);
  const writeRecord = (record) => {
    heap.fill(0,PTR,PTR+CPU_ENDPOINT_RECORD_BYTES);
    heapView.setUint32(PTR+0,record.status,true); heapView.setUint32(PTR+4,record.schema,true);
    heapView.setUint32(PTR+8,record.view,true); heapView.setUint32(PTR+12,record.provenance,true);
    heapView.setUint32(PTR+16,record.domain,true); heapView.setUint32(PTR+20,record.kind,true);
    heapView.setUint32(PTR+24,record.index,true); heapView.setUint32(PTR+28,record.count,true);
    heapView.setUint32(PTR+32,record.metadataBytes,true); heapView.setUint32(PTR+36,record.flags,true);
    record.fields.forEach((field,index)=>heapView.setBigUint64(PTR+40+index*8,BigInt(field),true));
    heapView.setBigUint64(PTR+296,BigInt(record.borrowedAddress),true);
    heapView.setUint32(PTR+304,record.borrowedBytes,true); heapView.setUint32(PTR+308,record.reserved,true);
  };
  const buildRow = (domainSpec,rowSpec,index,count) => {
    const values = overrides(domainSpec.name,rowSpec.kindName,index,audioKinds);
    const fields = rowSpec.fields.map((name,fieldIndex)=>values.has(name) ? values.get(name) :
      BigInt(0x1000 + domainSpec.id*100 + rowSpec.kind*10 + index + fieldIndex));
    let borrowedAddress=0, borrowedBytes=0;
    if (rowSpec.borrowField !== null) {
      borrowedBytes = Number(fields[rowSpec.fields.indexOf(rowSpec.borrowField)]);
      if (borrowedBytes) {
        borrowedAddress = nextAddress; nextAddress += borrowedBytes + 8;
        for(let offset=0;offset<borrowedBytes;offset++) heap[borrowedAddress+offset]=(domainSpec.id*31+rowSpec.kind*7+index+offset)&0xff;
        borrowSerial++;
      }
    }
    const record={status:0,schema:1,view,provenance,domain:domainSpec.id,kind:rowSpec.kind,index,count,
      metadataBytes:fields.length*8,flags:0,fields,borrowedAddress,borrowedBytes,reserved:0};
    rows.set(keyOf(domainSpec.id,rowSpec.kind,index),{record,domainName:domainSpec.name,
      kindName:rowSpec.kindName,fieldNames:rowSpec.fields});
    return {record,fieldNames:rowSpec.fields};
  };
  for(const domainSpec of CPU_ENDPOINT_LAYOUT.domains){
    let header=null;
    if(domainSpec.header){
      const built=buildRow(domainSpec,domainSpec.header,0,1);
      header={...built.record,fieldNames:built.fieldNames};
    }
    for(const group of domainSpec.groups){
      const count=countFor(group,header);
      for(let index=0;index<count;index++)buildRow(domainSpec,group.row,index,count);
    }
  }
  const expectedRows=rows.size;
  const calls=[];let batchActive=false;
  const module={HEAPU8:heap,wasmMemory:{buffer:heap.buffer},calledRun:true,
    _melee_web_synctest_cpu_endpoint_begin:(gotView,gotProvenance)=>{calls.push(['begin',gotView,gotProvenance]);batchActive=true;return 0;},
    _melee_web_synctest_cpu_endpoint_data:()=>PTR,
    _melee_web_synctest_cpu_endpoint_bytes:()=>CPU_ENDPOINT_RECORD_BYTES,
    _melee_web_synctest_cpu_endpoint_record:(domainId,kind,index)=>{
      calls.push(['record',domainId,kind,index]);
      if(borrowMutation&&previousBorrow)heap.fill(0xee,previousBorrow.address,previousBorrow.address+previousBorrow.bytes);
      const found=rows.get(keyOf(domainId,kind,index));
      if(!found){writeRecord({status:CPU_ENDPOINT_STATUS.invalidIndex,schema:1,view:0,provenance:0,domain:0,kind:0,index:0,count:0,metadataBytes:0,flags:0,fields:Array(32).fill(0n),borrowedAddress:0,borrowedBytes:0,reserved:0});return 0;}
      const record={...found.record,fields:[...found.record.fields]};
      writeRecord(record);
      if(record.borrowedBytes)previousBorrow={address:record.borrowedAddress,bytes:record.borrowedBytes};
      return 0;
    },
    _melee_web_synctest_cpu_endpoint_end:()=>{calls.push(['end']);batchActive=false;return 0;},
  };
  const page={evaluate:(callback,request)=>{
    const context=vm.createContext({window:{__slippiSynctest:{module}},Uint8Array,DataView,SharedArrayBuffer,
      atob,btoa,console,performance,URL,TextEncoder,TextDecoder});
    context.request=request;
    return vm.runInContext(`(${callback.toString()})(request)`,context);
  }};
  return {heap,heapView,rows,module,page,calls,expectedRows,get borrowSerial(){return borrowSerial;},
    get batchActive(){return batchActive;}};
}

async function collect(fixture,view=1,provenance=1,extra={}) {
  const raw=await fixture.page.evaluate(collectCpuEndpointInPage,{view,provenance,...extra});
  return decodeCpuEndpointResult(toWire(raw));
}

function updateRawRecord(fixture,key,mutate) {
  const found=fixture.rows.get(key);
  assert.ok(found,key);
  mutate(found.record);
}

const PARTICLE_HEADER_PTR=100000, PARTICLE_ROW_PTR=100064, PARTICLE_HOST_BASE=0x9000,
  PARTICLE_SOURCE_BASE=0x8000, PARTICLE_DESCRIPTOR=0xa000;
function setNamed(record,name,value){
  const index=record.fieldNames.indexOf(name);assert.ok(index>=0,name);record.record.fields[index]=BigInt(value);
}

function installParticleSidecar(fixture,{anchorSerial=2,historySerial=anchorSerial,anchorToken=3,
  liveRows=null,capturedRows=null,headerAfter=null,rowStatuses=new Map()}={}){
  const payloadHeader=fixture.rows.get(keyOf(4,0,0));
  const payloadRow=fixture.rows.get(keyOf(4,1,0));
  setNamed(payloadHeader,'PayloadBytes',32);
  setNamed(payloadHeader,'World',1);setNamed(payloadHeader,'OwnerEpoch',3);
  setNamed(payloadRow,'SourceAddress',PARTICLE_SOURCE_BASE);
  setNamed(payloadRow,'RequestedBytes',32);
  setNamed(payloadRow,'HostAddress',PARTICLE_HOST_BASE);
  setNamed(payloadRow,'AllocationGeneration',7);
  payloadRow.record.borrowedAddress=PARTICLE_HOST_BASE;
  payloadRow.record.borrowedBytes=32;
  fixture.heap.fill(0x44,PARTICLE_HOST_BASE,PARTICLE_HOST_BASE+32);
  const particles=fixture.rows.get(keyOf(2,4,0));
  setNamed(particles,'World',1);setNamed(particles,'Serial',anchorSerial);
  setNamed(particles,'AnchorToken',anchorToken);
  const live=liveRows??[{kind:1,index:0,objectKind:0,extent:8,receiptSerial:anchorSerial,
    object:PARTICLE_HOST_BASE+8,world:1,epoch:3,allocation:7,pool:PARTICLE_HOST_BASE,
    descriptor:PARTICLE_DESCRIPTOR,source:PARTICLE_SOURCE_BASE+8,cellOffset:8,stride:8}];
  const captured=capturedRows??live.map(row=>({...row,kind:2}));
  setNamed(particles,'ParticleCount',captured.filter(row=>row.objectKind===0).length);
  setNamed(particles,'AppsrtCount',captured.filter(row=>row.objectKind===1).length);
  const header={schema:1,bytes:64,liveCount:live.length,capturedCount:captured.length,
    world:1,anchorSerial,anchorToken,historySerial};
  const data=fixture.heapView;
  const writeHeader=value=>{
    data.setUint32(PARTICLE_HEADER_PTR,value.schema,true);data.setUint32(PARTICLE_HEADER_PTR+4,value.bytes,true);
    data.setUint32(PARTICLE_HEADER_PTR+8,value.liveCount,true);data.setUint32(PARTICLE_HEADER_PTR+12,value.capturedCount,true);
    data.setBigUint64(PARTICLE_HEADER_PTR+16,BigInt(value.world),true);
    data.setBigUint64(PARTICLE_HEADER_PTR+24,BigInt(value.anchorSerial),true);
    data.setBigUint64(PARTICLE_HEADER_PTR+32,BigInt(value.anchorToken),true);
    data.setBigUint64(PARTICLE_HEADER_PTR+40,BigInt(value.historySerial),true);
    fixture.heap.fill(0,PARTICLE_HEADER_PTR+48,PARTICLE_HEADER_PTR+64);
  };
  const writeRow=row=>{
    data.setUint32(PARTICLE_ROW_PTR,1,true);data.setUint32(PARTICLE_ROW_PTR+4,96,true);
    data.setUint32(PARTICLE_ROW_PTR+8,row.kind,true);data.setUint32(PARTICLE_ROW_PTR+12,row.index,true);
    data.setUint32(PARTICLE_ROW_PTR+16,row.objectKind,true);data.setUint32(PARTICLE_ROW_PTR+20,row.extent,true);
    for(const [offset,name] of [[24,'receiptSerial'],[32,'object'],[40,'world'],[48,'epoch'],
      [56,'allocation'],[64,'pool'],[72,'descriptor']])data.setBigUint64(PARTICLE_ROW_PTR+offset,BigInt(row[name]),true);
    data.setUint32(PARTICLE_ROW_PTR+80,row.source,true);data.setUint32(PARTICLE_ROW_PTR+84,row.cellOffset,true);
    data.setUint32(PARTICLE_ROW_PTR+88,row.stride,true);data.setUint32(PARTICLE_ROW_PTR+92,0,true);
  };
  let headerReads=0;
  fixture.module._melee_web_synctest_cpu_particle_endpoint_header=()=>{
    fixture.calls.push(['particle-header']);assert.equal(fixture.batchActive,true);
    writeHeader(headerReads++===0?header:(headerAfter??header));return 0;
  };
  fixture.module._melee_web_synctest_cpu_particle_endpoint_header_data=()=>PARTICLE_HEADER_PTR;
  fixture.module._melee_web_synctest_cpu_particle_endpoint_header_bytes=()=>64;
  fixture.module._melee_web_synctest_cpu_particle_endpoint_row=(kind,index)=>{
    fixture.calls.push(['particle-row',kind,index]);assert.equal(fixture.batchActive,true);
    const status=rowStatuses.get(`${kind}/${index}`)??0;if(status)return status;
    const rows=kind===1?live:captured, row=rows[index];if(!row)return 5;
    writeRow(row);return 0;
  };
  fixture.module._melee_web_synctest_cpu_particle_endpoint_row_data=()=>PARTICLE_ROW_PTR;
  fixture.module._melee_web_synctest_cpu_particle_endpoint_row_bytes=()=>96;
  return {header,live,captured};
}

function cellRow(kind,index,{objectKind=0,extent=8,receiptSerial,offset=8}={}){
  return {kind,index,objectKind,extent,receiptSerial,object:PARTICLE_HOST_BASE+offset,
    world:1,epoch:3,allocation:7,pool:PARTICLE_HOST_BASE,descriptor:PARTICLE_DESCRIPTOR,
    source:PARTICLE_SOURCE_BASE+offset,cellOffset:offset,stride:8};
}

async function collectParticle(fixture,view=1,provenance=1,extra={}){
  const raw=await fixture.page.evaluate(collectCpuEndpointInPage,
    {view,provenance,particleCellEndpoint:true,...extra});
  return decodeCpuParticleEndpointResult(toWire(raw));
}

test('one synchronous page callback walks every named domain and copies borrowed bytes before reuse',async()=>{
  const fixture=makeFixture({borrowMutation:true});
  const envelope=await collect(fixture,CPU_ENDPOINT_VIEW.pre,CPU_ENDPOINT_PROVENANCE.sharedPre,{expectedSourceFrame:300});
  assert.equal(envelope.domains.length,9);
  assert.equal(envelope.sourceFrame,'300');
  assert.equal(envelope.domains.reduce((n,item)=>n+item.records.length,0),fixture.expectedRows);
  assert.equal(fixture.calls[0][0],'begin');
  assert.deepEqual(fixture.calls.at(-1),['end']);
  assert.equal(fixture.calls.filter(call=>call[0]==='record').length,fixture.expectedRows);
  assert.equal(envelope.domains[0].records[0].fields[0],'9007199254740993');
  const firstBorrow=envelope.domains[2].records.find(item=>item.kindName==='ActionSlot');
  assert.notEqual(firstBorrow.bytesBase64,Buffer.alloc(firstBorrow.borrowedBytes,0xee).toString('base64'));
  assert.equal(firstBorrow.bytesBase64,Buffer.from([3*31+2*7,3*31+2*7+1,3*31+2*7+2]).toString('base64'));
  assert.equal(envelope.domains[7].records.find(item=>item.kindName==='AudioOwnedRange').fields[0],'1',
    'the historical one-row audio fixture remains supported');
});

test('native-shaped six/nine/77 audio inventories preserve distinct FX ownership',async()=>{
  for(const count of [6,9,77]){
    const fixture=makeFixture({audioRangeCount:count});
    const raw=await fixture.page.evaluate(collectCpuEndpointInPage,{view:1,provenance:1});
    const endpoint=decodeCpuEndpointResult(toWire(raw));
    const audio=endpoint.domains[7];
    assert.equal(audio.records.filter(item=>item.kindName==='AudioOwnedRange').length,count);
    const ranges=audio.records.filter(item=>item.kindName==='AudioOwnedRange');
    assert.deepEqual(ranges.map(item=>item.fields[0]),makeAudioRangeKinds(count).map(String));
    for(const row of ranges.slice(0,6)){
      assert.equal(row.fields[3],'25000');assert.equal(row.fields[4],'3');
    }
    if(count>=9){
      assert.equal(ranges[6].fields[1],'27000');
      for(const row of ranges.slice(6,9)){
        assert.equal(row.fields[3],'27000');assert.equal(row.fields[4],'13');
      }
    }
  }
});

test('FX owner proof follows actual source and rejects mismatched FX metadata in page and offline',async()=>{
  const source=readFileSync(new URL('../../gameplay_audio_fx.c',import.meta.url),'utf8');
  assert.match(source,/MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER,effects,sizeof\(\*effects\),effects,effects->generation/);
  assert.match(source,/MELEE_WEB_AUDIO_RANGE_EFFECTS_REVERB_HEAP,effects->reverb_heap,REVERB_HEAP_BYTES,effects,effects->generation/);
  assert.match(source,/MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP,effects->delay_heap,DELAY_HEAP_BYTES,effects,effects->generation/);
  for(const count of [9,77]){
    const good=await collect(makeFixture({audioRangeCount:count}));
    for(const [index,field,value,pattern] of [
      [7,3,27001n,/effects ranges disagree on owner identity or generation/],
      [8,3,27001n,/effects ranges disagree on owner identity or generation/],
      [7,4,14n,/effects ranges disagree on owner identity or generation/],
      [8,4,14n,/effects ranges disagree on owner identity or generation/],
      [6,1,27001n,/effects owner range address differs from its owner identity/],
    ]){
      const fixture=makeFixture({audioRangeCount:count});
      updateRawRecord(fixture,keyOf(8,1,index),record=>record.fields[field]=value);
      await assert.rejects(()=>collect(fixture),pattern);
      assert.equal(fixture.calls.at(-1)[0],'end');
      const corrupted=toWire(good);
      corrupted.domains[7].records.filter(row=>row.kindName==='AudioOwnedRange')[index].fields[field]=String(value);
      assert.throws(()=>decodeCpuEndpointResult(corrupted),pattern);
    }
  }
  const reference=await collect(makeFixture({view:2,provenance:2,audioRangeCount:9}),2,2);
  const corrected=await collect(makeFixture({view:2,provenance:3,audioRangeCount:9}),2,3);
  assert.equal(compareCpuEndpoints(reference,corrected).equal,true);
  for(const row of corrected.domains[7].records.filter(row=>row.kindName==='AudioOwnedRange').slice(6,9))
    row.fields[4]='14';
  assert.throws(()=>compareCpuEndpoints(reference,corrected),/metadata\[Generation\]/,
    'a valid distinct FX generation stays exact between reference and corrected endpoints');
});

test('audio endpoint validates the authored HPS kind/index set through the 77-row limit',async()=>{
  const fixture=makeFixture({audioRangeCount:CPU_ENDPOINT_AUDIO_RANGE.maxOwnedRanges});
  const endpoint=await collect(fixture);
  const ranges=endpoint.domains[7].records.filter(item=>item.kindName==='AudioOwnedRange');
  assert.equal(ranges.length,77);
  assert.equal(ranges.filter(row=>BigInt(row.fields[0])>=BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamBase)).length,68);
  assert.equal(ranges.at(-1).fields[0],String(CPU_ENDPOINT_AUDIO_RANGE.streamBase+
    CPU_ENDPOINT_AUDIO_RANGE.streamKinds.RELAY_BYTES*CPU_ENDPOINT_AUDIO_RANGE.streamKindStride));

  const reference=await collect(makeFixture({view:2,provenance:2,audioRangeCount:77}),2,2);
  const corrected=await collect(makeFixture({view:2,provenance:3,audioRangeCount:77}),2,3);
  assert.equal(compareCpuEndpoints(reference,corrected).equal,true);
  const changedAddress=structuredClone(corrected);
  const streamRange=changedAddress.domains[7].records.find(row=>row.kindName==='AudioOwnedRange'&&
    BigInt(row.fields[0])>=BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamBase));
  streamRange.fields[1]=(BigInt(streamRange.fields[1])+1n).toString(10);
  assert.throws(()=>compareCpuEndpoints(reference,changedAddress),/Audio\.AudioOwnedRange\[9\]\.metadata\[Address\]/,
    'stream owner addresses remain exact across the endpoint comparison');

  const missingPcm=await collect(makeFixture({audioRangeCount:71,omitPcmBytes:true}));
  assert.equal(missingPcm.domains[7].records.filter(row=>row.kindName==='AudioOwnedRange').length,71,
    'the leaf may omit authored PCM rows for absent channels');
  await assert.rejects(()=>collect(makeFixture({audioRangeCount:76})),/incomplete 23 row set/,
    'all non-PCM typed rows remain required');
});

test('audio endpoint refuses unknown HPS kinds, malformed indexes, and counts beyond the leaf bound',async()=>{
  for(const [label,kind,pattern] of [
    ['unknown stream kind',CPU_ENDPOINT_AUDIO_RANGE.streamBase+24*8,/kind\/index is not authored/],
    ['scalar index',CPU_ENDPOINT_AUDIO_RANGE.streamBase+
      CPU_ENDPOINT_AUDIO_RANGE.streamKinds.SELECTED_FILE*8+1,/kind\/index is not authored/],
    ['channel index',CPU_ENDPOINT_AUDIO_RANGE.streamBase+
      CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_PCM*8+6,/kind\/index is not authored/],
  ]){
    const fixture=makeFixture({audioRangeCount:10});
    updateRawRecord(fixture,keyOf(8,1,9),record=>{
      record.fields[0]=BigInt(kind);record.fields[1]=32000n;
      record.fields[3]=25000n;record.fields[4]=4n;
    });
    await assert.rejects(()=>collect(fixture),pattern,label);
    assert.equal(fixture.calls.at(-1)[0],'end',`${label} closes the endpoint batch`);
  }
  const overflow=makeFixture({audioRangeCount:78});
  await assert.rejects(()=>collect(overflow),/audio owned-range count is outside its authored bound/);
  assert.equal(overflow.calls.some(call=>call[0]==='record'&&call[1]===8&&call[2]===1),false,
    'a declared over-cap count is refused before any audio range is read');
  assert.equal(overflow.calls.at(-1)[0],'end');
  const badSpan=makeFixture();
  updateRawRecord(badSpan,keyOf(8,1,0),record=>record.fields[1]=0xffff_fffen);
  await assert.rejects(()=>collect(badSpan),/audio owned-range address, extent, owner, or generation is invalid/);
  const partialLegacy=makeFixture({audioRangeCount:2});
  await assert.rejects(()=>collect(partialLegacy),/audio stream snapshot is missing base audio range kind 3/);
  const splitStreamOwner=makeFixture({audioRangeCount:77});
  updateRawRecord(splitStreamOwner,keyOf(8,1,10),record=>record.fields[3]=25001n);
  await assert.rejects(()=>collect(splitStreamOwner),/stream ranges disagree on owner identity or generation/);
  const zeroSpan=makeFixture({audioRangeCount:77});
  updateRawRecord(zeroSpan,keyOf(8,1,62),record=>{
    record.fields[2]=0n;record.borrowedAddress=0;record.borrowedBytes=0;
  });
  await assert.rejects(()=>collect(zeroSpan),/address, extent, owner, or generation is invalid/,
    'an absent PCM buffer is omitted by the leaf rather than represented by a zero extent');
});

test('audio host bounds follow the source enums while the historical census remains nine rows',()=>{
  const audioHeader=readFileSync(new URL('../../gameplay_audio.h',import.meta.url),'utf8');
  const streamHeader=readFileSync(new URL('../../gameplay_audio_stream.h',import.meta.url),'utf8');
  const legacy=parseCEnum(audioHeader,'MeleeWebAudioOwnedRangeKind');
  assert.deepEqual([...legacy],[
    ['MELEE_WEB_AUDIO_RANGE_AUDIO_OWNER',1],['MELEE_WEB_AUDIO_RANGE_AUDIO_ENTRIES',2],
    ['MELEE_WEB_AUDIO_RANGE_AUDIO_BINDINGS',3],['MELEE_WEB_AUDIO_RANGE_AUDIO_PROGRAMS',4],
    ['MELEE_WEB_AUDIO_RANGE_AUDIO_WORDS',5],['MELEE_WEB_AUDIO_RANGE_AUDIO_STARTS',6],
    ['MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER',7],['MELEE_WEB_AUDIO_RANGE_EFFECTS_REVERB_HEAP',8],
    ['MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP',9],['MELEE_WEB_AUDIO_RANGE_STREAM_BASE',0x100],
  ]);
  const stream=parseCEnum(streamHeader,'MeleeWebAudioStreamRangeKind');
  assert.deepEqual(Object.entries(CPU_ENDPOINT_AUDIO_RANGE.streamKinds).map(([name,value])=>
    [`MELEE_WEB_AUDIO_STREAM_RANGE_${name}`,value]),[...stream]);
  const streamMax=Number(audioHeader.match(/#define MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX (\d+)u/)?.[1]);
  assert.equal(streamMax,CPU_ENDPOINT_AUDIO_RANGE.maxStreamRanges);
  assert.equal(9+streamMax,CPU_ENDPOINT_AUDIO_RANGE.maxOwnedRanges);
  assert.match(audioHeader,/MELEE_WEB_AUDIO_OWNED_RANGE_MAX\s*=\s*9\s*\+\s*MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX/);

  const census=readFileSync(new URL('./synctest_browser.cpp',import.meta.url),'utf8');
  assert.match(census,/constexpr uint32_t kAudioRangeWireCapacity = 9;/);
  assert.match(census,/AudioRangeWireRow rows\[kAudioRangeWireCapacity\]/);
  assert.match(census,/AudioRangeWireRow rows\[kAudioRangeWireCapacity\]\{\}/);
  assert.match(census,/sizeof\(AudioRangeWireInventory\) == 252/);
  assert.match(census,/audio_range_wire\.capacity = kAudioRangeWireCapacity/);
  assert.match(census,/melee_web_audio_visit_owned_ranges\(current_audio, expected_generation,\s*kAudioRangeWireCapacity/);
  assert.match(census,/capture\.count != 6 && capture\.count != 9/);
  assert.doesNotMatch(census,/MELEE_WEB_AUDIO_OWNED_RANGE_MAX/);
  const censusRunner=readFileSync(new URL('./run_synctest.mjs',import.meta.url),'utf8');
  assert.match(censusRunner,/wireVersion = 1, wireHeaderBytes = 36, wireRowBytes = 24, wireCapacity = 9/);
  assert.match(censusRunner,/\(count !== 6 && count !== 9\)/);
});

test('host comparison is strict for metadata and borrowed bytes while ignoring only transport addresses',async()=>{
  const leftFixture=makeFixture({view:2,provenance:2,baseAddress:32768});
  const rightFixture=makeFixture({view:2,provenance:3,baseAddress:40000});
  const left=await collect(leftFixture,2,2), right=await collect(rightFixture,2,3);
  assert.deepEqual(compareCpuEndpoints(left,right),{equal:true,view:2,sourceFrame:'300',domains:9});
  const metadata=structuredClone(right);
  const cache=metadata.domains[2].records.find(item=>item.kindName==='ActionCacheEntry'&&item.index===1);
  cache.fields[2]=(BigInt(cache.fields[2])+1n).toString();
  assert.throws(()=>compareCpuEndpoints(left,metadata),/Actions\.ActionCacheEntry\[1\]\.metadata\[Next\]/);
  const bytes=structuredClone(right);
  const payload=bytes.domains[3].records.find(item=>item.kindName==='PayloadRow');
  const changed=Buffer.from(payload.bytesBase64,'base64'); changed[1]^=0xff; payload.bytesBase64=changed.toString('base64');
  assert.throws(()=>compareCpuEndpoints(left,bytes),/Payload\.PayloadRow\[0\]\.bytes first differ at offset 1/);
});

test('collector closes a begun batch on malformed, truncated, overflowed, and refused rows',async()=>{
  for(const [label,alter,pattern] of [
    ['schema',(f)=>updateRawRecord(f,keyOf(1,1,0),r=>r.schema=2),/record metadata mismatch/],
    ['row',(f)=>updateRawRecord(f,keyOf(1,1,0),r=>r.index=2),/record metadata mismatch/],
    ['count-overflow',(f)=>updateRawRecord(f,keyOf(3,0,0),r=>r.fields[1]=65537n),/bounded endpoint count/],
    ['refusal',(f)=>updateRawRecord(f,keyOf(7,1,0),r=>{r.status=CPU_ENDPOINT_STATUS.leafRefused;r.schema=1;r.view=0;r.provenance=0;r.domain=0;r.kind=0;r.index=0;r.count=0;r.metadataBytes=0;r.flags=0;r.fields=Array(32).fill(0n);r.borrowedAddress=0;r.borrowedBytes=0;r.reserved=0;}),/getter refused Bootstrap\.BootstrapTicks/],
  ]){
    const fixture=makeFixture();alter(fixture);
    await assert.rejects(()=>collect(fixture),pattern,label);
    assert.equal(fixture.calls.at(-1)[0],'end',`${label} must revalidate/close`);
  }
  const truncated=makeFixture();truncated.module._melee_web_synctest_cpu_endpoint_bytes=()=>311;
  await assert.rejects(()=>collect(truncated),/extent is not exactly 312/);
  assert.equal(truncated.calls.at(-1)[0],'end');
  const bothFailures=makeFixture();
  updateRawRecord(bothFailures,keyOf(1,1,0),r=>r.schema=2);
  bothFailures.module._melee_web_synctest_cpu_endpoint_end=()=>{bothFailures.calls.push(['end']);return 0x53c;};
  await assert.rejects(()=>collect(bothFailures),error=>{
    assert.match(error.firstFailure,/record metadata mismatch/);
    assert.match(error.endFailure,/end\/revalidation refused with status 1340/);
    assert.match(String(error),/first failure:.*end failure:/);
    return true;
  });
  assert.equal(bothFailures.calls.at(-1)[0],'end');
});

test('request stamps, runtime modes, and heap identity fail closed before accepting a partial endpoint',async()=>{
  const hiddenMemory=makeFixture();
  Object.defineProperty(hiddenMemory.module,'wasmMemory',{configurable:true,get(){throw new Error('wasmMemory was not exported');}});
  for(const name of ['PThread','ABORT','calledRun','Asyncify'])
    Object.defineProperty(hiddenMemory.module,name,{configurable:true,get(){throw new Error(`${name} was not exported`);}});
  const hiddenMemoryEndpoint=await collect(hiddenMemory);
  assert.equal(hiddenMemoryEndpoint.domains.length,9);
  const badStamp=makeFixture();
  await assert.rejects(()=>collect(badStamp,1,1,{expectedSourceFrame:301}),/source-frame stamp differs/);
  assert.equal(badStamp.calls.at(-1)[0],'end');
  const shared=makeFixture();shared.module.HEAPU8=new Uint8Array(new SharedArrayBuffer(HEAP_BYTES));shared.module.wasmMemory={buffer:shared.module.HEAPU8.buffer};
  await assert.rejects(()=>collect(shared),/shared\/threaded Wasm memory/);
  assert.equal(shared.calls.length,0);
  const asyncified=makeFixture();asyncified.module.Asyncify={State:{Normal:0},state:1,currData:{}};
  await assert.rejects(()=>collect(asyncified),/non-idle Asyncify/);
  assert.equal(asyncified.calls.length,0);
  for(const [name,value,pattern] of [
    ['PThread',{},/threaded Emscripten runtime/],
    ['ABORT',true,/aborted runtime/],
    ['calledRun',false,/uninitialized runtime/],
    ['Asyncify',{State:{Normal:0},state:1,currData:{}},/non-idle Asyncify/],
  ]){
    const exposed=makeFixture();exposed.module[name]=value;
    await assert.rejects(()=>collect(exposed),pattern,`${name} data property remains checked`);
    assert.equal(exposed.calls.length,0,`${name} refusal happens before the native batch`);
  }
  const changedHeap=makeFixture();
  const original=changedHeap.module._melee_web_synctest_cpu_endpoint_record;
  changedHeap.module._melee_web_synctest_cpu_endpoint_record=(...args)=>{
    const result=original(...args);changedHeap.module.HEAPU8=new Uint8Array(HEAP_BYTES);return result;
  };
  await assert.rejects(()=>collect(changedHeap),error=>{
    assert.match(String(error),/heap identity or extent changed/);
    assert.match(String(error.endFailure),/heap identity or extent changed/);
    return true;
  });
  assert.equal(changedHeap.calls.at(-1)[0],'end');
  const beginChangedHeap=makeFixture();
  const originalBegin=beginChangedHeap.module._melee_web_synctest_cpu_endpoint_begin;
  beginChangedHeap.module._melee_web_synctest_cpu_endpoint_begin=(...args)=>{
    const result=originalBegin(...args);beginChangedHeap.module.HEAPU8=new Uint8Array(HEAP_BYTES);return result;
  };
  await assert.rejects(()=>collect(beginChangedHeap),error=>{
    assert.match(String(error.firstFailure),/heap identity or extent changed/);
    assert.match(String(error.endFailure),/heap identity or extent changed/);
    return true;
  });
  assert.deepEqual(beginChangedHeap.calls[0],['begin',1,1]);
  assert.deepEqual(beginChangedHeap.calls.at(-1),['end']);
  assert.equal(beginChangedHeap.calls.filter(call=>call[0]==='record').length,0);
});

test('host decoder rejects missing, reordered, malformed, and oversized endpoint data',async()=>{
  const valid=await collect(makeFixture());
  const missing=structuredClone(valid);missing.domains.pop();
  assert.throws(()=>decodeCpuEndpointResult(missing),/envelope header\/schema/);
  const missingRow=structuredClone(valid);missingRow.domains[2].records.pop();
  assert.throws(()=>decodeCpuEndpointResult(missingRow),/row count\/order is incomplete/);
  const reordered=structuredClone(valid);[reordered.domains[1],reordered.domains[2]]=[reordered.domains[2],reordered.domains[1]];
  assert.throws(()=>decodeCpuEndpointResult(reordered),/missing or out of order/);
  const wrongName=structuredClone(valid);wrongName.domains[0].records[0].fieldNames[0]='Guess';
  assert.throws(()=>decodeCpuEndpointResult(wrongName),/row sequence mismatch/);
  const badU64=structuredClone(valid);badU64.domains[0].records[0].fields[0]='9007199254740993.0';
  assert.throws(()=>decodeCpuEndpointResult(badU64),/canonical lossless u64/);
  const badBytes=structuredClone(valid);badBytes.domains[0].records[0].bytesBase64='***';
  assert.throws(()=>decodeCpuEndpointResult(badBytes),/canonical base64/);
  const badBorrowExtent=structuredClone(valid);badBorrowExtent.domains[2].records.find(item=>item.kindName==='ActionSlot').borrowedBytes++;
  assert.throws(()=>decodeCpuEndpointResult(badBorrowExtent),/borrowed-byte length mismatch/);
  const countProduct=structuredClone(valid);countProduct.domains[2].records[0].fields[1]='2';countProduct.domains[2].records[0].fields[2]='4294967295';
  assert.throws(()=>decodeCpuEndpointResult(countProduct),/count product overflows/);
});

test('typed particle endpoint keeps schema312 intact and compares local receipt authority by incarnation relation',async()=>{
  const preLeftFixture=makeFixture(),preRightFixture=makeFixture();
  installParticleSidecar(preLeftFixture,{anchorSerial:5,historySerial:5,
    liveRows:[cellRow(1,0,{receiptSerial:4})],capturedRows:[cellRow(2,0,{receiptSerial:4})]});
  installParticleSidecar(preRightFixture,{anchorSerial:19,historySerial:19,
    liveRows:[cellRow(1,0,{receiptSerial:18})],capturedRows:[cellRow(2,0,{receiptSerial:18})]});
  const preLeft=await collectParticle(preLeftFixture),preRight=await collectParticle(preRightFixture);
  assert.equal(preLeft.schema,2);assert.equal(preLeft.rawSchema,1);
  assert.deepEqual(preLeft.particleCells.liveRows[0],{
    kind:1,index:0,objectKind:0,extent:8,receiptSerial:'4',object:String(PARTICLE_HOST_BASE+8),
    world:'1',epoch:'3',allocation:'7',pool:String(PARTICLE_HOST_BASE),descriptor:String(PARTICLE_DESCRIPTOR),
    source:PARTICLE_SOURCE_BASE+8,cellOffset:8,stride:8});
  assert.deepEqual(compareCpuParticleEndpoints(preLeft,preRight),{
    equal:true,view:1,sourceFrame:'300',domains:9,particleCells:{live:1,captured:1,incarnationRelations:1}});
  const rawLeft={schema:1,recordBytes:preLeft.recordBytes,view:preLeft.view,provenance:preLeft.provenance,
    sourceFrame:preLeft.sourceFrame,revalidated:preLeft.revalidated,domains:preLeft.domains};
  const rawRight={...rawLeft,domains:preRight.domains};
  assert.throws(()=>compareCpuEndpoints(rawLeft,rawRight),/ParticleAnchor\[0\]\.metadata\[Serial\] differs/,
    'historical schema312 comparator remains strict for the raw serial');
  const changedToken=structuredClone(preRight),tokenRecord=changedToken.domains[1].records.find(row=>row.kindName==='ParticleAnchor');
  tokenRecord.fields[tokenRecord.fieldNames.indexOf('AnchorToken')]='4';
  changedToken.particleCells.anchorToken='4';
  assert.throws(()=>compareCpuParticleEndpoints(preLeft,changedToken),/metadata\[AnchorToken\] differs/,
    'AnchorToken remains cross-instance strict');

  const leftFixture=makeFixture({view:2,provenance:2}),rightFixture=makeFixture({view:2,provenance:3});
  installParticleSidecar(leftFixture,{anchorSerial:5,historySerial:6,
    liveRows:[cellRow(1,0,{receiptSerial:6,offset:16}),cellRow(1,1,{receiptSerial:4,offset:8})],
    capturedRows:[cellRow(2,0,{receiptSerial:4,offset:8})]});
  installParticleSidecar(rightFixture,{anchorSerial:19,historySerial:20,
    liveRows:[cellRow(1,0,{receiptSerial:20,offset:16}),cellRow(1,1,{receiptSerial:18,offset:8})],
    capturedRows:[cellRow(2,0,{receiptSerial:18,offset:8})]});
  const left=await collectParticle(leftFixture,2,2),right=await collectParticle(rightFixture,2,3);
  assert.deepEqual(validateCpuParticleEndpointAgainstPre(left,preLeft),{equal:true,capturedRows:1});
  assert.deepEqual(validateCpuParticleEndpointAgainstPre(right,preRight),{equal:true,capturedRows:1});
  const comparison=compareCpuParticleEndpoints(left,right,{preLeft,preRight});
  assert.equal(comparison.equal,true);
  assert.equal(comparison.particleCells.incarnationRelations,2);
  const differentIncarnation=structuredClone(right);
  differentIncarnation.particleCells.liveRows[1].receiptSerial='17';
  assert.throws(()=>compareCpuParticleEndpoints(left,differentIncarnation,{preLeft,preRight}),
    /live\/captured particle incarnation relation differs/,
    'same backing address with a different constructor receipt must not masquerade as captured');
  const shifted=structuredClone(left);
  shifted.particleCells.liveRows[1].index=0;
  assert.throws(()=>decodeCpuParticleEndpointResult(shifted),/row ordering|typed row metadata/);
});

test('particle sidecar is synchronous, exact, joined to payload generations, and closes on proof refusal',async()=>{
  const fixture=makeFixture();installParticleSidecar(fixture);
  const endpoint=await collectParticle(fixture);
  const begin=fixture.calls.findIndex(call=>call[0]==='begin'),firstHeader=fixture.calls.findIndex(call=>call[0]==='particle-header'),
    lastHeader=fixture.calls.map(call=>call[0]).lastIndexOf('particle-header'),end=fixture.calls.findIndex(call=>call[0]==='end');
  assert.ok(begin>=0&&firstHeader>begin&&lastHeader>firstHeader&&end>lastHeader);
  assert.equal(fixture.calls.filter(call=>call[0]==='particle-row').length,2);
  assert.equal(endpoint.particleCells.historySerial,'2');

  const missing=structuredClone(endpoint);missing.particleCells.liveRows.pop();
  assert.throws(()=>decodeCpuParticleEndpointResult(missing),/count\/row extent/);
  const wrongAllocation=structuredClone(endpoint);wrongAllocation.particleCells.liveRows[0].allocation='8';
  wrongAllocation.particleCells.capturedRows[0].allocation='8';
  assert.throws(()=>decodeCpuParticleEndpointResult(wrongAllocation),/payload allocation/);
  const wrongExtent=structuredClone(endpoint);wrongExtent.particleCells.liveRows[0].extent=24;
  assert.throws(()=>decodeCpuParticleEndpointResult(wrongExtent),/extent\/stride/);
  const wrongToken=structuredClone(endpoint);wrongToken.particleCells.anchorToken='4';
  assert.throws(()=>decodeCpuParticleEndpointResult(wrongToken),/authority differs/);

  const changedHeader=makeFixture();installParticleSidecar(changedHeader,{headerAfter:{schema:1,bytes:64,
    liveCount:1,capturedCount:1,world:1,anchorSerial:2,anchorToken:3,historySerial:3}});
  await assert.rejects(()=>collectParticle(changedHeader),/header changed while cell rows were read/);
  assert.equal(changedHeader.calls.at(-1)[0],'end');
  const refusedRow=makeFixture();installParticleSidecar(refusedRow,{rowStatuses:new Map([['1/0',5]])});
  await assert.rejects(()=>collectParticle(refusedRow),/particle row 1\[0\] refused with status 5/);
  assert.equal(refusedRow.calls.at(-1)[0],'end');
});


test('confirmed reference/reference comparison is explicit and preserves strict nine-domain typed proof',async()=>{
  const make=async(view,provenance)=>{
    const fixture=makeFixture({view,provenance});installParticleSidecar(fixture);
    return collectParticle(fixture,view,provenance);
  };
  const preLeft=await make(1,1),preRight=await make(1,1);
  for(const view of [2,3]){
    const left=await make(view,2),right=await make(view,2);
    assert.throws(()=>compareCpuParticleEndpoints(left,right,{preLeft,preRight}),/Reference then Corrected/);
    assert.throws(()=>compareCpuParticleEndpoints(left,right,{preLeft,preRight,allowReferenceReference:true}),
      /Reference then Corrected/);
    assert.equal(compareCpuReferenceParticleEndpoints(left,right,{preLeft,preRight}).domains,9);
    assert.throws(()=>compareCpuReferenceParticleEndpoints(left,right),/requires both captured PRE/);
    const changed=structuredClone(right);
    changed.domains[0].records[0].fields[0]='123';
    assert.throws(()=>compareCpuReferenceParticleEndpoints(left,changed,{preLeft,preRight}));
    const corrected=await make(view,3);
    assert.throws(()=>compareCpuReferenceParticleEndpoints(left,corrected,{preLeft,preRight}),/Reference/);
    assert.equal(compareCpuParticleEndpoints(left,corrected,{preLeft,preRight}).domains,9);
  }
});
