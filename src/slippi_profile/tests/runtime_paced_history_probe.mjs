// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded page-owned cadence probe. Native code owns source ticks, clock,
// snapshots, renderer submission and idle. No CDP operation drives this loop.
export function installRuntimePacedHistoryProbe({role}={}){
  const owner=globalThis.__meleeRuntimeMatchHistory;
  const peer=globalThis.__meleeRuntimePeerHistoryProbe;
  if(!owner||!peer||!['alpha','beta'].includes(role)||globalThis.__meleeRuntimePacedHistory)
    throw Error('Paced probe requires the existing exclusive runtime and peer owners');
  const report={scope:'eight actual source inputs; native admission and measured headless cadence',
    role,result:'pending',events:[],presentations:[],admissions:[],polls:[],
    authoritativeInputs:[],corrections:[],rebases:[],failure:null};
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
    while(remoteNext<8){
      const receipt=await call('peerRemoteSnapshot',{tick:remoteNext});
      if(!receipt)break;
      await call('episodeAdmit',{tick:remoteNext});remoteNext++;
    }
  };
  const reconcile=async(snapshot)=>{
    const current=state().metadata,pre=current.preFrame,frontier=current.currentFrame;
    const confirmed=Math.min(snapshot.remote_contiguous_through+1,frontier);
    if(confirmed<=pre||confirmed>frontier)throw Error('Paced correction lacks a newer confirmed anchor');
    await sealPresented();
    if(snapshot.dirty_from===null&&confirmed===frontier){
      await closeRebase(frontier);return;
    }
    await command('restore');admit(1,1);
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
        dirty:snapshot.dirty_from,checksum,prefix,adopted});
    }else{
      const replay=await call('episodeReplay',{dirty:snapshot.dirty_from,
        through:frontier-1,speculative:0});
      retainRows(replay.rows);admit(2,3);
      const checksum=[...owner.checksum()];
      await present(false,'corrected-frontier');admit(3,3);
      report.corrections.push({kind:'fully-confirmed-correction',pre,frontier,confirmed,
        dirty:snapshot.dirty_from,checksum,replay});
      if(frontier!==8)await closeRebase(frontier);
    }
  };
  const api=Object.freeze({
    snapshot:()=>structuredClone({...report,clock:owner.pacingJournal()}),
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
        for(;;){
          await new Promise(resolve=>requestAnimationFrame(resolve));
          // Budget zero accounts for actual elapsed time before reconciliation;
          // the clock is never reset during a network wait or state transfer.
          owner.pacingSample(0);
          await admitAvailable();
          let inputs=await timeline(),current=state().metadata;
          const frame=current.currentFrame,confirmed=inputs.remote_contiguous_through+1;
          if(frame===8&&confirmed===8&&inputs.dirty_from===null){
            // The final endpoint is explicitly post-draw, not CPU-before-draw.
            if(current.phase!==16)await sealPresented();
            report.finalChecksum={view:current.phase===16?'corrected-draw-end':'presented-frontier',
              bytes:[...owner.checksum({postDraw:true})]};
            owner.pacingSample(0);
            report.clockFinishedAt=performance.now();
            await command('close');await command('retire');
            report.finalOwner=state();
            report.authoritativeInputs=Array.from({length:8},(_,tick)=>({tick,bytes:selected.get(tick)}));
            if(report.authoritativeInputs.some(row=>!row.bytes||row.bytes.length!==44))
              throw Error('Paced terminal lost an authoritative input row');
            report.result='PACED_EIGHT_INPUTS_COMPLETE';break;
          }
          if(inputs.dirty_from!==null||
              (frame-current.preFrame>=4&&confirmed>current.preFrame)){
            await reconcile(inputs);
            owner.pacingSample(0);
            continue;
          }
          // Remote samples ahead of local execution are retained by the existing
          // transport/timeline; only actual simulated inputs can become anchors.
          if(frame===8||frame-current.preFrame>=7||frame-inputs.remote_contiguous_through>7){
            report.events.push({operation:'network-wait',frame,confirmed});continue;
          }
          const clock=owner.pacingSample(1);
          if(clock.steps===0)continue;
          if(clock.steps!==1||clock.currentFrame!==frame)throw Error('Paced grant differs from actual source frame');
          const polled=await call('episodePoll',{tick:frame});report.polls.push(polled.ticket);
          await call('episodePublish',{tick:frame,ticketId:polled.ticket.ticketId});
          // Network callbacks may retain packets during the reserved poll, but
          // this owner admits them only between complete source operations.
          const actual=frame<=inputs.remote_contiguous_through;
          const forwarded=await call('episodeForward',{tick:frame,ticketId:polled.ticket.ticketId,actual});
          retainRows([forwarded]);await present(true);
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
