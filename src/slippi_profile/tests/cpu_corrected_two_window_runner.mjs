// SPDX-License-Identifier: GPL-3.0-or-later
// Two independently checked wrong-prediction windows on one persistent input
// timeline. Native reuse/retirement is private diagnostic plumbing only.
import {
  correctedPageDefaults, operateCorrectedInPage, validateCorrectedCommandEvidence,
  recordPredictedActionDivergence,
} from './cpu_corrected_runner.mjs';
import {CPU_SEVEN_TICK_PAD44_CORPUS, createCpuSevenTickTimeline, decodeCpuSevenTickEvidence} from './cpu_seven_tick_runner.mjs';
import {collectCpuEndpointInPage, decodeCpuParticleEndpointResult, compareCpuParticleEndpoints,
  validateCpuParticleEndpointAgainstPre} from './cpu_endpoint.mjs';
import {runBrowserSample} from './run_synctest.mjs';
import {
  decodeDrawDiagnostics, inspectInteractiveObservation, validateAuroraFrame,
  validateSelectedDrawPath, validateSourceInputMapping,
} from './runner_contract.mjs';

export const CPU_CORRECTED_REUSE_EVIDENCE_BYTES = 40;
export const CPU_CORRECTED_TWO_WINDOW_BRIDGE_LIMIT = 60;
export const CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44 =
  'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=';
export const CPU_CORRECTED_TWO_WINDOW_REMOTE_B_PAD44 =
  'AAAAAAAAAAAAAAACAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=';

const PAD_BYTES=11, FRAME_BYTES=44, WINDOW_TICKS=7;
const rawLanes={observation:1280,pcm:4272,pad:44,rngProfile:32,transfer:40};
const decode=value=>Buffer.from(value,'base64');
const encode=value=>Buffer.from(value).toString('base64');
const padSlot=(pad,port)=>pad.subarray(port*PAD_BYTES,(port+1)*PAD_BYTES);
const equal=(a,b)=>Buffer.from(a).equals(Buffer.from(b));

export function buildCpuCorrectedTwoWindowFirstCorpus(localPort=0,remotePort=1){
  if(localPort!==0||remotePort!==1)throw new Error('Two-window witness is pinned to neutral port 0 and S4 port 1');
  return Object.freeze(CPU_SEVEN_TICK_PAD44_CORPUS.map((source,index)=>{
    const pad=decode(source);
    pad.fill(0,localPort*PAD_BYTES,(localPort+1)*PAD_BYTES);
    if(pad.length!==FRAME_BYTES||pad[remotePort*PAD_BYTES]===0&&index===0||
       pad[2*PAD_BYTES+10]!==0xff||pad[3*PAD_BYTES+10]!==0xff)
      throw new Error(`Two-window first corpus source frame ${index} is not the pinned S4/disconnected layout`);
    return encode(pad);
  }));
}

export function buildCpuCorrectedTwoWindowSecondCorpus(){
  const rows=[CPU_CORRECTED_TWO_WINDOW_REMOTE_B_PAD44,
    ...Array(WINDOW_TICKS-1).fill(CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44)];
  for(const [index,row] of rows.entries()){
    const pad=decode(row);
    if(pad.length!==FRAME_BYTES||pad[0]!==0||pad[PAD_BYTES]!== (index===0?2:0)||
       pad[2*PAD_BYTES+10]!==0xff||pad[3*PAD_BYTES+10]!==0xff)
      throw new Error(`Two-window second corpus source frame ${index} is not the pinned B/neutral layout`);
  }
  return Object.freeze(rows);
}

export function decodeCpuCorrectedReuseEvidence(input){
  const bytes=Buffer.from(input);
  if(bytes.length!==CPU_CORRECTED_REUSE_EVIDENCE_BYTES)throw new Error('Corrected reuse evidence extent');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const values=Array.from({length:10},(_,index)=>view.getUint32(index*4,true));
  if(values[0]!==1||values[1]!==CPU_CORRECTED_REUSE_EVIDENCE_BYTES||values[9]!==0)
    throw new Error('Corrected reuse evidence schema/reserved field');
  return {schema:values[0],bytes:values[1],windowsCaptured:values[2],windowsRetired:values[3],
    activeCapsules:values[4],maxActiveCapsules:values[5],windowFirstSample:values[6],
    windowPreFrame:values[7],windowPostFrame:values[8],reserved:values[9]};
}

export function compareCpuCorrectedRawFive(reference,corrected,label){
  for(const [lane,size] of Object.entries(rawLanes)){
    const left=decode(reference?.[lane]??''),right=decode(corrected?.[lane]??'');
    if(left.length!==size||right.length!==size)throw new Error(`${label} ${lane} raw extent`);
    if(!left.equals(right)){
      let offset=0;while(offset<left.length&&left[offset]===right[offset])offset++;
      throw new Error(`${label} raw ${lane} differs at byte ${offset}`);
    }
  }
  return {equal:true,lanes:Object.keys(rawLanes)};
}

function requirePad44(pad44){
  const pad=decode(pad44);
  if(pad.length!==FRAME_BYTES)throw new Error('Two-window input must be one literal 44-byte PAD frame');
  if(pad[2*PAD_BYTES+10]!==0xff||pad[3*PAD_BYTES+10]!==0xff)
    throw new Error('Two-window corpus must preserve disconnected ports 2 and 3');
  return pad;
}

export function planCpuCorrectedWindowForward(schedule,tick,pad44){
  if(!schedule||schedule.pendingPlan)throw new Error('Two-window forward plan is already outstanding');
  const actual=requirePad44(pad44),status=schedule.timeline.status();
  if(!Number.isSafeInteger(tick)||tick!==status.simulated_through+1)
    throw new Error(`Two-window forward tick is not contiguous at ${tick}`);
  schedule.timeline.recordLocal(tick,padSlot(actual,schedule.localPort));
  const plan=schedule.timeline.planForward();
  const expected=Buffer.from(actual);
  expected.set(schedule.initialRemotePad,schedule.remotePort*PAD_BYTES);
  if(plan.kind!=='forward'||plan.tick!==tick||plan.remote_source!=='last_known_prediction'||
     plan.speculative_ticks<1||plan.speculative_ticks>7||!equal(plan.frame,expected))
    throw new Error(`Two-window prediction plan differs at tick ${tick}`);
  schedule.pendingPlan=plan;
  return plan;
}

export function acknowledgeCpuCorrectedWindowForward(schedule,plan){
  if(!schedule||!plan||schedule.pendingPlan!==plan)throw new Error('Two-window forward acknowledgement is stale');
  schedule.timeline.commitForward(plan);
  schedule.pendingPlan=null;
}

export function planCpuCorrectedWindowReplay(schedule,rows,startTick){
  if(!schedule||schedule.pendingPlan||!Array.isArray(rows)||rows.length!==WINDOW_TICKS)
    throw new Error('Two-window replay requires seven executed rows and no outstanding forward plan');
  let earliestChanged=null;
  for(let index=0;index<WINDOW_TICKS;index++){
    const tick=startTick+index,row=rows[index],actual=requirePad44(row?.pad44Base64);
    if(row.sample!==tick)throw new Error(`Two-window authoritative row tick mismatch at ${tick}`);
    const accepted=schedule.timeline.acceptRemote(tick,padSlot(actual,schedule.remotePort));
    if(accepted.status!=='accepted')throw new Error(`Two-window remote PAD ${tick} refused: ${JSON.stringify(accepted)}`);
    if(earliestChanged===null&&accepted.earliest_changed_tick!==null)
      earliestChanged=accepted.earliest_changed_tick;
  }
  const status=schedule.timeline.status(),through=startTick+WINDOW_TICKS-1;
  if(earliestChanged!==startTick||status.dirty_from!==startTick||status.remote_contiguous_through!==through)
    throw new Error(`Two-window arrivals did not dirty the complete seven-tick suffix: ${JSON.stringify({earliestChanged,status})}`);
  const replay=schedule.timeline.planReplay();
  if(replay.kind!=='replay'||replay.from!==startTick||replay.through!==through||
     replay.frames.length!==WINDOW_TICKS||replay.speculative_ticks!==0)
    throw new Error('Two-window correction plan does not cover exactly the changed suffix');
  const frames=replay.frames.map((frame,index)=>{
    const actual=requirePad44(rows[index].pad44Base64);
    if(frame.tick!==startTick+index||!equal(frame.local_pad,padSlot(actual,schedule.localPort))||
       !equal(frame.remote_pad,padSlot(actual,schedule.remotePort))||!equal(frame.frame,actual))
      throw new Error(`Two-window replay changed original local or authoritative bytes at ${startTick+index}`);
    return {tick:frame.tick,frame:Buffer.from(frame.frame),localPad:Buffer.from(frame.local_pad),
      remotePad:Buffer.from(frame.remote_pad)};
  });
  return {earliestChanged,dirtyFrom:status.dirty_from,remoteContiguousThrough:status.remote_contiguous_through,
    replay,frames};
}

function expectedPhase(command){return command===32?1:command<=38?2:command===39?3:
  command===40?4:command===41?5:command===42?6:command<=48?6:command===49?7:
  command===50?8:command===51?9:0;}

function requireReuse(evidence,{captured,retired,active,sample,preFrame,postFrame},label){
  const expected={windowsCaptured:captured,windowsRetired:retired,activeCapsules:active,
    maxActiveCapsules:1,windowFirstSample:sample,windowPreFrame:preFrame,windowPostFrame:postFrame};
  for(const [key,value] of Object.entries(expected))if(evidence[key]!==value)
    throw new Error(`${label} reuse ${key}=${evidence[key]}, expected ${value}`);
  return evidence;
}

function compareWindowRawPairs(reference,corrected,startTick){
  if(reference.tickPairs?.length!==7||corrected.tickPairs?.length!==7)
    throw new Error('Two-window raw comparison requires seven pairs per page');
  for(let index=0;index<7;index++){
    const a=reference.tickPairs[index],b=corrected.tickPairs[index],sample=startTick+index;
    if(a.preFrame!==b.preFrame||a.postFrame!==b.postFrame)
      throw new Error(`Two-window raw source frame differs at tick ${sample}`);
    for(const lane of ['Observation','Pcm','Pad','RngProfile','Transfer'])
      if(!equal(decode(a[`baseline${lane}`]),decode(b[`replay${lane}`])))
        throw new Error(`Two-window raw ${lane} differs at tick ${sample}`);
  }
  return {equal:true,ticks:7,lanes:['Observation','Pcm','Pad','RngProfile','Transfer']};
}

function requireWaitReady(boundary){
  if(!boundary?.interactive||boundary.ready!==1||boundary.ending!==0||boundary.complete!==0||boundary.paused!==0||
     !Array.isArray(boundary.players)||boundary.players.length!==2||
     boundary.players.some((player,index)=>player.sourceSlot!==index||player.stocks!==4||player.motion!==14))
    return false;
  return true;
}

export async function runBrowserCpuCorrectedTwoWindow(predictedPage,referencePage,wire,checkpoint,
  expectedInputMapping,stocks,previousFrame,beforeDiagnostics){
  const mapping=validateSourceInputMapping(expectedInputMapping,beforeDiagnostics.slotCount);
  const localPort=mapping[0].rawController,remotePort=mapping[1].rawController;
  if(localPort!==0||remotePort!==1||stocks.length!==2||stocks.some(value=>value!==4))
    throw new Error('Two-window witness requires source player ports 0/1 and four stocks each');
  if(!equal(padSlot(decode(checkpoint.pad),remotePort),decode('AAAAAAAAAAAAAAA=')))
    throw new Error('Two-window explicit initial remote PAD differs from the neutral checkpoint');
  const schedule=createCpuSevenTickTimeline(localPort,remotePort,Buffer.alloc(PAD_BYTES));
  const firstCorpus=buildCpuCorrectedTwoWindowFirstCorpus(localPort,remotePort);
  const secondCorpus=buildCpuCorrectedTwoWindowSecondCorpus();
  const report={schema:'independent-browser-correction-two-window-v1',status:'running',
    timeline:{createdAt:300,initialRemotePadBase64:encode(Buffer.alloc(PAD_BYTES)),
      predictionLimit:7,persistentAcrossWindows:true},windows:[],bridge:{status:'not-started',maximumTicks:CPU_CORRECTED_TWO_WINDOW_BRIDGE_LIMIT},
    scope:'two same-match private input-correction windows with checked retirement between them; no live relay, whole-game snapshot, or profile acceptance'};
  const defaults=correctedPageDefaults(mapping,stocks);
  const stateFor=page=>page===referencePage?report.reference:report.predicted;
  report.reference={commands:[],rows:[],endpoints:{}};report.predicted={commands:[],rows:[],endpoints:{}};
  const gpu=new Map([[referencePage,previousFrame],[predictedPage,previousFrame]]);
  const diagnostics=new Map([[referencePage,beforeDiagnostics],[predictedPage,beforeDiagnostics]]);
  const operate=async(page,command,options={})=>{
    const result=await page.evaluate(operateCorrectedInPage,{...defaults,...options,command});
    if(command===53){
      const reuse=decodeCpuCorrectedReuseEvidence(decode(result.reuseEvidenceBase64));
      stateFor(page).commands.push({command,result:0,reuse,timing:result.timing});
      return {reuse,timing:result.timing};
    }
    const evidence=decodeCpuSevenTickEvidence(decode(result.evidenceBase64),2);
    validateCorrectedCommandEvidence(evidence,command,options.preFrame);
    const state=stateFor(page);state.nativeEvidence=evidence;
    state.commands.push({command,result:0,state:evidence.state,timing:result.timing});
    if(options.readReuseEvidence){
      result.reuse=decodeCpuCorrectedReuseEvidence(decode(result.reuseEvidenceBase64));
    }
    if(options.draw){
      const old=gpu.get(page),current=validateAuroraFrame(result.status,old.beginFrameId,old.endFrameId);
      const draw=decodeDrawDiagnostics(decode(result.drawDiagnosticsBase64));
      const selected=validateSelectedDrawPath(diagnostics.get(page),draw,result.boundary.players,mapping);
      state.draws??=[];state.draws.push({command,gpu:result.status,selected});
      gpu.set(page,current);diagnostics.set(page,draw);
    }
    return {result,evidence,reuse:result.reuse??null,timing:result.timing};
  };
  const endpoint=async(page,view,provenance,name)=>{
    const result=decodeCpuParticleEndpointResult(await page.evaluate(collectCpuEndpointInPage,
      {view,provenance,particleCellEndpoint:true}));
    stateFor(page).endpoints[name]=result;return result;
  };
  const runWindow=async({index,startTick,preFrame,corpus,enableReuse})=>{
    const captured=index+1,retired=index,active=1;
    const window={status:'running',index:index+1,startTick,preFrame,postFrame:preFrame+WINDOW_TICKS,
      reference:{commands:[],rows:[],endpoints:{}},predicted:{commands:[],rows:[],endpoints:{}},comparisons:{}};
    report.windows.push(window);
    const runCommand=async(page,command,options={})=>{
      const value=await operate(page,command,{...options,preFrame});
      const lane=page===referencePage?window.reference:window.predicted;
      lane.commands.push(command===53?{command,reuse:value.reuse,timing:value.timing}:
        {command,state:value.evidence.state,timing:value.timing});
      return value;
    };
    const setup={wire,reuseEnable:enableReuse,readReuseEvidence:true};
    const referenceCapture=await runCommand(referencePage,32,setup);
    const predictedCapture=await runCommand(predictedPage,32,setup);
    const captureExpected={captured,retired,active,sample:startTick,preFrame,postFrame:preFrame+7};
    requireReuse(referenceCapture.reuse,captureExpected,`window${index+1} reference capture`);
    requireReuse(predictedCapture.reuse,captureExpected,`window${index+1} predicted capture`);
    if(JSON.stringify(referenceCapture.reuse)!==JSON.stringify(predictedCapture.reuse))
      throw new Error(`Window ${index+1} capture metadata differs across pages`);
    const refPre=await endpoint(referencePage,1,1,'pre'),predPre=await endpoint(predictedPage,1,1,'pre');
    window.comparisons.pre=compareCpuParticleEndpoints(refPre,predPre);
    window.reference.endpoints.pre=refPre;window.predicted.endpoints.pre=predPre;
    for(let i=0;i<WINDOW_TICKS;i++){
      const tick=startTick+i,literal=corpus[i];
      const plan=planCpuCorrectedWindowForward(schedule,tick,literal);
      const predicted=encode(plan.frame);
      const ref=await runCommand(referencePage,33+i,{pad:literal,draw:i<6});
      const pred=await runCommand(predictedPage,33+i,{pad:predicted,draw:i<6});
      const refPair=ref.evidence.tickPairs[i],predPair=pred.evidence.tickPairs[i];
      if(!equal(decode(refPair.baselinePad),decode(literal))||
         !equal(decode(predPair.baselinePad),decode(predicted))||
         ref.result.boundary.sourceFrame!==preFrame+i+1||pred.result.boundary.sourceFrame!==preFrame+i+1)
        throw new Error(`Window ${index+1} native source boundary/input differs at tick ${tick}`);
      acknowledgeCpuCorrectedWindowForward(schedule,plan);
      const row={sample:tick,pad44Base64:literal,boundary:ref.result.boundary,
        predictedPad44Base64:predicted,predictedBoundary:pred.result.boundary,timelineAcknowledged:true};
      window.reference.rows.push(row);window.predicted.rows.push(row);
    }
    window.reference.baselineNativeEvidence=structuredClone(stateFor(referencePage).nativeEvidence);
    window.predicted.speculativeNativeEvidence=structuredClone(stateFor(predictedPage).nativeEvidence);
    const remoteMotionDivergence=window.reference.rows.some((row,index)=>
      row.boundary.players[1].motion!==window.predicted.rows[index].predictedBoundary.players[1].motion);
    if(!remoteMotionDivergence)throw new Error(`Window ${index+1} did not change the remote player's actual motion`);
    const refCpu=await endpoint(referencePage,2,2,'cpu');
    const predictedCpu=await endpoint(predictedPage,2,2,'speculativeCpu');
    validateCpuParticleEndpointAgainstPre(refCpu,refPre);
    validateCpuParticleEndpointAgainstPre(predictedCpu,predPre);
    window.reference.endpoints.cpu=refCpu;
    window.predicted.endpoints.speculativeCpu=predictedCpu;
    window.actionDivergence=recordPredictedActionDivergence(refCpu,predictedCpu);
    await runCommand(referencePage,40,{draw:true});await runCommand(referencePage,41);
    window.reference.endpoints.draw=await endpoint(referencePage,3,2,'draw');
    window.reference.nativeEvidence=stateFor(referencePage).nativeEvidence;
    await runCommand(predictedPage,40,{draw:true});await runCommand(predictedPage,41);
    window.predicted.endpoints.speculativeDraw=await endpoint(predictedPage,3,2,'speculativeDraw');
    validateCpuParticleEndpointAgainstPre(window.reference.endpoints.draw,refPre);
    validateCpuParticleEndpointAgainstPre(window.predicted.endpoints.speculativeDraw,predPre);
    window.predicted.nativeEvidence=stateFor(predictedPage).nativeEvidence;
    const correction=planCpuCorrectedWindowReplay(schedule,window.reference.rows,startTick);
    window.correction={earliestChanged:correction.earliestChanged,dirtyFrom:correction.dirtyFrom,
      remoteContiguousThrough:correction.remoteContiguousThrough,
      frames:correction.frames.map(frame=>({tick:frame.tick,frame:encode(frame.frame)}))};
    await runCommand(predictedPage,42);
    for(let i=0;i<WINDOW_TICKS;i++){
      const replay=correction.frames[i],result=await runCommand(predictedPage,43+i,{pad:encode(replay.frame)});
      if(result.result.boundary.sourceFrame!==preFrame+i+1||
         !equal(decode(result.evidence.tickPairs[i].replayPad),replay.frame))
        throw new Error(`Window ${index+1} corrected source input differs at tick ${replay.tick}`);
    }
    // Commands 43..49 replace this page's speculative tape with the replayed
    // tape; do not compare against the earlier command-41 evidence snapshot.
    window.predicted.nativeEvidence=stateFor(predictedPage).nativeEvidence;
    const correctedCpu=await endpoint(predictedPage,2,3,'cpu');
    window.predicted.endpoints.cpu=correctedCpu;
    window.comparisons.cpu=compareCpuParticleEndpoints(refCpu,correctedCpu,
      {preLeft:refPre,preRight:predPre});
    window.comparisons.raw=compareWindowRawPairs(window.reference.nativeEvidence,window.predicted.nativeEvidence,startTick);
    schedule.timeline.commitReplay(correction.replay);
    window.timelineAfterReplay=schedule.timeline.status();
    await runCommand(predictedPage,50,{draw:true});await runCommand(predictedPage,51);
    const correctedDraw=await endpoint(predictedPage,3,3,'draw');
    window.predicted.nativeEvidence=stateFor(predictedPage).nativeEvidence;
    window.predicted.endpoints.draw=correctedDraw;
    window.comparisons.draw=compareCpuParticleEndpoints(window.reference.endpoints.draw,correctedDraw,
      {preLeft:refPre,preRight:predPre});
    const a=window.reference.nativeEvidence.drawnPair,b=window.predicted.nativeEvidence.drawnPair;
    for(const lane of ['Observation','Pcm','Pad','RngProfile','Transfer'])
      if(!equal(decode(a[`baseline${lane}`]),decode(b[`replay${lane}`])))
        throw new Error(`Window ${index+1} final drawn raw ${lane} differs`);
    window.comparisons.drawRaw={equal:true,lanes:['Observation','Pcm','Pad','RngProfile','Transfer']};
    const releaseReference=await runCommand(referencePage,53);
    const releasePredicted=await runCommand(predictedPage,53);
    const retiredExpected={captured,retired:index+1,active:0,sample:startTick,preFrame,postFrame:preFrame+7};
    requireReuse(releaseReference.reuse,retiredExpected,`window${index+1} reference retirement`);
    requireReuse(releasePredicted.reuse,retiredExpected,`window${index+1} predicted retirement`);
    if(JSON.stringify(releaseReference.reuse)!==JSON.stringify(releasePredicted.reuse))
      throw new Error(`Window ${index+1} retirement metadata differs across pages`);
    window.retirement={reference:releaseReference.reuse,predicted:releasePredicted.reuse};
    window.status='complete';
    return window;
  };

  try{
    const firstPre=checkpoint.boundary.sourceFrame;
    const first=await runWindow({index:0,startTick:300,preFrame:firstPre,corpus:firstCorpus,enableReuse:true});
    if(first.reference.rows[0].boundary.players[1].motion!==60||
       first.predicted.rows[0].predictedBoundary.players[1].motion===60)
      throw new Error('First S4 window did not retain the actual-versus-predicted remote motion divergence');
    let referenceBoundary=first.reference.rows.at(-1).boundary;
    let predictedBoundary=first.predicted.rows.at(-1).predictedBoundary;
    let lastSourceFrame=first.postFrame;
    report.bridge={status:'running',maximumTicks:CPU_CORRECTED_TWO_WINDOW_BRIDGE_LIMIT,
      pad44Base64:CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,ticks:[]};
    let ready=false;
    for(let count=1;count<=CPU_CORRECTED_TWO_WINDOW_BRIDGE_LIMIT;count++){
      const tick=schedule.timeline.status().simulated_through+1;
      const local=Buffer.alloc(PAD_BYTES),remote=Buffer.alloc(PAD_BYTES),neutral=decode(CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44);
      schedule.timeline.recordLocal(tick,local);
      const accepted=schedule.timeline.acceptRemote(tick,remote);
      if(accepted.status!=='accepted')throw new Error(`Neutral bridge remote PAD ${tick} refused`);
      const plan=schedule.timeline.planForward();
      if(plan.kind!=='forward'||plan.tick!==tick||plan.speculative_ticks!==0||!equal(plan.frame,neutral))
        throw new Error(`Neutral bridge timeline did not produce confirmed literal input at tick ${tick}`);
      const [ref,pred]=await Promise.all([
        runBrowserSample(referencePage,tick,mapping,stocks,null,null,
          {pad44Base64:CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,captureRawFiveLanes:true}),
        runBrowserSample(predictedPage,tick,mapping,stocks,null,null,
          {pad44Base64:CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,captureRawFiveLanes:true}),
      ]);
      const bridgeTick={tick,pad44Base64:CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,
        referenceBoundary:ref.boundary,predictedBoundary:pred.boundary,
        rawFive:{reference:ref.rawFiveLanes,predicted:pred.rawFiveLanes},
        drawObserved:{reference:{gpu:ref.status,diagnosticsBase64:ref.drawDiagnostics},
          predicted:{gpu:pred.status,diagnosticsBase64:pred.drawDiagnostics}},timelineAcknowledged:false};
      report.bridge.ticks.push(bridgeTick);
      if(ref.boundary.sourceFrame!==lastSourceFrame+1||pred.boundary.sourceFrame!==lastSourceFrame+1||
         !equal(decode(ref.pad),neutral)||!equal(decode(pred.pad),neutral))
        throw new Error(`Neutral bridge source cursor or literal PAD differs at tick ${tick}`);
      const raw=compareCpuCorrectedRawFive(ref.rawFiveLanes,pred.rawFiveLanes,`Neutral bridge tick ${tick}`);
      bridgeTick.rawFive.comparison=raw;
      const frameFor=page=>{
        const before=gpu.get(page),result=page===referencePage?ref:pred;
        const current=validateAuroraFrame(result.status,before.beginFrameId,before.endFrameId);
        const next=decodeDrawDiagnostics(decode(result.drawDiagnostics));
        const selected=validateSelectedDrawPath(diagnostics.get(page),next,result.boundary.players,mapping);
        gpu.set(page,current);diagnostics.set(page,next);
        return {frame:current,diagnostics:next,selected};
      };
      const refDraw=frameFor(referencePage),predDraw=frameFor(predictedPage);
      // The predicted lane has an extra final-present from correction, so its
      // cumulative GPU frame IDs/counters need not equal the reference lane.
      // Validate each lane's monotonic GPU progress above, then compare the
      // selected source draw rows and their per-sample deltas exactly.
      const selectedDrawComparison=JSON.stringify(refDraw.selected)===JSON.stringify(predDraw.selected);
      if(!selectedDrawComparison)
        throw new Error(`Neutral bridge selected draw path differs at tick ${tick}`);
      schedule.pendingPlan=plan;
      acknowledgeCpuCorrectedWindowForward(schedule,plan);
      referenceBoundary=ref.boundary;predictedBoundary=pred.boundary;lastSourceFrame=ref.boundary.sourceFrame;
      bridgeTick.sourceFrame=lastSourceFrame;
      bridgeTick.draw={
        reference:{gpu:refDraw.frame,diagnostics:refDraw.diagnostics,selected:refDraw.selected},
        predicted:{gpu:predDraw.frame,diagnostics:predDraw.diagnostics,selected:predDraw.selected},
        selectedRowsEqual:selectedDrawComparison};
      bridgeTick.timelineAcknowledged=true;
      bridgeTick.status='complete';
      ready=requireWaitReady(referenceBoundary)&&requireWaitReady(predictedBoundary);
      if(ready){report.bridge.status='ready';break;}
    }
    if(!ready)throw new Error(`Neutral bridge failed Wait14/ready/four-stock predicate within ${CPU_CORRECTED_TWO_WINDOW_BRIDGE_LIMIT} ticks`);
    const timelineBeforeSecond=schedule.timeline.status();
    const startTick=timelineBeforeSecond.simulated_through+1;
    const expectedSample=300+(lastSourceFrame-firstPre);
    if(timelineBeforeSecond.confirmed_through!==startTick-1||
       timelineBeforeSecond.remote_contiguous_through!==startTick-1||
       timelineBeforeSecond.dirty_from!==null||timelineBeforeSecond.plan_outstanding||
       startTick!==307+report.bridge.ticks.length||startTick!==expectedSample)
      throw new Error(`Second corrected window is not contiguous with the persistent timeline: ${JSON.stringify({startTick,lastSourceFrame,expectedSample,timelineBeforeSecond})}`);
    const second=await runWindow({index:1,startTick,preFrame:lastSourceFrame,corpus:secondCorpus,enableReuse:false});
    if(second.reference.rows[0].boundary.players[1].motion!==343||
       second.predicted.rows[0].predictedBoundary.players[1].motion!==14)
      throw new Error('Second B window did not show actual motion343 against predicted Wait14 on its first tick');
    report.timelineAfterSecond=schedule.timeline.status();
    report.reuse={windowsCaptured:second.retirement.reference.windowsCaptured,
      windowsRetired:second.retirement.reference.windowsRetired,
      activeCapsules:second.retirement.reference.activeCapsules,
      maxActiveCapsules:second.retirement.reference.maxActiveCapsules,
      sameMatchPages:2,matchClosed:false};
    report.status='passed';
    return report;
  }catch(error){
    report.status='failed';report.failure=String(error);report.timelineFailure=schedule.timeline.status();
    for(const window of report.windows)if(window.status==='running')window.status='failed';
    for(const [name,page] of [['reference',referencePage],['predicted',predictedPage]]){
      try{report[name].partial=await page.evaluate(()=>window.__slippiSynctest?.cpuCorrectedProgress??null);}
      catch(readError){report[name].partialReadError=String(readError);}
    }
    error.cpuCorrectedTwoWindowPartial=report;throw error;
  }
}
