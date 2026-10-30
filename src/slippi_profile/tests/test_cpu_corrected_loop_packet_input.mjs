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
import {buildCpuCorrectedLoopPacketAgreement,openCpuCorrectedLoopPacketInput} from './cpu_corrected_loop_packet_input.mjs';
import {LOCKSTEP_VERSION} from '../../../scripts/net_lockstep_protocol.mjs';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const OUT=process.env.MELEE_PACKET_ADAPTER_TEST_EVIDENCE;
const toolsPath=process.env.MELEE_A3_ROOM_RELAY_TOOLS;
const toolsAvailable=Boolean(toolsPath&&path.isAbsolute(toolsPath)&&
  ['wrangler/package.json','miniflare/package.json','workerd/package.json']
  .every(file=>existsSync(path.join(toolsPath,file))));
const evidenceConfigured=Boolean(OUT&&path.isAbsolute(OUT));
const diagnosticProfile={identity:'slippi-render-dependencies-logical-frame-lobj-tobj-tev-stage-scratch-v19',
  sourceIdentity:'snapshot-source-identity-hash',rngHelper:'source-rng-profile-identity',
  stageBridge:'source-stage-kind-bridge-identity',seed:7,
  name:'slippi-render-dependencies-logical-frame-lobj-tobj-tev-stage-scratch-v19',
  sourceTextureMatrixWriterDiagnostic:'NOT_APPLICABLE_STATELESS_TOBJ_PRESENTATION_V15',
  sourceLvecWriterDiagnostic:'NOT_APPLICABLE_SOURCE_CACHE_RELOCATED_V7'};
const agreementInputs={producer:{commit:'a'.repeat(40),tree:'b'.repeat(40)},profile:diagnosticProfile,
  initializer:'{"profile":"diagnostic"}',
  inputMapping:[{sourceSlot:0,initializerPort:0,rawController:0},
    {sourceSlot:1,initializerPort:1,rawController:1}],
  sourceStateManifest:{configurationIdentitySha256:'c'.repeat(64),wasmSha256:'d'.repeat(64)},
  runtimeSha256:{runtime:'e'.repeat(64)},harnessSha256:{runner:'f'.repeat(64)},
  worker:{identity:{runtime:'owned-worker'},hashes:{worker:'1'.repeat(64)}}};

test('invalid start identity is rejected before opening the loopback relay',async()=>{
  await assert.rejects(()=>openCpuCorrectedLoopPacketInput({runtime:{base:'http://127.0.0.1:1'},agreement:{}}),
    /start identity is not bound/);
});

test('A2 agreement binds the initialized report sourceIdentity field',()=>{
  const agreement=buildCpuCorrectedLoopPacketAgreement(agreementInputs);
  assert.equal(agreement.profile.identity,diagnosticProfile.identity);
  assert.equal(agreement.profile.sourceIdentity,diagnosticProfile.sourceIdentity);
  assert.equal(agreement.profile.rngHelper,diagnosticProfile.rngHelper);
  assert.equal(agreement.profile.stageBridge,diagnosticProfile.stageBridge);
  assert.throws(()=>buildCpuCorrectedLoopPacketAgreement({...agreementInputs,
    profile:{...diagnosticProfile,sourceIdentity:null}}),/start identity is not bound/);
});

test('actual local Worker carries all 44 A2 remote PAD contributions with bounded one-capsule authority',
  {skip:!toolsAvailable||!evidenceConfigured},async(t)=>{
    try{await mkdir(OUT,{recursive:true});await access(OUT,constants.W_OK);}
    catch(error){t.skip(`explicit evidence path is unavailable: ${error.message}`);return;}
    const evidence=await mkdtemp(path.join(OUT,'worker-control-'));
    const {startRoomRelayRuntime}=await import('../../../scripts/net_room_relay_runtime_owner.mjs');
    const producer={commit:execFileSync('git',['rev-parse','HEAD'],{cwd:ROOT,encoding:'utf8'}).trim(),
      tree:execFileSync('git',['rev-parse','HEAD^{tree}'],{cwd:ROOT,encoding:'utf8'}).trim()};
    const runtime=await startRoomRelayRuntime({producer,evidenceDir:path.join(evidence,'worker-evidence')});
    let packetInput=null,refusalInput=null,passed=false,primary=null;
    try{
      const agreement=buildCpuCorrectedLoopPacketAgreement({...agreementInputs,producer,
        initializer:'{"test":"identity only"}',profile:diagnosticProfile,
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
