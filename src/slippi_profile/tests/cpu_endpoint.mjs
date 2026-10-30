// SPDX-License-Identifier: GPL-3.0-or-later
// Private host-side transport for the fixed read-only CPU endpoint. This file
// does not infer gameplay state and has no source-work or restore API.

export const CPU_ENDPOINT_RECORD_BYTES = 312;
export const CPU_ENDPOINT_SCHEMA = 1;
export const CPU_ENDPOINT_VIEW = Object.freeze({pre: 1, cpuEnd: 2, drawEnd: 3});
export const CPU_ENDPOINT_PROVENANCE = Object.freeze({sharedPre: 1, reference: 2, corrected: 3});
export const CPU_ENDPOINT_STATUS = Object.freeze({
  ok: 0, invalid: 1, wrongPhase: 2, ownerRefused: 3, leafRefused: 4,
  invalidIndex: 5, overflow: 6, incomplete: 7,
});
export const CPU_PARTICLE_ENDPOINT_SCHEMA = 2;
export const CPU_PARTICLE_ENDPOINT_HEADER_BYTES = 64;
export const CPU_PARTICLE_ENDPOINT_ROW_BYTES = 96;
export const CPU_ENDPOINT_DOMAIN = Object.freeze({
  owner: 1, effects: 2, actions: 3, payload: 4, flow: 5,
  context: 6, bootstrap: 7, audio: 8, statics: 9,
});
/* The audio leaf still uses schema 312. These limits and enum values mirror
 * gameplay_audio.h and gameplay_audio_stream.h; the row count itself comes
 * from Audio.Header.OwnedRangeCount. The leaf may omit PCM byte rows for
 * absent channels; required structural rows and every present kind/index are
 * checked against the authored stream shape. */
export const CPU_ENDPOINT_AUDIO_RANGE = Object.freeze({
  maxOwnedRanges: 77,
  maxStreamRanges: 68,
  streamBase: 0x100,
  streamKindStride: 8,
  legacyKinds: Object.freeze([1,2,3,4,5,6,7,8,9]),
  streamKinds: Object.freeze({
    SELECTED_FILE:1, HEADERS:2, SLOTS:3, SLOT_FILES:4,
    CHANNEL_PCM:5, CHANNEL_FRAMES:6, CHANNEL_LOOP_PCM:7,
    CHANNEL_LOOP_AND_NIBBLES:8, CHANNEL_COEFFICIENTS:9,
    CHANNEL_PREDICTORS:10, CHANNEL_HISTORIES:11, CHANNEL_LOOPING:12,
    PCM_POINTERS:13, PCM_BYTES:14, PCM_CAPACITIES:15, PREPARED:16,
    VISITED_POINTER:17, VISITED_BYTES:18, PROGRESS:19, AUX_HANDLE:20,
    AUX_BYTES:21, RELAY_HANDLE:22, RELAY_BYTES:23,
  }),
});
const AUDIO_STREAM_CHANNEL_KINDS = new Set([
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_PCM,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_FRAMES,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOP_PCM,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOP_AND_NIBBLES,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_COEFFICIENTS,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_PREDICTORS,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_HISTORIES,
  CPU_ENDPOINT_AUDIO_RANGE.streamKinds.CHANNEL_LOOPING,
]);

function validateAudioOwnedRanges(records, generation, audioOwner, failWith) {
  if (!Array.isArray(records) || records.length < 1 ||
      records.length > CPU_ENDPOINT_AUDIO_RANGE.maxOwnedRanges)
    failWith('audio owned-range count is outside its authored bound');
  if (audioOwner === 0n || audioOwner > BigInt(U32_MAX))
    failWith('audio owner identity is invalid');
  const seen = new Set(), legacyKinds = new Set(), streamRows = new Map();
  let streamOwner = null, streamGeneration = null, effectsOwner = null, effectsGeneration = null;
  for (const record of records) {
    const field = name => {
      const index = record.fieldNames.indexOf(name);
      if (index < 0) failWith(`AudioOwnedRange is missing ${name}`);
      return canonicalU64(record.fields[index], `AudioOwnedRange.${name}`);
    };
    const kind = field('Kind'), address = field('Address'), extent = field('Extent');
    const owner = field('OwnerIdentity'), rowGeneration = field('Generation');
    if (seen.has(kind.toString(10))) failWith('audio owned-range kind is duplicated');
    seen.add(kind.toString(10));
    if (address === 0n || address > BigInt(U32_MAX) || extent === 0n ||
        extent > BigInt(U32_MAX) || address + extent > 0x1_0000_0000n ||
        owner === 0n || owner > BigInt(U32_MAX) || rowGeneration === 0n)
      failWith('audio owned-range address, extent, owner, or generation is invalid');
    if (CPU_ENDPOINT_AUDIO_RANGE.legacyKinds.includes(Number(kind))) {
      legacyKinds.add(Number(kind));
      if (kind <= 6n) {
        if (owner !== audioOwner) failWith('base audio owned-range owner differs from the CPU owner row');
        if (rowGeneration !== generation)
          failWith('base audio owned-range generation differs from its header');
      } else {
        if (effectsOwner === null) { effectsOwner=owner;effectsGeneration=rowGeneration; }
        else if (effectsOwner!==owner || effectsGeneration!==rowGeneration)
          failWith('audio effects ranges disagree on owner identity or generation');
        if (kind === 7n && address !== owner)
          failWith('audio effects owner range address differs from its owner identity');
      }
      continue;
    }
    const encoded = kind - BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamBase);
    const streamKind = Number(encoded / BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamKindStride));
    const index = Number(encoded % BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamKindStride));
    const knownKinds = Object.values(CPU_ENDPOINT_AUDIO_RANGE.streamKinds);
    const allowedIndex = AUDIO_STREAM_CHANNEL_KINDS.has(streamKind) ||
      streamKind === CPU_ENDPOINT_AUDIO_RANGE.streamKinds.PCM_BYTES
      ? index < 6 : index === 0;
    if (kind < BigInt(CPU_ENDPOINT_AUDIO_RANGE.streamBase) ||
        !knownKinds.includes(streamKind) || !allowedIndex)
      failWith(`audio stream range kind/index is not authored (${kind})`);
    if (streamOwner === null) { streamOwner=owner;streamGeneration=rowGeneration; }
    else if (streamOwner!==owner || streamGeneration!==rowGeneration)
      failWith('audio stream ranges disagree on owner identity or generation');
    if (!streamRows.has(streamKind)) streamRows.set(streamKind,new Set());
    streamRows.get(streamKind).add(index);
  }
  if (legacyKinds.size > 1) {
    for (let kind=1;kind<=6;kind++) if(!legacyKinds.has(kind))
      failWith(`audio stream snapshot is missing base audio range kind ${kind}`);
    if (legacyKinds.size!==6 && legacyKinds.size!==9)
      failWith('audio base/effects range set is incomplete');
    for (let kind=7;kind<=9;kind++) if(legacyKinds.has(kind)!==(legacyKinds.size===9))
      failWith('audio effects range set is incomplete');
  }
  if (streamRows.size > 0) {
    if (legacyKinds.size!==6 && legacyKinds.size!==9)
      failWith('audio base/effects range set is incomplete');
    for (const kind of Object.values(CPU_ENDPOINT_AUDIO_RANGE.streamKinds)) {
      const actual=streamRows.get(kind);
      if (kind===CPU_ENDPOINT_AUDIO_RANGE.streamKinds.PCM_BYTES) continue;
      const expected=AUDIO_STREAM_CHANNEL_KINDS.has(kind)?[0,1,2,3,4,5]:[0];
      if (!actual || actual.size!==expected.length || expected.some(index=>!actual.has(index)))
        failWith(`audio stream snapshot has an incomplete ${kind} row set`);
    }
    const streamCount=[...streamRows.values()].reduce((total,indexes)=>total+indexes.size,0);
    const pcmBytesCount=streamRows.get(CPU_ENDPOINT_AUDIO_RANGE.streamKinds.PCM_BYTES)?.size??0;
    if (streamCount!==62+pcmBytesCount||streamCount>CPU_ENDPOINT_AUDIO_RANGE.maxStreamRanges)
      failWith('audio stream owned-range count is outside its authored row set');
  }
}

const MAX_ENDPOINT_RECORDS = 65_536;
const MAX_ENDPOINT_BORROWED_BYTES = 128 * 1024 * 1024;
const U32_MAX = 0xffff_ffff;

const row = (kind, kindName, fields, borrowField = null) => ({kind, kindName, fields, borrowField});
const domain = (id, name, header, groups) => ({id, name, header, groups});

/* Exact schema-312 mapping. This is deliberately explicit: semantic pointer
 * fields are named metadata; only record.borrowedAddress is transport-only. */
export const CPU_ENDPOINT_LAYOUT = Object.freeze({
  version: CPU_ENDPOINT_SCHEMA,
  recordBytes: CPU_ENDPOINT_RECORD_BYTES,
  domains: Object.freeze([
    domain(1, 'Owner', null, [
      {row: row(1, 'Owner', ['Storage','Context','Flow','Audio','World','ContextEpoch','SourceFrame']), count: 1},
    ]),
    domain(2, 'Effects', null, [
      {row: row(1, 'EffectToken', ['Token']), count: 1},
      {row: row(2, 'LObjAnchor', ['World','Serial','AnchorToken','Count','Reserved']), count: 1},
      {row: row(3, 'TObjAnchor', ['World','Serial','AnchorToken','Count','Reserved']), count: 1},
      {row: row(4, 'ParticleAnchor', ['World','Serial','AnchorToken','ParticleCount','AppsrtCount']), count: 1},
    ]),
    domain(3, 'Actions', row(0, 'Header', ['LeafSchema','BindingCount','SlotsPerBinding','CacheCount','EventCount']), [
      {row: row(1, 'ActionBinding', ['Present','Generation','Fighter','Store','SelectCallback','TransferCallback','Table']), count: 'BindingCount'},
      {row: row(2, 'ActionSlot', ['Binding','Slot','Present','Motion','Offset','ArchiveBytes','SymbolBytes','Fighter','Store','Table','Clip','Tree','RowIdentity','Animation','RowOwner','CommandOwner'], 'SymbolBytes'), count: {field:'BindingCount',factorField:'SlotsPerBinding'}},
      {row: row(3, 'ActionCacheEntry', ['CacheIndex','Entry','Next','Root','Owner','Cache','Runtime','IntervalOwner','HitCallback','ReplayCallback','RecordCallback','Offset','ArchiveBytes','NativeAction','SymbolBytes','Animation'], 'SymbolBytes'), count: {field:'CacheCount',factor:2}},
      {row: row(4, 'ActionEvent', ['Kind','Policy','CacheHit','Motion','Slot','ResultDomain','Offset','ArchiveBytes','SymbolBytes','Cache','Runtime','Row','Animation','Destination','Source','DestinationGeneration','SourceGeneration','SourceTable','RowOwner','Clip','CommandOwner'], 'SymbolBytes'), count: 'EventCount'},
    ]),
    domain(4, 'Payload', row(0, 'Header', ['Schema','HeapHandle','HeapBytes','OwnerEpoch','World','Watermark','TopologyRevision','ArenaBegin','ArenaEnd','RowCount','FreeCellCount','AllocatedCellCount','PayloadBytes']), [
      {row: row(1, 'PayloadRow', ['Index','SourceAddress','RequestedBytes','HostAddress','AllocationGeneration'], 'RequestedBytes'), count:'RowCount'},
      {row: row(2, 'PayloadFreeCell', ['Kind','SourceAddress','CellBytes','Index']), count:'FreeCellCount'},
      {row: row(3, 'PayloadAllocatedCell', ['Kind','SourceAddress','CellBytes','Index']), count:'AllocatedCellCount'},
    ]),
    domain(5, 'Flow', row(0, 'Header', ['Identity','Epoch','Generation','SourceValid','Complete','OpeningDemo','ClockFieldCount','ProcessMaskBytes','ProfilePhaseBytes','CadenceFieldCount']), [
      {row: row(1, 'FlowClockRecord', ['RecordAddress','RecordExtent','Unk0','Unk4','Unk8','UnkC','CallbackX0','CallbackX1','CallbackX2','CallbackX3','CallbackX4_0','CallbackX4_1','Unk10XC','Unk10X20','Unk10X28','Unk10X30','Unk10X34','Unk10X38_0','Unk10X38_1']), count:1},
      {row: row(2, 'FlowProcessMask', ['Address','Extent'], 'Extent'), count:1},
      {row: row(3, 'FlowProfilePhase', ['Address','Extent'], 'Extent'), count: {optional:'ProfilePhaseBytes'}},
      {row: row(4, 'FlowCadenceRecord', ['RecordAddress','RecordExtent','X0_0_X0','X0_0_X8','X0_0_X10','X0_1_X0','X0_1_X8','X0_1_X10','X4','X38','X40','X48','AlarmAddress','AlarmExtent'], 'AlarmExtent'), count:1},
    ]),
    domain(6, 'Context', null, [
      {row: row(1, 'ContextRange', ['Address','Extent','World','Epoch'], 'Extent'), count:1},
    ]),
    domain(7, 'Bootstrap', row(0, 'Header', ['RangeCount']), [
      {row: row(1, 'BootstrapTicks', ['Address','Extent'], 'Extent'), count:1},
      {row: row(2, 'BootstrapDisabledLinks', ['Address','Extent'], 'Extent'), count:1},
    ]),
    domain(8, 'Audio', row(0, 'Header', ['Generation','OwnedRangeCount','CommandBudgetAddress','SoundModeAddress']), [
      {row: row(1, 'AudioOwnedRange', ['Kind','Address','Extent','OwnerIdentity','Generation'], 'Extent'), count:'OwnedRangeCount'},
      {row: row(2, 'AudioCommandBudget', ['Address','Extent'], 'Extent'), count:1},
      {row: row(3, 'AudioSoundMode', ['Address','Extent'], 'Extent'), count:1},
    ]),
    domain(9, 'Statics', row(0, 'Header', ['TableIdentity','RowCount']), [
      {row: row(1, 'StaticRow', ['Index','Address','Extent','Policy','Domain'], 'Extent'), count:'RowCount'},
    ]),
  ]),
});

const fail = message => { throw new Error(`CPU endpoint ${message}`); };

function bytesLike(value, name) {
  if (!(value instanceof Uint8Array)) fail(`${name} must be a Uint8Array`);
  return value;
}

function canonicalU64(value, name) {
  if (typeof value !== 'string' || !/^(0|[1-9][0-9]*)$/.test(value))
    fail(`${name} is not a canonical lossless u64 string`);
  const parsed = BigInt(value);
  if (parsed > 0xffff_ffff_ffff_ffffn) fail(`${name} exceeds u64`);
  return parsed;
}

function encodeBase64(bytes) {
  let binary = '';
  for (let start = 0; start < bytes.length; start += 0x6000)
    binary += String.fromCharCode(...bytes.subarray(start, start + 0x6000));
  return btoa(binary);
}

function decodeBase64(value, name) {
  if (typeof value !== 'string' || value.length % 4 !== 0 ||
      !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    fail(`${name} is not canonical base64`);
  const binary = atob(value), bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; ++i) bytes[i] = binary.charCodeAt(i);
  if (encodeBase64(bytes) !== value) fail(`${name} has a noncanonical base64 encoding`);
  return bytes;
}

/** Decode a copied native record without converting any u64 to Number. */
export function decodeCpuEndpointRecord(input) {
  const bytes = bytesLike(input, 'record');
  if (bytes.byteLength !== CPU_ENDPOINT_RECORD_BYTES) fail(`record extent ${bytes.byteLength}`);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const fields = Array.from({length: 32}, (_, index) => view.getBigUint64(40 + index * 8, true));
  return {
    status: view.getUint32(0, true), schema: view.getUint32(4, true),
    view: view.getUint32(8, true), provenance: view.getUint32(12, true),
    domain: view.getUint32(16, true), kind: view.getUint32(20, true),
    index: view.getUint32(24, true), count: view.getUint32(28, true),
    metadataBytes: view.getUint32(32, true), flags: view.getUint32(36, true),
    fields, borrowedAddress: view.getBigUint64(296, true),
    borrowedBytes: view.getUint32(304, true), reserved: view.getUint32(308, true),
  };
}

/**
 * Pass this ordinary function directly to one page.evaluate call. It performs
 * no awaits, source work, draw work, formatting callback, or non-endpoint
 * native calls. Native begin/end enforce the checked source-owner/runtime gate;
 * JS additionally rejects shared heaps and any exposed non-idle Asyncify state.
 */
export function collectCpuEndpointInPage(request) {
  const failPage = message => { throw new Error(`CPU endpoint ${message}`); };
  const encodePageBase64 = bytes => {
    let binary='';
    for(let start=0;start<bytes.length;start+=0x6000)
      binary+=String.fromCharCode(...bytes.subarray(start,start+0x6000));
    return btoa(binary);
  };
  const wirePageRecord = record => ({kind:record.kind,kindName:record.kindName,index:record.index,
    count:record.count,fieldNames:record.fieldNames,fields:record.fields.map(value=>value.toString(10)),
    metadataBytes:record.metadataBytes,borrowedBytes:record.borrowedBytes,bytesBase64:encodePageBase64(record.copiedBytes)});
  const recordBytes = 312, schema = 1, maxRecords = 65536;
  const particleV2Schema=2, particleHeaderBytes=64, particleRowBytes=96;
  const maxBorrowedBytes = 128 * 1024 * 1024, u32max = 0xffff_ffff;
  const maxAudioOwnedRanges=77,maxAudioStreamRanges=68,audioStreamBase=0x100,audioStreamStride=8;
  const audioLegacyKinds=new Set([1,2,3,4,5,6,7,8,9]);
  const audioStreamKinds=new Set(Array.from({length:23},(_,index)=>index+1));
  const audioChannelKinds=new Set([5,6,7,8,9,10,11,12]);
  const row = (kind, kindName, fields, borrowField = null) => ({kind,kindName,fields,borrowField});
  const domain = (id, name, header, groups) => ({id,name,header,groups});
  // Keep the page callback self-contained for page.evaluate. This is the
  // numeric walk for schema 312; the host decoder independently checks names.
  const layout = [
    domain(1,'Owner',null,[{row:row(1,'Owner',['Storage','Context','Flow','Audio','World','ContextEpoch','SourceFrame']),count:1}]),
    domain(2,'Effects',null,[
      {row:row(1,'EffectToken',['Token']),count:1},
      {row:row(2,'LObjAnchor',['World','Serial','AnchorToken','Count','Reserved']),count:1},
      {row:row(3,'TObjAnchor',['World','Serial','AnchorToken','Count','Reserved']),count:1},
      {row:row(4,'ParticleAnchor',['World','Serial','AnchorToken','ParticleCount','AppsrtCount']),count:1},
    ]),
    domain(3,'Actions',row(0,'Header',['LeafSchema','BindingCount','SlotsPerBinding','CacheCount','EventCount']),[
      {row:row(1,'ActionBinding',['Present','Generation','Fighter','Store','SelectCallback','TransferCallback','Table']),count:'BindingCount'},
      {row:row(2,'ActionSlot',['Binding','Slot','Present','Motion','Offset','ArchiveBytes','SymbolBytes','Fighter','Store','Table','Clip','Tree','RowIdentity','Animation','RowOwner','CommandOwner'],'SymbolBytes'),count:{field:'BindingCount',factorField:'SlotsPerBinding'}},
      {row:row(3,'ActionCacheEntry',['CacheIndex','Entry','Next','Root','Owner','Cache','Runtime','IntervalOwner','HitCallback','ReplayCallback','RecordCallback','Offset','ArchiveBytes','NativeAction','SymbolBytes','Animation'],'SymbolBytes'),count:{field:'CacheCount',factor:2}},
      {row:row(4,'ActionEvent',['Kind','Policy','CacheHit','Motion','Slot','ResultDomain','Offset','ArchiveBytes','SymbolBytes','Cache','Runtime','Row','Animation','Destination','Source','DestinationGeneration','SourceGeneration','SourceTable','RowOwner','Clip','CommandOwner'],'SymbolBytes'),count:'EventCount'},
    ]),
    domain(4,'Payload',row(0,'Header',['Schema','HeapHandle','HeapBytes','OwnerEpoch','World','Watermark','TopologyRevision','ArenaBegin','ArenaEnd','RowCount','FreeCellCount','AllocatedCellCount','PayloadBytes']),[
      {row:row(1,'PayloadRow',['Index','SourceAddress','RequestedBytes','HostAddress','AllocationGeneration'],'RequestedBytes'),count:'RowCount'},
      {row:row(2,'PayloadFreeCell',['Kind','SourceAddress','CellBytes','Index']),count:'FreeCellCount'},
      {row:row(3,'PayloadAllocatedCell',['Kind','SourceAddress','CellBytes','Index']),count:'AllocatedCellCount'},
    ]),
    domain(5,'Flow',row(0,'Header',['Identity','Epoch','Generation','SourceValid','Complete','OpeningDemo','ClockFieldCount','ProcessMaskBytes','ProfilePhaseBytes','CadenceFieldCount']),[
      {row:row(1,'FlowClockRecord',['RecordAddress','RecordExtent','Unk0','Unk4','Unk8','UnkC','CallbackX0','CallbackX1','CallbackX2','CallbackX3','CallbackX4_0','CallbackX4_1','Unk10XC','Unk10X20','Unk10X28','Unk10X30','Unk10X34','Unk10X38_0','Unk10X38_1']),count:1},
      {row:row(2,'FlowProcessMask',['Address','Extent'],'Extent'),count:1},
      {row:row(3,'FlowProfilePhase',['Address','Extent'],'Extent'),count:{optional:'ProfilePhaseBytes'}},
      {row:row(4,'FlowCadenceRecord',['RecordAddress','RecordExtent','X0_0_X0','X0_0_X8','X0_0_X10','X0_1_X0','X0_1_X8','X0_1_X10','X4','X38','X40','X48','AlarmAddress','AlarmExtent'],'AlarmExtent'),count:1},
    ]),
    domain(6,'Context',null,[{row:row(1,'ContextRange',['Address','Extent','World','Epoch'],'Extent'),count:1}]),
    domain(7,'Bootstrap',row(0,'Header',['RangeCount']),[
      {row:row(1,'BootstrapTicks',['Address','Extent'],'Extent'),count:1},
      {row:row(2,'BootstrapDisabledLinks',['Address','Extent'],'Extent'),count:1},
    ]),
    domain(8,'Audio',row(0,'Header',['Generation','OwnedRangeCount','CommandBudgetAddress','SoundModeAddress']),[
      {row:row(1,'AudioOwnedRange',['Kind','Address','Extent','OwnerIdentity','Generation'],'Extent'),count:'OwnedRangeCount'},
      {row:row(2,'AudioCommandBudget',['Address','Extent'],'Extent'),count:1},
      {row:row(3,'AudioSoundMode',['Address','Extent'],'Extent'),count:1},
    ]),
    domain(9,'Statics',row(0,'Header',['TableIdentity','RowCount']),[
      {row:row(1,'StaticRow',['Index','Address','Extent','Policy','Domain'],'Extent'),count:'RowCount'},
    ]),
  ];
  const view = request && request.view, provenance = request && request.provenance;
  if (!request || Object.keys(request).some(key=>!['view','provenance','expectedSourceFrame','particleCellEndpoint'].includes(key)))
    failPage('request contains unsupported fields');
  if (request.particleCellEndpoint !== undefined && typeof request.particleCellEndpoint !== 'boolean')
    failPage('particle-cell endpoint option must be boolean');
  const includeParticleCells = request.particleCellEndpoint === true;
  if (![1,2,3].includes(view) || ![1,2,3].includes(provenance) ||
      ((view === 1) !== (provenance === 1)))
    failPage('view/provenance request is invalid');
  let expectedSourceFrame;
  if (request.expectedSourceFrame !== undefined) {
    const stamp = String(request.expectedSourceFrame);
    if (!/^(0|[1-9][0-9]*)$/.test(stamp) || BigInt(stamp) > 0xffff_ffff_ffff_ffffn)
      failPage('requested source-frame stamp is invalid');
    expectedSourceFrame = BigInt(stamp);
  }
  const module = typeof window === 'object' && window && window.__slippiSynctest &&
    window.__slippiSynctest.module;
  if (!module || !(module.HEAPU8 instanceof Uint8Array)) failPage('native module/HEAPU8 is unavailable');
  const moduleDataValue = name => {
    const descriptor = Object.getOwnPropertyDescriptor(module,name);
    return descriptor && 'value' in descriptor ? descriptor.value : undefined;
  };
  const heap = module.HEAPU8, heapBuffer = heap.buffer, heapLength = heap.byteLength;
  if (!heapBuffer || heapLength <= 0) failPage('Wasm heap is empty');
  const assertHeap = () => {
    if (module.HEAPU8 !== heap || heap.buffer !== heapBuffer || heap.byteLength !== heapLength)
      failPage('Wasm heap identity or extent changed during the endpoint batch');
  };
  const shared = typeof SharedArrayBuffer !== 'undefined' && heapBuffer instanceof SharedArrayBuffer;
  // Emscripten may install throwing getters for runtime properties that were
  // not exported. HEAPU8 and its buffer/extent are the available identity.
  if (shared) failPage('refused shared/threaded Wasm memory');
  if (moduleDataValue('PThread') !== undefined || (typeof PThread !== 'undefined' && PThread !== null))
    failPage('refused a threaded Emscripten runtime');
  if (moduleDataValue('ABORT') === true || (typeof ABORT !== 'undefined' && ABORT === true)) failPage('refused an aborted runtime');
  const calledRun = moduleDataValue('calledRun');
  if (typeof calledRun === 'boolean' && !calledRun) failPage('refused an uninitialized runtime');
  let asyncify;
  if (typeof Asyncify !== 'undefined') asyncify = Asyncify;
  else asyncify = moduleDataValue('Asyncify');
  if (asyncify !== undefined && (!asyncify || typeof asyncify !== 'object' || !asyncify.State ||
      typeof asyncify.State.Normal !== 'number' || asyncify.state !== asyncify.State.Normal || asyncify.currData !== null))
    failPage('refused a non-idle Asyncify/runtime boundary');
  const required = ['_melee_web_synctest_cpu_endpoint_begin','_melee_web_synctest_cpu_endpoint_record',
    '_melee_web_synctest_cpu_endpoint_data','_melee_web_synctest_cpu_endpoint_bytes',
    '_melee_web_synctest_cpu_endpoint_end'];
  if (includeParticleCells) required.push(
    '_melee_web_synctest_cpu_particle_endpoint_header',
    '_melee_web_synctest_cpu_particle_endpoint_header_data',
    '_melee_web_synctest_cpu_particle_endpoint_header_bytes',
    '_melee_web_synctest_cpu_particle_endpoint_row',
    '_melee_web_synctest_cpu_particle_endpoint_row_data',
    '_melee_web_synctest_cpu_particle_endpoint_row_bytes');
  if (required.some(name => typeof module[name] !== 'function')) failPage('native direct endpoint exports are unavailable');
  const wasmPointer = (value, name) => {
    if (!Number.isInteger(value) || value < -0x8000_0000 || value > u32max)
      failPage(`${name} is not a wasm32 pointer`);
    return value >>> 0;
  };
  const statusValue = (value, name) => {
    if (!Number.isInteger(value) || value < 0 || value > u32max)
      failPage(`${name} returned an invalid status`);
    return value;
  };
  const parse = copied => {
    const data = new DataView(copied.buffer, copied.byteOffset, copied.byteLength);
    return {
      status:data.getUint32(0,true), schema:data.getUint32(4,true), view:data.getUint32(8,true),
      provenance:data.getUint32(12,true), domain:data.getUint32(16,true), kind:data.getUint32(20,true),
      index:data.getUint32(24,true), count:data.getUint32(28,true), metadataBytes:data.getUint32(32,true),
      flags:data.getUint32(36,true), fields:Array.from({length:32},(_,i)=>data.getBigUint64(40+i*8,true)),
      borrowedAddress:data.getBigUint64(296,true), borrowedBytes:data.getUint32(304,true),
      reserved:data.getUint32(308,true),
    };
  };
  const refusalIsZero = (row, status) => row.status === status && row.schema === schema && row.view === 0 &&
    row.provenance === 0 && row.domain === 0 && row.kind === 0 && row.index === 0 && row.count === 0 &&
    row.metadataBytes === 0 && row.flags === 0 && row.borrowedAddress === 0n && row.borrowedBytes === 0 &&
    row.reserved === 0 && row.fields.every(field => field === 0n);
  const countValue = (value, name) => {
    if (typeof value !== 'bigint' || value < 0n || value > BigInt(u32max) || value > BigInt(maxRecords))
      failPage(`${name} exceeds the bounded endpoint count`);
    return Number(value);
  };
  const fieldValue = (row, fieldName) => {
    const index = row.fieldNames.indexOf(fieldName);
    if (index < 0) failPage(`native schema has no named field ${fieldName}`);
    return row.fields[index];
  };
  const validateAudioRanges = (rows, generation, audioOwner) => {
    const ranges=rows.filter(item=>item.kindName==='AudioOwnedRange');
    if(ranges.length<1||ranges.length>maxAudioOwnedRanges)
      failPage('audio owned-range count is outside its authored bound');
    if(audioOwner===0n||audioOwner>BigInt(u32max))failPage('audio owner identity is invalid');
    const seen=new Set(),legacyKinds=new Set(),streamRows=new Map();
    let streamOwner=null,streamGeneration=null,effectsOwner=null,effectsGeneration=null;
    for(const item of ranges){
      const value=name=>fieldValue(item,name);
      const kind=value('Kind'),address=value('Address'),extent=value('Extent');
      const owner=value('OwnerIdentity'),rowGeneration=value('Generation');
      if(seen.has(kind.toString(10)))failPage('audio owned-range kind is duplicated');
      seen.add(kind.toString(10));
      if(address===0n||address>BigInt(u32max)||extent===0n||extent>BigInt(u32max)||
         address+extent>0x1_0000_0000n||owner===0n||owner>BigInt(u32max)||rowGeneration===0n)
        failPage('audio owned-range address, extent, owner, or generation is invalid');
      if(audioLegacyKinds.has(Number(kind))){
        legacyKinds.add(Number(kind));
        if(kind<=6n){
          if(owner!==audioOwner)failPage('base audio owned-range owner differs from the CPU owner row');
          if(rowGeneration!==generation)failPage('base audio owned-range generation differs from its header');
        }else{
          if(effectsOwner===null){effectsOwner=owner;effectsGeneration=rowGeneration;}
          else if(effectsOwner!==owner||effectsGeneration!==rowGeneration)
            failPage('audio effects ranges disagree on owner identity or generation');
          if(kind===7n&&address!==owner)
            failPage('audio effects owner range address differs from its owner identity');
        }
        continue;
      }
      const encoded=kind-BigInt(audioStreamBase);
      const streamKind=Number(encoded/BigInt(audioStreamStride));
      const index=Number(encoded%BigInt(audioStreamStride));
      const allowedIndex=audioChannelKinds.has(streamKind)||streamKind===14?index<6:index===0;
      if(kind<BigInt(audioStreamBase)||!audioStreamKinds.has(streamKind)||!allowedIndex)
        failPage(`audio stream range kind/index is not authored (${kind})`);
      if(streamOwner===null){streamOwner=owner;streamGeneration=rowGeneration;}
      else if(streamOwner!==owner||streamGeneration!==rowGeneration)
        failPage('audio stream ranges disagree on owner identity or generation');
      if(!streamRows.has(streamKind))streamRows.set(streamKind,new Set());
      streamRows.get(streamKind).add(index);
    }
    if(legacyKinds.size>1){
      for(let kind=1;kind<=6;kind++)if(!legacyKinds.has(kind))
        failPage(`audio stream snapshot is missing base audio range kind ${kind}`);
      if(legacyKinds.size!==6&&legacyKinds.size!==9)failPage('audio base/effects range set is incomplete');
      for(let kind=7;kind<=9;kind++)if(legacyKinds.has(kind)!==(legacyKinds.size===9))
        failPage('audio effects range set is incomplete');
    }
    if(streamRows.size>0){
      if(legacyKinds.size!==6&&legacyKinds.size!==9)failPage('audio base/effects range set is incomplete');
      for(const streamKind of audioStreamKinds){
        const actual=streamRows.get(streamKind);
        if(streamKind===14)continue;
        const expected=audioChannelKinds.has(streamKind)?[0,1,2,3,4,5]:[0];
        if(!actual||actual.size!==expected.length||expected.some(index=>!actual.has(index)))
          failPage(`audio stream snapshot has an incomplete ${streamKind} row set`);
      }
      const streamCount=[...streamRows.values()].reduce((total,indexes)=>total+indexes.size,0);
      const pcmBytesCount=streamRows.get(14)?.size??0;
      if(streamCount!==62+pcmBytesCount||streamCount>maxAudioStreamRanges)
        failPage('audio stream owned-range count is outside its authored row set');
    }
  };
  const resolveCount = (group, header) => {
    const expression = group.count;
    if (Number.isInteger(expression) && expression >= 0) return countValue(BigInt(expression), group.row.kindName);
    if (typeof expression === 'string') return countValue(fieldValue(header, expression), `${group.row.kindName}.${expression}`);
    if (expression && typeof expression === 'object') {
      if (expression.optional)
        return fieldValue(header, expression.optional) > 0n ? 1 : 0;
      const base = fieldValue(header, expression.field);
      const factor = expression.factorField ? fieldValue(header, expression.factorField) : BigInt(expression.factor || 1);
      if (factor < 0n || (base !== 0n && factor > BigInt(u32max) / base)) failPage(`${group.row.kindName} count multiplication overflow`);
      return countValue(base * factor, group.row.kindName);
    }
    failPage(`unsupported count expression for ${group.row.kindName}`);
  };
  const total = {records:0,borrowedBytes:0,maxRowBytes:Math.min(maxBorrowedBytes,heapLength)};
  const nativeCall = (fn, name, args = []) => {
    let returned;
    try { returned = statusValue(fn(...args), name); }
    catch (error) { throw error; }
    assertHeap();
    return returned;
  };
  let begun = false, firstFailure = null, endFailure = null, captured = null;
  try {
    // Mark ownership immediately after a successful raw begin return, before
    // checking heap identity. If that check fails, finally must still close it.
    const rawBeginStatus = module._melee_web_synctest_cpu_endpoint_begin(view,provenance);
    if (rawBeginStatus === 0) begun = true;
    const beginStatus = statusValue(rawBeginStatus, 'begin');
    assertHeap();
    if (beginStatus !== 0) failPage(`begin refused with status ${beginStatus}`);
    const pointer = wasmPointer(module._melee_web_synctest_cpu_endpoint_data(), 'record buffer');
    assertHeap();
    const extent = module._melee_web_synctest_cpu_endpoint_bytes();
    assertHeap();
    if (extent !== recordBytes || pointer + extent > heapLength) failPage('native record buffer extent is not exactly 312 bytes');

    const readRow = (domainSpec, rowSpec, index, count) => {
      if (++total.records > maxRecords) failPage('endpoint record cap exceeded');
      const callStatus = nativeCall(module._melee_web_synctest_cpu_endpoint_record, 'record',
        [domainSpec.id,rowSpec.kind,index]);
      const copiedRecord = heap.slice(pointer,pointer+recordBytes);
      if (copiedRecord.byteLength !== recordBytes) failPage('native fixed record was truncated');
      const record = parse(copiedRecord);
      if (callStatus !== 0 || record.status !== 0) {
        if (record.status !== 0 && !refusalIsZero(record,record.status)) failPage('native refusal record was not zeroed');
        failPage(`getter refused ${domainSpec.name}.${rowSpec.kindName}[${index}] (export ${callStatus}, record ${record.status})`);
      }
      if (record.schema !== schema || record.view !== view || record.provenance !== provenance ||
          record.domain !== domainSpec.id || record.kind !== rowSpec.kind || record.index !== index ||
          record.count !== count || record.metadataBytes !== rowSpec.fields.length*8 || record.flags !== 0 || record.reserved !== 0)
        failPage(`record metadata mismatch at ${domainSpec.name}.${rowSpec.kindName}[${index}]`);
      if (rowSpec.fields.length > 32 || record.fields.slice(rowSpec.fields.length).some(field => field !== 0n))
        failPage(`unused metadata slot is nonzero at ${domainSpec.name}.${rowSpec.kindName}[${index}]`);
      let copiedBytes = new Uint8Array(0);
      const borrowBytes = record.borrowedBytes;
      const borrowFieldIndex = rowSpec.borrowField === null ? -1 : rowSpec.fields.indexOf(rowSpec.borrowField);
      if (borrowFieldIndex < 0 ? borrowBytes !== 0 : BigInt(borrowBytes) !== record.fields[borrowFieldIndex])
        failPage(`borrowed span does not match ${rowSpec.borrowField || 'zero-byte row'}`);
      if (borrowBytes > total.maxRowBytes || total.borrowedBytes + borrowBytes > maxBorrowedBytes)
        failPage('borrowed-byte cap exceeded');
      if (borrowBytes > 0) {
        if (record.borrowedAddress === 0n || record.borrowedAddress > 0xffff_ffffn)
          failPage('borrowed pointer is null or outside wasm32');
        const start = Number(record.borrowedAddress), end = start + borrowBytes;
        if (!Number.isSafeInteger(end) || end > heapLength) failPage('borrowed span exceeds HEAPU8');
        // Required immediate copy: record storage and borrowed views are native-owned.
        copiedBytes = heap.slice(start,end);
        if (copiedBytes.byteLength !== borrowBytes) failPage('borrowed span was truncated');
        total.borrowedBytes += borrowBytes;
      }
      return {kind:rowSpec.kind,kindName:rowSpec.kindName,index,count,
        fieldNames:rowSpec.fields,fields:record.fields.slice(0,rowSpec.fields.length),
        metadataBytes:record.metadataBytes,borrowedBytes:borrowBytes,copiedBytes};
    };

    const domains = [];
    for (let domainIndex=0; domainIndex<layout.length; ++domainIndex) {
      const domainSpec = layout[domainIndex];
      if (!domainSpec || domainSpec.id !== domainIndex+1 || !Array.isArray(domainSpec.groups))
        failPage('domain layout is not canonical');
      const rows = [], outputDomain = {id:domainSpec.id,name:domainSpec.name,records:rows};
      let header = null;
      if (domainSpec.header) {
        header = readRow(domainSpec,{...domainSpec.header},0,1);
        rows.push(header);
      }
      if(domainSpec.id===8){
        const ownedRangeCount=fieldValue(header,'OwnedRangeCount');
        if(ownedRangeCount<1n||ownedRangeCount>BigInt(maxAudioOwnedRanges))
          failPage('audio owned-range count is outside its authored bound');
      }
      for (const group of domainSpec.groups) {
        const count = resolveCount(group,header);
        for (let index=0; index<count; ++index) rows.push(readRow(domainSpec,group.row,index,count));
      }
      const rowValue = (rowName, fieldName) => {
        const found = rows.find(item=>item.kindName===rowName);
        return found && found.fields[found.fieldNames.indexOf(fieldName)];
      };
      if (domainSpec.id===3 && (rowValue('Header','LeafSchema')!==1n || rowValue('Header','SlotsPerBinding')!==2n))
        failPage('action header leaf schema or two-slot shape is invalid');
      if (domainSpec.id===4) {
        if (rowValue('Header','Schema')!==1n) failPage('payload header schema is invalid');
        const declaredBytes=rowValue('Header','PayloadBytes');
        let copiedPayloadBytes=0n;
        for (const item of rows) if (item.kindName==='PayloadRow')
          copiedPayloadBytes+=item.fields[item.fieldNames.indexOf('RequestedBytes')];
        if (declaredBytes!==copiedPayloadBytes) failPage('payload row byte sum differs from its header');
      }
      if (domainSpec.id===5 && (rowValue('Header','ClockFieldCount')!==19n || rowValue('Header','CadenceFieldCount')!==14n))
        failPage('flow named-field schema count is invalid');
      if (domainSpec.id===7 && rowValue('Header','RangeCount')!==2n)
        failPage('bootstrap range count is invalid');
      if (domainSpec.id===8) {
        const ownerRecord=domains[0]?.records.find(item=>item.kindName==='Owner');
        if(!ownerRecord)failPage('owner audio identity is missing from the endpoint');
        validateAudioRanges(rows,rowValue('Header','Generation'),fieldValue(ownerRecord,'Audio'));
      }
      if (domainSpec.id===3) {
        for (const item of rows) {
          if (item.kindName==='ActionSlot') {
            const index=item.index;
            if (item.fields[0]!==BigInt(Math.floor(index/2)) || item.fields[1]!==BigInt(index%2))
              failPage(`action slot index disagrees at ${index}`);
          } else if (item.kindName==='ActionCacheEntry') {
            const index=item.index;
            if (item.fields[0]!==BigInt(Math.floor(index/2)) || item.fields[1]!==BigInt(index%2))
              failPage(`action cache index disagrees at ${index}`);
          }
        }
      }
      if (domainSpec.id===4) {
        for (const item of rows) {
          if (item.kindName==='PayloadRow' && item.fields[0]!==BigInt(item.index))
            failPage(`payload row index disagrees at ${item.index}`);
          if ((item.kindName==='PayloadFreeCell' || item.kindName==='PayloadAllocatedCell') &&
              (item.fields[0]!==BigInt(item.kind===2?1:2) || item.fields[3]!==BigInt(item.index)))
            failPage(`payload cell kind/index disagrees at ${item.index}`);
        }
      }
      if (domainSpec.id===9) {
        for (const item of rows) if (item.kindName==='StaticRow' && item.fields[0]!==BigInt(item.index))
          failPage(`static row index disagrees at ${item.index}`);
      }
      domains.push(outputDomain);
    }
    if (domains.length !== 9 || domains.some((item,index)=>item.id!==index+1)) failPage('nine-domain walk was incomplete');
    const owner = domains[0].records[0];
    if (!owner || owner.kindName !== 'Owner') failPage('owner source-frame stamp is missing');
    const ownerSourceFrame = owner.fields[owner.fieldNames.indexOf('SourceFrame')];
    if (typeof ownerSourceFrame !== 'bigint') failPage('owner source-frame field is not a u64');
    if (expectedSourceFrame !== undefined && expectedSourceFrame !== ownerSourceFrame)
      failPage('owner source-frame stamp differs from the requested frame');
    let particleCells = null;
    if (includeParticleCells) {
      const readFixedBuffer = (pointerExport, bytesExport, expectedBytes, label) => {
        const pointer = wasmPointer(module[pointerExport](), `${label} buffer`);
        assertHeap();
        const extent = module[bytesExport]();
        assertHeap();
        if (extent !== expectedBytes || pointer + extent > heapLength)
          failPage(`${label} extent is not exactly ${expectedBytes}`);
        const copied = heap.slice(pointer,pointer+extent);
        if (copied.byteLength !== expectedBytes) failPage(`${label} was truncated`);
        return copied;
      };
      const readParticleHeader = () => {
        const status = nativeCall(module._melee_web_synctest_cpu_particle_endpoint_header,
          'particle header');
        if (status !== 0) failPage(`particle header refused with status ${status}`);
        const copied = readFixedBuffer('_melee_web_synctest_cpu_particle_endpoint_header_data',
          '_melee_web_synctest_cpu_particle_endpoint_header_bytes',particleHeaderBytes,
          'particle header');
        const data = new DataView(copied.buffer,copied.byteOffset,copied.byteLength);
        if (data.getUint32(0,true)!==1 || data.getUint32(4,true)!==particleHeaderBytes ||
            data.getUint32(48,true)!==0 || data.getUint32(52,true)!==0 ||
            data.getUint32(56,true)!==0 || data.getUint32(60,true)!==0)
          failPage('particle header schema/extent/reserved fields are invalid');
        return {copied,schema:1,bytes:particleHeaderBytes,
          liveCount:data.getUint32(8,true),capturedCount:data.getUint32(12,true),
          world:data.getBigUint64(16,true),anchorSerial:data.getBigUint64(24,true),
          anchorToken:data.getBigUint64(32,true),historySerial:data.getBigUint64(40,true)};
      };
      const beforeHeader = readParticleHeader();
      if (beforeHeader.liveCount>maxRecords || beforeHeader.capturedCount>maxRecords ||
          beforeHeader.liveCount+beforeHeader.capturedCount>maxRecords)
        failPage('particle cell row count exceeds the bounded endpoint cap');
      const readParticleRows = (rowKind,count) => {
        const result=[];
        for(let index=0;index<count;index++){
          const status=nativeCall(module._melee_web_synctest_cpu_particle_endpoint_row,
            'particle row',[rowKind,index]);
          if(status!==0)failPage(`particle row ${rowKind}[${index}] refused with status ${status}`);
          const copied=readFixedBuffer('_melee_web_synctest_cpu_particle_endpoint_row_data',
            '_melee_web_synctest_cpu_particle_endpoint_row_bytes',particleRowBytes,
            `particle row ${rowKind}[${index}]`);
          const data=new DataView(copied.buffer,copied.byteOffset,copied.byteLength);
          if(data.getUint32(0,true)!==1||data.getUint32(4,true)!==particleRowBytes||
             data.getUint32(8,true)!==rowKind||data.getUint32(12,true)!==index||
             data.getUint32(92,true)!==0)
            failPage(`particle row ${rowKind}[${index}] schema/order/reserved fields are invalid`);
          result.push({kind:rowKind,index,objectKind:data.getUint32(16,true),extent:data.getUint32(20,true),
            receiptSerial:data.getBigUint64(24,true).toString(10),object:data.getBigUint64(32,true).toString(10),
            world:data.getBigUint64(40,true).toString(10),epoch:data.getBigUint64(48,true).toString(10),
            allocation:data.getBigUint64(56,true).toString(10),pool:data.getBigUint64(64,true).toString(10),
            descriptor:data.getBigUint64(72,true).toString(10),source:data.getUint32(80,true),
            cellOffset:data.getUint32(84,true),stride:data.getUint32(88,true)});
        }
        return result;
      };
      const liveRows=readParticleRows(1,beforeHeader.liveCount);
      const capturedRows=readParticleRows(2,beforeHeader.capturedCount);
      const afterHeader=readParticleHeader();
      if(afterHeader.copied.length!==beforeHeader.copied.length ||
         afterHeader.copied.some((byte,index)=>byte!==beforeHeader.copied[index]))
        failPage('particle header changed while cell rows were read');
      const particleAnchor=domains[1].records.find(item=>item.kindName==='ParticleAnchor');
      if(!particleAnchor)failPage('raw Effects.ParticleAnchor row is missing');
      const anchorField=name=>particleAnchor.fields[particleAnchor.fieldNames.indexOf(name)];
      if(beforeHeader.world!==anchorField('World')||beforeHeader.anchorSerial!==anchorField('Serial')||
         beforeHeader.anchorToken!==anchorField('AnchorToken'))
        failPage('particle sidecar authority differs from the raw particle anchor');
      const capturedKinds={particle:capturedRows.filter(row=>row.objectKind===0).length,
        appsrt:capturedRows.filter(row=>row.objectKind===1).length};
      if(capturedRows.some(row=>row.objectKind!==0&&row.objectKind!==1)||
         BigInt(capturedKinds.particle)!==anchorField('ParticleCount')||
         BigInt(capturedKinds.appsrt)!==anchorField('AppsrtCount'))
        failPage('captured particle row membership differs from the raw anchor counts');
      particleCells={schema:1,headerBytes:particleHeaderBytes,
        rowBytes:particleRowBytes,liveCount:beforeHeader.liveCount,
        capturedCount:beforeHeader.capturedCount,world:beforeHeader.world.toString(10),
        anchorSerial:beforeHeader.anchorSerial.toString(10),anchorToken:beforeHeader.anchorToken.toString(10),
        historySerial:beforeHeader.historySerial.toString(10),liveRows,capturedRows};
    }
    captured = includeParticleCells
      ? {schema:particleV2Schema,rawSchema:schema,recordBytes,view,provenance,
        sourceFrame:ownerSourceFrame,revalidated:true,domains,particleCells}
      : {schema,recordBytes,view,provenance,sourceFrame:ownerSourceFrame,domains};
  } catch (error) {
    firstFailure = error instanceof Error ? error : new Error(String(error));
  } finally {
    if (begun) {
      try {
        const endStatus = nativeCall(module._melee_web_synctest_cpu_endpoint_end,'end');
        if (endStatus !== 0) failPage(`end/revalidation refused with status ${endStatus}`);
      } catch (error) {
        endFailure = error instanceof Error ? error : new Error(String(error));
      }
    }
    try { assertHeap(); }
    catch (error) { if (!endFailure) endFailure = error instanceof Error ? error : new Error(String(error)); }
  }
  if (firstFailure || endFailure) {
    const first = firstFailure ? firstFailure.message : null;
    const end = endFailure ? endFailure.message : null;
    const combined = new Error(first && end ? `first failure: ${first}; end failure: ${end}` : first || end);
    combined.firstFailure = first;
    combined.endFailure = end;
    throw combined;
  }
  if (!captured) failPage('batch produced no endpoint');
  // Encode/serialize only after end() has revalidated and closed the batch.
  const wire={
    schema:captured.schema,...(captured.rawSchema===undefined?{}:{rawSchema:captured.rawSchema}),
    recordBytes:captured.recordBytes,view:captured.view,provenance:captured.provenance,
    sourceFrame:captured.sourceFrame.toString(10),revalidated:true,
    domains:captured.domains.map(item=>({id:item.id,name:item.name,records:item.records.map(wirePageRecord)})),
  };
  if(captured.particleCells)wire.particleCells=captured.particleCells;
  return wire;
}

function compareRecord(left, right, path) {
  const label = `${path}.${left.kindName}[${left.index}]`;
  for (const key of ['kind', 'kindName', 'index', 'count', 'metadataBytes', 'borrowedBytes'])
    if (left[key] !== right[key]) fail(`${label}.${key} differs (${left[key]} vs ${right[key]})`);
  if (left.fieldNames.length !== right.fieldNames.length ||
      left.fieldNames.some((name, index) => name !== right.fieldNames[index]))
    fail(`${label}.fieldNames differ`);
  if (left.fields.length !== right.fields.length) fail(`${label}.metadata field count differs`);
  for (let index = 0; index < left.fields.length; ++index) {
    const a = canonicalU64(left.fields[index], `${label}.${left.fieldNames[index]}`);
    const b = canonicalU64(right.fields[index], `${label}.${right.fieldNames[index]}`);
    if (a !== b) fail(`${label}.metadata[${left.fieldNames[index]}] differs (${a} vs ${b})`);
  }
  const aBytes = decodeBase64(left.bytesBase64, `${label}.bytes`);
  const bBytes = decodeBase64(right.bytesBase64, `${label}.bytes`);
  if (aBytes.length !== left.borrowedBytes || bBytes.length !== right.borrowedBytes)
    fail(`${label}.borrowed byte extent differs from payload`);
  if (aBytes.length !== bBytes.length) fail(`${label}.bytes extent differs`);
  for (let offset = 0; offset < aBytes.length; ++offset)
    if (aBytes[offset] !== bBytes[offset]) fail(`${label}.bytes first differ at offset ${offset}`);
}

/** Strictly compare two fully decoded endpoints under the corrected-route policy. */
export function compareCpuEndpoints(left, right) {
  validateCpuEndpointEnvelope(left);
  validateCpuEndpointEnvelope(right);
  if (left.view !== right.view) fail('comparison views differ');
  const view = left.view;
  if (view === CPU_ENDPOINT_VIEW.pre) {
    if (left.provenance !== CPU_ENDPOINT_PROVENANCE.sharedPre ||
        right.provenance !== CPU_ENDPOINT_PROVENANCE.sharedPre)
      fail('PRE comparison requires SharedPre on both instances');
  } else if (view === CPU_ENDPOINT_VIEW.cpuEnd || view === CPU_ENDPOINT_VIEW.drawEnd) {
    const pair = [left.provenance, right.provenance];
    if (!(pair[0] === CPU_ENDPOINT_PROVENANCE.reference && pair[1] === CPU_ENDPOINT_PROVENANCE.corrected))
      fail('CPU/DRAW comparison requires Reference then Corrected');
  } else fail(`unknown comparison view ${view}`);
  if (left.sourceFrame !== right.sourceFrame) fail('source-frame stamps differ');
  if (left.domains.length !== 9 || right.domains.length !== 9) fail('all nine domains are required');
  for (let domainIndex = 0; domainIndex < 9; ++domainIndex) {
    const a = left.domains[domainIndex], b = right.domains[domainIndex];
    if (a.id !== domainIndex + 1 || b.id !== a.id || a.name !== b.name)
      fail(`domain order differs at ${domainIndex}`);
    if (!Array.isArray(a.records) || !Array.isArray(b.records) || a.records.length !== b.records.length)
      fail(`${a.name} record count differs`);
    for (let row = 0; row < a.records.length; ++row) {
      const record = a.records[row];
      compareRecord(record, b.records[row], a.name);
    }
  }
  return {equal: true, view, sourceFrame: left.sourceFrame, domains: 9};
}

/** Host-side envelope validation shared by production comparison and controls. */
export function validateCpuEndpointEnvelope(value) {
  if (!value || value.schema !== CPU_ENDPOINT_SCHEMA || value.recordBytes !== CPU_ENDPOINT_RECORD_BYTES ||
      value.revalidated !== true || ![1, 2, 3].includes(value.view) ||
      ![1, 2, 3].includes(value.provenance) || !Array.isArray(value.domains) || value.domains.length !== 9)
    fail('envelope header/schema/refinalization invalid');
  if ((value.view === CPU_ENDPOINT_VIEW.pre) !== (value.provenance === CPU_ENDPOINT_PROVENANCE.sharedPre))
    fail('envelope view/provenance combination invalid');
  canonicalU64(value.sourceFrame, 'sourceFrame');
  const exactKeys = (object, keys, name) => {
    if (!object || Object.keys(object).sort().join(',') !== [...keys].sort().join(','))
      fail(`${name} has missing or unrecognized fields`);
  };
  exactKeys(value, ['schema','recordBytes','view','provenance','sourceFrame','revalidated','domains'], 'envelope');
  let rows = 0, borrowed = 0;
  for (let domainIndex = 0; domainIndex < 9; ++domainIndex) {
    const domain = value.domains[domainIndex];
    const domainSpec = CPU_ENDPOINT_LAYOUT.domains[domainIndex];
    exactKeys(domain, ['id','name','records'], `domain ${domainIndex+1}`);
    if (!domain || domain.id !== domainSpec.id || domain.name !== domainSpec.name || !Array.isArray(domain.records))
      fail(`domain ${domainIndex + 1} is missing or out of order`);
    let previous = null;
    for (const record of domain.records) {
      exactKeys(record, ['kind','kindName','index','count','fieldNames','fields','metadataBytes','borrowedBytes','bytesBase64'], `${domain.name} row`);
      if (!record || !Number.isInteger(record.kind) || !Number.isInteger(record.index) ||
          !Number.isInteger(record.count) || record.count <= 0 || record.index < 0 || record.index >= record.count ||
          !Array.isArray(record.fieldNames) || !Array.isArray(record.fields) ||
          record.fieldNames.length !== record.fields.length || record.metadataBytes !== record.fields.length * 8 ||
          !Number.isInteger(record.borrowedBytes) || record.borrowedBytes < 0 || typeof record.bytesBase64 !== 'string')
        fail(`${domain.name} contains an invalid row`);
      if (new Set(record.fieldNames).size !== record.fieldNames.length ||
          record.fieldNames.some(name => typeof name !== 'string' || !name.length))
        fail(`${domain.name} contains invalid field names`);
      record.fields.forEach((field, index) => canonicalU64(field, `${domain.name}.${record.fieldNames[index]}`));
      const bytes = decodeBase64(record.bytesBase64, `${domain.name}.bytes`);
      if (bytes.length !== record.borrowedBytes) fail(`${domain.name} borrowed-byte length mismatch`);
      rows += 1; borrowed += bytes.length;
      if (rows > MAX_ENDPOINT_RECORDS || borrowed > MAX_ENDPOINT_BORROWED_BYTES)
        fail('envelope record/byte bounds exceeded');
      if (previous && (record.kind < previous.kind || (record.kind === previous.kind && record.index <= previous.index)))
        fail(`${domain.name} rows are not in canonical kind/index order`);
      previous = record;
    }

    const header = domainSpec.header ? domain.records[0] : null;
    if (domainSpec.header && (!header || header.kind !== 0 || header.kindName !== 'Header' ||
        header.index !== 0 || header.count !== 1 || header.fieldNames.join('\0') !== domainSpec.header.fields.join('\0')))
      fail(`${domain.name} header is missing or misplaced`);
    const readField = (record, name) => {
      if (!record) fail(`${domain.name}.${name} has no header`);
      const index = record.fieldNames.indexOf(name);
      if (index < 0) fail(`${domain.name} lacks named count field ${name}`);
      return canonicalU64(record.fields[index], `${domain.name}.${name}`);
    };
    if (domain.id === 8) {
      const ownedRangeCount = readField(header, 'OwnedRangeCount');
      if (ownedRangeCount < 1n || ownedRangeCount > BigInt(CPU_ENDPOINT_AUDIO_RANGE.maxOwnedRanges))
        fail('audio owned-range count is outside its authored bound');
    }
    const expected = [];
    if (domainSpec.header) expected.push({row:domainSpec.header,index:0,count:1});
    for (const group of domainSpec.groups) {
      let count;
      if (Number.isInteger(group.count)) count = group.count;
      else if (typeof group.count === 'string') count = Number(readField(header,group.count));
      else if (group.count && typeof group.count === 'object') {
        if (group.count.optional) count = readField(header,group.count.optional) > 0n ? 1 : 0;
        else {
          const base = readField(header,group.count.field);
          const factor = group.count.factorField ? readField(header,group.count.factorField) : BigInt(group.count.factor || 1);
          if (base !== 0n && factor > BigInt(U32_MAX) / base) fail(`${domain.name} count product overflows`);
          count = Number(base * factor);
        }
      } else fail(`${domain.name} has an invalid group count expression`);
      if (!Number.isSafeInteger(count) || count < 0 || count > MAX_ENDPOINT_RECORDS)
        fail(`${domain.name} group count exceeds the endpoint bound`);
      for (let index = 0; index < count; ++index) expected.push({row:group.row,index,count});
    }
    if (domain.records.length !== expected.length) fail(`${domain.name} row count/order is incomplete`);
    for (let index = 0; index < expected.length; ++index) {
      const actual = domain.records[index], want = expected[index];
      if (actual.kind !== want.row.kind || actual.kindName !== want.row.kindName ||
          actual.index !== want.index || actual.count !== want.count ||
          actual.fieldNames.join('\0') !== want.row.fields.join('\0'))
        fail(`${domain.name} row sequence mismatch at ${index}`);
      const borrowedField = want.row.borrowField;
      const expectedBorrowed = borrowedField === null ? 0n : readField(actual,borrowedField);
      if (BigInt(actual.borrowedBytes) !== expectedBorrowed)
        fail(`${domain.name}.${actual.kindName}[${actual.index}] borrowed extent differs from ${borrowedField ?? 'zero'}`);
    }

    const readRowField = (rowName, fieldName) => {
      const rowRecord = domain.records.find(record=>record.kindName===rowName);
      return readField(rowRecord,fieldName);
    };
    if (domain.id === 1) {
      const owner = domain.records[0];
      if (!owner || owner.kindName !== 'Owner' || readRowField('Owner','SourceFrame') !== BigInt(value.sourceFrame))
        fail('owner source-frame stamp differs from envelope');
    } else if (domain.id === 3) {
      if (readRowField('Header','LeafSchema') !== 1n || readRowField('Header','SlotsPerBinding') !== 2n)
        fail('action header schema/two-slot layout is invalid');
      for (const item of domain.records) {
        if (item.kindName === 'ActionSlot' &&
            (BigInt(item.fields[0]) !== BigInt(Math.floor(item.index/2)) || BigInt(item.fields[1]) !== BigInt(item.index%2)))
          fail(`action slot index mismatch at ${item.index}`);
        if (item.kindName === 'ActionCacheEntry' &&
            (BigInt(item.fields[0]) !== BigInt(Math.floor(item.index/2)) || BigInt(item.fields[1]) !== BigInt(item.index%2)))
          fail(`action cache index mismatch at ${item.index}`);
      }
    } else if (domain.id === 4) {
      if (readRowField('Header','Schema') !== 1n) fail('payload header schema is invalid');
      let payloadBytes = 0n;
      for (const item of domain.records) {
        if (item.kindName === 'PayloadRow') {
          if (BigInt(item.fields[0]) !== BigInt(item.index)) fail(`payload row index mismatch at ${item.index}`);
          payloadBytes += BigInt(item.fields[item.fieldNames.indexOf('RequestedBytes')]);
        }
        if ((item.kindName === 'PayloadFreeCell' || item.kindName === 'PayloadAllocatedCell') &&
            (BigInt(item.fields[0]) !== BigInt(item.kind === 2 ? 1 : 2) || BigInt(item.fields[3]) !== BigInt(item.index)))
          fail(`payload cell kind/index mismatch at ${item.index}`);
      }
      if (payloadBytes !== readRowField('Header','PayloadBytes')) fail('payload row byte sum differs from header');
    } else if (domain.id === 5) {
      if (readRowField('Header','ClockFieldCount') !== 19n || readRowField('Header','CadenceFieldCount') !== 14n)
        fail('flow named-field schema count is invalid');
    } else if (domain.id === 7) {
      if (readRowField('Header','RangeCount') !== 2n) fail('bootstrap range count is invalid');
    } else if (domain.id === 8) {
      const ownerRecord=value.domains[0]?.records.find(record=>record.kindName==='Owner');
      if(!ownerRecord)fail('owner audio identity is missing from the endpoint');
      const ownerAudioIndex=ownerRecord.fieldNames.indexOf('Audio');
      if(ownerAudioIndex<0)fail('owner row has no audio identity');
      validateAudioOwnedRanges(domain.records.filter(record=>record.kindName==='AudioOwnedRange'),
        readRowField('Header','Generation'),
        canonicalU64(ownerRecord.fields[ownerAudioIndex],'Owner.Audio'),fail);
    } else if (domain.id === 9) {
      for (const item of domain.records) if (item.kindName === 'StaticRow' && BigInt(item.fields[0]) !== BigInt(item.index))
        fail(`static row index mismatch at ${item.index}`);
    }
  }
  return true;
}

export function decodeCpuEndpointResult(value) {
  validateCpuEndpointEnvelope(value);
  return value;
}

// Historical local name retained for existing focused controls.
export const decodeCpuEndpointEnvelope = decodeCpuEndpointResult;

const PARTICLE_ROW_FIELDS = Object.freeze([
  'kind','index','objectKind','extent','receiptSerial','object','world','epoch',
  'allocation','pool','descriptor','source','cellOffset','stride',
]);
const PARTICLE_SEMANTIC_FIELDS = Object.freeze([
  'objectKind','extent','object','world','epoch','allocation','pool','descriptor',
  'source','cellOffset','stride',
]);
const PARTICLE_POINTER_FIELDS = Object.freeze(['object','pool','descriptor']);

function rawParticleEndpoint(value) {
  return {schema:1,recordBytes:value.recordBytes,view:value.view,provenance:value.provenance,
    sourceFrame:value.sourceFrame,revalidated:value.revalidated,domains:value.domains};
}

function readParticleAnchor(endpoint) {
  const domain=endpoint.domains[1];
  const record=domain?.records.find(item=>item.kindName==='ParticleAnchor');
  if(!record)fail('particle sidecar has no raw ParticleAnchor row');
  const result={};
  for(const name of ['World','Serial','AnchorToken','ParticleCount','AppsrtCount']){
    const index=record.fieldNames.indexOf(name);
    if(index<0)fail(`raw ParticleAnchor is missing ${name}`);
    result[name]=canonicalU64(record.fields[index],`raw ParticleAnchor.${name}`);
  }
  return record;
}

function particleAnchorFields(record) {
  return Object.fromEntries(['World','Serial','AnchorToken','ParticleCount','AppsrtCount'].map(name=>
    [name,canonicalU64(record.fields[record.fieldNames.indexOf(name)],`raw ParticleAnchor.${name}`)]));
}

function sameParticleRow(left,right,{includeReceiptSerial=true,ignoreKind=false,ignoreIndex=false}={}) {
  if((!ignoreKind&&left.kind!==right.kind)||(!ignoreIndex&&left.index!==right.index))return false;
  if(includeReceiptSerial&&left.receiptSerial!==right.receiptSerial)return false;
  return PARTICLE_SEMANTIC_FIELDS.every(name=>left[name]===right[name]);
}

function particleIdentityRelation(endpoint) {
  const capturedBySerial=new Map(endpoint.particleCells.capturedRows.map(row=>[row.receiptSerial,row]));
  const relation=[];
  for(const live of endpoint.particleCells.liveRows){
    const capturedIndex=endpoint.particleCells.capturedRows.findIndex(row=>row.receiptSerial===live.receiptSerial);
    if(capturedIndex>=0&&!sameParticleRow(live,endpoint.particleCells.capturedRows[capturedIndex],
      {includeReceiptSerial:true,ignoreKind:true,ignoreIndex:true}))
      fail(`particle receipt ${live.receiptSerial} names conflicting live/captured identities`);
    relation.push(capturedIndex);
  }
  if(capturedBySerial.size!==endpoint.particleCells.capturedRows.length)
    fail('captured particle receipts are not unique');
  return relation;
}

function validateParticleRows(endpoint, anchor) {
  const cells=endpoint.particleCells;
  if(!cells||cells.schema!==1||cells.headerBytes!==CPU_PARTICLE_ENDPOINT_HEADER_BYTES||
     cells.rowBytes!==CPU_PARTICLE_ENDPOINT_ROW_BYTES||!Number.isInteger(cells.liveCount)||
     !Number.isInteger(cells.capturedCount)||cells.liveCount<0||cells.capturedCount<0||
     cells.liveCount>MAX_ENDPOINT_RECORDS||cells.capturedCount>MAX_ENDPOINT_RECORDS||
     cells.liveCount+cells.capturedCount>MAX_ENDPOINT_RECORDS||
     !Array.isArray(cells.liveRows)||!Array.isArray(cells.capturedRows)||
     cells.liveRows.length!==cells.liveCount||cells.capturedRows.length!==cells.capturedCount)
    fail('particle sidecar header/count/row extent is invalid');
  const world=canonicalU64(cells.world,'particle sidecar world');
  const anchorSerial=canonicalU64(cells.anchorSerial,'particle sidecar anchor serial');
  const anchorToken=canonicalU64(cells.anchorToken,'particle sidecar anchor token');
  const historySerial=canonicalU64(cells.historySerial,'particle sidecar history serial');
  if(world===0n||anchorToken===0n||historySerial<anchorSerial)
    fail('particle sidecar local authority watermark is invalid');
  if(world!==anchor.World||anchorSerial!==anchor.Serial||anchorToken!==anchor.AnchorToken)
    fail('particle sidecar authority differs from raw ParticleAnchor');
  const validateList=(list,kind,label)=>{
    let priorSerial=null;const serials=new Set(),objects=new Set(),counts=[0,0];
    for(let index=0;index<list.length;index++){
      const row=list[index];
      if(!row||Object.keys(row).sort().join(',')!==[...PARTICLE_ROW_FIELDS].sort().join(',')||
         row.kind!==kind||row.index!==index||!Number.isInteger(row.objectKind)||
         (row.objectKind!==0&&row.objectKind!==1)||!Number.isInteger(row.extent)||row.extent<=0||
         !Number.isInteger(row.source)||row.source<=0||row.source>U32_MAX||
         !Number.isInteger(row.cellOffset)||row.cellOffset<0||row.cellOffset>U32_MAX||
         !Number.isInteger(row.stride)||row.stride<=0||row.stride>U32_MAX)
        fail(`${label}[${index}] has invalid typed row metadata`);
      const serial=canonicalU64(row.receiptSerial,`${label}[${index}].receiptSerial`);
      const object=canonicalU64(row.object,`${label}[${index}].object`);
      const rowWorld=canonicalU64(row.world,`${label}[${index}].world`);
      const epoch=canonicalU64(row.epoch,`${label}[${index}].epoch`);
      const allocation=canonicalU64(row.allocation,`${label}[${index}].allocation`);
      const pool=canonicalU64(row.pool,`${label}[${index}].pool`);
      const descriptor=canonicalU64(row.descriptor,`${label}[${index}].descriptor`);
      if(serial===0n||serial>historySerial||(kind===2&&serial>anchorSerial)||
         (priorSerial!==null&&serial>=priorSerial))
        fail(`${label}[${index}] receipt serial is outside its local constructor order`);
      if(rowWorld!==world||epoch===0n||allocation===0n||pool===0n||descriptor===0n||object===0n)
        fail(`${label}[${index}] source identity is incomplete or belongs to another world`);
      for(const field of PARTICLE_POINTER_FIELDS){
        const value=canonicalU64(row[field],`${label}[${index}].${field}`);
        if(value===0n||value>BigInt(U32_MAX))fail(`${label}[${index}].${field} is outside wasm32`);
      }
      if(row.extent!==row.stride||row.cellOffset%row.stride!==0)
        fail(`${label}[${index}] extent/stride/cell alignment is invalid`);
      const sourceCell=BigInt(row.source);
      const hostCell=pool+BigInt(row.cellOffset);
      if(sourceCell<=BigInt(row.cellOffset)||hostCell>BigInt(U32_MAX)||object!==hostCell)
        fail(`${label}[${index}] object/source cell address is inconsistent`);
      const identity=`${row.objectKind}/${row.object}`;
      if(objects.has(identity)||serials.has(serial.toString(10)))
        fail(`${label} contains duplicate cell identity or receipt serial`);
      objects.add(identity);serials.add(serial.toString(10));
      counts[row.objectKind]++;priorSerial=serial;
    }
    return counts;
  };
  const liveKinds=validateList(cells.liveRows,1,'particle live rows');
  const capturedKinds=validateList(cells.capturedRows,2,'particle captured rows');
  if(BigInt(capturedKinds[0])!==anchor.ParticleCount||BigInt(capturedKinds[1])!==anchor.AppsrtCount)
    fail('captured particle membership differs from raw ParticleAnchor counts');
  if(endpoint.view===CPU_ENDPOINT_VIEW.pre){
    if(liveKinds[0]!==capturedKinds[0]||liveKinds[1]!==capturedKinds[1]||
       cells.liveRows.some((row,index)=>!sameParticleRow(row,cells.capturedRows[index],
         {includeReceiptSerial:true,ignoreKind:true})))
      fail('PRE live particle cells do not exactly match the captured anchor membership');
  }
  return {world,anchorSerial,anchorToken,historySerial,liveKinds,capturedKinds};
}

function payloadRows(endpoint) {
  const rows=endpoint.domains[3].records.filter(row=>row.kindName==='PayloadRow');
  return rows.map(row=>({
    source:canonicalU64(row.fields[row.fieldNames.indexOf('SourceAddress')],'PayloadRow.SourceAddress'),
    requested:canonicalU64(row.fields[row.fieldNames.indexOf('RequestedBytes')],'PayloadRow.RequestedBytes'),
    host:canonicalU64(row.fields[row.fieldNames.indexOf('HostAddress')],'PayloadRow.HostAddress'),
    allocation:canonicalU64(row.fields[row.fieldNames.indexOf('AllocationGeneration')],'PayloadRow.AllocationGeneration'),
    bytes:decodeBase64(row.bytesBase64,'PayloadRow.bytes'),
  }));
}

function requireParticlePayloadCoverage(endpoint, rows, label) {
  const payloadHeader=endpoint.domains[3].records.find(row=>row.kindName==='Header');
  if(!payloadHeader)fail(`${label} has no payload header`);
  const payloadWorld=canonicalU64(payloadHeader.fields[payloadHeader.fieldNames.indexOf('World')],
    'Payload.Header.World');
  const payloadEpoch=canonicalU64(payloadHeader.fields[payloadHeader.fieldNames.indexOf('OwnerEpoch')],
    'Payload.Header.OwnerEpoch');
  const payload=payloadRows(endpoint);
  for(const row of rows){
    const offset=BigInt(row.cellOffset),extent=BigInt(row.extent),stride=BigInt(row.stride);
    const source=BigInt(row.source),host=canonicalU64(row.pool,`${label}.pool`);
    const allocation=canonicalU64(row.allocation,`${label}.allocation`),
      world=canonicalU64(row.world,`${label}.world`),epoch=canonicalU64(row.epoch,`${label}.epoch`);
    const matches=payload.filter(item=>item.source+offset===source&&item.host===host&&
      item.allocation===allocation);
    if(matches.length!==1)fail(`${label} does not join exactly one payload allocation by source/host/generation`);
    const match=matches[0];
    if(world!==payloadWorld||epoch!==payloadEpoch||offset%stride!==0n||offset+extent>match.requested||
       BigInt(row.object)!==match.host+offset||match.bytes.length!==Number(match.requested))
      fail(`${label} cell extent escapes its exact payload allocation`);
  }
}

function compareParticleAnchorRecord(left,right,{localSerial=false}={}) {
  const label='Effects.ParticleAnchor[0]';
  for(const key of ['kind','kindName','index','count','metadataBytes','borrowedBytes'])
    if(left[key]!==right[key])fail(`${label}.${key} differs`);
  if(left.fieldNames.join('\0')!==right.fieldNames.join('\0')||left.fields.length!==right.fields.length)
    fail(`${label}.fieldNames differ`);
  for(let index=0;index<left.fields.length;index++){
    const name=left.fieldNames[index];
    const a=canonicalU64(left.fields[index],`${label}.${name}`),
      b=canonicalU64(right.fields[index],`${label}.${name}`);
    if(!(localSerial&&name==='Serial')&&a!==b)
      fail(`${label}.metadata[${name}] differs (${a} vs ${b})`);
  }
  const aBytes=decodeBase64(left.bytesBase64,`${label}.bytes`),
    bBytes=decodeBase64(right.bytesBase64,`${label}.bytes`);
  if(aBytes.length!==bBytes.length||aBytes.some((byte,index)=>byte!==bBytes[index]))
    fail(`${label}.bytes differ`);
}

function compareParticleRows(leftRows,rightRows,label,{includeReceiptSerial=false,ignoreKind=false}={}) {
  if(leftRows.length!==rightRows.length)fail(`${label} membership count differs`);
  for(let index=0;index<leftRows.length;index++){
    const a=leftRows[index],b=rightRows[index];
    if((!ignoreKind&&a.kind!==b.kind)||a.index!==b.index)fail(`${label}[${index}] order differs`);
    if(includeReceiptSerial&&a.receiptSerial!==b.receiptSerial)
      fail(`${label}[${index}] local receipt differs from its captured PRE identity`);
    for(const field of PARTICLE_SEMANTIC_FIELDS)
      if(a[field]!==b[field])fail(`${label}[${index}].${field} differs`);
  }
}

function compareEndpointWithLocalParticleSerial(left,right,{allowReferenceReference=false}={}) {
  const a=rawParticleEndpoint(left),b=rawParticleEndpoint(right);
  validateCpuEndpointEnvelope(a);validateCpuEndpointEnvelope(b);
  if(a.view!==b.view)fail('comparison views differ');
  if(a.view===CPU_ENDPOINT_VIEW.pre){
    if(a.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre||b.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre)
      fail('PRE comparison requires SharedPre on both instances');
  }else if(a.view===CPU_ENDPOINT_VIEW.cpuEnd||a.view===CPU_ENDPOINT_VIEW.drawEnd){
    const correctedPair=a.provenance===CPU_ENDPOINT_PROVENANCE.reference&&b.provenance===CPU_ENDPOINT_PROVENANCE.corrected;
    const referencePair=allowReferenceReference&&a.provenance===CPU_ENDPOINT_PROVENANCE.reference&&
      b.provenance===CPU_ENDPOINT_PROVENANCE.reference;
    if(!correctedPair&&!referencePair)
      fail(allowReferenceReference?'CPU/DRAW comparison requires Reference/Reference or Reference/Corrected':
        'CPU/DRAW comparison requires Reference then Corrected');
  }else fail(`unknown comparison view ${a.view}`);
  if(a.sourceFrame!==b.sourceFrame)fail('source-frame stamps differ');
  for(let di=0;di<9;di++){
    const ad=a.domains[di],bd=b.domains[di];
    if(ad.id!==di+1||bd.id!==ad.id||ad.name!==bd.name||ad.records.length!==bd.records.length)
      fail(`${ad.name} record count/order differs`);
    for(let ri=0;ri<ad.records.length;ri++){
      const ar=ad.records[ri],br=bd.records[ri];
      if(di===1&&ar.kindName==='ParticleAnchor')compareParticleAnchorRecord(ar,br,{localSerial:true});
      else compareRecord(ar,br,ad.name);
    }
  }
}

/** Decode the opt-in v2 envelope while retaining the complete raw schema-312 rows. */
export function decodeCpuParticleEndpointResult(value) {
  if(!value||value.schema!==CPU_PARTICLE_ENDPOINT_SCHEMA||value.rawSchema!==CPU_ENDPOINT_SCHEMA||
     value.recordBytes!==CPU_ENDPOINT_RECORD_BYTES||value.revalidated!==true||
     ![1,2,3].includes(value.view)||![1,2,3].includes(value.provenance)||
     !Array.isArray(value.domains)||value.domains.length!==9)
    fail('particle envelope header/schema/refinalization invalid');
  const keys=['schema','rawSchema','recordBytes','view','provenance','sourceFrame','revalidated','domains','particleCells'];
  if(Object.keys(value).sort().join(',')!==keys.sort().join(','))fail('particle envelope has missing or unrecognized fields');
  const raw=rawParticleEndpoint(value);
  validateCpuEndpointEnvelope(raw);
  const anchor=particleAnchorFields(readParticleAnchor(raw));
  const authority=validateParticleRows(value,anchor);
  requireParticlePayloadCoverage(raw,value.particleCells.liveRows,'particle live row');
  if(value.view===CPU_ENDPOINT_VIEW.pre)
    requireParticlePayloadCoverage(raw,value.particleCells.capturedRows,'particle captured PRE row');
  particleIdentityRelation(value);
  return value;
}

/** Within-page continuity: captured rows remain the original PRE receipts. */
export function validateCpuParticleEndpointAgainstPre(current,pre) {
  decodeCpuParticleEndpointResult(current);decodeCpuParticleEndpointResult(pre);
  if(pre.view!==CPU_ENDPOINT_VIEW.pre||current.view===CPU_ENDPOINT_VIEW.pre||
     pre.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre)
    fail('captured particle continuity requires a PRE endpoint and a later view');
  const currentAnchor=particleAnchorFields(readParticleAnchor(rawParticleEndpoint(current)));
  const preAnchor=particleAnchorFields(readParticleAnchor(rawParticleEndpoint(pre)));
  for(const name of ['World','Serial','AnchorToken','ParticleCount','AppsrtCount'])
    if(currentAnchor[name]!==preAnchor[name])fail(`particle anchor ${name} changed after captured PRE`);
  const a=current.particleCells,b=pre.particleCells;
  if(a.world!==b.world||a.anchorSerial!==b.anchorSerial||a.anchorToken!==b.anchorToken||
     a.capturedCount!==b.capturedCount)
    fail('particle local anchor changed after captured PRE');
  compareParticleRows(a.capturedRows,b.capturedRows,'captured PRE particle rows',{includeReceiptSerial:true});
  return {equal:true,capturedRows:a.capturedCount};
}

/** Two-page v2 comparison; local receipt counters authenticate membership but are not identity keys. */
function compareCpuParticleEndpointsInternal(left,right,{preLeft=null,preRight=null,
  allowReferenceReference=false}={}) {
  decodeCpuParticleEndpointResult(left);decodeCpuParticleEndpointResult(right);
  compareEndpointWithLocalParticleSerial(left,right,{allowReferenceReference});
  const a=left.particleCells,b=right.particleCells;
  const localA=validateParticleRows(left,particleAnchorFields(readParticleAnchor(rawParticleEndpoint(left))));
  const localB=validateParticleRows(right,particleAnchorFields(readParticleAnchor(rawParticleEndpoint(right))));
  if(localA.world!==localB.world||a.anchorToken!==b.anchorToken||
     a.liveCount!==b.liveCount||a.capturedCount!==b.capturedCount)
    fail('particle semantic header/membership differs');
  compareParticleRows(a.liveRows,b.liveRows,'live particle rows');
  if(left.view===CPU_ENDPOINT_VIEW.pre){
    compareParticleRows(a.capturedRows,b.capturedRows,'captured PRE particle rows');
    compareParticleRows(a.liveRows,a.capturedRows,'left PRE live/captured rows',
      {includeReceiptSerial:true,ignoreKind:true});
    compareParticleRows(b.liveRows,b.capturedRows,'right PRE live/captured rows',
      {includeReceiptSerial:true,ignoreKind:true});
    const relationA=particleIdentityRelation(left),relationB=particleIdentityRelation(right);
    if(relationA.some((index,i)=>index!==relationB[i]))fail('PRE live/captured incarnation relation differs');
    requireParticlePayloadCoverage(rawParticleEndpoint(left),a.capturedRows,'left captured PRE particle row');
    requireParticlePayloadCoverage(rawParticleEndpoint(right),b.capturedRows,'right captured PRE particle row');
  }else{
    if(!preLeft||!preRight)fail('CPU/DRAW particle comparison requires both captured PRE endpoints');
    validateCpuParticleEndpointAgainstPre(left,preLeft);
    validateCpuParticleEndpointAgainstPre(right,preRight);
    compareCpuParticleEndpoints(preLeft,preRight);
    const relationA=particleIdentityRelation(left),relationB=particleIdentityRelation(right);
    if(relationA.length!==relationB.length||relationA.some((index,i)=>index!==relationB[i]))
      fail('live/captured particle incarnation relation differs');
  }
  return {equal:true,view:left.view,sourceFrame:left.sourceFrame,domains:9,
    particleCells:{live:a.liveCount,captured:a.capturedCount,incarnationRelations:particleIdentityRelation(left).length}};
}

export function compareCpuParticleEndpoints(left,right,{preLeft=null,preRight=null}={}) {
  return compareCpuParticleEndpointsInternal(left,right,{preLeft,preRight});
}

/** Strict independent-page comparison for two confirmed reference endpoints. */
export function compareCpuReferenceParticleEndpoints(left,right,{preLeft=null,preRight=null}={}) {
  decodeCpuParticleEndpointResult(left);decodeCpuParticleEndpointResult(right);
  if(left.view===CPU_ENDPOINT_VIEW.pre){
    if(left.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre||
       right.provenance!==CPU_ENDPOINT_PROVENANCE.sharedPre)
      fail('confirmed PRE comparison requires SharedPre on both instances');
  }else if(left.provenance!==CPU_ENDPOINT_PROVENANCE.reference||
           right.provenance!==CPU_ENDPOINT_PROVENANCE.reference)
    fail('confirmed CPU/DRAW comparison requires Reference on both instances');
  return compareCpuParticleEndpointsInternal(left,right,{preLeft,preRight,allowReferenceReference:true});
}
