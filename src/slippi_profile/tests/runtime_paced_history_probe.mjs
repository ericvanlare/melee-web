// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded page-owned cadence probe. Native code owns source ticks, clock,
// snapshots, renderer submission and idle. No CDP operation drives this loop.
import {RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS, RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_KEYS} from '../runtime_match_history_peer.mjs';

function pauseGate(label){
  const aborted=Symbol(`${label}-aborted`);
  let resolveReady,resolveResume,entered=false,resumed=false,wasAborted=false;
  const ready=new Promise(resolve=>{resolveReady=resolve;});
  const resume=new Promise(resolve=>{resolveResume=resolve;});
  return Object.freeze({
    pause(value){
      if(entered)throw Error(`${label} gate was entered more than once`);
      entered=true;resolveReady(value);
      return resume.then(()=>{
        if(wasAborted)throw Error(`${label} gate was aborted`);
      });
    },
    wait(){return ready.then(value=>{
      if(value===aborted)throw Error(`${label} gate was aborted before entry`);
      return value;
    });},
    release(){
      if(!entered||resumed)throw Error(`${label} gate release is stale or duplicated`);
      resumed=true;resolveResume();
    },
    abort(){
      if(resumed)return;
      wasAborted=true;
      if(!entered){entered=true;resolveReady(aborted);}
      resumed=true;resolveResume();
    },
    get entered(){return entered;},get resumed(){return resumed;},
  });
}

function matchesBytes(value, expected){
  return Array.isArray(value)&&value.length===expected.length&&
    value.every((byte,index)=>Number.isInteger(byte)&&byte>=0&&byte<=255&&byte===expected[index]);
}

export async function waitForLateRemotePairAndResume({tick,sourcePoll,
  requireFollowingNeutral=false,waitForRemoteSample,resumeLateFrontier}={}){
  if(!Number.isSafeInteger(tick)||tick<0||
      !sourcePoll||sourcePoll.sourceFrame!==tick||sourcePoll.localPort!==1||
      sourcePoll.remotePort!==0||
      typeof sourcePoll.pollSerial!=='string'||!Array.isArray(sourcePoll.raw)||
      sourcePoll.raw.length!==44||
      !sourcePoll.raw.every(byte=>Number.isInteger(byte)&&byte>=0&&byte<=255)||
      typeof waitForRemoteSample!=='function'||typeof resumeLateFrontier!=='function'||
      typeof requireFollowingNeutral!=='boolean')
    throw Error('Late target receipt requires the retained actual beta native source poll');

  const actualPad=sourcePoll.raw.slice(sourcePoll.localPort*11,(sourcePoll.localPort+1)*11);
  const targetPromise=Promise.resolve().then(()=>waitForRemoteSample(tick));
  const followingPromise=requireFollowingNeutral
    ?Promise.resolve().then(()=>waitForRemoteSample(tick+1)):Promise.resolve(null);
  const [targetValue,followingValue]=await Promise.all([targetPromise,followingPromise]);
  const receiptOf=value=>value?.receipt??value;
  const target=receiptOf(targetValue),following=receiptOf(followingValue);
  const validReceipt=(receipt,sourceTick,pad)=>receipt&&
    receipt.accepted_by==='lockstep-protocol'&&receipt.sender_role==='beta'&&
    receipt.remote_port===1&&receipt.protocol_input_tick===sourceTick&&
    matchesBytes(receipt.pad,pad);
  if(!validReceipt(target,tick,actualPad))
    throw Error('Late target receipt differs from the retained actual beta native source poll');
  if(requireFollowingNeutral&&!validReceipt(following,tick+1,new Array(11).fill(0)))
    throw Error('Following beta receipt is not the actual neutral source poll');

  await resumeLateFrontier(target);
  return {target:structuredClone(target),followingNeutral:following?structuredClone(following):null};
}

export function installRuntimePacedHistoryProbe({role,
  inputTicks=RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,lateClassPiece=false}={}){
  const owner=globalThis.__meleeRuntimeMatchHistory;
  const peer=globalThis.__meleeRuntimePeerHistoryProbe;
  if(!owner||!peer||!['alpha','beta'].includes(role)||globalThis.__meleeRuntimePacedHistory||
      ![RUNTIME_MATCH_HISTORY_REBASE_INPUT_TICKS,RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS,RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS].includes(inputTicks) ||
      typeof lateClassPiece!=='boolean' || (lateClassPiece&&inputTicks!==RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS))
    throw Error('Paced probe requires the existing exclusive owners and an explicit supported finite horizon');
  const targetFrame=inputTicks;
  const paced120=inputTicks===RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS;
  const respawn=inputTicks===RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS;
  const lateTicks=respawn?RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS:lateClassPiece?[26]:[];
  const lateKeys=respawn?RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_KEYS:[];
  const lateEvents=new Map(lateTicks.map(tick=>[tick,{
    scenario:{target_source_tick:tick,anchor:null,keyboard_change:null,held_packet:null,
      release_gate:null,alpha_frontier:null,network_release:null},
    key:pauseGate('late-key-change'),packet:pauseGate('late-packet-release'),
    frontier:pauseGate('late-alpha-frontier'),
  }]));
  const walkStart=pauseGate('respawn-walk-start'),walkStop=pauseGate('respawn-walk-stop');
  const recoveryStart=pauseGate('respawn-platform-sample');
  const recoveryStop=pauseGate('respawn-platform-input-complete');
  const report={scope:respawn?'600 actual source inputs, three deferred directional peer packets, and one sampled P1 respawn-platform exit':
      lateClassPiece?'120 actual source inputs with one deferred actual peer packet at source tick 26; existing clock and rollback bounds':
      paced120?'120 actual source inputs; functional pacing clock trace with native-sized rollback windows':
      'eight actual source inputs; native admission and measured headless cadence',
    input_ticks:inputTicks,role,result:'pending',events:[],presentations:[],admissions:[],polls:[],
    authoritativeInputs:[],corrections:[],rebases:[],callbacks:[],failure:null,
    ...(lateClassPiece?{late_class_piece:lateEvents.get(26).scenario}:{}),
    ...(respawn?{late_corrections:[...lateEvents.values()].map(event=>event.scenario),stock_samples:[],
      respawn_recovery:null}:{}),
  };
  let lateAborted=false;
  const pauseScenario=async(gate,value,label)=>{
    if(lateAborted)throw Error(`${label} was aborted before entering its gate`);
    await gate.pause(value);
    if(lateAborted)throw Error(`${label} was aborted before leaving its gate`);
  };
  const lateEvent=(tick=26)=>{
    const event=lateEvents.get(tick);
    if(!event)throw Error('No configured late input at the requested source tick');
    return event;
  };
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
  const observeStock=({metadata,pendingCommand,gpuPending,endpointActive,firstFailure,rendererErrors,ownerState})=>{
    const module=globalThis.Module;
    if(ownerState!==1||pendingCommand||gpuPending||endpointActive||firstFailure||rendererErrors||
        (module?.Asyncify&&module.Asyncify.state!==0))
      throw Error('Sparse stock observation requires quiescent history ownership');
    // The awaited native idle command proves Asyncify even when its JS object is private.
    const pointer=module._melee_web_native_menu_match_observe();
    if(!pointer)throw Error('Sparse native match observation is unavailable');
    const match=JSON.parse(module.UTF8ToString(pointer));
    const authoredPlayer=(player,index)=>{
      const {fighter,motion,groundAir,source_slot:slot}=player;
      const live=fighter===0&&Number.isInteger(motion)&&motion>=0&&[0,1].includes(groundAir);
      const absent=index===0&&fighter===-1&&motion===-1&&groundAir===-1;
      return player.human===true&&player.source_character===8&&player.source_player_index===index&&
        player.source_port===index&&Number.isInteger(slot)&&slot>=0&&
        (slot===0?index:slot-1)===index&&slot===match.rules.player_source_slots?.[index]&&
        (live||absent);
    };
    if(match.frame!==metadata.currentFrame||!match.ready||match.paused||match.ending||match.complete||
        match.outcome!==0||match.rules?.stage!==32||match.players?.length!==2||
        match.rules.match_kind!==1||match.rules.is_stock!==1||match.rules.is_vs!==1||
        match.rules.is_teams!==0||!match.players.every(authoredPlayer)||
        ![3,4].includes(match.players[0].stocks)||match.players[1].stocks!==4)
      throw Error(`Unexpected source stock observation: ${JSON.stringify(match)}`);
    const sample={frame:match.frame,kind:'post-present-idle',incarnation:metadata.incarnation,
      players:match.players.map(({stocks,fighter,motion,groundAir,facing_direction,x,y})=>
        ({stocks,fighter,motion,groundAir,facing_direction,x,y}))};
    report.stock_samples.push(sample);
    return sample;
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
    const recoveryFollow=respawn&&role==='alpha'&&recoveryStart.resumed&&
      !recoveryStop.entered&&
      row.idle.metadata.currentFrame===report.respawn_recovery?.platform_sample?.frame+1;
    if(respawn&&row.idle.metadata.currentFrame>=28&&
        (row.idle.metadata.currentFrame%4===0||kind!=='forward'||
          row.idle.metadata.currentFrame===targetFrame||recoveryFollow)){
      const sample=observeStock(state());
      if(role==='alpha'&&walkStart.resumed&&!walkStop.entered&&sample.players[0].stocks===3){
        report.walk_stop_frame=sample.frame;
        await pauseScenario(walkStop,sample,'Respawn walk-stop');
      }
      const p1=sample.players[0];
      if(respawn&&role==='alpha'&&walkStop.resumed&&!recoveryStart.entered&&
          p1.stocks===3&&p1.motion===13&&p1.groundAir===1){
        if(!Number.isFinite(p1.facing_direction)||![-1,1].includes(p1.facing_direction)||
            !Number.isFinite(p1.x)||!Number.isFinite(p1.y))
          throw Error('Respawn platform control requires actual finite P1 facing and position');
        report.respawn_recovery={platform_sample:sample,input_poll:null,
          following_neutral_poll:null};
        await pauseScenario(recoveryStart,sample,'Respawn platform sample');
      }
      if(recoveryFollow){
        const startFrame=report.respawn_recovery.platform_sample.frame;
        const poll=report.polls.find(item=>item.sourceFrame===startFrame);
        if(!poll||poll.localPort!==0||poll.remotePort!==1||
            typeof poll.pollSerial!=='string'||!Array.isArray(poll.raw)||poll.raw.length!==44)
          throw Error('Respawn platform input lost its exact following native P1 poll');
        report.respawn_recovery.input_poll=structuredClone(poll);
        await pauseScenario(recoveryStop,{sample,poll},'Respawn platform input completion');
      }
    }
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
      const event=lateEvents.get(remoteNext);
      if(event&&role==='alpha')
        event.scenario.remote_admission={tick:remoteNext,receipt:admission.receipt,
          timeline:admission.timeline};
      remoteNext++;
    }
  };
  const reconcile=async(snapshot)=>{
    const current=state().metadata,pre=current.preFrame,frontier=current.currentFrame;
    const confirmed=Math.min(snapshot.remote_contiguous_through+1,frontier);
    if(confirmed<=pre||confirmed>frontier)throw Error('Paced correction lacks a newer confirmed anchor');
    await sealPresented();
    const lateScenario=lateEvents.get(snapshot.dirty_from)?.scenario;
    const lateFrontier=lateScenario?.alpha_frontier;
    if(role==='alpha'&&lateFrontier?.pre_frame===pre&&
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
    if(respawn&&snapshot.dirty_from!==null)
      report.stock_samples=report.stock_samples.filter(sample=>sample.frame<=snapshot.dirty_from);
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
    if(respawn&&role==='alpha'&&snapshot.dirty_from===lateTicks[0]&&!walkStart.entered){
      if(pre!==21||frontier!==28)throw Error('Walk-off requires the first exact late correction');
      report.walk_start_frame=frontier;
      const baseline=report.stock_samples.at(-1);
      if(baseline?.frame!==frontier||baseline.players[0].stocks!==4)
        throw Error('Walk-off requires its actual corrected four-stock baseline');
      await pauseScenario(walkStart,baseline,'Respawn walk-start');
    }
  };
  const api=Object.freeze({
    snapshot:()=>structuredClone({...report,clock:owner.pacingJournal()}),
    lateSnapshot(tick){
      return structuredClone({scenario:lateEvent(tick).scenario,
        poll:report.polls.find(row=>row.sourceFrame===tick)});
    },
    waitForLateKeyChange(tick){if(role!=='beta')throw Error('Only beta owns the late key-change gate');return lateEvent(tick).key.wait();},
    noteLateKeyChange({tick=26,from,to}={}){
      const {key,scenario}=lateEvent(tick);
      const targetKey=respawn?lateKeys[lateTicks.indexOf(tick)]:'ArrowRight';
      if(role!=='beta'||!key.entered||key.resumed||scenario.keyboard_change||
          from!==(respawn?null:'ArrowLeft')||to!==targetKey)
        throw Error('Late key change must match the configured Playwright direction transition');
      const {preFrame,currentFrame}=state().metadata;
      if(currentFrame!==tick)throw Error('Late key change lost its source frame');
      scenario.keyboard_change={source_tick:tick,from,to,pre_frame:preFrame,
        current_frame:currentFrame,driver:'Playwright page.keyboard'};
      return structuredClone(scenario.keyboard_change);
    },
    resumeLateKeyChange(tick){
      const {key,scenario}=lateEvent(tick);
      if(role!=='beta'||!scenario.keyboard_change)throw Error('Late native poll cannot resume before its recorded key change');
      key.release();
    },
    waitForLatePacketGate(tick){if(role!=='beta')throw Error('Only beta owns the held-packet gate');return lateEvent(tick).packet.wait();},
    waitForLateFrontier(tick){if(role!=='alpha')throw Error('Only alpha owns the delayed-input frontier');return lateEvent(tick).frontier.wait();},
    async releaseLatePacket(tick=26){
      const {packet,scenario}=lateEvent(tick);
      if(role!=='beta'||!packet.entered||packet.resumed||scenario.network_release||!scenario.held_packet)
        throw Error('Late packet release requires the exact held beta source poll at the release gate');
      const released=await call('episodeReleaseLocal',{tick});
      const receipt=released.receipt,held=scenario.held_packet;
      if(receipt.protocol_input_tick!==tick||receipt.poll_serial!==held.poll_serial||
          receipt.local_port!==held.local_port||
          JSON.stringify(receipt.pad)!==JSON.stringify(held.actual_local_pad))
        throw Error('Late packet release changed the retained source poll identity or bytes');
      scenario.network_release={source_tick:tick,poll_serial:receipt.poll_serial,
        local_port:receipt.local_port,actual_local_pad:[...receipt.pad],source_poll:released.source};
      return structuredClone(scenario.network_release);
    },
    resumeLatePacket(tick){
      const {packet,scenario}=lateEvent(tick);
      if(role!=='beta'||!scenario.network_release)throw Error('Beta source loop cannot resume before the held packet was released');
      packet.release();
    },
    resumeLateFrontier(receipt){
      const {frontier,scenario}=lateEvent(receipt?.protocol_input_tick);
      if(role!=='alpha'||!frontier.entered||frontier.resumed||
          receipt.accepted_by!=='lockstep-protocol'||receipt.sender_role!=='beta'||
          receipt.remote_port!==1||!Array.isArray(receipt.pad)||receipt.pad.length!==11)
        throw Error('Alpha source loop requires the received actual beta packet before frontier release');
      scenario.remote_receipt=structuredClone(receipt);
      frontier.release();
    },
    waitLateRemotePairAndResume({tick,sourcePoll,requireFollowingNeutral=false}={}){
      if(role!=='alpha')throw Error('Only alpha owns the delayed-input frontier');
      return waitForLateRemotePairAndResume({tick,sourcePoll,requireFollowingNeutral,
        waitForRemoteSample:sourceTick=>peer.call('peerWaitRemote',{tick:sourceTick,timeoutMs:5000}),
        resumeLateFrontier:receipt=>api.resumeLateFrontier(receipt)});
    },
    waitForWalkStart(){return walkStart.wait();},
    resumeWalkStart(){walkStart.release();},
    waitForWalkStop(){return walkStop.wait();},
    resumeWalkStop(){walkStop.release();},
    waitForRespawnRecoveryStart(){
      if(role!=='alpha')throw Error('Only alpha owns the actual P1 respawn platform sample');
      return recoveryStart.wait();
    },
    resumeRespawnRecoveryStart(){
      if(role!=='alpha'||!recoveryStart.entered||recoveryStart.resumed)
        throw Error('P1 recovery input requires its actual post-idle platform sample');
      recoveryStart.release();
    },
    waitForRespawnRecoveryStop(){
      if(role!=='alpha')throw Error('Only alpha owns the P1 recovery input completion gate');
      return recoveryStop.wait();
    },
    resumeRespawnRecoveryStop(){
      if(role!=='alpha'||!recoveryStop.entered||recoveryStop.resumed)
        throw Error('P1 recovery completion gate is stale or duplicated');
      recoveryStop.release();
    },
    abortLateScenario(){
      lateAborted=true;
      for(const gate of [walkStart,walkStop,recoveryStart,recoveryStop,
        ...[...lateEvents.values()].flatMap(event=>[event.key,event.packet,event.frontier])])
        gate.abort();
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
          const activeLate=[...lateEvents.values()].find(({scenario})=>frame<=scenario.target_source_tick+2||
            !scenario.anchor);
          const lateScenario=activeLate?.scenario;
          const target=lateScenario?.target_source_tick;
          const lateAnchorFrame=target-5;
          const needsLateAnchor=lateScenario&&!lateScenario.anchor;
          const atLateAnchor=needsLateAnchor&&frame===lateAnchorFrame;
          if(needsLateAnchor&&frame>lateAnchorFrame)
            throw Error('Late-piece run advanced past its required native anchor');
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
          if(lateScenario&&role==='beta'&&frame===target&&!lateScenario.keyboard_change){
            if(current.preFrame>target)throw Error('Late key-change target is already before the active native PRE');
            await pauseScenario(activeLate.key,{source_tick:frame,pre_frame:current.preFrame,
              current_frame:current.currentFrame},'Late key change');
          }
          if(lateScenario&&role==='alpha'&&!lateScenario.alpha_frontier){
            const frontier=Math.min(target+2,current.preFrame+7);
            if(frame===frontier&&frame>target){
              if(!(lateScenario.anchor?.pre_frame===lateAnchorFrame&&current.preFrame===lateAnchorFrame&&
                  frame===target+2&&target<frame&&frame<=current.preFrame+7))
                throw Error('Late alpha frontier is outside the current native PRE and seven-tick bound');
              if(inputs.remote_contiguous_through<target-1){
                report.events.push({operation:'late-prefix-network-wait',frame,
                  remote_contiguous_through:inputs.remote_contiguous_through,dirty_from:inputs.dirty_from});
                continue;
              }
              if(inputs.remote_contiguous_through!==target-1||inputs.dirty_from!==null)
                throw Error('Late alpha frontier observed an escaped late tick or an earlier dirty correction');
              lateScenario.alpha_frontier={pre_frame:current.preFrame,frontier:frame,
                remote_contiguous_through:inputs.remote_contiguous_through,dirty_from:inputs.dirty_from,
                span:frame-current.preFrame};
              await pauseScenario(activeLate.frontier,structuredClone(lateScenario.alpha_frontier),'Late alpha frontier');
              continue;
            }
          }
          if(lateScenario&&role==='beta'&&frame===target+1&&lateScenario.held_packet&&
              !lateScenario.release_gate){
            lateScenario.release_gate={pre_frame:current.preFrame,frontier:frame,
              remote_contiguous_through:inputs.remote_contiguous_through};
            await pauseScenario(activeLate.packet,structuredClone(lateScenario.release_gate),'Late beta packet');
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
            for(const [tick,{scenario:lateScenario,packet}] of lateEvents){
              if(role==='beta'&&(!lateScenario.keyboard_change||!lateScenario.held_packet||
                  !lateScenario.network_release||!packet.resumed))
                throw Error('Late beta source poll, held packet, or exact packet release was not completed');
              if(role==='alpha'){
                const correction=report.corrections.find(row=>row.dirty===tick);
                if(!correction||!lateScenario.alpha_frontier||!lateScenario.remote_admission||
                    !lateScenario.remote_receipt||correction.pre!==tick-5||correction.frontier!==tick+2||
                    correction.frontier>correction.pre+7||correction.frontier!==lateScenario.alpha_frontier.frontier||
                    correction.restore_status?.metadata?.preFrame!==correction.pre||
                    correction.restore_status?.metadata?.sealedEnd!==correction.frontier||
                    correction.restore_status?.metadata?.restoreCount<1)
                  throw Error('Late input did not cross a bounded native restore lifecycle');
                const plan=correction.prefix?.plan??correction.replay?.plan;
                const frame=plan?.frames?.find(row=>row.tick===tick);
                if(!frame||frame.remote_source!=='actual'||frame.speculative!==false)
                  throw Error('Late input restore did not use its actual peer-confirmed PAD');
                lateScenario.correction={kind:correction.kind,pre:correction.pre,
                  frontier:correction.frontier,confirmed:correction.confirmed,dirty:correction.dirty,
                  restore_count:correction.restore_status.metadata.restoreCount,
                  corrected_tick:frame.tick,remote_source:frame.remote_source};
              }
            }
            if(respawn&&role==='alpha'&&(!walkStart.resumed||!walkStop.resumed))
              throw Error('Respawn source run lost its owned walk-off gates');
            if(respawn&&role==='alpha'&&(!recoveryStart.resumed||!recoveryStop.resumed||
                !report.respawn_recovery?.input_poll))
              throw Error('Respawn source run lost its actual P1 platform-exit input');
            report.result=respawn?'PACED_600_RESPAWN_COMPLETE':paced120?'PACED_120_INPUTS_COMPLETE':'PACED_EIGHT_INPUTS_COMPLETE';break;
          }
          const fullWindow=frame-current.preFrame===7;
          const cleanRebase=lateEvents.size>0
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
          const deferLatePacket=role==='beta'&&lateEvents.has(frame);
          const publication=await call('episodePublish',{tick:frame,ticketId:polled.ticket.ticketId,
            ...(deferLatePacket?{deferSend:true}:{})});
          if(deferLatePacket){
            if(publication?.deferred_send!==true||publication.receipt?.protocol_input_tick!==frame||
                publication.receipt?.poll_serial!==polled.ticket.pollSerial)
              throw Error('Late packet hold did not retain the exact late native poll');
            const localPort=polled.ticket.localPort;
            lateEvent(frame).scenario.held_packet={source_tick:frame,poll_serial:polled.ticket.pollSerial,
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
