// SPDX-License-Identifier: GPL-3.0-or-later
// Actual local Worker/A2 transport control. Node peers only; no browser or native execution.
import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {constants,existsSync} from 'node:fs';
import {access,mkdir,mkdtemp,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {buildCpuCorrectedLoopCapsules} from './cpu_corrected_loop_runner.mjs';
import {buildCpuCorrectedLoopPacketAgreement,buildCpuHistoryPacketAgreement,
  CpuCorrectedLoopPacketInput,openCpuCorrectedLoopPacketInput} from './cpu_corrected_loop_packet_input.mjs';
import {LOCKSTEP_VERSION} from '../../../scripts/net_lockstep_protocol.mjs';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const SOURCE_ROOT=path.resolve(process.env.MELEE_PACKET_ADAPTER_SOURCE_ROOT||ROOT);
const OUT=process.env.MELEE_PACKET_ADAPTER_TEST_EVIDENCE;
const toolsPath=process.env.MELEE_A3_ROOM_RELAY_TOOLS;
const runWorker=process.env.MELEE_PACKET_ADAPTER_RUN_WORKER==='1';
const toolsAvailable=Boolean(toolsPath&&path.isAbsolute(toolsPath)&&
  ['wrangler/package.json','miniflare/package.json','workerd/package.json']
  .every(file=>existsSync(path.join(toolsPath,file))));
const evidenceConfigured=Boolean(OUT&&path.isAbsolute(OUT));
const diagnosticProfile={identity:'slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20',
  sourceIdentity:'122a2a6d4e5361b0733c9d2b22c2865e01a81d960848bb79e82ce120b5cef271',
  rngHelper:'2b9f7398e09cb8ca8c2d53474739b313ce565bae77411cd29b4d2f4087a4812e',
  stageBridge:'a81e21f9629e711b3975ca93b821bedd2c13cf06fde6381585bc3918a8aef3db',
  seed:324508639,name:'slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20',
  sourceTextureMatrixWriterDiagnostic:'NOT_APPLICABLE_STATELESS_TOBJ_PRESENTATION_V15',
  sourceLvecWriterDiagnostic:'NOT_APPLICABLE_SOURCE_CACHE_RELOCATED_V7'};
const observedInitializer={valid:1,profile:0,seed:324508639,stage_kind:37,player_count:2,
  players:[{color:0,controller:0,fighter:8,port:0,stocks:4},
    {color:1,controller:1,fighter:8,port:0,stocks:4}]};
const observedInputMapping=[{sourceSlot:0,initializerPort:0,rawController:0},
  {sourceSlot:1,initializerPort:0,rawController:1}];
const agreementInputs={producer:{commit:'a'.repeat(40),tree:'b'.repeat(40)},profile:diagnosticProfile,
  initializer:JSON.stringify(observedInitializer),inputMapping:observedInputMapping,slotCount:6,
  sourceStateManifest:{configurationIdentitySha256:'c'.repeat(64),wasmSha256:'d'.repeat(64)},
  runtimeSha256:{runtime:'e'.repeat(64)},harnessSha256:{runner:'f'.repeat(64)},
  worker:{identity:{runtime:'owned-worker'},hashes:{worker:'1'.repeat(64)}}};

function createMockPair(){
  const listeners={alpha:null,beta:null},pending={alpha:new Set(),beta:new Set()};
  let closed=false;
  const endpoint=role=>({
    send(text){
      if(closed)return Promise.reject(new Error('mock pair is closed'));
      const receiver=role==='alpha'?'beta':'alpha',listener=listeners[receiver];
      if(typeof listener!=='function')return Promise.reject(new Error(`mock ${receiver} listener is not attached`));
      const task=Promise.resolve().then(()=>listener(text));
      pending[receiver].add(task);
      task.finally(()=>pending[receiver].delete(task)).catch(()=>{});
      task.catch(()=>{});
      return Promise.resolve();
    },
    onMessage(listener){listeners[role]=listener;},
    async drainInbound(timeoutMs){
      const deadline=Date.now()+timeoutMs;
      while(pending[role].size){
        const remaining=deadline-Date.now();
        if(remaining<=0)throw new Error(`mock ${role} inbound callbacks exceeded ${timeoutMs} ms`);
        let timer;
        try{await Promise.race([Promise.all([...pending[role]]),new Promise((_,reject)=>{
          timer=setTimeout(()=>reject(new Error(`mock ${role} inbound callbacks exceeded ${timeoutMs} ms`)),remaining);
        })]);}
        finally{clearTimeout(timer);}
      }
    },
  });
  return {alpha:endpoint('alpha'),beta:endpoint('beta'),transport:{type:'in-memory-test-only'},
    async close(){
      const drained=await Promise.allSettled([
        Promise.all([...pending.alpha]),Promise.all([...pending.beta])]);
      closed=true;
      const failure=drained.find(row=>row.status==='rejected');
      if(failure)throw failure.reason;
      return {closed:true};
    }};
}

function mockHistoryRows(){
  return Array.from({length:4},(_,index)=>{
    const frame=Buffer.alloc(44),p0=Buffer.alloc(11),p1=Buffer.alloc(11);
    p0[0]=0x10+index;p1[2]=0x40+index;
    frame.set(p0,0);frame.set(p1,11);frame[32]=0xff;frame[43]=0xff;
    return frame.toString('base64');
  });
}

test('invalid start identity is rejected before opening the loopback relay',async()=>{
  await assert.rejects(()=>openCpuCorrectedLoopPacketInput({runtime:{base:'http://127.0.0.1:1'},agreement:{}}),
    /start identity is not bound/);
});

test('A2 agreement binds the initialized source identity and observed port/controller map',()=>{
  const agreement=buildCpuCorrectedLoopPacketAgreement(agreementInputs);
  assert.equal(agreement.scope,'cpu-corrected-loop-packet-input-v1');
  assert.deepEqual(agreement.samples,{first:300,last:343});
  assert.equal(agreement.profile.identity,diagnosticProfile.identity);
  assert.equal(agreement.profile.sourceIdentity,diagnosticProfile.sourceIdentity);
  assert.equal(agreement.profile.rngHelper,diagnosticProfile.rngHelper);
  assert.equal(agreement.profile.stageBridge,diagnosticProfile.stageBridge);
  assert.deepEqual(agreement.profile.inputMapping,observedInputMapping);
  assert.throws(()=>buildCpuCorrectedLoopPacketAgreement({...agreementInputs,
    profile:{...diagnosticProfile,sourceIdentity:null}}),/start identity is not bound/);
  const changedInitializer={...observedInitializer,players:observedInitializer.players.map((player,index)=>
    index===1?{...player,port:2}:player)};
  assert.throws(()=>buildCpuCorrectedLoopPacketAgreement({...agreementInputs,
    initializer:JSON.stringify(changedInitializer)}),/differs from the parsed diagnostic initializer/);
  assert.throws(()=>buildCpuCorrectedLoopPacketAgreement({...agreementInputs,
    inputMapping:[observedInputMapping[0],{sourceSlot:1,initializerPort:1,rawController:1}]}),
    /raw controller does not match the initializer formula/);
  const equivalentInitializer={...observedInitializer,players:observedInitializer.players.map((player,index)=>
    index===1?{...player,port:2}:player)};
  const equivalentMapping=[observedInputMapping[0],{sourceSlot:1,initializerPort:2,rawController:1}];
  assert.deepEqual(buildCpuCorrectedLoopPacketAgreement({...agreementInputs,
    initializer:JSON.stringify(equivalentInitializer),inputMapping:equivalentMapping})
    .profile.inputMapping,equivalentMapping);
});

test('retained-PRE agreement and bounded mock peer carry exactly four rows in 2+1+1 capsules',async()=>{
  const agreement=buildCpuHistoryPacketAgreement(agreementInputs);
  assert.equal(agreement.protocol,'melee-web-local-lockstep-a2-v1');
  assert.equal(agreement.scope,'cpu-history-packet-input-v1');
  assert.deepEqual(agreement.samples,{first:300,last:303});
  assert.deepEqual(agreement.profile.inputMapping,observedInputMapping);
  assert.throws(()=>new CpuCorrectedLoopPacketInput(createMockPair(),
    {...agreement,samples:{first:300,last:343}}),/start identity is not bound/,
    'history admission must reject a 44-row extent under the four-row scope');

  const pair=createMockPair(),packetInput=new CpuCorrectedLoopPacketInput(pair,agreement);
  try{
    const startup=await packetInput.start();
    assert.deepEqual(startup.neutralPreroll,{alpha:2,beta:2});
    assert.deepEqual(startup.protocolFrameRows,{alpha:2,beta:2});
    const rows=mockHistoryRows();
    const capsules=[
      {startSample:300,span:2,kind:'speculative-history',rows:rows.slice(0,2)},
      {startSample:302,span:1,kind:'speculative-history',rows:rows.slice(2,3)},
      {startSample:303,span:1,kind:'confirmed-rebase',rows:rows.slice(3)},
    ];
    const receipts=[];
    for(const capsule of capsules){
      packetInput.prepareCapsule(capsule);
      if(capsule.kind.startsWith('speculative')){
        const requestNs=process.hrtime.bigint().toString();
        let resolveDraw;
        const drawPending=new Promise(resolve=>{resolveDraw=resolve;});
        const drawResult=drawPending.then(()=>packetInput.notePredictedDrawResolved(capsule.startSample));
        const published=await packetInput.publishCapsule(capsule,{predictedDrawRequestNs:requestNs});
        assert.equal(published.receivedRowCount,capsule.span);
        resolveDraw();
        await drawResult;
      }else await packetInput.publishCapsule(capsule);
      const receipt=await packetInput.drainCapsule(capsule);
      assert.equal(receipt.sourceStartSample,capsule.startSample);
      assert.equal(receipt.receivedRowCount,capsule.span);
      assert.equal(receipt.agreementScope,'cpu-history-packet-input-v1');
      assert.equal(receipt.transport,'in-memory-test-only');
      assert.equal(receipt.socketOwner,'test-only endpoint adapter; no browser execution');
      if(capsule.kind.startsWith('speculative')){
        assert.equal(receipt.releasedWithPredictedCommand67,true);
        assert.equal(receipt.predictedCommand67ReceiveCallbackOverlap,true);
        assert.equal('releasedWithPredictedCommand40' in receipt,false);
      }else assert.equal(receipt.releasedWithPredictedCommand67,false);
      receipts.push(receipt);
      assert.equal(packetInput.summary().pendingInboxRows,0);
    }
    const summary=packetInput.summary();
    assert.equal(summary.agreementScope,'cpu-history-packet-input-v1');
    assert.deepEqual(summary.expectedSamples,{first:300,last:303});
    assert.equal(summary.expectedRows,4);
    assert.equal(summary.expectedSourceTicks,6);
    assert.equal(summary.receivedRows,4);
    assert.equal(summary.receivedHistoryRows,4);
    assert.equal(summary.completedCapsules,3);
    assert.deepEqual(summary.protocolFrameRows,{alpha:6,beta:6});
    assert.equal(summary.peers.alpha.remote_contiguous_input,3);
    assert.equal(summary.peers.beta.remote_contiguous_input,3);
    for(let index=0;index<rows.length;index++){
      const expected=Buffer.from(rows[index],'base64').subarray(11,22);
      assert.deepEqual(packetInput.getRemotePad(300+index),expected);
    }
    assert.equal(receipts.length,3);
  }finally{await packetInput.close();}
});

test('actual local Worker carries all 44 A2 remote PAD contributions with bounded one-capsule authority',
  {skip:!runWorker?'set MELEE_PACKET_ADAPTER_RUN_WORKER=1 to opt in':
    !toolsAvailable?'pinned relay tools path is unavailable':
    !evidenceConfigured?'explicit Worker evidence path is unavailable':false},async(t)=>{
    try{await mkdir(OUT,{recursive:true});await access(OUT,constants.W_OK);}
    catch(error){t.skip(`explicit evidence path is unavailable: ${error.message}`);return;}
    const evidence=await mkdtemp(path.join(OUT,'worker-control-'));
    const {startRoomRelayRuntime}=await import('../../../scripts/net_room_relay_runtime_owner.mjs');
    const producer={commit:execFileSync('git',['rev-parse','HEAD'],{cwd:SOURCE_ROOT,encoding:'utf8'}).trim(),
      tree:execFileSync('git',['rev-parse','HEAD^{tree}'],{cwd:SOURCE_ROOT,encoding:'utf8'}).trim()};
    const runtime=await startRoomRelayRuntime({producer,evidenceDir:path.join(evidence,'worker-evidence')});
    let packetInput=null,refusalInput=null,passed=false,primary=null;
    try{
      const agreement=buildCpuCorrectedLoopPacketAgreement({...agreementInputs,producer,
        initializer:agreementInputs.initializer,inputMapping:observedInputMapping,slotCount:6,
        sourceStateManifest:{configurationIdentitySha256:'a'.repeat(64),wasmSha256:'b'.repeat(64)},
        runtimeSha256:{runtime:'c'.repeat(64)},harnessSha256:{runner:'d'.repeat(64)},
        worker:{identity:runtime.runtimeIdentity,hashes:runtime.hashes}});
      packetInput=await openCpuCorrectedLoopPacketInput({runtime,agreement});
      assert.equal(packetInput.startup.neutralPreroll.alpha,2);
      assert.equal(packetInput.startup.neutralPreroll.beta,2);
      const recipe=buildCpuCorrectedLoopCapsules();
      for(const capsule of recipe){
        packetInput.prepareCapsule(capsule);
        if(capsule.kind.startsWith('speculative')){
          const publish=packetInput.publishCapsule(capsule,
            {predictedDrawRequestNs:process.hrtime.bigint().toString()});
          // This transport-only control has no browser operation to time; its
          // report must leave command-40 overlap unproven.
          const receipt=await publish;
          assert.equal(receipt.predictedCommand40ReceiveCallbackOverlap,false);
          assert.equal(receipt.overlapEvidence,'not observed');
        }else await packetInput.publishCapsule(capsule);
        assert.ok(packetInput.summary().pendingInboxRows<=capsule.span);
        assert.ok(packetInput.summary().pendingInboxRows<=7);
        const receipt=await packetInput.drainCapsule(capsule);
        assert.equal(receipt.receivedRowCount,capsule.span);
        assert.equal(packetInput.summary().pendingInboxRows,0);
        for(let index=0;index<capsule.span;index++){
          const sample=capsule.startSample+index,expected=Buffer.from(capsule.rows[index],'base64').subarray(11,22);
          assert.deepEqual(packetInput.getRemotePad(sample),expected);
        }
      }
      const summary=packetInput.summary();
      assert.equal(summary.receivedRows,44);
      assert.equal(summary.receivedHistoryRows,44);
      assert.equal(summary.pendingInboxRows,0);
      assert.equal(summary.pendingInboxCapacity,7);
      assert.equal(summary.completedCapsules,8);
      assert.deepEqual(summary.protocolFrameRows,{alpha:46,beta:46});
      assert.equal(summary.peers.alpha.remote_contiguous_input,43);
      assert.equal(summary.peers.beta.remote_ack_input,43);
      assert.equal(summary.peers.alpha.local_checksum_ticks,0);
      assert.equal(summary.peers.beta.local_checksum_ticks,0);

      // Reuse the owned local endpoint to deliver one syntactically valid A2
      // state packet with an unrequested row. Refusal must precede peer-map,
      // pending-inbox, or owner-safe history admission.
      await packetInput.close();packetInput=null;
      refusalInput=await openCpuCorrectedLoopPacketInput({runtime,agreement});
      const firstCapsule=recipe[0];
      refusalInput.prepareCapsule(firstCapsule);
      let observedResolve;
      const observed=new Promise(resolve=>{observedResolve=resolve;});
      refusalInput.pair.alpha.onMessage(()=>observedResolve());
      const remotePad=Buffer.from(firstCapsule.rows[0],'base64').subarray(11,22);
      const unrequested={type:'state',version:LOCKSTEP_VERSION,role:'beta',sequence:0,
        ack_sequence:null,ack_input:null,ack_checksum:null,
        unacknowledged:[{tick:43,pad:remotePad.toString('base64')}],delayed_checksums:[]};
      await refusalInput.pair.beta.send(JSON.stringify(unrequested));
      await observed;
      await assert.rejects(refusalInput.pair.alpha.drainInbound(1000),/inbound callback failed/);
      const refused=refusalInput.summary();
      assert.match(refused.failure,/unrequested input tick 43/);
      assert.equal(refused.pendingInboxRows,0);
      assert.equal(refused.receivedHistoryRows,0);
      assert.equal(refusalInput.peers.alpha.remote.size,0);
      assert.equal(refusalInput.peers.alpha.remoteAckInput,-1);
      assert.throws(()=>refusalInput.getRemotePad(firstCapsule.startSample),/not passed owner-safe packet drain/);
      await writeFile(path.join(evidence,'unrequested-packet-refusal.json'),JSON.stringify({
        schema:'cpu-corrected-loop-a2-unrequested-packet-refusal-v1',
        transport:'existing local Worker room relay',socketOwner:'Node peer adapter',
        receivingPeer:'alpha',sendingEndpoint:'beta',packetType:'state',rejectedInputTick:43,
        activeCapsule:{startSample:firstCapsule.startSample,span:firstCapsule.span},
        failure:refused.failure,pendingInboxRows:refused.pendingInboxRows,
        receivedHistoryRows:refused.receivedHistoryRows,acceptedPeerRows:refusalInput.peers.alpha.remote.size,
        remoteAckInput:refusalInput.peers.alpha.remoteAckInput,ownerSafePadAvailable:false,
        outcome:'preflight rejected the unrequested row before peer receive or authority/history admission'},null,2)+'\n');
      await assert.rejects(refusalInput.close(),/Room relay pair did not close cleanly/);
      refusalInput=null;
      passed=true;
    }catch(error){primary=error;throw error;}
    finally{
      let cleanupError=null;
      if(packetInput){
        try{await packetInput.close();}
        catch(error){cleanupError=error;}
      }
      if(refusalInput){
        try{await refusalInput.close();}
        catch(error){cleanupError??=error;}
      }
      try{await runtime.close(Boolean(passed&&!primary&&!cleanupError));}
      catch(error){cleanupError??=error;}
      if(!primary&&cleanupError)throw cleanupError;
    }
  });
