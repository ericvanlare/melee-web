// Private two-tick drawn-reference runner. Source actions are issued only by the
// frozen native command route; JavaScript records evidence and waits for idle.
import {waitForAuroraSubmission} from './cpu_restore_runner.mjs';


// Diagnostic only: actual write occurrence and historical PRE join. It never
// proves uninterrupted live piece lifetime or changes strict CPU equality.
export function decodeCpuLObjWriterEvidence(bytes) {
 if(!(bytes instanceof Uint8Array)||bytes.length!==16784)throw Error('LObj writer extent');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=o=>v.getUint32(o,true),n=o=>v.getBigUint64(o,true);
 const hex=o=>n(o).toString(16).padStart(16,'0'),zero=(a,b)=>!bytes.slice(a,b).some(x=>x),words=(a,b)=>Array.from(bytes.slice(a,b),x=>x.toString(16).padStart(2,'0')).join('');
 if(u(0)!==1||u(4)!==16784||u(8)>2||u(12)>10||u(16)>1||u(20)>128||u(24)<u(20)||u(28)>1||u(100)>1||u(112)>1||u(140)||!zero(392,400))throw Error('LObj writer schema');
 const count=u(20),post=u(80),drawn=u(84),pathBytes=u(96);
 if(pathBytes>=232||!zero(160+pathBytes,392)||(pathBytes&&(!u(88)||!u(92)))||(!pathBytes&&u(92)))throw Error('LObj writer source path');
 let path='';if(pathBytes){path=new TextDecoder('utf-8',{fatal:true}).decode(bytes.slice(160,160+pathBytes));if(path.includes('\0'))throw Error('LObj writer path encoding');}
 if(post!==0xffffffff&&post>count||drawn!==0xffffffff&&(post===0xffffffff||drawn<post||drawn>count))throw Error('LObj writer endpoint order');
 const events=[];
 for(let i=0;i<count;++i){const b=400+i*128;
  if(u(b)!==i+1||u(b+4)<1||u(b+4)>13||u(b+8)>2||u(b+12)>10||u(b+92)>3||!zero(b+120,b+128))throw Error('LObj writer event schema');
  if(!n(b+32)||!n(b+40)||!u(b+20)||!u(b+24)||u(b+20)+u(b+24)>0x100000000||u(b+28)>u(b+24)||12>u(b+24)-u(b+28)||n(b+56)!==n(b+32)+BigInt(u(b+28))||n(b+48)+60n!==n(b+56)||n(b+48)<n(b+32)||n(b+48)+212n>n(b+32)+BigInt(u(b+24)))throw Error('LObj writer row span');
  if(u(b+92)&1){
   if(u(b+92)!==3||!u(b+64)||u(b+68)!==60||!u(b+72)||(u(b+72)&31)||u(b+76)<72||u(b+76)>u(b+72)||u(b+72)-u(b+76)>31||!u(b+80)||!u(b+84)||(u(b+84)&3)||u(b+88)!==u(b+84)||u(b+64)+u(b+68)!==u(b+20)+u(b+28)||u(b+64)<u(b+20)||u(b+64)+u(b+72)>u(b+20)+u(b+24))throw Error('LObj writer PRE historical piece');
  }else if(!zero(b+64,b+92))throw Error('LObj writer unknown piece fields');
  if(u(b+8)===1&&(u(b+12)||u(b+92)!==3))throw Error('LObj writer qualified event');
  events.push({sequence:u(b),command:u(b+4),status:u(b+8),reason:u(b+12),preRow:u(b+16),source:u(b+20),requested:u(b+24),fieldOffset:u(b+28),host:hex(b+32),generation:hex(b+40),object:hex(b+48),field:hex(b+56),pieceSource:u(b+64),pieceOffset:u(b+68),physical:u(b+72),originalRequested:u(b+76),pieceGeneration:u(b+80),classInfo:u(b+84),savedClassInfo:u(b+88),scope:'CAPTURE_TIME_ONLY',livePieceLifetime:'UNKNOWN',flags:u(b+92),beforeHex:words(b+96,b+108),afterHex:words(b+108,b+120)});
 }
 if(!zero(400+count*128,16784))throw Error('LObj writer unused events');
 if(u(8)===1){
  if(u(12)||!u(16)||!n(32)||!n(40)||!n(48)||!u(100)||!u(112)||u(28)||u(24)!==count||post===0xffffffff||drawn===0xffffffff||!u(108)||u(108)>post||u(104)>=post||!pathBytes||u(88)!==1||!n(144)||!n(152)||u(120)>=u(128)||u(132)>255||u(136)>255||u(132)===u(136))throw Error('LObj writer match header');
  const b=400+u(104)*128,off=u(120)-u(b+28);
  if(u(b+8)!==1||u(b+92)!==3||u(b+16)!==u(116)||u(b+20)!==u(124)||u(b+24)!==u(128)||n(b+32)!==n(144)||n(b+40)!==n(152)||off<0||off>=12||bytes[b+108+off]!==u(132))throw Error('LObj writer first-difference event');
  // Internal record-set consistency only. Native comparison against the actual
  // full POST field remains authoritative; POST12 is not a second wire field.
  const matches=[];
  for(let i=0;i<post;++i){const c=400+i*128,delta=u(120)-u(c+28);
   if(u(c+8)===1&&!u(c+12)&&u(c+92)===3&&u(c+16)===u(116)&&u(c+20)===u(124)&&u(c+24)===u(128)&&n(c+32)===n(144)&&n(c+40)===n(152)&&delta>=0&&delta<12&&words(c+108,c+120)===words(b+108,b+120))matches.push(i);
  }
  if(matches.length!==u(108)||matches.at(-1)!==u(104))throw Error('LObj writer candidate-set consistency');
 }
 return {schema:1,bytes:16784,status:u(8),reason:u(12),scope:'SOURCE_WRITE_OCCURRENCE_ONLY_CAPTURE_TIME_JOIN',livePieceLifetime:'UNKNOWN',hookActive:u(16),count,seen:u(24),overflow:u(28),epoch:hex(32),world:hex(40),session:hex(48),pieceInstance:hex(56),captureSerial:hex(64),topologyRevision:hex(72),postCount:post,drawnCount:drawn,siteKind:u(88),line:u(92),path,firstDifferenceMatches:u(100),matchedIndex:u(104),matchedCount:u(108),complete:u(112),row:u(116),offset:u(120),source:u(124),requested:u(128),expected:u(132),actual:u(136),host:hex(144),generation:hex(152),events};
}

export function decodeCpuTObjWriterEvidence(bytes) {
 if(!(bytes instanceof Uint8Array)||bytes.length!==28048)throw Error('TObj writer extent');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=o=>v.getUint32(o,true),n=o=>v.getBigUint64(o,true);
 const hex=o=>n(o).toString(16).padStart(16,'0'),zero=(a,b)=>!bytes.slice(a,b).some(x=>x),words=(a,b)=>Array.from(bytes.slice(a,b),x=>x.toString(16).padStart(2,'0')).join('');
 if(u(0)!==1||u(4)!==28048||u(8)>2||u(12)>12||u(16)>1||u(20)>128||u(24)<u(20)||u(28)>1||u(100)>1||u(112)>1||u(396)||(u(140)&3))throw Error('TObj writer schema');
 const count=u(20),post=u(80),drawn=u(84),pathBytes=u(96);
 if(pathBytes>=232||!zero(160+pathBytes,392)||(pathBytes&&(!u(88)||!u(92)))||(!pathBytes&&u(92)))throw Error('TObj writer source path');
 let path='';if(pathBytes){path=new TextDecoder('utf-8',{fatal:true}).decode(bytes.slice(160,160+pathBytes));if(path.includes('\0'))throw Error('TObj writer path encoding');}
 if(post!==0xffffffff&&post>count||drawn!==0xffffffff&&(post===0xffffffff||drawn<post||drawn>count))throw Error('TObj writer endpoint order');
 const events=[];
 for(let i=0;i<count;++i){const b=400+i*216;
  if(u(b)!==i+1||u(b+4)<1||u(b+4)>13||u(b+8)>2||u(b+12)>12||u(b+92)>3||u(b+204)>2||n(b+208)>2n)throw Error('TObj writer event schema');
  if(!n(b+32)||!n(b+40)||!u(b+20)||!u(b+24)||u(b+20)+u(b+24)>0x100000000||u(b+28)>u(b+24)||48>u(b+24)-u(b+28)||n(b+56)!==n(b+32)+BigInt(u(b+28))||n(b+48)+116n!==n(b+56)||n(b+48)<n(b+32)||n(b+48)+172n>n(b+32)+BigInt(u(b+24)))throw Error('TObj writer row span');
  if(u(b+92)&1){
   if(u(b+92)!==3||!u(b+64)||u(b+68)!==116||!u(b+72)||(u(b+72)&31)||u(b+76)!==172||u(b+76)>u(b+72)||u(b+72)-u(b+76)>31||!u(b+80)||!u(b+84)||(u(b+84)&3)||u(b+88)!==u(b+84)||u(b+64)+u(b+68)!==u(b+20)+u(b+28)||u(b+64)<u(b+20)||u(b+64)+u(b+72)>u(b+20)+u(b+24))throw Error('TObj writer PRE historical piece');
  }else if(!zero(b+64,b+92))throw Error('TObj writer unknown piece fields');
  if(u(b+8)===1&&(u(b+12)||u(b+92)!==3||!u(140)||u(b+84)!==u(140)||!u(b+204)||u(b+4)!==8+u(b+204)||n(b+208)!==BigInt(u(b+204))||u(392)>0xfffffffd||u(b+200)!==u(392)+u(b+204)))throw Error('TObj writer qualified event');
  events.push({sequence:u(b),command:u(b+4),status:u(b+8),reason:u(b+12),preRow:u(b+16),source:u(b+20),requested:u(b+24),fieldOffset:u(b+28),host:hex(b+32),generation:hex(b+40),object:hex(b+48),field:hex(b+56),pieceSource:u(b+64),pieceOffset:u(b+68),physical:u(b+72),originalRequested:u(b+76),pieceGeneration:u(b+80),classInfo:u(b+84),savedClassInfo:u(b+88),scope:'CAPTURE_TIME_ONLY',livePieceLifetime:'UNKNOWN',flags:u(b+92),beforeHex:words(b+96,b+144),afterHex:words(b+144,b+192),flagsBefore:u(b+192),flagsAfter:u(b+196),outerFrame:u(b+200),outerPhase:u(b+204),outerEpoch:hex(b+208)});
 }
 if(!zero(400+count*216,28048))throw Error('TObj writer unused events');
 if(u(8)===1){
  if(u(12)||!u(16)||!n(32)||!n(40)||!n(48)||!u(100)||!u(112)||u(28)||u(24)!==count||post===0xffffffff||drawn===0xffffffff||!u(108)||u(108)>post||u(104)>=post||!pathBytes||u(88)!==1||!n(144)||!n(152)||!u(140)||u(392)>0xfffffffd||u(120)>=u(128)||u(132)>255||u(136)>255||u(132)===u(136))throw Error('TObj writer match header');
  const b=400+u(104)*216,off=u(120)-u(b+28);
  if(u(b+8)!==1||u(b+92)!==3||u(b+204)!==1||n(b+208)!==1n||u(b+200)!==u(392)+1||u(b+16)!==u(116)||u(b+20)!==u(124)||u(b+24)!==u(128)||n(b+32)!==n(144)||n(b+40)!==n(152)||off<0||off>=48||bytes[b+144+off]!==u(132))throw Error('TObj writer first-difference event');
  // Internal record-set consistency only. Native comparison against the actual
  // full POST matrix remains authoritative; POST48 is not a second wire field.
  const matches=[];
  for(let i=0;i<post;++i){const c=400+i*216,delta=u(120)-u(c+28);
   if(u(c+8)===1&&!u(c+12)&&u(c+92)===3&&u(c+16)===u(116)&&u(c+20)===u(124)&&u(c+24)===u(128)&&n(c+32)===n(144)&&n(c+40)===n(152)&&delta>=0&&delta<48&&u(c+204)===1&&n(c+208)===1n&&u(c+200)===u(392)+1&&words(c+144,c+192)===words(b+144,b+192))matches.push(i);
  }
  if(matches.length!==u(108)||matches.at(-1)!==u(104))throw Error('TObj writer candidate-set consistency');
 }
 return {schema:1,bytes:28048,status:u(8),reason:u(12),scope:'SOURCE_WRITE_OCCURRENCE_ONLY_CAPTURE_TIME_JOIN',livePieceLifetime:'UNKNOWN',hookActive:u(16),count,seen:u(24),overflow:u(28),epoch:hex(32),world:hex(40),session:hex(48),pieceInstance:hex(56),captureSerial:hex(64),topologyRevision:hex(72),postCount:post,drawnCount:drawn,siteKind:u(88),line:u(92),path,firstDifferenceMatches:u(100),matchedIndex:u(104),matchedCount:u(108),complete:u(112),row:u(116),offset:u(120),source:u(124),requested:u(128),expected:u(132),actual:u(136),canonicalClass:u(140),preFrame:u(392),host:hex(144),generation:hex(152),events};
}

export const CPU_TWO_TICK_EVIDENCE_BYTES = 35368;
export const CPU_TWO_TICK_MODE = 2;
export const TWO_TICK_PAD_300 = 'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=';
export const TWO_TICK_PAD_301 = 'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=';

// scope=CAPTURE_TIME_ONLY: saved PRE bytes plus frozen table, never live class.
export function decodeCpuClassPieceCaptureWitness(bytes) {
 if(!(bytes instanceof Uint8Array)||bytes.length!==160)throw Error('Capture-time piece extent');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=o=>v.getUint32(o,true);
 const n=o=>v.getBigUint64(o,true),hex=o=>n(o).toString(16).padStart(16,'0');
 if(u(0)!==1||u(4)!==160||u(8)>2||u(12)>11||u(16)!==1||u(20)>1||u(24)>1||u(28)>11||u(32)>4||u(36)>3||u(60)||u(148)>1||bytes.slice(152).some(x=>x))throw Error('Capture-time piece schema');
 if(u(20)&&(!n(104)||!n(112)||!n(64)||!n(72)||!u(56)||u(48)>=u(56)))throw Error('Capture-time piece row');
 if(u(8)===1){
  if(u(12)||!u(20)||!u(24)||!n(80)||!u(128)||(u(128)&31)||!u(132)||u(132)>u(128)||u(128)-u(132)>31||!u(136)||u(124)>=u(128))throw Error('Capture-time piece geometry');
  if(!((u(32)===3&&!u(28)&&n(96)===n(88))||(u(32)===4&&u(28)===7&&n(88)<0xffffffffffffffffn&&n(96)===n(88)+1n)))throw Error('Capture-time piece frozen state');
  if(!u(52)||u(52)+u(56)>0x100000000||u(120)<u(52)||u(120)+u(128)>u(52)+u(56)||u(120)+u(124)!==u(52)+u(48)||u(148)!==Number(u(124)>=u(132)))throw Error('Capture-time piece bounds');
  if(u(36)===1){if(u(140)||u(144))throw Error('Capture-time raw class');}
  else if(u(36)!==3||!u(140)||(u(140)&3)||u(144)!==u(140)||u(132)<4)throw Error('Capture-time saved header');
 }
 return {schema:1,bytes:160,status:u(8),reason:u(12),scope:'CAPTURE_TIME_ONLY',firstDifferenceMatches:u(20),hookActive:u(24),liveReason:u(28),coverageState:u(32),recordState:u(36),preRow:u(40),postRow:u(44),offset:u(48),source:u(52),requested:u(56),epoch:hex(64),world:hex(72),instance:hex(80),captureSerial:hex(88),eventSerial:hex(96),host:hex(104),generation:hex(112),pieceSource:u(120),pieceOffset:u(124),physical:u(128),originalRequested:u(132),pieceGeneration:u(136),classInfo:u(140),savedClassInfo:u(144),physicalSlack:!!u(148)};
}

// Boundaries/type labels are diagnostic only; UNKNOWN never admits restore.
export function decodeCpuClassPieceWitness(bytes) {
  if(!(bytes instanceof Uint8Array)||bytes.length!==208)throw Error('Class piece extent');
  const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=o=>v.getUint32(o,true);
  const hex=o=>v.getBigUint64(o,true).toString(16).padStart(16,'0');
  if(u(0)!==1||u(4)!==208||u(8)>2||u(12)>11||u(16)>1||u(20)>1||u(24)>4||u(28)>3||u(132)>1||bytes.slice(192).some(b=>b))throw Error('Class piece schema');
  if(u(16)&&(!v.getBigUint64(72,true)||!v.getBigUint64(80,true)||!u(100)||u(92)>=u(100)))throw Error('Class piece allocation identity');
  if(u(8)===1&&(!u(16)||!u(20)||u(12)||!u(112)||(u(112)&31)||!u(116)||u(116)>u(112)||u(112)-u(116)>31||u(108)>=u(112)||!u(120)||!u(28)))throw Error('Class piece live geometry');
  if(u(8)===1&&(!v.getBigUint64(32,true)||!v.getBigUint64(40,true)||!v.getBigUint64(48,true)||
      v.getBigUint64(56,true)!==v.getBigUint64(64,true)||!u(96)||u(96)+u(100)>0x100000000||
      u(104)<u(96)||u(104)+u(112)>u(96)+u(100)||u(104)+u(108)!==u(96)+u(92)))throw Error('Class piece owner/bounds');
  if(u(8)===1&&u(28)===1&&(u(124)||u(128)||u(132)||u(136)||u(140)))throw Error('Class piece raw type');
  if(u(8)===1&&u(28)!==1&&u(28)!==3)throw Error('Class piece unsupported live state');
  if(u(8)===1&&u(132)&&(!u(124)||u(128)!==u(116)||u(136)!==u(124)||u(140)!==u(124)||u(28)!==3))throw Error('Class piece confirmed header');
  return {schema:1,bytes:208,status:u(8),reason:u(12),firstDifferenceMatches:u(16),hookActive:u(20),coverageState:u(24),recordState:u(28),
    epoch:hex(32),world:hex(40),instance:hex(48),captureSerial:hex(56),eventSerial:hex(64),host:hex(72),generation:hex(80),
    row:u(88),offset:u(92),source:u(96),requested:u(100),pieceSource:u(104),pieceOffset:u(108),physical:u(112),originalRequested:u(116),
    pieceGeneration:u(120),classInfo:u(124),classObjSize:u(128),confirmed:u(132),savedClassInfo:u(136),liveClassInfo:u(140),
    firstEvent:{serial:hex(144),host:hex(152),generation:hex(160),kind:u(168),source:u(172),requested:u(176),physical:u(180),classBefore:u(184),classAfter:u(188)}};
}

// Independent fixed diagnostic ABI; all 64-bit identities remain exact hex
// strings in JSON. This observer never grants state/type/restore authority.
export function decodeCpuAllocationOriginWitness(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== 320) throw Error('Allocation origin extent');
  const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength), u=o=>v.getUint32(o,true);
  const hex=o=>v.getBigUint64(o,true).toString(16).padStart(16,'0');
  if(u(0)!==1||u(4)!==320||u(8)>2||u(12)>7||u(16)>1||u(20)>1||u(84)!==0) throw Error('Allocation origin schema');
  const status=u(8), reason=u(12), matched=u(16), kind=u(72), length=u(80);
  if(length>=232||bytes.slice(88+length).some(x=>x!==0)) throw Error('Allocation origin path bounds');
  let path='';for(const b of bytes.slice(88,88+length)) {if(b===0)throw Error('Allocation origin embedded null');path+=String.fromCharCode(b);}
  if(matched&&(!v.getBigUint64(32,true)||!v.getBigUint64(40,true)||!v.getBigUint64(48,true)||!v.getBigUint64(56,true)||!u(68)||u(28)>=u(68))) throw Error('Allocation origin paired bounds');
  if(status===1&&(!matched||!u(20)||reason||!length||!u(76)||(kind!==1&&kind!==2)||!v.getBigUint64(32,true)||!v.getBigUint64(40,true))) throw Error('Allocation origin observed identity');
  if(status!==1&&(kind||u(76)||length)) throw Error('Allocation origin unobserved token');
  if(!matched&&bytes.slice(24).some(x=>x!==0)) throw Error('Allocation origin unpaired identity');
  return {schema:1,bytes:320,status,reason,firstDifferenceMatches:matched,hookActive:u(20),row:u(24),offset:u(28),
    host:hex(32),generation:hex(40),epoch:hex(48),world:hex(56),source:u(64),requested:u(68),siteKind:kind,line:u(76),path};
}

export function decodeCpuPayloadFailureWitness(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== 256) throw new Error('Payload witness extent');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u = o => v.getUint32(o, true), hex = o => v.getBigUint64(o, true).toString(16).padStart(16, '0');
  if (u(0) !== 1 || u(4) !== 256) throw new Error('Payload witness schema');
  const originalStatus = u(8), firstDifferenceMatches = u(12), windowStatus = u(24);
  if (originalStatus > 2 || firstDifferenceMatches > 1 || u(184) > 1 || u(188) > 2 || u(192) > 9 || u(196) > 2) throw new Error('Payload witness status');
  const encode = raw => { let s = ''; for (const b of raw) s += String.fromCharCode(b); return btoa(s); };
  const window = {schema: u(16), bytes: u(20), status: windowStatus, admission: u(28), row: u(32), offset: u(36),
    source: u(40), requested: u(44), start: u(48), count: u(52), host: hex(64), generation: hex(72),
    epoch: hex(80), world: hex(88), revision: hex(96), expected: encode(bytes.slice(120, 152)), actual: encode(bytes.slice(152, 184))};
  if (u(56) || u(60) || v.getBigUint64(104, true) || v.getBigUint64(112, true)) throw new Error('Payload witness unsupported root attribution');
  if (originalStatus === 0) {
    if (firstDifferenceMatches || bytes.slice(16).some(b => b !== 0)) throw new Error('Payload witness unexpected empty data');
  } else {
    if (window.schema !== 1 || window.bytes !== 168 || window.status > 2 || window.count > 32 ||
        window.start + window.count > window.requested) throw new Error('Payload witness window bounds');
    if (window.status === 2 && (window.offset < window.start || window.offset - window.start >= window.count)) throw new Error('Payload witness first-byte bounds');
    if (firstDifferenceMatches && (originalStatus !== 2 || window.status !== 2)) throw new Error('Payload witness crosscheck');
    if (u(188) && !firstDifferenceMatches) throw new Error('Payload witness unpaired attribution');
    if (window.status !== 2 && bytes.slice(32, 184).some(b => b !== 0)) throw new Error('Payload witness refused window data');
    if (bytes.slice(120 + window.count, 152).some(b => b !== 0) || bytes.slice(152 + window.count, 184).some(b => b !== 0)) throw new Error('Payload witness window tail');
  }
  if (u(188) && !u(184)) throw new Error('Payload witness inactive attribution');
  if (u(188) === 1) {
    const stride = u(232), count = u(236), mask = u(240), product = stride * count;
    if (!u(184) || !firstDifferenceMatches || u(192) || u(196) !== 1 || !v.getBigUint64(200, true) ||
        !stride || !count || mask === 0xffffffff || (mask & ((mask + 1) >>> 0)) || (stride & mask) ||
        !Number.isSafeInteger(product) || product > 0xffffffff || product !== u(244) || product !== window.requested ||
        hex(208) !== window.generation || hex(216) !== window.epoch || hex(224) !== window.world ||
        u(248) !== Math.floor(window.offset / stride) || u(252) !== window.offset % stride) throw new Error('Payload witness association bounds/lifetime');
  }
  if (u(188) === 2 && (!u(192) || bytes.slice(200).some(b => b !== 0))) throw new Error('Payload witness refused identity');
  return {schema: 1, bytes: 256, originalStatus, firstDifferenceMatches, window, hookActive: u(184),
    attribution: u(188), reason: u(192), associationState: u(196), descriptor: hex(200),
    allocationGeneration: hex(208), epoch: hex(216), world: hex(224), stride: u(232), count: u(236),
    alignmentMask: u(240), poolBytes: u(244), cellIndex: u(248), cellOffset: u(252), base64: encode(bytes)};
}

export function decodeCpuTwoTickEvidence(bytes) {
  const size = 35368, pairSize = 11416, diagnosticsOffset = 34312, diagnosticStride = 132;
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== size) throw new Error('Two-tick evidence extent');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u32 = offset => view.getUint32(offset, true);
  if (u32(0) !== 1 || u32(4) !== size || u32(8) !== 2) throw new Error('Two-tick evidence schema/mode');
  const encode = value => {
    let text = '';
    for (let i = 0; i < value.length; i += 0x6000) text += String.fromCharCode(...value.subarray(i, i + 0x6000));
    return btoa(text);
  };
  const decodePair = offset => {
    const raw = bytes.slice(offset, offset + pairSize), row = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
    if (row.getUint32(0, true) !== 1 || row.getUint32(4, true) !== pairSize) throw new Error('Two-tick raw-pair schema/bytes');
    const read = (start, count) => encode(raw.slice(start, start + count));
    const fields = ['domain','status','row','offset','expected','actual','address','extent','detail'];
    const comparison = Object.fromEntries(fields.map((name, index) => [name, row.getUint32(11376 + index * 4, true)]));
    const value = {
      bytes: pairSize, base64: encode(raw), schema: row.getUint32(0, true), declaredBytes: row.getUint32(4, true),
      state: row.getUint32(8, true), firstFailure: row.getUint32(12, true),
      preFrame: row.getUint32(16, true), postFrame: row.getUint32(20, true),
      commitResult: row.getUint32(24, true), equality: row.getUint32(28, true),
      baselineObservation: read(32, 1280), replayObservation: read(1312, 1280),
      baselinePcm: read(2592, 4272), replayPcm: read(6864, 4272),
      baselinePad: read(11136, 44), replayPad: read(11180, 44),
      baselineRngProfile: read(11224, 32), replayRngProfile: read(11256, 32),
      baselineTransfer: read(11288, 40), replayTransfer: read(11328, 40),
      checkedDomains: row.getUint32(11368, true), matchedDomains: row.getUint32(11372, true), comparison,
    };
    value.rawFiveMatch = ['baselineObservation','baselinePcm','baselinePad','baselineRngProfile','baselineTransfer']
      .every((name, index) => value[name] === value[['replayObservation','replayPcm','replayPad','replayRngProfile','replayTransfer'][index]]);
    return value;
  };
  const diagnosticMask = u32(56);
  if ((diagnosticMask & ~255) !== 0) throw new Error('Two-tick diagnostic mask contains unknown slots');
  const diagnostics = Array.from({length: 8}, (_, index) => {
    if (!(diagnosticMask & (1 << index))) return null;
    const raw = bytes.slice(diagnosticsOffset + index * diagnosticStride,
      diagnosticsOffset + (index + 1) * diagnosticStride);
    return {index, bytes: diagnosticStride, base64: encode(raw)};
  });
  return {
    schema: u32(0), bytes: u32(4), mode: u32(8), state: u32(12), firstFailure: u32(16),
    preFrame: u32(20), postFrame: u32(24), commitResult: u32(28),
    cpuEquality: u32(32), drawnEquality: u32(36), checkedCpu: u32(40), matchedCpu: u32(44),
    checkedDrawn: u32(48), matchedDrawn: u32(52), diagnosticMask, lastCommand: u32(60),
    pair300: decodePair(64), pair301: decodePair(11480), drawnPair301: decodePair(22896),
    diagnostics, base64: encode(bytes),
  };
}

export function validateCpuTwoTickFinalEvidence(evidence, checkpointSourceFrame) {
  const fail = message => { throw new Error(`Two-tick ${message}`); };
  if (!evidence || evidence.schema !== 1 || evidence.bytes !== 35368 || evidence.mode !== 2 ||
      evidence.state !== 5 || evidence.firstFailure !== 0 || evidence.lastCommand !== 13 ||
      evidence.commitResult !== 0 || evidence.cpuEquality !== 1 || evidence.drawnEquality !== 1 ||
      evidence.checkedCpu !== 0x3fe || evidence.matchedCpu !== 0x3fe ||
      evidence.checkedDrawn !== 0x3fe || evidence.matchedDrawn !== 0x3fe ||
      evidence.diagnosticMask !== 255 || evidence.preFrame !== checkpointSourceFrame ||
      evidence.postFrame !== checkpointSourceFrame + 2) fail('final typed endpoint/header contract missing');
  const p0 = evidence.pair300, p1 = evidence.pair301, drawn = evidence.drawnPair301;
  if (!p0 || p0.schema !== 1 || p0.declaredBytes !== 11416 || p0.preFrame !== checkpointSourceFrame ||
      p0.postFrame !== checkpointSourceFrame + 1 || p0.firstFailure !== 0 || p0.equality !== 1 ||
      p0.checkedDomains !== 0 || p0.matchedDomains !== 0 || !p0.rawFiveMatch) fail('sample 300 raw pair mismatch');
  if (!p1 || p1.schema !== 1 || p1.declaredBytes !== 11416 || p1.preFrame !== checkpointSourceFrame + 1 ||
      p1.postFrame !== checkpointSourceFrame + 2 || p1.firstFailure !== 0 || p1.commitResult !== 0 ||
      p1.equality !== 1 || p1.checkedDomains !== 0x3fe || p1.matchedDomains !== 0x3fe ||
      p1.comparison.status !== 0 || !p1.rawFiveMatch) fail('sample 301 CPU endpoint mismatch');
  if (!drawn || drawn.schema !== 1 || drawn.declaredBytes !== 11416 || drawn.firstFailure !== 0 ||
      drawn.commitResult !== 0 || drawn.equality !== 1 || drawn.checkedDomains !== 0x3fe ||
      drawn.matchedDomains !== 0x3fe || drawn.comparison.status !== 0 || !drawn.rawFiveMatch ||
      drawn.preFrame !== checkpointSourceFrame + 2 || drawn.postFrame !== checkpointSourceFrame + 2) {
    fail('sample 301 drawn endpoint mismatch');
  }
  if (p0.baselinePad !== 'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=' ||
      p0.replayPad !== p0.baselinePad || p1.baselinePad !== 'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=' ||
      p1.replayPad !== p1.baselinePad || drawn.baselinePad !== p1.baselinePad || drawn.replayPad !== p1.baselinePad) {
    fail('literal PAD samples 300/301 drift');
  }
  const slots = evidence.diagnostics;
  if (!Array.isArray(slots) || slots.length !== 8 || slots.some((row, index) =>
      !row || row.index !== index || row.bytes !== 132 || typeof row.base64 !== 'string')) fail('diagnostic slot evidence missing');
  if (slots[0].base64 !== slots[1].base64 || slots[2].base64 !== slots[3].base64 ||
      slots[4].base64 !== slots[5].base64 || slots[4].base64 !== slots[6].base64) {
    fail('a correction tick changed draw diagnostics');
  }
  return true;
}

export async function runBrowserCpuTwoTick(page, wire, checkpoint, expectedInputMapping, expectedStocks, helpers) {
  const plan = {wire, checkpointSourceFrame: checkpoint.boundary.sourceFrame, checkpointPad: checkpoint.pad,
    expectedInputMapping, expectedStocks, evidenceBytes: CPU_TWO_TICK_EVIDENCE_BYTES,
    tobjWriterDecodeSource: decodeCpuTObjWriterEvidence.toString(), lobjWriterDecodeSource: decodeCpuLObjWriterEvidence.toString(), capturePieceDecodeSource: decodeCpuClassPieceCaptureWitness.toString(), pieceDecodeSource: decodeCpuClassPieceWitness.toString(), originDecodeSource: decodeCpuAllocationOriginWitness.toString(), witnessDecodeSource: decodeCpuPayloadFailureWitness.toString(), decodeSource: decodeCpuTwoTickEvidence.toString(), validateFinalSource: validateCpuTwoTickFinalEvidence.toString(),
    waitSource: waitForAuroraSubmission.toString(), ...helpers,
    literalPad300: TWO_TICK_PAD_300, literalPad301: TWO_TICK_PAD_301};
  try {
    return await page.evaluate(async plan => {
      const {module} = window.__slippiSynctest;
      const progress = {status: 'running', mode: 'drawn-reference-no-draw-two-tick-v1',
        checkpointSourceFrame: plan.checkpointSourceFrame, commands: [], nativeEvidence: null,
        drawBoundaries: [], gpuWaits: []};
      window.__slippiSynctest.cpuRestoreProgress = progress;
      if (typeof SharedArrayBuffer !== 'undefined' && module.HEAPU8.buffer instanceof SharedArrayBuffer) {
        throw new Error('Shared Wasm heap refused before direct native reads');
      }
      if (typeof module._melee_web_synctest_cpu_two_tick_evidence !== 'function' ||
          typeof module._melee_web_synctest_cpu_two_tick_evidence_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_payload_failure_witness !== 'function' ||
          typeof module._melee_web_synctest_cpu_payload_failure_witness_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_allocation_origin_witness !== 'function' ||
          typeof module._melee_web_synctest_cpu_allocation_origin_witness_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_class_piece_witness !== 'function' ||
          typeof module._melee_web_synctest_cpu_class_piece_witness_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_class_piece_capture_witness !== 'function' ||
          typeof module._melee_web_synctest_cpu_class_piece_capture_witness_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_lobj_writer_evidence !== 'function' ||
          typeof module._melee_web_synctest_cpu_lobj_writer_evidence_bytes !== 'function' ||
          typeof module._melee_web_synctest_cpu_tobj_writer_evidence !== 'function' ||
          typeof module._melee_web_synctest_cpu_tobj_writer_evidence_bytes !== 'function') {
        throw new Error('Two-tick evidence exports are unavailable for the linked v6 profile');
      }
      const read = (pointer, size) => {
        if (!Number.isSafeInteger(pointer) || pointer <= 0 || !Number.isSafeInteger(size) || size <= 0 ||
            pointer + size > module.HEAPU8.length) throw new Error('CPU two-tick evidence extent');
        return module.HEAPU8.slice(pointer, pointer + size);
      };
      const encode = bytes => {
        let text = '';
        for (let i = 0; i < bytes.length; i += 0x6000) text += String.fromCharCode(...bytes.subarray(i, i + 0x6000));
        return btoa(text);
      };
      const decode = new Function(`return (${plan.decodeSource})`)();
      const validateFinal = new Function(`return (${plan.validateFinalSource})`)();
      const decodeLObjWriter = new Function(`return (${plan.lobjWriterDecodeSource})`)();
      const decodeTObjWriter = new Function(`return (${plan.tobjWriterDecodeSource})`)();
      const decodeCapturePiece = new Function(`return (${plan.capturePieceDecodeSource})`)();
      const decodePiece = new Function(`return (${plan.pieceDecodeSource})`)();
      const decodeOrigin = new Function(`return (${plan.originDecodeSource})`)();
      const decodeWitness = new Function(`return (${plan.witnessDecodeSource})`)();
      const evidence = () => {
        const size = module._melee_web_synctest_cpu_two_tick_evidence_bytes();
        if (size !== plan.evidenceBytes) throw new Error('CPU two-tick evidence ABI');
        const result = decode(read(module._melee_web_synctest_cpu_two_tick_evidence(), size));
        progress.nativeEvidence = result; // keep original evidence even if new observer refuses
        const witnessSize = module._melee_web_synctest_cpu_payload_failure_witness_bytes();
        if (witnessSize !== 256) throw new Error('CPU payload witness ABI');
        const witnessBytes = read(module._melee_web_synctest_cpu_payload_failure_witness(), witnessSize);
        progress.payloadFailureWitnessRaw = encode(witnessBytes);
        progress.payloadFailureWitness = decodeWitness(witnessBytes);
        const originSize=module._melee_web_synctest_cpu_allocation_origin_witness_bytes();
        if(originSize!==320)throw Error('Allocation origin ABI');
        const originBytes=read(module._melee_web_synctest_cpu_allocation_origin_witness(),originSize);
        progress.allocationOriginWitnessRaw=encode(originBytes);
        progress.allocationOriginWitness=decodeOrigin(originBytes);
        const pieceSize=module._melee_web_synctest_cpu_class_piece_witness_bytes();
        if(pieceSize!==208)throw Error('Class piece ABI');
        const pieceBytes=read(module._melee_web_synctest_cpu_class_piece_witness(),pieceSize);
        progress.classPieceWitnessRaw=encode(pieceBytes);progress.classPieceWitness=decodePiece(pieceBytes);
        const capturePieceSize=module._melee_web_synctest_cpu_class_piece_capture_witness_bytes();
        if(capturePieceSize!==160)throw Error('Capture-time piece ABI');
        const capturePieceBytes=read(module._melee_web_synctest_cpu_class_piece_capture_witness(),capturePieceSize);
        progress.classPieceCaptureWitnessRaw=encode(capturePieceBytes);
        progress.classPieceCaptureWitness=decodeCapturePiece(capturePieceBytes);
        const writerSize=module._melee_web_synctest_cpu_lobj_writer_evidence_bytes();
        if(writerSize!==16784)throw Error('LObj writer ABI');
        const writerBytes=read(module._melee_web_synctest_cpu_lobj_writer_evidence(),writerSize);
        progress.lobjWriterEvidenceRaw=encode(writerBytes);
        progress.lobjWriterEvidence=decodeLObjWriter(writerBytes);
        const tobjSize=module._melee_web_synctest_cpu_tobj_writer_evidence_bytes();
        if(tobjSize!==28048)throw Error('TObj writer ABI');
        const tobjBytes=read(module._melee_web_synctest_cpu_tobj_writer_evidence(),tobjSize);
        progress.tobjWriterEvidenceRaw=encode(tobjBytes);
        progress.tobjWriterEvidence=decodeTObjWriter(tobjBytes);
        return result;
      };
      const command = async number => {
        const record = {command: number, outcome: 'pending'};
        progress.commands.push(record);
        try {
          record.result = await module.ccall('melee_web_synctest_cpu_restore_command', 'number', ['number'], [number], {async: true});
          record.outcome = 'returned';
          record.nativeEvidence = evidence();
          record.payloadFailureWitness = progress.payloadFailureWitness;
          record.allocationOriginWitness = progress.allocationOriginWitness;
          record.classPieceWitness = progress.classPieceWitness;
          record.classPieceCaptureWitness = progress.classPieceCaptureWitness;
          record.lobjWriterEvidence = progress.lobjWriterEvidence; record.tobjWriterEvidence = progress.tobjWriterEvidence;
          if (record.result !== 0) throw new Error(`CPU two-tick command ${number} refused ${record.result}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
        } catch (error) {
          const ccallThrew = record.outcome === 'pending';
          record.error = String(error);
          record.outcome = ccallThrew ? 'threw' : record.result !== 0 ? 'returned-nonzero' : 'evidence-error';
          if (ccallThrew) {
            try { record.refreshedNativeEvidence = evidence(); record.classPieceWitness = progress.classPieceWitness; record.classPieceCaptureWitness = progress.classPieceCaptureWitness; record.lobjWriterEvidence = progress.lobjWriterEvidence; record.tobjWriterEvidence = progress.tobjWriterEvidence; }
            catch (readError) { record.evidenceRefreshError = String(readError); }
          }
          throw error;
        }
      };
      const drawBytes = () => read(module._melee_web_synctest_draw_diagnostics(), 132);
      const drawRecord = (label, bytes, boundary = null) => {
        const row = {label, bytes: bytes.length, base64: encode(bytes)};
        if (boundary) row.boundary = boundary;
        progress.drawBoundaries.push(row);
        return row;
      };
      const getDiagnostic = (value, index) => {
        const row = value.diagnostics?.[index];
        if (!row || row.index !== index || row.bytes !== 132) throw new Error(`Two-tick diagnostic slot ${index} is unavailable`);
        return Uint8Array.from(atob(row.base64), character => character.charCodeAt(0));
      };
      const decodeDraw = new Function(`return (${plan.decodeDrawSource})`)();
      const validateMapping = new Function(`return (${plan.inputMappingSource})`)();
      const motion = {interactive: 0, entry: 1, death: 2, respawn: 3, unknown: 4};
      const inspect = new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(motion, validateMapping);
      const selectedPath = new Function('validateSourceInputMapping',`return (${plan.selectedDrawSource})`)(validateMapping);
      const initialDiagnostics = decodeDraw(drawBytes());
      const inputMapping = validateMapping(plan.expectedInputMapping, initialDiagnostics.slotCount);
      const inspectAt = (observationBase64, diagBytes, beforeDiagBytes) => {
        const observation = Uint8Array.from(atob(observationBase64), character => character.charCodeAt(0));
        const diag = decodeDraw(diagBytes);
        const boundary = inspect(observation, {classifyMotion: kind => module._melee_web_synctest_motion_boundary(kind),
          isMarioKind: kind => module._melee_web_synctest_is_mario_kind(kind) === 1,
          slotCount: diag.slotCount, expectedInputMapping: inputMapping, expectedStocks: plan.expectedStocks});
        if (!boundary.interactive) throw new Error(`Two-tick sample is not interactive: ${JSON.stringify(boundary.refusalReasons)}`);
        const before = decodeDraw(beforeDiagBytes);
        const fighters = selectedPath(before, diag, boundary.players, inputMapping);
        return {boundary, fighters};
      };
      const validateBaselineSample = (value, pair, sample, expectedPad, expectedFrame, beforeDiagBytes, liveDiagBytes, expectedCommand, expectedMask) => {
        if (!pair || pair.schema !== 1 || pair.declaredBytes !== 11416 || pair.firstFailure !== 0 ||
            pair.preFrame !== expectedFrame - 1 || pair.postFrame !== expectedFrame ||
            value.lastCommand !== expectedCommand || value.state !== (sample === 300 ? 10 : 11) ||
            value.diagnosticMask !== expectedMask) throw new Error(`Two-tick sample ${sample} baseline evidence header/frame drift`);
        if (pair.baselinePad !== expectedPad || encode(read(module._melee_web_snapshot_input(), 44)) !== expectedPad ||
            encode(read(module._melee_web_snapshot_observation(), 1280)) !== pair.baselineObservation) {
          throw new Error(`Two-tick sample ${sample} live baseline PAD/observation drift`);
        }
        const result = inspectAt(pair.baselineObservation, liveDiagBytes, beforeDiagBytes);
        if (result.boundary.sourceFrame !== expectedFrame) throw new Error(`Two-tick sample ${sample} source frame drift`);
        const capturePieceSize=module._melee_web_synctest_cpu_class_piece_capture_witness_bytes();
        if(capturePieceSize!==160)throw Error('Capture-time piece ABI');
        const capturePieceBytes=read(module._melee_web_synctest_cpu_class_piece_capture_witness(),capturePieceSize);
        progress.classPieceCaptureWitnessRaw=encode(capturePieceBytes);
        progress.classPieceCaptureWitness=decodeCapturePiece(capturePieceBytes);
        const writerSize=module._melee_web_synctest_cpu_lobj_writer_evidence_bytes();
        if(writerSize!==16784)throw Error('LObj writer ABI');
        const writerBytes=read(module._melee_web_synctest_cpu_lobj_writer_evidence(),writerSize);
        progress.lobjWriterEvidenceRaw=encode(writerBytes);
        progress.lobjWriterEvidence=decodeLObjWriter(writerBytes);
        const tobjSize=module._melee_web_synctest_cpu_tobj_writer_evidence_bytes();
        if(tobjSize!==28048)throw Error('TObj writer ABI');
        const tobjBytes=read(module._melee_web_synctest_cpu_tobj_writer_evidence(),tobjSize);
        progress.tobjWriterEvidenceRaw=encode(tobjBytes);
        progress.tobjWriterEvidence=decodeTObjWriter(tobjBytes);
        return result;
      };
      if (module._melee_web_snapshot_observation_size() !== 1280 || module._melee_web_snapshot_pcm_size() !== 4272 ||
          module._melee_web_synctest_draw_diagnostics_size() !== 132) throw new Error('Fixture observation/PCM/draw ABI');
      const observed = read(module._melee_web_snapshot_observation(), 1280);
      if (new DataView(observed.buffer).getUint32(0, true) !== plan.checkpointSourceFrame ||
          encode(read(module._melee_web_snapshot_input(), 44)) !== plan.checkpointPad) {
        throw new Error('Actual sample299 live boundary drift');
      }
      const beforeCaptureDraw = drawBytes();
      const configBytes = new Uint8Array(plan.wire.length * 16), configView = new DataView(configBytes.buffer);
      plan.wire.forEach((row, index) => row.forEach((value, field) => configView.setUint32(index * 16 + field * 4, value, true)));
      const configPointer = module._malloc(configBytes.length);
      if (!configPointer) throw new Error('CPU wire allocation failed');
      try {
        if (typeof SharedArrayBuffer !== 'undefined' && module.HEAPU8.buffer instanceof SharedArrayBuffer) throw new Error('Shared Wasm memory refused');
        module.HEAPU8.set(configBytes, configPointer);
        const configured = module._melee_web_synctest_cpu_restore_configure(configPointer, plan.wire.length);
        progress.configurationResult = configured;
        if (configured !== 0) throw new Error(`CPU configuration refused ${configured}`);
      } finally { module._free(configPointer); }

      await command(8);
      const captured = evidence();
      const captureDraw = getDiagnostic(captured, 0);
      drawRecord('capture-before-baseline', captureDraw);
      if (!beforeCaptureDraw.every((byte, index) => byte === captureDraw[index])) throw new Error('CPU capture changed draw diagnostics');
      if (captured.diagnosticMask !== 1 || captured.mode !== 2 || captured.state !== 9 || captured.preFrame !== plan.checkpointSourceFrame) {
        throw new Error('CAPTURE_TWO state/frame contract missing');
      }

      await command(9);
      const after300Reference = evidence();
      const beforeDraw300 = getDiagnostic(after300Reference, 1);
      drawRecord('after-reference-cpu300-before-draw', beforeDraw300);
      if (after300Reference.diagnosticMask !== 3 || after300Reference.state !== 10 ||
          !captureDraw.every((byte, index) => beforeDraw300[index] === byte)) throw new Error('Baseline CPU300 changed draw diagnostics or state');
      const wait = new Function(`return (${plan.waitSource})`)();
      const checked = new Function(`return (${plan.checkedSource})`)();
      progress.baselineGpuStatus300 = await wait(module, 300);
      await checked(module, 'baseline native quiescence after sample 300 draw', 'melee_web_snapshot_quiescent');
      progress.gpuWaits.push({sample: 300, status: progress.baselineGpuStatus300});
      const liveDraw300 = drawBytes();
      const sample300BeforeNextTick = validateBaselineSample(after300Reference, after300Reference.pair300, 300,
        plan.literalPad300, plan.checkpointSourceFrame + 1, beforeDraw300, liveDraw300, 9, 3);
      progress.baselineSamples ??= {};
      progress.baselineSamples.sample300 = {observation: after300Reference.pair300.baselineObservation,
        pad: after300Reference.pair300.baselinePad, boundary: sample300BeforeNextTick.boundary,
        drawDiagnostics: {bytes:132,base64:encode(liveDraw300)},
        validation:'literal PAD/frame/interactive/selected-draw mapping checked before command10'};

      await command(10);
      const afterBaseline301Draw = evidence();
      const baseline300Idle = getDiagnostic(afterBaseline301Draw, 2);
      const beforeDraw301 = getDiagnostic(afterBaseline301Draw, 3);
      drawRecord('after-baseline-draw300-native-idle', baseline300Idle);
      drawRecord('after-reference-cpu301-before-draw', beforeDraw301);
      if (afterBaseline301Draw.diagnosticMask !== 15 || afterBaseline301Draw.state !== 11 ||
          !baseline300Idle.every((byte, index) => beforeDraw301[index] === byte)) throw new Error('CPU301 tick changed baseline draw300 diagnostics');
      progress.baselineGpuStatus301 = await wait(module, 301);
      await checked(module, 'baseline native quiescence after sample 301 draw', 'melee_web_snapshot_quiescent');
      progress.gpuWaits.push({sample: 301, status: progress.baselineGpuStatus301});
      const liveDraw301 = drawBytes();
      const sample301BeforeRestore = validateBaselineSample(afterBaseline301Draw, afterBaseline301Draw.pair301, 301,
        plan.literalPad301, plan.checkpointSourceFrame + 2, beforeDraw301, liveDraw301, 10, 15);
      progress.baselineSamples.sample301 = {observation: afterBaseline301Draw.pair301.baselineObservation,
        pad: afterBaseline301Draw.pair301.baselinePad, boundary: sample301BeforeRestore.boundary,
        drawDiagnostics: {bytes:132,base64:encode(liveDraw301)},
        validation:'literal PAD/frame/interactive/selected-draw mapping checked before command11'};

      await command(11);
      const corrected = evidence();
      const baseline301Idle = getDiagnostic(corrected, 4);
      const correction300 = getDiagnostic(corrected, 5);
      const correction301 = getDiagnostic(corrected, 6);
      drawRecord('after-baseline-draw301-native-idle', baseline301Idle);
      drawRecord('after-correction-cpu300', correction300);
      drawRecord('after-correction-cpu301', correction301);
      if (corrected.diagnosticMask !== 127 || corrected.state !== 12 ||
          !baseline301Idle.every((byte, index) => correction300[index] === byte) ||
          !baseline301Idle.every((byte, index) => correction301[index] === byte)) {
        throw new Error('No-draw correction ticks changed draw diagnostics or state');
      }

      await command(12);
      progress.finalGpuStatus = await wait(module, 301);
      await checked(module, 'final native quiescence after one present', 'melee_web_snapshot_quiescent');
      progress.gpuWaits.push({sample: 'final-present', status: progress.finalGpuStatus});
      await command(13);
      const finalEvidence = evidence();
      validateFinal(finalEvidence, plan.checkpointSourceFrame);
      const finalDraw = getDiagnostic(finalEvidence, 7);
      drawRecord('after-final-present-native-idle', finalDraw);
      if (finalEvidence.state !== 5 || finalEvidence.lastCommand !== 13 || finalEvidence.diagnosticMask !== 255) {
        throw new Error('COMPLETE_TWO endpoint state contract missing');
      }
      const slotCount = new DataView(finalDraw.buffer, finalDraw.byteOffset, finalDraw.byteLength).getUint32(0, true);
      const b0 = getDiagnostic(finalEvidence, 0), b2 = getDiagnostic(finalEvidence, 2),
        b4 = getDiagnostic(finalEvidence, 4), b6 = getDiagnostic(finalEvidence, 6), b7 = getDiagnostic(finalEvidence, 7);
      const sample300 = inspectAt(finalEvidence.pair300.baselineObservation, b2, b0);
      const sample301 = inspectAt(finalEvidence.pair301.baselineObservation, b4, b2);
      const final = inspectAt(finalEvidence.drawnPair301.replayObservation, b7, b6);
      if (sample300.boundary.sourceFrame !== plan.checkpointSourceFrame + 1 ||
          sample301.boundary.sourceFrame !== plan.checkpointSourceFrame + 2 ||
          final.boundary.sourceFrame !== plan.checkpointSourceFrame + 2) throw new Error('Two-tick source frame sequence drift');
      progress.drawBoundaries.find(row => row.label === 'after-baseline-draw300-native-idle').boundary = sample300.boundary;
      progress.drawBoundaries.find(row => row.label === 'after-baseline-draw301-native-idle').boundary = sample301.boundary;
      progress.drawBoundaries.find(row => row.label === 'after-final-present-native-idle').boundary = final.boundary;

      const observation = read(module._melee_web_snapshot_observation(), 1280);
      const pcm = read(module._melee_web_snapshot_pcm(), 4272);
      const pad = read(module._melee_web_snapshot_input(), 44);
      if (encode(observation) !== finalEvidence.drawnPair301.replayObservation ||
          encode(pcm) !== finalEvidence.drawnPair301.replayPcm || encode(pad) !== finalEvidence.drawnPair301.replayPad ||
          encode(drawBytes()) !== encode(finalDraw)) throw new Error('Live postdraw boundary differs from strict typed endpoint');
      if (encode(getDiagnostic(finalEvidence, 0)) !== encode(beforeCaptureDraw) ||
          finalEvidence.pair300.baselinePad !== 'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=' ||
          finalEvidence.pair301.baselinePad !== 'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=') {
        throw new Error('Two-tick original literal input or capture boundary drift');
      }
      progress.rawSamples = {
        sample300: {observation: finalEvidence.pair300.baselineObservation, pcm: finalEvidence.pair300.baselinePcm,
          pad: finalEvidence.pair300.baselinePad, boundary: sample300.boundary},
        sample301: {observation: finalEvidence.pair301.baselineObservation, pcm: finalEvidence.pair301.baselinePcm,
          pad: finalEvidence.pair301.baselinePad, boundary: sample301.boundary},
      };
      progress.status = 'drawn-reference-no-draw-two-tick-component-complete';
      return {observation: encode(observation), pcm: encode(pcm), pad: encode(pad), drawDiagnostics: encode(finalDraw),
        boundary: final.boundary, status: progress.finalGpuStatus, cpuRestore: progress};
    }, plan);
  } catch (error) {
    try { error.cpuRestorePartial = await page.evaluate(() => window.__slippiSynctest?.cpuRestoreProgress || null); }
    catch (readError) { error.cpuRestorePartialReadError = String(readError); }
    throw error;
  }
}
