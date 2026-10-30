// SPDX-License-Identifier: GPL-3.0-or-later
// Receive-only diagnostic input authority over the existing local A2 room relay.
// This adapter owns Node sockets and never invokes browser/native execution.
import {createHash} from 'node:crypto';
import {LockstepPeer,LOCKSTEP_VERSION,lockstepConstants} from '../../../scripts/net_lockstep_protocol.mjs';
import {createTransportCallbackQueue,openLockstepPeerPair} from '../../../scripts/net_lockstep_transport.mjs';
import {validateSourceInputMapping} from './runner_contract.mjs';

export const CPU_CORRECTED_LOOP_PACKET_WAIT_MS=3000;
export const CPU_CORRECTED_LOOP_PEER_TIMEOUT_MS=5000;
export const CPU_CORRECTED_LOOP_PACKET_ROWS=44;
export const CPU_CORRECTED_LOOP_SOURCE_TICKS=46;
export const CPU_HISTORY_PACKET_ROWS=4;
export const CPU_HISTORY_SOURCE_TICKS=6;
const PAD_BYTES=11,FRAME_BYTES=44;
const PACKET_MODES=Object.freeze({
  correctedLoop:Object.freeze({scope:'cpu-corrected-loop-packet-input-v1',first:300,last:343,
    rows:CPU_CORRECTED_LOOP_PACKET_ROWS,sourceTicks:CPU_CORRECTED_LOOP_SOURCE_TICKS,
    predictedDrawCommand:40,summaryScope:'bounded packet receive component; Node-held room WebSocket peers'}),
  retainedHistory:Object.freeze({scope:'cpu-history-packet-input-v1',first:300,last:303,
    rows:CPU_HISTORY_PACKET_ROWS,sourceTicks:CPU_HISTORY_SOURCE_TICKS,
    predictedDrawCommand:67,
    summaryScope:'four-row retained-PRE packet component; Node-held room WebSocket peers'}),
});
const sha256=value=>createHash('sha256').update(value).digest('hex');
const ns=()=>process.hrtime.bigint();
const asBuffer=value=>Buffer.from(value);
const padAt=(frame,port)=>frame.subarray(port*PAD_BYTES,(port+1)*PAD_BYTES);

function exactPad(value,label){
  const bytes=Buffer.from(value);
  if(bytes.length!==PAD_BYTES)throw new Error(`${label} must contain exactly 11 PAD bytes`);
  return bytes;
}

function decodeContribution(value,label){
  if(typeof value!=='string'||!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    throw new Error(`${label} is not canonical base64`);
  const bytes=Buffer.from(value,'base64');
  if(bytes.length!==PAD_BYTES||bytes.toString('base64')!==value)
    throw new Error(`${label} must encode exactly 11 PAD bytes`);
  return bytes;
}

function event(value){return {monotonicNs:value.toString()};}

function deadlinePromise(timeoutMs,label){
  let timer;
  const promise=new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(`${label} exceeded ${timeoutMs} ms`)),timeoutMs);});
  return {promise,clear:()=>clearTimeout(timer)};
}

function checkedAgreementMapping(mapping){
  try{
    const checked=validateSourceInputMapping(mapping,2);
    if(JSON.stringify(checked)!==JSON.stringify(mapping))return false;
    return checked.every((row,index)=>row.sourceSlot===index&&row.rawController===index);
  }catch{return false;}
}

function mappingFromInitializer(initializerText,inputMapping,slotCount){
  if(typeof initializerText!=='string'||!initializerText.length)
    throw new Error('Corrected-loop packet identity requires the serialized diagnostic initializer');
  let initializer;
  try{initializer=JSON.parse(initializerText);}
  catch{throw new Error('Corrected-loop packet initializer is not valid JSON');}
  if(!initializer||!Array.isArray(initializer.players)||initializer.players.length!==2)
    throw new Error('Corrected-loop packet initializer must contain its two actual players');
  const declared=validateSourceInputMapping(inputMapping,slotCount);
  const observed=validateSourceInputMapping(initializer.players.map((player,sourceSlot)=>({
    sourceSlot,initializerPort:player?.port,rawController:player?.controller,
  })),slotCount);
  if(JSON.stringify(declared)!==JSON.stringify(observed))
    throw new Error('Corrected-loop packet mapping differs from the parsed diagnostic initializer');
  return observed;
}

function packetModeForAgreement(agreement){
  const mode=Object.values(PACKET_MODES).find(row=>row.scope===agreement?.scope);
  if(!mode||agreement.samples?.first!==mode.first||agreement.samples?.last!==mode.last)
    throw new Error('Corrected-loop A2 start identity is not bound to the diagnostic producer/profile');
  return mode;
}

function expectedAgreement(agreement){
  const mode=packetModeForAgreement(agreement);
  if(!agreement||agreement.protocol!=='melee-web-local-lockstep-a2-v1'||
     !agreement.producer||!/^[0-9a-f]{40}$/.test(agreement.producer.commit||'')||
     !/^[0-9a-f]{40}$/.test(agreement.producer.tree||'')||
     !agreement.profile||typeof agreement.profile.name!=='string'||!agreement.profile.name.length||
     typeof agreement.profile.identity!=='string'||!agreement.profile.identity.length||
     typeof agreement.profile.sourceIdentity!=='string'||!agreement.profile.sourceIdentity.length||
     typeof agreement.profile.rngHelper!=='string'||!agreement.profile.rngHelper.length||
     typeof agreement.profile.stageBridge!=='string'||!agreement.profile.stageBridge.length||
     !Number.isSafeInteger(agreement.profile.seed)||
     !/^[0-9a-f]{64}$/.test(agreement.profile.initializerSha256||'')||
     !/^[0-9a-f]{64}$/.test(agreement.profile.configurationIdentitySha256||'')||
     !/^[0-9a-f]{64}$/.test(agreement.profile.wasmSha256||'')||
     !agreement.worker?.identity||!agreement.worker?.hashes||
     !checkedAgreementMapping(agreement.profile.inputMapping))
    throw new Error('Corrected-loop A2 start identity is not bound to the diagnostic producer/profile');
  return agreement;
}

function buildPacketAgreement({producer,profile,initializer,inputMapping,
  slotCount=2,sourceStateManifest,runtimeSha256,harnessSha256,worker}={},mode){
  if(!producer||!profile||typeof initializer!=='string'||!Array.isArray(inputMapping)||
     !Number.isInteger(slotCount)||slotCount<2||!sourceStateManifest||
     !runtimeSha256||!harnessSha256||!worker)
    throw new Error('Corrected-loop packet identity requires producer, profile, initializer, mapping, manifest, and runtime evidence');
  const observedMapping=mappingFromInitializer(initializer,inputMapping,slotCount);
  const agreement={protocol:'melee-web-local-lockstep-a2-v1',
    scope:mode.scope,samples:{first:mode.first,last:mode.last},
    producer:{commit:producer.commit,tree:producer.tree},
    profile:{name:profile.name,identity:profile.identity,sourceIdentity:profile.sourceIdentity,
      rngHelper:profile.rngHelper,stageBridge:profile.stageBridge,seed:profile.seed,
      initializerSha256:sha256(Buffer.from(initializer,'utf8')),
      configurationIdentitySha256:sourceStateManifest.configurationIdentitySha256,
      wasmSha256:sourceStateManifest.wasmSha256,
      runtimeSha256:{...runtimeSha256},harnessSha256:{...harnessSha256},
      inputMapping:observedMapping},
    worker:{identity:{...worker.identity},hashes:{...worker.hashes}}};
  return expectedAgreement(agreement);
}

/** Construct the exact legacy 44-row hello agreement from diagnostic/profile evidence. */
export function buildCpuCorrectedLoopPacketAgreement(input={}){
  return buildPacketAgreement(input,PACKET_MODES.correctedLoop);
}

/** Construct the distinct four-row agreement used by the retained-PRE history witness. */
export function buildCpuHistoryPacketAgreement(input={}){
  return buildPacketAgreement(input,PACKET_MODES.retainedHistory);
}

export class CpuCorrectedLoopPacketInput {
  constructor(pair,agreement,{now=ns}={}){
    this.pair=pair;this.agreement=expectedAgreement(agreement);this.mode=packetModeForAgreement(agreement);this.now=now;
    this.failure=null;this.closed=false;this.intentionalClose=false;this.closePromise=null;
    this.active=null;this.lastCompletedSample=this.mode.first-1;this.inbox=new Map();
    this.receivedHistory=new Map();
    this.completedP0=new Map();this.completedP1=new Map();this.receipts=[];this.frameRows={alpha:[],beta:[]};
    this.waiters=new Set();this.callbackErrors=[];this.peers={};
    this.callbackQueue=createTransportCallbackQueue(({role,kind,error})=>{
      this.callbackErrors.push({role,kind,message:String(error?.stack||error?.message||error)});
      this.#latch(Error(`${role} ${kind} callback failed: ${String(error?.message||error)}`));
    });
    const onTerminal=(role)=>(terminal)=>{
      if(!this.intentionalClose)this.#latch(Error(`${role} lockstep peer reached terminal state: ${JSON.stringify(terminal)}`));
    };
    const pushFrame=role=>(firstTick,bytes)=>this.#captureFrames(role,firstTick,bytes);
    this.peers.alpha=new LockstepPeer({role:'alpha',sourceTicks:this.mode.sourceTicks,
      inputTicks:this.mode.rows,pushFrame:pushFrame('alpha'),onTerminal:onTerminal('alpha')});
    this.peers.beta=new LockstepPeer({role:'beta',sourceTicks:this.mode.sourceTicks,
      inputTicks:this.mode.rows,pushFrame:pushFrame('beta'),onTerminal:onTerminal('beta')});
    for(const role of ['alpha','beta']){
      const endpoint=pair[role],peer=this.peers[role];
      peer.attach(text=>endpoint.send(text));
      endpoint.onMessage(text=>{
        const callbackEntryNs=this.now();
        return this.callbackQueue.track(role,'message',()=>this.#receive(role,text,callbackEntryNs));
      });
    }
  }

  #latch(error){
    this.failure??=error instanceof Error?error:Error(String(error));
    for(const wake of [...this.waiters])wake();
  }

  #wake(){for(const wake of [...this.waiters])wake();}

  #captureFrames(role,firstTick,input){
    const bytes=Buffer.from(input),peer=this.peers[role];
    if(!Number.isSafeInteger(firstTick)||firstTick<0||bytes.length===0||bytes.length%FRAME_BYTES!==0)
      throw new Error(`${role} protocol frame callback has an invalid range`);
    const count=bytes.length/FRAME_BYTES;
    if(firstTick+count>this.mode.sourceTicks)
      throw new Error(`${role} protocol emitted source frames beyond the diagnostic bound`);
    const prior=this.frameRows[role];
    if(firstTick!==prior.length)throw new Error(`${role} protocol source-frame callback is not contiguous`);
    const neutral=Buffer.from(lockstepConstants.neutralPad,'hex');
    const noController=Buffer.from(lockstepConstants.noControllerPad,'hex');
    for(let offset=0;offset<count;offset++){
      const sourceTick=firstTick+offset,frame=bytes.subarray(offset*FRAME_BYTES,(offset+1)*FRAME_BYTES);
      const expected=Buffer.alloc(FRAME_BYTES);
      if(sourceTick<2){expected.set(neutral,0);expected.set(neutral,PAD_BYTES);}
      else{
        const inputTick=sourceTick-2,local=peer.nativeLocal.get(inputTick),remote=peer.remote.get(inputTick);
        if(!local||!remote)throw new Error(`${role} protocol frame ${sourceTick} lacks both A2 input contributions`);
        expected.set(local,peer.localPort*PAD_BYTES);expected.set(remote,peer.remotePort*PAD_BYTES);
      }
      expected.set(noController,2*PAD_BYTES);expected.set(noController,3*PAD_BYTES);
      if(!frame.equals(expected))throw new Error(`${role} protocol frame ${sourceTick} differs from A2 PAD ownership`);
      prior.push({sourceTick,sha256:sha256(frame),inputTick:sourceTick<2?null:sourceTick-2,
        preRoll:sourceTick<2,localPort:peer.localPort,remotePort:peer.remotePort});
    }
    this.#wake();
  }

  #expectedInput(role,tick){
    if(this.active?.expected[role].has(tick))return this.active.expected[role].get(tick);
    if(role==='alpha'&&this.completedP1.has(tick))return this.completedP1.get(tick);
    if(role==='beta'&&this.completedP0.has(tick))return this.completedP0.get(tick);
    return null;
  }

  #preflight(role,text){
    if(typeof text!=='string'||text.length>1024*1024)throw new Error('A2 callback was not a bounded UTF-8 text packet');
    let packet;try{packet=JSON.parse(text);}catch{throw new Error('A2 callback JSON is malformed');}
    if(packet?.type!=='state')return packet;
    const peer=this.peers[role];
    if(packet.version!==LOCKSTEP_VERSION||packet.role!==peer.remoteRole)
      throw new Error('A2 state packet identity differs from its receiving peer');
    if(!Number.isSafeInteger(packet.sequence)||packet.sequence<0||packet.sequence>0xffffffff)
      throw new Error('A2 state packet sequence is invalid');
    if(packet.sequence>=peer.expectedSequence&&packet.sequence-peer.expectedSequence>lockstepConstants.window)
      throw new Error('A2 state packet is outside the existing bounded reorder window');
    if(packet.ack_sequence!==null&&(!Number.isSafeInteger(packet.ack_sequence)||packet.ack_sequence<0||
       packet.ack_sequence>=peer.nextSequence))
      throw new Error('A2 state packet acknowledges an unpublished sequence');
    if(packet.ack_input!==null&&(!Number.isSafeInteger(packet.ack_input)||packet.ack_input<0||
       packet.ack_input>peer.localContiguousInput))
      throw new Error('A2 state packet acknowledges unpublished input');
    if(packet.ack_checksum!==null||!Array.isArray(packet.delayed_checksums)||packet.delayed_checksums.length)
      throw new Error('Corrected-loop A2 packet unexpectedly carries checksum traffic');
    const list=packet.unacknowledged;
    if(!Array.isArray(list)||list.length>32)throw new Error('A2 input packet exceeded the existing bounded batch');
    const seen=new Set();
    for(const entry of list){
      const tick=entry?.tick;
      if(!Number.isSafeInteger(tick)||tick<0||tick>=this.mode.rows||seen.has(tick))
        throw new Error(`A2 input packet has an out-of-range or repeated tick ${tick}`);
      seen.add(tick);
      const expected=this.#expectedInput(role,tick);
      if(!expected)throw new Error(`A2 input packet delivered unrequested input tick ${tick}`);
      const actual=decodeContribution(entry.pad,`A2 input tick ${tick}`);
      if(!actual.equals(expected))throw new Error(`A2 input tick ${tick} differs from its immutable recipe contribution`);
      const old=this.peers[role].remote.get(tick);
      if(old&&!Buffer.from(old).equals(actual))throw new Error(`A2 input tick ${tick} conflicts with accepted peer history`);
      if(tick>peer.remoteContiguousInput+lockstepConstants.window+1)
        throw new Error(`A2 input tick ${tick} is outside the existing bounded reorder window`);
    }
    if(role==='alpha'&&packet.role==='beta'){
      const fresh=new Set();
      for(const entry of list){
        const old=this.receivedHistory.get(entry.tick)??this.inbox.get(entry.tick);
        if(old)continue;
        const active=this.active,first=active?active.start-this.mode.first:-1,last=active?active.end-this.mode.first:-1;
        if(!active||entry.tick<first||entry.tick>last)
          throw new Error(`A2 receive tick ${entry.tick} has no active capsule authority`);
        fresh.add(entry.tick);
      }
      if(this.inbox.size+fresh.size>(this.active?.span??0)||this.inbox.size+fresh.size>7)
        throw new Error('Corrected-loop pending PAD inbox would exceed the active capsule bound');
    }
    return packet;
  }

  async #receive(role,text,callbackEntryNs){
    if(this.failure)throw this.failure;
    let packet;
    try{packet=this.#preflight(role,text);}
    catch(error){
      this.#latch(error);
      try{await this.peers[role].fail('protocol',{reason:String(error.message||error)});}
      catch(terminalError){this.callbackErrors.push({role,kind:'preflight-terminal',message:String(terminalError)});}
      throw error;
    }
    try{await this.peers[role].receive(text);}
    catch(error){this.#latch(error);throw error;}
    if(this.peers[role].terminal){
      const error=Error(`${role} A2 peer became terminal while receiving a packet`);this.#latch(error);throw error;
    }
    if(role==='alpha'&&packet?.type==='state'&&packet.role==='beta'&&packet.unacknowledged.length){
      const staged=[];
      for(const entry of packet.unacknowledged){
        const bytes=decodeContribution(entry.pad,`A2 input tick ${entry.tick}`);
        const accepted=this.peers.alpha.remote.get(entry.tick);
        if(!accepted||!Buffer.from(accepted).equals(bytes)){
          const error=Error(`A2 receive tick ${entry.tick} was not atomically represented in the peer map`);
          this.#latch(error);throw error;
        }
        const old=this.receivedHistory.get(entry.tick)??this.inbox.get(entry.tick);
        if(old&&!old.equals(bytes)){
          const error=Error(`A2 receive tick ${entry.tick} conflicts with the bounded PAD inbox`);
          this.#latch(error);throw error;
        }
        if(!old){
          const active=this.active,first=active?active.start-this.mode.first:-1,last=active?active.end-this.mode.first:-1;
          if(!active||entry.tick<first||entry.tick>last){
            const error=Error(`A2 receive tick ${entry.tick} has no active capsule authority`);
            this.#latch(error);throw error;
          }
          staged.push([entry.tick,Buffer.from(accepted)]);
        }
      }
      if(this.inbox.size+staged.length>(this.active?.span??0)||this.inbox.size+staged.length>7){
        const error=Error('Corrected-loop pending PAD inbox would exceed the active capsule bound');
        this.#latch(error);throw error;
      }
      // Commit only after the entire packet has passed protocol and scope checks.
      for(const [tick,bytes] of staged){
        this.inbox.set(tick,bytes);
        this.active?.receiveEvents.push({inputTick:tick,callbackEntryNs:callbackEntryNs.toString(),
          acceptedNs:this.now().toString(),sha256:sha256(bytes)});
      }
    }
    this.#wake();
  }

  async start(){
    let startResolve;const started=new Promise(resolve=>{startResolve=resolve;});
    const markReady=()=>{if(this.peers.alpha.ready&&this.peers.beta.ready)startResolve();};
    for(const peer of Object.values(this.peers))peer.onReady=markReady;
    const tasks=Object.values(this.peers).map(peer=>peer.start(this.agreement));
    const deadline=deadlinePromise(CPU_CORRECTED_LOOP_PEER_TIMEOUT_MS,'A2 peer hello agreement');
    try{await Promise.race([Promise.all(tasks).then(()=>started),deadline.promise]);}
    finally{deadline.clear();}
    if(this.failure)throw this.failure;
    await this.#waitUntil(()=>Object.values(this.frameRows).every(rows=>rows.length>=2),
      CPU_CORRECTED_LOOP_PEER_TIMEOUT_MS,'two neutral protocol pre-roll frames');
    for(const role of ['alpha','beta']){
      const rows=this.frameRows[role];
      if(rows.length!==2||rows.some((row,index)=>row.sourceTick!==index||!row.preRoll))
        throw new Error(`${role} did not capture exactly two neutral A2 pre-roll frames`);
    }
    return this.summary();
  }

  prepareCapsule(capsule){
    if(this.failure)throw this.failure;
    if(this.closed||this.active)throw new Error('Corrected-loop packet capsule preparation is stale or overlapping');
    const start=capsule?.startSample,rows=capsule?.rows,span=capsule?.span;
    if(!Number.isSafeInteger(start)||!Number.isSafeInteger(span)||span<1||span>7||
       !Array.isArray(rows)||rows.length!==span||start!==this.lastCompletedSample+1||
       start+span-1>this.mode.last)
      throw new Error('Corrected-loop packet capsule is not the next bounded input range');
    const expected={alpha:new Map(),beta:new Map()},localP0=[],localP1=[];
    for(let index=0;index<span;index++){
      const sample=start+index,tick=sample-this.mode.first,frame=Buffer.from(rows[index],'base64');
      if(frame.length!==FRAME_BYTES||frame[32]!==0xff||frame[43]!==0xff)
        throw new Error(`Corrected-loop packet sample ${sample} is not an exact PAD44`);
      const p0=Buffer.from(padAt(frame,0)),p1=Buffer.from(padAt(frame,1));
      expected.alpha.set(tick,p1);expected.beta.set(tick,p0);
      localP0.push([tick,p0]);localP1.push([tick,p1]);
    }
    this.active={start,span,end:start+span-1,kind:capsule.kind,expected,localP0,localP1,
      receiveEvents:[],publishRequestedNs:null,predictedDrawResolvedNs:null,completed:false};
    return {startSample:start,span,kind:capsule.kind};
  }

  async publishCapsule(capsule,{predictedDrawRequestNs=null}={}){
    if(this.failure)throw this.failure;
    const active=this.active;
    if(!active||active.start!==capsule?.startSample||active.span!==capsule?.span||active.completed||
       this.lastCompletedSample+1!==active.start)
      throw new Error('Corrected-loop packet publication does not match its active capsule');
    const speculative=String(active.kind||'').startsWith('speculative');
    if(speculative!== (predictedDrawRequestNs!==null))
      throw new Error(`Speculative packet publication must begin at command ${this.mode.predictedDrawCommand}; confirmed publication must precede native input`);
    if(active.publishRequestedNs!==null)throw new Error('Corrected-loop packet capsule was published twice');
    active.publishRequestedNs=(predictedDrawRequestNs??this.now()).toString();
    const published=await Promise.all([
      this.peers.alpha.addLocalInputs(active.localP0),
      this.peers.beta.addLocalInputs(active.localP1),
    ]);
    if(published.some(row=>row?.status==='cancelled_unpublished'))
      throw new Error('Corrected-loop A2 local input contribution was not published');
    await this.#waitUntil(()=>{
      const first=active.start-this.mode.first,last=active.end-this.mode.first;
      return active.receiveEvents.length>=active.span&&this.peers.alpha.remoteContiguousInput>=last&&
        this.peers.beta.remoteContiguousInput>=last&&this.peers.alpha.remoteAckInput>=last&&
        this.peers.beta.remoteAckInput>=last&&
        active.receiveEvents.every(row=>Number(row.inputTick)>=first&&Number(row.inputTick)<=last);
    },CPU_CORRECTED_LOOP_PACKET_WAIT_MS,`actual A2 rows and reciprocal ACKs for samples ${active.start}..${active.end}`);
    const first=active.start-this.mode.first,last=active.end-this.mode.first;
    for(let tick=first;tick<=last;tick++){
      if(!this.inbox.has(tick)||!this.peers.alpha.remote.has(tick)||!this.peers.beta.remote.has(tick))
        throw new Error(`Corrected-loop A2 authority row ${tick+this.mode.first} is incomplete`);
    }
    return this.#capsuleReceipt(active);
  }

  notePredictedDrawResolved(sample,{resolvedNs=this.now()}={}){
    if(!this.active||this.active.start!==sample||this.active.publishRequestedNs===null)
      throw new Error(`Corrected-loop command-${this.mode.predictedDrawCommand} result has no matching packet release`);
    this.active.predictedDrawResolvedNs=resolvedNs.toString();
  }

  async drainCapsule(capsule){
    const active=this.active;
    if(!active||active.start!==capsule?.startSample||active.span!==capsule?.span||
       !active.publishRequestedNs)
      throw new Error('Corrected-loop owner drain does not match a published capsule');
    const drained=await Promise.allSettled([
      this.pair.alpha.drainInbound(CPU_CORRECTED_LOOP_PACKET_WAIT_MS),
      this.pair.beta.drainInbound(CPU_CORRECTED_LOOP_PACKET_WAIT_MS),
    ]);
    const failures=drained.filter(row=>row.status==='rejected').map(row=>row.reason);
    const callbackFailures=await this.callbackQueue.drain();
    if(failures.length||callbackFailures.length||this.failure)
      throw new AggregateError([...failures,...callbackFailures.map(row=>row.error),this.failure].filter(Boolean),
        'Corrected-loop A2 receive queue failed owner-safe drain');
    const acceptedRows=[];
    for(let tick=active.start-this.mode.first;tick<=active.end-this.mode.first;tick++){
      if(!this.inbox.has(tick))throw new Error(`Corrected-loop A2 inbox lacks sample ${tick+this.mode.first}`);
      if(this.receivedHistory.has(tick))throw new Error(`Corrected-loop received PAD history repeats sample ${tick+this.mode.first}`);
      acceptedRows.push([tick,Buffer.from(this.inbox.get(tick))]);
    }
    active.completed=true;this.lastCompletedSample=active.end;
    for(const [tick,bytes] of active.localP0)this.completedP0.set(tick,Buffer.from(bytes));
    for(const [tick,accepted] of acceptedRows){
      this.receivedHistory.set(tick,accepted);
      this.completedP1.set(tick,Buffer.from(accepted));
      this.inbox.delete(tick);
    }
    const receipt=this.#capsuleReceipt(active);this.receipts.push(receipt);this.active=null;
    this.#wake();return receipt;
  }

  getRemotePad(sample){
    const tick=sample-this.mode.first;
    if(!Number.isSafeInteger(sample)||sample<this.mode.first||sample>this.mode.last||
       sample>this.lastCompletedSample||!this.receivedHistory.has(tick))
      throw new Error(`Corrected-loop remote PAD ${sample} has not passed owner-safe packet drain`);
    return Buffer.from(this.receivedHistory.get(tick));
  }

  #capsuleReceipt(active){
    const first=active.start-this.mode.first,last=active.end-this.mode.first;
    const rows=[];
    for(let tick=first;tick<=last;tick++){
      const bytes=this.receivedHistory.get(tick)??this.inbox.get(tick);
      if(bytes)rows.push({sample:tick+this.mode.first,base64:bytes.toString('base64'),sha256:sha256(bytes)});
    }
    const drawRequestNs=active.publishRequestedNs===null?null:active.publishRequestedNs;
    const drawResolvedNs=active.predictedDrawResolvedNs;
    const overlapping=rows.some(row=>active.receiveEvents.some(received=>
      received.inputTick===row.sample-this.mode.first&&drawRequestNs!==null&&drawResolvedNs!==null&&
      BigInt(received.callbackEntryNs)>=BigInt(drawRequestNs)&&BigInt(received.callbackEntryNs)<=BigInt(drawResolvedNs)));
    const roomRelay=this.pair.transport?.type==='room-websocket';
    const receipt={transport:roomRelay?'room-websocket-local-worker':this.pair.transport?.type??'unknown',
      socketOwner:roomRelay?'Node peer adapter; browser pages are unchanged':'test-only endpoint adapter; no browser execution',
      agreementScope:this.mode.scope,
      sourceStartSample:active.start,sourceEndSample:active.end,span:active.span,
      predictedDrawRequestMonotonicNs:drawRequestNs,predictedDrawResolvedMonotonicNs:drawResolvedNs,
      receiveEvents:active.receiveEvents.map(row=>({...row})),
      receivedRows:rows,receivedRowCount:rows.length,
      alpha:{remoteContiguousInput:this.peers.alpha.remoteContiguousInput,
        remoteAckInput:this.peers.alpha.remoteAckInput,terminal:this.peers.alpha.terminal},
      beta:{remoteContiguousInput:this.peers.beta.remoteContiguousInput,
        remoteAckInput:this.peers.beta.remoteAckInput,terminal:this.peers.beta.terminal},
      overlapEvidence:overlapping?
        `at least one accepted Node receive callback entered after command ${this.mode.predictedDrawCommand} request and before its promise resolved`:
        'not observed'};
    if(this.mode===PACKET_MODES.correctedLoop){
      receipt.releasedWithPredictedCommand40=active.kind.startsWith('speculative');
      receipt.predictedCommand40ReceiveCallbackOverlap=overlapping;
    }else{
      receipt.releasedWithPredictedCommand67=active.kind.startsWith('speculative');
      receipt.predictedCommand67ReceiveCallbackOverlap=overlapping;
    }
    return receipt;
  }

  async #waitUntil(predicate,timeoutMs,label){
    const end=Date.now()+timeoutMs;
    while(true){
      if(this.failure)throw this.failure;
      if(predicate())return;
      const remaining=end-Date.now();
      if(remaining<=0)throw new Error(`${label} exceeded ${timeoutMs} ms`);
      let wake;const signal=new Promise(resolve=>{wake=resolve;this.waiters.add(wake);});
      const timeout=deadlinePromise(remaining,label);
      try{await Promise.race([signal,timeout.promise]);}
      finally{this.waiters.delete(wake);timeout.clear();}
    }
  }

  summary(){
    return {scope:this.mode.summaryScope,agreementScope:this.mode.scope,
      expectedSamples:{first:this.mode.first,last:this.mode.last},
      expectedRows:this.mode.rows,expectedSourceTicks:this.mode.sourceTicks,
      agreementSha256:sha256(Buffer.from(JSON.stringify(this.agreement))),
      transport:this.pair.transport,packetWaitMs:CPU_CORRECTED_LOOP_PACKET_WAIT_MS,
      peerReadyTimeoutMs:CPU_CORRECTED_LOOP_PEER_TIMEOUT_MS,
      neutralPreroll:{alpha:this.frameRows.alpha.filter(row=>row.preRoll).length,
        beta:this.frameRows.beta.filter(row=>row.preRoll).length},
      protocolFrameRows:{alpha:this.frameRows.alpha.length,beta:this.frameRows.beta.length},
      receivedRows:this.receivedHistory.size,pendingInboxRows:this.inbox.size,
      pendingInboxCapacity:7,receivedHistoryRows:this.receivedHistory.size,
      completedCapsules:this.receipts.length,
      peers:{alpha:this.peers.alpha.summary(),beta:this.peers.beta.summary()},
      callbackErrors:[...this.callbackErrors],failure:this.failure?String(this.failure):null};
  }

  close(){
    if(this.closePromise)return this.closePromise;
    this.closed=true;this.intentionalClose=true;
    this.closePromise=(async()=>{
      const pairClose=await this.pair.close();
      const failures=await this.callbackQueue.drain();
      if(failures.length||this.failure)throw new AggregateError([
        ...failures.map(row=>row.error),this.failure].filter(Boolean),'Corrected-loop packet adapter close observed failures');
      return {pairClose,summary:this.summary(),closed:true};
    })();
    return this.closePromise;
  }

  handleDisconnect(role,reason){return this.#transportFailure(role,'disconnect',reason);}
  handleEndpointError(role,error){return this.#transportFailure(role,'endpoint-error',error);}

  #transportFailure(role,kind,value){
    if(this.intentionalClose)return Promise.resolve();
    const message=String(value?.stack||value?.message||value);
    this.#latch(Error(`${role} relay ${kind}: ${message}`));
    return this.callbackQueue.track(role,kind,async()=>{
      const results=await Promise.allSettled(Object.entries(this.peers).map(([name,peer])=>
        peer.disconnect(`${role} relay ${kind}: ${message}`)));
      const failures=results.filter(row=>row.status==='rejected').map(row=>row.reason);
      if(failures.length)throw new AggregateError(failures,`${role} relay ${kind} terminal propagation failed`);
    });
  }
}

/** Opens only the existing local Worker relay and two existing protocol peers. */
export async function openCpuCorrectedLoopPacketInput({runtime,agreement,now=ns}={}){
  if(!runtime||typeof runtime.base!=='string'||!runtime.base.startsWith('http://127.0.0.1:'))
    throw new Error('Corrected-loop packet adapter requires the owned loopback Worker runtime');
  const validatedAgreement=expectedAgreement(agreement);
  let controller=null,openingError=null;
  const pair=await openLockstepPeerPair({relayUrl:runtime.base.replace(/^http:/,'ws:'),
    timeoutMs:CPU_CORRECTED_LOOP_PEER_TIMEOUT_MS,
    onDisconnect:(role,reason)=>{
      if(controller)return controller.handleDisconnect(role,reason);
      openingError??=Error(`${role} disconnected during packet adapter setup: ${reason}`);
    },
    onEndpointError:(role,error)=>{
      if(controller)return controller.handleEndpointError(role,error);
      openingError??=Error(`${role} relay endpoint failed during setup: ${String(error?.message||error)}`);
    }});
  try{
    controller=new CpuCorrectedLoopPacketInput(pair,validatedAgreement,{now});
    if(openingError)throw openingError;
    controller.startup=await controller.start();
    return controller;
  }catch(error){
    try{await (controller?controller.close():pair.close());}
    catch(cleanupError){throw new AggregateError([error,cleanupError],
      'Corrected-loop packet adapter setup failed and cleanup also failed',{cause:error});}
    throw error;
  }
}
