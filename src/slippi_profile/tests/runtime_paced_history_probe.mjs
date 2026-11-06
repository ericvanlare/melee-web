// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded page-owned cadence probe. Native code owns source ticks, clock,
// snapshots, renderer submission and idle. No CDP operation drives this loop.
import {RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS} from '../runtime_match_history_peer.mjs';

function pauseGate(label){
  let resolveReady,resolveResume,entered=false,resumed=false;
  const ready=new Promise(resolve=>{resolveReady=resolve;});
  const resume=new Promise(resolve=>{resolveResume=resolve;});
  return Object.freeze({
    pause(value){
      if(entered)throw Error(`${label} gate was entered more than once`);
      entered=true;resolveReady(value);return resume;
    },
    wait(){return ready;},
    release(){
      if(!entered||resumed)throw Error(`${label} gate release is stale or duplicated`);
      resumed=true;resolveResume();
    },
    get entered(){return entered;},get resumed(){return resumed;},
  });
}

export function installRuntimePacedHistoryProbe({role,
  inputTicks=RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,lateClassPiece=false}={}){
  const owner=globalThis.__meleeRuntimeMatchHistory;
  const peer=globalThis.__meleeRuntimePeerHistoryProbe;
  if(!owner||!peer||!['alpha','beta'].includes(role)||globalThis.__meleeRuntimePacedHistory||
      ![RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS].includes(inputTicks) ||
      typeof lateClassPiece!=='boolean' || (lateClassPiece&&inputTicks!==RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS))
    throw Error('Paced probe requires the existing exclusive owners and an explicit supported finite horizon');
  const targetFrame=inputTicks;
  const paced120=inputTicks===RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS;
  const lateScenario=lateClassPiece?{target_source_tick:26,anchor:null,keyboard_change:null,held_packet:null,
    release_gate:null,alpha_frontier:null,network_release:null}:null;
  const report={scope:lateClassPiece?'120 actual source inputs with one deferred actual peer packet at source tick 26; existing clock and rollback bounds':
      paced120?'120 actual source inputs; functional pacing clock trace with native-sized rollback windows':
      'eight actual source inputs; native admission and measured headless cadence',
    input_ticks:inputTicks,role,result:'pending',events:[],presentations:[],admissions:[],polls:[],
    authoritativeInputs:[],corrections:[],rebases:[],callbacks:[],failure:null,
    ...(lateScenario?{late_class_piece:lateScenario}:{})};
  const lateKeyGate=pauseGate('late-key-change'),latePacketGate=pauseGate('late-packet-release'),
    lateFrontierGate=pauseGate('late-alpha-frontier');
  let lateAborted=false;
  const requireLateScenario=()=>{if(!lateClassPiece)throw Error('Late class-piece controls are unavailable in this paced phase');};
  let prepared=false,started=false,remoteNext=0;
  const selected=new Map();
  const state=()=>owner.status();
  const timeline=()=>peer.call('episodeTimeline');
  const call=async(operation,value)=>{
    const event={operation,value,started:performance.now()};report.events.push(event);
    try{return event.result=await peer.call(operation,value);}
    catch(error){event.failure=String(error);throw error;}
    finally{event.elapsedMs=performance.now()-event.started;}
  };
  const command=name=>call('command',name);
  const admit=(view,provenance)=>{
    const receipt=owner.admitEndpoint({view,provenance});report.admissions.push(receipt);return receipt;
  };
  const retainRows=rows=>{
    for(const item of rows){const row=item.row??item;selected.set(row.sourceFrame,[...row.selected]);}
  };
  const present=async(recording,kind='forward')=>{
    const status=await command(recording?'recordingPresent':'present');
    const receipt=owner.presentation();
    const prior=report.presentations.at(-1)?.receipt;
    if(receipt.sourceFrameAfter!==status.metadata.currentFrame||
        BigInt(receipt.presentationSerial)!==BigInt(prior?.presentationSerial??'0')+1n)
      throw Error('Paced presentation lost its native frame or serial');
    const row={kind,receipt};report.presentations.push(row);
    row.idle=await command('idle');
  };
  const capture=async()=>{
    await command('capture');admit(1,1);
    await command('beginEpisode');admit(1,1);
  };
  const sealPresented=async()=>{
    await command('sealPresentedFrontier');admit(3,2);
  };
  const closeRebase=async(end)=>{
    await command('close');await command('retire');
    const rebased=await call('episodeRebase',end);report.rebases.push(rebased);
    await capture();
  };
  const admitAvailable=async()=>{
    while(remoteNext<targetFrame){
      const active=await timeline();
      const nextLocal=active.simulated_through+1;
      if(remoteNext-nextLocal>=active.max_lookahead_ticks)break;
      const receipt=await call('peerRemoteSnapshot',{tick:remoteNext});
      if(!receipt)break;
      const admission=await call('episodeAdmit',{tick:remoteNext});
      if(lateScenario&&role==='alpha'&&remoteNext===26)
        lateScenario.remote_admission={tick:remoteNext,receipt:admission.receipt,
          timeline:admission.timeline};
      remoteNext++;
    }
  };
  const reconcile=async(snapshot)=>{
    const current=state().metadata,pre=current.preFrame,frontier=current.currentFrame;
    const confirmed=Math.min(snapshot.remote_contiguous_through+1,frontier);
    if(confirmed<=pre||confirmed>frontier)throw Error('Paced correction lacks a newer confirmed anchor');
    await sealPresented();
    const lateFrontier=lateScenario?.alpha_frontier;
    if(role==='alpha'&&snapshot.dirty_from===26&&lateFrontier?.pre_frame===pre&&
        lateFrontier.frontier===frontier){
      const frontierStatus=state();
      if(frontierStatus.ownerState!==1||frontierStatus.pendingCommand||frontierStatus.gpuPending||
          frontierStatus.endpointActive||frontierStatus.inputReady!==1||
          frontierStatus.metadata.phase!==28||frontierStatus.metadata.endpointMask!==(1<<8)||
          frontierStatus.metadata.preFrame!==pre||frontierStatus.metadata.currentFrame!==frontier||
          frontierStatus.metadata.sealedEnd!==frontier)
        throw Error('Late pre-restore checksum requires its completed presented frontier');
      const checksumStarted=performance.now();
      const bytes=[...owner.checksum({postDraw:true})];
      if(bytes.length!==64)throw Error('Late presented-frontier checksum is not exactly 64 bytes');
      lateScenario.presented_checksum={status:'captured',pre_frame:pre,frontier,bytes};
      report.events.push({operation:'late-presented-frontier-checksum',frame:frontier,bytes,
        elapsedMs:performance.now()-checksumStarted});
    }
    if(snapshot.dirty_from===null&&confirmed===frontier){
      await closeRebase(frontier);return;
    }
    const restoreEventIndex=report.events.length;
    const restoreStatus=await command('restore');admit(1,1);
    if(confirmed<frontier){
      const prefix=await call('episodePrefixBegin',{confirmedEnd:confirmed,
        through:frontier-1,dirty:snapshot.dirty_from,speculative:frontier-confirmed});
      retainRows(prefix.rows);admit(2,3);
      const checksum=[...owner.checksum()];
      await command('closePrefix');await command('retirePrefix');
      const rebased=await call('episodePrefixRebase',confirmed);report.rebases.push(rebased);
      await capture();
      const adopted=await call('episodePrefixAdopt');retainRows(adopted.rows);
      await present(true,'rebuilt-frontier');
      report.corrections.push({kind:'confirmed-prefix',pre,frontier,confirmed,
        dirty:snapshot.dirty_from,checksum,prefix,adopted,
        ...(lateScenario?{restore_event_index:restoreEventIndex,restore_status:restoreStatus}:{})});
    }else{
      const replay=await call('episodeReplay',{dirty:snapshot.dirty_from,
        through:frontier-1,speculative:0});
      retainRows(replay.rows);admit(2,3);
      const checksum=[...owner.checksum()];
      await present(false,'corrected-frontier');admit(3,3);
      report.corrections.push({kind:'fully-confirmed-correction',pre,frontier,confirmed,
        dirty:snapshot.dirty_from,checksum,replay,
        ...(lateScenario?{restore_event_index:restoreEventIndex,restore_status:restoreStatus}:{})});
      if(frontier!==targetFrame)await closeRebase(frontier);
    }
  };
  const api=Object.freeze({
    snapshot:()=>structuredClone({...report,clock:owner.pacingJournal()}),
    waitForLateKeyChange(){requireLateScenario();if(role!=='beta')throw Error('Only beta owns the late key-change gate');return lateKeyGate.wait();},
    noteLateKeyChange({from,to}={}){
      requireLateScenario();
      if(role!=='beta'||!lateKeyGate.entered||lateKeyGate.resumed||lateScenario.keyboard_change||
          from!=='ArrowLeft'||to!=='ArrowRight')
        throw Error('Late key change must be one Playwright ArrowLeft-to-ArrowRight transition at source tick 26');
      lateScenario.keyboard_change={source_tick:26,from,to,pre_frame:state().metadata.preFrame,
        current_frame:state().metadata.currentFrame,driver:'Playwright page.keyboard'};
      return structuredClone(lateScenario.keyboard_change);
    },
    resumeLateKeyChange(){
      requireLateScenario();
      if(role!=='beta'||!lateScenario.keyboard_change)throw Error('Late native poll cannot resume before its recorded key change');
      lateKeyGate.release();
    },
    waitForLatePacketGate(){requireLateScenario();if(role!=='beta')throw Error('Only beta owns the held-packet gate');return latePacketGate.wait();},
    waitForLateFrontier(){requireLateScenario();if(role!=='alpha')throw Error('Only alpha owns the delayed-input frontier');return lateFrontierGate.wait();},
    async releaseLatePacket(){
      requireLateScenario();
      if(role!=='beta'||!latePacketGate.entered||latePacketGate.resumed||lateScenario.network_release||
          !lateScenario.held_packet)
        throw Error('Late packet release requires the exact held beta source poll at the release gate');
      const released=await call('episodeReleaseLocal',{tick:26});
      const receipt=released.receipt,held=lateScenario.held_packet;
      if(receipt.protocol_input_tick!==26||receipt.poll_serial!==held.poll_serial||
          receipt.local_port!==held.local_port||
          JSON.stringify(receipt.pad)!==JSON.stringify(held.actual_local_pad))
        throw Error('Late packet release changed the retained source poll identity or bytes');
      lateScenario.network_release={source_tick:26,poll_serial:receipt.poll_serial,
        local_port:receipt.local_port,actual_local_pad:[...receipt.pad],
        source_poll:released.source};
      return structuredClone(lateScenario.network_release);
    },
    resumeLatePacket(){
      requireLateScenario();
      if(role!=='beta'||!lateScenario.network_release)throw Error('Beta source loop cannot resume before the held packet was released');
      latePacketGate.release();
    },
    resumeLateFrontier(receipt){
      requireLateScenario();
      if(role!=='alpha'||!lateFrontierGate.entered||lateFrontierGate.resumed||
          receipt?.accepted_by!=='lockstep-protocol'||receipt.sender_role!=='beta'||
          receipt.remote_port!==1||receipt.protocol_input_tick!==26||
          !Array.isArray(receipt.pad)||receipt.pad.length!==11)
        throw Error('Alpha source loop requires the received actual beta packet before frontier release');
      lateScenario.remote_receipt=structuredClone(receipt);
      lateFrontierGate.release();
    },
    abortLateScenario(){
      requireLateScenario();lateAborted=true;
      for(const gate of [lateKeyGate,latePacketGate,lateFrontierGate])
        if(gate.entered&&!gate.resumed)gate.release();
      return {aborted:true,role};
    },
    async prepare(){
      if(prepared||started)throw Error('Paced owner preparation is once-only');
      await call('arenaEnable');await call('inlineEnable');await capture();prepared=true;
      return {owner:state(),scope:'initial preparation before clock start'};
    },
    async run(){
      if(!prepared||started)throw Error('Paced owner run requires its prepared initial state');
      started=true;report.startedAt=performance.now();
      try{
        owner.pacingStart();
        let continueCallback=false,callback=null;
        for(;;){
          if(lateAborted)throw Error('Late class-piece scenario was aborted by its external owner');
          if(!continueCallback){
            const timestamp=await new Promise(resolve=>requestAnimationFrame(resolve));
            callback={serial:report.callbacks.length+1,rafTimestamp:timestamp,
              startedAt:performance.now(),grants:0,reconciliations:0};
            report.callbacks.push(callback);
          }
          continueCallback=false;
          // Budget zero accounts for actual elapsed time before reconciliation;
          // the clock is never reset during a network wait or state transfer.
          owner.pacingSample(0);
          await admitAvailable();
          let inputs=await timeline(),current=state().metadata;
          const frame=current.currentFrame,confirmed=inputs.remote_contiguous_through+1;
          const lateAnchorFrame=21;
          const needsLateAnchor=lateClassPiece&&!lateScenario.anchor;
          const atLateAnchor=needsLateAnchor&&frame===lateAnchorFrame;
          if(needsLateAnchor&&frame>lateAnchorFrame)
            throw Error('Late-piece run advanced past its required frame-21 native anchor');
          if(atLateAnchor&&inputs.dirty_from===null){
            if(inputs.confirmed_through<lateAnchorFrame-1){
              report.events.push({operation:'late-anchor-network-wait',frame,confirmed});
              continue;
            }
            if(current.preFrame===lateAnchorFrame)
              lateScenario.anchor={pre_frame:current.preFrame,current_frame:frame,
                confirmed_through:inputs.confirmed_through,
                evidence:'existing native PRE after confirmed rebase'};
          }
          if(lateClassPiece&&role==='beta'&&frame===26&&!lateScenario.keyboard_change){
            if(current.preFrame>26)throw Error('Late key-change target tick 26 is already before the active native PRE');
            await lateKeyGate.pause({source_tick:frame,pre_frame:current.preFrame,
              current_frame:current.currentFrame});
            if(lateAborted)throw Error('Late key-change gate was aborted before its actual poll');
          }
          if(lateClassPiece&&role==='alpha'&&!lateScenario.alpha_frontier){
            const frontier=Math.min(28,current.preFrame+7);
            if(frame===frontier&&frame>26){
              if(!(lateScenario.anchor?.pre_frame===21&&current.preFrame===21&&
                  frame===28&&26<frame&&frame<=current.preFrame+7))
                throw Error('Late alpha frontier is outside the current native PRE and seven-tick bound');
              if(inputs.remote_contiguous_through<25){
                report.events.push({operation:'late-prefix-network-wait',frame,
                  remote_contiguous_through:inputs.remote_contiguous_through,dirty_from:inputs.dirty_from});
                continue;
              }
              if(inputs.remote_contiguous_through!==25||inputs.dirty_from!==null)
                throw Error('Late alpha frontier observed an escaped tick 26 or an earlier dirty correction');
              lateScenario.alpha_frontier={pre_frame:current.preFrame,frontier:frame,
                remote_contiguous_through:inputs.remote_contiguous_through,dirty_from:inputs.dirty_from,
                span:frame-current.preFrame};
              await lateFrontierGate.pause(structuredClone(lateScenario.alpha_frontier));
              if(lateAborted)throw Error('Late alpha frontier gate was aborted before packet admission');
              continue;
            }
          }
          if(lateClassPiece&&role==='beta'&&frame===27&&lateScenario.held_packet&&
              !lateScenario.release_gate){
            lateScenario.release_gate={pre_frame:current.preFrame,frontier:frame,
              remote_contiguous_through:inputs.remote_contiguous_through};
            await latePacketGate.pause(structuredClone(lateScenario.release_gate));
            if(lateAborted)throw Error('Late beta packet gate was aborted before source tick 27');
          }
          if(frame===targetFrame&&confirmed===targetFrame&&inputs.dirty_from===null){
            report.finalTimeline=inputs;
            // The final endpoint is explicitly post-draw, not CPU-before-draw.
            if(current.phase!==16)await sealPresented();
            report.finalChecksum={view:current.phase===16?'corrected-draw-end':'presented-frontier',
              bytes:[...owner.checksum({postDraw:true})]};
            owner.pacingSample(0);
            report.clockFinishedAt=performance.now();
            await command('close');await command('retire');
            report.finalOwner=state();
            report.authoritativeInputs=Array.from({length:targetFrame},(_,tick)=>({tick,bytes:selected.get(tick)}));
            if(report.authoritativeInputs.some(row=>!row.bytes||row.bytes.length!==44))
              throw Error('Paced terminal lost an authoritative input row');
            if(lateClassPiece){
              if(role==='beta'&&(!lateScenario.keyboard_change||!lateScenario.held_packet||
                  !lateScenario.network_release||!latePacketGate.resumed))
                throw Error('Late beta source poll, held packet, or exact packet release was not completed');
              if(role==='alpha'){
                const correction=report.corrections.find(row=>row.dirty===26);
                if(!correction||!lateScenario.alpha_frontier||!lateScenario.remote_admission||
                    !lateScenario.remote_receipt||correction.pre>26||correction.frontier<=26||
                    correction.frontier>correction.pre+7||correction.frontier!==lateScenario.alpha_frontier.frontier||
                    correction.restore_status?.metadata?.preFrame!==correction.pre||
                    correction.restore_status?.metadata?.sealedEnd!==correction.frontier||
                    correction.restore_status?.metadata?.restoreCount<1)
                  throw Error('Late tick 26 did not cross a bounded native restore lifecycle');
                const plan=correction.prefix?.plan??correction.replay?.plan;
                const frame=plan?.frames?.find(row=>row.tick===26);
                if(!frame||frame.remote_source!=='actual'||frame.speculative!==false)
                  throw Error('Late tick 26 restore did not use its actual peer-confirmed PAD');
                lateScenario.correction={kind:correction.kind,pre:correction.pre,
                  frontier:correction.frontier,confirmed:correction.confirmed,dirty:correction.dirty,
                  restore_count:correction.restore_status.metadata.restoreCount,
                  corrected_tick:frame.tick,remote_source:frame.remote_source};
              }
            }
            report.result=paced120?'PACED_120_INPUTS_COMPLETE':'PACED_EIGHT_INPUTS_COMPLETE';break;
          }
          const fullWindow=frame-current.preFrame===7;
          const cleanRebase=lateClassPiece
            ? (fullWindow||atLateAnchor)&&confirmed>=frame&&current.preFrame<frame
            : fullWindow&&confirmed>current.preFrame;
          if(inputs.dirty_from!==null||cleanRebase){
            if(callback.reconciliations===1)continue;
            callback.reconciliations++;
            await reconcile(inputs);
            const remaining=owner.pacingSample(0);
            callback.lastWorkAt=performance.now();
            continueCallback=(remaining.pendingSteps>0||frame===targetFrame)&&callback.grants<8;
            continue;
          }
          // Remote samples ahead of local execution are retained by the existing
          // transport/timeline; only actual simulated inputs can become anchors.
          if(frame===targetFrame||frame-current.preFrame>=7||frame-inputs.remote_contiguous_through>7){
            report.events.push({operation:'network-wait',frame,confirmed});continue;
          }
          if(callback.grants===8)continue;
          const clock=owner.pacingSample(1);
          if(clock.steps===0)continue;
          if(clock.steps!==1||clock.currentFrame!==frame)throw Error('Paced grant differs from actual source frame');
          callback.grants++;
          const polled=await call('episodePoll',{tick:frame});report.polls.push(polled.ticket);
          const deferLatePacket=lateClassPiece&&role==='beta'&&frame===26;
          const publication=await call('episodePublish',{tick:frame,ticketId:polled.ticket.ticketId,
            ...(deferLatePacket?{deferSend:true}:{})});
          if(deferLatePacket){
            if(publication?.deferred_send!==true||publication.receipt?.protocol_input_tick!==26||
                publication.receipt?.poll_serial!==polled.ticket.pollSerial)
              throw Error('Late packet hold did not retain the exact source tick 26 native poll');
            const localPort=polled.ticket.localPort;
            lateScenario.held_packet={source_tick:26,poll_serial:polled.ticket.pollSerial,
              local_port:localPort,actual_local_pad:polled.ticket.raw.slice(localPort*11,(localPort+1)*11),
              deferred_send:true};
          }
          // Network callbacks may retain packets during the reserved poll, but
          // this owner admits them only between complete source operations.
          const actual=frame<=inputs.remote_contiguous_through;
          const forwarded=await call('episodeForward',{tick:frame,ticketId:polled.ticket.ticketId,actual});
          retainRows([forwarded]);await present(true);
          const remaining=owner.pacingSample(0);
          callback.lastWorkAt=performance.now();
          continueCallback=(remaining.pendingSteps>0||frame+1===targetFrame)&&callback.grants<8;
        }
      }catch(error){
        report.result='FAIL';report.failure={message:String(error?.message??error),
          stack:error?.stack??null,pacingSample:error?.pacingSample??null};
      }finally{
        report.finishedAt=performance.now();report.clock=owner.pacingJournal();
        report.commands=owner.journal();
      }
      return structuredClone(report);
    },
  });
  globalThis.__meleeRuntimePacedHistory=api;return {role,scope:report.scope};
}
