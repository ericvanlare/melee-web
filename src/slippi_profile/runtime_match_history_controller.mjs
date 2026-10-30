// SPDX-License-Identifier: GPL-3.0-or-later
// Private one-tick ordinary-runtime owner. Commands execute on the native
// owner's rAF callback; this transport neither polls PAD nor performs source work.
import {collectCpuEndpointInPage} from './tests/cpu_endpoint.mjs';

export const MATCH_HISTORY_COMMAND=Object.freeze({capture:1,begin:2,forward:3,present:4,
  idle:5,restore:6,correct:7,close:8,retire:9,cleanup:10});
const PREFIX='_melee_web_native_menu_match_history_';
const STATUS_BYTES=128,INPUT_BYTES=168,PAD_BYTES=11;
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

export function decodeRuntimeMatchHistoryStatus(bytes){
  if(!(bytes instanceof Uint8Array)||bytes.length!==STATUS_BYTES)throw Error('Runtime history status extent');
  const data=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const result=fields(data,statusNames),metadata=fields(data,metadataNames,48);
  metadata.pollSerial=data.getBigUint64(96,true).toString();
  metadata.preAudioPhase=data.getUint32(104,true);metadata.currentAudioPhase=data.getUint32(108,true);
  Object.assign(result,{metadata,...fields(data,['inputReady','gpuPending','endpointActive','rendererErrors'],112)});
  if(result.schema!==1||result.bytes!==STATUS_BYTES||result.reserved!==0||result.ownerState>2||
    result.pendingCommand>10||result.lastCompletedCommand>10||result.completeSequence>result.requestSequence||
    result.inputReady>1||result.gpuPending>1||result.endpointActive>1||
    metadata.schema!==1||metadata.bytes!==64||metadata.phase>20||metadata.restoreCount>1||
    metadata.forwardSteps>1||metadata.replaySteps>1)
    throw Error('Runtime history status schema or bounded-owner violation');
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
  let busy=false,failed=null,terminal=false,captured=false;
  const journal=[];
  const read=(pointer,size)=>{
    if(!Number.isInteger(pointer)||pointer<-0x80000000||pointer>0xffffffff)throw Error('Native history pointer');
    const start=pointer>>>0,heap=module.HEAPU8;
    if(typeof SharedArrayBuffer!=='undefined'&&heap.buffer instanceof SharedArrayBuffer)
      throw Error('Private history refuses shared memory');
    if(!start||!Number.isSafeInteger(size)||size<=0||start+size>heap.length)throw Error('Native history borrowed extent');
    return heap.slice(start,start+size);
  };
  const status=()=>decodeRuntimeMatchHistoryStatus(read(module[PREFIX+'status'](),STATUS_BYTES));
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
  const requireSuccess=(name,...args)=>{
    if(module[PREFIX+name](...args)!==1)throw latch(Error(`Native history ${name} refused: ${errorText()}`));
  };
  let padArgument=0,padArgumentStart=0,padArgumentReleased=false;
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
    module._free(padArgument);
    padArgument=0;padArgumentStart=0;padArgumentReleased=true;
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
      const strings=['runtime_sha256','asset_identity_sha256','save_profile_sha256','profile_identity'];
      if(!host||strings.some(key=>typeof host[key]!=='string'||!host[key]||host[key].includes('\0')))
        throw Error('Runtime history identity requires explicit loaded identities');
      const encoded=strings.map(key=>new TextEncoder().encode(host[key]+'\0'));
      const invoke=(index,pointers)=>index===encoded.length
        ?module[PREFIX+'identity_json'](...pointers)
        :allocate(encoded[index],pointer=>invoke(index+1,[...pointers,pointer]));
      const pointer=invoke(0,[]);if(!pointer)throw latch(Error(`Native match identity refused: ${errorText()}`));
      const text=module.UTF8ToString(pointer),identity=JSON.parse(text);
      if(strings.some(key=>identity[key]!==host[key]))throw latch(Error('Native match identity did not bind supplied host identities'));
      return text;
    },
    validatePad(value){
      active();const bytes=pad(value);const pointer=writePadArgument(bytes);
      return module._melee_web_net_validate_pad(pointer,PAD_BYTES)===1;
    },
    remotePad(value){active();const bytes=pad(value);const pointer=writePadArgument(bytes);
      requireSuccess('remote_pad',pointer,PAD_BYTES);},
    input(){
      active();const snapshot=check(status());
      const result=decodeRuntimeMatchHistoryInput(read(module[PREFIX+'input'](),INPUT_BYTES));
      if(result.pollSerial!==snapshot.metadata.pollSerial||result.localPort!==snapshot.localPort||result.remotePort!==snapshot.remotePort)
        throw latch(Error('Native input receipt differs from its live owner'));
      return result;
    },
    endpoint(request){active();check(status());return collectCpuEndpointInPage(request,{runtime:'match-history',module});},
    motor(){active();const size=module[PREFIX+'motor_bytes']();if(size!==1016)throw Error('Runtime motor evidence ABI');return read(module[PREFIX+'motor_data'](),size);},
    motorCommit(){active();const size=module[PREFIX+'motor_commit_bytes']();if(size!==64)throw Error('Runtime motor commit ABI');return read(module[PREFIX+'motor_commit_data'](),size);},
    pcm(){active();const size=module[PREFIX+'pcm_bytes']();if(!Number.isInteger(size)||size<8||size>1068*4||size%8)throw Error('Runtime emitted stereo PCM extent');return read(module[PREFIX+'pcm_data'](),size);},
    async command(name){
      const command=MATCH_HISTORY_COMMAND[name];if(!command)throw Error('Unknown runtime history command');
      if(name!=='cleanup')active();else if(terminal||busy)throw Error('Runtime history cleanup owner unavailable');
      busy=true;const started=performance.now(),deadline=started+commandTimeoutMs;
      const entry={command,name,started,status:'pending'};journal.push(entry);
      try{
        const allowFailure=name==='cleanup';
        const before=await wait(value=>value.pendingCommand===0&&
          (!['idle','cleanup'].includes(name)||value.gpuPending===0),deadline,name,allowFailure);
        const next=before.requestSequence+1;
        requireSuccess('request',command);
        const after=await wait(value=>value.pendingCommand===0&&value.completeSequence===next,deadline,name,allowFailure);
        if(after.requestSequence!==next||after.lastCompletedCommand!==command)
          throw Error('Native command completion sequence mismatch');
        if(name==='cleanup'){
          if(after.ownerState!==2)throw Error('Native cleanup did not retire the owner');
          releasePadArgument();terminal=true;
        }
        if(name==='capture')captured=true;
        entry.status='complete';entry.after=after;entry.elapsedMs=performance.now()-started;return after;
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
