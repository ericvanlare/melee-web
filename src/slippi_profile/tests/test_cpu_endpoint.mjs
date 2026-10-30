// SPDX-License-Identifier: GPL-3.0-or-later
// Mocked endpoint-reader controls. These validate the fixed transport and
// comparator; they are not a gameplay, source-owner, or browser test.
import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {
  CPU_ENDPOINT_LAYOUT, CPU_ENDPOINT_PROVENANCE,
  CPU_ENDPOINT_RECORD_BYTES, CPU_ENDPOINT_STATUS, CPU_ENDPOINT_VIEW,
  collectCpuEndpointInPage, compareCpuEndpoints, decodeCpuEndpointResult,
} from './cpu_endpoint.mjs';

const PTR = 512;
const HEAP_BYTES = 131072;
const toWire = value => JSON.parse(JSON.stringify(value));
const keyOf = (domain, kind, index) => `${domain}/${kind}/${index}`;

function overrides(domain, kind, index) {
  const values = new Map();
  const set = (name, value) => values.set(name, BigInt(value));
  if (kind === 'Header') {
    if (domain === 'Actions') Object.entries({LeafSchema:1,BindingCount:1,SlotsPerBinding:2,CacheCount:1,EventCount:1}).forEach(([n,v])=>set(n,v));
    if (domain === 'Payload') Object.entries({Schema:1,HeapHandle:4,HeapBytes:64000,OwnerEpoch:3,World:2,Watermark:11,TopologyRevision:7,ArenaBegin:4096,ArenaEnd:60000,RowCount:1,FreeCellCount:1,AllocatedCellCount:1,PayloadBytes:3}).forEach(([n,v])=>set(n,v));
    if (domain === 'Flow') Object.entries({Identity:101,Epoch:3,Generation:4,SourceValid:1,Complete:1,OpeningDemo:0,ClockFieldCount:19,ProcessMaskBytes:3,ProfilePhaseBytes:2,CadenceFieldCount:14}).forEach(([n,v])=>set(n,v));
    if (domain === 'Bootstrap') set('RangeCount',2);
    if (domain === 'Audio') Object.entries({Generation:3,OwnedRangeCount:1,CommandBudgetAddress:20000,SoundModeAddress:21000}).forEach(([n,v])=>set(n,v));
    if (domain === 'Statics') Object.entries({TableIdentity:30000,RowCount:1}).forEach(([n,v])=>set(n,v));
  }
  if (domain === 'Owner' && kind === 'Owner') {
    set('Storage', 0x20_0000_0000_0001n); // Above Number's exact integer range.
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
  if (kind === 'AudioOwnedRange') set('Extent',3);
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

function makeFixture({view=1,provenance=1,baseAddress=32768,borrowMutation=false}={}) {
  const heap = new Uint8Array(HEAP_BYTES), rows = new Map();
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
    const values = overrides(domainSpec.name,rowSpec.kindName,index);
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
    rows.set(keyOf(domainSpec.id,rowSpec.kind,index),{record,domainName:domainSpec.name,kindName:rowSpec.kindName});
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
  const calls=[];
  const module={HEAPU8:heap,wasmMemory:{buffer:heap.buffer},calledRun:true,
    _melee_web_synctest_cpu_endpoint_begin:(gotView,gotProvenance)=>{calls.push(['begin',gotView,gotProvenance]);return 0;},
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
    _melee_web_synctest_cpu_endpoint_end:()=>{calls.push(['end']);return 0;},
  };
  const page={evaluate:(callback,request)=>{
    const context=vm.createContext({window:{__slippiSynctest:{module}},Uint8Array,DataView,SharedArrayBuffer,
      atob,btoa,console,performance,URL,TextEncoder,TextDecoder});
    context.request=request;
    return vm.runInContext(`(${callback.toString()})(request)`,context);
  }};
  return {heap,heapView,rows,module,page,calls,expectedRows,get borrowSerial(){return borrowSerial;}};
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
